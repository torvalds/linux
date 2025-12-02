// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe controller driver for Renesas R-Car Gen4 Series SoCs
 * Copyright (C) 2022-2023 Renesas Electronics Corporation
 *
 * The r8a779g0 (R-Car V4H) controller requires a specific firmware to be
 * provided, to initialize the PHY. Otherwise, the PCIe controller will not
 * work.
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "../../pci.h"
#include "pcie-designware.h"

/* Renesas-specific */
/* PCIe Mode Setting Register 0 */
#define PCIEMSR0		0x0000
#define APP_SRIS_MODE		BIT(6)
#define DEVICE_TYPE_EP		0
#define DEVICE_TYPE_RC		BIT(4)
#define BIFUR_MOD_SET_ON	BIT(0)

/* PCIe Interrupt Status 0 */
#define PCIEINTSTS0		0x0084

/* PCIe Interrupt Status 0 Enable */
#define PCIEINTSTS0EN		0x0310
#define MSI_CTRL_INT		BIT(26)
#define SMLH_LINK_UP		BIT(7)
#define RDLH_LINK_UP		BIT(6)

/* PCIe DMA Interrupt Status Enable */
#define PCIEDMAINTSTSEN		0x0314
#define PCIEDMAINTSTSEN_INIT	GENMASK(15, 0)

/* Port Logic Registers 89 */
#define PRTLGC89		0x0b70

/* Port Logic Registers 90 */
#define PRTLGC90		0x0b74

/* PCIe Reset Control Register 1 */
#define PCIERSTCTRL1		0x0014
#define APP_HOLD_PHY_RST	BIT(16)
#define APP_LTSSM_ENABLE	BIT(0)

/* PCIe Power Management Control */
#define PCIEPWRMNGCTRL		0x0070
#define APP_CLK_REQ_N		BIT(11)
#define APP_CLK_PM_EN		BIT(10)

#define RCAR_NUM_SPEED_CHANGE_RETRIES	10
#define RCAR_MAX_LINK_SPEED		4

#define RCAR_GEN4_PCIE_EP_FUNC_DBI_OFFSET	0x1000
#define RCAR_GEN4_PCIE_EP_FUNC_DBI2_OFFSET	0x800

#define RCAR_GEN4_PCIE_FIRMWARE_NAME		"rcar_gen4_pcie.bin"
#define RCAR_GEN4_PCIE_FIRMWARE_BASE_ADDR	0xc000
MODULE_FIRMWARE(RCAR_GEN4_PCIE_FIRMWARE_NAME);

struct rcar_gen4_pcie;
struct rcar_gen4_pcie_drvdata {
	void (*additional_common_init)(struct rcar_gen4_pcie *rcar);
	int (*ltssm_control)(struct rcar_gen4_pcie *rcar, bool enable);
	enum dw_pcie_device_mode mode;
};

struct rcar_gen4_pcie {
	struct dw_pcie dw;
	void __iomem *base;
	void __iomem *phy_base;
	struct platform_device *pdev;
	const struct rcar_gen4_pcie_drvdata *drvdata;
};
#define to_rcar_gen4_pcie(_dw)	container_of(_dw, struct rcar_gen4_pcie, dw)

/* Common */
static bool rcar_gen4_pcie_link_up(struct dw_pcie *dw)
{
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	u32 val, mask;

	val = readl(rcar->base + PCIEINTSTS0);
	mask = RDLH_LINK_UP | SMLH_LINK_UP;

	return (val & mask) == mask;
}

/*
 * Manually initiate the speed change. Return 0 if change succeeded; otherwise
 * -ETIMEDOUT.
 */
static int rcar_gen4_pcie_speed_change(struct dw_pcie *dw)
{
	u32 val;
	int i;

	val = dw_pcie_readl_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	val = dw_pcie_readl_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	for (i = 0; i < RCAR_NUM_SPEED_CHANGE_RETRIES; i++) {
		val = dw_pcie_readl_dbi(dw, PCIE_LINK_WIDTH_SPEED_CONTROL);
		if (!(val & PORT_LOGIC_SPEED_CHANGE))
			return 0;
		usleep_range(10000, 11000);
	}

	return -ETIMEDOUT;
}

