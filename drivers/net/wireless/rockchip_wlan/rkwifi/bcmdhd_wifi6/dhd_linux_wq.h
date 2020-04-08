/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom Dongle Host Driver (DHD), Generic work queue framework
 * Generic interface to handle dhd deferred work events
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
 * $Id: dhd_linux_wq.h 814378 2019-04-11 02:21:31Z $
 */
#ifndef _dhd_linux_wq_h_
#define _dhd_linux_wq_h_
/*
 *	Work event definitions
 */
enum _wq_event {
	DHD_WQ_WORK_IF_ADD = 1,
	DHD_WQ_WORK_IF_DEL,
	DHD_WQ_WORK_SET_MAC,
	DHD_WQ_WORK_SET_MCAST_LIST,
	DHD_WQ_WORK_IPV6_NDO,
	DHD_WQ_WORK_HANG_MSG,
	DHD_WQ_WORK_DHD_LOG_DUMP,
	DHD_WQ_WORK_PKTLOG_DUMP,
	DHD_WQ_WORK_INFORM_DHD_MON,
	DHD_WQ_WORK_EVENT_LOGTRACE,
	DHD_WQ_WORK_DMA_LB_MEM_REL,
	DHD_WQ_WORK_NATOE_EVENT,
	DHD_WQ_WORK_NATOE_IOCTL,
	DHD_WQ_WORK_MACDBG,
	DHD_WQ_WORK_DEBUG_UART_DUMP,
	DHD_WQ_WORK_GET_BIGDATA_AP,
	DHD_WQ_WORK_SOC_RAM_DUMP,
#ifdef DHD_ERPOM
	DHD_WQ_WORK_ERROR_RECOVERY,
#endif /* DHD_ERPOM */
	DHD_WQ_WORK_H2D_CONSOLE_TIME_STAMP_MATCH,
	DHD_WQ_WORK_AXI_ERROR_DUMP,
	DHD_WQ_WORK_CTO_RECOVERY,
#ifdef DHD_UPDATE_INTF_MAC
	DHD_WQ_WORK_IF_UPDATE,
#endif /* DHD_UPDATE_INTF_MAC */
	DHD_MAX_WQ_EVENTS
};

/*
 *	Work event priority
 */
enum wq_priority {
	DHD_WQ_WORK_PRIORITY_LOW = 1,
	DHD_WQ_WORK_PRIORITY_HIGH,
	DHD_WQ_MAX_PRIORITY
};

/*
 *	Error definitions
 */
#define DHD_WQ_STS_OK			 0
#define DHD_WQ_STS_FAILED		-1	/* General failure */
#define DHD_WQ_STS_UNINITIALIZED	-2
#define DHD_WQ_STS_SCHED_FAILED		-3
#define DHD_WQ_STS_UNKNOWN_EVENT	-4
#define DHD_WQ_STS_UNKNOWN_PRIORITY	-5
#define DHD_WQ_STS_EVENT_SKIPPED	-6

typedef void (*event_handler_t)(void *handle, void *event_data, u8 event);

void *dhd_deferred_work_init(void *dhd);
void dhd_deferred_work_deinit(void *workq);
int dhd_deferred_schedule_work(void *workq, void *event_data, u8 event,
	event_handler_t evt_handler, u8 priority);
void dhd_deferred_work_set_skip(void *work, u8 event, bool set);
#endif /* _dhd_linux_wq_h_ */
