// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "struct.h"
#include "debugfs.h"

void print_nix_cn20k_sq_ctx(struct seq_file *m,
			    struct nix_cn20k_sq_ctx_s *sq_ctx)
{
	seq_printf(m, "W0: ena \t\t\t%d\nW0: qint_idx \t\t\t%d\n",
		   sq_ctx->ena, sq_ctx->qint_idx);
	seq_printf(m, "W0: substream \t\t\t0x%03x\nW0: sdp_mcast \t\t\t%d\n",
		   sq_ctx->substream, sq_ctx->sdp_mcast);
	seq_printf(m, "W0: cq \t\t\t\t%d\nW0: sqe_way_mask \t\t%d\n\n",
		   sq_ctx->cq, sq_ctx->sqe_way_mask);

	seq_printf(m, "W1: smq \t\t\t%d\nW1: cq_ena \t\t\t%d\nW1: xoff\t\t\t%d\n",
		   sq_ctx->smq, sq_ctx->cq_ena, sq_ctx->xoff);
	seq_printf(m, "W1: sso_ena \t\t\t%d\nW1: smq_rr_weight\t\t%d\n",
		   sq_ctx->sso_ena, sq_ctx->smq_rr_weight);
	seq_printf(m, "W1: default_chan\t\t%d\nW1: sqb_count\t\t\t%d\n\n",
		   sq_ctx->default_chan, sq_ctx->sqb_count);

	seq_printf(m, "W1: smq_rr_count_lb \t\t%d\n", sq_ctx->smq_rr_count_lb);
	seq_printf(m, "W2: smq_rr_count_ub \t\t%d\n", sq_ctx->smq_rr_count_ub);
	seq_printf(m, "W2: sqb_aura \t\t\t%d\nW2: sq_int \t\t\t%d\n",
		   sq_ctx->sqb_aura, sq_ctx->sq_int);
	seq_printf(m, "W2: sq_int_ena \t\t\t%d\nW2: sqe_stype \t\t\t%d\n",
		   sq_ctx->sq_int_ena, sq_ctx->sqe_stype);

	seq_printf(m, "W3: max_sqe_size\t\t%d\nW3: cq_limit\t\t\t%d\n",
		   sq_ctx->max_sqe_size, sq_ctx->cq_limit);
	seq_printf(m, "W3: lmt_dis \t\t\t%d\nW3: mnq_dis \t\t\t%d\n",
		   sq_ctx->lmt_dis, sq_ctx->mnq_dis);
	seq_printf(m, "W3: smq_next_sq\t\t\t%d\nW3: smq_lso_segnum\t\t%d\n",
		   sq_ctx->smq_next_sq, sq_ctx->smq_lso_segnum);
	seq_printf(m, "W3: tail_offset \t\t%d\nW3: smenq_offset\t\t%d\n",
		   sq_ctx->tail_offset, sq_ctx->smenq_offset);
	seq_printf(m, "W3: head_offset\t\t\t%d\nW3: smenq_next_sqb_vld\t\t%d\n\n",
		   sq_ctx->head_offset, sq_ctx->smenq_next_sqb_vld);

	seq_printf(m, "W3: smq_next_sq_vld\t\t%d\nW3: smq_pend\t\t\t%d\n",
		   sq_ctx->smq_next_sq_vld, sq_ctx->smq_pend);
	seq_printf(m, "W4: next_sqb \t\t\t%llx\n\n", sq_ctx->next_sqb);
	seq_printf(m, "W5: tail_sqb \t\t\t%llx\n\n", sq_ctx->tail_sqb);
	seq_printf(m, "W6: smenq_sqb \t\t\t%llx\n\n", sq_ctx->smenq_sqb);
	seq_printf(m, "W7: smenq_next_sqb \t\t%llx\n\n",
		   sq_ctx->smenq_next_sqb);

	seq_printf(m, "W8: head_sqb\t\t\t%llx\n\n", sq_ctx->head_sqb);

	seq_printf(m, "W9: vfi_lso_total\t\t%d\n", sq_ctx->vfi_lso_total);
	seq_printf(m, "W9: vfi_lso_sizem1\t\t%d\nW9: vfi_lso_sb\t\t\t%d\n",
		   sq_ctx->vfi_lso_sizem1, sq_ctx->vfi_lso_sb);
	seq_printf(m, "W9: vfi_lso_mps\t\t\t%d\nW9: vfi_lso_vlan0_ins_ena\t%d\n",
		   sq_ctx->vfi_lso_mps, sq_ctx->vfi_lso_vlan0_ins_ena);
	seq_printf(m, "W9: vfi_lso_vlan1_ins_ena\t%d\nW9: vfi_lso_vld \t\t%d\n\n",
		   sq_ctx->vfi_lso_vld, sq_ctx->vfi_lso_vlan1_ins_ena);

