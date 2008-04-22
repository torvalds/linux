/*
 * Support for CompuLab EM-x270 platform
 *
 * Copyright (C) 2007 CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/platform_device.h>

#include <linux/dm9000.h>
#include <linux/rtc-v3020.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-types.h>

#include <asm/mach/arch.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx-gpio.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/ohci.h>
#include <asm/arch/mmc.h>
#include <asm/arch/bitfield.h>

#include "generic.h"

/* GPIO IRQ usage */
#define EM_X270_MMC_PD		(105)
#define EM_X270_ETHIRQ		IRQ_GPIO(41)
#define EM_X270_MMC_IRQ		IRQ_GPIO(13)

static struct resource em_x270_dm9k_resource[] = {
	[0] = {
		.start = PXA_CS2_PHYS,
		.end   = PXA_CS2_PHYS + 3,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PXA_CS2_PHYS + 8,
		.end   = PXA_CS2_PHYS + 8 + 0x3f,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = EM_X270_ETHIRQ,
		.end   = EM_X270_ETHIRQ,
		.flags = IORESOURCE_IRQ,
	}
};

/* for the moment we limit ourselves to 32bit IO until some
 * better IO routines can be written and tested
 */
static struct dm9000_plat_data em_x270_dm9k_platdata = {
	.flags		= DM9000_PLATF_32BITONLY,
};

/* Ethernet device */
static struct platform_device em_x270_dm9k = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(em_x270_dm9k_resource),
	.resource	= em_x270_dm9k_resource,
	.dev		= {
		.platform_data = &em_x270_dm9k_platdata,
	}
};

/* audio device */
static struct platform_device em_x270_audio = {
	.name		= "pxa2xx-ac97",
	.id		= -1,
};

/* WM9712 touchscreen controller. Hopefully the driver will make it to
 * the mainstream sometime */
static struct platform_device em_x270_ts = {
	.name		= "wm97xx-ts",
	.id		= -1,
};

/* RTC */
static struct resource em_x270_v3020_resource[] = {
	[0] = {
		.start = PXA_CS4_PHYS,
		.end   = PXA_CS4_PHYS + 3,
		.flags = IORESOURCE_MEM,
	},
};

static struct v3020_platform_data em_x270_v3020_platdata = {
	.leftshift = 0,
};

static struct platform_device em_x270_rtc = {
	.name		= "v3020",
	.num_resources	= ARRAY_SIZE(em_x270_v3020_resource),
	.resource	= em_x270_v3020_resource,
	.id		= -1,
	.dev		= {
		.platform_data = &em_x270_v3020_platdata,
	}
};

/* NAND flash */
#define GPIO_NAND_CS	(11)
#define GPIO_NAND_RB	(56)

static inline void nand_cs_on(void)
{
	GPCR(GPIO_NAND_CS) = GPIO_bit(GPIO_NAND_CS);
}

static void nand_cs_off(void)
{
	dsb();

	GPSR(GPIO_NAND_CS) = GPIO_bit(GPIO_NAND_CS);
}

/* hardware specific access to control-lines */
static void em_x270_nand_cmd_ctl(struct mtd_info *mtd, int dat,
				 unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;
	unsigned long nandaddr = (unsigned long)this->IO_ADDR_W;

	dsb();

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_ALE)
			nandaddr |=  (1 << 3);
		else
			nandaddr &= ~(1 << 3);
		if (ctrl & NAND_CLE)
			nandaddr |=  (1 << 2);
		else
			nandaddr &= ~(1 << 2);
		if (ctrl & NAND_NCE)
			nand_cs_on();
		else
			nand_cs_off();
	}

	dsb();
	this->IO_ADDR_W = (void __iomem *)nandaddr;
	if (dat != NAND_CMD_NONE)
		writel(dat, this->IO_ADDR_W);

	dsb();
}

/* read device ready pin */
static int em_x270_nand_device_ready(struct mtd_info *mtd)
{
	dsb();

	return GPLR(GPIO_NAND_RB) & GPIO_bit(GPIO_NAND_RB);
}

static struct mtd_partition em_x270_partition_info[] = {
	[0] = {
		.name	= "em_x270-0",
		.offset	= 0,
		.size	= SZ_4M,
	},
	[1] = {
		.name	= "em_x270-1",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL
	},
};

static const char *em_x270_part_probes[] = { "cmdlinepart", NULL };

struct platform_nand_data em_x270_nand_platdata = {
	.chip = {
		.nr_chips = 1,
		.chip_offset = 0,
		.nr_partitions = ARRAY_SIZE(em_x270_partition_info),
		.partitions = em_x270_partition_info,
		.chip_delay = 20,
		.part_probe_types = em_x270_part_probes,
	},
	.ctrl = {
		.hwcontrol = 0,
		.dev_ready = em_x270_nand_device_ready,
		.select_chip = 0,
		.cmd_ctrl = em_x270_nand_cmd_ctl,
	},
};

