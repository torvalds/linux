// SPDX-License-Identifier: GPL-2.0
/*
 * K3 NAVSS DMA glue interface
 *
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com
 *
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/ti/k3-ringacc.h>
#include <linux/dma/ti-cppi5.h>
#include <linux/dma/k3-udma-glue.h>

#include "k3-udma.h"
#include "k3-psil-priv.h"

struct k3_udma_glue_common {
	struct device *dev;
	struct device chan_dev;
	struct udma_dev *udmax;
	const struct udma_tisci_rm *tisci_rm;
	struct k3_ringacc *ringacc;
	u32 src_thread;
	u32 dst_thread;

	u32  hdesc_size;
	bool epib;
	u32  psdata_size;
	u32  swdata_size;
	u32  atype_asel;
	struct psil_endpoint_config *ep_config;
};

struct k3_udma_glue_tx_channel {
	struct k3_udma_glue_common common;

	struct udma_tchan *udma_tchanx;
	int udma_tchan_id;

	struct k3_ring *ringtx;
	struct k3_ring *ringtxcq;

	bool psil_paired;

	int virq;

	atomic_t free_pkts;
	bool tx_pause_on_err;
	bool tx_filt_einfo;
	bool tx_filt_pswords;
	bool tx_supr_tdpkt;

	int udma_tflow_id;
};

struct k3_udma_glue_rx_flow {
	struct udma_rflow *udma_rflow;
	int udma_rflow_id;
	struct k3_ring *ringrx;
	struct k3_ring *ringrxfdq;

	int virq;
};

struct k3_udma_glue_rx_channel {
	struct k3_udma_glue_common common;

	struct udma_rchan *udma_rchanx;
	int udma_rchan_id;
	bool remote;

	bool psil_paired;

	u32  swdata_size;
	int  flow_id_base;

	struct k3_udma_glue_rx_flow *flows;
	u32 flow_num;
	u32 flows_ready;
};

static void k3_udma_chan_dev_release(struct device *dev)
{
	/* The struct containing the device is devm managed */
}

static struct class k3_udma_glue_devclass = {
	.name		= "k3_udma_glue_chan",
	.dev_release	= k3_udma_chan_dev_release,
};

#define K3_UDMAX_TDOWN_TIMEOUT_US 1000

static int of_k3_udma_glue_parse(struct device_node *udmax_np,
				 struct k3_udma_glue_common *common)
{
	common->udmax = of_xudma_dev_get(udmax_np, NULL);
	if (IS_ERR(common->udmax))
		return PTR_ERR(common->udmax);

	common->ringacc = xudma_get_ringacc(common->udmax);
	common->tisci_rm = xudma_dev_get_tisci_rm(common->udmax);

	return 0;
}

static int of_k3_udma_glue_parse_chn(struct device_node *chn_np,
		const char *name, struct k3_udma_glue_common *common,
		bool tx_chn)
{
	struct of_phandle_args dma_spec;
	u32 thread_id;
	int ret = 0;
	int index;

	if (unlikely(!name))
		return -EINVAL;

	index = of_property_match_string(chn_np, "dma-names", name);
	if (index < 0)
		return index;

	if (of_parse_phandle_with_args(chn_np, "dmas", "#dma-cells", index,
				       &dma_spec))
		return -ENOENT;

	ret = of_k3_udma_glue_parse(dma_spec.np, common);
	if (ret)
		goto out_put_spec;

	thread_id = dma_spec.args[0];
	if (dma_spec.args_count == 2) {
		if (dma_spec.args[1] > 2 && !xudma_is_pktdma(common->udmax)) {
			dev_err(common->dev, "Invalid channel atype: %u\n",
				dma_spec.args[1]);
			ret = -EINVAL;
			goto out_put_spec;
		}
		if (dma_spec.args[1] > 15 && xudma_is_pktdma(common->udmax)) {
			dev_err(common->dev, "Invalid channel asel: %u\n",
				dma_spec.args[1]);
			ret = -EINVAL;
			goto out_put_spec;
		}

		common->atype_asel = dma_spec.args[1];
	}

	if (tx_chn && !(thread_id & K3_PSIL_DST_THREAD_ID_OFFSET)) {
		ret = -EINVAL;
		goto out_put_spec;
	}

	if (!tx_chn && (thread_id & K3_PSIL_DST_THREAD_ID_OFFSET)) {
		ret = -EINVAL;
		goto out_put_spec;
	}

	/* get psil endpoint config */
	common->ep_config = psil_get_ep_config(thread_id);
	if (IS_ERR(common->ep_config)) {
		dev_err(common->dev,
			"No configuration for psi-l thread 0x%04x\n",
			thread_id);
		ret = PTR_ERR(common->ep_config);
		goto out_put_spec;
	}

	common->epib = common->ep_config->needs_epib;
	common->psdata_size = common->ep_config->psd_size;

	if (tx_chn)
		common->dst_thread = thread_id;
	else
		common->src_thread = thread_id;

out_put_spec:
	of_node_put(dma_spec.np);
	return ret;
};

static void k3_udma_glue_dump_tx_chn(struct k3_udma_glue_tx_channel *tx_chn)
{
	struct device *dev = tx_chn->common.dev;

	dev_dbg(dev, "dump_tx_chn:\n"
		"udma_tchan_id: %d\n"
		"src_thread: %08x\n"
		"dst_thread: %08x\n",
		tx_chn->udma_tchan_id,
		tx_chn->common.src_thread,
		tx_chn->common.dst_thread);
}

static void k3_udma_glue_dump_tx_rt_chn(struct k3_udma_glue_tx_channel *chn,
					char *mark)
{
	struct device *dev = chn->common.dev;

	dev_dbg(dev, "=== dump ===> %s\n", mark);
	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_CTL_REG,
		xudma_tchanrt_read(chn->udma_tchanx, UDMA_CHAN_RT_CTL_REG));
	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_PEER_RT_EN_REG,
		xudma_tchanrt_read(chn->udma_tchanx,
				   UDMA_CHAN_RT_PEER_RT_EN_REG));
	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_PCNT_REG,
		xudma_tchanrt_read(chn->udma_tchanx, UDMA_CHAN_RT_PCNT_REG));
	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_BCNT_REG,
		xudma_tchanrt_read(chn->udma_tchanx, UDMA_CHAN_RT_BCNT_REG));
	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_SBCNT_REG,
		xudma_tchanrt_read(chn->udma_tchanx, UDMA_CHAN_RT_SBCNT_REG));
}

