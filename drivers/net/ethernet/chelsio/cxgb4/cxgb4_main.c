/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/crc32.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/log2.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/sockios.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <net/neighbour.h>
#include <net/netevent.h>
#include <net/addrconf.h>
#include <net/bonding.h>
#include <linux/uaccess.h>
#include <linux/crash_dump.h>
#include <net/udp_tunnel.h>
#include <net/xfrm.h>
#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)
#include <net/tls.h>
#endif

#include "cxgb4.h"
#include "cxgb4_filter.h"
#include "t4_regs.h"
#include "t4_values.h"
#include "t4_msg.h"
#include "t4fw_api.h"
#include "t4fw_version.h"
#include "cxgb4_dcb.h"
#include "srq.h"
#include "cxgb4_debugfs.h"
#include "clip_tbl.h"
#include "l2t.h"
#include "smt.h"
#include "sched.h"
#include "cxgb4_tc_u32.h"
#include "cxgb4_tc_flower.h"
#include "cxgb4_tc_mqprio.h"
#include "cxgb4_tc_matchall.h"
#include "cxgb4_ptp.h"
#include "cxgb4_cudbg.h"

char cxgb4_driver_name[] = KBUILD_MODNAME;

#define DRV_DESC "Chelsio T4/T5/T6 Network Driver"

#define DFLT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK | \
			 NETIF_MSG_TIMER | NETIF_MSG_IFDOWN | NETIF_MSG_IFUP |\
			 NETIF_MSG_RX_ERR | NETIF_MSG_TX_ERR)

/* Macros needed to support the PCI Device ID Table ...
 */
#define CH_PCI_DEVICE_ID_TABLE_DEFINE_BEGIN \
	static const struct pci_device_id cxgb4_pci_tbl[] = {
#define CXGB4_UNIFIED_PF 0x4

#define CH_PCI_DEVICE_ID_FUNCTION CXGB4_UNIFIED_PF

/* Include PCI Device IDs for both PF4 and PF0-3 so our PCI probe() routine is
 * called for both.
 */
#define CH_PCI_DEVICE_ID_FUNCTION2 0x0

#define CH_PCI_ID_TABLE_ENTRY(devid) \
		{PCI_VDEVICE(CHELSIO, (devid)), CXGB4_UNIFIED_PF}

#define CH_PCI_DEVICE_ID_TABLE_DEFINE_END \
		{ 0, } \
	}

#include "t4_pci_id_tbl.h"

#define FW4_FNAME "cxgb4/t4fw.bin"
#define FW5_FNAME "cxgb4/t5fw.bin"
#define FW6_FNAME "cxgb4/t6fw.bin"
#define FW4_CFNAME "cxgb4/t4-config.txt"
#define FW5_CFNAME "cxgb4/t5-config.txt"
#define FW6_CFNAME "cxgb4/t6-config.txt"
#define PHY_AQ1202_FIRMWARE "cxgb4/aq1202_fw.cld"
#define PHY_BCM84834_FIRMWARE "cxgb4/bcm8483.bin"
#define PHY_AQ1202_DEVICEID 0x4409
#define PHY_BCM84834_DEVICEID 0x4486

MODULE_DESCRIPTION(DRV_DESC);
MODULE_AUTHOR("Chelsio Communications");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DEVICE_TABLE(pci, cxgb4_pci_tbl);
MODULE_FIRMWARE(FW4_FNAME);
MODULE_FIRMWARE(FW5_FNAME);
MODULE_FIRMWARE(FW6_FNAME);

/*
 * The driver uses the best interrupt scheme available on a platform in the
 * order MSI-X, MSI, legacy INTx interrupts.  This parameter determines which
 * of these schemes the driver may consider as follows:
 *
 * msi = 2: choose from among all three options
 * msi = 1: only consider MSI and INTx interrupts
 * msi = 0: force INTx interrupts
 */
static int msi = 2;

module_param(msi, int, 0644);
MODULE_PARM_DESC(msi, "whether to use INTx (0), MSI (1) or MSI-X (2)");

/*
 * Normally we tell the chip to deliver Ingress Packets into our DMA buffers
 * offset by 2 bytes in order to have the IP headers line up on 4-byte
 * boundaries.  This is a requirement for many architectures which will throw
 * a machine check fault if an attempt is made to access one of the 4-byte IP
 * header fields on a non-4-byte boundary.  And it's a major performance issue
 * even on some architectures which allow it like some implementations of the
 * x86 ISA.  However, some architectures don't mind this and for some very
 * edge-case performance sensitive applications (like forwarding large volumes
 * of small packets), setting this DMA offset to 0 will decrease the number of
 * PCI-E Bus transfers enough to measurably affect performance.
 */
static int rx_dma_offset = 2;

/* TX Queue select used to determine what algorithm to use for selecting TX
 * queue. Select between the kernel provided function (select_queue=0) or user
 * cxgb_select_queue function (select_queue=1)
 *
 * Default: select_queue=0
 */
static int select_queue;
module_param(select_queue, int, 0644);
MODULE_PARM_DESC(select_queue,
		 "Select between kernel provided method of selecting or driver method of selecting TX queue. Default is kernel method.");

static struct dentry *cxgb4_debugfs_root;

LIST_HEAD(adapter_list);
DEFINE_MUTEX(uld_mutex);
LIST_HEAD(uld_list);

static int cfg_queues(struct adapter *adap);

static void link_report(struct net_device *dev)
{
	if (!netif_carrier_ok(dev))
		netdev_info(dev, "link down\n");
	else {
		static const char *fc[] = { "no", "Rx", "Tx", "Tx/Rx" };

		const char *s;
		const struct port_info *p = netdev_priv(dev);

		switch (p->link_cfg.speed) {
		case 100:
			s = "100Mbps";
			break;
		case 1000:
			s = "1Gbps";
			break;
		case 10000:
			s = "10Gbps";
			break;
		case 25000:
			s = "25Gbps";
			break;
		case 40000:
			s = "40Gbps";
			break;
		case 50000:
			s = "50Gbps";
			break;
		case 100000:
			s = "100Gbps";
			break;
		default:
			pr_info("%s: unsupported speed: %d\n",
				dev->name, p->link_cfg.speed);
			return;
		}

		netdev_info(dev, "link up, %s, full-duplex, %s PAUSE\n", s,
			    fc[p->link_cfg.fc]);
	}
}

#ifdef CONFIG_CHELSIO_T4_DCB
/* Set up/tear down Data Center Bridging Priority mapping for a net device. */
static void dcb_tx_queue_prio_enable(struct net_device *dev, int enable)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct sge_eth_txq *txq = &adap->sge.ethtxq[pi->first_qset];
	int i;

	/* We use a simple mapping of Port TX Queue Index to DCB
	 * Priority when we're enabling DCB.
	 */
	for (i = 0; i < pi->nqsets; i++, txq++) {
		u32 name, value;
		int err;

		name = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DMAQ) |
			FW_PARAMS_PARAM_X_V(
				FW_PARAMS_PARAM_DMAQ_EQ_DCBPRIO_ETH) |
			FW_PARAMS_PARAM_YZ_V(txq->q.cntxt_id));
		value = enable ? i : 0xffffffff;

		/* Since we can be called while atomic (from "interrupt
		 * level") we need to issue the Set Parameters Commannd
		 * without sleeping (timeout < 0).
		 */
		err = t4_set_params_timeout(adap, adap->mbox, adap->pf, 0, 1,
					    &name, &value,
					    -FW_CMD_MAX_TIMEOUT);

		if (err)
			dev_err(adap->pdev_dev,
				"Can't %s DCB Priority on port %d, TX Queue %d: err=%d\n",
				enable ? "set" : "unset", pi->port_id, i, -err);
		else
			txq->dcb_prio = enable ? value : 0;
	}
}

int cxgb4_dcb_enabled(const struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);

	if (!pi->dcb.enabled)
		return 0;

	return ((pi->dcb.state == CXGB4_DCB_STATE_FW_ALLSYNCED) ||
		(pi->dcb.state == CXGB4_DCB_STATE_HOST));
}
#endif /* CONFIG_CHELSIO_T4_DCB */

void t4_os_link_changed(struct adapter *adapter, int port_id, int link_stat)
{
	struct net_device *dev = adapter->port[port_id];

	/* Skip changes from disabled ports. */
	if (netif_running(dev) && link_stat != netif_carrier_ok(dev)) {
		if (link_stat)
			netif_carrier_on(dev);
		else {
#ifdef CONFIG_CHELSIO_T4_DCB
			if (cxgb4_dcb_enabled(dev)) {
				cxgb4_dcb_reset(dev);
				dcb_tx_queue_prio_enable(dev, false);
			}
#endif /* CONFIG_CHELSIO_T4_DCB */
			netif_carrier_off(dev);
		}

		link_report(dev);
	}
}

void t4_os_portmod_changed(struct adapter *adap, int port_id)
{
	static const char *mod_str[] = {
		NULL, "LR", "SR", "ER", "passive DA", "active DA", "LRM"
	};

	struct net_device *dev = adap->port[port_id];
	struct port_info *pi = netdev_priv(dev);

	if (pi->mod_type == FW_PORT_MOD_TYPE_NONE)
		netdev_info(dev, "port module unplugged\n");
	else if (pi->mod_type < ARRAY_SIZE(mod_str))
		netdev_info(dev, "%s module inserted\n", mod_str[pi->mod_type]);
	else if (pi->mod_type == FW_PORT_MOD_TYPE_NOTSUPPORTED)
		netdev_info(dev, "%s: unsupported port module inserted\n",
			    dev->name);
	else if (pi->mod_type == FW_PORT_MOD_TYPE_UNKNOWN)
		netdev_info(dev, "%s: unknown port module inserted\n",
			    dev->name);
	else if (pi->mod_type == FW_PORT_MOD_TYPE_ERROR)
		netdev_info(dev, "%s: transceiver module error\n", dev->name);
	else
		netdev_info(dev, "%s: unknown module type %d inserted\n",
			    dev->name, pi->mod_type);

	/* If the interface is running, then we'll need any "sticky" Link
	 * Parameters redone with a new Transceiver Module.
	 */
	pi->link_cfg.redo_l1cfg = netif_running(dev);
}

int dbfifo_int_thresh = 10; /* 10 == 640 entry threshold */
module_param(dbfifo_int_thresh, int, 0644);
MODULE_PARM_DESC(dbfifo_int_thresh, "doorbell fifo interrupt threshold");

/*
 * usecs to sleep while draining the dbfifo
 */
static int dbfifo_drain_delay = 1000;
module_param(dbfifo_drain_delay, int, 0644);
MODULE_PARM_DESC(dbfifo_drain_delay,
		 "usecs to sleep while draining the dbfifo");

static inline int cxgb4_set_addr_hash(struct port_info *pi)
{
	struct adapter *adap = pi->adapter;
	u64 vec = 0;
	bool ucast = false;
	struct hash_mac_addr *entry;

	/* Calculate the hash vector for the updated list and program it */
	list_for_each_entry(entry, &adap->mac_hlist, list) {
		ucast |= is_unicast_ether_addr(entry->addr);
		vec |= (1ULL << hash_mac_addr(entry->addr));
	}
	return t4_set_addr_hash(adap, adap->mbox, pi->viid, ucast,
				vec, false);
}

static int cxgb4_mac_sync(struct net_device *netdev, const u8 *mac_addr)
{
	struct port_info *pi = netdev_priv(netdev);
	struct adapter *adap = pi->adapter;
	int ret;
	u64 mhash = 0;
	u64 uhash = 0;
	/* idx stores the index of allocated filters,
	 * its size should be modified based on the number of
	 * MAC addresses that we allocate filters for
	 */

	u16 idx[1] = {};
	bool free = false;
	bool ucast = is_unicast_ether_addr(mac_addr);
	const u8 *maclist[1] = {mac_addr};
	struct hash_mac_addr *new_entry;

	ret = cxgb4_alloc_mac_filt(adap, pi->viid, free, 1, maclist,
				   idx, ucast ? &uhash : &mhash, false);
	if (ret < 0)
		goto out;
	/* if hash != 0, then add the addr to hash addr list
	 * so on the end we will calculate the hash for the
	 * list and program it
	 */
	if (uhash || mhash) {
		new_entry = kzalloc(sizeof(*new_entry), GFP_ATOMIC);
		if (!new_entry)
			return -ENOMEM;
		ether_addr_copy(new_entry->addr, mac_addr);
		list_add_tail(&new_entry->list, &adap->mac_hlist);
		ret = cxgb4_set_addr_hash(pi);
	}
out:
	return ret < 0 ? ret : 0;
}

static int cxgb4_mac_unsync(struct net_device *netdev, const u8 *mac_addr)
{
	struct port_info *pi = netdev_priv(netdev);
	struct adapter *adap = pi->adapter;
	int ret;
	const u8 *maclist[1] = {mac_addr};
	struct hash_mac_addr *entry, *tmp;

	/* If the MAC address to be removed is in the hash addr
	 * list, delete it from the list and update hash vector
	 */
	list_for_each_entry_safe(entry, tmp, &adap->mac_hlist, list) {
		if (ether_addr_equal(entry->addr, mac_addr)) {
			list_del(&entry->list);
			kfree(entry);
			return cxgb4_set_addr_hash(pi);
		}
	}

	ret = cxgb4_free_mac_filt(adap, pi->viid, 1, maclist, false);
	return ret < 0 ? -EINVAL : 0;
}

/*
 * Set Rx properties of a port, such as promiscruity, address filters, and MTU.
 * If @mtu is -1 it is left unchanged.
 */
static int set_rxmode(struct net_device *dev, int mtu, bool sleep_ok)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	__dev_uc_sync(dev, cxgb4_mac_sync, cxgb4_mac_unsync);
	__dev_mc_sync(dev, cxgb4_mac_sync, cxgb4_mac_unsync);

	return t4_set_rxmode(adapter, adapter->mbox, pi->viid, pi->viid_mirror,
			     mtu, (dev->flags & IFF_PROMISC) ? 1 : 0,
			     (dev->flags & IFF_ALLMULTI) ? 1 : 0, 1, -1,
			     sleep_ok);
}

/**
 *	cxgb4_change_mac - Update match filter for a MAC address.
 *	@pi: the port_info
 *	@viid: the VI id
 *	@tcam_idx: TCAM index of existing filter for old value of MAC address,
 *		   or -1
 *	@addr: the new MAC address value
 *	@persist: whether a new MAC allocation should be persistent
 *	@smt_idx: the destination to store the new SMT index.
 *
 *	Modifies an MPS filter and sets it to the new MAC address if
 *	@tcam_idx >= 0, or adds the MAC address to a new filter if
 *	@tcam_idx < 0. In the latter case the address is added persistently
 *	if @persist is %true.
 *	Addresses are programmed to hash region, if tcam runs out of entries.
 *
 */
int cxgb4_change_mac(struct port_info *pi, unsigned int viid,
		     int *tcam_idx, const u8 *addr, bool persist,
		     u8 *smt_idx)
{
	struct adapter *adapter = pi->adapter;
	struct hash_mac_addr *entry, *new_entry;
	int ret;

	ret = t4_change_mac(adapter, adapter->mbox, viid,
			    *tcam_idx, addr, persist, smt_idx);
	/* We ran out of TCAM entries. try programming hash region. */
	if (ret == -ENOMEM) {
		/* If the MAC address to be updated is in the hash addr
		 * list, update it from the list
		 */
		list_for_each_entry(entry, &adapter->mac_hlist, list) {
			if (entry->iface_mac) {
				ether_addr_copy(entry->addr, addr);
				goto set_hash;
			}
		}
		new_entry = kzalloc(sizeof(*new_entry), GFP_KERNEL);
		if (!new_entry)
			return -ENOMEM;
		ether_addr_copy(new_entry->addr, addr);
		new_entry->iface_mac = true;
		list_add_tail(&new_entry->list, &adapter->mac_hlist);
set_hash:
		ret = cxgb4_set_addr_hash(pi);
	} else if (ret >= 0) {
		*tcam_idx = ret;
		ret = 0;
	}

	return ret;
}

/*
 *	link_start - enable a port
 *	@dev: the port to enable
 *
 *	Performs the MAC and PHY actions needed to enable a port.
 */
static int link_start(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	unsigned int mb = pi->adapter->mbox;
	int ret;

	/*
	 * We do not set address filters and promiscuity here, the stack does
	 * that step explicitly.
	 */
	ret = t4_set_rxmode(pi->adapter, mb, pi->viid, pi->viid_mirror,
			    dev->mtu, -1, -1, -1,
			    !!(dev->features & NETIF_F_HW_VLAN_CTAG_RX), true);
	if (ret == 0)
		ret = cxgb4_update_mac_filt(pi, pi->viid, &pi->xact_addr_filt,
					    dev->dev_addr, true, &pi->smt_idx);
	if (ret == 0)
		ret = t4_link_l1cfg(pi->adapter, mb, pi->tx_chan,
				    &pi->link_cfg);
	if (ret == 0) {
		local_bh_disable();
		ret = t4_enable_pi_params(pi->adapter, mb, pi, true,
					  true, CXGB4_DCB_ENABLED);
		local_bh_enable();
	}

	return ret;
}

#ifdef CONFIG_CHELSIO_T4_DCB
/* Handle a Data Center Bridging update message from the firmware. */
static void dcb_rpl(struct adapter *adap, const struct fw_port_cmd *pcmd)
{
	int port = FW_PORT_CMD_PORTID_G(ntohl(pcmd->op_to_portid));
	struct net_device *dev = adap->port[adap->chan_map[port]];
	int old_dcb_enabled = cxgb4_dcb_enabled(dev);
	int new_dcb_enabled;

	cxgb4_dcb_handle_fw_update(adap, pcmd);
	new_dcb_enabled = cxgb4_dcb_enabled(dev);

	/* If the DCB has become enabled or disabled on the port then we're
	 * going to need to set up/tear down DCB Priority parameters for the
	 * TX Queues associated with the port.
	 */
	if (new_dcb_enabled != old_dcb_enabled)
		dcb_tx_queue_prio_enable(dev, new_dcb_enabled);
}
#endif /* CONFIG_CHELSIO_T4_DCB */

/* Response queue handler for the FW event queue.
 */
static int fwevtq_handler(struct sge_rspq *q, const __be64 *rsp,
			  const struct pkt_gl *gl)
{
	u8 opcode = ((const struct rss_header *)rsp)->opcode;

	rsp++;                                          /* skip RSS header */

	/* FW can send EGR_UPDATEs encapsulated in a CPL_FW4_MSG.
	 */
	if (unlikely(opcode == CPL_FW4_MSG &&
	   ((const struct cpl_fw4_msg *)rsp)->type == FW_TYPE_RSSCPL)) {
		rsp++;
		opcode = ((const struct rss_header *)rsp)->opcode;
		rsp++;
		if (opcode != CPL_SGE_EGR_UPDATE) {
			dev_err(q->adap->pdev_dev, "unexpected FW4/CPL %#x on FW event queue\n"
				, opcode);
			goto out;
		}
	}

	if (likely(opcode == CPL_SGE_EGR_UPDATE)) {
		const struct cpl_sge_egr_update *p = (void *)rsp;
		unsigned int qid = EGR_QID_G(ntohl(p->opcode_qid));
		struct sge_txq *txq;

		txq = q->adap->sge.egr_map[qid - q->adap->sge.egr_start];
		txq->restarts++;
		if (txq->q_type == CXGB4_TXQ_ETH) {
			struct sge_eth_txq *eq;

			eq = container_of(txq, struct sge_eth_txq, q);
			t4_sge_eth_txq_egress_update(q->adap, eq, -1);
		} else {
			struct sge_uld_txq *oq;

			oq = container_of(txq, struct sge_uld_txq, q);
			tasklet_schedule(&oq->qresume_tsk);
		}
	} else if (opcode == CPL_FW6_MSG || opcode == CPL_FW4_MSG) {
		const struct cpl_fw6_msg *p = (void *)rsp;

#ifdef CONFIG_CHELSIO_T4_DCB
		const struct fw_port_cmd *pcmd = (const void *)p->data;
		unsigned int cmd = FW_CMD_OP_G(ntohl(pcmd->op_to_portid));
		unsigned int action =
			FW_PORT_CMD_ACTION_G(ntohl(pcmd->action_to_len16));

		if (cmd == FW_PORT_CMD &&
		    (action == FW_PORT_ACTION_GET_PORT_INFO ||
		     action == FW_PORT_ACTION_GET_PORT_INFO32)) {
			int port = FW_PORT_CMD_PORTID_G(
					be32_to_cpu(pcmd->op_to_portid));
			struct net_device *dev;
			int dcbxdis, state_input;

			dev = q->adap->port[q->adap->chan_map[port]];
			dcbxdis = (action == FW_PORT_ACTION_GET_PORT_INFO
			  ? !!(pcmd->u.info.dcbxdis_pkd & FW_PORT_CMD_DCBXDIS_F)
			  : !!(be32_to_cpu(pcmd->u.info32.lstatus32_to_cbllen32)
			       & FW_PORT_CMD_DCBXDIS32_F));
			state_input = (dcbxdis
				       ? CXGB4_DCB_INPUT_FW_DISABLED
				       : CXGB4_DCB_INPUT_FW_ENABLED);

			cxgb4_dcb_state_fsm(dev, state_input);
		}

		if (cmd == FW_PORT_CMD &&
		    action == FW_PORT_ACTION_L2_DCB_CFG)
			dcb_rpl(q->adap, pcmd);
		else
#endif
			if (p->type == 0)
				t4_handle_fw_rpl(q->adap, p->data);
	} else if (opcode == CPL_L2T_WRITE_RPL) {
		const struct cpl_l2t_write_rpl *p = (void *)rsp;

		do_l2t_write_rpl(q->adap, p);
	} else if (opcode == CPL_SMT_WRITE_RPL) {
		const struct cpl_smt_write_rpl *p = (void *)rsp;

		do_smt_write_rpl(q->adap, p);
	} else if (opcode == CPL_SET_TCB_RPL) {
		const struct cpl_set_tcb_rpl *p = (void *)rsp;

		filter_rpl(q->adap, p);
	} else if (opcode == CPL_ACT_OPEN_RPL) {
		const struct cpl_act_open_rpl *p = (void *)rsp;

		hash_filter_rpl(q->adap, p);
	} else if (opcode == CPL_ABORT_RPL_RSS) {
		const struct cpl_abort_rpl_rss *p = (void *)rsp;

		hash_del_filter_rpl(q->adap, p);
	} else if (opcode == CPL_SRQ_TABLE_RPL) {
		const struct cpl_srq_table_rpl *p = (void *)rsp;

		do_srq_table_rpl(q->adap, p);
	} else
		dev_err(q->adap->pdev_dev,
			"unexpected CPL %#x on FW event queue\n", opcode);
out:
	return 0;
}

static void disable_msi(struct adapter *adapter)
{
	if (adapter->flags & CXGB4_USING_MSIX) {
		pci_disable_msix(adapter->pdev);
		adapter->flags &= ~CXGB4_USING_MSIX;
	} else if (adapter->flags & CXGB4_USING_MSI) {
		pci_disable_msi(adapter->pdev);
		adapter->flags &= ~CXGB4_USING_MSI;
	}
}

/*
 * Interrupt handler for non-data events used with MSI-X.
 */
static irqreturn_t t4_nondata_intr(int irq, void *cookie)
{
	struct adapter *adap = cookie;
	u32 v = t4_read_reg(adap, MYPF_REG(PL_PF_INT_CAUSE_A));

	if (v & PFSW_F) {
		adap->swintr = 1;
		t4_write_reg(adap, MYPF_REG(PL_PF_INT_CAUSE_A), v);
	}
	if (adap->flags & CXGB4_MASTER_PF)
		t4_slow_intr_handler(adap);
	return IRQ_HANDLED;
}

int cxgb4_set_msix_aff(struct adapter *adap, unsigned short vec,
		       cpumask_var_t *aff_mask, int idx)
{
	int rv;

	if (!zalloc_cpumask_var(aff_mask, GFP_KERNEL)) {
		dev_err(adap->pdev_dev, "alloc_cpumask_var failed\n");
		return -ENOMEM;
	}

	cpumask_set_cpu(cpumask_local_spread(idx, dev_to_node(adap->pdev_dev)),
			*aff_mask);

	rv = irq_set_affinity_hint(vec, *aff_mask);
	if (rv)
		dev_warn(adap->pdev_dev,
			 "irq_set_affinity_hint %u failed %d\n",
			 vec, rv);

	return 0;
}

void cxgb4_clear_msix_aff(unsigned short vec, cpumask_var_t aff_mask)
{
	irq_set_affinity_hint(vec, NULL);
	free_cpumask_var(aff_mask);
}

static int request_msix_queue_irqs(struct adapter *adap)
{
	struct sge *s = &adap->sge;
	struct msix_info *minfo;
	int err, ethqidx;

	if (s->fwevtq_msix_idx < 0)
		return -ENOMEM;

	err = request_irq(adap->msix_info[s->fwevtq_msix_idx].vec,
			  t4_sge_intr_msix, 0,
			  adap->msix_info[s->fwevtq_msix_idx].desc,
			  &s->fw_evtq);
	if (err)
		return err;

	for_each_ethrxq(s, ethqidx) {
		minfo = s->ethrxq[ethqidx].msix;
		err = request_irq(minfo->vec,
				  t4_sge_intr_msix, 0,
				  minfo->desc,
				  &s->ethrxq[ethqidx].rspq);
		if (err)
			goto unwind;

		cxgb4_set_msix_aff(adap, minfo->vec,
				   &minfo->aff_mask, ethqidx);
	}
	return 0;

unwind:
	while (--ethqidx >= 0) {
		minfo = s->ethrxq[ethqidx].msix;
		cxgb4_clear_msix_aff(minfo->vec, minfo->aff_mask);
		free_irq(minfo->vec, &s->ethrxq[ethqidx].rspq);
	}
	free_irq(adap->msix_info[s->fwevtq_msix_idx].vec, &s->fw_evtq);
	return err;
}

static void free_msix_queue_irqs(struct adapter *adap)
{
	struct sge *s = &adap->sge;
	struct msix_info *minfo;
	int i;

	free_irq(adap->msix_info[s->fwevtq_msix_idx].vec, &s->fw_evtq);
	for_each_ethrxq(s, i) {
		minfo = s->ethrxq[i].msix;
		cxgb4_clear_msix_aff(minfo->vec, minfo->aff_mask);
		free_irq(minfo->vec, &s->ethrxq[i].rspq);
	}
}

static int setup_ppod_edram(struct adapter *adap)
{
	unsigned int param, val;
	int ret;

	/* Driver sends FW_PARAMS_PARAM_DEV_PPOD_EDRAM read command to check
	 * if firmware supports ppod edram feature or not. If firmware
	 * returns 1, then driver can enable this feature by sending
	 * FW_PARAMS_PARAM_DEV_PPOD_EDRAM write command with value 1 to
	 * enable ppod edram feature.
	 */
	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_PPOD_EDRAM));

	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 1, &param, &val);
	if (ret < 0) {
		dev_warn(adap->pdev_dev,
			 "querying PPOD_EDRAM support failed: %d\n",
			 ret);
		return -1;
	}

	if (val != 1)
		return -1;

	ret = t4_set_params(adap, adap->mbox, adap->pf, 0, 1, &param, &val);
	if (ret < 0) {
		dev_err(adap->pdev_dev,
			"setting PPOD_EDRAM failed: %d\n", ret);
		return -1;
	}
	return 0;
}

static void adap_config_hpfilter(struct adapter *adapter)
{
	u32 param, val = 0;
	int ret;

	/* Enable HP filter region. Older fw will fail this request and
	 * it is fine.
	 */
	param = FW_PARAM_DEV(HPFILTER_REGION_SUPPORT);
	ret = t4_set_params(adapter, adapter->mbox, adapter->pf, 0,
			    1, &param, &val);

	/* An error means FW doesn't know about HP filter support,
	 * it's not a problem, don't return an error.
	 */
	if (ret < 0)
		dev_err(adapter->pdev_dev,
			"HP filter region isn't supported by FW\n");
}

static int cxgb4_config_rss(const struct port_info *pi, u16 *rss,
			    u16 rss_size, u16 viid)
{
	struct adapter *adap = pi->adapter;
	int ret;

	ret = t4_config_rss_range(adap, adap->mbox, viid, 0, rss_size, rss,
				  rss_size);
	if (ret)
		return ret;

	/* If Tunnel All Lookup isn't specified in the global RSS
	 * Configuration, then we need to specify a default Ingress
	 * Queue for any ingress packets which aren't hashed.  We'll
	 * use our first ingress queue ...
	 */
	return t4_config_vi_rss(adap, adap->mbox, viid,
				FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN_F |
				FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN_F |
				FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN_F |
				FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN_F |
				FW_RSS_VI_CONFIG_CMD_UDPEN_F,
				rss[0]);
}

/**
 *	cxgb4_write_rss - write the RSS table for a given port
 *	@pi: the port
 *	@queues: array of queue indices for RSS
 *
 *	Sets up the portion of the HW RSS table for the port's VI to distribute
 *	packets to the Rx queues in @queues.
 *	Should never be called before setting up sge eth rx queues
 */
int cxgb4_write_rss(const struct port_info *pi, const u16 *queues)
{
	struct adapter *adapter = pi->adapter;
	const struct sge_eth_rxq *rxq;
	int i, err;
	u16 *rss;

	rxq = &adapter->sge.ethrxq[pi->first_qset];
	rss = kmalloc_array(pi->rss_size, sizeof(u16), GFP_KERNEL);
	if (!rss)
		return -ENOMEM;

	/* map the queue indices to queue ids */
	for (i = 0; i < pi->rss_size; i++, queues++)
		rss[i] = rxq[*queues].rspq.abs_id;

	err = cxgb4_config_rss(pi, rss, pi->rss_size, pi->viid);
	kfree(rss);
	return err;
}

/**
 *	setup_rss - configure RSS
 *	@adap: the adapter
 *
 *	Sets up RSS for each port.
 */
static int setup_rss(struct adapter *adap)
{
	int i, j, err;

	for_each_port(adap, i) {
		const struct port_info *pi = adap2pinfo(adap, i);

		/* Fill default values with equal distribution */
		for (j = 0; j < pi->rss_size; j++)
			pi->rss[j] = j % pi->nqsets;

		err = cxgb4_write_rss(pi, pi->rss);
		if (err)
			return err;
	}
	return 0;
}

/*
 * Return the channel of the ingress queue with the given qid.
 */
static unsigned int rxq_to_chan(const struct sge *p, unsigned int qid)
{
	qid -= p->ingr_start;
	return netdev2pinfo(p->ingr_map[qid]->netdev)->tx_chan;
}

void cxgb4_quiesce_rx(struct sge_rspq *q)
{
	if (q->handler)
		napi_disable(&q->napi);
}

/*
 * Wait until all NAPI handlers are descheduled.
 */
static void quiesce_rx(struct adapter *adap)
{
	int i;

	for (i = 0; i < adap->sge.ingr_sz; i++) {
		struct sge_rspq *q = adap->sge.ingr_map[i];

		if (!q)
			continue;

		cxgb4_quiesce_rx(q);
	}
}

/* Disable interrupt and napi handler */
static void disable_interrupts(struct adapter *adap)
{
	struct sge *s = &adap->sge;

	if (adap->flags & CXGB4_FULL_INIT_DONE) {
		t4_intr_disable(adap);
		if (adap->flags & CXGB4_USING_MSIX) {
			free_msix_queue_irqs(adap);
			free_irq(adap->msix_info[s->nd_msix_idx].vec,
				 adap);
		} else {
			free_irq(adap->pdev->irq, adap);
		}
		quiesce_rx(adap);
	}
}

