/* linux/drivers/media/video/samsung/csis.c
 *
 * Copyright (c) 2010 Samsung Electronics Co,. Ltd.
 *	http://www.samsung.com/
 *
 * MIPI-CSI2 Support file for FIMC driver
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
#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>

#include <linux/io.h>
#include <linux/memory.h>
#include <linux/slab.h>
#include <plat/clock.h>
#include <plat/regs-csis.h>
#include <plat/csis.h>

#include "csis.h"
static s32 err_print_cnt;

static struct s3c_csis_info *s3c_csis[S3C_CSIS_CH_NUM];

static int s3c_csis_set_info(struct platform_device *pdev)
{
	s3c_csis[pdev->id] = (struct s3c_csis_info *)
			kzalloc(sizeof(struct s3c_csis_info), GFP_KERNEL);
	if (!s3c_csis[pdev->id]) {
		err("no memory for configuration\n");
		return -ENOMEM;
	}

	sprintf(s3c_csis[pdev->id]->name, "%s%d", S3C_CSIS_NAME, pdev->id);
	s3c_csis[pdev->id]->nr_lanes = S3C_CSIS_NR_LANES;

	return 0;
}

static void s3c_csis_reset(struct platform_device *pdev)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
	cfg |= S3C_CSIS_CONTROL_RESET;
	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
}

static void s3c_csis_set_nr_lanes(struct platform_device *pdev, int lanes)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_CONFIG);
	cfg &= ~S3C_CSIS_CONFIG_NR_LANE_MASK;

	if (lanes == 1)
		cfg |= S3C_CSIS_CONFIG_NR_LANE_1;
	else if (lanes == 2)
		cfg |= S3C_CSIS_CONFIG_NR_LANE_2;
	else if (lanes == 3)
		cfg |= S3C_CSIS_CONFIG_NR_LANE_3;
	else if (lanes == 4)
		cfg |= S3C_CSIS_CONFIG_NR_LANE_4;
	else
		err("%d is not supported lane\n", lanes);

	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_CONFIG);
}

static void s3c_csis_enable_interrupt(struct platform_device *pdev)
{
	u32 cfg = 0;

	/* enable all interrupts */
	cfg |= S3C_CSIS_INTMSK_EVEN_BEFORE_ENABLE | \
		S3C_CSIS_INTMSK_EVEN_AFTER_ENABLE | \
		S3C_CSIS_INTMSK_ODD_BEFORE_ENABLE | \
		S3C_CSIS_INTMSK_ODD_AFTER_ENABLE | \
		S3C_CSIS_INTMSK_ERR_SOT_HS_ENABLE | \
		S3C_CSIS_INTMSK_ERR_LOST_FS_ENABLE | \
		S3C_CSIS_INTMSK_ERR_LOST_FE_ENABLE | \
		S3C_CSIS_INTMSK_ERR_OVER_ENABLE |\
		S3C_CSIS_INTMSK_ERR_ECC_ENABLE | \
		S3C_CSIS_INTMSK_ERR_CRC_ENABLE | \
		S3C_CSIS_INTMSK_ERR_ID_ENABLE;

	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_INTMSK);
}

static void s3c_csis_disable_interrupt(struct platform_device *pdev)
{
	/* disable all interrupts */
	writel(0, s3c_csis[pdev->id]->regs + S3C_CSIS_INTMSK);
}

static void s3c_csis_system_on(struct platform_device *pdev)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
	cfg |= S3C_CSIS_CONTROL_ENABLE;
	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
}

static void s3c_csis_system_off(struct platform_device *pdev)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
	cfg &= ~S3C_CSIS_CONTROL_ENABLE;
	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
}

static void s3c_csis_phy_on(struct platform_device *pdev)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_DPHYCTRL);
	cfg |= S3C_CSIS_DPHYCTRL_ENABLE;
	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_DPHYCTRL);
}

static void s3c_csis_phy_off(struct platform_device *pdev)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_DPHYCTRL);
	cfg &= ~S3C_CSIS_DPHYCTRL_ENABLE;
	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_DPHYCTRL);
}

