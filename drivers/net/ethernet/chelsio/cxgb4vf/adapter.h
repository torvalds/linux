/*
 * This file is part of the Chelsio T4 PCI-E SR-IOV Virtual Function Ethernet
 * driver for Linux.
 *
 * Copyright (c) 2009-2010 Chelsio Communications, Inc. All rights reserved.
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
 */

/*
 * This file should not be included directly.  Include t4vf_common.h instead.
 */

#ifndef __CXGB4VF_ADAPTER_H__
#define __CXGB4VF_ADAPTER_H__

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>

#include "../cxgb4/t4_hw.h"

/*
 * Constants of the implementation.
 */
enum {
	MAX_NPORTS	= 1,		/* max # of "ports" */
	MAX_PORT_QSETS	= 8,		/* max # of Queue Sets / "port" */
	MAX_ETH_QSETS	= MAX_NPORTS*MAX_PORT_QSETS,

	/*
	 * MSI-X interrupt index usage.
	 */
	MSIX_FW		= 0,		/* MSI-X index for firmware Q */
	MSIX_IQFLINT	= 1,		/* MSI-X index base for Ingress Qs */
	MSIX_EXTRAS	= 1,
	MSIX_ENTRIES	= MAX_ETH_QSETS + MSIX_EXTRAS,

	/*
	 * The maximum number of Ingress and Egress Queues is determined by
	 * the maximum number of "Queue Sets" which we support plus any
	 * ancillary queues.  Each "Queue Set" requires one Ingress Queue
	 * for RX Packet Ingress Event notifications and two Egress Queues for
	 * a Free List and an Ethernet TX list.
	 */
	INGQ_EXTRAS	= 2,		/* firmware event queue and */
					/*   forwarded interrupts */
	MAX_INGQ	= MAX_ETH_QSETS+INGQ_EXTRAS,
	MAX_EGRQ	= MAX_ETH_QSETS*2,
};

/*
 * Forward structure definition references.
 */
struct adapter;
struct sge_eth_rxq;
struct sge_rspq;

/*
 * Per-"port" information.  This is really per-Virtual Interface information
 * but the use of the "port" nomanclature makes it easier to go back and forth
 * between the PF and VF drivers ...
 */
struct port_info {
	struct adapter *adapter;	/* our adapter */
	u32 vlan_id;			/* vlan id for VST */
	u16 viid;			/* virtual interface ID */
	int xact_addr_filt;		/* index of our MAC address filter */
	u16 rss_size;			/* size of VI's RSS table slice */
	u8 pidx;			/* index into adapter port[] */
	s8 mdio_addr;
	u8 port_type;			/* firmware port type */
	u8 mod_type;			/* firmware module type */
	u8 port_id;			/* physical port ID */
	u8 nqsets;			/* # of "Queue Sets" */
	u8 first_qset;			/* index of first "Queue Set" */
	struct link_config link_cfg;	/* physical port configuration */
};

/*
 * Scatter Gather Engine resources for the "adapter".  Our ingress and egress
 * queues are organized into "Queue Sets" with one ingress and one egress
 * queue per Queue Set.  These Queue Sets are aportionable between the "ports"
 * (Virtual Interfaces).  One extra ingress queue is used to receive
 * asynchronous messages from the firmware.  Note that the "Queue IDs" that we
 * use here are really "Relative Queue IDs" which are returned as part of the
 * firmware command to allocate queues.  These queue IDs are relative to the
 * absolute Queue ID base of the section of the Queue ID space allocated to
 * the PF/VF.
 */

/*
 * SGE free-list queue state.
 */
struct rx_sw_desc;
struct sge_fl {
	unsigned int avail;		/* # of available RX buffers */
	unsigned int pend_cred;		/* new buffers since last FL DB ring */
	unsigned int cidx;		/* consumer index */
	unsigned int pidx;		/* producer index */
	unsigned long alloc_failed;	/* # of buffer allocation failures */
	unsigned long large_alloc_failed;
	unsigned long starving;		/* # of times FL was found starving */

	/*
	 * Write-once/infrequently fields.
	 * -------------------------------
	 */

	unsigned int cntxt_id;		/* SGE relative QID for the free list */
	unsigned int abs_id;		/* SGE absolute QID for the free list */
	unsigned int size;		/* capacity of free list */
	struct rx_sw_desc *sdesc;	/* address of SW RX descriptor ring */
	__be64 *desc;			/* address of HW RX descriptor ring */
	dma_addr_t addr;		/* PCI bus address of hardware ring */
	void __iomem *bar2_addr;	/* address of BAR2 Queue registers */
	unsigned int bar2_qid;		/* Queue ID for BAR2 Queue registers */
};

/*
 * An ingress packet gather list.
 */