void cxgb4_enable_rx(struct adapter *adap, struct sge_rspq *q)
{
	if (q->handler)
		napi_enable(&q->napi);

	/* 0-increment GTS to start the timer and enable interrupts */
	t4_write_reg(adap, MYPF_REG(SGE_PF_GTS_A),
		     SEINTARM_V(q->intr_params) |
		     INGRESSQID_V(q->cntxt_id));
}

/*
 * Enable NAPI scheduling and interrupt generation for all Rx queues.
 */
static void enable_rx(struct adapter *adap)
{
	int i;

	for (i = 0; i < adap->sge.ingr_sz; i++) {
		struct sge_rspq *q = adap->sge.ingr_map[i];

		if (!q)
			continue;

		cxgb4_enable_rx(adap, q);
	}
}

static int setup_non_data_intr(struct adapter *adap)
{
	int msix;

	adap->sge.nd_msix_idx = -1;
	if (!(adap->flags & CXGB4_USING_MSIX))
		return 0;

	/* Request MSI-X vector for non-data interrupt */
	msix = cxgb4_get_msix_idx_from_bmap(adap);
	if (msix < 0)
		return -ENOMEM;

	snprintf(adap->msix_info[msix].desc,
		 sizeof(adap->msix_info[msix].desc),
		 "%s", adap->port[0]->name);

	adap->sge.nd_msix_idx = msix;
	return 0;
}

static int setup_fw_sge_queues(struct adapter *adap)
{
	struct sge *s = &adap->sge;
	int msix, err = 0;

	bitmap_zero(s->starving_fl, s->egr_sz);
	bitmap_zero(s->txq_maperr, s->egr_sz);

	if (adap->flags & CXGB4_USING_MSIX) {
		s->fwevtq_msix_idx = -1;
		msix = cxgb4_get_msix_idx_from_bmap(adap);
		if (msix < 0)
			return -ENOMEM;

		snprintf(adap->msix_info[msix].desc,
			 sizeof(adap->msix_info[msix].desc),
			 "%s-FWeventq", adap->port[0]->name);
	} else {
		err = t4_sge_alloc_rxq(adap, &s->intrq, false, adap->port[0], 0,
				       NULL, NULL, NULL, -1);
		if (err)
			return err;
		msix = -((int)s->intrq.abs_id + 1);
	}

	err = t4_sge_alloc_rxq(adap, &s->fw_evtq, true, adap->port[0],
			       msix, NULL, fwevtq_handler, NULL, -1);
	if (err && msix >= 0)
		cxgb4_free_msix_idx_in_bmap(adap, msix);

	s->fwevtq_msix_idx = msix;
	return err;
}

/**
 *	setup_sge_queues - configure SGE Tx/Rx/response queues
 *	@adap: the adapter
 *
 *	Determines how many sets of SGE queues to use and initializes them.
 *	We support multiple queue sets per port if we have MSI-X, otherwise
 *	just one queue set per port.
 */
static int setup_sge_queues(struct adapter *adap)
{
	struct sge_uld_rxq_info *rxq_info = NULL;
	struct sge *s = &adap->sge;
	unsigned int cmplqid = 0;
	int err, i, j, msix = 0;

	if (is_uld(adap))
		rxq_info = s->uld_rxq_info[CXGB4_ULD_RDMA];

	if (!(adap->flags & CXGB4_USING_MSIX))
		msix = -((int)s->intrq.abs_id + 1);

	for_each_port(adap, i) {
		struct net_device *dev = adap->port[i];
		struct port_info *pi = netdev_priv(dev);
		struct sge_eth_rxq *q = &s->ethrxq[pi->first_qset];
		struct sge_eth_txq *t = &s->ethtxq[pi->first_qset];

		for (j = 0; j < pi->nqsets; j++, q++) {
			if (msix >= 0) {
				msix = cxgb4_get_msix_idx_from_bmap(adap);
				if (msix < 0) {
					err = msix;
					goto freeout;
				}

				snprintf(adap->msix_info[msix].desc,
					 sizeof(adap->msix_info[msix].desc),
					 "%s-Rx%d", dev->name, j);
				q->msix = &adap->msix_info[msix];
			}

			err = t4_sge_alloc_rxq(adap, &q->rspq, false, dev,
					       msix, &q->fl,
					       t4_ethrx_handler,
					       NULL,
					       t4_get_tp_ch_map(adap,
								pi->tx_chan));
			if (err)
				goto freeout;
			q->rspq.idx = j;
			memset(&q->stats, 0, sizeof(q->stats));
		}

		q = &s->ethrxq[pi->first_qset];
		for (j = 0; j < pi->nqsets; j++, t++, q++) {
			err = t4_sge_alloc_eth_txq(adap, t, dev,
					netdev_get_tx_queue(dev, j),
					q->rspq.cntxt_id,
					!!(adap->flags & CXGB4_SGE_DBQ_TIMER));
			if (err)
				goto freeout;
		}
	}

	for_each_port(adap, i) {
		/* Note that cmplqid below is 0 if we don't
		 * have RDMA queues, and that's the right value.
		 */
		if (rxq_info)
			cmplqid	= rxq_info->uldrxq[i].rspq.cntxt_id;

		err = t4_sge_alloc_ctrl_txq(adap, &s->ctrlq[i], adap->port[i],
					    s->fw_evtq.cntxt_id, cmplqid);
		if (err)
			goto freeout;
	}

	if (!is_t4(adap->params.chip)) {
		err = t4_sge_alloc_eth_txq(adap, &s->ptptxq, adap->port[0],
					   netdev_get_tx_queue(adap->port[0], 0)
					   , s->fw_evtq.cntxt_id, false);
		if (err)
			goto freeout;
	}

	t4_write_reg(adap, is_t4(adap->params.chip) ?
				MPS_TRC_RSS_CONTROL_A :
				MPS_T5_TRC_RSS_CONTROL_A,
		     RSSCONTROL_V(netdev2pinfo(adap->port[0])->tx_chan) |
		     QUEUENUMBER_V(s->ethrxq[0].rspq.abs_id));
	return 0;
freeout:
	dev_err(adap->pdev_dev, "Can't allocate queues, err=%d\n", -err);
	t4_free_sge_resources(adap);
	return err;
}

static u16 cxgb_select_queue(struct net_device *dev, struct sk_buff *skb,
			     struct net_device *sb_dev)
{
	int txq;

#ifdef CONFIG_CHELSIO_T4_DCB
	/* If a Data Center Bridging has been successfully negotiated on this
	 * link then we'll use the skb's priority to map it to a TX Queue.
	 * The skb's priority is determined via the VLAN Tag Priority Code
	 * Point field.
	 */
	if (cxgb4_dcb_enabled(dev) && !is_kdump_kernel()) {
		u16 vlan_tci;
		int err;

		err = vlan_get_tag(skb, &vlan_tci);
		if (unlikely(err)) {
			if (net_ratelimit())
				netdev_warn(dev,
					    "TX Packet without VLAN Tag on DCB Link\n");
			txq = 0;
		} else {
			txq = (vlan_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
#ifdef CONFIG_CHELSIO_T4_FCOE
			if (skb->protocol == htons(ETH_P_FCOE))
				txq = skb->priority & 0x7;
#endif /* CONFIG_CHELSIO_T4_FCOE */
		}
		return txq;
	}
#endif /* CONFIG_CHELSIO_T4_DCB */

	if (dev->num_tc) {
		struct port_info *pi = netdev2pinfo(dev);
		u8 ver, proto;

		ver = ip_hdr(skb)->version;
		proto = (ver == 6) ? ipv6_hdr(skb)->nexthdr :
				     ip_hdr(skb)->protocol;

		/* Send unsupported traffic pattern to normal NIC queues. */
		txq = netdev_pick_tx(dev, skb, sb_dev);
		if (xfrm_offload(skb) || is_ptp_enabled(skb, dev) ||
		    skb->encapsulation ||
		    cxgb4_is_ktls_skb(skb) ||
		    (proto != IPPROTO_TCP && proto != IPPROTO_UDP))
			txq = txq % pi->nqsets;

		return txq;
	}

	if (select_queue) {
		txq = (skb_rx_queue_recorded(skb)
			? skb_get_rx_queue(skb)
			: smp_processor_id());

		while (unlikely(txq >= dev->real_num_tx_queues))
			txq -= dev->real_num_tx_queues;

		return txq;
	}

	return netdev_pick_tx(dev, skb, NULL) % dev->real_num_tx_queues;
}

static int closest_timer(const struct sge *s, int time)
{
	int i, delta, match = 0, min_delta = INT_MAX;

	for (i = 0; i < ARRAY_SIZE(s->timer_val); i++) {
		delta = time - s->timer_val[i];
		if (delta < 0)
			delta = -delta;
		if (delta < min_delta) {
			min_delta = delta;
			match = i;
		}
	}
	return match;
}

static int closest_thres(const struct sge *s, int thres)
{
	int i, delta, match = 0, min_delta = INT_MAX;

	for (i = 0; i < ARRAY_SIZE(s->counter_val); i++) {
		delta = thres - s->counter_val[i];
		if (delta < 0)
			delta = -delta;
		if (delta < min_delta) {
			min_delta = delta;
			match = i;
		}
	}
	return match;
}

/**
 *	cxgb4_set_rspq_intr_params - set a queue's interrupt holdoff parameters
 *	@q: the Rx queue
 *	@us: the hold-off time in us, or 0 to disable timer
 *	@cnt: the hold-off packet count, or 0 to disable counter
 *
 *	Sets an Rx queue's interrupt hold-off time and packet count.  At least
 *	one of the two needs to be enabled for the queue to generate interrupts.
 */
int cxgb4_set_rspq_intr_params(struct sge_rspq *q,
			       unsigned int us, unsigned int cnt)
{
	struct adapter *adap = q->adap;

	if ((us | cnt) == 0)
		cnt = 1;

	if (cnt) {
		int err;
		u32 v, new_idx;

		new_idx = closest_thres(&adap->sge, cnt);
		if (q->desc && q->pktcnt_idx != new_idx) {
			/* the queue has already been created, update it */
			v = FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DMAQ) |
			    FW_PARAMS_PARAM_X_V(
					FW_PARAMS_PARAM_DMAQ_IQ_INTCNTTHRESH) |
			    FW_PARAMS_PARAM_YZ_V(q->cntxt_id);
			err = t4_set_params(adap, adap->mbox, adap->pf, 0, 1,
					    &v, &new_idx);
			if (err)
				return err;
		}
		q->pktcnt_idx = new_idx;
	}

	us = us == 0 ? 6 : closest_timer(&adap->sge, us);
	q->intr_params = QINTR_TIMER_IDX_V(us) | QINTR_CNT_EN_V(cnt > 0);
	return 0;
}

static int cxgb_set_features(struct net_device *dev, netdev_features_t features)
{
	netdev_features_t changed = dev->features ^ features;
	const struct port_info *pi = netdev_priv(dev);
	int err;

	if (!(changed & NETIF_F_HW_VLAN_CTAG_RX))
		return 0;

	err = t4_set_rxmode(pi->adapter, pi->adapter->mbox, pi->viid,
			    pi->viid_mirror, -1, -1, -1, -1,
			    !!(features & NETIF_F_HW_VLAN_CTAG_RX), true);
	if (unlikely(err))
		dev->features = features ^ NETIF_F_HW_VLAN_CTAG_RX;
	return err;
}

static int setup_debugfs(struct adapter *adap)
{
	if (IS_ERR_OR_NULL(adap->debugfs_root))
		return -1;

#ifdef CONFIG_DEBUG_FS
	t4_setup_debugfs(adap);
#endif
	return 0;
}

static void cxgb4_port_mirror_free_rxq(struct adapter *adap,
				       struct sge_eth_rxq *mirror_rxq)
{
	if ((adap->flags & CXGB4_FULL_INIT_DONE) &&
	    !(adap->flags & CXGB4_SHUTTING_DOWN))
		cxgb4_quiesce_rx(&mirror_rxq->rspq);

	if (adap->flags & CXGB4_USING_MSIX) {
		cxgb4_clear_msix_aff(mirror_rxq->msix->vec,
				     mirror_rxq->msix->aff_mask);
		free_irq(mirror_rxq->msix->vec, &mirror_rxq->rspq);
		cxgb4_free_msix_idx_in_bmap(adap, mirror_rxq->msix->idx);
	}

	free_rspq_fl(adap, &mirror_rxq->rspq, &mirror_rxq->fl);
}

static int cxgb4_port_mirror_alloc_queues(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	struct sge_eth_rxq *mirror_rxq;
	struct sge *s = &adap->sge;
	int ret = 0, msix = 0;
	u16 i, rxqid;
	u16 *rss;

	if (!pi->vi_mirror_count)
		return 0;

	if (s->mirror_rxq[pi->port_id])
		return 0;

	mirror_rxq = kcalloc(pi->nmirrorqsets, sizeof(*mirror_rxq), GFP_KERNEL);
	if (!mirror_rxq)
		return -ENOMEM;

	s->mirror_rxq[pi->port_id] = mirror_rxq;

	if (!(adap->flags & CXGB4_USING_MSIX))
		msix = -((int)adap->sge.intrq.abs_id + 1);

	for (i = 0, rxqid = 0; i < pi->nmirrorqsets; i++, rxqid++) {
		mirror_rxq = &s->mirror_rxq[pi->port_id][i];

		/* Allocate Mirror Rxqs */
		if (msix >= 0) {
			msix = cxgb4_get_msix_idx_from_bmap(adap);
			if (msix < 0) {
				ret = msix;
				goto out_free_queues;
			}

			mirror_rxq->msix = &adap->msix_info[msix];
			snprintf(mirror_rxq->msix->desc,
				 sizeof(mirror_rxq->msix->desc),
				 "%s-mirrorrxq%d", dev->name, i);
		}

		init_rspq(adap, &mirror_rxq->rspq,
			  CXGB4_MIRROR_RXQ_DEFAULT_INTR_USEC,
			  CXGB4_MIRROR_RXQ_DEFAULT_PKT_CNT,
			  CXGB4_MIRROR_RXQ_DEFAULT_DESC_NUM,
			  CXGB4_MIRROR_RXQ_DEFAULT_DESC_SIZE);

		mirror_rxq->fl.size = CXGB4_MIRROR_FLQ_DEFAULT_DESC_NUM;

		ret = t4_sge_alloc_rxq(adap, &mirror_rxq->rspq, false,
				       dev, msix, &mirror_rxq->fl,
				       t4_ethrx_handler, NULL, 0);
		if (ret)
			goto out_free_msix_idx;

		/* Setup MSI-X vectors for Mirror Rxqs */
		if (adap->flags & CXGB4_USING_MSIX) {
			ret = request_irq(mirror_rxq->msix->vec,
					  t4_sge_intr_msix, 0,
					  mirror_rxq->msix->desc,
					  &mirror_rxq->rspq);
			if (ret)
				goto out_free_rxq;

			cxgb4_set_msix_aff(adap, mirror_rxq->msix->vec,
					   &mirror_rxq->msix->aff_mask, i);
		}

		/* Start NAPI for Mirror Rxqs */
		cxgb4_enable_rx(adap, &mirror_rxq->rspq);
	}

	/* Setup RSS for Mirror Rxqs */
	rss = kcalloc(pi->rss_size, sizeof(u16), GFP_KERNEL);
	if (!rss) {
		ret = -ENOMEM;
		goto out_free_queues;
	}

	mirror_rxq = &s->mirror_rxq[pi->port_id][0];
	for (i = 0; i < pi->rss_size; i++)
		rss[i] = mirror_rxq[i % pi->nmirrorqsets].rspq.abs_id;

	ret = cxgb4_config_rss(pi, rss, pi->rss_size, pi->viid_mirror);
	kfree(rss);
	if (ret)
		goto out_free_queues;

	return 0;

out_free_rxq:
	free_rspq_fl(adap, &mirror_rxq->rspq, &mirror_rxq->fl);

out_free_msix_idx:
	cxgb4_free_msix_idx_in_bmap(adap, mirror_rxq->msix->idx);

out_free_queues:
	while (rxqid-- > 0)
		cxgb4_port_mirror_free_rxq(adap,
					   &s->mirror_rxq[pi->port_id][rxqid]);

	kfree(s->mirror_rxq[pi->port_id]);
	s->mirror_rxq[pi->port_id] = NULL;
	return ret;
}

static void cxgb4_port_mirror_free_queues(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	struct sge *s = &adap->sge;
	u16 i;

	if (!pi->vi_mirror_count)
		return;

	if (!s->mirror_rxq[pi->port_id])
		return;

	for (i = 0; i < pi->nmirrorqsets; i++)
		cxgb4_port_mirror_free_rxq(adap,
					   &s->mirror_rxq[pi->port_id][i]);

	kfree(s->mirror_rxq[pi->port_id]);
	s->mirror_rxq[pi->port_id] = NULL;
}

static int cxgb4_port_mirror_start(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	int ret, idx = -1;

	if (!pi->vi_mirror_count)
		return 0;

	/* Mirror VIs can be created dynamically after stack had
	 * already setup Rx modes like MTU, promisc, allmulti, etc.
	 * on main VI. So, parse what the stack had setup on the
	 * main VI and update the same on the mirror VI.
	 */
	ret = t4_set_rxmode(adap, adap->mbox, pi->viid, pi->viid_mirror,
			    dev->mtu, (dev->flags & IFF_PROMISC) ? 1 : 0,
			    (dev->flags & IFF_ALLMULTI) ? 1 : 0, 1,
			    !!(dev->features & NETIF_F_HW_VLAN_CTAG_RX), true);
	if (ret) {
		dev_err(adap->pdev_dev,
			"Failed start up Rx mode for Mirror VI 0x%x, ret: %d\n",
			pi->viid_mirror, ret);
		return ret;
	}

	/* Enable replication bit for the device's MAC address
	 * in MPS TCAM, so that the packets for the main VI are
	 * replicated to mirror VI.
	 */
	ret = cxgb4_update_mac_filt(pi, pi->viid_mirror, &idx,
				    dev->dev_addr, true, NULL);
	if (ret) {
		dev_err(adap->pdev_dev,
			"Failed updating MAC filter for Mirror VI 0x%x, ret: %d\n",
			pi->viid_mirror, ret);
		return ret;
	}

	/* Enabling a Virtual Interface can result in an interrupt
	 * during the processing of the VI Enable command and, in some
	 * paths, result in an attempt to issue another command in the
	 * interrupt context. Thus, we disable interrupts during the
	 * course of the VI Enable command ...
	 */
	local_bh_disable();
	ret = t4_enable_vi_params(adap, adap->mbox, pi->viid_mirror, true, true,
				  false);
	local_bh_enable();
	if (ret)
		dev_err(adap->pdev_dev,
			"Failed starting Mirror VI 0x%x, ret: %d\n",
			pi->viid_mirror, ret);

	return ret;
}

static void cxgb4_port_mirror_stop(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);

	if (!pi->vi_mirror_count)
		return;

	t4_enable_vi_params(adap, adap->mbox, pi->viid_mirror, false, false,
			    false);
}

int cxgb4_port_mirror_alloc(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	int ret = 0;

	if (!pi->nmirrorqsets)
		return -EOPNOTSUPP;

	mutex_lock(&pi->vi_mirror_mutex);
	if (pi->viid_mirror) {
		pi->vi_mirror_count++;
		goto out_unlock;
	}

	ret = t4_init_port_mirror(pi, adap->mbox, pi->port_id, adap->pf, 0,
				  &pi->viid_mirror);
	if (ret)
		goto out_unlock;

	pi->vi_mirror_count = 1;

	if (adap->flags & CXGB4_FULL_INIT_DONE) {
		ret = cxgb4_port_mirror_alloc_queues(dev);
		if (ret)
			goto out_free_vi;

		ret = cxgb4_port_mirror_start(dev);
		if (ret)
			goto out_free_queues;
	}

	mutex_unlock(&pi->vi_mirror_mutex);
	return 0;

out_free_queues:
	cxgb4_port_mirror_free_queues(dev);

out_free_vi:
	pi->vi_mirror_count = 0;
	t4_free_vi(adap, adap->mbox, adap->pf, 0, pi->viid_mirror);
	pi->viid_mirror = 0;

out_unlock:
	mutex_unlock(&pi->vi_mirror_mutex);
	return ret;
}

void cxgb4_port_mirror_free(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);

	mutex_lock(&pi->vi_mirror_mutex);
	if (!pi->viid_mirror)
		goto out_unlock;

	if (pi->vi_mirror_count > 1) {
		pi->vi_mirror_count--;
		goto out_unlock;
	}

	cxgb4_port_mirror_stop(dev);
	cxgb4_port_mirror_free_queues(dev);

	pi->vi_mirror_count = 0;
	t4_free_vi(adap, adap->mbox, adap->pf, 0, pi->viid_mirror);
	pi->viid_mirror = 0;

out_unlock:
	mutex_unlock(&pi->vi_mirror_mutex);
}

/*
 * upper-layer driver support
 */

/*
 * Allocate an active-open TID and set it to the supplied value.
 */
int cxgb4_alloc_atid(struct tid_info *t, void *data)
{
	int atid = -1;

	spin_lock_bh(&t->atid_lock);
	if (t->afree) {
		union aopen_entry *p = t->afree;

		atid = (p - t->atid_tab) + t->atid_base;
		t->afree = p->next;
		p->data = data;
		t->atids_in_use++;
	}
	spin_unlock_bh(&t->atid_lock);
	return atid;
}
EXPORT_SYMBOL(cxgb4_alloc_atid);

/*
 * Release an active-open TID.
 */
void cxgb4_free_atid(struct tid_info *t, unsigned int atid)
{
	union aopen_entry *p = &t->atid_tab[atid - t->atid_base];

	spin_lock_bh(&t->atid_lock);
	p->next = t->afree;
	t->afree = p;
	t->atids_in_use--;
	spin_unlock_bh(&t->atid_lock);
}
EXPORT_SYMBOL(cxgb4_free_atid);

/*
 * Allocate a server TID and set it to the supplied value.
 */
int cxgb4_alloc_stid(struct tid_info *t, int family, void *data)
{
	int stid;

	spin_lock_bh(&t->stid_lock);
	if (family == PF_INET) {
		stid = find_first_zero_bit(t->stid_bmap, t->nstids);
		if (stid < t->nstids)
			__set_bit(stid, t->stid_bmap);
		else
			stid = -1;
	} else {
		stid = bitmap_find_free_region(t->stid_bmap, t->nstids, 1);
		if (stid < 0)
			stid = -1;
	}
	if (stid >= 0) {
		t->stid_tab[stid].data = data;
		stid += t->stid_base;
		/* IPv6 requires max of 520 bits or 16 cells in TCAM
		 * This is equivalent to 4 TIDs. With CLIP enabled it
		 * needs 2 TIDs.
		 */
		if (family == PF_INET6) {
			t->stids_in_use += 2;
			t->v6_stids_in_use += 2;
		} else {
			t->stids_in_use++;
		}
	}
	spin_unlock_bh(&t->stid_lock);
	return stid;
}
EXPORT_SYMBOL(cxgb4_alloc_stid);

/* Allocate a server filter TID and set it to the supplied value.
 */
int cxgb4_alloc_sftid(struct tid_info *t, int family, void *data)
{
	int stid;

	spin_lock_bh(&t->stid_lock);
	if (family == PF_INET) {
		stid = find_next_zero_bit(t->stid_bmap,
				t->nstids + t->nsftids, t->nstids);
		if (stid < (t->nstids + t->nsftids))
			__set_bit(stid, t->stid_bmap);
		else
			stid = -1;
	} else {
		stid = -1;
	}
	if (stid >= 0) {
		t->stid_tab[stid].data = data;
		stid -= t->nstids;
		stid += t->sftid_base;
		t->sftids_in_use++;
	}
	spin_unlock_bh(&t->stid_lock);
	return stid;
}
EXPORT_SYMBOL(cxgb4_alloc_sftid);

/* Release a server TID.
 */
void cxgb4_free_stid(struct tid_info *t, unsigned int stid, int family)
{
	/* Is it a server filter TID? */
	if (t->nsftids && (stid >= t->sftid_base)) {
		stid -= t->sftid_base;
		stid += t->nstids;
	} else {
		stid -= t->stid_base;
	}

	spin_lock_bh(&t->stid_lock);
	if (family == PF_INET)
		__clear_bit(stid, t->stid_bmap);
	else
		bitmap_release_region(t->stid_bmap, stid, 1);
	t->stid_tab[stid].data = NULL;
	if (stid < t->nstids) {
		if (family == PF_INET6) {
			t->stids_in_use -= 2;
			t->v6_stids_in_use -= 2;
		} else {
			t->stids_in_use--;
		}
	} else {
		t->sftids_in_use--;
	}

	spin_unlock_bh(&t->stid_lock);
}
EXPORT_SYMBOL(cxgb4_free_stid);

/*
 * Populate a TID_RELEASE WR.  Caller must properly size the skb.
 */
static void mk_tid_release(struct sk_buff *skb, unsigned int chan,
			   unsigned int tid)
{
	struct cpl_tid_release *req;

	set_wr_txq(skb, CPL_PRIORITY_SETUP, chan);
	req = __skb_put(skb, sizeof(*req));
	INIT_TP_WR(req, tid);
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_TID_RELEASE, tid));
}

/*
 * Queue a TID release request and if necessary schedule a work queue to
 * process it.
 */
static void cxgb4_queue_tid_release(struct tid_info *t, unsigned int chan,
				    unsigned int tid)
{
	struct adapter *adap = container_of(t, struct adapter, tids);
	void **p = &t->tid_tab[tid - t->tid_base];

	spin_lock_bh(&adap->tid_release_lock);
	*p = adap->tid_release_head;
	/* Low 2 bits encode the Tx channel number */
	adap->tid_release_head = (void **)((uintptr_t)p | chan);
	if (!adap->tid_release_task_busy) {
		adap->tid_release_task_busy = true;
		queue_work(adap->workq, &adap->tid_release_task);
	}
	spin_unlock_bh(&adap->tid_release_lock);
}

/*
 * Process the list of pending TID release requests.
 */
static void process_tid_release_list(struct work_struct *work)
{
	struct sk_buff *skb;
	struct adapter *adap;

	adap = container_of(work, struct adapter, tid_release_task);

	spin_lock_bh(&adap->tid_release_lock);
	while (adap->tid_release_head) {
		void **p = adap->tid_release_head;
		unsigned int chan = (uintptr_t)p & 3;
		p = (void *)p - chan;

		adap->tid_release_head = *p;
		*p = NULL;
		spin_unlock_bh(&adap->tid_release_lock);

		while (!(skb = alloc_skb(sizeof(struct cpl_tid_release),
					 GFP_KERNEL)))
			schedule_timeout_uninterruptible(1);

		mk_tid_release(skb, chan, p - adap->tids.tid_tab);
		t4_ofld_send(adap, skb);
		spin_lock_bh(&adap->tid_release_lock);
	}
	adap->tid_release_task_busy = false;
	spin_unlock_bh(&adap->tid_release_lock);
}

/*
 * Release a TID and inform HW.  If we are unable to allocate the release
 * message we defer to a work queue.
 */
void cxgb4_remove_tid(struct tid_info *t, unsigned int chan, unsigned int tid,
		      unsigned short family)
{
	struct adapter *adap = container_of(t, struct adapter, tids);
	struct sk_buff *skb;

	WARN_ON(tid_out_of_range(&adap->tids, tid));

	if (t->tid_tab[tid - adap->tids.tid_base]) {
		t->tid_tab[tid - adap->tids.tid_base] = NULL;
		atomic_dec(&t->conns_in_use);
		if (t->hash_base && (tid >= t->hash_base)) {
			if (family == AF_INET6)
				atomic_sub(2, &t->hash_tids_in_use);
			else
				atomic_dec(&t->hash_tids_in_use);
		} else {
			if (family == AF_INET6)
				atomic_sub(2, &t->tids_in_use);
			else
				atomic_dec(&t->tids_in_use);
		}
	}

	skb = alloc_skb(sizeof(struct cpl_tid_release), GFP_ATOMIC);
	if (likely(skb)) {
		mk_tid_release(skb, chan, tid);
		t4_ofld_send(adap, skb);
	} else
		cxgb4_queue_tid_release(t, chan, tid);
}
EXPORT_SYMBOL(cxgb4_remove_tid);

/*
 * Allocate and initialize the TID tables.  Returns 0 on success.
 */
static int tid_init(struct tid_info *t)
{
	struct adapter *adap = container_of(t, struct adapter, tids);
	unsigned int max_ftids = t->nftids + t->nsftids;
	unsigned int natids = t->natids;
	unsigned int hpftid_bmap_size;
	unsigned int eotid_bmap_size;
	unsigned int stid_bmap_size;
	unsigned int ftid_bmap_size;
	size_t size;

	stid_bmap_size = BITS_TO_LONGS(t->nstids + t->nsftids);
	ftid_bmap_size = BITS_TO_LONGS(t->nftids);
	hpftid_bmap_size = BITS_TO_LONGS(t->nhpftids);
	eotid_bmap_size = BITS_TO_LONGS(t->neotids);
	size = t->ntids * sizeof(*t->tid_tab) +
	       natids * sizeof(*t->atid_tab) +
	       t->nstids * sizeof(*t->stid_tab) +
	       t->nsftids * sizeof(*t->stid_tab) +
	       stid_bmap_size * sizeof(long) +
	       t->nhpftids * sizeof(*t->hpftid_tab) +
	       hpftid_bmap_size * sizeof(long) +
	       max_ftids * sizeof(*t->ftid_tab) +
	       ftid_bmap_size * sizeof(long) +
	       t->neotids * sizeof(*t->eotid_tab) +
	       eotid_bmap_size * sizeof(long);

	t->tid_tab = kvzalloc(size, GFP_KERNEL);
	if (!t->tid_tab)
		return -ENOMEM;

	t->atid_tab = (union aopen_entry *)&t->tid_tab[t->ntids];
	t->stid_tab = (struct serv_entry *)&t->atid_tab[natids];
	t->stid_bmap = (unsigned long *)&t->stid_tab[t->nstids + t->nsftids];
	t->hpftid_tab = (struct filter_entry *)&t->stid_bmap[stid_bmap_size];
	t->hpftid_bmap = (unsigned long *)&t->hpftid_tab[t->nhpftids];
	t->ftid_tab = (struct filter_entry *)&t->hpftid_bmap[hpftid_bmap_size];
	t->ftid_bmap = (unsigned long *)&t->ftid_tab[max_ftids];
	t->eotid_tab = (struct eotid_entry *)&t->ftid_bmap[ftid_bmap_size];
	t->eotid_bmap = (unsigned long *)&t->eotid_tab[t->neotids];
	spin_lock_init(&t->stid_lock);
	spin_lock_init(&t->atid_lock);
	spin_lock_init(&t->ftid_lock);

	t->stids_in_use = 0;
	t->v6_stids_in_use = 0;
	t->sftids_in_use = 0;
	t->afree = NULL;
	t->atids_in_use = 0;
	atomic_set(&t->tids_in_use, 0);
	atomic_set(&t->conns_in_use, 0);
	atomic_set(&t->hash_tids_in_use, 0);
	atomic_set(&t->eotids_in_use, 0);

	/* Setup the free list for atid_tab and clear the stid bitmap. */
	if (natids) {
		while (--natids)
			t->atid_tab[natids - 1].next = &t->atid_tab[natids];
		t->afree = t->atid_tab;
	}

	if (is_offload(adap)) {
		bitmap_zero(t->stid_bmap, t->nstids + t->nsftids);
		/* Reserve stid 0 for T4/T5 adapters */
		if (!t->stid_base &&
		    CHELSIO_CHIP_VERSION(adap->params.chip) <= CHELSIO_T5)
			__set_bit(0, t->stid_bmap);

		if (t->neotids)
			bitmap_zero(t->eotid_bmap, t->neotids);
	}

	if (t->nhpftids)
		bitmap_zero(t->hpftid_bmap, t->nhpftids);
	bitmap_zero(t->ftid_bmap, t->nftids);
	return 0;
}

