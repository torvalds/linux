/*
 * Copyright (C) 2007-2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

/* Kernel Programming */
#define LINUX

#define DRIVER_AUTHOR "Marek Lindner <lindner_marek@yahoo.de>, " \
		      "Simon Wunderlich <siwu@hrz.tu-chemnitz.de>"
#define DRIVER_DESC   "B.A.T.M.A.N. advanced"
#define DRIVER_DEVICE "batman-adv"

#define SOURCE_VERSION "0.2.2-beta"


/* B.A.T.M.A.N. parameters */

#define TQ_MAX_VALUE 255
#define JITTER 20
#define TTL 50			  /* Time To Live of broadcast messages */

#define PURGE_TIMEOUT 200000	  /* purge originators after time in ms if no
				   * valid packet comes in -> TODO: check
				   * influence on TQ_LOCAL_WINDOW_SIZE */
#define LOCAL_HNA_TIMEOUT 3600000

#define TQ_LOCAL_WINDOW_SIZE 64	  /* sliding packet range of received originator
				   * messages in squence numbers (should be a
				   * multiple of our word size) */
#define TQ_GLOBAL_WINDOW_SIZE 5
#define TQ_LOCAL_BIDRECT_SEND_MINIMUM 1
#define TQ_LOCAL_BIDRECT_RECV_MINIMUM 1
#define TQ_TOTAL_BIDRECT_LIMIT 1

#define TQ_HOP_PENALTY 10

#define NUM_WORDS (TQ_LOCAL_WINDOW_SIZE / WORD_BIT_SIZE)

#define PACKBUFF_SIZE 2000
#define LOG_BUF_LEN 8192	  /* has to be a power of 2 */
#define ETH_STR_LEN 20

#define MAX_AGGREGATION_BYTES 512 /* should not be bigger than 512 bytes or
				   * change the size of
				   * forw_packet->direct_link_flags */
#define MAX_AGGREGATION_MS 100

#define RESET_PROTECTION_MS 30000
#define EXPECTED_SEQNO_RANGE	4096
/* don't reset again within 30 seconds */

#define MODULE_INACTIVE 0
#define MODULE_ACTIVE 1
#define MODULE_DEACTIVATING 2

#define BCAST_QUEUE_LEN 256
#define BATMAN_QUEUE_LEN	256

/*
 * Debug Messages
 */

#define DBG_BATMAN 1	/* all messages related to routing / flooding /
			 * broadcasting / etc */
#define DBG_ROUTES 2	/* route or hna added / changed / deleted */

#ifdef CONFIG_BATMAN_ADV_DEBUG
extern int debug;

extern int bat_debug_type(int type);
#define bat_dbg(type, fmt, arg...) do {					\
		if (bat_debug_type(type))				\
			printk(KERN_DEBUG "batman-adv:" fmt, ## arg);	\
	}								\
	while (0)
#else /* !CONFIG_BATMAN_ADV_DEBUG */
#define bat_dbg(type, fmt, arg...) do {		\
	}					\
	while (0)
#endif

/*
 *  Vis
 */

/* #define VIS_SUBCLUSTERS_DISABLED */

/*
 * Kernel headers
 */

#include <linux/mutex.h>	/* mutex */
#include <linux/module.h>	/* needed by all modules */
#include <linux/netdevice.h>	/* netdevice */
#include <linux/if_ether.h>	/* ethernet header */
#include <linux/poll.h>		/* poll_table */
#include <linux/kthread.h>	/* kernel threads */
#include <linux/pkt_sched.h>	/* schedule types */
#include <linux/workqueue.h>	/* workqueue */
#include <linux/slab.h>
#include <net/sock.h>		/* struct sock */
#include <linux/jiffies.h>
#include "types.h"

#ifndef REVISION_VERSION
#define REVISION_VERSION_STR ""
#else
#define REVISION_VERSION_STR " "REVISION_VERSION
#endif

extern struct list_head if_list;
extern struct hlist_head forw_bat_list;
extern struct hlist_head forw_bcast_list;
extern struct hashtable_t *orig_hash;

extern spinlock_t orig_hash_lock;
extern spinlock_t forw_bat_list_lock;
extern spinlock_t forw_bcast_list_lock;

extern atomic_t vis_interval;
extern atomic_t bcast_queue_left;
extern atomic_t batman_queue_left;
extern int16_t num_hna;

extern struct net_device *soft_device;

extern unsigned char broadcastAddr[];
extern atomic_t module_state;
extern struct workqueue_struct *bat_event_workqueue;

void activate_module(void);
void deactivate_module(void);
void inc_module_count(void);
void dec_module_count(void);
int addr_to_string(char *buff, uint8_t *addr);
int compare_orig(void *data1, void *data2);
int choose_orig(void *data, int32_t size);
int is_my_mac(uint8_t *addr);
int is_bcast(uint8_t *addr);
int is_mcast(uint8_t *addr);
