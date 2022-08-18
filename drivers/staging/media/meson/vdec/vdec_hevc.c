// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 *
 * VDEC_HEVC is a video decoding block that allows decoding of
 * HEVC, VP9
 */

#include <linux/firmware.h>
#include <linux/clk.h>

#include "vdec_1.h"
#include "vdec_helpers.h"
#include "vdec_hevc.h"
#include "hevc_regs.h"
#include "dos_regs.h"

/* AO Registers */
#define AO_RTI_GEN_PWR_SLEEP0	0xe8
#define AO_RTI_GEN_PWR_ISO0	0xec
	#define GEN_PWR_VDEC_HEVC (BIT(7) | BIT(6))
	#define GEN_PWR_VDEC_HEVC_SM1 (BIT(2))

#define MC_SIZE	(4096 * 4)

static int vdec_hevc_load_firmware(struct amvdec_session *sess,
				   const char *fwname)
{
	struct amvdec_core *core = sess->core;
	struct device *dev = core->dev_dec;
	const struct firmware *fw;
	static void *mc_addr;
	static dma_addr_t mc_addr_map;
	int ret;
	u32 i = 100;

	ret = request_firmware(&fw, fwname, dev);
	if (ret < 0)  {
		dev_err(dev, "Unable to request firmware %s\n", fwname);
		return ret;
	}

	if (fw->size < MC_SIZE) {
		dev_err(dev, "Firmware size %zu is too small. Expected %u.\n",
			fw->size, MC_SIZE);
		ret = -EINVAL;
		goto release_firmware;
	}

	mc_addr = dma_alloc_coherent(core->dev, MC_SIZE, &mc_addr_map,
				     GFP_KERNEL);
	if (!mc_addr) {
		ret = -ENOMEM;
		goto release_firmware;
	}

	memcpy(mc_addr, fw->data, MC_SIZE);

	amvdec_write_dos(core, HEVC_MPSR, 0);
	amvdec_write_dos(core, HEVC_CPSR, 0);

	amvdec_write_dos(core, HEVC_IMEM_DMA_ADR, mc_addr_map);
	amvdec_write_dos(core, HEVC_IMEM_DMA_COUNT, MC_SIZE / 4);
	amvdec_write_dos(core, HEVC_IMEM_DMA_CTRL, (0x8000 | (7 << 16)));

	while (i && (readl(core->dos_base + HEVC_IMEM_DMA_CTRL) & 0x8000))
		i--;

	if (i == 0) {
		dev_err(dev, "Firmware load fail (DMA hang?)\n");
		ret = -ENODEV;
	}

	dma_free_coherent(core->dev, MC_SIZE, mc_addr, mc_addr_map);
release_firmware:
	release_firmware(fw);
	return ret;
}

static void vdec_hevc_stbuf_init(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	amvdec_write_dos(core, HEVC_STREAM_CONTROL,
			 amvdec_read_dos(core, HEVC_STREAM_CONTROL) & ~1);
	amvdec_write_dos(core, HEVC_STREAM_START_ADDR, sess->vififo_paddr);
	amvdec_write_dos(core, HEVC_STREAM_END_ADDR,
			 sess->vififo_paddr + sess->vififo_size);
	amvdec_write_dos(core, HEVC_STREAM_RD_PTR, sess->vififo_paddr);
	amvdec_write_dos(core, HEVC_STREAM_WR_PTR, sess->vififo_paddr);
}

/* VDEC_HEVC specific ESPARSER configuration */
static void vdec_hevc_conf_esparser(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;

	/* set vififo_vbuf_rp_sel=>vdec_hevc */
	amvdec_write_dos(core, DOS_GEN_CTRL0, 3 << 1);
	amvdec_write_dos(core, HEVC_STREAM_CONTROL,
			 amvdec_read_dos(core, HEVC_STREAM_CONTROL) | BIT(3));
	amvdec_write_dos(core, HEVC_STREAM_CONTROL,
			 amvdec_read_dos(core, HEVC_STREAM_CONTROL) | 1);
	amvdec_write_dos(core, HEVC_STREAM_FIFO_CTL,
			 amvdec_read_dos(core, HEVC_STREAM_FIFO_CTL) | BIT(29));
}

