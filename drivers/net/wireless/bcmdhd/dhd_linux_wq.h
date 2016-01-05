/*
 * Broadcom Dongle Host Driver (DHD), Generic work queue framework
 * Generic interface to handle dhd deferred work events
 *
 * Copyright (C) 1999-2015, Broadcom Corporation
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
 * $Id: dhd_linux_wq.h 408802 2013-06-20 19:08:47Z $
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

	DHD_MAX_WQ_EVENTS
};

/*
 *	Work event priority
 */
#define DHD_WORK_PRIORITY_LOW	0
#define DHD_WORK_PRIORITY_HIGH	1

/*
 *	Error definitions
 */
#define DHD_WQ_STS_OK			 0
#define DHD_WQ_STS_FAILED		-1	/* General failure */
#define DHD_WQ_STS_UNINITIALIZED	-2
#define DHD_WQ_STS_SCHED_FAILED		-3
#define DHD_WQ_STS_UNKNOWN_EVENT	-4

typedef void (*event_handler_t)(void *handle, void *event_data, u8 event);

void *dhd_deferred_work_init(void *dhd);
void dhd_deferred_work_deinit(void *work);
int dhd_deferred_schedule_work(void *event_data, u8 event,
	event_handler_t evt_handler, u8 priority);
#endif /* _dhd_linux_wq_h_ */