/*
 * Enable LTSSM of this controller and manually initiate the speed change.
 * Always return 0.
 */
static int rcar_gen4_pcie_start_link(struct dw_pcie *dw)
{
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	int i, changes, ret;

	if (rcar->drvdata->ltssm_control) {
		ret = rcar->drvdata->ltssm_control(rcar, true);
		if (ret)
			return ret;
	}

	/*
	 * Require direct speed change with retrying here if the max_link_speed
	 * is PCIe Gen2 or higher.
	 */
	changes = min_not_zero(dw->max_link_speed, RCAR_MAX_LINK_SPEED) - 1;

	/*
	 * Since dw_pcie_setup_rc() sets it once, PCIe Gen2 will be trained.
	 * So, this needs remaining times for up to PCIe Gen4 if RC mode.
	 */
	if (changes && rcar->drvdata->mode == DW_PCIE_RC_TYPE)
		changes--;

	for (i = 0; i < changes; i++) {
		/* It may not be connected in EP mode yet. So, break the loop */
		if (rcar_gen4_pcie_speed_change(dw))
			break;
	}

	return 0;
}

static void rcar_gen4_pcie_stop_link(struct dw_pcie *dw)
{
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);

	if (rcar->drvdata->ltssm_control)
		rcar->drvdata->ltssm_control(rcar, false);
}

static int rcar_gen4_pcie_common_init(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie *dw = &rcar->dw;
	u32 val;
	int ret;

	ret = clk_bulk_prepare_enable(DW_PCIE_NUM_CORE_CLKS, dw->core_clks);
	if (ret) {
		dev_err(dw->dev, "Enabling core clocks failed\n");
		return ret;
	}

	if (!reset_control_status(dw->core_rsts[DW_PCIE_PWR_RST].rstc)) {
		reset_control_assert(dw->core_rsts[DW_PCIE_PWR_RST].rstc);
		/*
		 * R-Car V4H Reference Manual R19UH0186EJ0130 Rev.1.30 Apr.
		 * 21, 2025 page 585 Figure 9.3.2 Software Reset flow (B)
		 * indicates that for peripherals in HSC domain, after
		 * reset has been asserted by writing a matching reset bit
		 * into register SRCR, it is mandatory to wait 1ms.
		 */
		fsleep(1000);
	}

	val = readl(rcar->base + PCIEMSR0);
	if (rcar->drvdata->mode == DW_PCIE_RC_TYPE) {
		val |= DEVICE_TYPE_RC;
	} else if (rcar->drvdata->mode == DW_PCIE_EP_TYPE) {
		val |= DEVICE_TYPE_EP;
	} else {
		ret = -EINVAL;
		goto err_unprepare;
	}

	if (dw->num_lanes < 4)
		val |= BIFUR_MOD_SET_ON;

	writel(val, rcar->base + PCIEMSR0);

	ret = reset_control_deassert(dw->core_rsts[DW_PCIE_PWR_RST].rstc);
	if (ret)
		goto err_unprepare;

	/*
	 * Assure the reset is latched and the core is ready for DBI access.
	 * On R-Car V4H, the PCIe reset is asynchronous and does not take
	 * effect immediately, but needs a short time to complete. In case
	 * DBI access happens in that short time, that access generates an
	 * SError. To make sure that condition can never happen, read back the
	 * state of the reset, which should turn the asynchronous reset into
	 * synchronous one, and wait a little over 1ms to add additional
	 * safety margin.
	 */
	reset_control_status(dw->core_rsts[DW_PCIE_PWR_RST].rstc);
	fsleep(1000);

	if (rcar->drvdata->additional_common_init)
		rcar->drvdata->additional_common_init(rcar);

	return 0;

err_unprepare:
	clk_bulk_disable_unprepare(DW_PCIE_NUM_CORE_CLKS, dw->core_clks);

	return ret;
}

