// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Chelsio Communications.  All rights reserved. */

#include "cxgb4.h"
#include "cxgb4_tc_mqprio.h"

static int cxgb4_mqprio_validate(struct net_device *dev,
				 struct tc_mqprio_qopt_offload *mqprio)
{
	u64 min_rate = 0, max_rate = 0, max_link_rate;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	u32 qcount = 0, qoffset = 0;
	u32 link_ok, speed, mtu;
	int ret;
	u8 i;

	if (!mqprio->qopt.num_tc)
		return 0;

	if (mqprio->qopt.hw != TC_MQPRIO_HW_OFFLOAD_TCS) {
		netdev_err(dev, "Only full TC hardware offload is supported\n");
		return -EINVAL;
	} else if (mqprio->mode != TC_MQPRIO_MODE_CHANNEL) {
		netdev_err(dev, "Only channel mode offload is supported\n");
		return -EINVAL;
	} else if (mqprio->shaper != TC_MQPRIO_SHAPER_BW_RATE) {
		netdev_err(dev,	"Only bandwidth rate shaper supported\n");
		return -EINVAL;
	} else if (mqprio->qopt.num_tc > adap->params.nsched_cls) {
		netdev_err(dev,
			   "Only %u traffic classes supported by hardware\n",
			   adap->params.nsched_cls);
		return -ERANGE;
	}

	ret = t4_get_link_params(pi, &link_ok, &speed, &mtu);
	if (ret) {
		netdev_err(dev, "Failed to get link speed, ret: %d\n", ret);
		return -EINVAL;
	}

	/* Convert from Mbps to bps */
	max_link_rate = (u64)speed * 1000 * 1000;

	for (i = 0; i < mqprio->qopt.num_tc; i++) {
		qoffset = max_t(u16, mqprio->qopt.offset[i], qoffset);
		qcount += mqprio->qopt.count[i];

		/* Convert byte per second to bits per second */
		min_rate += (mqprio->min_rate[i] * 8);
		max_rate += (mqprio->max_rate[i] * 8);
	}

	if (qoffset >= adap->tids.neotids || qcount > adap->tids.neotids)
		return -ENOMEM;

	if (min_rate > max_link_rate || max_rate > max_link_rate) {
		netdev_err(dev,
			   "Total Min/Max (%llu/%llu) Rate > supported (%llu)\n",
			   min_rate, max_rate, max_link_rate);
		return -EINVAL;
	}

	return 0;
}

