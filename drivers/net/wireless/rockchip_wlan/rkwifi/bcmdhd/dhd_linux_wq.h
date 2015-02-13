/*
 * Broadcom Dongle Host Driver (DHD), Generic work queue framework
 * Generic interface to handle dhd deferred work events
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: dhd_linux_wq.h 449578 2014-01-17 13:53:20Z $
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
void dhd_deferred_work_deinit(void *workq);
int dhd_deferred_schedule_work(void *workq, void *event_data, u8 event,
	event_handler_t evt_handler, u8 priority);
#endif /* _dhd_linux_wq_h_ */