static int k3_udma_glue_cfg_tx_chn(struct k3_udma_glue_tx_channel *tx_chn)
{
	const struct udma_tisci_rm *tisci_rm = tx_chn->common.tisci_rm;
	struct ti_sci_msg_rm_udmap_tx_ch_cfg req;

	memset(&req, 0, sizeof(req));

	req.valid_params = TI_SCI_MSG_VALUE_RM_UDMAP_CH_PAUSE_ON_ERR_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_FILT_EINFO_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_FILT_PSWORDS_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_CH_CHAN_TYPE_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_CH_TX_SUPR_TDPKT_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_CH_FETCH_SIZE_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_CH_CQ_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_CH_ATYPE_VALID;
	req.nav_id = tisci_rm->tisci_dev_id;
	req.index = tx_chn->udma_tchan_id;
	if (tx_chn->tx_pause_on_err)
		req.tx_pause_on_err = 1;
	if (tx_chn->tx_filt_einfo)
		req.tx_filt_einfo = 1;
	if (tx_chn->tx_filt_pswords)
		req.tx_filt_pswords = 1;
	req.tx_chan_type = TI_SCI_RM_UDMAP_CHAN_TYPE_PKT_PBRR;
	if (tx_chn->tx_supr_tdpkt)
		req.tx_supr_tdpkt = 1;
	req.tx_fetch_size = tx_chn->common.hdesc_size >> 2;
	req.txcq_qnum = k3_ringacc_get_ring_id(tx_chn->ringtxcq);
	req.tx_atype = tx_chn->common.atype_asel;

	return tisci_rm->tisci_udmap_ops->tx_ch_cfg(tisci_rm->tisci, &req);
}

struct k3_udma_glue_tx_channel *k3_udma_glue_request_tx_chn(struct device *dev,
		const char *name, struct k3_udma_glue_tx_channel_cfg *cfg)
{
	struct k3_udma_glue_tx_channel *tx_chn;
	int ret;

	tx_chn = devm_kzalloc(dev, sizeof(*tx_chn), GFP_KERNEL);
	if (!tx_chn)
		return ERR_PTR(-ENOMEM);

	tx_chn->common.dev = dev;
	tx_chn->common.swdata_size = cfg->swdata_size;
	tx_chn->tx_pause_on_err = cfg->tx_pause_on_err;
	tx_chn->tx_filt_einfo = cfg->tx_filt_einfo;
	tx_chn->tx_filt_pswords = cfg->tx_filt_pswords;
	tx_chn->tx_supr_tdpkt = cfg->tx_supr_tdpkt;

	/* parse of udmap channel */
	ret = of_k3_udma_glue_parse_chn(dev->of_node, name,
					&tx_chn->common, true);
	if (ret)
		goto err;

	tx_chn->common.hdesc_size = cppi5_hdesc_calc_size(tx_chn->common.epib,
						tx_chn->common.psdata_size,
						tx_chn->common.swdata_size);

	if (xudma_is_pktdma(tx_chn->common.udmax))
		tx_chn->udma_tchan_id = tx_chn->common.ep_config->mapped_channel_id;
	else
		tx_chn->udma_tchan_id = -1;

	/* request and cfg UDMAP TX channel */
	tx_chn->udma_tchanx = xudma_tchan_get(tx_chn->common.udmax,
					      tx_chn->udma_tchan_id);
	if (IS_ERR(tx_chn->udma_tchanx)) {
		ret = PTR_ERR(tx_chn->udma_tchanx);
		dev_err(dev, "UDMAX tchanx get err %d\n", ret);
		goto err;
	}
	tx_chn->udma_tchan_id = xudma_tchan_get_id(tx_chn->udma_tchanx);

	tx_chn->common.chan_dev.class = &k3_udma_glue_devclass;
	tx_chn->common.chan_dev.parent = xudma_get_device(tx_chn->common.udmax);
	dev_set_name(&tx_chn->common.chan_dev, "tchan%d-0x%04x",
		     tx_chn->udma_tchan_id, tx_chn->common.dst_thread);
	ret = device_register(&tx_chn->common.chan_dev);
	if (ret) {
		dev_err(dev, "Channel Device registration failed %d\n", ret);
		put_device(&tx_chn->common.chan_dev);
		tx_chn->common.chan_dev.parent = NULL;
		goto err;
	}

	if (xudma_is_pktdma(tx_chn->common.udmax)) {
		/* prepare the channel device as coherent */
		tx_chn->common.chan_dev.dma_coherent = true;
		dma_coerce_mask_and_coherent(&tx_chn->common.chan_dev,
					     DMA_BIT_MASK(48));
	}

	atomic_set(&tx_chn->free_pkts, cfg->txcq_cfg.size);

	if (xudma_is_pktdma(tx_chn->common.udmax))
		tx_chn->udma_tflow_id = tx_chn->common.ep_config->default_flow_id;
	else
		tx_chn->udma_tflow_id = tx_chn->udma_tchan_id;

	/* request and cfg rings */
	ret =  k3_ringacc_request_rings_pair(tx_chn->common.ringacc,
					     tx_chn->udma_tflow_id, -1,
					     &tx_chn->ringtx,
					     &tx_chn->ringtxcq);
	if (ret) {
		dev_err(dev, "Failed to get TX/TXCQ rings %d\n", ret);
		goto err;
	}

	/* Set the dma_dev for the rings to be configured */
	cfg->tx_cfg.dma_dev = k3_udma_glue_tx_get_dma_device(tx_chn);
	cfg->txcq_cfg.dma_dev = cfg->tx_cfg.dma_dev;

	/* Set the ASEL value for DMA rings of PKTDMA */
	if (xudma_is_pktdma(tx_chn->common.udmax)) {
		cfg->tx_cfg.asel = tx_chn->common.atype_asel;
		cfg->txcq_cfg.asel = tx_chn->common.atype_asel;
	}

	ret = k3_ringacc_ring_cfg(tx_chn->ringtx, &cfg->tx_cfg);
	if (ret) {
		dev_err(dev, "Failed to cfg ringtx %d\n", ret);
		goto err;
	}

	ret = k3_ringacc_ring_cfg(tx_chn->ringtxcq, &cfg->txcq_cfg);
	if (ret) {
		dev_err(dev, "Failed to cfg ringtx %d\n", ret);
		goto err;
	}

	/* request and cfg psi-l */
	tx_chn->common.src_thread =
			xudma_dev_get_psil_base(tx_chn->common.udmax) +
			tx_chn->udma_tchan_id;

	ret = k3_udma_glue_cfg_tx_chn(tx_chn);
	if (ret) {
		dev_err(dev, "Failed to cfg tchan %d\n", ret);
		goto err;
	}

	k3_udma_glue_dump_tx_chn(tx_chn);

	return tx_chn;

err:
	k3_udma_glue_release_tx_chn(tx_chn);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_request_tx_chn);

void k3_udma_glue_release_tx_chn(struct k3_udma_glue_tx_channel *tx_chn)
{
	if (tx_chn->psil_paired) {
		xudma_navss_psil_unpair(tx_chn->common.udmax,
					tx_chn->common.src_thread,
					tx_chn->common.dst_thread);
		tx_chn->psil_paired = false;
	}

	if (!IS_ERR_OR_NULL(tx_chn->udma_tchanx))
		xudma_tchan_put(tx_chn->common.udmax,
				tx_chn->udma_tchanx);

	if (tx_chn->ringtxcq)
		k3_ringacc_ring_free(tx_chn->ringtxcq);

	if (tx_chn->ringtx)
		k3_ringacc_ring_free(tx_chn->ringtx);

	if (tx_chn->common.chan_dev.parent) {
		device_unregister(&tx_chn->common.chan_dev);
		tx_chn->common.chan_dev.parent = NULL;
	}
}
EXPORT_SYMBOL_GPL(k3_udma_glue_release_tx_chn);

