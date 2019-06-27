/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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


#include "reg_helper.h"
#include "resource.h"
#include "mcif_wb.h"
#include "dcn20_mmhubbub.h"


#define REG(reg)\
	mcif_wb20->mcif_wb_regs->reg

#define CTX \
	mcif_wb20->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mcif_wb20->mcif_wb_shift->field_name, mcif_wb20->mcif_wb_mask->field_name

#define MCIF_ADDR(addr) (((unsigned long long)addr & 0xffffffffff) + 0xFE) >> 8
#define MCIF_ADDR_HIGH(addr) (unsigned long long)addr >> 40

/* wbif programming guide:
 * 1. set up wbif parameter:
 *    unsigned long long   luma_address[4];       //4 frame buffer
 *    unsigned long long   chroma_address[4];
 *    unsigned int	   luma_pitch;
 *    unsigned int	   chroma_pitch;
 *    unsigned int         warmup_pitch=0x10;     //256B align, the page size is 4KB when it is 0x10
 *    unsigned int	   slice_lines;           //slice size
 *    unsigned int         time_per_pixel;        // time per pixel, in ns
 *    unsigned int         arbitration_slice;     // 0: 512 bytes 1: 1024 bytes 2: 2048 Bytes
 *    unsigned int         max_scaled_time;       // used for QOS generation
 *    unsigned int         swlock=0x0;
 *    unsigned int         cli_watermark[4];      //4 group urgent watermark
 *    unsigned int         pstate_watermark[4];   //4 group pstate watermark
 *    unsigned int         sw_int_en;             // Software interrupt enable, frame end and overflow
 *    unsigned int         sw_slice_int_en;       // slice end interrupt enable
 *    unsigned int         sw_overrun_int_en;     // overrun error interrupt enable
 *    unsigned int         vce_int_en;            // VCE interrupt enable, frame end and overflow
 *    unsigned int         vce_slice_int_en;      // VCE slice end interrupt enable, frame end and overflow
 *
 * 2. configure wbif register
 *    a. call mmhubbub_config_wbif()
 *
 * 3. Enable wbif
 *    call set_wbif_bufmgr_enable();
 *
 * 4. wbif_dump_status(), option, for debug purpose
 *    the bufmgr status can show the progress of write back, can be used for debug purpose
 */

