// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include "q2spi-msm.h"
#include "q2spi-slave-reg.h"

static void q2spi_rx_xfer_completion_event(struct msm_gpi_dma_async_tx_cb_param *cb_param)
{
	struct q2spi_packet *q2spi_pkt = cb_param->userdata;
	struct q2spi_geni *q2spi = q2spi_pkt->q2spi;
	struct q2spi_dma_transfer *xfer;
	u32 status = 0;

	if (q2spi_pkt->m_cmd_param == Q2SPI_RX_ONLY) {
		Q2SPI_DBG_2(q2spi, "%s for Doorbell\n", __func__);
		xfer = q2spi->db_xfer;
	} else {
		xfer = q2spi_pkt->xfer;
		Q2SPI_DBG_2(q2spi, "%s for Rx Event\n", __func__);
	}
	if (!xfer || !xfer->rx_buf) {
		pr_err("%s rx buf NULL!!!\n", __func__);
		return;
	}

	Q2SPI_DBG_2(q2spi, "%s cb_param:%p len:%d status:%d xfer:%p rx buf:%p dma:%p len:%d\n",
		    __func__, cb_param, cb_param->length, cb_param->status, xfer, xfer->rx_buf,
		    (void *)xfer->rx_dma, xfer->rx_len);

	/* check status is 0 or EOT for success */
	status = cb_param->status;
	if (cb_param->length <= xfer->rx_len) {
		xfer->rx_len = cb_param->length;
		q2spi_dump_ipc(q2spi, "rx_xfer_completion_event RX",
			       (char *)xfer->rx_buf, cb_param->length);
		if (q2spi_pkt->m_cmd_param == Q2SPI_RX_ONLY) {
			Q2SPI_DBG_1(q2spi, "%s call db_rx_cb\n", __func__);
			complete_all(&q2spi->db_rx_cb);
		} else {
			Q2SPI_DBG_1(q2spi, "%s call rx_cb\n", __func__);
			complete_all(&q2spi->rx_cb);
		}
		Q2SPI_DBG_2(q2spi, "%s q2spi_pkt:%p state=%d vtype:%d\n",
			    __func__, q2spi_pkt, q2spi_pkt->state, q2spi_pkt->vtype);
	} else {
		Q2SPI_DEBUG(q2spi, "%s Err length miss-match %d %d\n",
			    __func__, cb_param->length, xfer->rx_len);
	}
}

static void q2spi_tx_xfer_completion_event(struct msm_gpi_dma_async_tx_cb_param *cb_param)
{
	struct q2spi_packet *q2spi_pkt;
	struct q2spi_geni *q2spi;
	struct q2spi_dma_transfer *xfer;

	q2spi_pkt = cb_param->userdata;
	if (!q2spi_pkt) {
		pr_err("%s Err Invalid q2spi_packet\n", __func__);
		return;
	}

	q2spi = q2spi_pkt->q2spi;
	if (!q2spi || !q2spi_pkt->xfer) {
		pr_err("%s Err Invalid q2spi or xfer\n", __func__);
		return;
	}

	xfer = q2spi_pkt->xfer;

	Q2SPI_DBG_1(q2spi, "%s xfer->tx_len:%d cb_param_length:%d\n", __func__,
		    xfer->tx_len, cb_param->length);
	if (cb_param->length == xfer->tx_len) {
		Q2SPI_DBG_1(q2spi, "%s complete_tx_cb\n", __func__);
		complete_all(&q2spi->tx_cb);
	} else {
		Q2SPI_DEBUG(q2spi, "%s Err length miss-match\n", __func__);
	}
}

static void q2spi_parse_q2spi_status(struct msm_gpi_dma_async_tx_cb_param *cb_param)
{
	struct q2spi_packet *q2spi_pkt = cb_param->userdata;
	struct q2spi_geni *q2spi = q2spi_pkt->q2spi;
	u32 status = 0;

	status = cb_param->q2spi_status;
	Q2SPI_DBG_1(q2spi, "%s status:%d complete_tx_cb\n", __func__, status);
	complete_all(&q2spi->tx_cb);
	Q2SPI_DBG_2(q2spi, "%s q2spi_pkt:%p state=%d vtype:%d\n",
		    __func__, q2spi_pkt, q2spi_pkt->state, q2spi_pkt->vtype);
}

static void q2spi_parse_cr_header(struct q2spi_geni *q2spi, struct msm_gpi_cb const *cb)
{
	q2spi_doorbell(q2spi, &cb->q2spi_cr_header_event);
}

/*
 * q2spi_check_m_irq_err_status() - this function checks m_irq error status and
 * also if start sequence error seen it will set is_start_seq_fail flag as true.
 *
 * @q2spi: q2spi master device handle
 * @cb_status: irq status fields
 *
 * Return: None
 */
