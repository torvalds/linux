/*
 * Copyright (c) 2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef INCLUDED_nvec_H
#define INCLUDED_nvec_H


#if defined(__cplusplus)
extern "C"
{
#endif


/**
 * @file nvec.h
 * @brief <b> Nv Embedded Controller (EC) Interface.</b>
 *
 * @b Description: This file declares the interface for communicating with
 *    an Embedded Controller (EC).
 *
 * Usage:
 *
 * The EC Interface (ECI) handles communication of packets the AP and EC.
 *
 * Multiple AP clients are allowed to communicate with the EC concurrently.
 * Each client opens its own channel by invoking NvEcOpen(), where the
 * InstanceId parameter specifies which EC to communicate with.  Typically,
 * only a single EC instance will be present.
 *
 * Three types of packets are supported --
 *
 * * Request Packets -- sent from AP to EC
 * * Response Packets -- sent from EC to AP
 * * Event Packets -- sent from EC to AP
 *
 * There is a one-to-one correspondence between Request Packets and Response
 * Packets.  For every Request Packet sent from the AP to the EC, there will be
 * one and only one corresponding Response Packet sent from the EC back to the
 * AP.
 *
 * Event Packets, on the other hand, are unsolicited and can be sent by the 
 * EC at any time.
 *
 * See below for detailed information about the format and content of the
 * packet types.
 * 
 * Since Requests and Responses are always paired, the ECI treats the process of
 * sending a Request and waiting for the corresponding Response as a single
 * operation.  The NvEcSendRequest() routine carries out this operation.
 * Normally the routine will block until after the Request is sent and the
 * Response received; however, certain error conditions can cause the routine to
 * return early, e.g., request transmit errors, response timeout errors, etc.
 *
 * Event packets are treated differently than Requests and Responses since they
 * can occur asynchronously with respect to other AP activities.  In a sense,
 * Events are similar to interrupts.  Several types of events are supported,
 * e.g., keyboard events, ps/2 device events, gpio events, etc.  The client
 * wishing to receive Event Packets must first register for the desired event
 * types by invoking the NvEcRegisterForEvents() routine.  The client also
 * provides a semaphore when registering.  The ECI will signal the semaphore
 * when an event of the specified type arrives.
 *
 * Next, the client blocks on the semaphore, waiting for an event to arrive.
 * When an event arrives, the ECI will signal the semaphore and hold the Event
 * Packet until it is retrieved by the client.  Since the ECI will have signaled
 * the semaphore, the client will become unblocked and can retrieve the pending
 * Event Packet using the NvEcGetEvent() routine.  If the client fails to
 * retrieve the event, any event buffering capability within the ECI will
 * eventually become exhausted and the ECI will be forced to stall the
 * communications channel between the AP and EC, thereby impacting all of the
 * the ECI clients in the system.  Finally, the client can call
 * NvEcUnregisterForEvents() to unregister when it no longer wishes to receive
 * events.  Note that events are discarded if no client is registered to receive
 * them.
 *
 * Generally, packets will be truncated to fit within the bounds of client-
 * supplied buffers and no error will be reported; however, if the client buffer
 * is too small to hold even a minimum-size packet (i.e., a packet with no
 * payload) then an error will be reported and the buffer contents will be
 * undefined.
 *
 */

#include "nvcommon.h"
#include "nvos.h"

/**
 * A type-safe handle for EC
 */

typedef struct NvEcRec *NvEcHandle;

/**
 * A type-safe handle for EC Event Registration
 */

typedef struct NvEcEventRegistrationRec *NvEcEventRegistrationHandle;

/**
 * Packet definitions
 *
 * Defines format of request, response, and event packets sent between the AP
 * and the EC.
 *
 * Note that the first element of any packet is the packet type, so given any
 * unknown packet it is possible to determine its type (request, response, or
 * event).  From there, the remainder of the packet can be decoded using the
 * structure definition -- NvEcRequest, NvEcResponse, or NvEcEvent.
 *
 * For example, a keyboard request would have a packet type of Request/Response
 * and a request/response type of Keyboard.  The response to a keyboard request
 * would have a packet type of Response and a request/response type of Keyboard.
 * Finally, a keyboard event would have a packet type of Event and an event type
 * of Keyboard.
 *
 * Request operations are specified as a combination of a request type and a
 * request sub-type.  Since every request has a corresponding response, requests
 * and responses have a common set of types and sub-types.
 *
 * There is a separate set of types for event packets, and events do not have a
 * sub-type.
 *
 * Note that these are the packet formats as presented to clients of the NvEc
 * API.  Actual format of data communicated between AP and EC may differ at the
 * transport level.
 */ 
#define NVEC_MAX_PAYLOAD_BYTES (30)

/**
 * Packet types
 */

typedef enum
{
    NvEcPacketType_Request,
    NvEcPacketType_Response,
    NvEcPacketType_Event,
    NvEcPacketType_Num,
    NvEcPacketType_Force32 = 0x7FFFFFFF
} NvEcPacketType;

/**
 * Request/response types
 *
 * Each request has a corresponding response, so they share a common set of types.
 */

typedef enum
{
    NvEcRequestResponseType_System = 1,
    NvEcRequestResponseType_Battery,
    NvEcRequestResponseType_Gpio,
    NvEcRequestResponseType_Sleep,
    NvEcRequestResponseType_Keyboard,
    NvEcRequestResponseType_AuxDevice,
    NvEcRequestResponseType_Control,
    NvEcRequestResponseType_OEM0 = 0xd,
    NvEcRequestResponseType_OEM1,
    NvEcRequestResponseType_Num,
    NvEcRequestResponseType_Force32 = 0x7FFFFFFF
} NvEcRequestResponseType;

/**
 * Request/response sub-types
 *
 * Each request has a corresponding response, so they share a common set of
 * sub-types.
 */

typedef enum
{
    NvEcRequestResponseSubtype_None,
    NvEcRequestResponseSubtype_Num,
    NvEcRequestResponseSubtype_Force32 = 0x7FFFFFFF
} NvEcRequestResponseSubtype;

/**
 * Event types
 */

typedef enum
{
    NvEcEventType_Keyboard,
    NvEcEventType_AuxDevice0,
    NvEcEventType_AuxDevice1,
    NvEcEventType_AuxDevice2,
    NvEcEventType_AuxDevice3,
    NvEcEventType_System,
    NvEcEventType_GpioScalar,
    NvEcEventType_GpioVector,
    NvEcEventType_Battery,
    NvEcEventType_OEM0 = 0xd,
    NvEcEventType_OEM1,
    NvEcEventType_Num,
    NvEcEventType_Force32 = 0x7FFFFFFF
} NvEcEventType;

/**
 * Supported status codes
 */

