/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#ifndef SCIF_MAIN_H
#define SCIF_MAIN_H

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/dmaengine.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/scif.h>

#include "../common/mic_dev.h"

#define SCIF_MGMT_NODE 0
#define SCIF_DEFAULT_WATCHDOG_TO 30
#define SCIF_NODE_ACCEPT_TIMEOUT (3 * HZ)
#define SCIF_NODE_ALIVE_TIMEOUT (SCIF_DEFAULT_WATCHDOG_TO * HZ)

/*
 * Generic state used for certain node QP message exchanges
 * like Unregister, Alloc etc.
 */
enum scif_msg_state {
	OP_IDLE = 1,
	OP_IN_PROGRESS,
	OP_COMPLETED,
	OP_FAILED
};

/*
 * struct scif_info - Global SCIF information
 *
 * @nodeid: Node ID this node is to others
 * @maxid: Max known node ID
 * @total: Total number of SCIF nodes
 * @nr_zombies: number of zombie endpoints
 * @eplock: Lock to synchronize listening, zombie endpoint lists
 * @connlock: Lock to synchronize connected and disconnected lists
 * @nb_connect_lock: Synchronize non blocking connect operations
 * @port_lock: Synchronize access to SCIF ports
 * @uaccept: List of user acceptreq waiting for acceptreg
 * @listen: List of listening end points
 * @zombie: List of zombie end points with pending RMA's
 * @connected: List of end points in connected state
 * @disconnected: List of end points in disconnected state
 * @nb_connect_list: List for non blocking connections
 * @misc_work: miscellaneous SCIF tasks
 * @conflock: Lock to synchronize SCIF node configuration changes
 * @en_msg_log: Enable debug message logging
 * @p2p_enable: Enable P2P SCIF network
 * @mdev: The MISC device
 * @conn_work: Work for workqueue handling all connections
 * @exitwq: Wait queue for waiting for an EXIT node QP message response
 * @loopb_dev: Dummy SCIF device used for loopback
 * @loopb_wq: Workqueue used for handling loopback messages
 * @loopb_wqname[16]: Name of loopback workqueue
 * @loopb_work: Used for submitting work to loopb_wq
 * @loopb_recv_q: List of messages received on the loopb_wq
 * @card_initiated_exit: set when the card has initiated the exit
 */
struct scif_info {
	u8 nodeid;
	u8 maxid;
	u8 total;
	u32 nr_zombies;
	spinlock_t eplock;
	struct mutex connlock;
	spinlock_t nb_connect_lock;
	spinlock_t port_lock;
	struct list_head uaccept;
	struct list_head listen;
	struct list_head zombie;
	struct list_head connected;
	struct list_head disconnected;
	struct list_head nb_connect_list;
	struct work_struct misc_work;
	struct mutex conflock;
	u8 en_msg_log;
	u8 p2p_enable;
	struct miscdevice mdev;
	struct work_struct conn_work;
	wait_queue_head_t exitwq;
	struct scif_dev *loopb_dev;
	struct workqueue_struct *loopb_wq;
	char loopb_wqname[16];
	struct work_struct loopb_work;
	struct list_head loopb_recv_q;
	bool card_initiated_exit;
};

/*
 * struct scif_p2p_info - SCIF mapping information used for P2P
 *
 * @ppi_peer_id - SCIF peer node id
 * @ppi_sg - Scatter list for bar information (One for mmio and one for aper)
 * @sg_nentries - Number of entries in the scatterlist
 * @ppi_da: DMA address for MMIO and APER bars
 * @ppi_len: Length of MMIO and APER bars
 * @ppi_list: Link in list of mapping information
 */
struct scif_p2p_info {
	u8 ppi_peer_id;
	struct scatterlist *ppi_sg[2];
	u64 sg_nentries[2];
	dma_addr_t ppi_da[2];
	u64 ppi_len[2];
#define SCIF_PPI_MMIO 0
#define SCIF_PPI_APER 1
	struct list_head ppi_list;
};

