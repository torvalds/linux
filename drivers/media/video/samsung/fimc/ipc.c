/* linux/drivers/media/video/samsung/fimc/ipc.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Driver file for Samsung IPC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/memory.h>
#include <plat/clock.h>
#include <plat/regs-ipc.h>

#include "fimc-ipc.h"
#include "ipc_table.h"

struct ipc_control *ipc;

void shadow_update(void)
{
	writel(S3C_IPC_SHADOW_UPDATE_ENABLE,
					ipc->regs + S3C_IPC_SHADOW_UPDATE);
}

void ipc_enable_postprocessing(u32 onoff)
{
	u32 cfg = readl(ipc->regs + S3C_IPC_BYPASS);

	if (!onoff)
		cfg |= S3C_IPC_PP_BYPASS_DISABLE;
	else
		cfg &= S3C_IPC_PP_BYPASS_ENABLE;

	writel(cfg, ipc->regs + S3C_IPC_BYPASS);

	shadow_update();
}

void ipc_enable(u32 onoff)
{
	u32 cfg = readl(ipc->regs + S3C_IPC_ENABLE);

	if (!onoff)
		cfg &= S3C_IPC_OFF;
	else
		cfg |= S3C_IPC_ON;

	writel(cfg, ipc->regs + S3C_IPC_ENABLE);
}

void ipc_reset(void)
{
	u32 cfg;

	do {
		cfg = readl(ipc->regs + S3C_IPC_SRESET);
	} while ((cfg & S3C_IPC_SRESET_MASK));

	writel(S3C_IPC_SRESET_ENABLE, ipc->regs + S3C_IPC_SRESET);
}

void ipc_start(void)
{
	ipc_enable_postprocessing(ON);
	ipc_enable(ON);
}

void ipc_stop(void)
{
	ipc_enable_postprocessing(OFF);
	ipc_enable(OFF);
	ipc_reset();

#if 1
	/* Jonghun Han
	*  After clk_disalbe, we cannot set register although clk is enable.
	*  Must be tested by System Application part.
	*/
	clk_disable(ipc->clk);
#endif
}

void ipc_field_id_control(enum ipc_field_id id)
{
	writel(id, ipc->regs + S3C_IPC_FIELD_ID);
	shadow_update();
}

void ipc_field_id_mode(enum ipc_field_id_sel sel,
					enum ipc_field_id_togl toggle)
{
	u32 cfg;

	cfg = readl(ipc->regs + S3C_IPC_MODE);
	cfg |= S3C_IPC_FIELD_ID_SELECTION(sel);
	writel(cfg, ipc->regs + S3C_IPC_MODE);

	cfg = readl(ipc->regs + S3C_IPC_MODE);
	cfg |= S3C_IPC_FIELD_ID_AUTO_TOGGLING(toggle);
	writel(cfg, ipc->regs + S3C_IPC_MODE);

	shadow_update();
}

void ipc_2d_enable(enum ipc_enoff onoff)
{
	u32 cfg;

	cfg = readl(ipc->regs + S3C_IPC_MODE);
	cfg &= ~S3C_IPC_2D_MASK;
	cfg |= S3C_IPC_2D_CTRL(onoff);
	writel(cfg, ipc->regs + S3C_IPC_MODE);

	shadow_update();
}

void ipc_set_mode(struct ipc_controlvariable con_var)
{
	u32 cfg = 0;

	/* Enalbed : 2D IPC , Disabled : Horizon Double Scailing */
	ipc_field_id_control(IPC_BOTTOM_FIELD);
	ipc_field_id_mode(CAM_FIELD_SIG, AUTO);
	ipc_2d_enable(con_var.modeval);

	if (con_var.modeval == IPC_2D)
		cfg = IPC_2D_ENABLE;
	else
		cfg = IPC_HOR_SCALING_ENABLE;
	writel(cfg, ipc->regs + S3C_IPC_H_RATIO);

	cfg = IPC_2D_ENABLE;
	writel(cfg, ipc->regs + S3C_IPC_V_RATIO);

	shadow_update();
}

void ipc_set_imgsize(struct ipc_source src, struct ipc_destination dst)
{
	writel(S3C_IPC_SRC_WIDTH_SET(src.srchsz),
						ipc->regs + S3C_IPC_SRC_WIDTH);
	writel(S3C_IPC_SRC_HEIGHT_SET(src.srcvsz),
						ipc->regs + S3C_IPC_SRC_HEIGHT);

	writel(S3C_IPC_DST_WIDTH_SET(dst.dsthsz),
						ipc->regs + S3C_IPC_DST_WIDTH);
	writel(S3C_IPC_DST_HEIGHT_SET(dst.dstvsz),
						ipc->regs + S3C_IPC_DST_HEIGHT);

	shadow_update();
}

void ipc_set_enhance_param(void)
{
	u32 i;

	for (i = 0; i < 8; i++) {
		ipc->enhance_var.brightness[i] = 0x0;
		ipc->enhance_var.contrast[i] = 0x80;
	}

	ipc->enhance_var.saturation = 0x80;
	ipc->enhance_var.sharpness = NO_EFFECT;
	ipc->enhance_var.thhnoise = 0x5;
	ipc->enhance_var.brightoffset = 0x0;
}