typedef enum
{
    NvEcStatus_Success,
    NvEcStatus_TimeOut,
    NvEcStatus_Parity,
    NvEcStatus_Unavailable,
    NvEcStatus_InvalidCommand,
    NvEcStatus_InvalidSize,
    NvEcStatus_InvalidParameter,
    NvEcStatus_UnsupportedConfiguration,
    NvEcStatus_ChecksumFailure,
    NvEcStatus_WriteFailure,
    NvEcStatus_ReadFailure,
    NvEcStatus_Overflow,
    NvEcStatus_Underflow,
    NvEcStatus_InvalidState,
    NvEcStatus_OEM0 = 0xd0,
    NvEcStatus_OEM1,
    NvEcStatus_OEM2,
    NvEcStatus_OEM3,
    NvEcStatus_OEM4,
    NvEcStatus_OEM5,
    NvEcStatus_OEM6,
    NvEcStatus_OEM7,
    NvEcStatus_OEM8,
    NvEcStatus_OEM9,
    NvEcStatus_OEM10,
    NvEcStatus_OEM11,
    NvEcStatus_OEM12,
    NvEcStatus_OEM13,
    NvEcStatus_OEM14,
    NvEcStatus_OEM15,
    NvEcStatus_OEM16,
    NvEcStatus_OEM17,
    NvEcStatus_OEM18,
    NvEcStatus_OEM19,
    NvEcStatus_OEM20,
    NvEcStatus_OEM21,
    NvEcStatus_OEM22,
    NvEcStatus_OEM23,
    NvEcStatus_OEM24,
    NvEcStatus_OEM25,
    NvEcStatus_OEM26,
    NvEcStatus_OEM27,
    NvEcStatus_OEM28,
    NvEcStatus_OEM29,
    NvEcStatus_OEM30,
    NvEcStatus_OEM31,
    NvEcStatus_UnspecifiedError = 0xff,
    NvEcStatus_Num,
    NvEcStatus_Force32 = 0x7FFFFFFF
} NvEcStatus;

/**
 * EC Request Packet
 */

typedef struct NvEcRequestRec
{
    NvEcPacketType PacketType;
    NvEcRequestResponseType RequestType;
    NvEcRequestResponseSubtype RequestSubtype;
    NvU32 RequestorTag;
    NvU32 NumPayloadBytes;
    NvU8 Payload[30];
} NvEcRequest;

#define NVEC_MIN_REQUEST_SIZE (offsetof(struct NvEcRequestRec, Payload[0]))

/**
 * EC Response Packet
 */

typedef struct NvEcResponseRec
{
    NvEcPacketType PacketType;
    NvEcRequestResponseType ResponseType;
    NvEcRequestResponseSubtype ResponseSubtype;
    NvU32 RequestorTag;
    NvEcStatus Status;
    NvU32 NumPayloadBytes;
    NvU8 Payload[30];
} NvEcResponse;

#define NVEC_MIN_RESPONSE_SIZE (offsetof(struct NvEcResponseRec, Payload[0]))

/**
 * EC Event Packet
 */

typedef struct NvEcEventRec
{
    NvEcPacketType PacketType;
    NvEcEventType EventType;
    NvEcStatus Status;
    NvU32 NumPayloadBytes;
    NvU8 Payload[30];
} NvEcEvent;

#define NVEC_MIN_EVENT_SIZE (offsetof(struct NvEcEventRec, Payload[0]))

/**
 * EC power states
 */

typedef enum
{
    NvEcPowerState_PowerDown,
    NvEcPowerState_Suspend,
    NvEcPowerState_Restart,
    NvEcPowerState_Num,
    NvEcPowerState_Force32 = 0x7FFFFFFF
} NvEcPowerState;

/**
 * Initialize and open a channel to the Embedded Controller (EC). This routine
 * allocates the handle for the EC channel and returns it to the caller.
 *
 * @param phEc pointer to location where EC channel handles is to be stored
 * @param InstanceId instance of EC to which a channel is to be opened
 *
 * @retval NvSuccess Channel has been successfully opened.
 * @retval NvError_InsufficientMemory Routine was unable to allocate memory.
 * @retval NvError_AlreadyAllocated Maximum number of channels have already
 *         been opened
 * @retval NvError_NotSupported InstanceId is invalid
 */

 NvError NvEcOpen( 
    NvEcHandle * phEc,
    NvU32 InstanceId );

/**
 * Closes and de-initializes a channel to the Embedded Controller (EC).  Also,
 * frees memory allocated for the handle.
 *
 * @param hEc handle for EC channel
 *
 * @retval none
 */

 void NvEcClose( 
    NvEcHandle hEc );

/**
 * Send a request to the EC.  Then wait for the EC's response and return it to
 * the caller.  This routine blocks until the EC's response is received.  The
 * response is only valid if NvSuccess is returned.
 *
 * The request or response can fail due to time-out errors or transmission
 * errors.
 *
 * If the EC sends a larger response packet than will fit in the provided
 * buffer, the response will be truncated to fit within the available buffer and
 * no error will be returned.
 *
 * @param hEc handle for EC channel
 * @param pRequest pointer to buffer containing EC request
 * @param pResponse pointer to buffer where EC response is to be stored
 * @param RequestSize length of EC request buffer, in bytes
 * @param ResponseSize length of EC response buffer, in bytes
 *
 * @retval NvSuccess Request was successfully sent to EC and a corresponding
 *         response was successfully received from the EC
 * @retval NvError_TimeOut EC failed to respond within required time interval
 * @retval NvError_InvalidSize Request or Response buffer is too small to hold
 *         minimum-size packet
 * @retval NvError_BadSize Request or response size is incorrect
 * @retval NvError_I2cWriteFailed Transmission error while sending request
 * @retval NvError_I2cReadFailed Transmission error while receiving request
 */

 NvError NvEcSendRequest( 
    NvEcHandle hEc,
    NvEcRequest * pRequest,
    NvEcResponse * pResponse,
    NvU32 RequestSize,
    NvU32 ResponseSize );

/**
 * Register the caller to receive certain types of events from the EC.
 *
 * The caller provides a list of the types of events to be received.
 * Registering for an event type guarantees that all event packets of the
 * specified type are delivered to the caller.  If the caller fails to retrieve
 * the event packet (via NvEcGetEvent), then EC communications will generally be
 * stalled until such time as a buffer is provided.  Thus, an ill-behaved client
 * can impact systemwide performance.
 *
 * To avoid stalling EC communications, the caller can also provide a hint as to
 * the amount of buffering that may be needed to account for any expected
 * "burstiness" in the arrival of events.  This allows stalling to be delayed
 * until all buffers have been exhaused.  Both the number of buffers and the
 * size of each buffer is specified.  Event Packets larger than the specified
 * buffer size will be truncated to fit and no error will be reported.
 *
 * Finally, the caller provides a semaphore, which is to be signaled when an
 * event (of the specified type) arrives.  The caller can then wait on the
 * semaphore, so as to block until an event occurs.  The semaphone must be
 * initialized to zero before being passed to this routine.
 *
 * @param hEc handle for EC channel
 * @param phEcEventRegistration pointer to location where EC Event Registration
 *        handle is to be stored
 * @param hSema handle for semaphore used to notify caller that an event has
 *        arrived.  Semaphore must be initialized to zero.
 * @param NumEventTypes number of entries in pEventTypes array
 * @param pEventTypes pointer to an array of EC event types to be reported to
 *        the caller
 * @param NumEventPackets number of event packets to buffer (hint)
 * @param EventPacketSize size of each event packet buffer (hint), in bytes
 *
 * @retval NvSuccess Registration for events was successful
 * @retval NvError_InsufficientMemory Routine was unable to allocate memory.
 * @retval NvError_BadParameter Invalid event type specified
 * @retval NvError_AlreadyAllocated Client has already registered for the 
 *         specified event type
 * @retval NvError_InvalidSize Buffer is too small to hold minimum-size Event
 *         Packet
 */

 NvError NvEcRegisterForEvents( 
    NvEcHandle hEc,
    NvEcEventRegistrationHandle * phEcEventRegistration,
    NvOsSemaphoreHandle hSema,
    NvU32 NumEventTypes,
    NvEcEventType * pEventTypes,
    NvU32 NumEventPackets,
    NvU32 EventPacketSize );

