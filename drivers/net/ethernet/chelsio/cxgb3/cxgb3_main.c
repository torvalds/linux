/*
 * Copyright (c) 2003-2008 Chelsio, Inc. All rights reserved.
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/mdio.h>
#include <linux/sockios.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/rtnetlink.h>
#include <linux/firmware.h>
#include <linux/log2.h>
#include <linux/stringify.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/nospec.h>

#include "common.h"
#include "cxgb3_ioctl.h"
#include "regs.h"
#include "cxgb3_offload.h"
#include "version.h"

#include "cxgb3_ctl_defs.h"
#include "t3_cpl.h"
#include "firmware_exports.h"

enum {
	MAX_TXQ_ENTRIES = 16384,
	MAX_CTRL_TXQ_ENTRIES = 1024,
	MAX_RSPQ_ENTRIES = 16384,
	MAX_RX_BUFFERS = 16384,
	MAX_RX_JUMBO_BUFFERS = 16384,
	MIN_TXQ_ENTRIES = 4,
	MIN_CTRL_TXQ_ENTRIES = 4,
	MIN_RSPQ_ENTRIES = 32,
	MIN_FL_ENTRIES = 32
};

#define PORT_MASK ((1 << MAX_NPORTS) - 1)

#define DFLT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK | \
			 NETIF_MSG_TIMER | NETIF_MSG_IFDOWN | NETIF_MSG_IFUP |\
			 NETIF_MSG_RX_ERR | NETIF_MSG_TX_ERR)

#define EEPROM_MAGIC 0x38E2F10C

#define CH_DEVICE(devid, idx) \
	{ PCI_VENDOR_ID_CHELSIO, devid, PCI_ANY_ID, PCI_ANY_ID, 0, 0, idx }

static const struct pci_device_id cxgb3_pci_tbl[] = {
	CH_DEVICE(0x20, 0),	/* PE9000 */
	CH_DEVICE(0x21, 1),	/* T302E */
	CH_DEVICE(0x22, 2),	/* T310E */
	CH_DEVICE(0x23, 3),	/* T320X */
	CH_DEVICE(0x24, 1),	/* T302X */
	CH_DEVICE(0x25, 3),	/* T320E */
	CH_DEVICE(0x26, 2),	/* T310X */
	CH_DEVICE(0x30, 2),	/* T3B10 */
	CH_DEVICE(0x31, 3),	/* T3B20 */
	CH_DEVICE(0x32, 1),	/* T3B02 */
	CH_DEVICE(0x35, 6),	/* T3C20-derived T3C10 */
	CH_DEVICE(0x36, 3),	/* S320E-CR */
	CH_DEVICE(0x37, 7),	/* N320E-G2 */
	{0,}
};

MODULE_DESCRIPTION(DRV_DESC);
MODULE_AUTHOR("Chelsio Communications");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DEVICE_TABLE(pci, cxgb3_pci_tbl);

static int dflt_msg_enable = DFLT_MSG_ENABLE;

module_param(dflt_msg_enable, int, 0644);
MODULE_PARM_DESC(dflt_msg_enable, "Chelsio T3 default message enable bitmap");

/*
 * The driver uses the best interrupt scheme available on a platform in the
 * order MSI-X, MSI, legacy pin interrupts.  This parameter determines which
 * of these schemes the driver may consider as follows:
 *
 * msi = 2: choose from among all three options
 * msi = 1: only consider MSI and pin interrupts
 * msi = 0: force pin interrupts
 */
static int msi = 2;

module_param(msi, int, 0644);
MODULE_PARM_DESC(msi, "whether to use MSI or MSI-X");

/*
 * The driver enables offload as a default.
 * To disable it, use ofld_disable = 1.
 */

static int ofld_disable = 0;

module_param(ofld_disable, int, 0644);
MODULE_PARM_DESC(ofld_disable, "whether to enable offload at init time or not");

/*
 * We have work elements that we need to cancel when an interface is taken
 * down.  Normally the work elements would be executed by keventd but that
 * can deadlock because of linkwatch.  If our close method takes the rtnl
 * lock and linkwatch is ahead of our work elements in keventd, linkwatch
 * will block keventd as it needs the rtnl lock, and we'll deadlock waiting
 * for our work to complete.  Get our own work queue to solve this.
 */
struct workqueue_struct *cxgb3_wq;

/**
 *	link_report - show link status and link speed/duplex
 *	@dev: the port whose settings are to be reported
 *
 *	Shows the link status, speed, and duplex of a port.
 */
static void link_report(struct net_device *dev)
{
	if (!netif_carrier_ok(dev))
		netdev_info(dev, "link down\n");
	else {
		const char *s = "10Mbps";
		const struct port_info *p = netdev_priv(dev);

		switch (p->link_config.speed) {
		case SPEED_10000:
			s = "10Gbps";
			break;
		case SPEED_1000:
			s = "1000Mbps";
			break;
		case SPEED_100:
			s = "100Mbps";
			break;
		}

		netdev_info(dev, "link up, %s, %s-duplex\n",
			    s, p->link_config.duplex == DUPLEX_FULL
			    ? "full" : "half");
	}
}

static void enable_tx_fifo_drain(struct adapter *adapter,
				 struct port_info *pi)
{
	t3_set_reg_field(adapter, A_XGM_TXFIFO_CFG + pi->mac.offset, 0,
			 F_ENDROPPKT);
	t3_write_reg(adapter, A_XGM_RX_CTRL + pi->mac.offset, 0);
	t3_write_reg(adapter, A_XGM_TX_CTRL + pi->mac.offset, F_TXEN);
	t3_write_reg(adapter, A_XGM_RX_CTRL + pi->mac.offset, F_RXEN);
}

static void disable_tx_fifo_drain(struct adapter *adapter,
				  struct port_info *pi)
{
	t3_set_reg_field(adapter, A_XGM_TXFIFO_CFG + pi->mac.offset,
			 F_ENDROPPKT, 0);
}

void t3_os_link_fault(struct adapter *adap, int port_id, int state)
{
	struct net_device *dev = adap->port[port_id];
	struct port_info *pi = netdev_priv(dev);

	if (state == netif_carrier_ok(dev))
		return;

	if (state) {
		struct cmac *mac = &pi->mac;

		netif_carrier_on(dev);

		disable_tx_fifo_drain(adap, pi);

		/* Clear local faults */
		t3_xgm_intr_disable(adap, pi->port_id);
		t3_read_reg(adap, A_XGM_INT_STATUS +
				    pi->mac.offset);
		t3_write_reg(adap,
			     A_XGM_INT_CAUSE + pi->mac.offset,
			     F_XGM_INT);

		t3_set_reg_field(adap,
				 A_XGM_INT_ENABLE +
				 pi->mac.offset,
				 F_XGM_INT, F_XGM_INT);
		t3_xgm_intr_enable(adap, pi->port_id);

		t3_mac_enable(mac, MAC_DIRECTION_TX);
	} else {
		netif_carrier_off(dev);

		/* Flush TX FIFO */
		enable_tx_fifo_drain(adap, pi);
	}
	link_report(dev);
}

/**
 *	t3_os_link_changed - handle link status changes
 *	@adapter: the adapter associated with the link change
 *	@port_id: the port index whose limk status has changed
 *	@link_stat: the new status of the link
 *	@speed: the new speed setting
 *	@duplex: the new duplex setting
 *	@pause: the new flow-control setting
 *
 *	This is the OS-dependent handler for link status changes.  The OS
 *	neutral handler takes care of most of the processing for these events,
 *	then calls this handler for any OS-specific processing.
 */
void t3_os_link_changed(struct adapter *adapter, int port_id, int link_stat,
			int speed, int duplex, int pause)
{
	struct net_device *dev = adapter->port[port_id];
	struct port_info *pi = netdev_priv(dev);
	struct cmac *mac = &pi->mac;

	/* Skip changes from disabled ports. */
	if (!netif_running(dev))
		return;

	if (link_stat != netif_carrier_ok(dev)) {
		if (link_stat) {
			disable_tx_fifo_drain(adapter, pi);

			t3_mac_enable(mac, MAC_DIRECTION_RX);

			/* Clear local faults */
			t3_xgm_intr_disable(adapter, pi->port_id);
			t3_read_reg(adapter, A_XGM_INT_STATUS +
				    pi->mac.offset);
			t3_write_reg(adapter,
				     A_XGM_INT_CAUSE + pi->mac.offset,
				     F_XGM_INT);

			t3_set_reg_field(adapter,
					 A_XGM_INT_ENABLE + pi->mac.offset,
					 F_XGM_INT, F_XGM_INT);
			t3_xgm_intr_enable(adapter, pi->port_id);

			netif_carrier_on(dev);
		} else {
			netif_carrier_off(dev);

			t3_xgm_intr_disable(adapter, pi->port_id);
			t3_read_reg(adapter, A_XGM_INT_STATUS + pi->mac.offset);
			t3_set_reg_field(adapter,
					 A_XGM_INT_ENABLE + pi->mac.offset,
					 F_XGM_INT, 0);

			if (is_10G(adapter))
				pi->phy.ops->power_down(&pi->phy, 1);

			t3_read_reg(adapter, A_XGM_INT_STATUS + pi->mac.offset);
			t3_mac_disable(mac, MAC_DIRECTION_RX);
			t3_link_start(&pi->phy, mac, &pi->link_config);

			/* Flush TX FIFO */
			enable_tx_fifo_drain(adapter, pi);
		}

		link_report(dev);
	}
}

/**
 *	t3_os_phymod_changed - handle PHY module changes
 *	@adap: the adapter associated with the link change
 *	@port_id: the port index whose limk status has changed
 *
 *	This is the OS-dependent handler for PHY module changes.  It is
 *	invoked when a PHY module is removed or inserted for any OS-specific
 *	processing.
 */
void t3_os_phymod_changed(struct adapter *adap, int port_id)
{
	static const char *mod_str[] = {
		NULL, "SR", "LR", "LRM", "TWINAX", "TWINAX", "unknown"
	};

	const struct net_device *dev = adap->port[port_id];
	const struct port_info *pi = netdev_priv(dev);

	if (pi->phy.modtype == phy_modtype_none)
		netdev_info(dev, "PHY module unplugged\n");
	else
		netdev_info(dev, "%s PHY module inserted\n",
			    mod_str[pi->phy.modtype]);
}

static void cxgb_set_rxmode(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);

	t3_mac_set_rx_mode(&pi->mac, dev);
}

/**
 *	link_start - enable a port
 *	@dev: the device to enable
 *
 *	Performs the MAC and PHY actions needed to enable a port.
 */
static void link_start(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct cmac *mac = &pi->mac;

	t3_mac_reset(mac);
	t3_mac_set_num_ucast(mac, MAX_MAC_IDX);
	t3_mac_set_mtu(mac, dev->mtu);
	t3_mac_set_address(mac, LAN_MAC_IDX, dev->dev_addr);
	t3_mac_set_address(mac, SAN_MAC_IDX, pi->iscsic.mac_addr);
	t3_mac_set_rx_mode(mac, dev);
	t3_link_start(&pi->phy, mac, &pi->link_config);
	t3_mac_enable(mac, MAC_DIRECTION_RX | MAC_DIRECTION_TX);
}

static inline void cxgb_disable_msi(struct adapter *adapter)
{
	if (adapter->flags & USING_MSIX) {
		pci_disable_msix(adapter->pdev);
		adapter->flags &= ~USING_MSIX;
	} else if (adapter->flags & USING_MSI) {
		pci_disable_msi(adapter->pdev);
		adapter->flags &= ~USING_MSI;
	}
}

/*
 * Interrupt handler for asynchronous events used with MSI-X.
 */
static irqreturn_t t3_async_intr_handler(int irq, void *cookie)
{
	t3_slow_intr_handler(cookie);
	return IRQ_HANDLED;
}

/*
 * Name the MSI-X interrupts.
 */
static void name_msix_vecs(struct adapter *adap)
{
	int i, j, msi_idx = 1, n = sizeof(adap->msix_info[0].desc) - 1;

	snprintf(adap->msix_info[0].desc, n, "%s", adap->name);
	adap->msix_info[0].desc[n] = 0;

	for_each_port(adap, j) {
		struct net_device *d = adap->port[j];
		const struct port_info *pi = netdev_priv(d);

		for (i = 0; i < pi->nqsets; i++, msi_idx++) {
			snprintf(adap->msix_info[msi_idx].desc, n,
				 "%s-%d", d->name, pi->first_qset + i);
			adap->msix_info[msi_idx].desc[n] = 0;
		}
	}
}

static int request_msix_data_irqs(struct adapter *adap)
{
	int i, j, err, qidx = 0;

	for_each_port(adap, i) {
		int nqsets = adap2pinfo(adap, i)->nqsets;

		for (j = 0; j < nqsets; ++j) {
			err = request_irq(adap->msix_info[qidx + 1].vec,
					  t3_intr_handler(adap,
							  adap->sge.qs[qidx].
							  rspq.polling), 0,
					  adap->msix_info[qidx + 1].desc,
					  &adap->sge.qs[qidx]);
			if (err) {
				while (--qidx >= 0)
					free_irq(adap->msix_info[qidx + 1].vec,
						 &adap->sge.qs[qidx]);
				return err;
			}
			qidx++;
		}
	}
	return 0;
}

static void free_irq_resources(struct adapter *adapter)
{
	if (adapter->flags & USING_MSIX) {
		int i, n = 0;

		free_irq(adapter->msix_info[0].vec, adapter);
		for_each_port(adapter, i)
			n += adap2pinfo(adapter, i)->nqsets;

		for (i = 0; i < n; ++i)
			free_irq(adapter->msix_info[i + 1].vec,
				 &adapter->sge.qs[i]);
	} else
		free_irq(adapter->pdev->irq, adapter);
}