int k3_udma_glue_push_tx_chn(struct k3_udma_glue_tx_channel *tx_chn,
			     struct cppi5_host_desc_t *desc_tx,
			     dma_addr_t desc_dma)
{
	u32 ringtxcq_id;

	if (!atomic_add_unless(&tx_chn->free_pkts, -1, 0))
		return -ENOMEM;

	ringtxcq_id = k3_ringacc_get_ring_id(tx_chn->ringtxcq);
	cppi5_desc_set_retpolicy(&desc_tx->hdr, 0, ringtxcq_id);

	return k3_ringacc_ring_push(tx_chn->ringtx, &desc_dma);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_push_tx_chn);

int k3_udma_glue_pop_tx_chn(struct k3_udma_glue_tx_channel *tx_chn,
			    dma_addr_t *desc_dma)
{
	int ret;

	ret = k3_ringacc_ring_pop(tx_chn->ringtxcq, desc_dma);
	if (!ret)
		atomic_inc(&tx_chn->free_pkts);

	return ret;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_pop_tx_chn);

int k3_udma_glue_enable_tx_chn(struct k3_udma_glue_tx_channel *tx_chn)
{
	int ret;

	ret = xudma_navss_psil_pair(tx_chn->common.udmax,
				    tx_chn->common.src_thread,
				    tx_chn->common.dst_thread);
	if (ret) {
		dev_err(tx_chn->common.dev, "PSI-L request err %d\n", ret);
		return ret;
	}

	tx_chn->psil_paired = true;

	xudma_tchanrt_write(tx_chn->udma_tchanx, UDMA_CHAN_RT_PEER_RT_EN_REG,
			    UDMA_PEER_RT_EN_ENABLE);

	xudma_tchanrt_write(tx_chn->udma_tchanx, UDMA_CHAN_RT_CTL_REG,
			    UDMA_CHAN_RT_CTL_EN);

	k3_udma_glue_dump_tx_rt_chn(tx_chn, "txchn en");
	return 0;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_enable_tx_chn);

void k3_udma_glue_disable_tx_chn(struct k3_udma_glue_tx_channel *tx_chn)
{
	k3_udma_glue_dump_tx_rt_chn(tx_chn, "txchn dis1");

	xudma_tchanrt_write(tx_chn->udma_tchanx, UDMA_CHAN_RT_CTL_REG, 0);

	xudma_tchanrt_write(tx_chn->udma_tchanx,
			    UDMA_CHAN_RT_PEER_RT_EN_REG, 0);
	k3_udma_glue_dump_tx_rt_chn(tx_chn, "txchn dis2");

	if (tx_chn->psil_paired) {
		xudma_navss_psil_unpair(tx_chn->common.udmax,
					tx_chn->common.src_thread,
					tx_chn->common.dst_thread);
		tx_chn->psil_paired = false;
	}
}
EXPORT_SYMBOL_GPL(k3_udma_glue_disable_tx_chn);

void k3_udma_glue_tdown_tx_chn(struct k3_udma_glue_tx_channel *tx_chn,
			       bool sync)
{
	int i = 0;
	u32 val;

	k3_udma_glue_dump_tx_rt_chn(tx_chn, "txchn tdown1");

	xudma_tchanrt_write(tx_chn->udma_tchanx, UDMA_CHAN_RT_CTL_REG,
			    UDMA_CHAN_RT_CTL_EN | UDMA_CHAN_RT_CTL_TDOWN);

	val = xudma_tchanrt_read(tx_chn->udma_tchanx, UDMA_CHAN_RT_CTL_REG);

	while (sync && (val & UDMA_CHAN_RT_CTL_EN)) {
		val = xudma_tchanrt_read(tx_chn->udma_tchanx,
					 UDMA_CHAN_RT_CTL_REG);
		udelay(1);
		if (i > K3_UDMAX_TDOWN_TIMEOUT_US) {
			dev_err(tx_chn->common.dev, "TX tdown timeout\n");
			break;
		}
		i++;
	}

	val = xudma_tchanrt_read(tx_chn->udma_tchanx,
				 UDMA_CHAN_RT_PEER_RT_EN_REG);
	if (sync && (val & UDMA_PEER_RT_EN_ENABLE))
		dev_err(tx_chn->common.dev, "TX tdown peer not stopped\n");
	k3_udma_glue_dump_tx_rt_chn(tx_chn, "txchn tdown2");
}
EXPORT_SYMBOL_GPL(k3_udma_glue_tdown_tx_chn);

void k3_udma_glue_reset_tx_chn(struct k3_udma_glue_tx_channel *tx_chn,
			       void *data,
			       void (*cleanup)(void *data, dma_addr_t desc_dma))
{
	struct device *dev = tx_chn->common.dev;
	dma_addr_t desc_dma;
	int occ_tx, i, ret;

	/*
	 * TXQ reset need to be special way as it is input for udma and its
	 * state cached by udma, so:
	 * 1) save TXQ occ
	 * 2) clean up TXQ and call callback .cleanup() for each desc
	 * 3) reset TXQ in a special way
	 */
	occ_tx = k3_ringacc_ring_get_occ(tx_chn->ringtx);
	dev_dbg(dev, "TX reset occ_tx %u\n", occ_tx);

	for (i = 0; i < occ_tx; i++) {
		ret = k3_ringacc_ring_pop(tx_chn->ringtx, &desc_dma);
		if (ret) {
			if (ret != -ENODATA)
				dev_err(dev, "TX reset pop %d\n", ret);
			break;
		}
		cleanup(data, desc_dma);
	}

	/* reset TXCQ as it is not input for udma - expected to be empty */
	k3_ringacc_ring_reset(tx_chn->ringtxcq);
	k3_ringacc_ring_reset_dma(tx_chn->ringtx, occ_tx);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_reset_tx_chn);

u32 k3_udma_glue_tx_get_hdesc_size(struct k3_udma_glue_tx_channel *tx_chn)
{
	return tx_chn->common.hdesc_size;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_tx_get_hdesc_size);

u32 k3_udma_glue_tx_get_txcq_id(struct k3_udma_glue_tx_channel *tx_chn)
{
	return k3_ringacc_get_ring_id(tx_chn->ringtxcq);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_tx_get_txcq_id);

int k3_udma_glue_tx_get_irq(struct k3_udma_glue_tx_channel *tx_chn)
{
	if (xudma_is_pktdma(tx_chn->common.udmax)) {
		tx_chn->virq = xudma_pktdma_tflow_get_irq(tx_chn->common.udmax,
							  tx_chn->udma_tflow_id);
	} else {
		tx_chn->virq = k3_ringacc_get_ring_irq_num(tx_chn->ringtxcq);
	}

	return tx_chn->virq;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_tx_get_irq);

struct device *
	k3_udma_glue_tx_get_dma_device(struct k3_udma_glue_tx_channel *tx_chn)
{
	if (xudma_is_pktdma(tx_chn->common.udmax) &&
	    (tx_chn->common.atype_asel == 14 || tx_chn->common.atype_asel == 15))
		return &tx_chn->common.chan_dev;

	return xudma_get_device(tx_chn->common.udmax);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_tx_get_dma_device);

