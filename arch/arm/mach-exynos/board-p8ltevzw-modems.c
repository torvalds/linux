/* linux/arch/arm/mach-xxxx/board-p8vzw-modems.c
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>

/* inlcude platform specific file */
#include <linux/platform_data/modem_na.h>
#include <mach/sec_modem.h>
#include <mach/gpio.h>
#include <mach/gpio-exynos4.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-mem.h>
#include <plat/regs-srom.h>

#include <plat/devs.h>
#include <plat/ehci.h>


#define IDPRAM_SIZE	0x4000
#define IDPRAM_PHY_START	0x13A00000
#define IDPRAM_PHY_END (IDPRAM_PHY_START + IDPRAM_SIZE)
#define MAGIC_DMDL                      0x4445444C

/*S5PV210 Interanl Dpram Special Function Register*/
#define IDPRAM_MIFCON_INT2APEN      (1<<2)
#define IDPRAM_MIFCON_INT2MSMEN     (1<<3)
#define IDPRAM_MIFCON_DMATXREQEN_0  (1<<16)
#define IDPRAM_MIFCON_DMATXREQEN_1  (1<<17)
#define IDPRAM_MIFCON_DMARXREQEN_0  (1<<18)
#define IDPRAM_MIFCON_DMARXREQEN_1  (1<<19)
#define IDPRAM_MIFCON_FIXBIT        (1<<20)

#define IDPRAM_MIFPCON_ADM_MODE     (1<<6) /* mux / demux mode  */

#define IDPRAM_DMA_ADR_MASK         0x3FFF
#define IDPRAM_DMA_TX_ADR_0         /* shift 0 */
#define IDPRAM_DMA_TX_ADR_1         /* shift 16  */
#define IDPRAM_DMA_RX_ADR_0         /* shift 0  */
#define IDPRAM_DMA_RX_ADR_1         /* shift 16  */

#define IDPRAM_SFR_PHYSICAL_ADDR 0x13A08000
#define IDPRAM_SFR_SIZE 0x1C

#define IDPRAM_ADDRESS_DEMUX

static int __init init_modem(void);
static int p8_lte_ota_reset(void);

struct idpram_sfr_reg {
	unsigned int2ap;
	unsigned int2msm;
	unsigned mifcon;
	unsigned mifpcon;
	unsigned msmintclr;
	unsigned dma_tx_adr;
	unsigned dma_rx_adr;
};

/*S5PV210 Internal Dpram GPIO table*/
struct idpram_gpio_data {
	unsigned num;
	unsigned cfg;
	unsigned pud;
	unsigned val;
};

static volatile void __iomem *s5pv310_dpram_sfr_va;

static struct idpram_gpio_data idpram_gpio_address[] = {
#ifdef IDPRAM_ADDRESS_DEMUX
	{
		.num = EXYNOS4210_GPE1(0),	/* MSM_ADDR 0 -12 */
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE1(1),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE1(2),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE1(3),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE1(4),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE1(5),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE1(6),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE1(7),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE2(0),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE2(1),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE2(2),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE2(3),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE2(4),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE2(5),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	},
#endif
};

static struct idpram_gpio_data idpram_gpio_data[] = {
	{
		.num = EXYNOS4210_GPE3(0), /* MSM_DATA 0 - 15 */
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE3(1),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE3(2),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE3(3),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE3(4),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE3(5),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE3(6),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE3(7),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE4(0),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE4(1),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE4(2),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE4(3),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE4(4),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE4(5),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE4(6),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE4(7),
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	},
};

static struct idpram_gpio_data idpram_gpio_init_control[] = {
	{
		.num = EXYNOS4210_GPE0(1), /* MDM_CSn */
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE0(0), /* MDM_WEn */
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE0(2), /* MDM_Rn */
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	}, {
		.num = EXYNOS4210_GPE0(3), /* MDM_IRQn */
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_UP,
	},
#ifndef IDPRAM_ADDRESS_DEMUX
	{
		.num = EXYNOS4210_GPE0(4), /* MDM_ADVN */
		.cfg = S3C_GPIO_SFN(0x2),
		.pud = S3C_GPIO_PULL_NONE,
	},
#endif
};

static void idpram_gpio_cfg(struct idpram_gpio_data *gpio)
{
	printk(KERN_DEBUG "MIF: idpram set gpio num=%d, cfg=0x%x, pud=%d, val=%d\n",
		gpio->num, gpio->cfg, gpio->pud, gpio->val);

	s3c_gpio_cfgpin(gpio->num, gpio->cfg);
	s3c_gpio_setpull(gpio->num, gpio->pud);
	if (gpio->val)
		gpio_set_value(gpio->num, gpio->val);
}

