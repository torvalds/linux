/**
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: chenhengming chm@rock-chips.com
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
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/reset.h>
#include <linux/rockchip/pmu.h>

#include "vpu_iommu_ops.h"
#include "mpp_service.h"
#include "mpp_dev_common.h"
#include "mpp_dev_rkvenc.h"

#define MPP_ALIGN_SIZE	0x1000

#define LINK_TABLE_START	12
#define LINK_TABLE_LEN		128

#define	RKVENC_ENC_START		0x004
#define		RKVENC_LKT_NUM(x)			(((x) & 0xff) << 0)
#define		RKVENC_CMD(x)				(((x) & 0x3) << 8)
#define		RKVENC_CLK_GATE_EN			BIT(16)
#define	RKVENC_SAFE_CLR			0x008
#define	RKVENC_LKT_ADDR			0x00c
#define	RKVENC_INT_EN			0x010
#define		RKVENC_INT_EN_SAFE_CLEAR		BIT(2)
#define		RKVENC_INT_EN_TIMEOUT			BIT(8)
#define	RKVENC_INT_MSK			0x014
#define		RKVENC_INT_MSK_OVERFLOW			BIT(4)
#define		RKVENC_INT_MSK_W_FIFO_FULL		BIT(5)
#define		RKVENC_INT_MSK_W_CHN_ERROR		BIT(6)
#define		RKVENC_INT_MSK_R_CHN_ERROR		BIT(7)
#define		RKVENC_INT_MSK_TIMEOUT			BIT(8)
#define	RKVENC_INT_CLR			0x018
#define	RKVENC_INT_STATUS		0x01c
#define		RKVENC_ONE_FRAME_FINISH			BIT(0)
#define		RKVENC_LINK_TABLE_FINISH		BIT(1)
#define		RKVENC_SAFE_CLEAR_FINISH		BIT(2)
#define		RKVENC_ONE_SLICE_FINISH			BIT(3)
#define		RKVENC_BIT_STREAM_OVERFLOW		BIT(4)
#define		RKVENC_AXI_WRITE_FIFO_FULL		BIT(5)
#define		RKVENC_AXI_WRITE_CHANNEL_ERROR		BIT(6)
#define		RKVENC_AXI_READ_CHANNEL_ERROR		BIT(7)
#define		RKVENC_TIMEOUT_ERROR			BIT(8)
#define RKVENC_INT_ERROR_BITS		((RKVENC_BIT_STREAM_OVERFLOW) |	   \
					 (RKVENC_AXI_WRITE_FIFO_FULL) |	   \
					 (RKVENC_AXI_WRITE_CHANNEL_ERROR) |\
					 (RKVENC_AXI_READ_CHANNEL_ERROR) | \
					 (RKVENC_TIMEOUT_ERROR))
#define	RKVENC_ENC_PIC			0x034
#define		RKVENC_ENC_PIC_NODE_INT_EN		BIT(31)
#define	RKVENC_ENC_WDG			0x038
#define		RKVENC_PPLN_ENC_LMT(x)			(((x) & 0xff) << 0)
#define	RKVENC_OSD_CFG			0x1c0
#define		RKVENC_OSD_PLT_TYPE			BIT(17)
#define		RKVENC_OSD_CLK_SEL_BIT			BIT(16)
#define	RKVENC_STATUS(i)		(0x210 + (4 * (i)))
#define	RKVENC_BSL_STATUS		0x210
#define		RKVENC_BITSTREAM_LENGTH(x)		((x) & 0x7FFFFFF)
#define	RKVENC_ENC_STATUS		0x220
#define		RKVENC_ENC_STATUS_ENC(x)		(((x) >> 0) & 0x3)
#define	RKVENC_LKT_STATUS		0x224
#define		RKVENC_LKT_STATUS_FNUM_ENC(x)		(((x) >> 0) & 0xff)
#define		RKVENC_LKT_STATUS_FNUM_CFG(x)		(((x) >> 8) & 0xff)
#define		RKVENC_LKT_STATUS_FNUM_INT(x)		(((x) >> 16) & 0xff)
#define	RKVENC_OSD_PLT(i)		(0x400 + (4 * (i)))

#define to_rkvenc_ctx(ctx)		\
		container_of(ctx, struct rkvenc_ctx, ictx)
#define to_rkvenc_session(session)	\
		container_of(session, struct rkvenc_session, isession)
#define to_rkvenc_dev(dev)		\
		container_of(dev, struct rockchip_rkvenc_dev, idev)

/*
 * file handle translate information
 */