void ipc_set_contrast(u32 *contrast)
{
	u32 i, line_eq[8];

	for (i = 0; i < 8; i++) {
		line_eq[i] = readl(ipc->regs + (S3C_IPC_PP_LINE_EQ0 + 4 * i));
		line_eq[i] &= ~S3C_IPC_PP_LINE_CONTRAST_MASK;
		line_eq[i] |= S3C_IPC_PP_LINE_CONTRAST(contrast[i]);
		writel(line_eq[i], ipc->regs + (S3C_IPC_PP_LINE_EQ0 + 4 * i));
	}

	shadow_update();
}

void ipc_set_brightness(u32 *brightness)
{
	u32 i, line_eq[8];

	for (i = 0; i < 8; i++) {
		line_eq[i] = readl(ipc->regs + (S3C_IPC_PP_LINE_EQ0 + 4 * i));
		line_eq[i] &= ~S3C_IPC_PP_LINE_BRIGTHNESS_MASK;
		line_eq[i] |= S3C_IPC_PP_LINE_BRIGHT(brightness[i]);
		writel(line_eq[i], ipc->regs + (S3C_IPC_PP_LINE_EQ0 + 4 * i));
	}

	shadow_update();
}

void ipc_set_bright_offset(u32 offset)
{
	writel(S3C_IPC_PP_BRIGHT_OFFSET_SET(offset),
					ipc->regs + S3C_IPC_PP_BRIGHT_OFFSET);
	shadow_update();
}

void ipc_set_saturation(u32 saturation)
{
	writel(S3C_IPC_PP_SATURATION_SET(saturation),
					ipc->regs + S3C_IPC_PP_SATURATION);
	shadow_update();
}

void ipc_set_sharpness(enum ipc_sharpness sharpness, u32 threshold)
{
	u32 sharpval;

	switch (sharpness) {
	case NO_EFFECT:
		sharpval = 0x0;
		break;
	case MIN_EDGE:
		sharpval = 0x1;
		break;
	case MODERATE_EDGE:
		sharpval = 0x2;
		break;
	default:
		sharpval = 0x3;
		break;
	}

	writel(S3C_IPC_PP_TH_HNOISE_SET(threshold) | sharpval,
					ipc->regs + S3C_IPC_PP_SHARPNESS);

	shadow_update();
}

void ipc_set_polyphase_filter(u32 filter_reg,
					const s8 *filter_coef, u16 tap)
{
	u32 base;
	u32 i, j;
	u16 tmp_tap;
	u8 *coef;

	base = (u32)ipc->regs + filter_reg;
	coef = (u8 *)filter_coef;

	for (i = 0; i < tap; i++) {
		tmp_tap = tap - i - 1;

		for (j = 0; j < 4; j++) {
			writel(((coef[4 * j * tap + tmp_tap] << 24)
				| (coef[(4 * j + 1) * tap + tmp_tap] << 16)
				| (coef[(4 * j + 2) * tap + tmp_tap] << 8)
				| (coef[(4 * j + 3) * tap + tmp_tap])), base);
			base += 4;
		}
	}
}

void ipc_set_polyphase_filterset(enum ipc_filter_h_pp h_filter,
						enum ipc_filter_v_pp v_filter)
{
	ipc_set_polyphase_filter(S3C_IPC_POLY8_Y0_LL,
				ipc_8tap_coef_y_h + h_filter * 16 * 8, 8);
	ipc_set_polyphase_filter(S3C_IPC_POLY4_C0_LL,
				ipc_4tap_coef_c_h + h_filter * 16 * 4, 4);
	ipc_set_polyphase_filter(S3C_IPC_POLY4_Y0_LL,
				ipc_4tap_coef_y_v + v_filter * 16 * 4, 4);
}

/* For the real interlace mode,
 *	the vertical ratio should be used after divided by 2.
 * Because in the interlace mode,
 *	all the IPC output is used for FIMD display
 * and it should be the same as one field of the progressive mode.
 * Therefore the same filter coefficients should be used for
 *						the same final output video.
 * When half of the interlace V_RATIO is same as the progressive V_RATIO,
 *	the final output video scale is same. (20051104,ishan)
*/
void ipc_set_filter(void)
{
	enum ipc_filter_h_pp h_filter;
	enum ipc_filter_v_pp v_filter;
	u32 h_ratio, v_ratio;

	h_ratio = readl(ipc->regs + S3C_IPC_H_RATIO);
	v_ratio = readl(ipc->regs + S3C_IPC_V_RATIO);

	/* Horizontal Y 8 tap , Horizontal C 4 tap */
	if (h_ratio <= (0x1 << 16))		/* 720 -> 720 or zoom in */
		h_filter = IPC_PP_H_NORMAL;
	else if (h_ratio <= (0x9 << 13))	/* 720 -> 640 */
		h_filter = IPC_PP_H_8_9 ;
	else if (h_ratio <= (0x1 << 17))		/* 2 -> 1 */
		h_filter = IPC_PP_H_1_2;
	else if (h_ratio <= (0x3 << 16))		/* 2 -> 1 */
		h_filter = IPC_PP_H_1_3;
	else					/* 4 -> 1 */
		h_filter = IPC_PP_H_1_4;

	/* Vertical Y 4 tap */
	if (v_ratio <= (0x1 << 16))		/* 720 -> 720 or zoom in */
		v_filter = IPC_PP_V_NORMAL;
	else if (v_ratio <= (0x3 << 15))	/* 6 -> 5 */
		v_filter = IPC_PP_V_5_6;
	else if (v_ratio <= (0x1 << 17))	/* 2 -> 1 */
		v_filter = IPC_PP_V_1_2;
	else if (v_ratio <= (0x3 << 16))	/* 3 -> 1 */
		v_filter = IPC_PP_V_1_3;
	else					/* 4 -> 1 */
		v_filter = IPC_PP_V_1_4;

	ipc_set_polyphase_filterset(h_filter, v_filter);
}

