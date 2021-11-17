// SPDX-License-Identifier: GPL-2.0-only
/*
 * eBPF JIT compiler
 *
 * Copyright 2016 Naveen N. Rao <naveen.n.rao@linux.vnet.ibm.com>
 *		  IBM Corporation
 *
 * Based on the powerpc classic BPF JIT compiler by Matt Evans
 */
#include <linux/moduleloader.h>
#include <asm/cacheflush.h>
#include <asm/asm-compat.h>
#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/if_vlan.h>
#include <asm/kprobes.h>
#include <linux/bpf.h>

#include "bpf_jit.h"

static void bpf_jit_fill_ill_insns(void *area, unsigned int size)
{
	memset32(area, BREAKPOINT_INSTRUCTION, size / 4);
}

/* Fix the branch target addresses for subprog calls */
static int bpf_jit_fixup_subprog_calls(struct bpf_prog *fp, u32 *image,
				       struct codegen_context *ctx, u32 *addrs)
{
	const struct bpf_insn *insn = fp->insnsi;
	bool func_addr_fixed;
	u64 func_addr;
	u32 tmp_idx;
	int i, ret;

	for (i = 0; i < fp->len; i++) {
		/*
		 * During the extra pass, only the branch target addresses for
		 * the subprog calls need to be fixed. All other instructions
		 * can left untouched.
		 *
		 * The JITed image length does not change because we already
		 * ensure that the JITed instruction sequence for these calls
		 * are of fixed length by padding them with NOPs.
		 */
		if (insn[i].code == (BPF_JMP | BPF_CALL) &&
		    insn[i].src_reg == BPF_PSEUDO_CALL) {
			ret = bpf_jit_get_func_addr(fp, &insn[i], true,
						    &func_addr,
						    &func_addr_fixed);
			if (ret < 0)
				return ret;

			/*
			 * Save ctx->idx as this would currently point to the
			 * end of the JITed image and set it to the offset of
			 * the instruction sequence corresponding to the
			 * subprog call temporarily.
			 */
			tmp_idx = ctx->idx;
			ctx->idx = addrs[i] / 4;
			bpf_jit_emit_func_call_rel(image, ctx, func_addr);

			/*
			 * Restore ctx->idx here. This is safe as the length
			 * of the JITed sequence remains unchanged.
			 */
			ctx->idx = tmp_idx;
		}
	}

	return 0;
}

struct powerpc64_jit_data {
	struct bpf_binary_header *header;
	u32 *addrs;
	u8 *image;
	u32 proglen;
	struct codegen_context ctx;
};

bool bpf_jit_needs_zext(void)
{
	return true;
}

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *fp)
{
	u32 proglen;
	u32 alloclen;
	u8 *image = NULL;
	u32 *code_base;
	u32 *addrs;
	struct powerpc64_jit_data *jit_data;
	struct codegen_context cgctx;
	int pass;
	int flen;
	struct bpf_binary_header *bpf_hdr;
	struct bpf_prog *org_fp = fp;
	struct bpf_prog *tmp_fp;
	bool bpf_blinded = false;
	bool extra_pass = false;

	if (!fp->jit_requested)
		return org_fp;

	tmp_fp = bpf_jit_blind_constants(org_fp);
	if (IS_ERR(tmp_fp))
		return org_fp;

	if (tmp_fp != org_fp) {
		bpf_blinded = true;
		fp = tmp_fp;
	}

	jit_data = fp->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc(sizeof(*jit_data), GFP_KERNEL);
		if (!jit_data) {
			fp = org_fp;
			goto out;
		}
		fp->aux->jit_data = jit_data;
	}

	flen = fp->len;
	addrs = jit_data->addrs;
	if (addrs) {
		cgctx = jit_data->ctx;
		image = jit_data->image;
		bpf_hdr = jit_data->header;
		proglen = jit_data->proglen;
		alloclen = proglen + FUNCTION_DESCR_SIZE;
		extra_pass = true;
		goto skip_init_ctx;
	}

	addrs = kcalloc(flen + 1, sizeof(*addrs), GFP_KERNEL);
	if (addrs == NULL) {
		fp = org_fp;
		goto out_addrs;
	}

	memset(&cgctx, 0, sizeof(struct codegen_context));
	memcpy(cgctx.b2p, b2p, sizeof(cgctx.b2p));

	/* Make sure that the stack is quadword aligned. */
	cgctx.stack_size = round_up(fp->aux->stack_depth, 16);

	/* Scouting faux-generate pass 0 */
	if (bpf_jit_build_body(fp, 0, &cgctx, addrs, false)) {
		/* We hit something illegal or unsupported. */
		fp = org_fp;
		goto out_addrs;
	}

	/*
	 * If we have seen a tail call, we need a second pass.
	 * This is because bpf_jit_emit_common_epilogue() is called
	 * from bpf_jit_emit_tail_call() with a not yet stable ctx->seen.
	 */
	if (cgctx.seen & SEEN_TAILCALL) {
		cgctx.idx = 0;
		if (bpf_jit_build_body(fp, 0, &cgctx, addrs, false)) {
			fp = org_fp;
			goto out_addrs;
		}
	}

	bpf_jit_realloc_regs(&cgctx);
	/*
	 * Pretend to build prologue, given the features we've seen.  This will
	 * update ctgtx.idx as it pretends to output instructions, then we can
	 * calculate total size from idx.
	 */
	bpf_jit_build_prologue(0, &cgctx);
	bpf_jit_build_epilogue(0, &cgctx);

	proglen = cgctx.idx * 4;
	alloclen = proglen + FUNCTION_DESCR_SIZE;

	bpf_hdr = bpf_jit_binary_alloc(alloclen, &image, 4, bpf_jit_fill_ill_insns);
	if (!bpf_hdr) {
		fp = org_fp;
		goto out_addrs;
	}