static const char trans_tbl_rkvenc[] = {
	70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86,
	124, 125, 126, 127, 128, 129, 130, 131
};

static struct mpp_trans_info trans_rkvenc[1] = {
	[0] = {
		.count = sizeof(trans_tbl_rkvenc),
		.table = trans_tbl_rkvenc,
	},
};

static struct mpp_dev_rkvenc_reg mpp_rkvenc_dummy_reg = {
	.enc_rsl = 0x00070007,          /* 64x64 */
	.enc_pic = 0x00001714,          /* h264, qp 30 */
	.enc_wdg = 0x00000002,
	.dtrns_map = 0x00007000,
	.dtrns_cfg = 0x0000007f,
	.src_fmt = 0x00000018,          /* nv12 */
	.src_strd = 0x003f003f,
	.sli_spl = 0x00000004,
	.me_rnge = 0x00002f7b,
	.me_cnst = 0x000e0505,
	.me_ram = 0x000e79ab,
	.rc_qp = 0x07340000,
	.rdo_cfg = 0x00000002,
	.synt_nal = 0x00000017,
	.synt_sps = 0x0000019c,
	.synt_pps = 0x01000d03,
	.synt_sli0 = 0x00000002,
};

static int rockchip_mpp_rkvenc_reset(struct rockchip_mpp_dev *mpp);

/*
 * In order to workaround hw bug which make the first frame run failure with
 * timeout interrupt occur, we make a dummy 64x64 encoding on power on here to
 * cover the hw bug.
 */
static void rockchip_mpp_war_init(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);
	size_t img_width = 64;
	size_t img_height = 64;
	size_t img_y_size = img_width * img_height;
	size_t img_uv_size = img_y_size / 2;
	size_t img_u_size = img_uv_size / 2;
	size_t img_size = img_y_size + img_uv_size;

	enc->war_reg = &mpp_rkvenc_dummy_reg;

	/* 4k align required */
	enc->war_reg->adr_rfpw = enc->war_dma_addr;
	enc->war_reg->adr_srcy = enc->war_reg->adr_rfpw + img_size;
	enc->war_reg->adr_srcu = enc->war_reg->adr_srcy + img_y_size;
	enc->war_reg->adr_srcv = enc->war_reg->adr_srcu + img_u_size;
	enc->war_reg->adr_bsbb = enc->war_reg->adr_srcv + img_u_size;
	enc->war_reg->adr_bsbt = enc->war_reg->adr_bsbb + img_size;
	enc->war_reg->adr_bsbr = enc->war_reg->adr_bsbb;
	enc->war_reg->adr_bsbw = enc->war_reg->adr_bsbb;

	/* 1k align required */
	enc->war_reg->adr_dspw = enc->war_dma_addr + 0x4000;
	enc->war_reg->adr_dspr = enc->war_reg->adr_dspw + 0x400;

	enc->dummy_ctx = kzalloc(sizeof(*enc->dummy_ctx), GFP_KERNEL);
	if (!enc->dummy_ctx)
		return;

	enc->dummy_ctx->ictx.mpp = mpp;
	enc->dummy_ctx->ictx.session = NULL;
	enc->dummy_ctx->mode = RKVENC_MODE_ONEFRAME;
	enc->dummy_ctx->cfg.mode = RKVENC_MODE_ONEFRAME;
	atomic_set(&enc->dummy_ctx_in_used, 0);
	memcpy(enc->dummy_ctx->cfg.elem[0].reg, enc->war_reg,
	       sizeof(*enc->war_reg));
	enc->dummy_ctx->cfg.elem[0].reg_num = sizeof(*enc->war_reg) / 4;
}

static void rockchip_mpp_rkvenc_cfg_palette(struct rockchip_mpp_dev *mpp,
					    struct mpp_session *isession)
{
	struct rkvenc_session *session;
	int i;
	u32 reg;

	mpp_debug_enter();

	if (!isession) {
		mpp_debug(DEBUG_TASK_INFO, "fake ctx, do not cfg palette\n");
		return;
	}
	session = to_rkvenc_session(isession);

	if (!session->palette_valid)
		return;

