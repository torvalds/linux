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

#ifndef __T7XX_HIF_DPMAIF_H__
#define __T7XX_HIF_DPMAIF_H__

#include <linux/bitmap.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "t7xx_dpmaif.h"
#include "t7xx_pci.h"
#include "t7xx_state_monitor.h"

/* SKB control buffer */
struct t7xx_skb_cb {
	u8	netif_idx;
	u8	txq_number;
	u8	rx_pkt_type;
};

#define T7XX_SKB_CB(__skb)	((struct t7xx_skb_cb *)(__skb)->cb)

enum dpmaif_rdwr {
	DPMAIF_READ,
	DPMAIF_WRITE,
};

/* Structure of DL BAT */
struct dpmaif_cur_rx_skb_info {
	bool			msg_pit_received;
	struct sk_buff		*cur_skb;
	unsigned int		cur_chn_idx;
	unsigned int		check_sum;
	unsigned int		pit_dp;
	unsigned int		pkt_type;
	int			err_payload;
};

struct dpmaif_bat {
	unsigned int		p_buffer_addr;
	unsigned int		buffer_addr_ext;
};

struct dpmaif_bat_skb {
	struct sk_buff		*skb;
	dma_addr_t		data_bus_addr;
	unsigned int		data_len;
};

struct dpmaif_bat_page {
	struct page		*page;
	dma_addr_t		data_bus_addr;
	unsigned int		offset;
	unsigned int		data_len;
};

enum bat_type {
	BAT_TYPE_NORMAL,
	BAT_TYPE_FRAG,
};

struct dpmaif_bat_request {
	void			*bat_base;
	dma_addr_t		bat_bus_addr;
	unsigned int		bat_size_cnt;
	unsigned int		bat_wr_idx;
	unsigned int		bat_release_rd_idx;
	void			*bat_skb;
	unsigned int		pkt_buf_sz;
	unsigned long		*bat_bitmap;
	atomic_t		refcnt;
	spinlock_t		mask_lock; /* Protects BAT mask */
	enum bat_type		type;
};

struct dpmaif_rx_queue {
	unsigned int		index;
	bool			que_started;
	unsigned int		budget;

	void			*pit_base;
	dma_addr_t		pit_bus_addr;
	unsigned int		pit_size_cnt;

	unsigned int		pit_rd_idx;
	unsigned int		pit_wr_idx;
	unsigned int		pit_release_rd_idx;

	struct dpmaif_bat_request *bat_req;
	struct dpmaif_bat_request *bat_frag;

	wait_queue_head_t	rx_wq;
	struct task_struct	*rx_thread;
	struct sk_buff_head	skb_list;
	unsigned int		skb_list_max_len;

	struct workqueue_struct	*worker;
	struct work_struct	dpmaif_rxq_work;

	atomic_t		rx_processing;

	struct dpmaif_ctrl	*dpmaif_ctrl;
	unsigned int		expect_pit_seq;
	unsigned int		pit_remain_release_cnt;
	struct dpmaif_cur_rx_skb_info rx_data_info;
};

struct dpmaif_tx_queue {
	unsigned int		index;
	bool			que_started;
	atomic_t		tx_budget;
	void			*drb_base;
	dma_addr_t		drb_bus_addr;
	unsigned int		drb_size_cnt;
	unsigned int		drb_wr_idx;
	unsigned int		drb_rd_idx;
	unsigned int		drb_release_rd_idx;
	void			*drb_skb_base;
	wait_queue_head_t	req_wq;
	struct workqueue_struct	*worker;
	struct work_struct	dpmaif_tx_work;
	spinlock_t		tx_lock; /* Protects txq DRB */
	atomic_t		tx_processing;

	struct dpmaif_ctrl	*dpmaif_ctrl;
	struct sk_buff_head	tx_skb_head;
};

struct dpmaif_isr_para {
	struct dpmaif_ctrl	*dpmaif_ctrl;
	unsigned char		pcie_int;
	unsigned char		dlq_id;
};

enum dpmaif_state {
	DPMAIF_STATE_MIN,
	DPMAIF_STATE_PWROFF,
	DPMAIF_STATE_PWRON,
	DPMAIF_STATE_EXCEPTION,
	DPMAIF_STATE_MAX
};

enum dpmaif_txq_state {
	DMPAIF_TXQ_STATE_IRQ,
	DMPAIF_TXQ_STATE_FULL,
};

struct dpmaif_callbacks {
	void (*state_notify)(struct t7xx_pci_dev *t7xx_dev,
			     enum dpmaif_txq_state state, int txq_number);
	void (*recv_skb)(struct t7xx_pci_dev *t7xx_dev, struct sk_buff *skb);
};

struct dpmaif_ctrl {
	struct device			*dev;
	struct t7xx_pci_dev		*t7xx_dev;
	enum dpmaif_state		state;
	bool				dpmaif_sw_init_done;
	struct dpmaif_hw_info		hw_info;
	struct dpmaif_tx_queue		txq[DPMAIF_TXQ_NUM];
	struct dpmaif_rx_queue		rxq[DPMAIF_RXQ_NUM];

	unsigned char			rxq_int_mapping[DPMAIF_RXQ_NUM];
	struct dpmaif_isr_para		isr_para[DPMAIF_RXQ_NUM];

	struct dpmaif_bat_request	bat_req;
	struct dpmaif_bat_request	bat_frag;
	struct workqueue_struct		*bat_release_wq;
	struct work_struct		bat_release_work;

	wait_queue_head_t		tx_wq;
	struct task_struct		*tx_thread;

	struct dpmaif_callbacks		*callbacks;
};

struct dpmaif_ctrl *t7xx_dpmaif_hif_init(struct t7xx_pci_dev *t7xx_dev,
					 struct dpmaif_callbacks *callbacks);
void t7xx_dpmaif_hif_exit(struct dpmaif_ctrl *dpmaif_ctrl);
int t7xx_dpmaif_md_state_callback(struct dpmaif_ctrl *dpmaif_ctrl, enum md_state state);
unsigned int t7xx_ring_buf_get_next_wr_idx(unsigned int buf_len, unsigned int buf_idx);
unsigned int t7xx_ring_buf_rd_wr_count(unsigned int total_cnt, unsigned int rd_idx,
				       unsigned int wr_idx, enum dpmaif_rdwr);

#endif /* __T7XX_HIF_DPMAIF_H__ */
