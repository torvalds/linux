/*
 * NAND support for Marvell Orion SoC platforms
 *
 * Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/sizes.h>
#include <linux/platform_data/mtd-orion_nand.h>

struct orion_nand_info {
	struct nand_chip chip;
	struct clk *clk;
};

static void orion_nand_cmd_ctrl(struct nand_chip *nc, int cmd,
				unsigned int ctrl)
{
	struct orion_nand_data *board = nand_get_controller_data(nc);
	u32 offs;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		offs = (1 << board->cle);
	else if (ctrl & NAND_ALE)
		offs = (1 << board->ale);
	else
		return;

	if (nc->options & NAND_BUSWIDTH_16)
		offs <<= 1;

	writeb(cmd, nc->legacy.IO_ADDR_W + offs);
}

static void orion_nand_read_buf(struct nand_chip *chip, uint8_t *buf, int len)
{
	void __iomem *io_base = chip->legacy.IO_ADDR_R;
#if defined(__LINUX_ARM_ARCH__) && __LINUX_ARM_ARCH__ >= 5
	uint64_t *buf64;
#endif
	int i = 0;

	while (len && (unsigned long)buf & 7) {
		*buf++ = readb(io_base);
		len--;
	}
#if defined(__LINUX_ARM_ARCH__) && __LINUX_ARM_ARCH__ >= 5
	buf64 = (uint64_t *)buf;
	while (i < len/8) {
		/*
		 * Since GCC has no proper constraint (PR 43518)
		 * force x variable to r2/r3 registers as ldrd instruction
		 * requires first register to be even.
		 */
		register uint64_t x asm ("r2");

		asm volatile ("ldrd\t%0, [%1]" : "=&r" (x) : "r" (io_base));
		buf64[i++] = x;
	}
	i *= 8;
#else
	readsl(io_base, buf, len/4);
	i = len / 4 * 4;
#endif
	while (i < len)
		buf[i++] = readb(io_base);
}

static int __init orion_nand_probe(struct platform_device *pdev)
{
	struct orion_nand_info *info;
	struct mtd_info *mtd;
	struct nand_chip *nc;
	struct orion_nand_data *board;
	struct resource *res;
	void __iomem *io_base;
	int ret = 0;
	u32 val = 0;

	info = devm_kzalloc(&pdev->dev,
			sizeof(struct orion_nand_info),
			GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	nc = &info->chip;
	mtd = nand_to_mtd(nc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io_base = devm_ioremap_resource(&pdev->dev, res);

	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	if (pdev->dev.of_node) {
		board = devm_kzalloc(&pdev->dev, sizeof(struct orion_nand_data),
					GFP_KERNEL);
		if (!board)
			return -ENOMEM;
		if (!of_property_read_u32(pdev->dev.of_node, "cle", &val))
			board->cle = (u8)val;
		else
			board->cle = 0;
		if (!of_property_read_u32(pdev->dev.of_node, "ale", &val))
			board->ale = (u8)val;
		else
			board->ale = 1;
		if (!of_property_read_u32(pdev->dev.of_node,
						"bank-width", &val))
			board->width = (u8)val * 8;
		else
			board->width = 8;
		if (!of_property_read_u32(pdev->dev.of_node,
						"chip-delay", &val))
			board->chip_delay = (u8)val;
	} else {
		board = dev_get_platdata(&pdev->dev);
	}

	mtd->dev.parent = &pdev->dev;

	nand_set_controller_data(nc, board);
	nand_set_flash_node(nc, pdev->dev.of_node);
	nc->legacy.IO_ADDR_R = nc->legacy.IO_ADDR_W = io_base;
	nc->legacy.cmd_ctrl = orion_nand_cmd_ctrl;
	nc->legacy.read_buf = orion_nand_read_buf;
	nc->ecc.mode = NAND_ECC_SOFT;
	nc->ecc.algo = NAND_ECC_HAMMING;

	if (board->chip_delay)
		nc->legacy.chip_delay = board->chip_delay;

	WARN(board->width > 16,
		"%d bit bus width out of range",
		board->width);

	if (board->width == 16)
		nc->options |= NAND_BUSWIDTH_16;

	platform_set_drvdata(pdev, info);

	/* Not all platforms can gate the clock, so it is not
	   an error if the clock does not exists. */
	info->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(info->clk)) {
		ret = PTR_ERR(info->clk);
		if (ret == -ENOENT) {
			info->clk = NULL;
		} else {
			dev_err(&pdev->dev, "failed to get clock!\n");
			return ret;
		}
	}

	ret = clk_prepare_enable(info->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare clock!\n");
		return ret;
	}

	ret = nand_scan(nc, 1);
	if (ret)
		goto no_dev;

	mtd->name = "orion_nand";
	ret = mtd_device_register(mtd, board->parts, board->nr_parts);
	if (ret) {
		nand_cleanup(nc);
		goto no_dev;
	}

	return 0;

no_dev:
	clk_disable_unprepare(info->clk);
	return ret;
}

static int orion_nand_remove(struct platform_device *pdev)
{
	struct orion_nand_info *info = platform_get_drvdata(pdev);
	struct nand_chip *chip = &info->chip;
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);

	nand_cleanup(chip);

	clk_disable_unprepare(info->clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id orion_nand_of_match_table[] = {
	{ .compatible = "marvell,orion-nand", },
	{},
};
MODULE_DEVICE_TABLE(of, orion_nand_of_match_table);
#endif

static struct platform_driver orion_nand_driver = {
	.remove		= orion_nand_remove,
	.driver		= {
		.name	= "orion_nand",
		.of_match_table = of_match_ptr(orion_nand_of_match_table),
	},
};

module_platform_driver_probe(orion_nand_driver, orion_nand_probe);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tzachi Perelstein");
MODULE_DESCRIPTION("NAND glue for Orion platforms");
MODULE_ALIAS("platform:orion_nand");
