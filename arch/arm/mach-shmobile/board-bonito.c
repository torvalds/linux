/*
 * bonito board support
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Copyright (C) 2011 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/videodev2.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-l2x0.h>
#include <mach/r8a7740.h>
#include <mach/irqs.h>
#include <video/sh_mobile_lcdc.h>

/*
 * CS	Address		device			note
 *----------------------------------------------------------------
 * 0	0x0000_0000	NOR Flash (64MB)	SW12 : bit3 = OFF
 * 2	0x0800_0000	ExtNOR (64MB)		SW12 : bit3 = OFF
 * 4			-
 * 5A			-
 * 5B	0x1600_0000	SRAM (8MB)
 * 6	0x1800_0000	FPGA (64K)
 *	0x1801_0000	Ether (4KB)
 *	0x1801_1000	USB (4KB)
 */

/*
 * SW12
 *
 *	bit1			bit2			bit3
 *----------------------------------------------------------------------------
 * ON	NOR WriteProtect	NAND WriteProtect	CS0 ExtNOR / CS2 NOR
 * OFF	NOR Not WriteProtect	NAND Not WriteProtect	CS0 NOR    / CS2 ExtNOR
 */

/*
 * SCIFA5 (CN42)
 *
 * S38.3 = ON
 * S39.6 = ON
 * S43.1 = ON
 */

/*
 * LCDC0 (CN3/CN4/CN7)
 *
 * S38.1 = OFF
 * S38.2 = OFF
 */

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

/*
 * FPGA
 */
#define IRQSR0		0x0020
#define IRQSR1		0x0022
#define IRQMR0		0x0030
#define IRQMR1		0x0032
#define BUSSWMR1	0x0070
#define BUSSWMR2	0x0072
#define BUSSWMR3	0x0074
#define BUSSWMR4	0x0076

#define LCDCR		0x10B4
#define DEVRSTCR1	0x10D0
#define DEVRSTCR2	0x10D2
#define A1MDSR		0x10E0
#define BVERR		0x1100

/* FPGA IRQ */
#define FPGA_IRQ_BASE		(512)
#define FPGA_IRQ0		(FPGA_IRQ_BASE)
#define FPGA_IRQ1		(FPGA_IRQ_BASE + 16)
#define FPGA_ETH_IRQ		(FPGA_IRQ0 + 15)
static u16 bonito_fpga_read(u32 offset)
{
	return __raw_readw(IOMEM(0xf0003000) + offset);
}

static void bonito_fpga_write(u32 offset, u16 val)
{
	__raw_writew(val, IOMEM(0xf0003000) + offset);
}

static void bonito_fpga_irq_disable(struct irq_data *data)
{
	unsigned int irq = data->irq;
	u32 addr = (irq < 1016) ? IRQMR0 : IRQMR1;
	int shift = irq % 16;

	bonito_fpga_write(addr, bonito_fpga_read(addr) | (1 << shift));
}

static void bonito_fpga_irq_enable(struct irq_data *data)
{
	unsigned int irq = data->irq;
	u32 addr = (irq < 1016) ? IRQMR0 : IRQMR1;
	int shift = irq % 16;

	bonito_fpga_write(addr, bonito_fpga_read(addr) & ~(1 << shift));
}

static struct irq_chip bonito_fpga_irq_chip __read_mostly = {
	.name		= "bonito FPGA",
	.irq_mask	= bonito_fpga_irq_disable,
	.irq_unmask	= bonito_fpga_irq_enable,
};

static void bonito_fpga_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	u32 val =  bonito_fpga_read(IRQSR1) << 16 |
		   bonito_fpga_read(IRQSR0);
	u32 mask = bonito_fpga_read(IRQMR1) << 16 |
		   bonito_fpga_read(IRQMR0);

	int i;

	val &= ~mask;

	for (i = 0; i < 32; i++) {
		if (!(val & (1 << i)))
			continue;

		generic_handle_irq(FPGA_IRQ_BASE + i);
	}
}

static void bonito_fpga_init(void)
{
	int i;

	bonito_fpga_write(IRQMR0, 0xffff); /* mask all */
	bonito_fpga_write(IRQMR1, 0xffff); /* mask all */

	/* Device reset */
	bonito_fpga_write(DEVRSTCR1,
		   (1 << 2));	/* Eth */

	/* FPGA irq require special handling */
	for (i = FPGA_IRQ_BASE; i < FPGA_IRQ_BASE + 32; i++) {
		irq_set_chip_and_handler_name(i, &bonito_fpga_irq_chip,
					      handle_level_irq, "level");
		set_irq_flags(i, IRQF_VALID); /* yuck */
	}

	irq_set_chained_handler(evt2irq(0x0340), bonito_fpga_irq_demux);
	irq_set_irq_type(evt2irq(0x0340), IRQ_TYPE_LEVEL_LOW);
}