void k3_udma_glue_tx_dma_to_cppi5_addr(struct k3_udma_glue_tx_channel *tx_chn,
				       dma_addr_t *addr)
{
	if (!xudma_is_pktdma(tx_chn->common.udmax) ||
	    !tx_chn->common.atype_asel)
		return;

	*addr |= (u64)tx_chn->common.atype_asel << K3_ADDRESS_ASEL_SHIFT;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_tx_dma_to_cppi5_addr);

void k3_udma_glue_tx_cppi5_to_dma_addr(struct k3_udma_glue_tx_channel *tx_chn,
				       dma_addr_t *addr)
{
	if (!xudma_is_pktdma(tx_chn->common.udmax) ||
	    !tx_chn->common.atype_asel)
		return;

	*addr &= (u64)GENMASK(K3_ADDRESS_ASEL_SHIFT - 1, 0);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_tx_cppi5_to_dma_addr);

static int k3_udma_glue_cfg_rx_chn(struct k3_udma_glue_rx_channel *rx_chn)
{
	const struct udma_tisci_rm *tisci_rm = rx_chn->common.tisci_rm;
	struct ti_sci_msg_rm_udmap_rx_ch_cfg req;
	int ret;

	memset(&req, 0, sizeof(req));

	req.valid_params = TI_SCI_MSG_VALUE_RM_UDMAP_CH_FETCH_SIZE_VALID |
			   TI_SCI_MSG_VALUE_RM_UDMAP_CH_CQ_QNUM_VALID |
			   TI_SCI_MSG_VALUE_RM_UDMAP_CH_CHAN_TYPE_VALID |
			   TI_SCI_MSG_VALUE_RM_UDMAP_CH_ATYPE_VALID;

	req.nav_id = tisci_rm->tisci_dev_id;
	req.index = rx_chn->udma_rchan_id;
	req.rx_fetch_size = rx_chn->common.hdesc_size >> 2;
	/*
	 * TODO: we can't support rxcq_qnum/RCHAN[a]_RCQ cfg with current sysfw
	 * and udmax impl, so just configure it to invalid value.
	 * req.rxcq_qnum = k3_ringacc_get_ring_id(rx_chn->flows[0].ringrx);
	 */
	req.rxcq_qnum = 0xFFFF;
	if (!xudma_is_pktdma(rx_chn->common.udmax) && rx_chn->flow_num &&
	    rx_chn->flow_id_base != rx_chn->udma_rchan_id) {
		/* Default flow + extra ones */
		req.valid_params |= TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_FLOWID_START_VALID |
				    TI_SCI_MSG_VALUE_RM_UDMAP_CH_RX_FLOWID_CNT_VALID;
		req.flowid_start = rx_chn->flow_id_base;
		req.flowid_cnt = rx_chn->flow_num;
	}
	req.rx_chan_type = TI_SCI_RM_UDMAP_CHAN_TYPE_PKT_PBRR;
	req.rx_atype = rx_chn->common.atype_asel;

	ret = tisci_rm->tisci_udmap_ops->rx_ch_cfg(tisci_rm->tisci, &req);
	if (ret)
		dev_err(rx_chn->common.dev, "rchan%d cfg failed %d\n",
			rx_chn->udma_rchan_id, ret);

	return ret;
}

static void k3_udma_glue_release_rx_flow(struct k3_udma_glue_rx_channel *rx_chn,
					 u32 flow_num)
{
	struct k3_udma_glue_rx_flow *flow = &rx_chn->flows[flow_num];

	if (IS_ERR_OR_NULL(flow->udma_rflow))
		return;

	if (flow->ringrxfdq)
		k3_ringacc_ring_free(flow->ringrxfdq);

	if (flow->ringrx)
		k3_ringacc_ring_free(flow->ringrx);

	xudma_rflow_put(rx_chn->common.udmax, flow->udma_rflow);
	flow->udma_rflow = NULL;
	rx_chn->flows_ready--;
}

static int k3_udma_glue_cfg_rx_flow(struct k3_udma_glue_rx_channel *rx_chn,
				    u32 flow_idx,
				    struct k3_udma_glue_rx_flow_cfg *flow_cfg)
{
	struct k3_udma_glue_rx_flow *flow = &rx_chn->flows[flow_idx];
	const struct udma_tisci_rm *tisci_rm = rx_chn->common.tisci_rm;
	struct device *dev = rx_chn->common.dev;
	struct ti_sci_msg_rm_udmap_flow_cfg req;
	int rx_ring_id;
	int rx_ringfdq_id;
	int ret = 0;

	flow->udma_rflow = xudma_rflow_get(rx_chn->common.udmax,
					   flow->udma_rflow_id);
	if (IS_ERR(flow->udma_rflow)) {
		ret = PTR_ERR(flow->udma_rflow);
		dev_err(dev, "UDMAX rflow get err %d\n", ret);
		return ret;
	}

	if (flow->udma_rflow_id != xudma_rflow_get_id(flow->udma_rflow)) {
		ret = -ENODEV;
		goto err_rflow_put;
	}

	if (xudma_is_pktdma(rx_chn->common.udmax)) {
		rx_ringfdq_id = flow->udma_rflow_id +
				xudma_get_rflow_ring_offset(rx_chn->common.udmax);
		rx_ring_id = 0;
	} else {
		rx_ring_id = flow_cfg->ring_rxq_id;
		rx_ringfdq_id = flow_cfg->ring_rxfdq0_id;
	}

	/* request and cfg rings */
	ret =  k3_ringacc_request_rings_pair(rx_chn->common.ringacc,
					     rx_ringfdq_id, rx_ring_id,
					     &flow->ringrxfdq,
					     &flow->ringrx);
	if (ret) {
		dev_err(dev, "Failed to get RX/RXFDQ rings %d\n", ret);
		goto err_rflow_put;
	}

	/* Set the dma_dev for the rings to be configured */
	flow_cfg->rx_cfg.dma_dev = k3_udma_glue_rx_get_dma_device(rx_chn);
	flow_cfg->rxfdq_cfg.dma_dev = flow_cfg->rx_cfg.dma_dev;

	/* Set the ASEL value for DMA rings of PKTDMA */
	if (xudma_is_pktdma(rx_chn->common.udmax)) {
		flow_cfg->rx_cfg.asel = rx_chn->common.atype_asel;
		flow_cfg->rxfdq_cfg.asel = rx_chn->common.atype_asel;
	}

	ret = k3_ringacc_ring_cfg(flow->ringrx, &flow_cfg->rx_cfg);
	if (ret) {
		dev_err(dev, "Failed to cfg ringrx %d\n", ret);
		goto err_ringrxfdq_free;
	}

	ret = k3_ringacc_ring_cfg(flow->ringrxfdq, &flow_cfg->rxfdq_cfg);
	if (ret) {
		dev_err(dev, "Failed to cfg ringrxfdq %d\n", ret);
		goto err_ringrxfdq_free;
	}

