/*
 * Copyright (c) 2007-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HTC_HIF_H
#define HTC_HIF_H

#include "htc.h"
#include "hif.h"

#define ATH6KL_MAILBOXES	4

/* HTC runs over mailbox 0 */
#define HTC_MAILBOX	0

#define ATH6KL_TARGET_DEBUG_INTR_MASK     0x01

#define OTHER_INTS_ENABLED		(INT_STATUS_ENABLE_ERROR_MASK |	\
					INT_STATUS_ENABLE_CPU_MASK   |	\
					INT_STATUS_ENABLE_COUNTER_MASK)

#define ATH6KL_REG_IO_BUFFER_SIZE			32
#define ATH6KL_MAX_REG_IO_BUFFERS			8
#define ATH6KL_SCATTER_ENTRIES_PER_REQ            16
#define ATH6KL_MAX_TRANSFER_SIZE_PER_SCATTER      (16 * 1024)
#define ATH6KL_SCATTER_REQS                       4

#ifndef A_CACHE_LINE_PAD
#define A_CACHE_LINE_PAD                        128
#endif
#define ATH6KL_MIN_SCATTER_ENTRIES_PER_REQ        2
#define ATH6KL_MIN_TRANSFER_SIZE_PER_SCATTER      (4 * 1024)

struct ath6kl_irq_proc_registers {
	u8 host_int_status;
	u8 cpu_int_status;
	u8 error_int_status;
	u8 counter_int_status;
	u8 mbox_frame;
	u8 rx_lkahd_valid;
	u8 host_int_status2;
	u8 gmbox_rx_avail;
	__le32 rx_lkahd[2];
	__le32 rx_gmbox_lkahd_alias[2];
} __packed;

struct ath6kl_irq_enable_reg {
	u8 int_status_en;
	u8 cpu_int_status_en;
	u8 err_int_status_en;
	u8 cntr_int_status_en;
} __packed;

/* buffers for ASYNC I/O */
struct ath6kl_async_reg_io_buffer {
	struct htc_packet packet;
	u8 pad1[A_CACHE_LINE_PAD];
	/* cache-line safe with pads around */
	u8 buf[ATH6KL_REG_IO_BUFFER_SIZE];
	u8 pad2[A_CACHE_LINE_PAD];
};

struct ath6kl_device {
	spinlock_t lock;
	u8 pad1[A_CACHE_LINE_PAD];
	struct ath6kl_irq_proc_registers irq_proc_reg;
	u8 pad2[A_CACHE_LINE_PAD];
	struct ath6kl_irq_enable_reg irq_en_reg;
	u8 pad3[A_CACHE_LINE_PAD];
	u32 block_sz;
	u32 block_mask;
	struct htc_target *htc_cnxt;
	struct list_head reg_io;
	struct ath6kl_async_reg_io_buffer reg_io_buf[ATH6KL_MAX_REG_IO_BUFFERS];
	int (*msg_pending) (struct htc_target *target, u32 lk_ahds[],
			    int *npkts_fetched);
	struct hif_dev_scat_sup_info hif_scat_info;
	bool virt_scat;
	int max_rx_bndl_sz;
	int max_tx_bndl_sz;
	int chk_irq_status_cnt;
	struct ath6kl *ar;
};

int ath6kldev_setup(struct ath6kl_device *dev);
int ath6kldev_unmask_intrs(struct ath6kl_device *dev);
int ath6kldev_mask_intrs(struct ath6kl_device *dev);
int ath6kldev_poll_mboxmsg_rx(struct ath6kl_device *dev,
			      u32 *lk_ahd, int timeout);
int ath6kldev_rx_control(struct ath6kl_device *dev, bool enable_rx);
int ath6kldev_disable_intrs(struct ath6kl_device *dev);

int ath6kldev_rw_comp_handler(void *context, int status);
int ath6kldev_intr_bh_handler(struct ath6kl *ar);

/* Scatter Function and Definitions */
int ath6kldev_setup_msg_bndl(struct ath6kl_device *dev, int max_msg_per_xfer);
int ath6kldev_submit_scat_req(struct ath6kl_device *dev,
			    struct hif_scatter_req *scat_req, bool read);

#endif /*ATH6KL_H_ */
