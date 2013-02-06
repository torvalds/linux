/* linux/arch/arm/mach-xxxx/board-u1-spr-modem.c
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
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/interrupt.h>

/* inlcude platform specific file */
#include <linux/platform_data/modem_na_spr.h>
#include <mach/gpio.h>
#include <plat/gpio-cfg.h>

static int __init init_modem(void);


#define IDPRAM_SIZE	0x4000
#define IDPRAM_PHY_START	0x13A00000
#define IDPRAM_PHY_END (IDPRAM_PHY_START + IDPRAM_SIZE)

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

/*#define IDPRAM_ADDRESS_DEMUX*/

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
	pr_info("MIF: idpram set gpio num=%d, cfg=0x%x, pud=%d, val=%d\n",
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
			pr_err("MIF: idpram_sfr_base io-remap fail\n");
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

#define QSC_DP_FMT_TX_BUFF_SZ	1020
#define QSC_DP_RAW_TX_BUFF_SZ	7160
#define QSC_DP_FMT_RX_BUFF_SZ	1020
#define QSC_DP_RAW_RX_BUFF_SZ	7160

/*
** CDMA target platform data
*/
static struct modem_io_t cdma_io_devices[] = {
	[0] = {
		.name = "cdma_boot0",
		.id = 0x1,
		.format = IPC_BOOT,
		.io_type = IODEV_MISC,
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
		.name = "cdma_multipdp",
		.id = 0x1,
		.format = IPC_MULTI_RAW,
		.io_type = IODEV_DUMMY,
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
};

/* To get modem state, register phone active irq using resource */
static struct modem_data cdma_modem_data = {
	.name = "qsc6085",

	.gpio_cp_on        = GPIO_QSC_PHONE_ON,
	.gpio_cp_reset     = GPIO_QSC_PHONE_RST,
	.gpio_pda_active   = GPIO_PDA_ACTIVE,
	.gpio_phone_active = GPIO_QSC_PHONE_ACTIVE,
	.gpio_ap_wakeup = GPIO_C210_DPRAM_INT_N,
	.gpio_cp_dump_int = GPIO_CP_DUMP_INT,

	.modem_net  = CDMA_NETWORK,
	.modem_type = QC_QSC6085,
	.link_type = LINKDEV_DPRAM,

	.num_iodevs = ARRAY_SIZE(cdma_io_devices),
	.iodevs     = cdma_io_devices,

	.clear_intr = idpram_clr_intr,
	.sfr_init = idpram_sfr_init,
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

/* if use more than one modem device, then set id num */
static struct platform_device cdma_modem = {
	.name = "modem_if",
	.id = -1,
	.num_resources = ARRAY_SIZE(cdma_modem_res),
	.resource = cdma_modem_res,
	.dev = {
		.platform_data = &cdma_modem_data,
	},
};

static void config_cdma_modem_gpio(void)
{
	int err;
	unsigned gpio_cp_on = cdma_modem_data.gpio_cp_on;
	unsigned gpio_cp_rst = cdma_modem_data.gpio_cp_reset;
	unsigned gpio_pda_active = cdma_modem_data.gpio_pda_active;
	unsigned gpio_phone_active = cdma_modem_data.gpio_phone_active;
	unsigned gpio_ap_wakeup = cdma_modem_data.gpio_ap_wakeup;
	unsigned gpio_cp_dump_int = cdma_modem_data.gpio_cp_dump_int;

	pr_info("MIF: <%s>\n", __func__);

	if (gpio_cp_on) {
		err = gpio_request(gpio_cp_on, "QSC_ON");
		if (err) {
			pr_err("MIF: fail to request gpio %s\n", "QSC_ON");
		} else {
			gpio_direction_output(gpio_cp_on, 0);
			s3c_gpio_setpull(gpio_cp_on, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_cp_rst) {
		err = gpio_request(gpio_cp_rst, "QSC_RST");
		if (err) {
			pr_err("MIF: fail to request gpio %s\n", "QSC_RST");
		} else {
			gpio_direction_output(gpio_cp_rst, 0);
			s3c_gpio_setpull(gpio_cp_rst, S3C_GPIO_PULL_NONE);
			s5p_gpio_set_drvstr(gpio_cp_rst, S5P_GPIO_DRVSTR_LV4);
		}
	}

	if (gpio_pda_active) {
		err = gpio_request(gpio_pda_active, "PDA_ACTIVE");
		if (err) {
			pr_err("MIF: fail to request gpio %s\n", "PDA_ACTIVE");
		} else {
			gpio_direction_output(gpio_pda_active, 1);
			s3c_gpio_setpull(gpio_pda_active, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_phone_active) {
		err = gpio_request(gpio_phone_active, "PHONE_ACTIVE");
		if (err) {
			pr_err("MIF: fail to request gpio %s\n",
				"PHONE_ACTIVE");
		} else {
			s3c_gpio_cfgpin(gpio_phone_active, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_phone_active,
				S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_ap_wakeup) {
		err = gpio_request(gpio_ap_wakeup, "HOST_WAKEUP");
		if (err) {
			pr_err("MIF: fail to request gpio %s\n",
				"HOST_WAKEUP");
		} else {
			s3c_gpio_cfgpin(gpio_ap_wakeup, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_ap_wakeup, S3C_GPIO_PULL_NONE);
		}
	}

	if (gpio_cp_dump_int) {
		err = gpio_request(gpio_cp_dump_int, "CP_DUMP_INT");
		if (err) {
			pr_err("MIF: fail to request gpio %s\n",
				"CP_DUMP_INT");
		} else {
			s3c_gpio_cfgpin(gpio_cp_dump_int, S3C_GPIO_SFN(0xF));
			s3c_gpio_setpull(gpio_cp_dump_int, S3C_GPIO_PULL_DOWN);
		}
	}
}

static int __init init_modem(void)
{
	pr_info("MIF: <%s>\n", __func__);

	/* interanl dpram gpio configure */
	idpram_gpio_init();
	idpram_init();

	config_cdma_modem_gpio();

	platform_device_register(&cdma_modem);
	return 0;
}
late_initcall(init_modem);
/*device_initcall(init_modem);*/
