/*
 * linux/arch/arm/mach-omap1/board-osk.c
 *
 * Board specific init for OMAP5912 OSK
 *
 * Written by Dirk Behme <dirk.behme@de.bosch.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/input.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <asm/arch/gpio.h>
#include <asm/arch/usb.h>
#include <asm/arch/mux.h>
#include <asm/arch/tc.h>
#include <asm/arch/keypad.h>
#include <asm/arch/common.h>
#include <asm/arch/mcbsp.h>
#include <asm/arch/omap-alsa.h>

static int osk_keymap[] = {
	KEY(0, 0, KEY_F1),
	KEY(0, 3, KEY_UP),
	KEY(1, 1, KEY_LEFTCTRL),
	KEY(1, 2, KEY_LEFT),
	KEY(2, 0, KEY_SPACE),
	KEY(2, 1, KEY_ESC),
	KEY(2, 2, KEY_DOWN),
	KEY(3, 2, KEY_ENTER),
	KEY(3, 3, KEY_RIGHT),
	0
};


static struct mtd_partition osk_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
	      .name		= "bootloader",
	      .offset		= 0,
	      .size		= SZ_128K,
	      .mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next sector */
	{
	      .name		= "params",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_128K,
	      .mask_flags	= 0,
	}, {
	      .name		= "kernel",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_2M,
	      .mask_flags	= 0
	}, {
	      .name		= "filesystem",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= MTDPART_SIZ_FULL,
	      .mask_flags	= 0
	}
};

static struct flash_platform_data osk_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= osk_partitions,
	.nr_parts	= ARRAY_SIZE(osk_partitions),
};

static struct resource osk_flash_resource = {
	/* this is on CS3, wherever it's mapped */
	.flags		= IORESOURCE_MEM,
};

static struct platform_device osk5912_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &osk_flash_data,
	},
	.num_resources	= 1,
	.resource	= &osk_flash_resource,
};

