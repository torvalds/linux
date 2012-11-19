/*
 * drivers/mtd/nand/orion_nand.c
 *
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
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <asm/io.h>
#include <asm/sizes.h>
#include <linux/platform_data/mtd-orion_nand.h>

static void orion_nand_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *nc = mtd->priv;
	struct orion_nand_data *board = nc->priv;
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

	writeb(cmd, nc->IO_ADDR_W + offs);
}

static void orion_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	void __iomem *io_base = chip->IO_ADDR_R;
	uint64_t *buf64;
	int i = 0;

	while (len && (unsigned long)buf & 7) {
		*buf++ = readb(io_base);
		len--;
	}
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
	while (i < len)
		buf[i++] = readb(io_base);
}

static int __init orion_nand_probe(struct platform_device *pdev)
{
	struct mtd_info *mtd;
	struct mtd_part_parser_data ppdata = {};
	struct nand_chip *nc;
	struct orion_nand_data *board;
	struct resource *res;
	struct clk *clk;
	void __iomem *io_base;
	int ret = 0;
	u32 val = 0;

	nc = kzalloc(sizeof(struct nand_chip) + sizeof(struct mtd_info), GFP_KERNEL);
	if (!nc) {
		printk(KERN_ERR "orion_nand: failed to allocate device structure.\n");
		ret = -ENOMEM;
		goto no_res;
	}
	mtd = (struct mtd_info *)(nc + 1);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto no_res;
	}

	io_base = ioremap(res->start, resource_size(res));
	if (!io_base) {
		printk(KERN_ERR "orion_nand: ioremap failed\n");
		ret = -EIO;
		goto no_res;
	}

	if (pdev->dev.of_node) {
		board = devm_kzalloc(&pdev->dev, sizeof(struct orion_nand_data),
					GFP_KERNEL);
		if (!board) {
			printk(KERN_ERR "orion_nand: failed to allocate board structure.\n");
			ret = -ENOMEM;
			goto no_res;
		}
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
	} else
		board = pdev->dev.platform_data;

	mtd->priv = nc;
	mtd->owner = THIS_MODULE;

	nc->priv = board;
	nc->IO_ADDR_R = nc->IO_ADDR_W = io_base;
	nc->cmd_ctrl = orion_nand_cmd_ctrl;
	nc->read_buf = orion_nand_read_buf;
	nc->ecc.mode = NAND_ECC_SOFT;

	if (board->chip_delay)
		nc->chip_delay = board->chip_delay;

	WARN(board->width > 16,
		"%d bit bus width out of range",
		board->width);

	if (board->width == 16)
		nc->options |= NAND_BUSWIDTH_16;

	if (board->dev_ready)
		nc->dev_ready = board->dev_ready;

	platform_set_drvdata(pdev, mtd);

	/* Not all platforms can gate the clock, so it is not
	   an error if the clock does not exists. */
	clk = clk_get(&pdev->dev, NULL);
	if (!IS_ERR(clk)) {
		clk_prepare_enable(clk);
		clk_put(clk);
	}

	if (nand_scan(mtd, 1)) {
		ret = -ENXIO;
		goto no_dev;
	}

	mtd->name = "orion_nand";
	ppdata.of_node = pdev->dev.of_node;
	ret = mtd_device_parse_register(mtd, NULL, &ppdata,
			board->parts, board->nr_parts);
	if (ret) {
		nand_release(mtd);
		goto no_dev;
	}

	return 0;

no_dev:
	if (!IS_ERR(clk)) {
		clk_disable_unprepare(clk);
		clk_put(clk);
	}
	platform_set_drvdata(pdev, NULL);
	iounmap(io_base);
no_res:
	kfree(nc);

	return ret;
}

static int __devexit orion_nand_remove(struct platform_device *pdev)
{
	struct mtd_info *mtd = platform_get_drvdata(pdev);
	struct nand_chip *nc = mtd->priv;
	struct clk *clk;

	nand_release(mtd);

	iounmap(nc->IO_ADDR_W);

	kfree(nc);

	clk = clk_get(&pdev->dev, NULL);
	if (!IS_ERR(clk)) {
		clk_disable_unprepare(clk);
		clk_put(clk);
	}

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id orion_nand_of_match_table[] = {
	{ .compatible = "marvell,orion-nand", },
	{},
};
#endif

static struct platform_driver orion_nand_driver = {
	.remove		= orion_nand_remove,
	.driver		= {
		.name	= "orion_nand",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(orion_nand_of_match_table),
	},
};

static int __init orion_nand_init(void)
{
	return platform_driver_probe(&orion_nand_driver, orion_nand_probe);
}

static void __exit orion_nand_exit(void)
{
	platform_driver_unregister(&orion_nand_driver);
}

module_init(orion_nand_init);
module_exit(orion_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tzachi Perelstein");
MODULE_DESCRIPTION("NAND glue for Orion platforms");
MODULE_ALIAS("platform:orion_nand");