/**
 *	cxgb4_create_server - create an IP server
 *	@dev: the device
 *	@stid: the server TID
 *	@sip: local IP address to bind server to
 *	@sport: the server's TCP port
 *	@vlan: the VLAN header information
 *	@queue: queue to direct messages from this server to
 *
 *	Create an IP server for the given port and address.
 *	Returns <0 on error and one of the %NET_XMIT_* values on success.
 */
int cxgb4_create_server(const struct net_device *dev, unsigned int stid,
			__be32 sip, __be16 sport, __be16 vlan,
			unsigned int queue)
{
	unsigned int chan;
	struct sk_buff *skb;
	struct adapter *adap;
	struct cpl_pass_open_req *req;
	int ret;

	skb = alloc_skb(sizeof(*req), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	adap = netdev2adap(dev);
	req = __skb_put(skb, sizeof(*req));
	INIT_TP_WR(req, 0);
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_PASS_OPEN_REQ, stid));
	req->local_port = sport;
	req->peer_port = htons(0);
	req->local_ip = sip;
	req->peer_ip = htonl(0);
	chan = rxq_to_chan(&adap->sge, queue);
	req->opt0 = cpu_to_be64(TX_CHAN_V(chan));
	req->opt1 = cpu_to_be64(CONN_POLICY_V(CPL_CONN_POLICY_ASK) |
				SYN_RSS_ENABLE_F | SYN_RSS_QUEUE_V(queue));
	ret = t4_mgmt_tx(adap, skb);
	return net_xmit_eval(ret);
}
EXPORT_SYMBOL(cxgb4_create_server);

/*	cxgb4_create_server6 - create an IPv6 server
 *	@dev: the device
 *	@stid: the server TID
 *	@sip: local IPv6 address to bind server to
 *	@sport: the server's TCP port
 *	@queue: queue to direct messages from this server to
 *
 *	Create an IPv6 server for the given port and address.
 *	Returns <0 on error and one of the %NET_XMIT_* values on success.
 */
int cxgb4_create_server6(const struct net_device *dev, unsigned int stid,
			 const struct in6_addr *sip, __be16 sport,
			 unsigned int queue)
{
	unsigned int chan;
	struct sk_buff *skb;
	struct adapter *adap;
	struct cpl_pass_open_req6 *req;
	int ret;

	skb = alloc_skb(sizeof(*req), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	adap = netdev2adap(dev);
	req = __skb_put(skb, sizeof(*req));
	INIT_TP_WR(req, 0);
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_PASS_OPEN_REQ6, stid));
	req->local_port = sport;
	req->peer_port = htons(0);
	req->local_ip_hi = *(__be64 *)(sip->s6_addr);
	req->local_ip_lo = *(__be64 *)(sip->s6_addr + 8);
	req->peer_ip_hi = cpu_to_be64(0);
	req->peer_ip_lo = cpu_to_be64(0);
	chan = rxq_to_chan(&adap->sge, queue);
	req->opt0 = cpu_to_be64(TX_CHAN_V(chan));
	req->opt1 = cpu_to_be64(CONN_POLICY_V(CPL_CONN_POLICY_ASK) |
				SYN_RSS_ENABLE_F | SYN_RSS_QUEUE_V(queue));
	ret = t4_mgmt_tx(adap, skb);
	return net_xmit_eval(ret);
}
EXPORT_SYMBOL(cxgb4_create_server6);

int cxgb4_remove_server(const struct net_device *dev, unsigned int stid,
			unsigned int queue, bool ipv6)
{
	struct sk_buff *skb;
	struct adapter *adap;
	struct cpl_close_listsvr_req *req;
	int ret;

	adap = netdev2adap(dev);

	skb = alloc_skb(sizeof(*req), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	req = __skb_put(skb, sizeof(*req));
	INIT_TP_WR(req, 0);
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_CLOSE_LISTSRV_REQ, stid));
	req->reply_ctrl = htons(NO_REPLY_V(0) | (ipv6 ? LISTSVR_IPV6_V(1) :
				LISTSVR_IPV6_V(0)) | QUEUENO_V(queue));
	ret = t4_mgmt_tx(adap, skb);
	return net_xmit_eval(ret);
}
EXPORT_SYMBOL(cxgb4_remove_server);

/**
 *	cxgb4_best_mtu - find the entry in the MTU table closest to an MTU
 *	@mtus: the HW MTU table
 *	@mtu: the target MTU
 *	@idx: index of selected entry in the MTU table
 *
 *	Returns the index and the value in the HW MTU table that is closest to
 *	but does not exceed @mtu, unless @mtu is smaller than any value in the
 *	table, in which case that smallest available value is selected.
 */
unsigned int cxgb4_best_mtu(const unsigned short *mtus, unsigned short mtu,
			    unsigned int *idx)
{
	unsigned int i = 0;

	while (i < NMTUS - 1 && mtus[i + 1] <= mtu)
		++i;
	if (idx)
		*idx = i;
	return mtus[i];
}
EXPORT_SYMBOL(cxgb4_best_mtu);

/**
 *     cxgb4_best_aligned_mtu - find best MTU, [hopefully] data size aligned
 *     @mtus: the HW MTU table
 *     @header_size: Header Size
 *     @data_size_max: maximum Data Segment Size
 *     @data_size_align: desired Data Segment Size Alignment (2^N)
 *     @mtu_idxp: HW MTU Table Index return value pointer (possibly NULL)
 *
 *     Similar to cxgb4_best_mtu() but instead of searching the Hardware
 *     MTU Table based solely on a Maximum MTU parameter, we break that
 *     parameter up into a Header Size and Maximum Data Segment Size, and
 *     provide a desired Data Segment Size Alignment.  If we find an MTU in
 *     the Hardware MTU Table which will result in a Data Segment Size with
 *     the requested alignment _and_ that MTU isn't "too far" from the
 *     closest MTU, then we'll return that rather than the closest MTU.
 */
unsigned int cxgb4_best_aligned_mtu(const unsigned short *mtus,
				    unsigned short header_size,
				    unsigned short data_size_max,
				    unsigned short data_size_align,
				    unsigned int *mtu_idxp)
{
	unsigned short max_mtu = header_size + data_size_max;
	unsigned short data_size_align_mask = data_size_align - 1;
	int mtu_idx, aligned_mtu_idx;

	/* Scan the MTU Table till we find an MTU which is larger than our
	 * Maximum MTU or we reach the end of the table.  Along the way,
	 * record the last MTU found, if any, which will result in a Data
	 * Segment Length matching the requested alignment.
	 */
	for (mtu_idx = 0, aligned_mtu_idx = -1; mtu_idx < NMTUS; mtu_idx++) {
		unsigned short data_size = mtus[mtu_idx] - header_size;

		/* If this MTU minus the Header Size would result in a
		 * Data Segment Size of the desired alignment, remember it.
		 */
		if ((data_size & data_size_align_mask) == 0)
			aligned_mtu_idx = mtu_idx;

		/* If we're not at the end of the Hardware MTU Table and the
		 * next element is larger than our Maximum MTU, drop out of
		 * the loop.
		 */
		if (mtu_idx+1 < NMTUS && mtus[mtu_idx+1] > max_mtu)
			break;
	}

	/* If we fell out of the loop because we ran to the end of the table,
	 * then we just have to use the last [largest] entry.
	 */
	if (mtu_idx == NMTUS)
		mtu_idx--;

	/* If we found an MTU which resulted in the requested Data Segment
	 * Length alignment and that's "not far" from the largest MTU which is
	 * less than or equal to the maximum MTU, then use that.
	 */
	if (aligned_mtu_idx >= 0 &&
	    mtu_idx - aligned_mtu_idx <= 1)
		mtu_idx = aligned_mtu_idx;

	/* If the caller has passed in an MTU Index pointer, pass the
	 * MTU Index back.  Return the MTU value.
	 */
	if (mtu_idxp)
		*mtu_idxp = mtu_idx;
	return mtus[mtu_idx];
}
EXPORT_SYMBOL(cxgb4_best_aligned_mtu);

/**
 *	cxgb4_port_chan - get the HW channel of a port
 *	@dev: the net device for the port
 *
 *	Return the HW Tx channel of the given port.
 */
unsigned int cxgb4_port_chan(const struct net_device *dev)
{
	return netdev2pinfo(dev)->tx_chan;
}
EXPORT_SYMBOL(cxgb4_port_chan);

/**
 *      cxgb4_port_e2cchan - get the HW c-channel of a port
 *      @dev: the net device for the port
 *
 *      Return the HW RX c-channel of the given port.
 */
unsigned int cxgb4_port_e2cchan(const struct net_device *dev)
{
	return netdev2pinfo(dev)->rx_cchan;
}
EXPORT_SYMBOL(cxgb4_port_e2cchan);

unsigned int cxgb4_dbfifo_count(const struct net_device *dev, int lpfifo)
{
	struct adapter *adap = netdev2adap(dev);
	u32 v1, v2, lp_count, hp_count;

	v1 = t4_read_reg(adap, SGE_DBFIFO_STATUS_A);
	v2 = t4_read_reg(adap, SGE_DBFIFO_STATUS2_A);
	if (is_t4(adap->params.chip)) {
		lp_count = LP_COUNT_G(v1);
		hp_count = HP_COUNT_G(v1);
	} else {
		lp_count = LP_COUNT_T5_G(v1);
		hp_count = HP_COUNT_T5_G(v2);
	}
	return lpfifo ? lp_count : hp_count;
}
EXPORT_SYMBOL(cxgb4_dbfifo_count);

/**
 *	cxgb4_port_viid - get the VI id of a port
 *	@dev: the net device for the port
 *
 *	Return the VI id of the given port.
 */
unsigned int cxgb4_port_viid(const struct net_device *dev)
{
	return netdev2pinfo(dev)->viid;
}
EXPORT_SYMBOL(cxgb4_port_viid);

/**
 *	cxgb4_port_idx - get the index of a port
 *	@dev: the net device for the port
 *
 *	Return the index of the given port.
 */
unsigned int cxgb4_port_idx(const struct net_device *dev)
{
	return netdev2pinfo(dev)->port_id;
}
EXPORT_SYMBOL(cxgb4_port_idx);

void cxgb4_get_tcp_stats(struct pci_dev *pdev, struct tp_tcp_stats *v4,
			 struct tp_tcp_stats *v6)
{
	struct adapter *adap = pci_get_drvdata(pdev);

	spin_lock(&adap->stats_lock);
	t4_tp_get_tcp_stats(adap, v4, v6, false);
	spin_unlock(&adap->stats_lock);
}
EXPORT_SYMBOL(cxgb4_get_tcp_stats);

void cxgb4_iscsi_init(struct net_device *dev, unsigned int tag_mask,
		      const unsigned int *pgsz_order)
{
	struct adapter *adap = netdev2adap(dev);

	t4_write_reg(adap, ULP_RX_ISCSI_TAGMASK_A, tag_mask);
	t4_write_reg(adap, ULP_RX_ISCSI_PSZ_A, HPZ0_V(pgsz_order[0]) |
		     HPZ1_V(pgsz_order[1]) | HPZ2_V(pgsz_order[2]) |
		     HPZ3_V(pgsz_order[3]));
}
EXPORT_SYMBOL(cxgb4_iscsi_init);

int cxgb4_flush_eq_cache(struct net_device *dev)
{
	struct adapter *adap = netdev2adap(dev);

	return t4_sge_ctxt_flush(adap, adap->mbox, CTXT_EGRESS);
}
EXPORT_SYMBOL(cxgb4_flush_eq_cache);

static int read_eq_indices(struct adapter *adap, u16 qid, u16 *pidx, u16 *cidx)
{
	u32 addr = t4_read_reg(adap, SGE_DBQ_CTXT_BADDR_A) + 24 * qid + 8;
	__be64 indices;
	int ret;

	spin_lock(&adap->win0_lock);
	ret = t4_memory_rw(adap, 0, MEM_EDC0, addr,
			   sizeof(indices), (__be32 *)&indices,
			   T4_MEMORY_READ);
	spin_unlock(&adap->win0_lock);
	if (!ret) {
		*cidx = (be64_to_cpu(indices) >> 25) & 0xffff;
		*pidx = (be64_to_cpu(indices) >> 9) & 0xffff;
	}
	return ret;
}

int cxgb4_sync_txq_pidx(struct net_device *dev, u16 qid, u16 pidx,
			u16 size)
{
	struct adapter *adap = netdev2adap(dev);
	u16 hw_pidx, hw_cidx;
	int ret;

	ret = read_eq_indices(adap, qid, &hw_pidx, &hw_cidx);
	if (ret)
		goto out;

	if (pidx != hw_pidx) {
		u16 delta;
		u32 val;

		if (pidx >= hw_pidx)
			delta = pidx - hw_pidx;
		else
			delta = size - hw_pidx + pidx;

		if (is_t4(adap->params.chip))
			val = PIDX_V(delta);
		else
			val = PIDX_T5_V(delta);
		wmb();
		t4_write_reg(adap, MYPF_REG(SGE_PF_KDOORBELL_A),
			     QID_V(qid) | val);
	}
out:
	return ret;
}
EXPORT_SYMBOL(cxgb4_sync_txq_pidx);

int cxgb4_read_tpte(struct net_device *dev, u32 stag, __be32 *tpte)
{
	u32 edc0_size, edc1_size, mc0_size, mc1_size, size;
	u32 edc0_end, edc1_end, mc0_end, mc1_end;
	u32 offset, memtype, memaddr;
	struct adapter *adap;
	u32 hma_size = 0;
	int ret;

	adap = netdev2adap(dev);

	offset = ((stag >> 8) * 32) + adap->vres.stag.start;

	/* Figure out where the offset lands in the Memory Type/Address scheme.
	 * This code assumes that the memory is laid out starting at offset 0
	 * with no breaks as: EDC0, EDC1, MC0, MC1. All cards have both EDC0
	 * and EDC1.  Some cards will have neither MC0 nor MC1, most cards have
	 * MC0, and some have both MC0 and MC1.
	 */
	size = t4_read_reg(adap, MA_EDRAM0_BAR_A);
	edc0_size = EDRAM0_SIZE_G(size) << 20;
	size = t4_read_reg(adap, MA_EDRAM1_BAR_A);
	edc1_size = EDRAM1_SIZE_G(size) << 20;
	size = t4_read_reg(adap, MA_EXT_MEMORY0_BAR_A);
	mc0_size = EXT_MEM0_SIZE_G(size) << 20;

	if (t4_read_reg(adap, MA_TARGET_MEM_ENABLE_A) & HMA_MUX_F) {
		size = t4_read_reg(adap, MA_EXT_MEMORY1_BAR_A);
		hma_size = EXT_MEM1_SIZE_G(size) << 20;
	}
	edc0_end = edc0_size;
	edc1_end = edc0_end + edc1_size;
	mc0_end = edc1_end + mc0_size;

	if (offset < edc0_end) {
		memtype = MEM_EDC0;
		memaddr = offset;
	} else if (offset < edc1_end) {
		memtype = MEM_EDC1;
		memaddr = offset - edc0_end;
	} else {
		if (hma_size && (offset < (edc1_end + hma_size))) {
			memtype = MEM_HMA;
			memaddr = offset - edc1_end;
		} else if (offset < mc0_end) {
			memtype = MEM_MC0;
			memaddr = offset - edc1_end;
		} else if (is_t5(adap->params.chip)) {
			size = t4_read_reg(adap, MA_EXT_MEMORY1_BAR_A);
			mc1_size = EXT_MEM1_SIZE_G(size) << 20;
			mc1_end = mc0_end + mc1_size;
			if (offset < mc1_end) {
				memtype = MEM_MC1;
				memaddr = offset - mc0_end;
			} else {
				/* offset beyond the end of any memory */
				goto err;
			}
		} else {
			/* T4/T6 only has a single memory channel */
			goto err;
		}
	}

	spin_lock(&adap->win0_lock);
	ret = t4_memory_rw(adap, 0, memtype, memaddr, 32, tpte, T4_MEMORY_READ);
	spin_unlock(&adap->win0_lock);
	return ret;

err:
	dev_err(adap->pdev_dev, "stag %#x, offset %#x out of range\n",
		stag, offset);
	return -EINVAL;
}
EXPORT_SYMBOL(cxgb4_read_tpte);

u64 cxgb4_read_sge_timestamp(struct net_device *dev)
{
	u32 hi, lo;
	struct adapter *adap;

	adap = netdev2adap(dev);
	lo = t4_read_reg(adap, SGE_TIMESTAMP_LO_A);
	hi = TSVAL_G(t4_read_reg(adap, SGE_TIMESTAMP_HI_A));

	return ((u64)hi << 32) | (u64)lo;
}
EXPORT_SYMBOL(cxgb4_read_sge_timestamp);

int cxgb4_bar2_sge_qregs(struct net_device *dev,
			 unsigned int qid,
			 enum cxgb4_bar2_qtype qtype,
			 int user,
			 u64 *pbar2_qoffset,
			 unsigned int *pbar2_qid)
{
	return t4_bar2_sge_qregs(netdev2adap(dev),
				 qid,
				 (qtype == CXGB4_BAR2_QTYPE_EGRESS
				  ? T4_BAR2_QTYPE_EGRESS
				  : T4_BAR2_QTYPE_INGRESS),
				 user,
				 pbar2_qoffset,
				 pbar2_qid);
}
EXPORT_SYMBOL(cxgb4_bar2_sge_qregs);

static struct pci_driver cxgb4_driver;

static void check_neigh_update(struct neighbour *neigh)
{
	const struct device *parent;
	const struct net_device *netdev = neigh->dev;

	if (is_vlan_dev(netdev))
		netdev = vlan_dev_real_dev(netdev);
	parent = netdev->dev.parent;
	if (parent && parent->driver == &cxgb4_driver.driver)
		t4_l2t_update(dev_get_drvdata(parent), neigh);
}

static int netevent_cb(struct notifier_block *nb, unsigned long event,
		       void *data)
{
	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		check_neigh_update(data);
		break;
	case NETEVENT_REDIRECT:
	default:
		break;
	}
	return 0;
}

static bool netevent_registered;
static struct notifier_block cxgb4_netevent_nb = {
	.notifier_call = netevent_cb
};

static void drain_db_fifo(struct adapter *adap, int usecs)
{
	u32 v1, v2, lp_count, hp_count;

	do {
		v1 = t4_read_reg(adap, SGE_DBFIFO_STATUS_A);
		v2 = t4_read_reg(adap, SGE_DBFIFO_STATUS2_A);
		if (is_t4(adap->params.chip)) {
			lp_count = LP_COUNT_G(v1);
			hp_count = HP_COUNT_G(v1);
		} else {
			lp_count = LP_COUNT_T5_G(v1);
			hp_count = HP_COUNT_T5_G(v2);
		}

		if (lp_count == 0 && hp_count == 0)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(usecs_to_jiffies(usecs));
	} while (1);
}

static void disable_txq_db(struct sge_txq *q)
{
	unsigned long flags;

	spin_lock_irqsave(&q->db_lock, flags);
	q->db_disabled = 1;
	spin_unlock_irqrestore(&q->db_lock, flags);
}

static void enable_txq_db(struct adapter *adap, struct sge_txq *q)
{
	spin_lock_irq(&q->db_lock);
	if (q->db_pidx_inc) {
		/* Make sure that all writes to the TX descriptors
		 * are committed before we tell HW about them.
		 */
		wmb();
		t4_write_reg(adap, MYPF_REG(SGE_PF_KDOORBELL_A),
			     QID_V(q->cntxt_id) | PIDX_V(q->db_pidx_inc));
		q->db_pidx_inc = 0;
	}
	q->db_disabled = 0;
	spin_unlock_irq(&q->db_lock);
}

static void disable_dbs(struct adapter *adap)
{
	int i;

	for_each_ethrxq(&adap->sge, i)
		disable_txq_db(&adap->sge.ethtxq[i].q);
	if (is_offload(adap)) {
		struct sge_uld_txq_info *txq_info =
			adap->sge.uld_txq_info[CXGB4_TX_OFLD];

		if (txq_info) {
			for_each_ofldtxq(&adap->sge, i) {
				struct sge_uld_txq *txq = &txq_info->uldtxq[i];

				disable_txq_db(&txq->q);
			}
		}
	}
	for_each_port(adap, i)
		disable_txq_db(&adap->sge.ctrlq[i].q);
}

static void enable_dbs(struct adapter *adap)
{
	int i;

	for_each_ethrxq(&adap->sge, i)
		enable_txq_db(adap, &adap->sge.ethtxq[i].q);
	if (is_offload(adap)) {
		struct sge_uld_txq_info *txq_info =
			adap->sge.uld_txq_info[CXGB4_TX_OFLD];

		if (txq_info) {
			for_each_ofldtxq(&adap->sge, i) {
				struct sge_uld_txq *txq = &txq_info->uldtxq[i];

				enable_txq_db(adap, &txq->q);
			}
		}
	}
	for_each_port(adap, i)
		enable_txq_db(adap, &adap->sge.ctrlq[i].q);
}

static void notify_rdma_uld(struct adapter *adap, enum cxgb4_control cmd)
{
	enum cxgb4_uld type = CXGB4_ULD_RDMA;

	if (adap->uld && adap->uld[type].handle)
		adap->uld[type].control(adap->uld[type].handle, cmd);
}

static void process_db_full(struct work_struct *work)
{
	struct adapter *adap;

	adap = container_of(work, struct adapter, db_full_task);

	drain_db_fifo(adap, dbfifo_drain_delay);
	enable_dbs(adap);
	notify_rdma_uld(adap, CXGB4_CONTROL_DB_EMPTY);
	if (CHELSIO_CHIP_VERSION(adap->params.chip) <= CHELSIO_T5)
		t4_set_reg_field(adap, SGE_INT_ENABLE3_A,
				 DBFIFO_HP_INT_F | DBFIFO_LP_INT_F,
				 DBFIFO_HP_INT_F | DBFIFO_LP_INT_F);
	else
		t4_set_reg_field(adap, SGE_INT_ENABLE3_A,
				 DBFIFO_LP_INT_F, DBFIFO_LP_INT_F);
}

static void sync_txq_pidx(struct adapter *adap, struct sge_txq *q)
{
	u16 hw_pidx, hw_cidx;
	int ret;

	spin_lock_irq(&q->db_lock);
	ret = read_eq_indices(adap, (u16)q->cntxt_id, &hw_pidx, &hw_cidx);
	if (ret)
		goto out;
	if (q->db_pidx != hw_pidx) {
		u16 delta;
		u32 val;

		if (q->db_pidx >= hw_pidx)
			delta = q->db_pidx - hw_pidx;
		else
			delta = q->size - hw_pidx + q->db_pidx;

		if (is_t4(adap->params.chip))
			val = PIDX_V(delta);
		else
			val = PIDX_T5_V(delta);
		wmb();
		t4_write_reg(adap, MYPF_REG(SGE_PF_KDOORBELL_A),
			     QID_V(q->cntxt_id) | val);
	}
out:
	q->db_disabled = 0;
	q->db_pidx_inc = 0;
	spin_unlock_irq(&q->db_lock);
	if (ret)
		CH_WARN(adap, "DB drop recovery failed.\n");
}

static void recover_all_queues(struct adapter *adap)
{
	int i;

	for_each_ethrxq(&adap->sge, i)
		sync_txq_pidx(adap, &adap->sge.ethtxq[i].q);
	if (is_offload(adap)) {
		struct sge_uld_txq_info *txq_info =
			adap->sge.uld_txq_info[CXGB4_TX_OFLD];
		if (txq_info) {
			for_each_ofldtxq(&adap->sge, i) {
				struct sge_uld_txq *txq = &txq_info->uldtxq[i];

				sync_txq_pidx(adap, &txq->q);
			}
		}
	}
	for_each_port(adap, i)
		sync_txq_pidx(adap, &adap->sge.ctrlq[i].q);
}

static void process_db_drop(struct work_struct *work)
{
	struct adapter *adap;

	adap = container_of(work, struct adapter, db_drop_task);

	if (is_t4(adap->params.chip)) {
		drain_db_fifo(adap, dbfifo_drain_delay);
		notify_rdma_uld(adap, CXGB4_CONTROL_DB_DROP);
		drain_db_fifo(adap, dbfifo_drain_delay);
		recover_all_queues(adap);
		drain_db_fifo(adap, dbfifo_drain_delay);
		enable_dbs(adap);
		notify_rdma_uld(adap, CXGB4_CONTROL_DB_EMPTY);
	} else if (is_t5(adap->params.chip)) {
		u32 dropped_db = t4_read_reg(adap, 0x010ac);
		u16 qid = (dropped_db >> 15) & 0x1ffff;
		u16 pidx_inc = dropped_db & 0x1fff;
		u64 bar2_qoffset;
		unsigned int bar2_qid;
		int ret;

		ret = t4_bar2_sge_qregs(adap, qid, T4_BAR2_QTYPE_EGRESS,
					0, &bar2_qoffset, &bar2_qid);
		if (ret)
			dev_err(adap->pdev_dev, "doorbell drop recovery: "
				"qid=%d, pidx_inc=%d\n", qid, pidx_inc);
		else
			writel(PIDX_T5_V(pidx_inc) | QID_V(bar2_qid),
			       adap->bar2 + bar2_qoffset + SGE_UDB_KDOORBELL);

		/* Re-enable BAR2 WC */
		t4_set_reg_field(adap, 0x10b0, 1<<15, 1<<15);
	}

	if (CHELSIO_CHIP_VERSION(adap->params.chip) <= CHELSIO_T5)
		t4_set_reg_field(adap, SGE_DOORBELL_CONTROL_A, DROPPED_DB_F, 0);
}

void t4_db_full(struct adapter *adap)
{
	if (is_t4(adap->params.chip)) {
		disable_dbs(adap);
		notify_rdma_uld(adap, CXGB4_CONTROL_DB_FULL);
		t4_set_reg_field(adap, SGE_INT_ENABLE3_A,
				 DBFIFO_HP_INT_F | DBFIFO_LP_INT_F, 0);
		queue_work(adap->workq, &adap->db_full_task);
	}
}

void t4_db_dropped(struct adapter *adap)
{
	if (is_t4(adap->params.chip)) {
		disable_dbs(adap);
		notify_rdma_uld(adap, CXGB4_CONTROL_DB_FULL);
	}
	queue_work(adap->workq, &adap->db_drop_task);
}

void t4_register_netevent_notifier(void)
{
	if (!netevent_registered) {
		register_netevent_notifier(&cxgb4_netevent_nb);
		netevent_registered = true;
	}
}

static void detach_ulds(struct adapter *adap)
{
	unsigned int i;

	if (!is_uld(adap))
		return;

	mutex_lock(&uld_mutex);
	list_del(&adap->list_node);

	for (i = 0; i < CXGB4_ULD_MAX; i++)
		if (adap->uld && adap->uld[i].handle)
			adap->uld[i].state_change(adap->uld[i].handle,
					     CXGB4_STATE_DETACH);

	if (netevent_registered && list_empty(&adapter_list)) {
		unregister_netevent_notifier(&cxgb4_netevent_nb);
		netevent_registered = false;
	}
	mutex_unlock(&uld_mutex);
}

static void notify_ulds(struct adapter *adap, enum cxgb4_state new_state)
{
	unsigned int i;

	mutex_lock(&uld_mutex);
	for (i = 0; i < CXGB4_ULD_MAX; i++)
		if (adap->uld && adap->uld[i].handle)
			adap->uld[i].state_change(adap->uld[i].handle,
						  new_state);
	mutex_unlock(&uld_mutex);
}

#if IS_ENABLED(CONFIG_IPV6)
static int cxgb4_inet6addr_handler(struct notifier_block *this,
				   unsigned long event, void *data)
{
	struct inet6_ifaddr *ifa = data;
	struct net_device *event_dev = ifa->idev->dev;
	const struct device *parent = NULL;
#if IS_ENABLED(CONFIG_BONDING)
	struct adapter *adap;
#endif
	if (is_vlan_dev(event_dev))
		event_dev = vlan_dev_real_dev(event_dev);
#if IS_ENABLED(CONFIG_BONDING)
	if (event_dev->flags & IFF_MASTER) {
		list_for_each_entry(adap, &adapter_list, list_node) {
			switch (event) {
			case NETDEV_UP:
				cxgb4_clip_get(adap->port[0],
					       (const u32 *)ifa, 1);
				break;
			case NETDEV_DOWN:
				cxgb4_clip_release(adap->port[0],
						   (const u32 *)ifa, 1);
				break;
			default:
				break;
			}
		}
		return NOTIFY_OK;
	}
#endif

	if (event_dev)
		parent = event_dev->dev.parent;

	if (parent && parent->driver == &cxgb4_driver.driver) {
		switch (event) {
		case NETDEV_UP:
			cxgb4_clip_get(event_dev, (const u32 *)ifa, 1);
			break;
		case NETDEV_DOWN:
			cxgb4_clip_release(event_dev, (const u32 *)ifa, 1);
			break;
		default:
			break;
		}
	}
	return NOTIFY_OK;
}

static bool inet6addr_registered;
static struct notifier_block cxgb4_inet6addr_notifier = {
	.notifier_call = cxgb4_inet6addr_handler
};

static void update_clip(const struct adapter *adap)
{
	int i;
	struct net_device *dev;
	int ret;

	rcu_read_lock();

	for (i = 0; i < MAX_NPORTS; i++) {
		dev = adap->port[i];
		ret = 0;

		if (dev)
			ret = cxgb4_update_root_dev_clip(dev);

		if (ret < 0)
			break;
	}
	rcu_read_unlock();
}
#endif /* IS_ENABLED(CONFIG_IPV6) */

/**
 *	cxgb_up - enable the adapter
 *	@adap: adapter being enabled
 *
 *	Called when the first port is enabled, this function performs the
 *	actions necessary to make an adapter operational, such as completing
 *	the initialization of HW modules, and enabling interrupts.
 *
 *	Must be called with the rtnl lock held.
 */
