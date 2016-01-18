/*
 * Copyright © 2014 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * DOC: Shader validator for VC4.
 *
 * The VC4 has no IOMMU between it and system memory, so a user with
 * access to execute shaders could escalate privilege by overwriting
 * system memory (using the VPM write address register in the
 * general-purpose DMA mode) or reading system memory it shouldn't
 * (reading it as a texture, or uniform data, or vertex data).
 *
 * This walks over a shader BO, ensuring that its accesses are
 * appropriately bounded, and recording how many texture accesses are
 * made and where so that we can do relocations for them in the
 * uniform stream.
 */

#include "vc4_drv.h"
#include "vc4_qpu_defines.h"

struct vc4_shader_validation_state {
	struct vc4_texture_sample_info tmu_setup[2];
	int tmu_write_count[2];

	/* For registers that were last written to by a MIN instruction with
	 * one argument being a uniform, the address of the uniform.
	 * Otherwise, ~0.
	 *
	 * This is used for the validation of direct address memory reads.
	 */
	uint32_t live_min_clamp_offsets[32 + 32 + 4];
	bool live_max_clamp_regs[32 + 32 + 4];
};

static uint32_t
waddr_to_live_reg_index(uint32_t waddr, bool is_b)
{
	if (waddr < 32) {
		if (is_b)
			return 32 + waddr;
		else
			return waddr;
	} else if (waddr <= QPU_W_ACC3) {
		return 64 + waddr - QPU_W_ACC0;
	} else {
		return ~0;
	}
}

static uint32_t
raddr_add_a_to_live_reg_index(uint64_t inst)
{
	uint32_t sig = QPU_GET_FIELD(inst, QPU_SIG);
	uint32_t add_a = QPU_GET_FIELD(inst, QPU_ADD_A);
	uint32_t raddr_a = QPU_GET_FIELD(inst, QPU_RADDR_A);
	uint32_t raddr_b = QPU_GET_FIELD(inst, QPU_RADDR_B);

	if (add_a == QPU_MUX_A)
		return raddr_a;
	else if (add_a == QPU_MUX_B && sig != QPU_SIG_SMALL_IMM)
		return 32 + raddr_b;
	else if (add_a <= QPU_MUX_R3)
		return 64 + add_a;
	else
		return ~0;
}

static bool
is_tmu_submit(uint32_t waddr)
{
	return (waddr == QPU_W_TMU0_S ||
		waddr == QPU_W_TMU1_S);
}

static bool
is_tmu_write(uint32_t waddr)
{
	return (waddr >= QPU_W_TMU0_S &&
		waddr <= QPU_W_TMU1_B);
}

static bool
record_texture_sample(struct vc4_validated_shader_info *validated_shader,
		      struct vc4_shader_validation_state *validation_state,
		      int tmu)
{
	uint32_t s = validated_shader->num_texture_samples;
	int i;
	struct vc4_texture_sample_info *temp_samples;

	temp_samples = krealloc(validated_shader->texture_samples,
				(s + 1) * sizeof(*temp_samples),
				GFP_KERNEL);
	if (!temp_samples)
		return false;

	memcpy(&temp_samples[s],
	       &validation_state->tmu_setup[tmu],
	       sizeof(*temp_samples));

	validated_shader->num_texture_samples = s + 1;
	validated_shader->texture_samples = temp_samples;

	for (i = 0; i < 4; i++)
		validation_state->tmu_setup[tmu].p_offset[i] = ~0;

	return true;
}