static void q2spi_check_m_irq_err_status(struct q2spi_geni *q2spi, u32 cb_status)
{
	/* bit 5 to 12 represents gp irq status */
	u32 status = (cb_status & M_GP_IRQ_MASK) >> M_GP_IRQ_ERR_START_BIT;

	if (status & Q2SPI_PWR_ON_NACK)
		Q2SPI_DEBUG(q2spi, "%s Q2SPI_PWR_ON_NACK\n", __func__);
	if (status & Q2SPI_HDR_FAIL)
		Q2SPI_DEBUG(q2spi, "%s Q2SPI_HDR_FAIL\n", __func__);
	if (status & Q2SPI_HCR_FAIL)
		Q2SPI_DEBUG(q2spi, "%s Q2SPI_HCR_FAIL\n", __func__);
	if (status & Q2SPI_CHECKSUM_FAIL)
		Q2SPI_DEBUG(q2spi, "%s Q2SPI_CHEKSUM_FAIL\n", __func__);
	if (status & Q2SPI_START_SEQ_TIMEOUT) {
		q2spi->is_start_seq_fail = true;
		complete_all(&q2spi->wait_comp_start_fail);
		Q2SPI_DEBUG(q2spi, "%s Q2SPI_START_SEQ_TIMEOUT\n", __func__);
	}
	if (status & Q2SPI_STOP_SEQ_TIMEOUT)
		Q2SPI_DEBUG(q2spi, "%s Q2SPI_STOP_SEQ_TIMEOUT\n", __func__);
	if (status & Q2SPI_WAIT_PHASE_TIMEOUT)
		Q2SPI_DEBUG(q2spi, "%s Q2SPI_WAIT_PHASE_TIMEOUT\n", __func__);
	if (status & Q2SPI_CLIENT_EN_NOT_DETECTED)
		Q2SPI_DEBUG(q2spi, "%s Q2SPI_CLIENT_EN_NOT_DETECTED\n", __func__);
}

static void q2spi_gsi_tx_callback(void *cb)
{
	struct msm_gpi_dma_async_tx_cb_param *cb_param = NULL;
	struct q2spi_packet *q2spi_pkt;
	struct q2spi_geni *q2spi;

	cb_param = (struct msm_gpi_dma_async_tx_cb_param *)cb;
	if (!cb_param) {
		pr_err("%s Err Invalid CB\n", __func__);
		return;
	}
	q2spi_pkt = cb_param->userdata;
	q2spi = q2spi_pkt->q2spi;
	if (!q2spi) {
		pr_err("%s Err Invalid q2spi\n", __func__);
		return;
	}

	if (cb_param->status == MSM_GPI_TCE_UNEXP_ERR) {
		Q2SPI_DEBUG(q2spi, "%s Unexpected CB status:0x%x\n", __func__, cb_param->status);
		return;
	}
	if (cb_param->completion_code == MSM_GPI_TCE_UNEXP_ERR) {
		Q2SPI_DEBUG(q2spi, "%s Unexpected GSI CB completion code CB status:0x%x\n",
			    __func__, cb_param->status);
		q2spi->gsi->qup_gsi_err = true;
		q2spi_check_m_irq_err_status(q2spi, cb_param->status);
		complete_all(&q2spi->tx_cb);
		return;
	} else if (cb_param->completion_code == MSM_GPI_TCE_EOT) {
		Q2SPI_DBG_2(q2spi, "%s MSM_GPI_TCE_EOT\n", __func__);
		if (cb_param->tce_type == XFER_COMPLETE_EV_TYPE) {
			Q2SPI_DBG_1(q2spi, "%s TCE XFER_COMPLETE_EV_TYPE\n", __func__);
			q2spi_tx_xfer_completion_event(cb_param);
		} else if (cb_param->tce_type == QUP_TCE_TYPE_Q2SPI_STATUS) {
			Q2SPI_DBG_1(q2spi, "%s QUP_TCE_TYPE_Q2SPI_STATUS\n", __func__);
			q2spi_parse_q2spi_status(cb_param);
		} else {
			Q2SPI_ERROR(q2spi, "%s cb_param->tce_type:%d\n",
				    __func__, cb_param->tce_type);
		}
	}
}

static void q2spi_gsi_rx_callback(void *cb)
{
	struct msm_gpi_dma_async_tx_cb_param *cb_param = NULL;
	struct q2spi_packet *q2spi_pkt;
	struct q2spi_geni *q2spi;

	cb_param = (struct msm_gpi_dma_async_tx_cb_param *)cb;
	if (!cb_param) {
		pr_err("%s Err Invalid CB\n", __func__);
		return;
	}
	q2spi_pkt = cb_param->userdata;
	if (!q2spi_pkt) {
		pr_err("%s Err Invalid packet\n", __func__);
		return;
	}
	q2spi = q2spi_pkt->q2spi;
	if (!q2spi) {
		pr_err("%s Err Invalid q2spi\n", __func__);
		return;
	}

	if (cb_param->status == MSM_GPI_TCE_UNEXP_ERR) {
		Q2SPI_DEBUG(q2spi, "%s Err cb_status:0x%x\n", __func__, cb_param->status);
		return;
	}

	if (cb_param->completion_code == MSM_GPI_TCE_UNEXP_ERR) {
		Q2SPI_DEBUG(q2spi, "%s Err MSM_GPI_TCE_UNEXP_ERR CB status:0x%x\n",
			    __func__, cb_param->status);
		return;
	} else if (cb_param->completion_code == MSM_GPI_TCE_EOT) {
		Q2SPI_DBG_2(q2spi, "%s MSM_GPI_TCE_EOT\n", __func__);
		if (cb_param->tce_type == XFER_COMPLETE_EV_TYPE) {
			/* CR header */
			Q2SPI_DBG_1(q2spi, "%s TCE XFER_COMPLETE_EV_TYPE\n", __func__);
			q2spi_rx_xfer_completion_event(cb_param);
		}
	} else {
		Q2SPI_DEBUG(q2spi, "%s: Err cb_param->completion_code = %d\n",
			    __func__, cb_param->completion_code);
	}
}

