/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
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

#include "core.h"
#include "debug.h"

int ath6kl_printk(const char *level, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int rtn;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	rtn = printk("%sath6kl: %pV", level, &vaf);

	va_end(args);

	return rtn;
}

#ifdef CONFIG_ATH6KL_DEBUG
void ath6kl_dump_registers(struct ath6kl_device *dev,
			   struct ath6kl_irq_proc_registers *irq_proc_reg,
			   struct ath6kl_irq_enable_reg *irq_enable_reg)
{

	ath6kl_dbg(ATH6KL_DBG_ANY, ("<------- Register Table -------->\n"));

	if (irq_proc_reg != NULL) {
		ath6kl_dbg(ATH6KL_DBG_ANY,
			"Host Int status:           0x%x\n",
			irq_proc_reg->host_int_status);
		ath6kl_dbg(ATH6KL_DBG_ANY,
			   "CPU Int status:            0x%x\n",
			irq_proc_reg->cpu_int_status);
		ath6kl_dbg(ATH6KL_DBG_ANY,
			   "Error Int status:          0x%x\n",
			irq_proc_reg->error_int_status);
		ath6kl_dbg(ATH6KL_DBG_ANY,
			   "Counter Int status:        0x%x\n",
			irq_proc_reg->counter_int_status);
		ath6kl_dbg(ATH6KL_DBG_ANY,
			   "Mbox Frame:                0x%x\n",
			irq_proc_reg->mbox_frame);
		ath6kl_dbg(ATH6KL_DBG_ANY,
			   "Rx Lookahead Valid:        0x%x\n",
			irq_proc_reg->rx_lkahd_valid);
		ath6kl_dbg(ATH6KL_DBG_ANY,
			   "Rx Lookahead 0:            0x%x\n",
			irq_proc_reg->rx_lkahd[0]);
		ath6kl_dbg(ATH6KL_DBG_ANY,
			   "Rx Lookahead 1:            0x%x\n",
			irq_proc_reg->rx_lkahd[1]);

		if (dev->ar->mbox_info.gmbox_addr != 0) {
			/*
			 * If the target supports GMBOX hardware, dump some
			 * additional state.
			 */
			ath6kl_dbg(ATH6KL_DBG_ANY,
				"GMBOX Host Int status 2:   0x%x\n",
				irq_proc_reg->host_int_status2);
			ath6kl_dbg(ATH6KL_DBG_ANY,
				"GMBOX RX Avail:            0x%x\n",
				irq_proc_reg->gmbox_rx_avail);
			ath6kl_dbg(ATH6KL_DBG_ANY,
				"GMBOX lookahead alias 0:   0x%x\n",
				irq_proc_reg->rx_gmbox_lkahd_alias[0]);
			ath6kl_dbg(ATH6KL_DBG_ANY,
				"GMBOX lookahead alias 1:   0x%x\n",
				irq_proc_reg->rx_gmbox_lkahd_alias[1]);
		}

	}

	if (irq_enable_reg != NULL) {
		ath6kl_dbg(ATH6KL_DBG_ANY,
			"Int status Enable:         0x%x\n",
			irq_enable_reg->int_status_en);
		ath6kl_dbg(ATH6KL_DBG_ANY, "Counter Int status Enable: 0x%x\n",
			irq_enable_reg->cntr_int_status_en);
	}
	ath6kl_dbg(ATH6KL_DBG_ANY, "<------------------------------->\n");
}

static void dump_cred_dist(struct htc_endpoint_credit_dist *ep_dist)
{
	ath6kl_dbg(ATH6KL_DBG_ANY,
		   "--- endpoint: %d  svc_id: 0x%X ---\n",
		   ep_dist->endpoint, ep_dist->svc_id);
	ath6kl_dbg(ATH6KL_DBG_ANY, " dist_flags     : 0x%X\n",
		   ep_dist->dist_flags);
	ath6kl_dbg(ATH6KL_DBG_ANY, " cred_norm      : %d\n",
		   ep_dist->cred_norm);
	ath6kl_dbg(ATH6KL_DBG_ANY, " cred_min       : %d\n",
		   ep_dist->cred_min);
	ath6kl_dbg(ATH6KL_DBG_ANY, " credits        : %d\n",
		   ep_dist->credits);
	ath6kl_dbg(ATH6KL_DBG_ANY, " cred_assngd    : %d\n",
		   ep_dist->cred_assngd);
	ath6kl_dbg(ATH6KL_DBG_ANY, " seek_cred      : %d\n",
		   ep_dist->seek_cred);
	ath6kl_dbg(ATH6KL_DBG_ANY, " cred_sz        : %d\n",
		   ep_dist->cred_sz);
	ath6kl_dbg(ATH6KL_DBG_ANY, " cred_per_msg   : %d\n",
		   ep_dist->cred_per_msg);
	ath6kl_dbg(ATH6KL_DBG_ANY, " cred_to_dist   : %d\n",
		   ep_dist->cred_to_dist);
	ath6kl_dbg(ATH6KL_DBG_ANY, " txq_depth      : %d\n",
		   get_queue_depth(&((struct htc_endpoint *)
				     ep_dist->htc_rsvd)->txq));
	ath6kl_dbg(ATH6KL_DBG_ANY,
		   "----------------------------------\n");
}

void dump_cred_dist_stats(struct htc_target *target)
{
	struct htc_endpoint_credit_dist *ep_list;

	if (!AR_DBG_LVL_CHECK(ATH6KL_DBG_TRC))
		return;

	list_for_each_entry(ep_list, &target->cred_dist_list, list)
		dump_cred_dist(ep_list);

	ath6kl_dbg(ATH6KL_DBG_HTC_SEND, "ctxt:%p dist:%p\n",
		   target->cred_dist_cntxt, NULL);
	ath6kl_dbg(ATH6KL_DBG_TRC, "credit distribution, total : %d, free : %d\n",
		   target->cred_dist_cntxt->total_avail_credits,
		   target->cred_dist_cntxt->cur_free_credits);
}

#endif