static struct resource em_x270_nand_resource[] = {
	[0] = {
		.start = PXA_CS1_PHYS,
		.end   = PXA_CS1_PHYS + 12,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device em_x270_nand = {
	.name		= "gen_nand",
	.num_resources	= ARRAY_SIZE(em_x270_nand_resource),
	.resource	= em_x270_nand_resource,
	.id		= -1,
	.dev		= {
		.platform_data = &em_x270_nand_platdata,
	}
};

/* platform devices */
static struct platform_device *platform_devices[] __initdata = {
	&em_x270_dm9k,
	&em_x270_audio,
	&em_x270_ts,
	&em_x270_rtc,
	&em_x270_nand,
};


/* PXA27x OHCI controller setup */
static int em_x270_ohci_init(struct device *dev)
{
	/* Set the Power Control Polarity Low */
	UHCHR = (UHCHR | UHCHR_PCPL) &
		~(UHCHR_SSEP1 | UHCHR_SSEP2 | UHCHR_SSE);

	/* enable port 2 transiever */
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE;

	return 0;
}

static struct pxaohci_platform_data em_x270_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.init		= em_x270_ohci_init,
};


static int em_x270_mci_init(struct device *dev,
			    irq_handler_t em_x270_detect_int,
			    void *data)
{
	int err;

	/* setup GPIO for PXA27x MMC controller */
	pxa_gpio_mode(GPIO32_MMCCLK_MD);
	pxa_gpio_mode(GPIO112_MMCCMD_MD);
	pxa_gpio_mode(GPIO92_MMCDAT0_MD);
	pxa_gpio_mode(GPIO109_MMCDAT1_MD);
	pxa_gpio_mode(GPIO110_MMCDAT2_MD);
	pxa_gpio_mode(GPIO111_MMCDAT3_MD);

	/* EM-X270 uses GPIO13 as SD power enable */
	pxa_gpio_mode(EM_X270_MMC_PD | GPIO_OUT);

	err = request_irq(EM_X270_MMC_IRQ, em_x270_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			  "MMC card detect", data);
	if (err) {
		printk(KERN_ERR "%s: can't request MMC card detect IRQ: %d\n",
		       __func__, err);
		return err;
	}

	return 0;
}

static void em_x270_mci_setpower(struct device *dev, unsigned int vdd)
{
	/*
	   FIXME: current hardware implementation does not allow to
	   enable/disable MMC power. This will be fixed in next HW releases,
	   and we'll need to add implmentation here.
	*/
	return;
}

static void em_x270_mci_exit(struct device *dev, void *data)
{
	free_irq(EM_X270_MMC_IRQ, data);
}

static struct pxamci_platform_data em_x270_mci_platform_data = {
	.ocr_mask	= MMC_VDD_28_29|MMC_VDD_29_30|MMC_VDD_30_31,
	.init 		= em_x270_mci_init,
	.setpower 	= em_x270_mci_setpower,
	.exit		= em_x270_mci_exit,
};

/* LCD 480x640 */
static struct pxafb_mode_info em_x270_lcd_mode = {
	.pixclock	= 50000,
	.bpp		= 16,
	.xres		= 480,
	.yres		= 640,
	.hsync_len	= 8,
	.vsync_len	= 2,
	.left_margin	= 8,
	.upper_margin	= 0,
	.right_margin	= 24,
	.lower_margin	= 4,
	.cmap_greyscale	= 0,
};

static struct pxafb_mach_info em_x270_lcd = {
	.modes		= &em_x270_lcd_mode,
	.num_modes	= 1,
	.cmap_inverse	= 0,
	.cmap_static	= 0,
	.lccr0		= LCCR0_PAS,
	.lccr3		= LCCR3_PixClkDiv(0x01) | LCCR3_Acb(0xff),
};

static void __init em_x270_init(void)
{
	/* setup LCD */
	set_pxa_fb_info(&em_x270_lcd);

	/* register EM-X270 platform devices */
	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

	/* set MCI and OHCI platform parameters */
	pxa_set_mci_info(&em_x270_mci_platform_data);
	pxa_set_ohci_info(&em_x270_ohci_platform_data);

	/* setup STUART GPIOs */
	pxa_gpio_mode(GPIO46_STRXD_MD);
	pxa_gpio_mode(GPIO47_STTXD_MD);

	/* setup BTUART GPIOs */
	pxa_gpio_mode(GPIO42_BTRXD_MD);
	pxa_gpio_mode(GPIO43_BTTXD_MD);
	pxa_gpio_mode(GPIO44_BTCTS_MD);
	pxa_gpio_mode(GPIO45_BTRTS_MD);

	/* Setup interrupt for dm9000 */
	set_irq_type(EM_X270_ETHIRQ, IRQT_RISING);
}

MACHINE_START(EM_X270, "Compulab EM-x270")
	.boot_params	= 0xa0000100,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= em_x270_init,
MACHINE_END
