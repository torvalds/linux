/*
 * ehci-omap.c - driver for USBHOST on OMAP3/4 processors
 *
 * Bus Glue for the EHCI controllers in OMAP3/4
 * Tested on several OMAP3 boards, and OMAP4 Pandaboard
 *
 * Copyright (C) 2007-2010 Texas Instruments, Inc.
 *	Author: Vikram Pandita <vikram.pandita@ti.com>
 *	Author: Anand Gadiyar <gadiyar@ti.com>
 *
 * Copyright (C) 2009 Nokia Corporation
 *	Contact: Felipe Balbi <felipe.balbi@nokia.com>
 *
 * Based on "ehci-fsl.c" and "ehci-au1xxx.c" ehci glue layers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * TODO (last updated Nov 21, 2010):
 *	- add kernel-doc
 *	- enable AUTOIDLE
 *	- add suspend/resume
 *	- move workarounds to board-files
 *	- factor out code common to OHCI
 *	- add HSIC and TLL support
 *	- convert to use hwmod and runtime PM
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/ulpi.h>
#include <plat/usb.h>

/*
 * OMAP USBHOST Register addresses: VIRTUAL ADDRESSES
 *	Use ehci_omap_readl()/ehci_omap_writel() functions
 */

/* TLL Register Set */
#define	OMAP_USBTLL_REVISION				(0x00)
#define	OMAP_USBTLL_SYSCONFIG				(0x10)
#define	OMAP_USBTLL_SYSCONFIG_CACTIVITY			(1 << 8)
#define	OMAP_USBTLL_SYSCONFIG_SIDLEMODE			(1 << 3)
#define	OMAP_USBTLL_SYSCONFIG_ENAWAKEUP			(1 << 2)
#define	OMAP_USBTLL_SYSCONFIG_SOFTRESET			(1 << 1)
#define	OMAP_USBTLL_SYSCONFIG_AUTOIDLE			(1 << 0)

#define	OMAP_USBTLL_SYSSTATUS				(0x14)
#define	OMAP_USBTLL_SYSSTATUS_RESETDONE			(1 << 0)

#define	OMAP_USBTLL_IRQSTATUS				(0x18)
#define	OMAP_USBTLL_IRQENABLE				(0x1C)

#define	OMAP_TLL_SHARED_CONF				(0x30)
#define	OMAP_TLL_SHARED_CONF_USB_90D_DDR_EN		(1 << 6)
#define	OMAP_TLL_SHARED_CONF_USB_180D_SDR_EN		(1 << 5)
#define	OMAP_TLL_SHARED_CONF_USB_DIVRATION		(1 << 2)
#define	OMAP_TLL_SHARED_CONF_FCLK_REQ			(1 << 1)
#define	OMAP_TLL_SHARED_CONF_FCLK_IS_ON			(1 << 0)

#define	OMAP_TLL_CHANNEL_CONF(num)			(0x040 + 0x004 * num)
#define	OMAP_TLL_CHANNEL_CONF_ULPINOBITSTUFF		(1 << 11)
#define	OMAP_TLL_CHANNEL_CONF_ULPI_ULPIAUTOIDLE		(1 << 10)
#define	OMAP_TLL_CHANNEL_CONF_UTMIAUTOIDLE		(1 << 9)
#define	OMAP_TLL_CHANNEL_CONF_ULPIDDRMODE		(1 << 8)
#define	OMAP_TLL_CHANNEL_CONF_CHANEN			(1 << 0)

#define	OMAP_TLL_ULPI_FUNCTION_CTRL(num)		(0x804 + 0x100 * num)
#define	OMAP_TLL_ULPI_INTERFACE_CTRL(num)		(0x807 + 0x100 * num)
#define	OMAP_TLL_ULPI_OTG_CTRL(num)			(0x80A + 0x100 * num)
#define	OMAP_TLL_ULPI_INT_EN_RISE(num)			(0x80D + 0x100 * num)
#define	OMAP_TLL_ULPI_INT_EN_FALL(num)			(0x810 + 0x100 * num)
#define	OMAP_TLL_ULPI_INT_STATUS(num)			(0x813 + 0x100 * num)
#define	OMAP_TLL_ULPI_INT_LATCH(num)			(0x814 + 0x100 * num)
#define	OMAP_TLL_ULPI_DEBUG(num)			(0x815 + 0x100 * num)
#define	OMAP_TLL_ULPI_SCRATCH_REGISTER(num)		(0x816 + 0x100 * num)

#define OMAP_TLL_CHANNEL_COUNT				3
#define OMAP_TLL_CHANNEL_1_EN_MASK			(1 << 0)
#define OMAP_TLL_CHANNEL_2_EN_MASK			(1 << 1)
#define OMAP_TLL_CHANNEL_3_EN_MASK			(1 << 2)