/*
 * struct scif_dev - SCIF remote device specific fields
 *
 * @node: Node id
 * @p2p: List of P2P mapping information
 * @qpairs: The node queue pair for exchanging control messages
 * @intr_wq: Workqueue for handling Node QP messages
 * @intr_wqname: Name of node QP workqueue for handling interrupts
 * @intr_bh: Used for submitting work to intr_wq
 * @lock: Lock used for synchronizing access to the scif device
 * @sdev: SCIF hardware device on the SCIF hardware bus
 * @db: doorbell the peer will trigger to generate an interrupt on self
 * @rdb: Doorbell to trigger on the peer to generate an interrupt on the peer
 * @cookie: Cookie received while registering the interrupt handler
 * @peer_add_work: Work for handling device_add for peer devices
 * @p2p_dwork: Delayed work to enable polling for P2P state
 * @qp_dwork: Delayed work for enabling polling for remote QP information
 * @p2p_retry: Number of times to retry polling of P2P state
 * @base_addr: P2P aperture bar base address
 * @mic_mw mmio: The peer MMIO information used for P2P
 * @spdev: SCIF peer device on the SCIF peer bus
 * @node_remove_ack_pending: True if a node_remove_ack is pending
 * @exit_ack_pending: true if an exit_ack is pending
 * @disconn_wq: Used while waiting for a node remove response
 * @disconn_rescnt: Keeps track of number of node remove requests sent
 * @exit: Status of exit message
 * @qp_dma_addr: Queue pair DMA address passed to the peer
*/
struct scif_dev {
	u8 node;
	struct list_head p2p;
	struct scif_qp *qpairs;
	struct workqueue_struct *intr_wq;
	char intr_wqname[16];
	struct work_struct intr_bh;
	struct mutex lock;
	struct scif_hw_dev *sdev;
	int db;
	int rdb;
	struct mic_irq *cookie;
	struct work_struct peer_add_work;
	struct delayed_work p2p_dwork;
	struct delayed_work qp_dwork;
	int p2p_retry;
	dma_addr_t base_addr;
	struct mic_mw mmio;
	struct scif_peer_dev __rcu *spdev;
	bool node_remove_ack_pending;
	bool exit_ack_pending;
	wait_queue_head_t disconn_wq;
	atomic_t disconn_rescnt;
	enum scif_msg_state exit;
	dma_addr_t qp_dma_addr;
};

extern struct scif_info scif_info;
extern struct idr scif_ports;
extern struct bus_type scif_peer_bus;
extern struct scif_dev *scif_dev;
extern const struct file_operations scif_fops;
extern const struct file_operations scif_anon_fops;

/* Size of the RB for the Node QP */
#define SCIF_NODE_QP_SIZE 0x10000

#include "scif_nodeqp.h"

/*
 * scifdev_self:
 * @dev: The remote SCIF Device
 *
 * Returns true if the SCIF Device passed is the self aka Loopback SCIF device.
 */
static inline int scifdev_self(struct scif_dev *dev)
{
	return dev->node == scif_info.nodeid;
}

static inline bool scif_is_mgmt_node(void)
{
	return !scif_info.nodeid;
}

/*
 * scifdev_is_p2p:
 * @dev: The remote SCIF Device
 *
 * Returns true if the SCIF Device is a MIC Peer to Peer SCIF device.
 */
static inline bool scifdev_is_p2p(struct scif_dev *dev)
{
	if (scif_is_mgmt_node())
		return false;
	else
		return dev != &scif_dev[SCIF_MGMT_NODE] &&
			!scifdev_self(dev);
}

/*
 * scifdev_alive:
 * @scifdev: The remote SCIF Device
 *
 * Returns true if the remote SCIF Device is running or sleeping for
 * this endpoint.
 */
static inline int _scifdev_alive(struct scif_dev *scifdev)
{
	struct scif_peer_dev *spdev;

	rcu_read_lock();
	spdev = rcu_dereference(scifdev->spdev);
	rcu_read_unlock();
	return !!spdev;
}

#include "scif_epd.h"

void __init scif_init_debugfs(void);
void scif_exit_debugfs(void);
int scif_setup_intr_wq(struct scif_dev *scifdev);
void scif_destroy_intr_wq(struct scif_dev *scifdev);
void scif_cleanup_scifdev(struct scif_dev *dev);
void scif_handle_remove_node(int node);
void scif_disconnect_node(u32 node_id, bool mgmt_initiated);
void scif_free_qp(struct scif_dev *dev);
void scif_misc_handler(struct work_struct *work);
void scif_stop(struct scif_dev *scifdev);
irqreturn_t scif_intr_handler(int irq, void *data);
#endif /* SCIF_MAIN_H */
