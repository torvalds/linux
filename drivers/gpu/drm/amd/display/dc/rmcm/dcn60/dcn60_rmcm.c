/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "dc.h"
#include "reg_helper.h"

#include "dcn/dcn_6_0_0_offset.h"
#include "dcn/dcn_6_0_0_sh_mask.h"

#include "dcn60_rmcm.h"

#define SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

/* DCN60 renamed MPC_RMCM_3DLUT_RAM_SEL to MPC_RMCM_3DLUT_READ_SEL */
#define MPC_RMCM0_MPC_RMCM_3DLUT_READ_WRITE_CONTROL__MPC_RMCM_3DLUT_RAM_SEL__SHIFT \
	MPC_RMCM0_MPC_RMCM_3DLUT_READ_WRITE_CONTROL__MPC_RMCM_3DLUT_READ_SEL__SHIFT
#define MPC_RMCM0_MPC_RMCM_3DLUT_READ_WRITE_CONTROL__MPC_RMCM_3DLUT_RAM_SEL_MASK \
	MPC_RMCM0_MPC_RMCM_3DLUT_READ_WRITE_CONTROL__MPC_RMCM_3DLUT_READ_SEL_MASK

/* Resource-level macros needed by MPC_RMCM_REG_LIST_DCN42 */
#define BASE_INNER(seg) ctx->dcn_reg_offsets[seg]

#define BASE(seg) BASE_INNER(seg)

#define SRII(reg_name, block, id)\
	REG_STRUCT.reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

static struct dcn42_rmcm_registers rmcm_regs;

#define dcn_rmcm_regs_init()               \
	MPC_RMCM_REG_LIST_DCN42(0),            \
		MPC_RMCM_REG_LIST_DCN42(1)

static const struct dcn42_rmcm_shift rmcm_shift = {
	MPC_RMCM_COMMON_MASK_SH_LIST_DCN42(__SHIFT)};

static const struct dcn42_rmcm_mask rmcm_mask = {
	MPC_RMCM_COMMON_MASK_SH_LIST_DCN42(_MASK)};

#define REG(reg)\
	rmcm42->rmcm_regs->reg

#define CTX \
	rmcm42->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	rmcm42->rmcm_shift->field_name, rmcm42->rmcm_mask->field_name

/*
 * DCN60 RMCM LUT-bank programming (single-SRAM variant).
 *
 * DCN60 has one LUT SRAM per RMCM instance instead of the DCN42 A/B pair, and the 3DLUT
 * read/write-control RAM select was renamed RAM_SEL -> READ_SEL. These overrides always
 * target bank A and ignore lut_bank_a, so nothing can select a bank that does not exist.
 * Everything bank-agnostic (LUT upload, fast load, power, MPCC routing) is reused from the
 * DCN42 rmcm42_* helpers.
 */
static void rmcm60_get_lut_mode(struct rmcm *rmcm,
		const enum MCM_LUT_ID id,
		int rmcm_id,
		bool *enable,
		bool *lut_bank_a)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);
	uint32_t lut_mode = 0;

	*enable = false;
	*lut_bank_a = true; /* single SRAM: always bank A */

	switch (id) {
	case MCM_LUT_SHAPER:
		REG_GET(MPC_RMCM_SHAPER_CONTROL[rmcm_id],
				MPC_RMCM_SHAPER_MODE_CURRENT, &lut_mode);
		*enable = lut_mode != 0;
		break;
	case MCM_LUT_1DLUT:
		/* RMCM doesn't support 1DLUT, return disabled */
		*enable = false;
		break;
	case MCM_LUT_3DLUT:
	default:
		REG_GET(MPC_RMCM_3DLUT_MODE[rmcm_id],
				MPC_RMCM_3DLUT_MODE_CURRENT, &lut_mode);
		*enable = lut_mode != 0;
		break;
	}
}

