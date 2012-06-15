/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 * Copyright (c) 2011 Qualcomm Atheros, Inc.
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

#ifndef HIF_OPS_H
#define HIF_OPS_H

#include "hif.h"
#include "debug.h"

static inline int hif_read_write_sync(struct ath6kl *ar, u32 addr, u8 *buf,
				      u32 len, u32 request)
{
	ath6kl_dbg(ATH6KL_DBG_HIF,
		   "hif %s sync addr 0x%x buf 0x%p len %d request 0x%x\n",
		   (request & HIF_WRITE) ? "write" : "read",
		   addr, buf, len, request);

	return ar->hif_ops->read_write_sync(ar, addr, buf, len, request);
}

static inline int hif_write_async(struct ath6kl *ar, u32 address, u8 *buffer,
				  u32 length, u32 request,
				  struct htc_packet *packet)
{
	ath6kl_dbg(ATH6KL_DBG_HIF,
		   "hif write async addr 0x%x buf 0x%p len %d request 0x%x\n",
		   address, buffer, length, request);

	return ar->hif_ops->write_async(ar, address, buffer, length,
					request, packet);
}
static inline void ath6kl_hif_irq_enable(struct ath6kl *ar)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif irq enable\n");

	return ar->hif_ops->irq_enable(ar);
}

static inline void ath6kl_hif_irq_disable(struct ath6kl *ar)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif irq disable\n");

	return ar->hif_ops->irq_disable(ar);
}

static inline struct hif_scatter_req *hif_scatter_req_get(struct ath6kl *ar)
{
	return ar->hif_ops->scatter_req_get(ar);
}

static inline void hif_scatter_req_add(struct ath6kl *ar,
				       struct hif_scatter_req *s_req)
{
	return ar->hif_ops->scatter_req_add(ar, s_req);
}

static inline int ath6kl_hif_enable_scatter(struct ath6kl *ar)
{
	return ar->hif_ops->enable_scatter(ar);
}

static inline int ath6kl_hif_scat_req_rw(struct ath6kl *ar,
					 struct hif_scatter_req *scat_req)
{
	return ar->hif_ops->scat_req_rw(ar, scat_req);
}

static inline void ath6kl_hif_cleanup_scatter(struct ath6kl *ar)
{
	return ar->hif_ops->cleanup_scatter(ar);
}

static inline int ath6kl_hif_suspend(struct ath6kl *ar,
				     struct cfg80211_wowlan *wow)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif suspend\n");

	return ar->hif_ops->suspend(ar, wow);
}

/*
 * Read from the ATH6KL through its diagnostic window. No cooperation from
 * the Target is required for this.
 */
static inline int ath6kl_hif_diag_read32(struct ath6kl *ar, u32 address,
					 u32 *value)
{
	return ar->hif_ops->diag_read32(ar, address, value);
}

/*
 * Write to the ATH6KL through its diagnostic window. No cooperation from
 * the Target is required for this.
 */
static inline int ath6kl_hif_diag_write32(struct ath6kl *ar, u32 address,
					  __le32 value)
{
	return ar->hif_ops->diag_write32(ar, address, value);
}

static inline int ath6kl_hif_bmi_read(struct ath6kl *ar, u8 *buf, u32 len)
{
	return ar->hif_ops->bmi_read(ar, buf, len);
}

static inline int ath6kl_hif_bmi_write(struct ath6kl *ar, u8 *buf, u32 len)
{
	return ar->hif_ops->bmi_write(ar, buf, len);
}

static inline int ath6kl_hif_resume(struct ath6kl *ar)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif resume\n");

	return ar->hif_ops->resume(ar);
}

static inline int ath6kl_hif_power_on(struct ath6kl *ar)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif power on\n");

	return ar->hif_ops->power_on(ar);
}

static inline int ath6kl_hif_power_off(struct ath6kl *ar)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif power off\n");

	return ar->hif_ops->power_off(ar);
}

static inline void ath6kl_hif_stop(struct ath6kl *ar)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif stop\n");

	ar->hif_ops->stop(ar);
}

static inline int ath6kl_hif_pipe_send(struct ath6kl *ar,
				       u8 pipe, struct sk_buff *hdr_buf,
				       struct sk_buff *buf)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif pipe send\n");

	return ar->hif_ops->pipe_send(ar, pipe, hdr_buf, buf);
}

static inline void ath6kl_hif_pipe_get_default(struct ath6kl *ar,
					       u8 *ul_pipe, u8 *dl_pipe)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif pipe get default\n");

	ar->hif_ops->pipe_get_default(ar, ul_pipe, dl_pipe);
}

static inline int ath6kl_hif_pipe_map_service(struct ath6kl *ar,
					      u16 service_id, u8 *ul_pipe,
					      u8 *dl_pipe)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif pipe get default\n");

	return ar->hif_ops->pipe_map_service(ar, service_id, ul_pipe, dl_pipe);
}

static inline u16 ath6kl_hif_pipe_get_free_queue_number(struct ath6kl *ar,
							u8 pipe)
{
	ath6kl_dbg(ATH6KL_DBG_HIF, "hif pipe get free queue number\n");

	return ar->hif_ops->pipe_get_free_queue_number(ar, pipe);
}

#endif
