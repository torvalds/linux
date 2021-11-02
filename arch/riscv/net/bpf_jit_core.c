// SPDX-License-Identifier: GPL-2.0
/*
 * Common functionality for RV32 and RV64 BPF JIT compilers
 *
 * Copyright (c) 2019 Björn Töpel <bjorn.topel@gmail.com>
 *
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include "bpf_jit.h"

/* Number of iterations to try until offsets converge. */
#define NR_JIT_ITERATIONS	16

static int build_body(struct rv_jit_context *ctx, bool extra_pass, int *offset)
{
	const struct bpf_prog *prog = ctx->prog;
	int i;

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
	return 0;
}

bool bpf_jit_needs_zext(void)
{
	return true;
}

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	bool tmp_blinded = false, extra_pass = false;
	struct bpf_prog *tmp, *orig_prog = prog;
	int pass = 0, prev_ninsns = 0, i;
	struct rv_jit_data *jit_data;
	struct rv_jit_context *ctx;
	unsigned int image_size = 0;

	if (!prog->jit_requested)
		return orig_prog;

	tmp = bpf_jit_blind_constants(prog);
	if (IS_ERR(tmp))
		return orig_prog;
	if (tmp != prog) {
		tmp_blinded = true;
		prog = tmp;
	}

	jit_data = prog->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc(sizeof(*jit_data), GFP_KERNEL);
		if (!jit_data) {
			prog = orig_prog;
			goto out;
		}
		prog->aux->jit_data = jit_data;
	}

	ctx = &jit_data->ctx;

	if (ctx->offset) {
		extra_pass = true;
		image_size = sizeof(*ctx->insns) * ctx->ninsns;
		goto skip_init_ctx;
	}

	ctx->prog = prog;
	ctx->offset = kcalloc(prog->len, sizeof(int), GFP_KERNEL);
	if (!ctx->offset) {
		prog = orig_prog;
		goto out_offset;
	}
	for (i = 0; i < prog->len; i++) {
		prev_ninsns += 32;
		ctx->offset[i] = prev_ninsns;
	}

	for (i = 0; i < NR_JIT_ITERATIONS; i++) {
		pass++;
		ctx->ninsns = 0;
		if (build_body(ctx, extra_pass, ctx->offset)) {
			prog = orig_prog;
			goto out_offset;
		}
		bpf_jit_build_prologue(ctx);
		ctx->epilogue_offset = ctx->ninsns;
		bpf_jit_build_epilogue(ctx);

		if (ctx->ninsns == prev_ninsns) {
			if (jit_data->header)
				break;

			image_size = sizeof(*ctx->insns) * ctx->ninsns;
			jit_data->header =
				bpf_jit_binary_alloc(image_size,
						     &jit_data->image,
						     sizeof(u32),
						     bpf_fill_ill_insns);
			if (!jit_data->header) {
				prog = orig_prog;
				goto out_offset;
			}

			ctx->insns = (u16 *)jit_data->image;
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
		prog = orig_prog;
		goto out_offset;
	}

skip_init_ctx:
	pass++;
	ctx->ninsns = 0;

	bpf_jit_build_prologue(ctx);
	if (build_body(ctx, extra_pass, NULL)) {
		bpf_jit_binary_free(jit_data->header);
		prog = orig_prog;
		goto out_offset;
	}
	bpf_jit_build_epilogue(ctx);

	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, image_size, pass, ctx->insns);

	prog->bpf_func = (void *)ctx->insns;
	prog->jited = 1;
	prog->jited_len = image_size;

	bpf_flush_icache(jit_data->header, ctx->insns + ctx->ninsns);

	if (!prog->is_func || extra_pass) {
out_offset:
		kfree(ctx->offset);
		kfree(jit_data);
		prog->aux->jit_data = NULL;
	}
out:

	if (tmp_blinded)
		bpf_jit_prog_release_other(prog, prog == orig_prog ?
					   tmp : orig_prog);
	return prog;
}