static void idpram_gpio_init(void)
{
	int i;

#ifdef IDPRAM_ADDRESS_DEMUX
	for (i = 0; i < ARRAY_SIZE(idpram_gpio_address); i++)
		idpram_gpio_cfg(&idpram_gpio_address[i]);
#endif

	for (i = 0; i < ARRAY_SIZE(idpram_gpio_data); i++)
		idpram_gpio_cfg(&idpram_gpio_data[i]);

	for (i = 0; i < ARRAY_SIZE(idpram_gpio_init_control); i++)
		idpram_gpio_cfg(&idpram_gpio_init_control[i]);
}

static void idpram_sfr_init(void)
{
	volatile struct idpram_sfr_reg __iomem *sfr = s5pv310_dpram_sfr_va;

	sfr->mifcon = (IDPRAM_MIFCON_FIXBIT | IDPRAM_MIFCON_INT2APEN |
		IDPRAM_MIFCON_INT2MSMEN);
#ifndef IDPRAM_ADDRESS_DEMUX
	sfr->mifpcon = (IDPRAM_MIFPCON_ADM_MODE);
#endif
}

static void idpram_init(void)
{
	struct clk *clk;

	/* enable internal dpram clock */
	clk = clk_get(NULL, "modem");
	if (!clk)
		pr_err("MIF: idpram failed to get clock %s\n", __func__);

	clk_enable(clk);

	if (!s5pv310_dpram_sfr_va) {
		s5pv310_dpram_sfr_va = (struct idpram_sfr_reg __iomem *)
		ioremap_nocache(IDPRAM_SFR_PHYSICAL_ADDR, IDPRAM_SFR_SIZE);
		if (!s5pv310_dpram_sfr_va) {
			printk(KERN_ERR "MIF: idpram_sfr_base io-remap fail\n");
			/*iounmap(idpram_base);*/
		}
	}

	idpram_sfr_init();
}

static void idpram_clr_intr(void)
{
	volatile struct idpram_sfr_reg __iomem *sfr = s5pv310_dpram_sfr_va;
	sfr->msmintclr = 0xFF;
}


/*
	magic_code +
	access_enable +
	fmt_tx_head + fmt_tx_tail + fmt_tx_buff +
	raw_tx_head + raw_tx_tail + raw_tx_buff +
	fmt_rx_head + fmt_rx_tail + fmt_rx_buff +
	raw_rx_head + raw_rx_tail + raw_rx_buff +
	padding +
	mbx_cp2ap +
	mbx_ap2cp
 =	2 +
	2 +
	2 + 2 + 2044 +
	2 + 2 + 6128 +
	2 + 2 + 2044 +
	2 + 2 + 6128 +
	16 +
	2 +
	2
 =	16384
*/

#define CBP_DP_FMT_TX_BUFF_SZ	2044
#define CBP_DP_RAW_TX_BUFF_SZ	6128
#define CBP_DP_FMT_RX_BUFF_SZ	2044
#define CBP_DP_RAW_RX_BUFF_SZ	6128

#define MAX_CBP_IDPRAM_IPC_DEV	(IPC_RAW + 1)	/* FMT, RAW */

