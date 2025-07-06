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

/* These offsets are from bpf prog end and stay the same across progs */
static int bpf_jit_ool_stub, bpf_jit_long_branch_stub;

static void bpf_jit_fill_ill_insns(void *area, unsigned int size)
{
	memset32(area, BREAKPOINT_INSTRUCTION, size / 4);
}

void dummy_tramp(void);

asm (
"	.pushsection .text, \"ax\", @progbits	;"
"	.global dummy_tramp			;"
"	.type dummy_tramp, @function		;"
"dummy_tramp:					;"
#ifdef CONFIG_PPC_FTRACE_OUT_OF_LINE
"	blr					;"
#else
/* LR is always in r11, so we don't need a 'mflr r11' here */
"	mtctr	11				;"
"	mtlr	0				;"
"	bctr					;"
#endif
"	.size dummy_tramp, .-dummy_tramp	;"
"	.popsection				;"
);

void bpf_jit_build_fentry_stubs(u32 *image, struct codegen_context *ctx)
{
	int ool_stub_idx, long_branch_stub_idx;

	/*
	 * Out-of-line stub:
	 *	mflr	r0
	 *	[b|bl]	tramp
	 *	mtlr	r0 // only with CONFIG_PPC_FTRACE_OUT_OF_LINE
	 *	b	bpf_func + 4
	 */
	ool_stub_idx = ctx->idx;
	EMIT(PPC_RAW_MFLR(_R0));
	EMIT(PPC_RAW_NOP());
	if (IS_ENABLED(CONFIG_PPC_FTRACE_OUT_OF_LINE))
		EMIT(PPC_RAW_MTLR(_R0));
	WARN_ON_ONCE(!is_offset_in_branch_range(4 - (long)ctx->idx * 4));
	EMIT(PPC_RAW_BRANCH(4 - (long)ctx->idx * 4));

	/*
	 * Long branch stub:
	 *	.long	<dummy_tramp_addr>
	 *	mflr	r11
	 *	bcl	20,31,$+4
	 *	mflr	r12
	 *	ld	r12, -8-SZL(r12)
	 *	mtctr	r12
	 *	mtlr	r11 // needed to retain ftrace ABI
	 *	bctr
	 */
	if (image)
		*((unsigned long *)&image[ctx->idx]) = (unsigned long)dummy_tramp;
	ctx->idx += SZL / 4;
	long_branch_stub_idx = ctx->idx;
	EMIT(PPC_RAW_MFLR(_R11));
	EMIT(PPC_RAW_BCL4());
	EMIT(PPC_RAW_MFLR(_R12));
	EMIT(PPC_RAW_LL(_R12, _R12, -8-SZL));
	EMIT(PPC_RAW_MTCTR(_R12));
	EMIT(PPC_RAW_MTLR(_R11));
	EMIT(PPC_RAW_BCTR());

	if (!bpf_jit_ool_stub) {
		bpf_jit_ool_stub = (ctx->idx - ool_stub_idx) * 4;
		bpf_jit_long_branch_stub = (ctx->idx - long_branch_stub_idx) * 4;
	}
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
	fp->jited_len = cgctx.idx * 4 + FUNCTION_DESCR_SIZE;

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

void *arch_alloc_bpf_trampoline(unsigned int size)
{
	return bpf_prog_pack_alloc(size, bpf_jit_fill_ill_insns);
}

void arch_free_bpf_trampoline(void *image, unsigned int size)
{
	bpf_prog_pack_free(image, size);
}

int arch_protect_bpf_trampoline(void *image, unsigned int size)
{
	return 0;
}

static int invoke_bpf_prog(u32 *image, u32 *ro_image, struct codegen_context *ctx,
			   struct bpf_tramp_link *l, int regs_off, int retval_off,
			   int run_ctx_off, bool save_ret)
{
	struct bpf_prog *p = l->link.prog;
	ppc_inst_t branch_insn;
	u32 jmp_idx;
	int ret = 0;

	/* Save cookie */
	if (IS_ENABLED(CONFIG_PPC64)) {
		PPC_LI64(_R3, l->cookie);
		EMIT(PPC_RAW_STD(_R3, _R1, run_ctx_off + offsetof(struct bpf_tramp_run_ctx,
				 bpf_cookie)));
	} else {
		PPC_LI32(_R3, l->cookie >> 32);
		PPC_LI32(_R4, l->cookie);
		EMIT(PPC_RAW_STW(_R3, _R1,
				 run_ctx_off + offsetof(struct bpf_tramp_run_ctx, bpf_cookie)));
		EMIT(PPC_RAW_STW(_R4, _R1,
				 run_ctx_off + offsetof(struct bpf_tramp_run_ctx, bpf_cookie) + 4));
	}

	/* __bpf_prog_enter(p, &bpf_tramp_run_ctx) */
	PPC_LI_ADDR(_R3, p);
	EMIT(PPC_RAW_MR(_R25, _R3));
	EMIT(PPC_RAW_ADDI(_R4, _R1, run_ctx_off));
	ret = bpf_jit_emit_func_call_rel(image, ro_image, ctx,
					 (unsigned long)bpf_trampoline_enter(p));
	if (ret)
		return ret;

	/* Remember prog start time returned by __bpf_prog_enter */
	EMIT(PPC_RAW_MR(_R26, _R3));

	/*
	 * if (__bpf_prog_enter(p) == 0)
	 *	goto skip_exec_of_prog;
	 *
	 * Emit a nop to be later patched with conditional branch, once offset is known
	 */
	EMIT(PPC_RAW_CMPLI(_R3, 0));
	jmp_idx = ctx->idx;
	EMIT(PPC_RAW_NOP());

	/* p->bpf_func(ctx) */
	EMIT(PPC_RAW_ADDI(_R3, _R1, regs_off));
	if (!p->jited)
		PPC_LI_ADDR(_R4, (unsigned long)p->insnsi);
	/* Account for max possible instructions during dummy pass for size calculation */
	if (image && !create_branch(&branch_insn, (u32 *)&ro_image[ctx->idx],
				    (unsigned long)p->bpf_func,
				    BRANCH_SET_LINK)) {
		image[ctx->idx] = ppc_inst_val(branch_insn);
		ctx->idx++;
	} else {
		EMIT(PPC_RAW_LL(_R12, _R25, offsetof(struct bpf_prog, bpf_func)));
		EMIT(PPC_RAW_MTCTR(_R12));
		EMIT(PPC_RAW_BCTRL());
	}

	if (save_ret)
		EMIT(PPC_RAW_STL(_R3, _R1, retval_off));

	/* Fix up branch */
	if (image) {
		if (create_cond_branch(&branch_insn, &image[jmp_idx],
				       (unsigned long)&image[ctx->idx], COND_EQ << 16))
			return -EINVAL;
		image[jmp_idx] = ppc_inst_val(branch_insn);
	}

	/* __bpf_prog_exit(p, start_time, &bpf_tramp_run_ctx) */
	EMIT(PPC_RAW_MR(_R3, _R25));
	EMIT(PPC_RAW_MR(_R4, _R26));
	EMIT(PPC_RAW_ADDI(_R5, _R1, run_ctx_off));
	ret = bpf_jit_emit_func_call_rel(image, ro_image, ctx,
					 (unsigned long)bpf_trampoline_exit(p));

	return ret;
}

static int invoke_bpf_mod_ret(u32 *image, u32 *ro_image, struct codegen_context *ctx,
			      struct bpf_tramp_links *tl, int regs_off, int retval_off,
			      int run_ctx_off, u32 *branches)
{
	int i;

	/*
	 * The first fmod_ret program will receive a garbage return value.
	 * Set this to 0 to avoid confusing the program.
	 */
	EMIT(PPC_RAW_LI(_R3, 0));
	EMIT(PPC_RAW_STL(_R3, _R1, retval_off));
	for (i = 0; i < tl->nr_links; i++) {
		if (invoke_bpf_prog(image, ro_image, ctx, tl->links[i], regs_off, retval_off,
				    run_ctx_off, true))
			return -EINVAL;

		/*
		 * mod_ret prog stored return value after prog ctx. Emit:
		 * if (*(u64 *)(ret_val) !=  0)
		 *	goto do_fexit;
		 */
		EMIT(PPC_RAW_LL(_R3, _R1, retval_off));
		EMIT(PPC_RAW_CMPLI(_R3, 0));

		/*
		 * Save the location of the branch and generate a nop, which is
		 * replaced with a conditional jump once do_fexit (i.e. the
		 * start of the fexit invocation) is finalized.
		 */
		branches[i] = ctx->idx;
		EMIT(PPC_RAW_NOP());
	}

	return 0;
}

static void bpf_trampoline_setup_tail_call_cnt(u32 *image, struct codegen_context *ctx,
					       int func_frame_offset, int r4_off)
{
	if (IS_ENABLED(CONFIG_PPC64)) {
		/* See bpf_jit_stack_tailcallcnt() */
		int tailcallcnt_offset = 6 * 8;

		EMIT(PPC_RAW_LL(_R3, _R1, func_frame_offset - tailcallcnt_offset));
		EMIT(PPC_RAW_STL(_R3, _R1, -tailcallcnt_offset));
	} else {
		/* See bpf_jit_stack_offsetof() and BPF_PPC_TC */
		EMIT(PPC_RAW_LL(_R4, _R1, r4_off));
	}
}

static void bpf_trampoline_restore_tail_call_cnt(u32 *image, struct codegen_context *ctx,
						 int func_frame_offset, int r4_off)
{
	if (IS_ENABLED(CONFIG_PPC64)) {
		/* See bpf_jit_stack_tailcallcnt() */
		int tailcallcnt_offset = 6 * 8;

		EMIT(PPC_RAW_LL(_R3, _R1, -tailcallcnt_offset));
		EMIT(PPC_RAW_STL(_R3, _R1, func_frame_offset - tailcallcnt_offset));
	} else {
		/* See bpf_jit_stack_offsetof() and BPF_PPC_TC */
		EMIT(PPC_RAW_STL(_R4, _R1, r4_off));
	}
}

static void bpf_trampoline_save_args(u32 *image, struct codegen_context *ctx, int func_frame_offset,
				     int nr_regs, int regs_off)
{
	int param_save_area_offset;

	param_save_area_offset = func_frame_offset; /* the two frames we alloted */
	param_save_area_offset += STACK_FRAME_MIN_SIZE; /* param save area is past frame header */

	for (int i = 0; i < nr_regs; i++) {
		if (i < 8) {
			EMIT(PPC_RAW_STL(_R3 + i, _R1, regs_off + i * SZL));
		} else {
			EMIT(PPC_RAW_LL(_R3, _R1, param_save_area_offset + i * SZL));
			EMIT(PPC_RAW_STL(_R3, _R1, regs_off + i * SZL));
		}
	}
}

/* Used when restoring just the register parameters when returning back */
static void bpf_trampoline_restore_args_regs(u32 *image, struct codegen_context *ctx,
					     int nr_regs, int regs_off)
{
	for (int i = 0; i < nr_regs && i < 8; i++)
		EMIT(PPC_RAW_LL(_R3 + i, _R1, regs_off + i * SZL));
}

/* Used when we call into the traced function. Replicate parameter save area */
static void bpf_trampoline_restore_args_stack(u32 *image, struct codegen_context *ctx,
					      int func_frame_offset, int nr_regs, int regs_off)
{
	int param_save_area_offset;

	param_save_area_offset = func_frame_offset; /* the two frames we alloted */
	param_save_area_offset += STACK_FRAME_MIN_SIZE; /* param save area is past frame header */

	for (int i = 8; i < nr_regs; i++) {
		EMIT(PPC_RAW_LL(_R3, _R1, param_save_area_offset + i * SZL));
		EMIT(PPC_RAW_STL(_R3, _R1, STACK_FRAME_MIN_SIZE + i * SZL));
	}
	bpf_trampoline_restore_args_regs(image, ctx, nr_regs, regs_off);
}

static int __arch_prepare_bpf_trampoline(struct bpf_tramp_image *im, void *rw_image,
					 void *rw_image_end, void *ro_image,
					 const struct btf_func_model *m, u32 flags,
					 struct bpf_tramp_links *tlinks,
					 void *func_addr)
{
	int regs_off, nregs_off, ip_off, run_ctx_off, retval_off, nvr_off, alt_lr_off, r4_off = 0;
	int i, ret, nr_regs, bpf_frame_size = 0, bpf_dummy_frame_size = 0, func_frame_offset;
	struct bpf_tramp_links *fmod_ret = &tlinks[BPF_TRAMP_MODIFY_RETURN];
	struct bpf_tramp_links *fentry = &tlinks[BPF_TRAMP_FENTRY];
	struct bpf_tramp_links *fexit = &tlinks[BPF_TRAMP_FEXIT];
	struct codegen_context codegen_ctx, *ctx;
	u32 *image = (u32 *)rw_image;
	ppc_inst_t branch_insn;
	u32 *branches = NULL;
	bool save_ret;

	if (IS_ENABLED(CONFIG_PPC32))
		return -EOPNOTSUPP;

	nr_regs = m->nr_args;
	/* Extra registers for struct arguments */
	for (i = 0; i < m->nr_args; i++)
		if (m->arg_size[i] > SZL)
			nr_regs += round_up(m->arg_size[i], SZL) / SZL - 1;

	if (nr_regs > MAX_BPF_FUNC_ARGS)
		return -EOPNOTSUPP;

	ctx = &codegen_ctx;
	memset(ctx, 0, sizeof(*ctx));

	/*
	 * Generated stack layout:
	 *
	 * func prev back chain         [ back chain        ]
	 *                              [                   ]
	 * bpf prog redzone/tailcallcnt [ ...               ] 64 bytes (64-bit powerpc)
	 *                              [                   ] --
	 * LR save area                 [ r0 save (64-bit)  ]   | header
	 *                              [ r0 save (32-bit)  ]   |
	 * dummy frame for unwind       [ back chain 1      ] --
	 *                              [ padding           ] align stack frame
	 *       r4_off                 [ r4 (tailcallcnt)  ] optional - 32-bit powerpc
	 *       alt_lr_off             [ real lr (ool stub)] optional - actual lr
	 *                              [ r26               ]
	 *       nvr_off                [ r25               ] nvr save area
	 *       retval_off             [ return value      ]
	 *                              [ reg argN          ]
	 *                              [ ...               ]
	 *       regs_off               [ reg_arg1          ] prog ctx context
	 *       nregs_off              [ args count        ]
	 *       ip_off                 [ traced function   ]
	 *                              [ ...               ]
	 *       run_ctx_off            [ bpf_tramp_run_ctx ]
	 *                              [ reg argN          ]
	 *                              [ ...               ]
	 *       param_save_area        [ reg_arg1          ] min 8 doublewords, per ABI
	 *                              [ TOC save (64-bit) ] --
	 *                              [ LR save (64-bit)  ]   | header
	 *                              [ LR save (32-bit)  ]   |
	 * bpf trampoline frame	        [ back chain 2      ] --
	 *
	 */

	/* Minimum stack frame header */
	bpf_frame_size = STACK_FRAME_MIN_SIZE;

	/*
	 * Room for parameter save area.
	 *
	 * As per the ABI, this is required if we call into the traced
	 * function (BPF_TRAMP_F_CALL_ORIG):
	 * - if the function takes more than 8 arguments for the rest to spill onto the stack
	 * - or, if the function has variadic arguments
	 * - or, if this functions's prototype was not available to the caller
	 *
	 * Reserve space for at least 8 registers for now. This can be optimized later.
	 */
	bpf_frame_size += (nr_regs > 8 ? nr_regs : 8) * SZL;

	/* Room for struct bpf_tramp_run_ctx */
	run_ctx_off = bpf_frame_size;
	bpf_frame_size += round_up(sizeof(struct bpf_tramp_run_ctx), SZL);

	/* Room for IP address argument */
	ip_off = bpf_frame_size;
	if (flags & BPF_TRAMP_F_IP_ARG)
		bpf_frame_size += SZL;

	/* Room for args count */
	nregs_off = bpf_frame_size;
	bpf_frame_size += SZL;

	/* Room for args */
	regs_off = bpf_frame_size;
	bpf_frame_size += nr_regs * SZL;

	/* Room for return value of func_addr or fentry prog */
	retval_off = bpf_frame_size;
	save_ret = flags & (BPF_TRAMP_F_CALL_ORIG | BPF_TRAMP_F_RET_FENTRY_RET);
	if (save_ret)
		bpf_frame_size += SZL;

	/* Room for nvr save area */
	nvr_off = bpf_frame_size;
	bpf_frame_size += 2 * SZL;

	/* Optional save area for actual LR in case of ool ftrace */
	if (IS_ENABLED(CONFIG_PPC_FTRACE_OUT_OF_LINE)) {
		alt_lr_off = bpf_frame_size;
		bpf_frame_size += SZL;
	}

	if (IS_ENABLED(CONFIG_PPC32)) {
		if (nr_regs < 2) {
			r4_off = bpf_frame_size;
			bpf_frame_size += SZL;
		} else {
			r4_off = regs_off + SZL;
		}
	}

	/* Padding to align stack frame, if any */
	bpf_frame_size = round_up(bpf_frame_size, SZL * 2);

	/* Dummy frame size for proper unwind - includes 64-bytes red zone for 64-bit powerpc */
	bpf_dummy_frame_size = STACK_FRAME_MIN_SIZE + 64;

	/* Offset to the traced function's stack frame */
	func_frame_offset = bpf_dummy_frame_size + bpf_frame_size;

	/* Create dummy frame for unwind, store original return value */
	EMIT(PPC_RAW_STL(_R0, _R1, PPC_LR_STKOFF));
	/* Protect red zone where tail call count goes */
	EMIT(PPC_RAW_STLU(_R1, _R1, -bpf_dummy_frame_size));

	/* Create our stack frame */
	EMIT(PPC_RAW_STLU(_R1, _R1, -bpf_frame_size));

	/* 64-bit: Save TOC and load kernel TOC */
	if (IS_ENABLED(CONFIG_PPC64_ELF_ABI_V2) && !IS_ENABLED(CONFIG_PPC_KERNEL_PCREL)) {
		EMIT(PPC_RAW_STD(_R2, _R1, 24));
		PPC64_LOAD_PACA();
	}

	/* 32-bit: save tail call count in r4 */
	if (IS_ENABLED(CONFIG_PPC32) && nr_regs < 2)
		EMIT(PPC_RAW_STL(_R4, _R1, r4_off));

	bpf_trampoline_save_args(image, ctx, func_frame_offset, nr_regs, regs_off);

	/* Save our return address */
	EMIT(PPC_RAW_MFLR(_R3));
	if (IS_ENABLED(CONFIG_PPC_FTRACE_OUT_OF_LINE))
		EMIT(PPC_RAW_STL(_R3, _R1, alt_lr_off));
	else
		EMIT(PPC_RAW_STL(_R3, _R1, bpf_frame_size + PPC_LR_STKOFF));

	/*
	 * Save ip address of the traced function.
	 * We could recover this from LR, but we will need to address for OOL trampoline,
	 * and optional GEP area.
	 */
	if (IS_ENABLED(CONFIG_PPC_FTRACE_OUT_OF_LINE) || flags & BPF_TRAMP_F_IP_ARG) {
		EMIT(PPC_RAW_LWZ(_R4, _R3, 4));
		EMIT(PPC_RAW_SLWI(_R4, _R4, 6));
		EMIT(PPC_RAW_SRAWI(_R4, _R4, 6));
		EMIT(PPC_RAW_ADD(_R3, _R3, _R4));
		EMIT(PPC_RAW_ADDI(_R3, _R3, 4));
	}

	if (flags & BPF_TRAMP_F_IP_ARG)
		EMIT(PPC_RAW_STL(_R3, _R1, ip_off));

	if (IS_ENABLED(CONFIG_PPC_FTRACE_OUT_OF_LINE))
		/* Fake our LR for unwind */
		EMIT(PPC_RAW_STL(_R3, _R1, bpf_frame_size + PPC_LR_STKOFF));

	/* Save function arg count -- see bpf_get_func_arg_cnt() */
	EMIT(PPC_RAW_LI(_R3, nr_regs));
	EMIT(PPC_RAW_STL(_R3, _R1, nregs_off));

	/* Save nv regs */
	EMIT(PPC_RAW_STL(_R25, _R1, nvr_off));
	EMIT(PPC_RAW_STL(_R26, _R1, nvr_off + SZL));

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		PPC_LI_ADDR(_R3, (unsigned long)im);
		ret = bpf_jit_emit_func_call_rel(image, ro_image, ctx,
						 (unsigned long)__bpf_tramp_enter);
		if (ret)
			return ret;
	}

	for (i = 0; i < fentry->nr_links; i++)
		if (invoke_bpf_prog(image, ro_image, ctx, fentry->links[i], regs_off, retval_off,
				    run_ctx_off, flags & BPF_TRAMP_F_RET_FENTRY_RET))
			return -EINVAL;

	if (fmod_ret->nr_links) {
		branches = kcalloc(fmod_ret->nr_links, sizeof(u32), GFP_KERNEL);
		if (!branches)
			return -ENOMEM;

		if (invoke_bpf_mod_ret(image, ro_image, ctx, fmod_ret, regs_off, retval_off,
				       run_ctx_off, branches)) {
			ret = -EINVAL;
			goto cleanup;
		}
	}

	/* Call the traced function */
	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		/*
		 * The address in LR save area points to the correct point in the original function
		 * with both PPC_FTRACE_OUT_OF_LINE as well as with traditional ftrace instruction
		 * sequence
		 */
		EMIT(PPC_RAW_LL(_R3, _R1, bpf_frame_size + PPC_LR_STKOFF));
		EMIT(PPC_RAW_MTCTR(_R3));

		/* Replicate tail_call_cnt before calling the original BPF prog */
		if (flags & BPF_TRAMP_F_TAIL_CALL_CTX)
			bpf_trampoline_setup_tail_call_cnt(image, ctx, func_frame_offset, r4_off);

		/* Restore args */
		bpf_trampoline_restore_args_stack(image, ctx, func_frame_offset, nr_regs, regs_off);

		/* Restore TOC for 64-bit */
		if (IS_ENABLED(CONFIG_PPC64_ELF_ABI_V2) && !IS_ENABLED(CONFIG_PPC_KERNEL_PCREL))
			EMIT(PPC_RAW_LD(_R2, _R1, 24));
		EMIT(PPC_RAW_BCTRL());
		if (IS_ENABLED(CONFIG_PPC64_ELF_ABI_V2) && !IS_ENABLED(CONFIG_PPC_KERNEL_PCREL))
			PPC64_LOAD_PACA();

		/* Store return value for bpf prog to access */
		EMIT(PPC_RAW_STL(_R3, _R1, retval_off));

		/* Restore updated tail_call_cnt */
		if (flags & BPF_TRAMP_F_TAIL_CALL_CTX)
			bpf_trampoline_restore_tail_call_cnt(image, ctx, func_frame_offset, r4_off);

		/* Reserve space to patch branch instruction to skip fexit progs */
		if (ro_image) /* image is NULL for dummy pass */
			im->ip_after_call = &((u32 *)ro_image)[ctx->idx];
		EMIT(PPC_RAW_NOP());
	}

	/* Update branches saved in invoke_bpf_mod_ret with address of do_fexit */
	for (i = 0; i < fmod_ret->nr_links && image; i++) {
		if (create_cond_branch(&branch_insn, &image[branches[i]],
				       (unsigned long)&image[ctx->idx], COND_NE << 16)) {
			ret = -EINVAL;
			goto cleanup;
		}

		image[branches[i]] = ppc_inst_val(branch_insn);
	}

	for (i = 0; i < fexit->nr_links; i++)
		if (invoke_bpf_prog(image, ro_image, ctx, fexit->links[i], regs_off, retval_off,
				    run_ctx_off, false)) {
			ret = -EINVAL;
			goto cleanup;
		}

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		if (ro_image) /* image is NULL for dummy pass */
			im->ip_epilogue = &((u32 *)ro_image)[ctx->idx];
		PPC_LI_ADDR(_R3, im);
		ret = bpf_jit_emit_func_call_rel(image, ro_image, ctx,
						 (unsigned long)__bpf_tramp_exit);
		if (ret)
			goto cleanup;
	}

	if (flags & BPF_TRAMP_F_RESTORE_REGS)
		bpf_trampoline_restore_args_regs(image, ctx, nr_regs, regs_off);

	/* Restore return value of func_addr or fentry prog */
	if (save_ret)
		EMIT(PPC_RAW_LL(_R3, _R1, retval_off));

	/* Restore nv regs */
	EMIT(PPC_RAW_LL(_R26, _R1, nvr_off + SZL));
	EMIT(PPC_RAW_LL(_R25, _R1, nvr_off));

	/* Epilogue */
	if (IS_ENABLED(CONFIG_PPC64_ELF_ABI_V2) && !IS_ENABLED(CONFIG_PPC_KERNEL_PCREL))
		EMIT(PPC_RAW_LD(_R2, _R1, 24));
	if (flags & BPF_TRAMP_F_SKIP_FRAME) {
		/* Skip the traced function and return to parent */
		EMIT(PPC_RAW_ADDI(_R1, _R1, func_frame_offset));
		EMIT(PPC_RAW_LL(_R0, _R1, PPC_LR_STKOFF));
		EMIT(PPC_RAW_MTLR(_R0));
		EMIT(PPC_RAW_BLR());
	} else {
		if (IS_ENABLED(CONFIG_PPC_FTRACE_OUT_OF_LINE)) {
			EMIT(PPC_RAW_LL(_R0, _R1, alt_lr_off));
			EMIT(PPC_RAW_MTLR(_R0));
			EMIT(PPC_RAW_ADDI(_R1, _R1, func_frame_offset));
			EMIT(PPC_RAW_LL(_R0, _R1, PPC_LR_STKOFF));
			EMIT(PPC_RAW_BLR());
		} else {
			EMIT(PPC_RAW_LL(_R0, _R1, bpf_frame_size + PPC_LR_STKOFF));
			EMIT(PPC_RAW_MTCTR(_R0));
			EMIT(PPC_RAW_ADDI(_R1, _R1, func_frame_offset));
			EMIT(PPC_RAW_LL(_R0, _R1, PPC_LR_STKOFF));
			EMIT(PPC_RAW_MTLR(_R0));
			EMIT(PPC_RAW_BCTR());
		}
	}

	/* Make sure the trampoline generation logic doesn't overflow */
	if (image && WARN_ON_ONCE(&image[ctx->idx] > (u32 *)rw_image_end - BPF_INSN_SAFETY)) {
		ret = -EFAULT;
		goto cleanup;
	}
	ret = ctx->idx * 4 + BPF_INSN_SAFETY * 4;

