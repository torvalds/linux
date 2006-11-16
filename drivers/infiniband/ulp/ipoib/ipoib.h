/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: ipoib.h 1358 2004-12-17 22:00:11Z roland $
 */

#ifndef _IPOIB_H
#define _IPOIB_H

#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/kref.h>
#include <linux/if_infiniband.h>
#include <linux/mutex.h>

#include <net/neighbour.h>

#include <asm/atomic.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_sa.h>

/* constants */

enum {
	IPOIB_PACKET_SIZE         = 2048,
	IPOIB_BUF_SIZE 		  = IPOIB_PACKET_SIZE + IB_GRH_BYTES,

	IPOIB_ENCAP_LEN 	  = 4,

	IPOIB_RX_RING_SIZE 	  = 128,
	IPOIB_TX_RING_SIZE 	  = 64,
	IPOIB_MAX_QUEUE_SIZE	  = 8192,
	IPOIB_MIN_QUEUE_SIZE	  = 2,

	IPOIB_NUM_WC 		  = 4,

	IPOIB_MAX_PATH_REC_QUEUE  = 3,
	IPOIB_MAX_MCAST_QUEUE     = 3,

	IPOIB_FLAG_OPER_UP 	  = 0,
	IPOIB_FLAG_INITIALIZED    = 1,
	IPOIB_FLAG_ADMIN_UP 	  = 2,
	IPOIB_PKEY_ASSIGNED 	  = 3,
	IPOIB_PKEY_STOP 	  = 4,
	IPOIB_FLAG_SUBINTERFACE   = 5,
	IPOIB_MCAST_RUN 	  = 6,
	IPOIB_STOP_REAPER         = 7,
	IPOIB_MCAST_STARTED       = 8,

	IPOIB_MAX_BACKOFF_SECONDS = 16,

	IPOIB_MCAST_FLAG_FOUND 	  = 0,	/* used in set_multicast_list */
	IPOIB_MCAST_FLAG_SENDONLY = 1,
	IPOIB_MCAST_FLAG_BUSY 	  = 2,	/* joining or already joined */
	IPOIB_MCAST_FLAG_ATTACHED = 3,
};

/* structs */

struct ipoib_header {
	__be16	proto;
	u16	reserved;
};

struct ipoib_pseudoheader {
	u8  hwaddr[INFINIBAND_ALEN];
};

struct ipoib_mcast;

struct ipoib_rx_buf {
	struct sk_buff *skb;
	dma_addr_t	mapping;
};

struct ipoib_tx_buf {
	struct sk_buff *skb;
	DECLARE_PCI_UNMAP_ADDR(mapping)
};

/*
 * Device private locking: tx_lock protects members used in TX fast
 * path (and we use LLTX so upper layers don't do extra locking).
 * lock protects everything else.  lock nests inside of tx_lock (ie
 * tx_lock must be acquired first if needed).
 */
struct ipoib_dev_priv {
	spinlock_t lock;

	struct net_device *dev;

	unsigned long flags;

	struct mutex mcast_mutex;
	struct mutex vlan_mutex;

	struct rb_root  path_tree;
	struct list_head path_list;

	struct ipoib_mcast *broadcast;
	struct list_head multicast_list;
	struct rb_root multicast_tree;

	struct work_struct pkey_task;
	struct work_struct mcast_task;
	struct work_struct flush_task;
	struct work_struct restart_task;
	struct work_struct ah_reap_task;

	struct ib_device *ca;
	u8            	  port;
	u16           	  pkey;
	struct ib_pd  	 *pd;
	struct ib_mr  	 *mr;
	struct ib_cq  	 *cq;
	struct ib_qp  	 *qp;
	u32           	  qkey;

	union ib_gid local_gid;
	u16          local_lid;
	u8           local_rate;

	unsigned int admin_mtu;
	unsigned int mcast_mtu;

	struct ipoib_rx_buf *rx_ring;

	spinlock_t           tx_lock;
	struct ipoib_tx_buf *tx_ring;
	unsigned             tx_head;
	unsigned             tx_tail;
	struct ib_sge        tx_sge;
	struct ib_send_wr    tx_wr;

	struct ib_wc ibwc[IPOIB_NUM_WC];

