/*
 * Copyright (C) 2009 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * common vpss system module platform driver for all video drivers.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/err.h>

#include <media/davinci/vpss.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VPSS Driver");
MODULE_AUTHOR("Texas Instruments");

/* DM644x defines */
#define DM644X_SBL_PCR_VPSS		(4)

#define DM355_VPSSBL_INTSEL		0x10
#define DM355_VPSSBL_EVTSEL		0x14
/* vpss BL register offsets */
#define DM355_VPSSBL_CCDCMUX		0x1c
/* vpss CLK register offsets */
#define DM355_VPSSCLK_CLKCTRL		0x04
/* masks and shifts */
#define VPSS_HSSISEL_SHIFT		4
/*
 * VDINT0 - vpss_int0, VDINT1 - vpss_int1, H3A - vpss_int4,
 * IPIPE_INT1_SDR - vpss_int5
 */
#define DM355_VPSSBL_INTSEL_DEFAULT	0xff83ff10
/* VENCINT - vpss_int8 */
#define DM355_VPSSBL_EVTSEL_DEFAULT	0x4

#define DM365_ISP5_PCCR				0x04
#define DM365_ISP5_PCCR_BL_CLK_ENABLE		BIT(0)
#define DM365_ISP5_PCCR_ISIF_CLK_ENABLE		BIT(1)
#define DM365_ISP5_PCCR_H3A_CLK_ENABLE		BIT(2)
#define DM365_ISP5_PCCR_RSZ_CLK_ENABLE		BIT(3)
#define DM365_ISP5_PCCR_IPIPE_CLK_ENABLE	BIT(4)
#define DM365_ISP5_PCCR_IPIPEIF_CLK_ENABLE	BIT(5)
#define DM365_ISP5_PCCR_RSV			BIT(6)

#define DM365_ISP5_BCR			0x08
#define DM365_ISP5_BCR_ISIF_OUT_ENABLE	BIT(1)

#define DM365_ISP5_INTSEL1		0x10
#define DM365_ISP5_INTSEL2		0x14
#define DM365_ISP5_INTSEL3		0x18
#define DM365_ISP5_CCDCMUX		0x20
#define DM365_ISP5_PG_FRAME_SIZE	0x28
#define DM365_VPBE_CLK_CTRL		0x00

#define VPSS_CLK_CTRL			0x01c40044
#define VPSS_CLK_CTRL_VENCCLKEN		BIT(3)
#define VPSS_CLK_CTRL_DACCLKEN		BIT(4)

/*
 * vpss interrupts. VDINT0 - vpss_int0, VDINT1 - vpss_int1,
 * AF - vpss_int3
 */
#define DM365_ISP5_INTSEL1_DEFAULT	0x0b1f0100
/* AEW - vpss_int6, RSZ_INT_DMA - vpss_int5 */
#define DM365_ISP5_INTSEL2_DEFAULT	0x1f0a0f1f
/* VENC - vpss_int8 */
#define DM365_ISP5_INTSEL3_DEFAULT	0x00000015

/* masks and shifts for DM365*/
#define DM365_CCDC_PG_VD_POL_SHIFT	0
#define DM365_CCDC_PG_HD_POL_SHIFT	1

#define CCD_SRC_SEL_MASK		(BIT_MASK(5) | BIT_MASK(4))
#define CCD_SRC_SEL_SHIFT		4

/* Different SoC platforms supported by this driver */
enum vpss_platform_type {
	DM644X,
	DM355,
	DM365,
};

/*
 * vpss operations. Depends on platform. Not all functions are available
 * on all platforms. The api, first check if a function is available before
 * invoking it. In the probe, the function ptrs are initialized based on
 * vpss name. vpss name can be "dm355_vpss", "dm644x_vpss" etc.
 */