/**
 * Retrieve pending Event Packet by copying contents into user-supplied buffer.
 *
 * If the user-supplied buffer is too small to hold the full payload of the
 * Event Packet, then the payload will be truncated and no error will be
 * returned.
 *
 * @param hEcEventRegistration EC Event Registration handle
 * @param pEvent pointer to buffer where EC event is to be stored
 * @param EventSize length of EC event buffer, in bytes
 * 
 * @retval NvSuccess Event Packet retrieved successfully
 * @retval NvError_InvalidSize Buffer is too small to hold minimum-size Event
 *         Packet
 * @retval NvError_BadParameter Invalid handle
 * @retval NvError_InvalidAddress Null buffer pointer
 * @retval NvError_InvalidState No Event Packets available
 */

 NvError NvEcGetEvent( 
    NvEcEventRegistrationHandle hEcEventRegistration,
    NvEcEvent * pEvent,
    NvU32 EventSize );

/**
 * Unregister the caller so that events previously registered for will no longer
 * be received.
 *
 * @param hEcEventRegistration EC Event Registration handle
 *
 * @retval NvSuccess Caller successfully unregistered from specified events
 * @retval NvError_BadParameter Invalid handle
 */

 NvError NvEcUnregisterForEvents( 
    NvEcEventRegistrationHandle hEcEventRegistration );

/**
 * Configure driver and EC for new power state (suspend, powerdown, or restart)
 *
 * @param PowerState desired new power state
 *
 * @retval NvSuccess .
 */

 NvError NvEcPowerSuspend( 
    NvEcPowerState PowerState );

/**
 * Power Resume.
 *
 * @param none
 *
 * @retval NvSuccess .
 */

 NvError NvEcPowerResume( 
    void  );

/*******************************************************************************
 *
 * Request/Response details
 *
 */

 /**
  * Variable-length strings
  *
  * Variable-length strings in Response Packets may not be null-terminated.
  * Maximum length for variable-length strings is defined below.
  */

#define NVEC_MAX_RESPONSE_STRING_SIZE  30

/**
 * Byte ordering
 * 
 * Multi-byte integers in the payload section of Request, Response, and Event
 * Packets are treated as byte arrays.  The bytes are stored in little-endian
 * order (least significant byte first, most significant byte last).
 */

/*******************************************************************************
 *
 * System Request/Response details
 *
 */

/**
 * System subtypes
 */

typedef enum
{
    NvEcSystemSubtype_GetStatus,
    NvEcSystemSubtype_ConfigureEventReporting,
    NvEcSystemSubtype_AcknowledgeSystemStatus,
    NvEcSystemSubtype_ConfigureWake = 0xfd,

    NvEcSystemSubtype_Num,
    NvEcSystemSubtype_Max = 0x7fffffff
} NvEcSystemSubtype;

/**
 * System payload data structures
 */

typedef struct NvEcSystemGetStateResponsePayloadRec
{
    NvU8 State[2];     // see NVEC_SYSTEM_STATE* #define's
    NvU8 OemState[2];
} NvEcSystemGetStateResponsePayload;

#define NVEC_SYSTEM_STATE0_0_EC_RESET_RANGE                             4:4      
#define NVEC_SYSTEM_STATE0_0_AP_POWERDOWN_NOW_RANGE                     3:3
#define NVEC_SYSTEM_STATE0_0_AP_SUSPEND_NOW_RANGE                       2:2
#define NVEC_SYSTEM_STATE0_0_AP_RESTART_NOW_RANGE                       1:1

#define NVEC_SYSTEM_STATE1_0_AC_RANGE                                   0:0
#define NVEC_SYSTEM_STATE1_0_AC_NOT_PRESENT                             0x0
#define NVEC_SYSTEM_STATE1_0_AC_PRESENT                                 0x1

typedef struct NvEcSystemConfigureEventReportingRequestPayloadRec
{
    NvU8 ReportEnable;     // see NVEC_SYSTEM_REPORT_ENABLE* #define's
    NvU8 SystemStateMask[2];     // see NVEC_SYSTEM_STATE* #define's
    NvU8 OemStateMask[2];
} NvEcSystemConfigureEventReportingRequestPayload;

#define NVEC_SYSTEM_REPORT_ENABLE_0_ACTION_RANGE                        7:0
#define NVEC_SYSTEM_REPORT_ENABLE_0_ACTION_DISABLE                      0x0
#define NVEC_SYSTEM_REPORT_ENABLE_0_ACTION_ENABLE                       0x1

typedef struct NvEcSystemAcknowledgeSystemStatusRequestPayloadRec
{
    NvU8 SystemStateMask[2];     // see NVEC_SYSTEM_STATE* #define's
    NvU8 OemStateMask[2];
} NvEcSystemAcknowledgeSystemStatusRequestPayload;

typedef struct NvEcSystemConfigureWakeRequestPayloadRec
{
    NvU8 WakeEnable;     // see NVEC_SYSTEM_WAKE_ENABLE* #define's
    NvU8 SystemStateMask[2];     // see NVEC_SYSTEM_STATE* #define's
    NvU8 OemStateMask[2];
} NvEcSystemConfigureWakeRequestPayload;

#define NVEC_SYSTEM_WAKE_ENABLE_0_ACTION_RANGE                          7:0
#define NVEC_SYSTEM_WAKE_ENABLE_0_ACTION_DISABLE                        0x0
#define NVEC_SYSTEM_WAKE_ENABLE_0_ACTION_ENABLE                         0x1


/*******************************************************************************
 *
 * Battery Request/Response details
 *
 */

/**
 * Battery subtypes
 */

typedef enum
{
    NvEcBatterySubtype_GetSlotStatus,
    NvEcBatterySubtype_GetVoltage,
    NvEcBatterySubtype_GetTimeRemaining,
    NvEcBatterySubtype_GetCurrent,
    NvEcBatterySubtype_GetAverageCurrent,
    NvEcBatterySubtype_GetAveragingTimeInterval,
    NvEcBatterySubtype_GetCapacityRemaining,
    NvEcBatterySubtype_GetLastFullChargeCapacity,
    NvEcBatterySubtype_GetDesignCapacity,
    NvEcBatterySubtype_GetCriticalCapacity,
    NvEcBatterySubtype_GetTemperature,
    NvEcBatterySubtype_GetManufacturer,
    NvEcBatterySubtype_GetModel,
    NvEcBatterySubtype_GetType,
    NvEcBatterySubtype_GetRemainingCapacityAlarm,
    NvEcBatterySubtype_SetRemainingCapacityAlarm,
    NvEcBatterySubtype_SetConfiguration,
    NvEcBatterySubtype_GetConfiguration,
    NvEcBatterySubtype_ConfigureEventReporting,
    NvEcBatterySubtype_ConfigureWake = 0x1d,

    NvEcBatterySubtype_Num,
    NvEcBatterySubtype_Max = 0x7fffffff
} NvEcBatterySubtype;

#define NVEC_SUBTYPE_0_BATTERY_SLOT_RANGE                               7:4
#define NVEC_SUBTYPE_0_BATTERY_INFO_RANGE                               3:0

