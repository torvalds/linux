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
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <asm/clock.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/sh_keysc.h>
#include <cpu/sh7724.h>

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

static struct platform_device *kfr2r09_devices[] __initdata = {
	&kfr2r09_nor_flash_device,
	&kfr2r09_sh_keysc_device,
};

#define BSC_CS0BCR 0xfec10004
#define BSC_CS0WCR 0xfec10024

static int __init kfr2r09_devices_setup(void)
{
	/* enable SCIF1 serial port for YC401 console support */
	gpio_request(GPIO_FN_SCIF1_RXD, NULL);
	gpio_request(GPIO_FN_SCIF1_TXD, NULL);

	/* setup NOR flash at CS0 */
	ctrl_outl(0x36db0400, BSC_CS0BCR);
	ctrl_outl(0x00000500, BSC_CS0WCR);

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