	reg = mpp_read(mpp, RKVENC_OSD_CFG);
	mpp_write(mpp, reg & (~RKVENC_OSD_CLK_SEL_BIT), RKVENC_OSD_CFG);

	for (i = 0; i < RKVENC_OSD_PLT_LEN; i++)
		mpp_write(mpp, session->palette.plalette[i].elem,
			  RKVENC_OSD_PLT(i));

	mpp_write(mpp, reg | RKVENC_OSD_CLK_SEL_BIT, RKVENC_OSD_CFG);

	mpp_debug_leave();
}

static struct mpp_ctx *rockchip_mpp_rkvenc_init(struct rockchip_mpp_dev *mpp,
						struct mpp_session *session,
						void __user *src, u32 size)
{
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	int i;

	mpp_debug_enter();

	if (!ctx)
		return NULL;

	/* HW defeat workaround start */
	if (!mpp_dev_is_power_on(mpp) && enc->dummy_ctx &&
	    atomic_inc_return(&enc->dummy_ctx_in_used) == 1) {
		mpp_debug(DEBUG_RESET, "add a dummy ctx\n");
		mpp_srv_pending_locked(mpp->srv, &enc->dummy_ctx->ictx);
	}

	mpp_dev_common_ctx_init(mpp, &ctx->ictx);

	ctx->ictx.session = session;
	ctx->mode = RKVENC_MODE_LINKTABLE_FIX;

	size = size > sizeof(ctx->cfg) ? sizeof(ctx->cfg) : size;

	if (copy_from_user(&ctx->cfg, src, size)) {
		mpp_err("error: copy_from_user failed in reg_init\n");
		kfree(ctx);
		return NULL;
	}

	ctx->mode = ctx->cfg.mode;
	if (ctx->mode >= RKVENC_MODE_NUM || ctx->mode == RKVENC_MODE_NONE) {
		mpp_err("Invalid rkvenc running mode %d\n", (int)ctx->mode);
		kfree(ctx);
		return NULL;
	} else if (ctx->mode == RKVENC_MODE_ONEFRAME && ctx->cfg.tbl_num > 1) {
		mpp_err("Configuration miss match, ignore redundant cfg\n");
		ctx->cfg.tbl_num = 1;
	}

	mpp_debug(DEBUG_SET_REG, "tbl num %u, mode %u\n",
		  ctx->cfg.tbl_num, ctx->cfg.mode);

	for (i = 0; i < ctx->cfg.tbl_num; i++) {
		if (mpp_reg_address_translate(mpp, ctx->cfg.elem[i].reg,
					      &ctx->ictx, 0) < 0) {
			mpp_err("error: translate reg address failed.\n");

			if (unlikely(mpp_dev_debug & DEBUG_DUMP_ERR_REG))
				mpp_dump_reg_mem(ctx->cfg.elem[i].reg,
						 ctx->cfg.elem[i].reg_num);

			mpp_dev_common_ctx_deinit(mpp, &ctx->ictx);
			kfree(ctx);

			return NULL;
		}

		mpp_debug(DEBUG_SET_REG, "extra info cnt %u, magic %08x",
			  ctx->cfg.elem[i].ext_inf.cnt,
			  ctx->cfg.elem[i].ext_inf.magic);

		mpp_translate_extra_info(&ctx->ictx, &ctx->cfg.elem[i].ext_inf,
					 ctx->cfg.elem[i].reg);
	}

	mpp_debug_leave();

	return &ctx->ictx;
}

static int rockchip_mpp_rkvenc_reset_init(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp_debug(DEBUG_RESET, "reset init in:\n");
	enc->rst_a = devm_reset_control_get(mpp->dev, "video_a");
	enc->rst_h = devm_reset_control_get(mpp->dev, "video_h");
	enc->rst_v = devm_reset_control_get(mpp->dev, "video_c");

	if (IS_ERR_OR_NULL(enc->rst_a)) {
		mpp_err("No aclk reset resource define\n");
		enc->rst_a = NULL;
	}

	if (IS_ERR_OR_NULL(enc->rst_h)) {
		mpp_err("No hclk reset resource define\n");
		enc->rst_h = NULL;
	}

	if (IS_ERR_OR_NULL(enc->rst_v)) {
		mpp_err("No core reset resource define\n");
		enc->rst_v = NULL;
	}

	return 0;
}

