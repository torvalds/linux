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
 * Since the VC4 has no IOMMU between it and system memory, a user
 * with access to execute shaders could escalate privilege by
 * overwriting system memory (using the VPM write address register in
 * the general-purpose DMA mode) or reading system memory it shouldn't
 * (reading it as a texture, uniform data, or direct-addressed TMU
 * lookup).
 *
 * The shader validator walks over a shader's BO, ensuring that its
 * accesses are appropriately bounded, and recording where texture
 * accesses are made so that we can do relocations for them in the
 * uniform stream.
 *
 * Shader BO are immutable for their lifetimes (enforced by not
 * allowing mmaps, GEM prime export, or rendering to from a CL), so
 * this validation is only performed at BO creation time.
 */

#include "vc4_drv.h"
#include "vc4_qpu_defines.h"

#define LIVE_REG_COUNT (32 + 32 + 4)

struct vc4_shader_validation_state {
	/* Current IP being validated. */
	uint32_t ip;

	/* IP at the end of the BO, do not read shader[max_ip] */
	uint32_t max_ip;

	uint64_t *shader;

	struct vc4_texture_sample_info tmu_setup[2];
	int tmu_write_count[2];

	/* For registers that were last written to by a MIN instruction with
	 * one argument being a uniform, the address of the uniform.
	 * Otherwise, ~0.
	 *
	 * This is used for the validation of direct address memory reads.
	 */
	uint32_t live_min_clamp_offsets[LIVE_REG_COUNT];
	bool live_max_clamp_regs[LIVE_REG_COUNT];
	uint32_t live_immediates[LIVE_REG_COUNT];

	/* Bitfield of which IPs are used as branch targets.
	 *
	 * Used for validation that the uniform stream is updated at the right
	 * points and clearing the texturing/clamping state.
	 */
	unsigned long *branch_targets;

	/* Set when entering a basic block, and cleared when the uniform
	 * address update is found.  This is used to make sure that we don't
	 * read uniforms when the address is undefined.
	 */
	bool needs_uniform_address_update;

	/* Set when we find a backwards branch.  If the branch is backwards,
	 * the taraget is probably doing an address reset to read uniforms,
	 * and so we need to be sure that a uniforms address is present in the
	 * stream, even if the shader didn't need to read uniforms in later
	 * basic blocks.
	 */
	bool needs_uniform_address_for_loop;

	/* Set when we find an instruction writing the top half of the
	 * register files.  If we allowed writing the unusable regs in
	 * a threaded shader, then the other shader running on our
	 * QPU's clamp validation would be invalid.
	 */
	bool all_registers_used;
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
live_reg_is_upper_half(uint32_t lri)
{
	return	(lri >= 16 && lri < 32) ||
		(lri >= 32 + 16 && lri < 32 + 32);
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
check_tmu_write(struct vc4_validated_shader_info *validated_shader,
		struct vc4_shader_validation_state *validation_state,
		bool is_mul)
{
	uint64_t inst = validation_state->shader[validation_state->ip];
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
			DRM_DEBUG("direct TMU read used small immediate\n");
			return false;
		}

		/* Make sure that this texture load is an add of the base
		 * address of the UBO to a clamped offset within the UBO.
		 */
		if (is_mul ||
		    QPU_GET_FIELD(inst, QPU_OP_ADD) != QPU_A_ADD) {
			DRM_DEBUG("direct TMU load wasn't an add\n");
			return false;
		}

		/* We assert that the clamped address is the first
		 * argument, and the UBO base address is the second argument.
		 * This is arbitrary, but simpler than supporting flipping the
		 * two either way.
		 */
		clamp_reg = raddr_add_a_to_live_reg_index(inst);
		if (clamp_reg == ~0) {
			DRM_DEBUG("direct TMU load wasn't clamped\n");
			return false;
		}

		clamp_offset = validation_state->live_min_clamp_offsets[clamp_reg];
		if (clamp_offset == ~0) {
			DRM_DEBUG("direct TMU load wasn't clamped\n");
			return false;
		}

		/* Store the clamp value's offset in p1 (see reloc_tex() in
		 * vc4_validate.c).
		 */
		validation_state->tmu_setup[tmu].p_offset[1] =
			clamp_offset;

		if (!(add_b == QPU_MUX_A && raddr_a == QPU_R_UNIF) &&
		    !(add_b == QPU_MUX_B && raddr_b == QPU_R_UNIF)) {
			DRM_DEBUG("direct TMU load didn't add to a uniform\n");
			return false;
		}

		validation_state->tmu_setup[tmu].is_direct = true;
	} else {
		if (raddr_a == QPU_R_UNIF || (sig != QPU_SIG_SMALL_IMM &&
					      raddr_b == QPU_R_UNIF)) {
			DRM_DEBUG("uniform read in the same instruction as "
				  "texture setup.\n");
			return false;
		}
	}