	seq_printf(m, "W10: scm_lso_rem \t\t%llu\n\n",
		   (u64)sq_ctx->scm_lso_rem);
	seq_printf(m, "W11: octs \t\t\t%llu\n\n", (u64)sq_ctx->octs);
	seq_printf(m, "W12: pkts \t\t\t%llu\n\n", (u64)sq_ctx->pkts);
	seq_printf(m, "W13: aged_drop_octs \t\t\t%llu\n\n",
		   (u64)sq_ctx->aged_drop_octs);
	seq_printf(m, "W13: aged_drop_pkts \t\t\t%llu\n\n",
		   (u64)sq_ctx->aged_drop_pkts);
	seq_printf(m, "W14: dropped_octs \t\t%llu\n\n",
		   (u64)sq_ctx->dropped_octs);
	seq_printf(m, "W15: dropped_pkts \t\t%llu\n\n",
		   (u64)sq_ctx->dropped_pkts);
}

void print_nix_cn20k_cq_ctx(struct seq_file *m,
			    struct nix_cn20k_aq_enq_rsp *rsp)
{
	struct nix_cn20k_cq_ctx_s *cq_ctx = &rsp->cq;

	seq_printf(m, "W0: base \t\t\t%llx\n\n", cq_ctx->base);

	seq_printf(m, "W1: wrptr \t\t\t%llx\n", (u64)cq_ctx->wrptr);
	seq_printf(m, "W1: avg_con \t\t\t%d\nW1: cint_idx \t\t\t%d\n",
		   cq_ctx->avg_con, cq_ctx->cint_idx);
	seq_printf(m, "W1: cq_err \t\t\t%d\nW1: qint_idx \t\t\t%d\n",
		   cq_ctx->cq_err, cq_ctx->qint_idx);
	seq_printf(m, "W1: bpid \t\t\t%d\nW1: bp_ena \t\t\t%d\n\n",
		   cq_ctx->bpid, cq_ctx->bp_ena);

	seq_printf(m, "W1: lbpid_high \t\t\t0x%03x\n", cq_ctx->lbpid_high);
	seq_printf(m, "W1: lbpid_med \t\t\t0x%03x\n", cq_ctx->lbpid_med);
	seq_printf(m, "W1: lbpid_low \t\t\t0x%03x\n", cq_ctx->lbpid_low);
	seq_printf(m, "(W1: lbpid) \t\t\t0x%03x\n",
		   cq_ctx->lbpid_high << 6 | cq_ctx->lbpid_med << 3 |
		   cq_ctx->lbpid_low);
	seq_printf(m, "W1: lbp_ena \t\t\t\t%d\n\n", cq_ctx->lbp_ena);

	seq_printf(m, "W2: update_time \t\t%d\nW2:avg_level \t\t\t%d\n",
		   cq_ctx->update_time, cq_ctx->avg_level);
	seq_printf(m, "W2: head \t\t\t%d\nW2:tail \t\t\t%d\n\n",
		   cq_ctx->head, cq_ctx->tail);

	seq_printf(m, "W3: cq_err_int_ena \t\t%d\nW3:cq_err_int \t\t\t%d\n",
		   cq_ctx->cq_err_int_ena, cq_ctx->cq_err_int);
	seq_printf(m, "W3: qsize \t\t\t%d\nW3:stashing \t\t\t%d\n",
		   cq_ctx->qsize, cq_ctx->stashing);

	seq_printf(m, "W3: caching \t\t\t%d\n", cq_ctx->caching);
	seq_printf(m, "W3: lbp_frac \t\t\t%d\n", cq_ctx->lbp_frac);
	seq_printf(m, "W3: stash_thresh \t\t\t%d\n",
		   cq_ctx->stash_thresh);

	seq_printf(m, "W3: msh_valid \t\t\t%d\nW3:msh_dst \t\t\t%d\n",
		   cq_ctx->msh_valid, cq_ctx->msh_dst);

	seq_printf(m, "W3: cpt_drop_err_en \t\t\t%d\n",
		   cq_ctx->cpt_drop_err_en);
	seq_printf(m, "W3: ena \t\t\t%d\n",
		   cq_ctx->ena);
	seq_printf(m, "W3: drop_ena \t\t\t%d\nW3: drop \t\t\t%d\n",
		   cq_ctx->drop_ena, cq_ctx->drop);
	seq_printf(m, "W3: bp \t\t\t\t%d\n\n", cq_ctx->bp);

	seq_printf(m, "W4: lbpid_ext \t\t\t\t%d\n\n", cq_ctx->lbpid_ext);
	seq_printf(m, "W4: bpid_ext \t\t\t\t%d\n\n", cq_ctx->bpid_ext);
}
