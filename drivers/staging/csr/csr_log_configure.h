#ifndef CSR_LOG_CONFIGURE_H__
#define CSR_LOG_CONFIGURE_H__
/*****************************************************************************

  (c) Cambridge Silicon Radio Limited 2010
  All rights reserved and confidential information of CSR

  Refer to LICENSE.txt included with this source for details
  on the license terms.

 *****************************************************************************/

#include "csr_log.h"

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

#endif