/**
 * Battery payload data structures
 */

typedef struct NvEcBatteryGetSlotStatusResponsePayloadRec
{
    NvU8 SlotStatus;     // see NVEC_BATTERY_SLOT_STATUS* #define's
    NvU8 CapacityGauge;
} NvEcBatteryGetSlotStatusResponsePayload;

#define NVEC_BATTERY_SLOT_STATUS_0_CRITICAL_CAPACITY_ALARM_RANGE        3:3
#define NVEC_BATTERY_SLOT_STATUS_0_CRITICAL_CAPACITY_ALARM_UNSET        0x0
#define NVEC_BATTERY_SLOT_STATUS_0_CRITICAL_CAPACITY_ALARM_SET          0x1

#define NVEC_BATTERY_SLOT_STATUS_0_CHARGING_STATE_RANGE                 2:1
#define NVEC_BATTERY_SLOT_STATUS_0_CHARGING_STATE_IDLE                  0x0
#define NVEC_BATTERY_SLOT_STATUS_0_CHARGING_STATE_CHARGING              0x1
#define NVEC_BATTERY_SLOT_STATUS_0_CHARGING_STATE_DISCHARGING           0x2

#define NVEC_BATTERY_SLOT_STATUS_0_PRESENT_STATE_RANGE                  0:0
#define NVEC_BATTERY_SLOT_STATUS_0_PRESENT_STATE_NOT_PRESENT            0x0
#define NVEC_BATTERY_SLOT_STATUS_0_PRESENT_STATE_PRESENT                0x1

typedef struct NvEcBatteryGetVoltageResponsePayloadRec
{
    NvU8 PresentVoltage[2];     // 16-bit unsigned value, in mV
} NvEcBatteryGetVoltageResponsePayload;

typedef struct NvEcBatteryGetTimeRemainingResponsePayloadRec
{
    NvU8 TimeRemaining[2];     // 16-bit unsigned value, in minutes
} NvEcBatteryGetTimeRemainingResponsePayload;

typedef struct NvEcBatteryGetCurrentResponsePayloadRec
{
    NvU8 PresentCurrent[2];     // 16-bit signed value, in mA
} NvEcBatteryGetCurrentResponsePayload;

typedef struct NvEcBatteryGetAverageCurrentResponsePayloadRec
{
    NvU8 AverageCurrent[2];     // 16-bit signed value, in mA
} NvEcBatteryGetAverageCurrentResponsePayload;

typedef struct NvEcBatteryGetAveragingTimeIntervalResponsePayloadRec
{
    NvU8 TimeInterval[2];     // 16-bit unsigned value, in msec
} NvEcBatteryGetAveragingTimeIntervalResponsePayload;

typedef struct NvEcBatteryGetCapacityRemainingResponsePayloadRec
{
    NvU8 CapacityRemaining[2];     // 16-bit unsigned value, in mAh or 10mWh
} NvEcBatteryGetCapacityRemainingResponsePayload;

typedef struct NvEcBatteryGetLastFullChargeCapacityResponsePayloadRec
{
    NvU8 LastFullChargeCapacity[2];     // 16-bit unsigned value, in mAh or 10mWh
} NvEcBatteryGetLastFullChargeCapacityResponsePayload;

typedef struct NvEcBatteryGetDesignCapacityResponsePayloadRec
{
    NvU8 DesignCapacity[2];     // 16-bit unsigned value, in mAh or 10mWh
} NvEcBatteryGetDesignCapacityResponsePayload;

typedef struct NvEcBatteryGetCriticalCapacityResponsePayloadRec
{
    NvU8 CriticalCapacity[2];     // 16-bit unsigned value, in mAh or 10mWh
} NvEcBatteryGetCriticalCapacityResponsePayload;

typedef struct NvEcBatteryGetTemperatureResponsePayloadRec
{
    NvU8 Temperature[2];     // 16-bit unsigned value, in 0.1 degrees Kelvin
} NvEcBatteryGetTemperatureResponsePayload;

typedef struct NvEcBatteryGetManufacturerResponsePayloadRec
{
    char Manufacturer[NVEC_MAX_RESPONSE_STRING_SIZE];
} NvEcBatteryGetManufacturerResponsePayload;

typedef struct NvEcBatteryGetModelResponsePayloadRec
{
    char Model[NVEC_MAX_RESPONSE_STRING_SIZE];
} NvEcBatteryGetModelResponsePayload;

typedef struct NvEcBatteryGetTypeResponsePayloadRec
{
    char Type[NVEC_MAX_RESPONSE_STRING_SIZE];
} NvEcBatteryGetTypeResponsePayload;

typedef struct NvEcBatterySetRemainingCapacityAlarmRequestPayloadRec
{
    NvU8 CapacityThreshold[2];     // 16-bit unsigned value, in mAh or 10mWh
} NvEcBatterySetRemainingCapacityAlarmRequestPayload;

typedef struct NvEcBatteryGetRemainingCapacityAlarmResponsePayloadRec
{
    NvU8 CapacityThreshold[2];     // 16-bit unsigned value, in mAh or 10mWh
} NvEcBatteryGetRemainingCapacityAlarmResponsePayload;

typedef struct NvEcBatterySetConfigurationRequestPayloadRec
{
    NvU8 Configuration;     // see NVEC_BATTERY_CONFIGURATION* #define's
} NvEcBatterySetConfigurationRequestPayload;

#define NVEC_BATTERY_CONFIGURATION_0_CAPACITY_UNITS_RANGE               0:0
#define NVEC_BATTERY_CONFIGURATION_0_CAPACITY_UNITS_MAH                 0x0
#define NVEC_BATTERY_CONFIGURATION_0_CAPACITY_UNITS_10MWH               0x1

typedef struct NvEcBatteryGetConfigurationResponsePayloadRec
{
    NvU8 Configuration;     // see NVEC_BATTERY_CONFIGURATION* #define's
} NvEcBatteryGetConfigurationResponsePayload;

typedef struct NvEcBatteryConfigureEventReportingRequestPayloadRec
{
    NvU8 ReportEnable;     // see NVEC_BATTERY_REPORT_ENABLE* #define's
    NvU8 EventTypes;     // see NVEC_BATTERY_EVENT_TYPE* #define's
} NvEcBatteryConfigureEventReportingRequestPayload;

#define NVEC_BATTERY_REPORT_ENABLE_0_ACTION_RANGE                       7:0
#define NVEC_BATTERY_REPORT_ENABLE_0_ACTION_DISABLE                     0x0
#define NVEC_BATTERY_REPORT_ENABLE_0_ACTION_ENABLE                      0x1

#define NVEC_BATTERY_EVENT_TYPE_0_REMAINING_CAPACITY_ALARM_RANGE        2:2
#define NVEC_BATTERY_EVENT_TYPE_0_REMAINING_CAPACITY_ALARM_ENABLE       0x0
#define NVEC_BATTERY_EVENT_TYPE_0_REMAINING_CAPACITY_ALARM_DISABLE      0x1

#define NVEC_BATTERY_EVENT_TYPE_0_CHARGING_STATE_RANGE                  1:1
#define NVEC_BATTERY_EVENT_TYPE_0_CHARGING_STATE_ENABLE                 0x0
#define NVEC_BATTERY_EVENT_TYPE_0_CHARGING_STATE_DISABLE                0x1

