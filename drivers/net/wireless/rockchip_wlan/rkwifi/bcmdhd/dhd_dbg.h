/*
 * Debug/trace/assert driver definitions for Dongle Host Driver.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_dbg.h 667145 2016-10-26 04:27:53Z $
 */

#ifndef _dhd_dbg_
#define _dhd_dbg_


#if defined(DHD_EFI) && defined(DHD_LOG_DUMP)
extern void dhd_log_dump_print(const char *fmt, ...);
extern void dhd_log_dump_print_drv(const char *fmt, ...);
#endif

#if defined(DHD_DEBUG)

#ifdef DHD_LOG_DUMP
extern void dhd_log_dump_write(int type, const char *fmt, ...);
extern char *dhd_log_dump_get_timestamp(void);
#ifndef _DHD_LOG_DUMP_DEFINITIONS_
#define _DHD_LOG_DUMP_DEFINITIONS_
#define DLD_BUF_TYPE_GENERAL    0
#define DLD_BUF_TYPE_SPECIAL    1
#define DHD_LOG_DUMP_WRITE(fmt, ...)	dhd_log_dump_write(DLD_BUF_TYPE_GENERAL, fmt, ##__VA_ARGS__)
#define DHD_LOG_DUMP_WRITE_EX(fmt, ...)	dhd_log_dump_write(DLD_BUF_TYPE_SPECIAL, fmt, ##__VA_ARGS__)
#endif /* !_DHD_LOG_DUMP_DEFINITIONS_ */