static int await_mgmt_replies(struct adapter *adap, unsigned long init_cnt,
			      unsigned long n)
{
	int attempts = 10;

	while (adap->sge.qs[0].rspq.offload_pkts < init_cnt + n) {
		if (!--attempts)
			return -ETIMEDOUT;
		msleep(10);
	}
	return 0;
}

static int init_tp_parity(struct adapter *adap)
{
	int i;
	struct sk_buff *skb;
	struct cpl_set_tcb_field *greq;
	unsigned long cnt = adap->sge.qs[0].rspq.offload_pkts;

	t3_tp_set_offload_mode(adap, 1);

	for (i = 0; i < 16; i++) {
		struct cpl_smt_write_req *req;

		skb = alloc_skb(sizeof(*req), GFP_KERNEL);
		if (!skb)
			skb = adap->nofail_skb;
		if (!skb)
			goto alloc_skb_fail;

		req = __skb_put_zero(skb, sizeof(*req));
		req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
		OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SMT_WRITE_REQ, i));
		req->mtu_idx = NMTUS - 1;
		req->iff = i;
		t3_mgmt_tx(adap, skb);
		if (skb == adap->nofail_skb) {
			await_mgmt_replies(adap, cnt, i + 1);
			adap->nofail_skb = alloc_skb(sizeof(*greq), GFP_KERNEL);
			if (!adap->nofail_skb)
				goto alloc_skb_fail;
		}
	}

	for (i = 0; i < 2048; i++) {
		struct cpl_l2t_write_req *req;

		skb = alloc_skb(sizeof(*req), GFP_KERNEL);
		if (!skb)
			skb = adap->nofail_skb;
		if (!skb)
			goto alloc_skb_fail;

		req = __skb_put_zero(skb, sizeof(*req));
		req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
		OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_L2T_WRITE_REQ, i));
		req->params = htonl(V_L2T_W_IDX(i));
		t3_mgmt_tx(adap, skb);
		if (skb == adap->nofail_skb) {
			await_mgmt_replies(adap, cnt, 16 + i + 1);
			adap->nofail_skb = alloc_skb(sizeof(*greq), GFP_KERNEL);
			if (!adap->nofail_skb)
				goto alloc_skb_fail;
		}
	}

	for (i = 0; i < 2048; i++) {
		struct cpl_rte_write_req *req;

		skb = alloc_skb(sizeof(*req), GFP_KERNEL);
		if (!skb)
			skb = adap->nofail_skb;
		if (!skb)
			goto alloc_skb_fail;

		req = __skb_put_zero(skb, sizeof(*req));
		req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
		OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_RTE_WRITE_REQ, i));
		req->l2t_idx = htonl(V_L2T_W_IDX(i));
		t3_mgmt_tx(adap, skb);
		if (skb == adap->nofail_skb) {
			await_mgmt_replies(adap, cnt, 16 + 2048 + i + 1);
			adap->nofail_skb = alloc_skb(sizeof(*greq), GFP_KERNEL);
			if (!adap->nofail_skb)
				goto alloc_skb_fail;
		}
	}

	skb = alloc_skb(sizeof(*greq), GFP_KERNEL);
	if (!skb)
		skb = adap->nofail_skb;
	if (!skb)
		goto alloc_skb_fail;

	greq = __skb_put_zero(skb, sizeof(*greq));
	greq->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(greq) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, 0));
	greq->mask = cpu_to_be64(1);
	t3_mgmt_tx(adap, skb);

	i = await_mgmt_replies(adap, cnt, 16 + 2048 + 2048 + 1);
	if (skb == adap->nofail_skb) {
		i = await_mgmt_replies(adap, cnt, 16 + 2048 + 2048 + 1);
		adap->nofail_skb = alloc_skb(sizeof(*greq), GFP_KERNEL);
	}

	t3_tp_set_offload_mode(adap, 0);
	return i;

alloc_skb_fail:
	t3_tp_set_offload_mode(adap, 0);
	return -ENOMEM;
}

/**
 *	setup_rss - configure RSS
 *	@adap: the adapter
 *
 *	Sets up RSS to distribute packets to multiple receive queues.  We
 *	configure the RSS CPU lookup table to distribute to the number of HW
 *	receive queues, and the response queue lookup table to narrow that
 *	down to the response queues actually configured for each port.
 *	We always configure the RSS mapping for two ports since the mapping
 *	table has plenty of entries.
 */
static void setup_rss(struct adapter *adap)
{
	int i;
	unsigned int nq0 = adap2pinfo(adap, 0)->nqsets;
	unsigned int nq1 = adap->port[1] ? adap2pinfo(adap, 1)->nqsets : 1;
	u8 cpus[SGE_QSETS + 1];
	u16 rspq_map[RSS_TABLE_SIZE + 1];

	for (i = 0; i < SGE_QSETS; ++i)
		cpus[i] = i;
	cpus[SGE_QSETS] = 0xff;	/* terminator */

	for (i = 0; i < RSS_TABLE_SIZE / 2; ++i) {
		rspq_map[i] = i % nq0;
		rspq_map[i + RSS_TABLE_SIZE / 2] = (i % nq1) + nq0;
	}
	rspq_map[RSS_TABLE_SIZE] = 0xffff; /* terminator */

	t3_config_rss(adap, F_RQFEEDBACKENABLE | F_TNLLKPEN | F_TNLMAPEN |
		      F_TNLPRTEN | F_TNL2TUPEN | F_TNL4TUPEN |
		      V_RRCPLCPUSIZE(6) | F_HASHTOEPLITZ, cpus, rspq_map);
}

static void ring_dbs(struct adapter *adap)
{
	int i, j;

	for (i = 0; i < SGE_QSETS; i++) {
		struct sge_qset *qs = &adap->sge.qs[i];

		if (qs->adap)
			for (j = 0; j < SGE_TXQ_PER_SET; j++)
				t3_write_reg(adap, A_SG_KDOORBELL, F_SELEGRCNTX | V_EGRCNTX(qs->txq[j].cntxt_id));
	}
}

static void init_napi(struct adapter *adap)
{
	int i;

	for (i = 0; i < SGE_QSETS; i++) {
		struct sge_qset *qs = &adap->sge.qs[i];

		if (qs->adap)
			netif_napi_add(qs->netdev, &qs->napi, qs->napi.poll);
	}

	/*
	 * netif_napi_add() can be called only once per napi_struct because it
	 * adds each new napi_struct to a list.  Be careful not to call it a
	 * second time, e.g., during EEH recovery, by making a note of it.
	 */
	adap->flags |= NAPI_INIT;
}

/*
 * Wait until all NAPI handlers are descheduled.  This includes the handlers of
 * both netdevices representing interfaces and the dummy ones for the extra
 * queues.
 */
static void quiesce_rx(struct adapter *adap)
{
	int i;

	for (i = 0; i < SGE_QSETS; i++)
		if (adap->sge.qs[i].adap)
			napi_disable(&adap->sge.qs[i].napi);
}

static void enable_all_napi(struct adapter *adap)
{
	int i;
	for (i = 0; i < SGE_QSETS; i++)
		if (adap->sge.qs[i].adap)
			napi_enable(&adap->sge.qs[i].napi);
}

/**
 *	setup_sge_qsets - configure SGE Tx/Rx/response queues
 *	@adap: the adapter
 *
 *	Determines how many sets of SGE queues to use and initializes them.
 *	We support multiple queue sets per port if we have MSI-X, otherwise
 *	just one queue set per port.
 */
static int setup_sge_qsets(struct adapter *adap)
{
	int i, j, err, irq_idx = 0, qset_idx = 0;
	unsigned int ntxq = SGE_TXQ_PER_SET;

	if (adap->params.rev > 0 && !(adap->flags & USING_MSI))
		irq_idx = -1;

	for_each_port(adap, i) {
		struct net_device *dev = adap->port[i];
		struct port_info *pi = netdev_priv(dev);

		pi->qs = &adap->sge.qs[pi->first_qset];
		for (j = 0; j < pi->nqsets; ++j, ++qset_idx) {
			err = t3_sge_alloc_qset(adap, qset_idx, 1,
				(adap->flags & USING_MSIX) ? qset_idx + 1 :
							     irq_idx,
				&adap->params.sge.qset[qset_idx], ntxq, dev,
				netdev_get_tx_queue(dev, j));
			if (err) {
				t3_free_sge_resources(adap);
				return err;
			}
		}
	}

	return 0;
}

static ssize_t attr_show(struct device *d, char *buf,
			 ssize_t(*format) (struct net_device *, char *))
{
	ssize_t len;

	/* Synchronize with ioctls that may shut down the device */
	rtnl_lock();
	len = (*format) (to_net_dev(d), buf);
	rtnl_unlock();
	return len;
}

static ssize_t attr_store(struct device *d,
			  const char *buf, size_t len,
			  ssize_t(*set) (struct net_device *, unsigned int),
			  unsigned int min_val, unsigned int max_val)
{
	ssize_t ret;
	unsigned int val;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;
	if (val < min_val || val > max_val)
		return -EINVAL;

	rtnl_lock();
	ret = (*set) (to_net_dev(d), val);
	if (!ret)
		ret = len;
	rtnl_unlock();
	return ret;
}

#define CXGB3_SHOW(name, val_expr) \
static ssize_t format_##name(struct net_device *dev, char *buf) \
{ \
	struct port_info *pi = netdev_priv(dev); \
	struct adapter *adap = pi->adapter; \
	return sprintf(buf, "%u\n", val_expr); \
} \
static ssize_t show_##name(struct device *d, struct device_attribute *attr, \
			   char *buf) \
{ \
	return attr_show(d, buf, format_##name); \
}

static ssize_t set_nfilters(struct net_device *dev, unsigned int val)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;
	int min_tids = is_offload(adap) ? MC5_MIN_TIDS : 0;

	if (adap->flags & FULL_INIT_DONE)
		return -EBUSY;
	if (val && adap->params.rev == 0)
		return -EINVAL;
	if (val > t3_mc5_size(&adap->mc5) - adap->params.mc5.nservers -
	    min_tids)
		return -EINVAL;
	adap->params.mc5.nfilters = val;
	return 0;
}

static ssize_t store_nfilters(struct device *d, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	return attr_store(d, buf, len, set_nfilters, 0, ~0);
}

static ssize_t set_nservers(struct net_device *dev, unsigned int val)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adap = pi->adapter;

	if (adap->flags & FULL_INIT_DONE)
		return -EBUSY;
	if (val > t3_mc5_size(&adap->mc5) - adap->params.mc5.nfilters -
	    MC5_MIN_TIDS)
		return -EINVAL;
	adap->params.mc5.nservers = val;
	return 0;
}

static ssize_t store_nservers(struct device *d, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	return attr_store(d, buf, len, set_nservers, 0, ~0);
}

#define CXGB3_ATTR_R(name, val_expr) \
CXGB3_SHOW(name, val_expr) \
static DEVICE_ATTR(name, 0444, show_##name, NULL)

#define CXGB3_ATTR_RW(name, val_expr, store_method) \
CXGB3_SHOW(name, val_expr) \
static DEVICE_ATTR(name, 0644, show_##name, store_method)

CXGB3_ATTR_R(cam_size, t3_mc5_size(&adap->mc5));
CXGB3_ATTR_RW(nfilters, adap->params.mc5.nfilters, store_nfilters);
CXGB3_ATTR_RW(nservers, adap->params.mc5.nservers, store_nservers);

static struct attribute *cxgb3_attrs[] = {
	&dev_attr_cam_size.attr,
	&dev_attr_nfilters.attr,
	&dev_attr_nservers.attr,
	NULL
};

static const struct attribute_group cxgb3_attr_group = {
	.attrs = cxgb3_attrs,
};

static ssize_t tm_attr_show(struct device *d,
			    char *buf, int sched)
{
	struct port_info *pi = netdev_priv(to_net_dev(d));
	struct adapter *adap = pi->adapter;
	unsigned int v, addr, bpt, cpt;
	ssize_t len;

	addr = A_TP_TX_MOD_Q1_Q0_RATE_LIMIT - sched / 2;
	rtnl_lock();
	t3_write_reg(adap, A_TP_TM_PIO_ADDR, addr);
	v = t3_read_reg(adap, A_TP_TM_PIO_DATA);
	if (sched & 1)
		v >>= 16;
	bpt = (v >> 8) & 0xff;
	cpt = v & 0xff;
	if (!cpt)
		len = sprintf(buf, "disabled\n");
	else {
		v = (adap->params.vpd.cclk * 1000) / cpt;
		len = sprintf(buf, "%u Kbps\n", (v * bpt) / 125);
	}
	rtnl_unlock();
	return len;
}

static ssize_t tm_attr_store(struct device *d,
			     const char *buf, size_t len, int sched)
{
	struct port_info *pi = netdev_priv(to_net_dev(d));
	struct adapter *adap = pi->adapter;
	unsigned int val;
	ssize_t ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;
	if (val > 10000000)
		return -EINVAL;

	rtnl_lock();
	ret = t3_config_sched(adap, val, sched);
	if (!ret)
		ret = len;
	rtnl_unlock();
	return ret;
}

#define TM_ATTR(name, sched) \
static ssize_t show_##name(struct device *d, struct device_attribute *attr, \
			   char *buf) \
{ \
	return tm_attr_show(d, buf, sched); \
} \
static ssize_t store_##name(struct device *d, struct device_attribute *attr, \
			    const char *buf, size_t len) \
{ \
	return tm_attr_store(d, buf, len, sched); \
} \
static DEVICE_ATTR(name, 0644, show_##name, store_##name)

TM_ATTR(sched0, 0);
TM_ATTR(sched1, 1);
TM_ATTR(sched2, 2);
TM_ATTR(sched3, 3);
TM_ATTR(sched4, 4);
TM_ATTR(sched5, 5);
TM_ATTR(sched6, 6);
TM_ATTR(sched7, 7);

static struct attribute *offload_attrs[] = {
	&dev_attr_sched0.attr,
	&dev_attr_sched1.attr,
	&dev_attr_sched2.attr,
	&dev_attr_sched3.attr,
	&dev_attr_sched4.attr,
	&dev_attr_sched5.attr,
	&dev_attr_sched6.attr,
	&dev_attr_sched7.attr,
	NULL
};

static const struct attribute_group offload_attr_group = {
	.attrs = offload_attrs,
};

/*
 * Sends an sk_buff to an offload queue driver
 * after dealing with any active network taps.
 */
static inline int offload_tx(struct t3cdev *tdev, struct sk_buff *skb)
{
	int ret;

	local_bh_disable();
	ret = t3_offload_tx(tdev, skb);
	local_bh_enable();
	return ret;
}

static int write_smt_entry(struct adapter *adapter, int idx)
{
	struct cpl_smt_write_req *req;
	struct port_info *pi = netdev_priv(adapter->port[idx]);
	struct sk_buff *skb = alloc_skb(sizeof(*req), GFP_KERNEL);

	if (!skb)
		return -ENOMEM;

	req = __skb_put(skb, sizeof(*req));
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SMT_WRITE_REQ, idx));
	req->mtu_idx = NMTUS - 1;	/* should be 0 but there's a T3 bug */
	req->iff = idx;
	memcpy(req->src_mac0, adapter->port[idx]->dev_addr, ETH_ALEN);
	memcpy(req->src_mac1, pi->iscsic.mac_addr, ETH_ALEN);
	skb->priority = 1;
	offload_tx(&adapter->tdev, skb);
	return 0;
}

