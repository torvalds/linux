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
#include <linux/can/platform/flexcan.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/micrel_phy.h>
#include <linux/mxsfb.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/pinctrl/consumer.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/common.h>
#include <mach/digctl.h>
#include <mach/mxs.h>

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

static struct fb_videomode apf28dev_video_modes[] = {
	{
		.name = "LW700",
		.refresh = 60,
		.xres = 800,
		.yres = 480,
		.pixclock = 30303, /* picosecond */
		.left_margin = 96,
		.right_margin = 96, /* at least 3 & 1 */
		.upper_margin = 0x14,
		.lower_margin = 0x15,
		.hsync_len = 64,
		.vsync_len = 4,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT |
				FB_SYNC_DATA_ENABLE_HIGH_ACT |
				FB_SYNC_DOTCLK_FAILING_ACT,
	},
};

static struct mxsfb_platform_data mxsfb_pdata __initdata;

/*
 * MX28EVK_FLEXCAN_SWITCH is shared between both flexcan controllers
 */
#define MX28EVK_FLEXCAN_SWITCH	MXS_GPIO_NR(2, 13)

static int flexcan0_en, flexcan1_en;

static void mx28evk_flexcan_switch(void)
{
	if (flexcan0_en || flexcan1_en)
		gpio_set_value(MX28EVK_FLEXCAN_SWITCH, 1);
	else
		gpio_set_value(MX28EVK_FLEXCAN_SWITCH, 0);
}

static void mx28evk_flexcan0_switch(int enable)
{
	flexcan0_en = enable;
	mx28evk_flexcan_switch();
}

static void mx28evk_flexcan1_switch(int enable)
{
	flexcan1_en = enable;
	mx28evk_flexcan_switch();
}

static struct flexcan_platform_data flexcan_pdata[2];

static struct of_dev_auxdata mxs_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("fsl,imx23-lcdif", 0x80030000, NULL, &mxsfb_pdata),
	OF_DEV_AUXDATA("fsl,imx28-lcdif", 0x80030000, NULL, &mxsfb_pdata),
	OF_DEV_AUXDATA("fsl,imx28-flexcan", 0x80032000, NULL, &flexcan_pdata[0]),
	OF_DEV_AUXDATA("fsl,imx28-flexcan", 0x80034000, NULL, &flexcan_pdata[1]),
	{ /* sentinel */ }
};

static void __init imx23_timer_init(void)
{
	mx23_clocks_init();
}

static void __init imx28_timer_init(void)
{
	mx28_clocks_init();
}

enum mac_oui {
	OUI_FSL,
	OUI_DENX,
	OUI_CRYSTALFONTZ,
};

static void __init update_fec_mac_prop(enum mac_oui oui)
{
	struct device_node *np, *from = NULL;
	struct property *newmac;
	const u32 *ocotp = mxs_get_ocotp();
	u8 *macaddr;
	u32 val;
	int i;

	for (i = 0; i < 2; i++) {
		np = of_find_compatible_node(from, NULL, "fsl,imx28-fec");
		if (!np)
			return;

		from = np;

		if (of_get_property(np, "local-mac-address", NULL))
			continue;

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
		case OUI_CRYSTALFONTZ:
			macaddr[0] = 0x58;
			macaddr[1] = 0xb9;
			macaddr[2] = 0xe1;
			break;
		}
		val = ocotp[i];
		macaddr[3] = (val >> 16) & 0xff;
		macaddr[4] = (val >> 8) & 0xff;
		macaddr[5] = (val >> 0) & 0xff;

		of_update_property(np, newmac);
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

	mxs_saif_clkmux_select(MXS_DIGCTL_SAIF_CLKMUX_EXTMSTR0);
}

static void __init imx28_evk_post_init(void)
{
	if (!gpio_request_one(MX28EVK_FLEXCAN_SWITCH, GPIOF_DIR_OUT,
			      "flexcan-switch")) {
		flexcan_pdata[0].transceiver_switch = mx28evk_flexcan0_switch;
		flexcan_pdata[1].transceiver_switch = mx28evk_flexcan1_switch;
	}
}

static void __init m28evk_init(void)
{
	mxsfb_pdata.mode_list = m28evk_video_modes;
	mxsfb_pdata.mode_count = ARRAY_SIZE(m28evk_video_modes);
	mxsfb_pdata.default_bpp = 16;
	mxsfb_pdata.ld_intf_width = STMLCDIF_18BIT;
}

static void __init sc_sps1_init(void)
{
	enable_clk_enet_out();
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
		phy_register_fixup_for_uid(PHY_ID_KSZ8051, MICREL_PHY_ID_MASK,
					   apx4devkit_phy_fixup);

	mxsfb_pdata.mode_list = apx4devkit_video_modes;
	mxsfb_pdata.mode_count = ARRAY_SIZE(apx4devkit_video_modes);
	mxsfb_pdata.default_bpp = 32;
	mxsfb_pdata.ld_intf_width = STMLCDIF_24BIT;
}

#define ENET0_MDC__GPIO_4_0	MXS_GPIO_NR(4, 0)
#define ENET0_MDIO__GPIO_4_1	MXS_GPIO_NR(4, 1)
#define ENET0_RX_EN__GPIO_4_2	MXS_GPIO_NR(4, 2)
#define ENET0_RXD0__GPIO_4_3	MXS_GPIO_NR(4, 3)
#define ENET0_RXD1__GPIO_4_4	MXS_GPIO_NR(4, 4)
#define ENET0_TX_EN__GPIO_4_6	MXS_GPIO_NR(4, 6)
#define ENET0_TXD0__GPIO_4_7	MXS_GPIO_NR(4, 7)
#define ENET0_TXD1__GPIO_4_8	MXS_GPIO_NR(4, 8)
#define ENET_CLK__GPIO_4_16	MXS_GPIO_NR(4, 16)