static void rmcm60_program_lut_mode(struct rmcm *rmcm,
	const enum MCM_LUT_ID id,
	bool enable,
	bool lut_bank_a,
	const enum dc_cm_lut_size size,
	uint16_t bias,
	uint16_t scale,
	int rmcm_id)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	(void)bias;
	(void)scale;
	(void)lut_bank_a; /* single SRAM: always bank A (mode 1) */

	switch (id) {
	case MCM_LUT_3DLUT:
		if (enable) {
			uint32_t lut_size = rmcm42_get_3dlut_width(size);

			REG_UPDATE_2(MPC_RMCM_3DLUT_MODE[rmcm_id],
					MPC_RMCM_3DLUT_MODE, 1,
					MPC_RMCM_3DLUT_SIZE, lut_size);

			/* Default to 12-bit (30BIT_EN=0) for DMA path;
			 * populate_lut overrides for host-load path.
			 */
			REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_id],
				MPC_RMCM_3DLUT_WRITE_EN_MASK, 0xF);
			REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_id],
				MPC_RMCM_3DLUT_30BIT_EN, 0);
		} else {
			if (rmcm->ctx->dc->debug.enable_mem_low_power.bits.mpc)
				rmcm42_power_on_shaper_3dlut(rmcm, rmcm_id, false);
			REG_UPDATE(MPC_RMCM_3DLUT_MODE[rmcm_id], MPC_RMCM_3DLUT_MODE, 0);
		}
		break;
	case MCM_LUT_SHAPER:
		if (enable) {
			REG_UPDATE(MPC_RMCM_SHAPER_CONTROL[rmcm_id],
					MPC_RMCM_SHAPER_LUT_MODE, 1);
		} else {
			if (rmcm->ctx->dc->debug.enable_mem_low_power.bits.mpc)
				rmcm42_power_on_shaper_3dlut(rmcm, rmcm_id, false);
			REG_UPDATE(MPC_RMCM_SHAPER_CONTROL[rmcm_id],
					MPC_RMCM_SHAPER_LUT_MODE, 0);
		}
		break;
	case MCM_LUT_1DLUT:
		/* RMCM doesn't support 1DLUT */
		break;
	}
}

static void rmcm60_program_lut_read_write_control(struct rmcm *rmcm, const enum MCM_LUT_ID id,
	bool lut_bank_a, bool enabled, int rmcm_id)
{
	struct dcn42_rmcm *rmcm42 = TO_DCN42_RMCM(rmcm);

	(void)lut_bank_a; /* single SRAM: always bank A */

	switch (id) {
	case MCM_LUT_3DLUT:
		REG_UPDATE(MPC_RMCM_3DLUT_MODE[rmcm_id], MPC_RMCM_3DLUT_MODE,
			enabled ? 1 : 0);

		/* DCN60 renamed RAM_SEL -> READ_SEL; single bank -> always 0 */
		REG_UPDATE(MPC_RMCM_3DLUT_READ_WRITE_CONTROL[rmcm_id],
			MPC_RMCM_3DLUT_RAM_SEL, 0);
		break;
	case MCM_LUT_SHAPER:
		REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[rmcm_id],
			MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, 7);

		REG_UPDATE(MPC_RMCM_SHAPER_LUT_WRITE_EN_MASK[rmcm_id],
			MPC_RMCM_SHAPER_LUT_WRITE_SEL, 0);

		REG_SET(MPC_RMCM_SHAPER_LUT_INDEX[rmcm_id], 0,
			MPC_RMCM_SHAPER_LUT_INDEX, 0);
		break;
	default:
		break;
	}
}

/* DCN60 has no RAM_SEL - it became READ_SEL, which the reused DCN42 read_state maps onto
 * via the compat macro. Clear the duplicate so the reported state stays unambiguous.
 */
static void rmcm60_read_state(struct rmcm *rmcm, int rmcm_inst,
		struct rmcm_state *s)
{
	rmcm42_read_state(rmcm, rmcm_inst, s);
	s->regs.rmcm_3dlut_ram_sel = 0;
}

static const struct rmcm_funcs dcn60_rmcm_funcs = {
	.get_lut_mode = rmcm60_get_lut_mode,
	.program_lut_mode = rmcm60_program_lut_mode,
	.populate_lut = rmcm42_populate_lut,
	.program_lut_read_write_control = rmcm60_program_lut_read_write_control,
	.update_3dlut_fast_load_select = rmcm42_update_3dlut_fast_load_select,
	.get_3dlut_fast_load_status = rmcm42_get_3dlut_fast_load_status,
	.read_rmcm_state = rmcm60_read_state,
	.connect_mpcc = rmcm42_connect_mpcc,
};

struct rmcm *dcn60_rmcm_create(struct dc_context *ctx, int inst)
{
	struct dcn42_rmcm *rmcm42 = kzalloc(sizeof(struct dcn42_rmcm), GFP_KERNEL);

	if (!rmcm42)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT rmcm_regs
	dcn_rmcm_regs_init();

	dcn42_rmcm_construct(rmcm42,
		ctx,
		&rmcm_regs,
		&rmcm_shift,
		&rmcm_mask,
		inst);

	/* Override the DCN42 defaults with the DCN60 single-SRAM LUT-bank ops. */
	rmcm42->base.funcs = &dcn60_rmcm_funcs;

	return &rmcm42->base;
}

void dcn60_rmcm_destroy(struct rmcm **rmcm)
{
	if (*rmcm) {
		kfree(TO_DCN42_RMCM(*rmcm));
		*rmcm = NULL;
	}
}
