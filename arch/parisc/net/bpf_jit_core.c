// SPDX-License-Identifier: GPL-2.0
/*
 * Common functionality for HPPA32 and HPPA64 BPF JIT compilers
 *
 * Copyright (c) 2023 Helge Deller <deller@gmx.de>
 *
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include "bpf_jit.h"

/* Number of iterations to try until offsets converge. */
#define NR_JIT_ITERATIONS	35

static int build_body(struct hppa_jit_context *ctx, bool extra_pass, int *offset)
{
	const struct bpf_prog *prog = ctx->prog;
	int i;

	ctx->reg_seen_collect = true;
	for (i = 0; i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];
		int ret;

		ret = bpf_jit_emit_insn(insn, ctx, extra_pass);
		/* BPF_LD | BPF_IMM | BPF_DW: skip the next instruction. */
		if (ret > 0)
			i++;
		if (offset)
			offset[i] = ctx->ninsns;
		if (ret < 0)
			return ret;
	}
	ctx->reg_seen_collect = false;
	return 0;
}

bool bpf_jit_needs_zext(void)
{
	return true;
}

struct bpf_prog *bpf_int_jit_compile(struct bpf_verifier_env *env, struct bpf_prog *prog)
{
	unsigned int prog_size = 0, extable_size = 0;
	bool extra_pass = false;
	int pass = 0, prev_ninsns = 0, prologue_len, i;
	struct hppa_jit_data *jit_data;
	struct hppa_jit_context *ctx;

	if (!prog->jit_requested)
		return prog;

	jit_data = prog->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc_obj(*jit_data);
		if (!jit_data)
			return prog;
		prog->aux->jit_data = jit_data;
	}

	ctx = &jit_data->ctx;

	if (ctx->offset) {
		extra_pass = true;
		prog_size = sizeof(*ctx->insns) * ctx->ninsns;
		goto skip_init_ctx;
	}

	ctx->prog = prog;
	ctx->offset = kzalloc_objs(int, prog->len);
	if (!ctx->offset)
		goto out_err;
	for (i = 0; i < prog->len; i++) {
		prev_ninsns += 20;
		ctx->offset[i] = prev_ninsns;
	}

	for (i = 0; i < NR_JIT_ITERATIONS; i++) {
		pass++;
		ctx->ninsns = 0;
		if (build_body(ctx, extra_pass, ctx->offset))
			goto out_err;
		ctx->body_len = ctx->ninsns;
		bpf_jit_build_prologue(ctx);
		ctx->prologue_len = ctx->ninsns - ctx->body_len;
		ctx->epilogue_offset = ctx->ninsns;
		bpf_jit_build_epilogue(ctx);

		if (ctx->ninsns == prev_ninsns) {
			if (jit_data->header)
				break;
			/* obtain the actual image size */
			extable_size = prog->aux->num_exentries *
				sizeof(struct exception_table_entry);
			prog_size = sizeof(*ctx->insns) * ctx->ninsns;

			jit_data->header =
				bpf_jit_binary_alloc(prog_size + extable_size,
						     &jit_data->image,
						     sizeof(long),
						     bpf_fill_ill_insns);
			if (!jit_data->header)
				goto out_err;

			ctx->insns = (u32 *)jit_data->image;
			/*
			 * Now, when the image is allocated, the image can
			 * potentially shrink more (auipc/jalr -> jal).
			 */
		}
		prev_ninsns = ctx->ninsns;
	}

	if (i == NR_JIT_ITERATIONS) {
		pr_err("bpf-jit: image did not converge in <%d passes!\n", i);
		if (jit_data->header)
			bpf_jit_binary_free(jit_data->header);
		goto out_err;
	}

	if (extable_size)
		prog->aux->extable = (void *)ctx->insns + prog_size;

skip_init_ctx:
	pass++;
	ctx->ninsns = 0;

	bpf_jit_build_prologue(ctx);
	if (build_body(ctx, extra_pass, NULL)) {
		bpf_jit_binary_free(jit_data->header);
		goto out_err;
	}
	bpf_jit_build_epilogue(ctx);

	if (HPPA_JIT_DEBUG || bpf_jit_enable > 1) {
		if (HPPA_JIT_DUMP)
			bpf_jit_dump(prog->len, prog_size, pass, ctx->insns);
		if (HPPA_JIT_REBOOT)
			{ extern int machine_restart(char *); machine_restart(""); }
	}

	if (!prog->is_func || extra_pass) {
		if (bpf_jit_binary_lock_ro(jit_data->header)) {
			bpf_jit_binary_free(jit_data->header);
			goto out_err;
		}
		bpf_flush_icache(jit_data->header, ctx->insns + ctx->ninsns);
	}

	prog->bpf_func = (void *)ctx->insns;
	prog->jited = 1;
	prog->jited_len = prog_size;

	if (!prog->is_func || extra_pass) {
		prologue_len = ctx->epilogue_offset - ctx->body_len;
		for (i = 0; i < prog->len; i++)
			ctx->offset[i] += prologue_len;
		bpf_prog_fill_jited_linfo(prog, ctx->offset);
out_offset:
		kfree(ctx->offset);
		kfree(jit_data);
		prog->aux->jit_data = NULL;
	}

	if (HPPA_JIT_REBOOT)
		{ extern int machine_restart(char *); machine_restart(""); }

	return prog;

out_err:
	if (extra_pass) {
		prog->bpf_func = NULL;
		prog->jited = 0;
		prog->jited_len = 0;
	}
	goto out_offset;
}

u64 hppa_div64(u64 div, u64 divisor)
{
	div = div64_u64(div, divisor);
	return div;
}

u64 hppa_div64_rem(u64 div, u64 divisor)
{
	u64 rem;
	div64_u64_rem(div, divisor, &rem);
	return rem;
}