struct pkt_gl {
	struct page_frag frags[MAX_SKB_FRAGS];
	void *va;			/* virtual address of first byte */
	unsigned int nfrags;		/* # of fragments */
	unsigned int tot_len;		/* total length of fragments */
};

typedef int (*rspq_handler_t)(struct sge_rspq *, const __be64 *,
			      const struct pkt_gl *);

/*
 * State for an SGE Response Queue.
 */
struct sge_rspq {
	struct napi_struct napi;	/* NAPI scheduling control */
	const __be64 *cur_desc;		/* current descriptor in queue */
	unsigned int cidx;		/* consumer index */
	u8 gen;				/* current generation bit */
	u8 next_intr_params;		/* holdoff params for next interrupt */
	int offset;			/* offset into current FL buffer */

	unsigned int unhandled_irqs;	/* bogus interrupts */

	/*
	 * Write-once/infrequently fields.
	 * -------------------------------
	 */

	u8 intr_params;			/* interrupt holdoff parameters */
	u8 pktcnt_idx;			/* interrupt packet threshold */
	u8 idx;				/* queue index within its group */
	u16 cntxt_id;			/* SGE rel QID for the response Q */
	u16 abs_id;			/* SGE abs QID for the response Q */
	__be64 *desc;			/* address of hardware response ring */
	dma_addr_t phys_addr;		/* PCI bus address of ring */
	void __iomem *bar2_addr;	/* address of BAR2 Queue registers */
	unsigned int bar2_qid;		/* Queue ID for BAR2 Queue registers */
	unsigned int iqe_len;		/* entry size */
	unsigned int size;		/* capcity of response Q */
	struct adapter *adapter;	/* our adapter */
	struct net_device *netdev;	/* associated net device */
	rspq_handler_t handler;		/* the handler for this response Q */
};

/*
 * Ethernet queue statistics
 */
struct sge_eth_stats {
	unsigned long pkts;		/* # of ethernet packets */
	unsigned long lro_pkts;		/* # of LRO super packets */
	unsigned long lro_merged;	/* # of wire packets merged by LRO */
	unsigned long rx_cso;		/* # of Rx checksum offloads */
	unsigned long vlan_ex;		/* # of Rx VLAN extractions */
	unsigned long rx_drops;		/* # of packets dropped due to no mem */
};

/*
 * State for an Ethernet Receive Queue.
 */
struct sge_eth_rxq {
	struct sge_rspq rspq;		/* Response Queue */
	struct sge_fl fl;		/* Free List */
	struct sge_eth_stats stats;	/* receive statistics */
};

/*
 * SGE Transmit Queue state.  This contains all of the resources associated
 * with the hardware status of a TX Queue which is a circular ring of hardware
 * TX Descriptors.  For convenience, it also contains a pointer to a parallel
 * "Software Descriptor" array but we don't know anything about it here other
 * than its type name.
 */
struct tx_desc {
	/*
	 * Egress Queues are measured in units of SGE_EQ_IDXSIZE by the
	 * hardware: Sizes, Producer and Consumer indices, etc.
	 */
	__be64 flit[SGE_EQ_IDXSIZE/sizeof(__be64)];
};
struct tx_sw_desc;
struct sge_txq {
	unsigned int in_use;		/* # of in-use TX descriptors */
	unsigned int size;		/* # of descriptors */
	unsigned int cidx;		/* SW consumer index */
	unsigned int pidx;		/* producer index */
	unsigned long stops;		/* # of times queue has been stopped */
	unsigned long restarts;		/* # of queue restarts */

	/*
	 * Write-once/infrequently fields.
	 * -------------------------------
	 */

	unsigned int cntxt_id;		/* SGE relative QID for the TX Q */
	unsigned int abs_id;		/* SGE absolute QID for the TX Q */
	struct tx_desc *desc;		/* address of HW TX descriptor ring */
	struct tx_sw_desc *sdesc;	/* address of SW TX descriptor ring */
	struct sge_qstat *stat;		/* queue status entry */
	dma_addr_t phys_addr;		/* PCI bus address of hardware ring */
	void __iomem *bar2_addr;	/* address of BAR2 Queue registers */
	unsigned int bar2_qid;		/* Queue ID for BAR2 Queue registers */
};

/*
 * State for an Ethernet Transmit Queue.
 */
struct sge_eth_txq {
	struct sge_txq q;		/* SGE TX Queue */
	struct netdev_queue *txq;	/* associated netdev TX queue */
	unsigned long tso;		/* # of TSO requests */
	unsigned long tx_cso;		/* # of TX checksum offloads */
	unsigned long vlan_ins;		/* # of TX VLAN insertions */
	unsigned long mapping_err;	/* # of I/O MMU packet mapping errors */
};

/*
 * The complete set of Scatter/Gather Engine resources.
 */