static void rcar_gen4_pcie_common_deinit(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie *dw = &rcar->dw;

	reset_control_assert(dw->core_rsts[DW_PCIE_PWR_RST].rstc);
	clk_bulk_disable_unprepare(DW_PCIE_NUM_CORE_CLKS, dw->core_clks);
}

static int rcar_gen4_pcie_prepare(struct rcar_gen4_pcie *rcar)
{
	struct device *dev = rcar->dw.dev;
	int err;

	pm_runtime_enable(dev);
	err = pm_runtime_resume_and_get(dev);
	if (err < 0) {
		dev_err(dev, "Runtime resume failed\n");
		pm_runtime_disable(dev);
	}

	return err;
}

static void rcar_gen4_pcie_unprepare(struct rcar_gen4_pcie *rcar)
{
	struct device *dev = rcar->dw.dev;

	pm_runtime_put(dev);
	pm_runtime_disable(dev);
}

static int rcar_gen4_pcie_get_resources(struct rcar_gen4_pcie *rcar)
{
	rcar->phy_base = devm_platform_ioremap_resource_byname(rcar->pdev, "phy");
	if (IS_ERR(rcar->phy_base))
		return PTR_ERR(rcar->phy_base);

	/* Renesas-specific registers */
	rcar->base = devm_platform_ioremap_resource_byname(rcar->pdev, "app");

	return PTR_ERR_OR_ZERO(rcar->base);
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = rcar_gen4_pcie_start_link,
	.stop_link = rcar_gen4_pcie_stop_link,
	.link_up = rcar_gen4_pcie_link_up,
};

static struct rcar_gen4_pcie *rcar_gen4_pcie_alloc(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_gen4_pcie *rcar;

	rcar = devm_kzalloc(dev, sizeof(*rcar), GFP_KERNEL);
	if (!rcar)
		return ERR_PTR(-ENOMEM);

	rcar->dw.ops = &dw_pcie_ops;
	rcar->dw.dev = dev;
	rcar->pdev = pdev;
	rcar->dw.edma.mf = EDMA_MF_EDMA_UNROLL;
	dw_pcie_cap_set(&rcar->dw, REQ_RES);
	platform_set_drvdata(pdev, rcar);

	return rcar;
}

/* Host mode */
static int rcar_gen4_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *dw = to_dw_pcie_from_pp(pp);
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	int ret;
	u32 val;

	gpiod_set_value_cansleep(dw->pe_rst, 1);

	ret = rcar_gen4_pcie_common_init(rcar);
	if (ret)
		return ret;

	/*
	 * According to the section 3.5.7.2 "RC Mode" in DWC PCIe Dual Mode
	 * Rev.5.20a and 3.5.6.1 "RC mode" in DWC PCIe RC databook v5.20a, we
	 * should disable two BARs to avoid unnecessary memory assignment
	 * during device enumeration.
	 */
	dw_pcie_writel_dbi2(dw, PCI_BASE_ADDRESS_0, 0x0);
	dw_pcie_writel_dbi2(dw, PCI_BASE_ADDRESS_1, 0x0);

	/* Enable MSI interrupt signal */
	val = readl(rcar->base + PCIEINTSTS0EN);
	val |= MSI_CTRL_INT;
	writel(val, rcar->base + PCIEINTSTS0EN);

	msleep(PCIE_T_PVPERL_MS);	/* pe_rst requires 100msec delay */

	gpiod_set_value_cansleep(dw->pe_rst, 0);

	return 0;
}

static void rcar_gen4_pcie_host_deinit(struct dw_pcie_rp *pp)
{
	struct dw_pcie *dw = to_dw_pcie_from_pp(pp);
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);

	gpiod_set_value_cansleep(dw->pe_rst, 1);
	rcar_gen4_pcie_common_deinit(rcar);
}