static int cxgb4_init_eosw_txq(struct net_device *dev,
			       struct sge_eosw_txq *eosw_txq,
			       u32 eotid, u32 hwqid)
{
	struct adapter *adap = netdev2adap(dev);
	struct sge_eosw_desc *ring;

	memset(eosw_txq, 0, sizeof(*eosw_txq));

	ring = kcalloc(CXGB4_EOSW_TXQ_DEFAULT_DESC_NUM,
		       sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return -ENOMEM;

	eosw_txq->desc = ring;
	eosw_txq->ndesc = CXGB4_EOSW_TXQ_DEFAULT_DESC_NUM;
	spin_lock_init(&eosw_txq->lock);
	eosw_txq->state = CXGB4_EO_STATE_CLOSED;
	eosw_txq->eotid = eotid;
	eosw_txq->hwtid = adap->tids.eotid_base + eosw_txq->eotid;
	eosw_txq->cred = adap->params.ofldq_wr_cred;
	eosw_txq->hwqid = hwqid;
	eosw_txq->netdev = dev;
	tasklet_init(&eosw_txq->qresume_tsk, cxgb4_ethofld_restart,
		     (unsigned long)eosw_txq);
	return 0;
}

static void cxgb4_clean_eosw_txq(struct net_device *dev,
				 struct sge_eosw_txq *eosw_txq)
{
	struct adapter *adap = netdev2adap(dev);

	cxgb4_eosw_txq_free_desc(adap, eosw_txq, eosw_txq->ndesc);
	eosw_txq->pidx = 0;
	eosw_txq->last_pidx = 0;
	eosw_txq->cidx = 0;
	eosw_txq->last_cidx = 0;
	eosw_txq->inuse = 0;
	eosw_txq->cred = adap->params.ofldq_wr_cred;
	eosw_txq->ncompl = 0;
	eosw_txq->last_compl = 0;
	eosw_txq->state = CXGB4_EO_STATE_CLOSED;
}

static void cxgb4_free_eosw_txq(struct net_device *dev,
				struct sge_eosw_txq *eosw_txq)
{
	spin_lock_bh(&eosw_txq->lock);
	cxgb4_clean_eosw_txq(dev, eosw_txq);
	kfree(eosw_txq->desc);
	spin_unlock_bh(&eosw_txq->lock);
	tasklet_kill(&eosw_txq->qresume_tsk);
}

static int cxgb4_mqprio_enable_offload(struct net_device *dev,
				       struct tc_mqprio_qopt_offload *mqprio)
{
	struct cxgb4_tc_port_mqprio *tc_port_mqprio;
	u32 qoffset, qcount, tot_qcount, qid, hwqid;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	struct sge_eosw_txq *eosw_txq;
	int eotid, ret;
	u16 i, j;

	tc_port_mqprio = &adap->tc_mqprio->port_mqprio[pi->port_id];
	for (i = 0; i < mqprio->qopt.num_tc; i++) {
		qoffset = mqprio->qopt.offset[i];
		qcount = mqprio->qopt.count[i];
		for (j = 0; j < qcount; j++) {
			eotid = cxgb4_get_free_eotid(&adap->tids);
			if (eotid < 0) {
				ret = -ENOMEM;
				goto out_free_eotids;
			}

			qid = qoffset + j;
			hwqid = pi->first_qset + (eotid % pi->nqsets);
			eosw_txq = &tc_port_mqprio->eosw_txq[qid];
			ret = cxgb4_init_eosw_txq(dev, eosw_txq,
						  eotid, hwqid);
			if (ret)
				goto out_free_eotids;

			cxgb4_alloc_eotid(&adap->tids, eotid, eosw_txq);
		}
	}

	memcpy(&tc_port_mqprio->mqprio, mqprio,
	       sizeof(struct tc_mqprio_qopt_offload));

	/* Inform the stack about the configured tc params.
	 *
	 * Set the correct queue map. If no queue count has been
	 * specified, then send the traffic through default NIC
	 * queues; instead of ETHOFLD queues.
	 */
	ret = netdev_set_num_tc(dev, mqprio->qopt.num_tc);
	if (ret)
		goto out_free_eotids;

	tot_qcount = pi->nqsets;
	for (i = 0; i < mqprio->qopt.num_tc; i++) {
		qcount = mqprio->qopt.count[i];
		if (qcount) {
			qoffset = mqprio->qopt.offset[i] + pi->nqsets;
		} else {
			qcount = pi->nqsets;
			qoffset = 0;
		}

		ret = netdev_set_tc_queue(dev, i, qcount, qoffset);
		if (ret)
			goto out_reset_tc;

		tot_qcount += mqprio->qopt.count[i];
	}

	ret = netif_set_real_num_tx_queues(dev, tot_qcount);
	if (ret)
		goto out_reset_tc;

	tc_port_mqprio->state = CXGB4_MQPRIO_STATE_ACTIVE;
	return 0;

out_reset_tc:
	netdev_reset_tc(dev);
	i = mqprio->qopt.num_tc;

out_free_eotids:
	while (i-- > 0) {
		qoffset = mqprio->qopt.offset[i];
		qcount = mqprio->qopt.count[i];
		for (j = 0; j < qcount; j++) {
			eosw_txq = &tc_port_mqprio->eosw_txq[qoffset + j];
			cxgb4_free_eotid(&adap->tids, eosw_txq->eotid);
			cxgb4_free_eosw_txq(dev, eosw_txq);
		}
	}

	return ret;
}

static void cxgb4_mqprio_disable_offload(struct net_device *dev)
{
	struct cxgb4_tc_port_mqprio *tc_port_mqprio;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	struct sge_eosw_txq *eosw_txq;
	u32 qoffset, qcount;
	u16 i, j;

	tc_port_mqprio = &adap->tc_mqprio->port_mqprio[pi->port_id];
	if (tc_port_mqprio->state != CXGB4_MQPRIO_STATE_ACTIVE)
		return;

	netdev_reset_tc(dev);
	netif_set_real_num_tx_queues(dev, pi->nqsets);

	for (i = 0; i < tc_port_mqprio->mqprio.qopt.num_tc; i++) {
		qoffset = tc_port_mqprio->mqprio.qopt.offset[i];
		qcount = tc_port_mqprio->mqprio.qopt.count[i];
		for (j = 0; j < qcount; j++) {
			eosw_txq = &tc_port_mqprio->eosw_txq[qoffset + j];
			cxgb4_free_eotid(&adap->tids, eosw_txq->eotid);
			cxgb4_free_eosw_txq(dev, eosw_txq);
		}
	}

	memset(&tc_port_mqprio->mqprio, 0,
	       sizeof(struct tc_mqprio_qopt_offload));

	tc_port_mqprio->state = CXGB4_MQPRIO_STATE_DISABLED;
}

int cxgb4_setup_tc_mqprio(struct net_device *dev,
			  struct tc_mqprio_qopt_offload *mqprio)
{
	bool needs_bring_up = false;
	int ret;

	ret = cxgb4_mqprio_validate(dev, mqprio);
	if (ret)
		return ret;

	/* To configure tc params, the current allocated EOTIDs must
	 * be freed up. However, they can't be freed up if there's
	 * traffic running on the interface. So, ensure interface is
	 * down before configuring tc params.
	 */
	if (netif_running(dev)) {
		cxgb_close(dev);
		needs_bring_up = true;
	}

	cxgb4_mqprio_disable_offload(dev);

	/* If requested for clear, then just return since resources are
	 * already freed up by now.
	 */
	if (!mqprio->qopt.num_tc)
		goto out;

	ret = cxgb4_mqprio_enable_offload(dev, mqprio);

out:
	if (needs_bring_up)
		cxgb_open(dev);

	return ret;
}

int cxgb4_init_tc_mqprio(struct adapter *adap)
{
	struct cxgb4_tc_port_mqprio *tc_port_mqprio, *port_mqprio;
	struct cxgb4_tc_mqprio *tc_mqprio;
	struct sge_eosw_txq *eosw_txq;
	int ret = 0;
	u8 i;

	tc_mqprio = kzalloc(sizeof(*tc_mqprio), GFP_KERNEL);
	if (!tc_mqprio)
		return -ENOMEM;

	tc_port_mqprio = kcalloc(adap->params.nports, sizeof(*tc_port_mqprio),
				 GFP_KERNEL);
	if (!tc_port_mqprio) {
		ret = -ENOMEM;
		goto out_free_mqprio;
	}

	tc_mqprio->port_mqprio = tc_port_mqprio;
	for (i = 0; i < adap->params.nports; i++) {
		port_mqprio = &tc_mqprio->port_mqprio[i];
		eosw_txq = kcalloc(adap->tids.neotids, sizeof(*eosw_txq),
				   GFP_KERNEL);
		if (!eosw_txq) {
			ret = -ENOMEM;
			goto out_free_ports;
		}
		port_mqprio->eosw_txq = eosw_txq;
	}

	adap->tc_mqprio = tc_mqprio;
	return 0;

out_free_ports:
	for (i = 0; i < adap->params.nports; i++) {
		port_mqprio = &tc_mqprio->port_mqprio[i];
		kfree(port_mqprio->eosw_txq);
	}
	kfree(tc_port_mqprio);

out_free_mqprio:
	kfree(tc_mqprio);
	return ret;
}

void cxgb4_cleanup_tc_mqprio(struct adapter *adap)
{
	struct cxgb4_tc_port_mqprio *port_mqprio;
	u8 i;

	if (adap->tc_mqprio) {
		if (adap->tc_mqprio->port_mqprio) {
			for (i = 0; i < adap->params.nports; i++) {
				struct net_device *dev = adap->port[i];

				if (dev)
					cxgb4_mqprio_disable_offload(dev);
				port_mqprio = &adap->tc_mqprio->port_mqprio[i];
				kfree(port_mqprio->eosw_txq);
			}
			kfree(adap->tc_mqprio->port_mqprio);
		}
		kfree(adap->tc_mqprio);
	}
}