struct sge {
	/*
	 * Our "Queue Sets" ...
	 */
	struct sge_eth_txq ethtxq[MAX_ETH_QSETS];
	struct sge_eth_rxq ethrxq[MAX_ETH_QSETS];

	/*
	 * Extra ingress queues for asynchronous firmware events and
	 * forwarded interrupts (when in MSI mode).
	 */
	struct sge_rspq fw_evtq ____cacheline_aligned_in_smp;

	struct sge_rspq intrq ____cacheline_aligned_in_smp;
	spinlock_t intrq_lock;

	/*
	 * State for managing "starving Free Lists" -- Free Lists which have
	 * fallen below a certain threshold of buffers available to the
	 * hardware and attempts to refill them up to that threshold have
	 * failed.  We have a regular "slow tick" timer process which will
	 * make periodic attempts to refill these starving Free Lists ...
	 */
	DECLARE_BITMAP(starving_fl, MAX_EGRQ);
	struct timer_list rx_timer;

	/*
	 * State for cleaning up completed TX descriptors.
	 */
	struct timer_list tx_timer;

	/*
	 * Write-once/infrequently fields.
	 * -------------------------------
	 */

	u16 max_ethqsets;		/* # of available Ethernet queue sets */
	u16 ethqsets;			/* # of active Ethernet queue sets */
	u16 ethtxq_rover;		/* Tx queue to clean up next */
	u16 timer_val[SGE_NTIMERS];	/* interrupt holdoff timer array */
	u8 counter_val[SGE_NCOUNTERS];	/* interrupt RX threshold array */

	/* Decoded Adapter Parameters.
	 */
	u32 fl_pg_order;		/* large page allocation size */
	u32 stat_len;			/* length of status page at ring end */
	u32 pktshift;			/* padding between CPL & packet data */
	u32 fl_align;			/* response queue message alignment */
	u32 fl_starve_thres;		/* Free List starvation threshold */

	/*
	 * Reverse maps from Absolute Queue IDs to associated queue pointers.
	 * The absolute Queue IDs are in a compact range which start at a
	 * [potentially large] Base Queue ID.  We perform the reverse map by
	 * first converting the Absolute Queue ID into a Relative Queue ID by
	 * subtracting off the Base Queue ID and then use a Relative Queue ID
	 * indexed table to get the pointer to the corresponding software
	 * queue structure.
	 */
	unsigned int egr_base;
	unsigned int ingr_base;
	void *egr_map[MAX_EGRQ];
	struct sge_rspq *ingr_map[MAX_INGQ];
};

/*
 * Utility macros to convert Absolute- to Relative-Queue indices and Egress-
 * and Ingress-Queues.  The EQ_MAP() and IQ_MAP() macros which provide
 * pointers to Ingress- and Egress-Queues can be used as both L- and R-values
 */
#define EQ_IDX(s, abs_id) ((unsigned int)((abs_id) - (s)->egr_base))
#define IQ_IDX(s, abs_id) ((unsigned int)((abs_id) - (s)->ingr_base))

#define EQ_MAP(s, abs_id) ((s)->egr_map[EQ_IDX(s, abs_id)])
#define IQ_MAP(s, abs_id) ((s)->ingr_map[IQ_IDX(s, abs_id)])

/*
 * Macro to iterate across Queue Sets ("rxq" is a historic misnomer).
 */
#define for_each_ethrxq(sge, iter) \
	for (iter = 0; iter < (sge)->ethqsets; iter++)

struct hash_mac_addr {
	struct list_head list;
	u8 addr[ETH_ALEN];
	unsigned int iface_mac;
};

struct mbox_list {
	struct list_head list;
};

/*
 * Per-"adapter" (Virtual Function) information.
 */
struct adapter {
	/* PCI resources */
	void __iomem *regs;
	void __iomem *bar2;
	struct pci_dev *pdev;
	struct device *pdev_dev;

	/* "adapter" resources */
	unsigned long registered_device_map;
	unsigned long open_device_map;
	unsigned long flags;
	struct adapter_params params;

	/* queue and interrupt resources */
	struct {
		unsigned short vec;
		char desc[22];
	} msix_info[MSIX_ENTRIES];
	struct sge sge;

	/* Linux network device resources */
	struct net_device *port[MAX_NPORTS];
	const char *name;
	unsigned int msg_enable;

	/* debugfs resources */
	struct dentry *debugfs_root;

	/* various locks */
	spinlock_t stats_lock;

	/* lock for mailbox cmd list */
	spinlock_t mbox_lock;
	struct mbox_list mlist;

	/* support for mailbox command/reply logging */
#define T4VF_OS_LOG_MBOX_CMDS 256
	struct mbox_cmd_log *mbox_log;

	/* list of MAC addresses in MPS Hash */
	struct list_head mac_hlist;
};