static int cxgb_up(struct adapter *adap)
{
	struct sge *s = &adap->sge;
	int err;

	mutex_lock(&uld_mutex);
	err = setup_sge_queues(adap);
	if (err)
		goto rel_lock;
	err = setup_rss(adap);
	if (err)
		goto freeq;

	if (adap->flags & CXGB4_USING_MSIX) {
		if (s->nd_msix_idx < 0) {
			err = -ENOMEM;
			goto irq_err;
		}

		err = request_irq(adap->msix_info[s->nd_msix_idx].vec,
				  t4_nondata_intr, 0,
				  adap->msix_info[s->nd_msix_idx].desc, adap);
		if (err)
			goto irq_err;

		err = request_msix_queue_irqs(adap);
		if (err)
			goto irq_err_free_nd_msix;
	} else {
		err = request_irq(adap->pdev->irq, t4_intr_handler(adap),
				  (adap->flags & CXGB4_USING_MSI) ? 0
								  : IRQF_SHARED,
				  adap->port[0]->name, adap);
		if (err)
			goto irq_err;
	}

	enable_rx(adap);
	t4_sge_start(adap);
	t4_intr_enable(adap);
	adap->flags |= CXGB4_FULL_INIT_DONE;
	mutex_unlock(&uld_mutex);

	notify_ulds(adap, CXGB4_STATE_UP);
#if IS_ENABLED(CONFIG_IPV6)
	update_clip(adap);
#endif
	return err;

irq_err_free_nd_msix:
	free_irq(adap->msix_info[s->nd_msix_idx].vec, adap);
irq_err:
	dev_err(adap->pdev_dev, "request_irq failed, err %d\n", err);
freeq:
	t4_free_sge_resources(adap);
rel_lock:
	mutex_unlock(&uld_mutex);
	return err;
}

static void cxgb_down(struct adapter *adapter)
{
	cancel_work_sync(&adapter->tid_release_task);
	cancel_work_sync(&adapter->db_full_task);
	cancel_work_sync(&adapter->db_drop_task);
	adapter->tid_release_task_busy = false;
	adapter->tid_release_head = NULL;

	t4_sge_stop(adapter);
	t4_free_sge_resources(adapter);

	adapter->flags &= ~CXGB4_FULL_INIT_DONE;
}

/*
 * net_device operations
 */
static int cxgb_open(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	int err;

	netif_carrier_off(dev);

	if (!(adapter->flags & CXGB4_FULL_INIT_DONE)) {
		err = cxgb_up(adapter);
		if (err < 0)
			return err;
	}

	/* It's possible that the basic port information could have
	 * changed since we first read it.
	 */
	err = t4_update_port_info(pi);
	if (err < 0)
		return err;

	err = link_start(dev);
	if (err)
		return err;

	if (pi->nmirrorqsets) {
		mutex_lock(&pi->vi_mirror_mutex);
		err = cxgb4_port_mirror_alloc_queues(dev);
		if (err)
			goto out_unlock;

		err = cxgb4_port_mirror_start(dev);
		if (err)
			goto out_free_queues;
		mutex_unlock(&pi->vi_mirror_mutex);
	}

	netif_tx_start_all_queues(dev);
	return 0;

out_free_queues:
	cxgb4_port_mirror_free_queues(dev);

out_unlock:
	mutex_unlock(&pi->vi_mirror_mutex);
	return err;
}

static int cxgb_close(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	int ret;

	netif_tx_stop_all_queues(dev);
	netif_carrier_off(dev);
	ret = t4_enable_pi_params(adapter, adapter->pf, pi,
				  false, false, false);
#ifdef CONFIG_CHELSIO_T4_DCB
	cxgb4_dcb_reset(dev);
	dcb_tx_queue_prio_enable(dev, false);
#endif
	if (ret)
		return ret;

	if (pi->nmirrorqsets) {
		mutex_lock(&pi->vi_mirror_mutex);
		cxgb4_port_mirror_stop(dev);
		cxgb4_port_mirror_free_queues(dev);
		mutex_unlock(&pi->vi_mirror_mutex);
	}

	return 0;
}

int cxgb4_create_server_filter(const struct net_device *dev, unsigned int stid,
		__be32 sip, __be16 sport, __be16 vlan,
		unsigned int queue, unsigned char port, unsigned char mask)
{
	int ret;
	struct filter_entry *f;
	struct adapter *adap;
	int i;
	u8 *val;

	adap = netdev2adap(dev);

	/* Adjust stid to correct filter index */
	stid -= adap->tids.sftid_base;
	stid += adap->tids.nftids;

	/* Check to make sure the filter requested is writable ...
	 */
	f = &adap->tids.ftid_tab[stid];
	ret = writable_filter(f);
	if (ret)
		return ret;

	/* Clear out any old resources being used by the filter before
	 * we start constructing the new filter.
	 */
	if (f->valid)
		clear_filter(adap, f);

	/* Clear out filter specifications */
	memset(&f->fs, 0, sizeof(struct ch_filter_specification));
	f->fs.val.lport = be16_to_cpu(sport);
	f->fs.mask.lport  = ~0;
	val = (u8 *)&sip;
	if ((val[0] | val[1] | val[2] | val[3]) != 0) {
		for (i = 0; i < 4; i++) {
			f->fs.val.lip[i] = val[i];
			f->fs.mask.lip[i] = ~0;
		}
		if (adap->params.tp.vlan_pri_map & PORT_F) {
			f->fs.val.iport = port;
			f->fs.mask.iport = mask;
		}
	}

	if (adap->params.tp.vlan_pri_map & PROTOCOL_F) {
		f->fs.val.proto = IPPROTO_TCP;
		f->fs.mask.proto = ~0;
	}

	f->fs.dirsteer = 1;
	f->fs.iq = queue;
	/* Mark filter as locked */
	f->locked = 1;
	f->fs.rpttid = 1;

	/* Save the actual tid. We need this to get the corresponding
	 * filter entry structure in filter_rpl.
	 */
	f->tid = stid + adap->tids.ftid_base;
	ret = set_filter_wr(adap, stid);
	if (ret) {
		clear_filter(adap, f);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(cxgb4_create_server_filter);

int cxgb4_remove_server_filter(const struct net_device *dev, unsigned int stid,
		unsigned int queue, bool ipv6)
{
	struct filter_entry *f;
	struct adapter *adap;

	adap = netdev2adap(dev);

	/* Adjust stid to correct filter index */
	stid -= adap->tids.sftid_base;
	stid += adap->tids.nftids;

	f = &adap->tids.ftid_tab[stid];
	/* Unlock the filter */
	f->locked = 0;

	return delete_filter(adap, stid);
}
EXPORT_SYMBOL(cxgb4_remove_server_filter);

static void cxgb_get_stats(struct net_device *dev,
			   struct rtnl_link_stats64 *ns)
{
	struct port_stats stats;
	struct port_info *p = netdev_priv(dev);
	struct adapter *adapter = p->adapter;

	/* Block retrieving statistics during EEH error
	 * recovery. Otherwise, the recovery might fail
	 * and the PCI device will be removed permanently
	 */
	spin_lock(&adapter->stats_lock);
	if (!netif_device_present(dev)) {
		spin_unlock(&adapter->stats_lock);
		return;
	}
	t4_get_port_stats_offset(adapter, p->tx_chan, &stats,
				 &p->stats_base);
	spin_unlock(&adapter->stats_lock);

	ns->tx_bytes   = stats.tx_octets;
	ns->tx_packets = stats.tx_frames;
	ns->rx_bytes   = stats.rx_octets;
	ns->rx_packets = stats.rx_frames;
	ns->multicast  = stats.rx_mcast_frames;

	/* detailed rx_errors */
	ns->rx_length_errors = stats.rx_jabber + stats.rx_too_long +
			       stats.rx_runt;
	ns->rx_over_errors   = 0;
	ns->rx_crc_errors    = stats.rx_fcs_err;
	ns->rx_frame_errors  = stats.rx_symbol_err;
	ns->rx_dropped	     = stats.rx_ovflow0 + stats.rx_ovflow1 +
			       stats.rx_ovflow2 + stats.rx_ovflow3 +
			       stats.rx_trunc0 + stats.rx_trunc1 +
			       stats.rx_trunc2 + stats.rx_trunc3;
	ns->rx_missed_errors = 0;

	/* detailed tx_errors */
	ns->tx_aborted_errors   = 0;
	ns->tx_carrier_errors   = 0;
	ns->tx_fifo_errors      = 0;
	ns->tx_heartbeat_errors = 0;
	ns->tx_window_errors    = 0;

	ns->tx_errors = stats.tx_error_frames;
	ns->rx_errors = stats.rx_symbol_err + stats.rx_fcs_err +
		ns->rx_length_errors + stats.rx_len_err + ns->rx_fifo_errors;
}

static int cxgb_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	unsigned int mbox;
	int ret = 0, prtad, devad;
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct mii_ioctl_data *data = (struct mii_ioctl_data *)&req->ifr_data;

	switch (cmd) {
	case SIOCGMIIPHY:
		if (pi->mdio_addr < 0)
			return -EOPNOTSUPP;
		data->phy_id = pi->mdio_addr;
		break;
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		if (mdio_phy_id_is_c45(data->phy_id)) {
			prtad = mdio_phy_id_prtad(data->phy_id);
			devad = mdio_phy_id_devad(data->phy_id);
		} else if (data->phy_id < 32) {
			prtad = data->phy_id;
			devad = 0;
			data->reg_num &= 0x1f;
		} else
			return -EINVAL;

		mbox = pi->adapter->pf;
		if (cmd == SIOCGMIIREG)
			ret = t4_mdio_rd(pi->adapter, mbox, prtad, devad,
					 data->reg_num, &data->val_out);
		else
			ret = t4_mdio_wr(pi->adapter, mbox, prtad, devad,
					 data->reg_num, data->val_in);
		break;
	case SIOCGHWTSTAMP:
		return copy_to_user(req->ifr_data, &pi->tstamp_config,
				    sizeof(pi->tstamp_config)) ?
			-EFAULT : 0;
	case SIOCSHWTSTAMP:
		if (copy_from_user(&pi->tstamp_config, req->ifr_data,
				   sizeof(pi->tstamp_config)))
			return -EFAULT;

		if (!is_t4(adapter->params.chip)) {
			switch (pi->tstamp_config.tx_type) {
			case HWTSTAMP_TX_OFF:
			case HWTSTAMP_TX_ON:
				break;
			default:
				return -ERANGE;
			}

			switch (pi->tstamp_config.rx_filter) {
			case HWTSTAMP_FILTER_NONE:
				pi->rxtstamp = false;
				break;
			case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
			case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
				cxgb4_ptprx_timestamping(pi, pi->port_id,
							 PTP_TS_L4);
				break;
			case HWTSTAMP_FILTER_PTP_V2_EVENT:
				cxgb4_ptprx_timestamping(pi, pi->port_id,
							 PTP_TS_L2_L4);
				break;
			case HWTSTAMP_FILTER_ALL:
			case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
			case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
			case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
			case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
				pi->rxtstamp = true;
				break;
			default:
				pi->tstamp_config.rx_filter =
					HWTSTAMP_FILTER_NONE;
				return -ERANGE;
			}

			if ((pi->tstamp_config.tx_type == HWTSTAMP_TX_OFF) &&
			    (pi->tstamp_config.rx_filter ==
				HWTSTAMP_FILTER_NONE)) {
				if (cxgb4_ptp_txtype(adapter, pi->port_id) >= 0)
					pi->ptp_enable = false;
			}

			if (pi->tstamp_config.rx_filter !=
				HWTSTAMP_FILTER_NONE) {
				if (cxgb4_ptp_redirect_rx_packet(adapter,
								 pi) >= 0)
					pi->ptp_enable = true;
			}
		} else {
			/* For T4 Adapters */
			switch (pi->tstamp_config.rx_filter) {
			case HWTSTAMP_FILTER_NONE:
			pi->rxtstamp = false;
			break;
			case HWTSTAMP_FILTER_ALL:
			pi->rxtstamp = true;
			break;
			default:
			pi->tstamp_config.rx_filter =
			HWTSTAMP_FILTER_NONE;
			return -ERANGE;
			}
		}
		return copy_to_user(req->ifr_data, &pi->tstamp_config,
				    sizeof(pi->tstamp_config)) ?
			-EFAULT : 0;
	default:
		return -EOPNOTSUPP;
	}
	return ret;
}

static void cxgb_set_rxmode(struct net_device *dev)
{
	/* unfortunately we can't return errors to the stack */
	set_rxmode(dev, -1, false);
}

static int cxgb_change_mtu(struct net_device *dev, int new_mtu)
{
	struct port_info *pi = netdev_priv(dev);
	int ret;

	ret = t4_set_rxmode(pi->adapter, pi->adapter->mbox, pi->viid,
			    pi->viid_mirror, new_mtu, -1, -1, -1, -1, true);
	if (!ret)
		dev->mtu = new_mtu;
	return ret;
}

#ifdef CONFIG_PCI_IOV
static int cxgb4_mgmt_open(struct net_device *dev)
{
	/* Turn carrier off since we don't have to transmit anything on this
	 * interface.
	 */
	netif_carrier_off(dev);
	return 0;
}

/* Fill MAC address that will be assigned by the FW */
static void cxgb4_mgmt_fill_vf_station_mac_addr(struct adapter *adap)
{
	u8 hw_addr[ETH_ALEN], macaddr[ETH_ALEN];
	unsigned int i, vf, nvfs;
	u16 a, b;
	int err;
	u8 *na;

	err = t4_get_raw_vpd_params(adap, &adap->params.vpd);
	if (err)
		return;

	na = adap->params.vpd.na;
	for (i = 0; i < ETH_ALEN; i++)
		hw_addr[i] = (hex2val(na[2 * i + 0]) * 16 +
			      hex2val(na[2 * i + 1]));

	a = (hw_addr[0] << 8) | hw_addr[1];
	b = (hw_addr[1] << 8) | hw_addr[2];
	a ^= b;
	a |= 0x0200;    /* locally assigned Ethernet MAC address */
	a &= ~0x0100;   /* not a multicast Ethernet MAC address */
	macaddr[0] = a >> 8;
	macaddr[1] = a & 0xff;

	for (i = 2; i < 5; i++)
		macaddr[i] = hw_addr[i + 1];

	for (vf = 0, nvfs = pci_sriov_get_totalvfs(adap->pdev);
		vf < nvfs; vf++) {
		macaddr[5] = adap->pf * nvfs + vf;
		ether_addr_copy(adap->vfinfo[vf].vf_mac_addr, macaddr);
	}
}

static int cxgb4_mgmt_set_vf_mac(struct net_device *dev, int vf, u8 *mac)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	int ret;

	/* verify MAC addr is valid */
	if (!is_valid_ether_addr(mac)) {
		dev_err(pi->adapter->pdev_dev,
			"Invalid Ethernet address %pM for VF %d\n",
			mac, vf);
		return -EINVAL;
	}

	dev_info(pi->adapter->pdev_dev,
		 "Setting MAC %pM on VF %d\n", mac, vf);
	ret = t4_set_vf_mac_acl(adap, vf + 1, 1, mac);
	if (!ret)
		ether_addr_copy(adap->vfinfo[vf].vf_mac_addr, mac);
	return ret;
}

static int cxgb4_mgmt_get_vf_config(struct net_device *dev,
				    int vf, struct ifla_vf_info *ivi)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct vf_info *vfinfo;

	if (vf >= adap->num_vfs)
		return -EINVAL;
	vfinfo = &adap->vfinfo[vf];

	ivi->vf = vf;
	ivi->max_tx_rate = vfinfo->tx_rate;
	ivi->min_tx_rate = 0;
	ether_addr_copy(ivi->mac, vfinfo->vf_mac_addr);
	ivi->vlan = vfinfo->vlan;
	ivi->linkstate = vfinfo->link_state;
	return 0;
}

static int cxgb4_mgmt_get_phys_port_id(struct net_device *dev,
				       struct netdev_phys_item_id *ppid)
{
	struct port_info *pi = netdev_priv(dev);
	unsigned int phy_port_id;

	phy_port_id = pi->adapter->adap_idx * 10 + pi->port_id;
	ppid->id_len = sizeof(phy_port_id);
	memcpy(ppid->id, &phy_port_id, ppid->id_len);
	return 0;
}

static int cxgb4_mgmt_set_vf_rate(struct net_device *dev, int vf,
				  int min_tx_rate, int max_tx_rate)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	unsigned int link_ok, speed, mtu;
	u32 fw_pfvf, fw_class;
	int class_id = vf;
	int ret;
	u16 pktsize;

	if (vf >= adap->num_vfs)
		return -EINVAL;

	if (min_tx_rate) {
		dev_err(adap->pdev_dev,
			"Min tx rate (%d) (> 0) for VF %d is Invalid.\n",
			min_tx_rate, vf);
		return -EINVAL;
	}

	if (max_tx_rate == 0) {
		/* unbind VF to to any Traffic Class */
		fw_pfvf =
		    (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_PFVF) |
		     FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_PFVF_SCHEDCLASS_ETH));
		fw_class = 0xffffffff;
		ret = t4_set_params(adap, adap->mbox, adap->pf, vf + 1, 1,
				    &fw_pfvf, &fw_class);
		if (ret) {
			dev_err(adap->pdev_dev,
				"Err %d in unbinding PF %d VF %d from TX Rate Limiting\n",
				ret, adap->pf, vf);
			return -EINVAL;
		}
		dev_info(adap->pdev_dev,
			 "PF %d VF %d is unbound from TX Rate Limiting\n",
			 adap->pf, vf);
		adap->vfinfo[vf].tx_rate = 0;
		return 0;
	}

	ret = t4_get_link_params(pi, &link_ok, &speed, &mtu);
	if (ret != FW_SUCCESS) {
		dev_err(adap->pdev_dev,
			"Failed to get link information for VF %d\n", vf);
		return -EINVAL;
	}

	if (!link_ok) {
		dev_err(adap->pdev_dev, "Link down for VF %d\n", vf);
		return -EINVAL;
	}

	if (max_tx_rate > speed) {
		dev_err(adap->pdev_dev,
			"Max tx rate %d for VF %d can't be > link-speed %u",
			max_tx_rate, vf, speed);
		return -EINVAL;
	}

	pktsize = mtu;
	/* subtract ethhdr size and 4 bytes crc since, f/w appends it */
	pktsize = pktsize - sizeof(struct ethhdr) - 4;
	/* subtract ipv4 hdr size, tcp hdr size to get typical IPv4 MSS size */
	pktsize = pktsize - sizeof(struct iphdr) - sizeof(struct tcphdr);
	/* configure Traffic Class for rate-limiting */
	ret = t4_sched_params(adap, SCHED_CLASS_TYPE_PACKET,
			      SCHED_CLASS_LEVEL_CL_RL,
			      SCHED_CLASS_MODE_CLASS,
			      SCHED_CLASS_RATEUNIT_BITS,
			      SCHED_CLASS_RATEMODE_ABS,
			      pi->tx_chan, class_id, 0,
			      max_tx_rate * 1000, 0, pktsize, 0);
	if (ret) {
		dev_err(adap->pdev_dev, "Err %d for Traffic Class config\n",
			ret);
		return -EINVAL;
	}
	dev_info(adap->pdev_dev,
		 "Class %d with MSS %u configured with rate %u\n",
		 class_id, pktsize, max_tx_rate);

	/* bind VF to configured Traffic Class */
	fw_pfvf = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_PFVF) |
		   FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_PFVF_SCHEDCLASS_ETH));
	fw_class = class_id;
	ret = t4_set_params(adap, adap->mbox, adap->pf, vf + 1, 1, &fw_pfvf,
			    &fw_class);
	if (ret) {
		dev_err(adap->pdev_dev,
			"Err %d in binding PF %d VF %d to Traffic Class %d\n",
			ret, adap->pf, vf, class_id);
		return -EINVAL;
	}
	dev_info(adap->pdev_dev, "PF %d VF %d is bound to Class %d\n",
		 adap->pf, vf, class_id);
	adap->vfinfo[vf].tx_rate = max_tx_rate;
	return 0;
}

static int cxgb4_mgmt_set_vf_vlan(struct net_device *dev, int vf,
				  u16 vlan, u8 qos, __be16 vlan_proto)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	int ret;

	if (vf >= adap->num_vfs || vlan > 4095 || qos > 7)
		return -EINVAL;

	if (vlan_proto != htons(ETH_P_8021Q) || qos != 0)
		return -EPROTONOSUPPORT;

	ret = t4_set_vlan_acl(adap, adap->mbox, vf + 1, vlan);
	if (!ret) {
		adap->vfinfo[vf].vlan = vlan;
		return 0;
	}

	dev_err(adap->pdev_dev, "Err %d %s VLAN ACL for PF/VF %d/%d\n",
		ret, (vlan ? "setting" : "clearing"), adap->pf, vf);
	return ret;
}

static int cxgb4_mgmt_set_vf_link_state(struct net_device *dev, int vf,
					int link)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	u32 param, val;
	int ret = 0;

	if (vf >= adap->num_vfs)
		return -EINVAL;

	switch (link) {
	case IFLA_VF_LINK_STATE_AUTO:
		val = FW_VF_LINK_STATE_AUTO;
		break;

	case IFLA_VF_LINK_STATE_ENABLE:
		val = FW_VF_LINK_STATE_ENABLE;
		break;

	case IFLA_VF_LINK_STATE_DISABLE:
		val = FW_VF_LINK_STATE_DISABLE;
		break;

	default:
		return -EINVAL;
	}

	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_PFVF) |
		 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_PFVF_LINK_STATE));
	ret = t4_set_params(adap, adap->mbox, adap->pf, vf + 1, 1,
			    &param, &val);
	if (ret) {
		dev_err(adap->pdev_dev,
			"Error %d in setting PF %d VF %d link state\n",
			ret, adap->pf, vf);
		return -EINVAL;
	}

	adap->vfinfo[vf].link_state = link;
	return ret;
}
#endif /* CONFIG_PCI_IOV */

static int cxgb_set_mac_addr(struct net_device *dev, void *p)
{
	int ret;
	struct sockaddr *addr = p;
	struct port_info *pi = netdev_priv(dev);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	ret = cxgb4_update_mac_filt(pi, pi->viid, &pi->xact_addr_filt,
				    addr->sa_data, true, &pi->smt_idx);
	if (ret < 0)
		return ret;

	eth_hw_addr_set(dev, addr->sa_data);
	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void cxgb_netpoll(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;

	if (adap->flags & CXGB4_USING_MSIX) {
		int i;
		struct sge_eth_rxq *rx = &adap->sge.ethrxq[pi->first_qset];

		for (i = pi->nqsets; i; i--, rx++)
			t4_sge_intr_msix(0, &rx->rspq);
	} else
		t4_intr_handler(adap)(0, adap);
}
#endif

static int cxgb_set_tx_maxrate(struct net_device *dev, int index, u32 rate)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	struct ch_sched_queue qe = { 0 };
	struct ch_sched_params p = { 0 };
	struct sched_class *e;
	u32 req_rate;
	int err = 0;

	if (!can_sched(dev))
		return -ENOTSUPP;

	if (index < 0 || index > pi->nqsets - 1)
		return -EINVAL;

	if (!(adap->flags & CXGB4_FULL_INIT_DONE)) {
		dev_err(adap->pdev_dev,
			"Failed to rate limit on queue %d. Link Down?\n",
			index);
		return -EINVAL;
	}

	qe.queue = index;
	e = cxgb4_sched_queue_lookup(dev, &qe);
	if (e && e->info.u.params.level != SCHED_CLASS_LEVEL_CL_RL) {
		dev_err(adap->pdev_dev,
			"Queue %u already bound to class %u of type: %u\n",
			index, e->idx, e->info.u.params.level);
		return -EBUSY;
	}

	/* Convert from Mbps to Kbps */
	req_rate = rate * 1000;

	/* Max rate is 100 Gbps */
	if (req_rate > SCHED_MAX_RATE_KBPS) {
		dev_err(adap->pdev_dev,
			"Invalid rate %u Mbps, Max rate is %u Mbps\n",
			rate, SCHED_MAX_RATE_KBPS / 1000);
		return -ERANGE;
	}

	/* First unbind the queue from any existing class */
	memset(&qe, 0, sizeof(qe));
	qe.queue = index;
	qe.class = SCHED_CLS_NONE;

	err = cxgb4_sched_class_unbind(dev, (void *)(&qe), SCHED_QUEUE);
	if (err) {
		dev_err(adap->pdev_dev,
			"Unbinding Queue %d on port %d fail. Err: %d\n",
			index, pi->port_id, err);
		return err;
	}

	/* Queue already unbound */
	if (!req_rate)
		return 0;

	/* Fetch any available unused or matching scheduling class */
	p.type = SCHED_CLASS_TYPE_PACKET;
	p.u.params.level    = SCHED_CLASS_LEVEL_CL_RL;
	p.u.params.mode     = SCHED_CLASS_MODE_CLASS;
	p.u.params.rateunit = SCHED_CLASS_RATEUNIT_BITS;
	p.u.params.ratemode = SCHED_CLASS_RATEMODE_ABS;
	p.u.params.channel  = pi->tx_chan;
	p.u.params.class    = SCHED_CLS_NONE;
	p.u.params.minrate  = 0;
	p.u.params.maxrate  = req_rate;
	p.u.params.weight   = 0;
	p.u.params.pktsize  = dev->mtu;

	e = cxgb4_sched_class_alloc(dev, &p);
	if (!e)
		return -ENOMEM;

	/* Bind the queue to a scheduling class */
	memset(&qe, 0, sizeof(qe));
	qe.queue = index;
	qe.class = e->idx;

	err = cxgb4_sched_class_bind(dev, (void *)(&qe), SCHED_QUEUE);
	if (err)
		dev_err(adap->pdev_dev,
			"Queue rate limiting failed. Err: %d\n", err);
	return err;
}

static int cxgb_setup_tc_flower(struct net_device *dev,
				struct flow_cls_offload *cls_flower)
{
	switch (cls_flower->command) {
	case FLOW_CLS_REPLACE:
		return cxgb4_tc_flower_replace(dev, cls_flower);
	case FLOW_CLS_DESTROY:
		return cxgb4_tc_flower_destroy(dev, cls_flower);
	case FLOW_CLS_STATS:
		return cxgb4_tc_flower_stats(dev, cls_flower);
	default:
		return -EOPNOTSUPP;
	}
}

static int cxgb_setup_tc_cls_u32(struct net_device *dev,
				 struct tc_cls_u32_offload *cls_u32)
{
	switch (cls_u32->command) {
	case TC_CLSU32_NEW_KNODE:
	case TC_CLSU32_REPLACE_KNODE:
		return cxgb4_config_knode(dev, cls_u32);
	case TC_CLSU32_DELETE_KNODE:
		return cxgb4_delete_knode(dev, cls_u32);
	default:
		return -EOPNOTSUPP;
	}
}