#define TX28_FEC_PHY_POWER	MXS_GPIO_NR(3, 29)
#define TX28_FEC_PHY_RESET	MXS_GPIO_NR(4, 13)
#define TX28_FEC_nINT		MXS_GPIO_NR(4, 5)

static const struct gpio tx28_gpios[] __initconst = {
	{ ENET0_MDC__GPIO_4_0, GPIOF_OUT_INIT_LOW, "GPIO_4_0" },
	{ ENET0_MDIO__GPIO_4_1, GPIOF_OUT_INIT_LOW, "GPIO_4_1" },
	{ ENET0_RX_EN__GPIO_4_2, GPIOF_OUT_INIT_LOW, "GPIO_4_2" },
	{ ENET0_RXD0__GPIO_4_3, GPIOF_OUT_INIT_LOW, "GPIO_4_3" },
	{ ENET0_RXD1__GPIO_4_4, GPIOF_OUT_INIT_LOW, "GPIO_4_4" },
	{ ENET0_TX_EN__GPIO_4_6, GPIOF_OUT_INIT_LOW, "GPIO_4_6" },
	{ ENET0_TXD0__GPIO_4_7, GPIOF_OUT_INIT_LOW, "GPIO_4_7" },
	{ ENET0_TXD1__GPIO_4_8, GPIOF_OUT_INIT_LOW, "GPIO_4_8" },
	{ ENET_CLK__GPIO_4_16, GPIOF_OUT_INIT_LOW, "GPIO_4_16" },
	{ TX28_FEC_PHY_POWER, GPIOF_OUT_INIT_LOW, "fec-phy-power" },
	{ TX28_FEC_PHY_RESET, GPIOF_OUT_INIT_LOW, "fec-phy-reset" },
	{ TX28_FEC_nINT, GPIOF_DIR_IN, "fec-int" },
};

static void __init tx28_post_init(void)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct pinctrl *pctl;
	int ret;

	enable_clk_enet_out();

	np = of_find_compatible_node(NULL, NULL, "fsl,imx28-fec");
	pdev = of_find_device_by_node(np);
	if (!pdev) {
		pr_err("%s: failed to find fec device\n", __func__);
		return;
	}

	pctl = pinctrl_get_select(&pdev->dev, "gpio_mode");
	if (IS_ERR(pctl)) {
		pr_err("%s: failed to get pinctrl state\n", __func__);
		return;
	}

	ret = gpio_request_array(tx28_gpios, ARRAY_SIZE(tx28_gpios));
	if (ret) {
		pr_err("%s: failed to request gpios: %d\n", __func__, ret);
		return;
	}

	/* Power up fec phy */
	gpio_set_value(TX28_FEC_PHY_POWER, 1);
	msleep(26); /* 25ms according to data sheet */

	/* Mode strap pins */
	gpio_set_value(ENET0_RX_EN__GPIO_4_2, 1);
	gpio_set_value(ENET0_RXD0__GPIO_4_3, 1);
	gpio_set_value(ENET0_RXD1__GPIO_4_4, 1);

	udelay(100); /* minimum assertion time for nRST */

	/* Deasserting FEC PHY RESET */
	gpio_set_value(TX28_FEC_PHY_RESET, 1);

	pinctrl_put(pctl);
}

static void __init cfa10049_init(void)
{
	enable_clk_enet_out();
	update_fec_mac_prop(OUI_CRYSTALFONTZ);
}

static void __init apf28_init(void)
{
	enable_clk_enet_out();

	mxsfb_pdata.mode_list = apf28dev_video_modes;
	mxsfb_pdata.mode_count = ARRAY_SIZE(apf28dev_video_modes);
	mxsfb_pdata.default_bpp = 16;
	mxsfb_pdata.ld_intf_width = STMLCDIF_16BIT;
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
	else if (of_machine_is_compatible("crystalfontz,cfa10049"))
		cfa10049_init();
	else if (of_machine_is_compatible("armadeus,imx28-apf28"))
		apf28_init();
	else if (of_machine_is_compatible("schulercontrol,imx28-sps1"))
		sc_sps1_init();

	of_platform_populate(NULL, of_default_bus_match_table,
			     mxs_auxdata_lookup, NULL);

	if (of_machine_is_compatible("karo,tx28"))
		tx28_post_init();

	if (of_machine_is_compatible("fsl,imx28-evk"))
		imx28_evk_post_init();
}

static const char *imx23_dt_compat[] __initdata = {
	"fsl,imx23",
	NULL,
};

static const char *imx28_dt_compat[] __initdata = {
	"fsl,imx28",
	NULL,
};

DT_MACHINE_START(IMX23, "Freescale i.MX23 (Device Tree)")
	.map_io		= mx23_map_io,
	.init_irq	= icoll_init_irq,
	.handle_irq	= icoll_handle_irq,
	.init_time	= imx23_timer_init,
	.init_machine	= mxs_machine_init,
	.dt_compat	= imx23_dt_compat,
	.restart	= mxs_restart,
MACHINE_END

DT_MACHINE_START(IMX28, "Freescale i.MX28 (Device Tree)")
	.map_io		= mx28_map_io,
	.init_irq	= icoll_init_irq,
	.handle_irq	= icoll_handle_irq,
	.init_time	= imx28_timer_init,
	.init_machine	= mxs_machine_init,
	.dt_compat	= imx28_dt_compat,
	.restart	= mxs_restart,
MACHINE_END