#ifdef CONFIG_MIPI_CSI_ADV_FEATURE
static void s3c_csis_update_shadow(struct platform_device *pdev)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
	cfg |= S3C_CSIS_CONTROL_UPDATE_SHADOW;
	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
}

static void s3c_csis_set_data_align(struct platform_device *pdev, int align)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
	cfg &= ~S3C_CSIS_CONTROL_ALIGN_MASK;

	if (align == 24)
		cfg |= S3C_CSIS_CONTROL_ALIGN_24BIT;
	else
		cfg |= S3C_CSIS_CONTROL_ALIGN_32BIT;

	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
}

static void s3c_csis_set_wclk(struct platform_device *pdev, int extclk)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
	cfg &= ~S3C_CSIS_CONTROL_WCLK_MASK;

	if (extclk)
		cfg |= S3C_CSIS_CONTROL_WCLK_EXTCLK;
	else
		cfg |= S3C_CSIS_CONTROL_WCLK_PCLK;

	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_CONTROL);
}

static void s3c_csis_set_format(struct platform_device *pdev, enum mipi_format fmt)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_CONFIG);
	cfg &= ~S3C_CSIS_CONFIG_FORMAT_MASK;
	cfg |= (fmt << S3C_CSIS_CONFIG_FORMAT_SHIFT);

	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_CONFIG);
}

static void s3c_csis_set_resol(struct platform_device *pdev, int width, int height)
{
	u32 cfg = 0;

	cfg |= width << S3C_CSIS_RESOL_HOR_SHIFT;
	cfg |= height << S3C_CSIS_RESOL_VER_SHIFT;

	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_RESOL);
}

static void s3c_csis_set_hs_settle(struct platform_device *pdev, int settle)
{
	u32 cfg;

	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_DPHYCTRL);
	cfg &= ~S3C_CSIS_DPHYCTRL_HS_SETTLE_MASK;
	cfg |= (settle << S3C_CSIS_DPHYCTRL_HS_SETTLE_SHIFT);

	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_DPHYCTRL);
}
#endif

int s3c_csis_get_pkt(int csis_id, void *pktdata)
{
	memcpy(pktdata, s3c_csis[csis_id]->bufs.pktdata, CSIS_PKTSIZE);
	return 0;
}

void s3c_csis_enable_pktdata(int csis_id, bool enable)
{
	s3c_csis[csis_id]->pktdata_enable = enable;
}

void s3c_csis_start(int csis_id, int lanes, int settle, int align, int width, \
				int height, int pixel_format)
{
	struct platform_device *pdev = NULL;
	struct s3c_platform_csis *pdata = NULL;
	int i;

	memset(&s3c_csis[csis_id]->bufs, 0, sizeof(s3c_csis[csis_id]->bufs));

	/* clock & power on */
	pdev = to_platform_device(s3c_csis[csis_id]->dev);
	pdata = to_csis_plat(&pdev->dev);

	if (pdata->clk_on)
		pdata->clk_on(to_platform_device(s3c_csis[csis_id]->dev),
			&s3c_csis[csis_id]->clock);
	if (pdata->cfg_phy_global)
		pdata->cfg_phy_global(1);

	s3c_csis_reset(pdev);
	s3c_csis_set_nr_lanes(pdev, lanes);

#ifdef CONFIG_MIPI_CSI_ADV_FEATURE
	/* FIXME: how configure the followings with FIMC dynamically? */
	s3c_csis_set_hs_settle(pdev, settle);	/* s5k6aa */
	s3c_csis_set_data_align(pdev, align);
	s3c_csis_set_wclk(pdev, 1);
	if (pixel_format == V4L2_PIX_FMT_JPEG ||
		pixel_format == V4L2_PIX_FMT_INTERLEAVED) {
		s3c_csis_set_format(pdev, MIPI_USER_DEF_PACKET_1);
	} else if (pixel_format == V4L2_PIX_FMT_SGRBG10)
		s3c_csis_set_format(pdev, MIPI_CSI_RAW10);
	else
		s3c_csis_set_format(pdev, MIPI_CSI_YCBCR422_8BIT);
	s3c_csis_set_resol(pdev, width, height);
	s3c_csis_update_shadow(pdev);
#endif

	s3c_csis_enable_interrupt(pdev);
	s3c_csis_system_on(pdev);
	s3c_csis_phy_on(pdev);

	err_print_cnt = 0;
	info("Samsung MIPI-CSIS%d operation started\n", pdev->id);
}

