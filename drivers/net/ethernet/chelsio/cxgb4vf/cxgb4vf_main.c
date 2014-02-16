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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/debugfs.h>
#include <linux/ethtool.h>

#include "t4vf_common.h"
#include "t4vf_defs.h"

#include "../cxgb4/t4_regs.h"
#include "../cxgb4/t4_msg.h"

/*
 * Generic information about the driver.
 */
#define DRV_VERSION "2.0.0-ko"
#define DRV_DESC "Chelsio T4/T5 Virtual Function (VF) Network Driver"

/*
 * Module Parameters.
 * ==================
 */

/*
 * Default ethtool "message level" for adapters.
 */
#define DFLT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK | \
			 NETIF_MSG_TIMER | NETIF_MSG_IFDOWN | NETIF_MSG_IFUP |\
			 NETIF_MSG_RX_ERR | NETIF_MSG_TX_ERR)

static int dflt_msg_enable = DFLT_MSG_ENABLE;

module_param(dflt_msg_enable, int, 0644);
MODULE_PARM_DESC(dflt_msg_enable,
		 "default adapter ethtool message level bitmap");

/*
 * The driver uses the best interrupt scheme available on a platform in the
 * order MSI-X then MSI.  This parameter determines which of these schemes the
 * driver may consider as follows:
 *
 *     msi = 2: choose from among MSI-X and MSI
 *     msi = 1: only consider MSI interrupts
 *
 * Note that unlike the Physical Function driver, this Virtual Function driver
 * does _not_ support legacy INTx interrupts (this limitation is mandated by
 * the PCI-E SR-IOV standard).
 */
#define MSI_MSIX	2
#define MSI_MSI		1
#define MSI_DEFAULT	MSI_MSIX

static int msi = MSI_DEFAULT;

module_param(msi, int, 0644);
MODULE_PARM_DESC(msi, "whether to use MSI-X or MSI");

/*
 * Fundamental constants.
 * ======================
 */

enum {
	MAX_TXQ_ENTRIES		= 16384,
	MAX_RSPQ_ENTRIES	= 16384,
	MAX_RX_BUFFERS		= 16384,

	MIN_TXQ_ENTRIES		= 32,
	MIN_RSPQ_ENTRIES	= 128,
	MIN_FL_ENTRIES		= 16,

	/*
	 * For purposes of manipulating the Free List size we need to
	 * recognize that Free Lists are actually Egress Queues (the host
	 * produces free buffers which the hardware consumes), Egress Queues
	 * indices are all in units of Egress Context Units bytes, and free
	 * list entries are 64-bit PCI DMA addresses.  And since the state of
	 * the Producer Index == the Consumer Index implies an EMPTY list, we
	 * always have at least one Egress Unit's worth of Free List entries
	 * unused.  See sge.c for more details ...
	 */
	EQ_UNIT = SGE_EQ_IDXSIZE,
	FL_PER_EQ_UNIT = EQ_UNIT / sizeof(__be64),
	MIN_FL_RESID = FL_PER_EQ_UNIT,
};

/*
 * Global driver state.
 * ====================
 */

static struct dentry *cxgb4vf_debugfs_root;

/*
 * OS "Callback" functions.
 * ========================
 */

/*
 * The link status has changed on the indicated "port" (Virtual Interface).
 */
void t4vf_os_link_changed(struct adapter *adapter, int pidx, int link_ok)
{
	struct net_device *dev = adapter->port[pidx];

	/*
	 * If the port is disabled or the current recorded "link up"
	 * status matches the new status, just return.
	 */
	if (!netif_running(dev) || link_ok == netif_carrier_ok(dev))
		return;

	/*
	 * Tell the OS that the link status has changed and print a short
	 * informative message on the console about the event.
	 */
	if (link_ok) {
		const char *s;
		const char *fc;
		const struct port_info *pi = netdev_priv(dev);

		netif_carrier_on(dev);

		switch (pi->link_cfg.speed) {
		case SPEED_10000:
			s = "10Gbps";
			break;

		case SPEED_1000:
			s = "1000Mbps";
			break;

		case SPEED_100:
			s = "100Mbps";
			break;

		default:
			s = "unknown";
			break;
		}

		switch (pi->link_cfg.fc) {
		case PAUSE_RX:
			fc = "RX";
			break;

		case PAUSE_TX:
			fc = "TX";
			break;

		case PAUSE_RX|PAUSE_TX:
			fc = "RX/TX";
			break;

		default:
			fc = "no";
			break;
		}

		netdev_info(dev, "link up, %s, full-duplex, %s PAUSE\n", s, fc);
	} else {
		netif_carrier_off(dev);
		netdev_info(dev, "link down\n");
	}
}

/*
 * Net device operations.
 * ======================
 */




/*
 * Perform the MAC and PHY actions needed to enable a "port" (Virtual
 * Interface).
 */
static int link_start(struct net_device *dev)
{
	int ret;
	struct port_info *pi = netdev_priv(dev);

	/*
	 * We do not set address filters and promiscuity here, the stack does
	 * that step explicitly. Enable vlan accel.
	 */
	ret = t4vf_set_rxmode(pi->adapter, pi->viid, dev->mtu, -1, -1, -1, 1,
			      true);
	if (ret == 0) {
		ret = t4vf_change_mac(pi->adapter, pi->viid,
				      pi->xact_addr_filt, dev->dev_addr, true);
		if (ret >= 0) {
			pi->xact_addr_filt = ret;
			ret = 0;
		}
	}

	/*
	 * We don't need to actually "start the link" itself since the
	 * firmware will do that for us when the first Virtual Interface
	 * is enabled on a port.
	 */
	if (ret == 0)
		ret = t4vf_enable_vi(pi->adapter, pi->viid, true, true);
	return ret;
}

/*
 * Name the MSI-X interrupts.
 */
static void name_msix_vecs(struct adapter *adapter)
{
	int namelen = sizeof(adapter->msix_info[0].desc) - 1;
	int pidx;

	/*
	 * Firmware events.
	 */
	snprintf(adapter->msix_info[MSIX_FW].desc, namelen,
		 "%s-FWeventq", adapter->name);
	adapter->msix_info[MSIX_FW].desc[namelen] = 0;

	/*
	 * Ethernet queues.
	 */
	for_each_port(adapter, pidx) {
		struct net_device *dev = adapter->port[pidx];
		const struct port_info *pi = netdev_priv(dev);
		int qs, msi;

		for (qs = 0, msi = MSIX_IQFLINT; qs < pi->nqsets; qs++, msi++) {
			snprintf(adapter->msix_info[msi].desc, namelen,
				 "%s-%d", dev->name, qs);
			adapter->msix_info[msi].desc[namelen] = 0;
		}
	}
}

/*
 * Request all of our MSI-X resources.
 */
static int request_msix_queue_irqs(struct adapter *adapter)
{
	struct sge *s = &adapter->sge;
	int rxq, msi, err;

	/*
	 * Firmware events.
	 */
	err = request_irq(adapter->msix_info[MSIX_FW].vec, t4vf_sge_intr_msix,
			  0, adapter->msix_info[MSIX_FW].desc, &s->fw_evtq);
	if (err)
		return err;

	/*
	 * Ethernet queues.
	 */
	msi = MSIX_IQFLINT;
	for_each_ethrxq(s, rxq) {
		err = request_irq(adapter->msix_info[msi].vec,
				  t4vf_sge_intr_msix, 0,
				  adapter->msix_info[msi].desc,
				  &s->ethrxq[rxq].rspq);
		if (err)
			goto err_free_irqs;
		msi++;
	}
	return 0;

err_free_irqs:
	while (--rxq >= 0)
		free_irq(adapter->msix_info[--msi].vec, &s->ethrxq[rxq].rspq);
	free_irq(adapter->msix_info[MSIX_FW].vec, &s->fw_evtq);
	return err;
}

/*
 * Free our MSI-X resources.
 */
static void free_msix_queue_irqs(struct adapter *adapter)
{
	struct sge *s = &adapter->sge;
	int rxq, msi;

	free_irq(adapter->msix_info[MSIX_FW].vec, &s->fw_evtq);
	msi = MSIX_IQFLINT;
	for_each_ethrxq(s, rxq)
		free_irq(adapter->msix_info[msi++].vec,
			 &s->ethrxq[rxq].rspq);
}

/*
 * Turn on NAPI and start up interrupts on a response queue.
 */
static void qenable(struct sge_rspq *rspq)
{
	napi_enable(&rspq->napi);

	/*
	 * 0-increment the Going To Sleep register to start the timer and
	 * enable interrupts.
	 */
	t4_write_reg(rspq->adapter, T4VF_SGE_BASE_ADDR + SGE_VF_GTS,
		     CIDXINC(0) |
		     SEINTARM(rspq->intr_params) |
		     INGRESSQID(rspq->cntxt_id));
}

/*
 * Enable NAPI scheduling and interrupt generation for all Receive Queues.
 */
static void enable_rx(struct adapter *adapter)
{
	int rxq;
	struct sge *s = &adapter->sge;

	for_each_ethrxq(s, rxq)
		qenable(&s->ethrxq[rxq].rspq);
	qenable(&s->fw_evtq);

	/*
	 * The interrupt queue doesn't use NAPI so we do the 0-increment of
	 * its Going To Sleep register here to get it started.
	 */
	if (adapter->flags & USING_MSI)
		t4_write_reg(adapter, T4VF_SGE_BASE_ADDR + SGE_VF_GTS,
			     CIDXINC(0) |
			     SEINTARM(s->intrq.intr_params) |
			     INGRESSQID(s->intrq.cntxt_id));

}

/*
 * Wait until all NAPI handlers are descheduled.
 */
static void quiesce_rx(struct adapter *adapter)
{
	struct sge *s = &adapter->sge;
	int rxq;

	for_each_ethrxq(s, rxq)
		napi_disable(&s->ethrxq[rxq].rspq.napi);
	napi_disable(&s->fw_evtq.napi);
}

/*
 * Response queue handler for the firmware event queue.
 */
static int fwevtq_handler(struct sge_rspq *rspq, const __be64 *rsp,
			  const struct pkt_gl *gl)
{
	/*
	 * Extract response opcode and get pointer to CPL message body.
	 */
	struct adapter *adapter = rspq->adapter;
	u8 opcode = ((const struct rss_header *)rsp)->opcode;
	void *cpl = (void *)(rsp + 1);

	switch (opcode) {
	case CPL_FW6_MSG: {
		/*
		 * We've received an asynchronous message from the firmware.
		 */
		const struct cpl_fw6_msg *fw_msg = cpl;
		if (fw_msg->type == FW6_TYPE_CMD_RPL)
			t4vf_handle_fw_rpl(adapter, fw_msg->data);
		break;
	}

	case CPL_FW4_MSG: {
		/* FW can send EGR_UPDATEs encapsulated in a CPL_FW4_MSG.
		 */
		const struct cpl_sge_egr_update *p = (void *)(rsp + 3);
		opcode = G_CPL_OPCODE(ntohl(p->opcode_qid));
		if (opcode != CPL_SGE_EGR_UPDATE) {
			dev_err(adapter->pdev_dev, "unexpected FW4/CPL %#x on FW event queue\n"
				, opcode);
			break;
		}
		cpl = (void *)p;
		/*FALLTHROUGH*/
	}

	case CPL_SGE_EGR_UPDATE: {
		/*
		 * We've received an Egress Queue Status Update message.  We
		 * get these, if the SGE is configured to send these when the
		 * firmware passes certain points in processing our TX
		 * Ethernet Queue or if we make an explicit request for one.
		 * We use these updates to determine when we may need to
		 * restart a TX Ethernet Queue which was stopped for lack of
		 * free TX Queue Descriptors ...
		 */
		const struct cpl_sge_egr_update *p = cpl;
		unsigned int qid = EGR_QID(be32_to_cpu(p->opcode_qid));
		struct sge *s = &adapter->sge;
		struct sge_txq *tq;
		struct sge_eth_txq *txq;
		unsigned int eq_idx;

		/*
		 * Perform sanity checking on the Queue ID to make sure it
		 * really refers to one of our TX Ethernet Egress Queues which
		 * is active and matches the queue's ID.  None of these error
		 * conditions should ever happen so we may want to either make
		 * them fatal and/or conditionalized under DEBUG.
		 */
		eq_idx = EQ_IDX(s, qid);
		if (unlikely(eq_idx >= MAX_EGRQ)) {
			dev_err(adapter->pdev_dev,
				"Egress Update QID %d out of range\n", qid);
			break;
		}
		tq = s->egr_map[eq_idx];
		if (unlikely(tq == NULL)) {
			dev_err(adapter->pdev_dev,
				"Egress Update QID %d TXQ=NULL\n", qid);
			break;
		}
		txq = container_of(tq, struct sge_eth_txq, q);
		if (unlikely(tq->abs_id != qid)) {
			dev_err(adapter->pdev_dev,
				"Egress Update QID %d refers to TXQ %d\n",
				qid, tq->abs_id);
			break;
		}

		/*
		 * Restart a stopped TX Queue which has less than half of its
		 * TX ring in use ...
		 */
		txq->q.restarts++;
		netif_tx_wake_queue(txq->txq);
		break;
	}

	default:
		dev_err(adapter->pdev_dev,
			"unexpected CPL %#x on FW event queue\n", opcode);
	}

	return 0;
}