static const struct dw_pcie_host_ops rcar_gen4_pcie_host_ops = {
	.init = rcar_gen4_pcie_host_init,
	.deinit = rcar_gen4_pcie_host_deinit,
};

static int rcar_gen4_add_dw_pcie_rp(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie_rp *pp = &rcar->dw.pp;

	if (!IS_ENABLED(CONFIG_PCIE_RCAR_GEN4_HOST))
		return -ENODEV;

	pp->num_vectors = MAX_MSI_IRQS;
	pp->ops = &rcar_gen4_pcie_host_ops;

	return dw_pcie_host_init(pp);
}

static void rcar_gen4_remove_dw_pcie_rp(struct rcar_gen4_pcie *rcar)
{
	dw_pcie_host_deinit(&rcar->dw.pp);
}

/* Endpoint mode */
static void rcar_gen4_pcie_ep_pre_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *dw = to_dw_pcie_from_ep(ep);
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	int ret;

	ret = rcar_gen4_pcie_common_init(rcar);
	if (ret)
		return;

	writel(PCIEDMAINTSTSEN_INIT, rcar->base + PCIEDMAINTSTSEN);
}

static void rcar_gen4_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie_ep_reset_bar(pci, bar);
}

static void rcar_gen4_pcie_ep_deinit(struct rcar_gen4_pcie *rcar)
{
	writel(0, rcar->base + PCIEDMAINTSTSEN);
	rcar_gen4_pcie_common_deinit(rcar);
}

static int rcar_gen4_pcie_ep_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				       unsigned int type, u16 interrupt_num)
{
	struct dw_pcie *dw = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_IRQ_INTX:
		return dw_pcie_ep_raise_intx_irq(ep, func_no);
	case PCI_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	default:
		dev_err(dw->dev, "Unknown IRQ type\n");
		return -EINVAL;
	}

	return 0;
}

static const struct pci_epc_features rcar_gen4_pcie_epc_features = {
	.msi_capable = true,
	.bar[BAR_1] = { .type = BAR_RESERVED, },
	.bar[BAR_3] = { .type = BAR_RESERVED, },
	.bar[BAR_4] = { .type = BAR_FIXED, .fixed_size = 256 },
	.bar[BAR_5] = { .type = BAR_RESERVED, },
	.align = SZ_1M,
};

static const struct pci_epc_features*
rcar_gen4_pcie_ep_get_features(struct dw_pcie_ep *ep)
{
	return &rcar_gen4_pcie_epc_features;
}

static unsigned int rcar_gen4_pcie_ep_get_dbi_offset(struct dw_pcie_ep *ep,
						       u8 func_no)
{
	return func_no * RCAR_GEN4_PCIE_EP_FUNC_DBI_OFFSET;
}

static unsigned int rcar_gen4_pcie_ep_get_dbi2_offset(struct dw_pcie_ep *ep,
						      u8 func_no)
{
	return func_no * RCAR_GEN4_PCIE_EP_FUNC_DBI2_OFFSET;
}

static const struct dw_pcie_ep_ops pcie_ep_ops = {
	.pre_init = rcar_gen4_pcie_ep_pre_init,
	.init = rcar_gen4_pcie_ep_init,
	.raise_irq = rcar_gen4_pcie_ep_raise_irq,
	.get_features = rcar_gen4_pcie_ep_get_features,
	.get_dbi_offset = rcar_gen4_pcie_ep_get_dbi_offset,
	.get_dbi2_offset = rcar_gen4_pcie_ep_get_dbi2_offset,
};

