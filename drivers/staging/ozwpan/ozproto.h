/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZPROTO_H
#define _OZPROTO_H

#include <asm/byteorder.h>
#include "ozdbg.h"
#include "ozappif.h"

#define OZ_ALLOCATED_SPACE(__x)	(LL_RESERVED_SPACE(__x)+(__x)->needed_tailroom)

/* Quantum in MS */
#define OZ_QUANTUM		8
/* Default timeouts.
 */
#define OZ_PRESLEEP_TOUT	11

/* Maximun sizes of tx frames. */
#define OZ_MAX_TX_SIZE		760

/* Maximum number of uncompleted isoc frames that can be pending in network. */
#define OZ_MAX_SUBMITTED_ISOC	16

/* Maximum number of uncompleted isoc frames that can be pending in Tx Queue. */
#define OZ_MAX_TX_QUEUE_ISOC	32

/* Application handler functions.
 */
struct oz_app_if {
	int  (*init)(void);
	void (*term)(void);
	int  (*start)(struct oz_pd *pd, int resume);
	void (*stop)(struct oz_pd *pd, int pause);
	void (*rx)(struct oz_pd *pd, struct oz_elt *elt);
	int  (*heartbeat)(struct oz_pd *pd);
	void (*farewell)(struct oz_pd *pd, u8 ep_num, u8 *data, u8 len);
};

int oz_protocol_init(char *devs);
void oz_protocol_term(void);
int oz_get_pd_list(struct oz_mac_addr *addr, int max_count);
void oz_app_enable(int app_id, int enable);
struct oz_pd *oz_pd_find(const u8 *mac_addr);
void oz_binding_add(const char *net_dev);
void oz_binding_remove(const char *net_dev);
void oz_timer_add(struct oz_pd *pd, int type, unsigned long due_time);
void oz_timer_delete(struct oz_pd *pd, int type);
void oz_pd_request_heartbeat(struct oz_pd *pd);
void oz_pd_heartbeat_handler(unsigned long data);
void oz_pd_timeout_handler(unsigned long data);
enum hrtimer_restart oz_pd_heartbeat_event(struct hrtimer *timer);
enum hrtimer_restart oz_pd_timeout_event(struct hrtimer *timer);
int oz_get_pd_status_list(char *pd_list, int max_count);
int oz_get_binding_list(char *buf, int max_if);

extern struct kmem_cache *oz_elt_info_cache;
extern struct kmem_cache *oz_tx_frame_cache;

#endif /* _OZPROTO_H */