static struct resource osk5912_smc91x_resources[] = {
	[0] = {
		.start	= OMAP_OSK_ETHR_START,		/* Physical */
		.end	= OMAP_OSK_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_GPIO_IRQ(0),
		.end	= OMAP_GPIO_IRQ(0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device osk5912_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(osk5912_smc91x_resources),
	.resource	= osk5912_smc91x_resources,
};

static struct resource osk5912_cf_resources[] = {
	[0] = {
		.start	= OMAP_GPIO_IRQ(62),
		.end	= OMAP_GPIO_IRQ(62),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device osk5912_cf_device = {
	.name		= "omap_cf",
	.id		= -1,
	.dev = {
		.platform_data	= (void *) 2 /* CS2 */,
	},
	.num_resources	= ARRAY_SIZE(osk5912_cf_resources),
	.resource	= osk5912_cf_resources,
};

#define DEFAULT_BITPERSAMPLE 16

static struct omap_mcbsp_reg_cfg mcbsp_regs = {
	.spcr2 = FREE | FRST | GRST | XRST | XINTM(3),
	.spcr1 = RINTM(3) | RRST,
	.rcr2 = RPHASE | RFRLEN2(OMAP_MCBSP_WORD_8) |
	    RWDLEN2(OMAP_MCBSP_WORD_16) | RDATDLY(0),
	.rcr1 = RFRLEN1(OMAP_MCBSP_WORD_8) | RWDLEN1(OMAP_MCBSP_WORD_16),
	.xcr2 = XPHASE | XFRLEN2(OMAP_MCBSP_WORD_8) |
	    XWDLEN2(OMAP_MCBSP_WORD_16) | XDATDLY(0) | XFIG,
	.xcr1 = XFRLEN1(OMAP_MCBSP_WORD_8) | XWDLEN1(OMAP_MCBSP_WORD_16),
	.srgr1 = FWID(DEFAULT_BITPERSAMPLE - 1),
	.srgr2 = GSYNC | CLKSP | FSGM | FPER(DEFAULT_BITPERSAMPLE * 2 - 1),
	/*.pcr0 = FSXM | FSRM | CLKXM | CLKRM | CLKXP | CLKRP,*/ /* mcbsp: master */
	.pcr0 = CLKXP | CLKRP,  /* mcbsp: slave */
};

static struct omap_alsa_codec_config alsa_config = {
	.name			= "OSK AIC23",
	.mcbsp_regs_alsa	= &mcbsp_regs,
	.codec_configure_dev	= NULL, // aic23_configure,
	.codec_set_samplerate	= NULL, // aic23_set_samplerate,
	.codec_clock_setup	= NULL, // aic23_clock_setup,
	.codec_clock_on		= NULL, // aic23_clock_on,
	.codec_clock_off	= NULL, // aic23_clock_off,
	.get_default_samplerate	= NULL, // aic23_get_default_samplerate,
};

static struct platform_device osk5912_mcbsp1_device = {
	.name	= "omap_alsa_mcbsp",
 	.id	= 1,
	.dev = {
		.platform_data	= &alsa_config,
	},
};

static struct resource osk5912_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_kp_platform_data osk_kp_data = {
	.rows	= 8,
	.cols	= 8,
	.keymap = osk_keymap,
};

static struct platform_device osk5912_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &osk_kp_data,
	},
	.num_resources	= ARRAY_SIZE(osk5912_kp_resources),
	.resource	= osk5912_kp_resources,
};

static struct platform_device osk5912_lcd_device = {
	.name		= "lcd_osk",
	.id		= -1,
};

static struct platform_device *osk5912_devices[] __initdata = {
	&osk5912_flash_device,
	&osk5912_smc91x_device,
	&osk5912_cf_device,
	&osk5912_mcbsp1_device,
	&osk5912_kp_device,
	&osk5912_lcd_device,
};

static void __init osk_init_smc91x(void)
{
	if ((omap_request_gpio(0)) < 0) {
		printk("Error requesting gpio 0 for smc91x irq\n");
		return;
	}

	/* Check EMIFS wait states to fix errors with SMC_GET_PKT_HDR */
	EMIFS_CCS(1) |= 0x3;
}

static void __init osk_init_cf(void)
{
	omap_cfg_reg(M7_1610_GPIO62);
	if ((omap_request_gpio(62)) < 0) {
		printk("Error requesting gpio 62 for CF irq\n");
		return;
	}
	/* the CF I/O IRQ is really active-low */
	set_irq_type(OMAP_GPIO_IRQ(62), IRQT_FALLING);
}

static void __init osk_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
	osk_init_smc91x();
	osk_init_cf();
}

static struct omap_usb_config osk_usb_config __initdata = {
	/* has usb host connector (A) ... for development it can also
	 * be used, with a NONSTANDARD gender-bending cable/dongle, as
	 * a peripheral.
	 */
#ifdef	CONFIG_USB_GADGET_OMAP
	.register_dev	= 1,
	.hmc_mode	= 0,
#else
	.register_host	= 1,
	.hmc_mode	= 16,
	.rwc		= 1,
#endif
	.pins[0]	= 2,
};

static struct omap_uart_config osk_uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

static struct omap_lcd_config osk_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel osk_config[] = {
	{ OMAP_TAG_USB,           &osk_usb_config },
	{ OMAP_TAG_UART,		&osk_uart_config },
	{ OMAP_TAG_LCD,			&osk_lcd_config },
};

#ifdef	CONFIG_OMAP_OSK_MISTRAL

#ifdef	CONFIG_PM
static irqreturn_t
osk_mistral_wake_interrupt(int irq, void *ignored, struct pt_regs *regs)
{
	return IRQ_HANDLED;
}
#endif

static void __init osk_mistral_init(void)
{
	/* FIXME here's where to feed in framebuffer, touchpad, and
	 * keyboard setup ...  not in the drivers for those devices!
	 *
	 * NOTE:  we could actually tell if there's a Mistral board
	 * attached, e.g. by trying to read something from the ads7846.
	 * But this is too early for that...
	 */

	/* the sideways button (SW1) is for use as a "wakeup" button */
	omap_cfg_reg(N15_1610_MPUIO2);
	if (omap_request_gpio(OMAP_MPUIO(2)) == 0) {
		int ret = 0;
		omap_set_gpio_direction(OMAP_MPUIO(2), 1);
		set_irq_type(OMAP_GPIO_IRQ(OMAP_MPUIO(2)), IRQT_RISING);
#ifdef	CONFIG_PM
		/* share the IRQ in case someone wants to use the
		 * button for more than wakeup from system sleep.
		 */
		ret = request_irq(OMAP_GPIO_IRQ(OMAP_MPUIO(2)),
				&osk_mistral_wake_interrupt,
				SA_SHIRQ, "mistral_wakeup",
				&osk_mistral_wake_interrupt);
		if (ret != 0) {
			omap_free_gpio(OMAP_MPUIO(2));
			printk(KERN_ERR "OSK+Mistral: no wakeup irq, %d?\n",
				ret);
		} else
			enable_irq_wake(OMAP_GPIO_IRQ(OMAP_MPUIO(2)));
#endif
	} else
		printk(KERN_ERR "OSK+Mistral: wakeup button is awol\n");
}
#else
static void __init osk_mistral_init(void) { }
#endif

#define EMIFS_CS3_VAL	(0x88013141)

static void __init osk_init(void)
{
	/* Workaround for wrong CS3 (NOR flash) timing
	 * There are some U-Boot versions out there which configure
	 * wrong CS3 memory timings. This mainly leads to CRC
	 * or similiar errors if you use NOR flash (e.g. with JFFS2)
	 */
	if (EMIFS_CCS(3) != EMIFS_CS3_VAL)
		EMIFS_CCS(3) = EMIFS_CS3_VAL;

	osk_flash_resource.end = osk_flash_resource.start = omap_cs3_phys();
	osk_flash_resource.end += SZ_32M - 1;
	platform_add_devices(osk5912_devices, ARRAY_SIZE(osk5912_devices));
	omap_board_config = osk_config;
	omap_board_config_size = ARRAY_SIZE(osk_config);
	USB_TRANSCEIVER_CTRL_REG |= (3 << 1);

	omap_serial_init();
	osk_mistral_init();
}

static void __init osk_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(OMAP_OSK, "TI-OSK")
	/* Maintainer: Dirk Behme <dirk.behme@de.bosch.com> */
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= osk_map_io,
	.init_irq	= osk_init_irq,
	.init_machine	= osk_init,
	.timer		= &omap_timer,
MACHINE_END
