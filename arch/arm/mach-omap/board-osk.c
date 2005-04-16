/*
 * linux/arch/arm/mach-omap/board-osk.c
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
#include <linux/device.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/gpio.h>
#include <asm/arch/usb.h>
#include <asm/arch/mux.h>
#include <asm/arch/tc.h>

#include "common.h"

static struct map_desc osk5912_io_desc[] __initdata = {
{ OMAP_OSK_NOR_FLASH_BASE, OMAP_OSK_NOR_FLASH_START, OMAP_OSK_NOR_FLASH_SIZE,
	MT_DEVICE },
};

static int __initdata osk_serial_ports[OMAP_MAX_NR_PORTS] = {1, 0, 0};

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

static struct platform_device *osk5912_devices[] __initdata = {
	&osk5912_smc91x_device,
	&osk5912_cf_device,
};

static void __init osk_init_smc91x(void)
{
	if ((omap_request_gpio(0)) < 0) {
		printk("Error requesting gpio 0 for smc91x irq\n");
		return;
	}
	omap_set_gpio_edge_ctrl(0, OMAP_GPIO_RISING_EDGE);

	/* Check EMIFS wait states to fix errors with SMC_GET_PKT_HDR */
	EMIFS_CCS(1) |= 0x2;
}

static void __init osk_init_cf(void)
{
	omap_cfg_reg(M7_1610_GPIO62);
	if ((omap_request_gpio(62)) < 0) {
		printk("Error requesting gpio 62 for CF irq\n");
		return;
	}
	/* it's really active-low */
	omap_set_gpio_edge_ctrl(62, OMAP_GPIO_FALLING_EDGE);
}

void osk_init_irq(void)
{
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

static struct omap_board_config_kernel osk_config[] = {
	{ OMAP_TAG_USB,           &osk_usb_config },
};

static void __init osk_init(void)
{
	platform_add_devices(osk5912_devices, ARRAY_SIZE(osk5912_devices));
	omap_board_config = osk_config;
	omap_board_config_size = ARRAY_SIZE(osk_config);
	USB_TRANSCEIVER_CTRL_REG |= (3 << 1);
}

static void __init osk_map_io(void)
{
	omap_map_io();
	iotable_init(osk5912_io_desc, ARRAY_SIZE(osk5912_io_desc));
	omap_serial_init(osk_serial_ports);
}

MACHINE_START(OMAP_OSK, "TI-OSK")
	MAINTAINER("Dirk Behme <dirk.behme@de.bosch.com>")
	BOOT_MEM(0x10000000, 0xfff00000, 0xfef00000)
	BOOT_PARAMS(0x10000100)
	MAPIO(osk_map_io)
	INITIRQ(osk_init_irq)
	INIT_MACHINE(osk_init)
	.timer		= &omap_timer,
MACHINE_END