	struct list_head dead_ahs;

	struct ib_event_handler event_handler;

	struct net_device_stats stats;

	struct net_device *parent;
	struct list_head child_intfs;
	struct list_head list;

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
	struct list_head fs_list;
	struct dentry *mcg_dentry;
	struct dentry *path_dentry;
#endif
};

struct ipoib_ah {
	struct net_device *dev;
	struct ib_ah      *ah;
	struct list_head   list;
	struct kref        ref;
	unsigned           last_send;
};

struct ipoib_path {
	struct net_device    *dev;
	struct ib_sa_path_rec pathrec;
	struct ipoib_ah      *ah;
	struct sk_buff_head   queue;

	struct list_head      neigh_list;

	int                   query_id;
	struct ib_sa_query   *query;
	struct completion     done;

	struct rb_node        rb_node;
	struct list_head      list;
};

struct ipoib_neigh {
	struct ipoib_ah    *ah;
	union ib_gid        dgid;
	struct sk_buff_head queue;

	struct neighbour   *neighbour;

	struct list_head    list;
};

/*
 * We stash a pointer to our private neighbour information after our
 * hardware address in neigh->ha.  The ALIGN() expression here makes
 * sure that this pointer is stored aligned so that an unaligned
 * load is not needed to dereference it.
 */
static inline struct ipoib_neigh **to_ipoib_neigh(struct neighbour *neigh)
{
	return (void*) neigh + ALIGN(offsetof(struct neighbour, ha) +
				     INFINIBAND_ALEN, sizeof(void *));
}

struct ipoib_neigh *ipoib_neigh_alloc(struct neighbour *neigh);
void ipoib_neigh_free(struct net_device *dev, struct ipoib_neigh *neigh);

extern struct workqueue_struct *ipoib_workqueue;

/* functions */

void ipoib_ib_completion(struct ib_cq *cq, void *dev_ptr);

struct ipoib_ah *ipoib_create_ah(struct net_device *dev,
				 struct ib_pd *pd, struct ib_ah_attr *attr);
void ipoib_free_ah(struct kref *kref);
static inline void ipoib_put_ah(struct ipoib_ah *ah)
{
	kref_put(&ah->ref, ipoib_free_ah);
}

int ipoib_open(struct net_device *dev);
int ipoib_add_pkey_attr(struct net_device *dev);

void ipoib_send(struct net_device *dev, struct sk_buff *skb,
		struct ipoib_ah *address, u32 qpn);
void ipoib_reap_ah(void *dev_ptr);

void ipoib_flush_paths(struct net_device *dev);
struct ipoib_dev_priv *ipoib_intf_alloc(const char *format);

int ipoib_ib_dev_init(struct net_device *dev, struct ib_device *ca, int port);
void ipoib_ib_dev_flush(void *dev);
void ipoib_ib_dev_cleanup(struct net_device *dev);

int ipoib_ib_dev_open(struct net_device *dev);
int ipoib_ib_dev_up(struct net_device *dev);
int ipoib_ib_dev_down(struct net_device *dev, int flush);
int ipoib_ib_dev_stop(struct net_device *dev);

int ipoib_dev_init(struct net_device *dev, struct ib_device *ca, int port);
void ipoib_dev_cleanup(struct net_device *dev);

void ipoib_mcast_join_task(void *dev_ptr);
void ipoib_mcast_send(struct net_device *dev, void *mgid, struct sk_buff *skb);

void ipoib_mcast_restart_task(void *dev_ptr);
int ipoib_mcast_start_thread(struct net_device *dev);
int ipoib_mcast_stop_thread(struct net_device *dev, int flush);

void ipoib_mcast_dev_down(struct net_device *dev);
void ipoib_mcast_dev_flush(struct net_device *dev);

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
struct ipoib_mcast_iter *ipoib_mcast_iter_init(struct net_device *dev);
int ipoib_mcast_iter_next(struct ipoib_mcast_iter *iter);
void ipoib_mcast_iter_read(struct ipoib_mcast_iter *iter,
				  union ib_gid *gid,
				  unsigned long *created,
				  unsigned int *queuelen,
				  unsigned int *complete,
				  unsigned int *send_only);

