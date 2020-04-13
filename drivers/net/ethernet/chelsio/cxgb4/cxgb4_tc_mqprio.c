// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Chelsio Communications.  All rights reserved. */

#include "cxgb4.h"
#include "cxgb4_tc_mqprio.h"
#include "sched.h"

static int cxgb4_mqprio_validate(struct net_device *dev,
				 struct tc_mqprio_qopt_offload *mqprio)
{
	u64 min_rate = 0, max_rate = 0, max_link_rate;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	u32 speed, qcount = 0, qoffset = 0;
	u32 start_a, start_b, end_a, end_b;
	int ret;
	u8 i, j;

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

	ret = t4_get_link_params(pi, NULL, &speed, NULL);
	if (ret) {
		netdev_err(dev, "Failed to get link speed, ret: %d\n", ret);
		return -EINVAL;
	}

	/* Convert from Mbps to bps */
	max_link_rate = (u64)speed * 1000 * 1000;

	for (i = 0; i < mqprio->qopt.num_tc; i++) {
		qoffset = max_t(u16, mqprio->qopt.offset[i], qoffset);
		qcount += mqprio->qopt.count[i];

		start_a = mqprio->qopt.offset[i];
		end_a = start_a + mqprio->qopt.count[i] - 1;
		for (j = i + 1; j < mqprio->qopt.num_tc; j++) {
			start_b = mqprio->qopt.offset[j];
			end_b = start_b + mqprio->qopt.count[j] - 1;

			/* If queue count is 0, then the traffic
			 * belonging to this class will not use
			 * ETHOFLD queues. So, no need to validate
			 * further.
			 */
			if (!mqprio->qopt.count[i])
				break;

			if (!mqprio->qopt.count[j])
				continue;

			if (max_t(u32, start_a, start_b) <=
			    min_t(u32, end_a, end_b)) {
				netdev_err(dev,
					   "Queues can't overlap across tc\n");
				return -EINVAL;
			}
		}

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
	struct tx_sw_desc *ring;

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
	eosw_txq->flowc_idx = 0;
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

static int cxgb4_mqprio_alloc_hw_resources(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	struct sge_ofld_rxq *eorxq;
	struct sge_eohw_txq *eotxq;
	int ret, msix = 0;
	u32 i;

	/* Allocate ETHOFLD hardware queue structures if not done already */
	if (!refcount_read(&adap->tc_mqprio->refcnt)) {
		adap->sge.eohw_rxq = kcalloc(adap->sge.eoqsets,
					     sizeof(struct sge_ofld_rxq),
					     GFP_KERNEL);
		if (!adap->sge.eohw_rxq)
			return -ENOMEM;

		adap->sge.eohw_txq = kcalloc(adap->sge.eoqsets,
					     sizeof(struct sge_eohw_txq),
					     GFP_KERNEL);
		if (!adap->sge.eohw_txq) {
			kfree(adap->sge.eohw_rxq);
			return -ENOMEM;
		}

		refcount_set(&adap->tc_mqprio->refcnt, 1);
	} else {
		refcount_inc(&adap->tc_mqprio->refcnt);
	}

	if (!(adap->flags & CXGB4_USING_MSIX))
		msix = -((int)adap->sge.intrq.abs_id + 1);

	for (i = 0; i < pi->nqsets; i++) {
		eorxq = &adap->sge.eohw_rxq[pi->first_qset + i];
		eotxq = &adap->sge.eohw_txq[pi->first_qset + i];

		/* Allocate Rxqs for receiving ETHOFLD Tx completions */
		if (msix >= 0) {
			msix = cxgb4_get_msix_idx_from_bmap(adap);
			if (msix < 0) {
				ret = msix;
				goto out_free_queues;
			}

			eorxq->msix = &adap->msix_info[msix];
			snprintf(eorxq->msix->desc,
				 sizeof(eorxq->msix->desc),
				 "%s-eorxq%d", dev->name, i);
		}

		init_rspq(adap, &eorxq->rspq,
			  CXGB4_EOHW_RXQ_DEFAULT_INTR_USEC,
			  CXGB4_EOHW_RXQ_DEFAULT_PKT_CNT,
			  CXGB4_EOHW_RXQ_DEFAULT_DESC_NUM,
			  CXGB4_EOHW_RXQ_DEFAULT_DESC_SIZE);

		eorxq->fl.size = CXGB4_EOHW_FLQ_DEFAULT_DESC_NUM;

		ret = t4_sge_alloc_rxq(adap, &eorxq->rspq, false,
				       dev, msix, &eorxq->fl,
				       cxgb4_ethofld_rx_handler,
				       NULL, 0);
		if (ret)
			goto out_free_queues;

		/* Allocate ETHOFLD hardware Txqs */
		eotxq->q.size = CXGB4_EOHW_TXQ_DEFAULT_DESC_NUM;
		ret = t4_sge_alloc_ethofld_txq(adap, eotxq, dev,
					       eorxq->rspq.cntxt_id);
		if (ret)
			goto out_free_queues;

		/* Allocate IRQs, set IRQ affinity, and start Rx */
		if (adap->flags & CXGB4_USING_MSIX) {
			ret = request_irq(eorxq->msix->vec, t4_sge_intr_msix, 0,
					  eorxq->msix->desc, &eorxq->rspq);
			if (ret)
				goto out_free_msix;

			cxgb4_set_msix_aff(adap, eorxq->msix->vec,
					   &eorxq->msix->aff_mask, i);
		}

		if (adap->flags & CXGB4_FULL_INIT_DONE)
			cxgb4_enable_rx(adap, &eorxq->rspq);
	}

	return 0;

out_free_msix:
	while (i-- > 0) {
		eorxq = &adap->sge.eohw_rxq[pi->first_qset + i];

		if (adap->flags & CXGB4_FULL_INIT_DONE)
			cxgb4_quiesce_rx(&eorxq->rspq);

		if (adap->flags & CXGB4_USING_MSIX) {
			cxgb4_clear_msix_aff(eorxq->msix->vec,
					     eorxq->msix->aff_mask);
			free_irq(eorxq->msix->vec, &eorxq->rspq);
		}
	}

out_free_queues:
	for (i = 0; i < pi->nqsets; i++) {
		eorxq = &adap->sge.eohw_rxq[pi->first_qset + i];
		eotxq = &adap->sge.eohw_txq[pi->first_qset + i];

		if (eorxq->rspq.desc)
			free_rspq_fl(adap, &eorxq->rspq, &eorxq->fl);
		if (eorxq->msix)
			cxgb4_free_msix_idx_in_bmap(adap, eorxq->msix->idx);
		t4_sge_free_ethofld_txq(adap, eotxq);
	}

	if (refcount_dec_and_test(&adap->tc_mqprio->refcnt)) {
		kfree(adap->sge.eohw_txq);
		kfree(adap->sge.eohw_rxq);
	}
	return ret;
}

static void cxgb4_mqprio_free_hw_resources(struct net_device *dev)
{
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	struct sge_ofld_rxq *eorxq;
	struct sge_eohw_txq *eotxq;
	u32 i;

	/* Return if no ETHOFLD structures have been allocated yet */
	if (!refcount_read(&adap->tc_mqprio->refcnt))
		return;

	/* Return if no hardware queues have been allocated */
	if (!adap->sge.eohw_rxq[pi->first_qset].rspq.desc)
		return;

	for (i = 0; i < pi->nqsets; i++) {
		eorxq = &adap->sge.eohw_rxq[pi->first_qset + i];
		eotxq = &adap->sge.eohw_txq[pi->first_qset + i];

		/* Device removal path will already disable NAPI
		 * before unregistering netdevice. So, only disable
		 * NAPI if we're not in device removal path
		 */
		if (!(adap->flags & CXGB4_SHUTTING_DOWN))
			cxgb4_quiesce_rx(&eorxq->rspq);

		if (adap->flags & CXGB4_USING_MSIX) {
			cxgb4_clear_msix_aff(eorxq->msix->vec,
					     eorxq->msix->aff_mask);
			free_irq(eorxq->msix->vec, &eorxq->rspq);
			cxgb4_free_msix_idx_in_bmap(adap, eorxq->msix->idx);
		}

		free_rspq_fl(adap, &eorxq->rspq, &eorxq->fl);
		t4_sge_free_ethofld_txq(adap, eotxq);
	}

	/* Free up ETHOFLD structures if there are no users */
	if (refcount_dec_and_test(&adap->tc_mqprio->refcnt)) {
		kfree(adap->sge.eohw_txq);
		kfree(adap->sge.eohw_rxq);
	}
}

static int cxgb4_mqprio_alloc_tc(struct net_device *dev,
				 struct tc_mqprio_qopt_offload *mqprio)
{
	struct ch_sched_params p = {
		.type = SCHED_CLASS_TYPE_PACKET,
		.u.params.level = SCHED_CLASS_LEVEL_CL_RL,
		.u.params.mode = SCHED_CLASS_MODE_FLOW,
		.u.params.rateunit = SCHED_CLASS_RATEUNIT_BITS,
		.u.params.ratemode = SCHED_CLASS_RATEMODE_ABS,
		.u.params.class = SCHED_CLS_NONE,
		.u.params.weight = 0,
		.u.params.pktsize = dev->mtu,
	};
	struct cxgb4_tc_port_mqprio *tc_port_mqprio;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	struct sched_class *e;
	int ret;
	u8 i;

	tc_port_mqprio = &adap->tc_mqprio->port_mqprio[pi->port_id];
	p.u.params.channel = pi->tx_chan;
	for (i = 0; i < mqprio->qopt.num_tc; i++) {
		/* Convert from bytes per second to Kbps */
		p.u.params.minrate = div_u64(mqprio->min_rate[i] * 8, 1000);
		p.u.params.maxrate = div_u64(mqprio->max_rate[i] * 8, 1000);

		e = cxgb4_sched_class_alloc(dev, &p);
		if (!e) {
			ret = -ENOMEM;
			goto out_err;
		}

		tc_port_mqprio->tc_hwtc_map[i] = e->idx;
	}

	return 0;

out_err:
	while (i--)
		cxgb4_sched_class_free(dev, tc_port_mqprio->tc_hwtc_map[i]);

	return ret;
}

static void cxgb4_mqprio_free_tc(struct net_device *dev)
{
	struct cxgb4_tc_port_mqprio *tc_port_mqprio;
	struct port_info *pi = netdev2pinfo(dev);
	struct adapter *adap = netdev2adap(dev);
	u8 i;

	tc_port_mqprio = &adap->tc_mqprio->port_mqprio[pi->port_id];
	for (i = 0; i < tc_port_mqprio->mqprio.qopt.num_tc; i++)
		cxgb4_sched_class_free(dev, tc_port_mqprio->tc_hwtc_map[i]);
}

static int cxgb4_mqprio_class_bind(struct net_device *dev,
				   struct sge_eosw_txq *eosw_txq,
				   u8 tc)
{
	struct ch_sched_flowc fe;
	int ret;

	init_completion(&eosw_txq->completion);

	fe.tid = eosw_txq->eotid;
	fe.class = tc;

	ret = cxgb4_sched_class_bind(dev, &fe, SCHED_FLOWC);
	if (ret)
		return ret;

	ret = wait_for_completion_timeout(&eosw_txq->completion,
					  CXGB4_FLOWC_WAIT_TIMEOUT);
	if (!ret)
		return -ETIMEDOUT;

	return 0;
}

static void cxgb4_mqprio_class_unbind(struct net_device *dev,
				      struct sge_eosw_txq *eosw_txq,
				      u8 tc)
{
	struct adapter *adap = netdev2adap(dev);
	struct ch_sched_flowc fe;

	/* If we're shutting down, interrupts are disabled and no completions
	 * come back. So, skip waiting for completions in this scenario.
	 */
	if (!(adap->flags & CXGB4_SHUTTING_DOWN))
		init_completion(&eosw_txq->completion);

	fe.tid = eosw_txq->eotid;
	fe.class = tc;
	cxgb4_sched_class_unbind(dev, &fe, SCHED_FLOWC);

	if (!(adap->flags & CXGB4_SHUTTING_DOWN))
		wait_for_completion_timeout(&eosw_txq->completion,
					    CXGB4_FLOWC_WAIT_TIMEOUT);
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
	u8 hwtc;

	ret = cxgb4_mqprio_alloc_hw_resources(dev);
	if (ret)
		return -ENOMEM;

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

			hwtc = tc_port_mqprio->tc_hwtc_map[i];
			ret = cxgb4_mqprio_class_bind(dev, eosw_txq, hwtc);
			if (ret)
				goto out_free_eotids;
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

			hwtc = tc_port_mqprio->tc_hwtc_map[i];
			cxgb4_mqprio_class_unbind(dev, eosw_txq, hwtc);

			cxgb4_free_eotid(&adap->tids, eosw_txq->eotid);
			cxgb4_free_eosw_txq(dev, eosw_txq);
		}
	}