void ipc_set_pixel_rate(void)
{
	writel(S3C_IPC_PEL_RATE_SET, ipc->regs + S3C_IPC_PEL_RATE_CTRL);
	shadow_update();
}

int ipc_init(u32 src_width, u32 src_height, enum ipc_2d ipc2d)
{
	if (src_width > IN_SC_MAX_WIDTH || src_height > IN_SC_MAX_HEIGHT) {
		ipc_err("IPC input size error\n");
		ipc_stop();
		return -EINVAL;
	}

	ipc->src.imghsz = src_width;
	ipc->src.imgvsz = src_height;
	ipc->src.srchsz = src_width;
	ipc->src.srcvsz = src_height;

	ipc->dst.scanmode = PROGRESSIVE;

	if (ipc2d == IPC_2D) {
		ipc->dst.dsthsz = src_width;
		ipc->dst.dstvsz = src_height * 2;
	} else {
		ipc->dst.dsthsz = src_width * 2;
		ipc->dst.dstvsz = src_height;
	}

	ipc->control_var.modeval = ipc2d;

	clk_enable(ipc->clk);

	ipc_reset();
	ipc_enable(OFF);
	ipc_enable_postprocessing(OFF);

	ipc_set_mode(ipc->control_var);
	ipc_set_imgsize(ipc->src, ipc->dst);

	ipc_set_enhance_param();
	ipc_set_contrast(ipc->enhance_var.contrast);
	ipc_set_brightness(ipc->enhance_var.brightness);
	ipc_set_bright_offset(ipc->enhance_var.brightoffset);
	ipc_set_saturation(ipc->enhance_var.saturation);
	ipc_set_sharpness(ipc->enhance_var.sharpness,
			ipc->enhance_var.thhnoise);

	ipc_set_filter();
	ipc_set_pixel_rate();

	return 0;
}

static int ipc_probe(struct platform_device *pdev)
{
	struct resource *res;
	ipc = (struct ipc_control *) \
			kmalloc(sizeof(struct ipc_control), GFP_KERNEL);
	if (!ipc) {
		ipc_err("no memory for configuration\n");
		return -ENOMEM;
	}
	strcpy(ipc->name, IPC_NAME);

	ipc->clk = clk_get(&pdev->dev, IPC_CLK_NAME);
	if (IS_ERR(ipc->clk)) {
		ipc_err("failed to get ipc clock source\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ipc_err("failed to get io memory region\n");
		return -EINVAL;
	}

	res = request_mem_region(res->start, res->end - res->start + 1,
								pdev->name);
	if (!res) {
		ipc_err("failed to request io memory region\n");
		return -EINVAL;
	}

	/* ioremap for register block */
	ipc->regs = ioremap(res->start, res->end - res->start + 1);
	if (!ipc->regs) {
		ipc_err("failed to remap io region\n");
		return -EINVAL;
	}

	printk(KERN_INFO "IPC probe success\n");

	return 0;
}

static int ipc_remove(struct platform_device *pdev)
{
	ipc_stop();
	kfree(ipc);

	return 0;
}

int ipc_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

int ipc_resume(struct platform_device *dev)
{
	return 0;
}

static struct platform_driver ipc_driver = {
	.probe		= ipc_probe,
	.remove		= ipc_remove,
	.suspend	= ipc_suspend,
	.resume		= ipc_resume,
	.driver		= {
		.name	= "s3c-ipc",
		.owner	= THIS_MODULE,
	},
};

static int ipc_register(void)
{
	platform_driver_register(&ipc_driver);

	return 0;
}

static void ipc_unregister(void)
{
	platform_driver_unregister(&ipc_driver);
}

module_init(ipc_register);
module_exit(ipc_unregister);

MODULE_AUTHOR("Jonghun, Han <jonghun.han@samsung.com>");
MODULE_AUTHOR("Youngmok, Song <ym.song@samsung.com>");
MODULE_DESCRIPTION("IPC support for FIMC driver");
MODULE_LICENSE("GPL");