#define NVEC_BATTERY_EVENT_TYPE_0_PRESENT_STATE_RANGE                   0:0
#define NVEC_BATTERY_EVENT_TYPE_0_PRESENT_STATE_ENABLE                  0x0
#define NVEC_BATTERY_EVENT_TYPE_0_PRESENT_STATE_DISABLE                 0x1

typedef struct NvEcBatteryConfigureWakeRequestPayloadRec
{
    NvU8 WakeEnable;     // see NVEC_BATTERY_WAKE_ENABLE* #define's
    NvU8 EventTypes;     // see NVEC_BATTERY_EVENT_TYPE* #define's
} NvEcBatteryConfigureWakeRequestPayload;

#define NVEC_BATTERY_WAKE_ENABLE_ACTION_RANGE                           7:0
#define NVEC_BATTERY_WAKE_ENABLE_ACTION_DISABLE                         0x0
#define NVEC_BATTERY_WAKE_ENABLE_ACTION_ENABLE                          0x1

/*******************************************************************************
 *
 * Gpio Request/Response details
 *
 */

/**
 * Gpio subtypes
 */

typedef enum
{
    NvEcGpioSubtype_ConfigurePin,
    NvEcGpioSubtype_SetPinScalar,
    NvEcGpioSubtype_GetPinScalar,
    NvEcGpioSubtype_ConfigureEventReportingScalar,
    NvEcGpioSubtype_AcknowledgeEventReportScalar,

    NvEcGpioSubtype_GetEventReportScalar = 0x6,

    NvEcGpioSubtype_ConfigureWakeScalar = 0x1d,

    NvEcGpioSubtype_SetPinVector = 0x21,
    NvEcGpioSubtype_GetPinVector,
    NvEcGpioSubtype_ConfigureEventReportingVector,
    NvEcGpioSubtype_AcknowledgeEventReportVector,

    NvEcGpioSubtype_GetEventReportVector = 0x26,

    NvEcGpioSubtype_ConfigureWakeVector = 0x3d,

    NvEcGpioSubtype_Num,
    NvEcGpioSubtype_Max = 0x7fffffff
} NvEcGpioSubtype;

/**
 * Gpio payload data structures
 */

typedef struct NvEcGpioConfigurePinRequestPayloadRec
{
    NvU8 Configuration[2];     // see NVEC_GPIO_CONFIGURATION* #define's
    NvU8 LogicalPinNumber;
} NvEcGpioConfigurePinRequestPayload;

#define NVEC_GPIO_CONFIGURATION0_0_MODE_RANGE                           7:5
#define NVEC_GPIO_CONFIGURATION0_0_MODE_INPUT                           0x0
#define NVEC_GPIO_CONFIGURATION0_0_MODE_OUTPUT                          0x1
#define NVEC_GPIO_CONFIGURATION0_0_MODE_TRISTATE                        0x2
#define NVEC_GPIO_CONFIGURATION0_0_MODE_UNUSED                          0x3

#define NVEC_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_RANGE             4:2
#define NVEC_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_NONE              0x0
#define NVEC_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_RISING_EDGE       0x1
#define NVEC_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_FALLING_EDGE      0x2
#define NVEC_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_ANY_EDGE          0x3
#define NVEC_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_LO_LEVEL          0x4
#define NVEC_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_HI_LEVEL          0x5
#define NVEC_GPIO_CONFIGURATION0_0_EVENT_TRIGGER_TYPE_LEVEL_CHANGE      0x6

#define NVEC_GPIO_CONFIGURATION0_0_PULL_RANGE                           1:0
#define NVEC_GPIO_CONFIGURATION0_0_PULL_NONE                            0x0
#define NVEC_GPIO_CONFIGURATION0_0_PULL_DOWN                            0x1
#define NVEC_GPIO_CONFIGURATION0_0_PULL_UP                              0x2

#define NVEC_GPIO_CONFIGURATION0_0_OUTPUT_DRIVE_TYPE_RANGE              7:6
#define NVEC_GPIO_CONFIGURATION0_0_OUTPUT_DRIVE_TYPE_PUSH_PULL          0x0
#define NVEC_GPIO_CONFIGURATION0_0_OUTPUT_DRIVE_TYPE_OPEN_DRAIN         0x1

#define NVEC_GPIO_CONFIGURATION0_0_SCHMITT_TRIGGER_RANGE                5:5
#define NVEC_GPIO_CONFIGURATION0_0_SCHMITT_TRIGGER_DISABLE              0x0
#define NVEC_GPIO_CONFIGURATION0_0_SCHMITT_TRIGGER_ENABLE               0x1

/**
 * GPIO scalar payload data structures
 */

typedef struct NvEcGpioSetPinScalarRequestPayloadRec
{
    NvU8 DriveLevel;     // see NVEC_GPIO_DRIVE_LEVEL* #define's
    NvU8 LogicalPinNumber;
} NvEcGpioSetPinScalarRequestPayload;

#define NVEC_GPIO_DRIVE_LEVEL_0_DRIVE_LEVEL_RANGE                       0:0
#define NVEC_GPIO_DRIVE_LEVEL_0_DRIVE_LEVEL_LOGICAL_LO                  0x0
#define NVEC_GPIO_DRIVE_LEVEL_0_DRIVE_LEVEL_LOGICAL_HI                  0x1

typedef struct NvEcGpioGetPinScalarRequestPayloadRec
{
    NvU8 LogicalPinNumber;
} NvEcGpioGetPinScalarRequestPayload;

typedef struct NvEcGpioGetPinScalarResponsePayloadRec
{
    NvU8 DriveLevel;     // see NVEC_GPIO_DRIVE_LEVEL* #define's
} NvEcGpioGetPinScalarResponsePayload;

typedef struct NvEcGpioConfigureEventReportingScalarRequestPayloadRec
{
    NvU8 ReportEnable;     // 0x0 to disable, 0x1 to enable
    NvU8 LogicalPinNumber;
} NvEcGpioConfigureEventReportingScalarRequestPayload;

typedef struct NvEcGpioAcknowledgeEventReportScalarRequestPayloadRec
{
    NvU8 LogicalPinNumber;
} NvEcGpioAcknowledgeEventReportScalarRequestPayload;

typedef struct NvEcGpioGetEventReportScalarRequestPayloadRec
{
    NvU8 LogicalPinNumber;
} NvEcGpioGetEventReportScalarRequestPayload;

typedef struct NvEcGpioGetEventReportScalarResponsePayloadRec
{
    NvU8 TriggerStatus;     // see NVEC_GPIO_TRIGGER_STATUS* #define's
} NvEcGpioGetEventReportScalarResponsePayload;

#define NVEC_GPIO_TRIGGER_STATUS_0_TRIGGER_STATUS_RANGE                 0:0
#define NVEC_GPIO_TRIGGER_STATUS_0_TRIGGER_STATUS_NO_EVENT_DETECTED     0x0
#define NVEC_GPIO_TRIGGER_STATUS_0_TRIGGER_STATUS_EVENT_DETECTED        0x1

typedef struct NvEcGpioConfigureWakeScalarRequestPayloadRec
{
    NvU8 WakeEnable;     // 0x0 to disable, 0x1 to enable
    NvU8 LogicalPinNumber;
} NvEcGpioConfigureWakeScalarRequestPayload;

/**
 * GPIO vector payload data structures
 */