/*
 * Allocate SGE TX/RX response queues.  Determine how many sets of SGE queues
 * to use and initializes them.  We support multiple "Queue Sets" per port if
 * we have MSI-X, otherwise just one queue set per port.
 */
static int setup_sge_queues(struct adapter *adapter)
{
	struct sge *s = &adapter->sge;
	int err, pidx, msix;

	/*
	 * Clear "Queue Set" Free List Starving and TX Queue Mapping Error
	 * state.
	 */
	bitmap_zero(s->starving_fl, MAX_EGRQ);

	/*
	 * If we're using MSI interrupt mode we need to set up a "forwarded
	 * interrupt" queue which we'll set up with our MSI vector.  The rest
	 * of the ingress queues will be set up to forward their interrupts to
	 * this queue ...  This must be first since t4vf_sge_alloc_rxq() uses
	 * the intrq's queue ID as the interrupt forwarding queue for the
	 * subsequent calls ...
	 */
	if (adapter->flags & USING_MSI) {
		err = t4vf_sge_alloc_rxq(adapter, &s->intrq, false,
					 adapter->port[0], 0, NULL, NULL);
		if (err)
			goto err_free_queues;
	}

	/*
	 * Allocate our ingress queue for asynchronous firmware messages.
	 */
	err = t4vf_sge_alloc_rxq(adapter, &s->fw_evtq, true, adapter->port[0],
				 MSIX_FW, NULL, fwevtq_handler);
	if (err)
		goto err_free_queues;

	/*
	 * Allocate each "port"'s initial Queue Sets.  These can be changed
	 * later on ... up to the point where any interface on the adapter is
	 * brought up at which point lots of things get nailed down
	 * permanently ...
	 */
	msix = MSIX_IQFLINT;
	for_each_port(adapter, pidx) {
		struct net_device *dev = adapter->port[pidx];
		struct port_info *pi = netdev_priv(dev);
		struct sge_eth_rxq *rxq = &s->ethrxq[pi->first_qset];
		struct sge_eth_txq *txq = &s->ethtxq[pi->first_qset];
		int qs;

		for (qs = 0; qs < pi->nqsets; qs++, rxq++, txq++) {
			err = t4vf_sge_alloc_rxq(adapter, &rxq->rspq, false,
						 dev, msix++,
						 &rxq->fl, t4vf_ethrx_handler);
			if (err)
				goto err_free_queues;

			err = t4vf_sge_alloc_eth_txq(adapter, txq, dev,
					     netdev_get_tx_queue(dev, qs),
					     s->fw_evtq.cntxt_id);
			if (err)
				goto err_free_queues;

			rxq->rspq.idx = qs;
			memset(&rxq->stats, 0, sizeof(rxq->stats));
		}
	}

	/*
	 * Create the reverse mappings for the queues.
	 */
	s->egr_base = s->ethtxq[0].q.abs_id - s->ethtxq[0].q.cntxt_id;
	s->ingr_base = s->ethrxq[0].rspq.abs_id - s->ethrxq[0].rspq.cntxt_id;
	IQ_MAP(s, s->fw_evtq.abs_id) = &s->fw_evtq;
	for_each_port(adapter, pidx) {
		struct net_device *dev = adapter->port[pidx];
		struct port_info *pi = netdev_priv(dev);
		struct sge_eth_rxq *rxq = &s->ethrxq[pi->first_qset];
		struct sge_eth_txq *txq = &s->ethtxq[pi->first_qset];
		int qs;

		for (qs = 0; qs < pi->nqsets; qs++, rxq++, txq++) {
			IQ_MAP(s, rxq->rspq.abs_id) = &rxq->rspq;
			EQ_MAP(s, txq->q.abs_id) = &txq->q;

			/*
			 * The FW_IQ_CMD doesn't return the Absolute Queue IDs
			 * for Free Lists but since all of the Egress Queues
			 * (including Free Lists) have Relative Queue IDs
			 * which are computed as Absolute - Base Queue ID, we
			 * can synthesize the Absolute Queue IDs for the Free
			 * Lists.  This is useful for debugging purposes when
			 * we want to dump Queue Contexts via the PF Driver.
			 */
			rxq->fl.abs_id = rxq->fl.cntxt_id + s->egr_base;
			EQ_MAP(s, rxq->fl.abs_id) = &rxq->fl;
		}
	}
	return 0;

err_free_queues:
	t4vf_free_sge_resources(adapter);
	return err;
}

/*
 * Set up Receive Side Scaling (RSS) to distribute packets to multiple receive
 * queues.  We configure the RSS CPU lookup table to distribute to the number
 * of HW receive queues, and the response queue lookup table to narrow that
 * down to the response queues actually configured for each "port" (Virtual
 * Interface).  We always configure the RSS mapping for all ports since the
 * mapping table has plenty of entries.
 */
static int setup_rss(struct adapter *adapter)
{
	int pidx;

	for_each_port(adapter, pidx) {
		struct port_info *pi = adap2pinfo(adapter, pidx);
		struct sge_eth_rxq *rxq = &adapter->sge.ethrxq[pi->first_qset];
		u16 rss[MAX_PORT_QSETS];
		int qs, err;

		for (qs = 0; qs < pi->nqsets; qs++)
			rss[qs] = rxq[qs].rspq.abs_id;

		err = t4vf_config_rss_range(adapter, pi->viid,
					    0, pi->rss_size, rss, pi->nqsets);
		if (err)
			return err;

		/*
		 * Perform Global RSS Mode-specific initialization.
		 */
		switch (adapter->params.rss.mode) {
		case FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL:
			/*
			 * If Tunnel All Lookup isn't specified in the global
			 * RSS Configuration, then we need to specify a
			 * default Ingress Queue for any ingress packets which
			 * aren't hashed.  We'll use our first ingress queue
			 * ...
			 */
			if (!adapter->params.rss.u.basicvirtual.tnlalllookup) {
				union rss_vi_config config;
				err = t4vf_read_rss_vi_config(adapter,
							      pi->viid,
							      &config);
				if (err)
					return err;
				config.basicvirtual.defaultq =
					rxq[0].rspq.abs_id;
				err = t4vf_write_rss_vi_config(adapter,
							       pi->viid,
							       &config);
				if (err)
					return err;
			}
			break;
		}
	}

	return 0;
}

/*
 * Bring the adapter up.  Called whenever we go from no "ports" open to having
 * one open.  This function performs the actions necessary to make an adapter
 * operational, such as completing the initialization of HW modules, and
 * enabling interrupts.  Must be called with the rtnl lock held.  (Note that
 * this is called "cxgb_up" in the PF Driver.)
 */
static int adapter_up(struct adapter *adapter)
{
	int err;

	/*
	 * If this is the first time we've been called, perform basic
	 * adapter setup.  Once we've done this, many of our adapter
	 * parameters can no longer be changed ...
	 */
	if ((adapter->flags & FULL_INIT_DONE) == 0) {
		err = setup_sge_queues(adapter);
		if (err)
			return err;
		err = setup_rss(adapter);
		if (err) {
			t4vf_free_sge_resources(adapter);
			return err;
		}

		if (adapter->flags & USING_MSIX)
			name_msix_vecs(adapter);
		adapter->flags |= FULL_INIT_DONE;
	}

	/*
	 * Acquire our interrupt resources.  We only support MSI-X and MSI.
	 */
	BUG_ON((adapter->flags & (USING_MSIX|USING_MSI)) == 0);
	if (adapter->flags & USING_MSIX)
		err = request_msix_queue_irqs(adapter);
	else
		err = request_irq(adapter->pdev->irq,
				  t4vf_intr_handler(adapter), 0,
				  adapter->name, adapter);
	if (err) {
		dev_err(adapter->pdev_dev, "request_irq failed, err %d\n",
			err);
		return err;
	}

	/*
	 * Enable NAPI ingress processing and return success.
	 */
	enable_rx(adapter);
	t4vf_sge_start(adapter);
	return 0;
}

/*
 * Bring the adapter down.  Called whenever the last "port" (Virtual
 * Interface) closed.  (Note that this routine is called "cxgb_down" in the PF
 * Driver.)
 */
static void adapter_down(struct adapter *adapter)
{
	/*
	 * Free interrupt resources.
	 */
	if (adapter->flags & USING_MSIX)
		free_msix_queue_irqs(adapter);
	else
		free_irq(adapter->pdev->irq, adapter);

	/*
	 * Wait for NAPI handlers to finish.
	 */
	quiesce_rx(adapter);
}

/*
 * Start up a net device.
 */
static int cxgb4vf_open(struct net_device *dev)
{
	int err;
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	/*
	 * If this is the first interface that we're opening on the "adapter",
	 * bring the "adapter" up now.
	 */
	if (adapter->open_device_map == 0) {
		err = adapter_up(adapter);
		if (err)
			return err;
	}

	/*
	 * Note that this interface is up and start everything up ...
	 */
	netif_set_real_num_tx_queues(dev, pi->nqsets);
	err = netif_set_real_num_rx_queues(dev, pi->nqsets);
	if (err)
		goto err_unwind;
	err = link_start(dev);
	if (err)
		goto err_unwind;

	netif_tx_start_all_queues(dev);
	set_bit(pi->port_id, &adapter->open_device_map);
	return 0;

err_unwind:
	if (adapter->open_device_map == 0)
		adapter_down(adapter);
	return err;
}

/*
 * Shut down a net device.  This routine is called "cxgb_close" in the PF
 * Driver ...
 */
static int cxgb4vf_stop(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	netif_tx_stop_all_queues(dev);
	netif_carrier_off(dev);
	t4vf_enable_vi(adapter, pi->viid, false, false);
	pi->link_cfg.link_ok = 0;

	clear_bit(pi->port_id, &adapter->open_device_map);
	if (adapter->open_device_map == 0)
		adapter_down(adapter);
	return 0;
}

/*
 * Translate our basic statistics into the standard "ifconfig" statistics.
 */
static struct net_device_stats *cxgb4vf_get_stats(struct net_device *dev)
{
	struct t4vf_port_stats stats;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adapter = pi->adapter;
	struct net_device_stats *ns = &dev->stats;
	int err;

	spin_lock(&adapter->stats_lock);
	err = t4vf_get_port_stats(adapter, pi->pidx, &stats);
	spin_unlock(&adapter->stats_lock);

	memset(ns, 0, sizeof(*ns));
	if (err)
		return ns;

	ns->tx_bytes = (stats.tx_bcast_bytes + stats.tx_mcast_bytes +
			stats.tx_ucast_bytes + stats.tx_offload_bytes);
	ns->tx_packets = (stats.tx_bcast_frames + stats.tx_mcast_frames +
			  stats.tx_ucast_frames + stats.tx_offload_frames);
	ns->rx_bytes = (stats.rx_bcast_bytes + stats.rx_mcast_bytes +
			stats.rx_ucast_bytes);
	ns->rx_packets = (stats.rx_bcast_frames + stats.rx_mcast_frames +
			  stats.rx_ucast_frames);
	ns->multicast = stats.rx_mcast_frames;
	ns->tx_errors = stats.tx_drop_frames;
	ns->rx_errors = stats.rx_err_frames;

	return ns;
}

