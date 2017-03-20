/**************************************************************************************************
  Filename:       DemoRouter.c

  Description:    Router application for the sensor demo utilizing the Simple API.

                  The router application binds to a gateway and will periodically
                  read temperature and supply voltage from the ADC and send report
                  towards the gateway node.


  Copyright 2009 Texas Instruments Incorporated. All rights reserved.

  IMPORTANT: Your use of this Software is limited to those specific rights
  granted under the terms of a software license agreement between the user
  who downloaded the software, his/her employer (which must be your employer)
  and Texas Instruments Incorporated (the "License").  You may not use this
  Software unless you agree to abide by the terms of the License. The License
  limits your use, and you acknowledge, that the Software may not be modified,
  copied or distributed unless embedded on a Texas Instruments microcontroller
  or used solely and exclusively in conjunction with a Texas Instruments radio
  frequency transceiver, which is integrated into your product.  Other than for
  the foregoing purpose, you may not use, reproduce, copy, prepare derivative
  works of, modify, distribute, perform, display or sell this Software and/or
  its documentation for any purpose.

  YOU FURTHER ACKNOWLEDGE AND AGREE THAT THE SOFTWARE AND DOCUMENTATION ARE
  PROVIDED �AS IS� WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED,
  INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE,
  NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
  TEXAS INSTRUMENTS OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT,
  NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER
  LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
  INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE
  OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT
  OF SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
  (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.

  Should you have any questions regarding your right to use this Software,
  contact Texas Instruments Incorporated at www.TI.com.
**************************************************************************************************/

/******************************************************************************
 * INCLUDES
 */
#include "sapi.h"
#include "hal_key.h"
#include "hal_led.h"
#include "hal_adc.h"
#include "hal_uart.h"
#include "DemoApp.h"

/******************************************************************************
 * CONSTANTS
 */
#define REPORT_FAILURE_LIMIT                3
#define ACK_REQ_INTERVAL                    5    // each 5th packet is sent with ACK request

// Application osal event identifiers
// Bit mask of events ( from 0x0000 to 0x00FF )
#define MY_START_EVT                        0x0001
#define MY_REPORT_EVT                       0x0002
#define MY_FIND_COLLECTOR_EVT               0x0004

// ADC definitions for CC2430/CC2530 from the hal_adc.c file
#if defined (HAL_MCU_CC2530)
#define HAL_ADC_REF_125V    0x00    /* Internal 1.25V Reference */
#define HAL_ADC_DEC_064     0x00    /* Decimate by 64 : 8-bit resolution */
#define HAL_ADC_DEC_128     0x10    /* Decimate by 128 : 10-bit resolution */
#define HAL_ADC_DEC_512     0x30    /* Decimate by 512 : 14-bit resolution */
#define HAL_ADC_CHN_VDD3    0x0f    /* Input channel: VDD/3 */
#define HAL_ADC_CHN_TEMP    0x0e    /* Temperature sensor */
#endif // HAL_MCU_CC2530

/******************************************************************************
 * LOCAL VARIABLES
 */
static uint8 appState =           APP_INIT;
static uint8 reportState =        FALSE;

static uint8 reportFailureNr =    0;
static uint8 bindRetries =        0;

static uint16 myReportPeriod =    5000;         // milliseconds
static uint16 myBindRetryDelay =  2000;         // milliseconds
static uint8 myStartRetryDelay =  10;           // milliseconds

static uint16 parentShortAddr;

/******************************************************************************
 * GLOBAL VARIABLES
 */
// Inputs and Outputs for Sensor device
#define NUM_OUT_CMD_SENSOR        1
#define NUM_IN_CMD_SENSOR         1

// List of output and input commands for Sensor device
const cId_t zb_OutCmdList[NUM_OUT_CMD_SENSOR] =
{
  KEYLOCK_CMD_ID
};

const cId_t zb_InCmdList[NUM_IN_CMD_SENSOR] =
{
  DOOR_STATUS_CMD_ID
};

// Define SimpleDescriptor for Sensor device
const SimpleDescriptionFormat_t zb_SimpleDesc =
{
  MY_ENDPOINT_ID,             //  Endpoint
  MY_PROFILE_ID,              //  Profile ID
  DEV_ID_SENSOR,              //  Device ID
  DEVICE_VERSION_SENSOR,      //  Device Version
  0,                          //  Reserved
  NUM_IN_CMD_SENSOR,          //  Number of Input Commands
  (cId_t *) zb_InCmdList,              //  Input Command List
  NUM_OUT_CMD_SENSOR,         //  Number of Output Commands
  (cId_t *) zb_OutCmdList     //  Output Command List
};