cleanup:
	kfree(branches);
	return ret;
}

int arch_bpf_trampoline_size(const struct btf_func_model *m, u32 flags,
			     struct bpf_tramp_links *tlinks, void *func_addr)
{
	struct bpf_tramp_image im;
	int ret;

	ret = __arch_prepare_bpf_trampoline(&im, NULL, NULL, NULL, m, flags, tlinks, func_addr);
	return ret;
}

int arch_prepare_bpf_trampoline(struct bpf_tramp_image *im, void *image, void *image_end,
				const struct btf_func_model *m, u32 flags,
				struct bpf_tramp_links *tlinks,
				void *func_addr)
{
	u32 size = image_end - image;
	void *rw_image, *tmp;
	int ret;

	/*
	 * rw_image doesn't need to be in module memory range, so we can
	 * use kvmalloc.
	 */
	rw_image = kvmalloc(size, GFP_KERNEL);
	if (!rw_image)
		return -ENOMEM;

	ret = __arch_prepare_bpf_trampoline(im, rw_image, rw_image + size, image, m,
					    flags, tlinks, func_addr);
	if (ret < 0)
		goto out;

	if (bpf_jit_enable > 1)
		bpf_jit_dump(1, ret - BPF_INSN_SAFETY * 4, 1, rw_image);

	tmp = bpf_arch_text_copy(image, rw_image, size);
	if (IS_ERR(tmp))
		ret = PTR_ERR(tmp);

out:
	kvfree(rw_image);
	return ret;
}