/*
 * Collect up to maxaddrs worth of a netdevice's unicast addresses, starting
 * at a specified offset within the list, into an array of addrss pointers and
 * return the number collected.
 */
static inline unsigned int collect_netdev_uc_list_addrs(const struct net_device *dev,
							const u8 **addr,
							unsigned int offset,
							unsigned int maxaddrs)
{
	unsigned int index = 0;
	unsigned int naddr = 0;
	const struct netdev_hw_addr *ha;

	for_each_dev_addr(dev, ha)
		if (index++ >= offset) {
			addr[naddr++] = ha->addr;
			if (naddr >= maxaddrs)
				break;
		}
	return naddr;
}

/*
 * Collect up to maxaddrs worth of a netdevice's multicast addresses, starting
 * at a specified offset within the list, into an array of addrss pointers and
 * return the number collected.
 */
static inline unsigned int collect_netdev_mc_list_addrs(const struct net_device *dev,
							const u8 **addr,
							unsigned int offset,
							unsigned int maxaddrs)
{
	unsigned int index = 0;
	unsigned int naddr = 0;
	const struct netdev_hw_addr *ha;

	netdev_for_each_mc_addr(ha, dev)
		if (index++ >= offset) {
			addr[naddr++] = ha->addr;
			if (naddr >= maxaddrs)
				break;
		}
	return naddr;
}

/*
 * Configure the exact and hash address filters to handle a port's multicast
 * and secondary unicast MAC addresses.
 */
static int set_addr_filters(const struct net_device *dev, bool sleep)
{
	u64 mhash = 0;
	u64 uhash = 0;
	bool free = true;
	unsigned int offset, naddr;
	const u8 *addr[7];
	int ret;
	const struct port_info *pi = netdev_priv(dev);

	/* first do the secondary unicast addresses */
	for (offset = 0; ; offset += naddr) {
		naddr = collect_netdev_uc_list_addrs(dev, addr, offset,
						     ARRAY_SIZE(addr));
		if (naddr == 0)
			break;

		ret = t4vf_alloc_mac_filt(pi->adapter, pi->viid, free,
					  naddr, addr, NULL, &uhash, sleep);
		if (ret < 0)
			return ret;

		free = false;
	}

	/* next set up the multicast addresses */
	for (offset = 0; ; offset += naddr) {
		naddr = collect_netdev_mc_list_addrs(dev, addr, offset,
						     ARRAY_SIZE(addr));
		if (naddr == 0)
			break;

		ret = t4vf_alloc_mac_filt(pi->adapter, pi->viid, free,
					  naddr, addr, NULL, &mhash, sleep);
		if (ret < 0)
			return ret;
		free = false;
	}

	return t4vf_set_addr_hash(pi->adapter, pi->viid, uhash != 0,
				  uhash | mhash, sleep);
}

/*
 * Set RX properties of a port, such as promiscruity, address filters, and MTU.
 * If @mtu is -1 it is left unchanged.
 */
static int set_rxmode(struct net_device *dev, int mtu, bool sleep_ok)
{
	int ret;
	struct port_info *pi = netdev_priv(dev);

	ret = set_addr_filters(dev, sleep_ok);
	if (ret == 0)
		ret = t4vf_set_rxmode(pi->adapter, pi->viid, -1,
				      (dev->flags & IFF_PROMISC) != 0,
				      (dev->flags & IFF_ALLMULTI) != 0,
				      1, -1, sleep_ok);
	return ret;
}

/*
 * Set the current receive modes on the device.
 */
static void cxgb4vf_set_rxmode(struct net_device *dev)
{
	/* unfortunately we can't return errors to the stack */
	set_rxmode(dev, -1, false);
}

/*
 * Find the entry in the interrupt holdoff timer value array which comes
 * closest to the specified interrupt holdoff value.
 */
static int closest_timer(const struct sge *s, int us)
{
	int i, timer_idx = 0, min_delta = INT_MAX;

	for (i = 0; i < ARRAY_SIZE(s->timer_val); i++) {
		int delta = us - s->timer_val[i];
		if (delta < 0)
			delta = -delta;
		if (delta < min_delta) {
			min_delta = delta;
			timer_idx = i;
		}
	}
	return timer_idx;
}

static int closest_thres(const struct sge *s, int thres)
{
	int i, delta, pktcnt_idx = 0, min_delta = INT_MAX;

	for (i = 0; i < ARRAY_SIZE(s->counter_val); i++) {
		delta = thres - s->counter_val[i];
		if (delta < 0)
			delta = -delta;
		if (delta < min_delta) {
			min_delta = delta;
			pktcnt_idx = i;
		}
	}
	return pktcnt_idx;
}

/*
 * Return a queue's interrupt hold-off time in us.  0 means no timer.
 */
static unsigned int qtimer_val(const struct adapter *adapter,
			       const struct sge_rspq *rspq)
{
	unsigned int timer_idx = QINTR_TIMER_IDX_GET(rspq->intr_params);

	return timer_idx < SGE_NTIMERS
		? adapter->sge.timer_val[timer_idx]
		: 0;
}

/**
 *	set_rxq_intr_params - set a queue's interrupt holdoff parameters
 *	@adapter: the adapter
 *	@rspq: the RX response queue
 *	@us: the hold-off time in us, or 0 to disable timer
 *	@cnt: the hold-off packet count, or 0 to disable counter
 *
 *	Sets an RX response queue's interrupt hold-off time and packet count.
 *	At least one of the two needs to be enabled for the queue to generate
 *	interrupts.
 */
static int set_rxq_intr_params(struct adapter *adapter, struct sge_rspq *rspq,
			       unsigned int us, unsigned int cnt)
{
	unsigned int timer_idx;

	/*
	 * If both the interrupt holdoff timer and count are specified as
	 * zero, default to a holdoff count of 1 ...
	 */
	if ((us | cnt) == 0)
		cnt = 1;

	/*
	 * If an interrupt holdoff count has been specified, then find the
	 * closest configured holdoff count and use that.  If the response
	 * queue has already been created, then update its queue context
	 * parameters ...
	 */
	if (cnt) {
		int err;
		u32 v, pktcnt_idx;

		pktcnt_idx = closest_thres(&adapter->sge, cnt);
		if (rspq->desc && rspq->pktcnt_idx != pktcnt_idx) {
			v = FW_PARAMS_MNEM(FW_PARAMS_MNEM_DMAQ) |
			    FW_PARAMS_PARAM_X(
					FW_PARAMS_PARAM_DMAQ_IQ_INTCNTTHRESH) |
			    FW_PARAMS_PARAM_YZ(rspq->cntxt_id);
			err = t4vf_set_params(adapter, 1, &v, &pktcnt_idx);
			if (err)
				return err;
		}
		rspq->pktcnt_idx = pktcnt_idx;
	}

	/*
	 * Compute the closest holdoff timer index from the supplied holdoff
	 * timer value.
	 */
	timer_idx = (us == 0
		     ? SGE_TIMER_RSTRT_CNTR
		     : closest_timer(&adapter->sge, us));

	/*
	 * Update the response queue's interrupt coalescing parameters and
	 * return success.
	 */
	rspq->intr_params = (QINTR_TIMER_IDX(timer_idx) |
			     (cnt > 0 ? QINTR_CNT_EN : 0));
	return 0;
}

/*
 * Return a version number to identify the type of adapter.  The scheme is:
 * - bits 0..9: chip version
 * - bits 10..15: chip revision
 */
static inline unsigned int mk_adap_vers(const struct adapter *adapter)
{
	/*
	 * Chip version 4, revision 0x3f (cxgb4vf).
	 */
	return CHELSIO_CHIP_VERSION(adapter->params.chip) | (0x3f << 10);
}

/*
 * Execute the specified ioctl command.
 */
static int cxgb4vf_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int ret = 0;

	switch (cmd) {
	    /*
	     * The VF Driver doesn't have access to any of the other
	     * common Ethernet device ioctl()'s (like reading/writing
	     * PHY registers, etc.
	     */

	default:
		ret = -EOPNOTSUPP;
		break;
	}
	return ret;
}

/*
 * Change the device's MTU.
 */
static int cxgb4vf_change_mtu(struct net_device *dev, int new_mtu)
{
	int ret;
	struct port_info *pi = netdev_priv(dev);

	/* accommodate SACK */
	if (new_mtu < 81)
		return -EINVAL;

	ret = t4vf_set_rxmode(pi->adapter, pi->viid, new_mtu,
			      -1, -1, -1, -1, true);
	if (!ret)
		dev->mtu = new_mtu;
	return ret;
}

static netdev_features_t cxgb4vf_fix_features(struct net_device *dev,
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

static int cxgb4vf_set_features(struct net_device *dev,
	netdev_features_t features)
{
	struct port_info *pi = netdev_priv(dev);
	netdev_features_t changed = dev->features ^ features;

	if (changed & NETIF_F_HW_VLAN_CTAG_RX)
		t4vf_set_rxmode(pi->adapter, pi->viid, -1, -1, -1, -1,
				features & NETIF_F_HW_VLAN_CTAG_TX, 0);

	return 0;
}

/*
 * Change the devices MAC address.
 */
static int cxgb4vf_set_mac_addr(struct net_device *dev, void *_addr)
{
	int ret;
	struct sockaddr *addr = _addr;
	struct port_info *pi = netdev_priv(dev);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	ret = t4vf_change_mac(pi->adapter, pi->viid, pi->xact_addr_filt,
			      addr->sa_data, true);
	if (ret < 0)
		return ret;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	pi->xact_addr_filt = ret;
	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Poll all of our receive queues.  This is called outside of normal interrupt
 * context.
 */
static void cxgb4vf_poll_controller(struct net_device *dev)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	if (adapter->flags & USING_MSIX) {
		struct sge_eth_rxq *rxq;
		int nqsets;

		rxq = &adapter->sge.ethrxq[pi->first_qset];
		for (nqsets = pi->nqsets; nqsets; nqsets--) {
			t4vf_sge_intr_msix(0, &rxq->rspq);
			rxq++;
		}
	} else
		t4vf_intr_handler(adapter)(0, adapter);
}
#endif

/*
 * Ethtool operations.
 * ===================
 *
 * Note that we don't support any ethtool operations which change the physical
 * state of the port to which we're linked.
 */

/*
 * Return current port link settings.
 */
static int cxgb4vf_get_settings(struct net_device *dev,
				struct ethtool_cmd *cmd)
{
	const struct port_info *pi = netdev_priv(dev);

	cmd->supported = pi->link_cfg.supported;
	cmd->advertising = pi->link_cfg.advertising;
	ethtool_cmd_speed_set(cmd,
			      netif_carrier_ok(dev) ? pi->link_cfg.speed : -1);
	cmd->duplex = DUPLEX_FULL;

	cmd->port = (cmd->supported & SUPPORTED_TP) ? PORT_TP : PORT_FIBRE;
	cmd->phy_address = pi->port_id;
	cmd->transceiver = XCVR_EXTERNAL;
	cmd->autoneg = pi->link_cfg.autoneg;
	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 0;
	return 0;
}

/*
 * Return our driver information.
 */
static void cxgb4vf_get_drvinfo(struct net_device *dev,
				struct ethtool_drvinfo *drvinfo)
{
	struct adapter *adapter = netdev2adap(dev);

	strlcpy(drvinfo->driver, KBUILD_MODNAME, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->bus_info, pci_name(to_pci_dev(dev->dev.parent)),
		sizeof(drvinfo->bus_info));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%u.%u.%u.%u, TP %u.%u.%u.%u",
		 FW_HDR_FW_VER_MAJOR_GET(adapter->params.dev.fwrev),
		 FW_HDR_FW_VER_MINOR_GET(adapter->params.dev.fwrev),
		 FW_HDR_FW_VER_MICRO_GET(adapter->params.dev.fwrev),
		 FW_HDR_FW_VER_BUILD_GET(adapter->params.dev.fwrev),
		 FW_HDR_FW_VER_MAJOR_GET(adapter->params.dev.tprev),
		 FW_HDR_FW_VER_MINOR_GET(adapter->params.dev.tprev),
		 FW_HDR_FW_VER_MICRO_GET(adapter->params.dev.tprev),
		 FW_HDR_FW_VER_BUILD_GET(adapter->params.dev.tprev));
}

