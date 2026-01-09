// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Etnaviv Project
 */

#include <linux/errno.h>
#include <linux/dev_printk.h>
#include <linux/string.h>
#include <linux/types.h>

#include "etnaviv_buffer.h"
#include "etnaviv_cmdbuf.h"
#include "etnaviv_gpu.h"
#include "state_3d.xml.h"

#include "etnaviv_flop_reset.h"

static int etnaviv_force_flop_reset;
module_param_named(force_flop_reset, etnaviv_force_flop_reset, int, 0);

#define PPU_IMAGE_STRIDE 64
#define PPU_IMAGE_XSIZE 64
#define PPU_IMAGE_YSIZE 6

#define PPU_FLOP_RESET_INSTR_DWORD_COUNT 16

static void etnaviv_emit_flop_reset_state_ppu(struct etnaviv_cmdbuf *cmdbuf,
					      u32 buffer_base, u32 input_offset,
					      u32 output_offset,
					      u32 shader_offset,
					      u32 shader_size,
					      u32 shader_register_count)
{
	CMD_LOAD_STATE(cmdbuf, VIVS_GL_API_MODE, VIVS_GL_API_MODE_OPENCL);
	CMD_SEM(cmdbuf, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);
	CMD_STALL(cmdbuf, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);

	CMD_LOAD_STATES_START(cmdbuf, VIVS_SH_HALTI5_UNIFORMS(0), 4);

	OUT(cmdbuf, buffer_base + input_offset);
	OUT(cmdbuf, PPU_IMAGE_STRIDE);
	OUT(cmdbuf, PPU_IMAGE_XSIZE | (PPU_IMAGE_YSIZE << 16));
	OUT(cmdbuf, 0x444051f0);
	OUT(cmdbuf, 0xffffffff);

	CMD_LOAD_STATES_START(cmdbuf, VIVS_SH_HALTI5_UNIFORMS(4), 4);
	OUT(cmdbuf, buffer_base + output_offset);
	OUT(cmdbuf, PPU_IMAGE_STRIDE);
	OUT(cmdbuf, PPU_IMAGE_XSIZE | (PPU_IMAGE_YSIZE << 16));
	OUT(cmdbuf, 0x444051f0);
	OUT(cmdbuf, 0xffffffff);

	CMD_LOAD_STATE(cmdbuf, VIVS_CL_CONFIG,
		       VIVS_CL_CONFIG_DIMENSIONS(2) |
			       VIVS_CL_CONFIG_VALUE_ORDER(3));
	CMD_LOAD_STATE(cmdbuf, VIVS_VS_ICACHE_INVALIDATE, 0x1f);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_VARYING_NUM_COMPONENTS(0), 0);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_TEMP_REGISTER_CONTROL,
		       shader_register_count);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_SAMPLER_BASE, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_UNIFORM_BASE, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_NEWRANGE_LOW, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_NEWRANGE_HIGH, shader_size / 16);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_INST_ADDR, buffer_base + shader_offset);
	CMD_LOAD_STATE(cmdbuf, VIVS_SH_CONFIG, VIVS_SH_CONFIG_RTNE_ROUNDING);
	CMD_LOAD_STATE(cmdbuf, VIVS_VS_ICACHE_CONTROL,
		       VIVS_VS_ICACHE_CONTROL_ENABLE);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_ICACHE_COUNT, shader_size / 16 - 1);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_INPUT_COUNT, 0x1f01);
	CMD_LOAD_STATE(cmdbuf, VIVS_VS_HALTI5_UNK008A0, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_PA_VS_OUTPUT_COUNT, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_GL_VARYING_TOTAL_COMPONENTS, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_CONTROL_EXT, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_VS_OUTPUT_COUNT, 0x1);
	CMD_LOAD_STATE(cmdbuf, VIVS_GL_HALTI5_SH_SPECIALS, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_PS_ICACHE_PREFETCH, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_CL_UNK00924, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_CL_THREAD_ALLOCATION, 0x1);

	CMD_LOAD_STATE(cmdbuf, VIVS_CL_GLOBAL_WORK_OFFSET_X, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_CL_GLOBAL_WORK_OFFSET_Y, 0x0);
	CMD_LOAD_STATE(cmdbuf, VIVS_CL_GLOBAL_WORK_OFFSET_Z, 0x0);

	CMD_LOAD_STATES_START(cmdbuf, VIVS_CL_WORKGROUP_COUNT_X, 9);
	OUT(cmdbuf, 0xf);
	OUT(cmdbuf, 0x5);
	OUT(cmdbuf, 0xffffffff);
	OUT(cmdbuf, 0x0);
	OUT(cmdbuf, 0x0);
	OUT(cmdbuf, 0x3ff);
	OUT(cmdbuf, 0x0);
	OUT(cmdbuf, 0x4);
	OUT(cmdbuf, 0x1);
	OUT(cmdbuf, 0x0);

	CMD_LOAD_STATE(cmdbuf, VIVS_CL_KICKER, 0xbadabeeb);
	CMD_LOAD_STATE(cmdbuf, VIVS_GL_FLUSH_CACHE,
		       VIVS_GL_FLUSH_CACHE_SHADER_L1 |
			       VIVS_GL_FLUSH_CACHE_UNK10 |
			       VIVS_GL_FLUSH_CACHE_UNK11);
}