static int init_smt(struct adapter *adapter)
{
	int i;

	for_each_port(adapter, i)
	    write_smt_entry(adapter, i);
	return 0;
}

static void init_port_mtus(struct adapter *adapter)
{
	unsigned int mtus = adapter->port[0]->mtu;

	if (adapter->port[1])
		mtus |= adapter->port[1]->mtu << 16;
	t3_write_reg(adapter, A_TP_MTU_PORT_TABLE, mtus);
}

static int send_pktsched_cmd(struct adapter *adap, int sched, int qidx, int lo,
			      int hi, int port)
{
	struct sk_buff *skb;
	struct mngt_pktsched_wr *req;
	int ret;

	skb = alloc_skb(sizeof(*req), GFP_KERNEL);
	if (!skb)
		skb = adap->nofail_skb;
	if (!skb)
		return -ENOMEM;

	req = skb_put(skb, sizeof(*req));
	req->wr_hi = htonl(V_WR_OP(FW_WROPCODE_MNGT));
	req->mngt_opcode = FW_MNGTOPCODE_PKTSCHED_SET;
	req->sched = sched;
	req->idx = qidx;
	req->min = lo;
	req->max = hi;
	req->binding = port;
	ret = t3_mgmt_tx(adap, skb);
	if (skb == adap->nofail_skb) {
		adap->nofail_skb = alloc_skb(sizeof(struct cpl_set_tcb_field),
					     GFP_KERNEL);
		if (!adap->nofail_skb)
			ret = -ENOMEM;
	}

	return ret;
}

static int bind_qsets(struct adapter *adap)
{
	int i, j, err = 0;

	for_each_port(adap, i) {
		const struct port_info *pi = adap2pinfo(adap, i);

		for (j = 0; j < pi->nqsets; ++j) {
			int ret = send_pktsched_cmd(adap, 1,
						    pi->first_qset + j, -1,
						    -1, i);
			if (ret)
				err = ret;
		}
	}

	return err;
}

#define FW_VERSION __stringify(FW_VERSION_MAJOR) "."			\
	__stringify(FW_VERSION_MINOR) "." __stringify(FW_VERSION_MICRO)
#define FW_FNAME "cxgb3/t3fw-" FW_VERSION ".bin"
#define TPSRAM_VERSION __stringify(TP_VERSION_MAJOR) "."		\
	__stringify(TP_VERSION_MINOR) "." __stringify(TP_VERSION_MICRO)
#define TPSRAM_NAME "cxgb3/t3%c_psram-" TPSRAM_VERSION ".bin"
#define AEL2005_OPT_EDC_NAME "cxgb3/ael2005_opt_edc.bin"
#define AEL2005_TWX_EDC_NAME "cxgb3/ael2005_twx_edc.bin"
#define AEL2020_TWX_EDC_NAME "cxgb3/ael2020_twx_edc.bin"
MODULE_FIRMWARE(FW_FNAME);
MODULE_FIRMWARE("cxgb3/t3b_psram-" TPSRAM_VERSION ".bin");
MODULE_FIRMWARE("cxgb3/t3c_psram-" TPSRAM_VERSION ".bin");
MODULE_FIRMWARE(AEL2005_OPT_EDC_NAME);
MODULE_FIRMWARE(AEL2005_TWX_EDC_NAME);
MODULE_FIRMWARE(AEL2020_TWX_EDC_NAME);

static inline const char *get_edc_fw_name(int edc_idx)
{
	const char *fw_name = NULL;

	switch (edc_idx) {
	case EDC_OPT_AEL2005:
		fw_name = AEL2005_OPT_EDC_NAME;
		break;
	case EDC_TWX_AEL2005:
		fw_name = AEL2005_TWX_EDC_NAME;
		break;
	case EDC_TWX_AEL2020:
		fw_name = AEL2020_TWX_EDC_NAME;
		break;
	}
	return fw_name;
}

int t3_get_edc_fw(struct cphy *phy, int edc_idx, int size)
{
	struct adapter *adapter = phy->adapter;
	const struct firmware *fw;
	const char *fw_name;
	u32 csum;
	const __be32 *p;
	u16 *cache = phy->phy_cache;
	int i, ret = -EINVAL;

	fw_name = get_edc_fw_name(edc_idx);
	if (fw_name)
		ret = request_firmware(&fw, fw_name, &adapter->pdev->dev);
	if (ret < 0) {
		dev_err(&adapter->pdev->dev,
			"could not upgrade firmware: unable to load %s\n",
			fw_name);
		return ret;
	}

	/* check size, take checksum in account */
	if (fw->size > size + 4) {
		CH_ERR(adapter, "firmware image too large %u, expected %d\n",
		       (unsigned int)fw->size, size + 4);
		ret = -EINVAL;
	}

	/* compute checksum */
	p = (const __be32 *)fw->data;
	for (csum = 0, i = 0; i < fw->size / sizeof(csum); i++)
		csum += ntohl(p[i]);

	if (csum != 0xffffffff) {
		CH_ERR(adapter, "corrupted firmware image, checksum %u\n",
		       csum);
		ret = -EINVAL;
	}

	for (i = 0; i < size / 4 ; i++) {
		*cache++ = (be32_to_cpu(p[i]) & 0xffff0000) >> 16;
		*cache++ = be32_to_cpu(p[i]) & 0xffff;
	}

	release_firmware(fw);

	return ret;
}

static int upgrade_fw(struct adapter *adap)
{
	int ret;
	const struct firmware *fw;
	struct device *dev = &adap->pdev->dev;

	ret = request_firmware(&fw, FW_FNAME, dev);
	if (ret < 0) {
		dev_err(dev, "could not upgrade firmware: unable to load %s\n",
			FW_FNAME);
		return ret;
	}
	ret = t3_load_fw(adap, fw->data, fw->size);
	release_firmware(fw);

	if (ret == 0)
		dev_info(dev, "successful upgrade to firmware %d.%d.%d\n",
			 FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_MICRO);
	else
		dev_err(dev, "failed to upgrade to firmware %d.%d.%d\n",
			FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_MICRO);

	return ret;
}

static inline char t3rev2char(struct adapter *adapter)
{
	char rev = 0;

	switch(adapter->params.rev) {
	case T3_REV_B:
	case T3_REV_B2:
		rev = 'b';
		break;
	case T3_REV_C:
		rev = 'c';
		break;
	}
	return rev;
}

static int update_tpsram(struct adapter *adap)
{
	const struct firmware *tpsram;
	char buf[64];
	struct device *dev = &adap->pdev->dev;
	int ret;
	char rev;

	rev = t3rev2char(adap);
	if (!rev)
		return 0;

	snprintf(buf, sizeof(buf), TPSRAM_NAME, rev);

	ret = request_firmware(&tpsram, buf, dev);
	if (ret < 0) {
		dev_err(dev, "could not load TP SRAM: unable to load %s\n",
			buf);
		return ret;
	}

	ret = t3_check_tpsram(adap, tpsram->data, tpsram->size);
	if (ret)
		goto release_tpsram;

	ret = t3_set_proto_sram(adap, tpsram->data);
	if (ret == 0)
		dev_info(dev,
			 "successful update of protocol engine "
			 "to %d.%d.%d\n",
			 TP_VERSION_MAJOR, TP_VERSION_MINOR, TP_VERSION_MICRO);
	else
		dev_err(dev, "failed to update of protocol engine %d.%d.%d\n",
			TP_VERSION_MAJOR, TP_VERSION_MINOR, TP_VERSION_MICRO);
	if (ret)
		dev_err(dev, "loading protocol SRAM failed\n");

release_tpsram:
	release_firmware(tpsram);

	return ret;
}

/**
 * t3_synchronize_rx - wait for current Rx processing on a port to complete
 * @adap: the adapter
 * @p: the port
 *
 * Ensures that current Rx processing on any of the queues associated with
 * the given port completes before returning.  We do this by acquiring and
 * releasing the locks of the response queues associated with the port.
 */
static void t3_synchronize_rx(struct adapter *adap, const struct port_info *p)
{
	int i;

	for (i = p->first_qset; i < p->first_qset + p->nqsets; i++) {
		struct sge_rspq *q = &adap->sge.qs[i].rspq;

		spin_lock_irq(&q->lock);
		spin_unlock_irq(&q->lock);
	}
}

static void cxgb_vlan_mode(struct net_device *dev, netdev_features_t features)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	if (adapter->params.rev > 0) {
		t3_set_vlan_accel(adapter, 1 << pi->port_id,
				  features & NETIF_F_HW_VLAN_CTAG_RX);
	} else {
		/* single control for all ports */
		unsigned int i, have_vlans = features & NETIF_F_HW_VLAN_CTAG_RX;

		for_each_port(adapter, i)
			have_vlans |=
				adapter->port[i]->features &
				NETIF_F_HW_VLAN_CTAG_RX;

		t3_set_vlan_accel(adapter, 1, have_vlans);
	}
	t3_synchronize_rx(adapter, pi);
}

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
	int i, err;

	if (!(adap->flags & FULL_INIT_DONE)) {
		err = t3_check_fw_version(adap);
		if (err == -EINVAL) {
			err = upgrade_fw(adap);
			CH_WARN(adap, "FW upgrade to %d.%d.%d %s\n",
				FW_VERSION_MAJOR, FW_VERSION_MINOR,
				FW_VERSION_MICRO, err ? "failed" : "succeeded");
		}

		err = t3_check_tpsram_version(adap);
		if (err == -EINVAL) {
			err = update_tpsram(adap);
			CH_WARN(adap, "TP upgrade to %d.%d.%d %s\n",
				TP_VERSION_MAJOR, TP_VERSION_MINOR,
				TP_VERSION_MICRO, err ? "failed" : "succeeded");
		}

		/*
		 * Clear interrupts now to catch errors if t3_init_hw fails.
		 * We clear them again later as initialization may trigger
		 * conditions that can interrupt.
		 */
		t3_intr_clear(adap);

		err = t3_init_hw(adap, 0);
		if (err)
			goto out;

		t3_set_reg_field(adap, A_TP_PARA_REG5, 0, F_RXDDPOFFINIT);
		t3_write_reg(adap, A_ULPRX_TDDP_PSZ, V_HPZ0(PAGE_SHIFT - 12));

		err = setup_sge_qsets(adap);
		if (err)
			goto out;

		for_each_port(adap, i)
			cxgb_vlan_mode(adap->port[i], adap->port[i]->features);

		setup_rss(adap);
		if (!(adap->flags & NAPI_INIT))
			init_napi(adap);

		t3_start_sge_timers(adap);
		adap->flags |= FULL_INIT_DONE;
	}

	t3_intr_clear(adap);

	if (adap->flags & USING_MSIX) {
		name_msix_vecs(adap);
		err = request_irq(adap->msix_info[0].vec,
				  t3_async_intr_handler, 0,
				  adap->msix_info[0].desc, adap);
		if (err)
			goto irq_err;

		err = request_msix_data_irqs(adap);
		if (err) {
			free_irq(adap->msix_info[0].vec, adap);
			goto irq_err;
		}
	} else {
		err = request_irq(adap->pdev->irq,
				  t3_intr_handler(adap, adap->sge.qs[0].rspq.polling),
				  (adap->flags & USING_MSI) ? 0 : IRQF_SHARED,
				  adap->name, adap);
		if (err)
			goto irq_err;
	}

	enable_all_napi(adap);
	t3_sge_start(adap);
	t3_intr_enable(adap);

	if (adap->params.rev >= T3_REV_C && !(adap->flags & TP_PARITY_INIT) &&
	    is_offload(adap) && init_tp_parity(adap) == 0)
		adap->flags |= TP_PARITY_INIT;

	if (adap->flags & TP_PARITY_INIT) {
		t3_write_reg(adap, A_TP_INT_CAUSE,
			     F_CMCACHEPERR | F_ARPLUTPERR);
		t3_write_reg(adap, A_TP_INT_ENABLE, 0x7fbfffff);
	}

	if (!(adap->flags & QUEUES_BOUND)) {
		int ret = bind_qsets(adap);

		if (ret < 0) {
			CH_ERR(adap, "failed to bind qsets, err %d\n", ret);
			t3_intr_disable(adap);
			quiesce_rx(adap);
			free_irq_resources(adap);
			err = ret;
			goto out;
		}
		adap->flags |= QUEUES_BOUND;
	}