/* UHH Register Set */
#define	OMAP_UHH_REVISION				(0x00)
#define	OMAP_UHH_SYSCONFIG				(0x10)
#define	OMAP_UHH_SYSCONFIG_MIDLEMODE			(1 << 12)
#define	OMAP_UHH_SYSCONFIG_CACTIVITY			(1 << 8)
#define	OMAP_UHH_SYSCONFIG_SIDLEMODE			(1 << 3)
#define	OMAP_UHH_SYSCONFIG_ENAWAKEUP			(1 << 2)
#define	OMAP_UHH_SYSCONFIG_SOFTRESET			(1 << 1)
#define	OMAP_UHH_SYSCONFIG_AUTOIDLE			(1 << 0)

#define	OMAP_UHH_SYSSTATUS				(0x14)
#define	OMAP_UHH_HOSTCONFIG				(0x40)
#define	OMAP_UHH_HOSTCONFIG_ULPI_BYPASS			(1 << 0)
#define	OMAP_UHH_HOSTCONFIG_ULPI_P1_BYPASS		(1 << 0)
#define	OMAP_UHH_HOSTCONFIG_ULPI_P2_BYPASS		(1 << 11)
#define	OMAP_UHH_HOSTCONFIG_ULPI_P3_BYPASS		(1 << 12)
#define OMAP_UHH_HOSTCONFIG_INCR4_BURST_EN		(1 << 2)
#define OMAP_UHH_HOSTCONFIG_INCR8_BURST_EN		(1 << 3)
#define OMAP_UHH_HOSTCONFIG_INCR16_BURST_EN		(1 << 4)
#define OMAP_UHH_HOSTCONFIG_INCRX_ALIGN_EN		(1 << 5)
#define OMAP_UHH_HOSTCONFIG_P1_CONNECT_STATUS		(1 << 8)
#define OMAP_UHH_HOSTCONFIG_P2_CONNECT_STATUS		(1 << 9)
#define OMAP_UHH_HOSTCONFIG_P3_CONNECT_STATUS		(1 << 10)

/* OMAP4-specific defines */
#define OMAP4_UHH_SYSCONFIG_IDLEMODE_CLEAR		(3 << 2)
#define OMAP4_UHH_SYSCONFIG_NOIDLE			(1 << 2)

#define OMAP4_UHH_SYSCONFIG_STDBYMODE_CLEAR		(3 << 4)
#define OMAP4_UHH_SYSCONFIG_NOSTDBY			(1 << 4)
#define OMAP4_UHH_SYSCONFIG_SOFTRESET			(1 << 0)

#define OMAP4_P1_MODE_CLEAR				(3 << 16)
#define OMAP4_P1_MODE_TLL				(1 << 16)
#define OMAP4_P1_MODE_HSIC				(3 << 16)
#define OMAP4_P2_MODE_CLEAR				(3 << 18)
#define OMAP4_P2_MODE_TLL				(1 << 18)
#define OMAP4_P2_MODE_HSIC				(3 << 18)

#define OMAP_REV2_TLL_CHANNEL_COUNT			2

#define	OMAP_UHH_DEBUG_CSR				(0x44)

/* EHCI Register Set */
#define EHCI_INSNREG04					(0xA0)
#define EHCI_INSNREG04_DISABLE_UNSUSPEND		(1 << 5)
#define	EHCI_INSNREG05_ULPI				(0xA4)
#define	EHCI_INSNREG05_ULPI_CONTROL_SHIFT		31
#define	EHCI_INSNREG05_ULPI_PORTSEL_SHIFT		24
#define	EHCI_INSNREG05_ULPI_OPSEL_SHIFT			22
#define	EHCI_INSNREG05_ULPI_REGADD_SHIFT		16
#define	EHCI_INSNREG05_ULPI_EXTREGADD_SHIFT		8
#define	EHCI_INSNREG05_ULPI_WRDATA_SHIFT		0

/* Values of UHH_REVISION - Note: these are not given in the TRM */
#define OMAP_EHCI_REV1	0x00000010	/* OMAP3 */
#define OMAP_EHCI_REV2	0x50700100	/* OMAP4 */

#define is_omap_ehci_rev1(x)	(x->omap_ehci_rev == OMAP_EHCI_REV1)
#define is_omap_ehci_rev2(x)	(x->omap_ehci_rev == OMAP_EHCI_REV2)

#define is_ehci_phy_mode(x)	(x == EHCI_HCD_OMAP_MODE_PHY)
#define is_ehci_tll_mode(x)	(x == EHCI_HCD_OMAP_MODE_TLL)
#define is_ehci_hsic_mode(x)	(x == EHCI_HCD_OMAP_MODE_HSIC)

/*-------------------------------------------------------------------------*/

static inline void ehci_omap_writel(void __iomem *base, u32 reg, u32 val)
{
	__raw_writel(val, base + reg);
}

static inline u32 ehci_omap_readl(void __iomem *base, u32 reg)
{
	return __raw_readl(base + reg);
}

static inline void ehci_omap_writeb(void __iomem *base, u8 reg, u8 val)
{
	__raw_writeb(val, base + reg);
}

static inline u8 ehci_omap_readb(void __iomem *base, u8 reg)
{
	return __raw_readb(base + reg);
}

/*-------------------------------------------------------------------------*/

struct ehci_hcd_omap {
	struct ehci_hcd		*ehci;
	struct device		*dev;