static int rcar_gen4_add_dw_pcie_ep(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie_ep *ep = &rcar->dw.ep;
	struct device *dev = rcar->dw.dev;
	int ret;

	if (!IS_ENABLED(CONFIG_PCIE_RCAR_GEN4_EP))
		return -ENODEV;

	ep->ops = &pcie_ep_ops;

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		rcar_gen4_pcie_ep_deinit(rcar);
		return ret;
	}

	ret = dw_pcie_ep_init_registers(ep);
	if (ret) {
		dev_err(dev, "Failed to initialize DWC endpoint registers\n");
		dw_pcie_ep_deinit(ep);
		rcar_gen4_pcie_ep_deinit(rcar);
	}

	pci_epc_init_notify(ep->epc);

	return ret;
}

static void rcar_gen4_remove_dw_pcie_ep(struct rcar_gen4_pcie *rcar)
{
	dw_pcie_ep_deinit(&rcar->dw.ep);
	rcar_gen4_pcie_ep_deinit(rcar);
}

/* Common */
static int rcar_gen4_add_dw_pcie(struct rcar_gen4_pcie *rcar)
{
	rcar->drvdata = of_device_get_match_data(&rcar->pdev->dev);
	if (!rcar->drvdata)
		return -EINVAL;

	switch (rcar->drvdata->mode) {
	case DW_PCIE_RC_TYPE:
		return rcar_gen4_add_dw_pcie_rp(rcar);
	case DW_PCIE_EP_TYPE:
		return rcar_gen4_add_dw_pcie_ep(rcar);
	default:
		return -EINVAL;
	}
}

static int rcar_gen4_pcie_probe(struct platform_device *pdev)
{
	struct rcar_gen4_pcie *rcar;
	int err;

	rcar = rcar_gen4_pcie_alloc(pdev);
	if (IS_ERR(rcar))
		return PTR_ERR(rcar);

	err = rcar_gen4_pcie_get_resources(rcar);
	if (err)
		return err;

	err = rcar_gen4_pcie_prepare(rcar);
	if (err)
		return err;

	err = rcar_gen4_add_dw_pcie(rcar);
	if (err)
		goto err_unprepare;

	return 0;

err_unprepare:
	rcar_gen4_pcie_unprepare(rcar);

	return err;
}

static void rcar_gen4_remove_dw_pcie(struct rcar_gen4_pcie *rcar)
{
	switch (rcar->drvdata->mode) {
	case DW_PCIE_RC_TYPE:
		rcar_gen4_remove_dw_pcie_rp(rcar);
		break;
	case DW_PCIE_EP_TYPE:
		rcar_gen4_remove_dw_pcie_ep(rcar);
		break;
	default:
		break;
	}
}

static void rcar_gen4_pcie_remove(struct platform_device *pdev)
{
	struct rcar_gen4_pcie *rcar = platform_get_drvdata(pdev);

	rcar_gen4_remove_dw_pcie(rcar);
	rcar_gen4_pcie_unprepare(rcar);
}

static int r8a779f0_pcie_ltssm_control(struct rcar_gen4_pcie *rcar, bool enable)
{
	u32 val;

	val = readl(rcar->base + PCIERSTCTRL1);
	if (enable) {
		val |= APP_LTSSM_ENABLE;
		val &= ~APP_HOLD_PHY_RST;
	} else {
		/*
		 * Since the datasheet of R-Car doesn't mention how to assert
		 * the APP_HOLD_PHY_RST, don't assert it again. Otherwise,
		 * hang-up issue happened in the dw_edma_core_off() when
		 * the controller didn't detect a PCI device.
		 */
		val &= ~APP_LTSSM_ENABLE;
	}
	writel(val, rcar->base + PCIERSTCTRL1);

	return 0;
}

static void rcar_gen4_pcie_additional_common_init(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie *dw = &rcar->dw;
	u32 val;

	val = dw_pcie_readl_dbi(dw, PCIE_PORT_LANE_SKEW);
	val &= ~PORT_LANE_SKEW_INSERT_MASK;
	if (dw->num_lanes < 4)
		val |= BIT(6);
	dw_pcie_writel_dbi(dw, PCIE_PORT_LANE_SKEW, val);

	val = readl(rcar->base + PCIEPWRMNGCTRL);
	val |= APP_CLK_REQ_N | APP_CLK_PM_EN;
	writel(val, rcar->base + PCIEPWRMNGCTRL);
}

