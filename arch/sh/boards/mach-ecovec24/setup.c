/*
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
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/usb/r8a66597.h>
#include <linux/i2c.h>
#include <linux/i2c/tsc2007.h>
#include <linux/input.h>
#include <video/sh_mobile_lcdc.h>
#include <media/sh_mobile_ceu.h>
#include <asm/heartbeat.h>
#include <asm/sh_eth.h>
#include <asm/sh_keysc.h>
#include <asm/clock.h>
#include <cpu/sh7724.h>

/*
 *  Address      Interface        BusWidth
 *-----------------------------------------
 *  0x0000_0000  uboot            16bit
 *  0x0004_0000  Linux romImage   16bit
 *  0x0014_0000  MTD for Linux    16bit
 *  0x0400_0000  Internal I/O     16/32bit
 *  0x0800_0000  DRAM             32bit
 *  0x1800_0000  MFI              16bit
 */

/* SWITCH
 *------------------------------
 * DS2[1] = FlashROM write protect  ON     : write protect
 *                                  OFF    : No write protect
 * DS2[2] = RMII / TS, SCIF         ON     : RMII
 *                                  OFF    : TS, SCIF3
 * DS2[3] = Camera / Video          ON     : Camera
 *                                  OFF    : NTSC/PAL (IN)
 * DS2[5] = NTSC_OUT Clock          ON     : On board OSC
 *                                  OFF    : SH7724 DV_CLK
 * DS2[6-7] = MMC / SD              ON-OFF : SD
 *                                  OFF-ON : MMC
 */

/* Heartbeat */
static unsigned char led_pos[] = { 0, 1, 2, 3 };
static struct heartbeat_data heartbeat_data = {
	.regsize = 8,
	.nr_bits = 4,
	.bit_pos = led_pos,
};