static int cxgb_setup_tc_matchall(struct net_device *dev,
				  struct tc_cls_matchall_offload *cls_matchall,
				  bool ingress)
{
	struct adapter *adap = netdev2adap(dev);

	if (!adap->tc_matchall)
		return -ENOMEM;

	switch (cls_matchall->command) {
	case TC_CLSMATCHALL_REPLACE:
		return cxgb4_tc_matchall_replace(dev, cls_matchall, ingress);
	case TC_CLSMATCHALL_DESTROY:
		return cxgb4_tc_matchall_destroy(dev, cls_matchall, ingress);
	case TC_CLSMATCHALL_STATS:
		if (ingress)
			return cxgb4_tc_matchall_stats(dev, cls_matchall);
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int cxgb_setup_tc_block_ingress_cb(enum tc_setup_type type,
					  void *type_data, void *cb_priv)
{
	struct net_device *dev = cb_priv;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);

	if (!(adap->flags & CXGB4_FULL_INIT_DONE)) {
		dev_err(adap->pdev_dev,
			"Failed to setup tc on port %d. Link Down?\n",
			pi->port_id);
		return -EINVAL;
	}

	if (!tc_cls_can_offload_and_chain0(dev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSU32:
		return cxgb_setup_tc_cls_u32(dev, type_data);
	case TC_SETUP_CLSFLOWER:
		return cxgb_setup_tc_flower(dev, type_data);
	case TC_SETUP_CLSMATCHALL:
		return cxgb_setup_tc_matchall(dev, type_data, true);
	default:
		return -EOPNOTSUPP;
	}
}

static int cxgb_setup_tc_block_egress_cb(enum tc_setup_type type,
					 void *type_data, void *cb_priv)
{
	struct net_device *dev = cb_priv;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);

	if (!(adap->flags & CXGB4_FULL_INIT_DONE)) {
		dev_err(adap->pdev_dev,
			"Failed to setup tc on port %d. Link Down?\n",
			pi->port_id);
		return -EINVAL;
	}

	if (!tc_cls_can_offload_and_chain0(dev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSMATCHALL:
		return cxgb_setup_tc_matchall(dev, type_data, false);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int cxgb_setup_tc_mqprio(struct net_device *dev,
				struct tc_mqprio_qopt_offload *mqprio)
{
	struct adapter *adap = netdev2adap(dev);

	if (!is_ethofld(adap) || !adap->tc_mqprio)
		return -ENOMEM;

	return cxgb4_setup_tc_mqprio(dev, mqprio);
}

static LIST_HEAD(cxgb_block_cb_list);

static int cxgb_setup_tc_block(struct net_device *dev,
			       struct flow_block_offload *f)
{
	struct port_info *pi = netdev_priv(dev);
	flow_setup_cb_t *cb;
	bool ingress_only;

	pi->tc_block_shared = f->block_shared;
	if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS) {
		cb = cxgb_setup_tc_block_egress_cb;
		ingress_only = false;
	} else {
		cb = cxgb_setup_tc_block_ingress_cb;
		ingress_only = true;
	}

	return flow_block_cb_setup_simple(f, &cxgb_block_cb_list,
					  cb, pi, dev, ingress_only);
}

static int cxgb_setup_tc(struct net_device *dev, enum tc_setup_type type,
			 void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
		return cxgb_setup_tc_mqprio(dev, type_data);
	case TC_SETUP_BLOCK:
		return cxgb_setup_tc_block(dev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int cxgb_udp_tunnel_unset_port(struct net_device *netdev,
				      unsigned int table, unsigned int entry,
				      struct udp_tunnel_info *ti)
{
	struct port_info *pi = netdev_priv(netdev);
	struct adapter *adapter = pi->adapter;
	u8 match_all_mac[] = { 0, 0, 0, 0, 0, 0 };
	int ret = 0, i;

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		adapter->vxlan_port = 0;
		t4_write_reg(adapter, MPS_RX_VXLAN_TYPE_A, 0);
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		adapter->geneve_port = 0;
		t4_write_reg(adapter, MPS_RX_GENEVE_TYPE_A, 0);
		break;
	default:
		return -EINVAL;
	}

	/* Matchall mac entries can be deleted only after all tunnel ports
	 * are brought down or removed.
	 */
	if (!adapter->rawf_cnt)
		return 0;
	for_each_port(adapter, i) {
		pi = adap2pinfo(adapter, i);
		ret = t4_free_raw_mac_filt(adapter, pi->viid,
					   match_all_mac, match_all_mac,
					   adapter->rawf_start + pi->port_id,
					   1, pi->port_id, false);
		if (ret < 0) {
			netdev_info(netdev, "Failed to free mac filter entry, for port %d\n",
				    i);
			return ret;
		}
	}

	return 0;
}

static int cxgb_udp_tunnel_set_port(struct net_device *netdev,
				    unsigned int table, unsigned int entry,
				    struct udp_tunnel_info *ti)
{
	struct port_info *pi = netdev_priv(netdev);
	struct adapter *adapter = pi->adapter;
	u8 match_all_mac[] = { 0, 0, 0, 0, 0, 0 };
	int i, ret;

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		adapter->vxlan_port = ti->port;
		t4_write_reg(adapter, MPS_RX_VXLAN_TYPE_A,
			     VXLAN_V(be16_to_cpu(ti->port)) | VXLAN_EN_F);
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		adapter->geneve_port = ti->port;
		t4_write_reg(adapter, MPS_RX_GENEVE_TYPE_A,
			     GENEVE_V(be16_to_cpu(ti->port)) | GENEVE_EN_F);
		break;
	default:
		return -EINVAL;
	}

	/* Create a 'match all' mac filter entry for inner mac,
	 * if raw mac interface is supported. Once the linux kernel provides
	 * driver entry points for adding/deleting the inner mac addresses,
	 * we will remove this 'match all' entry and fallback to adding
	 * exact match filters.
	 */
	for_each_port(adapter, i) {
		pi = adap2pinfo(adapter, i);

		ret = t4_alloc_raw_mac_filt(adapter, pi->viid,
					    match_all_mac,
					    match_all_mac,
					    adapter->rawf_start + pi->port_id,
					    1, pi->port_id, false);
		if (ret < 0) {
			netdev_info(netdev, "Failed to allocate a mac filter entry, not adding port %d\n",
				    be16_to_cpu(ti->port));
			return ret;
		}
	}

	return 0;
}

static const struct udp_tunnel_nic_info cxgb_udp_tunnels = {
	.set_port	= cxgb_udp_tunnel_set_port,
	.unset_port	= cxgb_udp_tunnel_unset_port,
	.tables		= {
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_VXLAN,  },
		{ .n_entries = 1, .tunnel_types = UDP_TUNNEL_TYPE_GENEVE, },
	},
};

static netdev_features_t cxgb_features_check(struct sk_buff *skb,
					     struct net_device *dev,
					     netdev_features_t features)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	if (CHELSIO_CHIP_VERSION(adapter->params.chip) < CHELSIO_T6)
		return features;

	/* Check if hw supports offload for this packet */
	if (!skb->encapsulation || cxgb_encap_offload_supported(skb))
		return features;

	/* Offload is not supported for this encapsulated packet */
	return features & ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
}

static netdev_features_t cxgb_fix_features(struct net_device *dev,
					   netdev_features_t features)
{
	/* Disable GRO, if RX_CSUM is disabled */
	if (!(features & NETIF_F_RXCSUM))
		features &= ~NETIF_F_GRO;

	return features;
}

static const struct net_device_ops cxgb4_netdev_ops = {
	.ndo_open             = cxgb_open,
	.ndo_stop             = cxgb_close,
	.ndo_start_xmit       = t4_start_xmit,
	.ndo_select_queue     =	cxgb_select_queue,
	.ndo_get_stats64      = cxgb_get_stats,
	.ndo_set_rx_mode      = cxgb_set_rxmode,
	.ndo_set_mac_address  = cxgb_set_mac_addr,
	.ndo_set_features     = cxgb_set_features,
	.ndo_validate_addr    = eth_validate_addr,
	.ndo_eth_ioctl         = cxgb_ioctl,
	.ndo_change_mtu       = cxgb_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller  = cxgb_netpoll,
#endif
#ifdef CONFIG_CHELSIO_T4_FCOE
	.ndo_fcoe_enable      = cxgb_fcoe_enable,
	.ndo_fcoe_disable     = cxgb_fcoe_disable,
#endif /* CONFIG_CHELSIO_T4_FCOE */
	.ndo_set_tx_maxrate   = cxgb_set_tx_maxrate,
	.ndo_setup_tc         = cxgb_setup_tc,
	.ndo_features_check   = cxgb_features_check,
	.ndo_fix_features     = cxgb_fix_features,
};

#ifdef CONFIG_PCI_IOV
static const struct net_device_ops cxgb4_mgmt_netdev_ops = {
	.ndo_open               = cxgb4_mgmt_open,
	.ndo_set_vf_mac         = cxgb4_mgmt_set_vf_mac,
	.ndo_get_vf_config      = cxgb4_mgmt_get_vf_config,
	.ndo_set_vf_rate        = cxgb4_mgmt_set_vf_rate,
	.ndo_get_phys_port_id   = cxgb4_mgmt_get_phys_port_id,
	.ndo_set_vf_vlan        = cxgb4_mgmt_set_vf_vlan,
	.ndo_set_vf_link_state	= cxgb4_mgmt_set_vf_link_state,
};

static void cxgb4_mgmt_get_drvinfo(struct net_device *dev,
				   struct ethtool_drvinfo *info)
{
	struct adapter *adapter = netdev2adap(dev);

	strlcpy(info->driver, cxgb4_driver_name, sizeof(info->driver));
	strlcpy(info->bus_info, pci_name(adapter->pdev),
		sizeof(info->bus_info));
}

static const struct ethtool_ops cxgb4_mgmt_ethtool_ops = {
	.get_drvinfo       = cxgb4_mgmt_get_drvinfo,
};
#endif

static void notify_fatal_err(struct work_struct *work)
{
	struct adapter *adap;

	adap = container_of(work, struct adapter, fatal_err_notify_task);
	notify_ulds(adap, CXGB4_STATE_FATAL_ERROR);
}

void t4_fatal_err(struct adapter *adap)
{
	int port;

	if (pci_channel_offline(adap->pdev))
		return;

	/* Disable the SGE since ULDs are going to free resources that
	 * could be exposed to the adapter.  RDMA MWs for example...
	 */
	t4_shutdown_adapter(adap);
	for_each_port(adap, port) {
		struct net_device *dev = adap->port[port];

		/* If we get here in very early initialization the network
		 * devices may not have been set up yet.
		 */
		if (!dev)
			continue;

		netif_tx_stop_all_queues(dev);
		netif_carrier_off(dev);
	}
	dev_alert(adap->pdev_dev, "encountered fatal error, adapter stopped\n");
	queue_work(adap->workq, &adap->fatal_err_notify_task);
}

static void setup_memwin(struct adapter *adap)
{
	u32 nic_win_base = t4_get_util_window(adap);

	t4_setup_memwin(adap, nic_win_base, MEMWIN_NIC);
}

static void setup_memwin_rdma(struct adapter *adap)
{
	if (adap->vres.ocq.size) {
		u32 start;
		unsigned int sz_kb;

		start = t4_read_pcie_cfg4(adap, PCI_BASE_ADDRESS_2);
		start &= PCI_BASE_ADDRESS_MEM_MASK;
		start += OCQ_WIN_OFFSET(adap->pdev, &adap->vres);
		sz_kb = roundup_pow_of_two(adap->vres.ocq.size) >> 10;
		t4_write_reg(adap,
			     PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_BASE_WIN_A, 3),
			     start | BIR_V(1) | WINDOW_V(ilog2(sz_kb)));
		t4_write_reg(adap,
			     PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_OFFSET_A, 3),
			     adap->vres.ocq.start);
		t4_read_reg(adap,
			    PCIE_MEM_ACCESS_REG(PCIE_MEM_ACCESS_OFFSET_A, 3));
	}
}

/* HMA Definitions */

/* The maximum number of address that can be send in a single FW cmd */
#define HMA_MAX_ADDR_IN_CMD	5

#define HMA_PAGE_SIZE		PAGE_SIZE

#define HMA_MAX_NO_FW_ADDRESS	(16 << 10)  /* FW supports 16K addresses */

#define HMA_PAGE_ORDER					\
	((HMA_PAGE_SIZE < HMA_MAX_NO_FW_ADDRESS) ?	\
	ilog2(HMA_MAX_NO_FW_ADDRESS / HMA_PAGE_SIZE) : 0)

/* The minimum and maximum possible HMA sizes that can be specified in the FW
 * configuration(in units of MB).
 */
#define HMA_MIN_TOTAL_SIZE	1
#define HMA_MAX_TOTAL_SIZE				\
	(((HMA_PAGE_SIZE << HMA_PAGE_ORDER) *		\
	  HMA_MAX_NO_FW_ADDRESS) >> 20)

static void adap_free_hma_mem(struct adapter *adapter)
{
	struct scatterlist *iter;
	struct page *page;
	int i;

	if (!adapter->hma.sgt)
		return;

	if (adapter->hma.flags & HMA_DMA_MAPPED_FLAG) {
		dma_unmap_sg(adapter->pdev_dev, adapter->hma.sgt->sgl,
			     adapter->hma.sgt->nents, DMA_BIDIRECTIONAL);
		adapter->hma.flags &= ~HMA_DMA_MAPPED_FLAG;
	}

	for_each_sg(adapter->hma.sgt->sgl, iter,
		    adapter->hma.sgt->orig_nents, i) {
		page = sg_page(iter);
		if (page)
			__free_pages(page, HMA_PAGE_ORDER);
	}

	kfree(adapter->hma.phy_addr);
	sg_free_table(adapter->hma.sgt);
	kfree(adapter->hma.sgt);
	adapter->hma.sgt = NULL;
}

static int adap_config_hma(struct adapter *adapter)
{
	struct scatterlist *sgl, *iter;
	struct sg_table *sgt;
	struct page *newpage;
	unsigned int i, j, k;
	u32 param, hma_size;
	unsigned int ncmds;
	size_t page_size;
	u32 page_order;
	int node, ret;

	/* HMA is supported only for T6+ cards.
	 * Avoid initializing HMA in kdump kernels.
	 */
	if (is_kdump_kernel() ||
	    CHELSIO_CHIP_VERSION(adapter->params.chip) < CHELSIO_T6)
		return 0;

	/* Get the HMA region size required by fw */
	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_HMA_SIZE));
	ret = t4_query_params(adapter, adapter->mbox, adapter->pf, 0,
			      1, &param, &hma_size);
	/* An error means card has its own memory or HMA is not supported by
	 * the firmware. Return without any errors.
	 */
	if (ret || !hma_size)
		return 0;

	if (hma_size < HMA_MIN_TOTAL_SIZE ||
	    hma_size > HMA_MAX_TOTAL_SIZE) {
		dev_err(adapter->pdev_dev,
			"HMA size %uMB beyond bounds(%u-%lu)MB\n",
			hma_size, HMA_MIN_TOTAL_SIZE, HMA_MAX_TOTAL_SIZE);
		return -EINVAL;
	}

	page_size = HMA_PAGE_SIZE;
	page_order = HMA_PAGE_ORDER;
	adapter->hma.sgt = kzalloc(sizeof(*adapter->hma.sgt), GFP_KERNEL);
	if (unlikely(!adapter->hma.sgt)) {
		dev_err(adapter->pdev_dev, "HMA SG table allocation failed\n");
		return -ENOMEM;
	}
	sgt = adapter->hma.sgt;
	/* FW returned value will be in MB's
	 */
	sgt->orig_nents = (hma_size << 20) / (page_size << page_order);
	if (sg_alloc_table(sgt, sgt->orig_nents, GFP_KERNEL)) {
		dev_err(adapter->pdev_dev, "HMA SGL allocation failed\n");
		kfree(adapter->hma.sgt);
		adapter->hma.sgt = NULL;
		return -ENOMEM;
	}

	sgl = adapter->hma.sgt->sgl;
	node = dev_to_node(adapter->pdev_dev);
	for_each_sg(sgl, iter, sgt->orig_nents, i) {
		newpage = alloc_pages_node(node, __GFP_NOWARN | GFP_KERNEL |
					   __GFP_ZERO, page_order);
		if (!newpage) {
			dev_err(adapter->pdev_dev,
				"Not enough memory for HMA page allocation\n");
			ret = -ENOMEM;
			goto free_hma;
		}
		sg_set_page(iter, newpage, page_size << page_order, 0);
	}

	sgt->nents = dma_map_sg(adapter->pdev_dev, sgl, sgt->orig_nents,
				DMA_BIDIRECTIONAL);
	if (!sgt->nents) {
		dev_err(adapter->pdev_dev,
			"Not enough memory for HMA DMA mapping");
		ret = -ENOMEM;
		goto free_hma;
	}
	adapter->hma.flags |= HMA_DMA_MAPPED_FLAG;

	adapter->hma.phy_addr = kcalloc(sgt->nents, sizeof(dma_addr_t),
					GFP_KERNEL);
	if (unlikely(!adapter->hma.phy_addr))
		goto free_hma;

	for_each_sg(sgl, iter, sgt->nents, i) {
		newpage = sg_page(iter);
		adapter->hma.phy_addr[i] = sg_dma_address(iter);
	}

	ncmds = DIV_ROUND_UP(sgt->nents, HMA_MAX_ADDR_IN_CMD);
	/* Pass on the addresses to firmware */
	for (i = 0, k = 0; i < ncmds; i++, k += HMA_MAX_ADDR_IN_CMD) {
		struct fw_hma_cmd hma_cmd;
		u8 naddr = HMA_MAX_ADDR_IN_CMD;
		u8 soc = 0, eoc = 0;
		u8 hma_mode = 1; /* Presently we support only Page table mode */

		soc = (i == 0) ? 1 : 0;
		eoc = (i == ncmds - 1) ? 1 : 0;

		/* For last cmd, set naddr corresponding to remaining
		 * addresses
		 */
		if (i == ncmds - 1) {
			naddr = sgt->nents % HMA_MAX_ADDR_IN_CMD;
			naddr = naddr ? naddr : HMA_MAX_ADDR_IN_CMD;
		}
		memset(&hma_cmd, 0, sizeof(hma_cmd));
		hma_cmd.op_pkd = htonl(FW_CMD_OP_V(FW_HMA_CMD) |
				       FW_CMD_REQUEST_F | FW_CMD_WRITE_F);
		hma_cmd.retval_len16 = htonl(FW_LEN16(hma_cmd));

		hma_cmd.mode_to_pcie_params =
			htonl(FW_HMA_CMD_MODE_V(hma_mode) |
			      FW_HMA_CMD_SOC_V(soc) | FW_HMA_CMD_EOC_V(eoc));

		/* HMA cmd size specified in MB's */
		hma_cmd.naddr_size =
			htonl(FW_HMA_CMD_SIZE_V(hma_size) |
			      FW_HMA_CMD_NADDR_V(naddr));

		/* Total Page size specified in units of 4K */
		hma_cmd.addr_size_pkd =
			htonl(FW_HMA_CMD_ADDR_SIZE_V
				((page_size << page_order) >> 12));

		/* Fill the 5 addresses */
		for (j = 0; j < naddr; j++) {
			hma_cmd.phy_address[j] =
				cpu_to_be64(adapter->hma.phy_addr[j + k]);
		}
		ret = t4_wr_mbox(adapter, adapter->mbox, &hma_cmd,
				 sizeof(hma_cmd), &hma_cmd);
		if (ret) {
			dev_err(adapter->pdev_dev,
				"HMA FW command failed with err %d\n", ret);
			goto free_hma;
		}
	}

	if (!ret)
		dev_info(adapter->pdev_dev,
			 "Reserved %uMB host memory for HMA\n", hma_size);
	return ret;

free_hma:
	adap_free_hma_mem(adapter);
	return ret;
}

static int adap_init1(struct adapter *adap, struct fw_caps_config_cmd *c)
{
	u32 v;
	int ret;

	/* Now that we've successfully configured and initialized the adapter
	 * can ask the Firmware what resources it has provisioned for us.
	 */
	ret = t4_get_pfres(adap);
	if (ret) {
		dev_err(adap->pdev_dev,
			"Unable to retrieve resource provisioning information\n");
		return ret;
	}

	/* get device capabilities */
	memset(c, 0, sizeof(*c));
	c->op_to_write = htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
			       FW_CMD_REQUEST_F | FW_CMD_READ_F);
	c->cfvalid_to_len16 = htonl(FW_LEN16(*c));
	ret = t4_wr_mbox(adap, adap->mbox, c, sizeof(*c), c);
	if (ret < 0)
		return ret;

	c->op_to_write = htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
			       FW_CMD_REQUEST_F | FW_CMD_WRITE_F);
	ret = t4_wr_mbox(adap, adap->mbox, c, sizeof(*c), NULL);
	if (ret < 0)
		return ret;

	ret = t4_config_glbl_rss(adap, adap->pf,
				 FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL,
				 FW_RSS_GLB_CONFIG_CMD_TNLMAPEN_F |
				 FW_RSS_GLB_CONFIG_CMD_TNLALLLKP_F);
	if (ret < 0)
		return ret;

	ret = t4_cfg_pfvf(adap, adap->mbox, adap->pf, 0, adap->sge.egr_sz, 64,
			  MAX_INGQ, 0, 0, 4, 0xf, 0xf, 16, FW_CMD_CAP_PF,
			  FW_CMD_CAP_PF);
	if (ret < 0)
		return ret;

	t4_sge_init(adap);

	/* tweak some settings */
	t4_write_reg(adap, TP_SHIFT_CNT_A, 0x64f8849);
	t4_write_reg(adap, ULP_RX_TDDP_PSZ_A, HPZ0_V(PAGE_SHIFT - 12));
	t4_write_reg(adap, TP_PIO_ADDR_A, TP_INGRESS_CONFIG_A);
	v = t4_read_reg(adap, TP_PIO_DATA_A);
	t4_write_reg(adap, TP_PIO_DATA_A, v & ~CSUM_HAS_PSEUDO_HDR_F);

	/* first 4 Tx modulation queues point to consecutive Tx channels */
	adap->params.tp.tx_modq_map = 0xE4;
	t4_write_reg(adap, TP_TX_MOD_QUEUE_REQ_MAP_A,
		     TX_MOD_QUEUE_REQ_MAP_V(adap->params.tp.tx_modq_map));

	/* associate each Tx modulation queue with consecutive Tx channels */
	v = 0x84218421;
	t4_write_indirect(adap, TP_PIO_ADDR_A, TP_PIO_DATA_A,
			  &v, 1, TP_TX_SCHED_HDR_A);
	t4_write_indirect(adap, TP_PIO_ADDR_A, TP_PIO_DATA_A,
			  &v, 1, TP_TX_SCHED_FIFO_A);
	t4_write_indirect(adap, TP_PIO_ADDR_A, TP_PIO_DATA_A,
			  &v, 1, TP_TX_SCHED_PCMD_A);

#define T4_TX_MODQ_10G_WEIGHT_DEFAULT 16 /* in KB units */
	if (is_offload(adap)) {
		t4_write_reg(adap, TP_TX_MOD_QUEUE_WEIGHT0_A,
			     TX_MODQ_WEIGHT0_V(T4_TX_MODQ_10G_WEIGHT_DEFAULT) |
			     TX_MODQ_WEIGHT1_V(T4_TX_MODQ_10G_WEIGHT_DEFAULT) |
			     TX_MODQ_WEIGHT2_V(T4_TX_MODQ_10G_WEIGHT_DEFAULT) |
			     TX_MODQ_WEIGHT3_V(T4_TX_MODQ_10G_WEIGHT_DEFAULT));
		t4_write_reg(adap, TP_TX_MOD_CHANNEL_WEIGHT_A,
			     TX_MODQ_WEIGHT0_V(T4_TX_MODQ_10G_WEIGHT_DEFAULT) |
			     TX_MODQ_WEIGHT1_V(T4_TX_MODQ_10G_WEIGHT_DEFAULT) |
			     TX_MODQ_WEIGHT2_V(T4_TX_MODQ_10G_WEIGHT_DEFAULT) |
			     TX_MODQ_WEIGHT3_V(T4_TX_MODQ_10G_WEIGHT_DEFAULT));
	}

	/* get basic stuff going */
	return t4_early_init(adap, adap->pf);
}

/*
 * Max # of ATIDs.  The absolute HW max is 16K but we keep it lower.
 */
#define MAX_ATIDS 8192U

/*
 * Phase 0 of initialization: contact FW, obtain config, perform basic init.
 *
 * If the firmware we're dealing with has Configuration File support, then
 * we use that to perform all configuration
 */

/*
 * Tweak configuration based on module parameters, etc.  Most of these have
 * defaults assigned to them by Firmware Configuration Files (if we're using
 * them) but need to be explicitly set if we're using hard-coded
 * initialization.  But even in the case of using Firmware Configuration
 * Files, we'd like to expose the ability to change these via module
 * parameters so these are essentially common tweaks/settings for
 * Configuration Files and hard-coded initialization ...
 */
static int adap_init0_tweaks(struct adapter *adapter)
{
	/*
	 * Fix up various Host-Dependent Parameters like Page Size, Cache
	 * Line Size, etc.  The firmware default is for a 4KB Page Size and
	 * 64B Cache Line Size ...
	 */
	t4_fixup_host_params(adapter, PAGE_SIZE, L1_CACHE_BYTES);

	/*
	 * Process module parameters which affect early initialization.
	 */
	if (rx_dma_offset != 2 && rx_dma_offset != 0) {
		dev_err(&adapter->pdev->dev,
			"Ignoring illegal rx_dma_offset=%d, using 2\n",
			rx_dma_offset);
		rx_dma_offset = 2;
	}
	t4_set_reg_field(adapter, SGE_CONTROL_A,
			 PKTSHIFT_V(PKTSHIFT_M),
			 PKTSHIFT_V(rx_dma_offset));

	/*
	 * Don't include the "IP Pseudo Header" in CPL_RX_PKT checksums: Linux
	 * adds the pseudo header itself.
	 */
	t4_tp_wr_bits_indirect(adapter, TP_INGRESS_CONFIG_A,
			       CSUM_HAS_PSEUDO_HDR_F, 0);

	return 0;
}

/* 10Gb/s-BT PHY Support. chip-external 10Gb/s-BT PHYs are complex chips
 * unto themselves and they contain their own firmware to perform their
 * tasks ...
 */
static int phy_aq1202_version(const u8 *phy_fw_data,
			      size_t phy_fw_size)
{
	int offset;

	/* At offset 0x8 you're looking for the primary image's
	 * starting offset which is 3 Bytes wide
	 *
	 * At offset 0xa of the primary image, you look for the offset
	 * of the DRAM segment which is 3 Bytes wide.
	 *
	 * The FW version is at offset 0x27e of the DRAM and is 2 Bytes
	 * wide
	 */
	#define be16(__p) (((__p)[0] << 8) | (__p)[1])
	#define le16(__p) ((__p)[0] | ((__p)[1] << 8))
	#define le24(__p) (le16(__p) | ((__p)[2] << 16))

	offset = le24(phy_fw_data + 0x8) << 12;
	offset = le24(phy_fw_data + offset + 0xa);
	return be16(phy_fw_data + offset + 0x27e);

	#undef be16
	#undef le16
	#undef le24
}

static struct info_10gbt_phy_fw {
	unsigned int phy_fw_id;		/* PCI Device ID */
	char *phy_fw_file;		/* /lib/firmware/ PHY Firmware file */
	int (*phy_fw_version)(const u8 *phy_fw_data, size_t phy_fw_size);
	int phy_flash;			/* Has FLASH for PHY Firmware */
} phy_info_array[] = {
	{
		PHY_AQ1202_DEVICEID,
		PHY_AQ1202_FIRMWARE,
		phy_aq1202_version,
		1,
	},
	{
		PHY_BCM84834_DEVICEID,
		PHY_BCM84834_FIRMWARE,
		NULL,
		0,
	},
	{ 0, NULL, NULL },
};

static struct info_10gbt_phy_fw *find_phy_info(int devid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(phy_info_array); i++) {
		if (phy_info_array[i].phy_fw_id == devid)
			return &phy_info_array[i];
	}
	return NULL;
}

/* Handle updating of chip-external 10Gb/s-BT PHY firmware.  This needs to
 * happen after the FW_RESET_CMD but before the FW_INITIALIZE_CMD.  On error
 * we return a negative error number.  If we transfer new firmware we return 1
 * (from t4_load_phy_fw()).  If we don't do anything we return 0.
 */
static int adap_init0_phy(struct adapter *adap)
{
	const struct firmware *phyf;
	int ret;
	struct info_10gbt_phy_fw *phy_info;

	/* Use the device ID to determine which PHY file to flash.
	 */
	phy_info = find_phy_info(adap->pdev->device);
	if (!phy_info) {
		dev_warn(adap->pdev_dev,
			 "No PHY Firmware file found for this PHY\n");
		return -EOPNOTSUPP;
	}

	/* If we have a T4 PHY firmware file under /lib/firmware/cxgb4/, then
	 * use that. The adapter firmware provides us with a memory buffer
	 * where we can load a PHY firmware file from the host if we want to
	 * override the PHY firmware File in flash.
	 */
	ret = request_firmware_direct(&phyf, phy_info->phy_fw_file,
				      adap->pdev_dev);
	if (ret < 0) {
		/* For adapters without FLASH attached to PHY for their
		 * firmware, it's obviously a fatal error if we can't get the
		 * firmware to the adapter.  For adapters with PHY firmware
		 * FLASH storage, it's worth a warning if we can't find the
		 * PHY Firmware but we'll neuter the error ...
		 */
		dev_err(adap->pdev_dev, "unable to find PHY Firmware image "
			"/lib/firmware/%s, error %d\n",
			phy_info->phy_fw_file, -ret);
		if (phy_info->phy_flash) {
			int cur_phy_fw_ver = 0;

			t4_phy_fw_ver(adap, &cur_phy_fw_ver);
			dev_warn(adap->pdev_dev, "continuing with, on-adapter "
				 "FLASH copy, version %#x\n", cur_phy_fw_ver);
			ret = 0;
		}

		return ret;
	}

	/* Load PHY Firmware onto adapter.
	 */
	ret = t4_load_phy_fw(adap, MEMWIN_NIC, phy_info->phy_fw_version,
			     (u8 *)phyf->data, phyf->size);
	if (ret < 0)
		dev_err(adap->pdev_dev, "PHY Firmware transfer error %d\n",
			-ret);
	else if (ret > 0) {
		int new_phy_fw_ver = 0;

		if (phy_info->phy_fw_version)
			new_phy_fw_ver = phy_info->phy_fw_version(phyf->data,
								  phyf->size);
		dev_info(adap->pdev_dev, "Successfully transferred PHY "
			 "Firmware /lib/firmware/%s, version %#x\n",
			 phy_info->phy_fw_file, new_phy_fw_ver);
	}

	release_firmware(phyf);

	return ret;
}

/*
 * Attempt to initialize the adapter via a Firmware Configuration File.
 */
static int adap_init0_config(struct adapter *adapter, int reset)
{
	char *fw_config_file, fw_config_file_path[256];
	u32 finiver, finicsum, cfcsum, param, val;
	struct fw_caps_config_cmd caps_cmd;
	unsigned long mtype = 0, maddr = 0;
	const struct firmware *cf;
	char *config_name = NULL;
	int config_issued = 0;
	int ret;

	/*
	 * Reset device if necessary.
	 */
	if (reset) {
		ret = t4_fw_reset(adapter, adapter->mbox,
				  PIORSTMODE_F | PIORST_F);
		if (ret < 0)
			goto bye;
	}

	/* If this is a 10Gb/s-BT adapter make sure the chip-external
	 * 10Gb/s-BT PHYs have up-to-date firmware.  Note that this step needs
	 * to be performed after any global adapter RESET above since some
	 * PHYs only have local RAM copies of the PHY firmware.
	 */
	if (is_10gbt_device(adapter->pdev->device)) {
		ret = adap_init0_phy(adapter);
		if (ret < 0)
			goto bye;
	}
	/*
	 * If we have a T4 configuration file under /lib/firmware/cxgb4/,
	 * then use that.  Otherwise, use the configuration file stored
	 * in the adapter flash ...
	 */
	switch (CHELSIO_CHIP_VERSION(adapter->params.chip)) {
	case CHELSIO_T4:
		fw_config_file = FW4_CFNAME;
		break;
	case CHELSIO_T5:
		fw_config_file = FW5_CFNAME;
		break;
	case CHELSIO_T6:
		fw_config_file = FW6_CFNAME;
		break;
	default:
		dev_err(adapter->pdev_dev, "Device %d is not supported\n",
		       adapter->pdev->device);
		ret = -EINVAL;
		goto bye;
	}

	ret = request_firmware(&cf, fw_config_file, adapter->pdev_dev);
	if (ret < 0) {
		config_name = "On FLASH";
		mtype = FW_MEMTYPE_CF_FLASH;
		maddr = t4_flash_cfg_addr(adapter);
	} else {
		u32 params[7], val[7];

		sprintf(fw_config_file_path,
			"/lib/firmware/%s", fw_config_file);
		config_name = fw_config_file_path;

		if (cf->size >= FLASH_CFG_MAX_SIZE)
			ret = -ENOMEM;
		else {
			params[0] = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
			     FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_CF));
			ret = t4_query_params(adapter, adapter->mbox,
					      adapter->pf, 0, 1, params, val);
			if (ret == 0) {
				/*
				 * For t4_memory_rw() below addresses and
				 * sizes have to be in terms of multiples of 4
				 * bytes.  So, if the Configuration File isn't
				 * a multiple of 4 bytes in length we'll have
				 * to write that out separately since we can't
				 * guarantee that the bytes following the
				 * residual byte in the buffer returned by
				 * request_firmware() are zeroed out ...
				 */
				size_t resid = cf->size & 0x3;
				size_t size = cf->size & ~0x3;
				__be32 *data = (__be32 *)cf->data;

				mtype = FW_PARAMS_PARAM_Y_G(val[0]);
				maddr = FW_PARAMS_PARAM_Z_G(val[0]) << 16;

				spin_lock(&adapter->win0_lock);
				ret = t4_memory_rw(adapter, 0, mtype, maddr,
						   size, data, T4_MEMORY_WRITE);
				if (ret == 0 && resid != 0) {
					union {
						__be32 word;
						char buf[4];
					} last;
					int i;

					last.word = data[size >> 2];
					for (i = resid; i < 4; i++)
						last.buf[i] = 0;
					ret = t4_memory_rw(adapter, 0, mtype,
							   maddr + size,
							   4, &last.word,
							   T4_MEMORY_WRITE);
				}
				spin_unlock(&adapter->win0_lock);
			}
		}

		release_firmware(cf);
		if (ret)
			goto bye;
	}

	val = 0;

	/* Ofld + Hash filter is supported. Older fw will fail this request and
	 * it is fine.
	 */
	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_HASHFILTER_WITH_OFLD));
	ret = t4_set_params(adapter, adapter->mbox, adapter->pf, 0,
			    1, &param, &val);

	/* FW doesn't know about Hash filter + ofld support,
	 * it's not a problem, don't return an error.
	 */
	if (ret < 0) {
		dev_warn(adapter->pdev_dev,
			 "Hash filter with ofld is not supported by FW\n");
	}

	/*
	 * Issue a Capability Configuration command to the firmware to get it
	 * to parse the Configuration File.  We don't use t4_fw_config_file()
	 * because we want the ability to modify various features after we've
	 * processed the configuration file ...
	 */
	memset(&caps_cmd, 0, sizeof(caps_cmd));
	caps_cmd.op_to_write =
		htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
		      FW_CMD_REQUEST_F |
		      FW_CMD_READ_F);
	caps_cmd.cfvalid_to_len16 =
		htonl(FW_CAPS_CONFIG_CMD_CFVALID_F |
		      FW_CAPS_CONFIG_CMD_MEMTYPE_CF_V(mtype) |
		      FW_CAPS_CONFIG_CMD_MEMADDR64K_CF_V(maddr >> 16) |
		      FW_LEN16(caps_cmd));
	ret = t4_wr_mbox(adapter, adapter->mbox, &caps_cmd, sizeof(caps_cmd),
			 &caps_cmd);

	/* If the CAPS_CONFIG failed with an ENOENT (for a Firmware
	 * Configuration File in FLASH), our last gasp effort is to use the
	 * Firmware Configuration File which is embedded in the firmware.  A
	 * very few early versions of the firmware didn't have one embedded
	 * but we can ignore those.
	 */
	if (ret == -ENOENT) {
		memset(&caps_cmd, 0, sizeof(caps_cmd));
		caps_cmd.op_to_write =
			htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
					FW_CMD_REQUEST_F |
					FW_CMD_READ_F);
		caps_cmd.cfvalid_to_len16 = htonl(FW_LEN16(caps_cmd));
		ret = t4_wr_mbox(adapter, adapter->mbox, &caps_cmd,
				sizeof(caps_cmd), &caps_cmd);
		config_name = "Firmware Default";
	}

	config_issued = 1;
	if (ret < 0)
		goto bye;

	finiver = ntohl(caps_cmd.finiver);
	finicsum = ntohl(caps_cmd.finicsum);
	cfcsum = ntohl(caps_cmd.cfcsum);
	if (finicsum != cfcsum)
		dev_warn(adapter->pdev_dev, "Configuration File checksum "\
			 "mismatch: [fini] csum=%#x, computed csum=%#x\n",
			 finicsum, cfcsum);

	/*
	 * And now tell the firmware to use the configuration we just loaded.
	 */
	caps_cmd.op_to_write =
		htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
		      FW_CMD_REQUEST_F |
		      FW_CMD_WRITE_F);
	caps_cmd.cfvalid_to_len16 = htonl(FW_LEN16(caps_cmd));
	ret = t4_wr_mbox(adapter, adapter->mbox, &caps_cmd, sizeof(caps_cmd),
			 NULL);
	if (ret < 0)
		goto bye;

	/*
	 * Tweak configuration based on system architecture, module
	 * parameters, etc.
	 */
	ret = adap_init0_tweaks(adapter);
	if (ret < 0)
		goto bye;

	/* We will proceed even if HMA init fails. */
	ret = adap_config_hma(adapter);
	if (ret)
		dev_err(adapter->pdev_dev,
			"HMA configuration failed with error %d\n", ret);

	if (is_t6(adapter->params.chip)) {
		adap_config_hpfilter(adapter);
		ret = setup_ppod_edram(adapter);
		if (!ret)
			dev_info(adapter->pdev_dev, "Successfully enabled "
				 "ppod edram feature\n");
	}

	/*
	 * And finally tell the firmware to initialize itself using the
	 * parameters from the Configuration File.
	 */
	ret = t4_fw_initialize(adapter, adapter->mbox);
	if (ret < 0)
		goto bye;

	/* Emit Firmware Configuration File information and return
	 * successfully.
	 */
	dev_info(adapter->pdev_dev, "Successfully configured using Firmware "\
		 "Configuration File \"%s\", version %#x, computed checksum %#x\n",
		 config_name, finiver, cfcsum);
	return 0;

	/*
	 * Something bad happened.  Return the error ...  (If the "error"
	 * is that there's no Configuration File on the adapter we don't
	 * want to issue a warning since this is fairly common.)
	 */