static void etnaviv_flop_reset_ppu_fill_input(u32 *buffer, u32 size)
{
	memset32(buffer, 0x01010101, size / 4);
}

static void etnaviv_flop_reset_ppu_set_shader(u8 *dest)
{
	static const u32 inst[PPU_FLOP_RESET_INSTR_DWORD_COUNT] = {
		/* img_load.u8 r1, c0, r0.xy */
		0x78011779,
		0x39000804,
		0x00A90050,
		0x00000000,
		/* img_load.u8 r2, c0, r0.xy */
		0x78021779,
		0x39000804,
		0x00A90050,
		0x00000000,
		/* dp2x8 r1, r1, r2, c3_512 */
		0xB8017145,
		0x390018FC,
		0x01C90140,
		0x40390028,
		/* img_store.u8 r1, c2, r0.xy, r1 */
		0x380007BA,
		0x39001804,
		0x00A90050,
		0x00390018,
	};
	memcpy(dest, inst, sizeof(inst));
}

static const struct etnaviv_flop_reset_entry {
	u16 chip_model;
	u16 revision;
	u32 flags;
} etnaviv_flop_reset_db[] = {
	{
		.chip_model = 0x8000,
		.revision = 0x6205,
	},
};

bool etnaviv_flop_reset_ppu_require(const struct etnaviv_chip_identity *chip_id)
{
	const struct etnaviv_flop_reset_entry *e = etnaviv_flop_reset_db;

	for (int i = 0; i < ARRAY_SIZE(etnaviv_flop_reset_db); ++i, ++e) {
		if (chip_id->model == e->chip_model &&
		    chip_id->revision == e->revision)
			return true;
	}

	if (etnaviv_force_flop_reset) {
		if (!(chip_id->features & chipFeatures_PIPE_3D)) {
			pr_warn("Etnaviv: model: 0x%04x, revision: 0x%04x does not support PIPE_3D\n",
				chip_id->model, chip_id->revision);
			pr_warn("Request to force PPU flop reset ignored.\n");
			return false;
		}

		pr_info("Force PPU flop reset for model: 0x%04x, revision: 0x%04x\n",
			chip_id->model, chip_id->revision);
		return true;
	}

	return false;
}

static const u32 image_data_size = PPU_IMAGE_STRIDE * PPU_IMAGE_YSIZE;
static const u32 output_offset = ALIGN(image_data_size, 64);
static const u32 shader_offset = ALIGN(output_offset + image_data_size, 64);
static const u32 shader_size = PPU_FLOP_RESET_INSTR_DWORD_COUNT * sizeof(u32);
static const u32 shader_register_count = 3;
static const u32 buffer_size = shader_offset + shader_size;

int etnaviv_flop_reset_ppu_init(struct etnaviv_drm_private *priv)
{
	/* Get some space from the ring buffer to put the payload
	 * (input and output image, and shader), we keep this buffer
	 * for the whole life time the driver is bound
	 */
	priv->flop_reset_data_ppu =
		kzalloc(sizeof(*priv->flop_reset_data_ppu), GFP_KERNEL);

	if (!priv->flop_reset_data_ppu)
		return -ENOMEM;

	int ret = etnaviv_cmdbuf_init(priv->cmdbuf_suballoc,
				      priv->flop_reset_data_ppu, buffer_size);
	if (ret) {
		kfree(priv->flop_reset_data_ppu);
		return ret;
	}

	void *buffer_base = priv->flop_reset_data_ppu->vaddr;
	u32 *input_data = (u32 *)buffer_base;
	u8 *shader_data = (u8 *)buffer_base + shader_offset;

	etnaviv_flop_reset_ppu_fill_input(input_data, image_data_size);
	etnaviv_flop_reset_ppu_set_shader(shader_data);

	return 0;
}

void etnaviv_flop_reset_ppu_run(struct etnaviv_gpu *gpu)
{
	struct etnaviv_drm_private *priv = gpu->drm->dev_private;

	if (!priv->flop_reset_data_ppu) {
		dev_err(gpu->dev,
			"Oops: Flop reset data was not initialized, skipping\n");
		return;
	}

	u32 buffer_base = etnaviv_cmdbuf_get_va(priv->flop_reset_data_ppu,
						&gpu->mmu_context->cmdbuf_mapping);

	etnaviv_emit_flop_reset_state_ppu(&gpu->buffer, buffer_base, 0,
					  output_offset, shader_offset,
					  shader_size, shader_register_count);
}