/*
** CDMA target platform data
*/
static struct modem_io_t cdma_io_devices[] = {
	[0] = {
		.name = "multipdp",
		.id = 0x1,
		.format = IPC_MULTI_RAW,
		.io_type = IODEV_DUMMY,
		.link = LINKDEV_DPRAM,
	},
	[1] = {
		.name = "cdma_ipc0",
		.id = 0x1,
		.format = IPC_FMT,
		.io_type = IODEV_MISC,
		.link = LINKDEV_DPRAM,
	},
	[2] = {
		.name = "cdma_rfs0",
		.id = 0x33,		/* 0x13 (ch.id) | 0x20 (mask) */
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.link = LINKDEV_DPRAM,
	},
	[3] = {
		.name = "cdma_boot0",
		.id = 0x1,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
		.link = LINKDEV_DPRAM,
	},
	[4] = {
		.name = "cdma_rmnet0",
		.id = 0x2A,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.link = LINKDEV_DPRAM,
	},
	[5] = {
		.name = "cdma_rmnet1",
		.id = 0x2B,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.link = LINKDEV_DPRAM,
	},
	[6] = {
		.name = "cdma_rmnet2",
		.id = 0x2C,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.link = LINKDEV_DPRAM,
	},
	[7] = {
		.name = "cdma_rmnet3",
		.id = 0x2D,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.link = LINKDEV_DPRAM,
	},
	[8] = {
		.name = "cdma_rmnet4",
		.id = 0x27,
		.format = IPC_RAW,
		.io_type = IODEV_NET,
		.link = LINKDEV_DPRAM,
	},
	[9] = {
		.name = "cdma_rmnet5", /* DM Port IO device */
		.id = 0x3A,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.link = LINKDEV_DPRAM,
	},
	[10] = {
		.name = "cdma_rmnet6", /* AT CMD IO device */
		.id = 0x31,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.link = LINKDEV_DPRAM,
	},
	[11] = {
		.name = "cdma_ramdump0",
		.id = 0x1,
		.format = IPC_RAMDUMP,
		.io_type = IODEV_MISC,
		.link = LINKDEV_DPRAM,
	},
		[12] = {
		.name = "cdma_cplog", /* cp log io-device */
		.id = 0x3D,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.link = LINKDEV_DPRAM,
	},
		[13] = {
		.name = "cdma_router", /* AT commands */
		.id = 0x39,
		.format = IPC_RAW,
		.io_type = IODEV_MISC,
		.link = LINKDEV_DPRAM,
	},
};

static struct modem_data cdma_modem_data = {
	.name = "cbp7.1",

	.gpio_cp_on        = GPIO_PHONE_ON,
	.gpio_cp_off	= GPIO_VIA_PS_HOLD_OFF,
	.gpio_cp_reset     = GPIO_CP_RST,
	.gpio_pda_active   = GPIO_PDA_ACTIVE,
	.gpio_phone_active = GPIO_PHONE_ACTIVE,
	.gpio_ap_wakeup = GPIO_CP_AP_DPRAM_INT,
	.gpio_mbx_intr = GPIO_VIA_DPRAM_INT_N,

	.modem_net  = CDMA_NETWORK,
	.modem_type = VIA_CBP71,
	.link_type = LINKDEV_DPRAM,

	.num_iodevs = ARRAY_SIZE(cdma_io_devices),
	.iodevs     = cdma_io_devices,

	.clear_intr = idpram_clr_intr,
	.ota_reset = p8_lte_ota_reset,
	.sfr_init = idpram_sfr_init,
	.align = 1, /* Adjust the IPC raw and Multi Raw HDLC buffer offsets */
};

