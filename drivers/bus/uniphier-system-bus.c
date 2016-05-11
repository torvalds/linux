/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
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
 */

#include <linux/io.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

/* System Bus Controller registers */
#define UNIPHIER_SBC_BASE	0x100	/* base address of bank0 space */
#define    UNIPHIER_SBC_BASE_BE		BIT(0)	/* bank_enable */
#define UNIPHIER_SBC_CTRL0	0x200	/* timing parameter 0 of bank0 */
#define UNIPHIER_SBC_CTRL1	0x204	/* timing parameter 1 of bank0 */
#define UNIPHIER_SBC_CTRL2	0x208	/* timing parameter 2 of bank0 */
#define UNIPHIER_SBC_CTRL3	0x20c	/* timing parameter 3 of bank0 */
#define UNIPHIER_SBC_CTRL4	0x300	/* timing parameter 4 of bank0 */

#define UNIPHIER_SBC_STRIDE	0x10	/* register stride to next bank */
#define UNIPHIER_SBC_NR_BANKS	8	/* number of banks (chip select) */
#define UNIPHIER_SBC_BASE_DUMMY	0xffffffff	/* data to squash bank 0, 1 */

struct uniphier_system_bus_bank {
	u32 base;
	u32 end;
};

struct uniphier_system_bus_priv {
	struct device *dev;
	void __iomem *membase;
	struct uniphier_system_bus_bank bank[UNIPHIER_SBC_NR_BANKS];
};

static int uniphier_system_bus_add_bank(struct uniphier_system_bus_priv *priv,
					int bank, u32 addr, u64 paddr, u32 size)
{
	u64 end, mask;

	dev_dbg(priv->dev,
		"range found: bank = %d, addr = %08x, paddr = %08llx, size = %08x\n",
		bank, addr, paddr, size);

	if (bank >= ARRAY_SIZE(priv->bank)) {
		dev_err(priv->dev, "unsupported bank number %d\n", bank);
		return -EINVAL;
	}

	if (priv->bank[bank].base || priv->bank[bank].end) {
		dev_err(priv->dev,
			"range for bank %d has already been specified\n", bank);
		return -EINVAL;
	}

	if (paddr > U32_MAX) {
		dev_err(priv->dev, "base address %llx is too high\n", paddr);
		return -EINVAL;
	}

	end = paddr + size;

	if (addr > paddr) {
		dev_err(priv->dev,
			"base %08x cannot be mapped to %08llx of parent\n",
			addr, paddr);
		return -EINVAL;
	}
	paddr -= addr;

	paddr = round_down(paddr, 0x00020000);
	end = round_up(end, 0x00020000);

	if (end > U32_MAX) {
		dev_err(priv->dev, "end address %08llx is too high\n", end);
		return -EINVAL;
	}
	mask = paddr ^ (end - 1);
	mask = roundup_pow_of_two(mask);

	paddr = round_down(paddr, mask);
	end = round_up(end, mask);

	priv->bank[bank].base = paddr;
	priv->bank[bank].end = end;

	dev_dbg(priv->dev, "range added: bank = %d, addr = %08x, end = %08x\n",
		bank, priv->bank[bank].base, priv->bank[bank].end);

	return 0;
}