struct vpss_hw_ops {
	/* enable clock */
	int (*enable_clock)(enum vpss_clock_sel clock_sel, int en);
	/* select input to ccdc */
	void (*select_ccdc_source)(enum vpss_ccdc_source_sel src_sel);
	/* clear wbl overflow bit */
	int (*clear_wbl_overflow)(enum vpss_wbl_sel wbl_sel);
	/* set sync polarity */
	void (*set_sync_pol)(struct vpss_sync_pol);
	/* set the PG_FRAME_SIZE register*/
	void (*set_pg_frame_size)(struct vpss_pg_frame_size);
	/* check and clear interrupt if occurred */
	int (*dma_complete_interrupt)(void);
};

/* vpss configuration */
struct vpss_oper_config {
	__iomem void *vpss_regs_base0;
	__iomem void *vpss_regs_base1;
	__iomem void *vpss_regs_base2;
	enum vpss_platform_type platform;
	spinlock_t vpss_lock;
	struct vpss_hw_ops hw_ops;
};

static struct vpss_oper_config oper_cfg;

/* register access routines */
static inline u32 bl_regr(u32 offset)
{
	return __raw_readl(oper_cfg.vpss_regs_base0 + offset);
}

static inline void bl_regw(u32 val, u32 offset)
{
	__raw_writel(val, oper_cfg.vpss_regs_base0 + offset);
}

static inline u32 vpss_regr(u32 offset)
{
	return __raw_readl(oper_cfg.vpss_regs_base1 + offset);
}

static inline void vpss_regw(u32 val, u32 offset)
{
	__raw_writel(val, oper_cfg.vpss_regs_base1 + offset);
}

/* For DM365 only */
static inline u32 isp5_read(u32 offset)
{
	return __raw_readl(oper_cfg.vpss_regs_base0 + offset);
}

/* For DM365 only */
static inline void isp5_write(u32 val, u32 offset)
{
	__raw_writel(val, oper_cfg.vpss_regs_base0 + offset);
}

static void dm365_select_ccdc_source(enum vpss_ccdc_source_sel src_sel)
{
	u32 temp = isp5_read(DM365_ISP5_CCDCMUX) & ~CCD_SRC_SEL_MASK;

	/* if we are using pattern generator, enable it */
	if (src_sel == VPSS_PGLPBK || src_sel == VPSS_CCDCPG)
		temp |= 0x08;

	temp |= (src_sel << CCD_SRC_SEL_SHIFT);
	isp5_write(temp, DM365_ISP5_CCDCMUX);
}

static void dm355_select_ccdc_source(enum vpss_ccdc_source_sel src_sel)
{
	bl_regw(src_sel << VPSS_HSSISEL_SHIFT, DM355_VPSSBL_CCDCMUX);
}

int vpss_dma_complete_interrupt(void)
{
	if (!oper_cfg.hw_ops.dma_complete_interrupt)
		return 2;
	return oper_cfg.hw_ops.dma_complete_interrupt();
}
EXPORT_SYMBOL(vpss_dma_complete_interrupt);

int vpss_select_ccdc_source(enum vpss_ccdc_source_sel src_sel)
{
	if (!oper_cfg.hw_ops.select_ccdc_source)
		return -EINVAL;

	oper_cfg.hw_ops.select_ccdc_source(src_sel);
	return 0;
}
EXPORT_SYMBOL(vpss_select_ccdc_source);

static int dm644x_clear_wbl_overflow(enum vpss_wbl_sel wbl_sel)
{
	u32 mask = 1, val;

	if (wbl_sel < VPSS_PCR_AEW_WBL_0 ||
	    wbl_sel > VPSS_PCR_CCDC_WBL_O)
		return -EINVAL;

	/* writing a 0 clear the overflow */
	mask = ~(mask << wbl_sel);
	val = bl_regr(DM644X_SBL_PCR_VPSS) & mask;
	bl_regw(val, DM644X_SBL_PCR_VPSS);
	return 0;
}