#define NVEC_GPIO_MAX_BIT_VECTOR_BYTES   24

typedef struct NvEcGpioSetPinVectorRequestPayloadRec
{
    NvU8 DriveLevel;     // see NVEC_GPIO_DRIVE_LEVEL* #define's
    NvU8 PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
} NvEcGpioSetPinVectorRequestPayload;

typedef struct NvEcGpioGetPinVectorRequestPayloadRec
{
    NvU8 PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
} NvEcGpioGetPinVectorRequestPayload;

typedef struct NvEcGpioGetPinVectorResponsePayloadRec
{
    NvU8 DriveLevelBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
} NvEcGpioGetPinVectorResponsePayload;

typedef struct NvEcGpioConfigureEventReportingVectorRequestPayloadRec
{
    NvU8 ReportEnable;     // see NVEC_GPIO_REPORT_ENABLE* #define's
    NvU8 PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
} NvEcGpioConfigureEventReportingVectorRequestPayload;

#define NVEC_GPIO_REPORT_ENABLE_0_ACTION_RANGE                          7:0
#define NVEC_GPIO_REPORT_ENABLE_0_ACTION_DISABLE                        0x0
#define NVEC_GPIO_REPORT_ENABLE_0_ACTION_ENABLE                         0x1

typedef struct NvEcGpioAcknowledgeEventReportVectorRequestPayloadRec
{
    NvU8 PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
} NvEcGpioAcknowledgeEventReportVectorRequestPayload;

typedef struct NvEcGpioGetEventReportVectorRequestPayloadRec
{
    NvU8 PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
} NvEcGpioGetEventReportVectorRequestPayload;

typedef struct NvEcGpioGetEventReportVectorResponsePayloadRec
{
    NvU8 TriggerStatusBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
} NvEcGpioGetEventReportVectorResponsePayload;

typedef struct NvEcGpioConfigureWakeVectorRequestPayloadRec
{
    NvU8 WakeEnable;     // see NVEC_GPIO_WAKE_ENABLE* #define's
    NvU8 PinSelectionBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
} NvEcGpioConfigureWakeVectorRequestPayload;

#define NVEC_GPIO_WAKE_ENABLE_0_ACTION_RANGE                            7:0
#define NVEC_GPIO_WAKE_ENABLE_0_ACTION_DISABLE                          0x0
#define NVEC_GPIO_WAKE_ENABLE_0_ACTION_ENABLE                           0x1

/*******************************************************************************
 *
 * Sleep Request/Response details
 *
 */

/**
 * Sleep subtypes
 */

typedef enum
{
    NvEcSleepSubtype_GlobalConfigureEventReporting,

    NvEcSleepSubtype_ApPowerDown = 0x1,
    NvEcSleepSubtype_ApSuspend = 0x2,
    NvEcSleepSubtype_ApRestart = 0x3,

    NvEcSleepSubtype_Num,
    NvEcSleepSubtype_Max = 0x7fffffff
} NvEcSleepSubtype;

/**
 * Sleep payload data structures
 */

typedef struct NvEcSleepGlobalConfigureEventReportingRequestPayloadRec
{
    NvU8 GlobalReportEnable;     // see NVEC_SLEEP_GLOBAL_REPORT_ENABLE* #define's
} NvEcSleepGlobalConfigureEventReportingRequestPayload;

#define NVEC_SLEEP_GLOBAL_REPORT_ENABLE_0_ACTION_RANGE                  7:0
#define NVEC_SLEEP_GLOBAL_REPORT_ENABLE_0_ACTION_DISABLE                0x0
#define NVEC_SLEEP_GLOBAL_REPORT_ENABLE_0_ACTION_ENABLE                 0x1

/*******************************************************************************
 *
 * Keyboard Request/Response details
 *
 */

/**
 * Keyboard subtypes
 */

typedef enum
{
    NvEcKeyboardSubtype_ConfigureWake = 0x3,
    NvEcKeyboardSubtype_ConfigureWakeKeyReport,

    NvEcKeyboardSubtype_Reset = 0xff,
    NvEcKeyboardSubtype_Enable = 0xf4,
    NvEcKeyboardSubtype_Disable = 0xf5,
    NvEcKeyboardSubtype_SetScanCodeSet = 0xf1,
    NvEcKeyboardSubtype_GetScanCodeSet = 0xf0,
    NvEcKeyboardSubtype_SetLeds = 0xed,

    NvEcKeyboardSubtype_Num,
    NvEcKeyboardSubtype_Max = 0x7fffffff
} NvEcKeyboardSubtype;

/**
 * Keyboard payload data structures
 */

typedef struct NvEcKeyboardConfigureWakeRequestPayloadRec
{
    NvU8 WakeEnable;     // see NVEC_KEYBOARD_WAKE_ENABLE* #define's
    NvU8 EventTypes;     // see NVEC_KEYBOARD_EVENT_TYPE* #define's
} NvEcKeyboardConfigureWakeRequestPayload;

#define NVEC_KEYBOARD_WAKE_ENABLE_0_ACTION_RANGE                        7:0
#define NVEC_KEYBOARD_WAKE_ENABLE_0_ACTION_DISABLE                      0x0
#define NVEC_KEYBOARD_WAKE_ENABLE_0_ACTION_ENABLE                       0x1

#define NVEC_KEYBOARD_EVENT_TYPE_0_SPECIAL_KEY_PRESS_RANGE              1:1
#define NVEC_KEYBOARD_EVENT_TYPE_0_SPECIAL_KEY_PRESS_DISABLE            0x0
#define NVEC_KEYBOARD_EVENT_TYPE_0_SPECIAL_KEY_PRESS_ENABLE             0x1

#define NVEC_KEYBOARD_EVENT_TYPE_0_ANY_KEY_PRESS_RANGE                  0:0
#define NVEC_KEYBOARD_EVENT_TYPE_0_ANY_KEY_PRESS_DISABLE                0x0
#define NVEC_KEYBOARD_EVENT_TYPE_0_ANY_KEY_PRESS_ENABLE                 0x1

typedef struct NvEcKeyboardConfigureWakeKeyReportingRequestPayloadRec
{
    NvU8 ReportWakeKey;     // see NVEC_KEYBOARD_REPORT_WAKE_KEY* #define's
} NvEcKeyboardConfigureWakeKeyReportingRequestPayload;

#define NVEC_KEYBOARD_REPORT_WAKE_KEY_0_ACTION_RANGE                    7:0
#define NVEC_KEYBOARD_REPORT_WAKE_KEY_0_ACTION_DISABLE                  0x0
#define NVEC_KEYBOARD_REPORT_WAKE_KEY_0_ACTION_ENABLE                   0x1

typedef struct NvEcKeyboardSetScanCodeSetRequestPayloadRec
{
    NvU8 ScanSet;
} NvEcKeyboardSetScanCodeSetRequestPayload;

typedef struct NvEcKeyboardGetScanCodeSetResponsePayloadRec
{
    NvU8 ScanSet;
} NvEcKeyboardGetScanCodeSetResponsePayload;

typedef struct NvEcKeyboardSetLedsRequestPayloadRec
{
    NvU8 LedFlag;     // see NVEC_KEYBOARD_SET_LEDS* #define's
} NvEcKeyboardSetLedsRequestPayload;

