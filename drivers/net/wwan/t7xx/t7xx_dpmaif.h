/* SPDX-License-Identifier: GPL-2.0-only
 *
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

#ifndef __T7XX_DPMAIF_H__
#define __T7XX_DPMAIF_H__

#include <linux/bits.h>
#include <linux/types.h>

#define DPMAIF_DL_PIT_SEQ_VALUE		251
#define DPMAIF_UL_DRB_SIZE_WORD		4

#define DPMAIF_MAX_CHECK_COUNT		1000000
#define DPMAIF_CHECK_TIMEOUT_US		10000
#define DPMAIF_CHECK_INIT_TIMEOUT_US	100000
#define DPMAIF_CHECK_DELAY_US		10

#define DPMAIF_RXQ_NUM			2
#define DPMAIF_TXQ_NUM			5

struct dpmaif_isr_en_mask {
	unsigned int			ap_ul_l2intr_en_msk;
	unsigned int			ap_dl_l2intr_en_msk;
	unsigned int			ap_udl_ip_busy_en_msk;
	unsigned int			ap_dl_l2intr_err_en_msk;
};

struct dpmaif_ul {
	bool				que_started;
	unsigned char			reserved[3];
	dma_addr_t			drb_base;
	unsigned int			drb_size_cnt;
};

struct dpmaif_dl {
	bool				que_started;
	unsigned char			reserved[3];
	dma_addr_t			pit_base;
	unsigned int			pit_size_cnt;
	dma_addr_t			bat_base;
	unsigned int			bat_size_cnt;
	dma_addr_t			frg_base;
	unsigned int			frg_size_cnt;
	unsigned int			pit_seq;
};

struct dpmaif_hw_info {
	struct device			*dev;
	void __iomem			*pcie_base;
	struct dpmaif_dl		dl_que[DPMAIF_RXQ_NUM];
	struct dpmaif_ul		ul_que[DPMAIF_TXQ_NUM];
	struct dpmaif_isr_en_mask	isr_en_mask;
};

/* DPMAIF HW Initialization parameter structure */
struct dpmaif_hw_params {
	/* UL part */
	dma_addr_t			drb_base_addr[DPMAIF_TXQ_NUM];
	unsigned int			drb_size_cnt[DPMAIF_TXQ_NUM];
	/* DL part */
	dma_addr_t			pkt_bat_base_addr[DPMAIF_RXQ_NUM];
	unsigned int			pkt_bat_size_cnt[DPMAIF_RXQ_NUM];
	dma_addr_t			frg_bat_base_addr[DPMAIF_RXQ_NUM];
	unsigned int			frg_bat_size_cnt[DPMAIF_RXQ_NUM];
	dma_addr_t			pit_base_addr[DPMAIF_RXQ_NUM];
	unsigned int			pit_size_cnt[DPMAIF_RXQ_NUM];
};

enum dpmaif_hw_intr_type {
	DPF_INTR_INVALID_MIN,
	DPF_INTR_UL_DONE,
	DPF_INTR_UL_DRB_EMPTY,
	DPF_INTR_UL_MD_NOTREADY,
	DPF_INTR_UL_MD_PWR_NOTREADY,
	DPF_INTR_UL_LEN_ERR,
	DPF_INTR_DL_DONE,
	DPF_INTR_DL_SKB_LEN_ERR,
	DPF_INTR_DL_BATCNT_LEN_ERR,
	DPF_INTR_DL_PITCNT_LEN_ERR,
	DPF_INTR_DL_PKT_EMPTY_SET,
	DPF_INTR_DL_FRG_EMPTY_SET,
	DPF_INTR_DL_MTU_ERR,
	DPF_INTR_DL_FRGCNT_LEN_ERR,
	DPF_INTR_DL_Q0_PITCNT_LEN_ERR,
	DPF_INTR_DL_Q1_PITCNT_LEN_ERR,
	DPF_INTR_DL_HPC_ENT_TYPE_ERR,
	DPF_INTR_DL_Q0_DONE,
	DPF_INTR_DL_Q1_DONE,
	DPF_INTR_INVALID_MAX
};

#define DPF_RX_QNO0			0
#define DPF_RX_QNO1			1
#define DPF_RX_QNO_DFT			DPF_RX_QNO0

struct dpmaif_hw_intr_st_para {
	unsigned int intr_cnt;
	enum dpmaif_hw_intr_type intr_types[DPF_INTR_INVALID_MAX - 1];
	unsigned int intr_queues[DPF_INTR_INVALID_MAX - 1];
};

#define DPMAIF_HW_BAT_REMAIN		64
#define DPMAIF_HW_BAT_PKTBUF		(128 * 28)
#define DPMAIF_HW_FRG_PKTBUF		128
#define DPMAIF_HW_BAT_RSVLEN		64
#define DPMAIF_HW_PKT_BIDCNT		1
#define DPMAIF_HW_MTU_SIZE		(3 * 1024 + 8)
#define DPMAIF_HW_CHK_BAT_NUM		62
#define DPMAIF_HW_CHK_FRG_NUM		3
#define DPMAIF_HW_CHK_PIT_NUM		(2 * DPMAIF_HW_CHK_BAT_NUM)