void vpss_set_sync_pol(struct vpss_sync_pol sync)
{
	if (!oper_cfg.hw_ops.set_sync_pol)
		return;

	oper_cfg.hw_ops.set_sync_pol(sync);
}
EXPORT_SYMBOL(vpss_set_sync_pol);

int vpss_clear_wbl_overflow(enum vpss_wbl_sel wbl_sel)
{
	if (!oper_cfg.hw_ops.clear_wbl_overflow)
		return -EINVAL;

	return oper_cfg.hw_ops.clear_wbl_overflow(wbl_sel);
}
EXPORT_SYMBOL(vpss_clear_wbl_overflow);

/*
 *  dm355_enable_clock - Enable VPSS Clock
 *  @clock_sel: Clock to be enabled/disabled
 *  @en: enable/disable flag
 *
 *  This is called to enable or disable a vpss clock
 */
static int dm355_enable_clock(enum vpss_clock_sel clock_sel, int en)
{
	unsigned long flags;
	u32 utemp, mask = 0x1, shift = 0;

	switch (clock_sel) {
	case VPSS_VPBE_CLOCK:
		/* nothing since lsb */
		break;
	case VPSS_VENC_CLOCK_SEL:
		shift = 2;
		break;
	case VPSS_CFALD_CLOCK:
		shift = 3;
		break;
	case VPSS_H3A_CLOCK:
		shift = 4;
		break;
	case VPSS_IPIPE_CLOCK:
		shift = 5;
		break;
	case VPSS_CCDC_CLOCK:
		shift = 6;
		break;
	default:
		printk(KERN_ERR "dm355_enable_clock: Invalid selector: %d\n",
		       clock_sel);
		return -EINVAL;
	}

	spin_lock_irqsave(&oper_cfg.vpss_lock, flags);
	utemp = vpss_regr(DM355_VPSSCLK_CLKCTRL);
	if (!en)
		utemp &= ~(mask << shift);
	else
		utemp |= (mask << shift);

	vpss_regw(utemp, DM355_VPSSCLK_CLKCTRL);
	spin_unlock_irqrestore(&oper_cfg.vpss_lock, flags);
	return 0;
}

static int dm365_enable_clock(enum vpss_clock_sel clock_sel, int en)
{
	unsigned long flags;
	u32 utemp, mask = 0x1, shift = 0, offset = DM365_ISP5_PCCR;
	u32 (*read)(u32 offset) = isp5_read;
	void(*write)(u32 val, u32 offset) = isp5_write;

	switch (clock_sel) {
	case VPSS_BL_CLOCK:
		break;
	case VPSS_CCDC_CLOCK:
		shift = 1;
		break;
	case VPSS_H3A_CLOCK:
		shift = 2;
		break;
	case VPSS_RSZ_CLOCK:
		shift = 3;
		break;
	case VPSS_IPIPE_CLOCK:
		shift = 4;
		break;
	case VPSS_IPIPEIF_CLOCK:
		shift = 5;
		break;
	case VPSS_PCLK_INTERNAL:
		shift = 6;
		break;
	case VPSS_PSYNC_CLOCK_SEL:
		shift = 7;
		break;
	case VPSS_VPBE_CLOCK:
		read = vpss_regr;
		write = vpss_regw;
		offset = DM365_VPBE_CLK_CTRL;
		break;
	case VPSS_VENC_CLOCK_SEL:
		shift = 2;
		read = vpss_regr;
		write = vpss_regw;
		offset = DM365_VPBE_CLK_CTRL;
		break;
	case VPSS_LDC_CLOCK:
		shift = 3;
		read = vpss_regr;
		write = vpss_regw;
		offset = DM365_VPBE_CLK_CTRL;
		break;
	case VPSS_FDIF_CLOCK:
		shift = 4;
		read = vpss_regr;
		write = vpss_regw;
		offset = DM365_VPBE_CLK_CTRL;
		break;
	case VPSS_OSD_CLOCK_SEL:
		shift = 6;
		read = vpss_regr;
		write = vpss_regw;
		offset = DM365_VPBE_CLK_CTRL;
		break;
	case VPSS_LDC_CLOCK_SEL:
		shift = 7;
		read = vpss_regr;
		write = vpss_regw;
		offset = DM365_VPBE_CLK_CTRL;
		break;
	default:
		printk(KERN_ERR "dm365_enable_clock: Invalid selector: %d\n",
		       clock_sel);
		return -1;
	}

	spin_lock_irqsave(&oper_cfg.vpss_lock, flags);
	utemp = read(offset);
	if (!en) {
		mask = ~mask;
		utemp &= (mask << shift);
	} else
		utemp |= (mask << shift);

	write(utemp, offset);
	spin_unlock_irqrestore(&oper_cfg.vpss_lock, flags);

	return 0;
}

