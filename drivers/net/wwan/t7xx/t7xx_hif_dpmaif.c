// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "t7xx_dpmaif.h"
#include "t7xx_hif_dpmaif.h"
#include "t7xx_hif_dpmaif_rx.h"
#include "t7xx_hif_dpmaif_tx.h"
#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"
#include "t7xx_state_monitor.h"

unsigned int t7xx_ring_buf_get_next_wr_idx(unsigned int buf_len, unsigned int buf_idx)
{
	buf_idx++;

	return buf_idx < buf_len ? buf_idx : 0;
}

unsigned int t7xx_ring_buf_rd_wr_count(unsigned int total_cnt, unsigned int rd_idx,
				       unsigned int wr_idx, enum dpmaif_rdwr rd_wr)
{
	int pkt_cnt;

	if (rd_wr == DPMAIF_READ)
		pkt_cnt = wr_idx - rd_idx;
	else
		pkt_cnt = rd_idx - wr_idx - 1;

	if (pkt_cnt < 0)
		pkt_cnt += total_cnt;

	return (unsigned int)pkt_cnt;
}

static void t7xx_dpmaif_enable_irq(struct dpmaif_ctrl *dpmaif_ctrl)
{
	struct dpmaif_isr_para *isr_para;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpmaif_ctrl->isr_para); i++) {
		isr_para = &dpmaif_ctrl->isr_para[i];
		t7xx_pcie_mac_set_int(dpmaif_ctrl->t7xx_dev, isr_para->pcie_int);
	}
}

static void t7xx_dpmaif_disable_irq(struct dpmaif_ctrl *dpmaif_ctrl)
{
	struct dpmaif_isr_para *isr_para;
	int i;

	for (i = 0; i < ARRAY_SIZE(dpmaif_ctrl->isr_para); i++) {
		isr_para = &dpmaif_ctrl->isr_para[i];
		t7xx_pcie_mac_clear_int(dpmaif_ctrl->t7xx_dev, isr_para->pcie_int);
	}
}

static void t7xx_dpmaif_irq_cb(struct dpmaif_isr_para *isr_para)
{
	struct dpmaif_ctrl *dpmaif_ctrl = isr_para->dpmaif_ctrl;
	struct dpmaif_hw_intr_st_para intr_status;
	struct device *dev = dpmaif_ctrl->dev;
	struct dpmaif_hw_info *hw_info;
	int i;

	memset(&intr_status, 0, sizeof(intr_status));
	hw_info = &dpmaif_ctrl->hw_info;

	if (t7xx_dpmaif_hw_get_intr_cnt(hw_info, &intr_status, isr_para->dlq_id) < 0) {
		dev_err(dev, "Failed to get HW interrupt count\n");
		return;
	}

	t7xx_pcie_mac_clear_int_status(dpmaif_ctrl->t7xx_dev, isr_para->pcie_int);

	for (i = 0; i < intr_status.intr_cnt; i++) {
		switch (intr_status.intr_types[i]) {
		case DPF_INTR_UL_DONE:
			t7xx_dpmaif_irq_tx_done(dpmaif_ctrl, intr_status.intr_queues[i]);
			break;

		case DPF_INTR_UL_DRB_EMPTY:
		case DPF_INTR_UL_MD_NOTREADY:
		case DPF_INTR_UL_MD_PWR_NOTREADY:
			/* No need to log an error for these */
			break;

		case DPF_INTR_DL_BATCNT_LEN_ERR:
			dev_err_ratelimited(dev, "DL interrupt: packet BAT count length error\n");
			t7xx_dpmaif_dl_unmask_batcnt_len_err_intr(hw_info);
			break;

		case DPF_INTR_DL_PITCNT_LEN_ERR:
			dev_err_ratelimited(dev, "DL interrupt: PIT count length error\n");
			t7xx_dpmaif_dl_unmask_pitcnt_len_err_intr(hw_info);
			break;

		case DPF_INTR_DL_Q0_PITCNT_LEN_ERR:
			dev_err_ratelimited(dev, "DL interrupt: DLQ0 PIT count length error\n");
			t7xx_dpmaif_dlq_unmask_pitcnt_len_err_intr(hw_info, DPF_RX_QNO_DFT);
			break;

		case DPF_INTR_DL_Q1_PITCNT_LEN_ERR:
			dev_err_ratelimited(dev, "DL interrupt: DLQ1 PIT count length error\n");
			t7xx_dpmaif_dlq_unmask_pitcnt_len_err_intr(hw_info, DPF_RX_QNO1);
			break;

		case DPF_INTR_DL_DONE:
		case DPF_INTR_DL_Q0_DONE:
		case DPF_INTR_DL_Q1_DONE:
			t7xx_dpmaif_irq_rx_done(dpmaif_ctrl, intr_status.intr_queues[i]);
			break;

		default:
			dev_err_ratelimited(dev, "DL interrupt error: unknown type : %d\n",
					    intr_status.intr_types[i]);
		}
	}
}

