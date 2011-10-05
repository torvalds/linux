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

/* HTC runs over mailbox 0 */
#define HTC_MAILBOX	0

#define ATH6KL_TARGET_DEBUG_INTR_MASK     0x01

/* FIXME: are these duplicates with MAX_SCATTER_ values in hif.h? */
#define ATH6KL_SCATTER_ENTRIES_PER_REQ            16
#define ATH6KL_MAX_TRANSFER_SIZE_PER_SCATTER      (16 * 1024)
#define ATH6KL_SCATTER_REQS                       4

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

struct ath6kl_device {
	spinlock_t lock;
	struct ath6kl_irq_proc_registers irq_proc_reg;
	struct ath6kl_irq_enable_reg irq_en_reg;
	struct htc_target *htc_cnxt;
	struct ath6kl *ar;
};

int ath6kl_hif_setup(struct ath6kl_device *dev);
int ath6kl_hif_unmask_intrs(struct ath6kl_device *dev);
int ath6kl_hif_mask_intrs(struct ath6kl_device *dev);
int ath6kl_hif_poll_mboxmsg_rx(struct ath6kl_device *dev,
			       u32 *lk_ahd, int timeout);
int ath6kl_hif_rx_control(struct ath6kl_device *dev, bool enable_rx);
int ath6kl_hif_disable_intrs(struct ath6kl_device *dev);

int ath6kl_hif_rw_comp_handler(void *context, int status);
int ath6kl_hif_intr_bh_handler(struct ath6kl *ar);

/* Scatter Function and Definitions */
int ath6kl_hif_submit_scat_req(struct ath6kl_device *dev,
			       struct hif_scatter_req *scat_req, bool read);

#endif /*ATH6KL_H_ */