/*
 * Return current adapter message level.
 */
static u32 cxgb4vf_get_msglevel(struct net_device *dev)
{
	return netdev2adap(dev)->msg_enable;
}

/*
 * Set current adapter message level.
 */
static void cxgb4vf_set_msglevel(struct net_device *dev, u32 msglevel)
{
	netdev2adap(dev)->msg_enable = msglevel;
}

/*
 * Return the device's current Queue Set ring size parameters along with the
 * allowed maximum values.  Since ethtool doesn't understand the concept of
 * multi-queue devices, we just return the current values associated with the
 * first Queue Set.
 */
static void cxgb4vf_get_ringparam(struct net_device *dev,
				  struct ethtool_ringparam *rp)
{
	const struct port_info *pi = netdev_priv(dev);
	const struct sge *s = &pi->adapter->sge;

	rp->rx_max_pending = MAX_RX_BUFFERS;
	rp->rx_mini_max_pending = MAX_RSPQ_ENTRIES;
	rp->rx_jumbo_max_pending = 0;
	rp->tx_max_pending = MAX_TXQ_ENTRIES;

	rp->rx_pending = s->ethrxq[pi->first_qset].fl.size - MIN_FL_RESID;
	rp->rx_mini_pending = s->ethrxq[pi->first_qset].rspq.size;
	rp->rx_jumbo_pending = 0;
	rp->tx_pending = s->ethtxq[pi->first_qset].q.size;
}

/*
 * Set the Queue Set ring size parameters for the device.  Again, since
 * ethtool doesn't allow for the concept of multiple queues per device, we'll
 * apply these new values across all of the Queue Sets associated with the
 * device -- after vetting them of course!
 */
static int cxgb4vf_set_ringparam(struct net_device *dev,
				 struct ethtool_ringparam *rp)
{
	const struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	struct sge *s = &adapter->sge;
	int qs;

	if (rp->rx_pending > MAX_RX_BUFFERS ||
	    rp->rx_jumbo_pending ||
	    rp->tx_pending > MAX_TXQ_ENTRIES ||
	    rp->rx_mini_pending > MAX_RSPQ_ENTRIES ||
	    rp->rx_mini_pending < MIN_RSPQ_ENTRIES ||
	    rp->rx_pending < MIN_FL_ENTRIES ||
	    rp->tx_pending < MIN_TXQ_ENTRIES)
		return -EINVAL;

	if (adapter->flags & FULL_INIT_DONE)
		return -EBUSY;

	for (qs = pi->first_qset; qs < pi->first_qset + pi->nqsets; qs++) {
		s->ethrxq[qs].fl.size = rp->rx_pending + MIN_FL_RESID;
		s->ethrxq[qs].rspq.size = rp->rx_mini_pending;
		s->ethtxq[qs].q.size = rp->tx_pending;
	}
	return 0;
}

/*
 * Return the interrupt holdoff timer and count for the first Queue Set on the
 * device.  Our extension ioctl() (the cxgbtool interface) allows the
 * interrupt holdoff timer to be read on all of the device's Queue Sets.
 */
static int cxgb4vf_get_coalesce(struct net_device *dev,
				struct ethtool_coalesce *coalesce)
{
	const struct port_info *pi = netdev_priv(dev);
	const struct adapter *adapter = pi->adapter;
	const struct sge_rspq *rspq = &adapter->sge.ethrxq[pi->first_qset].rspq;

	coalesce->rx_coalesce_usecs = qtimer_val(adapter, rspq);
	coalesce->rx_max_coalesced_frames =
		((rspq->intr_params & QINTR_CNT_EN)
		 ? adapter->sge.counter_val[rspq->pktcnt_idx]
		 : 0);
	return 0;
}

/*
 * Set the RX interrupt holdoff timer and count for the first Queue Set on the
 * interface.  Our extension ioctl() (the cxgbtool interface) allows us to set
 * the interrupt holdoff timer on any of the device's Queue Sets.
 */
static int cxgb4vf_set_coalesce(struct net_device *dev,
				struct ethtool_coalesce *coalesce)
{
	const struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;

	return set_rxq_intr_params(adapter,
				   &adapter->sge.ethrxq[pi->first_qset].rspq,
				   coalesce->rx_coalesce_usecs,
				   coalesce->rx_max_coalesced_frames);
}

/*
 * Report current port link pause parameter settings.
 */
static void cxgb4vf_get_pauseparam(struct net_device *dev,
				   struct ethtool_pauseparam *pauseparam)
{
	struct port_info *pi = netdev_priv(dev);

	pauseparam->autoneg = (pi->link_cfg.requested_fc & PAUSE_AUTONEG) != 0;
	pauseparam->rx_pause = (pi->link_cfg.fc & PAUSE_RX) != 0;
	pauseparam->tx_pause = (pi->link_cfg.fc & PAUSE_TX) != 0;
}

/*
 * Identify the port by blinking the port's LED.
 */
static int cxgb4vf_phys_id(struct net_device *dev,
			   enum ethtool_phys_id_state state)
{
	unsigned int val;
	struct port_info *pi = netdev_priv(dev);

	if (state == ETHTOOL_ID_ACTIVE)
		val = 0xffff;
	else if (state == ETHTOOL_ID_INACTIVE)
		val = 0;
	else
		return -EINVAL;

	return t4vf_identify_port(pi->adapter, pi->viid, val);
}

/*
 * Port stats maintained per queue of the port.
 */
struct queue_port_stats {
	u64 tso;
	u64 tx_csum;
	u64 rx_csum;
	u64 vlan_ex;
	u64 vlan_ins;
	u64 lro_pkts;
	u64 lro_merged;
};

/*
 * Strings for the ETH_SS_STATS statistics set ("ethtool -S").  Note that
 * these need to match the order of statistics returned by
 * t4vf_get_port_stats().
 */
static const char stats_strings[][ETH_GSTRING_LEN] = {
	/*
	 * These must match the layout of the t4vf_port_stats structure.
	 */
	"TxBroadcastBytes  ",
	"TxBroadcastFrames ",
	"TxMulticastBytes  ",
	"TxMulticastFrames ",
	"TxUnicastBytes    ",
	"TxUnicastFrames   ",
	"TxDroppedFrames   ",
	"TxOffloadBytes    ",
	"TxOffloadFrames   ",
	"RxBroadcastBytes  ",
	"RxBroadcastFrames ",
	"RxMulticastBytes  ",
	"RxMulticastFrames ",
	"RxUnicastBytes    ",
	"RxUnicastFrames   ",
	"RxErrorFrames     ",

	/*
	 * These are accumulated per-queue statistics and must match the
	 * order of the fields in the queue_port_stats structure.
	 */
	"TSO               ",
	"TxCsumOffload     ",
	"RxCsumGood        ",
	"VLANextractions   ",
	"VLANinsertions    ",
	"GROPackets        ",
	"GROMerged         ",
};

/*
 * Return the number of statistics in the specified statistics set.
 */
static int cxgb4vf_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(stats_strings);
	default:
		return -EOPNOTSUPP;
	}
	/*NOTREACHED*/
}

/*
 * Return the strings for the specified statistics set.
 */
static void cxgb4vf_get_strings(struct net_device *dev,
				u32 sset,
				u8 *data)
{
	switch (sset) {
	case ETH_SS_STATS:
		memcpy(data, stats_strings, sizeof(stats_strings));
		break;
	}
}

/*
 * Small utility routine to accumulate queue statistics across the queues of
 * a "port".
 */
static void collect_sge_port_stats(const struct adapter *adapter,
				   const struct port_info *pi,
				   struct queue_port_stats *stats)
{
	const struct sge_eth_txq *txq = &adapter->sge.ethtxq[pi->first_qset];
	const struct sge_eth_rxq *rxq = &adapter->sge.ethrxq[pi->first_qset];
	int qs;

	memset(stats, 0, sizeof(*stats));
	for (qs = 0; qs < pi->nqsets; qs++, rxq++, txq++) {
		stats->tso += txq->tso;
		stats->tx_csum += txq->tx_cso;
		stats->rx_csum += rxq->stats.rx_cso;
		stats->vlan_ex += rxq->stats.vlan_ex;
		stats->vlan_ins += txq->vlan_ins;
		stats->lro_pkts += rxq->stats.lro_pkts;
		stats->lro_merged += rxq->stats.lro_merged;
	}
}

/*
 * Return the ETH_SS_STATS statistics set.
 */
static void cxgb4vf_get_ethtool_stats(struct net_device *dev,
				      struct ethtool_stats *stats,
				      u64 *data)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adapter = pi->adapter;
	int err = t4vf_get_port_stats(adapter, pi->pidx,
				      (struct t4vf_port_stats *)data);
	if (err)
		memset(data, 0, sizeof(struct t4vf_port_stats));

	data += sizeof(struct t4vf_port_stats) / sizeof(u64);
	collect_sge_port_stats(adapter, pi, (struct queue_port_stats *)data);
}

/*
 * Return the size of our register map.
 */
static int cxgb4vf_get_regs_len(struct net_device *dev)
{
	return T4VF_REGMAP_SIZE;
}

/*
 * Dump a block of registers, start to end inclusive, into a buffer.
 */
static void reg_block_dump(struct adapter *adapter, void *regbuf,
			   unsigned int start, unsigned int end)
{
	u32 *bp = regbuf + start - T4VF_REGMAP_START;

	for ( ; start <= end; start += sizeof(u32)) {
		/*
		 * Avoid reading the Mailbox Control register since that
		 * can trigger a Mailbox Ownership Arbitration cycle and
		 * interfere with communication with the firmware.
		 */
		if (start == T4VF_CIM_BASE_ADDR + CIM_VF_EXT_MAILBOX_CTRL)
			*bp++ = 0xffff;
		else
			*bp++ = t4_read_reg(adapter, start);
	}
}

/*
 * Copy our entire register map into the provided buffer.
 */
static void cxgb4vf_get_regs(struct net_device *dev,
			     struct ethtool_regs *regs,
			     void *regbuf)
{
	struct adapter *adapter = netdev2adap(dev);

	regs->version = mk_adap_vers(adapter);

	/*
	 * Fill in register buffer with our register map.
	 */
	memset(regbuf, 0, T4VF_REGMAP_SIZE);

	reg_block_dump(adapter, regbuf,
		       T4VF_SGE_BASE_ADDR + T4VF_MOD_MAP_SGE_FIRST,
		       T4VF_SGE_BASE_ADDR + T4VF_MOD_MAP_SGE_LAST);
	reg_block_dump(adapter, regbuf,
		       T4VF_MPS_BASE_ADDR + T4VF_MOD_MAP_MPS_FIRST,
		       T4VF_MPS_BASE_ADDR + T4VF_MOD_MAP_MPS_LAST);

	/* T5 adds new registers in the PL Register map.
	 */
	reg_block_dump(adapter, regbuf,
		       T4VF_PL_BASE_ADDR + T4VF_MOD_MAP_PL_FIRST,
		       T4VF_PL_BASE_ADDR + (is_t4(adapter->params.chip)
		       ? A_PL_VF_WHOAMI : A_PL_VF_REVISION));
	reg_block_dump(adapter, regbuf,
		       T4VF_CIM_BASE_ADDR + T4VF_MOD_MAP_CIM_FIRST,
		       T4VF_CIM_BASE_ADDR + T4VF_MOD_MAP_CIM_LAST);

	reg_block_dump(adapter, regbuf,
		       T4VF_MBDATA_BASE_ADDR + T4VF_MBDATA_FIRST,
		       T4VF_MBDATA_BASE_ADDR + T4VF_MBDATA_LAST);
}

/*
 * Report current Wake On LAN settings.
 */