static irqreturn_t t7xx_dpmaif_isr_handler(int irq, void *data)
{
	struct dpmaif_isr_para *isr_para = data;
	struct dpmaif_ctrl *dpmaif_ctrl;

	dpmaif_ctrl = isr_para->dpmaif_ctrl;
	if (dpmaif_ctrl->state != DPMAIF_STATE_PWRON) {
		dev_err(dpmaif_ctrl->dev, "Interrupt received before initializing DPMAIF\n");
		return IRQ_HANDLED;
	}

	t7xx_pcie_mac_clear_int(dpmaif_ctrl->t7xx_dev, isr_para->pcie_int);
	t7xx_dpmaif_irq_cb(isr_para);
	t7xx_pcie_mac_set_int(dpmaif_ctrl->t7xx_dev, isr_para->pcie_int);
	return IRQ_HANDLED;
}

static void t7xx_dpmaif_isr_parameter_init(struct dpmaif_ctrl *dpmaif_ctrl)
{
	struct dpmaif_isr_para *isr_para;
	unsigned char i;

	dpmaif_ctrl->rxq_int_mapping[DPF_RX_QNO0] = DPMAIF_INT;
	dpmaif_ctrl->rxq_int_mapping[DPF_RX_QNO1] = DPMAIF2_INT;

	for (i = 0; i < DPMAIF_RXQ_NUM; i++) {
		isr_para = &dpmaif_ctrl->isr_para[i];
		isr_para->dpmaif_ctrl = dpmaif_ctrl;
		isr_para->dlq_id = i;
		isr_para->pcie_int = dpmaif_ctrl->rxq_int_mapping[i];
	}
}

static void t7xx_dpmaif_register_pcie_irq(struct dpmaif_ctrl *dpmaif_ctrl)
{
	struct t7xx_pci_dev *t7xx_dev = dpmaif_ctrl->t7xx_dev;
	struct dpmaif_isr_para *isr_para;
	enum t7xx_int int_type;
	int i;

	t7xx_dpmaif_isr_parameter_init(dpmaif_ctrl);

	for (i = 0; i < DPMAIF_RXQ_NUM; i++) {
		isr_para = &dpmaif_ctrl->isr_para[i];
		int_type = isr_para->pcie_int;
		t7xx_pcie_mac_clear_int(t7xx_dev, int_type);

		t7xx_dev->intr_handler[int_type] = t7xx_dpmaif_isr_handler;
		t7xx_dev->intr_thread[int_type] = NULL;
		t7xx_dev->callback_param[int_type] = isr_para;

		t7xx_pcie_mac_clear_int_status(t7xx_dev, int_type);
		t7xx_pcie_mac_set_int(t7xx_dev, int_type);
	}
}