static bool
check_tmu_write(uint64_t inst,
		struct vc4_validated_shader_info *validated_shader,
		struct vc4_shader_validation_state *validation_state,
		bool is_mul)
{
	uint32_t waddr = (is_mul ?
			  QPU_GET_FIELD(inst, QPU_WADDR_MUL) :
			  QPU_GET_FIELD(inst, QPU_WADDR_ADD));
	uint32_t raddr_a = QPU_GET_FIELD(inst, QPU_RADDR_A);
	uint32_t raddr_b = QPU_GET_FIELD(inst, QPU_RADDR_B);
	int tmu = waddr > QPU_W_TMU0_B;
	bool submit = is_tmu_submit(waddr);
	bool is_direct = submit && validation_state->tmu_write_count[tmu] == 0;
	uint32_t sig = QPU_GET_FIELD(inst, QPU_SIG);

	if (is_direct) {
		uint32_t add_b = QPU_GET_FIELD(inst, QPU_ADD_B);
		uint32_t clamp_reg, clamp_offset;

		if (sig == QPU_SIG_SMALL_IMM) {
			DRM_ERROR("direct TMU read used small immediate\n");
			return false;
		}

		/* Make sure that this texture load is an add of the base
		 * address of the UBO to a clamped offset within the UBO.
		 */
		if (is_mul ||
		    QPU_GET_FIELD(inst, QPU_OP_ADD) != QPU_A_ADD) {
			DRM_ERROR("direct TMU load wasn't an add\n");
			return false;
		}

		/* We assert that the the clamped address is the first
		 * argument, and the UBO base address is the second argument.
		 * This is arbitrary, but simpler than supporting flipping the
		 * two either way.
		 */
		clamp_reg = raddr_add_a_to_live_reg_index(inst);
		if (clamp_reg == ~0) {
			DRM_ERROR("direct TMU load wasn't clamped\n");
			return false;
		}

		clamp_offset = validation_state->live_min_clamp_offsets[clamp_reg];
		if (clamp_offset == ~0) {
			DRM_ERROR("direct TMU load wasn't clamped\n");
			return false;
		}

		/* Store the clamp value's offset in p1 (see reloc_tex() in
		 * vc4_validate.c).
		 */
		validation_state->tmu_setup[tmu].p_offset[1] =
			clamp_offset;

		if (!(add_b == QPU_MUX_A && raddr_a == QPU_R_UNIF) &&
		    !(add_b == QPU_MUX_B && raddr_b == QPU_R_UNIF)) {
			DRM_ERROR("direct TMU load didn't add to a uniform\n");
			return false;
		}

		validation_state->tmu_setup[tmu].is_direct = true;
	} else {
		if (raddr_a == QPU_R_UNIF || (sig != QPU_SIG_SMALL_IMM &&
					      raddr_b == QPU_R_UNIF)) {
			DRM_ERROR("uniform read in the same instruction as "
				  "texture setup.\n");
			return false;
		}
	}

	if (validation_state->tmu_write_count[tmu] >= 4) {
		DRM_ERROR("TMU%d got too many parameters before dispatch\n",
			  tmu);
		return false;
	}
	validation_state->tmu_setup[tmu].p_offset[validation_state->tmu_write_count[tmu]] =
		validated_shader->uniforms_size;
	validation_state->tmu_write_count[tmu]++;
	/* Since direct uses a RADDR uniform reference, it will get counted in
	 * check_instruction_reads()
	 */
	if (!is_direct)
		validated_shader->uniforms_size += 4;

	if (submit) {
		if (!record_texture_sample(validated_shader,
					   validation_state, tmu)) {
			return false;
		}

		validation_state->tmu_write_count[tmu] = 0;
	}

	return true;
}

static bool
check_reg_write(uint64_t inst,
		struct vc4_validated_shader_info *validated_shader,
		struct vc4_shader_validation_state *validation_state,
		bool is_mul)
{
	uint32_t waddr = (is_mul ?
			  QPU_GET_FIELD(inst, QPU_WADDR_MUL) :
			  QPU_GET_FIELD(inst, QPU_WADDR_ADD));

	switch (waddr) {
	case QPU_W_UNIFORMS_ADDRESS:
		/* XXX: We'll probably need to support this for reladdr, but
		 * it's definitely a security-related one.
		 */
		DRM_ERROR("uniforms address load unsupported\n");
		return false;

	case QPU_W_TLB_COLOR_MS:
	case QPU_W_TLB_COLOR_ALL:
	case QPU_W_TLB_Z:
		/* These only interact with the tile buffer, not main memory,
		 * so they're safe.
		 */
		return true;

	case QPU_W_TMU0_S:
	case QPU_W_TMU0_T:
	case QPU_W_TMU0_R:
	case QPU_W_TMU0_B:
	case QPU_W_TMU1_S:
	case QPU_W_TMU1_T:
	case QPU_W_TMU1_R:
	case QPU_W_TMU1_B:
		return check_tmu_write(inst, validated_shader, validation_state,
				       is_mul);

	case QPU_W_HOST_INT:
	case QPU_W_TMU_NOSWAP:
	case QPU_W_TLB_ALPHA_MASK:
	case QPU_W_MUTEX_RELEASE:
		/* XXX: I haven't thought about these, so don't support them
		 * for now.
		 */
		DRM_ERROR("Unsupported waddr %d\n", waddr);
		return false;

	case QPU_W_VPM_ADDR:
		DRM_ERROR("General VPM DMA unsupported\n");
		return false;

	case QPU_W_VPM:
	case QPU_W_VPMVCD_SETUP:
		/* We allow VPM setup in general, even including VPM DMA
		 * configuration setup, because the (unsafe) DMA can only be
		 * triggered by QPU_W_VPM_ADDR writes.
		 */
		return true;

	case QPU_W_TLB_STENCIL_SETUP:
		return true;
	}

	return true;
}

