/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Debug/trace/assert driver definitions for Dongle Host Driver.
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 * $Id: dhd_dbg.h 798329 2019-01-08 05:40:39Z $
 */

#ifndef _dhd_dbg_
#define _dhd_dbg_

#ifdef DHD_LOG_DUMP
extern char *dhd_log_dump_get_timestamp(void);
extern void dhd_log_dump_write(int type, char *binary_data,
		int binary_len, const char *fmt, ...);
#ifndef _DHD_LOG_DUMP_DEFINITIONS_
#define _DHD_LOG_DUMP_DEFINITIONS_
#define GENERAL_LOG_HDR "\n-------------------- General log ---------------------------\n"
#define PRESERVE_LOG_HDR "\n-------------------- Preserve log ---------------------------\n"
#define SPECIAL_LOG_HDR "\n-------------------- Special log ---------------------------\n"
#define DHD_DUMP_LOG_HDR "\n-------------------- 'dhd dump' log -----------------------\n"
#define EXT_TRAP_LOG_HDR "\n-------------------- Extended trap data -------------------\n"
#define HEALTH_CHK_LOG_HDR "\n-------------------- Health check data --------------------\n"
#ifdef DHD_DUMP_PCIE_RINGS
#define FLOWRING_DUMP_HDR "\n-------------------- Flowring dump --------------------\n"
#endif /* DHD_DUMP_PCIE_RINGS */
#define DHD_LOG_DUMP_WRITE(fmt, ...) \
	dhd_log_dump_write(DLD_BUF_TYPE_GENERAL, NULL, 0, fmt, ##__VA_ARGS__)
#define DHD_LOG_DUMP_WRITE_EX(fmt, ...) \
	dhd_log_dump_write(DLD_BUF_TYPE_SPECIAL, NULL, 0, fmt, ##__VA_ARGS__)
#define DHD_LOG_DUMP_WRITE_PRSRV(fmt, ...) \
	dhd_log_dump_write(DLD_BUF_TYPE_PRESERVE, NULL, 0, fmt, ##__VA_ARGS__)
#endif /* !_DHD_LOG_DUMP_DEFINITIONS_ */
#define CONCISE_DUMP_BUFLEN 16 * 1024
#define ECNTRS_LOG_HDR "\n-------------------- Ecounters log --------------------------\n"
#ifdef DHD_STATUS_LOGGING
#define STATUS_LOG_HDR "\n-------------------- Status log -----------------------\n"
#endif /* DHD_STATUS_LOGGING */
#define RTT_LOG_HDR "\n-------------------- RTT log --------------------------\n"
#define COOKIE_LOG_HDR "\n-------------------- Cookie List ----------------------------\n"
#endif /* DHD_LOG_DUMP */

#if defined(DHD_DEBUG)

/* NON-NDIS cases */
#ifdef DHD_LOG_DUMP
/* Common case for EFI and non EFI */
#define DHD_ERROR(args)	\
do {	\
	if (dhd_msg_level & DHD_ERROR_VAL) {	\
		printf args;	\
		DHD_LOG_DUMP_WRITE("[%s]: ", dhd_log_dump_get_timestamp());	\
		DHD_LOG_DUMP_WRITE args;	\
	}	\
} while (0)

/* !defined(DHD_EFI) and defined(DHD_LOG_DUMP) */
#define DHD_INFO(args)		do {if (dhd_msg_level & DHD_INFO_VAL) printf args;} while (0)
#else /* DHD_LOG_DUMP */
/* !defined(DHD_LOG_DUMP cases) */
#define DHD_ERROR(args)		do {if (dhd_msg_level & DHD_ERROR_VAL) printf args;} while (0)
#define DHD_INFO(args)		do {if (dhd_msg_level & DHD_INFO_VAL) printf args;} while (0)
#endif /* DHD_LOG_DUMP */

#define DHD_TRACE(args)		do {if (dhd_msg_level & DHD_TRACE_VAL) printf args;} while (0)

#ifdef DHD_LOG_DUMP
/* LOG_DUMP defines common to EFI and NON-EFI */
#define DHD_ERROR_MEM(args) \
do {	\
	if (dhd_msg_level & DHD_ERROR_VAL) {	\
		if (dhd_msg_level & DHD_ERROR_MEM_VAL) {	\
			printf args; \
		}	\
		DHD_LOG_DUMP_WRITE("[%s]: ", dhd_log_dump_get_timestamp());	\
		DHD_LOG_DUMP_WRITE args;	\
	}	\
} while (0)
#define DHD_IOVAR_MEM(args) \
do {	\
	if (dhd_msg_level & DHD_ERROR_VAL) {	\
		if (dhd_msg_level & DHD_IOVAR_MEM_VAL) {	\
			printf args; \
		}	\
		DHD_LOG_DUMP_WRITE("[%s]: ", dhd_log_dump_get_timestamp());	\
		DHD_LOG_DUMP_WRITE args;	\
	}	\
} while (0)
#define DHD_LOG_MEM(args) \
do {	\
	if (dhd_msg_level & DHD_ERROR_VAL) {	\
		DHD_LOG_DUMP_WRITE("[%s]: ", dhd_log_dump_get_timestamp());	\
		DHD_LOG_DUMP_WRITE args;	\
	}	\
} while (0)
/* NON-EFI builds with LOG DUMP enabled */
#define DHD_EVENT(args) \
do {	\
	if (dhd_msg_level & DHD_EVENT_VAL) {	\
		printf args; \
		DHD_LOG_DUMP_WRITE("[%s]: ", dhd_log_dump_get_timestamp());	\
		DHD_LOG_DUMP_WRITE args;	\
	}	\
} while (0)
#define DHD_PRSRV_MEM(args) \
do {	\
	if (dhd_msg_level & DHD_EVENT_VAL) {	\
		if (dhd_msg_level & DHD_PRSRV_MEM_VAL) \
			printf args; \
		DHD_LOG_DUMP_WRITE_PRSRV("[%s]: ", dhd_log_dump_get_timestamp()); \
		DHD_LOG_DUMP_WRITE_PRSRV args;	\
	}	\
} while (0)

/* Re-using 'DHD_MSGTRACE_VAL' for controlling printing of ecounter binary event
* logs to console and debug dump -- need to cleanup in the future to use separate
* 'DHD_ECNTR_VAL' bitmap flag. 'DHD_MSGTRACE_VAL' will be defined only
* for non-android builds.
*/
#define DHD_ECNTR_LOG(args) \
do {	\
	if (dhd_msg_level & DHD_EVENT_VAL) {	\
		if (dhd_msg_level & DHD_MSGTRACE_VAL) {	\
			printf args; \
			DHD_LOG_DUMP_WRITE("[%s]: ", dhd_log_dump_get_timestamp()); \
			DHD_LOG_DUMP_WRITE args;	\
		}	\
	}	\
} while (0)

#define DHD_ERROR_EX(args)					\
do {										\
	if (dhd_msg_level & DHD_ERROR_VAL) {    \
		printf args;	\
		DHD_LOG_DUMP_WRITE_EX("[%s]: ", dhd_log_dump_get_timestamp());	\
		DHD_LOG_DUMP_WRITE_EX args;	\
	}	\
} while (0)

#define DHD_MSGTRACE_LOG(args)	\
do {	\
	if (dhd_msg_level & DHD_MSGTRACE_VAL) {	\
		printf args;	\
	}	\
	DHD_LOG_DUMP_WRITE("[%s]: ", dhd_log_dump_get_timestamp());	\
	DHD_LOG_DUMP_WRITE args;	\
} while (0)
#else /* DHD_LOG_DUMP */
/* !DHD_LOG_DUMP */
#define DHD_MSGTRACE_LOG(args)  do {if (dhd_msg_level & DHD_MSGTRACE_VAL) printf args;} while (0)
#define DHD_ERROR_MEM(args)	DHD_ERROR(args)
#define DHD_IOVAR_MEM(args)	DHD_ERROR(args)
#define DHD_LOG_MEM(args)	DHD_ERROR(args)
#define DHD_EVENT(args)		do {if (dhd_msg_level & DHD_EVENT_VAL) printf args;} while (0)
#define DHD_ECNTR_LOG(args)	DHD_EVENT(args)
#define DHD_PRSRV_MEM(args)	DHD_EVENT(args)
#define DHD_ERROR_EX(args)	DHD_ERROR(args)
#endif /* DHD_LOG_DUMP */

#define DHD_DATA(args)		do {if (dhd_msg_level & DHD_DATA_VAL) printf args;} while (0)
#define DHD_CTL(args)		do {if (dhd_msg_level & DHD_CTL_VAL) printf args;} while (0)
#define DHD_TIMER(args)		do {if (dhd_msg_level & DHD_TIMER_VAL) printf args;} while (0)
#define DHD_HDRS(args)		do {if (dhd_msg_level & DHD_HDRS_VAL) printf args;} while (0)
#define DHD_BYTES(args)		do {if (dhd_msg_level & DHD_BYTES_VAL) printf args;} while (0)
#define DHD_INTR(args)		do {if (dhd_msg_level & DHD_INTR_VAL) printf args;} while (0)
#define DHD_GLOM(args)		do {if (dhd_msg_level & DHD_GLOM_VAL) printf args;} while (0)
#define DHD_BTA(args)		do {if (dhd_msg_level & DHD_BTA_VAL) printf args;} while (0)
#define DHD_ISCAN(args)		do {if (dhd_msg_level & DHD_ISCAN_VAL) printf args;} while (0)
#define DHD_ARPOE(args)		do {if (dhd_msg_level & DHD_ARPOE_VAL) printf args;} while (0)
#define DHD_REORDER(args)	do {if (dhd_msg_level & DHD_REORDER_VAL) printf args;} while (0)
#define DHD_PNO(args)		do {if (dhd_msg_level & DHD_PNO_VAL) printf args;} while (0)
#define DHD_RTT(args)		do {if (dhd_msg_level & DHD_RTT_VAL) printf args;} while (0)
#define DHD_PKT_MON(args)	do {if (dhd_msg_level & DHD_PKT_MON_VAL) printf args;} while (0)

#if defined(DHD_LOG_DUMP)
#if defined(DHD_LOG_PRINT_RATE_LIMIT)
#define DHD_FWLOG(args)	\
	do { \
		if (dhd_msg_level & DHD_FWLOG_VAL) { \
			if (!log_print_threshold) \
				printf args; \
			DHD_LOG_DUMP_WRITE args; \
		} \
	} while (0)
#else
#define DHD_FWLOG(args)	\
	do { \
		if (dhd_msg_level & DHD_FWLOG_VAL) { \
			printf args; \
			DHD_LOG_DUMP_WRITE args; \
		} \
	} while (0)
#endif // endif
#else /* DHD_LOG_DUMP */
#define DHD_FWLOG(args)		do {if (dhd_msg_level & DHD_FWLOG_VAL) printf args;} while (0)
#endif /* DHD_LOG_DUMP */

#define DHD_DBGIF(args)		do {if (dhd_msg_level & DHD_DBGIF_VAL) printf args;} while (0)

#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#define DHD_RPM(args)		do {if (dhd_msg_level & DHD_RPM_VAL) printf args;} while (0)
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#define DHD_TRACE_HW4	DHD_TRACE
#define DHD_INFO_HW4	DHD_INFO
#define DHD_ERROR_NO_HW4	DHD_ERROR

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
#define DHD_BTA_ON()		(dhd_msg_level & DHD_BTA_VAL)
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
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#define DHD_RPM_ON()		(dhd_msg_level & DHD_RPM_VAL)
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */

#else /* defined(BCMDBG) || defined(DHD_DEBUG) */

#define DHD_ERROR(args)		do {if (dhd_msg_level & DHD_ERROR_VAL) \
								printf args;} while (0)
#define DHD_TRACE(args)
#define DHD_INFO(args)

#define DHD_DATA(args)
#define DHD_CTL(args)
#define DHD_TIMER(args)
#define DHD_HDRS(args)
#define DHD_BYTES(args)
#define DHD_INTR(args)
#define DHD_GLOM(args)

#define DHD_EVENT(args)
#define DHD_ECNTR_LOG(args)	DHD_EVENT(args)

#define DHD_PRSRV_MEM(args)	DHD_EVENT(args)

#define DHD_BTA(args)
#define DHD_ISCAN(args)
#define DHD_ARPOE(args)
#define DHD_REORDER(args)
#define DHD_PNO(args)
#define DHD_RTT(args)
#define DHD_PKT_MON(args)

#define DHD_MSGTRACE_LOG(args)
#define DHD_FWLOG(args)

#define DHD_DBGIF(args)

#define DHD_ERROR_MEM(args)	DHD_ERROR(args)
#define DHD_IOVAR_MEM(args)	DHD_ERROR(args)
#define DHD_LOG_MEM(args)	DHD_ERROR(args)
#define DHD_ERROR_EX(args)	DHD_ERROR(args)

#define DHD_TRACE_HW4	DHD_TRACE
#define DHD_INFO_HW4	DHD_INFO
#define DHD_ERROR_NO_HW4	DHD_ERROR

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
#define DHD_BTA_ON()		0
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
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
#define DHD_RPM_ON()		0
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
#endif // endif

#define PRINT_RATE_LIMIT_PERIOD 5000000u /* 5s in units of us */
#define DHD_ERROR_RLMT(args) \
do {	\
	if (dhd_msg_level & DHD_ERROR_VAL) {	\
		static uint64 __err_ts = 0; \
		static uint32 __err_cnt = 0; \
		uint64 __cur_ts = 0; \
		__cur_ts = OSL_SYSUPTIME_US(); \
		if (__err_ts == 0 || (__cur_ts > __err_ts && \
		(__cur_ts - __err_ts > PRINT_RATE_LIMIT_PERIOD))) { \
			__err_ts = __cur_ts; \
			DHD_ERROR(args);	\
			DHD_ERROR(("[Repeats %u times]\n", __err_cnt)); \
			__err_cnt = 0; \
		} else { \
			++__err_cnt; \
		} \
	}	\
} while (0)

/* even in non-BCMDBG builds, logging of dongle iovars should be available */
#define DHD_DNGL_IOVAR_SET(args) \
	do {if (dhd_msg_level & DHD_DNGL_IOVAR_SET_VAL) printf args;} while (0)

#define DHD_LOG(args)

#define DHD_BLOG(cp, size)

#define DHD_NONE(args)
extern int dhd_msg_level;
#ifdef DHD_LOG_PRINT_RATE_LIMIT
extern int log_print_threshold;
#endif /* DHD_LOG_PRINT_RATE_LIMIT */

#define DHD_RTT_MEM(args) DHD_LOG_MEM(args)
#define DHD_RTT_ERR(args) DHD_ERROR(args)

/* Defines msg bits */
#include <dhdioctl.h>

#endif /* _dhd_dbg_ */