static int t7xx_dpmaif_rxtx_sw_allocs(struct dpmaif_ctrl *dpmaif_ctrl)
{
	struct dpmaif_rx_queue *rx_q;
	struct dpmaif_tx_queue *tx_q;
	int ret, rx_idx, tx_idx, i;

	ret = t7xx_dpmaif_bat_alloc(dpmaif_ctrl, &dpmaif_ctrl->bat_req, BAT_TYPE_NORMAL);
	if (ret) {
		dev_err(dpmaif_ctrl->dev, "Failed to allocate normal BAT table: %d\n", ret);
		return ret;
	}

	ret = t7xx_dpmaif_bat_alloc(dpmaif_ctrl, &dpmaif_ctrl->bat_frag, BAT_TYPE_FRAG);
	if (ret) {
		dev_err(dpmaif_ctrl->dev, "Failed to allocate frag BAT table: %d\n", ret);
		goto err_free_normal_bat;
	}

	for (rx_idx = 0; rx_idx < DPMAIF_RXQ_NUM; rx_idx++) {
		rx_q = &dpmaif_ctrl->rxq[rx_idx];
		rx_q->index = rx_idx;
		rx_q->dpmaif_ctrl = dpmaif_ctrl;
		ret = t7xx_dpmaif_rxq_init(rx_q);
		if (ret)
			goto err_free_rxq;
	}

	for (tx_idx = 0; tx_idx < DPMAIF_TXQ_NUM; tx_idx++) {
		tx_q = &dpmaif_ctrl->txq[tx_idx];
		tx_q->index = tx_idx;
		tx_q->dpmaif_ctrl = dpmaif_ctrl;
		ret = t7xx_dpmaif_txq_init(tx_q);
		if (ret)
			goto err_free_txq;
	}

	ret = t7xx_dpmaif_tx_thread_init(dpmaif_ctrl);
	if (ret) {
		dev_err(dpmaif_ctrl->dev, "Failed to start TX thread\n");
		goto err_free_txq;
	}

	ret = t7xx_dpmaif_bat_rel_wq_alloc(dpmaif_ctrl);
	if (ret)
		goto err_thread_rel;

	return 0;

err_thread_rel:
	t7xx_dpmaif_tx_thread_rel(dpmaif_ctrl);

err_free_txq:
	for (i = 0; i < tx_idx; i++) {
		tx_q = &dpmaif_ctrl->txq[i];
		t7xx_dpmaif_txq_free(tx_q);
	}

err_free_rxq:
	for (i = 0; i < rx_idx; i++) {
		rx_q = &dpmaif_ctrl->rxq[i];
		t7xx_dpmaif_rxq_free(rx_q);
	}

	t7xx_dpmaif_bat_free(dpmaif_ctrl, &dpmaif_ctrl->bat_frag);

err_free_normal_bat:
	t7xx_dpmaif_bat_free(dpmaif_ctrl, &dpmaif_ctrl->bat_req);

	return ret;
}

static void t7xx_dpmaif_sw_release(struct dpmaif_ctrl *dpmaif_ctrl)
{
	struct dpmaif_rx_queue *rx_q;
	struct dpmaif_tx_queue *tx_q;
	int i;

	t7xx_dpmaif_tx_thread_rel(dpmaif_ctrl);
	t7xx_dpmaif_bat_wq_rel(dpmaif_ctrl);

	for (i = 0; i < DPMAIF_TXQ_NUM; i++) {
		tx_q = &dpmaif_ctrl->txq[i];
		t7xx_dpmaif_txq_free(tx_q);
	}

	for (i = 0; i < DPMAIF_RXQ_NUM; i++) {
		rx_q = &dpmaif_ctrl->rxq[i];
		t7xx_dpmaif_rxq_free(rx_q);
	}
}