static void q2spi_geni_deallocate_chan(struct q2spi_gsi *gsi)
{
	dma_release_channel(gsi->rx_c);
	dma_release_channel(gsi->tx_c);
	gsi->tx_c = NULL;
	gsi->rx_c = NULL;
}

/**
 * q2spi_geni_gsi_release - Releases GSI channel resources
 *
 * @q2spi: pointer to q2spi_geni driver data
 *
 * Return: None
 */
void q2spi_geni_gsi_release(struct q2spi_geni *q2spi)
{
	q2spi_geni_deallocate_chan(q2spi->gsi);
	kfree(q2spi->gsi);
}

/**
 * q2spi_geni_gsi_setup - Does setup of GSI channel resources
 *
 * @q2spi: pointer to q2spi_geni driver data
 *
 * Return: 0 on success, linux error code on failure
 */
int q2spi_geni_gsi_setup(struct q2spi_geni *q2spi)
{
	struct q2spi_gsi *gsi = NULL;
	int ret = 0;

	gsi = kzalloc(sizeof(struct q2spi_gsi), GFP_ATOMIC);
	if (!gsi) {
		Q2SPI_ERROR(q2spi, "%s Err GSI structure memory alloc failed\n", __func__);
		return -ENOMEM;
	}
	q2spi->gsi = gsi;
	Q2SPI_DBG_2(q2spi, "%s gsi:%p\n", __func__, gsi);
	if (gsi->chan_setup) {
		Q2SPI_DEBUG(q2spi, "%s Err GSI channel already configured\n", __func__);
		return ret;
	}

	gsi->tx_c = dma_request_slave_channel(q2spi->dev, "tx");
	if (IS_ERR_OR_NULL(gsi->tx_c)) {
		Q2SPI_ERROR(q2spi, "%s Err Failed to get tx DMA ch %ld\n",
			    __func__, PTR_ERR(gsi->tx_c));
		q2spi_kfree(q2spi, q2spi->gsi, __LINE__);
		return -EIO;
	}
	Q2SPI_DBG_2(q2spi, "%s gsi_tx_c:%p\n", __func__, gsi->tx_c);
	gsi->rx_c = dma_request_slave_channel(q2spi->dev, "rx");
	if (IS_ERR_OR_NULL(gsi->rx_c)) {
		Q2SPI_ERROR(q2spi, "%s Err Failed to get rx DMA ch %ld\n",
			    __func__, PTR_ERR(gsi->rx_c));
		dma_release_channel(gsi->tx_c);
		gsi->tx_c = NULL;
		q2spi_kfree(q2spi, q2spi->gsi, __LINE__);
		return -EIO;
	}
	Q2SPI_DBG_2(q2spi, "%s gsi_rx_c:%p\n", __func__, gsi->rx_c);
	gsi->tx_ev.init.callback = q2spi_gsi_ch_ev_cb;
	gsi->tx_ev.init.cb_param = q2spi;
	gsi->tx_ev.cmd = MSM_GPI_INIT;
	gsi->tx_c->private = &gsi->tx_ev;
	ret = dmaengine_slave_config(gsi->tx_c, NULL);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s tx dma slave config ret :%d\n", __func__, ret);
		goto dmaengine_slave_config_fail;
	}

	gsi->rx_ev.init.callback = q2spi_gsi_ch_ev_cb;
	gsi->rx_ev.init.cb_param = q2spi;
	gsi->rx_ev.cmd = MSM_GPI_INIT;
	gsi->rx_c->private = &gsi->rx_ev;
	ret = dmaengine_slave_config(gsi->rx_c, NULL);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s rx dma slave config ret :%d\n", __func__, ret);
		goto dmaengine_slave_config_fail;
	}
	Q2SPI_DBG_1(q2spi, "%s q2spi:%p gsi:%p q2spi_gsi:%p\n", __func__, q2spi, gsi, q2spi->gsi);
	q2spi->gsi->chan_setup = true;
	return ret;

dmaengine_slave_config_fail:
	q2spi_geni_gsi_release(q2spi);
	return ret;
}