static int rockchip_mpp_rkvenc_reset(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);
	int cnt = 100;

	if (enc->rst_a && enc->rst_h && enc->rst_v) {
		mpp_debug(DEBUG_RESET, "reset in\n");
		mpp_write(mpp, 0, RKVENC_INT_EN);
		mpp_write(mpp, 1, RKVENC_SAFE_CLR);
		while (cnt-- > 0) {
			int status;

			usleep_range(100, 200);
			status = mpp_read(mpp, RKVENC_ENC_STATUS);
			if (status & 4) {
				mpp_debug(DEBUG_RESET, "st_enc %08x\n", status);
				break;
			}
		}
		reset_control_assert(enc->rst_v);
		reset_control_assert(enc->rst_a);
		reset_control_assert(enc->rst_h);

		udelay(1);

		reset_control_deassert(enc->rst_v);
		reset_control_deassert(enc->rst_a);
		reset_control_deassert(enc->rst_h);
		mpp_debug(DEBUG_RESET, "reset out\n");
	}
	return 0;
}

static int rockchip_mpp_rkvenc_prepare(struct rockchip_mpp_dev *mpp)
{
	struct rkvenc_ctx *ctx_curr;
	struct rkvenc_ctx *ctx_ready;
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);
	u32 lkt_status;
	u32 fnum_int;
	u32 fnum_cfg;
	u32 fnum_enc;
	u8 *cpu_addr;
	int i;

	u32 reg = 0;

	mpp_debug_enter();

	if (!mpp_srv_is_running(mpp->srv))
		return 0;

	/* if service running, determine link table mode */
	ctx_curr = to_rkvenc_ctx(mpp_srv_get_current_ctx(mpp->srv));
	ctx_ready = to_rkvenc_ctx(mpp_srv_get_pending_ctx(mpp->srv));

	if (ctx_curr->mode != RKVENC_MODE_LINKTABLE_UPDATE ||
	    ctx_ready->mode != ctx_curr->mode) {
		mpp_debug(DEBUG_TASK_INFO,
			  "link table condition not fulfill\n");
		return -1;
	}

	lkt_status = mpp_read(mpp, RKVENC_LKT_STATUS);
	fnum_int = RKVENC_LKT_STATUS_FNUM_INT(lkt_status);
	fnum_cfg = RKVENC_LKT_STATUS_FNUM_CFG(lkt_status);
	fnum_enc = RKVENC_LKT_STATUS_FNUM_ENC(lkt_status);
	cpu_addr = (u8 *)enc->lkt_cpu_addr + fnum_cfg * LINK_TABLE_LEN * 4;

	mpp_dev_power_on(mpp);

	mpp_debug(DEBUG_GET_REG, "frame number int %u, cfg %u, enc %u\n",
		  fnum_int, fnum_cfg, fnum_enc);

	for (i = 0; i < ctx_ready->cfg.tbl_num; i++) {
		u32 *src = ctx_ready->cfg.elem[i].reg;

		memcpy(cpu_addr + i * LINK_TABLE_LEN * 4,
		       &src[LINK_TABLE_START], LINK_TABLE_LEN * 4);
	}

	reg = RKVENC_CLK_GATE_EN |
		RKVENC_CMD(ctx_curr->mode) |
		RKVENC_LKT_NUM(ctx_ready->cfg.tbl_num);
	mpp_write_relaxed(mpp, reg, RKVENC_ENC_START);

	/* remove from pending queue */
	mpp_dev_common_ctx_deinit(mpp, &ctx_ready->ictx);

	mpp_debug_leave();

	return 0;
}