static u32 vdec_hevc_vififo_level(struct amvdec_session *sess)
{
	return readl_relaxed(sess->core->dos_base + HEVC_STREAM_LEVEL);
}

static int vdec_hevc_stop(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	/* Disable interrupt */
	amvdec_write_dos(core, HEVC_ASSIST_MBOX1_MASK, 0);
	/* Disable firmware processor */
	amvdec_write_dos(core, HEVC_MPSR, 0);

	if (sess->priv)
		codec_ops->stop(sess);

	/* Enable VDEC_HEVC Isolation */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_HEVC_SM1,
				   GEN_PWR_VDEC_HEVC_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   0xc00, 0xc00);

	/* VDEC_HEVC Memories */
	amvdec_write_dos(core, DOS_MEM_PD_HEVC, 0xffffffffUL);

	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC_SM1,
				   GEN_PWR_VDEC_HEVC_SM1);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC, GEN_PWR_VDEC_HEVC);

	clk_disable_unprepare(core->vdec_hevc_clk);
	if (core->platform->revision == VDEC_REVISION_G12A ||
	    core->platform->revision == VDEC_REVISION_SM1)
		clk_disable_unprepare(core->vdec_hevcf_clk);

	return 0;
}

static int vdec_hevc_start(struct amvdec_session *sess)
{
	int ret;
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;

	if (core->platform->revision == VDEC_REVISION_G12A ||
	    core->platform->revision == VDEC_REVISION_SM1) {
		clk_set_rate(core->vdec_hevcf_clk, 666666666);
		ret = clk_prepare_enable(core->vdec_hevcf_clk);
		if (ret)
			return ret;
	}

	clk_set_rate(core->vdec_hevc_clk, 666666666);
	ret = clk_prepare_enable(core->vdec_hevc_clk);
	if (ret) {
		if (core->platform->revision == VDEC_REVISION_G12A ||
		    core->platform->revision == VDEC_REVISION_SM1)
			clk_disable_unprepare(core->vdec_hevcf_clk);
		return ret;
	}

	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_SLEEP0,
				   GEN_PWR_VDEC_HEVC, 0);
	usleep_range(10, 20);

	/* Reset VDEC_HEVC*/
	amvdec_write_dos(core, DOS_SW_RESET3, 0xffffffff);
	amvdec_write_dos(core, DOS_SW_RESET3, 0x00000000);

	amvdec_write_dos(core, DOS_GCLK_EN3, 0xffffffff);

	/* VDEC_HEVC Memories */
	amvdec_write_dos(core, DOS_MEM_PD_HEVC, 0x00000000);

	/* Remove VDEC_HEVC Isolation */
	if (core->platform->revision == VDEC_REVISION_SM1)
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   GEN_PWR_VDEC_HEVC_SM1, 0);
	else
		regmap_update_bits(core->regmap_ao, AO_RTI_GEN_PWR_ISO0,
				   0xc00, 0);

	amvdec_write_dos(core, DOS_SW_RESET3, 0xffffffff);
	amvdec_write_dos(core, DOS_SW_RESET3, 0x00000000);

	vdec_hevc_stbuf_init(sess);

	ret = vdec_hevc_load_firmware(sess, sess->fmt_out->firmware_path);
	if (ret)
		goto stop;

	ret = codec_ops->start(sess);
	if (ret)
		goto stop;

	amvdec_write_dos(core, DOS_SW_RESET3, BIT(12) | BIT(11));
	amvdec_write_dos(core, DOS_SW_RESET3, 0);
	amvdec_read_dos(core, DOS_SW_RESET3);

	amvdec_write_dos(core, HEVC_MPSR, 1);
	/* Let the firmware settle */
	usleep_range(10, 20);

	return 0;

stop:
	vdec_hevc_stop(sess);
	return ret;
}

struct amvdec_ops vdec_hevc_ops = {
	.start = vdec_hevc_start,
	.stop = vdec_hevc_stop,
	.conf_esparser = vdec_hevc_conf_esparser,
	.vififo_level = vdec_hevc_vififo_level,
};