static void cxgb4vf_get_wol(struct net_device *dev,
			    struct ethtool_wolinfo *wol)
{
	wol->supported = 0;
	wol->wolopts = 0;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

/*
 * TCP Segmentation Offload flags which we support.
 */
#define TSO_FLAGS (NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_TSO_ECN)

static const struct ethtool_ops cxgb4vf_ethtool_ops = {
	.get_settings		= cxgb4vf_get_settings,
	.get_drvinfo		= cxgb4vf_get_drvinfo,
	.get_msglevel		= cxgb4vf_get_msglevel,
	.set_msglevel		= cxgb4vf_set_msglevel,
	.get_ringparam		= cxgb4vf_get_ringparam,
	.set_ringparam		= cxgb4vf_set_ringparam,
	.get_coalesce		= cxgb4vf_get_coalesce,
	.set_coalesce		= cxgb4vf_set_coalesce,
	.get_pauseparam		= cxgb4vf_get_pauseparam,
	.get_link		= ethtool_op_get_link,
	.get_strings		= cxgb4vf_get_strings,
	.set_phys_id		= cxgb4vf_phys_id,
	.get_sset_count		= cxgb4vf_get_sset_count,
	.get_ethtool_stats	= cxgb4vf_get_ethtool_stats,
	.get_regs_len		= cxgb4vf_get_regs_len,
	.get_regs		= cxgb4vf_get_regs,
	.get_wol		= cxgb4vf_get_wol,
};

/*
 * /sys/kernel/debug/cxgb4vf support code and data.
 * ================================================
 */

/*
 * Show SGE Queue Set information.  We display QPL Queues Sets per line.
 */
#define QPL	4

static int sge_qinfo_show(struct seq_file *seq, void *v)
{
	struct adapter *adapter = seq->private;
	int eth_entries = DIV_ROUND_UP(adapter->sge.ethqsets, QPL);
	int qs, r = (uintptr_t)v - 1;

	if (r)
		seq_putc(seq, '\n');

	#define S3(fmt_spec, s, v) \
		do {\
			seq_printf(seq, "%-12s", s); \
			for (qs = 0; qs < n; ++qs) \
				seq_printf(seq, " %16" fmt_spec, v); \
			seq_putc(seq, '\n'); \
		} while (0)
	#define S(s, v)		S3("s", s, v)
	#define T(s, v)		S3("u", s, txq[qs].v)
	#define R(s, v)		S3("u", s, rxq[qs].v)

	if (r < eth_entries) {
		const struct sge_eth_rxq *rxq = &adapter->sge.ethrxq[r * QPL];
		const struct sge_eth_txq *txq = &adapter->sge.ethtxq[r * QPL];
		int n = min(QPL, adapter->sge.ethqsets - QPL * r);

		S("QType:", "Ethernet");
		S("Interface:",
		  (rxq[qs].rspq.netdev
		   ? rxq[qs].rspq.netdev->name
		   : "N/A"));
		S3("d", "Port:",
		   (rxq[qs].rspq.netdev
		    ? ((struct port_info *)
		       netdev_priv(rxq[qs].rspq.netdev))->port_id
		    : -1));
		T("TxQ ID:", q.abs_id);
		T("TxQ size:", q.size);
		T("TxQ inuse:", q.in_use);
		T("TxQ PIdx:", q.pidx);
		T("TxQ CIdx:", q.cidx);
		R("RspQ ID:", rspq.abs_id);
		R("RspQ size:", rspq.size);
		R("RspQE size:", rspq.iqe_len);
		S3("u", "Intr delay:", qtimer_val(adapter, &rxq[qs].rspq));
		S3("u", "Intr pktcnt:",
		   adapter->sge.counter_val[rxq[qs].rspq.pktcnt_idx]);
		R("RspQ CIdx:", rspq.cidx);
		R("RspQ Gen:", rspq.gen);
		R("FL ID:", fl.abs_id);
		R("FL size:", fl.size - MIN_FL_RESID);
		R("FL avail:", fl.avail);
		R("FL PIdx:", fl.pidx);
		R("FL CIdx:", fl.cidx);
		return 0;
	}

	r -= eth_entries;
	if (r == 0) {
		const struct sge_rspq *evtq = &adapter->sge.fw_evtq;

		seq_printf(seq, "%-12s %16s\n", "QType:", "FW event queue");
		seq_printf(seq, "%-12s %16u\n", "RspQ ID:", evtq->abs_id);
		seq_printf(seq, "%-12s %16u\n", "Intr delay:",
			   qtimer_val(adapter, evtq));
		seq_printf(seq, "%-12s %16u\n", "Intr pktcnt:",
			   adapter->sge.counter_val[evtq->pktcnt_idx]);
		seq_printf(seq, "%-12s %16u\n", "RspQ Cidx:", evtq->cidx);
		seq_printf(seq, "%-12s %16u\n", "RspQ Gen:", evtq->gen);
	} else if (r == 1) {
		const struct sge_rspq *intrq = &adapter->sge.intrq;

		seq_printf(seq, "%-12s %16s\n", "QType:", "Interrupt Queue");
		seq_printf(seq, "%-12s %16u\n", "RspQ ID:", intrq->abs_id);
		seq_printf(seq, "%-12s %16u\n", "Intr delay:",
			   qtimer_val(adapter, intrq));
		seq_printf(seq, "%-12s %16u\n", "Intr pktcnt:",
			   adapter->sge.counter_val[intrq->pktcnt_idx]);
		seq_printf(seq, "%-12s %16u\n", "RspQ Cidx:", intrq->cidx);
		seq_printf(seq, "%-12s %16u\n", "RspQ Gen:", intrq->gen);
	}

	#undef R
	#undef T
	#undef S
	#undef S3

	return 0;
}

/*
 * Return the number of "entries" in our "file".  We group the multi-Queue
 * sections with QPL Queue Sets per "entry".  The sections of the output are:
 *
 *     Ethernet RX/TX Queue Sets
 *     Firmware Event Queue
 *     Forwarded Interrupt Queue (if in MSI mode)
 */
static int sge_queue_entries(const struct adapter *adapter)
{
	return DIV_ROUND_UP(adapter->sge.ethqsets, QPL) + 1 +
		((adapter->flags & USING_MSI) != 0);
}

static void *sge_queue_start(struct seq_file *seq, loff_t *pos)
{
	int entries = sge_queue_entries(seq->private);

	return *pos < entries ? (void *)((uintptr_t)*pos + 1) : NULL;
}

static void sge_queue_stop(struct seq_file *seq, void *v)
{
}

static void *sge_queue_next(struct seq_file *seq, void *v, loff_t *pos)
{
	int entries = sge_queue_entries(seq->private);

	++*pos;
	return *pos < entries ? (void *)((uintptr_t)*pos + 1) : NULL;
}

static const struct seq_operations sge_qinfo_seq_ops = {
	.start = sge_queue_start,
	.next  = sge_queue_next,
	.stop  = sge_queue_stop,
	.show  = sge_qinfo_show
};

static int sge_qinfo_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &sge_qinfo_seq_ops);

	if (!res) {
		struct seq_file *seq = file->private_data;
		seq->private = inode->i_private;
	}
	return res;
}

static const struct file_operations sge_qinfo_debugfs_fops = {
	.owner   = THIS_MODULE,
	.open    = sge_qinfo_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/*
 * Show SGE Queue Set statistics.  We display QPL Queues Sets per line.
 */
#define QPL	4

static int sge_qstats_show(struct seq_file *seq, void *v)
{
	struct adapter *adapter = seq->private;
	int eth_entries = DIV_ROUND_UP(adapter->sge.ethqsets, QPL);
	int qs, r = (uintptr_t)v - 1;

	if (r)
		seq_putc(seq, '\n');

	#define S3(fmt, s, v) \
		do { \
			seq_printf(seq, "%-16s", s); \
			for (qs = 0; qs < n; ++qs) \
				seq_printf(seq, " %8" fmt, v); \
			seq_putc(seq, '\n'); \
		} while (0)
	#define S(s, v)		S3("s", s, v)

	#define T3(fmt, s, v)	S3(fmt, s, txq[qs].v)
	#define T(s, v)		T3("lu", s, v)

	#define R3(fmt, s, v)	S3(fmt, s, rxq[qs].v)
	#define R(s, v)		R3("lu", s, v)

	if (r < eth_entries) {
		const struct sge_eth_rxq *rxq = &adapter->sge.ethrxq[r * QPL];
		const struct sge_eth_txq *txq = &adapter->sge.ethtxq[r * QPL];
		int n = min(QPL, adapter->sge.ethqsets - QPL * r);

		S("QType:", "Ethernet");
		S("Interface:",
		  (rxq[qs].rspq.netdev
		   ? rxq[qs].rspq.netdev->name
		   : "N/A"));
		R3("u", "RspQNullInts:", rspq.unhandled_irqs);
		R("RxPackets:", stats.pkts);
		R("RxCSO:", stats.rx_cso);
		R("VLANxtract:", stats.vlan_ex);
		R("LROmerged:", stats.lro_merged);
		R("LROpackets:", stats.lro_pkts);
		R("RxDrops:", stats.rx_drops);
		T("TSO:", tso);
		T("TxCSO:", tx_cso);
		T("VLANins:", vlan_ins);
		T("TxQFull:", q.stops);
		T("TxQRestarts:", q.restarts);
		T("TxMapErr:", mapping_err);
		R("FLAllocErr:", fl.alloc_failed);
		R("FLLrgAlcErr:", fl.large_alloc_failed);
		R("FLStarving:", fl.starving);
		return 0;
	}

	r -= eth_entries;
	if (r == 0) {
		const struct sge_rspq *evtq = &adapter->sge.fw_evtq;

		seq_printf(seq, "%-8s %16s\n", "QType:", "FW event queue");
		seq_printf(seq, "%-16s %8u\n", "RspQNullInts:",
			   evtq->unhandled_irqs);
		seq_printf(seq, "%-16s %8u\n", "RspQ CIdx:", evtq->cidx);
		seq_printf(seq, "%-16s %8u\n", "RspQ Gen:", evtq->gen);
	} else if (r == 1) {
		const struct sge_rspq *intrq = &adapter->sge.intrq;

		seq_printf(seq, "%-8s %16s\n", "QType:", "Interrupt Queue");
		seq_printf(seq, "%-16s %8u\n", "RspQNullInts:",
			   intrq->unhandled_irqs);
		seq_printf(seq, "%-16s %8u\n", "RspQ CIdx:", intrq->cidx);
		seq_printf(seq, "%-16s %8u\n", "RspQ Gen:", intrq->gen);
	}

	#undef R
	#undef T
	#undef S
	#undef R3
	#undef T3
	#undef S3

	return 0;
}

/*
 * Return the number of "entries" in our "file".  We group the multi-Queue
 * sections with QPL Queue Sets per "entry".  The sections of the output are:
 *
 *     Ethernet RX/TX Queue Sets
 *     Firmware Event Queue
 *     Forwarded Interrupt Queue (if in MSI mode)
 */
static int sge_qstats_entries(const struct adapter *adapter)
{
	return DIV_ROUND_UP(adapter->sge.ethqsets, QPL) + 1 +
		((adapter->flags & USING_MSI) != 0);
}

static void *sge_qstats_start(struct seq_file *seq, loff_t *pos)
{
	int entries = sge_qstats_entries(seq->private);

	return *pos < entries ? (void *)((uintptr_t)*pos + 1) : NULL;
}

static void sge_qstats_stop(struct seq_file *seq, void *v)
{
}

static void *sge_qstats_next(struct seq_file *seq, void *v, loff_t *pos)
{
	int entries = sge_qstats_entries(seq->private);

	(*pos)++;
	return *pos < entries ? (void *)((uintptr_t)*pos + 1) : NULL;
}

static const struct seq_operations sge_qstats_seq_ops = {
	.start = sge_qstats_start,
	.next  = sge_qstats_next,
	.stop  = sge_qstats_stop,
	.show  = sge_qstats_show
};

static int sge_qstats_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &sge_qstats_seq_ops);

	if (res == 0) {
		struct seq_file *seq = file->private_data;
		seq->private = inode->i_private;
	}
	return res;
}

