// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-pxa/mainstone.c
 *
 *  Support for the Intel HCDDBBVA0 Development Platform.
 *  (go figure how they came up with such name...)
 *
 *  Author:	Nicolas Pitre
 *  Created:	Nov 05, 2002
 *  Copyright:	MontaVista Software Inc.
 */
#include <linux/gpio.h>
#include <linux/gpio/gpio-reg.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/smc91x.h>
#include <linux/platform_data/i2c-pxa.h>
#include <linux/slab.h>
#include <linux/leds.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <linux/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include "pxa27x.h"
#include <mach/mainstone.h>
#include <mach/audio.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include <linux/platform_data/keypad-pxa27x.h>
#include <mach/smemc.h>

#include "generic.h"
#include "devices.h"

static unsigned long mainstone_pin_config[] = {
	/* Chip Select */
	GPIO15_nCS_1,

	/* LCD - 16bpp Active TFT */
	GPIOxx_LCD_TFT_16BPP,
	GPIO16_PWM0_OUT,	/* Backlight */

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO112_MMC_CMD,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,

	/* USB Host Port 1 */
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,

	/* PC Card */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO79_PSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO45_AC97_SYSCLK,

	/* Keypad */
	GPIO93_KP_DKIN_0,
	GPIO94_KP_DKIN_1,
	GPIO95_KP_DKIN_2,
	GPIO100_KP_MKIN_0	| WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1	| WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2	| WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3	| WAKEUP_ON_LEVEL_HIGH,
	GPIO98_KP_MKIN_4	| WAKEUP_ON_LEVEL_HIGH,
	GPIO99_KP_MKIN_5	| WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,
	GPIO96_KP_MKOUT_6,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* GPIO */
	GPIO1_GPIO | WAKEUP_ON_EDGE_BOTH,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= (MST_ETH_PHYS + 0x300),
		.end	= (MST_ETH_PHYS + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MAINSTONE_IRQ(3),
		.end	= MAINSTONE_IRQ(3),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct smc91x_platdata mainstone_smc91x_info = {
	.flags	= SMC91X_USE_8BIT | SMC91X_USE_16BIT | SMC91X_USE_32BIT |
		  SMC91X_NOWAIT | SMC91X_USE_DMA,
	.pxa_u16_align4 = true,
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
	.dev		= {
		.platform_data = &mainstone_smc91x_info,
	},
};

static int mst_audio_startup(struct snd_pcm_substream *substream, void *priv)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		MST_MSCWR2 &= ~MST_MSCWR2_AC97_SPKROFF;
	return 0;
}

static void mst_audio_shutdown(struct snd_pcm_substream *substream, void *priv)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		MST_MSCWR2 |= MST_MSCWR2_AC97_SPKROFF;
}

static long mst_audio_suspend_mask;

static void mst_audio_suspend(void *priv)
{
	mst_audio_suspend_mask = MST_MSCWR2;
	MST_MSCWR2 |= MST_MSCWR2_AC97_SPKROFF;
}

static void mst_audio_resume(void *priv)
{
	MST_MSCWR2 &= mst_audio_suspend_mask | ~MST_MSCWR2_AC97_SPKROFF;
}

static pxa2xx_audio_ops_t mst_audio_ops = {
	.startup	= mst_audio_startup,
	.shutdown	= mst_audio_shutdown,
	.suspend	= mst_audio_suspend,
	.resume		= mst_audio_resume,
};

