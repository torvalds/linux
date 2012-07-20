/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifndef CSR_WIFI_FSM_TYPES_H
#define CSR_WIFI_FSM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "csr_types.h"
#include "csr_util.h"
#include "csr_pmem.h"
#include "csr_panic.h"
#include "csr_sched.h"

#ifdef CSR_WIFI_FSM_MUTEX_ENABLE
#include "csr_framework_ext.h"
#endif

#include "csr_wifi_fsm.h"

#define CSR_WIFI_FSM_MAX_TRANSITION_HISTORY 10

/**
 * @brief
 *   FSM event list header.
 *
 * @par Description
 *   Singly linked list of events.
 */
typedef struct CsrWifiFsmEventList
{
    CsrWifiFsmEvent *first;
    CsrWifiFsmEvent *last;
} CsrWifiFsmEventList;


/**
 * @brief
 *   FSM timer id.
 *
 * @par Description
 *   Composite Id made up of the type, dest and a unique id so
 *   CsrWifiFsmRemoveTimer knows where to look when removing the timer
 */
typedef struct CsrWifiFsmTimerId
{
    CsrPrim     type;
    u16   primtype;
    CsrSchedQid destination;
    u16   uniqueid;
} CsrWifiFsmTimerId;

/**
 * @brief
 *   FSM timer header.
 *
 * @par Description
 *   All timer MUST have this struct as the FIRST member.
 *   The first members of the structure MUST remain compatable
 *   with the CsrWifiFsmEvent so that timers are just specialised events
 */
typedef struct CsrWifiFsmTimer
{
    CsrPrim     type;
    u16   primtype;
    CsrSchedQid destination;
    CsrSchedQid source;

    /* Private pointer to allow an optimal Event list */
    struct CsrWifiFsmTimer *next;

    CsrWifiFsmTimerId timerid;
    u32         timeoutTimeMs;
} CsrWifiFsmTimer;


/**
 * @brief
 *   Fsm Alien Event
 *
 * @par Description
 *   Allows the wrapping of alien events that do not use CsrWifiFsmEvent
 *   as the first member of the Event struct
 */
typedef struct
{
    CsrWifiFsmEvent event;
    void           *alienEvent;
} CsrWifiFsmAlienEvent;


/**
 * @brief
 *   FSM timer list header.
 *
 * @par Description
 *   Singly linked list of timers.
 */
typedef struct CsrWifiFsmTimerList
{
    CsrWifiFsmTimer *first;
    CsrWifiFsmTimer *last;
    u16        nexttimerid;
} CsrWifiFsmTimerList;

/**
 * @brief
 *   Process Entry Function Pointer
 *
 * @par Description
 *   Defines the entry function for a processes.
 *   Called at process initialisation.
 *
 * @param[in]    context : FSM context
 *
 * @return
 *   void
 */
typedef void (*CsrWifiFsmProcEntryFnPtr)(CsrWifiFsmContext *context);

/**
 * @brief
 *   Process Transition Function Pointer
 *
 * @par Description
 *   Defines a transition function for a processes.
 *   Called when an event causes a transition on a process
 *
 * @param[in]    CsrWifiFsmContext* : FSM context
 * @param[in]    void* : FSM data (can be NULL)
 * @param[in]    const CsrWifiFsmEvent*  : event to process
 *
 * @return
 *   void
 */
typedef void (*CsrWifiFsmTransitionFnPtr)(CsrWifiFsmContext *context, void *fsmData, const CsrWifiFsmEvent *event);

/**
 * @brief
 *   Process reset/shutdown Function Pointer
 *
 * @par Description
 *   Defines the reset/shutdown function for a processes.
 *   Called to reset or shutdown an fsm.
 *
 * @param[in]    context      : FSM context
 *
 * @return
 *   void
 */
typedef void (*CsrWifiFsmProcResetFnPtr)(CsrWifiFsmContext *context);

/**
 * @brief
 *   FSM Default Destination CallbackFunction Pointer
 *
 * @par Description
 *   Defines the default destination function for the FSM
 *   to call when an event does not have a valid destination.
 *   This
 *
 * @param[in]    context : External context
 *
 * @return
 *   u16 a valid destination OR CSR_WIFI_FSM_ENV
 */
typedef u16 (*CsrWifiFsmDestLookupCallbackPtr)(void *context, const CsrWifiFsmEvent *event);


#ifdef CSR_WIFI_FSM_DUMP_ENABLE
/**
 * @brief
 *   Trace Dump Function Pointer
 *
 * @par Description
 *   Called when we want to trace the FSM
 *
 * @param[in]    context : FSM context
 * @param[in]    id      : fsm id
 *
 * @return
 *   void
 */
