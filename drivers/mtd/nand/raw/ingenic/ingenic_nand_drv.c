// SPDX-License-Identifier: GPL-2.0
/*
 * Ingenic JZ47xx NAND driver
 *
 * Copyright (c) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>

#include <linux/jz4780-nemc.h>

#include "ingenic_ecc.h"

#define DRV_NAME	"ingenic-nand"

/* Command delay when there is no R/B pin. */
#define RB_DELAY_US	100

struct jz_soc_info {
	unsigned long data_offset;
	unsigned long addr_offset;
	unsigned long cmd_offset;
	const struct mtd_ooblayout_ops *oob_layout;
};

struct ingenic_nand_cs {
	unsigned int bank;
	void __iomem *base;
};

struct ingenic_nfc {
	struct device *dev;
	struct ingenic_ecc *ecc;
	const struct jz_soc_info *soc_info;
	struct nand_controller controller;
	unsigned int num_banks;
	struct list_head chips;
	int selected;
	struct ingenic_nand_cs cs[];
};

struct ingenic_nand {
	struct nand_chip chip;
	struct list_head chip_list;

	struct gpio_desc *busy_gpio;
	struct gpio_desc *wp_gpio;
	unsigned int reading: 1;
};

static inline struct ingenic_nand *to_ingenic_nand(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct ingenic_nand, chip);
}

static inline struct ingenic_nfc *to_ingenic_nfc(struct nand_controller *ctrl)
{
	return container_of(ctrl, struct ingenic_nfc, controller);
}

static int qi_lb60_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (section || !ecc->total)
		return -ERANGE;

	oobregion->length = ecc->total;
	oobregion->offset = 12;

	return 0;
}

static int qi_lb60_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (section)
		return -ERANGE;

	oobregion->length = mtd->oobsize - ecc->total - 12;
	oobregion->offset = 12 + ecc->total;

	return 0;
}

const struct mtd_ooblayout_ops qi_lb60_ooblayout_ops = {
	.ecc = qi_lb60_ooblayout_ecc,
	.free = qi_lb60_ooblayout_free,
};

static int jz4725b_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (section || !ecc->total)
		return -ERANGE;

	oobregion->length = ecc->total;
	oobregion->offset = 3;

	return 0;
}

static int jz4725b_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (section)
		return -ERANGE;

	oobregion->length = mtd->oobsize - ecc->total - 3;
	oobregion->offset = 3 + ecc->total;

	return 0;
}

static const struct mtd_ooblayout_ops jz4725b_ooblayout_ops = {
	.ecc = jz4725b_ooblayout_ecc,
	.free = jz4725b_ooblayout_free,
};

static void ingenic_nand_select_chip(struct nand_chip *chip, int chipnr)
{
	struct ingenic_nand *nand = to_ingenic_nand(nand_to_mtd(chip));
	struct ingenic_nfc *nfc = to_ingenic_nfc(nand->chip.controller);
	struct ingenic_nand_cs *cs;

	/* Ensure the currently selected chip is deasserted. */
	if (chipnr == -1 && nfc->selected >= 0) {
		cs = &nfc->cs[nfc->selected];
		jz4780_nemc_assert(nfc->dev, cs->bank, false);
	}

	nfc->selected = chipnr;
}

static void ingenic_nand_cmd_ctrl(struct nand_chip *chip, int cmd,
				  unsigned int ctrl)
{
	struct ingenic_nand *nand = to_ingenic_nand(nand_to_mtd(chip));
	struct ingenic_nfc *nfc = to_ingenic_nfc(nand->chip.controller);
	struct ingenic_nand_cs *cs;

	if (WARN_ON(nfc->selected < 0))
		return;

	cs = &nfc->cs[nfc->selected];

	jz4780_nemc_assert(nfc->dev, cs->bank, ctrl & NAND_NCE);

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_ALE)
		writeb(cmd, cs->base + nfc->soc_info->addr_offset);
	else if (ctrl & NAND_CLE)
		writeb(cmd, cs->base + nfc->soc_info->cmd_offset);
}