static int get_q2spi_clk_cfg(u32 speed_hz, struct q2spi_geni *q2spi, int *clk_idx, int *clk_div)
{
	unsigned long sclk_freq;
	unsigned long res_freq;
	struct geni_se *se = &q2spi->se;
	int ret = 0;

	Q2SPI_DBG_2(q2spi, "%s Start PID=%d\n", __func__, current->pid);

	ret = geni_se_clk_freq_match(&q2spi->se, (speed_hz * q2spi->oversampling),
				     clk_idx, &sclk_freq, false);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err Failed(%d) to find src clk for 0x%x\n",
			    __func__, ret, speed_hz);
		return ret;
	}

	*clk_div = DIV_ROUND_UP(sclk_freq, (q2spi->oversampling * speed_hz));

	if (!(*clk_div)) {
		Q2SPI_ERROR(q2spi, "%s Err sclk:%lu oversampling:%d speed:%u\n",
			    __func__, sclk_freq, q2spi->oversampling, speed_hz);
		return -EINVAL;
	}

	res_freq = (sclk_freq / (*clk_div));

	Q2SPI_DBG_1(q2spi, "%s req %u resultant %lu sclk %lu, idx %d, div %d\n",
		    __func__, speed_hz, res_freq, sclk_freq, *clk_idx, *clk_div);

	ret = clk_set_rate(se->clk, sclk_freq);
	if (ret) {
		Q2SPI_ERROR(q2spi, "%s Err clk_set_rate failed %d\n",  __func__, ret);
		return ret;
	}
	Q2SPI_DBG_2(q2spi, "%s End PID=%d\n", __func__, current->pid);
	return 0;
}

/* 3.10.2.8 Q2SPI */
static struct msm_gpi_tre *setup_cfg0_tre(struct q2spi_geni *q2spi)
{
	struct msm_gpi_tre *c0_tre = &q2spi->gsi->config0_tre;
	u8 word_len = 0;
	u8 cs_mode = 0;
	u8 intr_pol = 0;
	u8 pack = 0;
	u8 cs_clk_delay = SPI_CS_CLK_DLY;
	int div = 0;
	int ret = 0;
	int idx = 0;
	int tdn = S_GP_CNT5_TDN;
	int tsn = M_GP_CNT7_TSN;
	int tan = M_GP_CNT4_TAN;
	int ssn = S_GP_CNT7_SSN;
	int cn_delay = M_GP_CNT6_CN_DELAY;

	ret = get_q2spi_clk_cfg(q2spi->cur_speed_hz, q2spi, &idx, &div);
	if (ret) {
		Q2SPI_DEBUG(q2spi, "%s Err setting clks:%d\n", __func__, ret);
		return ERR_PTR(ret);
	}

	word_len = MIN_WORD_LEN;
	pack |= (GSI_TX_PACK_EN | GSI_RX_PACK_EN);
	cs_mode = CS_LESS_MODE;
	intr_pol = INTR_HIGH_POLARITY;
	/* config0 */
	c0_tre->dword[0] = MSM_GPI_Q2SPI_CONFIG0_TRE_DWORD0(tsn, pack, tdn, cs_mode,
							    intr_pol, word_len);
	c0_tre->dword[1] = MSM_GPI_Q2SPI_CONFIG0_TRE_DWORD1(tan, cs_clk_delay, ssn);
	c0_tre->dword[2] = MSM_GPI_Q2SPI_CONFIG0_TRE_DWORD2(cn_delay, idx, div);
	c0_tre->dword[3] = MSM_GPI_Q2SPI_CONFIG0_TRE_DWORD3(0, 0, 0, 0, 1);
	Q2SPI_DBG_2(q2spi, "%s cs:x%x word:%d pack:%d idx:%d div:%d dword:0x%x 0x%x 0x%x 0x%x\n",
		    __func__, cs_mode, word_len, pack, idx, div, c0_tre->dword[0], c0_tre->dword[1],
		    c0_tre->dword[2], c0_tre->dword[3]);
	q2spi->setup_config0 = true;
	return c0_tre;
}

/* 3.10.4.9 Q2SPI */
static struct
msm_gpi_tre *setup_go_tre(int cmd, int cs, int rx_len, int flags, struct q2spi_geni *q2spi)
{
	struct msm_gpi_tre *go_tre = &q2spi->gsi->go_tre;
	int chain = 0;
	int eot = 0;
	int eob = 0;
	int link_rx = 0;

	if (IS_ERR_OR_NULL(go_tre))
		return go_tre;

	go_tre->dword[0] = MSM_GPI_Q2SPI_GO_TRE_DWORD0(flags, cs, cmd);
	go_tre->dword[1] = MSM_GPI_Q2SPI_GO_TRE_DWORD1;
	go_tre->dword[2] = MSM_GPI_Q2SPI_GO_TRE_DWORD2(rx_len);
	if (cmd == Q2SPI_RX_ONLY) {
		eot = 0;
		eob = 0;
		/* GO TRE on RX: processing needed check this */
		chain = 0;
		link_rx = 1;
	} else if (cmd == Q2SPI_TX_ONLY) {
		eot = 0;
		/* GO TRE on TX: processing needed check this */
		eob = 0;
		chain = 1;
	} else if (cmd == Q2SPI_TX_RX) {
		eot = 0;
		eob = 0;
		chain = 1;
		link_rx = 1;
	}
	go_tre->dword[3] = MSM_GPI_Q2SPI_GO_TRE_DWORD3(link_rx, 0, eot, eob, chain);

	Q2SPI_DBG_2(q2spi, "%s len:%d flags:0x%x cs:%d cmd:%d chain %d dword:0x%x 0x%x 0x%x 0x%x\n",
		    __func__, rx_len, flags, cs, cmd, chain, go_tre->dword[0],
		    go_tre->dword[1], go_tre->dword[2], go_tre->dword[3]);
	return go_tre;
}