	struct clk		*usbhost_ick;
	struct clk		*usbhost_hs_fck;
	struct clk		*usbhost_fs_fck;
	struct clk		*usbtll_fck;
	struct clk		*usbtll_ick;
	struct clk		*xclk60mhsp1_ck;
	struct clk		*xclk60mhsp2_ck;
	struct clk		*utmi_p1_fck;
	struct clk		*utmi_p2_fck;

	/* FIXME the following two workarounds are
	 * board specific not silicon-specific so these
	 * should be moved to board-file instead.
	 *
	 * Maybe someone from TI will know better which
	 * board is affected and needs the workarounds
	 * to be applied
	 */

	/* gpio for resetting phy */
	int			reset_gpio_port[OMAP3_HS_USB_PORTS];

	/* phy reset workaround */
	int			phy_reset;

	/* IP revision */
	u32			omap_ehci_rev;

	/* desired phy_mode: TLL, PHY */
	enum ehci_hcd_omap_mode	port_mode[OMAP3_HS_USB_PORTS];

	void __iomem		*uhh_base;
	void __iomem		*tll_base;
	void __iomem		*ehci_base;

	/* Regulators for USB PHYs.
	 * Each PHY can have a separate regulator.
	 */
	struct regulator        *regulator[OMAP3_HS_USB_PORTS];
};

/*-------------------------------------------------------------------------*/

static void omap_usb_utmi_init(struct ehci_hcd_omap *omap, u8 tll_channel_mask,
				u8 tll_channel_count)
{
	unsigned reg;
	int i;

	/* Program the 3 TLL channels upfront */
	for (i = 0; i < tll_channel_count; i++) {
		reg = ehci_omap_readl(omap->tll_base, OMAP_TLL_CHANNEL_CONF(i));

		/* Disable AutoIdle, BitStuffing and use SDR Mode */
		reg &= ~(OMAP_TLL_CHANNEL_CONF_UTMIAUTOIDLE
				| OMAP_TLL_CHANNEL_CONF_ULPINOBITSTUFF
				| OMAP_TLL_CHANNEL_CONF_ULPIDDRMODE);
		ehci_omap_writel(omap->tll_base, OMAP_TLL_CHANNEL_CONF(i), reg);
	}

	/* Program Common TLL register */
	reg = ehci_omap_readl(omap->tll_base, OMAP_TLL_SHARED_CONF);
	reg |= (OMAP_TLL_SHARED_CONF_FCLK_IS_ON
			| OMAP_TLL_SHARED_CONF_USB_DIVRATION
			| OMAP_TLL_SHARED_CONF_USB_180D_SDR_EN);
	reg &= ~OMAP_TLL_SHARED_CONF_USB_90D_DDR_EN;

	ehci_omap_writel(omap->tll_base, OMAP_TLL_SHARED_CONF, reg);

	/* Enable channels now */
	for (i = 0; i < tll_channel_count; i++) {
		reg = ehci_omap_readl(omap->tll_base, OMAP_TLL_CHANNEL_CONF(i));

		/* Enable only the reg that is needed */
		if (!(tll_channel_mask & 1<<i))
			continue;

		reg |= OMAP_TLL_CHANNEL_CONF_CHANEN;
		ehci_omap_writel(omap->tll_base, OMAP_TLL_CHANNEL_CONF(i), reg);

		ehci_omap_writeb(omap->tll_base,
				OMAP_TLL_ULPI_SCRATCH_REGISTER(i), 0xbe);
		dev_dbg(omap->dev, "ULPI_SCRATCH_REG[ch=%d]= 0x%02x\n",
				i+1, ehci_omap_readb(omap->tll_base,
				OMAP_TLL_ULPI_SCRATCH_REGISTER(i)));
	}
}

/*-------------------------------------------------------------------------*/

static void omap_ehci_soft_phy_reset(struct ehci_hcd_omap *omap, u8 port)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	unsigned reg = 0;

	reg = ULPI_FUNC_CTRL_RESET
		/* FUNCTION_CTRL_SET register */
		| (ULPI_SET(ULPI_FUNC_CTRL) << EHCI_INSNREG05_ULPI_REGADD_SHIFT)
		/* Write */
		| (2 << EHCI_INSNREG05_ULPI_OPSEL_SHIFT)
		/* PORTn */
		| ((port + 1) << EHCI_INSNREG05_ULPI_PORTSEL_SHIFT)
		/* start ULPI access*/
		| (1 << EHCI_INSNREG05_ULPI_CONTROL_SHIFT);

	ehci_omap_writel(omap->ehci_base, EHCI_INSNREG05_ULPI, reg);

	/* Wait for ULPI access completion */
	while ((ehci_omap_readl(omap->ehci_base, EHCI_INSNREG05_ULPI)
			& (1 << EHCI_INSNREG05_ULPI_CONTROL_SHIFT))) {
		cpu_relax();

		if (time_after(jiffies, timeout)) {
			dev_dbg(omap->dev, "phy reset operation timed out\n");
			break;
		}
	}
}

/* omap_start_ehc
 *	- Start the TI USBHOST controller
 */