static int ingenic_nand_dev_ready(struct nand_chip *chip)
{
	struct ingenic_nand *nand = to_ingenic_nand(nand_to_mtd(chip));

	return !gpiod_get_value_cansleep(nand->busy_gpio);
}

static void ingenic_nand_ecc_hwctl(struct nand_chip *chip, int mode)
{
	struct ingenic_nand *nand = to_ingenic_nand(nand_to_mtd(chip));

	nand->reading = (mode == NAND_ECC_READ);
}

static int ingenic_nand_ecc_calculate(struct nand_chip *chip, const u8 *dat,
				      u8 *ecc_code)
{
	struct ingenic_nand *nand = to_ingenic_nand(nand_to_mtd(chip));
	struct ingenic_nfc *nfc = to_ingenic_nfc(nand->chip.controller);
	struct ingenic_ecc_params params;

	/*
	 * Don't need to generate the ECC when reading, the ECC engine does it
	 * for us as part of decoding/correction.
	 */
	if (nand->reading)
		return 0;

	params.size = nand->chip.ecc.size;
	params.bytes = nand->chip.ecc.bytes;
	params.strength = nand->chip.ecc.strength;

	return ingenic_ecc_calculate(nfc->ecc, &params, dat, ecc_code);
}

static int ingenic_nand_ecc_correct(struct nand_chip *chip, u8 *dat,
				    u8 *read_ecc, u8 *calc_ecc)
{
	struct ingenic_nand *nand = to_ingenic_nand(nand_to_mtd(chip));
	struct ingenic_nfc *nfc = to_ingenic_nfc(nand->chip.controller);
	struct ingenic_ecc_params params;

	params.size = nand->chip.ecc.size;
	params.bytes = nand->chip.ecc.bytes;
	params.strength = nand->chip.ecc.strength;

	return ingenic_ecc_correct(nfc->ecc, &params, dat, read_ecc);
}

static int ingenic_nand_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct ingenic_nfc *nfc = to_ingenic_nfc(chip->controller);
	int eccbytes;

	if (chip->ecc.strength == 4) {
		/* JZ4740 uses 9 bytes of ECC to correct maximum 4 errors */
		chip->ecc.bytes = 9;
	} else {
		chip->ecc.bytes = fls((1 + 8) * chip->ecc.size)	*
				  (chip->ecc.strength / 8);
	}

	switch (chip->ecc.mode) {
	case NAND_ECC_HW:
		if (!nfc->ecc) {
			dev_err(nfc->dev, "HW ECC selected, but ECC controller not found\n");
			return -ENODEV;
		}

		chip->ecc.hwctl = ingenic_nand_ecc_hwctl;
		chip->ecc.calculate = ingenic_nand_ecc_calculate;
		chip->ecc.correct = ingenic_nand_ecc_correct;
		/* fall through */
	case NAND_ECC_SOFT:
		dev_info(nfc->dev, "using %s (strength %d, size %d, bytes %d)\n",
			 (nfc->ecc) ? "hardware ECC" : "software ECC",
			 chip->ecc.strength, chip->ecc.size, chip->ecc.bytes);
		break;
	case NAND_ECC_NONE:
		dev_info(nfc->dev, "not using ECC\n");
		break;
	default:
		dev_err(nfc->dev, "ECC mode %d not supported\n",
			chip->ecc.mode);
		return -EINVAL;
	}

	/* The NAND core will generate the ECC layout for SW ECC */
	if (chip->ecc.mode != NAND_ECC_HW)
		return 0;

	/* Generate ECC layout. ECC codes are right aligned in the OOB area. */
	eccbytes = mtd->writesize / chip->ecc.size * chip->ecc.bytes;

	if (eccbytes > mtd->oobsize - 2) {
		dev_err(nfc->dev,
			"invalid ECC config: required %d ECC bytes, but only %d are available",
			eccbytes, mtd->oobsize - 2);
		return -EINVAL;
	}

	/*
	 * The generic layout for BBT markers will most likely overlap with our
	 * ECC bytes in the OOB, so move the BBT markers outside the OOB area.
	 */
	if (chip->bbt_options & NAND_BBT_USE_FLASH)
		chip->bbt_options |= NAND_BBT_NO_OOB;

	/* For legacy reasons we use a different layout on the qi,lb60 board. */
	if (of_machine_is_compatible("qi,lb60"))
		mtd_set_ooblayout(mtd, &qi_lb60_ooblayout_ops);
	else
		mtd_set_ooblayout(mtd, nfc->soc_info->oob_layout);

	return 0;
}

