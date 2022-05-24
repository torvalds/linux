// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 *
 * author:
 *	Yandong Lin, yandong.lin@rock-chips.com
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/proc_fs.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <soc/rockchip/rockchip_dvbm.h>

#include "rockchip_dvbm.h"

#define RK_DVBM		"rk_dvbm"

unsigned int dvbm_debug;
module_param(dvbm_debug, uint, 0644);
MODULE_PARM_DESC(dvbm_debug, "bit switch for dvbm debug information");

static struct dvbm_ctx *g_ctx;

#define DVBM_DEBUG	0x00000001
#define DVBM_DEBUG_IRQ	0x00000002
#define DVBM_DEBUG_REG	0x00000004
#define DVBM_DEBUG_DUMP	0x00000008
#define DVBM_DEBUG_FRM	0x00000010

#define dvbm_debug(fmt, args...)				\
	do {							\
		if (unlikely(dvbm_debug & (DVBM_DEBUG)))	\
			pr_info(fmt, ##args);			\
	} while (0)

#define dvbm_debug_reg(fmt, args...)				\
	do {							\
		if (unlikely(dvbm_debug & (DVBM_DEBUG_REG)))	\
			pr_info(fmt, ##args);			\
	} while (0)

#define dvbm_debug_irq(fmt, args...)				\
	do {							\
		if (unlikely(dvbm_debug & (DVBM_DEBUG_IRQ)))	\
			pr_info(fmt, ##args);			\
	} while (0)

#define dvbm_debug_dump(fmt, args...)				\
	do {							\
		if (unlikely(dvbm_debug & (DVBM_DEBUG_DUMP)))	\
			pr_info(fmt, ##args);			\
	} while (0)

#define dvbm_debug_frm(fmt, args...)				\
	do {							\
		if (unlikely(dvbm_debug & (DVBM_DEBUG_FRM)))	\
			pr_info(fmt, ##args);			\
	} while (0)

#define dvbm_err(fmt, args...)	\
	pr_err(fmt, ##args)

enum dvbm_flow {
	ISP_CFG		= 1,
	ISP_CONNECT	= 2,
	VEPU_CFG	= 3,
	VEPU_CONNECT	= 4,
};
/* dvbm status reg bit value define */
#define BUF_OVERFLOW		BIT(0)
#define RESYNC_FINISH		BIT(1)
#define ISP_CNCT_TIMEOUT	BIT(2)
#define VEPU_CNCT_TIMEOUT	BIT(3)
#define VEPU_HANDSHAKE_TIMEOUT	BIT(4)
#define ISP_CNCT		BIT(5)
#define ISP_DISCNCT		BIT(6)
#define VEPU_CNCT		BIT(7)
#define VEPU_DISCNCT		BIT(8)

/* dvbm reg addr define */
#define DVBM_VERSION	0x0
#define DVBM_ISP_CNCT	0x4
#define DVBM_VEPU_CNCT	0x8
/* cfg regs */
#define DVBM_CFG	0xC
#define DVBM_WDG_CFG0	0x10
#define DVBM_WDG_CFG1	0x14
#define DVBM_WDG_CFG2	0x18
/* interrupt regs */
#define DVBM_INT_EN	0x1c
#define DVBM_INT_MSK	0x20
#define DVBM_INT_CLR	0x24
#define DVBM_INT_ST	0x28
/* addr regs */
#define DVBM_YBUF_BOT	0x2c
#define DVBM_YBUF_TOP	0x30
#define DVBM_YBUF_SADR	0x34
#define DVBM_YBUF_LSTD	0x38
#define DVBM_YBUF_FSTD	0x3c
#define DVBM_CBUF_BOT	0x40
#define DVBM_CBUF_TOP	0x44
#define DVBM_CBUF_SADR	0x48
#define DVBM_CBUF_LSTD	0x4c
#define DVBM_CBUF_FSTD	0x50
#define DVBM_AFUL_THDY	0x54
#define DVBM_AFUL_THDC	0x58
#define DVBM_OVFL_THDY	0x5c
#define DVBM_OVFL_THDC	0x60
/* status regs */
#define DVBM_ST		0x80
#define DVBM_OVFL_ST	0x84

#define DVBM_REG_OFFSET 0x2c

#define SOFT_DVBM 1
#define UPDATE_LINE_CNT 0

static void rk_dvbm_set_reg(struct dvbm_ctx *ctx, u32 offset, u32 val)
{
	if (!SOFT_DVBM) {
		dvbm_debug_reg("write reg[%d] 0x%x = 0x%08x\n", offset >> 2, offset, val);
		writel(val, ctx->reg_base + offset);
	}
}

static u32 rk_dvbm_read_reg(struct dvbm_ctx *ctx, u32 offset)
{
	u32 val = 0;

	if (!SOFT_DVBM) {
		val = readl(ctx->reg_base + offset);
		dvbm_debug_reg("read reg[%d] 0x%x = 0x%08x\n", offset >> 2, offset, val);
	}
	return val;
}

static struct dvbm_ctx *port_to_ctx(struct dvbm_port *port)
{
	struct dvbm_ctx *ctx = NULL;

	if (IS_ERR_OR_NULL(port))
		return g_ctx;
	if (port->dir == DVBM_ISP_PORT)
		ctx = container_of(port, struct dvbm_ctx, port_isp);
	else if (port->dir == DVBM_VEPU_PORT)
		ctx = container_of(port, struct dvbm_ctx, port_vepu);

	return ctx;
}

static void dvbm2enc_callback(struct dvbm_ctx *ctx, enum dvbm_cb_event event, void *arg)
{
	struct dvbm_cb *callback = &ctx->vepu_cb;
	dvbm_callback cb = callback->cb;

	if (!ctx->port_vepu.linked)
		return;
	if (cb)
		cb(callback->ctx, event, arg);
}

static void rk_dvbm_dump_regs(struct dvbm_ctx *ctx)
{
	u32 start = ctx->dump_s;//0x80;
	u32 end = ctx->dump_e;//0xb8;
	u32 i;
	dvbm_debug_dump("=== %s ===\n", __func__);
	for (i = start; i <= end; i += 4)
		dvbm_debug_dump("reg[0x%0x] = 0x%08x\n", i, readl(ctx->reg_base + i));
	dvbm_debug_dump("=== %s ===\n", __func__);
}

static int rk_dvbm_clk_on(struct dvbm_ctx *ctx)
{
	int ret = 0;

	if (ctx->clk)
		ret = clk_prepare_enable(ctx->clk);
	if (ret)
		dev_err(ctx->dev, "clk on failed\n");
	return ret;
}

static int rk_dvbm_clk_off(struct dvbm_ctx *ctx)
{
	if (ctx->clk)
		clk_disable_unprepare(ctx->clk);
	return 0;
}

static void init_isp_infos(struct dvbm_ctx *ctx)
{
	ctx->isp_frm_start = 0;
	ctx->isp_frm_end = 0;
	ctx->isp_frm_time = 0;
}

static void rk_dvbm_show_time(struct dvbm_ctx *ctx)
{
	ktime_t time = ktime_get();

	if (ctx->isp_frm_time)
		dvbm_debug("isp frame start[%d : %d] times %lld us\n",
			   ctx->isp_frm_start, ctx->isp_frm_end,
			   ktime_us_delta(time, ctx->isp_frm_time));
	ctx->isp_frm_time = time;
}

static void rk_dvbm_update_isp_frm_info(struct dvbm_ctx *ctx, u32 line_cnt)
{
#if UPDATE_LINE_CNT
	struct dvbm_isp_frm_info *frm_info = &ctx->isp_frm_info;

	frm_info->line_cnt = ALIGN(line_cnt, 32);
	dvbm_debug_frm("dvbm frame %d line %d\n", frm_info->frame_cnt, frm_info->line_cnt);
	dvbm2enc_callback(ctx, DVBM_VEPU_NOTIFY_FRM_INFO, frm_info);
#endif
}

static int rk_dvbm_setup_iobuf(struct dvbm_ctx *ctx)
{
	u32 *data;
	u32 i;
	struct rk_dvbm_base *addr_base = &ctx->regs.addr_base;
	struct dvbm_isp_cfg_t *cfg = &ctx->isp_cfg;

	addr_base->ybuf_bot = cfg->dma_addr + cfg->ybuf_bot;
	addr_base->ybuf_top = cfg->dma_addr + cfg->ybuf_top;
	addr_base->ybuf_sadr = cfg->dma_addr + cfg->ybuf_bot;
	addr_base->ybuf_fstd = cfg->ybuf_fstd;
	addr_base->ybuf_lstd = cfg->ybuf_lstd;

	addr_base->cbuf_bot = cfg->dma_addr + cfg->cbuf_bot;
	addr_base->cbuf_top = cfg->dma_addr + cfg->cbuf_top;
	addr_base->cbuf_sadr = cfg->dma_addr + cfg->cbuf_bot;
	addr_base->cbuf_fstd = cfg->cbuf_fstd;
	addr_base->cbuf_lstd = cfg->cbuf_lstd;

	addr_base->aful_thdy = cfg->ybuf_lstd;
	addr_base->aful_thdc = cfg->ybuf_lstd;
	addr_base->oful_thdy = cfg->ybuf_lstd;
	addr_base->oful_thdc = cfg->ybuf_lstd;

	ctx->isp_max_lcnt = cfg->ybuf_fstd / cfg->ybuf_lstd;
	ctx->wrap_line = (cfg->ybuf_top - cfg->ybuf_bot) / cfg->ybuf_lstd;
	ctx->isp_frm_info.frame_cnt = 0;
	ctx->isp_frm_info.line_cnt = 0;
	ctx->isp_frm_info.max_line_cnt = ALIGN(ctx->isp_max_lcnt, 32);
	ctx->isp_frm_info.wrap_line = ctx->wrap_line;
	dvbm_debug("dma_addr 0x%08x y_lstd %d y_fstd %d\n",
		   cfg->dma_addr, cfg->ybuf_lstd, cfg->ybuf_fstd);
	dvbm_debug("ybot 0x%x top 0x%x cbuf bot 0x%x top 0x%x\n",
		   addr_base->ybuf_bot, addr_base->ybuf_top,
		   addr_base->cbuf_bot, addr_base->cbuf_top);

	data = (u32 *)addr_base;
	for (i = 0; i < sizeof(struct rk_dvbm_base) / sizeof(u32); i++)
		rk_dvbm_set_reg(ctx, i * sizeof(u32) + DVBM_REG_OFFSET, data[i]);

	for (i = 1; i < 65536; i++)
		if (!((addr_base->ybuf_fstd * i) % (cfg->ybuf_top - cfg->ybuf_bot)))
			break;
	ctx->loopcnt = i;
	return 0;
}

static void rk_dvbm_reg_init(struct dvbm_ctx *ctx)
{
	struct rk_dvbm_regs *reg = &ctx->regs;
	u32 *val = (u32 *)reg;

	reg->int_en.buf_ovfl               = 1;
	reg->int_en.isp_cnct               = 1;
	reg->int_en.vepu_cnct              = 1;
	reg->int_en.vepu_discnct           = 1;
	reg->int_en.isp_discnct            = 1;
	reg->int_en.resync_finish          = 1;
	reg->int_en.isp_cnct_timeout       = 1;
	reg->int_en.vepu_cnct_timeout      = 1;
	reg->int_en.vepu_handshake_timeout = 1;

	reg->dvbm_cfg.fmt                         = 0;
	reg->dvbm_cfg.auto_resyn                  = 0;
	reg->dvbm_cfg.ignore_vepu_cnct_ack        = 0;
	reg->dvbm_cfg.start_point_after_vepu_cnct = 0;

	reg->wdg_cfg0.wdg_isp_cnct_timeout       = 0xfffff;
	reg->wdg_cfg1.wdg_vepu_cnct_timeout      = 0xfffff;
	reg->wdg_cfg2.wdg_vepu_handshake_timeout = 0xfffff;

	rk_dvbm_set_reg(ctx, DVBM_WDG_CFG0, val[DVBM_WDG_CFG0 >> 2]);
	rk_dvbm_set_reg(ctx, DVBM_WDG_CFG1, val[DVBM_WDG_CFG1 >> 2]);
	rk_dvbm_set_reg(ctx, DVBM_WDG_CFG2, val[DVBM_WDG_CFG2 >> 2]);
	rk_dvbm_set_reg(ctx, DVBM_CFG, val[DVBM_CFG >> 2]);
	rk_dvbm_set_reg(ctx, DVBM_INT_EN, val[DVBM_INT_EN >> 2]);
}

struct dvbm_port *rk_dvbm_get_port(struct platform_device *pdev,
				   enum dvbm_port_dir dir)
{
	struct dvbm_ctx *ctx = NULL;
	struct dvbm_port *port = NULL;

	if (WARN_ON(!pdev))
		return NULL;

	ctx = (struct dvbm_ctx *)platform_get_drvdata(pdev);
	WARN_ON(!ctx);
	dvbm_debug("%s dir %d\n", __func__, dir);
	if (dir == DVBM_ISP_PORT)
		port = &ctx->port_isp;
	else if (dir == DVBM_VEPU_PORT)
		port = &ctx->port_vepu;

	return port;
}
EXPORT_SYMBOL(rk_dvbm_get_port);

int rk_dvbm_put(struct dvbm_port *port)
{
	struct dvbm_ctx *ctx = NULL;

	if (WARN_ON(!port))
		return -EINVAL;

	ctx = port_to_ctx(port);

	if (!ctx)
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL(rk_dvbm_put);

int rk_dvbm_link(struct dvbm_port *port)
{
	struct dvbm_ctx *ctx;
	enum dvbm_port_dir dir;
	struct rk_dvbm_regs *reg;
	int ret = 0;

	if (WARN_ON(!port))
		return -EINVAL;

	ctx = port_to_ctx(port);
	dir = port->dir;
	reg = &ctx->regs;

	if (dir == DVBM_ISP_PORT) {
		if (port->linked) {
			rk_dvbm_unlink(port);
			udelay(5);
		}
		reg->isp_cnct.isp_cnct = 1;
		rk_dvbm_set_reg(ctx, DVBM_ISP_CNCT, 0x1);
	} else if (dir == DVBM_VEPU_PORT) {
		if (!port->linked) {
			reg->vepu_cnct.vepu_cnct = 1;
			rk_dvbm_set_reg(ctx, DVBM_VEPU_CNCT, 0x1);
		}
		port->linked = 1;
		dvbm_debug_dump("=== vepu link ===\n");
		rk_dvbm_dump_regs(ctx);
		dvbm_debug_dump("=== vepu link ===\n");
	}

	dvbm_debug("%s connect frm_cnt[%d : %d]\n",
		   dir == DVBM_ISP_PORT ? "isp" : "vepu",
		   ctx->isp_frm_start, ctx->isp_frm_end);

	return ret;
}
EXPORT_SYMBOL(rk_dvbm_link);

int rk_dvbm_unlink(struct dvbm_port *port)
{
	struct dvbm_ctx *ctx;
	enum dvbm_port_dir dir;
	struct rk_dvbm_regs *reg;

	if (WARN_ON(!port))
		return -EINVAL;

	ctx = port_to_ctx(port);
	dir = port->dir;
	reg = &ctx->regs;

	if (dir == DVBM_ISP_PORT) {
		reg->isp_cnct.isp_cnct = 0;
		rk_dvbm_set_reg(ctx, DVBM_ISP_CNCT, 0);
	} else if (dir == DVBM_VEPU_PORT) {
		reg->vepu_cnct.vepu_cnct = 0;
		port->linked = 0;
		rk_dvbm_set_reg(ctx, DVBM_VEPU_CNCT, 0);
		if (!ctx->regs.dvbm_cfg.auto_resyn) {
			u32 connect = 0;

			dvbm2enc_callback(ctx, DVBM_VEPU_REQ_CONNECT, &connect);
		}
	}
	dvbm_debug("%s disconnect\n", dir == DVBM_ISP_PORT ? "isp" : "vepu");

	return 0;
}
EXPORT_SYMBOL(rk_dvbm_unlink);

int rk_dvbm_set_cb(struct dvbm_port *port, struct dvbm_cb *cb)
{
	struct dvbm_ctx *ctx;
	enum dvbm_port_dir dir;

	if (WARN_ON(!port) || WARN_ON(!cb))
		return -EINVAL;

	ctx = port_to_ctx(port);
	dir = port->dir;

	if (dir == DVBM_ISP_PORT) {

	} else if (dir == DVBM_VEPU_PORT) {
		ctx->vepu_cb.cb = cb->cb;
		ctx->vepu_cb.ctx = cb->ctx;
	}

	return 0;
}
EXPORT_SYMBOL(rk_dvbm_set_cb);

static void rk_dvbm_update_next_adr(struct dvbm_ctx *ctx)
{
	u32 frame_cnt = ctx->isp_frm_start;
	struct dvbm_isp_cfg_t *isp_cfg = &ctx->isp_cfg;
	struct dvbm_addr_cfg *vepu_cfg = &ctx->vepu_cfg;
	u32 y_wrap_size = isp_cfg->ybuf_top - isp_cfg->ybuf_bot;
	u32 c_wrap_size = isp_cfg->cbuf_top - isp_cfg->cbuf_bot;
	u32 s_off;

	frame_cnt = (frame_cnt + 1) % (ctx->loopcnt);
	s_off = (frame_cnt * isp_cfg->ybuf_fstd) % y_wrap_size;
	vepu_cfg->ybuf_sadr = isp_cfg->dma_addr + isp_cfg->ybuf_bot + s_off;

	s_off = (frame_cnt * isp_cfg->cbuf_fstd) % c_wrap_size;
	vepu_cfg->cbuf_sadr = isp_cfg->dma_addr + isp_cfg->cbuf_bot + s_off;
}

int rk_dvbm_ctrl(struct dvbm_port *port, enum dvbm_cmd cmd, void *arg)
{
	struct dvbm_ctx *ctx;
	struct rk_dvbm_regs *reg;

	if ((cmd < DVBM_ISP_CMD_BASE) || (cmd > DVBM_VEPU_CMD_BUTT)) {
		dvbm_err("%s input cmd invalid\n", __func__);
		return -EINVAL;
	}

	ctx = port_to_ctx(port);
	reg = &ctx->regs;

	switch (cmd) {
	case DVBM_ISP_SET_CFG: {
		struct dvbm_isp_cfg_t *cfg = (struct dvbm_isp_cfg_t *)arg;

		memcpy(&ctx->isp_cfg, cfg, sizeof(struct dvbm_isp_cfg_t));
		rk_dvbm_setup_iobuf(ctx);
		init_isp_infos(ctx);
		rk_dvbm_update_next_adr(ctx);
	} break;
	case DVBM_ISP_FRM_START: {
		rk_dvbm_update_isp_frm_info(ctx, 0);
		rk_dvbm_show_time(ctx);
	} break;
	case DVBM_ISP_FRM_END: {
		u32 line_cnt = ctx->isp_max_lcnt;

		ctx->isp_frm_end = *(u32 *)arg;
		/* wrap frame_cnt 0 - 255 */
		ctx->isp_frm_info.frame_cnt = (ctx->isp_frm_start + 1) % 256;
		rk_dvbm_update_next_adr(ctx);
		rk_dvbm_update_isp_frm_info(ctx, line_cnt);
		ctx->isp_frm_start++;
		dvbm_debug("isp frame end[%d : %d]\n", ctx->isp_frm_start, ctx->isp_frm_end);
	} break;
	case DVBM_ISP_FRM_QUARTER: {
		u32 line_cnt;

		line_cnt = ctx->isp_max_lcnt >> 2;
		rk_dvbm_update_isp_frm_info(ctx, line_cnt);
	} break;
	case DVBM_ISP_FRM_HALF: {
		u32 line_cnt;

		line_cnt = ctx->isp_max_lcnt >> 1;
		rk_dvbm_update_isp_frm_info(ctx, line_cnt);
	} break;
	case DVBM_ISP_FRM_THREE_QUARTERS: {
		u32 line_cnt;

		line_cnt = (ctx->isp_max_lcnt >> 2) * 3;
		rk_dvbm_update_isp_frm_info(ctx, line_cnt);
	} break;
	case DVBM_VEPU_GET_ADR: {
		struct dvbm_addr_cfg *dvbm_adr = (struct dvbm_addr_cfg *)arg;
		struct rk_dvbm_base *addr_base = &reg->addr_base;

		dvbm_adr->ybuf_top = addr_base->ybuf_top;
		dvbm_adr->ybuf_bot = addr_base->ybuf_bot;
		dvbm_adr->cbuf_top = addr_base->cbuf_top;
		dvbm_adr->cbuf_bot = addr_base->cbuf_bot;
		dvbm_adr->cbuf_sadr = ctx->vepu_cfg.cbuf_sadr;
		dvbm_adr->ybuf_sadr = ctx->vepu_cfg.ybuf_sadr;
		dvbm_adr->overflow = ctx->isp_frm_info.line_cnt >= ctx->wrap_line;
		dvbm_adr->frame_id = ctx->isp_frm_info.frame_cnt;
		dvbm_adr->line_cnt = ctx->isp_frm_info.line_cnt;
	} break;
	case DVBM_VEPU_GET_FRAME_INFO: {
		memcpy(arg, &ctx->isp_frm_info, sizeof(struct dvbm_isp_frm_info));
	} break;
	case DVBM_VEPU_SET_RESYNC: {
		reg->dvbm_cfg.auto_resyn = *(u32 *)arg;
		dev_info(ctx->dev, "change resync %s\n",
			 reg->dvbm_cfg.auto_resyn ? "auto" : "soft");
		rk_dvbm_set_reg(ctx, DVBM_CFG, ((u32 *)&reg->dvbm_cfg)[0]);
	} break;
	case DVBM_VEPU_SET_CFG: {
		struct dvbm_vepu_cfg *cfg = (struct dvbm_vepu_cfg *)arg;

		reg->dvbm_cfg.auto_resyn = cfg->auto_resyn;
		reg->dvbm_cfg.ignore_vepu_cnct_ack = cfg->ignore_vepu_cnct_ack;
		reg->dvbm_cfg.start_point_after_vepu_cnct = cfg->start_point_after_vepu_cnct;

		rk_dvbm_set_reg(ctx, DVBM_CFG, ((u32 *)&reg->dvbm_cfg)[0]);
	} break;
	case DVBM_VEPU_DUMP_REGS: {
		rk_dvbm_dump_regs(ctx);
	} break;
	default: {
	} break;
	}

	return 0;
}
EXPORT_SYMBOL(rk_dvbm_ctrl);

static void dvbm_check_irq(struct dvbm_ctx *ctx)
{
	u32 irq_st = ctx->irq_status;
	u32 cur_st = ctx->dvbm_status;

	if (irq_st & ISP_CNCT) {
		dvbm_debug_irq("%s isp connect success! st 0x%08x\n",
			       __func__, cur_st);
		ctx->port_isp.linked = 1;
	}
	if (irq_st & ISP_DISCNCT) {
		dvbm_debug_irq("%s isp disconnect success!\n", __func__);
		ctx->port_isp.linked = 0;
	}
	if (irq_st & VEPU_CNCT) {
		dvbm_debug_irq("%s vepu connect success! st 0x%08x\n",
			       __func__, cur_st);
		ctx->port_vepu.linked = 1;
	}
	if (irq_st & VEPU_DISCNCT) {
		dvbm_debug_irq("%s vepu disconnect success! st 0x%08x\n", __func__, cur_st);
		ctx->port_vepu.linked = 0;
	}
	if (irq_st & BUF_OVERFLOW) {
		dvbm_debug_irq("%s buf overflow st 0x%08x auto_resync %d ignore %d\n",
			       __func__, cur_st, ctx->regs.dvbm_cfg.auto_resyn, ctx->ignore_ovfl);

		if (!ctx->regs.dvbm_cfg.auto_resyn && !ctx->ignore_ovfl)
			rk_dvbm_unlink(&ctx->port_vepu);
	}
	if (irq_st & (ISP_CNCT_TIMEOUT | VEPU_CNCT_TIMEOUT))
		rk_dvbm_dump_regs(ctx);
}

static irqreturn_t rk_dvbm_irq(int irq, void *param)
{
	struct dvbm_ctx *ctx = param;
	u32 irq_st = 0;
	u32 cur_st = 0;

	if (ctx->reg_base) {
		/* read irq st */
		irq_st = rk_dvbm_read_reg(ctx, DVBM_INT_ST);
		cur_st = rk_dvbm_read_reg(ctx, DVBM_ST);
		if (irq_st & BUF_OVERFLOW) {
			dvbm_debug_dump("=== dvbm overflow! dump reg st: 0x%08x===\n", irq_st);
			rk_dvbm_dump_regs(ctx);
			dvbm2enc_callback(ctx, DVBM_VEPU_NOTIFY_DUMP, NULL);
			dvbm_debug_dump("=== dvbm overflow! dump reg end===\n");
		}
		/* clr irq */
		rk_dvbm_set_reg(ctx, DVBM_INT_CLR, irq_st);
		rk_dvbm_set_reg(ctx, DVBM_INT_ST, 0);
	}
	ctx->irq_status = irq_st;
	ctx->dvbm_status = cur_st;

	dvbm_debug_irq("%s irq status 0x%08x\n", __func__, irq_st);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rk_dvbm_isr(int irq, void *param)
{
	struct dvbm_ctx *ctx = param;

	dvbm_check_irq(ctx);

	return IRQ_HANDLED;
}

static int rk_dvbm_probe(struct platform_device *pdev)
{
	int ret;
	struct dvbm_ctx *ctx = NULL;
	struct device *dev = &pdev->dev;
	struct resource *res = NULL;

	dev_info(dev, "probe start\n");
	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	dev_info(dev, "dvbm ctx %p\n", ctx);

	ctx->dev = dev;

	atomic_set(&ctx->isp_ref, 0);
	atomic_set(&ctx->vepu_ref, 0);
	ctx->port_isp.dir = DVBM_ISP_PORT;
	ctx->port_vepu.dir = DVBM_VEPU_PORT;

	platform_set_drvdata(pdev, ctx);

	pm_runtime_enable(dev);

	/* get irq */
	ctx->irq = platform_get_irq(pdev, 0);
	if (ctx->irq < 0) {
		dev_err(&pdev->dev, "no interrupt resource found\n");
		ret = -ENODEV;
		goto failed;
	}
	/* get mem resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		ret = -ENODEV;
		goto failed;
	}

	ctx->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR_OR_NULL(ctx->reg_base)) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		ret = -ENODEV;
		goto failed;
	}

	ctx->clk = devm_clk_get(ctx->dev, "clk_core");
	if (IS_ERR_OR_NULL(ctx->clk)) {
		dev_err(dev, "clk_get failed for resource %pR\n", res);
		ret = -ENODEV;
		goto failed;
	}
	ctx->rst = devm_reset_control_get(ctx->dev, "dvbm_rst");
	if (IS_ERR_OR_NULL(ctx->rst)) {
		dev_err(dev, "clk_rst failed for resource %pR\n", res);
		ret = -ENODEV;
		goto failed;
	}
	if (!SOFT_DVBM) {
		ret = pm_runtime_get_sync(dev);
		if (ret)
			dev_err(dev, "pm get failed!\n");
		ret = rk_dvbm_clk_on(ctx);
		if (ret)
			goto failed;
	}
	g_ctx = ctx;
	rk_dvbm_reg_init(ctx);
	ctx->ignore_ovfl = 1;
	ctx->dump_s = 0x80;
	ctx->dump_e = 0xb8;
	ret = devm_request_threaded_irq(dev, ctx->irq,
					rk_dvbm_irq, rk_dvbm_isr,
					IRQF_ONESHOT, dev_name(dev), ctx);
	if (ret) {
		dev_err(dev, "register interrupter failed\n");
		goto failed;
	}
	dev_info(dev, "probe success\n");

	return 0;

failed:
	pm_runtime_disable(dev);

	return ret;
}

static int rk_dvbm_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_info(dev, "remove device\n");
	if (!SOFT_DVBM) {
		rk_dvbm_clk_off(g_ctx);
		pm_runtime_put(dev);
	}
	pm_runtime_disable(dev);

	return 0;
}

static const struct of_device_id rk_dvbm_dt_ids[] = {
	{
		.compatible = "rockchip,rk-dvbm",
	},
	{ },
};

static struct platform_driver rk_dvbm_driver = {
	.probe = rk_dvbm_probe,
	.remove = rk_dvbm_remove,
	.driver = {
		.name = "rk_dvbm",
		.of_match_table = of_match_ptr(rk_dvbm_dt_ids),
	},
};

static int __init rk_dvbm_init(void)
{
	return platform_driver_register(&rk_dvbm_driver);
}

static __exit void rk_dvbm_exit(void)
{
	platform_driver_unregister(&rk_dvbm_driver);
}

subsys_initcall(rk_dvbm_init);
module_exit(rk_dvbm_exit);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Yandong Lin yandong.lin@rock-chips.com");
MODULE_DESCRIPTION("Rockchip dvbm driver");
