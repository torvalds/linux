#ifndef CSR_LOG_H__
#define CSR_LOG_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_types.h"
#include "csr_sched.h"
#include "csr_panic.h"
#include "csr_prim_defs.h"
#include "csr_msgconv.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Log filtering
 */

/*----------------------------------------------------*/
/*  Filtering on environment specific log levels      */
/*----------------------------------------------------*/
typedef u32 CsrLogLevelEnvironment;
#define CSR_LOG_LEVEL_ENVIRONMENT_OFF          ((CsrLogLevelEnvironment) 0x00000000) /* No environment data/events are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_BCI_ACL      ((CsrLogLevelEnvironment) 0x00000001) /* BlueCore Channel Interface HCI Acl data are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_BCI_HCI      ((CsrLogLevelEnvironment) 0x00000002) /* BlueCore Channel Interface HCI Cmd/Evt data are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_BCI_SCO      ((CsrLogLevelEnvironment) 0x00000004) /* BlueCore Channel Interface HCI Sco data are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_BCI_VENDOR   ((CsrLogLevelEnvironment) 0x00000008) /* BlueCore Channel Interface HCI Vendor specific data are logged (This includes BCCMD, HQ, VM etc) */
#define CSR_LOG_LEVEL_ENVIRONMENT_TRANSPORTS   ((CsrLogLevelEnvironment) 0x00000010) /* Transport protocol data is logged (This includes transport protocols like BCSP, H4 etc.) */
#define CSR_LOG_LEVEL_ENVIRONMENT_BGINT_REG    ((CsrLogLevelEnvironment) 0x00000020) /* Background Interrupt registration events are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_BGINT_UNREG  ((CsrLogLevelEnvironment) 0x00000040) /* Background Interrupt unregistration events are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_BGINT_SET    ((CsrLogLevelEnvironment) 0x00000080) /* Background Interrupt set events are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_BGINT_START  ((CsrLogLevelEnvironment) 0x00000100) /* Background Interrupt start events are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_BGINT_DONE   ((CsrLogLevelEnvironment) 0x00000200) /* Background Interrupt done events are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_PROTO        ((CsrLogLevelEnvironment) 0x00000400) /* Transport protocol events are logged */
#define CSR_LOG_LEVEL_ENVIRONMENT_PROTO_LOC    ((CsrLogLevelEnvironment) 0x00000800) /* The Location where the transport protocol event occured are logged NB: This is a supplement to CSR_LOG_LEVEL_ENVIRONMENT_PROTO, it has no effect without it */
/* The bit masks between here are reserved for future usage */
#define CSR_LOG_LEVEL_ENVIRONMENT_ALL          ((CsrLogLevelEnvironment) 0xFFFFFFFF) /* All possible environment data/events are logged WARNING: By using this define the application also accepts future possible environment data/events in the logs */

