/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZPD_H_
#define _OZPD_H_

#include <linux/interrupt.h>
#include "ozeltbuf.h"

/* PD state
 */
#define OZ_PD_S_IDLE		0x1
#define OZ_PD_S_CONNECTED	0x2
#define OZ_PD_S_SLEEP		0x4
#define OZ_PD_S_STOPPED		0x8

/* Timer event types.
 */
#define OZ_TIMER_TOUT		1
#define OZ_TIMER_HEARTBEAT	2
#define OZ_TIMER_STOP		3

/*
 *External spinlock variable
 */
extern spinlock_t g_polling_lock;

/* Data structure that hold information on a frame for transmisson. This is
 * built when the frame is first transmitted and is used to rebuild the frame
 * if a re-transmission is required.
 */
struct oz_tx_frame {
	struct list_head link;
	struct list_head elt_list;
	struct oz_hdr hdr;
	struct sk_buff *skb;
	int total_size;
};

struct oz_isoc_stream {
	struct list_head link;
	u8 ep_num;
	u8 frame_num;
	u8 nb_units;
	int size;
	struct sk_buff *skb;
	struct oz_hdr *oz_hdr;
};

struct oz_farewell {
	struct list_head link;
	u8 ep_num;
	u8 index;
	u8 len;
	u8 report[0];
};

/* Data structure that holds information on a specific peripheral device (PD).
 */
struct oz_pd {
	struct list_head link;
	atomic_t	ref_count;
	u8		mac_addr[ETH_ALEN];
	unsigned	state;
	unsigned	state_flags;
	unsigned	send_flags;
	u16		total_apps;
	u16		paused_apps;
	u8		session_id;
	u8		param_rsp_status;
	u8		pd_info;
	u8		isoc_sent;
	u32		last_rx_pkt_num;
	u32		last_tx_pkt_num;
	struct timespec last_rx_timestamp;
	u32		trigger_pkt_num;
	unsigned long	pulse_time;
	unsigned long	pulse_period;
	unsigned long	presleep;
	unsigned long	keep_alive;
	struct oz_elt_buf elt_buff;
	void		*app_ctx[OZ_NB_APPS];
	spinlock_t	app_lock[OZ_NB_APPS];
	int		max_tx_size;
	u8		mode;
	u8		ms_per_isoc;
	unsigned	isoc_latency;
	unsigned	max_stream_buffering;
	int		nb_queued_frames;
	int		nb_queued_isoc_frames;
	spinlock_t	tx_frame_lock;
	struct list_head *last_sent_frame;
	struct list_head tx_queue;
	struct list_head farewell_list;
	spinlock_t	stream_lock;
	struct list_head stream_list;
	struct net_device *net_dev;
	struct hrtimer  heartbeat;
	struct hrtimer  timeout;
	u8      timeout_type;
	struct tasklet_struct   heartbeat_tasklet;
	struct tasklet_struct   timeout_tasklet;
	struct work_struct workitem;
};

#define OZ_MAX_QUEUED_FRAMES	4

struct oz_pd *oz_pd_alloc(const u8 *mac_addr);
void oz_pd_destroy(struct oz_pd *pd);
void oz_pd_get(struct oz_pd *pd);
void oz_pd_put(struct oz_pd *pd);
void oz_pd_set_state(struct oz_pd *pd, unsigned state);
void oz_pd_indicate_farewells(struct oz_pd *pd);
int oz_pd_sleep(struct oz_pd *pd);
void oz_pd_stop(struct oz_pd *pd);
void oz_pd_heartbeat(struct oz_pd *pd, u16 apps);
int oz_services_start(struct oz_pd *pd, u16 apps, int resume);
void oz_services_stop(struct oz_pd *pd, u16 apps, int pause);
int oz_prepare_frame(struct oz_pd *pd, int empty);
void oz_send_queued_frames(struct oz_pd *pd, int backlog);
void oz_retire_tx_frames(struct oz_pd *pd, u8 lpn);
int oz_isoc_stream_create(struct oz_pd *pd, u8 ep_num);
int oz_isoc_stream_delete(struct oz_pd *pd, u8 ep_num);
int oz_send_isoc_unit(struct oz_pd *pd, u8 ep_num, const u8 *data, int len);
void oz_handle_app_elt(struct oz_pd *pd, u8 app_id, struct oz_elt *elt);
void oz_apps_init(void);
void oz_apps_term(void);

extern struct kmem_cache *oz_elt_info_cache;
extern struct kmem_cache *oz_tx_frame_cache;

#endif /* Sentry */