/*
* PMIC settings
*
* FIXME
*
* bonito board needs some settings by pmic which use i2c access.
* pmic settings use device_initcall() here for use it.
*/
static __u8 *pmic_settings = NULL;
static __u8 pmic_do_2A[] = {
	0x1C, 0x09,
	0x1A, 0x80,
	0xff, 0xff,
};

static int __init pmic_init(void)
{
	struct i2c_adapter *a = i2c_get_adapter(0);
	struct i2c_msg msg;
	__u8 buf[2];
	int i, ret;

	if (!pmic_settings)
		return 0;
	if (!a)
		return 0;

	msg.addr	= 0x46;
	msg.buf		= buf;
	msg.len		= 2;
	msg.flags	= 0;

	for (i = 0; ; i += 2) {
		buf[0] = pmic_settings[i + 0];
		buf[1] = pmic_settings[i + 1];

		if ((0xff == buf[0]) && (0xff == buf[1]))
			break;

		ret = i2c_transfer(a, &msg, 1);
		if (ret < 0) {
			pr_err("i2c transfer fail\n");
			break;
		}
	}

	return 0;
}
device_initcall(pmic_init);

/*
 * LCDC0
 */
static const struct fb_videomode lcdc0_mode = {
	.name		= "WVGA Panel",
	.xres		= 800,
	.yres		= 480,
	.left_margin	= 88,
	.right_margin	= 40,
	.hsync_len	= 128,
	.upper_margin	= 20,
	.lower_margin	= 5,
	.vsync_len	= 5,
	.sync		= 0,
};

static struct sh_mobile_lcdc_info lcdc0_info = {
	.clock_source	= LCDC_CLK_BUS,
	.ch[0] = {
		.chan			= LCDC_CHAN_MAINLCD,
		.fourcc = V4L2_PIX_FMT_RGB565,
		.interface_type		= RGB24,
		.clock_divider		= 5,
		.flags			= 0,
		.lcd_modes		= &lcdc0_mode,
		.num_modes		= 1,
		.panel_cfg = {
			.width	= 152,
			.height = 91,
		},
	},
};