static int rockchip_mpp_rkvenc_run(struct rockchip_mpp_dev *mpp)
{
	struct rkvenc_ctx *ctx =
			to_rkvenc_ctx(mpp_srv_get_current_ctx(mpp->srv));
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);
	u32 reg;
	int i;

	mpp_debug_enter();

	switch (ctx->mode) {
	case RKVENC_MODE_ONEFRAME:
		{
			u32 *src = ctx->cfg.elem[0].reg;

			for (i = 2; i < (LINK_TABLE_START + LINK_TABLE_LEN); i++)
				mpp_write_relaxed(mpp, src[i], i * 4);

			rockchip_mpp_rkvenc_cfg_palette(mpp, ctx->ictx.session);

			mpp_write_relaxed(mpp, 0x1ff, RKVENC_INT_EN);
			reg = RKVENC_CLK_GATE_EN
				| RKVENC_CMD(1);
			mpp_write(mpp, reg, RKVENC_ENC_START);

			break;
		}
	case RKVENC_MODE_LINKTABLE_FIX:
	case RKVENC_MODE_LINKTABLE_UPDATE:
		{
			for (i = 0; i < ctx->cfg.tbl_num; i++) {
				u32 *src = ctx->cfg.elem[i].reg;

				memcpy(enc->lkt_cpu_addr +
				       i * LINK_TABLE_LEN * 4,
				       &src[LINK_TABLE_START],
				       LINK_TABLE_LEN * 4);
			}

			rockchip_mpp_rkvenc_cfg_palette(mpp, ctx->ictx.session);

			mpp_write_relaxed(mpp,
					  enc->lkt_dma_addr,
					  RKVENC_LKT_ADDR);
			mpp_write_relaxed(mpp, 0xffffffff, RKVENC_INT_EN);

			reg = RKVENC_LKT_NUM(ctx->cfg.tbl_num) |
				RKVENC_CMD(RKVENC_MODE_LINKTABLE_FIX) |
				RKVENC_CLK_GATE_EN;

			mpp_write_relaxed(mpp, reg, RKVENC_ENC_START);

			break;
		}
	default:
		break;
	}

	mpp_debug_leave();

	return 0;
}

static int rockchip_mpp_rkvenc_done(struct rockchip_mpp_dev *mpp)
{
	struct mpp_ctx *ictx = mpp_srv_get_current_ctx(mpp->srv);
	struct rkvenc_ctx *ctx;
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_result *result;
	int i;

	mpp_debug_enter();

	if (IS_ERR_OR_NULL(ictx)) {
		mpp_err("Invaidate context to save result\n");
		return -1;
	}

	ctx = to_rkvenc_ctx(ictx);

	if (enc->irq_status & RKVENC_INT_ERROR_BITS)
		/*
		 * according to war running, if the dummy encoding
		 * running with timeout, we enable a safe clear process,
		 * we reset the ip, and complete the war procedure.
		 */
		atomic_inc(&mpp->reset_request);

	if (ctx == enc->dummy_ctx) {
		mpp_debug(DEBUG_RESET, "war done\n");

		/* for war do not trigger service done process */
		list_del_init(&ictx->status_link);
		atomic_set(&enc->dummy_ctx_in_used, 0);

		/* dummy ctx, do not trigger service to wake up done process */
		return -1;
	}

	result = &ctx->result;
	switch (ctx->mode) {
	case RKVENC_MODE_ONEFRAME:
		result->tbl_num = 1;
		result->elem[0].status = enc->irq_status;
		for (i = 0; i < sizeof(result->elem[0].result) / 4; i++)
			result->elem[0].result[i] =
						    mpp_read(mpp,
							     RKVENC_STATUS(i));
		break;
	case RKVENC_MODE_LINKTABLE_FIX:
	case RKVENC_MODE_LINKTABLE_UPDATE:
		{
			u32 lkt_status = mpp_read(mpp, RKVENC_LKT_STATUS);
			u32 fnum_int = RKVENC_LKT_STATUS_FNUM_INT(lkt_status);
			u32 fnum_cfg = RKVENC_LKT_STATUS_FNUM_CFG(lkt_status);
			u32 fnum_enc = RKVENC_LKT_STATUS_FNUM_ENC(lkt_status);

			u32 *lkt_cpu_addr = (u32 *)enc->lkt_cpu_addr;

			if (unlikely(mpp_dev_debug & DEBUG_DUMP_ERR_REG))
				mpp_dump_reg_mem(lkt_cpu_addr, LINK_TABLE_LEN);

			result->tbl_num = fnum_int;
			for (i = 0; i < fnum_int; i++) {
				result->elem[i].status = enc->irq_status;
				memcpy(result->elem[i].result,
				       &lkt_cpu_addr[i * LINK_TABLE_LEN + 120],
				       sizeof(result->elem[i].result));
				mpp_debug(DEBUG_GET_REG, "stream length %u\n",
					  result->elem[i].result[0]);
			}
			mpp_debug(DEBUG_GET_REG, "frame number %u, %u, %u\n",
				  fnum_int, fnum_cfg, fnum_enc);
			break;
		}
	default:
		break;
	}

	mpp_debug_leave();

	return 0;
}

