// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory-mapped interface driver for DW SPI Core
 *
 * Copyright (c) 2010, Octasic semiconductor.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/scatterlist.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/acpi.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "spi-dw.h"

#define DRIVER_NAME "dw_spi_mmio"

struct dw_spi_mmio {
	struct dw_spi  dws;
	struct clk     *clk;
	struct clk     *pclk;
	void           *priv;
	struct reset_control *rstc;
};

#define MSCC_CPU_SYSTEM_CTRL_GENERAL_CTRL	0x24
#define OCELOT_IF_SI_OWNER_OFFSET		4
#define JAGUAR2_IF_SI_OWNER_OFFSET		6
#define MSCC_IF_SI_OWNER_MASK			GENMASK(1, 0)
#define MSCC_IF_SI_OWNER_SISL			0
#define MSCC_IF_SI_OWNER_SIBM			1
#define MSCC_IF_SI_OWNER_SIMC			2

#define MSCC_SPI_MST_SW_MODE			0x14
#define MSCC_SPI_MST_SW_MODE_SW_PIN_CTRL_MODE	BIT(13)
#define MSCC_SPI_MST_SW_MODE_SW_SPI_CS(x)	(x << 5)

/*
 * For Keem Bay, CTRLR0[31] is used to select controller mode.
 * 0: SSI is slave
 * 1: SSI is master
 */
#define KEEMBAY_CTRLR0_SSIC_IS_MST		BIT(31)

struct dw_spi_mscc {
	struct regmap       *syscon;
	void __iomem        *spi_mst;
};

/*
 * The Designware SPI controller (referred to as master in the documentation)
 * automatically deasserts chip select when the tx fifo is empty. The chip
 * selects then needs to be either driven as GPIOs or, for the first 4 using the
 * the SPI boot controller registers. the final chip select is an OR gate
 * between the Designware SPI controller and the SPI boot controller.
 */
static void dw_spi_mscc_set_cs(struct spi_device *spi, bool enable)
{
	struct dw_spi *dws = spi_master_get_devdata(spi->master);
	struct dw_spi_mmio *dwsmmio = container_of(dws, struct dw_spi_mmio, dws);
	struct dw_spi_mscc *dwsmscc = dwsmmio->priv;
	u32 cs = spi->chip_select;

	if (cs < 4) {
		u32 sw_mode = MSCC_SPI_MST_SW_MODE_SW_PIN_CTRL_MODE;

		if (!enable)
			sw_mode |= MSCC_SPI_MST_SW_MODE_SW_SPI_CS(BIT(cs));

		writel(sw_mode, dwsmscc->spi_mst + MSCC_SPI_MST_SW_MODE);
	}

	dw_spi_set_cs(spi, enable);
}

static int dw_spi_mscc_init(struct platform_device *pdev,
			    struct dw_spi_mmio *dwsmmio,
			    const char *cpu_syscon, u32 if_si_owner_offset)
{
	struct dw_spi_mscc *dwsmscc;

	dwsmscc = devm_kzalloc(&pdev->dev, sizeof(*dwsmscc), GFP_KERNEL);
	if (!dwsmscc)
		return -ENOMEM;

	dwsmscc->spi_mst = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(dwsmscc->spi_mst)) {
		dev_err(&pdev->dev, "SPI_MST region map failed\n");
		return PTR_ERR(dwsmscc->spi_mst);
	}

	dwsmscc->syscon = syscon_regmap_lookup_by_compatible(cpu_syscon);
	if (IS_ERR(dwsmscc->syscon))
		return PTR_ERR(dwsmscc->syscon);

	/* Deassert all CS */
	writel(0, dwsmscc->spi_mst + MSCC_SPI_MST_SW_MODE);

	/* Select the owner of the SI interface */
	regmap_update_bits(dwsmscc->syscon, MSCC_CPU_SYSTEM_CTRL_GENERAL_CTRL,
			   MSCC_IF_SI_OWNER_MASK << if_si_owner_offset,
			   MSCC_IF_SI_OWNER_SIMC << if_si_owner_offset);

	dwsmmio->dws.set_cs = dw_spi_mscc_set_cs;
	dwsmmio->priv = dwsmscc;

	/* Register hook to configure CTRLR0 */
	dwsmmio->dws.update_cr0 = dw_spi_update_cr0;

	return 0;
}

static int dw_spi_mscc_ocelot_init(struct platform_device *pdev,
				   struct dw_spi_mmio *dwsmmio)
{
	return dw_spi_mscc_init(pdev, dwsmmio, "mscc,ocelot-cpu-syscon",
				OCELOT_IF_SI_OWNER_OFFSET);
}

static int dw_spi_mscc_jaguar2_init(struct platform_device *pdev,
				    struct dw_spi_mmio *dwsmmio)
{
	return dw_spi_mscc_init(pdev, dwsmmio, "mscc,jaguar2-cpu-syscon",
				JAGUAR2_IF_SI_OWNER_OFFSET);
}

static int dw_spi_alpine_init(struct platform_device *pdev,
			      struct dw_spi_mmio *dwsmmio)
{
	dwsmmio->dws.cs_override = 1;

	/* Register hook to configure CTRLR0 */
	dwsmmio->dws.update_cr0 = dw_spi_update_cr0;

	return 0;
}

static int dw_spi_dw_apb_init(struct platform_device *pdev,
			      struct dw_spi_mmio *dwsmmio)
{
	/* Register hook to configure CTRLR0 */
	dwsmmio->dws.update_cr0 = dw_spi_update_cr0;

	dw_spi_dma_setup_generic(&dwsmmio->dws);

	return 0;
}