static struct resource lcdc0_resources[] = {
	[0] = {
		.name	= "LCDC0",
		.start	= 0xfe940000,
		.end	= 0xfe943fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x0580),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc0_device = {
	.name		= "sh_mobile_lcdc_fb",
	.id		= 0,
	.resource	= lcdc0_resources,
	.num_resources	= ARRAY_SIZE(lcdc0_resources),
	.dev	= {
		.platform_data	= &lcdc0_info,
		.coherent_dma_mask = ~0,
	},
};

static const struct pinctrl_map lcdc0_pinctrl_map[] = {
	/* LCD0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-r8a7740",
				  "lcd0_data24_1", "lcd0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-r8a7740",
				  "lcd0_lclk_1", "lcd0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_lcdc_fb.0", "pfc-r8a7740",
				  "lcd0_sync", "lcd0"),
};

/*
 * SMSC 9221
 */
static struct resource smsc_resources[] = {
	[0] = {
		.start		= 0x18010000,
		.end		= 0x18011000 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= FPGA_ETH_IRQ,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc_platdata = {
	.flags		= SMSC911X_USE_16BIT,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device smsc_device = {
	.name		= "smsc911x",
	.dev  = {
		.platform_data = &smsc_platdata,
	},
	.resource	= smsc_resources,
	.num_resources	= ARRAY_SIZE(smsc_resources),
};

/*
 * base board devices
 */
static struct platform_device *bonito_base_devices[] __initdata = {
	&lcdc0_device,
	&smsc_device,
};

/*
 * map I/O
 */
static struct map_desc bonito_io_desc[] __initdata = {
	/*
	 * for FPGA (0x1800000-0x19ffffff)
	 * 0x18000000-0x18002000 -> 0xf0003000-0xf0005000
	 */
	{
		.virtual	= 0xf0003000,
		.pfn		= __phys_to_pfn(0x18000000),
		.length		= PAGE_SIZE * 2,
		.type		= MT_DEVICE_NONSHARED
	}
};

static void __init bonito_map_io(void)
{
	r8a7740_map_io();
	iotable_init(bonito_io_desc, ARRAY_SIZE(bonito_io_desc));
}

/*
 * board init
 */
#define BIT_ON(sw, bit)		(sw & (1 << bit))
#define BIT_OFF(sw, bit)	(!(sw & (1 << bit)))

#define VCCQ1CR		IOMEM(0xE6058140)
#define VCCQ1LCDCR	IOMEM(0xE6058186)

/*
 * HACK: The FPGA mappings should be associated with the FPGA device, but we
 * don't have one at the moment. Associate them with the PFC device to make
 * sure they will be applied.
 */
static const struct pinctrl_map fpga_pinctrl_map[] = {
	/* FPGA */
	PIN_MAP_MUX_GROUP_DEFAULT("pfc-r8a7740", "pfc-r8a7740",
				  "bsc_cs5a_0", "bsc"),
	PIN_MAP_MUX_GROUP_DEFAULT("pfc-r8a7740", "pfc-r8a7740",
				  "bsc_cs5b", "bsc"),
	PIN_MAP_MUX_GROUP_DEFAULT("pfc-r8a7740", "pfc-r8a7740",
				  "bsc_cs6a", "bsc"),
	PIN_MAP_MUX_GROUP_DEFAULT("pfc-r8a7740", "pfc-r8a7740",
				  "intc_irq10", "intc"),
};

static const struct pinctrl_map scifa5_pinctrl_map[] = {
	/* SCIFA5 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.5", "pfc-r8a7740",
				  "scifa5_data_2", "scifa5"),
};

static void __init bonito_init(void)
{
	u16 val;

	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	pinctrl_register_mappings(fpga_pinctrl_map,
				  ARRAY_SIZE(fpga_pinctrl_map));
	r8a7740_pinmux_init();
	bonito_fpga_init();

	pmic_settings = pmic_do_2A;

	/*
	 * core board settings
	 */

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 32K*8way */
	l2x0_init(IOMEM(0xf0002000), 0x40440000, 0x82000fff);
#endif

	r8a7740_add_standard_devices();

	/*
	 * base board settings
	 */
	gpio_request_one(176, GPIOF_IN, NULL);
	if (!gpio_get_value(176)) {
		u16 bsw2;
		u16 bsw3;
		u16 bsw4;

		val = bonito_fpga_read(BVERR);
		pr_info("bonito version: cpu %02x, base %02x\n",
			((val >> 8) & 0xFF),
			((val >> 0) & 0xFF));

		bsw2 = bonito_fpga_read(BUSSWMR2);
		bsw3 = bonito_fpga_read(BUSSWMR3);
		bsw4 = bonito_fpga_read(BUSSWMR4);

		/*
		 * SCIFA5 (CN42)
		 */
		if (BIT_OFF(bsw2, 1) &&	/* S38.3 = ON */
		    BIT_OFF(bsw3, 9) &&	/* S39.6 = ON */
		    BIT_OFF(bsw4, 4)) {	/* S43.1 = ON */
			pinctrl_register_mappings(scifa5_pinctrl_map,
						  ARRAY_SIZE(scifa5_pinctrl_map));
		}

		/*
		 * LCDC0 (CN3)
		 */
		if (BIT_ON(bsw2, 3) &&	/* S38.1 = OFF */
		    BIT_ON(bsw2, 2)) {	/* S38.2 = OFF */
			pinctrl_register_mappings(lcdc0_pinctrl_map,
						  ARRAY_SIZE(lcdc0_pinctrl_map));
			gpio_request(GPIO_FN_LCDC0_SELECT, NULL);

			gpio_request_one(61, GPIOF_OUT_INIT_HIGH,
					 NULL); /* LCDDON */

			/* backlight on */
			bonito_fpga_write(LCDCR, 1);

			/*  drivability Max */
			__raw_writew(0x00FF , VCCQ1LCDCR);
			__raw_writew(0xFFFF , VCCQ1CR);
		}

		platform_add_devices(bonito_base_devices,
				     ARRAY_SIZE(bonito_base_devices));
	}
}

static void __init bonito_earlytimer_init(void)
{
	u16 val;
	u8 md_ck = 0;

	/* read MD_CK value */
	val = bonito_fpga_read(A1MDSR);
	if (val & (1 << 10))
		md_ck |= MD_CK2;
	if (val & (1 << 9))
		md_ck |= MD_CK1;
	if (val & (1 << 8))
		md_ck |= MD_CK0;

	r8a7740_clock_init(md_ck);
	shmobile_earlytimer_init();
}

static void __init bonito_add_early_devices(void)
{
	r8a7740_add_early_devices();
}

MACHINE_START(BONITO, "bonito")
	.map_io		= bonito_map_io,
	.init_early	= bonito_add_early_devices,
	.init_irq	= r8a7740_init_irq,
	.handle_irq	= shmobile_handle_irq_intc,
	.init_machine	= bonito_init,
	.init_late	= shmobile_init_late,
	.init_time	= bonito_earlytimer_init,
MACHINE_END
