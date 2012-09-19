#ifndef CSR_LOG_CONFIGURE_H__
#define CSR_LOG_CONFIGURE_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------*/
/* Log init/deinit                 */
/*---------------------------------*/
void CsrLogInit(u8 size);
void CsrLogDeinit(void);

/*---------------------------------*/
/* Log Framework Tech info         */
/*---------------------------------*/
void CsrLogTechInfoRegister(void);

/* Set the logging level for the environment outside the scheduler context */
void CsrLogLevelEnvironmentSet(CsrLogLevelEnvironment environmentLogLevel);


/* Set the logging level for all scheduler tasks */
/* This function call takes precedence over all previous calls to CsrLogLevelTaskSetSpecific() */
void CsrLogLevelTaskSetAll(CsrLogLevelTask tasksLogLevelMask);

/* Set the logging level for a given Task */
/* This function can be used as a complement to CsrLogLevelTaskSetAll() to add more _or_ less log from a given task than what is set
generally with CsrLogLevelTaskSetAll(). */
void CsrLogLevelTaskSetSpecific(CsrSchedQid taskId, CsrLogLevelTask taskLogLevelMask);


/*--------------------------------------------*/
/*  Filtering on log text warning levels      */
/*--------------------------------------------*/
typedef u32 CsrLogLevelText;
#define CSR_LOG_LEVEL_TEXT_OFF       ((CsrLogLevelText) 0x0000)

#define CSR_LOG_LEVEL_TEXT_CRITICAL  ((CsrLogLevelText) 0x0001)
#define CSR_LOG_LEVEL_TEXT_ERROR     ((CsrLogLevelText) 0x0002)
#define CSR_LOG_LEVEL_TEXT_WARNING   ((CsrLogLevelText) 0x0004)
#define CSR_LOG_LEVEL_TEXT_INFO      ((CsrLogLevelText) 0x0008)
#define CSR_LOG_LEVEL_TEXT_DEBUG     ((CsrLogLevelText) 0x0010)

#define CSR_LOG_LEVEL_TEXT_ALL       ((CsrLogLevelText) 0xFFFF)

/* The log text interface is used by both scheduler tasks and components outside the scheduler context.
 * Therefore a CsrLogTextTaskId is introduced. It is effectively considered as two u16's. The lower
 * 16 bits corresponds one2one with the scheduler queueId's (CsrSchedQid) and as such these bits can not be used
 * by components outside scheduler tasks. The upper 16 bits are allocated for use of components outside the
 * scheduler like drivers etc. Components in this range is defined independently by each technology. To avoid
 * clashes the technologies are only allowed to assign values within the same restrictive range as allies to
 * primitive identifiers. eg. for the framework components outside the scheduler is only allowed to assign
 * taskId's in the range 0x0600xxxx to 0x06FFxxxx. And so on for other technologies. */
typedef u32 CsrLogTextTaskId;

/* Set the text logging level for all Tasks */
/* This function call takes precedence over all previous calls to CsrLogLevelTextSetTask() and CsrLogLevelTextSetTaskSubOrigin() */
void CsrLogLevelTextSetAll(CsrLogLevelText warningLevelMask);

/* Set the text logging level for a given Task */
/* This function call takes precedence over all previous calls to CsrLogLevelTextSetTaskSubOrigin(), but it can be used as a complement to
 * CsrLogLevelTextSetAll() to add more _or_ less log from a given task than what is set generally with CsrLogLevelTextSetAll(). */
void CsrLogLevelTextSetTask(CsrLogTextTaskId taskId, CsrLogLevelText warningLevelMask);

/* Set the text logging level for a given tasks subOrigin */
/* This function can be used as a complement to CsrLogLevelTextSetAll() and CsrLogLevelTextSetTask() to add more _or_ less log from a given
 * subOrigin within a task than what is set generally with CsrLogLevelTextSetAll() _or_ CsrLogLevelTextSetTask(). */
void CsrLogLevelTextSetTaskSubOrigin(CsrLogTextTaskId taskId, u16 subOrigin, CsrLogLevelText warningLevelMask);

/*******************************************************************************

    NAME
        CsrLogLevelTextSet

    DESCRIPTION
        Set the text logging level for a given origin and optionally sub origin
        by name. If either string is NULL or zero length, it is interpreted as
        all origins and/or all sub origins respectively. If originName is NULL
        or zero length, subOriginName is ignored.

        Passing NULL or zero length strings in both originName and subOriginName
        is equivalent to calling CsrLogLevelTextSetAll, and overrides all
        previous filter configurations for all origins and sub origins.

        Passing NULL or a zero length string in subOriginName overrides all
        previous filter configurations for all sub origins of the specified
        origin.

        Note: the supplied strings may be accessed after the function returns
        and must remain valid and constant until CsrLogDeinit is called.

        Note: when specifying an origin (originName is not NULL and not zero
        length), this function can only be used for origins that use the
        csr_log_text_2.h interface for registration and logging. Filtering for
        origins that use the legacy csr_log_text.h interface must be be
        configured using the legacy filter configuration functions that accept
        a CsrLogTextTaskId as origin specifier. However, when not specifying an
        origin this function also affects origins that have been registered with
        the legacy csr_log_text.h interface. Furthermore, using this function
        and the legacy filter configuration functions on the same origin is not
        allowed.

    PARAMETERS
        originName - a string containing the name of the origin. Can be NULL or
            zero length to set the log level for all origins. In this case, the
            subOriginName parameter will be ignored.
        subOriginName - a string containing the name of the sub origin. Can be
            NULL or zero length to set the log level for all sub origins of the
            specified origin.
        warningLevelMask - The desired log level for the specified origin(s) and
            sub origin(s).

*******************************************************************************/
void CsrLogLevelTextSet(const char *originName,
    const char *subOriginName,
    CsrLogLevelText warningLevelMask);

#ifdef __cplusplus
}
#endif

#endif