static void mmhubbub2_config_mcif_buf(struct mcif_wb *mcif_wb,
		struct mcif_buf_params *params,
		unsigned int dest_height)
{
	struct dcn20_mmhubbub *mcif_wb20 = TO_DCN20_MMHUBBUB(mcif_wb);

	/* sw lock buffer0~buffer3, default is 0 */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_LOCK, params->swlock);

	/* buffer address for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_Y, MCIF_WB_BUF_1_ADDR_Y, MCIF_ADDR(params->luma_address[0]));
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_Y_HIGH, MCIF_WB_BUF_1_ADDR_Y_HIGH, MCIF_ADDR_HIGH(params->luma_address[0]));
	/* right eye sub-buffer address offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_Y_OFFSET, MCIF_WB_BUF_1_ADDR_Y_OFFSET, 0);

	/* buffer address for Chroma in planar mode (unused in packing mode) */
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_C, MCIF_WB_BUF_1_ADDR_C, MCIF_ADDR(params->chroma_address[0]));
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_C_HIGH, MCIF_WB_BUF_1_ADDR_C_HIGH, MCIF_ADDR_HIGH(params->chroma_address[0]));
	/* right eye offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_1_ADDR_C_OFFSET, MCIF_WB_BUF_1_ADDR_C_OFFSET, 0);

	/* buffer address for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_Y, MCIF_WB_BUF_2_ADDR_Y, MCIF_ADDR(params->luma_address[1]));
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_Y_HIGH, MCIF_WB_BUF_2_ADDR_Y_HIGH, MCIF_ADDR_HIGH(params->luma_address[1]));
	/* right eye sub-buffer address offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_Y_OFFSET, MCIF_WB_BUF_2_ADDR_Y_OFFSET, 0);

	/* buffer address for Chroma in planar mode (unused in packing mode) */
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_C, MCIF_WB_BUF_2_ADDR_C, MCIF_ADDR(params->chroma_address[1]));
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_C_HIGH, MCIF_WB_BUF_2_ADDR_C_HIGH, MCIF_ADDR_HIGH(params->chroma_address[1]));
	/* right eye offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_2_ADDR_C_OFFSET, MCIF_WB_BUF_2_ADDR_C_OFFSET, 0);

	/* buffer address for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_Y, MCIF_WB_BUF_3_ADDR_Y, MCIF_ADDR(params->luma_address[2]));
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_Y_HIGH, MCIF_WB_BUF_3_ADDR_Y_HIGH, MCIF_ADDR_HIGH(params->luma_address[2]));
	/* right eye sub-buffer address offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_Y_OFFSET, MCIF_WB_BUF_3_ADDR_Y_OFFSET, 0);

	/* buffer address for Chroma in planar mode (unused in packing mode) */
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_C, MCIF_WB_BUF_3_ADDR_C, MCIF_ADDR(params->chroma_address[2]));
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_C_HIGH, MCIF_WB_BUF_3_ADDR_C_HIGH, MCIF_ADDR_HIGH(params->chroma_address[2]));
	/* right eye offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_3_ADDR_C_OFFSET, MCIF_WB_BUF_3_ADDR_C_OFFSET, 0);

	/* buffer address for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_Y, MCIF_WB_BUF_4_ADDR_Y, MCIF_ADDR(params->luma_address[3]));
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_Y_HIGH, MCIF_WB_BUF_4_ADDR_Y_HIGH, MCIF_ADDR_HIGH(params->luma_address[3]));
	/* right eye sub-buffer address offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_Y_OFFSET, MCIF_WB_BUF_4_ADDR_Y_OFFSET, 0);

	/* buffer address for Chroma in planar mode (unused in packing mode) */
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_C, MCIF_WB_BUF_4_ADDR_C, MCIF_ADDR(params->chroma_address[3]));
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_C_HIGH, MCIF_WB_BUF_4_ADDR_C_HIGH, MCIF_ADDR_HIGH(params->chroma_address[3]));
	/* right eye offset for packing mode or Luma in planar mode */
	REG_UPDATE(MCIF_WB_BUF_4_ADDR_C_OFFSET, MCIF_WB_BUF_4_ADDR_C_OFFSET, 0);

	/* setup luma & chroma size
	 * should be enough to contain a whole frame Luma data,
	 * the programmed value is frame buffer size [27:8], 256-byte aligned
	 */
	REG_UPDATE(MCIF_WB_BUF_LUMA_SIZE, MCIF_WB_BUF_LUMA_SIZE, (params->luma_pitch>>8) * dest_height);
	REG_UPDATE(MCIF_WB_BUF_CHROMA_SIZE, MCIF_WB_BUF_CHROMA_SIZE, (params->chroma_pitch>>8) * dest_height);

	/* enable address fence */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUF_ADDR_FENCE_EN, 1);

	/* setup pitch, the programmed value is [15:8], 256B align */
	REG_UPDATE_2(MCIF_WB_BUF_PITCH, MCIF_WB_BUF_LUMA_PITCH, params->luma_pitch >> 8,
			MCIF_WB_BUF_CHROMA_PITCH, params->chroma_pitch >> 8);

	/* Set pitch for MC cache warm up mode */
	/* Pitch is 256 bytes aligned. The default pitch is 4K */
	/* default is 0x10 */
	REG_UPDATE(MCIF_WB_WARM_UP_CNTL, MCIF_WB_PITCH_SIZE_WARMUP, params->warmup_pitch);
}

static void mmhubbub2_config_mcif_arb(struct mcif_wb *mcif_wb,
		struct mcif_arb_params *params)
{
	struct dcn20_mmhubbub *mcif_wb20 = TO_DCN20_MMHUBBUB(mcif_wb);

	/* Programmed by the video driver based on the CRTC timing (for DWB) */
	REG_UPDATE(MCIF_WB_ARBITRATION_CONTROL, MCIF_WB_TIME_PER_PIXEL, params->time_per_pixel);