static int omap_start_ehc(struct ehci_hcd_omap *omap, struct usb_hcd *hcd)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	u8 tll_ch_mask = 0;
	unsigned reg = 0;
	int ret = 0;

	dev_dbg(omap->dev, "starting TI EHCI USB Controller\n");

	/* Enable Clocks for USBHOST */
	omap->usbhost_ick = clk_get(omap->dev, "usbhost_ick");
	if (IS_ERR(omap->usbhost_ick)) {
		ret =  PTR_ERR(omap->usbhost_ick);
		goto err_host_ick;
	}
	clk_enable(omap->usbhost_ick);

	omap->usbhost_hs_fck = clk_get(omap->dev, "hs_fck");
	if (IS_ERR(omap->usbhost_hs_fck)) {
		ret = PTR_ERR(omap->usbhost_hs_fck);
		goto err_host_120m_fck;
	}
	clk_enable(omap->usbhost_hs_fck);

	omap->usbhost_fs_fck = clk_get(omap->dev, "fs_fck");
	if (IS_ERR(omap->usbhost_fs_fck)) {
		ret = PTR_ERR(omap->usbhost_fs_fck);
		goto err_host_48m_fck;
	}
	clk_enable(omap->usbhost_fs_fck);

	if (omap->phy_reset) {
		/* Refer: ISSUE1 */
		if (gpio_is_valid(omap->reset_gpio_port[0])) {
			gpio_request(omap->reset_gpio_port[0],
						"USB1 PHY reset");
			gpio_direction_output(omap->reset_gpio_port[0], 0);
		}

		if (gpio_is_valid(omap->reset_gpio_port[1])) {
			gpio_request(omap->reset_gpio_port[1],
						"USB2 PHY reset");
			gpio_direction_output(omap->reset_gpio_port[1], 0);
		}

		/* Hold the PHY in RESET for enough time till DIR is high */
		udelay(10);
	}

	/* Configure TLL for 60Mhz clk for ULPI */
	omap->usbtll_fck = clk_get(omap->dev, "usbtll_fck");
	if (IS_ERR(omap->usbtll_fck)) {
		ret = PTR_ERR(omap->usbtll_fck);
		goto err_tll_fck;
	}
	clk_enable(omap->usbtll_fck);

	omap->usbtll_ick = clk_get(omap->dev, "usbtll_ick");
	if (IS_ERR(omap->usbtll_ick)) {
		ret = PTR_ERR(omap->usbtll_ick);
		goto err_tll_ick;
	}
	clk_enable(omap->usbtll_ick);

	omap->omap_ehci_rev = ehci_omap_readl(omap->uhh_base,
						OMAP_UHH_REVISION);
	dev_dbg(omap->dev, "OMAP UHH_REVISION 0x%x\n",
					omap->omap_ehci_rev);

	/*
	 * Enable per-port clocks as needed (newer controllers only).
	 * - External ULPI clock for PHY mode
	 * - Internal clocks for TLL and HSIC modes (TODO)
	 */
	if (is_omap_ehci_rev2(omap)) {
		switch (omap->port_mode[0]) {
		case EHCI_HCD_OMAP_MODE_PHY:
			omap->xclk60mhsp1_ck = clk_get(omap->dev,
							"xclk60mhsp1_ck");
			if (IS_ERR(omap->xclk60mhsp1_ck)) {
				ret = PTR_ERR(omap->xclk60mhsp1_ck);
				dev_err(omap->dev,
					"Unable to get Port1 ULPI clock\n");
			}

			omap->utmi_p1_fck = clk_get(omap->dev,
							"utmi_p1_gfclk");
			if (IS_ERR(omap->utmi_p1_fck)) {
				ret = PTR_ERR(omap->utmi_p1_fck);
				dev_err(omap->dev,
					"Unable to get utmi_p1_fck\n");
			}

			ret = clk_set_parent(omap->utmi_p1_fck,
						omap->xclk60mhsp1_ck);
			if (ret != 0) {
				dev_err(omap->dev,
					"Unable to set P1 f-clock\n");
			}
			break;
		case EHCI_HCD_OMAP_MODE_TLL:
			/* TODO */
		default:
			break;
		}
		switch (omap->port_mode[1]) {
		case EHCI_HCD_OMAP_MODE_PHY:
			omap->xclk60mhsp2_ck = clk_get(omap->dev,
							"xclk60mhsp2_ck");
			if (IS_ERR(omap->xclk60mhsp2_ck)) {
				ret = PTR_ERR(omap->xclk60mhsp2_ck);
				dev_err(omap->dev,
					"Unable to get Port2 ULPI clock\n");
			}

			omap->utmi_p2_fck = clk_get(omap->dev,
							"utmi_p2_gfclk");
			if (IS_ERR(omap->utmi_p2_fck)) {
				ret = PTR_ERR(omap->utmi_p2_fck);
				dev_err(omap->dev,
					"Unable to get utmi_p2_fck\n");
			}

			ret = clk_set_parent(omap->utmi_p2_fck,
						omap->xclk60mhsp2_ck);
			if (ret != 0) {
				dev_err(omap->dev,
					"Unable to set P2 f-clock\n");
			}
			break;
		case EHCI_HCD_OMAP_MODE_TLL:
			/* TODO */
		default:
			break;
		}
	}


	/* perform TLL soft reset, and wait until reset is complete */
	ehci_omap_writel(omap->tll_base, OMAP_USBTLL_SYSCONFIG,
			OMAP_USBTLL_SYSCONFIG_SOFTRESET);

	/* Wait for TLL reset to complete */
	while (!(ehci_omap_readl(omap->tll_base, OMAP_USBTLL_SYSSTATUS)
			& OMAP_USBTLL_SYSSTATUS_RESETDONE)) {
		cpu_relax();

		if (time_after(jiffies, timeout)) {
			dev_dbg(omap->dev, "operation timed out\n");
			ret = -EINVAL;
			goto err_sys_status;
		}
	}

	dev_dbg(omap->dev, "TLL RESET DONE\n");

	/* (1<<3) = no idle mode only for initial debugging */
	ehci_omap_writel(omap->tll_base, OMAP_USBTLL_SYSCONFIG,
			OMAP_USBTLL_SYSCONFIG_ENAWAKEUP |
			OMAP_USBTLL_SYSCONFIG_SIDLEMODE |
			OMAP_USBTLL_SYSCONFIG_CACTIVITY);


	/* Put UHH in NoIdle/NoStandby mode */
	reg = ehci_omap_readl(omap->uhh_base, OMAP_UHH_SYSCONFIG);
	if (is_omap_ehci_rev1(omap)) {
		reg |= (OMAP_UHH_SYSCONFIG_ENAWAKEUP
				| OMAP_UHH_SYSCONFIG_SIDLEMODE
				| OMAP_UHH_SYSCONFIG_CACTIVITY
				| OMAP_UHH_SYSCONFIG_MIDLEMODE);
		reg &= ~OMAP_UHH_SYSCONFIG_AUTOIDLE;


	} else if (is_omap_ehci_rev2(omap)) {
		reg &= ~OMAP4_UHH_SYSCONFIG_IDLEMODE_CLEAR;
		reg |= OMAP4_UHH_SYSCONFIG_NOIDLE;
		reg &= ~OMAP4_UHH_SYSCONFIG_STDBYMODE_CLEAR;
		reg |= OMAP4_UHH_SYSCONFIG_NOSTDBY;
	}
	ehci_omap_writel(omap->uhh_base, OMAP_UHH_SYSCONFIG, reg);

	reg = ehci_omap_readl(omap->uhh_base, OMAP_UHH_HOSTCONFIG);

	/* setup ULPI bypass and burst configurations */
	reg |= (OMAP_UHH_HOSTCONFIG_INCR4_BURST_EN
			| OMAP_UHH_HOSTCONFIG_INCR8_BURST_EN
			| OMAP_UHH_HOSTCONFIG_INCR16_BURST_EN);
	reg &= ~OMAP_UHH_HOSTCONFIG_INCRX_ALIGN_EN;

	if (is_omap_ehci_rev1(omap)) {
		if (omap->port_mode[0] == EHCI_HCD_OMAP_MODE_UNKNOWN)
			reg &= ~OMAP_UHH_HOSTCONFIG_P1_CONNECT_STATUS;
		if (omap->port_mode[1] == EHCI_HCD_OMAP_MODE_UNKNOWN)
			reg &= ~OMAP_UHH_HOSTCONFIG_P2_CONNECT_STATUS;
		if (omap->port_mode[2] == EHCI_HCD_OMAP_MODE_UNKNOWN)
			reg &= ~OMAP_UHH_HOSTCONFIG_P3_CONNECT_STATUS;

		/* Bypass the TLL module for PHY mode operation */
		if (cpu_is_omap3430() && (omap_rev() <= OMAP3430_REV_ES2_1)) {
			dev_dbg(omap->dev, "OMAP3 ES version <= ES2.1\n");
			if (is_ehci_phy_mode(omap->port_mode[0]) ||
				is_ehci_phy_mode(omap->port_mode[1]) ||
					is_ehci_phy_mode(omap->port_mode[2]))
				reg &= ~OMAP_UHH_HOSTCONFIG_ULPI_BYPASS;
			else
				reg |= OMAP_UHH_HOSTCONFIG_ULPI_BYPASS;
		} else {
			dev_dbg(omap->dev, "OMAP3 ES version > ES2.1\n");
			if (is_ehci_phy_mode(omap->port_mode[0]))
				reg &= ~OMAP_UHH_HOSTCONFIG_ULPI_P1_BYPASS;
			else if (is_ehci_tll_mode(omap->port_mode[0]))
				reg |= OMAP_UHH_HOSTCONFIG_ULPI_P1_BYPASS;

			if (is_ehci_phy_mode(omap->port_mode[1]))
				reg &= ~OMAP_UHH_HOSTCONFIG_ULPI_P2_BYPASS;
			else if (is_ehci_tll_mode(omap->port_mode[1]))
				reg |= OMAP_UHH_HOSTCONFIG_ULPI_P2_BYPASS;

			if (is_ehci_phy_mode(omap->port_mode[2]))
				reg &= ~OMAP_UHH_HOSTCONFIG_ULPI_P3_BYPASS;
			else if (is_ehci_tll_mode(omap->port_mode[2]))
				reg |= OMAP_UHH_HOSTCONFIG_ULPI_P3_BYPASS;
		}
	} else if (is_omap_ehci_rev2(omap)) {
		/* Clear port mode fields for PHY mode*/
		reg &= ~OMAP4_P1_MODE_CLEAR;
		reg &= ~OMAP4_P2_MODE_CLEAR;

		if (is_ehci_tll_mode(omap->port_mode[0]))
			reg |= OMAP4_P1_MODE_TLL;
		else if (is_ehci_hsic_mode(omap->port_mode[0]))
			reg |= OMAP4_P1_MODE_HSIC;

		if (is_ehci_tll_mode(omap->port_mode[1]))
			reg |= OMAP4_P2_MODE_TLL;
		else if (is_ehci_hsic_mode(omap->port_mode[1]))
			reg |= OMAP4_P2_MODE_HSIC;
	}

	ehci_omap_writel(omap->uhh_base, OMAP_UHH_HOSTCONFIG, reg);
	dev_dbg(omap->dev, "UHH setup done, uhh_hostconfig=%x\n", reg);


	/*
	 * An undocumented "feature" in the OMAP3 EHCI controller,
	 * causes suspended ports to be taken out of suspend when
	 * the USBCMD.Run/Stop bit is cleared (for example when
	 * we do ehci_bus_suspend).
	 * This breaks suspend-resume if the root-hub is allowed
	 * to suspend. Writing 1 to this undocumented register bit
	 * disables this feature and restores normal behavior.
	 */
	ehci_omap_writel(omap->ehci_base, EHCI_INSNREG04,
				EHCI_INSNREG04_DISABLE_UNSUSPEND);

	if ((omap->port_mode[0] == EHCI_HCD_OMAP_MODE_TLL) ||
		(omap->port_mode[1] == EHCI_HCD_OMAP_MODE_TLL) ||
			(omap->port_mode[2] == EHCI_HCD_OMAP_MODE_TLL)) {

		if (omap->port_mode[0] == EHCI_HCD_OMAP_MODE_TLL)
			tll_ch_mask |= OMAP_TLL_CHANNEL_1_EN_MASK;
		if (omap->port_mode[1] == EHCI_HCD_OMAP_MODE_TLL)
			tll_ch_mask |= OMAP_TLL_CHANNEL_2_EN_MASK;
		if (omap->port_mode[2] == EHCI_HCD_OMAP_MODE_TLL)
			tll_ch_mask |= OMAP_TLL_CHANNEL_3_EN_MASK;

		/* Enable UTMI mode for required TLL channels */
		omap_usb_utmi_init(omap, tll_ch_mask, OMAP_TLL_CHANNEL_COUNT);
	}

	if (omap->phy_reset) {
		/* Refer ISSUE1:
		 * Hold the PHY in RESET for enough time till
		 * PHY is settled and ready
		 */
		udelay(10);

		if (gpio_is_valid(omap->reset_gpio_port[0]))
			gpio_set_value(omap->reset_gpio_port[0], 1);

		if (gpio_is_valid(omap->reset_gpio_port[1]))
			gpio_set_value(omap->reset_gpio_port[1], 1);
	}

	/* Soft reset the PHY using PHY reset command over ULPI */
	if (omap->port_mode[0] == EHCI_HCD_OMAP_MODE_PHY)
		omap_ehci_soft_phy_reset(omap, 0);
	if (omap->port_mode[1] == EHCI_HCD_OMAP_MODE_PHY)
		omap_ehci_soft_phy_reset(omap, 1);

	return 0;