out:
	return err;
irq_err:
	CH_ERR(adap, "request_irq failed, err %d\n", err);
	goto out;
}

/*
 * Release resources when all the ports and offloading have been stopped.
 */
static void cxgb_down(struct adapter *adapter, int on_wq)
{
	t3_sge_stop(adapter);
	spin_lock_irq(&adapter->work_lock);	/* sync with PHY intr task */
	t3_intr_disable(adapter);
	spin_unlock_irq(&adapter->work_lock);

	free_irq_resources(adapter);
	quiesce_rx(adapter);
	t3_sge_stop(adapter);
	if (!on_wq)
		flush_workqueue(cxgb3_wq);/* wait for external IRQ handler */
}

static void schedule_chk_task(struct adapter *adap)
{
	unsigned int timeo;

	timeo = adap->params.linkpoll_period ?
	    (HZ * adap->params.linkpoll_period) / 10 :
	    adap->params.stats_update_period * HZ;
	if (timeo)
		queue_delayed_work(cxgb3_wq, &adap->adap_check_task, timeo);
}

static int offload_open(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct t3cdev *tdev = dev2t3cdev(dev);
	int adap_up = adapter->open_device_map & PORT_MASK;
	int err;

	if (test_and_set_bit(OFFLOAD_DEVMAP_BIT, &adapter->open_device_map))
		return 0;

	if (!adap_up && (err = cxgb_up(adapter)) < 0)
		goto out;

	t3_tp_set_offload_mode(adapter, 1);
	tdev->lldev = adapter->port[0];
	err = cxgb3_offload_activate(adapter);
	if (err)
		goto out;

	init_port_mtus(adapter);
	t3_load_mtus(adapter, adapter->params.mtus, adapter->params.a_wnd,
		     adapter->params.b_wnd,
		     adapter->params.rev == 0 ?
		     adapter->port[0]->mtu : 0xffff);
	init_smt(adapter);

	if (sysfs_create_group(&tdev->lldev->dev.kobj, &offload_attr_group))
		dev_dbg(&dev->dev, "cannot create sysfs group\n");

	/* Call back all registered clients */
	cxgb3_add_clients(tdev);

out:
	/* restore them in case the offload module has changed them */
	if (err) {
		t3_tp_set_offload_mode(adapter, 0);
		clear_bit(OFFLOAD_DEVMAP_BIT, &adapter->open_device_map);
		cxgb3_set_dummy_ops(tdev);
	}
	return err;
}

static int offload_close(struct t3cdev *tdev)
{
	struct adapter *adapter = tdev2adap(tdev);
	struct t3c_data *td = T3C_DATA(tdev);

	if (!test_bit(OFFLOAD_DEVMAP_BIT, &adapter->open_device_map))
		return 0;

	/* Call back all registered clients */
	cxgb3_remove_clients(tdev);

	sysfs_remove_group(&tdev->lldev->dev.kobj, &offload_attr_group);

	/* Flush work scheduled while releasing TIDs */
	flush_work(&td->tid_release_task);

	tdev->lldev = NULL;
	cxgb3_set_dummy_ops(tdev);
	t3_tp_set_offload_mode(adapter, 0);
	clear_bit(OFFLOAD_DEVMAP_BIT, &adapter->open_device_map);

	if (!adapter->open_device_map)
		cxgb_down(adapter, 0);

	cxgb3_offload_deactivate(adapter);
	return 0;
}

static int cxgb_open(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	int other_ports = adapter->open_device_map & PORT_MASK;
	int err;

	if (!adapter->open_device_map && (err = cxgb_up(adapter)) < 0)
		return err;

	set_bit(pi->port_id, &adapter->open_device_map);
	if (is_offload(adapter) && !ofld_disable) {
		err = offload_open(dev);
		if (err)
			pr_warn("Could not initialize offload capabilities\n");
	}

	netif_set_real_num_tx_queues(dev, pi->nqsets);
	err = netif_set_real_num_rx_queues(dev, pi->nqsets);
	if (err)
		return err;
	link_start(dev);
	t3_port_intr_enable(adapter, pi->port_id);
	netif_tx_start_all_queues(dev);
	if (!other_ports)
		schedule_chk_task(adapter);

	cxgb3_event_notify(&adapter->tdev, OFFLOAD_PORT_UP, pi->port_id);
	return 0;
}

static int __cxgb_close(struct net_device *dev, int on_wq)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	
	if (!adapter->open_device_map)
		return 0;

	/* Stop link fault interrupts */
	t3_xgm_intr_disable(adapter, pi->port_id);
	t3_read_reg(adapter, A_XGM_INT_STATUS + pi->mac.offset);

	t3_port_intr_disable(adapter, pi->port_id);
	netif_tx_stop_all_queues(dev);
	pi->phy.ops->power_down(&pi->phy, 1);
	netif_carrier_off(dev);
	t3_mac_disable(&pi->mac, MAC_DIRECTION_TX | MAC_DIRECTION_RX);

	spin_lock_irq(&adapter->work_lock);	/* sync with update task */
	clear_bit(pi->port_id, &adapter->open_device_map);
	spin_unlock_irq(&adapter->work_lock);

	if (!(adapter->open_device_map & PORT_MASK))
		cancel_delayed_work_sync(&adapter->adap_check_task);

	if (!adapter->open_device_map)
		cxgb_down(adapter, on_wq);

	cxgb3_event_notify(&adapter->tdev, OFFLOAD_PORT_DOWN, pi->port_id);
	return 0;
}

static int cxgb_close(struct net_device *dev)
{
	return __cxgb_close(dev, 0);
}

static struct net_device_stats *cxgb_get_stats(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct net_device_stats *ns = &dev->stats;
	const struct mac_stats *pstats;

	spin_lock(&adapter->stats_lock);
	pstats = t3_mac_update_stats(&pi->mac);
	spin_unlock(&adapter->stats_lock);

	ns->tx_bytes = pstats->tx_octets;
	ns->tx_packets = pstats->tx_frames;
	ns->rx_bytes = pstats->rx_octets;
	ns->rx_packets = pstats->rx_frames;
	ns->multicast = pstats->rx_mcast_frames;

	ns->tx_errors = pstats->tx_underrun;
	ns->rx_errors = pstats->rx_symbol_errs + pstats->rx_fcs_errs +
	    pstats->rx_too_long + pstats->rx_jabber + pstats->rx_short +
	    pstats->rx_fifo_ovfl;

	/* detailed rx_errors */
	ns->rx_length_errors = pstats->rx_jabber + pstats->rx_too_long;
	ns->rx_over_errors = 0;
	ns->rx_crc_errors = pstats->rx_fcs_errs;
	ns->rx_frame_errors = pstats->rx_symbol_errs;
	ns->rx_fifo_errors = pstats->rx_fifo_ovfl;
	ns->rx_missed_errors = pstats->rx_cong_drops;

	/* detailed tx_errors */
	ns->tx_aborted_errors = 0;
	ns->tx_carrier_errors = 0;
	ns->tx_fifo_errors = pstats->tx_underrun;
	ns->tx_heartbeat_errors = 0;
	ns->tx_window_errors = 0;
	return ns;
}

static u32 get_msglevel(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	return adapter->msg_enable;
}

static void set_msglevel(struct net_device *dev, u32 val)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	adapter->msg_enable = val;
}

static const char stats_strings[][ETH_GSTRING_LEN] = {
	"TxOctetsOK         ",
	"TxFramesOK         ",
	"TxMulticastFramesOK",
	"TxBroadcastFramesOK",
	"TxPauseFrames      ",
	"TxUnderrun         ",
	"TxExtUnderrun      ",

	"TxFrames64         ",
	"TxFrames65To127    ",
	"TxFrames128To255   ",
	"TxFrames256To511   ",
	"TxFrames512To1023  ",
	"TxFrames1024To1518 ",
	"TxFrames1519ToMax  ",

	"RxOctetsOK         ",
	"RxFramesOK         ",
	"RxMulticastFramesOK",
	"RxBroadcastFramesOK",
	"RxPauseFrames      ",
	"RxFCSErrors        ",
	"RxSymbolErrors     ",
	"RxShortErrors      ",
	"RxJabberErrors     ",
	"RxLengthErrors     ",
	"RxFIFOoverflow     ",

	"RxFrames64         ",
	"RxFrames65To127    ",
	"RxFrames128To255   ",
	"RxFrames256To511   ",
	"RxFrames512To1023  ",
	"RxFrames1024To1518 ",
	"RxFrames1519ToMax  ",

	"PhyFIFOErrors      ",
	"TSO                ",
	"VLANextractions    ",
	"VLANinsertions     ",
	"TxCsumOffload      ",
	"RxCsumGood         ",
	"LroAggregated      ",
	"LroFlushed         ",
	"LroNoDesc          ",
	"RxDrops            ",

	"CheckTXEnToggled   ",
	"CheckResets        ",

	"LinkFaults         ",
};

static int get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(stats_strings);
	default:
		return -EOPNOTSUPP;
	}
}

#define T3_REGMAP_SIZE (3 * 1024)

static int get_regs_len(struct net_device *dev)
{
	return T3_REGMAP_SIZE;
}

static int get_eeprom_len(struct net_device *dev)
{
	return EEPROMSIZE;
}

static void get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	u32 fw_vers = 0;
	u32 tp_vers = 0;

	spin_lock(&adapter->stats_lock);
	t3_get_fw_version(adapter, &fw_vers);
	t3_get_tp_version(adapter, &tp_vers);
	spin_unlock(&adapter->stats_lock);

	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, pci_name(adapter->pdev),
		sizeof(info->bus_info));
	if (fw_vers)
		snprintf(info->fw_version, sizeof(info->fw_version),
			 "%s %u.%u.%u TP %u.%u.%u",
			 G_FW_VERSION_TYPE(fw_vers) ? "T" : "N",
			 G_FW_VERSION_MAJOR(fw_vers),
			 G_FW_VERSION_MINOR(fw_vers),
			 G_FW_VERSION_MICRO(fw_vers),
			 G_TP_VERSION_MAJOR(tp_vers),
			 G_TP_VERSION_MINOR(tp_vers),
			 G_TP_VERSION_MICRO(tp_vers));
}

static void get_strings(struct net_device *dev, u32 stringset, u8 * data)
{
	if (stringset == ETH_SS_STATS)
		memcpy(data, stats_strings, sizeof(stats_strings));
}

static unsigned long collect_sge_port_stats(struct adapter *adapter,
					    struct port_info *p, int idx)
{
	int i;
	unsigned long tot = 0;

	for (i = p->first_qset; i < p->first_qset + p->nqsets; ++i)
		tot += adapter->sge.qs[i].port_stats[idx];
	return tot;
}

static void get_stats(struct net_device *dev, struct ethtool_stats *stats,
		      u64 *data)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	const struct mac_stats *s;

	spin_lock(&adapter->stats_lock);
	s = t3_mac_update_stats(&pi->mac);
	spin_unlock(&adapter->stats_lock);

	*data++ = s->tx_octets;
	*data++ = s->tx_frames;
	*data++ = s->tx_mcast_frames;
	*data++ = s->tx_bcast_frames;
	*data++ = s->tx_pause;
	*data++ = s->tx_underrun;
	*data++ = s->tx_fifo_urun;

	*data++ = s->tx_frames_64;
	*data++ = s->tx_frames_65_127;
	*data++ = s->tx_frames_128_255;
	*data++ = s->tx_frames_256_511;
	*data++ = s->tx_frames_512_1023;
	*data++ = s->tx_frames_1024_1518;
	*data++ = s->tx_frames_1519_max;

	*data++ = s->rx_octets;
	*data++ = s->rx_frames;
	*data++ = s->rx_mcast_frames;
	*data++ = s->rx_bcast_frames;
	*data++ = s->rx_pause;
	*data++ = s->rx_fcs_errs;
	*data++ = s->rx_symbol_errs;
	*data++ = s->rx_short;
	*data++ = s->rx_jabber;
	*data++ = s->rx_too_long;
	*data++ = s->rx_fifo_ovfl;

	*data++ = s->rx_frames_64;
	*data++ = s->rx_frames_65_127;
	*data++ = s->rx_frames_128_255;
	*data++ = s->rx_frames_256_511;
	*data++ = s->rx_frames_512_1023;
	*data++ = s->rx_frames_1024_1518;
	*data++ = s->rx_frames_1519_max;

	*data++ = pi->phy.fifo_errors;

	*data++ = collect_sge_port_stats(adapter, pi, SGE_PSTAT_TSO);
	*data++ = collect_sge_port_stats(adapter, pi, SGE_PSTAT_VLANEX);
	*data++ = collect_sge_port_stats(adapter, pi, SGE_PSTAT_VLANINS);
	*data++ = collect_sge_port_stats(adapter, pi, SGE_PSTAT_TX_CSUM);
	*data++ = collect_sge_port_stats(adapter, pi, SGE_PSTAT_RX_CSUM_GOOD);
	*data++ = 0;
	*data++ = 0;
	*data++ = 0;
	*data++ = s->rx_cong_drops;

	*data++ = s->num_toggled;
	*data++ = s->num_resets;

	*data++ = s->link_faults;
}