static const struct file_operations sge_qstats_proc_fops = {
	.owner   = THIS_MODULE,
	.open    = sge_qstats_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/*
 * Show PCI-E SR-IOV Virtual Function Resource Limits.
 */
static int resources_show(struct seq_file *seq, void *v)
{
	struct adapter *adapter = seq->private;
	struct vf_resources *vfres = &adapter->params.vfres;

	#define S(desc, fmt, var) \
		seq_printf(seq, "%-60s " fmt "\n", \
			   desc " (" #var "):", vfres->var)

	S("Virtual Interfaces", "%d", nvi);
	S("Egress Queues", "%d", neq);
	S("Ethernet Control", "%d", nethctrl);
	S("Ingress Queues/w Free Lists/Interrupts", "%d", niqflint);
	S("Ingress Queues", "%d", niq);
	S("Traffic Class", "%d", tc);
	S("Port Access Rights Mask", "%#x", pmask);
	S("MAC Address Filters", "%d", nexactf);
	S("Firmware Command Read Capabilities", "%#x", r_caps);
	S("Firmware Command Write/Execute Capabilities", "%#x", wx_caps);

	#undef S

	return 0;
}

static int resources_open(struct inode *inode, struct file *file)
{
	return single_open(file, resources_show, inode->i_private);
}

static const struct file_operations resources_proc_fops = {
	.owner   = THIS_MODULE,
	.open    = resources_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/*
 * Show Virtual Interfaces.
 */
static int interfaces_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Interface  Port   VIID\n");
	} else {
		struct adapter *adapter = seq->private;
		int pidx = (uintptr_t)v - 2;
		struct net_device *dev = adapter->port[pidx];
		struct port_info *pi = netdev_priv(dev);

		seq_printf(seq, "%9s  %4d  %#5x\n",
			   dev->name, pi->port_id, pi->viid);
	}
	return 0;
}

static inline void *interfaces_get_idx(struct adapter *adapter, loff_t pos)
{
	return pos <= adapter->params.nports
		? (void *)(uintptr_t)(pos + 1)
		: NULL;
}

static void *interfaces_start(struct seq_file *seq, loff_t *pos)
{
	return *pos
		? interfaces_get_idx(seq->private, *pos)
		: SEQ_START_TOKEN;
}

static void *interfaces_next(struct seq_file *seq, void *v, loff_t *pos)
{
	(*pos)++;
	return interfaces_get_idx(seq->private, *pos);
}

static void interfaces_stop(struct seq_file *seq, void *v)
{
}

static const struct seq_operations interfaces_seq_ops = {
	.start = interfaces_start,
	.next  = interfaces_next,
	.stop  = interfaces_stop,
	.show  = interfaces_show
};

static int interfaces_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &interfaces_seq_ops);

	if (res == 0) {
		struct seq_file *seq = file->private_data;
		seq->private = inode->i_private;
	}
	return res;
}