err_sys_status:
	clk_disable(omap->utmi_p2_fck);
	clk_put(omap->utmi_p2_fck);
	clk_disable(omap->xclk60mhsp2_ck);
	clk_put(omap->xclk60mhsp2_ck);
	clk_disable(omap->utmi_p1_fck);
	clk_put(omap->utmi_p1_fck);
	clk_disable(omap->xclk60mhsp1_ck);
	clk_put(omap->xclk60mhsp1_ck);
	clk_disable(omap->usbtll_ick);
	clk_put(omap->usbtll_ick);

err_tll_ick:
	clk_disable(omap->usbtll_fck);
	clk_put(omap->usbtll_fck);

err_tll_fck:
	clk_disable(omap->usbhost_fs_fck);
	clk_put(omap->usbhost_fs_fck);

	if (omap->phy_reset) {
		if (gpio_is_valid(omap->reset_gpio_port[0]))
			gpio_free(omap->reset_gpio_port[0]);

		if (gpio_is_valid(omap->reset_gpio_port[1]))
			gpio_free(omap->reset_gpio_port[1]);
	}

err_host_48m_fck:
	clk_disable(omap->usbhost_hs_fck);
	clk_put(omap->usbhost_hs_fck);

err_host_120m_fck:
	clk_disable(omap->usbhost_ick);
	clk_put(omap->usbhost_ick);