	if (rx_chn->remote) {
		rx_ring_id = TI_SCI_RESOURCE_NULL;
		rx_ringfdq_id = TI_SCI_RESOURCE_NULL;
	} else {
		rx_ring_id = k3_ringacc_get_ring_id(flow->ringrx);
		rx_ringfdq_id = k3_ringacc_get_ring_id(flow->ringrxfdq);
	}

	memset(&req, 0, sizeof(req));

	req.valid_params =
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_EINFO_PRESENT_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_PSINFO_PRESENT_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_ERROR_HANDLING_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DESC_TYPE_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DEST_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_SRC_TAG_HI_SEL_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_SRC_TAG_LO_SEL_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DEST_TAG_HI_SEL_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DEST_TAG_LO_SEL_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ0_SZ0_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ1_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ2_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ3_QNUM_VALID;
	req.nav_id = tisci_rm->tisci_dev_id;
	req.flow_index = flow->udma_rflow_id;
	if (rx_chn->common.epib)
		req.rx_einfo_present = 1;
	if (rx_chn->common.psdata_size)
		req.rx_psinfo_present = 1;
	if (flow_cfg->rx_error_handling)
		req.rx_error_handling = 1;
	req.rx_desc_type = 0;
	req.rx_dest_qnum = rx_ring_id;
	req.rx_src_tag_hi_sel = 0;
	req.rx_src_tag_lo_sel = flow_cfg->src_tag_lo_sel;
	req.rx_dest_tag_hi_sel = 0;
	req.rx_dest_tag_lo_sel = 0;
	req.rx_fdq0_sz0_qnum = rx_ringfdq_id;
	req.rx_fdq1_qnum = rx_ringfdq_id;
	req.rx_fdq2_qnum = rx_ringfdq_id;
	req.rx_fdq3_qnum = rx_ringfdq_id;

	ret = tisci_rm->tisci_udmap_ops->rx_flow_cfg(tisci_rm->tisci, &req);
	if (ret) {
		dev_err(dev, "flow%d config failed: %d\n", flow->udma_rflow_id,
			ret);
		goto err_ringrxfdq_free;
	}

	rx_chn->flows_ready++;
	dev_dbg(dev, "flow%d config done. ready:%d\n",
		flow->udma_rflow_id, rx_chn->flows_ready);

	return 0;

err_ringrxfdq_free:
	k3_ringacc_ring_free(flow->ringrxfdq);
	k3_ringacc_ring_free(flow->ringrx);

err_rflow_put:
	xudma_rflow_put(rx_chn->common.udmax, flow->udma_rflow);
	flow->udma_rflow = NULL;

	return ret;
}

static void k3_udma_glue_dump_rx_chn(struct k3_udma_glue_rx_channel *chn)
{
	struct device *dev = chn->common.dev;

	dev_dbg(dev, "dump_rx_chn:\n"
		"udma_rchan_id: %d\n"
		"src_thread: %08x\n"
		"dst_thread: %08x\n"
		"epib: %d\n"
		"hdesc_size: %u\n"
		"psdata_size: %u\n"
		"swdata_size: %u\n"
		"flow_id_base: %d\n"
		"flow_num: %d\n",
		chn->udma_rchan_id,
		chn->common.src_thread,
		chn->common.dst_thread,
		chn->common.epib,
		chn->common.hdesc_size,
		chn->common.psdata_size,
		chn->common.swdata_size,
		chn->flow_id_base,
		chn->flow_num);
}

static void k3_udma_glue_dump_rx_rt_chn(struct k3_udma_glue_rx_channel *chn,
					char *mark)
{
	struct device *dev = chn->common.dev;

	dev_dbg(dev, "=== dump ===> %s\n", mark);

	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_CTL_REG,
		xudma_rchanrt_read(chn->udma_rchanx, UDMA_CHAN_RT_CTL_REG));
	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_PEER_RT_EN_REG,
		xudma_rchanrt_read(chn->udma_rchanx,
				   UDMA_CHAN_RT_PEER_RT_EN_REG));
	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_PCNT_REG,
		xudma_rchanrt_read(chn->udma_rchanx, UDMA_CHAN_RT_PCNT_REG));
	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_BCNT_REG,
		xudma_rchanrt_read(chn->udma_rchanx, UDMA_CHAN_RT_BCNT_REG));
	dev_dbg(dev, "0x%08X: %08X\n", UDMA_CHAN_RT_SBCNT_REG,
		xudma_rchanrt_read(chn->udma_rchanx, UDMA_CHAN_RT_SBCNT_REG));
}

static int
k3_udma_glue_allocate_rx_flows(struct k3_udma_glue_rx_channel *rx_chn,
			       struct k3_udma_glue_rx_channel_cfg *cfg)
{
	int ret;

	/* default rflow */
	if (cfg->flow_id_use_rxchan_id)
		return 0;

	/* not a GP rflows */
	if (rx_chn->flow_id_base != -1 &&
	    !xudma_rflow_is_gp(rx_chn->common.udmax, rx_chn->flow_id_base))
		return 0;

	/* Allocate range of GP rflows */
	ret = xudma_alloc_gp_rflow_range(rx_chn->common.udmax,
					 rx_chn->flow_id_base,
					 rx_chn->flow_num);
	if (ret < 0) {
		dev_err(rx_chn->common.dev, "UDMAX reserve_rflow %d cnt:%d err: %d\n",
			rx_chn->flow_id_base, rx_chn->flow_num, ret);
		return ret;
	}
	rx_chn->flow_id_base = ret;

	return 0;
}

static struct k3_udma_glue_rx_channel *
k3_udma_glue_request_rx_chn_priv(struct device *dev, const char *name,
				 struct k3_udma_glue_rx_channel_cfg *cfg)
{
	struct k3_udma_glue_rx_channel *rx_chn;
	struct psil_endpoint_config *ep_cfg;
	int ret, i;

	if (cfg->flow_id_num <= 0)
		return ERR_PTR(-EINVAL);

	if (cfg->flow_id_num != 1 &&
	    (cfg->def_flow_cfg || cfg->flow_id_use_rxchan_id))
		return ERR_PTR(-EINVAL);

	rx_chn = devm_kzalloc(dev, sizeof(*rx_chn), GFP_KERNEL);
	if (!rx_chn)
		return ERR_PTR(-ENOMEM);

	rx_chn->common.dev = dev;
	rx_chn->common.swdata_size = cfg->swdata_size;
	rx_chn->remote = false;

	/* parse of udmap channel */
	ret = of_k3_udma_glue_parse_chn(dev->of_node, name,
					&rx_chn->common, false);
	if (ret)
		goto err;

	rx_chn->common.hdesc_size = cppi5_hdesc_calc_size(rx_chn->common.epib,
						rx_chn->common.psdata_size,
						rx_chn->common.swdata_size);

	ep_cfg = rx_chn->common.ep_config;

	if (xudma_is_pktdma(rx_chn->common.udmax))
		rx_chn->udma_rchan_id = ep_cfg->mapped_channel_id;
	else
		rx_chn->udma_rchan_id = -1;