static int bpf_modify_inst(void *ip, ppc_inst_t old_inst, ppc_inst_t new_inst)
{
	ppc_inst_t org_inst;

	if (copy_inst_from_kernel_nofault(&org_inst, ip)) {
		pr_err("0x%lx: fetching instruction failed\n", (unsigned long)ip);
		return -EFAULT;
	}

	if (!ppc_inst_equal(org_inst, old_inst)) {
		pr_err("0x%lx: expected (%08lx) != found (%08lx)\n",
		       (unsigned long)ip, ppc_inst_as_ulong(old_inst), ppc_inst_as_ulong(org_inst));
		return -EINVAL;
	}

	if (ppc_inst_equal(old_inst, new_inst))
		return 0;

	return patch_instruction(ip, new_inst);
}

static void do_isync(void *info __maybe_unused)
{
	isync();
}

/*
 * A 3-step process for bpf prog entry:
 * 1. At bpf prog entry, a single nop/b:
 * bpf_func:
 *	[nop|b]	ool_stub
 * 2. Out-of-line stub:
 * ool_stub:
 *	mflr	r0
 *	[b|bl]	<bpf_prog>/<long_branch_stub>
 *	mtlr	r0 // CONFIG_PPC_FTRACE_OUT_OF_LINE only
 *	b	bpf_func + 4
 * 3. Long branch stub:
 * long_branch_stub:
 *	.long	<branch_addr>/<dummy_tramp>
 *	mflr	r11
 *	bcl	20,31,$+4
 *	mflr	r12
 *	ld	r12, -16(r12)
 *	mtctr	r12
 *	mtlr	r11 // needed to retain ftrace ABI
 *	bctr
 *
 * dummy_tramp is used to reduce synchronization requirements.
 *
 * When attaching a bpf trampoline to a bpf prog, we do not need any
 * synchronization here since we always have a valid branch target regardless
 * of the order in which the above stores are seen. dummy_tramp ensures that
 * the long_branch stub goes to a valid destination on other cpus, even when
 * the branch to the long_branch stub is seen before the updated trampoline
 * address.
 *
 * However, when detaching a bpf trampoline from a bpf prog, or if changing
 * the bpf trampoline address, we need synchronization to ensure that other
 * cpus can no longer branch into the older trampoline so that it can be
 * safely freed. bpf_tramp_image_put() uses rcu_tasks to ensure all cpus
 * make forward progress, but we still need to ensure that other cpus
 * execute isync (or some CSI) so that they don't go back into the
 * trampoline again.
 */