static struct resource cdma_modem_res[] = {
	[0] = {
		.name = "dpram",
		.start = IDPRAM_PHY_START,
		.end = IDPRAM_PHY_END,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.name = "dpram_irq",
		.start = IRQ_MODEM_IF,
		.end = IRQ_MODEM_IF,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device cdma_modem = {
	.name = "modem_if",
	.id = 1,
	.num_resources = ARRAY_SIZE(cdma_modem_res),
	.resource = cdma_modem_res,
	.dev = {
		.platform_data = &cdma_modem_data,
	},
};
static int p8_lte_ota_reset(void)
{
	unsigned gpio_cp_rst = cdma_modem_data.gpio_cp_reset;
	unsigned gpio_cp_on = cdma_modem_data.gpio_cp_on;
	unsigned int *magickey_va;
	int i;

	pr_err("[MODEM_IF] %s Modem OTA reset\n", __func__);
	magickey_va = ioremap_nocache(IDPRAM_PHY_START, sizeof(unsigned int));
	if (!magickey_va) {
		pr_err("%s: ioremap fail\n", __func__);
		return -ENOMEM;
	}

	gpio_set_value(gpio_cp_on, 1);
	msleep(100);
	gpio_set_value(gpio_cp_rst, 0);

	for (i = 0; i < 3; i++) {
		*magickey_va = MAGIC_DMDL;
		if (*magickey_va == MAGIC_DMDL) {
			pr_err("magic key is ok!");
			break;
		}
	}

	msleep(500);
	gpio_set_value(gpio_cp_rst, 1);
	for (i = 0; i < 3; i++) {
		*magickey_va = MAGIC_DMDL;
		if (*magickey_va == MAGIC_DMDL) {
			pr_err("magic key is ok!");
			break;
		}
	}

	iounmap(magickey_va);

	return 0;
}
static void config_cdma_modem_gpio(void)
{
	int err;
	unsigned gpio_cp_on = cdma_modem_data.gpio_cp_on;
	unsigned gpio_cp_off = cdma_modem_data.gpio_cp_off;
	unsigned gpio_cp_rst = cdma_modem_data.gpio_cp_reset;
	unsigned gpio_pda_active = cdma_modem_data.gpio_pda_active;
	unsigned gpio_phone_active = cdma_modem_data.gpio_phone_active;
	unsigned gpio_ap_wakeup = cdma_modem_data.gpio_ap_wakeup;

	pr_info("MIF: <%s>\n", __func__);

	if (gpio_cp_on) {
		err = gpio_request(gpio_cp_on, "VIACP_ON");
		if (err)
			pr_err("fail to request gpio %s\n", "VIACP_ON");
		 else
			gpio_direction_output(gpio_cp_on, 0);
			}

	if (gpio_cp_rst) {
		err = gpio_request(gpio_cp_rst, "VAICP_RST");
		if (err)
			pr_err("fail to request gpio %s\n", "VIACP_RST");
		 else
			gpio_direction_output(gpio_cp_rst, 0);
			}

	if (gpio_cp_off) {
		err = gpio_request(gpio_cp_off, "VAICP_OFF");
		if (err)
			pr_err("fail to request gpio %s\n", "VIACP_OFF");
		 else
			gpio_direction_output(gpio_cp_off, 1);
			}

	if (gpio_pda_active) {
		err = gpio_request(gpio_pda_active, "PDA_ACTIVE");
		if (err)
			pr_err("fail to request gpio %s\n", "PDA_ACTIVE");
		 else
			gpio_direction_output(gpio_pda_active, 0);
		}

	if (gpio_phone_active) {
		err = gpio_request(gpio_phone_active, "PHONE_ACTIVE");
		if (err) {
			pr_err("fail to request gpio %s\n", "PHONE_ACTIVE");
		} else {
			s3c_gpio_cfgpin(gpio_phone_active, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_phone_active, S3C_GPIO_PULL_NONE);
		}
	}
	if (gpio_ap_wakeup) {
		err = gpio_request(GPIO_CP_AP_DPRAM_INT, "HOST_WAKEUP");
		if (err) {
			pr_err("fail to request gpio %s\n", "HOST_WAKEUP");
		} else {
			s3c_gpio_cfgpin(GPIO_CP_AP_DPRAM_INT, \
			S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(GPIO_CP_AP_DPRAM_INT, \
			S3C_GPIO_PULL_NONE);
			}
	}

}

/* lte target platform data */
static struct modem_io_t lte_io_devices[] = {
	[0] = {
	.name = "lte_ipc0",
	.id = 0x1,
	.format = IPC_FMT,
	.io_type = IODEV_MISC,
	.link = LINKDEV_USB,
	},
	[1] = {
	.name = "lte_rmnet0",
	.id = 0x2A,
	.format = IPC_RAW,
	.io_type = IODEV_NET,
	.link = LINKDEV_USB,
	},
	[2] = {
	.name = "lte_rfs0",
	.id = 0x0,
	.format = IPC_RFS,
	.io_type = IODEV_MISC,
	.link = LINKDEV_USB,
	},
	[3] = {
	.name = "lte_boot0",
	.id = 0x0,
	.format = IPC_BOOT,
	.io_type = IODEV_MISC,
	.link = LINKDEV_USB,
	},
	[4] = {
	.name = "lte_rmnet1",
	.id = 0x2B,
	.format = IPC_RAW,
	.io_type = IODEV_NET,
	.link = LINKDEV_USB,
	},
	[5] = {
	.name = "lte_rmnet2",
	.id = 0x2C,
	.format = IPC_RAW,
	.io_type = IODEV_NET,
	.link = LINKDEV_USB,
	},
	[6] = {
	.name = "lte_rmnet3",
	.id = 0x2D,
	.format = IPC_RAW,
	.io_type = IODEV_NET,
	.link = LINKDEV_USB,
	},
	[7] = {
	.name = "lte_multipdp",
	.id = 0x1,
	.format = IPC_MULTI_RAW,
	.io_type = IODEV_DUMMY,
	.link = LINKDEV_USB,
	},
	 [8] = {
	.name = "lte_rmnet4", /* DM Port io-device */
	.id = 0x3F,
	.format = IPC_RAW,
	.io_type = IODEV_MISC,
	.link = LINKDEV_USB,
	},
	[9] = {
	.name = "lte_ramdump0",
	.id = 0x0,
	.format = IPC_RAMDUMP,
	.io_type = IODEV_MISC,
	.link = LINKDEV_USB,
	},
};

static struct modemlink_pm_data lte_link_pm_data = {
	.name = "lte_link_pm",

	.gpio_link_enable = 0,
	.gpio_link_active  = GPIO_AP2LTE_STATUS,
	.gpio_link_hostwake = GPIO_LTE2AP_WAKEUP,
	.gpio_link_slavewake = GPIO_AP2LTE_WAKEUP,

	/*
	.port_enable = host_port_enable,
	.freqlock = ATOMIC_INIT(0),
	.cpufreq_lock = exynos_cpu_frequency_lock,
	.cpufreq_unlock = exynos_cpu_frequency_unlock,
	*/
};

static struct modem_data lte_modem_data = {
	.name = "cmc220",
	.gpio_cp_on = GPIO_220_PMIC_PWRON,
	.gpio_reset_req_n = 0,
	.gpio_cp_reset = GPIO_CMC_RST,
	.gpio_pda_active = 0,/*NOT YET CONNECTED*/
	.gpio_phone_active = GPIO_LTE_ACTIVE,
	.gpio_cp_dump_int = GPIO_LTE_ACTIVE,/*TO BE CHECKED*/
	.gpio_cp_warm_reset = 0,
	/*.gpio_cp_off = GPIO_220_PMIC_PWRHOLD_OFF,*/
#ifdef CONFIG_LTE_MODEM_CMC220
	.gpio_cp_off = GPIO_LTE_PS_HOLD_OFF,
	.gpio_slave_wakeup = GPIO_AP2LTE_WAKEUP,
	.gpio_host_wakeup = GPIO_LTE2AP_WAKEUP,
	.gpio_host_active = GPIO_AP2LTE_STATUS,
#endif
	.modem_type = SEC_CMC220,
	.link_type = LINKDEV_USB,
	.modem_net = LTE_NETWORK,

	.num_iodevs = ARRAY_SIZE(lte_io_devices),
	.iodevs = lte_io_devices,

	.link_pm_data = &lte_link_pm_data,
};

static struct resource lte_modem_res[] = {
	[0] = {
		.name = "lte_phone_active",
		/* phone active irq */
		.start = IRQ_LTE_ACTIVE,
		.end = IRQ_LTE_ACTIVE,
		.flags = IORESOURCE_IRQ,
		},
	[1] = {
		.name = "lte_host_wakeup",
		/* host wakeup irq */
		.start = IRQ_LTE2AP_WAKEUP,
		.end = IRQ_LTE2AP_WAKEUP,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device lte_modem_wake = {
	.name = "modem_lte_wake",
	.id = -1,
};

static struct platform_device lte_modem = {
	.name = "modem_if",
	.id = 2,
	.num_resources = ARRAY_SIZE(lte_modem_res),
	.resource = lte_modem_res,
	.dev = {
		.platform_data = &lte_modem_data,
	},
};

static void lte_modem_cfg_gpio(void)
{
	unsigned gpio_cp_on = lte_modem_data.gpio_cp_on;
	unsigned gpio_cp_rst = lte_modem_data.gpio_cp_reset;
	unsigned gpio_phone_active = lte_modem_data.gpio_phone_active;
#ifdef CONFIG_LTE_MODEM_CMC220
	unsigned gpio_cp_off = lte_modem_data.gpio_cp_off;
	unsigned gpio_slave_wakeup = lte_modem_data.gpio_slave_wakeup;
	unsigned gpio_host_wakeup = lte_modem_data.gpio_host_wakeup;
	unsigned gpio_host_active = lte_modem_data.gpio_host_active;
#endif

	if (gpio_cp_on) {
		gpio_request(gpio_cp_on, "LTE_ON");
		gpio_direction_output(gpio_cp_on, 0);
		s3c_gpio_setpull(gpio_cp_on, S3C_GPIO_PULL_NONE);
	}

	if (gpio_cp_rst) {
		gpio_request(gpio_cp_rst, "LTE_RST");
		gpio_direction_output(gpio_cp_rst, 0);
		s3c_gpio_setpull(gpio_cp_rst, S3C_GPIO_PULL_NONE);
	}

	if (gpio_phone_active) {
		gpio_request(gpio_phone_active, "LTE_ACTIVE");
		gpio_direction_input(gpio_phone_active);
		s3c_gpio_setpull(gpio_phone_active, S3C_GPIO_PULL_DOWN);
		s3c_gpio_cfgpin(gpio_phone_active, S3C_GPIO_SFN(0xF));
	}

#ifdef CONFIG_LTE_MODEM_CMC220
	if (gpio_cp_off) {
		gpio_request(gpio_cp_off, "LTE_OFF");
		gpio_direction_output(gpio_cp_off, 1);
		s3c_gpio_setpull(gpio_cp_off, S3C_GPIO_PULL_NONE);
	}

	if (gpio_slave_wakeup) {
		gpio_request(gpio_slave_wakeup, "LTE_SLAVE_WAKEUP");
		gpio_direction_output(gpio_slave_wakeup, 0);
		s3c_gpio_setpull(gpio_slave_wakeup, S3C_GPIO_PULL_NONE);
	}

	if (gpio_host_wakeup) {
		gpio_request(gpio_host_wakeup, "LTE_HOST_WAKEUP");
		gpio_direction_input(gpio_host_wakeup);
		s3c_gpio_setpull(gpio_host_wakeup, S3C_GPIO_PULL_DOWN);
		s3c_gpio_cfgpin(gpio_host_wakeup, S3C_GPIO_SFN(0xF));
	}

	if (gpio_host_active) {
		gpio_request(gpio_host_active, "LTE_HOST_ACTIVE");
		gpio_direction_output(gpio_host_active, 1);
		s3c_gpio_setpull(gpio_host_active, S3C_GPIO_PULL_NONE);
	}
#endif
}

void set_host_states(struct platform_device *pdev, int type)
{
	int spin = 20;

	if (!type) {
		gpio_direction_output(lte_modem_data.gpio_host_active, type);
		return;
	}

	if (gpio_get_value(lte_modem_data.gpio_host_wakeup)) {
		gpio_direction_output(lte_modem_data.gpio_host_active, type);
		mdelay(10);
		while (spin--) {
			if (!gpio_get_value(lte_modem_data.gpio_host_wakeup))
				break;
			mdelay(10);
		}
	} else {
		pr_err("mif: host wakeup is low\n");
	}
}

int get_cp_active_state(void)
{
	return gpio_get_value(lte_modem_data.gpio_phone_active);
}

void set_hsic_lpa_states(int states)
{
	int val = gpio_get_value(lte_modem_data.gpio_cp_reset);

	pr_info("mif: %s: states = %d\n", __func__, states);

	if (val) {
		switch (states) {
		case STATE_HSIC_LPA_ENTER:
			/*
			gpio_set_value(lte_modem_data.gpio_link_active, 0);
			gpio_set_value(umts_modem_data.gpio_pda_active, 0);
			pr_info(LOG_TAG "set hsic lpa enter: "
				"active state (%d)" ", pda active (%d)\n",
				gpio_get_value(
					lte_modem_data.gpio_link_active),
				gpio_get_value(umts_modem_data.gpio_pda_active)
				);
			*/
			break;
		case STATE_HSIC_LPA_WAKE:
			/*
			gpio_set_value(umts_modem_data.gpio_pda_active, 1);
			pr_info(LOG_TAG "set hsic lpa wake: "
				"pda active (%d)\n",
				gpio_get_value(umts_modem_data.gpio_pda_active)
				);
			*/
			break;
		case STATE_HSIC_LPA_PHY_INIT:
			/*
			gpio_set_value(umts_modem_data.gpio_pda_active, 1);
			gpio_set_value(lte_modem_data.gpio_link_slavewake,
				1);
			pr_info(LOG_TAG "set hsic lpa phy init: "
				"slave wake-up (%d)\n",
				gpio_get_value(
					lte_modem_data.gpio_link_slavewake)
				);
			*/
			break;
		}
	}
}

/* lte_modem_wake must be registered before the ehci driver */
void __init modem_p8ltevzw_init(void)
{
	lte_modem_wake.dev.platform_data = &lte_modem_data;
	platform_device_register(&lte_modem_wake);
}

static int __init init_modem(void)
{
	pr_err("[MDM] <%s>\n", __func__);

	/* interanl dpram gpio configure */
	idpram_gpio_init();
	idpram_init();
	config_cdma_modem_gpio();
	platform_device_register(&cdma_modem);

	/* lte gpios configuration */
	lte_modem_cfg_gpio();
	platform_device_register(&lte_modem);

	return 0;
}
late_initcall(init_modem);