	if (validation_state->tmu_write_count[tmu] >= 4) {
		DRM_DEBUG("TMU%d got too many parameters before dispatch\n",
			  tmu);
		return false;
	}
	validation_state->tmu_setup[tmu].p_offset[validation_state->tmu_write_count[tmu]] =
		validated_shader->uniforms_size;
	validation_state->tmu_write_count[tmu]++;
	/* Since direct uses a RADDR uniform reference, it will get counted in
	 * check_instruction_reads()
	 */
	if (!is_direct) {
		if (validation_state->needs_uniform_address_update) {
			DRM_DEBUG("Texturing with undefined uniform address\n");
			return false;
		}

		validated_shader->uniforms_size += 4;
	}

	if (submit) {
		if (!record_texture_sample(validated_shader,
					   validation_state, tmu)) {
			return false;
		}

		validation_state->tmu_write_count[tmu] = 0;
	}

	return true;
}

static bool require_uniform_address_uniform(struct vc4_validated_shader_info *validated_shader)
{
	uint32_t o = validated_shader->num_uniform_addr_offsets;
	uint32_t num_uniforms = validated_shader->uniforms_size / 4;

	validated_shader->uniform_addr_offsets =
		krealloc(validated_shader->uniform_addr_offsets,
			 (o + 1) *
			 sizeof(*validated_shader->uniform_addr_offsets),
			 GFP_KERNEL);
	if (!validated_shader->uniform_addr_offsets)
		return false;

	validated_shader->uniform_addr_offsets[o] = num_uniforms;
	validated_shader->num_uniform_addr_offsets++;

	return true;
}

static bool
validate_uniform_address_write(struct vc4_validated_shader_info *validated_shader,
			       struct vc4_shader_validation_state *validation_state,
			       bool is_mul)
{
	uint64_t inst = validation_state->shader[validation_state->ip];
	u32 add_b = QPU_GET_FIELD(inst, QPU_ADD_B);
	u32 raddr_a = QPU_GET_FIELD(inst, QPU_RADDR_A);
	u32 raddr_b = QPU_GET_FIELD(inst, QPU_RADDR_B);
	u32 add_lri = raddr_add_a_to_live_reg_index(inst);
	/* We want our reset to be pointing at whatever uniform follows the
	 * uniforms base address.
	 */
	u32 expected_offset = validated_shader->uniforms_size + 4;

	/* We only support absolute uniform address changes, and we
	 * require that they be in the current basic block before any
	 * of its uniform reads.
	 *
	 * One could potentially emit more efficient QPU code, by
	 * noticing that (say) an if statement does uniform control
	 * flow for all threads and that the if reads the same number
	 * of uniforms on each side.  However, this scheme is easy to
	 * validate so it's all we allow for now.
	 */
	switch (QPU_GET_FIELD(inst, QPU_SIG)) {
	case QPU_SIG_NONE:
	case QPU_SIG_SCOREBOARD_UNLOCK:
	case QPU_SIG_COLOR_LOAD:
	case QPU_SIG_LOAD_TMU0:
	case QPU_SIG_LOAD_TMU1:
		break;
	default:
		DRM_DEBUG("uniforms address change must be "
			  "normal math\n");
		return false;
	}

	if (is_mul || QPU_GET_FIELD(inst, QPU_OP_ADD) != QPU_A_ADD) {
		DRM_DEBUG("Uniform address reset must be an ADD.\n");
		return false;
	}

	if (QPU_GET_FIELD(inst, QPU_COND_ADD) != QPU_COND_ALWAYS) {
		DRM_DEBUG("Uniform address reset must be unconditional.\n");
		return false;
	}

	if (QPU_GET_FIELD(inst, QPU_PACK) != QPU_PACK_A_NOP &&
	    !(inst & QPU_PM)) {
		DRM_DEBUG("No packing allowed on uniforms reset\n");
		return false;
	}

	if (add_lri == -1) {
		DRM_DEBUG("First argument of uniform address write must be "
			  "an immediate value.\n");
		return false;
	}

	if (validation_state->live_immediates[add_lri] != expected_offset) {
		DRM_DEBUG("Resetting uniforms with offset %db instead of %db\n",
			  validation_state->live_immediates[add_lri],
			  expected_offset);
		return false;
	}

	if (!(add_b == QPU_MUX_A && raddr_a == QPU_R_UNIF) &&
	    !(add_b == QPU_MUX_B && raddr_b == QPU_R_UNIF)) {
		DRM_DEBUG("Second argument of uniform address write must be "
			  "a uniform.\n");
		return false;
	}

	validation_state->needs_uniform_address_update = false;
	validation_state->needs_uniform_address_for_loop = false;
	return require_uniform_address_uniform(validated_shader);
}