#define NVEC_KEYBOARD_SET_LEDS_0_SCROLL_LOCK_LED_RANGE    2:2
#define NVEC_KEYBOARD_SET_LEDS_0_SCROLL_LOCK_LED_ON       0x1
#define NVEC_KEYBOARD_SET_LEDS_0_SCROLL_LOCK_LED_OFF      0x0

#define NVEC_KEYBOARD_SET_LEDS_0_NUM_LOCK_LED_RANGE       1:1
#define NVEC_KEYBOARD_SET_LEDS_0_NUM_LOCK_LED_ON          0x1
#define NVEC_KEYBOARD_SET_LEDS_0_NUM_LOCK_LED_OFF         0x0

#define NVEC_KEYBOARD_SET_LEDS_0_CAPS_LOCK_LED_RANGE      0:0
#define NVEC_KEYBOARD_SET_LEDS_0_CAPS_LOCK_LED_ON         0x1
#define NVEC_KEYBOARD_SET_LEDS_0_CAPS_LOCK_LED_OFF        0x0


/*******************************************************************************
 *
 * AuxDevice Request/Response details
 *
 */

/**
 * AuxDevice subtypes
 *
 * Note that for AuxDevice's the subtype setting contains two bit-fields which
 * encode the following information --
 * * port id on which operation is to be performed
 * * operation subtype to perform
 */

typedef enum
{
    NvEcAuxDeviceSubtype_Reset,
    NvEcAuxDeviceSubtype_SendCommand,
    NvEcAuxDeviceSubtype_ReceiveBytes,
    NvEcAuxDeviceSubtype_AutoReceiveBytes,
    NvEcAuxDeviceSubtype_CancelAutoReceive,
    NvEcAuxDeviceSubtype_SetCompression,

    NvEcAuxDeviceSubtype_ConfigureWake = 0x3d,

    NvEcAuxDeviceSubtype_Num,
    NvEcAuxDeviceSubtype_Max = 0x7fffffff
} NvEcAuxDeviceSubtype;

#define NVEC_SUBTYPE_0_AUX_PORT_ID_RANGE           7:6

#define NVEC_SUBTYPE_0_AUX_PORT_ID_0               0x0
#define NVEC_SUBTYPE_0_AUX_PORT_ID_1               0x1
#define NVEC_SUBTYPE_0_AUX_PORT_ID_2               0x2
#define NVEC_SUBTYPE_0_AUX_PORT_ID_3               0x3

#define NVEC_SUBTYPE_0_AUX_PORT_SUBTYPE_RANGE      5:0

/**
 * AuxDevice payload data structures
 */

typedef struct NvEcAuxDeviceSendCommandRequestPayloadRec
{
    NvU8 Operation;
    NvU8 NumBytesToReceive;
} NvEcAuxDeviceSendCommandRequestPayload;

typedef struct NvEcAuxDeviceReceiveBytesRequestPayloadRec
{
    NvU8 NumBytesToReceive;
} NvEcAuxDeviceReceiveBytesRequestPayload;

typedef struct NvEcAuxDeviceAutoReceiveBytesRequestPayloadRec
{
    NvU8 NumBytesToReceive;
} NvEcAuxDeviceAutoReceiveBytesRequestPayload;

typedef struct NvEcAuxDeviceSetCompressionRequestPayloadRec
{
    NvU8 CompressionEnable;     // see NVEC_AUX_DEVICE_SET_COMPRESSION* #define's
} NvEcAuxDeviceSetCompressionRequestPayload;

#define NVEC_AUX_DEVICE_COMPRESSION_ENABLE_0_ACTION_RANGE               0:0
#define NVEC_AUX_DEVICE_COMPRESSION_ENABLE_0_ACTION_DISABLE             0x0
#define NVEC_AUX_DEVICE_COMPRESSION_ENABLE_0_ACTION_ENABLE              0x1

typedef struct NvEcAuxDeviceConfigureWakeRequestPayloadRec
{
    NvU8 WakeEnable;     // see NVEC_AUX_DEVICE_WAKE_ENABLE* #define's
    NvU8 EventTypes;     // see NVEC_AUX_DEVICE_EVENT_TYPE* #define's
} NvEcAuxDeviceConfigureWakeRequestPayload;

#define NVEC_AUX_DEVICE_WAKE_ENABLE_0_ACTION_RANGE                      7:0
#define NVEC_AUX_DEVICE_WAKE_ENABLE_0_ACTION_DISABLE                    0x0
#define NVEC_AUX_DEVICE_WAKE_ENABLE_0_ACTION_ENABLE                     0x1

#define NVEC_AUX_DEVICE_EVENT_TYPE_0_ANY_EVENT_RANGE                    0:0
#define NVEC_AUX_DEVICE_EVENT_TYPE_0_ANY_EVENT_DISABLE                  0x0
#define NVEC_AUX_DEVICE_EVENT_TYPE_0_ANY_EVENT_ENABLE                   0x1


/*******************************************************************************
 *
 * Control Request/Response details
 *
 */

/**
 * Control subtypes
 */

typedef enum
{
    NvEcControlSubtype_Reset,
    NvEcControlSubtype_SelfTest,
    NvEcControlSubtype_NoOperation,

    NvEcControlSubtype_GetSpecVersion = 0x10,
    NvEcControlSubtype_GetCapabilities,
    NvEcControlSubtype_GetConfiguration,
    NvEcControlSubtype_GetProductName = 0x14,
    NvEcControlSubtype_GetFirmwareVersion,

    NvEcControlSubtype_InitializeGenericConfiguration = 0x20,
    NvEcControlSubtype_SendGenericConfigurationBytes,
    NvEcControlSubtype_FinalizeGenericConfiguration,

    NvEcControlSubtype_InitializeFirmwareUpdate = 0x30,
    NvEcControlSubtype_SendFirmwareBytes,
    NvEcControlSubtype_FinalizeFirmwareUpdate,
    NvEcControlSubtype_PollFirmwareUpdate,

    NvEcControlSubtype_GetFirmwareSize = 0x40,
    NvEcControlSubtype_ReadFirmwareBytes,

    NvEcControlSubtype_Num,
    NvEcControlSubtype_Max = 0x7fffffff
} NvEcControlSubtype;

/**
 * Control payload data structures
 */

typedef struct NvEcControlGetSpecVersionResponsePayloadRec
{
    NvU8 Version;
} NvEcControlGetSpecVersionResponsePayload;

// extract 4-bit major version number from 8-bit version number
#define NVEC_SPEC_VERSION_MAJOR(x)  (((x)>>4) & 0xf)

// extract 4-bit minor version number from 8-bit version number
#define NVEC_SPEC_VERSION_MINOR(x)  ((x) & 0xf)

// assemble 8-bit version number from 4-bit major version number
// and 4-bit minor version number
#define NVEC_SPEC_VERSION(major, minor)  ((((major)&0xf) << 4) | ((minor)&0xf))

#define NVEC_SPEC_VERSION_1_0  NVEC_SPEC_VERSION(1,0)

typedef struct NvEcControlGetCapabilitiesResponsePayloadRec
{
    NvU8 Capabilities[2];     // see NVEC_CONTROL_CAPABILITIES* #define's
    NvU8 OEMCapabilities[2];
} NvEcControlGetCapabilitiesResponsePayload;