static int rockchip_mpp_rkvenc_irq(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);

	enc->irq_status = mpp_read(mpp, RKVENC_INT_STATUS);

	mpp_debug_enter();

	if (enc->irq_status == 0)
		return -1;

	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", enc->irq_status);
	mpp_write(mpp, 0xffffffff, RKVENC_INT_CLR);
	if (enc->irq_status & RKVENC_INT_ERROR_BITS) {
		mpp_err("error irq %08x\n", enc->irq_status);
		/* time out error */
		mpp_write(mpp, RKVENC_INT_ERROR_BITS, RKVENC_INT_MSK);
	}

	mpp_debug_leave();

	return 0;
}

static int rockchip_mpp_rkvenc_result(struct rockchip_mpp_dev *mpp,
				      struct mpp_ctx *ictx, u32 __user *dst)
{
	struct rkvenc_ctx *ctx = to_rkvenc_ctx(ictx);
	struct rkvenc_result *result = &ctx->result;
	unsigned long tbl_size = sizeof(result->tbl_num) +
				 sizeof(result->elem[0]) * result->tbl_num;

	switch (ctx->mode) {
	case RKVENC_MODE_ONEFRAME:
	case RKVENC_MODE_LINKTABLE_FIX:
	case RKVENC_MODE_LINKTABLE_UPDATE:
		{
			if (copy_to_user(dst, &ctx->result, tbl_size)) {
				mpp_err("copy result to user failed\n");
				return -1;
			}
			break;
		}
	default:
		mpp_err("invalid context mode %d\n", (int)ctx->mode);
		return -1;
	}

	return 0;
}

static long rockchip_mpp_rkvenc_ioctl(struct mpp_session *isession,
				      unsigned int cmd,
				      unsigned long arg)
{
	struct rkvenc_session *session = to_rkvenc_session(isession);

	mpp_debug_enter();

	switch (cmd) {
	case MPP_DEV_RKVENC_SET_COLOR_PALETTE:
		if (copy_from_user(&session->palette, (void __user *)arg,
				   sizeof(session->palette))) {
			mpp_err("copy palette from user failed\n");
			return -EINVAL;
		}
		session->palette_valid = true;

		break;
	default:
		mpp_err("%s, unknown ioctl cmd %x\n",
			dev_name(isession->mpp->dev), cmd);
		break;
	}

	mpp_debug_leave();

	return 0;
}

static struct mpp_session *mpp_dev_rkvenc_open(struct rockchip_mpp_dev *mpp)
{
	struct rkvenc_session *session = kzalloc(sizeof(*session), GFP_KERNEL);

	mpp_debug_enter();

	if (!session)
		return NULL;

	session->palette_valid = false;

	mpp_debug_leave();

	return &session->isession;
}

static void mpp_dev_rkvenc_release(struct mpp_session *isession)
{
	struct rkvenc_session *session = to_rkvenc_session(isession);

	kfree(session);
}

struct mpp_dev_ops rkvenc_ops = {
	.init = rockchip_mpp_rkvenc_init,
	.prepare = rockchip_mpp_rkvenc_prepare,
	.run = rockchip_mpp_rkvenc_run,
	.done = rockchip_mpp_rkvenc_done,
	.irq = rockchip_mpp_rkvenc_irq,
	.result = rockchip_mpp_rkvenc_result,
	.ioctl = rockchip_mpp_rkvenc_ioctl,
	.open = mpp_dev_rkvenc_open,
	.release = mpp_dev_rkvenc_release,
};

static void rockchip_mpp_rkvenc_power_on(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);

	if (enc->aclk)
		clk_prepare_enable(enc->aclk);
	if (enc->hclk)
		clk_prepare_enable(enc->hclk);
	if (enc->core)
		clk_prepare_enable(enc->core);

	/*
	 * Because hw cannot reset status fully in all its modules, we make a
	 * reset here to make sure the hw status fully reset.
	 */
	rockchip_mpp_rkvenc_reset(mpp);
}

static void rockchip_mpp_rkvenc_power_off(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);

	if (enc->core)
		clk_disable_unprepare(enc->core);
	if (enc->hclk)
		clk_disable_unprepare(enc->hclk);
	if (enc->aclk)
		clk_disable_unprepare(enc->aclk);
}