int bpf_arch_text_poke(void *ip, enum bpf_text_poke_type poke_type,
		       void *old_addr, void *new_addr)
{
	unsigned long bpf_func, bpf_func_end, size, offset;
	ppc_inst_t old_inst, new_inst;
	int ret = 0, branch_flags;
	char name[KSYM_NAME_LEN];

	if (IS_ENABLED(CONFIG_PPC32))
		return -EOPNOTSUPP;

	bpf_func = (unsigned long)ip;
	branch_flags = poke_type == BPF_MOD_CALL ? BRANCH_SET_LINK : 0;

	/* We currently only support poking bpf programs */
	if (!__bpf_address_lookup(bpf_func, &size, &offset, name)) {
		pr_err("%s (0x%lx): kernel/modules are not supported\n", __func__, bpf_func);
		return -EOPNOTSUPP;
	}

	/*
	 * If we are not poking at bpf prog entry, then we are simply patching in/out
	 * an unconditional branch instruction at im->ip_after_call
	 */
	if (offset) {
		if (poke_type != BPF_MOD_JUMP) {
			pr_err("%s (0x%lx): calls are not supported in bpf prog body\n", __func__,
			       bpf_func);
			return -EOPNOTSUPP;
		}
		old_inst = ppc_inst(PPC_RAW_NOP());
		if (old_addr)
			if (create_branch(&old_inst, ip, (unsigned long)old_addr, 0))
				return -ERANGE;
		new_inst = ppc_inst(PPC_RAW_NOP());
		if (new_addr)
			if (create_branch(&new_inst, ip, (unsigned long)new_addr, 0))
				return -ERANGE;
		mutex_lock(&text_mutex);
		ret = bpf_modify_inst(ip, old_inst, new_inst);
		mutex_unlock(&text_mutex);

		/* Make sure all cpus see the new instruction */
		smp_call_function(do_isync, NULL, 1);
		return ret;
	}

	bpf_func_end = bpf_func + size;

	/* Address of the jmp/call instruction in the out-of-line stub */
	ip = (void *)(bpf_func_end - bpf_jit_ool_stub + 4);

	if (!is_offset_in_branch_range((long)ip - 4 - bpf_func)) {
		pr_err("%s (0x%lx): bpf prog too large, ool stub out of branch range\n", __func__,
		       bpf_func);
		return -ERANGE;
	}

	old_inst = ppc_inst(PPC_RAW_NOP());
	if (old_addr) {
		if (is_offset_in_branch_range(ip - old_addr))
			create_branch(&old_inst, ip, (unsigned long)old_addr, branch_flags);
		else
			create_branch(&old_inst, ip, bpf_func_end - bpf_jit_long_branch_stub,
				      branch_flags);
	}
	new_inst = ppc_inst(PPC_RAW_NOP());
	if (new_addr) {
		if (is_offset_in_branch_range(ip - new_addr))
			create_branch(&new_inst, ip, (unsigned long)new_addr, branch_flags);
		else
			create_branch(&new_inst, ip, bpf_func_end - bpf_jit_long_branch_stub,
				      branch_flags);
	}

	mutex_lock(&text_mutex);

	/*
	 * 1. Update the address in the long branch stub:
	 * If new_addr is out of range, we will have to use the long branch stub, so patch new_addr
	 * here. Otherwise, revert to dummy_tramp, but only if we had patched old_addr here.
	 */
	if ((new_addr && !is_offset_in_branch_range(new_addr - ip)) ||
	    (old_addr && !is_offset_in_branch_range(old_addr - ip)))
		ret = patch_ulong((void *)(bpf_func_end - bpf_jit_long_branch_stub - SZL),
				  (new_addr && !is_offset_in_branch_range(new_addr - ip)) ?
				  (unsigned long)new_addr : (unsigned long)dummy_tramp);
	if (ret)
		goto out;

	/* 2. Update the branch/call in the out-of-line stub */
	ret = bpf_modify_inst(ip, old_inst, new_inst);
	if (ret)
		goto out;

	/* 3. Update instruction at bpf prog entry */
	ip = (void *)bpf_func;
	if (!old_addr || !new_addr) {
		if (!old_addr) {
			old_inst = ppc_inst(PPC_RAW_NOP());
			create_branch(&new_inst, ip, bpf_func_end - bpf_jit_ool_stub, 0);
		} else {
			new_inst = ppc_inst(PPC_RAW_NOP());
			create_branch(&old_inst, ip, bpf_func_end - bpf_jit_ool_stub, 0);
		}
		ret = bpf_modify_inst(ip, old_inst, new_inst);
	}

out:
	mutex_unlock(&text_mutex);

	/*
	 * Sync only if we are not attaching a trampoline to a bpf prog so the older
	 * trampoline can be freed safely.
	 */
	if (old_addr)
		smp_call_function(do_isync, NULL, 1);

	return ret;
}