static const struct file_operations interfaces_proc_fops = {
	.owner   = THIS_MODULE,
	.open    = interfaces_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/*
 * /sys/kernel/debugfs/cxgb4vf/ files list.
 */
struct cxgb4vf_debugfs_entry {
	const char *name;		/* name of debugfs node */
	umode_t mode;			/* file system mode */
	const struct file_operations *fops;
};

static struct cxgb4vf_debugfs_entry debugfs_files[] = {
	{ "sge_qinfo",  S_IRUGO, &sge_qinfo_debugfs_fops },
	{ "sge_qstats", S_IRUGO, &sge_qstats_proc_fops },
	{ "resources",  S_IRUGO, &resources_proc_fops },
	{ "interfaces", S_IRUGO, &interfaces_proc_fops },
};

/*
 * Module and device initialization and cleanup code.
 * ==================================================
 */

/*
 * Set up out /sys/kernel/debug/cxgb4vf sub-nodes.  We assume that the
 * directory (debugfs_root) has already been set up.
 */
static int setup_debugfs(struct adapter *adapter)
{
	int i;

	BUG_ON(IS_ERR_OR_NULL(adapter->debugfs_root));

	/*
	 * Debugfs support is best effort.
	 */
	for (i = 0; i < ARRAY_SIZE(debugfs_files); i++)
		(void)debugfs_create_file(debugfs_files[i].name,
				  debugfs_files[i].mode,
				  adapter->debugfs_root,
				  (void *)adapter,
				  debugfs_files[i].fops);

	return 0;
}

/*
 * Tear down the /sys/kernel/debug/cxgb4vf sub-nodes created above.  We leave
 * it to our caller to tear down the directory (debugfs_root).
 */
static void cleanup_debugfs(struct adapter *adapter)
{
	BUG_ON(IS_ERR_OR_NULL(adapter->debugfs_root));

	/*
	 * Unlike our sister routine cleanup_proc(), we don't need to remove
	 * individual entries because a call will be made to
	 * debugfs_remove_recursive().  We just need to clean up any ancillary
	 * persistent state.
	 */
	/* nothing to do */
}

/*
 * Perform early "adapter" initialization.  This is where we discover what
 * adapter parameters we're going to be using and initialize basic adapter
 * hardware support.
 */
static int adap_init0(struct adapter *adapter)
{
	struct vf_resources *vfres = &adapter->params.vfres;
	struct sge_params *sge_params = &adapter->params.sge;
	struct sge *s = &adapter->sge;
	unsigned int ethqsets;
	int err;
	u32 param, val = 0;
	unsigned int chipid;

	/*
	 * Wait for the device to become ready before proceeding ...
	 */
	err = t4vf_wait_dev_ready(adapter);
	if (err) {
		dev_err(adapter->pdev_dev, "device didn't become ready:"
			" err=%d\n", err);
		return err;
	}

	/*
	 * Some environments do not properly handle PCIE FLRs -- e.g. in Linux
	 * 2.6.31 and later we can't call pci_reset_function() in order to
	 * issue an FLR because of a self- deadlock on the device semaphore.
	 * Meanwhile, the OS infrastructure doesn't issue FLRs in all the
	 * cases where they're needed -- for instance, some versions of KVM
	 * fail to reset "Assigned Devices" when the VM reboots.  Therefore we
	 * use the firmware based reset in order to reset any per function
	 * state.
	 */
	err = t4vf_fw_reset(adapter);
	if (err < 0) {
		dev_err(adapter->pdev_dev, "FW reset failed: err=%d\n", err);
		return err;
	}

	adapter->params.chip = 0;
	switch (adapter->pdev->device >> 12) {
	case CHELSIO_T4:
		adapter->params.chip = CHELSIO_CHIP_CODE(CHELSIO_T4, 0);
		break;
	case CHELSIO_T5:
		chipid = G_REV(t4_read_reg(adapter, A_PL_VF_REV));
		adapter->params.chip |= CHELSIO_CHIP_CODE(CHELSIO_T5, chipid);
		break;
	}

	/*
	 * Grab basic operational parameters.  These will predominantly have
	 * been set up by the Physical Function Driver or will be hard coded
	 * into the adapter.  We just have to live with them ...  Note that
	 * we _must_ get our VPD parameters before our SGE parameters because
	 * we need to know the adapter's core clock from the VPD in order to
	 * properly decode the SGE Timer Values.
	 */
	err = t4vf_get_dev_params(adapter);
	if (err) {
		dev_err(adapter->pdev_dev, "unable to retrieve adapter"
			" device parameters: err=%d\n", err);
		return err;
	}
	err = t4vf_get_vpd_params(adapter);
	if (err) {
		dev_err(adapter->pdev_dev, "unable to retrieve adapter"
			" VPD parameters: err=%d\n", err);
		return err;
	}
	err = t4vf_get_sge_params(adapter);
	if (err) {
		dev_err(adapter->pdev_dev, "unable to retrieve adapter"
			" SGE parameters: err=%d\n", err);
		return err;
	}
	err = t4vf_get_rss_glb_config(adapter);
	if (err) {
		dev_err(adapter->pdev_dev, "unable to retrieve adapter"
			" RSS parameters: err=%d\n", err);
		return err;
	}
	if (adapter->params.rss.mode !=
	    FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL) {
		dev_err(adapter->pdev_dev, "unable to operate with global RSS"
			" mode %d\n", adapter->params.rss.mode);
		return -EINVAL;
	}
	err = t4vf_sge_init(adapter);
	if (err) {
		dev_err(adapter->pdev_dev, "unable to use adapter parameters:"
			" err=%d\n", err);
		return err;
	}

	/* If we're running on newer firmware, let it know that we're
	 * prepared to deal with encapsulated CPL messages.  Older
	 * firmware won't understand this and we'll just get
	 * unencapsulated messages ...
	 */
	param = FW_PARAMS_MNEM(FW_PARAMS_MNEM_PFVF) |
		FW_PARAMS_PARAM_X(FW_PARAMS_PARAM_PFVF_CPLFW4MSG_ENCAP);
	val = 1;
	(void) t4vf_set_params(adapter, 1, &param, &val);

	/*
	 * Retrieve our RX interrupt holdoff timer values and counter
	 * threshold values from the SGE parameters.
	 */
	s->timer_val[0] = core_ticks_to_us(adapter,
		TIMERVALUE0_GET(sge_params->sge_timer_value_0_and_1));
	s->timer_val[1] = core_ticks_to_us(adapter,
		TIMERVALUE1_GET(sge_params->sge_timer_value_0_and_1));
	s->timer_val[2] = core_ticks_to_us(adapter,
		TIMERVALUE0_GET(sge_params->sge_timer_value_2_and_3));
	s->timer_val[3] = core_ticks_to_us(adapter,
		TIMERVALUE1_GET(sge_params->sge_timer_value_2_and_3));
	s->timer_val[4] = core_ticks_to_us(adapter,
		TIMERVALUE0_GET(sge_params->sge_timer_value_4_and_5));
	s->timer_val[5] = core_ticks_to_us(adapter,
		TIMERVALUE1_GET(sge_params->sge_timer_value_4_and_5));

	s->counter_val[0] =
		THRESHOLD_0_GET(sge_params->sge_ingress_rx_threshold);
	s->counter_val[1] =
		THRESHOLD_1_GET(sge_params->sge_ingress_rx_threshold);
	s->counter_val[2] =
		THRESHOLD_2_GET(sge_params->sge_ingress_rx_threshold);
	s->counter_val[3] =
		THRESHOLD_3_GET(sge_params->sge_ingress_rx_threshold);

	/*
	 * Grab our Virtual Interface resource allocation, extract the
	 * features that we're interested in and do a bit of sanity testing on
	 * what we discover.
	 */
	err = t4vf_get_vfres(adapter);
	if (err) {
		dev_err(adapter->pdev_dev, "unable to get virtual interface"
			" resources: err=%d\n", err);
		return err;
	}

	/*
	 * The number of "ports" which we support is equal to the number of
	 * Virtual Interfaces with which we've been provisioned.
	 */
	adapter->params.nports = vfres->nvi;
	if (adapter->params.nports > MAX_NPORTS) {
		dev_warn(adapter->pdev_dev, "only using %d of %d allowed"
			 " virtual interfaces\n", MAX_NPORTS,
			 adapter->params.nports);
		adapter->params.nports = MAX_NPORTS;
	}

	/*
	 * We need to reserve a number of the ingress queues with Free List
	 * and Interrupt capabilities for special interrupt purposes (like
	 * asynchronous firmware messages, or forwarded interrupts if we're
	 * using MSI).  The rest of the FL/Intr-capable ingress queues will be
	 * matched up one-for-one with Ethernet/Control egress queues in order
	 * to form "Queue Sets" which will be aportioned between the "ports".
	 * For each Queue Set, we'll need the ability to allocate two Egress
	 * Contexts -- one for the Ingress Queue Free List and one for the TX
	 * Ethernet Queue.
	 */
	ethqsets = vfres->niqflint - INGQ_EXTRAS;
	if (vfres->nethctrl != ethqsets) {
		dev_warn(adapter->pdev_dev, "unequal number of [available]"
			 " ingress/egress queues (%d/%d); using minimum for"
			 " number of Queue Sets\n", ethqsets, vfres->nethctrl);
		ethqsets = min(vfres->nethctrl, ethqsets);
	}
	if (vfres->neq < ethqsets*2) {
		dev_warn(adapter->pdev_dev, "Not enough Egress Contexts (%d)"
			 " to support Queue Sets (%d); reducing allowed Queue"
			 " Sets\n", vfres->neq, ethqsets);
		ethqsets = vfres->neq/2;
	}
	if (ethqsets > MAX_ETH_QSETS) {
		dev_warn(adapter->pdev_dev, "only using %d of %d allowed Queue"
			 " Sets\n", MAX_ETH_QSETS, adapter->sge.max_ethqsets);
		ethqsets = MAX_ETH_QSETS;
	}
	if (vfres->niq != 0 || vfres->neq > ethqsets*2) {
		dev_warn(adapter->pdev_dev, "unused resources niq/neq (%d/%d)"
			 " ignored\n", vfres->niq, vfres->neq - ethqsets*2);
	}
	adapter->sge.max_ethqsets = ethqsets;

	/*
	 * Check for various parameter sanity issues.  Most checks simply
	 * result in us using fewer resources than our provissioning but we
	 * do need at least  one "port" with which to work ...
	 */
	if (adapter->sge.max_ethqsets < adapter->params.nports) {
		dev_warn(adapter->pdev_dev, "only using %d of %d available"
			 " virtual interfaces (too few Queue Sets)\n",
			 adapter->sge.max_ethqsets, adapter->params.nports);
		adapter->params.nports = adapter->sge.max_ethqsets;
	}
	if (adapter->params.nports == 0) {
		dev_err(adapter->pdev_dev, "no virtual interfaces configured/"
			"usable!\n");
		return -EINVAL;
	}
	return 0;
}

static inline void init_rspq(struct sge_rspq *rspq, u8 timer_idx,
			     u8 pkt_cnt_idx, unsigned int size,
			     unsigned int iqe_size)
{
	rspq->intr_params = (QINTR_TIMER_IDX(timer_idx) |
			     (pkt_cnt_idx < SGE_NCOUNTERS ? QINTR_CNT_EN : 0));
	rspq->pktcnt_idx = (pkt_cnt_idx < SGE_NCOUNTERS
			    ? pkt_cnt_idx
			    : 0);
	rspq->iqe_len = iqe_size;
	rspq->size = size;
}

/*
 * Perform default configuration of DMA queues depending on the number and
 * type of ports we found and the number of available CPUs.  Most settings can
 * be modified by the admin via ethtool and cxgbtool prior to the adapter
 * being brought up for the first time.
 */
static void cfg_queues(struct adapter *adapter)
{
	struct sge *s = &adapter->sge;
	int q10g, n10g, qidx, pidx, qs;
	size_t iqe_size;

	/*
	 * We should not be called till we know how many Queue Sets we can
	 * support.  In particular, this means that we need to know what kind
	 * of interrupts we'll be using ...
	 */
	BUG_ON((adapter->flags & (USING_MSIX|USING_MSI)) == 0);

	/*
	 * Count the number of 10GbE Virtual Interfaces that we have.
	 */
	n10g = 0;
	for_each_port(adapter, pidx)
		n10g += is_10g_port(&adap2pinfo(adapter, pidx)->link_cfg);

	/*
	 * We default to 1 queue per non-10G port and up to # of cores queues
	 * per 10G port.
	 */
	if (n10g == 0)
		q10g = 0;
	else {
		int n1g = (adapter->params.nports - n10g);
		q10g = (adapter->sge.max_ethqsets - n1g) / n10g;
		if (q10g > num_online_cpus())
			q10g = num_online_cpus();
	}

	/*
	 * Allocate the "Queue Sets" to the various Virtual Interfaces.
	 * The layout will be established in setup_sge_queues() when the
	 * adapter is brough up for the first time.
	 */
	qidx = 0;
	for_each_port(adapter, pidx) {
		struct port_info *pi = adap2pinfo(adapter, pidx);

		pi->first_qset = qidx;
		pi->nqsets = is_10g_port(&pi->link_cfg) ? q10g : 1;
		qidx += pi->nqsets;
	}
	s->ethqsets = qidx;

	/*
	 * The Ingress Queue Entry Size for our various Response Queues needs
	 * to be big enough to accommodate the largest message we can receive
	 * from the chip/firmware; which is 64 bytes ...
	 */
	iqe_size = 64;

	/*
	 * Set up default Queue Set parameters ...  Start off with the
	 * shortest interrupt holdoff timer.
	 */
	for (qs = 0; qs < s->max_ethqsets; qs++) {
		struct sge_eth_rxq *rxq = &s->ethrxq[qs];
		struct sge_eth_txq *txq = &s->ethtxq[qs];

		init_rspq(&rxq->rspq, 0, 0, 1024, iqe_size);
		rxq->fl.size = 72;
		txq->q.size = 1024;
	}

	/*
	 * The firmware event queue is used for link state changes and
	 * notifications of TX DMA completions.
	 */
	init_rspq(&s->fw_evtq, SGE_TIMER_RSTRT_CNTR, 0, 512, iqe_size);

	/*
	 * The forwarded interrupt queue is used when we're in MSI interrupt
	 * mode.  In this mode all interrupts associated with RX queues will
	 * be forwarded to a single queue which we'll associate with our MSI
	 * interrupt vector.  The messages dropped in the forwarded interrupt
	 * queue will indicate which ingress queue needs servicing ...  This
	 * queue needs to be large enough to accommodate all of the ingress
	 * queues which are forwarding their interrupt (+1 to prevent the PIDX
	 * from equalling the CIDX if every ingress queue has an outstanding
	 * interrupt).  The queue doesn't need to be any larger because no
	 * ingress queue will ever have more than one outstanding interrupt at
	 * any time ...
	 */
	init_rspq(&s->intrq, SGE_TIMER_RSTRT_CNTR, 0, MSIX_ENTRIES + 1,
		  iqe_size);
}

/*
 * Reduce the number of Ethernet queues across all ports to at most n.
 * n provides at least one queue per port.
 */
static void reduce_ethqs(struct adapter *adapter, int n)
{
	int i;
	struct port_info *pi;

	/*
	 * While we have too many active Ether Queue Sets, interate across the
	 * "ports" and reduce their individual Queue Set allocations.
	 */
	BUG_ON(n < adapter->params.nports);
	while (n < adapter->sge.ethqsets)
		for_each_port(adapter, i) {
			pi = adap2pinfo(adapter, i);
			if (pi->nqsets > 1) {
				pi->nqsets--;
				adapter->sge.ethqsets--;
				if (adapter->sge.ethqsets <= n)
					break;
			}
		}

	/*
	 * Reassign the starting Queue Sets for each of the "ports" ...
	 */
	n = 0;
	for_each_port(adapter, i) {
		pi = adap2pinfo(adapter, i);
		pi->first_qset = n;
		n += pi->nqsets;
	}
}

/*
 * We need to grab enough MSI-X vectors to cover our interrupt needs.  Ideally
 * we get a separate MSI-X vector for every "Queue Set" plus any extras we
 * need.  Minimally we need one for every Virtual Interface plus those needed
 * for our "extras".  Note that this process may lower the maximum number of
 * allowed Queue Sets ...
 */
static int enable_msix(struct adapter *adapter)
{
	int i, err, want, need;
	struct msix_entry entries[MSIX_ENTRIES];
	struct sge *s = &adapter->sge;

	for (i = 0; i < MSIX_ENTRIES; ++i)
		entries[i].entry = i;

	/*
	 * We _want_ enough MSI-X interrupts to cover all of our "Queue Sets"
	 * plus those needed for our "extras" (for example, the firmware
	 * message queue).  We _need_ at least one "Queue Set" per Virtual
	 * Interface plus those needed for our "extras".  So now we get to see
	 * if the song is right ...
	 */
	want = s->max_ethqsets + MSIX_EXTRAS;
	need = adapter->params.nports + MSIX_EXTRAS;
	while ((err = pci_enable_msix(adapter->pdev, entries, want)) >= need)
		want = err;

	if (err == 0) {
		int nqsets = want - MSIX_EXTRAS;
		if (nqsets < s->max_ethqsets) {
			dev_warn(adapter->pdev_dev, "only enough MSI-X vectors"
				 " for %d Queue Sets\n", nqsets);
			s->max_ethqsets = nqsets;
			if (nqsets < s->ethqsets)
				reduce_ethqs(adapter, nqsets);
		}
		for (i = 0; i < want; ++i)
			adapter->msix_info[i].vec = entries[i].vector;
	} else if (err > 0) {
		pci_disable_msix(adapter->pdev);
		dev_info(adapter->pdev_dev, "only %d MSI-X vectors left,"
			 " not using MSI-X\n", err);
	}
	return err;
}

static const struct net_device_ops cxgb4vf_netdev_ops	= {
	.ndo_open		= cxgb4vf_open,
	.ndo_stop		= cxgb4vf_stop,
	.ndo_start_xmit		= t4vf_eth_xmit,
	.ndo_get_stats		= cxgb4vf_get_stats,
	.ndo_set_rx_mode	= cxgb4vf_set_rxmode,
	.ndo_set_mac_address	= cxgb4vf_set_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= cxgb4vf_do_ioctl,
	.ndo_change_mtu		= cxgb4vf_change_mtu,
	.ndo_fix_features	= cxgb4vf_fix_features,
	.ndo_set_features	= cxgb4vf_set_features,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= cxgb4vf_poll_controller,
#endif
};

/*
 * "Probe" a device: initialize a device and construct all kernel and driver
 * state needed to manage the device.  This routine is called "init_one" in
 * the PF Driver ...
 */
static int cxgb4vf_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *ent)
{
	int pci_using_dac;
	int err, pidx;
	unsigned int pmask;
	struct adapter *adapter;
	struct port_info *pi;
	struct net_device *netdev;

	/*
	 * Print our driver banner the first time we're called to initialize a
	 * device.
	 */
	pr_info_once("%s - version %s\n", DRV_DESC, DRV_VERSION);

	/*
	 * Initialize generic PCI device state.
	 */
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "cannot enable PCI device\n");
		return err;
	}

	/*
	 * Reserve PCI resources for the device.  If we can't get them some
	 * other driver may have already claimed the device ...
	 */
	err = pci_request_regions(pdev, KBUILD_MODNAME);
	if (err) {
		dev_err(&pdev->dev, "cannot obtain PCI resources\n");
		goto err_disable_device;
	}

	/*
	 * Set up our DMA mask: try for 64-bit address masking first and
	 * fall back to 32-bit if we can't get 64 bits ...
	 */
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err == 0) {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (err) {
			dev_err(&pdev->dev, "unable to obtain 64-bit DMA for"
				" coherent allocations\n");
			goto err_release_regions;
		}
		pci_using_dac = 1;
	} else {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err != 0) {
			dev_err(&pdev->dev, "no usable DMA configuration\n");
			goto err_release_regions;
		}
		pci_using_dac = 0;
	}

	/*
	 * Enable bus mastering for the device ...
	 */
	pci_set_master(pdev);

	/*
	 * Allocate our adapter data structure and attach it to the device.
	 */
	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		err = -ENOMEM;
		goto err_release_regions;
	}
	pci_set_drvdata(pdev, adapter);
	adapter->pdev = pdev;
	adapter->pdev_dev = &pdev->dev;

	/*
	 * Initialize SMP data synchronization resources.
	 */
	spin_lock_init(&adapter->stats_lock);

	/*
	 * Map our I/O registers in BAR0.
	 */
	adapter->regs = pci_ioremap_bar(pdev, 0);
	if (!adapter->regs) {
		dev_err(&pdev->dev, "cannot map device registers\n");
		err = -ENOMEM;
		goto err_free_adapter;
	}

	/*
	 * Initialize adapter level features.
	 */
	adapter->name = pci_name(pdev);
	adapter->msg_enable = dflt_msg_enable;
	err = adap_init0(adapter);
	if (err)
		goto err_unmap_bar;

	/*
	 * Allocate our "adapter ports" and stitch everything together.
	 */
	pmask = adapter->params.vfres.pmask;
	for_each_port(adapter, pidx) {
		int port_id, viid;

		/*
		 * We simplistically allocate our virtual interfaces
		 * sequentially across the port numbers to which we have
		 * access rights.  This should be configurable in some manner
		 * ...
		 */
		if (pmask == 0)
			break;
		port_id = ffs(pmask) - 1;
		pmask &= ~(1 << port_id);
		viid = t4vf_alloc_vi(adapter, port_id);
		if (viid < 0) {
			dev_err(&pdev->dev, "cannot allocate VI for port %d:"
				" err=%d\n", port_id, viid);
			err = viid;
			goto err_free_dev;
		}

		/*
		 * Allocate our network device and stitch things together.
		 */
		netdev = alloc_etherdev_mq(sizeof(struct port_info),
					   MAX_PORT_QSETS);
		if (netdev == NULL) {
			t4vf_free_vi(adapter, viid);
			err = -ENOMEM;
			goto err_free_dev;
		}
		adapter->port[pidx] = netdev;
		SET_NETDEV_DEV(netdev, &pdev->dev);
		pi = netdev_priv(netdev);
		pi->adapter = adapter;
		pi->pidx = pidx;
		pi->port_id = port_id;
		pi->viid = viid;

		/*
		 * Initialize the starting state of our "port" and register
		 * it.
		 */
		pi->xact_addr_filt = -1;
		netif_carrier_off(netdev);
		netdev->irq = pdev->irq;

		netdev->hw_features = NETIF_F_SG | TSO_FLAGS |
			NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_RXCSUM;
		netdev->vlan_features = NETIF_F_SG | TSO_FLAGS |
			NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			NETIF_F_HIGHDMA;
		netdev->features = netdev->hw_features |
				   NETIF_F_HW_VLAN_CTAG_TX;
		if (pci_using_dac)
			netdev->features |= NETIF_F_HIGHDMA;

		netdev->priv_flags |= IFF_UNICAST_FLT;

		netdev->netdev_ops = &cxgb4vf_netdev_ops;
		SET_ETHTOOL_OPS(netdev, &cxgb4vf_ethtool_ops);

		/*
		 * Initialize the hardware/software state for the port.
		 */
		err = t4vf_port_init(adapter, pidx);
		if (err) {
			dev_err(&pdev->dev, "cannot initialize port %d\n",
				pidx);
			goto err_free_dev;
		}
	}

	/*
	 * The "card" is now ready to go.  If any errors occur during device
	 * registration we do not fail the whole "card" but rather proceed
	 * only with the ports we manage to register successfully.  However we
	 * must register at least one net device.
	 */
	for_each_port(adapter, pidx) {
		netdev = adapter->port[pidx];
		if (netdev == NULL)
			continue;

		err = register_netdev(netdev);
		if (err) {
			dev_warn(&pdev->dev, "cannot register net device %s,"
				 " skipping\n", netdev->name);
			continue;
		}

		set_bit(pidx, &adapter->registered_device_map);
	}
	if (adapter->registered_device_map == 0) {
		dev_err(&pdev->dev, "could not register any net devices\n");
		goto err_free_dev;
	}

	/*
	 * Set up our debugfs entries.
	 */
	if (!IS_ERR_OR_NULL(cxgb4vf_debugfs_root)) {
		adapter->debugfs_root =
			debugfs_create_dir(pci_name(pdev),
					   cxgb4vf_debugfs_root);
		if (IS_ERR_OR_NULL(adapter->debugfs_root))
			dev_warn(&pdev->dev, "could not create debugfs"
				 " directory");
		else
			setup_debugfs(adapter);
	}

	/*
	 * See what interrupts we'll be using.  If we've been configured to
	 * use MSI-X interrupts, try to enable them but fall back to using
	 * MSI interrupts if we can't enable MSI-X interrupts.  If we can't
	 * get MSI interrupts we bail with the error.
	 */
	if (msi == MSI_MSIX && enable_msix(adapter) == 0)
		adapter->flags |= USING_MSIX;
	else {
		err = pci_enable_msi(pdev);
		if (err) {
			dev_err(&pdev->dev, "Unable to allocate %s interrupts;"
				" err=%d\n",
				msi == MSI_MSIX ? "MSI-X or MSI" : "MSI", err);
			goto err_free_debugfs;
		}
		adapter->flags |= USING_MSI;
	}

	/*
	 * Now that we know how many "ports" we have and what their types are,
	 * and how many Queue Sets we can support, we can configure our queue
	 * resources.
	 */
	cfg_queues(adapter);

	/*
	 * Print a short notice on the existence and configuration of the new
	 * VF network device ...
	 */
	for_each_port(adapter, pidx) {
		dev_info(adapter->pdev_dev, "%s: Chelsio VF NIC PCIe %s\n",
			 adapter->port[pidx]->name,
			 (adapter->flags & USING_MSIX) ? "MSI-X" :
			 (adapter->flags & USING_MSI)  ? "MSI" : "");
	}

	/*
	 * Return success!
	 */
	return 0;

	/*
	 * Error recovery and exit code.  Unwind state that's been created
	 * so far and return the error.
	 */

