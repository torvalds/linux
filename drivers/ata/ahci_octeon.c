/*
 * SATA glue for Cavium Octeon III SOCs.
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2010-2015 Cavium Networks
 *
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>

#include <asm/octeon/octeon.h>

#define CVMX_SATA_UCTL_SHIM_CFG		0xE8

#define SATA_UCTL_ENDIAN_MODE_BIG	1
#define SATA_UCTL_ENDIAN_MODE_LITTLE	0
#define SATA_UCTL_ENDIAN_MODE_MASK	3

#define SATA_UCTL_DMA_ENDIAN_MODE_SHIFT	8
#define SATA_UCTL_CSR_ENDIAN_MODE_SHIFT	0
#define SATA_UCTL_DMA_READ_CMD_SHIFT	12

static int ahci_octeon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	void __iomem *base;
	u64 cfg;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	cfg = cvmx_readq_csr(base + CVMX_SATA_UCTL_SHIM_CFG);

	cfg &= ~(SATA_UCTL_ENDIAN_MODE_MASK << SATA_UCTL_DMA_ENDIAN_MODE_SHIFT);
	cfg &= ~(SATA_UCTL_ENDIAN_MODE_MASK << SATA_UCTL_CSR_ENDIAN_MODE_SHIFT);

#ifdef __BIG_ENDIAN
	cfg |= SATA_UCTL_ENDIAN_MODE_BIG << SATA_UCTL_DMA_ENDIAN_MODE_SHIFT;
	cfg |= SATA_UCTL_ENDIAN_MODE_BIG << SATA_UCTL_CSR_ENDIAN_MODE_SHIFT;
#else
	cfg |= SATA_UCTL_ENDIAN_MODE_LITTLE << SATA_UCTL_DMA_ENDIAN_MODE_SHIFT;
	cfg |= SATA_UCTL_ENDIAN_MODE_LITTLE << SATA_UCTL_CSR_ENDIAN_MODE_SHIFT;
#endif

	cfg |= 1 << SATA_UCTL_DMA_READ_CMD_SHIFT;

	cvmx_writeq_csr(base + CVMX_SATA_UCTL_SHIM_CFG, cfg);

	if (!node) {
		dev_err(dev, "no device node, failed to add octeon sata\n");
		return -ENODEV;
	}

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to add ahci-platform core\n");
		return ret;
	}

	return 0;
}

static const struct of_device_id octeon_ahci_match[] = {
	{ .compatible = "cavium,octeon-7130-sata-uctl", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, octeon_ahci_match);

static struct platform_driver ahci_octeon_driver = {
	.probe          = ahci_octeon_probe,
	.driver         = {
		.name   = "octeon-ahci",
		.of_match_table = octeon_ahci_match,
	},
};

module_platform_driver(ahci_octeon_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cavium, Inc. <support@cavium.com>");
MODULE_DESCRIPTION("Cavium Inc. sata config.");