static int t7xx_dpmaif_start(struct dpmaif_ctrl *dpmaif_ctrl)
{
	struct dpmaif_hw_info *hw_info = &dpmaif_ctrl->hw_info;
	struct dpmaif_hw_params hw_init_para;
	struct dpmaif_rx_queue *rxq;
	struct dpmaif_tx_queue *txq;
	unsigned int buf_cnt;
	int i, ret = 0;

	if (dpmaif_ctrl->state == DPMAIF_STATE_PWRON)
		return -EFAULT;

	memset(&hw_init_para, 0, sizeof(hw_init_para));

	for (i = 0; i < DPMAIF_RXQ_NUM; i++) {
		rxq = &dpmaif_ctrl->rxq[i];
		rxq->que_started = true;
		rxq->index = i;
		rxq->budget = rxq->bat_req->bat_size_cnt - 1;

		hw_init_para.pkt_bat_base_addr[i] = rxq->bat_req->bat_bus_addr;
		hw_init_para.pkt_bat_size_cnt[i] = rxq->bat_req->bat_size_cnt;
		hw_init_para.pit_base_addr[i] = rxq->pit_bus_addr;
		hw_init_para.pit_size_cnt[i] = rxq->pit_size_cnt;
		hw_init_para.frg_bat_base_addr[i] = rxq->bat_frag->bat_bus_addr;
		hw_init_para.frg_bat_size_cnt[i] = rxq->bat_frag->bat_size_cnt;
	}

	bitmap_zero(dpmaif_ctrl->bat_req.bat_bitmap, dpmaif_ctrl->bat_req.bat_size_cnt);
	buf_cnt = dpmaif_ctrl->bat_req.bat_size_cnt - 1;
	ret = t7xx_dpmaif_rx_buf_alloc(dpmaif_ctrl, &dpmaif_ctrl->bat_req, 0, buf_cnt, true);
	if (ret) {
		dev_err(dpmaif_ctrl->dev, "Failed to allocate RX buffer: %d\n", ret);
		return ret;
	}

	buf_cnt = dpmaif_ctrl->bat_frag.bat_size_cnt - 1;
	ret = t7xx_dpmaif_rx_frag_alloc(dpmaif_ctrl, &dpmaif_ctrl->bat_frag, buf_cnt, true);
	if (ret) {
		dev_err(dpmaif_ctrl->dev, "Failed to allocate frag RX buffer: %d\n", ret);
		goto err_free_normal_bat;
	}

	for (i = 0; i < DPMAIF_TXQ_NUM; i++) {
		txq = &dpmaif_ctrl->txq[i];
		txq->que_started = true;

		hw_init_para.drb_base_addr[i] = txq->drb_bus_addr;
		hw_init_para.drb_size_cnt[i] = txq->drb_size_cnt;
	}

	ret = t7xx_dpmaif_hw_init(hw_info, &hw_init_para);
	if (ret) {
		dev_err(dpmaif_ctrl->dev, "Failed to initialize DPMAIF HW: %d\n", ret);
		goto err_free_frag_bat;
	}

	ret = t7xx_dpmaif_dl_snd_hw_bat_cnt(hw_info, rxq->bat_req->bat_size_cnt - 1);
	if (ret)
		goto err_free_frag_bat;

	ret = t7xx_dpmaif_dl_snd_hw_frg_cnt(hw_info, rxq->bat_frag->bat_size_cnt - 1);
	if (ret)
		goto err_free_frag_bat;

	t7xx_dpmaif_ul_clr_all_intr(hw_info);
	t7xx_dpmaif_dl_clr_all_intr(hw_info);
	dpmaif_ctrl->state = DPMAIF_STATE_PWRON;
	t7xx_dpmaif_enable_irq(dpmaif_ctrl);
	wake_up(&dpmaif_ctrl->tx_wq);
	return 0;

err_free_frag_bat:
	t7xx_dpmaif_bat_free(rxq->dpmaif_ctrl, rxq->bat_frag);

err_free_normal_bat:
	t7xx_dpmaif_bat_free(rxq->dpmaif_ctrl, rxq->bat_req);

	return ret;
}

static void t7xx_dpmaif_stop_sw(struct dpmaif_ctrl *dpmaif_ctrl)
{
	t7xx_dpmaif_tx_stop(dpmaif_ctrl);
	t7xx_dpmaif_rx_stop(dpmaif_ctrl);
}

static void t7xx_dpmaif_stop_hw(struct dpmaif_ctrl *dpmaif_ctrl)
{
	t7xx_dpmaif_hw_stop_all_txq(&dpmaif_ctrl->hw_info);
	t7xx_dpmaif_hw_stop_all_rxq(&dpmaif_ctrl->hw_info);
}