int vpss_enable_clock(enum vpss_clock_sel clock_sel, int en)
{
	if (!oper_cfg.hw_ops.enable_clock)
		return -EINVAL;

	return oper_cfg.hw_ops.enable_clock(clock_sel, en);
}
EXPORT_SYMBOL(vpss_enable_clock);

void dm365_vpss_set_sync_pol(struct vpss_sync_pol sync)
{
	int val = 0;
	val = isp5_read(DM365_ISP5_CCDCMUX);

	val |= (sync.ccdpg_hdpol << DM365_CCDC_PG_HD_POL_SHIFT);
	val |= (sync.ccdpg_vdpol << DM365_CCDC_PG_VD_POL_SHIFT);

	isp5_write(val, DM365_ISP5_CCDCMUX);
}
EXPORT_SYMBOL(dm365_vpss_set_sync_pol);

void vpss_set_pg_frame_size(struct vpss_pg_frame_size frame_size)
{
	if (!oper_cfg.hw_ops.set_pg_frame_size)
		return;

	oper_cfg.hw_ops.set_pg_frame_size(frame_size);
}
EXPORT_SYMBOL(vpss_set_pg_frame_size);

void dm365_vpss_set_pg_frame_size(struct vpss_pg_frame_size frame_size)
{
	int current_reg = ((frame_size.hlpfr >> 1) - 1) << 16;

	current_reg |= (frame_size.pplen - 1);
	isp5_write(current_reg, DM365_ISP5_PG_FRAME_SIZE);
}
EXPORT_SYMBOL(dm365_vpss_set_pg_frame_size);