/*3.10.5 DMA TRE */
static struct
msm_gpi_tre *setup_dma_tre(struct msm_gpi_tre *tre, dma_addr_t buf, u32 len,
			   struct q2spi_geni *q2spi, bool is_tx)
{
	if (IS_ERR_OR_NULL(tre))
		return tre;

	tre->dword[0] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD0(buf);
	tre->dword[1] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD1(buf);
	tre->dword[2] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD2(len);
	tre->dword[3] = MSM_GPI_DMA_W_BUFFER_TRE_DWORD3(0, 0, is_tx, 0, 0);
	Q2SPI_DBG_2(q2spi, "%s dma_tre->dword[0]:0x%x dword[1]:0x%x dword[2]:0x%x dword[3]:0x%x\n",
		    __func__, tre->dword[0], tre->dword[1],
		    tre->dword[2], tre->dword[3]);
	return tre;
}

int check_gsi_transfer_completion_db_rx(struct q2spi_geni *q2spi)
{
	int i = 0, ret = 0;
	unsigned long timeout = 0, xfer_timeout = 0;

	xfer_timeout = XFER_TIMEOUT_OFFSET;
	timeout = wait_for_completion_timeout(&q2spi->db_rx_cb, msecs_to_jiffies(xfer_timeout));
	if (!timeout) {
		Q2SPI_DEBUG(q2spi, "%s Rx[%d] timeout%lu\n", __func__, i, timeout);
		ret = -ETIMEDOUT;
	} else {
		Q2SPI_DBG_1(q2spi, "%s rx completed\n", __func__);
	}

	if (q2spi->gsi->qup_gsi_err) {
		ret = -EIO;
		Q2SPI_DEBUG(q2spi, "%s Err QUP GSI Error\n", __func__);
		q2spi->gsi->qup_gsi_err = false;
		q2spi->setup_config0 = false;
		gpi_q2spi_terminate_all(q2spi->gsi->tx_c);
	}
	return ret;
}

int check_gsi_transfer_completion(struct q2spi_geni *q2spi)
{
	int i = 0, ret = 0;
	unsigned long timeleft = 0, xfer_timeout = 0;

	xfer_timeout = XFER_TIMEOUT_OFFSET;
	Q2SPI_DBG_1(q2spi, "%s tx_eot:%d rx_eot:%d\n", __func__,
		    q2spi->gsi->num_tx_eot, q2spi->gsi->num_rx_eot);
	for (i = 0 ; i < q2spi->gsi->num_tx_eot; i++) {
		timeleft =
			wait_for_completion_timeout(&q2spi->tx_cb, msecs_to_jiffies(xfer_timeout));
		if (!timeleft) {
			Q2SPI_DEBUG(q2spi, "%s PID:%d Tx[%d] timeout\n", __func__, current->pid, i);
			ret = -ETIMEDOUT;
			goto err_gsi_geni_transfer;
		} else if (!q2spi->gsi->qup_gsi_err) {
			Q2SPI_DBG_1(q2spi, "%s tx completed\n", __func__);
		}
	}

	for (i = 0 ; i < q2spi->gsi->num_rx_eot; i++) {
		timeleft =
			wait_for_completion_timeout(&q2spi->rx_cb, msecs_to_jiffies(xfer_timeout));
		if (!timeleft) {
			Q2SPI_DEBUG(q2spi, "%s PID:%d Rx[%d] timeout\n", __func__, current->pid, i);
			ret = -ETIMEDOUT;
			goto err_gsi_geni_transfer;
		} else if (!q2spi->gsi->qup_gsi_err) {
			Q2SPI_DBG_1(q2spi, "%s rx completed\n", __func__);
		}
	}
err_gsi_geni_transfer:
	if (q2spi->gsi->qup_gsi_err || !timeleft) {
		ret = -ETIMEDOUT;
		Q2SPI_DEBUG(q2spi, "%s Err QUP Gsi Error\n", __func__);
		q2spi->gsi->qup_gsi_err = false;
		q2spi->setup_config0 = false;
		/* Block on TX completion callback for start sequence failure */
		wait_for_completion_interruptible_timeout
				(&q2spi->wait_comp_start_fail,
				msecs_to_jiffies(TIMEOUT_MSECONDS));
		if (!q2spi->is_start_seq_fail)
			gpi_q2spi_terminate_all(q2spi->gsi->tx_c);
	}
	return ret;
}