bye:
	if (config_issued && ret != -ENOENT)
		dev_warn(adapter->pdev_dev, "\"%s\" configuration file error %d\n",
			 config_name, -ret);
	return ret;
}

static struct fw_info fw_info_array[] = {
	{
		.chip = CHELSIO_T4,
		.fs_name = FW4_CFNAME,
		.fw_mod_name = FW4_FNAME,
		.fw_hdr = {
			.chip = FW_HDR_CHIP_T4,
			.fw_ver = __cpu_to_be32(FW_VERSION(T4)),
			.intfver_nic = FW_INTFVER(T4, NIC),
			.intfver_vnic = FW_INTFVER(T4, VNIC),
			.intfver_ri = FW_INTFVER(T4, RI),
			.intfver_iscsi = FW_INTFVER(T4, ISCSI),
			.intfver_fcoe = FW_INTFVER(T4, FCOE),
		},
	}, {
		.chip = CHELSIO_T5,
		.fs_name = FW5_CFNAME,
		.fw_mod_name = FW5_FNAME,
		.fw_hdr = {
			.chip = FW_HDR_CHIP_T5,
			.fw_ver = __cpu_to_be32(FW_VERSION(T5)),
			.intfver_nic = FW_INTFVER(T5, NIC),
			.intfver_vnic = FW_INTFVER(T5, VNIC),
			.intfver_ri = FW_INTFVER(T5, RI),
			.intfver_iscsi = FW_INTFVER(T5, ISCSI),
			.intfver_fcoe = FW_INTFVER(T5, FCOE),
		},
	}, {
		.chip = CHELSIO_T6,
		.fs_name = FW6_CFNAME,
		.fw_mod_name = FW6_FNAME,
		.fw_hdr = {
			.chip = FW_HDR_CHIP_T6,
			.fw_ver = __cpu_to_be32(FW_VERSION(T6)),
			.intfver_nic = FW_INTFVER(T6, NIC),
			.intfver_vnic = FW_INTFVER(T6, VNIC),
			.intfver_ofld = FW_INTFVER(T6, OFLD),
			.intfver_ri = FW_INTFVER(T6, RI),
			.intfver_iscsipdu = FW_INTFVER(T6, ISCSIPDU),
			.intfver_iscsi = FW_INTFVER(T6, ISCSI),
			.intfver_fcoepdu = FW_INTFVER(T6, FCOEPDU),
			.intfver_fcoe = FW_INTFVER(T6, FCOE),
		},
	}

};

static struct fw_info *find_fw_info(int chip)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fw_info_array); i++) {
		if (fw_info_array[i].chip == chip)
			return &fw_info_array[i];
	}
	return NULL;
}

/*
 * Phase 0 of initialization: contact FW, obtain config, perform basic init.
 */
static int adap_init0(struct adapter *adap, int vpd_skip)
{
	struct fw_caps_config_cmd caps_cmd;
	u32 params[7], val[7];
	enum dev_state state;
	u32 v, port_vec;
	int reset = 1;
	int ret;

	/* Grab Firmware Device Log parameters as early as possible so we have
	 * access to it for debugging, etc.
	 */
	ret = t4_init_devlog_params(adap);
	if (ret < 0)
		return ret;

	/* Contact FW, advertising Master capability */
	ret = t4_fw_hello(adap, adap->mbox, adap->mbox,
			  is_kdump_kernel() ? MASTER_MUST : MASTER_MAY, &state);
	if (ret < 0) {
		dev_err(adap->pdev_dev, "could not connect to FW, error %d\n",
			ret);
		return ret;
	}
	if (ret == adap->mbox)
		adap->flags |= CXGB4_MASTER_PF;

	/*
	 * If we're the Master PF Driver and the device is uninitialized,
	 * then let's consider upgrading the firmware ...  (We always want
	 * to check the firmware version number in order to A. get it for
	 * later reporting and B. to warn if the currently loaded firmware
	 * is excessively mismatched relative to the driver.)
	 */

	t4_get_version_info(adap);
	ret = t4_check_fw_version(adap);
	/* If firmware is too old (not supported by driver) force an update. */
	if (ret)
		state = DEV_STATE_UNINIT;
	if ((adap->flags & CXGB4_MASTER_PF) && state != DEV_STATE_INIT) {
		struct fw_info *fw_info;
		struct fw_hdr *card_fw;
		const struct firmware *fw;
		const u8 *fw_data = NULL;
		unsigned int fw_size = 0;

		/* This is the firmware whose headers the driver was compiled
		 * against
		 */
		fw_info = find_fw_info(CHELSIO_CHIP_VERSION(adap->params.chip));
		if (fw_info == NULL) {
			dev_err(adap->pdev_dev,
				"unable to get firmware info for chip %d.\n",
				CHELSIO_CHIP_VERSION(adap->params.chip));
			return -EINVAL;
		}

		/* allocate memory to read the header of the firmware on the
		 * card
		 */
		card_fw = kvzalloc(sizeof(*card_fw), GFP_KERNEL);
		if (!card_fw) {
			ret = -ENOMEM;
			goto bye;
		}

		/* Get FW from from /lib/firmware/ */
		ret = request_firmware(&fw, fw_info->fw_mod_name,
				       adap->pdev_dev);
		if (ret < 0) {
			dev_err(adap->pdev_dev,
				"unable to load firmware image %s, error %d\n",
				fw_info->fw_mod_name, ret);
		} else {
			fw_data = fw->data;
			fw_size = fw->size;
		}

		/* upgrade FW logic */
		ret = t4_prep_fw(adap, fw_info, fw_data, fw_size, card_fw,
				 state, &reset);

		/* Cleaning up */
		release_firmware(fw);
		kvfree(card_fw);

		if (ret < 0)
			goto bye;
	}

	/* If the firmware is initialized already, emit a simply note to that
	 * effect. Otherwise, it's time to try initializing the adapter.
	 */
	if (state == DEV_STATE_INIT) {
		ret = adap_config_hma(adap);
		if (ret)
			dev_err(adap->pdev_dev,
				"HMA configuration failed with error %d\n",
				ret);
		dev_info(adap->pdev_dev, "Coming up as %s: "\
			 "Adapter already initialized\n",
			 adap->flags & CXGB4_MASTER_PF ? "MASTER" : "SLAVE");
	} else {
		dev_info(adap->pdev_dev, "Coming up as MASTER: "\
			 "Initializing adapter\n");

		/* Find out whether we're dealing with a version of the
		 * firmware which has configuration file support.
		 */
		params[0] = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
			     FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_CF));
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 1,
				      params, val);

		/* If the firmware doesn't support Configuration Files,
		 * return an error.
		 */
		if (ret < 0) {
			dev_err(adap->pdev_dev, "firmware doesn't support "
				"Firmware Configuration Files\n");
			goto bye;
		}

		/* The firmware provides us with a memory buffer where we can
		 * load a Configuration File from the host if we want to
		 * override the Configuration File in flash.
		 */
		ret = adap_init0_config(adap, reset);
		if (ret == -ENOENT) {
			dev_err(adap->pdev_dev, "no Configuration File "
				"present on adapter.\n");
			goto bye;
		}
		if (ret < 0) {
			dev_err(adap->pdev_dev, "could not initialize "
				"adapter, error %d\n", -ret);
			goto bye;
		}
	}

	/* Now that we've successfully configured and initialized the adapter
	 * (or found it already initialized), we can ask the Firmware what
	 * resources it has provisioned for us.
	 */
	ret = t4_get_pfres(adap);
	if (ret) {
		dev_err(adap->pdev_dev,
			"Unable to retrieve resource provisioning information\n");
		goto bye;
	}

	/* Grab VPD parameters.  This should be done after we establish a
	 * connection to the firmware since some of the VPD parameters
	 * (notably the Core Clock frequency) are retrieved via requests to
	 * the firmware.  On the other hand, we need these fairly early on
	 * so we do this right after getting ahold of the firmware.
	 *
	 * We need to do this after initializing the adapter because someone
	 * could have FLASHed a new VPD which won't be read by the firmware
	 * until we do the RESET ...
	 */
	if (!vpd_skip) {
		ret = t4_get_vpd_params(adap, &adap->params.vpd);
		if (ret < 0)
			goto bye;
	}

	/* Find out what ports are available to us.  Note that we need to do
	 * this before calling adap_init0_no_config() since it needs nports
	 * and portvec ...
	 */
	v =
	    FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
	    FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_PORTVEC);
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 1, &v, &port_vec);
	if (ret < 0)
		goto bye;

	adap->params.nports = hweight32(port_vec);
	adap->params.portvec = port_vec;

	/* Give the SGE code a chance to pull in anything that it needs ...
	 * Note that this must be called after we retrieve our VPD parameters
	 * in order to know how to convert core ticks to seconds, etc.
	 */
	ret = t4_sge_init(adap);
	if (ret < 0)
		goto bye;

	/* Grab the SGE Doorbell Queue Timer values.  If successful, that
	 * indicates that the Firmware and Hardware support this.
	 */
	params[0] = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		    FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_DBQ_TIMERTICK));
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0,
			      1, params, val);

	if (!ret) {
		adap->sge.dbqtimer_tick = val[0];
		ret = t4_read_sge_dbqtimers(adap,
					    ARRAY_SIZE(adap->sge.dbqtimer_val),
					    adap->sge.dbqtimer_val);
	}

	if (!ret)
		adap->flags |= CXGB4_SGE_DBQ_TIMER;

	if (is_bypass_device(adap->pdev->device))
		adap->params.bypass = 1;

	/*
	 * Grab some of our basic fundamental operating parameters.
	 */
	params[0] = FW_PARAM_PFVF(EQ_START);
	params[1] = FW_PARAM_PFVF(L2T_START);
	params[2] = FW_PARAM_PFVF(L2T_END);
	params[3] = FW_PARAM_PFVF(FILTER_START);
	params[4] = FW_PARAM_PFVF(FILTER_END);
	params[5] = FW_PARAM_PFVF(IQFLINT_START);
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 6, params, val);
	if (ret < 0)
		goto bye;
	adap->sge.egr_start = val[0];
	adap->l2t_start = val[1];
	adap->l2t_end = val[2];
	adap->tids.ftid_base = val[3];
	adap->tids.nftids = val[4] - val[3] + 1;
	adap->sge.ingr_start = val[5];

	if (CHELSIO_CHIP_VERSION(adap->params.chip) > CHELSIO_T5) {
		params[0] = FW_PARAM_PFVF(HPFILTER_START);
		params[1] = FW_PARAM_PFVF(HPFILTER_END);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2,
				      params, val);
		if (ret < 0)
			goto bye;

		adap->tids.hpftid_base = val[0];
		adap->tids.nhpftids = val[1] - val[0] + 1;

		/* Read the raw mps entries. In T6, the last 2 tcam entries
		 * are reserved for raw mac addresses (rawf = 2, one per port).
		 */
		params[0] = FW_PARAM_PFVF(RAWF_START);
		params[1] = FW_PARAM_PFVF(RAWF_END);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2,
				      params, val);
		if (ret == 0) {
			adap->rawf_start = val[0];
			adap->rawf_cnt = val[1] - val[0] + 1;
		}

		adap->tids.tid_base =
			t4_read_reg(adap, LE_DB_ACTIVE_TABLE_START_INDEX_A);
	}

	/* qids (ingress/egress) returned from firmware can be anywhere
	 * in the range from EQ(IQFLINT)_START to EQ(IQFLINT)_END.
	 * Hence driver needs to allocate memory for this range to
	 * store the queue info. Get the highest IQFLINT/EQ index returned
	 * in FW_EQ_*_CMD.alloc command.
	 */
	params[0] = FW_PARAM_PFVF(EQ_END);
	params[1] = FW_PARAM_PFVF(IQFLINT_END);
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2, params, val);
	if (ret < 0)
		goto bye;
	adap->sge.egr_sz = val[0] - adap->sge.egr_start + 1;
	adap->sge.ingr_sz = val[1] - adap->sge.ingr_start + 1;

	adap->sge.egr_map = kcalloc(adap->sge.egr_sz,
				    sizeof(*adap->sge.egr_map), GFP_KERNEL);
	if (!adap->sge.egr_map) {
		ret = -ENOMEM;
		goto bye;
	}

	adap->sge.ingr_map = kcalloc(adap->sge.ingr_sz,
				     sizeof(*adap->sge.ingr_map), GFP_KERNEL);
	if (!adap->sge.ingr_map) {
		ret = -ENOMEM;
		goto bye;
	}

	/* Allocate the memory for the vaious egress queue bitmaps
	 * ie starving_fl, txq_maperr and blocked_fl.
	 */
	adap->sge.starving_fl =	kcalloc(BITS_TO_LONGS(adap->sge.egr_sz),
					sizeof(long), GFP_KERNEL);
	if (!adap->sge.starving_fl) {
		ret = -ENOMEM;
		goto bye;
	}

	adap->sge.txq_maperr = kcalloc(BITS_TO_LONGS(adap->sge.egr_sz),
				       sizeof(long), GFP_KERNEL);
	if (!adap->sge.txq_maperr) {
		ret = -ENOMEM;
		goto bye;
	}

#ifdef CONFIG_DEBUG_FS
	adap->sge.blocked_fl = kcalloc(BITS_TO_LONGS(adap->sge.egr_sz),
				       sizeof(long), GFP_KERNEL);
	if (!adap->sge.blocked_fl) {
		ret = -ENOMEM;
		goto bye;
	}
	bitmap_zero(adap->sge.blocked_fl, adap->sge.egr_sz);
#endif

	params[0] = FW_PARAM_PFVF(CLIP_START);
	params[1] = FW_PARAM_PFVF(CLIP_END);
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2, params, val);
	if (ret < 0)
		goto bye;
	adap->clipt_start = val[0];
	adap->clipt_end = val[1];

	/* Get the supported number of traffic classes */
	params[0] = FW_PARAM_DEV(NUM_TM_CLASS);
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 1, params, val);
	if (ret < 0) {
		/* We couldn't retrieve the number of Traffic Classes
		 * supported by the hardware/firmware. So we hard
		 * code it here.
		 */
		adap->params.nsched_cls = is_t4(adap->params.chip) ? 15 : 16;
	} else {
		adap->params.nsched_cls = val[0];
	}

	/* query params related to active filter region */
	params[0] = FW_PARAM_PFVF(ACTIVE_FILTER_START);
	params[1] = FW_PARAM_PFVF(ACTIVE_FILTER_END);
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2, params, val);
	/* If Active filter size is set we enable establishing
	 * offload connection through firmware work request
	 */
	if ((val[0] != val[1]) && (ret >= 0)) {
		adap->flags |= CXGB4_FW_OFLD_CONN;
		adap->tids.aftid_base = val[0];
		adap->tids.aftid_end = val[1];
	}

	/* If we're running on newer firmware, let it know that we're
	 * prepared to deal with encapsulated CPL messages.  Older
	 * firmware won't understand this and we'll just get
	 * unencapsulated messages ...
	 */
	params[0] = FW_PARAM_PFVF(CPLFW4MSG_ENCAP);
	val[0] = 1;
	(void)t4_set_params(adap, adap->mbox, adap->pf, 0, 1, params, val);

	/*
	 * Find out whether we're allowed to use the T5+ ULPTX MEMWRITE DSGL
	 * capability.  Earlier versions of the firmware didn't have the
	 * ULPTX_MEMWRITE_DSGL so we'll interpret a query failure as no
	 * permission to use ULPTX MEMWRITE DSGL.
	 */
	if (is_t4(adap->params.chip)) {
		adap->params.ulptx_memwrite_dsgl = false;
	} else {
		params[0] = FW_PARAM_DEV(ULPTX_MEMWRITE_DSGL);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0,
				      1, params, val);
		adap->params.ulptx_memwrite_dsgl = (ret == 0 && val[0] != 0);
	}

	/* See if FW supports FW_RI_FR_NSMR_TPTE_WR work request */
	params[0] = FW_PARAM_DEV(RI_FR_NSMR_TPTE_WR);
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0,
			      1, params, val);
	adap->params.fr_nsmr_tpte_wr_support = (ret == 0 && val[0] != 0);

	/* See if FW supports FW_FILTER2 work request */
	if (is_t4(adap->params.chip)) {
		adap->params.filter2_wr_support = false;
	} else {
		params[0] = FW_PARAM_DEV(FILTER2_WR);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0,
				      1, params, val);
		adap->params.filter2_wr_support = (ret == 0 && val[0] != 0);
	}

	/* Check if FW supports returning vin and smt index.
	 * If this is not supported, driver will interpret
	 * these values from viid.
	 */
	params[0] = FW_PARAM_DEV(OPAQUE_VIID_SMT_EXTN);
	ret = t4_query_params(adap, adap->mbox, adap->pf, 0,
			      1, params, val);
	adap->params.viid_smt_extn_support = (ret == 0 && val[0] != 0);

	/*
	 * Get device capabilities so we can determine what resources we need
	 * to manage.
	 */
	memset(&caps_cmd, 0, sizeof(caps_cmd));
	caps_cmd.op_to_write = htonl(FW_CMD_OP_V(FW_CAPS_CONFIG_CMD) |
				     FW_CMD_REQUEST_F | FW_CMD_READ_F);
	caps_cmd.cfvalid_to_len16 = htonl(FW_LEN16(caps_cmd));
	ret = t4_wr_mbox(adap, adap->mbox, &caps_cmd, sizeof(caps_cmd),
			 &caps_cmd);
	if (ret < 0)
		goto bye;

	/* hash filter has some mandatory register settings to be tested and for
	 * that it needs to test whether offload is enabled or not, hence
	 * checking and setting it here.
	 */
	if (caps_cmd.ofldcaps)
		adap->params.offload = 1;

	if (caps_cmd.ofldcaps ||
	    (caps_cmd.niccaps & htons(FW_CAPS_CONFIG_NIC_HASHFILTER)) ||
	    (caps_cmd.niccaps & htons(FW_CAPS_CONFIG_NIC_ETHOFLD))) {
		/* query offload-related parameters */
		params[0] = FW_PARAM_DEV(NTID);
		params[1] = FW_PARAM_PFVF(SERVER_START);
		params[2] = FW_PARAM_PFVF(SERVER_END);
		params[3] = FW_PARAM_PFVF(TDDP_START);
		params[4] = FW_PARAM_PFVF(TDDP_END);
		params[5] = FW_PARAM_DEV(FLOWC_BUFFIFO_SZ);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 6,
				      params, val);
		if (ret < 0)
			goto bye;
		adap->tids.ntids = val[0];
		adap->tids.natids = min(adap->tids.ntids / 2, MAX_ATIDS);
		adap->tids.stid_base = val[1];
		adap->tids.nstids = val[2] - val[1] + 1;
		/*
		 * Setup server filter region. Divide the available filter
		 * region into two parts. Regular filters get 1/3rd and server
		 * filters get 2/3rd part. This is only enabled if workarond
		 * path is enabled.
		 * 1. For regular filters.
		 * 2. Server filter: This are special filters which are used
		 * to redirect SYN packets to offload queue.
		 */
		if (adap->flags & CXGB4_FW_OFLD_CONN && !is_bypass(adap)) {
			adap->tids.sftid_base = adap->tids.ftid_base +
					DIV_ROUND_UP(adap->tids.nftids, 3);
			adap->tids.nsftids = adap->tids.nftids -
					 DIV_ROUND_UP(adap->tids.nftids, 3);
			adap->tids.nftids = adap->tids.sftid_base -
						adap->tids.ftid_base;
		}
		adap->vres.ddp.start = val[3];
		adap->vres.ddp.size = val[4] - val[3] + 1;
		adap->params.ofldq_wr_cred = val[5];

		if (caps_cmd.niccaps & htons(FW_CAPS_CONFIG_NIC_HASHFILTER)) {
			init_hash_filter(adap);
		} else {
			adap->num_ofld_uld += 1;
		}

		if (caps_cmd.niccaps & htons(FW_CAPS_CONFIG_NIC_ETHOFLD)) {
			params[0] = FW_PARAM_PFVF(ETHOFLD_START);
			params[1] = FW_PARAM_PFVF(ETHOFLD_END);
			ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2,
					      params, val);
			if (!ret) {
				adap->tids.eotid_base = val[0];
				adap->tids.neotids = min_t(u32, MAX_ATIDS,
							   val[1] - val[0] + 1);
				adap->params.ethofld = 1;
			}
		}
	}
	if (caps_cmd.rdmacaps) {
		params[0] = FW_PARAM_PFVF(STAG_START);
		params[1] = FW_PARAM_PFVF(STAG_END);
		params[2] = FW_PARAM_PFVF(RQ_START);
		params[3] = FW_PARAM_PFVF(RQ_END);
		params[4] = FW_PARAM_PFVF(PBL_START);
		params[5] = FW_PARAM_PFVF(PBL_END);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 6,
				      params, val);
		if (ret < 0)
			goto bye;
		adap->vres.stag.start = val[0];
		adap->vres.stag.size = val[1] - val[0] + 1;
		adap->vres.rq.start = val[2];
		adap->vres.rq.size = val[3] - val[2] + 1;
		adap->vres.pbl.start = val[4];
		adap->vres.pbl.size = val[5] - val[4] + 1;

		params[0] = FW_PARAM_PFVF(SRQ_START);
		params[1] = FW_PARAM_PFVF(SRQ_END);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2,
				      params, val);
		if (!ret) {
			adap->vres.srq.start = val[0];
			adap->vres.srq.size = val[1] - val[0] + 1;
		}
		if (adap->vres.srq.size) {
			adap->srq = t4_init_srq(adap->vres.srq.size);
			if (!adap->srq)
				dev_warn(&adap->pdev->dev, "could not allocate SRQ, continuing\n");
		}

		params[0] = FW_PARAM_PFVF(SQRQ_START);
		params[1] = FW_PARAM_PFVF(SQRQ_END);
		params[2] = FW_PARAM_PFVF(CQ_START);
		params[3] = FW_PARAM_PFVF(CQ_END);
		params[4] = FW_PARAM_PFVF(OCQ_START);
		params[5] = FW_PARAM_PFVF(OCQ_END);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 6, params,
				      val);
		if (ret < 0)
			goto bye;
		adap->vres.qp.start = val[0];
		adap->vres.qp.size = val[1] - val[0] + 1;
		adap->vres.cq.start = val[2];
		adap->vres.cq.size = val[3] - val[2] + 1;
		adap->vres.ocq.start = val[4];
		adap->vres.ocq.size = val[5] - val[4] + 1;

		params[0] = FW_PARAM_DEV(MAXORDIRD_QP);
		params[1] = FW_PARAM_DEV(MAXIRD_ADAPTER);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2, params,
				      val);
		if (ret < 0) {
			adap->params.max_ordird_qp = 8;
			adap->params.max_ird_adapter = 32 * adap->tids.ntids;
			ret = 0;
		} else {
			adap->params.max_ordird_qp = val[0];
			adap->params.max_ird_adapter = val[1];
		}
		dev_info(adap->pdev_dev,
			 "max_ordird_qp %d max_ird_adapter %d\n",
			 adap->params.max_ordird_qp,
			 adap->params.max_ird_adapter);

		/* Enable write_with_immediate if FW supports it */
		params[0] = FW_PARAM_DEV(RDMA_WRITE_WITH_IMM);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 1, params,
				      val);
		adap->params.write_w_imm_support = (ret == 0 && val[0] != 0);

		/* Enable write_cmpl if FW supports it */
		params[0] = FW_PARAM_DEV(RI_WRITE_CMPL_WR);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 1, params,
				      val);
		adap->params.write_cmpl_support = (ret == 0 && val[0] != 0);
		adap->num_ofld_uld += 2;
	}
	if (caps_cmd.iscsicaps) {
		params[0] = FW_PARAM_PFVF(ISCSI_START);
		params[1] = FW_PARAM_PFVF(ISCSI_END);
		ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2,
				      params, val);
		if (ret < 0)
			goto bye;
		adap->vres.iscsi.start = val[0];
		adap->vres.iscsi.size = val[1] - val[0] + 1;
		if (is_t6(adap->params.chip)) {
			params[0] = FW_PARAM_PFVF(PPOD_EDRAM_START);
			params[1] = FW_PARAM_PFVF(PPOD_EDRAM_END);
			ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 2,
					      params, val);
			if (!ret) {
				adap->vres.ppod_edram.start = val[0];
				adap->vres.ppod_edram.size =
					val[1] - val[0] + 1;

				dev_info(adap->pdev_dev,
					 "ppod edram start 0x%x end 0x%x size 0x%x\n",
					 val[0], val[1],
					 adap->vres.ppod_edram.size);
			}
		}
		/* LIO target and cxgb4i initiaitor */
		adap->num_ofld_uld += 2;
	}
	if (caps_cmd.cryptocaps) {
		if (ntohs(caps_cmd.cryptocaps) &
		    FW_CAPS_CONFIG_CRYPTO_LOOKASIDE) {
			params[0] = FW_PARAM_PFVF(NCRYPTO_LOOKASIDE);
			ret = t4_query_params(adap, adap->mbox, adap->pf, 0,
					      2, params, val);
			if (ret < 0) {
				if (ret != -EINVAL)
					goto bye;
			} else {
				adap->vres.ncrypto_fc = val[0];
			}
			adap->num_ofld_uld += 1;
		}
		if (ntohs(caps_cmd.cryptocaps) &
		    FW_CAPS_CONFIG_TLS_INLINE) {
			params[0] = FW_PARAM_PFVF(TLS_START);
			params[1] = FW_PARAM_PFVF(TLS_END);
			ret = t4_query_params(adap, adap->mbox, adap->pf, 0,
					      2, params, val);
			if (ret < 0)
				goto bye;
			adap->vres.key.start = val[0];
			adap->vres.key.size = val[1] - val[0] + 1;
			adap->num_uld += 1;
		}
		adap->params.crypto = ntohs(caps_cmd.cryptocaps);
	}

	/* The MTU/MSS Table is initialized by now, so load their values.  If
	 * we're initializing the adapter, then we'll make any modifications
	 * we want to the MTU/MSS Table and also initialize the congestion
	 * parameters.
	 */
	t4_read_mtu_tbl(adap, adap->params.mtus, NULL);
	if (state != DEV_STATE_INIT) {
		int i;

		/* The default MTU Table contains values 1492 and 1500.
		 * However, for TCP, it's better to have two values which are
		 * a multiple of 8 +/- 4 bytes apart near this popular MTU.
		 * This allows us to have a TCP Data Payload which is a
		 * multiple of 8 regardless of what combination of TCP Options
		 * are in use (always a multiple of 4 bytes) which is
		 * important for performance reasons.  For instance, if no
		 * options are in use, then we have a 20-byte IP header and a
		 * 20-byte TCP header.  In this case, a 1500-byte MSS would
		 * result in a TCP Data Payload of 1500 - 40 == 1460 bytes
		 * which is not a multiple of 8.  So using an MSS of 1488 in
		 * this case results in a TCP Data Payload of 1448 bytes which
		 * is a multiple of 8.  On the other hand, if 12-byte TCP Time
		 * Stamps have been negotiated, then an MTU of 1500 bytes
		 * results in a TCP Data Payload of 1448 bytes which, as
		 * above, is a multiple of 8 bytes ...
		 */
		for (i = 0; i < NMTUS; i++)
			if (adap->params.mtus[i] == 1492) {
				adap->params.mtus[i] = 1488;
				break;
			}

		t4_load_mtus(adap, adap->params.mtus, adap->params.a_wnd,
			     adap->params.b_wnd);
	}
	t4_init_sge_params(adap);
	adap->flags |= CXGB4_FW_OK;
	t4_init_tp_params(adap, true);
	return 0;

	/*
	 * Something bad happened.  If a command timed out or failed with EIO
	 * FW does not operate within its spec or something catastrophic
	 * happened to HW/FW, stop issuing commands.
	 */
bye:
	adap_free_hma_mem(adap);
	kfree(adap->sge.egr_map);
	kfree(adap->sge.ingr_map);
	kfree(adap->sge.starving_fl);
	kfree(adap->sge.txq_maperr);
#ifdef CONFIG_DEBUG_FS
	kfree(adap->sge.blocked_fl);
#endif
	if (ret != -ETIMEDOUT && ret != -EIO)
		t4_fw_bye(adap, adap->mbox);
	return ret;
}

/* EEH callbacks */