	/* Programming dwb watermark */
	/* Watermark to generate urgent in MCIF_WB_CLI, value is determined by MCIF_WB_CLI_WATERMARK_MASK. */
	/* Program in ns. A formula will be provided in the pseudo code to calculate the value. */
	REG_UPDATE(MCIF_WB_SCLK_CHANGE, MCIF_WB_CLI_WATERMARK_MASK, 0x0);
	/* urgent_watermarkA */
	REG_UPDATE(MCIF_WB_WATERMARK, MCIF_WB_CLI_WATERMARK,  params->cli_watermark[0]);
	REG_UPDATE(MCIF_WB_SCLK_CHANGE, MCIF_WB_CLI_WATERMARK_MASK, 0x1);
	/* urgent_watermarkB */
	REG_UPDATE(MCIF_WB_WATERMARK, MCIF_WB_CLI_WATERMARK,  params->cli_watermark[1]);
	REG_UPDATE(MCIF_WB_SCLK_CHANGE, MCIF_WB_CLI_WATERMARK_MASK, 0x2);
	/* urgent_watermarkC */
	REG_UPDATE(MCIF_WB_WATERMARK, MCIF_WB_CLI_WATERMARK,  params->cli_watermark[2]);
	REG_UPDATE(MCIF_WB_SCLK_CHANGE, MCIF_WB_CLI_WATERMARK_MASK, 0x3);
	/* urgent_watermarkD */
	REG_UPDATE(MCIF_WB_WATERMARK, MCIF_WB_CLI_WATERMARK,  params->cli_watermark[3]);

	/* Programming nb pstate watermark */
	/* nbp_state_change_watermarkA */
	REG_UPDATE(MCIF_WB_NB_PSTATE_CONTROL, NB_PSTATE_CHANGE_WATERMARK_MASK, 0x0);
	REG_UPDATE(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
			NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->pstate_watermark[0]);
	/* nbp_state_change_watermarkB */
	REG_UPDATE(MCIF_WB_NB_PSTATE_CONTROL, NB_PSTATE_CHANGE_WATERMARK_MASK, 0x1);
	REG_UPDATE(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
			NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->pstate_watermark[1]);
	/* nbp_state_change_watermarkC */
	REG_UPDATE(MCIF_WB_NB_PSTATE_CONTROL, NB_PSTATE_CHANGE_WATERMARK_MASK, 0x2);
	REG_UPDATE(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
			NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->pstate_watermark[2]);
	/* nbp_state_change_watermarkD */
	REG_UPDATE(MCIF_WB_NB_PSTATE_CONTROL, NB_PSTATE_CHANGE_WATERMARK_MASK, 0x3);
	REG_UPDATE(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
			NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->pstate_watermark[3]);

	/* max_scaled_time */
	REG_UPDATE(MULTI_LEVEL_QOS_CTRL, MAX_SCALED_TIME_TO_URGENT, params->max_scaled_time);

	/* slice_lines */
	REG_UPDATE(MCIF_WB_BUFMGR_VCE_CONTROL, MCIF_WB_BUFMGR_SLICE_SIZE, params->slice_lines-1);

	/* Set arbitration unit for Luma/Chroma */
	/* arb_unit=2 should be chosen for more efficiency */
	/* Arbitration size, 0: 512 bytes 1: 1024 bytes 2: 2048 Bytes */
	REG_UPDATE(MCIF_WB_ARBITRATION_CONTROL, MCIF_WB_CLIENT_ARBITRATION_SLICE,  params->arbitration_slice);
}

void mmhubbub2_config_mcif_irq(struct mcif_wb *mcif_wb,
		struct mcif_irq_params *params)
{
	struct dcn20_mmhubbub *mcif_wb20 = TO_DCN20_MMHUBBUB(mcif_wb);

	/* Set interrupt mask */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_INT_EN, params->sw_int_en);
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_SLICE_INT_EN, params->sw_slice_int_en);
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_OVERRUN_INT_EN,  params->sw_overrun_int_en);

	REG_UPDATE(MCIF_WB_BUFMGR_VCE_CONTROL, MCIF_WB_BUFMGR_VCE_INT_EN,  params->vce_int_en);
	REG_UPDATE(MCIF_WB_BUFMGR_VCE_CONTROL, MCIF_WB_BUFMGR_VCE_SLICE_INT_EN,  params->vce_slice_int_en);
}

void mmhubbub2_enable_mcif(struct mcif_wb *mcif_wb)
{
	struct dcn20_mmhubbub *mcif_wb20 = TO_DCN20_MMHUBBUB(mcif_wb);

	/* Enable Mcifwb */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_ENABLE, 1);
}

void mmhubbub2_disable_mcif(struct mcif_wb *mcif_wb)
{
	struct dcn20_mmhubbub *mcif_wb20 = TO_DCN20_MMHUBBUB(mcif_wb);

	/* disable buffer manager */
	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_ENABLE, 0);
}