static struct resource flash_resources[] = {
	[0] = {
		.start	= PXA_CS0_PHYS,
		.end	= PXA_CS0_PHYS + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PXA_CS1_PHYS,
		.end	= PXA_CS1_PHYS + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct mtd_partition mainstoneflash0_partitions[] = {
	{
		.name =		"Bootloader",
		.size =		0x00040000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	},{
		.name =		"Kernel",
		.size =		0x00400000,
		.offset =	0x00040000,
	},{
		.name =		"Filesystem",
		.size =		MTDPART_SIZ_FULL,
		.offset =	0x00440000
	}
};

static struct flash_platform_data mst_flash_data[2] = {
	{
		.map_name	= "cfi_probe",
		.parts		= mainstoneflash0_partitions,
		.nr_parts	= ARRAY_SIZE(mainstoneflash0_partitions),
	}, {
		.map_name	= "cfi_probe",
		.parts		= NULL,
		.nr_parts	= 0,
	}
};

static struct platform_device mst_flash_device[2] = {
	{
		.name		= "pxa2xx-flash",
		.id		= 0,
		.dev = {
			.platform_data = &mst_flash_data[0],
		},
		.resource = &flash_resources[0],
		.num_resources = 1,
	},
	{
		.name		= "pxa2xx-flash",
		.id		= 1,
		.dev = {
			.platform_data = &mst_flash_data[1],
		},
		.resource = &flash_resources[1],
		.num_resources = 1,
	},
};

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct pwm_lookup mainstone_pwm_lookup[] = {
	PWM_LOOKUP("pxa27x-pwm.0", 0, "pwm-backlight.0", NULL, 78770,
		   PWM_POLARITY_NORMAL),
};

static struct platform_pwm_backlight_data mainstone_backlight_data = {
	.max_brightness	= 1023,
	.dft_brightness	= 1023,
	.enable_gpio	= -1,
};

static struct platform_device mainstone_backlight_device = {
	.name		= "pwm-backlight",
	.dev		= {
		.parent = &pxa27x_device_pwm0.dev,
		.platform_data = &mainstone_backlight_data,
	},
};

static void __init mainstone_backlight_register(void)
{
	int ret;

	pwm_add_table(mainstone_pwm_lookup, ARRAY_SIZE(mainstone_pwm_lookup));

	ret = platform_device_register(&mainstone_backlight_device);
	if (ret) {
		printk(KERN_ERR "mainstone: failed to register backlight device: %d\n", ret);
		pwm_remove_table(mainstone_pwm_lookup,
				 ARRAY_SIZE(mainstone_pwm_lookup));
	}
}
#else
#define mainstone_backlight_register()	do { } while (0)
#endif

static struct pxafb_mode_info toshiba_ltm04c380k_mode = {
	.pixclock		= 50000,
	.xres			= 640,
	.yres			= 480,
	.bpp			= 16,
	.hsync_len		= 1,
	.left_margin		= 0x9f,
	.right_margin		= 1,
	.vsync_len		= 44,
	.upper_margin		= 0,
	.lower_margin		= 0,
	.sync			= FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mode_info toshiba_ltm035a776c_mode = {
	.pixclock		= 110000,
	.xres			= 240,
	.yres			= 320,
	.bpp			= 16,
	.hsync_len		= 4,
	.left_margin		= 8,
	.right_margin		= 20,
	.vsync_len		= 3,
	.upper_margin		= 1,
	.lower_margin		= 10,
	.sync			= FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info mainstone_pxafb_info = {
	.num_modes      	= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
};

static int mainstone_mci_init(struct device *dev, irq_handler_t mstone_detect_int, void *data)
{
	int err;

	/* make sure SD/Memory Stick multiplexer's signals
	 * are routed to MMC controller
	 */
	MST_MSCWR1 &= ~MST_MSCWR1_MS_SEL;

	err = request_irq(MAINSTONE_MMC_IRQ, mstone_detect_int, 0,
			     "MMC card detect", data);
	if (err)
		printk(KERN_ERR "mainstone_mci_init: MMC/SD: can't request MMC card detect IRQ\n");

	return err;
}

static int mainstone_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if (( 1 << vdd) & p_d->ocr_mask) {
		printk(KERN_DEBUG "%s: on\n", __func__);
		MST_MSCWR1 |= MST_MSCWR1_MMC_ON;
		MST_MSCWR1 &= ~MST_MSCWR1_MS_SEL;
	} else {
		printk(KERN_DEBUG "%s: off\n", __func__);
		MST_MSCWR1 &= ~MST_MSCWR1_MMC_ON;
	}
	return 0;
}

static void mainstone_mci_exit(struct device *dev, void *data)
{
	free_irq(MAINSTONE_MMC_IRQ, data);
}

static struct pxamci_platform_data mainstone_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 			= mainstone_mci_init,
	.setpower 		= mainstone_mci_setpower,
	.exit			= mainstone_mci_exit,
};

static void mainstone_irda_transceiver_mode(struct device *dev, int mode)
{
	unsigned long flags;

	local_irq_save(flags);
	if (mode & IR_SIRMODE) {
		MST_MSCWR1 &= ~MST_MSCWR1_IRDA_FIR;
	} else if (mode & IR_FIRMODE) {
		MST_MSCWR1 |= MST_MSCWR1_IRDA_FIR;
	}
	pxa2xx_transceiver_mode(dev, mode);
	if (mode & IR_OFF) {
		MST_MSCWR1 = (MST_MSCWR1 & ~MST_MSCWR1_IRDA_MASK) | MST_MSCWR1_IRDA_OFF;
	} else {
		MST_MSCWR1 = (MST_MSCWR1 & ~MST_MSCWR1_IRDA_MASK) | MST_MSCWR1_IRDA_FULL;
	}
	local_irq_restore(flags);
}

static struct pxaficp_platform_data mainstone_ficp_platform_data = {
	.gpio_pwdown		= -1,
	.transceiver_cap	= IR_SIRMODE | IR_FIRMODE | IR_OFF,
	.transceiver_mode	= mainstone_irda_transceiver_mode,
};

static struct gpio_keys_button gpio_keys_button[] = {
	[0] = {
		.desc	= "wakeup",
		.code	= KEY_SUSPEND,
		.type	= EV_KEY,
		.gpio	= 1,
		.wakeup	= 1,
	},
};

static struct gpio_keys_platform_data mainstone_gpio_keys = {
	.buttons	= gpio_keys_button,
	.nbuttons	= 1,
};

static struct platform_device mst_gpio_keys_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &mainstone_gpio_keys,
	},
};