err_host_ick:
	return ret;
}

static void omap_stop_ehc(struct ehci_hcd_omap *omap, struct usb_hcd *hcd)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(100);

	dev_dbg(omap->dev, "stopping TI EHCI USB Controller\n");

	/* Reset OMAP modules for insmod/rmmod to work */
	ehci_omap_writel(omap->uhh_base, OMAP_UHH_SYSCONFIG,
			is_omap_ehci_rev2(omap) ?
			OMAP4_UHH_SYSCONFIG_SOFTRESET :
			OMAP_UHH_SYSCONFIG_SOFTRESET);
	while (!(ehci_omap_readl(omap->uhh_base, OMAP_UHH_SYSSTATUS)
				& (1 << 0))) {
		cpu_relax();

		if (time_after(jiffies, timeout))
			dev_dbg(omap->dev, "operation timed out\n");
	}

	while (!(ehci_omap_readl(omap->uhh_base, OMAP_UHH_SYSSTATUS)
				& (1 << 1))) {
		cpu_relax();

		if (time_after(jiffies, timeout))
			dev_dbg(omap->dev, "operation timed out\n");
	}

	while (!(ehci_omap_readl(omap->uhh_base, OMAP_UHH_SYSSTATUS)
				& (1 << 2))) {
		cpu_relax();

		if (time_after(jiffies, timeout))
			dev_dbg(omap->dev, "operation timed out\n");
	}

	ehci_omap_writel(omap->tll_base, OMAP_USBTLL_SYSCONFIG, (1 << 1));

	while (!(ehci_omap_readl(omap->tll_base, OMAP_USBTLL_SYSSTATUS)
				& (1 << 0))) {
		cpu_relax();

		if (time_after(jiffies, timeout))
			dev_dbg(omap->dev, "operation timed out\n");
	}

	if (omap->usbtll_fck != NULL) {
		clk_disable(omap->usbtll_fck);
		clk_put(omap->usbtll_fck);
		omap->usbtll_fck = NULL;
	}

	if (omap->usbhost_ick != NULL) {
		clk_disable(omap->usbhost_ick);
		clk_put(omap->usbhost_ick);
		omap->usbhost_ick = NULL;
	}

	if (omap->usbhost_fs_fck != NULL) {
		clk_disable(omap->usbhost_fs_fck);
		clk_put(omap->usbhost_fs_fck);
		omap->usbhost_fs_fck = NULL;
	}

	if (omap->usbhost_hs_fck != NULL) {
		clk_disable(omap->usbhost_hs_fck);
		clk_put(omap->usbhost_hs_fck);
		omap->usbhost_hs_fck = NULL;
	}

	if (omap->usbtll_ick != NULL) {
		clk_disable(omap->usbtll_ick);
		clk_put(omap->usbtll_ick);
		omap->usbtll_ick = NULL;
	}

	if (is_omap_ehci_rev2(omap)) {
		if (omap->xclk60mhsp1_ck != NULL) {
			clk_disable(omap->xclk60mhsp1_ck);
			clk_put(omap->xclk60mhsp1_ck);
			omap->xclk60mhsp1_ck = NULL;
		}

		if (omap->utmi_p1_fck != NULL) {
			clk_disable(omap->utmi_p1_fck);
			clk_put(omap->utmi_p1_fck);
			omap->utmi_p1_fck = NULL;
		}

		if (omap->xclk60mhsp2_ck != NULL) {
			clk_disable(omap->xclk60mhsp2_ck);
			clk_put(omap->xclk60mhsp2_ck);
			omap->xclk60mhsp2_ck = NULL;
		}

		if (omap->utmi_p2_fck != NULL) {
			clk_disable(omap->utmi_p2_fck);
			clk_put(omap->utmi_p2_fck);
			omap->utmi_p2_fck = NULL;
		}
	}

	if (omap->phy_reset) {
		if (gpio_is_valid(omap->reset_gpio_port[0]))
			gpio_free(omap->reset_gpio_port[0]);

		if (gpio_is_valid(omap->reset_gpio_port[1]))
			gpio_free(omap->reset_gpio_port[1]);
	}

	dev_dbg(omap->dev, "Clock to USB host has been disabled\n");
}

