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
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/bpf.h>

#include <asm/kprobes.h>
#include <asm/text-patching.h>

#include "bpf_jit.h"

static void bpf_jit_fill_ill_insns(void *area, unsigned int size)
{
	memset32(area, BREAKPOINT_INSTRUCTION, size / 4);
}

int bpf_jit_emit_exit_insn(u32 *image, struct codegen_context *ctx, int tmp_reg, long exit_addr)
{
	if (!exit_addr || is_offset_in_branch_range(exit_addr - (ctx->idx * 4))) {
		PPC_JMP(exit_addr);
	} else if (ctx->alt_exit_addr) {
		if (WARN_ON(!is_offset_in_branch_range((long)ctx->alt_exit_addr - (ctx->idx * 4))))
			return -1;
		PPC_JMP(ctx->alt_exit_addr);
	} else {
		ctx->alt_exit_addr = ctx->idx * 4;
		bpf_jit_build_epilogue(image, ctx);
	}

	return 0;
}

struct powerpc_jit_data {
	/* address of rw header */
	struct bpf_binary_header *hdr;
	/* address of ro final header */
	struct bpf_binary_header *fhdr;
	u32 *addrs;
	u8 *fimage;
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
	struct powerpc_jit_data *jit_data;
	struct codegen_context cgctx;
	int pass;
	int flen;
	struct bpf_binary_header *fhdr = NULL;
	struct bpf_binary_header *hdr = NULL;
	struct bpf_prog *org_fp = fp;
	struct bpf_prog *tmp_fp;
	bool bpf_blinded = false;
	bool extra_pass = false;
	u8 *fimage = NULL;
	u32 *fcode_base;
	u32 extable_len;
	u32 fixup_len;

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
		/*
		 * JIT compiled to a writable location (image/code_base) first.
		 * It is then moved to the readonly final location (fimage/fcode_base)
		 * using instruction patching.
		 */
		fimage = jit_data->fimage;
		fhdr = jit_data->fhdr;
		proglen = jit_data->proglen;
		hdr = jit_data->hdr;
		image = (void *)hdr + ((void *)fimage - (void *)fhdr);
		extra_pass = true;
		/* During extra pass, ensure index is reset before repopulating extable entries */
		cgctx.exentry_idx = 0;
		goto skip_init_ctx;
	}

	addrs = kcalloc(flen + 1, sizeof(*addrs), GFP_KERNEL);
	if (addrs == NULL) {
		fp = org_fp;
		goto out_addrs;
	}

	memset(&cgctx, 0, sizeof(struct codegen_context));
	bpf_jit_init_reg_mapping(&cgctx);

	/* Make sure that the stack is quadword aligned. */
	cgctx.stack_size = round_up(fp->aux->stack_depth, 16);

	/* Scouting faux-generate pass 0 */
	if (bpf_jit_build_body(fp, NULL, NULL, &cgctx, addrs, 0, false)) {
		/* We hit something illegal or unsupported. */
		fp = org_fp;
		goto out_addrs;
	}

	/*
	 * If we have seen a tail call, we need a second pass.
	 * This is because bpf_jit_emit_common_epilogue() is called
	 * from bpf_jit_emit_tail_call() with a not yet stable ctx->seen.
	 * We also need a second pass if we ended up with too large
	 * a program so as to ensure BPF_EXIT branches are in range.
	 */
	if (cgctx.seen & SEEN_TAILCALL || !is_offset_in_branch_range((long)cgctx.idx * 4)) {
		cgctx.idx = 0;
		if (bpf_jit_build_body(fp, NULL, NULL, &cgctx, addrs, 0, false)) {
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
	bpf_jit_build_prologue(NULL, &cgctx);
	addrs[fp->len] = cgctx.idx * 4;
	bpf_jit_build_epilogue(NULL, &cgctx);

	fixup_len = fp->aux->num_exentries * BPF_FIXUP_LEN * 4;
	extable_len = fp->aux->num_exentries * sizeof(struct exception_table_entry);

	proglen = cgctx.idx * 4;
	alloclen = proglen + FUNCTION_DESCR_SIZE + fixup_len + extable_len;

	fhdr = bpf_jit_binary_pack_alloc(alloclen, &fimage, 4, &hdr, &image,
					      bpf_jit_fill_ill_insns);
	if (!fhdr) {
		fp = org_fp;
		goto out_addrs;
	}

	if (extable_len)
		fp->aux->extable = (void *)fimage + FUNCTION_DESCR_SIZE + proglen + fixup_len;

skip_init_ctx:
	code_base = (u32 *)(image + FUNCTION_DESCR_SIZE);
	fcode_base = (u32 *)(fimage + FUNCTION_DESCR_SIZE);

	/* Code generation passes 1-2 */
	for (pass = 1; pass < 3; pass++) {
		/* Now build the prologue, body code & epilogue for real. */
		cgctx.idx = 0;
		cgctx.alt_exit_addr = 0;
		bpf_jit_build_prologue(code_base, &cgctx);
		if (bpf_jit_build_body(fp, code_base, fcode_base, &cgctx, addrs, pass,
				       extra_pass)) {
			bpf_arch_text_copy(&fhdr->size, &hdr->size, sizeof(hdr->size));
			bpf_jit_binary_pack_free(fhdr, hdr);
			fp = org_fp;
			goto out_addrs;
		}
		bpf_jit_build_epilogue(code_base, &cgctx);

		if (bpf_jit_enable > 1)
			pr_info("Pass %d: shrink = %d, seen = 0x%x\n", pass,
				proglen - (cgctx.idx * 4), cgctx.seen);
	}

	if (bpf_jit_enable > 1)
		/*
		 * Note that we output the base address of the code_base
		 * rather than image, since opcodes are in code_base.
		 */
		bpf_jit_dump(flen, proglen, pass, code_base);

#ifdef CONFIG_PPC64_ELF_ABI_V1
	/* Function descriptor nastiness: Address + TOC */
	((u64 *)image)[0] = (u64)fcode_base;
	((u64 *)image)[1] = local_paca->kernel_toc;
#endif

	fp->bpf_func = (void *)fimage;
	fp->jited = 1;
	fp->jited_len = proglen + FUNCTION_DESCR_SIZE;

	if (!fp->is_func || extra_pass) {
		if (bpf_jit_binary_pack_finalize(fhdr, hdr)) {
			fp = org_fp;
			goto out_addrs;
		}
		bpf_prog_fill_jited_linfo(fp, addrs);
out_addrs:
		kfree(addrs);
		kfree(jit_data);
		fp->aux->jit_data = NULL;
	} else {
		jit_data->addrs = addrs;
		jit_data->ctx = cgctx;
		jit_data->proglen = proglen;
		jit_data->fimage = fimage;
		jit_data->fhdr = fhdr;
		jit_data->hdr = hdr;
	}

out:
	if (bpf_blinded)
		bpf_jit_prog_release_other(fp, fp == org_fp ? tmp_fp : org_fp);

	return fp;
}

/*
 * The caller should check for (BPF_MODE(code) == BPF_PROBE_MEM) before calling
 * this function, as this only applies to BPF_PROBE_MEM, for now.
 */
int bpf_add_extable_entry(struct bpf_prog *fp, u32 *image, u32 *fimage, int pass,
			  struct codegen_context *ctx, int insn_idx, int jmp_off,
			  int dst_reg)
{
	off_t offset;
	unsigned long pc;
	struct exception_table_entry *ex, *ex_entry;
	u32 *fixup;

	/* Populate extable entries only in the last pass */
	if (pass != 2)
		return 0;

	if (!fp->aux->extable ||
	    WARN_ON_ONCE(ctx->exentry_idx >= fp->aux->num_exentries))
		return -EINVAL;

	/*
	 * Program is first written to image before copying to the
	 * final location (fimage). Accordingly, update in the image first.
	 * As all offsets used are relative, copying as is to the
	 * final location should be alright.
	 */
	pc = (unsigned long)&image[insn_idx];
	ex = (void *)fp->aux->extable - (void *)fimage + (void *)image;

	fixup = (void *)ex -
		(fp->aux->num_exentries * BPF_FIXUP_LEN * 4) +
		(ctx->exentry_idx * BPF_FIXUP_LEN * 4);

	fixup[0] = PPC_RAW_LI(dst_reg, 0);
	if (IS_ENABLED(CONFIG_PPC32))
		fixup[1] = PPC_RAW_LI(dst_reg - 1, 0); /* clear higher 32-bit register too */

	fixup[BPF_FIXUP_LEN - 1] =
		PPC_RAW_BRANCH((long)(pc + jmp_off) - (long)&fixup[BPF_FIXUP_LEN - 1]);

	ex_entry = &ex[ctx->exentry_idx];

	offset = pc - (long)&ex_entry->insn;
	if (WARN_ON_ONCE(offset >= 0 || offset < INT_MIN))
		return -ERANGE;
	ex_entry->insn = offset;

	offset = (long)fixup - (long)&ex_entry->fixup;
	if (WARN_ON_ONCE(offset >= 0 || offset < INT_MIN))
		return -ERANGE;
	ex_entry->fixup = offset;

	ctx->exentry_idx++;
	return 0;
}

void *bpf_arch_text_copy(void *dst, void *src, size_t len)
{
	int err;

	if (WARN_ON_ONCE(core_kernel_text((unsigned long)dst)))
		return ERR_PTR(-EINVAL);

	mutex_lock(&text_mutex);
	err = patch_instructions(dst, src, len, false);
	mutex_unlock(&text_mutex);

	return err ? ERR_PTR(err) : dst;
}

int bpf_arch_text_invalidate(void *dst, size_t len)
{
	u32 insn = BREAKPOINT_INSTRUCTION;
	int ret;

	if (WARN_ON_ONCE(core_kernel_text((unsigned long)dst)))
		return -EINVAL;

	mutex_lock(&text_mutex);
	ret = patch_instructions(dst, &insn, len, true);
	mutex_unlock(&text_mutex);

	return ret;
}

void bpf_jit_free(struct bpf_prog *fp)
{
	if (fp->jited) {
		struct powerpc_jit_data *jit_data = fp->aux->jit_data;
		struct bpf_binary_header *hdr;

		/*
		 * If we fail the final pass of JIT (from jit_subprogs),
		 * the program may not be finalized yet. Call finalize here
		 * before freeing it.
		 */
		if (jit_data) {
			bpf_jit_binary_pack_finalize(jit_data->fhdr, jit_data->hdr);
			kvfree(jit_data->addrs);
			kfree(jit_data);
		}
		hdr = bpf_jit_binary_pack_hdr(fp);
		bpf_jit_binary_pack_free(hdr, NULL);
		WARN_ON_ONCE(!bpf_prog_kallsyms_verify_off(fp));
	}

	bpf_prog_unlock_free(fp);
}

bool bpf_jit_supports_kfunc_call(void)
{
	return true;
}

bool bpf_jit_supports_far_kfunc_call(void)
{
	return IS_ENABLED(CONFIG_PPC64);
}
