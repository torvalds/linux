/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/init.h>
#include <linux/irqdomain.h>
#include <linux/micrel_phy.h>
#include <linux/mxsfb.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/common.h>

static struct fb_videomode mx23evk_video_modes[] = {
	{
		.name		= "Samsung-LMS430HF02",
		.refresh	= 60,
		.xres		= 480,
		.yres		= 272,
		.pixclock	= 108096, /* picosecond (9.2 MHz) */
		.left_margin	= 15,
		.right_margin	= 8,
		.upper_margin	= 12,
		.lower_margin	= 4,
		.hsync_len	= 1,
		.vsync_len	= 1,
		.sync		= FB_SYNC_DATA_ENABLE_HIGH_ACT |
				  FB_SYNC_DOTCLK_FAILING_ACT,
	},
};

static struct fb_videomode mx28evk_video_modes[] = {
	{
		.name		= "Seiko-43WVF1G",
		.refresh	= 60,
		.xres		= 800,
		.yres		= 480,
		.pixclock	= 29851, /* picosecond (33.5 MHz) */
		.left_margin	= 89,
		.right_margin	= 164,
		.upper_margin	= 23,
		.lower_margin	= 10,
		.hsync_len	= 10,
		.vsync_len	= 10,
		.sync		= FB_SYNC_DATA_ENABLE_HIGH_ACT |
				  FB_SYNC_DOTCLK_FAILING_ACT,
	},
};

static struct fb_videomode m28evk_video_modes[] = {
	{
		.name		= "Ampire AM-800480R2TMQW-T01H",
		.refresh	= 60,
		.xres		= 800,
		.yres		= 480,
		.pixclock	= 30066, /* picosecond (33.26 MHz) */
		.left_margin	= 0,
		.right_margin	= 256,
		.upper_margin	= 0,
		.lower_margin	= 45,
		.hsync_len	= 1,
		.vsync_len	= 1,
		.sync		= FB_SYNC_DATA_ENABLE_HIGH_ACT,
	},
};

static struct fb_videomode apx4devkit_video_modes[] = {
	{
		.name		= "HannStar PJ70112A",
		.refresh	= 60,
		.xres		= 800,
		.yres		= 480,
		.pixclock	= 33333, /* picosecond (30.00 MHz) */
		.left_margin	= 88,
		.right_margin	= 40,
		.upper_margin	= 32,
		.lower_margin	= 13,
		.hsync_len	= 48,
		.vsync_len	= 3,
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT |
				  FB_SYNC_DATA_ENABLE_HIGH_ACT |
				  FB_SYNC_DOTCLK_FAILING_ACT,
	},
};

static struct mxsfb_platform_data mxsfb_pdata __initdata;

static struct of_dev_auxdata mxs_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("fsl,imx23-lcdif", 0x80030000, NULL, &mxsfb_pdata),
	OF_DEV_AUXDATA("fsl,imx28-lcdif", 0x80030000, NULL, &mxsfb_pdata),
	{ /* sentinel */ }
};

static int __init mxs_icoll_add_irq_domain(struct device_node *np,
				struct device_node *interrupt_parent)
{
	irq_domain_add_legacy(np, 128, 0, 0, &irq_domain_simple_ops, NULL);

	return 0;
}

static int __init mxs_gpio_add_irq_domain(struct device_node *np,
				struct device_node *interrupt_parent)
{
	static int gpio_irq_base = MXS_GPIO_IRQ_START;

	irq_domain_add_legacy(np, 32, gpio_irq_base, 0, &irq_domain_simple_ops, NULL);
	gpio_irq_base += 32;

	return 0;
}

static const struct of_device_id mxs_irq_match[] __initconst = {
	{ .compatible = "fsl,mxs-icoll", .data = mxs_icoll_add_irq_domain, },
	{ .compatible = "fsl,mxs-gpio", .data = mxs_gpio_add_irq_domain, },
	{ /* sentinel */ }
};

static void __init mxs_dt_init_irq(void)
{
	icoll_init_irq();
	of_irq_init(mxs_irq_match);
}

static void __init imx23_timer_init(void)
{
	mx23_clocks_init();
}

static struct sys_timer imx23_timer = {
	.init = imx23_timer_init,
};

static void __init imx28_timer_init(void)
{
	mx28_clocks_init();
}

static struct sys_timer imx28_timer = {
	.init = imx28_timer_init,
};

enum mac_oui {
	OUI_FSL,
	OUI_DENX,
};