static int t7xx_dpmaif_stop(struct dpmaif_ctrl *dpmaif_ctrl)
{
	if (!dpmaif_ctrl->dpmaif_sw_init_done) {
		dev_err(dpmaif_ctrl->dev, "dpmaif SW init fail\n");
		return -EFAULT;
	}

	if (dpmaif_ctrl->state == DPMAIF_STATE_PWROFF)
		return -EFAULT;

	t7xx_dpmaif_disable_irq(dpmaif_ctrl);
	dpmaif_ctrl->state = DPMAIF_STATE_PWROFF;
	t7xx_dpmaif_stop_sw(dpmaif_ctrl);
	t7xx_dpmaif_tx_clear(dpmaif_ctrl);
	t7xx_dpmaif_rx_clear(dpmaif_ctrl);
	return 0;
}

int t7xx_dpmaif_md_state_callback(struct dpmaif_ctrl *dpmaif_ctrl, enum md_state state)
{
	int ret = 0;

	switch (state) {
	case MD_STATE_WAITING_FOR_HS1:
		ret = t7xx_dpmaif_start(dpmaif_ctrl);
		break;

	case MD_STATE_EXCEPTION:
		ret = t7xx_dpmaif_stop(dpmaif_ctrl);
		break;

	case MD_STATE_STOPPED:
		ret = t7xx_dpmaif_stop(dpmaif_ctrl);
		break;

	case MD_STATE_WAITING_TO_STOP:
		t7xx_dpmaif_stop_hw(dpmaif_ctrl);
		break;

	default:
		break;
	}

	return ret;
}

/**
 * t7xx_dpmaif_hif_init() - Initialize data path.
 * @t7xx_dev: MTK context structure.
 * @callbacks: Callbacks implemented by the network layer to handle RX skb and
 *	       event notifications.
 *
 * Allocate and initialize datapath control block.
 * Register datapath ISR, TX and RX resources.
 *
 * Return:
 * * dpmaif_ctrl pointer - Pointer to DPMAIF context structure.
 * * NULL		 - In case of error.
 */
struct dpmaif_ctrl *t7xx_dpmaif_hif_init(struct t7xx_pci_dev *t7xx_dev,
					 struct dpmaif_callbacks *callbacks)
{
	struct device *dev = &t7xx_dev->pdev->dev;
	struct dpmaif_ctrl *dpmaif_ctrl;
	int ret;

	if (!callbacks)
		return NULL;

	dpmaif_ctrl = devm_kzalloc(dev, sizeof(*dpmaif_ctrl), GFP_KERNEL);
	if (!dpmaif_ctrl)
		return NULL;

	dpmaif_ctrl->t7xx_dev = t7xx_dev;
	dpmaif_ctrl->callbacks = callbacks;
	dpmaif_ctrl->dev = dev;
	dpmaif_ctrl->dpmaif_sw_init_done = false;
	dpmaif_ctrl->hw_info.dev = dev;
	dpmaif_ctrl->hw_info.pcie_base = t7xx_dev->base_addr.pcie_ext_reg_base -
					 t7xx_dev->base_addr.pcie_dev_reg_trsl_addr;

	t7xx_dpmaif_register_pcie_irq(dpmaif_ctrl);
	t7xx_dpmaif_disable_irq(dpmaif_ctrl);

	ret = t7xx_dpmaif_rxtx_sw_allocs(dpmaif_ctrl);
	if (ret) {
		dev_err(dev, "Failed to allocate RX/TX SW resources: %d\n", ret);
		return NULL;
	}

	dpmaif_ctrl->dpmaif_sw_init_done = true;
	return dpmaif_ctrl;
}

void t7xx_dpmaif_hif_exit(struct dpmaif_ctrl *dpmaif_ctrl)
{
	if (dpmaif_ctrl->dpmaif_sw_init_done) {
		t7xx_dpmaif_stop(dpmaif_ctrl);
		t7xx_dpmaif_sw_release(dpmaif_ctrl);
		dpmaif_ctrl->dpmaif_sw_init_done = false;
	}
}