void s3c_csis_stop(int csis_id)
{
	struct platform_device *pdev = NULL;
	struct s3c_platform_csis *pdata = NULL;

	pdev = to_platform_device(s3c_csis[csis_id]->dev);
	pdata = to_csis_plat(&pdev->dev);

	s3c_csis_disable_interrupt(pdev);
	s3c_csis_system_off(pdev);
	s3c_csis_phy_off(pdev);
	s3c_csis[csis_id]->pktdata_enable = 0;

	if (pdata->cfg_phy_global)
		pdata->cfg_phy_global(0);

	if (pdata->clk_off) {
		if (s3c_csis[csis_id]->clock != NULL)
			pdata->clk_off(pdev, &s3c_csis[csis_id]->clock);
	}
}

static irqreturn_t s3c_csis_irq(int irq, void *dev_id)
{
	u32 cfg;

	struct platform_device *pdev = (struct platform_device *) dev_id;
	int bufnum = 0;
	/* just clearing the pends */
	cfg = readl(s3c_csis[pdev->id]->regs + S3C_CSIS_INTSRC);
	writel(cfg, s3c_csis[pdev->id]->regs + S3C_CSIS_INTSRC);
	/* receiving non-image data is not error */
	cfg &= 0xFFFFFFFF;

#ifdef CONFIG_VIDEO_FIMC_MIPI_IRQ_DEBUG
	if (unlikely(cfg & S3C_CSIS_INTSRC_ERR)) {
		if (err_print_cnt < 30) {
			err("csis error interrupt[%d]: %#x\n", err_print_cnt, cfg);
			err_print_cnt++;
		}
	}
#endif
	if(s3c_csis[pdev->id]->pktdata_enable) {
		if (unlikely(cfg & S3C_CSIS_INTSRC_NON_IMAGE_DATA)) {
			/* printk("%s NON Image Data bufnum = %d 0x%x\n", __func__, bufnum, cfg); */

			if (cfg & S3C_CSIS_INTSRC_EVEN_BEFORE) {
				/* printk(KERN_INFO "S3C_CSIS_INTSRC_EVEN_BEFORE\n"); */
				memcpy_fromio(s3c_csis[pdev->id]->bufs.pktdata,
					(s3c_csis[pdev->id]->regs + S3C_CSIS_PKTDATA_EVEN), CSIS_PKTSIZE);
			} else if (cfg & S3C_CSIS_INTSRC_EVEN_AFTER) {
				/* printk(KERN_INFO "S3C_CSIS_INTSRC_EVEN_AFTER\n"); */
				memcpy_fromio(s3c_csis[pdev->id]->bufs.pktdata,
					(s3c_csis[pdev->id]->regs + S3C_CSIS_PKTDATA_EVEN), CSIS_PKTSIZE);
			} else if (cfg & S3C_CSIS_INTSRC_ODD_BEFORE) {
				/* printk(KERN_INFO "S3C_CSIS_INTSRC_ODD_BEFORE\n"); */
				memcpy_fromio(s3c_csis[pdev->id]->bufs.pktdata,
					(s3c_csis[pdev->id]->regs + S3C_CSIS_PKTDATA_ODD), CSIS_PKTSIZE);
			} else if (cfg & S3C_CSIS_INTSRC_ODD_AFTER) {
				/* printk(KERN_INFO "S3C_CSIS_INTSRC_ODD_AFTER\n"); */
				memcpy_fromio(s3c_csis[pdev->id]->bufs.pktdata,
					(s3c_csis[pdev->id]->regs + S3C_CSIS_PKTDATA_ODD), CSIS_PKTSIZE);
			}
			/* printk(KERN_INFO "0x%x\n", s3c_csis[pdev->id]->bufs.pktdata[0x2c/4]); */
			/* printk(KERN_INFO "0x%x\n", s3c_csis[pdev->id]->bufs.pktdata[0x30/4]); */
		}
	}

	return IRQ_HANDLED;
}