static bool
check_reg_write(struct vc4_validated_shader_info *validated_shader,
		struct vc4_shader_validation_state *validation_state,
		bool is_mul)
{
	uint64_t inst = validation_state->shader[validation_state->ip];
	uint32_t waddr = (is_mul ?
			  QPU_GET_FIELD(inst, QPU_WADDR_MUL) :
			  QPU_GET_FIELD(inst, QPU_WADDR_ADD));
	uint32_t sig = QPU_GET_FIELD(inst, QPU_SIG);
	bool ws = inst & QPU_WS;
	bool is_b = is_mul ^ ws;
	u32 lri = waddr_to_live_reg_index(waddr, is_b);

	if (lri != -1) {
		uint32_t cond_add = QPU_GET_FIELD(inst, QPU_COND_ADD);
		uint32_t cond_mul = QPU_GET_FIELD(inst, QPU_COND_MUL);

		if (sig == QPU_SIG_LOAD_IMM &&
		    QPU_GET_FIELD(inst, QPU_PACK) == QPU_PACK_A_NOP &&
		    ((is_mul && cond_mul == QPU_COND_ALWAYS) ||
		     (!is_mul && cond_add == QPU_COND_ALWAYS))) {
			validation_state->live_immediates[lri] =
				QPU_GET_FIELD(inst, QPU_LOAD_IMM);
		} else {
			validation_state->live_immediates[lri] = ~0;
		}

		if (live_reg_is_upper_half(lri))
			validation_state->all_registers_used = true;
	}

	switch (waddr) {
	case QPU_W_UNIFORMS_ADDRESS:
		if (is_b) {
			DRM_DEBUG("relative uniforms address change "
				  "unsupported\n");
			return false;
		}

		return validate_uniform_address_write(validated_shader,
						      validation_state,
						      is_mul);

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
		return check_tmu_write(validated_shader, validation_state,
				       is_mul);

	case QPU_W_HOST_INT:
	case QPU_W_TMU_NOSWAP:
	case QPU_W_TLB_ALPHA_MASK:
	case QPU_W_MUTEX_RELEASE:
		/* XXX: I haven't thought about these, so don't support them
		 * for now.
		 */
		DRM_DEBUG("Unsupported waddr %d\n", waddr);
		return false;

	case QPU_W_VPM_ADDR:
		DRM_DEBUG("General VPM DMA unsupported\n");
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
track_live_clamps(struct vc4_validated_shader_info *validated_shader,
		  struct vc4_shader_validation_state *validation_state)
{
	uint64_t inst = validation_state->shader[validation_state->ip];
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
check_instruction_writes(struct vc4_validated_shader_info *validated_shader,
			 struct vc4_shader_validation_state *validation_state)
{
	uint64_t inst = validation_state->shader[validation_state->ip];
	uint32_t waddr_add = QPU_GET_FIELD(inst, QPU_WADDR_ADD);
	uint32_t waddr_mul = QPU_GET_FIELD(inst, QPU_WADDR_MUL);
	bool ok;

	if (is_tmu_write(waddr_add) && is_tmu_write(waddr_mul)) {
		DRM_DEBUG("ADD and MUL both set up textures\n");
		return false;
	}

	ok = (check_reg_write(validated_shader, validation_state, false) &&
	      check_reg_write(validated_shader, validation_state, true));

	track_live_clamps(validated_shader, validation_state);

	return ok;
}

static bool
check_branch(uint64_t inst,
	     struct vc4_validated_shader_info *validated_shader,
	     struct vc4_shader_validation_state *validation_state,
	     int ip)
{
	int32_t branch_imm = QPU_GET_FIELD(inst, QPU_BRANCH_TARGET);
	uint32_t waddr_add = QPU_GET_FIELD(inst, QPU_WADDR_ADD);
	uint32_t waddr_mul = QPU_GET_FIELD(inst, QPU_WADDR_MUL);

	if ((int)branch_imm < 0)
		validation_state->needs_uniform_address_for_loop = true;

	/* We don't want to have to worry about validation of this, and
	 * there's no need for it.
	 */
	if (waddr_add != QPU_W_NOP || waddr_mul != QPU_W_NOP) {
		DRM_DEBUG("branch instruction at %d wrote a register.\n",
			  validation_state->ip);
		return false;
	}

	return true;
}

static bool
check_instruction_reads(struct vc4_validated_shader_info *validated_shader,
			struct vc4_shader_validation_state *validation_state)
{
	uint64_t inst = validation_state->shader[validation_state->ip];
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

		if (validation_state->needs_uniform_address_update) {
			DRM_DEBUG("Uniform read with undefined uniform "
				  "address\n");
			return false;
		}
	}

	if ((raddr_a >= 16 && raddr_a < 32) ||
	    (raddr_b >= 16 && raddr_b < 32 && sig != QPU_SIG_SMALL_IMM)) {
		validation_state->all_registers_used = true;
	}

	return true;
}

/* Make sure that all branches are absolute and point within the shader, and
 * note their targets for later.
 */
static bool
vc4_validate_branches(struct vc4_shader_validation_state *validation_state)
{
	uint32_t max_branch_target = 0;
	int ip;
	int last_branch = -2;

	for (ip = 0; ip < validation_state->max_ip; ip++) {
		uint64_t inst = validation_state->shader[ip];
		int32_t branch_imm = QPU_GET_FIELD(inst, QPU_BRANCH_TARGET);
		uint32_t sig = QPU_GET_FIELD(inst, QPU_SIG);
		uint32_t after_delay_ip = ip + 4;
		uint32_t branch_target_ip;

		if (sig == QPU_SIG_PROG_END) {
			/* There are two delay slots after program end is
			 * signaled that are still executed, then we're
			 * finished.  validation_state->max_ip is the
			 * instruction after the last valid instruction in the
			 * program.
			 */
			validation_state->max_ip = ip + 3;
			continue;
		}

		if (sig != QPU_SIG_BRANCH)
			continue;

		if (ip - last_branch < 4) {
			DRM_DEBUG("Branch at %d during delay slots\n", ip);
			return false;
		}
		last_branch = ip;

		if (inst & QPU_BRANCH_REG) {
			DRM_DEBUG("branching from register relative "
				  "not supported\n");
			return false;
		}

		if (!(inst & QPU_BRANCH_REL)) {
			DRM_DEBUG("relative branching required\n");
			return false;
		}

		/* The actual branch target is the instruction after the delay
		 * slots, plus whatever byte offset is in the low 32 bits of
		 * the instruction.  Make sure we're not branching beyond the
		 * end of the shader object.
		 */
		if (branch_imm % sizeof(inst) != 0) {
			DRM_DEBUG("branch target not aligned\n");
			return false;
		}

		branch_target_ip = after_delay_ip + (branch_imm >> 3);
		if (branch_target_ip >= validation_state->max_ip) {
			DRM_DEBUG("Branch at %d outside of shader (ip %d/%d)\n",
				  ip, branch_target_ip,
				  validation_state->max_ip);
			return false;
		}
		set_bit(branch_target_ip, validation_state->branch_targets);

		/* Make sure that the non-branching path is also not outside
		 * the shader.
		 */
		if (after_delay_ip >= validation_state->max_ip) {
			DRM_DEBUG("Branch at %d continues past shader end "
				  "(%d/%d)\n",
				  ip, after_delay_ip, validation_state->max_ip);
			return false;
		}
		set_bit(after_delay_ip, validation_state->branch_targets);
		max_branch_target = max(max_branch_target, after_delay_ip);
	}

	if (max_branch_target > validation_state->max_ip - 3) {
		DRM_DEBUG("Branch landed after QPU_SIG_PROG_END");
		return false;
	}

	return true;
}

/* Resets any known state for the shader, used when we may be branched to from
 * multiple locations in the program (or at shader start).
 */
static void
reset_validation_state(struct vc4_shader_validation_state *validation_state)
{
	int i;

	for (i = 0; i < 8; i++)
		validation_state->tmu_setup[i / 4].p_offset[i % 4] = ~0;

	for (i = 0; i < LIVE_REG_COUNT; i++) {
		validation_state->live_min_clamp_offsets[i] = ~0;
		validation_state->live_max_clamp_regs[i] = false;
		validation_state->live_immediates[i] = ~0;
	}
}

static bool
texturing_in_progress(struct vc4_shader_validation_state *validation_state)
{
	return (validation_state->tmu_write_count[0] != 0 ||
		validation_state->tmu_write_count[1] != 0);
}

static bool
vc4_handle_branch_target(struct vc4_shader_validation_state *validation_state)
{
	uint32_t ip = validation_state->ip;

	if (!test_bit(ip, validation_state->branch_targets))
		return true;

	if (texturing_in_progress(validation_state)) {
		DRM_DEBUG("Branch target landed during TMU setup\n");
		return false;
	}

	/* Reset our live values tracking, since this instruction may have
	 * multiple predecessors.
	 *
	 * One could potentially do analysis to determine that, for
	 * example, all predecessors have a live max clamp in the same
	 * register, but we don't bother with that.
	 */
	reset_validation_state(validation_state);

	/* Since we've entered a basic block from potentially multiple
	 * predecessors, we need the uniforms address to be updated before any
	 * unforms are read.  We require that after any branch point, the next
	 * uniform to be loaded is a uniform address offset.  That uniform's
	 * offset will be marked by the uniform address register write
	 * validation, or a one-off the end-of-program check.
	 */
	validation_state->needs_uniform_address_update = true;

	return true;
}

struct vc4_validated_shader_info *
vc4_validate_shader(struct drm_gem_dma_object *shader_obj)
{
	struct vc4_dev *vc4 = to_vc4_dev(shader_obj->base.dev);
	bool found_shader_end = false;
	int shader_end_ip = 0;
	uint32_t last_thread_switch_ip = -3;
	uint32_t ip;
	struct vc4_validated_shader_info *validated_shader = NULL;
	struct vc4_shader_validation_state validation_state;

	if (WARN_ON_ONCE(vc4->gen == VC4_GEN_5))
		return NULL;

	memset(&validation_state, 0, sizeof(validation_state));
	validation_state.shader = shader_obj->vaddr;
	validation_state.max_ip = shader_obj->base.size / sizeof(uint64_t);

	reset_validation_state(&validation_state);

	validation_state.branch_targets =
		kcalloc(BITS_TO_LONGS(validation_state.max_ip),
			sizeof(unsigned long), GFP_KERNEL);
	if (!validation_state.branch_targets)
		goto fail;

	validated_shader = kcalloc(1, sizeof(*validated_shader), GFP_KERNEL);
	if (!validated_shader)
		goto fail;

	if (!vc4_validate_branches(&validation_state))
		goto fail;

	for (ip = 0; ip < validation_state.max_ip; ip++) {
		uint64_t inst = validation_state.shader[ip];
		uint32_t sig = QPU_GET_FIELD(inst, QPU_SIG);

		validation_state.ip = ip;

		if (!vc4_handle_branch_target(&validation_state))
			goto fail;

		if (ip == last_thread_switch_ip + 3) {
			/* Reset r0-r3 live clamp data */
			int i;

			for (i = 64; i < LIVE_REG_COUNT; i++) {
				validation_state.live_min_clamp_offsets[i] = ~0;
				validation_state.live_max_clamp_regs[i] = false;
				validation_state.live_immediates[i] = ~0;
			}
		}

		switch (sig) {
		case QPU_SIG_NONE:
		case QPU_SIG_WAIT_FOR_SCOREBOARD:
		case QPU_SIG_SCOREBOARD_UNLOCK:
		case QPU_SIG_COLOR_LOAD:
		case QPU_SIG_LOAD_TMU0:
		case QPU_SIG_LOAD_TMU1:
		case QPU_SIG_PROG_END:
		case QPU_SIG_SMALL_IMM:
		case QPU_SIG_THREAD_SWITCH:
		case QPU_SIG_LAST_THREAD_SWITCH:
			if (!check_instruction_writes(validated_shader,
						      &validation_state)) {
				DRM_DEBUG("Bad write at ip %d\n", ip);
				goto fail;
			}

			if (!check_instruction_reads(validated_shader,
						     &validation_state))
				goto fail;

			if (sig == QPU_SIG_PROG_END) {
				found_shader_end = true;
				shader_end_ip = ip;
			}

			if (sig == QPU_SIG_THREAD_SWITCH ||
			    sig == QPU_SIG_LAST_THREAD_SWITCH) {
				validated_shader->is_threaded = true;

				if (ip < last_thread_switch_ip + 3) {
					DRM_DEBUG("Thread switch too soon after "
						  "last switch at ip %d\n", ip);
					goto fail;
				}
				last_thread_switch_ip = ip;
			}

			break;

		case QPU_SIG_LOAD_IMM:
			if (!check_instruction_writes(validated_shader,
						      &validation_state)) {
				DRM_DEBUG("Bad LOAD_IMM write at ip %d\n", ip);
				goto fail;
			}
			break;

		case QPU_SIG_BRANCH:
			if (!check_branch(inst, validated_shader,
					  &validation_state, ip))
				goto fail;

			if (ip < last_thread_switch_ip + 3) {
				DRM_DEBUG("Branch in thread switch at ip %d",
					  ip);
				goto fail;
			}

			break;
		default:
			DRM_DEBUG("Unsupported QPU signal %d at "
				  "instruction %d\n", sig, ip);
			goto fail;
		}

		/* There are two delay slots after program end is signaled
		 * that are still executed, then we're finished.
		 */
		if (found_shader_end && ip == shader_end_ip + 2)
			break;
	}

	if (ip == validation_state.max_ip) {
		DRM_DEBUG("shader failed to terminate before "
			  "shader BO end at %zd\n",
			  shader_obj->base.size);
		goto fail;
	}

	/* Might corrupt other thread */
	if (validated_shader->is_threaded &&
	    validation_state.all_registers_used) {
		DRM_DEBUG("Shader uses threading, but uses the upper "
			  "half of the registers, too\n");
		goto fail;
	}

	/* If we did a backwards branch and we haven't emitted a uniforms
	 * reset since then, we still need the uniforms stream to have the
	 * uniforms address available so that the backwards branch can do its
	 * uniforms reset.
	 *
	 * We could potentially prove that the backwards branch doesn't
	 * contain any uses of uniforms until program exit, but that doesn't
	 * seem to be worth the trouble.
	 */
	if (validation_state.needs_uniform_address_for_loop) {
		if (!require_uniform_address_uniform(validated_shader))
			goto fail;
		validated_shader->uniforms_size += 4;
	}

	/* Again, no chance of integer overflow here because the worst case
	 * scenario is 8 bytes of uniforms plus handles per 8-byte
	 * instruction.
	 */
	validated_shader->uniforms_src_size =
		(validated_shader->uniforms_size +
		 4 * validated_shader->num_texture_samples);

	kfree(validation_state.branch_targets);

	return validated_shader;

fail:
	kfree(validation_state.branch_targets);
	if (validated_shader) {
		kfree(validated_shader->uniform_addr_offsets);
		kfree(validated_shader->texture_samples);
		kfree(validated_shader);
	}
	return NULL;
}