	/* request and cfg UDMAP RX channel */
	rx_chn->udma_rchanx = xudma_rchan_get(rx_chn->common.udmax,
					      rx_chn->udma_rchan_id);
	if (IS_ERR(rx_chn->udma_rchanx)) {
		ret = PTR_ERR(rx_chn->udma_rchanx);
		dev_err(dev, "UDMAX rchanx get err %d\n", ret);
		goto err;
	}
	rx_chn->udma_rchan_id = xudma_rchan_get_id(rx_chn->udma_rchanx);

	rx_chn->common.chan_dev.class = &k3_udma_glue_devclass;
	rx_chn->common.chan_dev.parent = xudma_get_device(rx_chn->common.udmax);
	dev_set_name(&rx_chn->common.chan_dev, "rchan%d-0x%04x",
		     rx_chn->udma_rchan_id, rx_chn->common.src_thread);
	ret = device_register(&rx_chn->common.chan_dev);
	if (ret) {
		dev_err(dev, "Channel Device registration failed %d\n", ret);
		put_device(&rx_chn->common.chan_dev);
		rx_chn->common.chan_dev.parent = NULL;
		goto err;
	}

	if (xudma_is_pktdma(rx_chn->common.udmax)) {
		/* prepare the channel device as coherent */
		rx_chn->common.chan_dev.dma_coherent = true;
		dma_coerce_mask_and_coherent(&rx_chn->common.chan_dev,
					     DMA_BIT_MASK(48));
	}

	if (xudma_is_pktdma(rx_chn->common.udmax)) {
		int flow_start = cfg->flow_id_base;
		int flow_end;

		if (flow_start == -1)
			flow_start = ep_cfg->flow_start;

		flow_end = flow_start + cfg->flow_id_num - 1;
		if (flow_start < ep_cfg->flow_start ||
		    flow_end > (ep_cfg->flow_start + ep_cfg->flow_num - 1)) {
			dev_err(dev, "Invalid flow range requested\n");
			ret = -EINVAL;
			goto err;
		}
		rx_chn->flow_id_base = flow_start;
	} else {
		rx_chn->flow_id_base = cfg->flow_id_base;

		/* Use RX channel id as flow id: target dev can't generate flow_id */
		if (cfg->flow_id_use_rxchan_id)
			rx_chn->flow_id_base = rx_chn->udma_rchan_id;
	}

	rx_chn->flow_num = cfg->flow_id_num;

	rx_chn->flows = devm_kcalloc(dev, rx_chn->flow_num,
				     sizeof(*rx_chn->flows), GFP_KERNEL);
	if (!rx_chn->flows) {
		ret = -ENOMEM;
		goto err;
	}

	ret = k3_udma_glue_allocate_rx_flows(rx_chn, cfg);
	if (ret)
		goto err;

	for (i = 0; i < rx_chn->flow_num; i++)
		rx_chn->flows[i].udma_rflow_id = rx_chn->flow_id_base + i;

	/* request and cfg psi-l */
	rx_chn->common.dst_thread =
			xudma_dev_get_psil_base(rx_chn->common.udmax) +
			rx_chn->udma_rchan_id;

	ret = k3_udma_glue_cfg_rx_chn(rx_chn);
	if (ret) {
		dev_err(dev, "Failed to cfg rchan %d\n", ret);
		goto err;
	}

	/* init default RX flow only if flow_num = 1 */
	if (cfg->def_flow_cfg) {
		ret = k3_udma_glue_cfg_rx_flow(rx_chn, 0, cfg->def_flow_cfg);
		if (ret)
			goto err;
	}

	k3_udma_glue_dump_rx_chn(rx_chn);

	return rx_chn;

err:
	k3_udma_glue_release_rx_chn(rx_chn);
	return ERR_PTR(ret);
}

static struct k3_udma_glue_rx_channel *
k3_udma_glue_request_remote_rx_chn(struct device *dev, const char *name,
				   struct k3_udma_glue_rx_channel_cfg *cfg)
{
	struct k3_udma_glue_rx_channel *rx_chn;
	int ret, i;

	if (cfg->flow_id_num <= 0 ||
	    cfg->flow_id_use_rxchan_id ||
	    cfg->def_flow_cfg ||
	    cfg->flow_id_base < 0)
		return ERR_PTR(-EINVAL);

	/*
	 * Remote RX channel is under control of Remote CPU core, so
	 * Linux can only request and manipulate by dedicated RX flows
	 */

	rx_chn = devm_kzalloc(dev, sizeof(*rx_chn), GFP_KERNEL);
	if (!rx_chn)
		return ERR_PTR(-ENOMEM);

	rx_chn->common.dev = dev;
	rx_chn->common.swdata_size = cfg->swdata_size;
	rx_chn->remote = true;
	rx_chn->udma_rchan_id = -1;
	rx_chn->flow_num = cfg->flow_id_num;
	rx_chn->flow_id_base = cfg->flow_id_base;
	rx_chn->psil_paired = false;

	/* parse of udmap channel */
	ret = of_k3_udma_glue_parse_chn(dev->of_node, name,
					&rx_chn->common, false);
	if (ret)
		goto err;

	rx_chn->common.hdesc_size = cppi5_hdesc_calc_size(rx_chn->common.epib,
						rx_chn->common.psdata_size,
						rx_chn->common.swdata_size);

	rx_chn->flows = devm_kcalloc(dev, rx_chn->flow_num,
				     sizeof(*rx_chn->flows), GFP_KERNEL);
	if (!rx_chn->flows) {
		ret = -ENOMEM;
		goto err;
	}

	rx_chn->common.chan_dev.class = &k3_udma_glue_devclass;
	rx_chn->common.chan_dev.parent = xudma_get_device(rx_chn->common.udmax);
	dev_set_name(&rx_chn->common.chan_dev, "rchan_remote-0x%04x",
		     rx_chn->common.src_thread);
	ret = device_register(&rx_chn->common.chan_dev);
	if (ret) {
		dev_err(dev, "Channel Device registration failed %d\n", ret);
		put_device(&rx_chn->common.chan_dev);
		rx_chn->common.chan_dev.parent = NULL;
		goto err;
	}

	if (xudma_is_pktdma(rx_chn->common.udmax)) {
		/* prepare the channel device as coherent */
		rx_chn->common.chan_dev.dma_coherent = true;
		dma_coerce_mask_and_coherent(&rx_chn->common.chan_dev,
					     DMA_BIT_MASK(48));
	}

	ret = k3_udma_glue_allocate_rx_flows(rx_chn, cfg);
	if (ret)
		goto err;

	for (i = 0; i < rx_chn->flow_num; i++)
		rx_chn->flows[i].udma_rflow_id = rx_chn->flow_id_base + i;

	k3_udma_glue_dump_rx_chn(rx_chn);

	return rx_chn;

err:
	k3_udma_glue_release_rx_chn(rx_chn);
	return ERR_PTR(ret);
}

struct k3_udma_glue_rx_channel *
k3_udma_glue_request_rx_chn(struct device *dev, const char *name,
			    struct k3_udma_glue_rx_channel_cfg *cfg)
{
	if (cfg->remote)
		return k3_udma_glue_request_remote_rx_chn(dev, name, cfg);
	else
		return k3_udma_glue_request_rx_chn_priv(dev, name, cfg);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_request_rx_chn);