int q2spi_setup_gsi_xfer(struct q2spi_packet *q2spi_pkt)
{
	struct msm_gpi_tre *c0_tre = NULL;
	struct msm_gpi_tre *go_tre = NULL;
	struct msm_gpi_tre *tx_tre = NULL;
	struct msm_gpi_tre *rx_tre = NULL;
	struct scatterlist *xfer_tx_sg;
	struct scatterlist *xfer_rx_sg;
	u8 cs = 0;
	u32 tx_rx_len = 0;
	int rx_nent = 0;
	int tx_nent = 0;
	int go_flags = 0;
	unsigned long flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
	struct q2spi_geni *q2spi = q2spi_pkt->q2spi;
	struct q2spi_dma_transfer *xfer;
	u8 cmd;

	if (q2spi_pkt->m_cmd_param == Q2SPI_RX_ONLY)
		xfer = q2spi->db_xfer;
	else
		xfer = q2spi_pkt->xfer;
	cmd = xfer->cmd;

	Q2SPI_DBG_2(q2spi, "%s PID=%d xfer:%p vtype=%d cmd:%d q2spi_pkt:%p\n", __func__,
		    current->pid, xfer, q2spi_pkt->vtype, cmd, q2spi_pkt);
	q2spi->gsi->num_tx_eot = 0;
	q2spi->gsi->num_rx_eot = 0;
	q2spi->gsi->qup_gsi_err = false;
	q2spi->gsi->qup_gsi_global_err = false;
	xfer_tx_sg = q2spi->gsi->tx_sg;
	xfer_rx_sg = q2spi->gsi->rx_sg;
	c0_tre = &q2spi->gsi->config0_tre;
	go_tre = &q2spi->gsi->go_tre;
	tx_nent++;
	if (!q2spi->setup_config0) {
		c0_tre = setup_cfg0_tre(q2spi);
		if (IS_ERR_OR_NULL(c0_tre)) {
			Q2SPI_DEBUG(q2spi, "%s Err setting c0_tre", __func__);
			return -EINVAL;
		}
	}

	if (cmd == Q2SPI_TX_ONLY)
		tx_rx_len = xfer->tx_data_len;
	else
		tx_rx_len = xfer->rx_data_len;
	go_flags |= Q2SPI_CMD;
	go_flags |= (SINGLE_SDR_MODE << Q2SPI_MODE_SHIFT) & Q2SPI_MODE;
	go_tre = setup_go_tre(cmd, cs, tx_rx_len, go_flags, q2spi);
	if (IS_ERR_OR_NULL(go_tre)) {
		Q2SPI_DEBUG(q2spi, "%s Err setting g0_tre", __func__);
		return -EINVAL;
	}
	if (cmd == Q2SPI_TX_ONLY) {
		tx_nent += 2;
	} else if (cmd == Q2SPI_RX_ONLY) {
		tx_nent++;
		rx_nent++;
	} else if (cmd == Q2SPI_TX_RX) {
		tx_nent += 2;
		rx_nent++;
	}
	Q2SPI_DBG_2(q2spi, "%s tx_nent:%d rx_nent:%d\n", __func__, tx_nent, rx_nent);
	sg_init_table(xfer_tx_sg, tx_nent);
	if (rx_nent)
		sg_init_table(xfer_rx_sg, rx_nent);
	if (c0_tre)
		sg_set_buf(xfer_tx_sg++, c0_tre, sizeof(*c0_tre));
	sg_set_buf(xfer_tx_sg++, go_tre, sizeof(*go_tre));
	tx_tre = &q2spi->gsi->tx_dma_tre;
	tx_tre = setup_dma_tre(tx_tre, xfer->tx_dma, xfer->tx_len, q2spi, 1);
	if (IS_ERR_OR_NULL(tx_tre)) {
		Q2SPI_DEBUG(q2spi, "%s Err setting up tx tre\n", __func__);
		return -EINVAL;
	}
	sg_set_buf(xfer_tx_sg++, tx_tre, sizeof(*tx_tre));
	q2spi->gsi->num_tx_eot++;

	q2spi->gsi->tx_desc = dmaengine_prep_slave_sg(q2spi->gsi->tx_c, q2spi->gsi->tx_sg, tx_nent,
						      DMA_MEM_TO_DEV, flags);
	if (IS_ERR_OR_NULL(q2spi->gsi->tx_desc)) {
		Q2SPI_DEBUG(q2spi, "%s Err setting up tx desc\n", __func__);
		return -EIO;
	}
	q2spi->gsi->tx_ev.init.cb_param = q2spi_pkt;
	q2spi->gsi->tx_desc->callback = q2spi_gsi_tx_callback;
	q2spi->gsi->tx_desc->callback_param = &q2spi->gsi->tx_cb_param;
	q2spi->gsi->tx_cb_param.userdata = q2spi_pkt;
	q2spi->gsi->tx_cookie = dmaengine_submit(q2spi->gsi->tx_desc);
	Q2SPI_DBG_2(q2spi, "%s Tx cb_param:%p\n", __func__, q2spi->gsi->tx_desc->callback_param);
	if (dma_submit_error(q2spi->gsi->tx_cookie)) {
		Q2SPI_DEBUG(q2spi, "%s Err dmaengine_submit failed (%d)\n",
			    __func__, q2spi->gsi->tx_cookie);
		gpi_q2spi_terminate_all(q2spi->gsi->tx_c);
		return -EINVAL;
	}

	if (cmd == Q2SPI_TX_RX) {
		rx_tre = &q2spi->gsi->rx_dma_tre;
		rx_tre = setup_dma_tre(rx_tre, xfer->rx_dma, xfer->rx_len, q2spi, 1);
		if (IS_ERR_OR_NULL(rx_tre)) {
			Q2SPI_DEBUG(q2spi, "%s Err setting up rx tre\n", __func__);
			return -EINVAL;
		}
		sg_set_buf(xfer_rx_sg, rx_tre, sizeof(*rx_tre));
		q2spi->gsi->rx_desc = dmaengine_prep_slave_sg(q2spi->gsi->rx_c, q2spi->gsi->rx_sg,
							      rx_nent, DMA_DEV_TO_MEM, flags);
		if (IS_ERR_OR_NULL(q2spi->gsi->rx_desc)) {
			Q2SPI_DEBUG(q2spi, "%s rx_desc fail\n", __func__);
			return -EIO;
		}
		q2spi->gsi->rx_ev.init.cb_param = q2spi_pkt;
		q2spi->gsi->rx_desc->callback = q2spi_gsi_rx_callback;
		q2spi->gsi->rx_desc->callback_param = &q2spi->gsi->rx_cb_param;
		q2spi->gsi->rx_cb_param.userdata = q2spi_pkt;
		q2spi->gsi->num_rx_eot++;
		q2spi->gsi->rx_cookie = dmaengine_submit(q2spi->gsi->rx_desc);
		Q2SPI_DBG_2(q2spi, "%s Rx cb_param:%p\n", __func__,
			    q2spi->gsi->rx_desc->callback_param);
		if (dma_submit_error(q2spi->gsi->rx_cookie)) {
			Q2SPI_DEBUG(q2spi, "%s Err dmaengine_submit failed (%d)\n",
				    __func__, q2spi->gsi->rx_cookie);
			gpi_q2spi_terminate_all(q2spi->gsi->rx_c);
			return -EINVAL;
		}
	} else if (cmd == Q2SPI_RX_ONLY) {
		rx_tre = &q2spi->gsi->rx_dma_tre;
		rx_tre = setup_dma_tre(rx_tre, xfer->rx_dma, xfer->rx_len, q2spi, 1);
		if (IS_ERR_OR_NULL(rx_tre)) {
			Q2SPI_DEBUG(q2spi, "%s Err setting up rx tre\n", __func__);
			return -EINVAL;
		}
		sg_set_buf(xfer_rx_sg, rx_tre, sizeof(*rx_tre));
		q2spi->gsi->db_rx_desc = dmaengine_prep_slave_sg(q2spi->gsi->rx_c,
								 q2spi->gsi->rx_sg,
								 rx_nent, DMA_DEV_TO_MEM, flags);
		if (IS_ERR_OR_NULL(q2spi->gsi->db_rx_desc)) {
			Q2SPI_DEBUG(q2spi, "%s Err db_rx_desc fail\n", __func__);
			return -EIO;
		}
		q2spi->gsi->db_rx_desc->callback = q2spi_gsi_rx_callback;
		q2spi->gsi->db_rx_desc->callback_param = &q2spi->gsi->db_rx_cb_param;
		q2spi->gsi->db_rx_cb_param.userdata = q2spi_pkt;
		q2spi->gsi->num_rx_eot++;
		q2spi->gsi->rx_cookie = dmaengine_submit(q2spi->gsi->db_rx_desc);
		Q2SPI_DBG_1(q2spi, "%s DB cb_param:%p\n", __func__,
			    q2spi->gsi->db_rx_desc->callback_param);
		if (dma_submit_error(q2spi->gsi->rx_cookie)) {
			Q2SPI_DEBUG(q2spi, "%s Err dmaengine_submit failed (%d)\n",
				    __func__, q2spi->gsi->rx_cookie);
			gpi_q2spi_terminate_all(q2spi->gsi->rx_c);
			return -EINVAL;
		}
	}
	if (cmd & Q2SPI_RX_ONLY) {
		Q2SPI_DBG_1(q2spi, "%s rx_c dma_async_issue_pending\n", __func__);
		q2spi_dump_ipc(q2spi, "GSI DMA-RX", (char *)xfer->rx_buf, tx_rx_len);
		if (q2spi_pkt->m_cmd_param == Q2SPI_RX_ONLY)
			reinit_completion(&q2spi->db_rx_cb);
		else
			reinit_completion(&q2spi->rx_cb);
		dma_async_issue_pending(q2spi->gsi->rx_c);
	}

	if (cmd & Q2SPI_TX_ONLY)
		q2spi_dump_ipc(q2spi, "GSI DMA TX",
			       (char *)xfer->tx_buf, Q2SPI_HEADER_LEN + tx_rx_len);

	Q2SPI_DBG_1(q2spi, "%s tx_c dma_async_issue_pending\n", __func__);
	reinit_completion(&q2spi->tx_cb);
	dma_async_issue_pending(q2spi->gsi->tx_c);
	Q2SPI_DBG_2(q2spi, "%s End PID=%d\n", __func__, current->pid);
	return 0;
}