/*----------------------------------------------------*/
/*  Filtering on task specific log levels             */
/*----------------------------------------------------*/
typedef u32 CsrLogLevelTask;
#define CSR_LOG_LEVEL_TASK_OFF                 ((CsrLogLevelTask) 0x00000000) /* No events are logged for this task */
#define CSR_LOG_LEVEL_TASK_TEXT                ((CsrLogLevelTask) 0x00000001) /* Text strings printed by a task are logged NB: This bit does not affect the CSR_LOG_TEXT_LEVEL interface. This has to be configured separately */
#define CSR_LOG_LEVEL_TASK_TEXT_LOC            ((CsrLogLevelTask) 0x00000002) /* The locaction where the text string call occured are logged. NB: This is a supplement to CSR_LOG_LEVEL_TASK_TEXT, it has no effect without it */
#define CSR_LOG_LEVEL_TASK_STATE               ((CsrLogLevelTask) 0x00000004) /* FSM state transitions in a task are logged */
#define CSR_LOG_LEVEL_TASK_STATE_NAME          ((CsrLogLevelTask) 0x00000008) /* The name of each state in a FSM state transition are logged. NB: This is a supplement to CSR_LOG_LEVEL_TASK_STATE, it has no effect without it */
#define CSR_LOG_LEVEL_TASK_STATE_LOC           ((CsrLogLevelTask) 0x00000010) /* The location where the FSM state transition occured are logged. NB: This is a supplement to CSR_LOG_LEVEL_TASK_STATE, it has no effect without it */
#define CSR_LOG_LEVEL_TASK_TASK_SWITCH         ((CsrLogLevelTask) 0x00000020) /* Activation and deactiation of a task are logged */
#define CSR_LOG_LEVEL_TASK_MESSAGE_PUT         ((CsrLogLevelTask) 0x00000080) /* Message put operations are logged */
#define CSR_LOG_LEVEL_TASK_MESSAGE_PUT_LOC     ((CsrLogLevelTask) 0x00000100) /* The location where a message was sent are logged. NB: This is a supplement to CSR_LOG_LEVEL_TASK_MESSAGE_PUT, it has no effect without it */
#define CSR_LOG_LEVEL_TASK_MESSAGE_GET         ((CsrLogLevelTask) 0x00000200) /* Message get operations are logged */
#define CSR_LOG_LEVEL_TASK_MESSAGE_QUEUE_PUSH  ((CsrLogLevelTask) 0x00000400) /* Message push operations are logged */
#define CSR_LOG_LEVEL_TASK_MESSAGE_QUEUE_POP   ((CsrLogLevelTask) 0x00000800) /* Message pop operations are logged */
#define CSR_LOG_LEVEL_TASK_PRIM_ONLY_TYPE      ((CsrLogLevelTask) 0x00001000) /* Only the type of primitives in messages are logged. By default the entire primitive is serialized and logged */
#define CSR_LOG_LEVEL_TASK_PRIM_APPLY_LIMIT    ((CsrLogLevelTask) 0x00002000) /* An upper limit (defined by CSR_LOG_PRIM_SIZE_UPPER_LIMIT) is applied to how much of a primitive in a message are logged. NB: This limit is only applied if CSR_LOG_LEVEL_TASK_PRIM_ONLY_TYPE is _not_ defined */
#define CSR_LOG_LEVEL_TASK_TIMER_IN            ((CsrLogLevelTask) 0x00004000) /* TimedEventIn events are logged */
#define CSR_LOG_LEVEL_TASK_TIMER_IN_LOC        ((CsrLogLevelTask) 0x00008000) /* The location where a timer was started are logged. NB: This is a supplement to CSR_LOG_LEVEL_TASK_TIMER_IN, it has no effect without it */
#define CSR_LOG_LEVEL_TASK_TIMER_CANCEL        ((CsrLogLevelTask) 0x00010000) /* TimedEventCancel events are logged */
#define CSR_LOG_LEVEL_TASK_TIMER_CANCEL_LOC    ((CsrLogLevelTask) 0x00020000) /* The location where a timer was cancelled are logged. NB: This is a supplement to CSR_LOG_LEVEL_TASK_TIMER_CANCEL, it has no effect without it */
#define CSR_LOG_LEVEL_TASK_TIMER_FIRE          ((CsrLogLevelTask) 0x00040000) /* TimedEventFire events are logged */
#define CSR_LOG_LEVEL_TASK_TIMER_DONE          ((CsrLogLevelTask) 0x00080000) /* TimedEventDone events are logged */
/* The bit masks between here are reserved for future usage */
#define CSR_LOG_LEVEL_TASK_ALL                 ((CsrLogLevelTask) 0xFFFFFFFF & ~(CSR_LOG_LEVEL_TASK_PRIM_ONLY_TYPE | CSR_LOG_LEVEL_TASK_PRIM_APPLY_LIMIT)) /* All info possible to log for a task are logged. WARNING: By using this define the application also accepts future possible task data/events in the logs */

CsrBool CsrLogEnvironmentIsFiltered(CsrLogLevelEnvironment level);
CsrLogLevelTask CsrLogTaskFilterGet(CsrSchedQid taskId);
CsrBool CsrLogTaskIsFiltered(CsrSchedQid taskId, CsrLogLevelTask level);

/*
 * Logging stuff
 */
#define CSR_LOG_STRINGIFY_REAL(a) #a
#define CSR_LOG_STRINGIFY(a) CSR_LOG_STRINGIFY_REAL(a)

#ifdef CSR_LOG_ASSERT_ENABLE
#define CSR_LOG_ASSERT(cond)                        \
    do {                                                \
        if (!(cond))                                    \
        {                                               \
            CsrCharString *panic_arg = "[" __FILE__ ":" CSR_LOG_STRINGIFY(__LINE__) "] - " CSR_LOG_STRINGIFY(cond); \
            CsrPanic(CSR_TECH_FW, CSR_PANIC_FW_ASSERTION_FAIL, panic_arg); \
        }                                               \
    } while (0)
#else
#define CSR_LOG_ASSERT(cond)
#endif

typedef struct
{
    u16            primitiveType;
    const CsrCharString *primitiveName;
    CsrMsgConvMsgEntry  *messageConv; /* Private - do not use */
} CsrLogPrimitiveInformation;

typedef struct
{
    const CsrCharString        *techVer;
    u32                   primitiveInfoCount;
    CsrLogPrimitiveInformation *primitiveInfo;
} CsrLogTechInformation;

/*---------------------------------*/
/*  Tech logging */
/*---------------------------------*/
typedef u8 bitmask8_t;
typedef u16 bitmask16_t;
typedef u32 bitmask32_t;