static void
track_live_clamps(uint64_t inst,
		  struct vc4_validated_shader_info *validated_shader,
		  struct vc4_shader_validation_state *validation_state)
{
	uint32_t op_add = QPU_GET_FIELD(inst, QPU_OP_ADD);
	uint32_t waddr_add = QPU_GET_FIELD(inst, QPU_WADDR_ADD);
	uint32_t waddr_mul = QPU_GET_FIELD(inst, QPU_WADDR_MUL);
	uint32_t cond_add = QPU_GET_FIELD(inst, QPU_COND_ADD);
	uint32_t add_a = QPU_GET_FIELD(inst, QPU_ADD_A);
	uint32_t add_b = QPU_GET_FIELD(inst, QPU_ADD_B);
	uint32_t raddr_a = QPU_GET_FIELD(inst, QPU_RADDR_A);
	uint32_t raddr_b = QPU_GET_FIELD(inst, QPU_RADDR_B);
	uint32_t sig = QPU_GET_FIELD(inst, QPU_SIG);
	bool ws = inst & QPU_WS;
	uint32_t lri_add_a, lri_add, lri_mul;
	bool add_a_is_min_0;

	/* Check whether OP_ADD's A argumennt comes from a live MAX(x, 0),
	 * before we clear previous live state.
	 */
	lri_add_a = raddr_add_a_to_live_reg_index(inst);
	add_a_is_min_0 = (lri_add_a != ~0 &&
			  validation_state->live_max_clamp_regs[lri_add_a]);

	/* Clear live state for registers written by our instruction. */
	lri_add = waddr_to_live_reg_index(waddr_add, ws);
	lri_mul = waddr_to_live_reg_index(waddr_mul, !ws);
	if (lri_mul != ~0) {
		validation_state->live_max_clamp_regs[lri_mul] = false;
		validation_state->live_min_clamp_offsets[lri_mul] = ~0;
	}
	if (lri_add != ~0) {
		validation_state->live_max_clamp_regs[lri_add] = false;
		validation_state->live_min_clamp_offsets[lri_add] = ~0;
	} else {
		/* Nothing further to do for live tracking, since only ADDs
		 * generate new live clamp registers.
		 */
		return;
	}

	/* Now, handle remaining live clamp tracking for the ADD operation. */

	if (cond_add != QPU_COND_ALWAYS)
		return;

	if (op_add == QPU_A_MAX) {
		/* Track live clamps of a value to a minimum of 0 (in either
		 * arg).
		 */
		if (sig != QPU_SIG_SMALL_IMM || raddr_b != 0 ||
		    (add_a != QPU_MUX_B && add_b != QPU_MUX_B)) {
			return;
		}

		validation_state->live_max_clamp_regs[lri_add] = true;
	} else if (op_add == QPU_A_MIN) {
		/* Track live clamps of a value clamped to a minimum of 0 and
		 * a maximum of some uniform's offset.
		 */
		if (!add_a_is_min_0)
			return;

		if (!(add_b == QPU_MUX_A && raddr_a == QPU_R_UNIF) &&
		    !(add_b == QPU_MUX_B && raddr_b == QPU_R_UNIF &&
		      sig != QPU_SIG_SMALL_IMM)) {
			return;
		}

		validation_state->live_min_clamp_offsets[lri_add] =
			validated_shader->uniforms_size;
	}
}

static bool
check_instruction_writes(uint64_t inst,
			 struct vc4_validated_shader_info *validated_shader,
			 struct vc4_shader_validation_state *validation_state)
{
	uint32_t waddr_add = QPU_GET_FIELD(inst, QPU_WADDR_ADD);
	uint32_t waddr_mul = QPU_GET_FIELD(inst, QPU_WADDR_MUL);
	bool ok;

