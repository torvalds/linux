/**
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 *	   Alpha Lin, alpha.lin@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/pmu.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "vpu_iommu_ops.h"
#include "mpp_dev_common.h"
#include "mpp_dev_vepu.h"
#include "mpp_service.h"

#define to_vepu_ctx(ctx)		\
		container_of(ctx, struct vepu_ctx, ictx)
#define to_vepu_dev(dev)		\
		container_of(dev, struct rockchip_vepu_dev, idev)

#define VEPU_REG_INTERRUPT		0x1b4
#define VEPU_REG_ENC_START		0x19c
#define		VEPU_ENC_GET_FORMAT(x)		(((x) >> 4) & 0x3)
#define		VEPU_ENC_FMT_VP8E		1
#define		VEPU_ENC_ENABLE			BIT(0)

/*
 * file handle translate information
 */
static const char trans_tbl_default[] = {
	77, 78, 56, 57, 63, 64, 48, 49, 50, 81
};

static const char trans_tbl_vp8e[] = {
	77, 78, 56, 57, 63, 64, 48, 49, 50, 76, 106, 108, 81, 80, 44, 45, 27
};

static struct mpp_trans_info trans_vepu[2] = {
	[0] = {
		.count = sizeof(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[1] = {
		.count = sizeof(trans_tbl_vp8e),
		.table = trans_tbl_vp8e,
	},
};

static struct mpp_ctx *rockchip_mpp_vepu_init(struct rockchip_mpp_dev *mpp,
					      struct mpp_session *session,
					      void __user *src, u32 size)
{
	struct vepu_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	u32 reg_len;
	u32 extinf_len;
	u32 fmt = 0;
	int trans_idx = 0;
	u32 dwsize = size / sizeof(u32);

	mpp_debug_enter();

	if (!ctx)
		return NULL;

	mpp_dev_common_ctx_init(mpp, &ctx->ictx);

	ctx->ictx.session = session;

	reg_len = dwsize > ROCKCHIP_VEPU_REG_LEN ?
		  ROCKCHIP_VEPU_REG_LEN : dwsize;
	extinf_len = dwsize > reg_len ? (dwsize - reg_len) * 4 : 0;

	if (copy_from_user(ctx->reg, src, reg_len * 4)) {
		mpp_err("error: copy_from_user failed in reg_init\n");
		kfree(ctx);
		return NULL;
	}

	if (extinf_len > 0) {
		u32 ext_cpy = min_t(size_t, extinf_len, sizeof(ctx->ext_inf));

		if (copy_from_user(&ctx->ext_inf, (u8 *)src + reg_len,
				   ext_cpy)) {
			mpp_err("copy_from_user failed when extra info\n");
			kfree(ctx);
			return NULL;
		}
	}

	fmt = VEPU_ENC_GET_FORMAT(ctx->reg[VEPU_REG_ENC_START / 4]);
	if (fmt == VEPU_ENC_FMT_VP8E)
		trans_idx = 1;

	if (mpp_reg_address_translate(mpp, ctx->reg, &ctx->ictx,
				      trans_idx) < 0) {
		mpp_err("error: translate reg address failed.\n");

		if (unlikely(mpp_dev_debug & DEBUG_DUMP_ERR_REG))
			mpp_dump_reg_mem(ctx->reg, ROCKCHIP_VEPU_REG_LEN);

		mpp_dev_common_ctx_deinit(mpp, &ctx->ictx);
		kfree(ctx);

		return NULL;
	}

	mpp_debug(DEBUG_SET_REG, "extra info cnt %u, magic %08x",
		  ctx->ext_inf.cnt, ctx->ext_inf.magic);

	mpp_translate_extra_info(&ctx->ictx, &ctx->ext_inf, ctx->reg);

	mpp_debug_leave();

	return &ctx->ictx;
}

static int rockchip_mpp_vepu_reset_init(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_vepu_dev *enc = to_vepu_dev(mpp);

	mpp_debug(DEBUG_RESET, "reset init in:\n");
	enc->rst_a = devm_reset_control_get(mpp->dev, "video_a");
	enc->rst_h = devm_reset_control_get(mpp->dev, "video_h");

	if (IS_ERR_OR_NULL(enc->rst_a)) {
		mpp_err("No aclk reset resource define\n");
		enc->rst_a = NULL;
	}

	if (IS_ERR_OR_NULL(enc->rst_h)) {
		mpp_err("No hclk reset resource define\n");
		enc->rst_h = NULL;
	}

	return 0;
}

static int rockchip_mpp_vepu_reset(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_vepu_dev *enc = to_vepu_dev(mpp);

	if (enc->rst_a && enc->rst_h) {
		mpp_debug(DEBUG_RESET, "reset in\n");
		reset_control_assert(enc->rst_a);
		reset_control_assert(enc->rst_h);

		udelay(1);

		reset_control_deassert(enc->rst_a);
		reset_control_deassert(enc->rst_h);
		mpp_debug(DEBUG_RESET, "reset out\n");
	}
	return 0;
}

static int rockchip_mpp_vepu_run(struct rockchip_mpp_dev *mpp)
{
	struct vepu_ctx *ctx =
			       to_vepu_ctx(mpp_srv_get_current_ctx(mpp->srv));
	struct rockchip_vepu_dev *enc = to_vepu_dev(mpp);
	int i;

	mpp_debug_enter();

	/*
	 * before encoding running, we have to switch grf ctrl bit to ensure
	 * ip inner-sram controlled by vepu
	 */
#ifdef CONFIG_MFD_SYSCON
	if (enc->grf) {
		u32 raw;
		u32 bits = BIT(enc->mode_bit);

		regmap_read(enc->grf, enc->mode_ctrl, &raw);
		regmap_write(enc->grf, enc->mode_ctrl,
			     (raw & (~bits)) | (bits << 16));
	}
#endif

	/*
	 * NOTE: encoder need to setup mode first
	 */
	mpp_write(mpp,
		  ctx->reg[VEPU_REG_ENC_START / 4] & (~VEPU_ENC_ENABLE),
		  VEPU_REG_ENC_START);

	for (i = 0; i < ROCKCHIP_VEPU_REG_LEN; i++) {
		if (i * 4 != VEPU_REG_ENC_START)
			mpp_write_relaxed(mpp, ctx->reg[i], i * 4);
	}

	mpp_write(mpp, ctx->reg[VEPU_REG_ENC_START / 4], VEPU_REG_ENC_START);

	mpp_debug_leave();

	return 0;
}

static int rockchip_mpp_vepu_done(struct rockchip_mpp_dev *mpp)
{
	struct mpp_ctx *ictx = mpp_srv_get_current_ctx(mpp->srv);
	struct vepu_ctx *ctx;
	struct rockchip_vepu_dev *enc = to_vepu_dev(mpp);
	int i;

	mpp_debug_enter();

	if (IS_ERR_OR_NULL(ictx)) {
		mpp_err("Invaidate context to save result\n");
		return -1;
	}

	ctx = to_vepu_ctx(ictx);

	for (i = 0; i < ROCKCHIP_VEPU_REG_LEN; i++)
		ctx->reg[i] = mpp_read(mpp, i * 4);

	ctx->reg[VEPU_REG_INTERRUPT / 4] = enc->irq_status;

	mpp_debug_leave();

	return 0;
}

static int rockchip_mpp_vepu_irq(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_vepu_dev *enc = to_vepu_dev(mpp);

	enc->irq_status = mpp_read(mpp, VEPU_REG_INTERRUPT);

	mpp_debug_enter();

	if (enc->irq_status == 0)
		return -1;

	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", enc->irq_status);
	mpp_write(mpp, 0, VEPU_REG_INTERRUPT);

	mpp_debug_leave();

	return 0;
}

static int rockchip_mpp_vepu_result(struct rockchip_mpp_dev *mpp,
				    struct mpp_ctx *ictx, u32 __user *dst)
{
	struct vepu_ctx *ctx = to_vepu_ctx(ictx);

	if (copy_to_user(dst, ctx->reg, ROCKCHIP_VEPU_REG_LEN * 4)) {
		mpp_err("copy_to_user failed\n");
		return -1;
	}

	return 0;
}

struct mpp_dev_ops vepu_ops = {
	.init = rockchip_mpp_vepu_init,
	.run = rockchip_mpp_vepu_run,
	.done = rockchip_mpp_vepu_done,
	.irq = rockchip_mpp_vepu_irq,
	.result = rockchip_mpp_vepu_result,
};

static void rockchip_mpp_vepu_power_on(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_vepu_dev *enc = to_vepu_dev(mpp);

	if (enc->aclk)
		clk_prepare_enable(enc->aclk);
	if (enc->hclk)
		clk_prepare_enable(enc->hclk);
	if (enc->cclk)
		clk_prepare_enable(enc->cclk);
}

static void rockchip_mpp_vepu_power_off(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_vepu_dev *enc = to_vepu_dev(mpp);

	if (enc->hclk)
		clk_disable_unprepare(enc->hclk);
	if (enc->aclk)
		clk_disable_unprepare(enc->aclk);
	if (enc->cclk)
		clk_disable_unprepare(enc->cclk);
}

static int rockchip_mpp_vepu_probe(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_vepu_dev *enc = to_vepu_dev(mpp);
	struct device_node *np = mpp->dev->of_node;

	enc->idev.ops = &vepu_ops;

	enc->aclk = devm_clk_get(mpp->dev, "aclk_vcodec");
	if (IS_ERR_OR_NULL(enc->aclk)) {
		dev_err(mpp->dev, "failed on clk_get aclk\n");
		goto fail;
	}

	enc->hclk = devm_clk_get(mpp->dev, "hclk_vcodec");
	if (IS_ERR_OR_NULL(enc->hclk)) {
		dev_err(mpp->dev, "failed on clk_get hclk\n");
		goto fail;
	}

	enc->cclk = devm_clk_get(mpp->dev, "clk_core");
	if (IS_ERR_OR_NULL(enc->cclk)) {
		dev_err(mpp->dev, "failed on clk_get cclk\n");
		goto fail;
	}

	if (of_property_read_bool(np, "mode_ctrl")) {
		of_property_read_u32(np, "mode_bit", &enc->mode_bit);
		of_property_read_u32(np, "mode_ctrl", &enc->mode_ctrl);

#ifdef COFNIG_MFD_SYSCON
		enc->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
		if (IS_ERR_OR_NULL(enc->grf)) {
			enc->grf = NULL;
			mpp_err("can't find vpu grf property\n");
			goto fail;
		}
#endif
	}

	rockchip_mpp_vepu_reset_init(mpp);

	return 0;

fail:
	return -1;
}

static void rockchip_mpp_vepu_remove(struct rockchip_mpp_dev *mpp)
{
}

const struct rockchip_mpp_dev_variant vepu_variant = {
	.data_len = sizeof(struct rockchip_vepu_dev),
	.reg_len = ROCKCHIP_VEPU_REG_LEN,
	.trans_info = trans_vepu,
	.mmu_dev_dts_name = NULL,
	.hw_probe = rockchip_mpp_vepu_probe,
	.hw_remove = rockchip_mpp_vepu_remove,
	.power_on = rockchip_mpp_vepu_power_on,
	.power_off = rockchip_mpp_vepu_power_off,
	.reset = rockchip_mpp_vepu_reset,
};
EXPORT_SYMBOL(vepu_variant);