	cxgb4_mqprio_free_hw_resources(dev);
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
	u8 hwtc;

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

			hwtc = tc_port_mqprio->tc_hwtc_map[i];
			cxgb4_mqprio_class_unbind(dev, eosw_txq, hwtc);

			cxgb4_free_eotid(&adap->tids, eosw_txq->eotid);
			cxgb4_free_eosw_txq(dev, eosw_txq);
		}
	}

	cxgb4_mqprio_free_hw_resources(dev);

	/* Free up the traffic classes */
	cxgb4_mqprio_free_tc(dev);

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

	/* Allocate free available traffic classes and configure
	 * their rate parameters.
	 */
	ret = cxgb4_mqprio_alloc_tc(dev, mqprio);
	if (ret)
		goto out;

	ret = cxgb4_mqprio_enable_offload(dev, mqprio);
	if (ret) {
		cxgb4_mqprio_free_tc(dev);
		goto out;
	}

out:
	if (needs_bring_up)
		cxgb_open(dev);

	return ret;
}

void cxgb4_mqprio_stop_offload(struct adapter *adap)
{
	struct cxgb4_tc_port_mqprio *tc_port_mqprio;
	struct net_device *dev;
	u8 i;

	if (!adap->tc_mqprio || !adap->tc_mqprio->port_mqprio)
		return;

	for_each_port(adap, i) {
		dev = adap->port[i];
		if (!dev)
			continue;

		tc_port_mqprio = &adap->tc_mqprio->port_mqprio[i];
		if (!tc_port_mqprio->mqprio.qopt.num_tc)
			continue;

		cxgb4_mqprio_disable_offload(dev);
	}
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
	refcount_set(&adap->tc_mqprio->refcnt, 0);
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