/******************************************************************************
 * LOCAL FUNCTIONS
 */
void uartRxCB( uint8 port, uint8 event );

static void openDoorOnCoordinator(bool openDoor); 
static void setDoorStatusLed(bool doorIsOpened );


static bool doorIsOpen = false;

/******************************************************************************
 * GLOBAL FUNCTIONS
 */

/*****************************************************************************
 * @fn          zb_HandleOsalEvent
 *
 * @brief       The zb_HandleOsalEvent function is called by the operating
 *              system when a task event is set
 *
 * @param       event - Bitmask containing the events that have been set
 *
 * @return      none
 */
void zb_HandleOsalEvent( uint16 event )
{
  if( event & SYS_EVENT_MSG )
  {
  }

  if( event & ZB_ENTRY_EVENT )
  {
    // blind LED 1 to indicate joining a network
    HalLedBlink ( HAL_LED_1, 0, 50, 500 );

    // Start the device
    appState = APP_START;
    zb_StartRequest();
    
     MCU_IO_DIR_OUTPUT(1,2); //set output led
  }

  if ( event & MY_START_EVT )
  {
    zb_StartRequest();
  }

  if ( event & MY_REPORT_EVT )
  {
    if ( appState == APP_RUN )
    {
      //sendReport();
      //not sending any reports.
    }
  }

  if ( event & MY_FIND_COLLECTOR_EVT )
  {
    // blind LED 2 to indicate discovery and binding
    HalLedBlink ( HAL_LED_2, 0, 50, 500 );

    // Find and bind to a collector device
    appState = APP_BIND;
    zb_BindDevice( TRUE, KEYLOCK_CMD_ID, (uint8 *)NULL );
  }
}

/******************************************************************************
 * @fn      zb_HandleKeys
 *
 * @brief   Handles all key events for this device.
 *
 * @param   shift - true if in shift/alt.
 * @param   keys - bit field for key events. Valid entries:
 *                 EVAL_SW4
 *                 EVAL_SW3
 *                 EVAL_SW2
 *                 EVAL_SW1
 *
 * @return  none
 */
void zb_HandleKeys( uint8 shift, uint8 keys )
{
  // Shift is used to make each button/switch dual purpose.
  if ( shift )
  {
    if ( keys & HAL_KEY_SW_1 )
    {
    }
    if ( keys & HAL_KEY_SW_2 )
    {
    }
    if ( keys & HAL_KEY_SW_3 )
    {
    }
    if ( keys & HAL_KEY_SW_4 )
    {
    }
  }
  else
  {
    if ( keys & HAL_KEY_SW_1 )
    {
      // Start reporting
      if ( reportState == FALSE ) {
        osal_start_reload_timer( sapi_TaskID, MY_REPORT_EVT, myReportPeriod );
        reportState = TRUE;

        // switch LED 2 on to indicate reporting
        HalLedSet( HAL_LED_2, HAL_LED_MODE_ON );
      }
    }
    if ( keys & HAL_KEY_SW_2 )
    {
      doorIsOpen = !doorIsOpen;
      openDoorOnCoordinator(doorIsOpen);
    }
    if ( keys & HAL_KEY_SW_3 )
    {
    }
    if ( keys & HAL_KEY_SW_4 )
    {
    }
  }
}

//true to open the door, false to close it.
void openDoorOnCoordinator(bool openDoor) {
  uint8 pData[1];
  pData[0]= (uint8)openDoor;
  zb_SendDataRequest( ZB_BINDING_ADDR, KEYLOCK_CMD_ID, 1, pData, 0, AF_MSG_ACK_REQUEST, 0 );
}


void setDoorStatusLed(bool doorIsOpened ){
  if(doorIsOpened){
    MCU_IO_SET_HIGH(1,2);
  }
  else{
    MCU_IO_SET_LOW(1,2);
  }
  
}

/******************************************************************************
 * @fn          zb_StartConfirm
 *
 * @brief       The zb_StartConfirm callback is called by the ZigBee stack
 *              after a start request operation completes
 *
 * @param       status - The status of the start operation.  Status of
 *                       ZB_SUCCESS indicates the start operation completed
 *                       successfully.  Else the status is an error code.
 *
 * @return      none
 */
void zb_StartConfirm( uint8 status )
{
  // If the device sucessfully started, change state to running
  if ( status == ZB_SUCCESS )
  {
    // Set LED 1 to indicate that node is operational on the network
    HalLedSet( HAL_LED_1, HAL_LED_MODE_ON );

    // Store parent short address
    zb_GetDeviceInfo(ZB_INFO_PARENT_SHORT_ADDR, &parentShortAddr);

    // Set event to bind to a collector
    osal_set_event( sapi_TaskID, MY_FIND_COLLECTOR_EVT );

    // Turn ON Allow Bind mode infinitly
    zb_AllowBind( 0xFF );
    HalLedSet( HAL_LED_2, HAL_LED_MODE_OFF );
  }
  else
  {
    // Try again later with a delay
    osal_start_timerEx( sapi_TaskID, MY_START_EVT, myStartRetryDelay );
  }
}