static void __init update_fec_mac_prop(enum mac_oui oui)
{
	struct device_node *np, *from = NULL;
	struct property *oldmac, *newmac;
	const u32 *ocotp = mxs_get_ocotp();
	u8 *macaddr;
	u32 val;
	int i;

	for (i = 0; i < 2; i++) {
		np = of_find_compatible_node(from, NULL, "fsl,imx28-fec");
		if (!np)
			return;
		from = np;

		newmac = kzalloc(sizeof(*newmac) + 6, GFP_KERNEL);
		if (!newmac)
			return;
		newmac->value = newmac + 1;
		newmac->length = 6;

		newmac->name = kstrdup("local-mac-address", GFP_KERNEL);
		if (!newmac->name) {
			kfree(newmac);
			return;
		}

		/*
		 * OCOTP only stores the last 4 octets for each mac address,
		 * so hard-code OUI here.
		 */
		macaddr = newmac->value;
		switch (oui) {
		case OUI_FSL:
			macaddr[0] = 0x00;
			macaddr[1] = 0x04;
			macaddr[2] = 0x9f;
			break;
		case OUI_DENX:
			macaddr[0] = 0xc0;
			macaddr[1] = 0xe5;
			macaddr[2] = 0x4e;
			break;
		}
		val = ocotp[i];
		macaddr[3] = (val >> 16) & 0xff;
		macaddr[4] = (val >> 8) & 0xff;
		macaddr[5] = (val >> 0) & 0xff;

		oldmac = of_find_property(np, newmac->name, NULL);
		if (oldmac)
			prom_update_property(np, newmac, oldmac);
		else
			prom_add_property(np, newmac);
	}
}

static void __init imx23_evk_init(void)
{
	mxsfb_pdata.mode_list = mx23evk_video_modes;
	mxsfb_pdata.mode_count = ARRAY_SIZE(mx23evk_video_modes);
	mxsfb_pdata.default_bpp = 32;
	mxsfb_pdata.ld_intf_width = STMLCDIF_24BIT;
}

static inline void enable_clk_enet_out(void)
{
	struct clk *clk = clk_get_sys("enet_out", NULL);

	if (!IS_ERR(clk))
		clk_prepare_enable(clk);
}

static void __init imx28_evk_init(void)
{
	enable_clk_enet_out();
	update_fec_mac_prop(OUI_FSL);

	mxsfb_pdata.mode_list = mx28evk_video_modes;
	mxsfb_pdata.mode_count = ARRAY_SIZE(mx28evk_video_modes);
	mxsfb_pdata.default_bpp = 32;
	mxsfb_pdata.ld_intf_width = STMLCDIF_24BIT;
}

static void __init m28evk_init(void)
{
	enable_clk_enet_out();
	update_fec_mac_prop(OUI_DENX);

	mxsfb_pdata.mode_list = m28evk_video_modes;
	mxsfb_pdata.mode_count = ARRAY_SIZE(m28evk_video_modes);
	mxsfb_pdata.default_bpp = 16;
	mxsfb_pdata.ld_intf_width = STMLCDIF_18BIT;
}

static int apx4devkit_phy_fixup(struct phy_device *phy)
{
	phy->dev_flags |= MICREL_PHY_50MHZ_CLK;
	return 0;
}

static void __init apx4devkit_init(void)
{
	enable_clk_enet_out();

	if (IS_BUILTIN(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KS8051, MICREL_PHY_ID_MASK,
					   apx4devkit_phy_fixup);

	mxsfb_pdata.mode_list = apx4devkit_video_modes;
	mxsfb_pdata.mode_count = ARRAY_SIZE(apx4devkit_video_modes);
	mxsfb_pdata.default_bpp = 32;
	mxsfb_pdata.ld_intf_width = STMLCDIF_24BIT;
}

static void __init mxs_machine_init(void)
{
	if (of_machine_is_compatible("fsl,imx28-evk"))
		imx28_evk_init();
	else if (of_machine_is_compatible("fsl,imx23-evk"))
		imx23_evk_init();
	else if (of_machine_is_compatible("denx,m28evk"))
		m28evk_init();
	else if (of_machine_is_compatible("bluegiga,apx4devkit"))
		apx4devkit_init();

	of_platform_populate(NULL, of_default_bus_match_table,
			     mxs_auxdata_lookup, NULL);
}

static const char *imx23_dt_compat[] __initdata = {
	"fsl,imx23-evk",
	"fsl,stmp378x_devb"
	"olimex,imx23-olinuxino",
	"fsl,imx23",
	NULL,
};

static const char *imx28_dt_compat[] __initdata = {
	"bluegiga,apx4devkit",
	"crystalfontz,cfa10036",
	"denx,m28evk",
	"fsl,imx28-evk",
	"karo,tx28",
	"fsl,imx28",
	NULL,
};

DT_MACHINE_START(IMX23, "Freescale i.MX23 (Device Tree)")
	.map_io		= mx23_map_io,
	.init_irq	= mxs_dt_init_irq,
	.timer		= &imx23_timer,
	.init_machine	= mxs_machine_init,
	.dt_compat	= imx23_dt_compat,
	.restart	= mxs_restart,
MACHINE_END

DT_MACHINE_START(IMX28, "Freescale i.MX28 (Device Tree)")
	.map_io		= mx28_map_io,
	.init_irq	= mxs_dt_init_irq,
	.timer		= &imx28_timer,
	.init_machine	= mxs_machine_init,
	.dt_compat	= imx28_dt_compat,
	.restart	= mxs_restart,
MACHINE_END