static const struct nand_controller_ops ingenic_nand_controller_ops = {
	.attach_chip = ingenic_nand_attach_chip,
};

static int ingenic_nand_init_chip(struct platform_device *pdev,
				  struct ingenic_nfc *nfc,
				  struct device_node *np,
				  unsigned int chipnr)
{
	struct device *dev = &pdev->dev;
	struct ingenic_nand *nand;
	struct ingenic_nand_cs *cs;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	const __be32 *reg;
	int ret = 0;

	cs = &nfc->cs[chipnr];

	reg = of_get_property(np, "reg", NULL);
	if (!reg)
		return -EINVAL;

	cs->bank = be32_to_cpu(*reg);

	jz4780_nemc_set_type(nfc->dev, cs->bank, JZ4780_NEMC_BANK_NAND);

	cs->base = devm_platform_ioremap_resource(pdev, chipnr);
	if (IS_ERR(cs->base))
		return PTR_ERR(cs->base);

	nand = devm_kzalloc(dev, sizeof(*nand), GFP_KERNEL);
	if (!nand)
		return -ENOMEM;

	nand->busy_gpio = devm_gpiod_get_optional(dev, "rb", GPIOD_IN);

	if (IS_ERR(nand->busy_gpio)) {
		ret = PTR_ERR(nand->busy_gpio);
		dev_err(dev, "failed to request busy GPIO: %d\n", ret);
		return ret;
	} else if (nand->busy_gpio) {
		nand->chip.legacy.dev_ready = ingenic_nand_dev_ready;
	}

	nand->wp_gpio = devm_gpiod_get_optional(dev, "wp", GPIOD_OUT_LOW);

	if (IS_ERR(nand->wp_gpio)) {
		ret = PTR_ERR(nand->wp_gpio);
		dev_err(dev, "failed to request WP GPIO: %d\n", ret);
		return ret;
	}

	chip = &nand->chip;
	mtd = nand_to_mtd(chip);
	mtd->name = devm_kasprintf(dev, GFP_KERNEL, "%s.%d", dev_name(dev),
				   cs->bank);
	if (!mtd->name)
		return -ENOMEM;
	mtd->dev.parent = dev;

	chip->legacy.IO_ADDR_R = cs->base + nfc->soc_info->data_offset;
	chip->legacy.IO_ADDR_W = cs->base + nfc->soc_info->data_offset;
	chip->legacy.chip_delay = RB_DELAY_US;
	chip->options = NAND_NO_SUBPAGE_WRITE;
	chip->legacy.select_chip = ingenic_nand_select_chip;
	chip->legacy.cmd_ctrl = ingenic_nand_cmd_ctrl;
	chip->ecc.mode = NAND_ECC_HW;
	chip->controller = &nfc->controller;
	nand_set_flash_node(chip, np);

	chip->controller->ops = &ingenic_nand_controller_ops;
	ret = nand_scan(chip, 1);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		nand_release(chip);
		return ret;
	}

	list_add_tail(&nand->chip_list, &nfc->chips);

	return 0;
}

static void ingenic_nand_cleanup_chips(struct ingenic_nfc *nfc)
{
	struct ingenic_nand *chip;

	while (!list_empty(&nfc->chips)) {
		chip = list_first_entry(&nfc->chips,
					struct ingenic_nand, chip_list);
		nand_release(&chip->chip);
		list_del(&chip->chip_list);
	}
}