static int dw_spi_dwc_ssi_init(struct platform_device *pdev,
			       struct dw_spi_mmio *dwsmmio)
{
	/* Register hook to configure CTRLR0 */
	dwsmmio->dws.update_cr0 = dw_spi_update_cr0_v1_01a;

	dw_spi_dma_setup_generic(&dwsmmio->dws);

	return 0;
}

static u32 dw_spi_update_cr0_keembay(struct spi_controller *master,
				     struct spi_device *spi,
				     struct spi_transfer *transfer)
{
	u32 cr0 = dw_spi_update_cr0_v1_01a(master, spi, transfer);

	return cr0 | KEEMBAY_CTRLR0_SSIC_IS_MST;
}

static int dw_spi_keembay_init(struct platform_device *pdev,
			       struct dw_spi_mmio *dwsmmio)
{
	/* Register hook to configure CTRLR0 */
	dwsmmio->dws.update_cr0 = dw_spi_update_cr0_keembay;

	return 0;
}

static int dw_spi_mmio_probe(struct platform_device *pdev)
{
	int (*init_func)(struct platform_device *pdev,
			 struct dw_spi_mmio *dwsmmio);
	struct dw_spi_mmio *dwsmmio;
	struct resource *mem;
	struct dw_spi *dws;
	int ret;
	int num_cs;

	dwsmmio = devm_kzalloc(&pdev->dev, sizeof(struct dw_spi_mmio),
			GFP_KERNEL);
	if (!dwsmmio)
		return -ENOMEM;

	dws = &dwsmmio->dws;

	/* Get basic io resource and map it */
	dws->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(dws->regs))
		return PTR_ERR(dws->regs);

	dws->paddr = mem->start;

	dws->irq = platform_get_irq(pdev, 0);
	if (dws->irq < 0)
		return dws->irq; /* -ENXIO */

	dwsmmio->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dwsmmio->clk))
		return PTR_ERR(dwsmmio->clk);
	ret = clk_prepare_enable(dwsmmio->clk);
	if (ret)
		return ret;

	/* Optional clock needed to access the registers */
	dwsmmio->pclk = devm_clk_get_optional(&pdev->dev, "pclk");
	if (IS_ERR(dwsmmio->pclk)) {
		ret = PTR_ERR(dwsmmio->pclk);
		goto out_clk;
	}
	ret = clk_prepare_enable(dwsmmio->pclk);
	if (ret)
		goto out_clk;

	/* find an optional reset controller */
	dwsmmio->rstc = devm_reset_control_get_optional_exclusive(&pdev->dev, "spi");
	if (IS_ERR(dwsmmio->rstc)) {
		ret = PTR_ERR(dwsmmio->rstc);
		goto out_clk;
	}
	reset_control_deassert(dwsmmio->rstc);

	dws->bus_num = pdev->id;

	dws->max_freq = clk_get_rate(dwsmmio->clk);

	device_property_read_u32(&pdev->dev, "reg-io-width", &dws->reg_io_width);

	num_cs = 4;

	device_property_read_u32(&pdev->dev, "num-cs", &num_cs);

	dws->num_cs = num_cs;

	init_func = device_get_match_data(&pdev->dev);
	if (init_func) {
		ret = init_func(pdev, dwsmmio);
		if (ret)
			goto out;
	}

	pm_runtime_enable(&pdev->dev);

	ret = dw_spi_add_host(&pdev->dev, dws);
	if (ret)
		goto out;

	platform_set_drvdata(pdev, dwsmmio);
	return 0;

out:
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(dwsmmio->pclk);
out_clk:
	clk_disable_unprepare(dwsmmio->clk);
	reset_control_assert(dwsmmio->rstc);

	return ret;
}

static int dw_spi_mmio_remove(struct platform_device *pdev)
{
	struct dw_spi_mmio *dwsmmio = platform_get_drvdata(pdev);

	dw_spi_remove_host(&dwsmmio->dws);
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(dwsmmio->pclk);
	clk_disable_unprepare(dwsmmio->clk);
	reset_control_assert(dwsmmio->rstc);

	return 0;
}

static const struct of_device_id dw_spi_mmio_of_match[] = {
	{ .compatible = "snps,dw-apb-ssi", .data = dw_spi_dw_apb_init},
	{ .compatible = "mscc,ocelot-spi", .data = dw_spi_mscc_ocelot_init},
	{ .compatible = "mscc,jaguar2-spi", .data = dw_spi_mscc_jaguar2_init},
	{ .compatible = "amazon,alpine-dw-apb-ssi", .data = dw_spi_alpine_init},
	{ .compatible = "renesas,rzn1-spi", .data = dw_spi_dw_apb_init},
	{ .compatible = "snps,dwc-ssi-1.01a", .data = dw_spi_dwc_ssi_init},
	{ .compatible = "intel,keembay-ssi", .data = dw_spi_keembay_init},
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, dw_spi_mmio_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id dw_spi_mmio_acpi_match[] = {
	{"HISI0173", (kernel_ulong_t)dw_spi_dw_apb_init},
	{},
};
MODULE_DEVICE_TABLE(acpi, dw_spi_mmio_acpi_match);
#endif

static struct platform_driver dw_spi_mmio_driver = {
	.probe		= dw_spi_mmio_probe,
	.remove		= dw_spi_mmio_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = dw_spi_mmio_of_match,
		.acpi_match_table = ACPI_PTR(dw_spi_mmio_acpi_match),
	},
};
module_platform_driver(dw_spi_mmio_driver);

MODULE_AUTHOR("Jean-Hugues Deschenes <jean-hugues.deschenes@octasic.com>");
MODULE_DESCRIPTION("Memory-mapped I/O interface driver for DW SPI Core");
MODULE_LICENSE("GPL v2");