skip_init_ctx:
	code_base = (u32 *)(image + FUNCTION_DESCR_SIZE);

	if (extra_pass) {
		/*
		 * Do not touch the prologue and epilogue as they will remain
		 * unchanged. Only fix the branch target address for subprog
		 * calls in the body.
		 *
		 * This does not change the offsets and lengths of the subprog
		 * call instruction sequences and hence, the size of the JITed
		 * image as well.
		 */
		bpf_jit_fixup_subprog_calls(fp, code_base, &cgctx, addrs);

		/* There is no need to perform the usual passes. */
		goto skip_codegen_passes;
	}

	/* Code generation passes 1-2 */
	for (pass = 1; pass < 3; pass++) {
		/* Now build the prologue, body code & epilogue for real. */
		cgctx.idx = 0;
		bpf_jit_build_prologue(code_base, &cgctx);
		if (bpf_jit_build_body(fp, code_base, &cgctx, addrs, extra_pass)) {
			bpf_jit_binary_free(bpf_hdr);
			fp = org_fp;
			goto out_addrs;
		}
		bpf_jit_build_epilogue(code_base, &cgctx);

		if (bpf_jit_enable > 1)
			pr_info("Pass %d: shrink = %d, seen = 0x%x\n", pass,
				proglen - (cgctx.idx * 4), cgctx.seen);
	}

skip_codegen_passes:
	if (bpf_jit_enable > 1)
		/*
		 * Note that we output the base address of the code_base
		 * rather than image, since opcodes are in code_base.
		 */
		bpf_jit_dump(flen, proglen, pass, code_base);

#ifdef PPC64_ELF_ABI_v1
	/* Function descriptor nastiness: Address + TOC */
	((u64 *)image)[0] = (u64)code_base;
	((u64 *)image)[1] = local_paca->kernel_toc;
#endif

	fp->bpf_func = (void *)image;
	fp->jited = 1;
	fp->jited_len = alloclen;

	bpf_flush_icache(bpf_hdr, (u8 *)bpf_hdr + (bpf_hdr->pages * PAGE_SIZE));
	bpf_jit_binary_lock_ro(bpf_hdr);
	if (!fp->is_func || extra_pass) {
		bpf_prog_fill_jited_linfo(fp, addrs);
out_addrs:
		kfree(addrs);
		kfree(jit_data);
		fp->aux->jit_data = NULL;
	} else {
		jit_data->addrs = addrs;
		jit_data->ctx = cgctx;
		jit_data->proglen = proglen;
		jit_data->image = image;
		jit_data->header = bpf_hdr;
	}

out:
	if (bpf_blinded)
		bpf_jit_prog_release_other(fp, fp == org_fp ? tmp_fp : org_fp);

	return fp;
}
