#ifndef CSR_LOG_TEXT_H__
#define CSR_LOG_TEXT_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_log_configure.h"

typedef struct CsrLogSubOrigin
{
    u16            subOriginNumber;  /* Id of the given SubOrigin */
    const char *subOriginName;    /* Prefix Text for this SubOrigin */
} CsrLogSubOrigin;

/* Register a task which is going to use the CSR_LOG_TEXT_XXX interface */
#ifdef CSR_LOG_ENABLE
void CsrLogTextRegister(CsrLogTextTaskId taskId, const char *taskName, u16 subOriginsLength, const CsrLogSubOrigin *subOrigins);
#else
#define CsrLogTextRegister(taskId, taskName, subOriginsLength, subOrigins)
#endif

/* CRITICAL: Conditions that are threatening to the integrity/stability of the
   system as a whole. */
#if defined(CSR_LOG_ENABLE) && !defined(CSR_LOG_LEVEL_TEXT_CRITICAL_DISABLE)
void CsrLogTextCritical(CsrLogTextTaskId taskId, u16 subOrigin, const char *formatString, ...);
void CsrLogTextBufferCritical(CsrLogTextTaskId taskId, u16 subOrigin, size_t bufferLength, const void *buffer, const char *formatString, ...);
#define CSR_LOG_TEXT_CRITICAL(taskId_subOrigin_formatString_varargs) CsrLogTextCritical taskId_subOrigin_formatString_varargs
#define CSR_LOG_TEXT_CONDITIONAL_CRITICAL(condition, logtextargs) {if (condition) {CSR_LOG_TEXT_CRITICAL(logtextargs);}}
#define CSR_LOG_TEXT_BUFFER_CRITICAL(taskId_subOrigin_length_buffer_formatString_varargs) CsrLogTextBufferCritical taskId_subOrigin_length_buffer_formatString_varargs
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_CRITICAL(condition, logtextbufferargs) {if (condition) {CSR_LOG_TEXT_BUFFER_CRITICAL(logtextbufferargs);}}
#else
#define CSR_LOG_TEXT_CRITICAL(taskId_subOrigin_formatString_varargs)
#define CSR_LOG_TEXT_CONDITIONAL_CRITICAL(condition, logtextargs)
#define CSR_LOG_TEXT_BUFFER_CRITICAL(taskId_subOrigin_length_buffer_formatString_varargs)
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_CRITICAL(condition, logtextbufferargs)
#endif

/* ERROR: Malfunction of a component rendering it unable to operate correctly,
   causing lack of functionality but not loss of system integrity/stability. */
#if defined(CSR_LOG_ENABLE) && !defined(CSR_LOG_LEVEL_TEXT_ERROR_DISABLE)
void CsrLogTextError(CsrLogTextTaskId taskId, u16 subOrigin, const char *formatString, ...);
void CsrLogTextBufferError(CsrLogTextTaskId taskId, u16 subOrigin, size_t bufferLength, const void *buffer, const char *formatString, ...);
#define CSR_LOG_TEXT_ERROR(taskId_subOrigin_formatString_varargs) CsrLogTextError taskId_subOrigin_formatString_varargs
#define CSR_LOG_TEXT_CONDITIONAL_ERROR(condition, logtextargs) {if (condition) {CSR_LOG_TEXT_ERROR(logtextargs);}}
#define CSR_LOG_TEXT_BUFFER_ERROR(taskId_subOrigin_length_buffer_formatString_varargs) CsrLogTextBufferError taskId_subOrigin_length_buffer_formatString_varargs
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_ERROR(condition, logtextbufferargs) {if (condition) {CSR_LOG_TEXT_BUFFER_ERROR(logtextbufferargs);}}
#else
#define CSR_LOG_TEXT_ERROR(taskId_subOrigin_formatString_varargs)
#define CSR_LOG_TEXT_CONDITIONAL_ERROR(condition, logtextargs)
#define CSR_LOG_TEXT_BUFFER_ERROR(taskId_subOrigin_length_buffer_formatString_varargs)
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_ERROR(condition, logtextbufferargs)
#endif

/* WARNING: Conditions that are unexpected and indicative of possible problems
   or violations of specifications, where the result of such deviations does not
   lead to malfunction of the component. */
#if defined(CSR_LOG_ENABLE) && !defined(CSR_LOG_LEVEL_TEXT_WARNING_DISABLE)
void CsrLogTextWarning(CsrLogTextTaskId taskId, u16 subOrigin, const char *formatString, ...);
void CsrLogTextBufferWarning(CsrLogTextTaskId taskId, u16 subOrigin, size_t bufferLength, const void *buffer, const char *formatString, ...);
#define CSR_LOG_TEXT_WARNING(taskId_subOrigin_formatString_varargs) CsrLogTextWarning taskId_subOrigin_formatString_varargs
#define CSR_LOG_TEXT_CONDITIONAL_WARNING(condition, logtextargs) {if (condition) {CSR_LOG_TEXT_WARNING(logtextargs);}}
#define CSR_LOG_TEXT_BUFFER_WARNING(taskId_subOrigin_length_buffer_formatString_varargs) CsrLogTextBufferWarning taskId_subOrigin_length_buffer_formatString_varargs
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_WARNING(condition, logtextbufferargs) {if (condition) {CSR_LOG_TEXT_BUFFER_WARNING(logtextbufferargs);}}
#else
#define CSR_LOG_TEXT_WARNING(taskId_subOrigin_formatString_varargs)
#define CSR_LOG_TEXT_CONDITIONAL_WARNING(condition, logtextargs)
#define CSR_LOG_TEXT_BUFFER_WARNING(taskId_subOrigin_length_buffer_formatString_varargs)
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_WARNING(condition, logtextbufferargs)
#endif