static inline void reg_block_dump(struct adapter *ap, void *buf,
				  unsigned int start, unsigned int end)
{
	u32 *p = buf + start;

	for (; start <= end; start += sizeof(u32))
		*p++ = t3_read_reg(ap, start);
}

static void get_regs(struct net_device *dev, struct ethtool_regs *regs,
		     void *buf)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *ap = pi->adapter;

	/*
	 * Version scheme:
	 * bits 0..9: chip version
	 * bits 10..15: chip revision
	 * bit 31: set for PCIe cards
	 */
	regs->version = 3 | (ap->params.rev << 10) | (is_pcie(ap) << 31);

	/*
	 * We skip the MAC statistics registers because they are clear-on-read.
	 * Also reading multi-register stats would need to synchronize with the
	 * periodic mac stats accumulation.  Hard to justify the complexity.
	 */
	memset(buf, 0, T3_REGMAP_SIZE);
	reg_block_dump(ap, buf, 0, A_SG_RSPQ_CREDIT_RETURN);
	reg_block_dump(ap, buf, A_SG_HI_DRB_HI_THRSH, A_ULPRX_PBL_ULIMIT);
	reg_block_dump(ap, buf, A_ULPTX_CONFIG, A_MPS_INT_CAUSE);
	reg_block_dump(ap, buf, A_CPL_SWITCH_CNTRL, A_CPL_MAP_TBL_DATA);
	reg_block_dump(ap, buf, A_SMB_GLOBAL_TIME_CFG, A_XGM_SERDES_STAT3);
	reg_block_dump(ap, buf, A_XGM_SERDES_STATUS0,
		       XGM_REG(A_XGM_SERDES_STAT3, 1));
	reg_block_dump(ap, buf, XGM_REG(A_XGM_SERDES_STATUS0, 1),
		       XGM_REG(A_XGM_RX_SPI4_SOP_EOP_CNT, 1));
}

static int restart_autoneg(struct net_device *dev)
{
	struct port_info *p = netdev_priv(dev);

	if (!netif_running(dev))
		return -EAGAIN;
	if (p->link_config.autoneg != AUTONEG_ENABLE)
		return -EINVAL;
	p->phy.ops->autoneg_restart(&p->phy);
	return 0;
}

static int set_phys_id(struct net_device *dev,
		       enum ethtool_phys_id_state state)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		return 1;	/* cycle on/off once per second */

	case ETHTOOL_ID_OFF:
		t3_set_reg_field(adapter, A_T3DBG_GPIO_EN, F_GPIO0_OUT_VAL, 0);
		break;

	case ETHTOOL_ID_ON:
	case ETHTOOL_ID_INACTIVE:
		t3_set_reg_field(adapter, A_T3DBG_GPIO_EN, F_GPIO0_OUT_VAL,
			 F_GPIO0_OUT_VAL);
	}

	return 0;
}

static int get_link_ksettings(struct net_device *dev,
			      struct ethtool_link_ksettings *cmd)
{
	struct port_info *p = netdev_priv(dev);
	u32 supported;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						p->link_config.supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						p->link_config.advertising);

	if (netif_carrier_ok(dev)) {
		cmd->base.speed = p->link_config.speed;
		cmd->base.duplex = p->link_config.duplex;
	} else {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}

	ethtool_convert_link_mode_to_legacy_u32(&supported,
						cmd->link_modes.supported);

	cmd->base.port = (supported & SUPPORTED_TP) ? PORT_TP : PORT_FIBRE;
	cmd->base.phy_address = p->phy.mdio.prtad;
	cmd->base.autoneg = p->link_config.autoneg;
	return 0;
}

static int speed_duplex_to_caps(int speed, int duplex)
{
	int cap = 0;

	switch (speed) {
	case SPEED_10:
		if (duplex == DUPLEX_FULL)
			cap = SUPPORTED_10baseT_Full;
		else
			cap = SUPPORTED_10baseT_Half;
		break;
	case SPEED_100:
		if (duplex == DUPLEX_FULL)
			cap = SUPPORTED_100baseT_Full;
		else
			cap = SUPPORTED_100baseT_Half;
		break;
	case SPEED_1000:
		if (duplex == DUPLEX_FULL)
			cap = SUPPORTED_1000baseT_Full;
		else
			cap = SUPPORTED_1000baseT_Half;
		break;
	case SPEED_10000:
		if (duplex == DUPLEX_FULL)
			cap = SUPPORTED_10000baseT_Full;
	}
	return cap;
}

#define ADVERTISED_MASK (ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full | \
		      ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full | \
		      ADVERTISED_1000baseT_Half | ADVERTISED_1000baseT_Full | \
		      ADVERTISED_10000baseT_Full)

static int set_link_ksettings(struct net_device *dev,
			      const struct ethtool_link_ksettings *cmd)
{
	struct port_info *p = netdev_priv(dev);
	struct link_config *lc = &p->link_config;
	u32 advertising;

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);

	if (!(lc->supported & SUPPORTED_Autoneg)) {
		/*
		 * PHY offers a single speed/duplex.  See if that's what's
		 * being requested.
		 */
		if (cmd->base.autoneg == AUTONEG_DISABLE) {
			u32 speed = cmd->base.speed;
			int cap = speed_duplex_to_caps(speed, cmd->base.duplex);
			if (lc->supported & cap)
				return 0;
		}
		return -EINVAL;
	}

	if (cmd->base.autoneg == AUTONEG_DISABLE) {
		u32 speed = cmd->base.speed;
		int cap = speed_duplex_to_caps(speed, cmd->base.duplex);

		if (!(lc->supported & cap) || (speed == SPEED_1000))
			return -EINVAL;
		lc->requested_speed = speed;
		lc->requested_duplex = cmd->base.duplex;
		lc->advertising = 0;
	} else {
		advertising &= ADVERTISED_MASK;
		advertising &= lc->supported;
		if (!advertising)
			return -EINVAL;
		lc->requested_speed = SPEED_INVALID;
		lc->requested_duplex = DUPLEX_INVALID;
		lc->advertising = advertising | ADVERTISED_Autoneg;
	}
	lc->autoneg = cmd->base.autoneg;
	if (netif_running(dev))
		t3_link_start(&p->phy, &p->mac, lc);
	return 0;
}

static void get_pauseparam(struct net_device *dev,
			   struct ethtool_pauseparam *epause)
{
	struct port_info *p = netdev_priv(dev);

	epause->autoneg = (p->link_config.requested_fc & PAUSE_AUTONEG) != 0;
	epause->rx_pause = (p->link_config.fc & PAUSE_RX) != 0;
	epause->tx_pause = (p->link_config.fc & PAUSE_TX) != 0;
}

static int set_pauseparam(struct net_device *dev,
			  struct ethtool_pauseparam *epause)
{
	struct port_info *p = netdev_priv(dev);
	struct link_config *lc = &p->link_config;

	if (epause->autoneg == AUTONEG_DISABLE)
		lc->requested_fc = 0;
	else if (lc->supported & SUPPORTED_Autoneg)
		lc->requested_fc = PAUSE_AUTONEG;
	else
		return -EINVAL;

	if (epause->rx_pause)
		lc->requested_fc |= PAUSE_RX;
	if (epause->tx_pause)
		lc->requested_fc |= PAUSE_TX;
	if (lc->autoneg == AUTONEG_ENABLE) {
		if (netif_running(dev))
			t3_link_start(&p->phy, &p->mac, lc);
	} else {
		lc->fc = lc->requested_fc & (PAUSE_RX | PAUSE_TX);
		if (netif_running(dev))
			t3_mac_set_speed_duplex_fc(&p->mac, -1, -1, lc->fc);
	}
	return 0;
}

static void get_sge_param(struct net_device *dev, struct ethtool_ringparam *e,
			  struct kernel_ethtool_ringparam *kernel_e,
			  struct netlink_ext_ack *extack)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	const struct qset_params *q = &adapter->params.sge.qset[pi->first_qset];

	e->rx_max_pending = MAX_RX_BUFFERS;
	e->rx_jumbo_max_pending = MAX_RX_JUMBO_BUFFERS;
	e->tx_max_pending = MAX_TXQ_ENTRIES;

	e->rx_pending = q->fl_size;
	e->rx_mini_pending = q->rspq_size;
	e->rx_jumbo_pending = q->jumbo_size;
	e->tx_pending = q->txq_size[0];
}

static int set_sge_param(struct net_device *dev, struct ethtool_ringparam *e,
			 struct kernel_ethtool_ringparam *kernel_e,
			 struct netlink_ext_ack *extack)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct qset_params *q;
	int i;

	if (e->rx_pending > MAX_RX_BUFFERS ||
	    e->rx_jumbo_pending > MAX_RX_JUMBO_BUFFERS ||
	    e->tx_pending > MAX_TXQ_ENTRIES ||
	    e->rx_mini_pending > MAX_RSPQ_ENTRIES ||
	    e->rx_mini_pending < MIN_RSPQ_ENTRIES ||
	    e->rx_pending < MIN_FL_ENTRIES ||
	    e->rx_jumbo_pending < MIN_FL_ENTRIES ||
	    e->tx_pending < adapter->params.nports * MIN_TXQ_ENTRIES)
		return -EINVAL;

	if (adapter->flags & FULL_INIT_DONE)
		return -EBUSY;

	q = &adapter->params.sge.qset[pi->first_qset];
	for (i = 0; i < pi->nqsets; ++i, ++q) {
		q->rspq_size = e->rx_mini_pending;
		q->fl_size = e->rx_pending;
		q->jumbo_size = e->rx_jumbo_pending;
		q->txq_size[0] = e->tx_pending;
		q->txq_size[1] = e->tx_pending;
		q->txq_size[2] = e->tx_pending;
	}
	return 0;
}

static int set_coalesce(struct net_device *dev, struct ethtool_coalesce *c,
			struct kernel_ethtool_coalesce *kernel_coal,
			struct netlink_ext_ack *extack)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct qset_params *qsp;
	struct sge_qset *qs;
	int i;

	if (c->rx_coalesce_usecs * 10 > M_NEWTIMER)
		return -EINVAL;

	for (i = 0; i < pi->nqsets; i++) {
		qsp = &adapter->params.sge.qset[i];
		qs = &adapter->sge.qs[i];
		qsp->coalesce_usecs = c->rx_coalesce_usecs;
		t3_update_qset_coalesce(qs, qsp);
	}

	return 0;
}

static int get_coalesce(struct net_device *dev, struct ethtool_coalesce *c,
			struct kernel_ethtool_coalesce *kernel_coal,
			struct netlink_ext_ack *extack)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct qset_params *q = adapter->params.sge.qset;

	c->rx_coalesce_usecs = q->coalesce_usecs;
	return 0;
}

static int get_eeprom(struct net_device *dev, struct ethtool_eeprom *e,
		      u8 * data)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	int cnt;

	e->magic = EEPROM_MAGIC;
	cnt = pci_read_vpd(adapter->pdev, e->offset, e->len, data);
	if (cnt < 0)
		return cnt;

	e->len = cnt;

	return 0;
}

static int set_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		      u8 * data)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	u32 aligned_offset, aligned_len;
	u8 *buf;
	int err;

	if (eeprom->magic != EEPROM_MAGIC)
		return -EINVAL;

	aligned_offset = eeprom->offset & ~3;
	aligned_len = (eeprom->len + (eeprom->offset & 3) + 3) & ~3;

	if (aligned_offset != eeprom->offset || aligned_len != eeprom->len) {
		buf = kmalloc(aligned_len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		err = pci_read_vpd(adapter->pdev, aligned_offset, aligned_len,
				   buf);
		if (err < 0)
			goto out;
		memcpy(buf + (eeprom->offset & 3), data, eeprom->len);
	} else
		buf = data;

	err = t3_seeprom_wp(adapter, 0);
	if (err)
		goto out;

	err = pci_write_vpd(adapter->pdev, aligned_offset, aligned_len, buf);
	if (err >= 0)
		err = t3_seeprom_wp(adapter, 1);
out:
	if (buf != data)
		kfree(buf);
	return err < 0 ? err : 0;
}

static void get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	wol->supported = 0;
	wol->wolopts = 0;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static const struct ethtool_ops cxgb_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS,
	.get_drvinfo = get_drvinfo,
	.get_msglevel = get_msglevel,
	.set_msglevel = set_msglevel,
	.get_ringparam = get_sge_param,
	.set_ringparam = set_sge_param,
	.get_coalesce = get_coalesce,
	.set_coalesce = set_coalesce,
	.get_eeprom_len = get_eeprom_len,
	.get_eeprom = get_eeprom,
	.set_eeprom = set_eeprom,
	.get_pauseparam = get_pauseparam,
	.set_pauseparam = set_pauseparam,
	.get_link = ethtool_op_get_link,
	.get_strings = get_strings,
	.set_phys_id = set_phys_id,
	.nway_reset = restart_autoneg,
	.get_sset_count = get_sset_count,
	.get_ethtool_stats = get_stats,
	.get_regs_len = get_regs_len,
	.get_regs = get_regs,
	.get_wol = get_wol,
	.get_link_ksettings = get_link_ksettings,
	.set_link_ksettings = set_link_ksettings,
};

static int cxgb_in_range(int val, int lo, int hi)
{
	return val < 0 || (val <= hi && val >= lo);
}