	if (is_tmu_write(waddr_add) && is_tmu_write(waddr_mul)) {
		DRM_ERROR("ADD and MUL both set up textures\n");
		return false;
	}

	ok = (check_reg_write(inst, validated_shader, validation_state,
			      false) &&
	      check_reg_write(inst, validated_shader, validation_state,
			      true));

	track_live_clamps(inst, validated_shader, validation_state);

	return ok;
}

static bool
check_instruction_reads(uint64_t inst,
			struct vc4_validated_shader_info *validated_shader)
{
	uint32_t raddr_a = QPU_GET_FIELD(inst, QPU_RADDR_A);
	uint32_t raddr_b = QPU_GET_FIELD(inst, QPU_RADDR_B);
	uint32_t sig = QPU_GET_FIELD(inst, QPU_SIG);

	if (raddr_a == QPU_R_UNIF ||
	    (raddr_b == QPU_R_UNIF && sig != QPU_SIG_SMALL_IMM)) {
		/* This can't overflow the uint32_t, because we're reading 8
		 * bytes of instruction to increment by 4 here, so we'd
		 * already be OOM.
		 */
		validated_shader->uniforms_size += 4;
	}

	return true;
}

struct vc4_validated_shader_info *
vc4_validate_shader(struct drm_gem_cma_object *shader_obj)
{
	bool found_shader_end = false;
	int shader_end_ip = 0;
	uint32_t ip, max_ip;
	uint64_t *shader;
	struct vc4_validated_shader_info *validated_shader;
	struct vc4_shader_validation_state validation_state;
	int i;

	memset(&validation_state, 0, sizeof(validation_state));

	for (i = 0; i < 8; i++)
		validation_state.tmu_setup[i / 4].p_offset[i % 4] = ~0;
	for (i = 0; i < ARRAY_SIZE(validation_state.live_min_clamp_offsets); i++)
		validation_state.live_min_clamp_offsets[i] = ~0;

	shader = shader_obj->vaddr;
	max_ip = shader_obj->base.size / sizeof(uint64_t);

	validated_shader = kcalloc(1, sizeof(*validated_shader), GFP_KERNEL);
	if (!validated_shader)
		return NULL;

	for (ip = 0; ip < max_ip; ip++) {
		uint64_t inst = shader[ip];
		uint32_t sig = QPU_GET_FIELD(inst, QPU_SIG);

		switch (sig) {
		case QPU_SIG_NONE:
		case QPU_SIG_WAIT_FOR_SCOREBOARD:
		case QPU_SIG_SCOREBOARD_UNLOCK:
		case QPU_SIG_COLOR_LOAD:
		case QPU_SIG_LOAD_TMU0:
		case QPU_SIG_LOAD_TMU1:
		case QPU_SIG_PROG_END:
		case QPU_SIG_SMALL_IMM:
			if (!check_instruction_writes(inst, validated_shader,
						      &validation_state)) {
				DRM_ERROR("Bad write at ip %d\n", ip);
				goto fail;
			}

			if (!check_instruction_reads(inst, validated_shader))
				goto fail;

			if (sig == QPU_SIG_PROG_END) {
				found_shader_end = true;
				shader_end_ip = ip;
			}

			break;

		case QPU_SIG_LOAD_IMM:
			if (!check_instruction_writes(inst, validated_shader,
						      &validation_state)) {
				DRM_ERROR("Bad LOAD_IMM write at ip %d\n", ip);
				goto fail;
			}
			break;

		default:
			DRM_ERROR("Unsupported QPU signal %d at "
				  "instruction %d\n", sig, ip);
			goto fail;
		}

		/* There are two delay slots after program end is signaled
		 * that are still executed, then we're finished.
		 */
		if (found_shader_end && ip == shader_end_ip + 2)
			break;
	}

	if (ip == max_ip) {
		DRM_ERROR("shader failed to terminate before "
			  "shader BO end at %zd\n",
			  shader_obj->base.size);
		goto fail;
	}

	/* Again, no chance of integer overflow here because the worst case
	 * scenario is 8 bytes of uniforms plus handles per 8-byte
	 * instruction.
	 */
	validated_shader->uniforms_src_size =
		(validated_shader->uniforms_size +
		 4 * validated_shader->num_texture_samples);

	return validated_shader;

fail:
	if (validated_shader) {
		kfree(validated_shader->texture_samples);
		kfree(validated_shader);
	}
	return NULL;
}