/*-------------------------------------------------------------------------*/

static const struct hc_driver ehci_omap_hc_driver;

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * ehci_hcd_omap_probe - initialize TI-based HCDs
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int ehci_hcd_omap_probe(struct platform_device *pdev)
{
	struct ehci_hcd_omap_platform_data *pdata = pdev->dev.platform_data;
	struct ehci_hcd_omap *omap;
	struct resource *res;
	struct usb_hcd *hcd;

	int irq = platform_get_irq(pdev, 0);
	int ret = -ENODEV;
	int i;
	char supply[7];

	if (!pdata) {
		dev_dbg(&pdev->dev, "missing platform_data\n");
		goto err_pdata;
	}

	if (usb_disabled())
		goto err_disabled;

	omap = kzalloc(sizeof(*omap), GFP_KERNEL);
	if (!omap) {
		ret = -ENOMEM;
		goto err_disabled;
	}

	hcd = usb_create_hcd(&ehci_omap_hc_driver, &pdev->dev,
			dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "failed to create hcd with err %d\n", ret);
		ret = -ENOMEM;
		goto err_create_hcd;
	}

	platform_set_drvdata(pdev, omap);
	omap->dev		= &pdev->dev;
	omap->phy_reset		= pdata->phy_reset;
	omap->reset_gpio_port[0]	= pdata->reset_gpio_port[0];
	omap->reset_gpio_port[1]	= pdata->reset_gpio_port[1];
	omap->reset_gpio_port[2]	= pdata->reset_gpio_port[2];
	omap->port_mode[0]		= pdata->port_mode[0];
	omap->port_mode[1]		= pdata->port_mode[1];
	omap->port_mode[2]		= pdata->port_mode[2];
	omap->ehci		= hcd_to_ehci(hcd);
	omap->ehci->sbrn	= 0x20;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "EHCI ioremap failed\n");
		ret = -ENOMEM;
		goto err_ioremap;
	}

	/* we know this is the memory we want, no need to ioremap again */
	omap->ehci->caps = hcd->regs;
	omap->ehci_base = hcd->regs;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	omap->uhh_base = ioremap(res->start, resource_size(res));
	if (!omap->uhh_base) {
		dev_err(&pdev->dev, "UHH ioremap failed\n");
		ret = -ENOMEM;
		goto err_uhh_ioremap;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	omap->tll_base = ioremap(res->start, resource_size(res));
	if (!omap->tll_base) {
		dev_err(&pdev->dev, "TLL ioremap failed\n");
		ret = -ENOMEM;
		goto err_tll_ioremap;
	}

	/* get ehci regulator and enable */
	for (i = 0 ; i < OMAP3_HS_USB_PORTS ; i++) {
		if (omap->port_mode[i] != EHCI_HCD_OMAP_MODE_PHY) {
			omap->regulator[i] = NULL;
			continue;
		}
		snprintf(supply, sizeof(supply), "hsusb%d", i);
		omap->regulator[i] = regulator_get(omap->dev, supply);
		if (IS_ERR(omap->regulator[i])) {
			omap->regulator[i] = NULL;
			dev_dbg(&pdev->dev,
			"failed to get ehci port%d regulator\n", i);
		} else {
			regulator_enable(omap->regulator[i]);
		}
	}

	ret = omap_start_ehc(omap, hcd);
	if (ret) {
		dev_err(&pdev->dev, "failed to start ehci with err %d\n", ret);
		goto err_start;
	}

	omap->ehci->regs = hcd->regs
		+ HC_LENGTH(readl(&omap->ehci->caps->hc_capbase));

	dbg_hcs_params(omap->ehci, "reset");
	dbg_hcc_params(omap->ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	omap->ehci->hcs_params = readl(&omap->ehci->caps->hcs_params);

	ret = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (ret) {
		dev_err(&pdev->dev, "failed to add hcd with err %d\n", ret);
		goto err_add_hcd;
	}

	/* root ports should always stay powered */
	ehci_port_power(omap->ehci, 1);

	return 0;

err_add_hcd:
	omap_stop_ehc(omap, hcd);

err_start:
	for (i = 0 ; i < OMAP3_HS_USB_PORTS ; i++) {
		if (omap->regulator[i]) {
			regulator_disable(omap->regulator[i]);
			regulator_put(omap->regulator[i]);
		}
	}
	iounmap(omap->tll_base);

err_tll_ioremap:
	iounmap(omap->uhh_base);

err_uhh_ioremap:
	iounmap(hcd->regs);

err_ioremap:
	usb_put_hcd(hcd);

err_create_hcd:
	kfree(omap);
err_disabled:
err_pdata:
	return ret;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * ehci_hcd_omap_remove - shutdown processing for EHCI HCDs
 * @pdev: USB Host Controller being removed
 *
 * Reverses the effect of usb_ehci_hcd_omap_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 */
static int ehci_hcd_omap_remove(struct platform_device *pdev)
{
	struct ehci_hcd_omap *omap = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(omap->ehci);
	int i;

	usb_remove_hcd(hcd);
	omap_stop_ehc(omap, hcd);
	iounmap(hcd->regs);
	for (i = 0 ; i < OMAP3_HS_USB_PORTS ; i++) {
		if (omap->regulator[i]) {
			regulator_disable(omap->regulator[i]);
			regulator_put(omap->regulator[i]);
		}
	}
	iounmap(omap->tll_base);
	iounmap(omap->uhh_base);
	usb_put_hcd(hcd);
	kfree(omap);

	return 0;
}

static void ehci_hcd_omap_shutdown(struct platform_device *pdev)
{
	struct ehci_hcd_omap *omap = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(omap->ehci);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct platform_driver ehci_hcd_omap_driver = {
	.probe			= ehci_hcd_omap_probe,
	.remove			= ehci_hcd_omap_remove,
	.shutdown		= ehci_hcd_omap_shutdown,
	/*.suspend		= ehci_hcd_omap_suspend, */
	/*.resume		= ehci_hcd_omap_resume, */
	.driver = {
		.name		= "ehci-omap",
	}
};

/*-------------------------------------------------------------------------*/

static const struct hc_driver ehci_omap_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "OMAP-EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset			= ehci_init,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number	= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

MODULE_ALIAS("platform:omap-ehci");
MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_AUTHOR("Felipe Balbi <felipe.balbi@nokia.com>");