void k3_udma_glue_release_rx_chn(struct k3_udma_glue_rx_channel *rx_chn)
{
	int i;

	if (IS_ERR_OR_NULL(rx_chn->common.udmax))
		return;

	if (rx_chn->psil_paired) {
		xudma_navss_psil_unpair(rx_chn->common.udmax,
					rx_chn->common.src_thread,
					rx_chn->common.dst_thread);
		rx_chn->psil_paired = false;
	}

	for (i = 0; i < rx_chn->flow_num; i++)
		k3_udma_glue_release_rx_flow(rx_chn, i);

	if (xudma_rflow_is_gp(rx_chn->common.udmax, rx_chn->flow_id_base))
		xudma_free_gp_rflow_range(rx_chn->common.udmax,
					  rx_chn->flow_id_base,
					  rx_chn->flow_num);

	if (!IS_ERR_OR_NULL(rx_chn->udma_rchanx))
		xudma_rchan_put(rx_chn->common.udmax,
				rx_chn->udma_rchanx);

	if (rx_chn->common.chan_dev.parent) {
		device_unregister(&rx_chn->common.chan_dev);
		rx_chn->common.chan_dev.parent = NULL;
	}
}
EXPORT_SYMBOL_GPL(k3_udma_glue_release_rx_chn);

int k3_udma_glue_rx_flow_init(struct k3_udma_glue_rx_channel *rx_chn,
			      u32 flow_idx,
			      struct k3_udma_glue_rx_flow_cfg *flow_cfg)
{
	if (flow_idx >= rx_chn->flow_num)
		return -EINVAL;

	return k3_udma_glue_cfg_rx_flow(rx_chn, flow_idx, flow_cfg);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_rx_flow_init);

u32 k3_udma_glue_rx_flow_get_fdq_id(struct k3_udma_glue_rx_channel *rx_chn,
				    u32 flow_idx)
{
	struct k3_udma_glue_rx_flow *flow;

	if (flow_idx >= rx_chn->flow_num)
		return -EINVAL;

	flow = &rx_chn->flows[flow_idx];

	return k3_ringacc_get_ring_id(flow->ringrxfdq);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_rx_flow_get_fdq_id);

u32 k3_udma_glue_rx_get_flow_id_base(struct k3_udma_glue_rx_channel *rx_chn)
{
	return rx_chn->flow_id_base;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_rx_get_flow_id_base);