static int ingenic_nand_init_chips(struct ingenic_nfc *nfc,
				   struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	int i = 0;
	int ret;
	int num_chips = of_get_child_count(dev->of_node);

	if (num_chips > nfc->num_banks) {
		dev_err(dev, "found %d chips but only %d banks\n",
			num_chips, nfc->num_banks);
		return -EINVAL;
	}

	for_each_child_of_node(dev->of_node, np) {
		ret = ingenic_nand_init_chip(pdev, nfc, np, i);
		if (ret) {
			ingenic_nand_cleanup_chips(nfc);
			of_node_put(np);
			return ret;
		}

		i++;
	}

	return 0;
}

static int ingenic_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned int num_banks;
	struct ingenic_nfc *nfc;
	int ret;

	num_banks = jz4780_nemc_num_banks(dev);
	if (num_banks == 0) {
		dev_err(dev, "no banks found\n");
		return -ENODEV;
	}

	nfc = devm_kzalloc(dev, struct_size(nfc, cs, num_banks), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	nfc->soc_info = device_get_match_data(dev);
	if (!nfc->soc_info)
		return -EINVAL;

	/*
	 * Check for ECC HW before we call nand_scan_ident, to prevent us from
	 * having to call it again if the ECC driver returns -EPROBE_DEFER.
	 */
	nfc->ecc = of_ingenic_ecc_get(dev->of_node);
	if (IS_ERR(nfc->ecc))
		return PTR_ERR(nfc->ecc);

	nfc->dev = dev;
	nfc->num_banks = num_banks;

	nand_controller_init(&nfc->controller);
	INIT_LIST_HEAD(&nfc->chips);

	ret = ingenic_nand_init_chips(nfc, pdev);
	if (ret) {
		if (nfc->ecc)
			ingenic_ecc_release(nfc->ecc);
		return ret;
	}

	platform_set_drvdata(pdev, nfc);
	return 0;
}

static int ingenic_nand_remove(struct platform_device *pdev)
{
	struct ingenic_nfc *nfc = platform_get_drvdata(pdev);

	if (nfc->ecc)
		ingenic_ecc_release(nfc->ecc);

	ingenic_nand_cleanup_chips(nfc);

	return 0;
}

static const struct jz_soc_info jz4740_soc_info = {
	.data_offset = 0x00000000,
	.cmd_offset = 0x00008000,
	.addr_offset = 0x00010000,
	.oob_layout = &nand_ooblayout_lp_ops,
};

static const struct jz_soc_info jz4725b_soc_info = {
	.data_offset = 0x00000000,
	.cmd_offset = 0x00008000,
	.addr_offset = 0x00010000,
	.oob_layout = &jz4725b_ooblayout_ops,
};

static const struct jz_soc_info jz4780_soc_info = {
	.data_offset = 0x00000000,
	.cmd_offset = 0x00400000,
	.addr_offset = 0x00800000,
	.oob_layout = &nand_ooblayout_lp_ops,
};

static const struct of_device_id ingenic_nand_dt_match[] = {
	{ .compatible = "ingenic,jz4740-nand", .data = &jz4740_soc_info },
	{ .compatible = "ingenic,jz4725b-nand", .data = &jz4725b_soc_info },
	{ .compatible = "ingenic,jz4780-nand", .data = &jz4780_soc_info },
	{},
};
MODULE_DEVICE_TABLE(of, ingenic_nand_dt_match);

static struct platform_driver ingenic_nand_driver = {
	.probe		= ingenic_nand_probe,
	.remove		= ingenic_nand_remove,
	.driver	= {
		.name	= DRV_NAME,
		.of_match_table = of_match_ptr(ingenic_nand_dt_match),
	},
};
module_platform_driver(ingenic_nand_driver);

MODULE_AUTHOR("Alex Smith <alex@alex-smith.me.uk>");
MODULE_AUTHOR("Harvey Hunt <harveyhuntnexus@gmail.com>");
MODULE_DESCRIPTION("Ingenic JZ47xx NAND driver");
MODULE_LICENSE("GPL v2");