typedef void (*CsrWifiFsmDumpFnPtr)(CsrWifiFsmContext *context, void *fsmData);
#endif

/**
 * @brief
 *   Event ID to transition function entry
 *
 * @par Description
 *   Event ID to Transition Entry in a state table.
 */
typedef struct
{
    u32                 eventid;
    CsrWifiFsmTransitionFnPtr transition;
#ifdef CSR_LOG_ENABLE
    const char *transitionName;
#endif
} CsrWifiFsmEventEntry;

/**
 * @brief
 *   Single State's Transition Table
 *
 * @par Description
 *   Stores Data for a single State's event to
 *   transition functions mapping
 */
typedef struct
{
    const u8              numEntries;
    const CsrBool               saveAll;
    const CsrWifiFsmEventEntry *eventEntryArray; /* array of transition function pointers for state */
#ifdef CSR_LOG_ENABLE
    u16            stateNumber;
    const char *stateName;
#endif
} CsrWifiFsmTableEntry;

/**
 * @brief
 *   Process State Transtion table
 *
 * @par Description
 *   Stores Data for a processes State to transition table
 */
typedef struct
{
    u16                   numStates;         /* number of states    */
    const CsrWifiFsmTableEntry *aStateEventMatrix; /* state event matrix  */
} CsrWifiFsmTransitionFunctionTable;

/**
 * @brief
 *   Const Process definition
 *
 * @par Description
 *   Constant process specification.
 *   This is ALL the non dynamic data that defines
 *   a process.
 */
typedef struct
{
    const char                    *processName;
    const u32                         processId;
    const CsrWifiFsmTransitionFunctionTable transitionTable;
    const CsrWifiFsmTableEntry              unhandledTransitions;
    const CsrWifiFsmTableEntry              ignoreFunctions;
    const CsrWifiFsmProcEntryFnPtr          entryFn;
    const CsrWifiFsmProcResetFnPtr          resetFn;
#ifdef CSR_WIFI_FSM_DUMP_ENABLE
    const CsrWifiFsmDumpFnPtr dumpFn;               /* Called to dump fsm specific trace if not NULL */
#endif
} CsrWifiFsmProcessStateMachine;

#ifdef CSR_WIFI_FSM_DUMP_ENABLE
/**
 * @brief
 *   Storage for state transition info
 */
typedef struct
{
    u16                 transitionNumber;
    CsrWifiFsmEvent           event;
    u16                 fromState;
    u16                 toState;
    CsrWifiFsmTransitionFnPtr transitionFn;
    u16                 transitionCount; /* number consecutive of times this transition was seen */
#ifdef CSR_LOG_ENABLE
    const char *transitionName;
#endif
} CsrWifiFsmTransitionRecord;

/**
 * @brief
 *   Storage for the last state X transitions
 */
typedef struct
{
    u16                  numTransitions;
    CsrWifiFsmTransitionRecord records[CSR_WIFI_FSM_MAX_TRANSITION_HISTORY];
} CsrWifiFsmTransitionRecords;
#endif

/**
 * @brief
 *   Dynamic Process data
 *
 * @par Description
 *   Dynamic process data that is used to keep track of the
 *   state and data for a process instance
 */
typedef struct
{
    const CsrWifiFsmProcessStateMachine *fsmInfo;         /* state machine info that is constant regardless of context */
    u16                            instanceId;      /* Runtime process id */
    u16                            state;           /* Current state */
    void                                *params;          /* Instance user data */
    CsrWifiFsmEventList                  savedEventQueue; /* The saved event queue */
    struct CsrWifiFsmInstanceEntry      *subFsm;          /* Sub Fsm instance data */
    struct CsrWifiFsmInstanceEntry      *subFsmCaller;    /* The Fsm instance that created the SubFsm and should be used for callbacks*/
#ifdef CSR_WIFI_FSM_DUMP_ENABLE
    CsrWifiFsmTransitionRecords transitionRecords;        /* Last X transitions in the FSM */
#endif
} CsrWifiFsmInstanceEntry;

/**
 * @brief
 *   OnCreate Callback Function Pointer
 *
 * @par Description
 *   Called when an fsm is created.
 *
 * @param[in]    extContext : External context
 * @param[in]    instance : FSM instance
 *
 * @return
 *   void
 */
typedef void (*CsrWifiFsmOnCreateFnPtr)(void *extContext, const CsrWifiFsmInstanceEntry *instance);

/**
 * @brief
 *   OnTransition Callback Function Pointer
 *
 * @par Description
 *   Called when an event is processed by a fsm
 *
 * @param[in]    extContext : External context
 * @param[in]    eventEntryArray : Entry data
 * @param[in]    event : Event
 *
 * @return
 *   void
 */