static struct resource mst_cplds_resources[] = {
	[0] = {
		.start	= MST_FPGA_PHYS + 0xc0,
		.end	= MST_FPGA_PHYS + 0xe0 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PXA_GPIO_TO_IRQ(0),
		.end	= PXA_GPIO_TO_IRQ(0),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
	[2] = {
		.start	= MAINSTONE_IRQ(0),
		.end	= MAINSTONE_IRQ(15),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mst_cplds_device = {
	.name		= "pxa_cplds_irqs",
	.id		= -1,
	.resource	= &mst_cplds_resources[0],
	.num_resources	= 3,
};

static struct platform_device *platform_devices[] __initdata = {
	&smc91x_device,
	&mst_flash_device[0],
	&mst_flash_device[1],
	&mst_gpio_keys_device,
	&mst_cplds_device,
};

static struct pxaohci_platform_data mainstone_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT_ALL | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static const unsigned int mainstone_matrix_keys[] = {
	KEY(0, 0, KEY_A), KEY(1, 0, KEY_B), KEY(2, 0, KEY_C),
	KEY(3, 0, KEY_D), KEY(4, 0, KEY_E), KEY(5, 0, KEY_F),
	KEY(0, 1, KEY_G), KEY(1, 1, KEY_H), KEY(2, 1, KEY_I),
	KEY(3, 1, KEY_J), KEY(4, 1, KEY_K), KEY(5, 1, KEY_L),
	KEY(0, 2, KEY_M), KEY(1, 2, KEY_N), KEY(2, 2, KEY_O),
	KEY(3, 2, KEY_P), KEY(4, 2, KEY_Q), KEY(5, 2, KEY_R),
	KEY(0, 3, KEY_S), KEY(1, 3, KEY_T), KEY(2, 3, KEY_U),
	KEY(3, 3, KEY_V), KEY(4, 3, KEY_W), KEY(5, 3, KEY_X),
	KEY(2, 4, KEY_Y), KEY(3, 4, KEY_Z),

	KEY(0, 4, KEY_DOT),	/* . */
	KEY(1, 4, KEY_CLOSE),	/* @ */
	KEY(4, 4, KEY_SLASH),
	KEY(5, 4, KEY_BACKSLASH),
	KEY(0, 5, KEY_HOME),
	KEY(1, 5, KEY_LEFTSHIFT),
	KEY(2, 5, KEY_SPACE),
	KEY(3, 5, KEY_SPACE),
	KEY(4, 5, KEY_ENTER),
	KEY(5, 5, KEY_BACKSPACE),

	KEY(0, 6, KEY_UP),
	KEY(1, 6, KEY_DOWN),
	KEY(2, 6, KEY_LEFT),
	KEY(3, 6, KEY_RIGHT),
	KEY(4, 6, KEY_SELECT),
};

static struct matrix_keymap_data mainstone_matrix_keymap_data = {
	.keymap			= mainstone_matrix_keys,
	.keymap_size		= ARRAY_SIZE(mainstone_matrix_keys),
};

struct pxa27x_keypad_platform_data mainstone_keypad_info = {
	.matrix_key_rows	= 6,
	.matrix_key_cols	= 7,
	.matrix_keymap_data	= &mainstone_matrix_keymap_data,

	.enable_rotary0		= 1,
	.rotary0_up_key		= KEY_UP,
	.rotary0_down_key	= KEY_DOWN,

	.debounce_interval	= 30,
};

static void __init mainstone_init_keypad(void)
{
	pxa_set_keypad_info(&mainstone_keypad_info);
}
#else
static inline void mainstone_init_keypad(void) {}
#endif

static int mst_pcmcia0_irqs[11] = {
	[0 ... 10] = -1,
	[5] = MAINSTONE_S0_CD_IRQ,
	[8] = MAINSTONE_S0_STSCHG_IRQ,
	[10] = MAINSTONE_S0_IRQ,
};

static int mst_pcmcia1_irqs[11] = {
	[0 ... 10] = -1,
	[5] = MAINSTONE_S1_CD_IRQ,
	[8] = MAINSTONE_S1_STSCHG_IRQ,
	[10] = MAINSTONE_S1_IRQ,
};

static struct gpiod_lookup_table mainstone_pcmcia_gpio_table = {
	.dev_id = "pxa2xx-pcmcia",
	.table = {
		GPIO_LOOKUP("mst-pcmcia0",  0, "a0vpp",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia0",  1, "a1vpp",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia0",  2, "a0vcc",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia0",  3, "a1vcc",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia0",  4, "areset",  GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia0",  5, "adetect", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("mst-pcmcia0",  6, "avs1",    GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("mst-pcmcia0",  7, "avs2",    GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("mst-pcmcia0",  8, "abvd1",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia0",  9, "abvd2",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia0", 10, "aready",  GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia1",  0, "b0vpp",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia1",  1, "b1vpp",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia1",  2, "b0vcc",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia1",  3, "b1vcc",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia1",  4, "breset",  GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia1",  5, "bdetect", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("mst-pcmcia1",  6, "bvs1",    GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("mst-pcmcia1",  7, "bvs2",    GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("mst-pcmcia1",  8, "bbvd1",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia1",  9, "bbvd2",   GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("mst-pcmcia1", 10, "bready",  GPIO_ACTIVE_HIGH),
		{ },
	},
};

static void __init mainstone_init(void)
{
	int SW7 = 0;  /* FIXME: get from SCR (Mst doc section 3.2.1.1) */

	pxa2xx_mfp_config(ARRAY_AND_SIZE(mainstone_pin_config));

	/* Register board control register(s) as GPIOs */
	gpio_reg_init(NULL, (void __iomem *)&MST_PCMCIA0, -1, 11,
		      "mst-pcmcia0", MST_PCMCIA_INPUTS, 0, NULL,
		      NULL, mst_pcmcia0_irqs);
	gpio_reg_init(NULL, (void __iomem *)&MST_PCMCIA1, -1, 11,
		      "mst-pcmcia1", MST_PCMCIA_INPUTS, 0, NULL,
		      NULL, mst_pcmcia1_irqs);
	gpiod_add_lookup_table(&mainstone_pcmcia_gpio_table);

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	mst_flash_data[0].width = (__raw_readl(BOOT_DEF) & 1) ? 2 : 4;
	mst_flash_data[1].width = 4;

	/* Compensate for SW7 which swaps the flash banks */
	mst_flash_data[SW7].name = "processor-flash";
	mst_flash_data[SW7 ^ 1].name = "mainboard-flash";

	printk(KERN_NOTICE "Mainstone configured to boot from %s\n",
	       mst_flash_data[0].name);

	/* system bus arbiter setting
	 * - Core_Park
	 * - LCD_wt:DMA_wt:CORE_Wt = 2:3:4
	 */
	ARB_CNTRL = ARB_CORE_PARK | 0x234;

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

	/* reading Mainstone's "Virtual Configuration Register"
	   might be handy to select LCD type here */
	if (0)
		mainstone_pxafb_info.modes = &toshiba_ltm04c380k_mode;
	else
		mainstone_pxafb_info.modes = &toshiba_ltm035a776c_mode;

	pxa_set_fb_info(NULL, &mainstone_pxafb_info);
	mainstone_backlight_register();

	pxa_set_mci_info(&mainstone_mci_platform_data);
	pxa_set_ficp_info(&mainstone_ficp_platform_data);
	pxa_set_ohci_info(&mainstone_ohci_platform_data);
	pxa_set_i2c_info(NULL);
	pxa_set_ac97_info(&mst_audio_ops);

	mainstone_init_keypad();
}


static struct map_desc mainstone_io_desc[] __initdata = {
  	{	/* CPLD */
		.virtual	=  MST_FPGA_VIRT,
		.pfn		= __phys_to_pfn(MST_FPGA_PHYS),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}
};

static void __init mainstone_map_io(void)
{
	pxa27x_map_io();
	iotable_init(mainstone_io_desc, ARRAY_SIZE(mainstone_io_desc));

 	/*	for use I SRAM as framebuffer.	*/
 	PSLR |= 0xF04;
 	PCFR = 0x66;
}

/*
 * Driver for the 8 discrete LEDs available for general use:
 * Note: bits [15-8] are used to enable/blank the 8 7 segment hex displays
 * so be sure to not monkey with them here.
 */

#if defined(CONFIG_NEW_LEDS) && defined(CONFIG_LEDS_CLASS)
struct mainstone_led {
	struct led_classdev	cdev;
	u8			mask;
};

/*
 * The triggers lines up below will only be used if the
 * LED triggers are compiled in.
 */
static const struct {
	const char *name;
	const char *trigger;
} mainstone_leds[] = {
	{ "mainstone:D28", "default-on", },
	{ "mainstone:D27", "cpu0", },
	{ "mainstone:D26", "heartbeat" },
	{ "mainstone:D25", },
	{ "mainstone:D24", },
	{ "mainstone:D23", },
	{ "mainstone:D22", },
	{ "mainstone:D21", },
};

static void mainstone_led_set(struct led_classdev *cdev,
			      enum led_brightness b)
{
	struct mainstone_led *led = container_of(cdev,
					 struct mainstone_led, cdev);
	u32 reg = MST_LEDCTRL;

	if (b != LED_OFF)
		reg |= led->mask;
	else
		reg &= ~led->mask;

	MST_LEDCTRL = reg;
}

static enum led_brightness mainstone_led_get(struct led_classdev *cdev)
{
	struct mainstone_led *led = container_of(cdev,
					 struct mainstone_led, cdev);
	u32 reg = MST_LEDCTRL;

	return (reg & led->mask) ? LED_FULL : LED_OFF;
}

static int __init mainstone_leds_init(void)
{
	int i;

	if (!machine_is_mainstone())
		return -ENODEV;

	/* All ON */
	MST_LEDCTRL |= 0xff;
	for (i = 0; i < ARRAY_SIZE(mainstone_leds); i++) {
		struct mainstone_led *led;

		led = kzalloc(sizeof(*led), GFP_KERNEL);
		if (!led)
			break;

		led->cdev.name = mainstone_leds[i].name;
		led->cdev.brightness_set = mainstone_led_set;
		led->cdev.brightness_get = mainstone_led_get;
		led->cdev.default_trigger = mainstone_leds[i].trigger;
		led->mask = BIT(i);

		if (led_classdev_register(NULL, &led->cdev) < 0) {
			kfree(led);
			break;
		}
	}

	return 0;
}

/*
 * Since we may have triggers on any subsystem, defer registration
 * until after subsystem_init.
 */
fs_initcall(mainstone_leds_init);
#endif

MACHINE_START(MAINSTONE, "Intel HCDDBBVA0 Development Platform (aka Mainstone)")
	/* Maintainer: MontaVista Software Inc. */
	.atag_offset	= 0x100,	/* BLOB boot parameter setting */
	.map_io		= mainstone_map_io,
	.nr_irqs	= MAINSTONE_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= mainstone_init,
	.restart	= pxa_restart,
MACHINE_END
