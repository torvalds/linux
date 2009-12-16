/*
 * KFR2R09 board support code
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/onenand.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/input/sh_keysc.h>
#include <linux/i2c.h>
#include <linux/usb/r8a66597.h>
#include <media/rj54n1cb0c.h>
#include <media/soc_camera.h>
#include <media/sh_mobile_ceu.h>
#include <video/sh_mobile_lcdc.h>
#include <asm/suspend.h>
#include <asm/clock.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <cpu/sh7724.h>
#include <mach/kfr2r09.h>

static struct mtd_partition kfr2r09_nor_flash_partitions[] =
{
	{
		.name = "boot",
		.offset = 0,
		.size = (4 * 1024 * 1024),
		.mask_flags = MTD_WRITEABLE,	/* Read-only */
	},
	{
		.name = "other",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data kfr2r09_nor_flash_data = {
	.width		= 2,
	.parts		= kfr2r09_nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(kfr2r09_nor_flash_partitions),
};

static struct resource kfr2r09_nor_flash_resources[] = {
	[0] = {
		.name		= "NOR Flash",
		.start		= 0x00000000,
		.end		= 0x03ffffff,
		.flags		= IORESOURCE_MEM,
	}
};

static struct platform_device kfr2r09_nor_flash_device = {
	.name		= "physmap-flash",
	.resource	= kfr2r09_nor_flash_resources,
	.num_resources	= ARRAY_SIZE(kfr2r09_nor_flash_resources),
	.dev		= {
		.platform_data = &kfr2r09_nor_flash_data,
	},
};

static struct resource kfr2r09_nand_flash_resources[] = {
	[0] = {
		.name		= "NAND Flash",
		.start		= 0x10000000,
		.end		= 0x1001ffff,
		.flags		= IORESOURCE_MEM,
	}
};

static struct platform_device kfr2r09_nand_flash_device = {
	.name		= "onenand-flash",
	.resource	= kfr2r09_nand_flash_resources,
	.num_resources	= ARRAY_SIZE(kfr2r09_nand_flash_resources),
};

static struct sh_keysc_info kfr2r09_sh_keysc_info = {
	.mode = SH_KEYSC_MODE_1, /* KEYOUT0->4, KEYIN0->4 */
	.scan_timing = 3,
	.delay = 10,
	.keycodes = {
		KEY_PHONE, KEY_CLEAR, KEY_MAIL, KEY_WWW, KEY_ENTER,
		KEY_1, KEY_2, KEY_3, 0, KEY_UP,
		KEY_4, KEY_5, KEY_6, 0, KEY_LEFT,
		KEY_7, KEY_8, KEY_9, KEY_PROG1, KEY_RIGHT,
		KEY_S, KEY_0, KEY_P, KEY_PROG2, KEY_DOWN,
		0, 0, 0, 0, 0
	},
};

static struct resource kfr2r09_sh_keysc_resources[] = {
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

static struct platform_device kfr2r09_sh_keysc_device = {
	.name           = "sh_keysc",
	.id             = 0, /* "keysc0" clock */
	.num_resources  = ARRAY_SIZE(kfr2r09_sh_keysc_resources),
	.resource       = kfr2r09_sh_keysc_resources,
	.dev	= {
		.platform_data	= &kfr2r09_sh_keysc_info,
	},
	.archdata = {
		.hwblk_id = HWBLK_KEYSC,
	},
};

static struct sh_mobile_lcdc_info kfr2r09_sh_lcdc_info = {
	.clock_source = LCDC_CLK_BUS,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.bpp = 16,
		.interface_type = SYS18,
		.clock_divider = 6,
		.flags = LCDC_FLAGS_DWPOL,
		.lcd_cfg = {
			.name = "TX07D34VM0AAA",
			.xres = 240,
			.yres = 400,
			.left_margin = 0,
			.right_margin = 16,
			.hsync_len = 8,
			.upper_margin = 0,
			.lower_margin = 1,
			.vsync_len = 1,
			.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		},
		.lcd_size_cfg = {
			.width = 35,
			.height = 58,
		},
		.board_cfg = {
			.setup_sys = kfr2r09_lcd_setup,
			.start_transfer = kfr2r09_lcd_start,
			.display_on = kfr2r09_lcd_on,
			.display_off = kfr2r09_lcd_off,
		},
		.sys_bus_cfg = {
			.ldmt2r = 0x07010904,
			.ldmt3r = 0x14012914,
			/* set 1s delay to encourage fsync() */
			.deferred_io_msec = 1000,
		},
	}
};

static struct resource kfr2r09_sh_lcdc_resources[] = {
	[0] = {
		.name	= "LCDC",
		.start	= 0xfe940000, /* P4-only space */
		.end	= 0xfe942fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 106,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kfr2r09_sh_lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(kfr2r09_sh_lcdc_resources),
	.resource	= kfr2r09_sh_lcdc_resources,
	.dev	= {
		.platform_data	= &kfr2r09_sh_lcdc_info,
	},
	.archdata = {
		.hwblk_id = HWBLK_LCDC,
	},
};

static struct r8a66597_platdata kfr2r09_usb0_gadget_data = {
	.on_chip = 1,
};

static struct resource kfr2r09_usb0_gadget_resources[] = {
	[0] = {
		.start	= 0x04d80000,
		.end	= 0x04d80123,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 65,
		.end	= 65,
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device kfr2r09_usb0_gadget_device = {
	.name		= "r8a66597_udc",
	.id		= 0,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data	= &kfr2r09_usb0_gadget_data,
	},
	.num_resources	= ARRAY_SIZE(kfr2r09_usb0_gadget_resources),
	.resource	= kfr2r09_usb0_gadget_resources,
};

static struct sh_mobile_ceu_info sh_mobile_ceu_info = {
	.flags = SH_CEU_FLAG_USE_8BIT_BUS,
};

static struct resource kfr2r09_ceu_resources[] = {
	[0] = {
		.name	= "CEU",
		.start	= 0xfe910000,
		.end	= 0xfe91009f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = 52,
		.end  = 52,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		/* place holder for contiguous memory */
	},
};

static struct platform_device kfr2r09_ceu_device = {
	.name		= "sh_mobile_ceu",
	.id             = 0, /* "ceu0" clock */
	.num_resources	= ARRAY_SIZE(kfr2r09_ceu_resources),
	.resource	= kfr2r09_ceu_resources,
	.dev	= {
		.platform_data	= &sh_mobile_ceu_info,
	},
	.archdata = {
		.hwblk_id = HWBLK_CEU0,
	},
};

static struct i2c_board_info kfr2r09_i2c_camera = {
	I2C_BOARD_INFO("rj54n1cb0c", 0x50),
};

static struct clk *camera_clk;

/* set VIO_CKO clock to 25MHz */
#define CEU_MCLK_FREQ 25000000

#define DRVCRB 0xA405018C
static int camera_power(struct device *dev, int mode)
{
	int ret;

	if (mode) {
		long rate;

		camera_clk = clk_get(NULL, "video_clk");
		if (IS_ERR(camera_clk))
			return PTR_ERR(camera_clk);

		rate = clk_round_rate(camera_clk, CEU_MCLK_FREQ);
		ret = clk_set_rate(camera_clk, rate);
		if (ret < 0)
			goto eclkrate;

		/* set DRVCRB
		 *
		 * use 1.8 V for VccQ_VIO
		 * use 2.85V for VccQ_SR
		 */
		ctrl_outw((ctrl_inw(DRVCRB) & ~0x0003) | 0x0001, DRVCRB);

		/* reset clear */
		ret = gpio_request(GPIO_PTB4, NULL);
		if (ret < 0)
			goto eptb4;
		ret = gpio_request(GPIO_PTB7, NULL);
		if (ret < 0)
			goto eptb7;

		ret = gpio_direction_output(GPIO_PTB4, 1);
		if (!ret)
			ret = gpio_direction_output(GPIO_PTB7, 1);
		if (ret < 0)
			goto egpioout;
		msleep(1);

		ret = clk_enable(camera_clk);	/* start VIO_CKO */
		if (ret < 0)
			goto eclkon;

		return 0;
	}

	ret = 0;

	clk_disable(camera_clk);
eclkon:
	gpio_set_value(GPIO_PTB7, 0);
egpioout:
	gpio_set_value(GPIO_PTB4, 0);
	gpio_free(GPIO_PTB7);
eptb7:
	gpio_free(GPIO_PTB4);
eptb4:
eclkrate:
	clk_put(camera_clk);
	return ret;
}

static struct rj54n1_pdata rj54n1_priv = {
	.mclk_freq	= CEU_MCLK_FREQ,
	.ioctl_high	= false,
};

static struct soc_camera_link rj54n1_link = {
	.power		= camera_power,
	.board_info	= &kfr2r09_i2c_camera,
	.i2c_adapter_id	= 1,
	.module_name	= "rj54n1cb0c",
	.priv		= &rj54n1_priv,
};

static struct platform_device kfr2r09_camera = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.platform_data = &rj54n1_link,
	},
};

static struct resource kfr2r09_sh_sdhi0_resources[] = {
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

static struct platform_device kfr2r09_sh_sdhi0_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(kfr2r09_sh_sdhi0_resources),
	.resource       = kfr2r09_sh_sdhi0_resources,
	.archdata = {
		.hwblk_id = HWBLK_SDHI0,
	},
};

static struct platform_device *kfr2r09_devices[] __initdata = {
	&kfr2r09_nor_flash_device,
	&kfr2r09_nand_flash_device,
	&kfr2r09_sh_keysc_device,
	&kfr2r09_sh_lcdc_device,
	&kfr2r09_ceu_device,
	&kfr2r09_camera,
	&kfr2r09_sh_sdhi0_device,
};

#define BSC_CS0BCR 0xfec10004
#define BSC_CS0WCR 0xfec10024
#define BSC_CS4BCR 0xfec10010
#define BSC_CS4WCR 0xfec10030
#define PORT_MSELCRB 0xa4050182

#ifdef CONFIG_I2C
static int kfr2r09_usb0_gadget_i2c_setup(void)
{
	struct i2c_adapter *a;
	struct i2c_msg msg;
	unsigned char buf[2];
	int ret;

	a = i2c_get_adapter(0);
	if (!a)
		return -ENODEV;

	/* set bit 1 (the second bit) of chip at 0x09, register 0x13 */
	buf[0] = 0x13;
	msg.addr = 0x09;
	msg.buf = buf;
	msg.len = 1;
	msg.flags = 0;
	ret = i2c_transfer(a, &msg, 1);
	if (ret != 1)
		return -ENODEV;

	buf[0] = 0;
	msg.addr = 0x09;
	msg.buf = buf;
	msg.len = 1;
	msg.flags = I2C_M_RD;
	ret = i2c_transfer(a, &msg, 1);
	if (ret != 1)
		return -ENODEV;

	buf[1] = buf[0] | (1 << 1);
	buf[0] = 0x13;
	msg.addr = 0x09;
	msg.buf = buf;
	msg.len = 2;
	msg.flags = 0;
	ret = i2c_transfer(a, &msg, 1);
	if (ret != 1)
		return -ENODEV;

	return 0;
}

static int kfr2r09_serial_i2c_setup(void)
{
	struct i2c_adapter *a;
	struct i2c_msg msg;
	unsigned char buf[2];
	int ret;

	a = i2c_get_adapter(0);
	if (!a)
		return -ENODEV;

	/* set bit 6 (the 7th bit) of chip at 0x09, register 0x13 */
	buf[0] = 0x13;
	msg.addr = 0x09;
	msg.buf = buf;
	msg.len = 1;
	msg.flags = 0;
	ret = i2c_transfer(a, &msg, 1);
	if (ret != 1)
		return -ENODEV;

	buf[0] = 0;
	msg.addr = 0x09;
	msg.buf = buf;
	msg.len = 1;
	msg.flags = I2C_M_RD;
	ret = i2c_transfer(a, &msg, 1);
	if (ret != 1)
		return -ENODEV;

	buf[1] = buf[0] | (1 << 6);
	buf[0] = 0x13;
	msg.addr = 0x09;
	msg.buf = buf;
	msg.len = 2;
	msg.flags = 0;
	ret = i2c_transfer(a, &msg, 1);
	if (ret != 1)
		return -ENODEV;

	return 0;
}
#else
static int kfr2r09_usb0_gadget_i2c_setup(void)
{
	return -ENODEV;
}

static int kfr2r09_serial_i2c_setup(void)
{
	return -ENODEV;
}
#endif

static int kfr2r09_usb0_gadget_setup(void)
{
	int plugged_in;

	gpio_request(GPIO_PTN4, NULL); /* USB_DET */
	gpio_direction_input(GPIO_PTN4);
	plugged_in = gpio_get_value(GPIO_PTN4);
	if (!plugged_in)
		return -ENODEV; /* no cable plugged in */

	if (kfr2r09_usb0_gadget_i2c_setup() != 0)
		return -ENODEV; /* unable to configure using i2c */

	ctrl_outw((ctrl_inw(PORT_MSELCRB) & ~0xc000) | 0x8000, PORT_MSELCRB);
	gpio_request(GPIO_FN_PDSTATUS, NULL); /* R-standby disables USB clock */
	gpio_request(GPIO_PTV6, NULL); /* USBCLK_ON */
	gpio_direction_output(GPIO_PTV6, 1); /* USBCLK_ON = H */
	msleep(20); /* wait 20ms to let the clock settle */
	clk_enable(clk_get(NULL, "usb0"));
	ctrl_outw(0x0600, 0xa40501d4);

	return 0;
}

extern char kfr2r09_sdram_enter_start;
extern char kfr2r09_sdram_enter_end;
extern char kfr2r09_sdram_leave_start;
extern char kfr2r09_sdram_leave_end;

static int __init kfr2r09_devices_setup(void)
{
	/* register board specific self-refresh code */
	sh_mobile_register_self_refresh(SUSP_SH_STANDBY | SUSP_SH_SF |
					SUSP_SH_RSTANDBY,
					&kfr2r09_sdram_enter_start,
					&kfr2r09_sdram_enter_end,
					&kfr2r09_sdram_leave_start,
					&kfr2r09_sdram_leave_end);

	/* enable SCIF1 serial port for YC401 console support */
	gpio_request(GPIO_FN_SCIF1_RXD, NULL);
	gpio_request(GPIO_FN_SCIF1_TXD, NULL);
	kfr2r09_serial_i2c_setup(); /* ECONTMSK(bit6=L10ONEN) set 1 */
	gpio_request(GPIO_PTG3, NULL); /* HPON_ON */
	gpio_direction_output(GPIO_PTG3, 1); /* HPON_ON = H */

	/* setup NOR flash at CS0 */
	ctrl_outl(0x36db0400, BSC_CS0BCR);
	ctrl_outl(0x00000500, BSC_CS0WCR);

	/* setup NAND flash at CS4 */
	ctrl_outl(0x36db0400, BSC_CS4BCR);
	ctrl_outl(0x00000500, BSC_CS4WCR);

	/* setup KEYSC pins */
	gpio_request(GPIO_FN_KEYOUT0, NULL);
	gpio_request(GPIO_FN_KEYOUT1, NULL);
	gpio_request(GPIO_FN_KEYOUT2, NULL);
	gpio_request(GPIO_FN_KEYOUT3, NULL);
	gpio_request(GPIO_FN_KEYOUT4_IN6, NULL);
	gpio_request(GPIO_FN_KEYIN0, NULL);
	gpio_request(GPIO_FN_KEYIN1, NULL);
	gpio_request(GPIO_FN_KEYIN2, NULL);
	gpio_request(GPIO_FN_KEYIN3, NULL);
	gpio_request(GPIO_FN_KEYIN4, NULL);
	gpio_request(GPIO_FN_KEYOUT5_IN5, NULL);

	/* setup LCDC pins for SYS panel */
	gpio_request(GPIO_FN_LCDD17, NULL);
	gpio_request(GPIO_FN_LCDD16, NULL);
	gpio_request(GPIO_FN_LCDD15, NULL);
	gpio_request(GPIO_FN_LCDD14, NULL);
	gpio_request(GPIO_FN_LCDD13, NULL);
	gpio_request(GPIO_FN_LCDD12, NULL);
	gpio_request(GPIO_FN_LCDD11, NULL);
	gpio_request(GPIO_FN_LCDD10, NULL);
	gpio_request(GPIO_FN_LCDD9, NULL);
	gpio_request(GPIO_FN_LCDD8, NULL);
	gpio_request(GPIO_FN_LCDD7, NULL);
	gpio_request(GPIO_FN_LCDD6, NULL);
	gpio_request(GPIO_FN_LCDD5, NULL);
	gpio_request(GPIO_FN_LCDD4, NULL);
	gpio_request(GPIO_FN_LCDD3, NULL);
	gpio_request(GPIO_FN_LCDD2, NULL);
	gpio_request(GPIO_FN_LCDD1, NULL);
	gpio_request(GPIO_FN_LCDD0, NULL);
	gpio_request(GPIO_FN_LCDRS, NULL); /* LCD_RS */
	gpio_request(GPIO_FN_LCDCS, NULL); /* LCD_CS/ */
	gpio_request(GPIO_FN_LCDRD, NULL); /* LCD_RD/ */
	gpio_request(GPIO_FN_LCDWR, NULL); /* LCD_WR/ */
	gpio_request(GPIO_FN_LCDVSYN, NULL); /* LCD_VSYNC */
	gpio_request(GPIO_PTE4, NULL); /* LCD_RST/ */
	gpio_direction_output(GPIO_PTE4, 1);
	gpio_request(GPIO_PTF4, NULL); /* PROTECT/ */
	gpio_direction_output(GPIO_PTF4, 1);
	gpio_request(GPIO_PTU0, NULL); /* LEDSTDBY/ */
	gpio_direction_output(GPIO_PTU0, 1);

	/* setup USB function */
	if (kfr2r09_usb0_gadget_setup() == 0)
		platform_device_register(&kfr2r09_usb0_gadget_device);

	/* CEU */
	gpio_request(GPIO_FN_VIO_CKO, NULL);
	gpio_request(GPIO_FN_VIO0_CLK, NULL);
	gpio_request(GPIO_FN_VIO0_VD, NULL);
	gpio_request(GPIO_FN_VIO0_HD, NULL);
	gpio_request(GPIO_FN_VIO0_FLD, NULL);
	gpio_request(GPIO_FN_VIO0_D7, NULL);
	gpio_request(GPIO_FN_VIO0_D6, NULL);
	gpio_request(GPIO_FN_VIO0_D5, NULL);
	gpio_request(GPIO_FN_VIO0_D4, NULL);
	gpio_request(GPIO_FN_VIO0_D3, NULL);
	gpio_request(GPIO_FN_VIO0_D2, NULL);
	gpio_request(GPIO_FN_VIO0_D1, NULL);
	gpio_request(GPIO_FN_VIO0_D0, NULL);

	platform_resource_setup_memory(&kfr2r09_ceu_device, "ceu", 4 << 20);

	/* SDHI0 connected to yc304 */
	gpio_request(GPIO_FN_SDHI0CD, NULL);
	gpio_request(GPIO_FN_SDHI0D3, NULL);
	gpio_request(GPIO_FN_SDHI0D2, NULL);
	gpio_request(GPIO_FN_SDHI0D1, NULL);
	gpio_request(GPIO_FN_SDHI0D0, NULL);
	gpio_request(GPIO_FN_SDHI0CMD, NULL);
	gpio_request(GPIO_FN_SDHI0CLK, NULL);

	return platform_add_devices(kfr2r09_devices,
				    ARRAY_SIZE(kfr2r09_devices));
}
device_initcall(kfr2r09_devices_setup);

/* Return the board specific boot mode pin configuration */
static int kfr2r09_mode_pins(void)
{
	/* MD0=1, MD1=1, MD2=0: Clock Mode 3
	 * MD3=0: 16-bit Area0 Bus Width
	 * MD5=1: Little Endian
	 * MD8=1: Test Mode Disabled
	 */
	return MODE_PIN0 | MODE_PIN1 | MODE_PIN5 | MODE_PIN8;
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_kfr2r09 __initmv = {
	.mv_name		= "kfr2r09",
	.mv_mode_pins		= kfr2r09_mode_pins,
};