static int uniphier_system_bus_check_overlap(
				const struct uniphier_system_bus_priv *priv)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(priv->bank); i++) {
		for (j = i + 1; j < ARRAY_SIZE(priv->bank); j++) {
			if (priv->bank[i].end > priv->bank[j].base &&
			    priv->bank[i].base < priv->bank[j].end) {
				dev_err(priv->dev,
					"region overlap between bank%d and bank%d\n",
					i, j);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static void uniphier_system_bus_check_boot_swap(
					struct uniphier_system_bus_priv *priv)
{
	void __iomem *base_reg = priv->membase + UNIPHIER_SBC_BASE;
	int is_swapped;

	is_swapped = !(readl(base_reg) & UNIPHIER_SBC_BASE_BE);

	dev_dbg(priv->dev, "Boot Swap: %s\n", is_swapped ? "on" : "off");

	/*
	 * If BOOT_SWAP was asserted on power-on-reset, the CS0 and CS1 are
	 * swapped.  In this case, bank0 and bank1 should be swapped as well.
	 */
	if (is_swapped)
		swap(priv->bank[0], priv->bank[1]);
}

static void uniphier_system_bus_set_reg(
				const struct uniphier_system_bus_priv *priv)
{
	void __iomem *base_reg = priv->membase + UNIPHIER_SBC_BASE;
	u32 base, end, mask, val;
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->bank); i++) {
		base = priv->bank[i].base;
		end = priv->bank[i].end;

		if (base == end) {
			/*
			 * If SBC_BASE0 or SBC_BASE1 is set to zero, the access
			 * to anywhere in the system bus space is routed to
			 * bank 0 (if boot swap if off) or bank 1 (if boot swap
			 * if on).  It means that CPUs cannot get access to
			 * bank 2 or later.  In other words, bank 0/1 cannot
			 * be disabled even if its bank_enable bits is cleared.
			 * This seems odd, but it is how this hardware goes.
			 * As a workaround, dummy data (0xffffffff) should be
			 * set when the bank 0/1 is unused.  As for bank 2 and
			 * later, they can be simply disable by clearing the
			 * bank_enable bit.
			 */
			if (i < 2)
				val = UNIPHIER_SBC_BASE_DUMMY;
			else
				val = 0;
		} else {
			mask = base ^ (end - 1);

			val = base & 0xfffe0000;
			val |= (~mask >> 16) & 0xfffe;
			val |= UNIPHIER_SBC_BASE_BE;
		}
		dev_dbg(priv->dev, "SBC_BASE[%d] = 0x%08x\n", i, val);

		writel(val, base_reg + UNIPHIER_SBC_STRIDE * i);
	}
}

static int uniphier_system_bus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_system_bus_priv *priv;
	struct resource *regs;
	const __be32 *ranges;
	u32 cells, addr, size;
	u64 paddr;
	int pna, bank, rlen, rone, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->membase = devm_ioremap_resource(dev, regs);
	if (IS_ERR(priv->membase))
		return PTR_ERR(priv->membase);

	priv->dev = dev;

	pna = of_n_addr_cells(dev->of_node);

	ret = of_property_read_u32(dev->of_node, "#address-cells", &cells);
	if (ret) {
		dev_err(dev, "failed to get #address-cells\n");
		return ret;
	}
	if (cells != 2) {
		dev_err(dev, "#address-cells must be 2\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(dev->of_node, "#size-cells", &cells);
	if (ret) {
		dev_err(dev, "failed to get #size-cells\n");
		return ret;
	}
	if (cells != 1) {
		dev_err(dev, "#size-cells must be 1\n");
		return -EINVAL;
	}

	ranges = of_get_property(dev->of_node, "ranges", &rlen);
	if (!ranges) {
		dev_err(dev, "failed to get ranges property\n");
		return -ENOENT;
	}

	rlen /= sizeof(*ranges);
	rone = pna + 2;

	for (; rlen >= rone; rlen -= rone) {
		bank = be32_to_cpup(ranges++);
		addr = be32_to_cpup(ranges++);
		paddr = of_translate_address(dev->of_node, ranges);
		if (paddr == OF_BAD_ADDR)
			return -EINVAL;
		ranges += pna;
		size = be32_to_cpup(ranges++);

		ret = uniphier_system_bus_add_bank(priv, bank, addr,
						   paddr, size);
		if (ret)
			return ret;
	}

	ret = uniphier_system_bus_check_overlap(priv);
	if (ret)
		return ret;

	uniphier_system_bus_check_boot_swap(priv);

	uniphier_system_bus_set_reg(priv);

	/* Now, the bus is configured.  Populate platform_devices below it */
	return of_platform_populate(dev->of_node, of_default_bus_match_table,
				    NULL, dev);
}

static const struct of_device_id uniphier_system_bus_match[] = {
	{ .compatible = "socionext,uniphier-system-bus" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_system_bus_match);

static struct platform_driver uniphier_system_bus_driver = {
	.probe		= uniphier_system_bus_probe,
	.driver = {
		.name	= "uniphier-system-bus",
		.of_match_table = uniphier_system_bus_match,
	},
};
module_platform_driver(uniphier_system_bus_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier System Bus driver");
MODULE_LICENSE("GPL");