static int cxgb_siocdevprivate(struct net_device *dev,
			       struct ifreq *ifreq,
			       void __user *useraddr,
			       int cmd)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	int ret;

	if (cmd != SIOCCHIOCTL)
		return -EOPNOTSUPP;

	if (copy_from_user(&cmd, useraddr, sizeof(cmd)))
		return -EFAULT;

	switch (cmd) {
	case CHELSIO_SET_QSET_PARAMS:{
		int i;
		struct qset_params *q;
		struct ch_qset_params t;
		int q1 = pi->first_qset;
		int nqsets = pi->nqsets;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;
		if (t.cmd != CHELSIO_SET_QSET_PARAMS)
			return -EINVAL;
		if (t.qset_idx >= SGE_QSETS)
			return -EINVAL;
		if (!cxgb_in_range(t.intr_lat, 0, M_NEWTIMER) ||
		    !cxgb_in_range(t.cong_thres, 0, 255) ||
		    !cxgb_in_range(t.txq_size[0], MIN_TXQ_ENTRIES,
			      MAX_TXQ_ENTRIES) ||
		    !cxgb_in_range(t.txq_size[1], MIN_TXQ_ENTRIES,
			      MAX_TXQ_ENTRIES) ||
		    !cxgb_in_range(t.txq_size[2], MIN_CTRL_TXQ_ENTRIES,
			      MAX_CTRL_TXQ_ENTRIES) ||
		    !cxgb_in_range(t.fl_size[0], MIN_FL_ENTRIES,
			      MAX_RX_BUFFERS) ||
		    !cxgb_in_range(t.fl_size[1], MIN_FL_ENTRIES,
			      MAX_RX_JUMBO_BUFFERS) ||
		    !cxgb_in_range(t.rspq_size, MIN_RSPQ_ENTRIES,
			      MAX_RSPQ_ENTRIES))
			return -EINVAL;

		if ((adapter->flags & FULL_INIT_DONE) &&
			(t.rspq_size >= 0 || t.fl_size[0] >= 0 ||
			t.fl_size[1] >= 0 || t.txq_size[0] >= 0 ||
			t.txq_size[1] >= 0 || t.txq_size[2] >= 0 ||
			t.polling >= 0 || t.cong_thres >= 0))
			return -EBUSY;

		/* Allow setting of any available qset when offload enabled */
		if (test_bit(OFFLOAD_DEVMAP_BIT, &adapter->open_device_map)) {
			q1 = 0;
			for_each_port(adapter, i) {
				pi = adap2pinfo(adapter, i);
				nqsets += pi->first_qset + pi->nqsets;
			}
		}

		if (t.qset_idx < q1)
			return -EINVAL;
		if (t.qset_idx > q1 + nqsets - 1)
			return -EINVAL;

		q = &adapter->params.sge.qset[t.qset_idx];

		if (t.rspq_size >= 0)
			q->rspq_size = t.rspq_size;
		if (t.fl_size[0] >= 0)
			q->fl_size = t.fl_size[0];
		if (t.fl_size[1] >= 0)
			q->jumbo_size = t.fl_size[1];
		if (t.txq_size[0] >= 0)
			q->txq_size[0] = t.txq_size[0];
		if (t.txq_size[1] >= 0)
			q->txq_size[1] = t.txq_size[1];
		if (t.txq_size[2] >= 0)
			q->txq_size[2] = t.txq_size[2];
		if (t.cong_thres >= 0)
			q->cong_thres = t.cong_thres;
		if (t.intr_lat >= 0) {
			struct sge_qset *qs =
				&adapter->sge.qs[t.qset_idx];

			q->coalesce_usecs = t.intr_lat;
			t3_update_qset_coalesce(qs, q);
		}
		if (t.polling >= 0) {
			if (adapter->flags & USING_MSIX)
				q->polling = t.polling;
			else {
				/* No polling with INTx for T3A */
				if (adapter->params.rev == 0 &&
					!(adapter->flags & USING_MSI))
					t.polling = 0;

				for (i = 0; i < SGE_QSETS; i++) {
					q = &adapter->params.sge.
						qset[i];
					q->polling = t.polling;
				}
			}
		}

		if (t.lro >= 0) {
			if (t.lro)
				dev->wanted_features |= NETIF_F_GRO;
			else
				dev->wanted_features &= ~NETIF_F_GRO;
			netdev_update_features(dev);
		}

		break;
	}
	case CHELSIO_GET_QSET_PARAMS:{
		struct qset_params *q;
		struct ch_qset_params t;
		int q1 = pi->first_qset;
		int nqsets = pi->nqsets;
		int i;

		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;

		if (t.cmd != CHELSIO_GET_QSET_PARAMS)
			return -EINVAL;

		/* Display qsets for all ports when offload enabled */
		if (test_bit(OFFLOAD_DEVMAP_BIT, &adapter->open_device_map)) {
			q1 = 0;
			for_each_port(adapter, i) {
				pi = adap2pinfo(adapter, i);
				nqsets = pi->first_qset + pi->nqsets;
			}
		}

		if (t.qset_idx >= nqsets)
			return -EINVAL;
		t.qset_idx = array_index_nospec(t.qset_idx, nqsets);

		q = &adapter->params.sge.qset[q1 + t.qset_idx];
		t.rspq_size = q->rspq_size;
		t.txq_size[0] = q->txq_size[0];
		t.txq_size[1] = q->txq_size[1];
		t.txq_size[2] = q->txq_size[2];
		t.fl_size[0] = q->fl_size;
		t.fl_size[1] = q->jumbo_size;
		t.polling = q->polling;
		t.lro = !!(dev->features & NETIF_F_GRO);
		t.intr_lat = q->coalesce_usecs;
		t.cong_thres = q->cong_thres;
		t.qnum = q1;

		if (adapter->flags & USING_MSIX)
			t.vector = adapter->msix_info[q1 + t.qset_idx + 1].vec;
		else
			t.vector = adapter->pdev->irq;

		if (copy_to_user(useraddr, &t, sizeof(t)))
			return -EFAULT;
		break;
	}
	case CHELSIO_SET_QSET_NUM:{
		struct ch_reg edata;
		unsigned int i, first_qset = 0, other_qsets = 0;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (adapter->flags & FULL_INIT_DONE)
			return -EBUSY;
		if (copy_from_user(&edata, useraddr, sizeof(edata)))
			return -EFAULT;
		if (edata.cmd != CHELSIO_SET_QSET_NUM)
			return -EINVAL;
		if (edata.val < 1 ||
			(edata.val > 1 && !(adapter->flags & USING_MSIX)))
			return -EINVAL;

		for_each_port(adapter, i)
			if (adapter->port[i] && adapter->port[i] != dev)
				other_qsets += adap2pinfo(adapter, i)->nqsets;

		if (edata.val + other_qsets > SGE_QSETS)
			return -EINVAL;

		pi->nqsets = edata.val;

		for_each_port(adapter, i)
			if (adapter->port[i]) {
				pi = adap2pinfo(adapter, i);
				pi->first_qset = first_qset;
				first_qset += pi->nqsets;
			}
		break;
	}
	case CHELSIO_GET_QSET_NUM:{
		struct ch_reg edata;

		memset(&edata, 0, sizeof(struct ch_reg));

		edata.cmd = CHELSIO_GET_QSET_NUM;
		edata.val = pi->nqsets;
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		break;
	}
	case CHELSIO_LOAD_FW:{
		u8 *fw_data;
		struct ch_mem_range t;

		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;
		if (t.cmd != CHELSIO_LOAD_FW)
			return -EINVAL;
		/* Check t.len sanity ? */
		fw_data = memdup_user(useraddr + sizeof(t), t.len);
		if (IS_ERR(fw_data))
			return PTR_ERR(fw_data);

		ret = t3_load_fw(adapter, fw_data, t.len);
		kfree(fw_data);
		if (ret)
			return ret;
		break;
	}
	case CHELSIO_SETMTUTAB:{
		struct ch_mtus m;
		int i;

		if (!is_offload(adapter))
			return -EOPNOTSUPP;
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (offload_running(adapter))
			return -EBUSY;
		if (copy_from_user(&m, useraddr, sizeof(m)))
			return -EFAULT;
		if (m.cmd != CHELSIO_SETMTUTAB)
			return -EINVAL;
		if (m.nmtus != NMTUS)
			return -EINVAL;
		if (m.mtus[0] < 81)	/* accommodate SACK */
			return -EINVAL;

		/* MTUs must be in ascending order */
		for (i = 1; i < NMTUS; ++i)
			if (m.mtus[i] < m.mtus[i - 1])
				return -EINVAL;

		memcpy(adapter->params.mtus, m.mtus,
			sizeof(adapter->params.mtus));
		break;
	}
	case CHELSIO_GET_PM:{
		struct tp_params *p = &adapter->params.tp;
		struct ch_pm m = {.cmd = CHELSIO_GET_PM };

		if (!is_offload(adapter))
			return -EOPNOTSUPP;
		m.tx_pg_sz = p->tx_pg_size;
		m.tx_num_pg = p->tx_num_pgs;
		m.rx_pg_sz = p->rx_pg_size;
		m.rx_num_pg = p->rx_num_pgs;
		m.pm_total = p->pmtx_size + p->chan_rx_size * p->nchan;
		if (copy_to_user(useraddr, &m, sizeof(m)))
			return -EFAULT;
		break;
	}
	case CHELSIO_SET_PM:{
		struct ch_pm m;
		struct tp_params *p = &adapter->params.tp;

		if (!is_offload(adapter))
			return -EOPNOTSUPP;
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (adapter->flags & FULL_INIT_DONE)
			return -EBUSY;
		if (copy_from_user(&m, useraddr, sizeof(m)))
			return -EFAULT;
		if (m.cmd != CHELSIO_SET_PM)
			return -EINVAL;
		if (!is_power_of_2(m.rx_pg_sz) ||
			!is_power_of_2(m.tx_pg_sz))
			return -EINVAL;	/* not power of 2 */
		if (!(m.rx_pg_sz & 0x14000))
			return -EINVAL;	/* not 16KB or 64KB */
		if (!(m.tx_pg_sz & 0x1554000))
			return -EINVAL;
		if (m.tx_num_pg == -1)
			m.tx_num_pg = p->tx_num_pgs;
		if (m.rx_num_pg == -1)
			m.rx_num_pg = p->rx_num_pgs;
		if (m.tx_num_pg % 24 || m.rx_num_pg % 24)
			return -EINVAL;
		if (m.rx_num_pg * m.rx_pg_sz > p->chan_rx_size ||
			m.tx_num_pg * m.tx_pg_sz > p->chan_tx_size)
			return -EINVAL;
		p->rx_pg_size = m.rx_pg_sz;
		p->tx_pg_size = m.tx_pg_sz;
		p->rx_num_pgs = m.rx_num_pg;
		p->tx_num_pgs = m.tx_num_pg;
		break;
	}
	case CHELSIO_GET_MEM:{
		struct ch_mem_range t;
		struct mc7 *mem;
		u64 buf[32];

		if (!is_offload(adapter))
			return -EOPNOTSUPP;
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (!(adapter->flags & FULL_INIT_DONE))
			return -EIO;	/* need the memory controllers */
		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;
		if (t.cmd != CHELSIO_GET_MEM)
			return -EINVAL;
		if ((t.addr & 7) || (t.len & 7))
			return -EINVAL;
		if (t.mem_id == MEM_CM)
			mem = &adapter->cm;
		else if (t.mem_id == MEM_PMRX)
			mem = &adapter->pmrx;
		else if (t.mem_id == MEM_PMTX)
			mem = &adapter->pmtx;
		else
			return -EINVAL;

		/*
		 * Version scheme:
		 * bits 0..9: chip version
		 * bits 10..15: chip revision
		 */
		t.version = 3 | (adapter->params.rev << 10);
		if (copy_to_user(useraddr, &t, sizeof(t)))
			return -EFAULT;

		/*
		 * Read 256 bytes at a time as len can be large and we don't
		 * want to use huge intermediate buffers.
		 */
		useraddr += sizeof(t);	/* advance to start of buffer */
		while (t.len) {
			unsigned int chunk =
				min_t(unsigned int, t.len, sizeof(buf));

			ret =
				t3_mc7_bd_read(mem, t.addr / 8, chunk / 8,
						buf);
			if (ret)
				return ret;
			if (copy_to_user(useraddr, buf, chunk))
				return -EFAULT;
			useraddr += chunk;
			t.addr += chunk;
			t.len -= chunk;
		}
		break;
	}
	case CHELSIO_SET_TRACE_FILTER:{
		struct ch_trace t;
		const struct trace_params *tp;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (!offload_running(adapter))
			return -EAGAIN;
		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;
		if (t.cmd != CHELSIO_SET_TRACE_FILTER)
			return -EINVAL;

		tp = (const struct trace_params *)&t.sip;
		if (t.config_tx)
			t3_config_trace_filter(adapter, tp, 0,
						t.invert_match,
						t.trace_tx);
		if (t.config_rx)
			t3_config_trace_filter(adapter, tp, 1,
						t.invert_match,
						t.trace_rx);
		break;
	}
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int cxgb_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	struct mii_ioctl_data *data = if_mii(req);
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	switch (cmd) {
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		/* Convert phy_id from older PRTAD/DEVAD format */
		if (is_10G(adapter) &&
		    !mdio_phy_id_is_c45(data->phy_id) &&
		    (data->phy_id & 0x1f00) &&
		    !(data->phy_id & 0xe0e0))
			data->phy_id = mdio_phy_id_c45(data->phy_id >> 8,
						       data->phy_id & 0x1f);
		fallthrough;
	case SIOCGMIIPHY:
		return mdio_mii_ioctl(&pi->phy.mdio, data, cmd);
	default:
		return -EOPNOTSUPP;
	}
}