static pci_ers_result_t eeh_err_detected(struct pci_dev *pdev,
					 pci_channel_state_t state)
{
	int i;
	struct adapter *adap = pci_get_drvdata(pdev);

	if (!adap)
		goto out;

	rtnl_lock();
	adap->flags &= ~CXGB4_FW_OK;
	notify_ulds(adap, CXGB4_STATE_START_RECOVERY);
	spin_lock(&adap->stats_lock);
	for_each_port(adap, i) {
		struct net_device *dev = adap->port[i];
		if (dev) {
			netif_device_detach(dev);
			netif_carrier_off(dev);
		}
	}
	spin_unlock(&adap->stats_lock);
	disable_interrupts(adap);
	if (adap->flags & CXGB4_FULL_INIT_DONE)
		cxgb_down(adap);
	rtnl_unlock();
	if ((adap->flags & CXGB4_DEV_ENABLED)) {
		pci_disable_device(pdev);
		adap->flags &= ~CXGB4_DEV_ENABLED;
	}
out:	return state == pci_channel_io_perm_failure ?
		PCI_ERS_RESULT_DISCONNECT : PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t eeh_slot_reset(struct pci_dev *pdev)
{
	int i, ret;
	struct fw_caps_config_cmd c;
	struct adapter *adap = pci_get_drvdata(pdev);

	if (!adap) {
		pci_restore_state(pdev);
		pci_save_state(pdev);
		return PCI_ERS_RESULT_RECOVERED;
	}

	if (!(adap->flags & CXGB4_DEV_ENABLED)) {
		if (pci_enable_device(pdev)) {
			dev_err(&pdev->dev, "Cannot reenable PCI "
					    "device after reset\n");
			return PCI_ERS_RESULT_DISCONNECT;
		}
		adap->flags |= CXGB4_DEV_ENABLED;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	if (t4_wait_dev_ready(adap->regs) < 0)
		return PCI_ERS_RESULT_DISCONNECT;
	if (t4_fw_hello(adap, adap->mbox, adap->pf, MASTER_MUST, NULL) < 0)
		return PCI_ERS_RESULT_DISCONNECT;
	adap->flags |= CXGB4_FW_OK;
	if (adap_init1(adap, &c))
		return PCI_ERS_RESULT_DISCONNECT;

	for_each_port(adap, i) {
		struct port_info *pi = adap2pinfo(adap, i);
		u8 vivld = 0, vin = 0;

		ret = t4_alloc_vi(adap, adap->mbox, pi->tx_chan, adap->pf, 0, 1,
				  NULL, NULL, &vivld, &vin);
		if (ret < 0)
			return PCI_ERS_RESULT_DISCONNECT;
		pi->viid = ret;
		pi->xact_addr_filt = -1;
		/* If fw supports returning the VIN as part of FW_VI_CMD,
		 * save the returned values.
		 */
		if (adap->params.viid_smt_extn_support) {
			pi->vivld = vivld;
			pi->vin = vin;
		} else {
			/* Retrieve the values from VIID */
			pi->vivld = FW_VIID_VIVLD_G(pi->viid);
			pi->vin = FW_VIID_VIN_G(pi->viid);
		}
	}

	t4_load_mtus(adap, adap->params.mtus, adap->params.a_wnd,
		     adap->params.b_wnd);
	setup_memwin(adap);
	if (cxgb_up(adap))
		return PCI_ERS_RESULT_DISCONNECT;
	return PCI_ERS_RESULT_RECOVERED;
}

static void eeh_resume(struct pci_dev *pdev)
{
	int i;
	struct adapter *adap = pci_get_drvdata(pdev);

	if (!adap)
		return;

	rtnl_lock();
	for_each_port(adap, i) {
		struct net_device *dev = adap->port[i];
		if (dev) {
			if (netif_running(dev)) {
				link_start(dev);
				cxgb_set_rxmode(dev);
			}
			netif_device_attach(dev);
		}
	}
	rtnl_unlock();
}

static void eeh_reset_prepare(struct pci_dev *pdev)
{
	struct adapter *adapter = pci_get_drvdata(pdev);
	int i;

	if (adapter->pf != 4)
		return;

	adapter->flags &= ~CXGB4_FW_OK;

	notify_ulds(adapter, CXGB4_STATE_DOWN);

	for_each_port(adapter, i)
		if (adapter->port[i]->reg_state == NETREG_REGISTERED)
			cxgb_close(adapter->port[i]);

	disable_interrupts(adapter);
	cxgb4_free_mps_ref_entries(adapter);

	adap_free_hma_mem(adapter);

	if (adapter->flags & CXGB4_FULL_INIT_DONE)
		cxgb_down(adapter);
}

static void eeh_reset_done(struct pci_dev *pdev)
{
	struct adapter *adapter = pci_get_drvdata(pdev);
	int err, i;

	if (adapter->pf != 4)
		return;

	err = t4_wait_dev_ready(adapter->regs);
	if (err < 0) {
		dev_err(adapter->pdev_dev,
			"Device not ready, err %d", err);
		return;
	}

	setup_memwin(adapter);

	err = adap_init0(adapter, 1);
	if (err) {
		dev_err(adapter->pdev_dev,
			"Adapter init failed, err %d", err);
		return;
	}

	setup_memwin_rdma(adapter);

	if (adapter->flags & CXGB4_FW_OK) {
		err = t4_port_init(adapter, adapter->pf, adapter->pf, 0);
		if (err) {
			dev_err(adapter->pdev_dev,
				"Port init failed, err %d", err);
			return;
		}
	}

	err = cfg_queues(adapter);
	if (err) {
		dev_err(adapter->pdev_dev,
			"Config queues failed, err %d", err);
		return;
	}

	cxgb4_init_mps_ref_entries(adapter);

	err = setup_fw_sge_queues(adapter);
	if (err) {
		dev_err(adapter->pdev_dev,
			"FW sge queue allocation failed, err %d", err);
		return;
	}

	for_each_port(adapter, i)
		if (adapter->port[i]->reg_state == NETREG_REGISTERED)
			cxgb_open(adapter->port[i]);
}

static const struct pci_error_handlers cxgb4_eeh = {
	.error_detected = eeh_err_detected,
	.slot_reset     = eeh_slot_reset,
	.resume         = eeh_resume,
	.reset_prepare  = eeh_reset_prepare,
	.reset_done     = eeh_reset_done,
};

/* Return true if the Link Configuration supports "High Speeds" (those greater
 * than 1Gb/s).
 */
static inline bool is_x_10g_port(const struct link_config *lc)
{
	unsigned int speeds, high_speeds;

	speeds = FW_PORT_CAP32_SPEED_V(FW_PORT_CAP32_SPEED_G(lc->pcaps));
	high_speeds = speeds &
			~(FW_PORT_CAP32_SPEED_100M | FW_PORT_CAP32_SPEED_1G);

	return high_speeds != 0;
}

/* Perform default configuration of DMA queues depending on the number and type
 * of ports we found and the number of available CPUs.  Most settings can be
 * modified by the admin prior to actual use.
 */
static int cfg_queues(struct adapter *adap)
{
	u32 avail_qsets, avail_eth_qsets, avail_uld_qsets;
	u32 ncpus = num_online_cpus();
	u32 niqflint, neq, num_ulds;
	struct sge *s = &adap->sge;
	u32 i, n10g = 0, qidx = 0;
	u32 q10g = 0, q1g;

	/* Reduce memory usage in kdump environment, disable all offload. */
	if (is_kdump_kernel() || (is_uld(adap) && t4_uld_mem_alloc(adap))) {
		adap->params.offload = 0;
		adap->params.crypto = 0;
		adap->params.ethofld = 0;
	}

	/* Calculate the number of Ethernet Queue Sets available based on
	 * resources provisioned for us.  We always have an Asynchronous
	 * Firmware Event Ingress Queue.  If we're operating in MSI or Legacy
	 * IRQ Pin Interrupt mode, then we'll also have a Forwarded Interrupt
	 * Ingress Queue.  Meanwhile, we need two Egress Queues for each
	 * Queue Set: one for the Free List and one for the Ethernet TX Queue.
	 *
	 * Note that we should also take into account all of the various
	 * Offload Queues.  But, in any situation where we're operating in
	 * a Resource Constrained Provisioning environment, doing any Offload
	 * at all is problematic ...
	 */
	niqflint = adap->params.pfres.niqflint - 1;
	if (!(adap->flags & CXGB4_USING_MSIX))
		niqflint--;
	neq = adap->params.pfres.neq / 2;
	avail_qsets = min(niqflint, neq);

	if (avail_qsets < adap->params.nports) {
		dev_err(adap->pdev_dev, "avail_eth_qsets=%d < nports=%d\n",
			avail_qsets, adap->params.nports);
		return -ENOMEM;
	}

	/* Count the number of 10Gb/s or better ports */
	for_each_port(adap, i)
		n10g += is_x_10g_port(&adap2pinfo(adap, i)->link_cfg);

	avail_eth_qsets = min_t(u32, avail_qsets, MAX_ETH_QSETS);

	/* We default to 1 queue per non-10G port and up to # of cores queues
	 * per 10G port.
	 */
	if (n10g)
		q10g = (avail_eth_qsets - (adap->params.nports - n10g)) / n10g;

#ifdef CONFIG_CHELSIO_T4_DCB
	/* For Data Center Bridging support we need to be able to support up
	 * to 8 Traffic Priorities; each of which will be assigned to its
	 * own TX Queue in order to prevent Head-Of-Line Blocking.
	 */
	q1g = 8;
	if (adap->params.nports * 8 > avail_eth_qsets) {
		dev_err(adap->pdev_dev, "DCB avail_eth_qsets=%d < %d!\n",
			avail_eth_qsets, adap->params.nports * 8);
		return -ENOMEM;
	}

	if (adap->params.nports * ncpus < avail_eth_qsets)
		q10g = max(8U, ncpus);
	else
		q10g = max(8U, q10g);

	while ((q10g * n10g) >
	       (avail_eth_qsets - (adap->params.nports - n10g) * q1g))
		q10g--;

#else /* !CONFIG_CHELSIO_T4_DCB */
	q1g = 1;
	q10g = min(q10g, ncpus);
#endif /* !CONFIG_CHELSIO_T4_DCB */
	if (is_kdump_kernel()) {
		q10g = 1;
		q1g = 1;
	}

	for_each_port(adap, i) {
		struct port_info *pi = adap2pinfo(adap, i);

		pi->first_qset = qidx;
		pi->nqsets = is_x_10g_port(&pi->link_cfg) ? q10g : q1g;
		qidx += pi->nqsets;
	}

	s->ethqsets = qidx;
	s->max_ethqsets = qidx;   /* MSI-X may lower it later */
	avail_qsets -= qidx;

	if (is_uld(adap)) {
		/* For offload we use 1 queue/channel if all ports are up to 1G,
		 * otherwise we divide all available queues amongst the channels
		 * capped by the number of available cores.
		 */
		num_ulds = adap->num_uld + adap->num_ofld_uld;
		i = min_t(u32, MAX_OFLD_QSETS, ncpus);
		avail_uld_qsets = roundup(i, adap->params.nports);
		if (avail_qsets < num_ulds * adap->params.nports) {
			adap->params.offload = 0;
			adap->params.crypto = 0;
			s->ofldqsets = 0;
		} else if (avail_qsets < num_ulds * avail_uld_qsets || !n10g) {
			s->ofldqsets = adap->params.nports;
		} else {
			s->ofldqsets = avail_uld_qsets;
		}

		avail_qsets -= num_ulds * s->ofldqsets;
	}

	/* ETHOFLD Queues used for QoS offload should follow same
	 * allocation scheme as normal Ethernet Queues.
	 */
	if (is_ethofld(adap)) {
		if (avail_qsets < s->max_ethqsets) {
			adap->params.ethofld = 0;
			s->eoqsets = 0;
		} else {
			s->eoqsets = s->max_ethqsets;
		}
		avail_qsets -= s->eoqsets;
	}

	/* Mirror queues must follow same scheme as normal Ethernet
	 * Queues, when there are enough queues available. Otherwise,
	 * allocate at least 1 queue per port. If even 1 queue is not
	 * available, then disable mirror queues support.
	 */
	if (avail_qsets >= s->max_ethqsets)
		s->mirrorqsets = s->max_ethqsets;
	else if (avail_qsets >= adap->params.nports)
		s->mirrorqsets = adap->params.nports;
	else
		s->mirrorqsets = 0;
	avail_qsets -= s->mirrorqsets;

	for (i = 0; i < ARRAY_SIZE(s->ethrxq); i++) {
		struct sge_eth_rxq *r = &s->ethrxq[i];

		init_rspq(adap, &r->rspq, 5, 10, 1024, 64);
		r->fl.size = 72;
	}

	for (i = 0; i < ARRAY_SIZE(s->ethtxq); i++)
		s->ethtxq[i].q.size = 1024;

	for (i = 0; i < ARRAY_SIZE(s->ctrlq); i++)
		s->ctrlq[i].q.size = 512;

	if (!is_t4(adap->params.chip))
		s->ptptxq.q.size = 8;

	init_rspq(adap, &s->fw_evtq, 0, 1, 1024, 64);
	init_rspq(adap, &s->intrq, 0, 1, 512, 64);

	return 0;
}

/*
 * Reduce the number of Ethernet queues across all ports to at most n.
 * n provides at least one queue per port.
 */
static void reduce_ethqs(struct adapter *adap, int n)
{
	int i;
	struct port_info *pi;

	while (n < adap->sge.ethqsets)
		for_each_port(adap, i) {
			pi = adap2pinfo(adap, i);
			if (pi->nqsets > 1) {
				pi->nqsets--;
				adap->sge.ethqsets--;
				if (adap->sge.ethqsets <= n)
					break;
			}
		}

	n = 0;
	for_each_port(adap, i) {
		pi = adap2pinfo(adap, i);
		pi->first_qset = n;
		n += pi->nqsets;
	}
}

static int alloc_msix_info(struct adapter *adap, u32 num_vec)
{
	struct msix_info *msix_info;

	msix_info = kcalloc(num_vec, sizeof(*msix_info), GFP_KERNEL);
	if (!msix_info)
		return -ENOMEM;

	adap->msix_bmap.msix_bmap = kcalloc(BITS_TO_LONGS(num_vec),
					    sizeof(long), GFP_KERNEL);
	if (!adap->msix_bmap.msix_bmap) {
		kfree(msix_info);
		return -ENOMEM;
	}

	spin_lock_init(&adap->msix_bmap.lock);
	adap->msix_bmap.mapsize = num_vec;

	adap->msix_info = msix_info;
	return 0;
}

static void free_msix_info(struct adapter *adap)
{
	kfree(adap->msix_bmap.msix_bmap);
	kfree(adap->msix_info);
}

int cxgb4_get_msix_idx_from_bmap(struct adapter *adap)
{
	struct msix_bmap *bmap = &adap->msix_bmap;
	unsigned int msix_idx;
	unsigned long flags;

	spin_lock_irqsave(&bmap->lock, flags);
	msix_idx = find_first_zero_bit(bmap->msix_bmap, bmap->mapsize);
	if (msix_idx < bmap->mapsize) {
		__set_bit(msix_idx, bmap->msix_bmap);
	} else {
		spin_unlock_irqrestore(&bmap->lock, flags);
		return -ENOSPC;
	}

	spin_unlock_irqrestore(&bmap->lock, flags);
	return msix_idx;
}

void cxgb4_free_msix_idx_in_bmap(struct adapter *adap,
				 unsigned int msix_idx)
{
	struct msix_bmap *bmap = &adap->msix_bmap;
	unsigned long flags;

	spin_lock_irqsave(&bmap->lock, flags);
	__clear_bit(msix_idx, bmap->msix_bmap);
	spin_unlock_irqrestore(&bmap->lock, flags);
}

/* 2 MSI-X vectors needed for the FW queue and non-data interrupts */
#define EXTRA_VECS 2

static int enable_msix(struct adapter *adap)
{
	u32 eth_need, uld_need = 0, ethofld_need = 0, mirror_need = 0;
	u32 ethqsets = 0, ofldqsets = 0, eoqsets = 0, mirrorqsets = 0;
	u8 num_uld = 0, nchan = adap->params.nports;
	u32 i, want, need, num_vec;
	struct sge *s = &adap->sge;
	struct msix_entry *entries;
	struct port_info *pi;
	int allocated, ret;

	want = s->max_ethqsets;
#ifdef CONFIG_CHELSIO_T4_DCB
	/* For Data Center Bridging we need 8 Ethernet TX Priority Queues for
	 * each port.
	 */
	need = 8 * nchan;
#else
	need = nchan;
#endif
	eth_need = need;
	if (is_uld(adap)) {
		num_uld = adap->num_ofld_uld + adap->num_uld;
		want += num_uld * s->ofldqsets;
		uld_need = num_uld * nchan;
		need += uld_need;
	}

	if (is_ethofld(adap)) {
		want += s->eoqsets;
		ethofld_need = eth_need;
		need += ethofld_need;
	}

	if (s->mirrorqsets) {
		want += s->mirrorqsets;
		mirror_need = nchan;
		need += mirror_need;
	}

	want += EXTRA_VECS;
	need += EXTRA_VECS;

	entries = kmalloc_array(want, sizeof(*entries), GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

	for (i = 0; i < want; i++)
		entries[i].entry = i;

	allocated = pci_enable_msix_range(adap->pdev, entries, need, want);
	if (allocated < 0) {
		/* Disable offload and attempt to get vectors for NIC
		 * only mode.
		 */
		want = s->max_ethqsets + EXTRA_VECS;
		need = eth_need + EXTRA_VECS;
		allocated = pci_enable_msix_range(adap->pdev, entries,
						  need, want);
		if (allocated < 0) {
			dev_info(adap->pdev_dev,
				 "Disabling MSI-X due to insufficient MSI-X vectors\n");
			ret = allocated;
			goto out_free;
		}

		dev_info(adap->pdev_dev,
			 "Disabling offload due to insufficient MSI-X vectors\n");
		adap->params.offload = 0;
		adap->params.crypto = 0;
		adap->params.ethofld = 0;
		s->ofldqsets = 0;
		s->eoqsets = 0;
		s->mirrorqsets = 0;
		uld_need = 0;
		ethofld_need = 0;
		mirror_need = 0;
	}

	num_vec = allocated;
	if (num_vec < want) {
		/* Distribute available vectors to the various queue groups.
		 * Every group gets its minimum requirement and NIC gets top
		 * priority for leftovers.
		 */
		ethqsets = eth_need;
		if (is_uld(adap))
			ofldqsets = nchan;
		if (is_ethofld(adap))
			eoqsets = ethofld_need;
		if (s->mirrorqsets)
			mirrorqsets = mirror_need;

		num_vec -= need;
		while (num_vec) {
			if (num_vec < eth_need + ethofld_need ||
			    ethqsets > s->max_ethqsets)
				break;

			for_each_port(adap, i) {
				pi = adap2pinfo(adap, i);
				if (pi->nqsets < 2)
					continue;

				ethqsets++;
				num_vec--;
				if (ethofld_need) {
					eoqsets++;
					num_vec--;
				}
			}
		}

		if (is_uld(adap)) {
			while (num_vec) {
				if (num_vec < uld_need ||
				    ofldqsets > s->ofldqsets)
					break;

				ofldqsets++;
				num_vec -= uld_need;
			}
		}

		if (s->mirrorqsets) {
			while (num_vec) {
				if (num_vec < mirror_need ||
				    mirrorqsets > s->mirrorqsets)
					break;

				mirrorqsets++;
				num_vec -= mirror_need;
			}
		}
	} else {
		ethqsets = s->max_ethqsets;
		if (is_uld(adap))
			ofldqsets = s->ofldqsets;
		if (is_ethofld(adap))
			eoqsets = s->eoqsets;
		if (s->mirrorqsets)
			mirrorqsets = s->mirrorqsets;
	}

	if (ethqsets < s->max_ethqsets) {
		s->max_ethqsets = ethqsets;
		reduce_ethqs(adap, ethqsets);
	}

	if (is_uld(adap)) {
		s->ofldqsets = ofldqsets;
		s->nqs_per_uld = s->ofldqsets;
	}

	if (is_ethofld(adap))
		s->eoqsets = eoqsets;

	if (s->mirrorqsets) {
		s->mirrorqsets = mirrorqsets;
		for_each_port(adap, i) {
			pi = adap2pinfo(adap, i);
			pi->nmirrorqsets = s->mirrorqsets / nchan;
			mutex_init(&pi->vi_mirror_mutex);
		}
	}

	/* map for msix */
	ret = alloc_msix_info(adap, allocated);
	if (ret)
		goto out_disable_msix;

	for (i = 0; i < allocated; i++) {
		adap->msix_info[i].vec = entries[i].vector;
		adap->msix_info[i].idx = i;
	}

	dev_info(adap->pdev_dev,
		 "%d MSI-X vectors allocated, nic %d eoqsets %d per uld %d mirrorqsets %d\n",
		 allocated, s->max_ethqsets, s->eoqsets, s->nqs_per_uld,
		 s->mirrorqsets);

	kfree(entries);
	return 0;

out_disable_msix:
	pci_disable_msix(adap->pdev);

out_free:
	kfree(entries);
	return ret;
}

#undef EXTRA_VECS

static int init_rss(struct adapter *adap)
{
	unsigned int i;
	int err;

	err = t4_init_rss_mode(adap, adap->mbox);
	if (err)
		return err;

	for_each_port(adap, i) {
		struct port_info *pi = adap2pinfo(adap, i);

		pi->rss = kcalloc(pi->rss_size, sizeof(u16), GFP_KERNEL);
		if (!pi->rss)
			return -ENOMEM;
	}
	return 0;
}

/* Dump basic information about the adapter */
static void print_adapter_info(struct adapter *adapter)
{
	/* Hardware/Firmware/etc. Version/Revision IDs */
	t4_dump_version_info(adapter);

	/* Software/Hardware configuration */
	dev_info(adapter->pdev_dev, "Configuration: %sNIC %s, %s capable\n",
		 is_offload(adapter) ? "R" : "",
		 ((adapter->flags & CXGB4_USING_MSIX) ? "MSI-X" :
		  (adapter->flags & CXGB4_USING_MSI) ? "MSI" : ""),
		 is_offload(adapter) ? "Offload" : "non-Offload");
}

static void print_port_info(const struct net_device *dev)
{
	char buf[80];
	char *bufp = buf;
	const struct port_info *pi = netdev_priv(dev);
	const struct adapter *adap = pi->adapter;

	if (pi->link_cfg.pcaps & FW_PORT_CAP32_SPEED_100M)
		bufp += sprintf(bufp, "100M/");
	if (pi->link_cfg.pcaps & FW_PORT_CAP32_SPEED_1G)
		bufp += sprintf(bufp, "1G/");
	if (pi->link_cfg.pcaps & FW_PORT_CAP32_SPEED_10G)
		bufp += sprintf(bufp, "10G/");
	if (pi->link_cfg.pcaps & FW_PORT_CAP32_SPEED_25G)
		bufp += sprintf(bufp, "25G/");
	if (pi->link_cfg.pcaps & FW_PORT_CAP32_SPEED_40G)
		bufp += sprintf(bufp, "40G/");
	if (pi->link_cfg.pcaps & FW_PORT_CAP32_SPEED_50G)
		bufp += sprintf(bufp, "50G/");
	if (pi->link_cfg.pcaps & FW_PORT_CAP32_SPEED_100G)
		bufp += sprintf(bufp, "100G/");
	if (pi->link_cfg.pcaps & FW_PORT_CAP32_SPEED_200G)
		bufp += sprintf(bufp, "200G/");
	if (pi->link_cfg.pcaps & FW_PORT_CAP32_SPEED_400G)
		bufp += sprintf(bufp, "400G/");
	if (bufp != buf)
		--bufp;
	sprintf(bufp, "BASE-%s", t4_get_port_type_description(pi->port_type));

	netdev_info(dev, "Chelsio %s %s\n", adap->params.vpd.id, buf);
}

/*
 * Free the following resources:
 * - memory used for tables
 * - MSI/MSI-X
 * - net devices
 * - resources FW is holding for us
 */
static void free_some_resources(struct adapter *adapter)
{
	unsigned int i;

	kvfree(adapter->smt);
	kvfree(adapter->l2t);
	kvfree(adapter->srq);
	t4_cleanup_sched(adapter);
	kvfree(adapter->tids.tid_tab);
	cxgb4_cleanup_tc_matchall(adapter);
	cxgb4_cleanup_tc_mqprio(adapter);
	cxgb4_cleanup_tc_flower(adapter);
	cxgb4_cleanup_tc_u32(adapter);
	cxgb4_cleanup_ethtool_filters(adapter);
	kfree(adapter->sge.egr_map);
	kfree(adapter->sge.ingr_map);
	kfree(adapter->sge.starving_fl);
	kfree(adapter->sge.txq_maperr);
#ifdef CONFIG_DEBUG_FS
	kfree(adapter->sge.blocked_fl);
#endif
	disable_msi(adapter);

	for_each_port(adapter, i)
		if (adapter->port[i]) {
			struct port_info *pi = adap2pinfo(adapter, i);

			if (pi->viid != 0)
				t4_free_vi(adapter, adapter->mbox, adapter->pf,
					   0, pi->viid);
			kfree(adap2pinfo(adapter, i)->rss);
			free_netdev(adapter->port[i]);
		}
	if (adapter->flags & CXGB4_FW_OK)
		t4_fw_bye(adapter, adapter->pf);
}

#define TSO_FLAGS (NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_TSO_ECN | \
		   NETIF_F_GSO_UDP_L4)
#define VLAN_FEAT (NETIF_F_SG | NETIF_F_IP_CSUM | TSO_FLAGS | \
		   NETIF_F_GRO | NETIF_F_IPV6_CSUM | NETIF_F_HIGHDMA)
#define SEGMENT_SIZE 128

static int t4_get_chip_type(struct adapter *adap, int ver)
{
	u32 pl_rev = REV_G(t4_read_reg(adap, PL_REV_A));

	switch (ver) {
	case CHELSIO_T4:
		return CHELSIO_CHIP_CODE(CHELSIO_T4, pl_rev);
	case CHELSIO_T5:
		return CHELSIO_CHIP_CODE(CHELSIO_T5, pl_rev);
	case CHELSIO_T6:
		return CHELSIO_CHIP_CODE(CHELSIO_T6, pl_rev);
	default:
		break;
	}
	return -EINVAL;
}

#ifdef CONFIG_PCI_IOV
static void cxgb4_mgmt_setup(struct net_device *dev)
{
	dev->type = ARPHRD_NONE;
	dev->mtu = 0;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->tx_queue_len = 0;
	dev->flags |= IFF_NOARP;
	dev->priv_flags |= IFF_NO_QUEUE;

	/* Initialize the device structure. */
	dev->netdev_ops = &cxgb4_mgmt_netdev_ops;
	dev->ethtool_ops = &cxgb4_mgmt_ethtool_ops;
}

static int cxgb4_iov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct adapter *adap = pci_get_drvdata(pdev);
	int err = 0;
	int current_vfs = pci_num_vf(pdev);
	u32 pcie_fw;

	pcie_fw = readl(adap->regs + PCIE_FW_A);
	/* Check if fw is initialized */
	if (!(pcie_fw & PCIE_FW_INIT_F)) {
		dev_warn(&pdev->dev, "Device not initialized\n");
		return -EOPNOTSUPP;
	}

	/* If any of the VF's is already assigned to Guest OS, then
	 * SRIOV for the same cannot be modified
	 */
	if (current_vfs && pci_vfs_assigned(pdev)) {
		dev_err(&pdev->dev,
			"Cannot modify SR-IOV while VFs are assigned\n");
		return current_vfs;
	}
	/* Note that the upper-level code ensures that we're never called with
	 * a non-zero "num_vfs" when we already have VFs instantiated.  But
	 * it never hurts to code defensively.
	 */
	if (num_vfs != 0 && current_vfs != 0)
		return -EBUSY;

	/* Nothing to do for no change. */
	if (num_vfs == current_vfs)
		return num_vfs;

	/* Disable SRIOV when zero is passed. */
	if (!num_vfs) {
		pci_disable_sriov(pdev);
		/* free VF Management Interface */
		unregister_netdev(adap->port[0]);
		free_netdev(adap->port[0]);
		adap->port[0] = NULL;

		/* free VF resources */
		adap->num_vfs = 0;
		kfree(adap->vfinfo);
		adap->vfinfo = NULL;
		return 0;
	}

	if (!current_vfs) {
		struct fw_pfvf_cmd port_cmd, port_rpl;
		struct net_device *netdev;
		unsigned int pmask, port;
		struct pci_dev *pbridge;
		struct port_info *pi;
		char name[IFNAMSIZ];
		u32 devcap2;
		u16 flags;

		/* If we want to instantiate Virtual Functions, then our
		 * parent bridge's PCI-E needs to support Alternative Routing
		 * ID (ARI) because our VFs will show up at function offset 8
		 * and above.
		 */
		pbridge = pdev->bus->self;
		pcie_capability_read_word(pbridge, PCI_EXP_FLAGS, &flags);
		pcie_capability_read_dword(pbridge, PCI_EXP_DEVCAP2, &devcap2);

		if ((flags & PCI_EXP_FLAGS_VERS) < 2 ||
		    !(devcap2 & PCI_EXP_DEVCAP2_ARI)) {
			/* Our parent bridge does not support ARI so issue a
			 * warning and skip instantiating the VFs.  They
			 * won't be reachable.
			 */
			dev_warn(&pdev->dev, "Parent bridge %02x:%02x.%x doesn't support ARI; can't instantiate Virtual Functions\n",
				 pbridge->bus->number, PCI_SLOT(pbridge->devfn),
				 PCI_FUNC(pbridge->devfn));
			return -ENOTSUPP;
		}
		memset(&port_cmd, 0, sizeof(port_cmd));
		port_cmd.op_to_vfn = cpu_to_be32(FW_CMD_OP_V(FW_PFVF_CMD) |
						 FW_CMD_REQUEST_F |
						 FW_CMD_READ_F |
						 FW_PFVF_CMD_PFN_V(adap->pf) |
						 FW_PFVF_CMD_VFN_V(0));
		port_cmd.retval_len16 = cpu_to_be32(FW_LEN16(port_cmd));
		err = t4_wr_mbox(adap, adap->mbox, &port_cmd, sizeof(port_cmd),
				 &port_rpl);
		if (err)
			return err;
		pmask = FW_PFVF_CMD_PMASK_G(be32_to_cpu(port_rpl.type_to_neq));
		port = ffs(pmask) - 1;
		/* Allocate VF Management Interface. */
		snprintf(name, IFNAMSIZ, "mgmtpf%d,%d", adap->adap_idx,
			 adap->pf);
		netdev = alloc_netdev(sizeof(struct port_info),
				      name, NET_NAME_UNKNOWN, cxgb4_mgmt_setup);
		if (!netdev)
			return -ENOMEM;

		pi = netdev_priv(netdev);
		pi->adapter = adap;
		pi->lport = port;
		pi->tx_chan = port;
		SET_NETDEV_DEV(netdev, &pdev->dev);

		adap->port[0] = netdev;
		pi->port_id = 0;

		err = register_netdev(adap->port[0]);
		if (err) {
			pr_info("Unable to register VF mgmt netdev %s\n", name);
			free_netdev(adap->port[0]);
			adap->port[0] = NULL;
			return err;
		}
		/* Allocate and set up VF Information. */
		adap->vfinfo = kcalloc(pci_sriov_get_totalvfs(pdev),
				       sizeof(struct vf_info), GFP_KERNEL);
		if (!adap->vfinfo) {
			unregister_netdev(adap->port[0]);
			free_netdev(adap->port[0]);
			adap->port[0] = NULL;
			return -ENOMEM;
		}
		cxgb4_mgmt_fill_vf_station_mac_addr(adap);
	}
	/* Instantiate the requested number of VFs. */
	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		pr_info("Unable to instantiate %d VFs\n", num_vfs);
		if (!current_vfs) {
			unregister_netdev(adap->port[0]);
			free_netdev(adap->port[0]);
			adap->port[0] = NULL;
			kfree(adap->vfinfo);
			adap->vfinfo = NULL;
		}
		return err;
	}

	adap->num_vfs = num_vfs;
	return num_vfs;
}
#endif /* CONFIG_PCI_IOV */

