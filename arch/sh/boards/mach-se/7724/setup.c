/*
 * linux/arch/sh/boards/se/7724/setup.c
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 *
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/delay.h>
#include <linux/smc91x.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/usb/r8a66597.h>
#include <video/sh_mobile_lcdc.h>
#include <media/sh_mobile_ceu.h>
#include <sound/sh_fsi.h>
#include <asm/io.h>
#include <asm/heartbeat.h>
#include <asm/sh_eth.h>
#include <asm/clock.h>
#include <asm/sh_keysc.h>
#include <cpu/sh7724.h>
#include <mach-se/mach/se7724.h>

/*
 * SWx    1234 5678
 * ------------------------------------
 * SW31 : 1001 1100    : default
 * SW32 : 0111 1111    : use on board flash
 *
 * SW41 : abxx xxxx  -> a = 0 : Analog  monitor
 *                          1 : Digital monitor
 *                      b = 0 : VGA
 *                          1 : 720p
 */

/*
 * about 720p
 *
 * When you use 1280 x 720 lcdc output,
 * you should change OSC6 lcdc clock from 25.175MHz to 74.25MHz,
 * and change SW41 to use 720p
 */

/* Heartbeat */
static struct heartbeat_data heartbeat_data = {
	.regsize = 16,
};

static struct resource heartbeat_resources[] = {
	[0] = {
		.start  = PA_LED,
		.end    = PA_LED,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name           = "heartbeat",
	.id             = -1,
	.dev = {
		.platform_data = &heartbeat_data,
	},
	.num_resources  = ARRAY_SIZE(heartbeat_resources),
	.resource       = heartbeat_resources,
};

/* LAN91C111 */
static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_16BIT | SMC91X_NOWAIT,
};