static int cxgb_change_mtu(struct net_device *dev, int new_mtu)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	int ret;

	if ((ret = t3_mac_set_mtu(&pi->mac, new_mtu)))
		return ret;
	dev->mtu = new_mtu;
	init_port_mtus(adapter);
	if (adapter->params.rev == 0 && offload_running(adapter))
		t3_load_mtus(adapter, adapter->params.mtus,
			     adapter->params.a_wnd, adapter->params.b_wnd,
			     adapter->port[0]->mtu);
	return 0;
}

static int cxgb_set_mac_addr(struct net_device *dev, void *p)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(dev, addr->sa_data);
	t3_mac_set_address(&pi->mac, LAN_MAC_IDX, dev->dev_addr);
	if (offload_running(adapter))
		write_smt_entry(adapter, pi->port_id);
	return 0;
}

static netdev_features_t cxgb_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	/*
	 * Since there is no support for separate rx/tx vlan accel
	 * enable/disable make sure tx flag is always in same state as rx.
	 */
	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		features |= NETIF_F_HW_VLAN_CTAG_TX;
	else
		features &= ~NETIF_F_HW_VLAN_CTAG_TX;

	return features;
}

static int cxgb_set_features(struct net_device *dev, netdev_features_t features)
{
	netdev_features_t changed = dev->features ^ features;

	if (changed & NETIF_F_HW_VLAN_CTAG_RX)
		cxgb_vlan_mode(dev, features);

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void cxgb_netpoll(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	int qidx;

	for (qidx = pi->first_qset; qidx < pi->first_qset + pi->nqsets; qidx++) {
		struct sge_qset *qs = &adapter->sge.qs[qidx];
		void *source;

		if (adapter->flags & USING_MSIX)
			source = qs;
		else
			source = adapter;

		t3_intr_handler(adapter, qs->rspq.polling) (0, source);
	}
}
#endif

/*
 * Periodic accumulation of MAC statistics.
 */
static void mac_stats_update(struct adapter *adapter)
{
	int i;

	for_each_port(adapter, i) {
		struct net_device *dev = adapter->port[i];
		struct port_info *p = netdev_priv(dev);

		if (netif_running(dev)) {
			spin_lock(&adapter->stats_lock);
			t3_mac_update_stats(&p->mac);
			spin_unlock(&adapter->stats_lock);
		}
	}
}

static void check_link_status(struct adapter *adapter)
{
	int i;

	for_each_port(adapter, i) {
		struct net_device *dev = adapter->port[i];
		struct port_info *p = netdev_priv(dev);
		int link_fault;

		spin_lock_irq(&adapter->work_lock);
		link_fault = p->link_fault;
		spin_unlock_irq(&adapter->work_lock);

		if (link_fault) {
			t3_link_fault(adapter, i);
			continue;
		}

		if (!(p->phy.caps & SUPPORTED_IRQ) && netif_running(dev)) {
			t3_xgm_intr_disable(adapter, i);
			t3_read_reg(adapter, A_XGM_INT_STATUS + p->mac.offset);

			t3_link_changed(adapter, i);
			t3_xgm_intr_enable(adapter, i);
		}
	}
}

static void check_t3b2_mac(struct adapter *adapter)
{
	int i;

	if (!rtnl_trylock())	/* synchronize with ifdown */
		return;

	for_each_port(adapter, i) {
		struct net_device *dev = adapter->port[i];
		struct port_info *p = netdev_priv(dev);
		int status;

		if (!netif_running(dev))
			continue;

		status = 0;
		if (netif_running(dev) && netif_carrier_ok(dev))
			status = t3b2_mac_watchdog_task(&p->mac);
		if (status == 1)
			p->mac.stats.num_toggled++;
		else if (status == 2) {
			struct cmac *mac = &p->mac;

			t3_mac_set_mtu(mac, dev->mtu);
			t3_mac_set_address(mac, LAN_MAC_IDX, dev->dev_addr);
			cxgb_set_rxmode(dev);
			t3_link_start(&p->phy, mac, &p->link_config);
			t3_mac_enable(mac, MAC_DIRECTION_RX | MAC_DIRECTION_TX);
			t3_port_intr_enable(adapter, p->port_id);
			p->mac.stats.num_resets++;
		}
	}
	rtnl_unlock();
}


static void t3_adap_check_task(struct work_struct *work)
{
	struct adapter *adapter = container_of(work, struct adapter,
					       adap_check_task.work);
	const struct adapter_params *p = &adapter->params;
	int port;
	unsigned int v, status, reset;

	adapter->check_task_cnt++;

	check_link_status(adapter);

	/* Accumulate MAC stats if needed */
	if (!p->linkpoll_period ||
	    (adapter->check_task_cnt * p->linkpoll_period) / 10 >=
	    p->stats_update_period) {
		mac_stats_update(adapter);
		adapter->check_task_cnt = 0;
	}

	if (p->rev == T3_REV_B2)
		check_t3b2_mac(adapter);

	/*
	 * Scan the XGMAC's to check for various conditions which we want to
	 * monitor in a periodic polling manner rather than via an interrupt
	 * condition.  This is used for conditions which would otherwise flood
	 * the system with interrupts and we only really need to know that the
	 * conditions are "happening" ...  For each condition we count the
	 * detection of the condition and reset it for the next polling loop.
	 */
	for_each_port(adapter, port) {
		struct cmac *mac =  &adap2pinfo(adapter, port)->mac;
		u32 cause;

		cause = t3_read_reg(adapter, A_XGM_INT_CAUSE + mac->offset);
		reset = 0;
		if (cause & F_RXFIFO_OVERFLOW) {
			mac->stats.rx_fifo_ovfl++;
			reset |= F_RXFIFO_OVERFLOW;
		}

		t3_write_reg(adapter, A_XGM_INT_CAUSE + mac->offset, reset);
	}

	/*
	 * We do the same as above for FL_EMPTY interrupts.
	 */
	status = t3_read_reg(adapter, A_SG_INT_CAUSE);
	reset = 0;

	if (status & F_FLEMPTY) {
		struct sge_qset *qs = &adapter->sge.qs[0];
		int i = 0;

		reset |= F_FLEMPTY;

		v = (t3_read_reg(adapter, A_SG_RSPQ_FL_STATUS) >> S_FL0EMPTY) &
		    0xffff;

		while (v) {
			qs->fl[i].empty += (v & 1);
			if (i)
				qs++;
			i ^= 1;
			v >>= 1;
		}
	}

	t3_write_reg(adapter, A_SG_INT_CAUSE, reset);

	/* Schedule the next check update if any port is active. */
	spin_lock_irq(&adapter->work_lock);
	if (adapter->open_device_map & PORT_MASK)
		schedule_chk_task(adapter);
	spin_unlock_irq(&adapter->work_lock);
}

static void db_full_task(struct work_struct *work)
{
	struct adapter *adapter = container_of(work, struct adapter,
					       db_full_task);

	cxgb3_event_notify(&adapter->tdev, OFFLOAD_DB_FULL, 0);
}

static void db_empty_task(struct work_struct *work)
{
	struct adapter *adapter = container_of(work, struct adapter,
					       db_empty_task);

	cxgb3_event_notify(&adapter->tdev, OFFLOAD_DB_EMPTY, 0);
}

static void db_drop_task(struct work_struct *work)
{
	struct adapter *adapter = container_of(work, struct adapter,
					       db_drop_task);
	unsigned long delay = 1000;
	unsigned short r;

	cxgb3_event_notify(&adapter->tdev, OFFLOAD_DB_DROP, 0);

	/*
	 * Sleep a while before ringing the driver qset dbs.
	 * The delay is between 1000-2023 usecs.
	 */
	get_random_bytes(&r, 2);
	delay += r & 1023;
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(usecs_to_jiffies(delay));
	ring_dbs(adapter);
}

/*
 * Processes external (PHY) interrupts in process context.
 */
static void ext_intr_task(struct work_struct *work)
{
	struct adapter *adapter = container_of(work, struct adapter,
					       ext_intr_handler_task);
	int i;

	/* Disable link fault interrupts */
	for_each_port(adapter, i) {
		struct net_device *dev = adapter->port[i];
		struct port_info *p = netdev_priv(dev);

		t3_xgm_intr_disable(adapter, i);
		t3_read_reg(adapter, A_XGM_INT_STATUS + p->mac.offset);
	}

	/* Re-enable link fault interrupts */
	t3_phy_intr_handler(adapter);

	for_each_port(adapter, i)
		t3_xgm_intr_enable(adapter, i);

	/* Now reenable external interrupts */
	spin_lock_irq(&adapter->work_lock);
	if (adapter->slow_intr_mask) {
		adapter->slow_intr_mask |= F_T3DBG;
		t3_write_reg(adapter, A_PL_INT_CAUSE0, F_T3DBG);
		t3_write_reg(adapter, A_PL_INT_ENABLE0,
			     adapter->slow_intr_mask);
	}
	spin_unlock_irq(&adapter->work_lock);
}

/*
 * Interrupt-context handler for external (PHY) interrupts.
 */
void t3_os_ext_intr_handler(struct adapter *adapter)
{
	/*
	 * Schedule a task to handle external interrupts as they may be slow
	 * and we use a mutex to protect MDIO registers.  We disable PHY
	 * interrupts in the meantime and let the task reenable them when
	 * it's done.
	 */
	spin_lock(&adapter->work_lock);
	if (adapter->slow_intr_mask) {
		adapter->slow_intr_mask &= ~F_T3DBG;
		t3_write_reg(adapter, A_PL_INT_ENABLE0,
			     adapter->slow_intr_mask);
		queue_work(cxgb3_wq, &adapter->ext_intr_handler_task);
	}
	spin_unlock(&adapter->work_lock);
}

void t3_os_link_fault_handler(struct adapter *adapter, int port_id)
{
	struct net_device *netdev = adapter->port[port_id];
	struct port_info *pi = netdev_priv(netdev);

	spin_lock(&adapter->work_lock);
	pi->link_fault = 1;
	spin_unlock(&adapter->work_lock);
}

static int t3_adapter_error(struct adapter *adapter, int reset, int on_wq)
{
	int i, ret = 0;

	if (is_offload(adapter) &&
	    test_bit(OFFLOAD_DEVMAP_BIT, &adapter->open_device_map)) {
		cxgb3_event_notify(&adapter->tdev, OFFLOAD_STATUS_DOWN, 0);
		offload_close(&adapter->tdev);
	}

	/* Stop all ports */
	for_each_port(adapter, i) {
		struct net_device *netdev = adapter->port[i];

		if (netif_running(netdev))
			__cxgb_close(netdev, on_wq);
	}

	/* Stop SGE timers */
	t3_stop_sge_timers(adapter);

	adapter->flags &= ~FULL_INIT_DONE;

	if (reset)
		ret = t3_reset_adapter(adapter);

	pci_disable_device(adapter->pdev);

	return ret;
}

static int t3_reenable_adapter(struct adapter *adapter)
{
	if (pci_enable_device(adapter->pdev)) {
		dev_err(&adapter->pdev->dev,
			"Cannot re-enable PCI device after reset.\n");
		goto err;
	}
	pci_set_master(adapter->pdev);
	pci_restore_state(adapter->pdev);
	pci_save_state(adapter->pdev);

	/* Free sge resources */
	t3_free_sge_resources(adapter);

	if (t3_replay_prep_adapter(adapter))
		goto err;

	return 0;
err:
	return -1;
}

static void t3_resume_ports(struct adapter *adapter)
{
	int i;

	/* Restart the ports */
	for_each_port(adapter, i) {
		struct net_device *netdev = adapter->port[i];

		if (netif_running(netdev)) {
			if (cxgb_open(netdev)) {
				dev_err(&adapter->pdev->dev,
					"can't bring device back up"
					" after reset\n");
				continue;
			}
		}
	}

	if (is_offload(adapter) && !ofld_disable)
		cxgb3_event_notify(&adapter->tdev, OFFLOAD_STATUS_UP, 0);
}

/*
 * processes a fatal error.
 * Bring the ports down, reset the chip, bring the ports back up.
 */
static void fatal_error_task(struct work_struct *work)
{
	struct adapter *adapter = container_of(work, struct adapter,
					       fatal_error_handler_task);
	int err = 0;

	rtnl_lock();
	err = t3_adapter_error(adapter, 1, 1);
	if (!err)
		err = t3_reenable_adapter(adapter);
	if (!err)
		t3_resume_ports(adapter);

	CH_ALERT(adapter, "adapter reset %s\n", err ? "failed" : "succeeded");
	rtnl_unlock();
}

void t3_fatal_err(struct adapter *adapter)
{
	unsigned int fw_status[4];

	if (adapter->flags & FULL_INIT_DONE) {
		t3_sge_stop_dma(adapter);
		t3_write_reg(adapter, A_XGM_TX_CTRL, 0);
		t3_write_reg(adapter, A_XGM_RX_CTRL, 0);
		t3_write_reg(adapter, XGM_REG(A_XGM_TX_CTRL, 1), 0);
		t3_write_reg(adapter, XGM_REG(A_XGM_RX_CTRL, 1), 0);

		spin_lock(&adapter->work_lock);
		t3_intr_disable(adapter);
		queue_work(cxgb3_wq, &adapter->fatal_error_handler_task);
		spin_unlock(&adapter->work_lock);
	}
	CH_ALERT(adapter, "encountered fatal error, operation suspended\n");
	if (!t3_cim_ctl_blk_read(adapter, 0xa0, 4, fw_status))
		CH_ALERT(adapter, "FW status: 0x%x, 0x%x, 0x%x, 0x%x\n",
			 fw_status[0], fw_status[1],
			 fw_status[2], fw_status[3]);
}

/**
 * t3_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t t3_io_error_detected(struct pci_dev *pdev,
					     pci_channel_state_t state)
{
	struct adapter *adapter = pci_get_drvdata(pdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	t3_adapter_error(adapter, 0, 0);

	/* Request a slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * t3_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 */
static pci_ers_result_t t3_io_slot_reset(struct pci_dev *pdev)
{
	struct adapter *adapter = pci_get_drvdata(pdev);

	if (!t3_reenable_adapter(adapter))
		return PCI_ERS_RESULT_RECOVERED;

	return PCI_ERS_RESULT_DISCONNECT;
}

/**
 * t3_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation.
 */
static void t3_io_resume(struct pci_dev *pdev)
{
	struct adapter *adapter = pci_get_drvdata(pdev);

	CH_ALERT(adapter, "adapter recovering, PEX ERR 0x%x\n",
		 t3_read_reg(adapter, A_PCIE_PEX_ERR));

	rtnl_lock();
	t3_resume_ports(adapter);
	rtnl_unlock();
}

static const struct pci_error_handlers t3_err_handler = {
	.error_detected = t3_io_error_detected,
	.slot_reset = t3_io_slot_reset,
	.resume = t3_io_resume,
};

/*
 * Set the number of qsets based on the number of CPUs and the number of ports,
 * not to exceed the number of available qsets, assuming there are enough qsets
 * per port in HW.
 */
static void set_nqsets(struct adapter *adap)
{
	int i, j = 0;
	int num_cpus = netif_get_num_default_rss_queues();
	int hwports = adap->params.nports;
	int nqsets = adap->msix_nvectors - 1;

	if (adap->params.rev > 0 && adap->flags & USING_MSIX) {
		if (hwports == 2 &&
		    (hwports * nqsets > SGE_QSETS ||
		     num_cpus >= nqsets / hwports))
			nqsets /= hwports;
		if (nqsets > num_cpus)
			nqsets = num_cpus;
		if (nqsets < 1 || hwports == 4)
			nqsets = 1;
	} else {
		nqsets = 1;
	}

	for_each_port(adap, i) {
		struct port_info *pi = adap2pinfo(adap, i);

		pi->first_qset = j;
		pi->nqsets = nqsets;
		j = pi->first_qset + nqsets;

		dev_info(&adap->pdev->dev,
			 "Port %d using %d queue sets.\n", i, nqsets);
	}
}

static int cxgb_enable_msix(struct adapter *adap)
{
	struct msix_entry entries[SGE_QSETS + 1];
	int vectors;
	int i;

	vectors = ARRAY_SIZE(entries);
	for (i = 0; i < vectors; ++i)
		entries[i].entry = i;

	vectors = pci_enable_msix_range(adap->pdev, entries,
					adap->params.nports + 1, vectors);
	if (vectors < 0)
		return vectors;

	for (i = 0; i < vectors; ++i)
		adap->msix_info[i].vec = entries[i].vector;
	adap->msix_nvectors = vectors;

	return 0;
}

static void print_port_info(struct adapter *adap, const struct adapter_info *ai)
{
	static const char *pci_variant[] = {
		"PCI", "PCI-X", "PCI-X ECC", "PCI-X 266", "PCI Express"
	};

	int i;
	char buf[80];

	if (is_pcie(adap))
		snprintf(buf, sizeof(buf), "%s x%d",
			 pci_variant[adap->params.pci.variant],
			 adap->params.pci.width);
	else
		snprintf(buf, sizeof(buf), "%s %dMHz/%d-bit",
			 pci_variant[adap->params.pci.variant],
			 adap->params.pci.speed, adap->params.pci.width);

	for_each_port(adap, i) {
		struct net_device *dev = adap->port[i];
		const struct port_info *pi = netdev_priv(dev);

		if (!test_bit(i, &adap->registered_device_map))
			continue;
		netdev_info(dev, "%s %s %sNIC (rev %d) %s%s\n",
			    ai->desc, pi->phy.desc,
			    is_offload(adap) ? "R" : "", adap->params.rev, buf,
			    (adap->flags & USING_MSIX) ? " MSI-X" :
			    (adap->flags & USING_MSI) ? " MSI" : "");
		if (adap->name == dev->name && adap->params.vpd.mclk)
			pr_info("%s: %uMB CM, %uMB PMTX, %uMB PMRX, S/N: %s\n",
			       adap->name, t3_mc7_size(&adap->cm) >> 20,
			       t3_mc7_size(&adap->pmtx) >> 20,
			       t3_mc7_size(&adap->pmrx) >> 20,
			       adap->params.vpd.sn);
	}
}

static const struct net_device_ops cxgb_netdev_ops = {
	.ndo_open		= cxgb_open,
	.ndo_stop		= cxgb_close,
	.ndo_start_xmit		= t3_eth_xmit,
	.ndo_get_stats		= cxgb_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= cxgb_set_rxmode,
	.ndo_eth_ioctl		= cxgb_ioctl,
	.ndo_siocdevprivate	= cxgb_siocdevprivate,
	.ndo_change_mtu		= cxgb_change_mtu,
	.ndo_set_mac_address	= cxgb_set_mac_addr,
	.ndo_fix_features	= cxgb_fix_features,
	.ndo_set_features	= cxgb_set_features,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= cxgb_netpoll,
#endif
};

static void cxgb3_init_iscsi_mac(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);

	memcpy(pi->iscsic.mac_addr, dev->dev_addr, ETH_ALEN);
	pi->iscsic.mac_addr[3] |= 0x80;
}