#ifdef DHD_EFI
#define DHD_ERROR(args)	\
do {	\
	if (dhd_msg_level & DHD_ERROR_VAL) {	\
		printf args;	\
		dhd_log_dump_print_drv("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__); \
		dhd_log_dump_print_drv args; \
	}	\
} while (0)
#define DHD_INFO(args) \
do {	\
	if (dhd_msg_level & DHD_INFO_VAL) {	\
		printf args;	\
		dhd_log_dump_print_drv("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__); \
		dhd_log_dump_print_drv args; \
	}	\
} while (0)
#else /* DHD_EFI */
#define DHD_ERROR(args)	\
do {	\
	if (dhd_msg_level & DHD_ERROR_VAL) {	\
		printf args;	\
		DHD_LOG_DUMP_WRITE("[%s] %s: ", dhd_log_dump_get_timestamp(), __func__);	\
		DHD_LOG_DUMP_WRITE args;	\
	}	\
} while (0)
#define DHD_INFO(args)		do {if (dhd_msg_level & DHD_INFO_VAL) printf args;} while (0)
#endif /* DHD_EFI */
#else /* DHD_LOG_DUMP */
#define DHD_ERROR(args)		do {if (dhd_msg_level & DHD_ERROR_VAL) printf args;} while (0)
#define DHD_INFO(args)		do {if (dhd_msg_level & DHD_INFO_VAL) printf args;} while (0)
#endif /* DHD_LOG_DUMP */
#define DHD_TRACE(args)		do {if (dhd_msg_level & DHD_TRACE_VAL) printf args;} while (0)

#define DHD_DATA(args)		do {if (dhd_msg_level & DHD_DATA_VAL) printf args;} while (0)
#define DHD_CTL(args)		do {if (dhd_msg_level & DHD_CTL_VAL) printf args;} while (0)
#define DHD_TIMER(args)		do {if (dhd_msg_level & DHD_TIMER_VAL) printf args;} while (0)
#define DHD_HDRS(args)		do {if (dhd_msg_level & DHD_HDRS_VAL) printf args;} while (0)
#define DHD_BYTES(args)		do {if (dhd_msg_level & DHD_BYTES_VAL) printf args;} while (0)
#define DHD_INTR(args)		do {if (dhd_msg_level & DHD_INTR_VAL) printf args;} while (0)
#define DHD_GLOM(args)		do {if (dhd_msg_level & DHD_GLOM_VAL) printf args;} while (0)
#ifdef DHD_LOG_DUMP
#ifndef DHD_EFI
#define DHD_EVENT(args) \
do {	\
	if (dhd_msg_level & DHD_EVENT_VAL) {	\
		printf args;	\
		DHD_LOG_DUMP_WRITE("[%s] %s: ", dhd_log_dump_get_timestamp(), __func__);	\
		DHD_LOG_DUMP_WRITE args;	\
	}	\
} while (0)
#else
#define DHD_EVENT(args) \
do {	\
	if (dhd_msg_level & DHD_EVENT_VAL) {	\
		dhd_log_dump_print("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__);	\
		dhd_log_dump_print args;	\
	}	\
} while (0)
#endif /* !DHD_EFI */
#else
#define DHD_EVENT(args)		do {if (dhd_msg_level & DHD_EVENT_VAL) printf args;} while (0)
#endif /* DHD_LOG_DUMP */
#define DHD_ISCAN(args)		do {if (dhd_msg_level & DHD_ISCAN_VAL) printf args;} while (0)
#define DHD_ARPOE(args)		do {if (dhd_msg_level & DHD_ARPOE_VAL) printf args;} while (0)
#define DHD_REORDER(args)	do {if (dhd_msg_level & DHD_REORDER_VAL) printf args;} while (0)
#define DHD_PNO(args)		do {if (dhd_msg_level & DHD_PNO_VAL) printf args;} while (0)
#define DHD_RTT(args)		do {if (dhd_msg_level & DHD_RTT_VAL) printf args;} while (0)
#define DHD_PKT_MON(args)	do {if (dhd_msg_level & DHD_PKT_MON_VAL) printf args;} while (0)
#ifdef DHD_LOG_DUMP
#ifndef DHD_EFI
#define DHD_MSGTRACE_LOG(args)	\
do {	\
	if (dhd_msg_level & DHD_MSGTRACE_VAL) {	\
		printf args;	\
		DHD_LOG_DUMP_WRITE("[%s] %s: ", dhd_log_dump_get_timestamp(), __func__);	\
		DHD_LOG_DUMP_WRITE args;	\
	}   \
} while (0)
#else
#define DHD_MSGTRACE_LOG(args)	\
do {	\
	if (dhd_msg_level & DHD_MSGTRACE_VAL) {	\
		dhd_log_dump_print("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__);	\
		dhd_log_dump_print args;	\
	}   \
} while (0)
#endif /* !DHD_EFI	*/
#else
#define DHD_MSGTRACE_LOG(args)  do {if (dhd_msg_level & DHD_MSGTRACE_VAL) printf args;} while (0)
#endif /* DHD_LOG_DUMP */

#if defined(DHD_LOG_DUMP) && defined(DHD_EFI)
#define DHD_FWLOG(args) DHD_MSGTRACE_LOG(args)
#else

#define DHD_FWLOG(args)		do {if (dhd_msg_level & DHD_FWLOG_VAL) printf args;} while (0)
#endif /* DHD_LOG_DUMP & DHD_EFI */
#define DHD_DBGIF(args)		do {if (dhd_msg_level & DHD_DBGIF_VAL) printf args;} while (0)

#ifdef DHD_LOG_DUMP
#ifdef DHD_EFI
#define DHD_ERROR_MEM(args)					\
do {                                        \
	if (dhd_msg_level & DHD_ERROR_VAL) {    \
		dhd_log_dump_print_drv("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__); \
		dhd_log_dump_print_drv args;	\
	}	\
} while (0)
#define DHD_ERROR_EX(args)	DHD_ERROR(args)
#else
#define DHD_ERROR_MEM(args)					\
do {                                        \
	if (dhd_msg_level & DHD_ERROR_VAL) {    \
			if (dhd_msg_level & DHD_ERROR_MEM_VAL) {	\
				printf args;	\
			}	\
		DHD_LOG_DUMP_WRITE("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__);	\
		DHD_LOG_DUMP_WRITE args;	\
	}	\
} while (0)
#define DHD_ERROR_EX(args)					\
do {                                        \
	if (dhd_msg_level & DHD_ERROR_VAL) {    \
		printf args;	\
		DHD_LOG_DUMP_WRITE_EX("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__);	\
		DHD_LOG_DUMP_WRITE_EX args;	\
	}	\
} while (0)
#endif /* DHD_EFI */
#else
#define DHD_ERROR_MEM(args)	DHD_ERROR(args)
#define DHD_ERROR_EX(args)	DHD_ERROR(args)
#endif /* DHD_LOG_DUMP */

#ifdef CUSTOMER_HW4_DEBUG
#define DHD_TRACE_HW4	DHD_ERROR
#define DHD_INFO_HW4	DHD_ERROR
#else
#define DHD_TRACE_HW4	DHD_TRACE
#define DHD_INFO_HW4	DHD_INFO
#endif /* CUSTOMER_HW4_DEBUG */

#define DHD_ERROR_ON()		(dhd_msg_level & DHD_ERROR_VAL)
#define DHD_TRACE_ON()		(dhd_msg_level & DHD_TRACE_VAL)
#define DHD_INFO_ON()		(dhd_msg_level & DHD_INFO_VAL)
#define DHD_DATA_ON()		(dhd_msg_level & DHD_DATA_VAL)
#define DHD_CTL_ON()		(dhd_msg_level & DHD_CTL_VAL)
#define DHD_TIMER_ON()		(dhd_msg_level & DHD_TIMER_VAL)
#define DHD_HDRS_ON()		(dhd_msg_level & DHD_HDRS_VAL)
#define DHD_BYTES_ON()		(dhd_msg_level & DHD_BYTES_VAL)
#define DHD_INTR_ON()		(dhd_msg_level & DHD_INTR_VAL)
#define DHD_GLOM_ON()		(dhd_msg_level & DHD_GLOM_VAL)
#define DHD_EVENT_ON()		(dhd_msg_level & DHD_EVENT_VAL)
#define DHD_ISCAN_ON()		(dhd_msg_level & DHD_ISCAN_VAL)
#define DHD_ARPOE_ON()		(dhd_msg_level & DHD_ARPOE_VAL)
#define DHD_REORDER_ON()	(dhd_msg_level & DHD_REORDER_VAL)
#define DHD_NOCHECKDIED_ON()	(dhd_msg_level & DHD_NOCHECKDIED_VAL)
#define DHD_PNO_ON()		(dhd_msg_level & DHD_PNO_VAL)
#define DHD_RTT_ON()		(dhd_msg_level & DHD_RTT_VAL)
#define DHD_MSGTRACE_ON()	(dhd_msg_level & DHD_MSGTRACE_VAL)
#define DHD_FWLOG_ON()		(dhd_msg_level & DHD_FWLOG_VAL)
#define DHD_DBGIF_ON()		(dhd_msg_level & DHD_DBGIF_VAL)
#define DHD_PKT_MON_ON()	(dhd_msg_level & DHD_PKT_MON_VAL)
#define DHD_PKT_MON_DUMP_ON()	(dhd_msg_level & DHD_PKT_MON_DUMP_VAL)

#else /* defined(BCMDBG) || defined(DHD_DEBUG) */

#if defined(DHD_EFI)
extern void dhd_log_dump_print_drv(const char *fmt, ...);
extern char *dhd_log_dump_get_timestamp(void);
#define DHD_ERROR(args)	\
do {	\
	if (dhd_msg_level & DHD_ERROR_VAL) {	\
		printf args;	\
		dhd_log_dump_print_drv("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__); \
		dhd_log_dump_print_drv args; \
	}	\
} while (0)
#define DHD_INFO(args) \
do {	\
	if (dhd_msg_level & DHD_INFO_VAL) {	\
		printf args;	\
		dhd_log_dump_print_drv("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__); \
		dhd_log_dump_print_drv args; \
	}	\
} while (0)
#define DHD_TRACE(args)
#else /* DHD_EFI */

#define DHD_ERROR(args)		do {if (dhd_msg_level & DHD_ERROR_VAL) \
								printf args;} while (0)
#define DHD_TRACE(args)
#define DHD_INFO(args)
#endif 

#define DHD_DATA(args)
#define DHD_CTL(args)
#define DHD_TIMER(args)
#define DHD_HDRS(args)
#define DHD_BYTES(args)
#define DHD_INTR(args)
#define DHD_GLOM(args)

#if defined(DHD_EFI) && defined(DHD_LOG_DUMP)
#define DHD_EVENT(args) \
do {	\
	if (dhd_msg_level & DHD_EVENT_VAL) {	\
		dhd_log_dump_print("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__);	\
		dhd_log_dump_print args;	\
	}	\
} while (0)
#else
#define DHD_EVENT(args)
#endif /* DHD_EFI && DHD_LOG_DUMP */

#define DHD_ISCAN(args)
#define DHD_ARPOE(args)
#define DHD_REORDER(args)
#define DHD_PNO(args)
#define DHD_RTT(args)
#define DHD_PKT_MON(args)

#if defined(DHD_EFI) && defined(DHD_LOG_DUMP)
#define DHD_MSGTRACE_LOG(args)	\
do {	\
	if (dhd_msg_level & DHD_MSGTRACE_VAL) {	\
		dhd_log_dump_print("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__);	\
		dhd_log_dump_print args;	\
	}   \
} while (0)
#define DHD_FWLOG(args) DHD_MSGTRACE_LOG(args)
#else
#define DHD_MSGTRACE_LOG(args)
#define DHD_FWLOG(args)
#endif /* DHD_EFI && DHD_LOG_DUMP */

#define DHD_DBGIF(args)

#if defined(DHD_EFI) && defined(DHD_LOG_DUMP)
#define DHD_ERROR_MEM(args)                  \
do {                                        \
	if (dhd_msg_level & DHD_ERROR_VAL) {    \
		dhd_log_dump_print("[%s] %s: ", dhd_log_dump_get_timestamp(), __FUNCTION__);	\
		dhd_log_dump_print args;	\
	}	\
} while (0)
#define DHD_ERROR_EX(args)	DHD_ERROR(args)
#else
#define DHD_ERROR_MEM(args) DHD_ERROR(args)
#define DHD_ERROR_EX(args)	DHD_ERROR(args)
#endif /* DHD_EFI && DHD_LOG_DUMP */

#ifdef CUSTOMER_HW4_DEBUG
#define DHD_TRACE_HW4	DHD_ERROR
#define DHD_INFO_HW4	DHD_ERROR
#else
#define DHD_TRACE_HW4	DHD_TRACE
#define DHD_INFO_HW4	DHD_INFO
#endif /* CUSTOMER_HW4_DEBUG */

#define DHD_ERROR_ON()		0
#define DHD_TRACE_ON()		0
#define DHD_INFO_ON()		0
#define DHD_DATA_ON()		0
#define DHD_CTL_ON()		0
#define DHD_TIMER_ON()		0
#define DHD_HDRS_ON()		0
#define DHD_BYTES_ON()		0
#define DHD_INTR_ON()		0
#define DHD_GLOM_ON()		0
#define DHD_EVENT_ON()		0
#define DHD_ISCAN_ON()		0
#define DHD_ARPOE_ON()		0
#define DHD_REORDER_ON()	0
#define DHD_NOCHECKDIED_ON()	0
#define DHD_PNO_ON()		0
#define DHD_RTT_ON()		0
#define DHD_PKT_MON_ON()	0
#define DHD_PKT_MON_DUMP_ON()	0
#define DHD_MSGTRACE_ON()	0
#define DHD_FWLOG_ON()		0
#define DHD_DBGIF_ON()		0
#endif 

#define DHD_LOG(args)

#define DHD_BLOG(cp, size)

#define DHD_NONE(args)
extern int dhd_msg_level;

/* Defines msg bits */
#include <dhdioctl.h>

#endif /* _dhd_dbg_ */
