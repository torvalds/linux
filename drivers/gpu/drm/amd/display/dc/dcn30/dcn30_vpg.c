/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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


#include "dc_bios_types.h"
#include "dcn30_vpg.h"
#include "reg_helper.h"

#define DC_LOGGER \
		vpg3->base.ctx->logger

#define REG(reg)\
	(vpg3->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	vpg3->vpg_shift->field_name, vpg3->vpg_mask->field_name


#define CTX \
	vpg3->base.ctx


void vpg3_update_generic_info_packet(
	struct vpg *vpg,
	uint32_t packet_index,
	const struct dc_info_packet *info_packet,
	bool immediate_update)
{
	struct dcn30_vpg *vpg3 = DCN30_VPG_FROM_VPG(vpg);
	uint32_t i;

	/* TODOFPGA Figure out a proper number for max_retries polling for lock
	 * use 50 for now.
	 */
	uint32_t max_retries = 50;

	if (packet_index > 14)
		ASSERT(0);

	/* poll dig_update_lock is not locked -> asic internal signal
	 * assume otg master lock will unlock it
	 */
	/* REG_WAIT(AFMT_VBI_PACKET_CONTROL, AFMT_GENERIC_LOCK_STATUS,
	 * 		0, 10, max_retries);
	 */

	/* TODO: Check if this is required */
	/* check if HW reading GSP memory */
	REG_WAIT(VPG_GENERIC_STATUS, VPG_GENERIC_CONFLICT_OCCURED,
			0, 10, max_retries);

	/* HW does is not reading GSP memory not reading too long ->
	 * something wrong. clear GPS memory access and notify?
	 * hw SW is writing to GSP memory
	 */
	REG_UPDATE(VPG_GENERIC_STATUS, VPG_GENERIC_CONFLICT_CLR, 1);

	/* choose which generic packet to use */
	REG_UPDATE(VPG_GENERIC_PACKET_ACCESS_CTRL,
			VPG_GENERIC_DATA_INDEX, packet_index*9);

	/* write generic packet header
	 * (4th byte is for GENERIC0 only)
	 */
	REG_SET_4(VPG_GENERIC_PACKET_DATA, 0,
			VPG_GENERIC_DATA_BYTE0, info_packet->hb0,
			VPG_GENERIC_DATA_BYTE1, info_packet->hb1,
			VPG_GENERIC_DATA_BYTE2, info_packet->hb2,
			VPG_GENERIC_DATA_BYTE3, info_packet->hb3);

	/* write generic packet contents
	 * (we never use last 4 bytes)
	 * there are 8 (0-7) mmDIG0_AFMT_GENERIC0_x registers
	 */
	{
		const uint32_t *content =
			(const uint32_t *) &info_packet->sb[0];

		for (i = 0; i < 8; i++) {
			REG_WRITE(VPG_GENERIC_PACKET_DATA, *content++);
		}
	}

	/* atomically update double-buffered GENERIC0 registers in immediate mode
	 * (update at next block_update when block_update_lock == 0).
	 */
	if (immediate_update) {
		switch (packet_index) {
		case 0:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC0_IMMEDIATE_UPDATE, 1);
			break;
		case 1:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC1_IMMEDIATE_UPDATE, 1);
			break;
		case 2:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC2_IMMEDIATE_UPDATE, 1);
			break;
		case 3:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC3_IMMEDIATE_UPDATE, 1);
			break;
		case 4:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC4_IMMEDIATE_UPDATE, 1);
			break;
		case 5:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC5_IMMEDIATE_UPDATE, 1);
			break;
		case 6:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC6_IMMEDIATE_UPDATE, 1);
			break;
		case 7:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC7_IMMEDIATE_UPDATE, 1);
			break;
		case 8:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC8_IMMEDIATE_UPDATE, 1);
			break;
		case 9:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC9_IMMEDIATE_UPDATE, 1);
			break;
		case 10:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC10_IMMEDIATE_UPDATE, 1);
			break;
		case 11:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC11_IMMEDIATE_UPDATE, 1);
			break;
		case 12:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC12_IMMEDIATE_UPDATE, 1);
			break;
		case 13:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC13_IMMEDIATE_UPDATE, 1);
			break;
		case 14:
			REG_UPDATE(VPG_GSP_IMMEDIATE_UPDATE_CTRL,
					VPG_GENERIC14_IMMEDIATE_UPDATE, 1);
			break;
		default:
			break;
		}
	} else {
		switch (packet_index) {
		case 0:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC0_FRAME_UPDATE, 1);
			break;
		case 1:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC1_FRAME_UPDATE, 1);
			break;
		case 2:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC2_FRAME_UPDATE, 1);
			break;
		case 3:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC3_FRAME_UPDATE, 1);
			break;
		case 4:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC4_FRAME_UPDATE, 1);
			break;
		case 5:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC5_FRAME_UPDATE, 1);
			break;
		case 6:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC6_FRAME_UPDATE, 1);
			break;
		case 7:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC7_FRAME_UPDATE, 1);
			break;
		case 8:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC8_FRAME_UPDATE, 1);
			break;
		case 9:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC9_FRAME_UPDATE, 1);
			break;
		case 10:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC10_FRAME_UPDATE, 1);
			break;
		case 11:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC11_FRAME_UPDATE, 1);
			break;
		case 12:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC12_FRAME_UPDATE, 1);
			break;
		case 13:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC13_FRAME_UPDATE, 1);
			break;
		case 14:
			REG_UPDATE(VPG_GSP_FRAME_UPDATE_CTRL,
					VPG_GENERIC14_FRAME_UPDATE, 1);
			break;

		default:
			break;
		}

	}
}

static struct vpg_funcs dcn30_vpg_funcs = {
	.update_generic_info_packet	= vpg3_update_generic_info_packet,
};

void vpg3_construct(struct dcn30_vpg *vpg3,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn30_vpg_registers *vpg_regs,
	const struct dcn30_vpg_shift *vpg_shift,
	const struct dcn30_vpg_mask *vpg_mask)
{
	vpg3->base.ctx = ctx;

	vpg3->base.inst = inst;
	vpg3->base.funcs = &dcn30_vpg_funcs;

	vpg3->regs = vpg_regs;
	vpg3->vpg_shift = vpg_shift;
	vpg3->vpg_mask = vpg_mask;
}
