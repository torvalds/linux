// SPDX-License-Identifier: GPL-2.0
/*
 * mtu3_dr.c - dual role switch and host glue layer
 *
 * Copyright (C) 2016 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include "mtu3.h"
#include "mtu3_dr.h"

#define PERI_WK_CTRL1		0x404
#define UWK_CTL1_IS_C(x)	(((x) & 0xf) << 26)
#define UWK_CTL1_IS_E		BIT(25)
#define UWK_CTL1_IDDIG_C(x)	(((x) & 0xf) << 11)  /* cycle debounce */
#define UWK_CTL1_IDDIG_E	BIT(10) /* enable debounce */
#define UWK_CTL1_IDDIG_P	BIT(9)  /* polarity */
#define UWK_CTL1_IS_P		BIT(6)  /* polarity for ip sleep */

/*
 * ip-sleep wakeup mode:
 * all clocks can be turn off, but power domain should be kept on
 */
static void ssusb_wakeup_ip_sleep_en(struct ssusb_mtk *ssusb)
{
	u32 tmp;
	struct regmap *pericfg = ssusb->pericfg;

	regmap_read(pericfg, PERI_WK_CTRL1, &tmp);
	tmp &= ~UWK_CTL1_IS_P;
	tmp &= ~(UWK_CTL1_IS_C(0xf));
	tmp |= UWK_CTL1_IS_C(0x8);
	regmap_write(pericfg, PERI_WK_CTRL1, tmp);
	regmap_write(pericfg, PERI_WK_CTRL1, tmp | UWK_CTL1_IS_E);

	regmap_read(pericfg, PERI_WK_CTRL1, &tmp);
	dev_dbg(ssusb->dev, "%s(): WK_CTRL1[P6,E25,C26:29]=%#x\n",
		__func__, tmp);
}

static void ssusb_wakeup_ip_sleep_dis(struct ssusb_mtk *ssusb)
{
	u32 tmp;

	regmap_read(ssusb->pericfg, PERI_WK_CTRL1, &tmp);
	tmp &= ~UWK_CTL1_IS_E;
	regmap_write(ssusb->pericfg, PERI_WK_CTRL1, tmp);
}

int ssusb_wakeup_of_property_parse(struct ssusb_mtk *ssusb,
				struct device_node *dn)
{
	struct device *dev = ssusb->dev;

	/*
	 * Wakeup function is optional, so it is not an error if this property
	 * does not exist, and in such case, no need to get relative
	 * properties anymore.
	 */
	ssusb->wakeup_en = of_property_read_bool(dn, "mediatek,enable-wakeup");
	if (!ssusb->wakeup_en)
		return 0;

	ssusb->pericfg = syscon_regmap_lookup_by_phandle(dn,
						"mediatek,syscon-wakeup");
	if (IS_ERR(ssusb->pericfg)) {
		dev_err(dev, "fail to get pericfg regs\n");
		return PTR_ERR(ssusb->pericfg);
	}

	return 0;
}

static void host_ports_num_get(struct ssusb_mtk *ssusb)
{
	u32 xhci_cap;

	xhci_cap = mtu3_readl(ssusb->ippc_base, U3D_SSUSB_IP_XHCI_CAP);
	ssusb->u2_ports = SSUSB_IP_XHCI_U2_PORT_NUM(xhci_cap);
	ssusb->u3_ports = SSUSB_IP_XHCI_U3_PORT_NUM(xhci_cap);

	dev_dbg(ssusb->dev, "host - u2_ports:%d, u3_ports:%d\n",
		 ssusb->u2_ports, ssusb->u3_ports);
}

/* only configure ports will be used later */
int ssusb_host_enable(struct ssusb_mtk *ssusb)
{
	void __iomem *ibase = ssusb->ippc_base;
	int num_u3p = ssusb->u3_ports;
	int num_u2p = ssusb->u2_ports;
	int u3_ports_disabed;
	u32 check_clk;
	u32 value;
	int i;

	/* power on host ip */
	mtu3_clrbits(ibase, U3D_SSUSB_IP_PW_CTRL1, SSUSB_IP_HOST_PDN);

	/* power on and enable u3 ports except skipped ones */
	u3_ports_disabed = 0;
	for (i = 0; i < num_u3p; i++) {
		if ((0x1 << i) & ssusb->u3p_dis_msk) {
			u3_ports_disabed++;
			continue;
		}

		value = mtu3_readl(ibase, SSUSB_U3_CTRL(i));
		value &= ~(SSUSB_U3_PORT_PDN | SSUSB_U3_PORT_DIS);
		value |= SSUSB_U3_PORT_HOST_SEL;
		mtu3_writel(ibase, SSUSB_U3_CTRL(i), value);
	}

	/* power on and enable all u2 ports */
	for (i = 0; i < num_u2p; i++) {
		value = mtu3_readl(ibase, SSUSB_U2_CTRL(i));
		value &= ~(SSUSB_U2_PORT_PDN | SSUSB_U2_PORT_DIS);
		value |= SSUSB_U2_PORT_HOST_SEL;
		mtu3_writel(ibase, SSUSB_U2_CTRL(i), value);
	}

	check_clk = SSUSB_XHCI_RST_B_STS;
	if (num_u3p > u3_ports_disabed)
		check_clk = SSUSB_U3_MAC_RST_B_STS;

	return ssusb_check_clocks(ssusb, check_clk);
}