static int rockchip_mpp_rkvenc_probe(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct mpp_session *session = list_first_entry(&mpp->srv->session,
						       struct mpp_session,
						       list_session);
	int ret;
	size_t tmp;

	enc->idev.ops = &rkvenc_ops;

	enc->lkt_hdl = vpu_iommu_alloc(mpp->iommu_info, session,
				       LINK_TABLE_LEN * 4 * 256,
				       MPP_ALIGN_SIZE);

	if (enc->lkt_hdl < 0) {
		dev_err(mpp->dev, "allocate link table buffer failure\n");
		return -1;
	}

	ret = vpu_iommu_map_iommu(mpp->iommu_info, session,
				  enc->lkt_hdl, &enc->lkt_dma_addr, &tmp);

	if (ret < 0) {
		dev_err(mpp->dev, "get link table dma_addr failed\n");
		goto fail;
	}

	enc->lkt_cpu_addr = vpu_iommu_map_kernel(mpp->iommu_info,
						 session, enc->lkt_hdl);

	/*
	 * buffer for workaround context running, include input picture, output
	 * stream, reconstruction picture. we set the output stream buffer to 1
	 * time picture size, so the total buffer size is 3 times picture size,
	 * 64 * 64 * 3 / 2 * 3 = 4.5 * 4k.
	 */
	enc->war_hdl = vpu_iommu_alloc(mpp->iommu_info, session,
				       MPP_ALIGN_SIZE * 5,
				       MPP_ALIGN_SIZE);
	if (enc->war_hdl < 0) {
		dev_err(mpp->dev, "allocate workaround buffer failure\n");
		goto fail;
	}

	ret = vpu_iommu_map_iommu(mpp->iommu_info, session,
				  enc->war_hdl, &enc->war_dma_addr, &tmp);

	if (ret < 0) {
		dev_err(mpp->dev, "get war dma_addr failed\n");
		goto fail;
	}

	rockchip_mpp_war_init(mpp);

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

	enc->core = devm_clk_get(mpp->dev, "clk_core");
	if (IS_ERR_OR_NULL(enc->core)) {
		dev_err(mpp->dev, "failed on clk_get core\n");
		goto fail;
	}

	rockchip_mpp_rkvenc_reset_init(mpp);

	return 0;

fail:
	kfree(enc->dummy_ctx);

	if (enc->war_hdl >= 0) {
		vpu_iommu_unmap_iommu(mpp->iommu_info,
				      session, enc->war_hdl);
		vpu_iommu_free(mpp->iommu_info, session, enc->war_hdl);
	}
	if (enc->lkt_cpu_addr)
		vpu_iommu_unmap_kernel(mpp->iommu_info, session, enc->lkt_hdl);
	if (enc->lkt_hdl >= 0) {
		vpu_iommu_unmap_iommu(mpp->iommu_info,
				      session, enc->lkt_hdl);
		vpu_iommu_free(mpp->iommu_info, session, enc->lkt_hdl);
	}

	return -1;
}

static void rockchip_mpp_rkvenc_remove(struct rockchip_mpp_dev *mpp)
{
	struct rockchip_rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct mpp_session *session = list_first_entry(&mpp->srv->session,
						       struct mpp_session,
						       list_session);

	vpu_iommu_unmap_kernel(mpp->iommu_info, session, enc->lkt_hdl);
	vpu_iommu_unmap_iommu(mpp->iommu_info,
			      session, enc->lkt_hdl);
	vpu_iommu_free(mpp->iommu_info, session, enc->lkt_hdl);

	vpu_iommu_unmap_iommu(mpp->iommu_info,
			      session, enc->war_hdl);
	vpu_iommu_free(mpp->iommu_info, session, enc->war_hdl);

	kfree(enc->dummy_ctx);
}

const struct rockchip_mpp_dev_variant rkvenc_variant = {
	.data_len = sizeof(struct rockchip_rkvenc_dev),
	.reg_len = 140,
	.trans_info = trans_rkvenc,
	.hw_probe = rockchip_mpp_rkvenc_probe,
	.hw_remove = rockchip_mpp_rkvenc_remove,
	.power_on = rockchip_mpp_rkvenc_power_on,
	.power_off = rockchip_mpp_rkvenc_power_off,
	.reset = rockchip_mpp_rkvenc_reset,
};
EXPORT_SYMBOL(rkvenc_variant);