static struct resource heartbeat_resources[] = {
	[0] = {
		.start  = 0xA405012C, /* PTG */
		.end    = 0xA405012E - 1,
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

/* MTD */
static struct mtd_partition nor_flash_partitions[] = {
	{
		.name = "boot loader",
		.offset = 0,
		.size = (5 * 1024 * 1024),
		.mask_flags = MTD_WRITEABLE,  /* force read-only */
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
		.end	= 0x03ffffff,
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

/* SH Eth */
#define SH_ETH_ADDR	(0xA4600000)
#define SH_ETH_MAHR	(SH_ETH_ADDR + 0x1C0)
#define SH_ETH_MALR	(SH_ETH_ADDR + 0x1C8)
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
	.phy = 0x1f, /* SMSC LAN8700 */
	.edmac_endian = EDMAC_LITTLE_ENDIAN,
	.ether_link_active_low = 1
};

static struct platform_device sh_eth_device = {
	.name = "sh-eth",
	.id	= 0,
	.dev = {
		.platform_data = &sh_eth_plat,
	},
	.num_resources = ARRAY_SIZE(sh_eth_resources),
	.resource = sh_eth_resources,
};

/* USB0 host */
void usb0_port_power(int port, int power)
{
	gpio_set_value(GPIO_PTB4, power);
}

static struct r8a66597_platdata usb0_host_data = {
	.on_chip = 1,
	.port_power = usb0_port_power,
};

static struct resource usb0_host_resources[] = {
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

static struct platform_device usb0_host_device = {
	.name		= "r8a66597_hcd",
	.id		= 0,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usb0_host_data,
	},
	.num_resources	= ARRAY_SIZE(usb0_host_resources),
	.resource	= usb0_host_resources,
};

/*
 * USB1
 *
 * CN5 can use both host/function,
 * and we can determine it by checking PTB[3]
 *
 * This time only USB1 host is supported.
 */
void usb1_port_power(int port, int power)
{
	if (!gpio_get_value(GPIO_PTB3)) {
		printk(KERN_ERR "USB1 function is not supported\n");
		return;
	}

	gpio_set_value(GPIO_PTB5, power);
}

static struct r8a66597_platdata usb1_host_data = {
	.on_chip = 1,
	.port_power = usb1_port_power,
};

static struct resource usb1_host_resources[] = {
	[0] = {
		.start	= 0xa4d90000,
		.end	= 0xa4d90124 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 66,
		.end	= 66,
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device usb1_host_device = {
	.name		= "r8a66597_hcd",
	.id		= 1,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usb1_host_data,
	},
	.num_resources	= ARRAY_SIZE(usb1_host_resources),
	.resource	= usb1_host_resources,
};

/* LCDC */
static struct sh_mobile_lcdc_info lcdc_info = {
	.ch[0] = {
		.interface_type = RGB18,
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.lcd_cfg = {
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

/* I2C device */
static struct i2c_board_info i2c1_devices[] = {
	{
		I2C_BOARD_INFO("r2025sd", 0x32),
	},
};

/* KEYSC */
static struct sh_keysc_info keysc_info = {
	.mode		= SH_KEYSC_MODE_1,
	.scan_timing	= 3,
	.delay		= 50,
	.kycr2_delay	= 100,
	.keycodes	= { KEY_1, 0, 0, 0, 0,
			    KEY_2, 0, 0, 0, 0,
			    KEY_3, 0, 0, 0, 0,
			    KEY_4, 0, 0, 0, 0,
			    KEY_5, 0, 0, 0, 0,
			    KEY_6, 0, 0, 0, 0, },
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
	.id             = 0, /* keysc0 clock */
	.num_resources  = ARRAY_SIZE(keysc_resources),
	.resource       = keysc_resources,
	.dev	= {
		.platform_data	= &keysc_info,
	},
	.archdata = {
		.hwblk_id = HWBLK_KEYSC,
	},
};

/* TouchScreen */
#define IRQ0 32
static int ts_get_pendown_state(void)
{
	int val = 0;
	gpio_free(GPIO_FN_INTC_IRQ0);
	gpio_request(GPIO_PTZ0, NULL);
	gpio_direction_input(GPIO_PTZ0);

	val = gpio_get_value(GPIO_PTZ0);

	gpio_free(GPIO_PTZ0);
	gpio_request(GPIO_FN_INTC_IRQ0, NULL);

	return val ? 0 : 1;
}

static int ts_init(void)
{
	gpio_request(GPIO_FN_INTC_IRQ0, NULL);
	return 0;
}

struct tsc2007_platform_data tsc2007_info = {
	.model			= 2007,
	.x_plate_ohms		= 180,
	.get_pendown_state	= ts_get_pendown_state,
	.init_platform_hw	= ts_init,
};

static struct i2c_board_info ts_i2c_clients = {
	I2C_BOARD_INFO("tsc2007", 0x48),
	.type		= "tsc2007",
	.platform_data	= &tsc2007_info,
	.irq		= IRQ0,
};

static struct platform_device *ecovec_devices[] __initdata = {
	&heartbeat_device,
	&nor_flash_device,
	&sh_eth_device,
	&usb0_host_device,
	&usb1_host_device, /* USB1 host support */
	&lcdc_device,
	&ceu0_device,
	&ceu1_device,
	&keysc_device,
};

#define EEPROM_ADDR 0x50
static u8 mac_read(struct i2c_adapter *a, u8 command)
{
	struct i2c_msg msg[2];
	u8 buf;
	int ret;

	msg[0].addr  = EEPROM_ADDR;
	msg[0].flags = 0;
	msg[0].len   = 1;
	msg[0].buf   = &command;

	msg[1].addr  = EEPROM_ADDR;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = 1;
	msg[1].buf   = &buf;

	ret = i2c_transfer(a, msg, 2);
	if (ret < 0) {
		printk(KERN_ERR "error %d\n", ret);
		buf = 0xff;
	}

	return buf;
}

#define MAC_LEN 6
static void __init sh_eth_init(void)
{
	struct i2c_adapter *a = i2c_get_adapter(1);
	struct clk *eth_clk;
	u8 mac[MAC_LEN];
	int i;

	if (!a) {
		pr_err("can not get I2C 1\n");
		return;
	}

	eth_clk = clk_get(NULL, "eth0");
	if (!eth_clk) {
		pr_err("can not get eth0 clk\n");
		return;
	}

	/* read MAC address frome EEPROM */
	for (i = 0; i < MAC_LEN; i++) {
		mac[i] = mac_read(a, 0x10 + i);
		msleep(10);
	}

	/* clock enable */
	clk_enable(eth_clk);

	/* reset sh-eth */
	ctrl_outl(0x1, SH_ETH_ADDR + 0x0);

	/* set MAC addr */
	ctrl_outl((mac[0] << 24) |
		  (mac[1] << 16) |
		  (mac[2] <<  8) |
		  (mac[3] <<  0), SH_ETH_MAHR);
	ctrl_outl((mac[4] <<  8) |
		  (mac[5] <<  0), SH_ETH_MALR);

	clk_put(eth_clk);
}

#define PORT_HIZA 0xA4050158
#define IODRIVEA  0xA405018A
static int __init arch_setup(void)
{
	/* enable STATUS0, STATUS2 and PDSTATUS */
	gpio_request(GPIO_FN_STATUS0, NULL);
	gpio_request(GPIO_FN_STATUS2, NULL);
	gpio_request(GPIO_FN_PDSTATUS, NULL);

	/* enable SCIFA0 */
	gpio_request(GPIO_FN_SCIF0_TXD, NULL);
	gpio_request(GPIO_FN_SCIF0_RXD, NULL);

	/* enable debug LED */
	gpio_request(GPIO_PTG0, NULL);
	gpio_request(GPIO_PTG1, NULL);
	gpio_request(GPIO_PTG2, NULL);
	gpio_request(GPIO_PTG3, NULL);
	gpio_direction_output(GPIO_PTG0, 0);
	gpio_direction_output(GPIO_PTG1, 0);
	gpio_direction_output(GPIO_PTG2, 0);
	gpio_direction_output(GPIO_PTG3, 0);
	ctrl_outw((ctrl_inw(PORT_HIZA) & ~(0x1 << 1)) , PORT_HIZA);

	/* enable SH-Eth */
	gpio_request(GPIO_PTA1, NULL);
	gpio_direction_output(GPIO_PTA1, 1);
	mdelay(20);

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
	gpio_request(GPIO_FN_LNKSTA,       NULL);

	/* enable USB */
	ctrl_outw(0x0000, 0xA4D80000);
	ctrl_outw(0x0000, 0xA4D90000);
	gpio_request(GPIO_PTB3,  NULL);
	gpio_request(GPIO_PTB4,  NULL);
	gpio_request(GPIO_PTB5,  NULL);
	gpio_direction_input(GPIO_PTB3);
	gpio_direction_output(GPIO_PTB4, 0);
	gpio_direction_output(GPIO_PTB5, 0);
	ctrl_outw(0x0600, 0xa40501d4);
	ctrl_outw(0x0600, 0xa4050192);

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
	gpio_request(GPIO_FN_LCDLCLK,  NULL);
	ctrl_outw((ctrl_inw(PORT_HIZA) & ~0x0001), PORT_HIZA);

	gpio_request(GPIO_PTE6, NULL);
	gpio_request(GPIO_PTU1, NULL);
	gpio_request(GPIO_PTR1, NULL);
	gpio_request(GPIO_PTA2, NULL);
	gpio_direction_input(GPIO_PTE6);
	gpio_direction_output(GPIO_PTU1, 0);
	gpio_direction_output(GPIO_PTR1, 0);
	gpio_direction_output(GPIO_PTA2, 0);

	/* I/O buffer drive ability is low */
	ctrl_outw((ctrl_inw(IODRIVEA) & ~0x00c0) | 0x0040 , IODRIVEA);

	if (gpio_get_value(GPIO_PTE6)) {
		/* DVI */
		lcdc_info.clock_source			= LCDC_CLK_EXTERNAL;
		lcdc_info.ch[0].clock_divider		= 1,
		lcdc_info.ch[0].lcd_cfg.name		= "DVI";
		lcdc_info.ch[0].lcd_cfg.xres		= 1280;
		lcdc_info.ch[0].lcd_cfg.yres		= 720;
		lcdc_info.ch[0].lcd_cfg.left_margin	= 220;
		lcdc_info.ch[0].lcd_cfg.right_margin	= 110;
		lcdc_info.ch[0].lcd_cfg.hsync_len	= 40;
		lcdc_info.ch[0].lcd_cfg.upper_margin	= 20;
		lcdc_info.ch[0].lcd_cfg.lower_margin	= 5;
		lcdc_info.ch[0].lcd_cfg.vsync_len	= 5;

		gpio_set_value(GPIO_PTA2, 1);
		gpio_set_value(GPIO_PTU1, 1);
	} else {
		/* Panel */

		lcdc_info.clock_source			= LCDC_CLK_PERIPHERAL;
		lcdc_info.ch[0].clock_divider		= 2,
		lcdc_info.ch[0].lcd_cfg.name		= "Panel";
		lcdc_info.ch[0].lcd_cfg.xres		= 800;
		lcdc_info.ch[0].lcd_cfg.yres		= 480;
		lcdc_info.ch[0].lcd_cfg.left_margin	= 220;
		lcdc_info.ch[0].lcd_cfg.right_margin	= 110;
		lcdc_info.ch[0].lcd_cfg.hsync_len	= 70;
		lcdc_info.ch[0].lcd_cfg.upper_margin	= 20;
		lcdc_info.ch[0].lcd_cfg.lower_margin	= 5;
		lcdc_info.ch[0].lcd_cfg.vsync_len	= 5;

		gpio_set_value(GPIO_PTR1, 1);

		/* FIXME
		 *
		 * LCDDON control is needed for Panel,
		 * but current sh_mobile_lcdc driver doesn't control it.
		 * It is temporary correspondence
		 */
		gpio_request(GPIO_PTF4, NULL);
		gpio_direction_output(GPIO_PTF4, 1);

		/* enable TouchScreen */
		i2c_register_board_info(0, &ts_i2c_clients, 1);
		set_irq_type(IRQ0, IRQ_TYPE_LEVEL_LOW);
	}

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

	/* enable KEYSC */
	gpio_request(GPIO_FN_KEYOUT5_IN5, NULL);
	gpio_request(GPIO_FN_KEYOUT4_IN6, NULL);
	gpio_request(GPIO_FN_KEYOUT3,     NULL);
	gpio_request(GPIO_FN_KEYOUT2,     NULL);
	gpio_request(GPIO_FN_KEYOUT1,     NULL);
	gpio_request(GPIO_FN_KEYOUT0,     NULL);
	gpio_request(GPIO_FN_KEYIN0,      NULL);

	/* enable user debug switch */
	gpio_request(GPIO_PTR0, NULL);
	gpio_request(GPIO_PTR4, NULL);
	gpio_request(GPIO_PTR5, NULL);
	gpio_request(GPIO_PTR6, NULL);
	gpio_direction_input(GPIO_PTR0);
	gpio_direction_input(GPIO_PTR4);
	gpio_direction_input(GPIO_PTR5);
	gpio_direction_input(GPIO_PTR6);

	/* enable I2C device */
	i2c_register_board_info(1, i2c1_devices,
				ARRAY_SIZE(i2c1_devices));

	return platform_add_devices(ecovec_devices,
				    ARRAY_SIZE(ecovec_devices));
}
arch_initcall(arch_setup);

static int __init devices_setup(void)
{
	sh_eth_init();
	return 0;
}
device_initcall(devices_setup);


static struct sh_machine_vector mv_ecovec __initmv = {
	.mv_name	= "R0P7724 (EcoVec)",
};