int k3_udma_glue_rx_flow_enable(struct k3_udma_glue_rx_channel *rx_chn,
				u32 flow_idx)
{
	struct k3_udma_glue_rx_flow *flow = &rx_chn->flows[flow_idx];
	const struct udma_tisci_rm *tisci_rm = rx_chn->common.tisci_rm;
	struct device *dev = rx_chn->common.dev;
	struct ti_sci_msg_rm_udmap_flow_cfg req;
	int rx_ring_id;
	int rx_ringfdq_id;
	int ret = 0;

	if (!rx_chn->remote)
		return -EINVAL;

	rx_ring_id = k3_ringacc_get_ring_id(flow->ringrx);
	rx_ringfdq_id = k3_ringacc_get_ring_id(flow->ringrxfdq);

	memset(&req, 0, sizeof(req));

	req.valid_params =
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DEST_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ0_SZ0_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ1_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ2_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ3_QNUM_VALID;
	req.nav_id = tisci_rm->tisci_dev_id;
	req.flow_index = flow->udma_rflow_id;
	req.rx_dest_qnum = rx_ring_id;
	req.rx_fdq0_sz0_qnum = rx_ringfdq_id;
	req.rx_fdq1_qnum = rx_ringfdq_id;
	req.rx_fdq2_qnum = rx_ringfdq_id;
	req.rx_fdq3_qnum = rx_ringfdq_id;

	ret = tisci_rm->tisci_udmap_ops->rx_flow_cfg(tisci_rm->tisci, &req);
	if (ret) {
		dev_err(dev, "flow%d enable failed: %d\n", flow->udma_rflow_id,
			ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_rx_flow_enable);

int k3_udma_glue_rx_flow_disable(struct k3_udma_glue_rx_channel *rx_chn,
				 u32 flow_idx)
{
	struct k3_udma_glue_rx_flow *flow = &rx_chn->flows[flow_idx];
	const struct udma_tisci_rm *tisci_rm = rx_chn->common.tisci_rm;
	struct device *dev = rx_chn->common.dev;
	struct ti_sci_msg_rm_udmap_flow_cfg req;
	int ret = 0;

	if (!rx_chn->remote)
		return -EINVAL;

	memset(&req, 0, sizeof(req));
	req.valid_params =
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_DEST_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ0_SZ0_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ1_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ2_QNUM_VALID |
			TI_SCI_MSG_VALUE_RM_UDMAP_FLOW_FDQ3_QNUM_VALID;
	req.nav_id = tisci_rm->tisci_dev_id;
	req.flow_index = flow->udma_rflow_id;
	req.rx_dest_qnum = TI_SCI_RESOURCE_NULL;
	req.rx_fdq0_sz0_qnum = TI_SCI_RESOURCE_NULL;
	req.rx_fdq1_qnum = TI_SCI_RESOURCE_NULL;
	req.rx_fdq2_qnum = TI_SCI_RESOURCE_NULL;
	req.rx_fdq3_qnum = TI_SCI_RESOURCE_NULL;

	ret = tisci_rm->tisci_udmap_ops->rx_flow_cfg(tisci_rm->tisci, &req);
	if (ret) {
		dev_err(dev, "flow%d disable failed: %d\n", flow->udma_rflow_id,
			ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_rx_flow_disable);

int k3_udma_glue_enable_rx_chn(struct k3_udma_glue_rx_channel *rx_chn)
{
	int ret;

	if (rx_chn->remote)
		return -EINVAL;

	if (rx_chn->flows_ready < rx_chn->flow_num)
		return -EINVAL;

	ret = xudma_navss_psil_pair(rx_chn->common.udmax,
				    rx_chn->common.src_thread,
				    rx_chn->common.dst_thread);
	if (ret) {
		dev_err(rx_chn->common.dev, "PSI-L request err %d\n", ret);
		return ret;
	}

	rx_chn->psil_paired = true;

	xudma_rchanrt_write(rx_chn->udma_rchanx, UDMA_CHAN_RT_CTL_REG,
			    UDMA_CHAN_RT_CTL_EN);

	xudma_rchanrt_write(rx_chn->udma_rchanx, UDMA_CHAN_RT_PEER_RT_EN_REG,
			    UDMA_PEER_RT_EN_ENABLE);

	k3_udma_glue_dump_rx_rt_chn(rx_chn, "rxrt en");
	return 0;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_enable_rx_chn);

void k3_udma_glue_disable_rx_chn(struct k3_udma_glue_rx_channel *rx_chn)
{
	k3_udma_glue_dump_rx_rt_chn(rx_chn, "rxrt dis1");

	xudma_rchanrt_write(rx_chn->udma_rchanx,
			    UDMA_CHAN_RT_PEER_RT_EN_REG, 0);
	xudma_rchanrt_write(rx_chn->udma_rchanx, UDMA_CHAN_RT_CTL_REG, 0);

	k3_udma_glue_dump_rx_rt_chn(rx_chn, "rxrt dis2");

	if (rx_chn->psil_paired) {
		xudma_navss_psil_unpair(rx_chn->common.udmax,
					rx_chn->common.src_thread,
					rx_chn->common.dst_thread);
		rx_chn->psil_paired = false;
	}
}
EXPORT_SYMBOL_GPL(k3_udma_glue_disable_rx_chn);

void k3_udma_glue_tdown_rx_chn(struct k3_udma_glue_rx_channel *rx_chn,
			       bool sync)
{
	int i = 0;
	u32 val;

	if (rx_chn->remote)
		return;

	k3_udma_glue_dump_rx_rt_chn(rx_chn, "rxrt tdown1");

	xudma_rchanrt_write(rx_chn->udma_rchanx, UDMA_CHAN_RT_PEER_RT_EN_REG,
			    UDMA_PEER_RT_EN_ENABLE | UDMA_PEER_RT_EN_TEARDOWN);

	val = xudma_rchanrt_read(rx_chn->udma_rchanx, UDMA_CHAN_RT_CTL_REG);

	while (sync && (val & UDMA_CHAN_RT_CTL_EN)) {
		val = xudma_rchanrt_read(rx_chn->udma_rchanx,
					 UDMA_CHAN_RT_CTL_REG);
		udelay(1);
		if (i > K3_UDMAX_TDOWN_TIMEOUT_US) {
			dev_err(rx_chn->common.dev, "RX tdown timeout\n");
			break;
		}
		i++;
	}

	val = xudma_rchanrt_read(rx_chn->udma_rchanx,
				 UDMA_CHAN_RT_PEER_RT_EN_REG);
	if (sync && (val & UDMA_PEER_RT_EN_ENABLE))
		dev_err(rx_chn->common.dev, "TX tdown peer not stopped\n");
	k3_udma_glue_dump_rx_rt_chn(rx_chn, "rxrt tdown2");
}
EXPORT_SYMBOL_GPL(k3_udma_glue_tdown_rx_chn);

void k3_udma_glue_reset_rx_chn(struct k3_udma_glue_rx_channel *rx_chn,
		u32 flow_num, void *data,
		void (*cleanup)(void *data, dma_addr_t desc_dma), bool skip_fdq)
{
	struct k3_udma_glue_rx_flow *flow = &rx_chn->flows[flow_num];
	struct device *dev = rx_chn->common.dev;
	dma_addr_t desc_dma;
	int occ_rx, i, ret;

	/* reset RXCQ as it is not input for udma - expected to be empty */
	occ_rx = k3_ringacc_ring_get_occ(flow->ringrx);
	dev_dbg(dev, "RX reset flow %u occ_rx %u\n", flow_num, occ_rx);

	/* Skip RX FDQ in case one FDQ is used for the set of flows */
	if (skip_fdq)
		goto do_reset;

	/*
	 * RX FDQ reset need to be special way as it is input for udma and its
	 * state cached by udma, so:
	 * 1) save RX FDQ occ
	 * 2) clean up RX FDQ and call callback .cleanup() for each desc
	 * 3) reset RX FDQ in a special way
	 */
	occ_rx = k3_ringacc_ring_get_occ(flow->ringrxfdq);
	dev_dbg(dev, "RX reset flow %u occ_rx_fdq %u\n", flow_num, occ_rx);

	for (i = 0; i < occ_rx; i++) {
		ret = k3_ringacc_ring_pop(flow->ringrxfdq, &desc_dma);
		if (ret) {
			if (ret != -ENODATA)
				dev_err(dev, "RX reset pop %d\n", ret);
			break;
		}
		cleanup(data, desc_dma);
	}

	k3_ringacc_ring_reset_dma(flow->ringrxfdq, occ_rx);

do_reset:
	k3_ringacc_ring_reset(flow->ringrx);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_reset_rx_chn);

int k3_udma_glue_push_rx_chn(struct k3_udma_glue_rx_channel *rx_chn,
			     u32 flow_num, struct cppi5_host_desc_t *desc_rx,
			     dma_addr_t desc_dma)
{
	struct k3_udma_glue_rx_flow *flow = &rx_chn->flows[flow_num];

	return k3_ringacc_ring_push(flow->ringrxfdq, &desc_dma);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_push_rx_chn);

int k3_udma_glue_pop_rx_chn(struct k3_udma_glue_rx_channel *rx_chn,
			    u32 flow_num, dma_addr_t *desc_dma)
{
	struct k3_udma_glue_rx_flow *flow = &rx_chn->flows[flow_num];

	return k3_ringacc_ring_pop(flow->ringrx, desc_dma);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_pop_rx_chn);

int k3_udma_glue_rx_get_irq(struct k3_udma_glue_rx_channel *rx_chn,
			    u32 flow_num)
{
	struct k3_udma_glue_rx_flow *flow;

	flow = &rx_chn->flows[flow_num];

	if (xudma_is_pktdma(rx_chn->common.udmax)) {
		flow->virq = xudma_pktdma_rflow_get_irq(rx_chn->common.udmax,
							flow->udma_rflow_id);
	} else {
		flow->virq = k3_ringacc_get_ring_irq_num(flow->ringrx);
	}

	return flow->virq;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_rx_get_irq);

struct device *
	k3_udma_glue_rx_get_dma_device(struct k3_udma_glue_rx_channel *rx_chn)
{
	if (xudma_is_pktdma(rx_chn->common.udmax) &&
	    (rx_chn->common.atype_asel == 14 || rx_chn->common.atype_asel == 15))
		return &rx_chn->common.chan_dev;

	return xudma_get_device(rx_chn->common.udmax);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_rx_get_dma_device);

void k3_udma_glue_rx_dma_to_cppi5_addr(struct k3_udma_glue_rx_channel *rx_chn,
				       dma_addr_t *addr)
{
	if (!xudma_is_pktdma(rx_chn->common.udmax) ||
	    !rx_chn->common.atype_asel)
		return;

	*addr |= (u64)rx_chn->common.atype_asel << K3_ADDRESS_ASEL_SHIFT;
}
EXPORT_SYMBOL_GPL(k3_udma_glue_rx_dma_to_cppi5_addr);

void k3_udma_glue_rx_cppi5_to_dma_addr(struct k3_udma_glue_rx_channel *rx_chn,
				       dma_addr_t *addr)
{
	if (!xudma_is_pktdma(rx_chn->common.udmax) ||
	    !rx_chn->common.atype_asel)
		return;

	*addr &= (u64)GENMASK(K3_ADDRESS_ASEL_SHIFT - 1, 0);
}
EXPORT_SYMBOL_GPL(k3_udma_glue_rx_cppi5_to_dma_addr);

static int __init k3_udma_glue_class_init(void)
{
	return class_register(&k3_udma_glue_devclass);
}
arch_initcall(k3_udma_glue_class_init);