static void rcar_gen4_pcie_phy_reg_update_bits(struct rcar_gen4_pcie *rcar,
					       u32 offset, u32 mask, u32 val)
{
	u32 tmp;

	tmp = readl(rcar->phy_base + offset);
	tmp &= ~mask;
	tmp |= val;
	writel(tmp, rcar->phy_base + offset);
}

/*
 * SoC datasheet suggests checking port logic register bits during firmware
 * write. If read returns non-zero value, then this function returns -EAGAIN
 * indicating that the write needs to be done again. If read returns zero,
 * then return 0 to indicate success.
 */
static int rcar_gen4_pcie_reg_test_bit(struct rcar_gen4_pcie *rcar,
				       u32 offset, u32 mask)
{
	struct dw_pcie *dw = &rcar->dw;

	if (dw_pcie_readl_dbi(dw, offset) & mask)
		return -EAGAIN;

	return 0;
}

static int rcar_gen4_pcie_download_phy_firmware(struct rcar_gen4_pcie *rcar)
{
	/* The check_addr values are magical numbers in the datasheet */
	static const u32 check_addr[] = {
		0x00101018,
		0x00101118,
		0x00101021,
		0x00101121,
	};
	struct dw_pcie *dw = &rcar->dw;
	const struct firmware *fw;
	unsigned int i, timeout;
	u32 data;
	int ret;

	ret = request_firmware(&fw, RCAR_GEN4_PCIE_FIRMWARE_NAME, dw->dev);
	if (ret) {
		dev_err(dw->dev, "Failed to load firmware (%s): %d\n",
			RCAR_GEN4_PCIE_FIRMWARE_NAME, ret);
		return ret;
	}

	for (i = 0; i < (fw->size / 2); i++) {
		data = fw->data[(i * 2) + 1] << 8 | fw->data[i * 2];
		timeout = 100;
		do {
			dw_pcie_writel_dbi(dw, PRTLGC89, RCAR_GEN4_PCIE_FIRMWARE_BASE_ADDR + i);
			dw_pcie_writel_dbi(dw, PRTLGC90, data);
			if (!rcar_gen4_pcie_reg_test_bit(rcar, PRTLGC89, BIT(30)))
				break;
			if (!(--timeout)) {
				ret = -ETIMEDOUT;
				goto exit;
			}
			usleep_range(100, 200);
		} while (1);
	}

	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x0f8, BIT(17), BIT(17));

	for (i = 0; i < ARRAY_SIZE(check_addr); i++) {
		timeout = 100;
		do {
			dw_pcie_writel_dbi(dw, PRTLGC89, check_addr[i]);
			ret = rcar_gen4_pcie_reg_test_bit(rcar, PRTLGC89, BIT(30));
			ret |= rcar_gen4_pcie_reg_test_bit(rcar, PRTLGC90, BIT(0));
			if (!ret)
				break;
			if (!(--timeout)) {
				ret = -ETIMEDOUT;
				goto exit;
			}
			usleep_range(100, 200);
		} while (1);
	}

exit:
	release_firmware(fw);

	return ret;
}