static int vpss_probe(struct platform_device *pdev)
{
	struct resource *res;
	char *platform_name;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "no platform data\n");
		return -ENOENT;
	}

	platform_name = pdev->dev.platform_data;
	if (!strcmp(platform_name, "dm355_vpss"))
		oper_cfg.platform = DM355;
	else if (!strcmp(platform_name, "dm365_vpss"))
		oper_cfg.platform = DM365;
	else if (!strcmp(platform_name, "dm644x_vpss"))
		oper_cfg.platform = DM644X;
	else {
		dev_err(&pdev->dev, "vpss driver not supported on this platform\n");
		return -ENODEV;
	}

	dev_info(&pdev->dev, "%s vpss probed\n", platform_name);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	oper_cfg.vpss_regs_base0 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(oper_cfg.vpss_regs_base0))
		return PTR_ERR(oper_cfg.vpss_regs_base0);

	if (oper_cfg.platform == DM355 || oper_cfg.platform == DM365) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);

		oper_cfg.vpss_regs_base1 = devm_ioremap_resource(&pdev->dev,
								 res);
		if (IS_ERR(oper_cfg.vpss_regs_base1))
			return PTR_ERR(oper_cfg.vpss_regs_base1);
	}

	if (oper_cfg.platform == DM355) {
		oper_cfg.hw_ops.enable_clock = dm355_enable_clock;
		oper_cfg.hw_ops.select_ccdc_source = dm355_select_ccdc_source;
		/* Setup vpss interrupts */
		bl_regw(DM355_VPSSBL_INTSEL_DEFAULT, DM355_VPSSBL_INTSEL);
		bl_regw(DM355_VPSSBL_EVTSEL_DEFAULT, DM355_VPSSBL_EVTSEL);
	} else if (oper_cfg.platform == DM365) {
		oper_cfg.hw_ops.enable_clock = dm365_enable_clock;
		oper_cfg.hw_ops.select_ccdc_source = dm365_select_ccdc_source;
		/* Setup vpss interrupts */
		isp5_write((isp5_read(DM365_ISP5_PCCR) |
				      DM365_ISP5_PCCR_BL_CLK_ENABLE |
				      DM365_ISP5_PCCR_ISIF_CLK_ENABLE |
				      DM365_ISP5_PCCR_H3A_CLK_ENABLE |
				      DM365_ISP5_PCCR_RSZ_CLK_ENABLE |
				      DM365_ISP5_PCCR_IPIPE_CLK_ENABLE |
				      DM365_ISP5_PCCR_IPIPEIF_CLK_ENABLE |
				      DM365_ISP5_PCCR_RSV), DM365_ISP5_PCCR);
		isp5_write((isp5_read(DM365_ISP5_BCR) |
			    DM365_ISP5_BCR_ISIF_OUT_ENABLE), DM365_ISP5_BCR);
		isp5_write(DM365_ISP5_INTSEL1_DEFAULT, DM365_ISP5_INTSEL1);
		isp5_write(DM365_ISP5_INTSEL2_DEFAULT, DM365_ISP5_INTSEL2);
		isp5_write(DM365_ISP5_INTSEL3_DEFAULT, DM365_ISP5_INTSEL3);
	} else
		oper_cfg.hw_ops.clear_wbl_overflow = dm644x_clear_wbl_overflow;

	pm_runtime_enable(&pdev->dev);

	pm_runtime_get(&pdev->dev);

	spin_lock_init(&oper_cfg.vpss_lock);
	dev_info(&pdev->dev, "%s vpss probe success\n", platform_name);

	return 0;
}

static int vpss_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int vpss_suspend(struct device *dev)
{
	pm_runtime_put(dev);
	return 0;
}

static int vpss_resume(struct device *dev)
{
	pm_runtime_get(dev);
	return 0;
}

static const struct dev_pm_ops vpss_pm_ops = {
	.suspend = vpss_suspend,
	.resume = vpss_resume,
};

static struct platform_driver vpss_driver = {
	.driver = {
		.name	= "vpss",
		.pm = &vpss_pm_ops,
	},
	.remove = vpss_remove,
	.probe = vpss_probe,
};

static void vpss_exit(void)
{
	iounmap(oper_cfg.vpss_regs_base2);
	release_mem_region(VPSS_CLK_CTRL, 4);
	platform_driver_unregister(&vpss_driver);
}

static int __init vpss_init(void)
{
	int ret;

	if (!request_mem_region(VPSS_CLK_CTRL, 4, "vpss_clock_control"))
		return -EBUSY;

	oper_cfg.vpss_regs_base2 = ioremap(VPSS_CLK_CTRL, 4);
	if (unlikely(!oper_cfg.vpss_regs_base2)) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	writel(VPSS_CLK_CTRL_VENCCLKEN |
	       VPSS_CLK_CTRL_DACCLKEN, oper_cfg.vpss_regs_base2);

	ret = platform_driver_register(&vpss_driver);
	if (ret)
		goto err_pd_register;

	return 0;

err_pd_register:
	iounmap(oper_cfg.vpss_regs_base2);
err_ioremap:
	release_mem_region(VPSS_CLK_CTRL, 4);
	return ret;
}
subsys_initcall(vpss_init);
module_exit(vpss_exit);