#define DP_UL_INT_DONE_OFFSET		0
#define DP_UL_INT_QDONE_MSK		GENMASK(4, 0)
#define DP_UL_INT_EMPTY_MSK		GENMASK(9, 5)
#define DP_UL_INT_MD_NOTREADY_MSK	GENMASK(14, 10)
#define DP_UL_INT_MD_PWR_NOTREADY_MSK	GENMASK(19, 15)
#define DP_UL_INT_ERR_MSK		GENMASK(24, 20)

#define DP_DL_INT_QDONE_MSK		BIT(0)
#define DP_DL_INT_SKB_LEN_ERR		BIT(1)
#define DP_DL_INT_BATCNT_LEN_ERR	BIT(2)
#define DP_DL_INT_PITCNT_LEN_ERR	BIT(3)
#define DP_DL_INT_PKT_EMPTY_MSK		BIT(4)
#define DP_DL_INT_FRG_EMPTY_MSK		BIT(5)
#define DP_DL_INT_MTU_ERR_MSK		BIT(6)
#define DP_DL_INT_FRG_LEN_ERR_MSK	BIT(7)
#define DP_DL_INT_Q0_PITCNT_LEN_ERR	BIT(8)
#define DP_DL_INT_Q1_PITCNT_LEN_ERR	BIT(9)
#define DP_DL_INT_HPC_ENT_TYPE_ERR	BIT(10)
#define DP_DL_INT_Q0_DONE		BIT(13)
#define DP_DL_INT_Q1_DONE		BIT(14)

#define DP_DL_Q0_STATUS_MASK		(DP_DL_INT_Q0_PITCNT_LEN_ERR | DP_DL_INT_Q0_DONE)
#define DP_DL_Q1_STATUS_MASK		(DP_DL_INT_Q1_PITCNT_LEN_ERR | DP_DL_INT_Q1_DONE)

int t7xx_dpmaif_hw_init(struct dpmaif_hw_info *hw_info, struct dpmaif_hw_params *init_param);
int t7xx_dpmaif_hw_stop_all_txq(struct dpmaif_hw_info *hw_info);
int t7xx_dpmaif_hw_stop_all_rxq(struct dpmaif_hw_info *hw_info);
void t7xx_dpmaif_start_hw(struct dpmaif_hw_info *hw_info);
int t7xx_dpmaif_hw_get_intr_cnt(struct dpmaif_hw_info *hw_info,
				struct dpmaif_hw_intr_st_para *para, int qno);
void t7xx_dpmaif_unmask_ulq_intr(struct dpmaif_hw_info *hw_info, unsigned int q_num);
void t7xx_dpmaif_ul_update_hw_drb_cnt(struct dpmaif_hw_info *hw_info, unsigned int q_num,
				      unsigned int drb_entry_cnt);
int t7xx_dpmaif_dl_snd_hw_bat_cnt(struct dpmaif_hw_info *hw_info, unsigned int bat_entry_cnt);
int t7xx_dpmaif_dl_snd_hw_frg_cnt(struct dpmaif_hw_info *hw_info, unsigned int frg_entry_cnt);
int t7xx_dpmaif_dlq_add_pit_remain_cnt(struct dpmaif_hw_info *hw_info, unsigned int dlq_pit_idx,
				       unsigned int pit_remain_cnt);
void t7xx_dpmaif_dlq_unmask_pitcnt_len_err_intr(struct dpmaif_hw_info *hw_info,
						unsigned int qno);
void t7xx_dpmaif_dlq_unmask_rx_done(struct dpmaif_hw_info *hw_info, unsigned int qno);
bool t7xx_dpmaif_ul_clr_done(struct dpmaif_hw_info *hw_info, unsigned int qno);
void t7xx_dpmaif_ul_clr_all_intr(struct dpmaif_hw_info *hw_info);
void t7xx_dpmaif_dl_clr_all_intr(struct dpmaif_hw_info *hw_info);
void t7xx_dpmaif_clr_ip_busy_sts(struct dpmaif_hw_info *hw_info);
void t7xx_dpmaif_dl_unmask_batcnt_len_err_intr(struct dpmaif_hw_info *hw_info);
void t7xx_dpmaif_dl_unmask_pitcnt_len_err_intr(struct dpmaif_hw_info *hw_info);
unsigned int t7xx_dpmaif_ul_get_rd_idx(struct dpmaif_hw_info *hw_info, unsigned int q_num);
unsigned int t7xx_dpmaif_dl_get_bat_rd_idx(struct dpmaif_hw_info *hw_info, unsigned int q_num);
unsigned int t7xx_dpmaif_dl_get_bat_wr_idx(struct dpmaif_hw_info *hw_info, unsigned int q_num);
unsigned int t7xx_dpmaif_dl_get_frg_rd_idx(struct dpmaif_hw_info *hw_info, unsigned int q_num);
unsigned int t7xx_dpmaif_dl_dlq_pit_get_wr_idx(struct dpmaif_hw_info *hw_info,
					       unsigned int dlq_pit_idx);

#endif /* __T7XX_DPMAIF_H__ */
