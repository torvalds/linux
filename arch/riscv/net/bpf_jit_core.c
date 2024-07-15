// SPDX-License-Identifier: GPL-2.0
/*
 * Common functionality for RV32 and RV64 BPF JIT compilers
 *
 * Copyright (c) 2019 Björn Töpel <bjorn.topel@gmail.com>
 *
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/memory.h>
#include <asm/patch.h>
#include <asm/cfi.h>
#include "bpf_jit.h"

/* Number of iterations to try until offsets converge. */
#define NR_JIT_ITERATIONS	32

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
	unsigned int prog_size = 0, extable_size = 0;
	bool tmp_blinded = false, extra_pass = false;
	struct bpf_prog *tmp, *orig_prog = prog;
	int pass = 0, prev_ninsns = 0, i;
	struct rv_jit_data *jit_data;
	struct rv_jit_context *ctx;

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
		prog_size = sizeof(*ctx->insns) * ctx->ninsns;
		goto skip_init_ctx;
	}

	ctx->arena_vm_start = bpf_arena_get_kern_vm_start(prog->aux->arena);
	ctx->user_vm_start = bpf_arena_get_user_vm_start(prog->aux->arena);
	ctx->prog = prog;
	ctx->offset = kcalloc(prog->len, sizeof(int), GFP_KERNEL);
	if (!ctx->offset) {
		prog = orig_prog;
		goto out_offset;
	}

	if (build_body(ctx, extra_pass, NULL)) {
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

		bpf_jit_build_prologue(ctx, bpf_is_subprog(prog));
		ctx->prologue_len = ctx->ninsns;

		if (build_body(ctx, extra_pass, ctx->offset)) {
			prog = orig_prog;
			goto out_offset;
		}

		ctx->epilogue_offset = ctx->ninsns;
		bpf_jit_build_epilogue(ctx);

		if (ctx->ninsns == prev_ninsns) {
			if (jit_data->header)
				break;
			/* obtain the actual image size */
			extable_size = prog->aux->num_exentries *
				sizeof(struct exception_table_entry);
			prog_size = sizeof(*ctx->insns) * ctx->ninsns;

			jit_data->ro_header =
				bpf_jit_binary_pack_alloc(prog_size + extable_size,
							  &jit_data->ro_image, sizeof(u32),
							  &jit_data->header, &jit_data->image,
							  bpf_fill_ill_insns);
			if (!jit_data->ro_header) {
				prog = orig_prog;
				goto out_offset;
			}

			/*
			 * Use the image(RW) for writing the JITed instructions. But also save
			 * the ro_image(RX) for calculating the offsets in the image. The RW
			 * image will be later copied to the RX image from where the program
			 * will run. The bpf_jit_binary_pack_finalize() will do this copy in the
			 * final step.
			 */
			ctx->ro_insns = (u16 *)jit_data->ro_image;
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
		prog = orig_prog;
		goto out_free_hdr;
	}

	if (extable_size)
		prog->aux->extable = (void *)ctx->ro_insns + prog_size;

skip_init_ctx:
	pass++;
	ctx->ninsns = 0;
	ctx->nexentries = 0;

	bpf_jit_build_prologue(ctx, bpf_is_subprog(prog));
	if (build_body(ctx, extra_pass, NULL)) {
		prog = orig_prog;
		goto out_free_hdr;
	}
	bpf_jit_build_epilogue(ctx);

	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, prog_size, pass, ctx->insns);

	prog->bpf_func = (void *)ctx->ro_insns + cfi_get_offset();
	prog->jited = 1;
	prog->jited_len = prog_size - cfi_get_offset();

	if (!prog->is_func || extra_pass) {
		if (WARN_ON(bpf_jit_binary_pack_finalize(prog, jit_data->ro_header,
							 jit_data->header))) {
			/* ro_header has been freed */
			jit_data->ro_header = NULL;
			prog = orig_prog;
			goto out_offset;
		}
		/*
		 * The instructions have now been copied to the ROX region from
		 * where they will execute.
		 * Write any modified data cache blocks out to memory and
		 * invalidate the corresponding blocks in the instruction cache.
		 */
		bpf_flush_icache(jit_data->ro_header, ctx->ro_insns + ctx->ninsns);
		for (i = 0; i < prog->len; i++)
			ctx->offset[i] = ninsns_rvoff(ctx->offset[i]);
		bpf_prog_fill_jited_linfo(prog, ctx->offset);
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

out_free_hdr:
	if (jit_data->header) {
		bpf_arch_text_copy(&jit_data->ro_header->size, &jit_data->header->size,
				   sizeof(jit_data->header->size));
		bpf_jit_binary_pack_free(jit_data->ro_header, jit_data->header);
	}
	goto out_offset;
}

u64 bpf_jit_alloc_exec_limit(void)
{
	return BPF_JIT_REGION_SIZE;
}

void *bpf_arch_text_copy(void *dst, void *src, size_t len)
{
	int ret;

	mutex_lock(&text_mutex);
	ret = patch_text_nosync(dst, src, len);
	mutex_unlock(&text_mutex);

	if (ret)
		return ERR_PTR(-EINVAL);

	return dst;
}

int bpf_arch_text_invalidate(void *dst, size_t len)
{
	int ret;

	mutex_lock(&text_mutex);
	ret = patch_text_set_nosync(dst, 0, len);
	mutex_unlock(&text_mutex);

	return ret;
}

void bpf_jit_free(struct bpf_prog *prog)
{
	if (prog->jited) {
		struct rv_jit_data *jit_data = prog->aux->jit_data;
		struct bpf_binary_header *hdr;

		/*
		 * If we fail the final pass of JIT (from jit_subprogs),
		 * the program may not be finalized yet. Call finalize here
		 * before freeing it.
		 */
		if (jit_data) {
			bpf_jit_binary_pack_finalize(prog, jit_data->ro_header, jit_data->header);
			kfree(jit_data);
		}
		hdr = bpf_jit_binary_pack_hdr(prog);
		bpf_jit_binary_pack_free(hdr, NULL);
		WARN_ON_ONCE(!bpf_prog_kallsyms_verify_off(prog));
	}

	bpf_prog_unlock_free(prog);
}