struct ipoib_path_iter *ipoib_path_iter_init(struct net_device *dev);
int ipoib_path_iter_next(struct ipoib_path_iter *iter);
void ipoib_path_iter_read(struct ipoib_path_iter *iter,
			  struct ipoib_path *path);
#endif

int ipoib_mcast_attach(struct net_device *dev, u16 mlid,
		       union ib_gid *mgid);
int ipoib_mcast_detach(struct net_device *dev, u16 mlid,
		       union ib_gid *mgid);

int ipoib_init_qp(struct net_device *dev);
int ipoib_transport_dev_init(struct net_device *dev, struct ib_device *ca);
void ipoib_transport_dev_cleanup(struct net_device *dev);

void ipoib_event(struct ib_event_handler *handler,
		 struct ib_event *record);

int ipoib_vlan_add(struct net_device *pdev, unsigned short pkey);
int ipoib_vlan_delete(struct net_device *pdev, unsigned short pkey);

void ipoib_pkey_poll(void *dev);
int ipoib_pkey_dev_delay_open(struct net_device *dev);

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
void ipoib_create_debug_files(struct net_device *dev);
void ipoib_delete_debug_files(struct net_device *dev);
int ipoib_register_debugfs(void);
void ipoib_unregister_debugfs(void);
#else
static inline void ipoib_create_debug_files(struct net_device *dev) { }
static inline void ipoib_delete_debug_files(struct net_device *dev) { }
static inline int ipoib_register_debugfs(void) { return 0; }
static inline void ipoib_unregister_debugfs(void) { }
#endif


#define ipoib_printk(level, priv, format, arg...)	\
	printk(level "%s: " format, ((struct ipoib_dev_priv *) priv)->dev->name , ## arg)
#define ipoib_warn(priv, format, arg...)		\
	ipoib_printk(KERN_WARNING, priv, format , ## arg)

extern int ipoib_sendq_size;
extern int ipoib_recvq_size;

extern struct ib_sa_client ipoib_sa_client;

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
extern int ipoib_debug_level;

#define ipoib_dbg(priv, format, arg...)			\
	do {					        \
		if (ipoib_debug_level > 0)			\
			ipoib_printk(KERN_DEBUG, priv, format , ## arg); \
	} while (0)
#define ipoib_dbg_mcast(priv, format, arg...)		\
	do {					        \
		if (mcast_debug_level > 0)		\
			ipoib_printk(KERN_DEBUG, priv, format , ## arg); \
	} while (0)
#else /* CONFIG_INFINIBAND_IPOIB_DEBUG */
#define ipoib_dbg(priv, format, arg...)			\
	do { (void) (priv); } while (0)
#define ipoib_dbg_mcast(priv, format, arg...)		\
	do { (void) (priv); } while (0)
#endif /* CONFIG_INFINIBAND_IPOIB_DEBUG */

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG_DATA
#define ipoib_dbg_data(priv, format, arg...)		\
	do {					        \
		if (data_debug_level > 0)		\
			ipoib_printk(KERN_DEBUG, priv, format , ## arg); \
	} while (0)
#else /* CONFIG_INFINIBAND_IPOIB_DEBUG_DATA */
#define ipoib_dbg_data(priv, format, arg...)		\
	do { (void) (priv); } while (0)
#endif /* CONFIG_INFINIBAND_IPOIB_DEBUG_DATA */


#define IPOIB_GID_FMT		"%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:" \
				"%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x"

#define IPOIB_GID_RAW_ARG(gid)	((u8 *)(gid))[0], \
				((u8 *)(gid))[1], \
				((u8 *)(gid))[2], \
				((u8 *)(gid))[3], \
				((u8 *)(gid))[4], \
				((u8 *)(gid))[5], \
				((u8 *)(gid))[6], \
				((u8 *)(gid))[7], \
				((u8 *)(gid))[8], \
				((u8 *)(gid))[9], \
				((u8 *)(gid))[10],\
				((u8 *)(gid))[11],\
				((u8 *)(gid))[12],\
				((u8 *)(gid))[13],\
				((u8 *)(gid))[14],\
				((u8 *)(gid))[15]

#define IPOIB_GID_ARG(gid)	IPOIB_GID_RAW_ARG((gid).raw)

#endif /* _IPOIB_H */
