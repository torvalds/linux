#ifndef CSR_PANIC_H__
#define CSR_PANIC_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

/* Synergy techonology ID definitions */
#define CSR_TECH_FW     0
#define CSR_TECH_BT     1
#define CSR_TECH_WIFI   2
#define CSR_TECH_GPS    3
#define CSR_TECH_NFC    4

/* Panic type ID definitions for technology type CSR_TECH_FW */
#define CSR_PANIC_FW_UNEXPECTED_VALUE        0
#define CSR_PANIC_FW_HEAP_EXHAUSTION         1
#define CSR_PANIC_FW_INVALID_PFREE_POINTER   2
#define CSR_PANIC_FW_EXCEPTION               3
#define CSR_PANIC_FW_ASSERTION_FAIL          4
#define CSR_PANIC_FW_NULL_TASK_HANDLER       5
#define CSR_PANIC_FW_UNKNOWN_TASK            6
#define CSR_PANIC_FW_QUEUE_ACCESS_VIOLATION  7
#define CSR_PANIC_FW_TOO_MANY_MESSAGES       8
#define CSR_PANIC_FW_TOO_MANY_TIMED_EVENTS   9
#define CSR_PANIC_FW_ABCSP_SYNC_LOST        10
#define CSR_PANIC_FW_OVERSIZE_ABCSP_PRIM    11
#define CSR_PANIC_FW_H4_CORRUPTION          12
#define CSR_PANIC_FW_H4_SYNC_LOST           13
#define CSR_PANIC_FW_H4_RX_OVERRUN          14
#define CSR_PANIC_FW_H4_TX_OVERRUN          15
#define CSR_PANIC_FW_TM_BC_RESTART_FAIL     16
#define CSR_PANIC_FW_TM_BC_START_FAIL       17
#define CSR_PANIC_FW_TM_BC_BAD_STATE        18
#define CSR_PANIC_FW_TM_BC_TRANSPORT_LOST   19

/* Panic interface used by technologies */
/* DEPRECATED - replaced by csr_log_text.h */
void CsrPanic(u8 tech, u16 reason, const char *p);

#ifdef __cplusplus
}
#endif

#endif /* CSR_PANIC_H__ */