typedef void (*CsrWifiFsmOnTransitionFnPtr)(void *extContext, const CsrWifiFsmEventEntry *eventEntryArray, const CsrWifiFsmEvent *event);

/**
 * @brief
 *   OnStateChange Callback Function Pointer
 *
 * @par Description
 *   Called when CsrWifiFsmNextState is called
 *
 * @param[in]    extContext : External context
 *
 * @return
 *   void
 */
typedef void (*CsrWifiFsmOnStateChangeFnPtr)(void *extContext, u16 nextstate);

/**
 * @brief
 *   OnIgnore,OnError or OnInvalid Callback Function Pointer
 *
 * @par Description
 *   Called when an event is processed by a fsm
 *
 * @param[in]    extContext : External context
 * @param[in]    event : Event
 *
 * @return
 *   void
 */
typedef void (*CsrWifiFsmOnEventFnPtr)(void *extContext, const CsrWifiFsmEvent *event);

/**
 * @brief
 *   Toplevel FSM context data
 *
 * @par Description
 *   Holds ALL FSM static and dynamic data for a FSM
 */
struct CsrWifiFsmContext
{
    CsrWifiFsmEventList eventQueue;                           /* The internal event queue                     */
    CsrWifiFsmEventList externalEventQueue;                   /* The external event queue                     */
#ifdef CSR_WIFI_FSM_MUTEX_ENABLE
    CsrMutexHandle externalEventQueueLock;                    /* The external event queue mutex               */
#endif
    u32                          timeOffset;            /* Amount to adjust the TimeOfDayMs by          */
    CsrWifiFsmTimerList                timerQueue;            /* The internal timer queue                     */
    CsrBool                            useTempSaveList;       /* Should the temp save list be used            */
    CsrWifiFsmEventList                tempSaveList;          /* The temp save event queue                    */
    CsrWifiFsmEvent                   *eventForwardedOrSaved; /* The event that was forwarded or Saved        */
    u16                          maxProcesses;          /* Size of instanceArray                        */
    u16                          numProcesses;          /* Current number allocated in instanceArray    */
    CsrWifiFsmInstanceEntry           *instanceArray;         /* Array of processes for this component        */
    CsrWifiFsmInstanceEntry           *ownerInstance;         /* The Process that owns currentInstance (SubFsm support) */
    CsrWifiFsmInstanceEntry           *currentInstance;       /* Current Process that is executing            */
    CsrWifiFsmExternalWakupCallbackPtr externalEventFn;       /* External event Callback                      */
    CsrWifiFsmOnEventFnPtr             appIgnoreCallback;     /* Application Ignore event Callback            */
    CsrWifiFsmDestLookupCallbackPtr    appEvtDstCallback;     /* Application Lookup event Destination Function*/

    void            *applicationContext;                      /* Internal fsm application context             */
    void            *externalContext;                         /* External context (set by the user of the fsm)*/
    CsrLogTextTaskId loggingTaskId;                           /* Task Id to use in any logging output         */

#ifndef CSR_WIFI_FSM_SCHEDULER_DISABLED
    CsrSchedTid schedTimerId;                                 /* Scheduler TimerId for use in Scheduler Tasks */
    u32   schedTimerNexttimeoutMs;                      /* Next timeout time for the current timer      */
#endif

#ifdef CSR_WIFI_FSM_MUTEX_ENABLE
#ifdef CSR_WIFI_FSM_TRANSITION_LOCK
    CsrMutexHandle transitionLock;                     /* Lock when calling transition functions        */
#endif
#endif

#ifdef CSR_LOG_ENABLE
    CsrWifiFsmOnCreateFnPtr      onCreate;             /* Debug Transition Callback                    */
    CsrWifiFsmOnTransitionFnPtr  onTransition;         /* Debug Transition Callback                    */
    CsrWifiFsmOnTransitionFnPtr  onUnhandedCallback;   /* Unhanded event Callback                      */
    CsrWifiFsmOnStateChangeFnPtr onStateChange;        /* Debug State Change Callback                  */
    CsrWifiFsmOnEventFnPtr       onIgnoreCallback;     /* Ignore event Callback                        */
    CsrWifiFsmOnEventFnPtr       onSaveCallback;       /* Save event Callback                          */
    CsrWifiFsmOnEventFnPtr       onErrorCallback;      /* Error event Callback                         */
    CsrWifiFsmOnEventFnPtr       onInvalidCallback;    /* Invalid event Callback                       */
#endif
#ifdef CSR_WIFI_FSM_DUMP_ENABLE
    u16 masterTransitionNumber;                  /* Increments on every transition              */
#endif
};


#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_FSM_TYPES_H */
