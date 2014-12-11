/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#ifndef _HIF_H_
#define _HIF_H_

#include <linux/kernel.h>
#include "core.h"
#include "debug.h"

struct ath10k_hif_sg_item {
	u16 transfer_id;
	void *transfer_context; /* NULL = tx completion callback not called */
	void *vaddr; /* for debugging mostly */
	u32 paddr;
	u16 len;
};

struct ath10k_hif_cb {
	int (*tx_completion)(struct ath10k *ar,
			     struct sk_buff *wbuf);
	int (*rx_completion)(struct ath10k *ar,
			     struct sk_buff *wbuf);
};

struct ath10k_hif_ops {
	/* send a scatter-gather list to the target */
	int (*tx_sg)(struct ath10k *ar, u8 pipe_id,
		     struct ath10k_hif_sg_item *items, int n_items);

	/* read firmware memory through the diagnose interface */
	int (*diag_read)(struct ath10k *ar, u32 address, void *buf,
			 size_t buf_len);

	int (*diag_write)(struct ath10k *ar, u32 address, const void *data,
			  int nbytes);
	/*
	 * API to handle HIF-specific BMI message exchanges, this API is
	 * synchronous and only allowed to be called from a context that
	 * can block (sleep)
	 */
	int (*exchange_bmi_msg)(struct ath10k *ar,
				void *request, u32 request_len,
				void *response, u32 *response_len);

	/* Post BMI phase, after FW is loaded. Starts regular operation */
	int (*start)(struct ath10k *ar);

	/* Clean up what start() did. This does not revert to BMI phase. If
	 * desired so, call power_down() and power_up() */
	void (*stop)(struct ath10k *ar);

	int (*map_service_to_pipe)(struct ath10k *ar, u16 service_id,
				   u8 *ul_pipe, u8 *dl_pipe,
				   int *ul_is_polled, int *dl_is_polled);

	void (*get_default_pipe)(struct ath10k *ar, u8 *ul_pipe, u8 *dl_pipe);

	/*
	 * Check if prior sends have completed.
	 *
	 * Check whether the pipe in question has any completed
	 * sends that have not yet been processed.
	 * This function is only relevant for HIF pipes that are configured
	 * to be polled rather than interrupt-driven.
	 */
	void (*send_complete_check)(struct ath10k *ar, u8 pipe_id, int force);

	void (*set_callbacks)(struct ath10k *ar,
			      struct ath10k_hif_cb *callbacks);

	u16 (*get_free_queue_number)(struct ath10k *ar, u8 pipe_id);

	u32 (*read32)(struct ath10k *ar, u32 address);

	void (*write32)(struct ath10k *ar, u32 address, u32 value);

	/* Power up the device and enter BMI transfer mode for FW download */
	int (*power_up)(struct ath10k *ar);

	/* Power down the device and free up resources. stop() must be called
	 * before this if start() was called earlier */
	void (*power_down)(struct ath10k *ar);

	int (*suspend)(struct ath10k *ar);
	int (*resume)(struct ath10k *ar);
};

static inline int ath10k_hif_tx_sg(struct ath10k *ar, u8 pipe_id,
				   struct ath10k_hif_sg_item *items,
				   int n_items)
{
	return ar->hif.ops->tx_sg(ar, pipe_id, items, n_items);
}

static inline int ath10k_hif_diag_read(struct ath10k *ar, u32 address, void *buf,
				       size_t buf_len)
{
	return ar->hif.ops->diag_read(ar, address, buf, buf_len);
}

static inline int ath10k_hif_diag_write(struct ath10k *ar, u32 address,
					const void *data, int nbytes)
{
	if (!ar->hif.ops->diag_write)
		return -EOPNOTSUPP;

	return ar->hif.ops->diag_write(ar, address, data, nbytes);
}

static inline int ath10k_hif_exchange_bmi_msg(struct ath10k *ar,
					      void *request, u32 request_len,
					      void *response, u32 *response_len)
{
	return ar->hif.ops->exchange_bmi_msg(ar, request, request_len,
					     response, response_len);
}

static inline int ath10k_hif_start(struct ath10k *ar)
{
	return ar->hif.ops->start(ar);
}

static inline void ath10k_hif_stop(struct ath10k *ar)
{
	return ar->hif.ops->stop(ar);
}

static inline int ath10k_hif_map_service_to_pipe(struct ath10k *ar,
						 u16 service_id,
						 u8 *ul_pipe, u8 *dl_pipe,
						 int *ul_is_polled,
						 int *dl_is_polled)
{
	return ar->hif.ops->map_service_to_pipe(ar, service_id,
						ul_pipe, dl_pipe,
						ul_is_polled, dl_is_polled);
}

static inline void ath10k_hif_get_default_pipe(struct ath10k *ar,
					       u8 *ul_pipe, u8 *dl_pipe)
{
	ar->hif.ops->get_default_pipe(ar, ul_pipe, dl_pipe);
}

static inline void ath10k_hif_send_complete_check(struct ath10k *ar,
						  u8 pipe_id, int force)
{
	ar->hif.ops->send_complete_check(ar, pipe_id, force);
}

static inline void ath10k_hif_set_callbacks(struct ath10k *ar,
					    struct ath10k_hif_cb *callbacks)
{
	ar->hif.ops->set_callbacks(ar, callbacks);
}

static inline u16 ath10k_hif_get_free_queue_number(struct ath10k *ar,
						   u8 pipe_id)
{
	return ar->hif.ops->get_free_queue_number(ar, pipe_id);
}

static inline int ath10k_hif_power_up(struct ath10k *ar)
{
	return ar->hif.ops->power_up(ar);
}

static inline void ath10k_hif_power_down(struct ath10k *ar)
{
	ar->hif.ops->power_down(ar);
}

static inline int ath10k_hif_suspend(struct ath10k *ar)
{
	if (!ar->hif.ops->suspend)
		return -EOPNOTSUPP;

	return ar->hif.ops->suspend(ar);
}

static inline int ath10k_hif_resume(struct ath10k *ar)
{
	if (!ar->hif.ops->resume)
		return -EOPNOTSUPP;

	return ar->hif.ops->resume(ar);
}

static inline u32 ath10k_hif_read32(struct ath10k *ar, u32 address)
{
	if (!ar->hif.ops->read32) {
		ath10k_warn(ar, "hif read32 not supported\n");
		return 0xdeaddead;
	}

	return ar->hif.ops->read32(ar, address);
}

static inline void ath10k_hif_write32(struct ath10k *ar,
				      u32 address, u32 data)
{
	if (!ar->hif.ops->write32) {
		ath10k_warn(ar, "hif write32 not supported\n");
		return;
	}

	ar->hif.ops->write32(ar, address, data);
}

#endif /* _HIF_H_ */
