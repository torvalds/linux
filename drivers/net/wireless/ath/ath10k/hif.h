/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2015,2017 Qualcomm Atheros, Inc.
 */

#ifndef _HIF_H_
#define _HIF_H_

#include <linux/kernel.h>
#include "core.h"
#include "bmi.h"
#include "debug.h"

struct ath10k_hif_sg_item {
	u16 transfer_id;
	void *transfer_context; /* NULL = tx completion callback not called */
	void *vaddr; /* for debugging mostly */
	dma_addr_t paddr;
	u16 len;
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
	 * desired so, call power_down() and power_up()
	 */
	void (*stop)(struct ath10k *ar);

	int (*swap_mailbox)(struct ath10k *ar);

	int (*map_service_to_pipe)(struct ath10k *ar, u16 service_id,
				   u8 *ul_pipe, u8 *dl_pipe);

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

	u16 (*get_free_queue_number)(struct ath10k *ar, u8 pipe_id);

	u32 (*read32)(struct ath10k *ar, u32 address);

	void (*write32)(struct ath10k *ar, u32 address, u32 value);

	/* Power up the device and enter BMI transfer mode for FW download */
	int (*power_up)(struct ath10k *ar, enum ath10k_firmware_mode fw_mode);

	/* Power down the device and free up resources. stop() must be called
	 * before this if start() was called earlier
	 */
	void (*power_down)(struct ath10k *ar);

	int (*suspend)(struct ath10k *ar);
	int (*resume)(struct ath10k *ar);

	/* fetch calibration data from target eeprom */
	int (*fetch_cal_eeprom)(struct ath10k *ar, void **data,
				size_t *data_len);

	int (*get_target_info)(struct ath10k *ar,
			       struct bmi_target_info *target_info);
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

static inline int ath10k_hif_swap_mailbox(struct ath10k *ar)
{
	if (ar->hif.ops->swap_mailbox)
		return ar->hif.ops->swap_mailbox(ar);
	return 0;
}

static inline int ath10k_hif_map_service_to_pipe(struct ath10k *ar,
						 u16 service_id,
						 u8 *ul_pipe, u8 *dl_pipe)
{
	return ar->hif.ops->map_service_to_pipe(ar, service_id,
						ul_pipe, dl_pipe);
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

static inline u16 ath10k_hif_get_free_queue_number(struct ath10k *ar,
						   u8 pipe_id)
{
	return ar->hif.ops->get_free_queue_number(ar, pipe_id);
}

static inline int ath10k_hif_power_up(struct ath10k *ar,
				      enum ath10k_firmware_mode fw_mode)
{
	return ar->hif.ops->power_up(ar, fw_mode);
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

static inline int ath10k_hif_fetch_cal_eeprom(struct ath10k *ar,
					      void **data,
					      size_t *data_len)
{
	if (!ar->hif.ops->fetch_cal_eeprom)
		return -EOPNOTSUPP;

	return ar->hif.ops->fetch_cal_eeprom(ar, data, data_len);
}

static inline int ath10k_hif_get_target_info(struct ath10k *ar,
					     struct bmi_target_info *tgt_info)
{
	if (!ar->hif.ops->get_target_info)
		return -EOPNOTSUPP;

	return ar->hif.ops->get_target_info(ar, tgt_info);
}

#endif /* _HIF_H_ */