static struct resource smc91x_eth_resources[] = {
	[0] = {
		.name   = "SMC91C111" ,
		.start  = 0x1a300300,
		.end    = 0x1a30030f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ0_SMC,
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct platform_device smc91x_eth_device = {
	.name	= "smc91x",
	.num_resources  = ARRAY_SIZE(smc91x_eth_resources),
	.resource       = smc91x_eth_resources,
	.dev	= {
		.platform_data	= &smc91x_info,
	},
};

/* MTD */
static struct mtd_partition nor_flash_partitions[] = {
	{
		.name = "uboot",
		.offset = 0,
		.size = (1 * 1024 * 1024),
		.mask_flags = MTD_WRITEABLE,	/* Read-only */
	}, {
		.name = "kernel",
		.offset = MTDPART_OFS_APPEND,
		.size = (2 * 1024 * 1024),
	}, {
		.name = "free-area",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
	.parts		= nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(nor_flash_partitions),
};

static struct resource nor_flash_resources[] = {
	[0] = {
		.name	= "NOR Flash",
		.start	= 0x00000000,
		.end	= 0x01ffffff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device nor_flash_device = {
	.name		= "physmap-flash",
	.resource	= nor_flash_resources,
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
	.dev		= {
		.platform_data = &nor_flash_data,
	},
};

/* LCDC */
static struct sh_mobile_lcdc_info lcdc_info = {
	.clock_source = LCDC_CLK_EXTERNAL,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.clock_divider = 1,
		.lcd_cfg = {
			.name = "LB070WV1",
			.sync = 0, /* hsync and vsync are active low */
		},
		.lcd_size_cfg = { /* 7.0 inch */
			.width = 152,
			.height = 91,
		},
		.board_cfg = {
		},
	}
};

static struct resource lcdc_resources[] = {
	[0] = {
		.name	= "LCDC",
		.start	= 0xfe940000,
		.end	= 0xfe942fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 106,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc_resources),
	.resource	= lcdc_resources,
	.dev		= {
		.platform_data	= &lcdc_info,
	},
	.archdata = {
		.hwblk_id = HWBLK_LCDC,
	},
};

/* CEU0 */
static struct sh_mobile_ceu_info sh_mobile_ceu0_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
};

static struct resource ceu0_resources[] = {
	[0] = {
		.name	= "CEU0",
		.start	= 0xfe910000,
		.end	= 0xfe91009f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = 52,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device ceu0_device = {
	.name		= "sh_mobile_ceu",
	.id             = 0, /* "ceu0" clock */
	.num_resources	= ARRAY_SIZE(ceu0_resources),
	.resource	= ceu0_resources,
	.dev	= {
		.platform_data	= &sh_mobile_ceu0_info,
	},
	.archdata = {
		.hwblk_id = HWBLK_CEU0,
	},
};

/* CEU1 */
static struct sh_mobile_ceu_info sh_mobile_ceu1_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
};

static struct resource ceu1_resources[] = {
	[0] = {
		.name	= "CEU1",
		.start	= 0xfe914000,
		.end	= 0xfe91409f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = 63,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device ceu1_device = {
	.name		= "sh_mobile_ceu",
	.id             = 1, /* "ceu1" clock */
	.num_resources	= ARRAY_SIZE(ceu1_resources),
	.resource	= ceu1_resources,
	.dev	= {
		.platform_data	= &sh_mobile_ceu1_info,
	},
	.archdata = {
		.hwblk_id = HWBLK_CEU1,
	},
};

/* FSI */
/*
 * FSI-A use external clock which came from ak464x.
 * So, we should change parent of fsi
 */
#define FCLKACR		0xa4150008
static void fsimck_init(struct clk *clk)
{
	u32 status = ctrl_inl(clk->enable_reg);

	/* use external clock */
	status &= ~0x000000ff;
	status |= 0x00000080;
	ctrl_outl(status, clk->enable_reg);
}

static struct clk_ops fsimck_clk_ops = {
	.init = fsimck_init,
};

static struct clk fsimcka_clk = {
	.name		= "fsimcka_clk",
	.id		= -1,
	.ops		= &fsimck_clk_ops,
	.enable_reg	= (void __iomem *)FCLKACR,
	.rate		= 0, /* unknown */
};

struct sh_fsi_platform_info fsi_info = {
	.porta_flags = SH_FSI_BRS_INV |
		       SH_FSI_OUT_SLAVE_MODE |
		       SH_FSI_IN_SLAVE_MODE |
		       SH_FSI_OFMT(PCM) |
		       SH_FSI_IFMT(PCM),
};

static struct resource fsi_resources[] = {
	[0] = {
		.name	= "FSI",
		.start	= 0xFE3C0000,
		.end	= 0xFE3C021d,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = 108,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device fsi_device = {
	.name		= "sh_fsi",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(fsi_resources),
	.resource	= fsi_resources,
	.dev	= {
		.platform_data	= &fsi_info,
	},
};

/* KEYSC in SoC (Needs SW33-2 set to ON) */
static struct sh_keysc_info keysc_info = {
	.mode = SH_KEYSC_MODE_1,
	.scan_timing = 10,
	.delay = 50,
	.keycodes = {
		KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
		KEY_6, KEY_7, KEY_8, KEY_9, KEY_A,
		KEY_B, KEY_C, KEY_D, KEY_E, KEY_F,
		KEY_G, KEY_H, KEY_I, KEY_K, KEY_L,
		KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q,
		KEY_R, KEY_S, KEY_T, KEY_U, KEY_V,
	},
};

static struct resource keysc_resources[] = {
	[0] = {
		.name	= "KEYSC",
		.start  = 0x044b0000,
		.end    = 0x044b000f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 79,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device keysc_device = {
	.name           = "sh_keysc",
	.id             = 0, /* "keysc0" clock */
	.num_resources  = ARRAY_SIZE(keysc_resources),
	.resource       = keysc_resources,
	.dev	= {
		.platform_data	= &keysc_info,
	},
	.archdata = {
		.hwblk_id = HWBLK_KEYSC,
	},
};

/* SH Eth */
static struct resource sh_eth_resources[] = {
	[0] = {
		.start = SH_ETH_ADDR,
		.end   = SH_ETH_ADDR + 0x1FC,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = 91,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

struct sh_eth_plat_data sh_eth_plat = {
	.phy = 0x1f, /* SMSC LAN8187 */
	.edmac_endian = EDMAC_LITTLE_ENDIAN,
};

static struct platform_device sh_eth_device = {
	.name = "sh-eth",
	.id	= 0,
	.dev = {
		.platform_data = &sh_eth_plat,
	},
	.num_resources = ARRAY_SIZE(sh_eth_resources),
	.resource = sh_eth_resources,
	.archdata = {
		.hwblk_id = HWBLK_ETHER,
	},
};

static struct r8a66597_platdata sh7724_usb0_host_data = {
	.on_chip = 1,
};

static struct resource sh7724_usb0_host_resources[] = {
	[0] = {
		.start	= 0xa4d80000,
		.end	= 0xa4d80124 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 65,
		.end	= 65,
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device sh7724_usb0_host_device = {
	.name		= "r8a66597_hcd",
	.id		= 0,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &sh7724_usb0_host_data,
	},
	.num_resources	= ARRAY_SIZE(sh7724_usb0_host_resources),
	.resource	= sh7724_usb0_host_resources,
	.archdata = {
		.hwblk_id = HWBLK_USB0,
	},
};

static struct r8a66597_platdata sh7724_usb1_gadget_data = {
	.on_chip = 1,
};

static struct resource sh7724_usb1_gadget_resources[] = {
	[0] = {
		.start	= 0xa4d90000,
		.end	= 0xa4d90123,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 66,
		.end	= 66,
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device sh7724_usb1_gadget_device = {
	.name		= "r8a66597_udc",
	.id		= 1, /* USB1 */
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &sh7724_usb1_gadget_data,
	},
	.num_resources	= ARRAY_SIZE(sh7724_usb1_gadget_resources),
	.resource	= sh7724_usb1_gadget_resources,
};

static struct resource sdhi0_cn7_resources[] = {
	[0] = {
		.name	= "SDHI0",
		.start  = 0x04ce0000,
		.end    = 0x04ce01ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 101,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_cn7_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(sdhi0_cn7_resources),
	.resource       = sdhi0_cn7_resources,
	.archdata = {
		.hwblk_id = HWBLK_SDHI0,
	},
};

static struct platform_device *ms7724se_devices[] __initdata = {
	&heartbeat_device,
	&smc91x_eth_device,
	&lcdc_device,
	&nor_flash_device,
	&ceu0_device,
	&ceu1_device,
	&keysc_device,
	&sh_eth_device,
	&sh7724_usb0_host_device,
	&sh7724_usb1_gadget_device,
	&fsi_device,
	&sdhi0_cn7_device,
};

#define EEPROM_OP   0xBA206000
#define EEPROM_ADR  0xBA206004
#define EEPROM_DATA 0xBA20600C
#define EEPROM_STAT 0xBA206010
#define EEPROM_STRT 0xBA206014
static int __init sh_eth_is_eeprom_ready(void)
{
	int t = 10000;

	while (t--) {
		if (!ctrl_inw(EEPROM_STAT))
			return 1;
		cpu_relax();
	}

	printk(KERN_ERR "ms7724se can not access to eeprom\n");
	return 0;
}

static void __init sh_eth_init(void)
{
	int i;
	u16 mac;

	/* check EEPROM status */
	if (!sh_eth_is_eeprom_ready())
		return;

	/* read MAC addr from EEPROM */
	for (i = 0 ; i < 3 ; i++) {
		ctrl_outw(0x0, EEPROM_OP); /* read */
		ctrl_outw(i*2, EEPROM_ADR);
		ctrl_outw(0x1, EEPROM_STRT);
		if (!sh_eth_is_eeprom_ready())
			return;

		mac = ctrl_inw(EEPROM_DATA);
		sh_eth_plat.mac_addr[i << 1] = mac & 0xff;
		sh_eth_plat.mac_addr[(i << 1) + 1] = mac >> 8;
	}
}

#define SW4140    0xBA201000
#define FPGA_OUT  0xBA200400
#define PORT_HIZA 0xA4050158
#define PORT_MSELCRB 0xA4050182

#define SW41_A    0x0100
#define SW41_B    0x0200
#define SW41_C    0x0400
#define SW41_D    0x0800
#define SW41_E    0x1000
#define SW41_F    0x2000
#define SW41_G    0x4000
#define SW41_H    0x8000

static int __init devices_setup(void)
{
	u16 sw = ctrl_inw(SW4140); /* select camera, monitor */
	struct clk *fsia_clk;

	/* Reset Release */
	ctrl_outw(ctrl_inw(FPGA_OUT) &
		  ~((1 << 1)  | /* LAN */
		    (1 << 6)  | /* VIDEO DAC */
		    (1 << 7)  | /* AK4643 */
		    (1 << 12) | /* USB0 */
		    (1 << 14)), /* RMII */
		  FPGA_OUT);

	/* turn on USB clocks, use external clock */
	ctrl_outw((ctrl_inw(PORT_MSELCRB) & ~0xc000) | 0x8000, PORT_MSELCRB);

#ifdef CONFIG_PM
	/* Let LED9 show STATUS2 */
	gpio_request(GPIO_FN_STATUS2, NULL);

	/* Lit LED10 show STATUS0 */
	gpio_request(GPIO_FN_STATUS0, NULL);

	/* Lit LED11 show PDSTATUS */
	gpio_request(GPIO_FN_PDSTATUS, NULL);
#else
	/* Lit LED9 */
	gpio_request(GPIO_PTJ6, NULL);
	gpio_direction_output(GPIO_PTJ6, 1);
	gpio_export(GPIO_PTJ6, 0);

	/* Lit LED10 */
	gpio_request(GPIO_PTJ5, NULL);
	gpio_direction_output(GPIO_PTJ5, 1);
	gpio_export(GPIO_PTJ5, 0);

	/* Lit LED11 */
	gpio_request(GPIO_PTJ7, NULL);
	gpio_direction_output(GPIO_PTJ7, 1);
	gpio_export(GPIO_PTJ7, 0);
#endif

	/* enable USB0 port */
	ctrl_outw(0x0600, 0xa40501d4);

	/* enable USB1 port */
	ctrl_outw(0x0600, 0xa4050192);

	/* enable IRQ 0,1,2 */
	gpio_request(GPIO_FN_INTC_IRQ0, NULL);
	gpio_request(GPIO_FN_INTC_IRQ1, NULL);
	gpio_request(GPIO_FN_INTC_IRQ2, NULL);

	/* enable SCIFA3 */
	gpio_request(GPIO_FN_SCIF3_I_SCK, NULL);
	gpio_request(GPIO_FN_SCIF3_I_RXD, NULL);
	gpio_request(GPIO_FN_SCIF3_I_TXD, NULL);
	gpio_request(GPIO_FN_SCIF3_I_CTS, NULL);
	gpio_request(GPIO_FN_SCIF3_I_RTS, NULL);

	/* enable LCDC */
	gpio_request(GPIO_FN_LCDD23,   NULL);
	gpio_request(GPIO_FN_LCDD22,   NULL);
	gpio_request(GPIO_FN_LCDD21,   NULL);
	gpio_request(GPIO_FN_LCDD20,   NULL);
	gpio_request(GPIO_FN_LCDD19,   NULL);
	gpio_request(GPIO_FN_LCDD18,   NULL);
	gpio_request(GPIO_FN_LCDD17,   NULL);
	gpio_request(GPIO_FN_LCDD16,   NULL);
	gpio_request(GPIO_FN_LCDD15,   NULL);
	gpio_request(GPIO_FN_LCDD14,   NULL);
	gpio_request(GPIO_FN_LCDD13,   NULL);
	gpio_request(GPIO_FN_LCDD12,   NULL);
	gpio_request(GPIO_FN_LCDD11,   NULL);
	gpio_request(GPIO_FN_LCDD10,   NULL);
	gpio_request(GPIO_FN_LCDD9,    NULL);
	gpio_request(GPIO_FN_LCDD8,    NULL);
	gpio_request(GPIO_FN_LCDD7,    NULL);
	gpio_request(GPIO_FN_LCDD6,    NULL);
	gpio_request(GPIO_FN_LCDD5,    NULL);
	gpio_request(GPIO_FN_LCDD4,    NULL);
	gpio_request(GPIO_FN_LCDD3,    NULL);
	gpio_request(GPIO_FN_LCDD2,    NULL);
	gpio_request(GPIO_FN_LCDD1,    NULL);
	gpio_request(GPIO_FN_LCDD0,    NULL);
	gpio_request(GPIO_FN_LCDDISP,  NULL);
	gpio_request(GPIO_FN_LCDHSYN,  NULL);
	gpio_request(GPIO_FN_LCDDCK,   NULL);
	gpio_request(GPIO_FN_LCDVSYN,  NULL);
	gpio_request(GPIO_FN_LCDDON,   NULL);
	gpio_request(GPIO_FN_LCDVEPWC, NULL);
	gpio_request(GPIO_FN_LCDVCPWC, NULL);
	gpio_request(GPIO_FN_LCDRD,    NULL);
	gpio_request(GPIO_FN_LCDLCLK,  NULL);
	ctrl_outw((ctrl_inw(PORT_HIZA) & ~0x0001), PORT_HIZA);

	/* enable CEU0 */
	gpio_request(GPIO_FN_VIO0_D15, NULL);
	gpio_request(GPIO_FN_VIO0_D14, NULL);
	gpio_request(GPIO_FN_VIO0_D13, NULL);
	gpio_request(GPIO_FN_VIO0_D12, NULL);
	gpio_request(GPIO_FN_VIO0_D11, NULL);
	gpio_request(GPIO_FN_VIO0_D10, NULL);
	gpio_request(GPIO_FN_VIO0_D9,  NULL);
	gpio_request(GPIO_FN_VIO0_D8,  NULL);
	gpio_request(GPIO_FN_VIO0_D7,  NULL);
	gpio_request(GPIO_FN_VIO0_D6,  NULL);
	gpio_request(GPIO_FN_VIO0_D5,  NULL);
	gpio_request(GPIO_FN_VIO0_D4,  NULL);
	gpio_request(GPIO_FN_VIO0_D3,  NULL);
	gpio_request(GPIO_FN_VIO0_D2,  NULL);
	gpio_request(GPIO_FN_VIO0_D1,  NULL);
	gpio_request(GPIO_FN_VIO0_D0,  NULL);
	gpio_request(GPIO_FN_VIO0_VD,  NULL);
	gpio_request(GPIO_FN_VIO0_CLK, NULL);
	gpio_request(GPIO_FN_VIO0_FLD, NULL);
	gpio_request(GPIO_FN_VIO0_HD,  NULL);
	platform_resource_setup_memory(&ceu0_device, "ceu0", 4 << 20);

	/* enable CEU1 */
	gpio_request(GPIO_FN_VIO1_D7,  NULL);
	gpio_request(GPIO_FN_VIO1_D6,  NULL);
	gpio_request(GPIO_FN_VIO1_D5,  NULL);
	gpio_request(GPIO_FN_VIO1_D4,  NULL);
	gpio_request(GPIO_FN_VIO1_D3,  NULL);
	gpio_request(GPIO_FN_VIO1_D2,  NULL);
	gpio_request(GPIO_FN_VIO1_D1,  NULL);
	gpio_request(GPIO_FN_VIO1_D0,  NULL);
	gpio_request(GPIO_FN_VIO1_FLD, NULL);
	gpio_request(GPIO_FN_VIO1_HD,  NULL);
	gpio_request(GPIO_FN_VIO1_VD,  NULL);
	gpio_request(GPIO_FN_VIO1_CLK, NULL);
	platform_resource_setup_memory(&ceu1_device, "ceu1", 4 << 20);

	/* KEYSC */
	gpio_request(GPIO_FN_KEYOUT5_IN5, NULL);
	gpio_request(GPIO_FN_KEYOUT4_IN6, NULL);
	gpio_request(GPIO_FN_KEYIN4,      NULL);
	gpio_request(GPIO_FN_KEYIN3,      NULL);
	gpio_request(GPIO_FN_KEYIN2,      NULL);
	gpio_request(GPIO_FN_KEYIN1,      NULL);
	gpio_request(GPIO_FN_KEYIN0,      NULL);
	gpio_request(GPIO_FN_KEYOUT3,     NULL);
	gpio_request(GPIO_FN_KEYOUT2,     NULL);
	gpio_request(GPIO_FN_KEYOUT1,     NULL);
	gpio_request(GPIO_FN_KEYOUT0,     NULL);

	/* enable FSI */
	gpio_request(GPIO_FN_FSIMCKB,    NULL);
	gpio_request(GPIO_FN_FSIMCKA,    NULL);
	gpio_request(GPIO_FN_FSIOASD,    NULL);
	gpio_request(GPIO_FN_FSIIABCK,   NULL);
	gpio_request(GPIO_FN_FSIIALRCK,  NULL);
	gpio_request(GPIO_FN_FSIOABCK,   NULL);
	gpio_request(GPIO_FN_FSIOALRCK,  NULL);
	gpio_request(GPIO_FN_CLKAUDIOAO, NULL);
	gpio_request(GPIO_FN_FSIIBSD,    NULL);
	gpio_request(GPIO_FN_FSIOBSD,    NULL);
	gpio_request(GPIO_FN_FSIIBBCK,   NULL);
	gpio_request(GPIO_FN_FSIIBLRCK,  NULL);
	gpio_request(GPIO_FN_FSIOBBCK,   NULL);
	gpio_request(GPIO_FN_FSIOBLRCK,  NULL);
	gpio_request(GPIO_FN_CLKAUDIOBO, NULL);
	gpio_request(GPIO_FN_FSIIASD,    NULL);

	/* change parent of FSI A */
	fsia_clk = clk_get(NULL, "fsia_clk");
	clk_register(&fsimcka_clk);
	clk_set_parent(fsia_clk, &fsimcka_clk);
	clk_set_rate(fsia_clk, 11000);
	clk_set_rate(&fsimcka_clk, 11000);
	clk_put(fsia_clk);

	/* SDHI0 connected to cn7 */
	gpio_request(GPIO_FN_SDHI0CD, NULL);
	gpio_request(GPIO_FN_SDHI0WP, NULL);
	gpio_request(GPIO_FN_SDHI0D3, NULL);
	gpio_request(GPIO_FN_SDHI0D2, NULL);
	gpio_request(GPIO_FN_SDHI0D1, NULL);
	gpio_request(GPIO_FN_SDHI0D0, NULL);
	gpio_request(GPIO_FN_SDHI0CMD, NULL);
	gpio_request(GPIO_FN_SDHI0CLK, NULL);

	/*
	 * enable SH-Eth
	 *
	 * please remove J33 pin from your board !!
	 *
	 * ms7724 board should not use GPIO_FN_LNKSTA pin
	 * So, This time PTX5 is set to input pin
	 */
	gpio_request(GPIO_FN_RMII_RXD0,    NULL);
	gpio_request(GPIO_FN_RMII_RXD1,    NULL);
	gpio_request(GPIO_FN_RMII_TXD0,    NULL);
	gpio_request(GPIO_FN_RMII_TXD1,    NULL);
	gpio_request(GPIO_FN_RMII_REF_CLK, NULL);
	gpio_request(GPIO_FN_RMII_TX_EN,   NULL);
	gpio_request(GPIO_FN_RMII_RX_ER,   NULL);
	gpio_request(GPIO_FN_RMII_CRS_DV,  NULL);
	gpio_request(GPIO_FN_MDIO,         NULL);
	gpio_request(GPIO_FN_MDC,          NULL);
	gpio_request(GPIO_PTX5, NULL);
	gpio_direction_input(GPIO_PTX5);
	sh_eth_init();

	if (sw & SW41_B) {
		/* 720p */
		lcdc_info.ch[0].lcd_cfg.xres         = 1280;
		lcdc_info.ch[0].lcd_cfg.yres         = 720;
		lcdc_info.ch[0].lcd_cfg.left_margin  = 220;
		lcdc_info.ch[0].lcd_cfg.right_margin = 110;
		lcdc_info.ch[0].lcd_cfg.hsync_len    = 40;
		lcdc_info.ch[0].lcd_cfg.upper_margin = 20;
		lcdc_info.ch[0].lcd_cfg.lower_margin = 5;
		lcdc_info.ch[0].lcd_cfg.vsync_len    = 5;
	} else {
		/* VGA */
		lcdc_info.ch[0].lcd_cfg.xres         = 640;
		lcdc_info.ch[0].lcd_cfg.yres         = 480;
		lcdc_info.ch[0].lcd_cfg.left_margin  = 105;
		lcdc_info.ch[0].lcd_cfg.right_margin = 50;
		lcdc_info.ch[0].lcd_cfg.hsync_len    = 96;
		lcdc_info.ch[0].lcd_cfg.upper_margin = 33;
		lcdc_info.ch[0].lcd_cfg.lower_margin = 10;
		lcdc_info.ch[0].lcd_cfg.vsync_len    = 2;
	}

	if (sw & SW41_A) {
		/* Digital monitor */
		lcdc_info.ch[0].interface_type = RGB18;
		lcdc_info.ch[0].flags          = 0;
	} else {
		/* Analog monitor */
		lcdc_info.ch[0].interface_type = RGB24;
		lcdc_info.ch[0].flags          = LCDC_FLAGS_DWPOL;
	}

	return platform_add_devices(ms7724se_devices,
				    ARRAY_SIZE(ms7724se_devices));
}
device_initcall(devices_setup);

static struct sh_machine_vector mv_ms7724se __initmv = {
	.mv_name	= "ms7724se",
	.mv_init_irq	= init_se7724_IRQ,
	.mv_nr_irqs	= SE7724_FPGA_IRQ_BASE + SE7724_FPGA_IRQ_NR,
};