/* INFO: Important events that may aid in determining the conditions under which
   the more severe conditions are encountered. */
#if defined(CSR_LOG_ENABLE) && !defined(CSR_LOG_LEVEL_TEXT_INFO_DISABLE)
void CsrLogTextInfo(CsrLogTextTaskId taskId, u16 subOrigin, const char *formatString, ...);
void CsrLogTextBufferInfo(CsrLogTextTaskId taskId, u16 subOrigin, size_t bufferLength, const void *buffer, const char *formatString, ...);
#define CSR_LOG_TEXT_INFO(taskId_subOrigin_formatString_varargs) CsrLogTextInfo taskId_subOrigin_formatString_varargs
#define CSR_LOG_TEXT_CONDITIONAL_INFO(condition, logtextargs) {if (condition) {CSR_LOG_TEXT_INFO(logtextargs);}}
#define CSR_LOG_TEXT_BUFFER_INFO(taskId_subOrigin_length_buffer_formatString_varargs) CsrLogTextBufferInfo taskId_subOrigin_length_buffer_formatString_varargs
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_INFO(condition, logtextbufferargs) {if (condition) {CSR_LOG_TEXT_BUFFER_INFO(logtextbufferargs);}}
#else
#define CSR_LOG_TEXT_INFO(taskId_subOrigin_formatString_varargs)
#define CSR_LOG_TEXT_CONDITIONAL_INFO(condition, logtextargs)
#define CSR_LOG_TEXT_BUFFER_INFO(taskId_subOrigin_length_buffer_formatString_varargs)
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_INFO(condition, logtextbufferargs)
#endif

/* DEBUG: Similar to INFO, but dedicated to events that occur more frequently. */
#if defined(CSR_LOG_ENABLE) && !defined(CSR_LOG_LEVEL_TEXT_DEBUG_DISABLE)
void CsrLogTextDebug(CsrLogTextTaskId taskId, u16 subOrigin, const char *formatString, ...);
void CsrLogTextBufferDebug(CsrLogTextTaskId taskId, u16 subOrigin, size_t bufferLength, const void *buffer, const char *formatString, ...);
#define CSR_LOG_TEXT_DEBUG(taskId_subOrigin_formatString_varargs) CsrLogTextDebug taskId_subOrigin_formatString_varargs
#define CSR_LOG_TEXT_CONDITIONAL_DEBUG(condition, logtextargs) {if (condition) {CSR_LOG_TEXT_DEBUG(logtextargs);}}
#define CSR_LOG_TEXT_BUFFER_DEBUG(taskId_subOrigin_length_buffer_formatString_varargs) CsrLogTextBufferDebug taskId_subOrigin_length_buffer_formatString_varargs
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_DEBUG(condition, logtextbufferargs) {if (condition) {CSR_LOG_TEXT_BUFFER_DEBUG(logtextbufferargs);}}
#else
#define CSR_LOG_TEXT_DEBUG(taskId_subOrigin_formatString_varargs)
#define CSR_LOG_TEXT_CONDITIONAL_DEBUG(condition, logtextargs)
#define CSR_LOG_TEXT_BUFFER_DEBUG(taskId_subOrigin_length_buffer_formatString_varargs)
#define CSR_LOG_TEXT_BUFFER_CONDITIONAL_DEBUG(condition, logtextbufferargs)
#endif

/* CSR_LOG_TEXT_ASSERT (CRITICAL) */
#ifdef CSR_LOG_ENABLE
#define CSR_LOG_TEXT_ASSERT(origin, suborigin, condition) \
    {if (!(condition)) {CSR_LOG_TEXT_CRITICAL((origin, suborigin, "Assertion \"%s\" failed at %s:%u", #condition, __FILE__, __LINE__));}}
#else
#define CSR_LOG_TEXT_ASSERT(origin, suborigin, condition)
#endif

/* CSR_LOG_TEXT_UNHANDLED_PRIM (CRITICAL) */
#ifdef CSR_LOG_ENABLE
#define CSR_LOG_TEXT_UNHANDLED_PRIMITIVE(origin, suborigin, primClass, primType) \
    CSR_LOG_TEXT_CRITICAL((origin, suborigin, "Unhandled primitive 0x%04X:0x%04X at %s:%u", primClass, primType, __FILE__, __LINE__))
#else
#define CSR_LOG_TEXT_UNHANDLED_PRIMITIVE(origin, suborigin, primClass, primType)
#endif

#endif