int ssusb_host_disable(struct ssusb_mtk *ssusb, bool suspend)
{
	void __iomem *ibase = ssusb->ippc_base;
	int num_u3p = ssusb->u3_ports;
	int num_u2p = ssusb->u2_ports;
	u32 value;
	int ret;
	int i;

	/* power down and disable u3 ports except skipped ones */
	for (i = 0; i < num_u3p; i++) {
		if ((0x1 << i) & ssusb->u3p_dis_msk)
			continue;

		value = mtu3_readl(ibase, SSUSB_U3_CTRL(i));
		value |= SSUSB_U3_PORT_PDN;
		value |= suspend ? 0 : SSUSB_U3_PORT_DIS;
		mtu3_writel(ibase, SSUSB_U3_CTRL(i), value);
	}

	/* power down and disable all u2 ports */
	for (i = 0; i < num_u2p; i++) {
		value = mtu3_readl(ibase, SSUSB_U2_CTRL(i));
		value |= SSUSB_U2_PORT_PDN;
		value |= suspend ? 0 : SSUSB_U2_PORT_DIS;
		mtu3_writel(ibase, SSUSB_U2_CTRL(i), value);
	}

	/* power down host ip */
	mtu3_setbits(ibase, U3D_SSUSB_IP_PW_CTRL1, SSUSB_IP_HOST_PDN);

	if (!suspend)
		return 0;

	/* wait for host ip to sleep */
	ret = readl_poll_timeout(ibase + U3D_SSUSB_IP_PW_STS1, value,
			  (value & SSUSB_IP_SLEEP_STS), 100, 100000);
	if (ret)
		dev_err(ssusb->dev, "ip sleep failed!!!\n");

	return ret;
}

static void ssusb_host_setup(struct ssusb_mtk *ssusb)
{
	struct otg_switch_mtk *otg_sx = &ssusb->otg_switch;

	host_ports_num_get(ssusb);

	/*
	 * power on host and power on/enable all ports
	 * if support OTG, gadget driver will switch port0 to device mode
	 */
	ssusb_host_enable(ssusb);

	if (otg_sx->manual_drd_enabled)
		ssusb_set_force_mode(ssusb, MTU3_DR_FORCE_HOST);

	/* if port0 supports dual-role, works as host mode by default */
	ssusb_set_vbus(&ssusb->otg_switch, 1);
}

static void ssusb_host_cleanup(struct ssusb_mtk *ssusb)
{
	if (ssusb->is_host)
		ssusb_set_vbus(&ssusb->otg_switch, 0);

	ssusb_host_disable(ssusb, false);
}

/*
 * If host supports multiple ports, the VBUSes(5V) of ports except port0
 * which supports OTG are better to be enabled by default in DTS.
 * Because the host driver will keep link with devices attached when system
 * enters suspend mode, so no need to control VBUSes after initialization.
 */
int ssusb_host_init(struct ssusb_mtk *ssusb, struct device_node *parent_dn)
{
	struct device *parent_dev = ssusb->dev;
	int ret;

	ssusb_host_setup(ssusb);

	ret = of_platform_populate(parent_dn, NULL, NULL, parent_dev);
	if (ret) {
		dev_dbg(parent_dev, "failed to create child devices at %pOF\n",
				parent_dn);
		return ret;
	}

	dev_info(parent_dev, "xHCI platform device register success...\n");

	return 0;
}

void ssusb_host_exit(struct ssusb_mtk *ssusb)
{
	of_platform_depopulate(ssusb->dev);
	ssusb_host_cleanup(ssusb);
}

int ssusb_wakeup_enable(struct ssusb_mtk *ssusb)
{
	if (ssusb->wakeup_en)
		ssusb_wakeup_ip_sleep_en(ssusb);

	return 0;
}

void ssusb_wakeup_disable(struct ssusb_mtk *ssusb)
{
	if (ssusb->wakeup_en)
		ssusb_wakeup_ip_sleep_dis(ssusb);
}