err_free_debugfs:
	if (!IS_ERR_OR_NULL(adapter->debugfs_root)) {
		cleanup_debugfs(adapter);
		debugfs_remove_recursive(adapter->debugfs_root);
	}

err_free_dev:
	for_each_port(adapter, pidx) {
		netdev = adapter->port[pidx];
		if (netdev == NULL)
			continue;
		pi = netdev_priv(netdev);
		t4vf_free_vi(adapter, pi->viid);
		if (test_bit(pidx, &adapter->registered_device_map))
			unregister_netdev(netdev);
		free_netdev(netdev);
	}

err_unmap_bar:
	iounmap(adapter->regs);

err_free_adapter:
	kfree(adapter);

err_release_regions:
	pci_release_regions(pdev);
	pci_clear_master(pdev);

err_disable_device:
	pci_disable_device(pdev);

	return err;
}

/*
 * "Remove" a device: tear down all kernel and driver state created in the
 * "probe" routine and quiesce the device (disable interrupts, etc.).  (Note
 * that this is called "remove_one" in the PF Driver.)
 */
static void cxgb4vf_pci_remove(struct pci_dev *pdev)
{
	struct adapter *adapter = pci_get_drvdata(pdev);

	/*
	 * Tear down driver state associated with device.
	 */
	if (adapter) {
		int pidx;

		/*
		 * Stop all of our activity.  Unregister network port,
		 * disable interrupts, etc.
		 */
		for_each_port(adapter, pidx)
			if (test_bit(pidx, &adapter->registered_device_map))
				unregister_netdev(adapter->port[pidx]);
		t4vf_sge_stop(adapter);
		if (adapter->flags & USING_MSIX) {
			pci_disable_msix(adapter->pdev);
			adapter->flags &= ~USING_MSIX;
		} else if (adapter->flags & USING_MSI) {
			pci_disable_msi(adapter->pdev);
			adapter->flags &= ~USING_MSI;
		}

		/*
		 * Tear down our debugfs entries.
		 */
		if (!IS_ERR_OR_NULL(adapter->debugfs_root)) {
			cleanup_debugfs(adapter);
			debugfs_remove_recursive(adapter->debugfs_root);
		}

		/*
		 * Free all of the various resources which we've acquired ...
		 */
		t4vf_free_sge_resources(adapter);
		for_each_port(adapter, pidx) {
			struct net_device *netdev = adapter->port[pidx];
			struct port_info *pi;

			if (netdev == NULL)
				continue;

			pi = netdev_priv(netdev);
			t4vf_free_vi(adapter, pi->viid);
			free_netdev(netdev);
		}
		iounmap(adapter->regs);
		kfree(adapter);
	}

	/*
	 * Disable the device and release its PCI resources.
	 */
	pci_disable_device(pdev);
	pci_clear_master(pdev);
	pci_release_regions(pdev);
}

/*
 * "Shutdown" quiesce the device, stopping Ingress Packet and Interrupt
 * delivery.
 */
static void cxgb4vf_pci_shutdown(struct pci_dev *pdev)
{
	struct adapter *adapter;
	int pidx;

	adapter = pci_get_drvdata(pdev);
	if (!adapter)
		return;

	/*
	 * Disable all Virtual Interfaces.  This will shut down the
	 * delivery of all ingress packets into the chip for these
	 * Virtual Interfaces.
	 */
	for_each_port(adapter, pidx) {
		struct net_device *netdev;
		struct port_info *pi;

		if (!test_bit(pidx, &adapter->registered_device_map))
			continue;

		netdev = adapter->port[pidx];
		if (!netdev)
			continue;

		pi = netdev_priv(netdev);
		t4vf_enable_vi(adapter, pi->viid, false, false);
	}

	/*
	 * Free up all Queues which will prevent further DMA and
	 * Interrupts allowing various internal pathways to drain.
	 */
	t4vf_free_sge_resources(adapter);
}

/*
 * PCI Device registration data structures.
 */
#define CH_DEVICE(devid, idx) \
	{ PCI_VENDOR_ID_CHELSIO, devid, PCI_ANY_ID, PCI_ANY_ID, 0, 0, idx }

static DEFINE_PCI_DEVICE_TABLE(cxgb4vf_pci_tbl) = {
	CH_DEVICE(0xb000, 0),	/* PE10K FPGA */
	CH_DEVICE(0x4800, 0),	/* T440-dbg */
	CH_DEVICE(0x4801, 0),	/* T420-cr */
	CH_DEVICE(0x4802, 0),	/* T422-cr */
	CH_DEVICE(0x4803, 0),	/* T440-cr */
	CH_DEVICE(0x4804, 0),	/* T420-bch */
	CH_DEVICE(0x4805, 0),   /* T440-bch */
	CH_DEVICE(0x4806, 0),	/* T460-ch */
	CH_DEVICE(0x4807, 0),	/* T420-so */
	CH_DEVICE(0x4808, 0),	/* T420-cx */
	CH_DEVICE(0x4809, 0),	/* T420-bt */
	CH_DEVICE(0x480a, 0),   /* T404-bt */
	CH_DEVICE(0x480d, 0),   /* T480-cr */
	CH_DEVICE(0x480e, 0),   /* T440-lp-cr */
	CH_DEVICE(0x5800, 0),	/* T580-dbg */
	CH_DEVICE(0x5801, 0),	/* T520-cr */
	CH_DEVICE(0x5802, 0),	/* T522-cr */
	CH_DEVICE(0x5803, 0),	/* T540-cr */
	CH_DEVICE(0x5804, 0),	/* T520-bch */
	CH_DEVICE(0x5805, 0),   /* T540-bch */
	CH_DEVICE(0x5806, 0),	/* T540-ch */
	CH_DEVICE(0x5807, 0),	/* T520-so */
	CH_DEVICE(0x5808, 0),	/* T520-cx */
	CH_DEVICE(0x5809, 0),	/* T520-bt */
	CH_DEVICE(0x580a, 0),   /* T504-bt */
	CH_DEVICE(0x580b, 0),   /* T520-sr */
	CH_DEVICE(0x580c, 0),   /* T504-bt */
	CH_DEVICE(0x580d, 0),   /* T580-cr */
	CH_DEVICE(0x580e, 0),   /* T540-lp-cr */
	CH_DEVICE(0x580f, 0),   /* Amsterdam */
	CH_DEVICE(0x5810, 0),   /* T580-lp-cr */
	CH_DEVICE(0x5811, 0),   /* T520-lp-cr */
	CH_DEVICE(0x5812, 0),   /* T560-cr */
	CH_DEVICE(0x5813, 0),   /* T580-cr */
	{ 0, }
};

MODULE_DESCRIPTION(DRV_DESC);
MODULE_AUTHOR("Chelsio Communications");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, cxgb4vf_pci_tbl);

static struct pci_driver cxgb4vf_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= cxgb4vf_pci_tbl,
	.probe		= cxgb4vf_pci_probe,
	.remove		= cxgb4vf_pci_remove,
	.shutdown	= cxgb4vf_pci_shutdown,
};

/*
 * Initialize global driver state.
 */
static int __init cxgb4vf_module_init(void)
{
	int ret;

	/*
	 * Vet our module parameters.
	 */
	if (msi != MSI_MSIX && msi != MSI_MSI) {
		pr_warn("bad module parameter msi=%d; must be %d (MSI-X or MSI) or %d (MSI)\n",
			msi, MSI_MSIX, MSI_MSI);
		return -EINVAL;
	}

	/* Debugfs support is optional, just warn if this fails */
	cxgb4vf_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR_OR_NULL(cxgb4vf_debugfs_root))
		pr_warn("could not create debugfs entry, continuing\n");

	ret = pci_register_driver(&cxgb4vf_driver);
	if (ret < 0 && !IS_ERR_OR_NULL(cxgb4vf_debugfs_root))
		debugfs_remove(cxgb4vf_debugfs_root);
	return ret;
}

/*
 * Tear down global driver state.
 */
static void __exit cxgb4vf_module_exit(void)
{
	pci_unregister_driver(&cxgb4vf_driver);
	debugfs_remove(cxgb4vf_debugfs_root);
}

module_init(cxgb4vf_module_init);
module_exit(cxgb4vf_module_exit);