void q2spi_gsi_ch_ev_cb(struct dma_chan *ch, struct msm_gpi_cb const *cb, void *ptr)
{
	const struct qup_q2spi_cr_header_event *q2spi_cr_hdr_event;
	struct q2spi_geni *q2spi = ptr;
	int num_crs, i = 0;

	Q2SPI_DBG_1(q2spi, "%s event:%s\n", __func__, TO_GPI_CB_EVENT_STR(cb->cb_event));
	switch (cb->cb_event) {
	case MSM_GPI_QUP_NOTIFY:
	case MSM_GPI_QUP_MAX_EVENT:
		Q2SPI_DBG_1(q2spi, "%s cb_ev %s status %llu ts %llu count %llu\n",
			    __func__, TO_GPI_CB_EVENT_STR(cb->cb_event), cb->status,
			    cb->timestamp, cb->count);
		break;
	case MSM_GPI_QUP_ERROR:
	case MSM_GPI_QUP_CH_ERROR:
	case MSM_GPI_QUP_FW_ERROR:
	case MSM_GPI_QUP_PENDING_EVENT:
	case MSM_GPI_QUP_EOT_DESC_MISMATCH:
	case MSM_GPI_QUP_SW_ERROR:
		Q2SPI_DBG_1(q2spi, "%s cb_ev %s status %llu ts %llu count %llu\n",
			    __func__, TO_GPI_CB_EVENT_STR(cb->cb_event), cb->status,
			    cb->timestamp, cb->count);
		Q2SPI_DBG_2(q2spi, "%s err_routine:%u err_type:%u err.code%u\n",
			    __func__, cb->error_log.routine, cb->error_log.type,
			    cb->error_log.error_code);
		q2spi->gsi->qup_gsi_err = true;
		complete_all(&q2spi->tx_cb);
		complete_all(&q2spi->rx_cb);
		break;
	case MSM_GPI_QUP_CR_HEADER:
		/* Update last access time of a device for autosuspend */
		pm_runtime_mark_last_busy(q2spi->dev);
		q2spi->gsi->qup_gsi_err = false;
		q2spi_cr_hdr_event = &cb->q2spi_cr_header_event;
		num_crs = q2spi_cr_hdr_event->byte0_len;
		if (q2spi_cr_hdr_event->code == Q2SPI_CR_HEADER_LEN_ZERO ||
		    q2spi_cr_hdr_event->code == Q2SPI_CR_HEADER_INCORRECT ||
		    num_crs > MAX_RX_CRS) {
			Q2SPI_DEBUG(q2spi, "%s Invalid num_crs:%d or Negative DB header code:%d\n",
				    __func__, num_crs, q2spi_cr_hdr_event->code);
			q2spi->q2spi_cr_hdr_err = true;
			break;
		}

		for (i = 0; i < num_crs; i++) {
			if (q2spi_cr_hdr_event->cr_hdr[i] == CR_ADDR_LESS_RD) {
				reinit_completion(&q2spi->sma_rd_comp);
				atomic_inc(&q2spi->doorbell_pending);
				if (!atomic_read(&q2spi->sma_wr_pending))
					atomic_set(&q2spi->sma_rd_pending, 1);
			}
			if (q2spi_cr_hdr_event->cr_hdr[i] == CR_BULK_ACCESS_STATUS)
				atomic_dec(&q2spi->doorbell_pending);
			if (q2spi_cr_hdr_event->cr_hdr[i] == CR_ADDR_LESS_WR) {
				if (!atomic_read(&q2spi->sma_rd_pending))
					atomic_set(&q2spi->sma_wr_pending, 1);
			}
		}
		Q2SPI_DBG_2(q2spi, "%s GSI doorbell event, db_pending:%d\n",
			    __func__, atomic_read(&q2spi->doorbell_pending));
		q2spi_parse_cr_header(q2spi, cb);
		break;
	default:
		break;
	}

	if (cb->cb_event == MSM_GPI_QUP_ERROR)
		q2spi->gsi->qup_gsi_global_err = true;

	if (cb->cb_event == MSM_GPI_QUP_FW_ERROR) {
		q2spi_geni_se_dump_regs(q2spi);
		gpi_dump_for_geni(q2spi->gsi->tx_c);
	}

	if (q2spi->gsi->qup_gsi_err)
		Q2SPI_DEBUG(q2spi, "%s set qup_gsi_err\n", __func__);
}