enum { /* adapter flags */
	FULL_INIT_DONE     = (1UL << 0),
	USING_MSI          = (1UL << 1),
	USING_MSIX         = (1UL << 2),
	QUEUES_BOUND       = (1UL << 3),
	ROOT_NO_RELAXED_ORDERING = (1UL << 4),
};

/*
 * The following register read/write routine definitions are required by
 * the common code.
 */

/**
 * t4_read_reg - read a HW register
 * @adapter: the adapter
 * @reg_addr: the register address
 *
 * Returns the 32-bit value of the given HW register.
 */
static inline u32 t4_read_reg(struct adapter *adapter, u32 reg_addr)
{
	return readl(adapter->regs + reg_addr);
}

/**
 * t4_write_reg - write a HW register
 * @adapter: the adapter
 * @reg_addr: the register address
 * @val: the value to write
 *
 * Write a 32-bit value into the given HW register.
 */
static inline void t4_write_reg(struct adapter *adapter, u32 reg_addr, u32 val)
{
	writel(val, adapter->regs + reg_addr);
}

#ifndef readq
static inline u64 readq(const volatile void __iomem *addr)
{
	return readl(addr) + ((u64)readl(addr + 4) << 32);
}

static inline void writeq(u64 val, volatile void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr + 4);
}
#endif

/**
 * t4_read_reg64 - read a 64-bit HW register
 * @adapter: the adapter
 * @reg_addr: the register address
 *
 * Returns the 64-bit value of the given HW register.
 */
static inline u64 t4_read_reg64(struct adapter *adapter, u32 reg_addr)
{
	return readq(adapter->regs + reg_addr);
}

/**
 * t4_write_reg64 - write a 64-bit HW register
 * @adapter: the adapter
 * @reg_addr: the register address
 * @val: the value to write
 *
 * Write a 64-bit value into the given HW register.
 */
static inline void t4_write_reg64(struct adapter *adapter, u32 reg_addr,
				  u64 val)
{
	writeq(val, adapter->regs + reg_addr);
}

/**
 * port_name - return the string name of a port
 * @adapter: the adapter
 * @pidx: the port index
 *
 * Return the string name of the selected port.
 */
static inline const char *port_name(struct adapter *adapter, int pidx)
{
	return adapter->port[pidx]->name;
}

/**
 * t4_os_set_hw_addr - store a port's MAC address in SW
 * @adapter: the adapter
 * @pidx: the port index
 * @hw_addr: the Ethernet address
 *
 * Store the Ethernet address of the given port in SW.  Called by the common
 * code when it retrieves a port's Ethernet address from EEPROM.
 */
static inline void t4_os_set_hw_addr(struct adapter *adapter, int pidx,
				     u8 hw_addr[])
{
	memcpy(adapter->port[pidx]->dev_addr, hw_addr, ETH_ALEN);
}

/**
 * netdev2pinfo - return the port_info structure associated with a net_device
 * @dev: the netdev
 *
 * Return the struct port_info associated with a net_device
 */
static inline struct port_info *netdev2pinfo(const struct net_device *dev)
{
	return netdev_priv(dev);
}

/**
 * adap2pinfo - return the port_info of a port
 * @adap: the adapter
 * @pidx: the port index
 *
 * Return the port_info structure for the adapter.
 */
static inline struct port_info *adap2pinfo(struct adapter *adapter, int pidx)
{
	return netdev_priv(adapter->port[pidx]);
}

/**
 * netdev2adap - return the adapter structure associated with a net_device
 * @dev: the netdev
 *
 * Return the struct adapter associated with a net_device
 */
static inline struct adapter *netdev2adap(const struct net_device *dev)
{
	return netdev2pinfo(dev)->adapter;
}

/*
 * OS "Callback" function declarations.  These are functions that the OS code
 * is "contracted" to provide for the common code.
 */
void t4vf_os_link_changed(struct adapter *, int, int);
void t4vf_os_portmod_changed(struct adapter *, int);

/*
 * SGE function prototype declarations.
 */
int t4vf_sge_alloc_rxq(struct adapter *, struct sge_rspq *, bool,
		       struct net_device *, int,
		       struct sge_fl *, rspq_handler_t);
int t4vf_sge_alloc_eth_txq(struct adapter *, struct sge_eth_txq *,
			   struct net_device *, struct netdev_queue *,
			   unsigned int);
void t4vf_free_sge_resources(struct adapter *);

int t4vf_eth_xmit(struct sk_buff *, struct net_device *);
int t4vf_ethrx_handler(struct sge_rspq *, const __be64 *,
		       const struct pkt_gl *);

irq_handler_t t4vf_intr_handler(struct adapter *);
irqreturn_t t4vf_sge_intr_msix(int, void *);

int t4vf_sge_init(struct adapter *);
void t4vf_sge_start(struct adapter *);
void t4vf_sge_stop(struct adapter *);

#endif /* __CXGB4VF_ADAPTER_H__ */