/* set which group of pstate watermark to use and set wbif watermark change request */
/*
static void mmhubbub2_wbif_watermark_change_req(struct mcif_wb *mcif_wb, unsigned int wm_set)
{
	struct dcn20_mmhubbub *mcif_wb20 = TO_DCN20_MMHUBBUB(mcif_wb);
	uint32_t change_req;

	REG_GET(SMU_WM_CONTROL, MCIF_WB0_WM_CHG_REQ, &change_req);
	change_req = (change_req == 0) ? 1 : 0;
	REG_UPDATE(SMU_WM_CONTROL, MCIF_WB0_WM_CHG_SEL, wm_set);
	REG_UPDATE(SMU_WM_CONTROL, MCIF_WB0_WM_CHG_REQ, change_req);
}
*/
/* Set watermark change interrupt disable bit */
/*
static void mmhubbub2_set_wbif_watermark_change_int_disable(struct mcif_wb *mcif_wb, unsigned int ack_int_dis)
{
	struct dcn20_mmhubbub *mcif_wb20 = TO_DCN20_MMHUBBUB(mcif_wb);

	REG_UPDATE(SMU_WM_CONTROL, MCIF_WB0_WM_CHG_ACK_INT_DIS, ack_int_dis);
}
*/
/* Read watermark change interrupt status */
/*
unsigned int mmhubbub2_get_wbif_watermark_change_int_status(struct mcif_wb *mcif_wb)
{
	struct dcn20_mmhubbub *mcif_wb20 = TO_DCN20_MMHUBBUB(mcif_wb);
	uint32_t irq_status;

	REG_GET(SMU_WM_CONTROL, MCIF_WB0_WM_CHG_ACK_INT_STATUS, &irq_status);
	return irq_status;
}
*/

void mcifwb2_dump_frame(struct mcif_wb *mcif_wb,
		struct mcif_buf_params *mcif_params,
		enum dwb_scaler_mode out_format,
		unsigned int dest_width,
		unsigned int dest_height,
		struct mcif_wb_frame_dump_info *dump_info,
		unsigned char *luma_buffer,
		unsigned char *chroma_buffer,
		unsigned char *dest_luma_buffer,
		unsigned char *dest_chroma_buffer)
{
	struct dcn20_mmhubbub *mcif_wb20 = TO_DCN20_MMHUBBUB(mcif_wb);

	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_LOCK, 0xf);

	memcpy(dest_luma_buffer,   luma_buffer,   mcif_params->luma_pitch * dest_height);
	memcpy(dest_chroma_buffer, chroma_buffer, mcif_params->chroma_pitch * dest_height / 2);

	REG_UPDATE(MCIF_WB_BUFMGR_SW_CONTROL, MCIF_WB_BUFMGR_SW_LOCK, 0x0);

	dump_info->format	= out_format;
	dump_info->width	= dest_width;
	dump_info->height	= dest_height;
	dump_info->luma_pitch	= mcif_params->luma_pitch;
	dump_info->chroma_pitch	= mcif_params->chroma_pitch;
	dump_info->size		= dest_height * (mcif_params->luma_pitch + mcif_params->chroma_pitch);
}

const struct mcif_wb_funcs dcn20_mmhubbub_funcs = {
	.enable_mcif		= mmhubbub2_enable_mcif,
	.disable_mcif		= mmhubbub2_disable_mcif,
	.config_mcif_buf	= mmhubbub2_config_mcif_buf,
	.config_mcif_arb	= mmhubbub2_config_mcif_arb,
	.config_mcif_irq	= mmhubbub2_config_mcif_irq,
	.dump_frame		= mcifwb2_dump_frame,
};

void dcn20_mmhubbub_construct(struct dcn20_mmhubbub *mcif_wb20,
		struct dc_context *ctx,
		const struct dcn20_mmhubbub_registers *mcif_wb_regs,
		const struct dcn20_mmhubbub_shift *mcif_wb_shift,
		const struct dcn20_mmhubbub_mask *mcif_wb_mask,
		int inst)
{
	mcif_wb20->base.ctx = ctx;

	mcif_wb20->base.inst = inst;
	mcif_wb20->base.funcs = &dcn20_mmhubbub_funcs;

	mcif_wb20->mcif_wb_regs = mcif_wb_regs;
	mcif_wb20->mcif_wb_shift = mcif_wb_shift;
	mcif_wb20->mcif_wb_mask = mcif_wb_mask;
}