#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE) || IS_ENABLED(CONFIG_CHELSIO_IPSEC_INLINE)

static int chcr_offload_state(struct adapter *adap,
			      enum cxgb4_netdev_tls_ops op_val)
{
	switch (op_val) {
#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)
	case CXGB4_TLSDEV_OPS:
		if (!adap->uld[CXGB4_ULD_KTLS].handle) {
			dev_dbg(adap->pdev_dev, "ch_ktls driver is not loaded\n");
			return -EOPNOTSUPP;
		}
		if (!adap->uld[CXGB4_ULD_KTLS].tlsdev_ops) {
			dev_dbg(adap->pdev_dev,
				"ch_ktls driver has no registered tlsdev_ops\n");
			return -EOPNOTSUPP;
		}
		break;
#endif /* CONFIG_CHELSIO_TLS_DEVICE */
#if IS_ENABLED(CONFIG_CHELSIO_IPSEC_INLINE)
	case CXGB4_XFRMDEV_OPS:
		if (!adap->uld[CXGB4_ULD_IPSEC].handle) {
			dev_dbg(adap->pdev_dev, "chipsec driver is not loaded\n");
			return -EOPNOTSUPP;
		}
		if (!adap->uld[CXGB4_ULD_IPSEC].xfrmdev_ops) {
			dev_dbg(adap->pdev_dev,
				"chipsec driver has no registered xfrmdev_ops\n");
			return -EOPNOTSUPP;
		}
		break;
#endif /* CONFIG_CHELSIO_IPSEC_INLINE */
	default:
		dev_dbg(adap->pdev_dev,
			"driver has no support for offload %d\n", op_val);
		return -EOPNOTSUPP;
	}

	return 0;
}

#endif /* CONFIG_CHELSIO_TLS_DEVICE || CONFIG_CHELSIO_IPSEC_INLINE */

#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)

static int cxgb4_ktls_dev_add(struct net_device *netdev, struct sock *sk,
			      enum tls_offload_ctx_dir direction,
			      struct tls_crypto_info *crypto_info,
			      u32 tcp_sn)
{
	struct adapter *adap = netdev2adap(netdev);
	int ret;

	mutex_lock(&uld_mutex);
	ret = chcr_offload_state(adap, CXGB4_TLSDEV_OPS);
	if (ret)
		goto out_unlock;

	ret = cxgb4_set_ktls_feature(adap, FW_PARAMS_PARAM_DEV_KTLS_HW_ENABLE);
	if (ret)
		goto out_unlock;

	ret = adap->uld[CXGB4_ULD_KTLS].tlsdev_ops->tls_dev_add(netdev, sk,
								direction,
								crypto_info,
								tcp_sn);
	/* if there is a failure, clear the refcount */
	if (ret)
		cxgb4_set_ktls_feature(adap,
				       FW_PARAMS_PARAM_DEV_KTLS_HW_DISABLE);
out_unlock:
	mutex_unlock(&uld_mutex);
	return ret;
}

static void cxgb4_ktls_dev_del(struct net_device *netdev,
			       struct tls_context *tls_ctx,
			       enum tls_offload_ctx_dir direction)
{
	struct adapter *adap = netdev2adap(netdev);

	mutex_lock(&uld_mutex);
	if (chcr_offload_state(adap, CXGB4_TLSDEV_OPS))
		goto out_unlock;

	adap->uld[CXGB4_ULD_KTLS].tlsdev_ops->tls_dev_del(netdev, tls_ctx,
							  direction);

out_unlock:
	cxgb4_set_ktls_feature(adap, FW_PARAMS_PARAM_DEV_KTLS_HW_DISABLE);
	mutex_unlock(&uld_mutex);
}

static const struct tlsdev_ops cxgb4_ktls_ops = {
	.tls_dev_add = cxgb4_ktls_dev_add,
	.tls_dev_del = cxgb4_ktls_dev_del,
};
#endif /* CONFIG_CHELSIO_TLS_DEVICE */

#if IS_ENABLED(CONFIG_CHELSIO_IPSEC_INLINE)

static int cxgb4_xfrm_add_state(struct xfrm_state *x)
{
	struct adapter *adap = netdev2adap(x->xso.dev);
	int ret;

	if (!mutex_trylock(&uld_mutex)) {
		dev_dbg(adap->pdev_dev,
			"crypto uld critical resource is under use\n");
		return -EBUSY;
	}
	ret = chcr_offload_state(adap, CXGB4_XFRMDEV_OPS);
	if (ret)
		goto out_unlock;

	ret = adap->uld[CXGB4_ULD_IPSEC].xfrmdev_ops->xdo_dev_state_add(x);

out_unlock:
	mutex_unlock(&uld_mutex);

	return ret;
}

static void cxgb4_xfrm_del_state(struct xfrm_state *x)
{
	struct adapter *adap = netdev2adap(x->xso.dev);

	if (!mutex_trylock(&uld_mutex)) {
		dev_dbg(adap->pdev_dev,
			"crypto uld critical resource is under use\n");
		return;
	}
	if (chcr_offload_state(adap, CXGB4_XFRMDEV_OPS))
		goto out_unlock;

	adap->uld[CXGB4_ULD_IPSEC].xfrmdev_ops->xdo_dev_state_delete(x);

out_unlock:
	mutex_unlock(&uld_mutex);
}

static void cxgb4_xfrm_free_state(struct xfrm_state *x)
{
	struct adapter *adap = netdev2adap(x->xso.dev);

	if (!mutex_trylock(&uld_mutex)) {
		dev_dbg(adap->pdev_dev,
			"crypto uld critical resource is under use\n");
		return;
	}
	if (chcr_offload_state(adap, CXGB4_XFRMDEV_OPS))
		goto out_unlock;

	adap->uld[CXGB4_ULD_IPSEC].xfrmdev_ops->xdo_dev_state_free(x);

out_unlock:
	mutex_unlock(&uld_mutex);
}

static bool cxgb4_ipsec_offload_ok(struct sk_buff *skb, struct xfrm_state *x)
{
	struct adapter *adap = netdev2adap(x->xso.dev);
	bool ret = false;

	if (!mutex_trylock(&uld_mutex)) {
		dev_dbg(adap->pdev_dev,
			"crypto uld critical resource is under use\n");
		return ret;
	}
	if (chcr_offload_state(adap, CXGB4_XFRMDEV_OPS))
		goto out_unlock;

	ret = adap->uld[CXGB4_ULD_IPSEC].xfrmdev_ops->xdo_dev_offload_ok(skb, x);

out_unlock:
	mutex_unlock(&uld_mutex);
	return ret;
}

static void cxgb4_advance_esn_state(struct xfrm_state *x)
{
	struct adapter *adap = netdev2adap(x->xso.dev);

	if (!mutex_trylock(&uld_mutex)) {
		dev_dbg(adap->pdev_dev,
			"crypto uld critical resource is under use\n");
		return;
	}
	if (chcr_offload_state(adap, CXGB4_XFRMDEV_OPS))
		goto out_unlock;

	adap->uld[CXGB4_ULD_IPSEC].xfrmdev_ops->xdo_dev_state_advance_esn(x);

out_unlock:
	mutex_unlock(&uld_mutex);
}

static const struct xfrmdev_ops cxgb4_xfrmdev_ops = {
	.xdo_dev_state_add      = cxgb4_xfrm_add_state,
	.xdo_dev_state_delete   = cxgb4_xfrm_del_state,
	.xdo_dev_state_free     = cxgb4_xfrm_free_state,
	.xdo_dev_offload_ok     = cxgb4_ipsec_offload_ok,
	.xdo_dev_state_advance_esn = cxgb4_advance_esn_state,
};

#endif /* CONFIG_CHELSIO_IPSEC_INLINE */

static int init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct adapter *adapter;
	static int adap_idx = 1;
	int s_qpp, qpp, num_seg;
	struct port_info *pi;
	enum chip_type chip;
	void __iomem *regs;
	int func, chip_ver;
	u16 device_id;
	int i, err;
	u32 whoami;

	err = pci_request_regions(pdev, KBUILD_MODNAME);
	if (err) {
		/* Just info, some other driver may have claimed the device. */
		dev_info(&pdev->dev, "cannot obtain PCI resources\n");
		return err;
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "cannot enable PCI device\n");
		goto out_release_regions;
	}

	regs = pci_ioremap_bar(pdev, 0);
	if (!regs) {
		dev_err(&pdev->dev, "cannot map device registers\n");
		err = -ENOMEM;
		goto out_disable_device;
	}

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		err = -ENOMEM;
		goto out_unmap_bar0;
	}

	adapter->regs = regs;
	err = t4_wait_dev_ready(regs);
	if (err < 0)
		goto out_free_adapter;

	/* We control everything through one PF */
	whoami = t4_read_reg(adapter, PL_WHOAMI_A);
	pci_read_config_word(pdev, PCI_DEVICE_ID, &device_id);
	chip = t4_get_chip_type(adapter, CHELSIO_PCI_ID_VER(device_id));
	if ((int)chip < 0) {
		dev_err(&pdev->dev, "Device %d is not supported\n", device_id);
		err = chip;
		goto out_free_adapter;
	}
	chip_ver = CHELSIO_CHIP_VERSION(chip);
	func = chip_ver <= CHELSIO_T5 ?
	       SOURCEPF_G(whoami) : T6_SOURCEPF_G(whoami);

	adapter->pdev = pdev;
	adapter->pdev_dev = &pdev->dev;
	adapter->name = pci_name(pdev);
	adapter->mbox = func;
	adapter->pf = func;
	adapter->params.chip = chip;
	adapter->adap_idx = adap_idx;
	adapter->msg_enable = DFLT_MSG_ENABLE;
	adapter->mbox_log = kzalloc(sizeof(*adapter->mbox_log) +
				    (sizeof(struct mbox_cmd) *
				     T4_OS_LOG_MBOX_CMDS),
				    GFP_KERNEL);
	if (!adapter->mbox_log) {
		err = -ENOMEM;
		goto out_free_adapter;
	}
	spin_lock_init(&adapter->mbox_lock);
	INIT_LIST_HEAD(&adapter->mlist.list);
	adapter->mbox_log->size = T4_OS_LOG_MBOX_CMDS;
	pci_set_drvdata(pdev, adapter);

	if (func != ent->driver_data) {
		pci_disable_device(pdev);
		pci_save_state(pdev);        /* to restore SR-IOV later */
		return 0;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev, "no usable DMA configuration\n");
		goto out_free_adapter;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);
	pci_save_state(pdev);
	adap_idx++;
	adapter->workq = create_singlethread_workqueue("cxgb4");
	if (!adapter->workq) {
		err = -ENOMEM;
		goto out_free_adapter;
	}

	/* PCI device has been enabled */
	adapter->flags |= CXGB4_DEV_ENABLED;
	memset(adapter->chan_map, 0xff, sizeof(adapter->chan_map));

	/* If possible, we use PCIe Relaxed Ordering Attribute to deliver
	 * Ingress Packet Data to Free List Buffers in order to allow for
	 * chipset performance optimizations between the Root Complex and
	 * Memory Controllers.  (Messages to the associated Ingress Queue
	 * notifying new Packet Placement in the Free Lists Buffers will be
	 * send without the Relaxed Ordering Attribute thus guaranteeing that
	 * all preceding PCIe Transaction Layer Packets will be processed
	 * first.)  But some Root Complexes have various issues with Upstream
	 * Transaction Layer Packets with the Relaxed Ordering Attribute set.
	 * The PCIe devices which under the Root Complexes will be cleared the
	 * Relaxed Ordering bit in the configuration space, So we check our
	 * PCIe configuration space to see if it's flagged with advice against
	 * using Relaxed Ordering.
	 */
	if (!pcie_relaxed_ordering_enabled(pdev))
		adapter->flags |= CXGB4_ROOT_NO_RELAXED_ORDERING;

	spin_lock_init(&adapter->stats_lock);
	spin_lock_init(&adapter->tid_release_lock);
	spin_lock_init(&adapter->win0_lock);

	INIT_WORK(&adapter->tid_release_task, process_tid_release_list);
	INIT_WORK(&adapter->db_full_task, process_db_full);
	INIT_WORK(&adapter->db_drop_task, process_db_drop);
	INIT_WORK(&adapter->fatal_err_notify_task, notify_fatal_err);

	err = t4_prep_adapter(adapter);
	if (err)
		goto out_free_adapter;

	if (is_kdump_kernel()) {
		/* Collect hardware state and append to /proc/vmcore */
		err = cxgb4_cudbg_vmcore_add_dump(adapter);
		if (err) {
			dev_warn(adapter->pdev_dev,
				 "Fail collecting vmcore device dump, err: %d. Continuing\n",
				 err);
			err = 0;
		}
	}

	if (!is_t4(adapter->params.chip)) {
		s_qpp = (QUEUESPERPAGEPF0_S +
			(QUEUESPERPAGEPF1_S - QUEUESPERPAGEPF0_S) *
			adapter->pf);
		qpp = 1 << QUEUESPERPAGEPF0_G(t4_read_reg(adapter,
		      SGE_EGRESS_QUEUES_PER_PAGE_PF_A) >> s_qpp);
		num_seg = PAGE_SIZE / SEGMENT_SIZE;

		/* Each segment size is 128B. Write coalescing is enabled only
		 * when SGE_EGRESS_QUEUES_PER_PAGE_PF reg value for the
		 * queue is less no of segments that can be accommodated in
		 * a page size.
		 */
		if (qpp > num_seg) {
			dev_err(&pdev->dev,
				"Incorrect number of egress queues per page\n");
			err = -EINVAL;
			goto out_free_adapter;
		}
		adapter->bar2 = ioremap_wc(pci_resource_start(pdev, 2),
		pci_resource_len(pdev, 2));
		if (!adapter->bar2) {
			dev_err(&pdev->dev, "cannot map device bar2 region\n");
			err = -ENOMEM;
			goto out_free_adapter;
		}
	}

	setup_memwin(adapter);
	err = adap_init0(adapter, 0);
	if (err)
		goto out_unmap_bar;

	setup_memwin_rdma(adapter);

	/* configure SGE_STAT_CFG_A to read WC stats */
	if (!is_t4(adapter->params.chip))
		t4_write_reg(adapter, SGE_STAT_CFG_A, STATSOURCE_T5_V(7) |
			     (is_t5(adapter->params.chip) ? STATMODE_V(0) :
			      T6_STATMODE_V(0)));

	/* Initialize hash mac addr list */
	INIT_LIST_HEAD(&adapter->mac_hlist);

	for_each_port(adapter, i) {
		/* For supporting MQPRIO Offload, need some extra
		 * queues for each ETHOFLD TIDs. Keep it equal to
		 * MAX_ATIDs for now. Once we connect to firmware
		 * later and query the EOTID params, we'll come to
		 * know the actual # of EOTIDs supported.
		 */
		netdev = alloc_etherdev_mq(sizeof(struct port_info),
					   MAX_ETH_QSETS + MAX_ATIDS);
		if (!netdev) {
			err = -ENOMEM;
			goto out_free_dev;
		}

		SET_NETDEV_DEV(netdev, &pdev->dev);

		adapter->port[i] = netdev;
		pi = netdev_priv(netdev);
		pi->adapter = adapter;
		pi->xact_addr_filt = -1;
		pi->port_id = i;
		netdev->irq = pdev->irq;

		netdev->hw_features = NETIF_F_SG | TSO_FLAGS |
			NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			NETIF_F_RXCSUM | NETIF_F_RXHASH | NETIF_F_GRO |
			NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
			NETIF_F_HW_TC | NETIF_F_NTUPLE | NETIF_F_HIGHDMA;

		if (chip_ver > CHELSIO_T5) {
			netdev->hw_enc_features |= NETIF_F_IP_CSUM |
						   NETIF_F_IPV6_CSUM |
						   NETIF_F_RXCSUM |
						   NETIF_F_GSO_UDP_TUNNEL |
						   NETIF_F_GSO_UDP_TUNNEL_CSUM |
						   NETIF_F_TSO | NETIF_F_TSO6;

			netdev->hw_features |= NETIF_F_GSO_UDP_TUNNEL |
					       NETIF_F_GSO_UDP_TUNNEL_CSUM |
					       NETIF_F_HW_TLS_RECORD;

			if (adapter->rawf_cnt)
				netdev->udp_tunnel_nic_info = &cxgb_udp_tunnels;
		}

		netdev->features |= netdev->hw_features;
		netdev->vlan_features = netdev->features & VLAN_FEAT;
#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)
		if (pi->adapter->params.crypto & FW_CAPS_CONFIG_TLS_HW) {
			netdev->hw_features |= NETIF_F_HW_TLS_TX;
			netdev->tlsdev_ops = &cxgb4_ktls_ops;
			/* initialize the refcount */
			refcount_set(&pi->adapter->chcr_ktls.ktls_refcount, 0);
		}
#endif /* CONFIG_CHELSIO_TLS_DEVICE */
#if IS_ENABLED(CONFIG_CHELSIO_IPSEC_INLINE)
		if (pi->adapter->params.crypto & FW_CAPS_CONFIG_IPSEC_INLINE) {
			netdev->hw_enc_features |= NETIF_F_HW_ESP;
			netdev->features |= NETIF_F_HW_ESP;
			netdev->xfrmdev_ops = &cxgb4_xfrmdev_ops;
		}
#endif /* CONFIG_CHELSIO_IPSEC_INLINE */

		netdev->priv_flags |= IFF_UNICAST_FLT;

		/* MTU range: 81 - 9600 */
		netdev->min_mtu = 81;              /* accommodate SACK */
		netdev->max_mtu = MAX_MTU;

		netdev->netdev_ops = &cxgb4_netdev_ops;
#ifdef CONFIG_CHELSIO_T4_DCB
		netdev->dcbnl_ops = &cxgb4_dcb_ops;
		cxgb4_dcb_state_init(netdev);
		cxgb4_dcb_version_init(netdev);
#endif
		cxgb4_set_ethtool_ops(netdev);
	}

	cxgb4_init_ethtool_dump(adapter);

	pci_set_drvdata(pdev, adapter);

	if (adapter->flags & CXGB4_FW_OK) {
		err = t4_port_init(adapter, func, func, 0);
		if (err)
			goto out_free_dev;
	} else if (adapter->params.nports == 1) {
		/* If we don't have a connection to the firmware -- possibly
		 * because of an error -- grab the raw VPD parameters so we
		 * can set the proper MAC Address on the debug network
		 * interface that we've created.
		 */
		u8 hw_addr[ETH_ALEN];
		u8 *na = adapter->params.vpd.na;

		err = t4_get_raw_vpd_params(adapter, &adapter->params.vpd);
		if (!err) {
			for (i = 0; i < ETH_ALEN; i++)
				hw_addr[i] = (hex2val(na[2 * i + 0]) * 16 +
					      hex2val(na[2 * i + 1]));
			t4_set_hw_addr(adapter, 0, hw_addr);
		}
	}

	if (!(adapter->flags & CXGB4_FW_OK))
		goto fw_attach_fail;

	/* Configure queues and allocate tables now, they can be needed as
	 * soon as the first register_netdev completes.
	 */
	err = cfg_queues(adapter);
	if (err)
		goto out_free_dev;

	adapter->smt = t4_init_smt();
	if (!adapter->smt) {
		/* We tolerate a lack of SMT, giving up some functionality */
		dev_warn(&pdev->dev, "could not allocate SMT, continuing\n");
	}

	adapter->l2t = t4_init_l2t(adapter->l2t_start, adapter->l2t_end);
	if (!adapter->l2t) {
		/* We tolerate a lack of L2T, giving up some functionality */
		dev_warn(&pdev->dev, "could not allocate L2T, continuing\n");
		adapter->params.offload = 0;
	}

#if IS_ENABLED(CONFIG_IPV6)
	if (chip_ver <= CHELSIO_T5 &&
	    (!(t4_read_reg(adapter, LE_DB_CONFIG_A) & ASLIPCOMPEN_F))) {
		/* CLIP functionality is not present in hardware,
		 * hence disable all offload features
		 */
		dev_warn(&pdev->dev,
			 "CLIP not enabled in hardware, continuing\n");
		adapter->params.offload = 0;
	} else {
		adapter->clipt = t4_init_clip_tbl(adapter->clipt_start,
						  adapter->clipt_end);
		if (!adapter->clipt) {
			/* We tolerate a lack of clip_table, giving up
			 * some functionality
			 */
			dev_warn(&pdev->dev,
				 "could not allocate Clip table, continuing\n");
			adapter->params.offload = 0;
		}
	}
#endif

	for_each_port(adapter, i) {
		pi = adap2pinfo(adapter, i);
		pi->sched_tbl = t4_init_sched(adapter->params.nsched_cls);
		if (!pi->sched_tbl)
			dev_warn(&pdev->dev,
				 "could not activate scheduling on port %d\n",
				 i);
	}

	if (is_offload(adapter) || is_hashfilter(adapter)) {
		if (t4_read_reg(adapter, LE_DB_CONFIG_A) & HASHEN_F) {
			u32 v;

			v = t4_read_reg(adapter, LE_DB_HASH_CONFIG_A);
			if (chip_ver <= CHELSIO_T5) {
				adapter->tids.nhash = 1 << HASHTIDSIZE_G(v);
				v = t4_read_reg(adapter, LE_DB_TID_HASHBASE_A);
				adapter->tids.hash_base = v / 4;
			} else {
				adapter->tids.nhash = HASHTBLSIZE_G(v) << 3;
				v = t4_read_reg(adapter,
						T6_LE_DB_HASH_TID_BASE_A);
				adapter->tids.hash_base = v;
			}
		}
	}

	if (tid_init(&adapter->tids) < 0) {
		dev_warn(&pdev->dev, "could not allocate TID table, "
			 "continuing\n");
		adapter->params.offload = 0;
	} else {
		adapter->tc_u32 = cxgb4_init_tc_u32(adapter);
		if (!adapter->tc_u32)
			dev_warn(&pdev->dev,
				 "could not offload tc u32, continuing\n");

		if (cxgb4_init_tc_flower(adapter))
			dev_warn(&pdev->dev,
				 "could not offload tc flower, continuing\n");

		if (cxgb4_init_tc_mqprio(adapter))
			dev_warn(&pdev->dev,
				 "could not offload tc mqprio, continuing\n");

		if (cxgb4_init_tc_matchall(adapter))
			dev_warn(&pdev->dev,
				 "could not offload tc matchall, continuing\n");
		if (cxgb4_init_ethtool_filters(adapter))
			dev_warn(&pdev->dev,
				 "could not initialize ethtool filters, continuing\n");
	}

	/* See what interrupts we'll be using */
	if (msi > 1 && enable_msix(adapter) == 0)
		adapter->flags |= CXGB4_USING_MSIX;
	else if (msi > 0 && pci_enable_msi(pdev) == 0) {
		adapter->flags |= CXGB4_USING_MSI;
		if (msi > 1)
			free_msix_info(adapter);
	}

	/* check for PCI Express bandwidth capabiltites */
	pcie_print_link_status(pdev);

	cxgb4_init_mps_ref_entries(adapter);

	err = init_rss(adapter);
	if (err)
		goto out_free_dev;

	err = setup_non_data_intr(adapter);
	if (err) {
		dev_err(adapter->pdev_dev,
			"Non Data interrupt allocation failed, err: %d\n", err);
		goto out_free_dev;
	}

	err = setup_fw_sge_queues(adapter);
	if (err) {
		dev_err(adapter->pdev_dev,
			"FW sge queue allocation failed, err %d", err);
		goto out_free_dev;
	}

fw_attach_fail:
	/*
	 * The card is now ready to go.  If any errors occur during device
	 * registration we do not fail the whole card but rather proceed only
	 * with the ports we manage to register successfully.  However we must
	 * register at least one net device.
	 */
	for_each_port(adapter, i) {
		pi = adap2pinfo(adapter, i);
		adapter->port[i]->dev_port = pi->lport;
		netif_set_real_num_tx_queues(adapter->port[i], pi->nqsets);
		netif_set_real_num_rx_queues(adapter->port[i], pi->nqsets);

		netif_carrier_off(adapter->port[i]);

		err = register_netdev(adapter->port[i]);
		if (err)
			break;
		adapter->chan_map[pi->tx_chan] = i;
		print_port_info(adapter->port[i]);
	}
	if (i == 0) {
		dev_err(&pdev->dev, "could not register any net devices\n");
		goto out_free_dev;
	}
	if (err) {
		dev_warn(&pdev->dev, "only %d net devices registered\n", i);
		err = 0;
	}

	if (cxgb4_debugfs_root) {
		adapter->debugfs_root = debugfs_create_dir(pci_name(pdev),
							   cxgb4_debugfs_root);
		setup_debugfs(adapter);
	}

	/* PCIe EEH recovery on powerpc platforms needs fundamental reset */
	pdev->needs_freset = 1;

	if (is_uld(adapter))
		cxgb4_uld_enable(adapter);

	if (!is_t4(adapter->params.chip))
		cxgb4_ptp_init(adapter);

	if (IS_REACHABLE(CONFIG_THERMAL) &&
	    !is_t4(adapter->params.chip) && (adapter->flags & CXGB4_FW_OK))
		cxgb4_thermal_init(adapter);

	print_adapter_info(adapter);
	return 0;

 out_free_dev:
	t4_free_sge_resources(adapter);
	free_some_resources(adapter);
	if (adapter->flags & CXGB4_USING_MSIX)
		free_msix_info(adapter);
	if (adapter->num_uld || adapter->num_ofld_uld)
		t4_uld_mem_free(adapter);
 out_unmap_bar:
	if (!is_t4(adapter->params.chip))
		iounmap(adapter->bar2);
 out_free_adapter:
	if (adapter->workq)
		destroy_workqueue(adapter->workq);

	kfree(adapter->mbox_log);
	kfree(adapter);
 out_unmap_bar0:
	iounmap(regs);
 out_disable_device:
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
 out_release_regions:
	pci_release_regions(pdev);
	return err;
}

static void remove_one(struct pci_dev *pdev)
{
	struct adapter *adapter = pci_get_drvdata(pdev);
	struct hash_mac_addr *entry, *tmp;

	if (!adapter) {
		pci_release_regions(pdev);
		return;
	}

	/* If we allocated filters, free up state associated with any
	 * valid filters ...
	 */
	clear_all_filters(adapter);

	adapter->flags |= CXGB4_SHUTTING_DOWN;

	if (adapter->pf == 4) {
		int i;

		/* Tear down per-adapter Work Queue first since it can contain
		 * references to our adapter data structure.
		 */
		destroy_workqueue(adapter->workq);

		detach_ulds(adapter);

		for_each_port(adapter, i)
			if (adapter->port[i]->reg_state == NETREG_REGISTERED)
				unregister_netdev(adapter->port[i]);

		t4_uld_clean_up(adapter);

		adap_free_hma_mem(adapter);

		disable_interrupts(adapter);

		cxgb4_free_mps_ref_entries(adapter);

		debugfs_remove_recursive(adapter->debugfs_root);

		if (!is_t4(adapter->params.chip))
			cxgb4_ptp_stop(adapter);
		if (IS_REACHABLE(CONFIG_THERMAL))
			cxgb4_thermal_remove(adapter);

		if (adapter->flags & CXGB4_FULL_INIT_DONE)
			cxgb_down(adapter);

		if (adapter->flags & CXGB4_USING_MSIX)
			free_msix_info(adapter);
		if (adapter->num_uld || adapter->num_ofld_uld)
			t4_uld_mem_free(adapter);
		free_some_resources(adapter);
		list_for_each_entry_safe(entry, tmp, &adapter->mac_hlist,
					 list) {
			list_del(&entry->list);
			kfree(entry);
		}

#if IS_ENABLED(CONFIG_IPV6)
		t4_cleanup_clip_tbl(adapter);
#endif
		if (!is_t4(adapter->params.chip))
			iounmap(adapter->bar2);
	}
#ifdef CONFIG_PCI_IOV
	else {
		cxgb4_iov_configure(adapter->pdev, 0);
	}
#endif
	iounmap(adapter->regs);
	pci_disable_pcie_error_reporting(pdev);
	if ((adapter->flags & CXGB4_DEV_ENABLED)) {
		pci_disable_device(pdev);
		adapter->flags &= ~CXGB4_DEV_ENABLED;
	}
	pci_release_regions(pdev);
	kfree(adapter->mbox_log);
	synchronize_rcu();
	kfree(adapter);
}

/* "Shutdown" quiesces the device, stopping Ingress Packet and Interrupt
 * delivery.  This is essentially a stripped down version of the PCI remove()
 * function where we do the minimal amount of work necessary to shutdown any
 * further activity.
 */
static void shutdown_one(struct pci_dev *pdev)
{
	struct adapter *adapter = pci_get_drvdata(pdev);

	/* As with remove_one() above (see extended comment), we only want do
	 * do cleanup on PCI Devices which went all the way through init_one()
	 * ...
	 */
	if (!adapter) {
		pci_release_regions(pdev);
		return;
	}

	adapter->flags |= CXGB4_SHUTTING_DOWN;

	if (adapter->pf == 4) {
		int i;

		for_each_port(adapter, i)
			if (adapter->port[i]->reg_state == NETREG_REGISTERED)
				cxgb_close(adapter->port[i]);

		rtnl_lock();
		cxgb4_mqprio_stop_offload(adapter);
		rtnl_unlock();

		if (is_uld(adapter)) {
			detach_ulds(adapter);
			t4_uld_clean_up(adapter);
		}

		disable_interrupts(adapter);
		disable_msi(adapter);

		t4_sge_stop(adapter);
		if (adapter->flags & CXGB4_FW_OK)
			t4_fw_bye(adapter, adapter->mbox);
	}
}

static struct pci_driver cxgb4_driver = {
	.name     = KBUILD_MODNAME,
	.id_table = cxgb4_pci_tbl,
	.probe    = init_one,
	.remove   = remove_one,
	.shutdown = shutdown_one,
#ifdef CONFIG_PCI_IOV
	.sriov_configure = cxgb4_iov_configure,
#endif
	.err_handler = &cxgb4_eeh,
};

static int __init cxgb4_init_module(void)
{
	int ret;

	cxgb4_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);

	ret = pci_register_driver(&cxgb4_driver);
	if (ret < 0)
		goto err_pci;

#if IS_ENABLED(CONFIG_IPV6)
	if (!inet6addr_registered) {
		ret = register_inet6addr_notifier(&cxgb4_inet6addr_notifier);
		if (ret)
			pci_unregister_driver(&cxgb4_driver);
		else
			inet6addr_registered = true;
	}
#endif

	if (ret == 0)
		return ret;

err_pci:
	debugfs_remove(cxgb4_debugfs_root);

	return ret;
}

static void __exit cxgb4_cleanup_module(void)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (inet6addr_registered) {
		unregister_inet6addr_notifier(&cxgb4_inet6addr_notifier);
		inet6addr_registered = false;
	}
#endif
	pci_unregister_driver(&cxgb4_driver);
	debugfs_remove(cxgb4_debugfs_root);  /* NULL ok */
}

module_init(cxgb4_init_module);
module_exit(cxgb4_cleanup_module);