static int s3c_csis_probe(struct platform_device *pdev)
{
	struct s3c_platform_csis *pdata;
	struct resource *res;
	int ret = 0;

	ret = s3c_csis_set_info(pdev);

	s3c_csis[pdev->id]->dev = &pdev->dev;

	pdata = to_csis_plat(&pdev->dev);
	if (pdata->cfg_gpio)
		pdata->cfg_gpio();

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err("failed to get io memory region\n");
		ret = -ENOENT;
		goto err_info;
	}

	s3c_csis[pdev->id]->regs_res = request_mem_region(res->start,
				resource_size(res), pdev->name);
	if (!s3c_csis[pdev->id]->regs_res) {
		err("failed to request io memory region\n");
		ret = -ENOENT;
		goto err_info;
	}

	/* ioremap for register block */
	s3c_csis[pdev->id]->regs = ioremap(res->start, resource_size(res));
	if (!s3c_csis[pdev->id]->regs) {
		err("failed to remap io region\n");
		ret = -ENXIO;
		goto err_req_region;
	}

	/* irq */
	s3c_csis[pdev->id]->irq = platform_get_irq(pdev, 0);
	ret = request_irq(s3c_csis[pdev->id]->irq, s3c_csis_irq, IRQF_DISABLED,
			s3c_csis[pdev->id]->name, pdev);
	if (ret) {
		err("request_irq failed\n");
		goto err_regs_unmap;
	}

	info("Samsung MIPI-CSIS%d driver probed successfully\n", pdev->id);

	return 0;

err_regs_unmap:
	iounmap(s3c_csis[pdev->id]->regs);
err_req_region:
	release_resource(s3c_csis[pdev->id]->regs_res);
	kfree(s3c_csis[pdev->id]->regs_res);
err_info:
	kfree(s3c_csis[pdev->id]);

	return ret;
}

static int s3c_csis_remove(struct platform_device *pdev)
{
	s3c_csis_stop(pdev->id);

	free_irq(s3c_csis[pdev->id]->irq, s3c_csis[pdev->id]);
	iounmap(s3c_csis[pdev->id]->regs);
	release_resource(s3c_csis[pdev->id]->regs_res);

	kfree(s3c_csis[pdev->id]);

	return 0;
}

/* sleep */
int s3c_csis_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct s3c_platform_csis *pdata = NULL;
	pdata = to_csis_plat(&pdev->dev);

	return 0;
}

/* wakeup */
int s3c_csis_resume(struct platform_device *pdev)
{
	struct s3c_platform_csis *pdata = NULL;
	pdata = to_csis_plat(&pdev->dev);

	return 0;
}

static struct platform_driver s3c_csis_driver = {
	.probe		= s3c_csis_probe,
	.remove		= s3c_csis_remove,
	.suspend	= s3c_csis_suspend,
	.resume		= s3c_csis_resume,
	.driver		= {
		.name	= "s3c-csis",
		.owner	= THIS_MODULE,
	},
};

static int s3c_csis_register(void)
{
	return platform_driver_register(&s3c_csis_driver);
}

static void s3c_csis_unregister(void)
{
	platform_driver_unregister(&s3c_csis_driver);
}

module_init(s3c_csis_register);
module_exit(s3c_csis_unregister);

MODULE_AUTHOR("Jinsung, Yang <jsgood.yang@samsung.com>");
MODULE_AUTHOR("Sewoon, Park <seuni.park@samsung.com>");
MODULE_AUTHOR("Sungchun, Kang<sungchun.kang@samsung.com>");
MODULE_DESCRIPTION("MIPI-CSI2 support for FIMC driver");
MODULE_LICENSE("GPL");