/******************************************************************************
 * @fn          zb_BindConfirm
 *
 * @brief       The zb_BindConfirm callback is called by the ZigBee stack
 *              after a bind operation completes.
 *
 * @param       commandId - The command ID of the binding being confirmed.
 *              status - The status of the bind operation.
 *
 * @return      none
 */
void zb_BindConfirm( uint16 commandId, uint8 status )
{
  if( status == ZB_SUCCESS )
  {
    appState = APP_RUN;
    HalLedSet( HAL_LED_2, HAL_LED_MODE_OFF );

    // After failure reporting start automatically when the device
    // is bound to a new gateway
    if ( reportState )
    {
      // Start reporting
      osal_start_reload_timer( sapi_TaskID, MY_REPORT_EVT, myReportPeriod );

      // switch LED 2 on to indicate reporting
      HalLedSet( HAL_LED_2, HAL_LED_MODE_ON );
    }
  }
  else
  {
    if ( ++bindRetries >= 2 ) {
      // Reset the system
      zb_SystemReset();
    }
    else
    {
      osal_start_timerEx( sapi_TaskID, MY_FIND_COLLECTOR_EVT, myBindRetryDelay );
    }
  }
}

/******************************************************************************
 * @fn          zb_SendDataConfirm
 *
 * @brief       The zb_SendDataConfirm callback function is called by the
 *              ZigBee after a send data operation completes
 *
 * @param       handle - The handle identifying the data transmission.
 *              status - The status of the operation.
 *
 * @return      none
 */
void zb_SendDataConfirm( uint8 handle, uint8 status )
{
  if( status != ZB_SUCCESS )
  {
    if ( ++reportFailureNr >= REPORT_FAILURE_LIMIT )
    {
       // Stop reporting
       osal_stop_timerEx( sapi_TaskID, MY_REPORT_EVT );

       // After failure start reporting automatically when the device
       // is binded to a new gateway
       reportState = TRUE;

      // Try to bind a new gateway
       osal_start_timerEx( sapi_TaskID, MY_FIND_COLLECTOR_EVT, myBindRetryDelay );
       reportFailureNr = 0;
    }
  }
  // status == SUCCESS
  else
  {
    // Reset failure counter
    reportFailureNr = 0;
  }
}

/******************************************************************************
 * @fn          zb_AllowBindConfirm
 *
 * @brief       Indicates when another device attempted to bind to this device
 *
 * @param
 *
 * @return      none
 */
void zb_AllowBindConfirm( uint16 source )
{
  //better to check if the coordinator is the source before disallowing Allow Bind.
  zb_AllowBind( 0x00 );
}

/******************************************************************************
 * @fn          zb_FindDeviceConfirm
 *
 * @brief       The zb_FindDeviceConfirm callback function is called by the
 *              ZigBee stack when a find device operation completes.
 *
 * @param       searchType - The type of search that was performed.
 *              searchKey - Value that the search was executed on.
 *              result - The result of the search.
 *
 * @return      none
 */
void zb_FindDeviceConfirm( uint8 searchType, uint8 *searchKey, uint8 *result )
{
  (void)searchType;
  (void)searchKey;
  (void)result;
}

/******************************************************************************
 * @fn          zb_ReceiveDataIndication
 *
 * @brief       The zb_ReceiveDataIndication callback function is called
 *              asynchronously by the ZigBee stack to notify the application
 *              when data is received from a peer device.
 *
 * @param       source - The short address of the peer device that sent the data
 *              command - The commandId associated with the data
 *              len - The number of bytes in the pData parameter
 *              pData - The data sent by the peer device
 *
 * @return      none
 */
void zb_ReceiveDataIndication( uint16 source, uint16 command, uint16 len, uint8 *pData  )
{
  if(command == KEYLOCK_CMD_ID){
    if(pData[0]){
     setDoorStatusLed(false); 
    }
    else{
      setDoorStatusLed(true); 
    }
  }
}

/******************************************************************************
 * @fn          uartRxCB
 *
 * @brief       Callback function for UART
 *
 * @param       port - UART port
 *              event - UART event that caused callback
 *
 * @return      none
 */
void uartRxCB( uint8 port, uint8 event )
{
  (void)port;
  (void)event;
}

