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

struct jz_soc_info {
	unsigned long data_offset;
	unsigned long addr_offset;
	unsigned long cmd_offset;
	const struct mtd_ooblayout_ops *oob_layout;
	bool oob_first;
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

static const struct mtd_ooblayout_ops qi_lb60_ooblayout_ops = {
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

	switch (chip->ecc.engine_type) {
	case NAND_ECC_ENGINE_TYPE_ON_HOST:
		if (!nfc->ecc) {
			dev_err(nfc->dev, "HW ECC selected, but ECC controller not found\n");
			return -ENODEV;
		}

		chip->ecc.hwctl = ingenic_nand_ecc_hwctl;
		chip->ecc.calculate = ingenic_nand_ecc_calculate;
		chip->ecc.correct = ingenic_nand_ecc_correct;
		fallthrough;
	case NAND_ECC_ENGINE_TYPE_SOFT:
		dev_info(nfc->dev, "using %s (strength %d, size %d, bytes %d)\n",
			 (nfc->ecc) ? "hardware ECC" : "software ECC",
			 chip->ecc.strength, chip->ecc.size, chip->ecc.bytes);
		break;
	case NAND_ECC_ENGINE_TYPE_NONE:
		dev_info(nfc->dev, "not using ECC\n");
		break;
	default:
		dev_err(nfc->dev, "ECC mode %d not supported\n",
			chip->ecc.engine_type);
		return -EINVAL;
	}

	/* The NAND core will generate the ECC layout for SW ECC */
	if (chip->ecc.engine_type != NAND_ECC_ENGINE_TYPE_ON_HOST)
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

	if (nfc->soc_info->oob_first)
		chip->ecc.read_page = nand_read_page_hwecc_oob_first;

	/* For legacy reasons we use a different layout on the qi,lb60 board. */
	if (of_machine_is_compatible("qi,lb60"))
		mtd_set_ooblayout(mtd, &qi_lb60_ooblayout_ops);
	else if (nfc->soc_info->oob_layout)
		mtd_set_ooblayout(mtd, nfc->soc_info->oob_layout);
	else
		mtd_set_ooblayout(mtd, nand_get_large_page_ooblayout());

	return 0;
}

static int ingenic_nand_exec_instr(struct nand_chip *chip,
				   struct ingenic_nand_cs *cs,
				   const struct nand_op_instr *instr)
{
	struct ingenic_nand *nand = to_ingenic_nand(nand_to_mtd(chip));
	struct ingenic_nfc *nfc = to_ingenic_nfc(chip->controller);
	unsigned int i;

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		writeb(instr->ctx.cmd.opcode,
		       cs->base + nfc->soc_info->cmd_offset);
		return 0;
	case NAND_OP_ADDR_INSTR:
		for (i = 0; i < instr->ctx.addr.naddrs; i++)
			writeb(instr->ctx.addr.addrs[i],
			       cs->base + nfc->soc_info->addr_offset);
		return 0;
	case NAND_OP_DATA_IN_INSTR:
		if (instr->ctx.data.force_8bit ||
		    !(chip->options & NAND_BUSWIDTH_16))
			ioread8_rep(cs->base + nfc->soc_info->data_offset,
				    instr->ctx.data.buf.in,
				    instr->ctx.data.len);
		else
			ioread16_rep(cs->base + nfc->soc_info->data_offset,
				     instr->ctx.data.buf.in,
				     instr->ctx.data.len);
		return 0;
	case NAND_OP_DATA_OUT_INSTR:
		if (instr->ctx.data.force_8bit ||
		    !(chip->options & NAND_BUSWIDTH_16))
			iowrite8_rep(cs->base + nfc->soc_info->data_offset,
				     instr->ctx.data.buf.out,
				     instr->ctx.data.len);
		else
			iowrite16_rep(cs->base + nfc->soc_info->data_offset,
				      instr->ctx.data.buf.out,
				      instr->ctx.data.len);
		return 0;
	case NAND_OP_WAITRDY_INSTR:
		if (!nand->busy_gpio)
			return nand_soft_waitrdy(chip,
						 instr->ctx.waitrdy.timeout_ms);

		return nand_gpio_waitrdy(chip, nand->busy_gpio,
					 instr->ctx.waitrdy.timeout_ms);
	default:
		break;
	}

	return -EINVAL;
}

static int ingenic_nand_exec_op(struct nand_chip *chip,
				const struct nand_operation *op,
				bool check_only)
{
	struct ingenic_nand *nand = to_ingenic_nand(nand_to_mtd(chip));
	struct ingenic_nfc *nfc = to_ingenic_nfc(nand->chip.controller);
	struct ingenic_nand_cs *cs;
	unsigned int i;
	int ret = 0;

	if (check_only)
		return 0;

	cs = &nfc->cs[op->cs];
	jz4780_nemc_assert(nfc->dev, cs->bank, true);
	for (i = 0; i < op->ninstrs; i++) {
		ret = ingenic_nand_exec_instr(chip, cs, &op->instrs[i]);
		if (ret)
			break;

		if (op->instrs[i].delay_ns)
			ndelay(op->instrs[i].delay_ns);
	}
	jz4780_nemc_assert(nfc->dev, cs->bank, false);

	return ret;
}

static const struct nand_controller_ops ingenic_nand_controller_ops = {
	.attach_chip = ingenic_nand_attach_chip,
	.exec_op = ingenic_nand_exec_op,
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
	}

	/*
	 * The rb-gpios semantics was undocumented and qi,lb60 (along with
	 * the ingenic driver) got it wrong. The active state encodes the
	 * NAND ready state, which is high level. Since there's no signal
	 * inverter on this board, it should be active-high. Let's fix that
	 * here for older DTs so we can re-use the generic nand_gpio_waitrdy()
	 * helper, and be consistent with what other drivers do.
	 */
	if (of_machine_is_compatible("qi,lb60") &&
	    gpiod_is_active_low(nand->busy_gpio))
		gpiod_toggle_active_low(nand->busy_gpio);

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

	chip->options = NAND_NO_SUBPAGE_WRITE;
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;
	chip->controller = &nfc->controller;
	nand_set_flash_node(chip, np);

	chip->controller->ops = &ingenic_nand_controller_ops;
	ret = nand_scan(chip, 1);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		nand_cleanup(chip);
		return ret;
	}

	list_add_tail(&nand->chip_list, &nfc->chips);

	return 0;
}

static void ingenic_nand_cleanup_chips(struct ingenic_nfc *nfc)
{
	struct ingenic_nand *ingenic_chip;
	struct nand_chip *chip;
	int ret;

	while (!list_empty(&nfc->chips)) {
		ingenic_chip = list_first_entry(&nfc->chips,
						struct ingenic_nand, chip_list);
		chip = &ingenic_chip->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
		list_del(&ingenic_chip->chip_list);
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
	.oob_first = true,
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
		.of_match_table = ingenic_nand_dt_match,
	},
};
module_platform_driver(ingenic_nand_driver);

MODULE_AUTHOR("Alex Smith <alex@alex-smith.me.uk>");
MODULE_AUTHOR("Harvey Hunt <harveyhuntnexus@gmail.com>");
MODULE_DESCRIPTION("Ingenic JZ47xx NAND driver");
MODULE_LICENSE("GPL v2");