#define NVEC_CONTROL_CAPABILITIES0_0_FIXED_SIZE_EVENT_PACKET_RANGE          4:4
#define NVEC_CONTROL_CAPABILITIES0_0_FIXED_SIZE_EVENT_PACKET_NOT_SUPPORTED  0x0
#define NVEC_CONTROL_CAPABILITIES0_0_FIXED_SIZE_EVENT_PACKET_SUPPORTED      0x1

#define NVEC_CONTROL_CAPABILITIES0_0_NON_EC_WAKE_RANGE                      3:3
#define NVEC_CONTROL_CAPABILITIES0_0_NON_EC_WAKE_NOT_SUPPORTED              0x0
#define NVEC_CONTROL_CAPABILITIES0_0_NON_EC_WAKE_SUPPORTED                  0x1

#define NVEC_CONTROL_CAPABILITIES0_0_GENERIC_CONFIGURATION_RANGE            0:0
#define NVEC_CONTROL_CAPABILITIES0_0_GENERIC_CONFIGURATION_NOT_SUPPORTED    0x0
#define NVEC_CONTROL_CAPABILITIES0_0_GENERIC_CONFIGURATION_SUPPORTED        0x1

typedef struct NvEcControlGetConfigurationResponsePayloadRec
{
    NvU8 Configuration[2];     // see NVEC_CONTROL_CONFIGURATION* #define's
    NvU8 OEMConfiguration[2];
} NvEcControlGetConfigurationResponsePayload;

#define NVEC_CONTROL_CONFIGURATION0_0_NUM_AUX_DEVICE_PORTS_RANGE        5:4
#define NVEC_CONTROL_CONFIGURATION0_0_NUM_BATTERY_SLOTS_RANGE           3:0

typedef struct NvEcControlGetProductNameResponsePayloadRec
{
    char ProductName[NVEC_MAX_RESPONSE_STRING_SIZE];
} NvEcControlGetProductNameResponsePayload;

typedef struct NvEcControlGetFirmwareVersionResponsePayloadRec
{
    NvU8 VersionMinor[2];
    NvU8 VersionMajor[2];
} NvEcControlGetFirmwareVersionResponsePayload;

typedef struct NvEcControlInitializeGenericConfigurationRequestPayloadRec
{
    NvU8 ConfigurationId[4];
} NvEcControlInitializeGenericConfigurationRequestPayload;

typedef struct NvEcControlSendGenericConfigurationBytesResponsePayloadRec
{
    NvU8 NumBytes[4];
} NvEcControlSendGenericConfigurationBytesResponsePayload;

typedef struct NvEcControlSendFirmwareBytesResponsePayloadRec
{
    NvU8 NumBytes[4];
} NvEcControlSendFirmwareBytesResponsePayload;

typedef struct NvEcControlPollFirmwareUpdateResponsePayloadRec
{
    NvU8 Flag;     // see NVEC_CONTROL_POLL_FIRMWARE_UPDATE* #define's
} NvEcControlPollFirmwareUpdateResponsePayload;

#define NVEC_CONTROL_POLL_FIRMWARE_UPDATE_0_FLAG_RANGE                      7:0
#define NVEC_CONTROL_POLL_FIRMWARE_UPDATE_0_FLAG_BUSY                       0x0
#define NVEC_CONTROL_POLL_FIRMWARE_UPDATE_0_FLAG_READY                      0x1

typedef struct NvEcControlGetFirmwareSizeResponsePayloadRec
{
    NvU8 NumBytes[4];
} NvEcControlGetFirmwareSizeResponsePayload;


/*******************************************************************************
 *
 * Keyboard Event details
 *
 */

// there are no predefined structures for payload content; only the higher-level
// keyboard driver will know how to interpret the payload data

/*******************************************************************************
 *
 * Auxiliary Device Event details
 *
 */

// there are no predefined structures for payload content; only the higher-level
// auxiliary device driver will know how to interpret the payload data

/*******************************************************************************
 *
 * System Event details
 *
 */

typedef struct NvEcSystemEventPayloadRec
{
    NvU8 State[2];     // see NVEC_SYSTEM_STATE* #define's
    NvU8 OEMState[2];
} NvEcSystemEventPayload;

/*******************************************************************************
 *
 * GPIO Scalar Event details
 *
 */

typedef struct NvEcGpioScalarEventPayloadRec
{
    NvU8 LogicalPinNumber;
} NvEcGpioScalarEventPayload;

/*******************************************************************************
 *
 * GPIO Vector Event details
 *
 */

typedef struct NvEcGpioVectorEventPayloadRec
{
    NvU8 TriggerStatusBitVector[NVEC_GPIO_MAX_BIT_VECTOR_BYTES];
} NvEcGpioVectorEventPayload;

/*******************************************************************************
 *
 * Battery Event details
 *
 */

typedef struct NvEcBatteryEventPayloadRec
{
    NvU8 SlotNumber;
    NvU8 SlotStatus;     // see NVEC_BATTERY_SLOT_STATUS* #define's
} NvEcBatteryEventPayload;


/*******************************************************************************
 *
 * Generic Configuration Package Header
 *
 */

typedef struct NvEcGenericConfigurationPackageHeaderRec
{
    NvU8 MagicNumber[4];     // see NVEC_GENERIC_CONFIGURATION_MAGIC* #define's
    NvU8 SpecVersion;
    NvU8 Reserved0;
    char ProductName[NVEC_MAX_RESPONSE_STRING_SIZE];
    NvU8 FirmwareVersionMinor[2];
    NvU8 FirmwareVersionMajor[2];
    NvU8 ConfigurationID[4];
    NvU8 BodyLength[4];
    NvU8 Checksum[4];     // CRC-32 from IEEE 802.3
} NvEcGenericConfigurationPackageHeader;

#define NVEC_GENERIC_CONFIGURATION_MAGIC_HEADER_NUMBER_BYTE_0 'c'
#define NVEC_GENERIC_CONFIGURATION_MAGIC_HEADER_NUMBER_BYTE_1 'n'
#define NVEC_GENERIC_CONFIGURATION_MAGIC_HEADER_NUMBER_BYTE_2 'f'
#define NVEC_GENERIC_CONFIGURATION_MAGIC_HEADER_NUMBER_BYTE_3 'g'

/*******************************************************************************
 *
 * Firmware Update Package Header
 *
 */

typedef struct NvEcFirmwareUpdatePackageHeaderRec
{
    NvU8 MagicNumber[4];     // see NVEC_FIRMWARE_UPDATE_MAGIC* #define's
    NvU8 SpecVersion;
    NvU8 Reserved0;
    char ProductName[NVEC_MAX_RESPONSE_STRING_SIZE];
    NvU8 FirmwareVersionMinor[2];
    NvU8 FirmwareVersionMajor[2];
    NvU8 BodyLength[4];
    NvU8 Checksum[4];     // CRC-32 from IEEE 802.3
} NvEcFirmwareUpdatePackageHeader;

#define NVEC_FIRMWARE_UPDATE_HEADER_MAGIC_NUMBER_BYTE_0 'u'
#define NVEC_FIRMWARE_UPDATE_HEADER_MAGIC_NUMBER_BYTE_1 'p'
#define NVEC_FIRMWARE_UPDATE_HEADER_MAGIC_NUMBER_BYTE_2 'd'
#define NVEC_FIRMWARE_UPDATE_HEADER_MAGIC_NUMBER_BYTE_3 't'


#if defined(__cplusplus)
}
#endif

#endif