#ifdef CSR_LOG_ENABLE
#ifdef CSR_LOG_INCLUDE_FILE_NAME_AND_LINE_NUMBER
/* DEPRECATED - replaced by csr_log_text.h */
#define CSR_LOG_TEXT(text) \
    do { \
        if (!CsrLogTaskIsFiltered(CsrSchedTaskQueueGet(), CSR_LOG_LEVEL_TASK_TEXT)) \
        { \
            CsrLogTaskText(text, __LINE__, __FILE__); \
        } \
    } while (0)
#else
/* DEPRECATED - replaced by csr_log_text.h */
#define CSR_LOG_TEXT(text) \
    do { \
        if (!CsrLogTaskIsFiltered(CsrSchedTaskQueueGet(), CSR_LOG_LEVEL_TASK_TEXT)) \
        { \
            CsrLogTaskText(text, 0, NULL); \
        } \
    } while (0)
#endif
#else
#define CSR_LOG_TEXT(text)
#endif

/* DEPRECATED - replaced by csr_log_text.h */
void CsrLogTaskText(const CsrCharString *text,
    u32 line,
    const CsrCharString *file);

#define CSR_LOG_STATE_TRANSITION_MASK_FSM_NAME          (0x001)
#define CSR_LOG_STATE_TRANSITION_MASK_NEXT_STATE        (0x002)
#define CSR_LOG_STATE_TRANSITION_MASK_NEXT_STATE_STR    (0x004)
#define CSR_LOG_STATE_TRANSITION_MASK_PREV_STATE        (0x008)
#define CSR_LOG_STATE_TRANSITION_MASK_PREV_STATE_STR    (0x010)
#define CSR_LOG_STATE_TRANSITION_MASK_EVENT             (0x020)
#define CSR_LOG_STATE_TRANSITION_MASK_EVENT_STR         (0x040)

/* DEPRECATED - replaced by csr_log_text.h */
void CsrLogStateTransition(bitmask16_t mask,
    u32 identifier,
    const CsrCharString *fsm_name,
    u32 prev_state,
    const CsrCharString *prev_state_str,
    u32 in_event,
    const CsrCharString *in_event_str,
    u32 next_state,
    const CsrCharString *next_state_str,
    u32 line,
    const CsrCharString *file);

/*---------------------------------*/
/*  BSP logging */
/*---------------------------------*/
void CsrLogSchedInit(u8 thread_id);
void CsrLogSchedDeinit(u8 thread_id);

void CsrLogSchedStart(u8 thread_id);
void CsrLogSchedStop(u8 thread_id);

void CsrLogInitTask(u8 thread_id, CsrSchedQid tskid, const CsrCharString *tskName);
void CsrLogDeinitTask(u16 task_id);

void CsrLogActivate(CsrSchedQid tskid);
void CsrLogDeactivate(CsrSchedQid tskid);

#define SYNERGY_SERIALIZER_TYPE_DUMP    (0x000)
#define SYNERGY_SERIALIZER_TYPE_SER     (0x001)

void CsrLogMessagePut(u32 line,
    const CsrCharString *file,
    CsrSchedQid src_task_id,
    CsrSchedQid dst_taskid,
    CsrSchedMsgId msg_id,
    u16 prim_type,
    const void *msg);

void CsrLogMessageGet(CsrSchedQid src_task_id,
    CsrSchedQid dst_taskid,
    CsrBool get_res,
    CsrSchedMsgId msg_id,
    u16 prim_type,
    const void *msg);

void CsrLogTimedEventIn(u32 line,
    const CsrCharString *file,
    CsrSchedQid task_id,
    CsrSchedTid tid,
    CsrTime requested_delay,
    u16 fniarg,
    const void *fnvarg);

void CsrLogTimedEventFire(CsrSchedQid task_id,
    CsrSchedTid tid);

void CsrLogTimedEventDone(CsrSchedQid task_id,
    CsrSchedTid tid);

void CsrLogTimedEventCancel(u32 line,
    const CsrCharString *file,
    CsrSchedQid task_id,
    CsrSchedTid tid,
    CsrBool cancel_res);

void CsrLogBgintRegister(u8 thread_id,
    CsrSchedBgint irq,
    const CsrCharString *callback,
    const void *ptr);
void CsrLogBgintUnregister(CsrSchedBgint irq);
void CsrLogBgintSet(CsrSchedBgint irq);
void CsrLogBgintServiceStart(CsrSchedBgint irq);
void CsrLogBgintServiceDone(CsrSchedBgint irq);

void CsrLogExceptionStateEvent(u16 prim_type,
    CsrPrim msg_type,
    u16 state,
    u32 line,
    const CsrCharString *file);
void CsrLogExceptionGeneral(u16 prim_type,
    u16 state,
    const CsrCharString *text,
    u32 line,
    const CsrCharString *file);
void CsrLogExceptionWarning(u16 prim_type,
    u16 state,
    const CsrCharString *text,
    u32 line,
    const CsrCharString *file);

#ifdef __cplusplus
}
#endif

#endif
