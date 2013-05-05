/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifndef CSR_WIFI_FSM_H
#define CSR_WIFI_FSM_H

#include "csr_prim_defs.h"
#include "csr_log_text.h"
#include "csr_wifi_fsm_event.h"

/* including this file for CsrWifiInterfaceMode*/
#include "csr_wifi_common.h"

#define CSR_WIFI_FSM_ENV       (0xFFFF)

/**
 * @brief
 *   Toplevel FSM context data
 *
 * @par Description
 *   Holds ALL FSM static and dynamic data for a FSM
 */
typedef struct CsrWifiFsmContext CsrWifiFsmContext;

/**
 * @brief
 *   FSM External Wakeup CallbackFunction Pointer
 *
 * @par Description
 *   Defines the external wakeup function for the FSM
 *   to call when an external event is injected into the systen
 *
 * @param[in]    context : External context
 *
 * @return
 *   void
 */
typedef void (*CsrWifiFsmExternalWakupCallbackPtr)(void *context);

/**
 * @brief
 *   Initialises a top level FSM context
 *
 * @par Description
 *   Initialises the FSM Context to an initial state and allocates
 *   space for "maxProcesses" number of instances
 *
 * @param[in]    osaContext         : OSA context
 * @param[in]    applicationContext : Internal fsm application context
 * @param[in]    externalContext    : External context
 * @param[in]    maxProcesses       : Max processes to allocate room for
 *
 * @return
 *   CsrWifiFsmContext* fsm context
 */
extern CsrWifiFsmContext* CsrWifiFsmInit(void *applicationContext, void *externalContext, u16 maxProcesses, CsrLogTextTaskId loggingTaskId);

/**
 * @brief
 *   Resets the FSM's back to first conditions
 *
 * @par Description
 *   This function is used to free any dynamic resources allocated for the
 *   given context by CsrWifiFsmInit().
 *   The FSM's reset function is called to cleanup any fsm specific memory
 *   The reset function does NOT need to free the fsm data pointer as
 *   CsrWifiFsmShutdown() will do it.
 *   the FSM's init function is call again to reinitialise the FSM context.
 *   CsrWifiFsmReset() should NEVER be called when CsrWifiFsmExecute() is running.
 *
 * @param[in]    context    : FSM context
 *
 * @return
 *   void
 */
extern void CsrWifiFsmReset(CsrWifiFsmContext *context);

/**
 * @brief
 *   Frees resources allocated by CsrWifiFsmInit
 *
 * @par Description
 *   This function is used to free any dynamic resources allocated for the
 *   given context by CsrWifiFsmInit(), prior to complete termination of
 *   the program.
 *   The FSM's reset function is called to cleanup any fsm specific memory.
 *   The reset function does NOT need to free the fsm data pointer as
 *   CsrWifiFsmShutdown() will do it.
 *   CsrWifiFsmShutdown() should NEVER be called when CsrWifiFsmExecute() is running.
 *
 * @param[in]    context       : FSM context
 *
 * @return
 *   void
 */
extern void CsrWifiFsmShutdown(CsrWifiFsmContext *context);

/**
 * @brief
 *   Executes the fsm context
 *
 * @par Description
 *   Executes the FSM context and runs until ALL events in the context are processed.
 *   When no more events are left to process then CsrWifiFsmExecute() returns to a time
 *   specifying when to next call the CsrWifiFsmExecute()
 *   Scheduling, threading, blocking and external event notification are outside
 *   the scope of the FSM and CsrWifiFsmExecute().
 *
 * @param[in]    context  : FSM context
 *
 * @return
 *   u32    Time in ms until next timeout or 0xFFFFFFFF for no timer set
 */
extern u32 CsrWifiFsmExecute(CsrWifiFsmContext *context);

/**
 * @brief
 *   Adds an event to the FSM context's external event queue for processing
 *
 * @par Description
 *   Adds an event to the contexts external queue
 *   This is thread safe and adds an event to the fsm's external event queue.
 *
 * @param[in]    context      : FSM context
 * @param[in]    event        : event to add to the event queue
 * @param[in]    source       : source of the event (this can be a synergy task queue or an fsm instance id)
 * @param[in]    destination  : destination of the event (This can be a fsm instance id or CSR_WIFI_FSM_ENV)
 * @param[in]    id           : event id
 *
 * @return
 *   void
 */
extern void CsrWifiFsmSendEventExternal(CsrWifiFsmContext *context, CsrWifiFsmEvent *event, u16 source, u16 destination, CsrPrim primtype, u16 id);

/**
 * @brief
 *   Adds an Alien event to the FSM context's external event queue for processing
 *
 * @par Description
 *   Adds an event to the contexts external queue
 *   This is thread safe and adds an event to the fsm's external event queue.
 *
 * @param[in]    context      : FSM context
 * @param[in]    event        : event to add to the event queue
 * @param[in]    source       : source of the event (this can be a synergy task queue or an fsm instance id)
 * @param[in]    destination  : destination of the event (This can be a fsm instance id or CSR_WIFI_FSM_ENV)
 * @param[in]    id           : event id
 */
#define CsrWifiFsmSendAlienEventExternal(_context, _alienEvent, _source, _destination, _primtype, _id) \
    { \
        CsrWifiFsmAlienEvent *_evt = kmalloc(sizeof(CsrWifiFsmAlienEvent), GFP_KERNEL); \
        _evt->alienEvent = _alienEvent; \
        CsrWifiFsmSendEventExternal(_context, (CsrWifiFsmEvent *)_evt, _source, _destination, _primtype, _id); \
    }


/**
 * @brief
 *   Current time of day in ms
 *
 * @param[in]    context   : FSM context
 *
 * @return
 *   u32 32 bit ms tick
 */
extern u32 CsrWifiFsmGetTimeOfDayMs(CsrWifiFsmContext *context);

/**
 * @brief
 *   Gets the time until the next FSM timer expiry
 *
 * @par Description
 *   Returns the next timeout time or 0 if no timers are set.
 *
 * @param[in]    context    : FSM context
 *
 * @return
 *   u32    Time in ms until next timeout or 0xFFFFFFFF for no timer set
 */
extern u32 CsrWifiFsmGetNextTimeout(CsrWifiFsmContext *context);

/**
 * @brief
 *   Fast forwards the fsm timers by ms Milliseconds
 *
 * @param[in]  context : FSM context
 * @param[in]  ms      : Milliseconds to fast forward by
 *
 * @return
 *   void
 */
extern void CsrWifiFsmFastForward(CsrWifiFsmContext *context, u16 ms);

/**
 * @brief
 *   shift the current time of day by ms amount
 *
 * @par Description
 *   useful to speed up tests where time needs to pass
 *
 * @param[in]    context  : FSM context
 * @param[in]    ms       : ms to adjust time by
 *
 * @return
 *   void
 */
extern void CsrWifiFsmTestAdvanceTime(CsrWifiFsmContext *context, u32 ms);

/**
 * @brief
 *    Check if the fsm has events to process
 *
 * @param[in]    context    : FSM context
 *
 * @return
 *   u8 returns TRUE if there are events for the FSM to process
 */
extern u8 CsrWifiFsmHasEvents(CsrWifiFsmContext *context);

/**
 * @brief
 *   function that installs the contexts wakeup function
 *
 * @param[in]    context    : FSM context
 * @param[in]    callback   : Callback function pointer
 *
 * @return
 *   void
 */
extern void CsrWifiFsmInstallWakeupCallback(CsrWifiFsmContext *context, CsrWifiFsmExternalWakupCallbackPtr callback);

#endif /* CSR_WIFI_FSM_H */