static int rcar_gen4_pcie_ltssm_control(struct rcar_gen4_pcie *rcar, bool enable)
{
	struct dw_pcie *dw = &rcar->dw;
	u32 val;
	int ret;

	if (!enable) {
		val = readl(rcar->base + PCIERSTCTRL1);
		val &= ~APP_LTSSM_ENABLE;
		writel(val, rcar->base + PCIERSTCTRL1);

		return 0;
	}

	val = dw_pcie_readl_dbi(dw, PCIE_PORT_FORCE);
	val |= PORT_FORCE_DO_DESKEW_FOR_SRIS;
	dw_pcie_writel_dbi(dw, PCIE_PORT_FORCE, val);

	val = readl(rcar->base + PCIEMSR0);
	val |= APP_SRIS_MODE;
	writel(val, rcar->base + PCIEMSR0);

	/*
	 * The R-Car Gen4 datasheet doesn't describe the PHY registers' name.
	 * But, the initialization procedure describes these offsets. So,
	 * this driver has magical offset numbers.
	 */
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x700, BIT(28), 0);
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x700, BIT(20), 0);
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x700, BIT(12), 0);
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x700, BIT(4), 0);

	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x148, GENMASK(23, 22), BIT(22));
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x148, GENMASK(18, 16), GENMASK(17, 16));
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x148, GENMASK(7, 6), BIT(6));
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x148, GENMASK(2, 0), GENMASK(1, 0));
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x1d4, GENMASK(16, 15), GENMASK(16, 15));
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x514, BIT(26), BIT(26));
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x0f8, BIT(16), 0);
	rcar_gen4_pcie_phy_reg_update_bits(rcar, 0x0f8, BIT(19), BIT(19));

	val = readl(rcar->base + PCIERSTCTRL1);
	val &= ~APP_HOLD_PHY_RST;
	writel(val, rcar->base + PCIERSTCTRL1);

	ret = readl_poll_timeout(rcar->phy_base + 0x0f8, val, val & BIT(18), 100, 10000);
	if (ret < 0)
		return ret;

	ret = rcar_gen4_pcie_download_phy_firmware(rcar);
	if (ret)
		return ret;

	val = readl(rcar->base + PCIERSTCTRL1);
	val |= APP_LTSSM_ENABLE;
	writel(val, rcar->base + PCIERSTCTRL1);

	return 0;
}

static struct rcar_gen4_pcie_drvdata drvdata_r8a779f0_pcie = {
	.ltssm_control = r8a779f0_pcie_ltssm_control,
	.mode = DW_PCIE_RC_TYPE,
};

static struct rcar_gen4_pcie_drvdata drvdata_r8a779f0_pcie_ep = {
	.ltssm_control = r8a779f0_pcie_ltssm_control,
	.mode = DW_PCIE_EP_TYPE,
};

static struct rcar_gen4_pcie_drvdata drvdata_rcar_gen4_pcie = {
	.additional_common_init = rcar_gen4_pcie_additional_common_init,
	.ltssm_control = rcar_gen4_pcie_ltssm_control,
	.mode = DW_PCIE_RC_TYPE,
};

static struct rcar_gen4_pcie_drvdata drvdata_rcar_gen4_pcie_ep = {
	.additional_common_init = rcar_gen4_pcie_additional_common_init,
	.ltssm_control = rcar_gen4_pcie_ltssm_control,
	.mode = DW_PCIE_EP_TYPE,
};

static const struct of_device_id rcar_gen4_pcie_of_match[] = {
	{
		.compatible = "renesas,r8a779f0-pcie",
		.data = &drvdata_r8a779f0_pcie,
	},
	{
		.compatible = "renesas,r8a779f0-pcie-ep",
		.data = &drvdata_r8a779f0_pcie_ep,
	},
	{
		.compatible = "renesas,rcar-gen4-pcie",
		.data = &drvdata_rcar_gen4_pcie,
	},
	{
		.compatible = "renesas,rcar-gen4-pcie-ep",
		.data = &drvdata_rcar_gen4_pcie_ep,
	},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_gen4_pcie_of_match);

static struct platform_driver rcar_gen4_pcie_driver = {
	.driver = {
		.name = "pcie-rcar-gen4",
		.of_match_table = rcar_gen4_pcie_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = rcar_gen4_pcie_probe,
	.remove = rcar_gen4_pcie_remove,
};
module_platform_driver(rcar_gen4_pcie_driver);

MODULE_DESCRIPTION("Renesas R-Car Gen4 PCIe controller driver");
MODULE_LICENSE("GPL");