#define TSO_FLAGS (NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_TSO_ECN)
#define VLAN_FEAT (NETIF_F_SG | NETIF_F_IP_CSUM | TSO_FLAGS | \
			NETIF_F_IPV6_CSUM | NETIF_F_HIGHDMA)
static int init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int i, err;
	resource_size_t mmio_start, mmio_len;
	const struct adapter_info *ai;
	struct adapter *adapter = NULL;
	struct port_info *pi;

	if (!cxgb3_wq) {
		cxgb3_wq = create_singlethread_workqueue(DRV_NAME);
		if (!cxgb3_wq) {
			pr_err("cannot initialize work queue\n");
			return -ENOMEM;
		}
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "cannot enable PCI device\n");
		goto out;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		/* Just info, some other driver may have claimed the device. */
		dev_info(&pdev->dev, "cannot obtain PCI resources\n");
		goto out_disable_device;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev, "no usable DMA configuration\n");
		goto out_release_regions;
	}

	pci_set_master(pdev);
	pci_save_state(pdev);

	mmio_start = pci_resource_start(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);
	ai = t3_get_adapter_info(ent->driver_data);

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		err = -ENOMEM;
		goto out_release_regions;
	}

	adapter->nofail_skb =
		alloc_skb(sizeof(struct cpl_set_tcb_field), GFP_KERNEL);
	if (!adapter->nofail_skb) {
		dev_err(&pdev->dev, "cannot allocate nofail buffer\n");
		err = -ENOMEM;
		goto out_free_adapter;
	}

	adapter->regs = ioremap(mmio_start, mmio_len);
	if (!adapter->regs) {
		dev_err(&pdev->dev, "cannot map device registers\n");
		err = -ENOMEM;
		goto out_free_adapter_nofail;
	}

	adapter->pdev = pdev;
	adapter->name = pci_name(pdev);
	adapter->msg_enable = dflt_msg_enable;
	adapter->mmio_len = mmio_len;

	mutex_init(&adapter->mdio_lock);
	spin_lock_init(&adapter->work_lock);
	spin_lock_init(&adapter->stats_lock);

	INIT_LIST_HEAD(&adapter->adapter_list);
	INIT_WORK(&adapter->ext_intr_handler_task, ext_intr_task);
	INIT_WORK(&adapter->fatal_error_handler_task, fatal_error_task);

	INIT_WORK(&adapter->db_full_task, db_full_task);
	INIT_WORK(&adapter->db_empty_task, db_empty_task);
	INIT_WORK(&adapter->db_drop_task, db_drop_task);

	INIT_DELAYED_WORK(&adapter->adap_check_task, t3_adap_check_task);

	for (i = 0; i < ai->nports0 + ai->nports1; ++i) {
		struct net_device *netdev;

		netdev = alloc_etherdev_mq(sizeof(struct port_info), SGE_QSETS);
		if (!netdev) {
			err = -ENOMEM;
			goto out_free_dev;
		}

		SET_NETDEV_DEV(netdev, &pdev->dev);

		adapter->port[i] = netdev;
		pi = netdev_priv(netdev);
		pi->adapter = adapter;
		pi->port_id = i;
		netif_carrier_off(netdev);
		netdev->irq = pdev->irq;
		netdev->mem_start = mmio_start;
		netdev->mem_end = mmio_start + mmio_len - 1;
		netdev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM |
			NETIF_F_TSO | NETIF_F_RXCSUM | NETIF_F_HW_VLAN_CTAG_RX;
		netdev->features |= netdev->hw_features |
				    NETIF_F_HW_VLAN_CTAG_TX;
		netdev->vlan_features |= netdev->features & VLAN_FEAT;

		netdev->features |= NETIF_F_HIGHDMA;

		netdev->netdev_ops = &cxgb_netdev_ops;
		netdev->ethtool_ops = &cxgb_ethtool_ops;
		netdev->min_mtu = 81;
		netdev->max_mtu = ETH_MAX_MTU;
		netdev->dev_port = pi->port_id;
	}

	pci_set_drvdata(pdev, adapter);
	if (t3_prep_adapter(adapter, ai, 1) < 0) {
		err = -ENODEV;
		goto out_free_dev;
	}

	/*
	 * The card is now ready to go.  If any errors occur during device
	 * registration we do not fail the whole card but rather proceed only
	 * with the ports we manage to register successfully.  However we must
	 * register at least one net device.
	 */
	for_each_port(adapter, i) {
		err = register_netdev(adapter->port[i]);
		if (err)
			dev_warn(&pdev->dev,
				 "cannot register net device %s, skipping\n",
				 adapter->port[i]->name);
		else {
			/*
			 * Change the name we use for messages to the name of
			 * the first successfully registered interface.
			 */
			if (!adapter->registered_device_map)
				adapter->name = adapter->port[i]->name;

			__set_bit(i, &adapter->registered_device_map);
		}
	}
	if (!adapter->registered_device_map) {
		dev_err(&pdev->dev, "could not register any net devices\n");
		err = -ENODEV;
		goto out_free_dev;
	}

	for_each_port(adapter, i)
		cxgb3_init_iscsi_mac(adapter->port[i]);

	/* Driver's ready. Reflect it on LEDs */
	t3_led_ready(adapter);

	if (is_offload(adapter)) {
		__set_bit(OFFLOAD_DEVMAP_BIT, &adapter->registered_device_map);
		cxgb3_adapter_ofld(adapter);
	}

	/* See what interrupts we'll be using */
	if (msi > 1 && cxgb_enable_msix(adapter) == 0)
		adapter->flags |= USING_MSIX;
	else if (msi > 0 && pci_enable_msi(pdev) == 0)
		adapter->flags |= USING_MSI;

	set_nqsets(adapter);

	err = sysfs_create_group(&adapter->port[0]->dev.kobj,
				 &cxgb3_attr_group);
	if (err) {
		dev_err(&pdev->dev, "cannot create sysfs group\n");
		goto out_close_led;
	}

	print_port_info(adapter, ai);
	return 0;

out_close_led:
	t3_set_reg_field(adapter, A_T3DBG_GPIO_EN, F_GPIO0_OUT_VAL, 0);

out_free_dev:
	iounmap(adapter->regs);
	for (i = ai->nports0 + ai->nports1 - 1; i >= 0; --i)
		if (adapter->port[i])
			free_netdev(adapter->port[i]);

out_free_adapter_nofail:
	kfree_skb(adapter->nofail_skb);

out_free_adapter:
	kfree(adapter);

out_release_regions:
	pci_release_regions(pdev);
out_disable_device:
	pci_disable_device(pdev);
out:
	return err;
}

static void remove_one(struct pci_dev *pdev)
{
	struct adapter *adapter = pci_get_drvdata(pdev);

	if (adapter) {
		int i;

		t3_sge_stop(adapter);
		sysfs_remove_group(&adapter->port[0]->dev.kobj,
				   &cxgb3_attr_group);

		if (is_offload(adapter)) {
			cxgb3_adapter_unofld(adapter);
			if (test_bit(OFFLOAD_DEVMAP_BIT,
				     &adapter->open_device_map))
				offload_close(&adapter->tdev);
		}

		for_each_port(adapter, i)
		    if (test_bit(i, &adapter->registered_device_map))
			unregister_netdev(adapter->port[i]);

		t3_stop_sge_timers(adapter);
		t3_free_sge_resources(adapter);
		cxgb_disable_msi(adapter);

		for_each_port(adapter, i)
			if (adapter->port[i])
				free_netdev(adapter->port[i]);

		iounmap(adapter->regs);
		kfree_skb(adapter->nofail_skb);
		kfree(adapter);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
	}
}

static struct pci_driver driver = {
	.name = DRV_NAME,
	.id_table = cxgb3_pci_tbl,
	.probe = init_one,
	.remove = remove_one,
	.err_handler = &t3_err_handler,
};

static int __init cxgb3_init_module(void)
{
	int ret;

	cxgb3_offload_init();

	ret = pci_register_driver(&driver);
	return ret;
}

static void __exit cxgb3_cleanup_module(void)
{
	pci_unregister_driver(&driver);
	if (cxgb3_wq)
		destroy_workqueue(cxgb3_wq);
}

module_init(cxgb3_init_module);
module_exit(cxgb3_cleanup_module);
