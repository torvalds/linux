/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SoC NAND controller driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <linux/gpio.h>

#include <asm/mach-jz4740/jz4740_nand.h>

#define JZ_REG_NAND_CTRL	0x50
#define JZ_REG_NAND_ECC_CTRL	0x100
#define JZ_REG_NAND_DATA	0x104
#define JZ_REG_NAND_PAR0	0x108
#define JZ_REG_NAND_PAR1	0x10C
#define JZ_REG_NAND_PAR2	0x110
#define JZ_REG_NAND_IRQ_STAT	0x114
#define JZ_REG_NAND_IRQ_CTRL	0x118
#define JZ_REG_NAND_ERR(x)	(0x11C + ((x) << 2))

#define JZ_NAND_ECC_CTRL_PAR_READY	BIT(4)
#define JZ_NAND_ECC_CTRL_ENCODING	BIT(3)
#define JZ_NAND_ECC_CTRL_RS		BIT(2)
#define JZ_NAND_ECC_CTRL_RESET		BIT(1)
#define JZ_NAND_ECC_CTRL_ENABLE		BIT(0)

#define JZ_NAND_STATUS_ERR_COUNT	(BIT(31) | BIT(30) | BIT(29))
#define JZ_NAND_STATUS_PAD_FINISH	BIT(4)
#define JZ_NAND_STATUS_DEC_FINISH	BIT(3)
#define JZ_NAND_STATUS_ENC_FINISH	BIT(2)
#define JZ_NAND_STATUS_UNCOR_ERROR	BIT(1)
#define JZ_NAND_STATUS_ERROR		BIT(0)

#define JZ_NAND_CTRL_ENABLE_CHIP(x) BIT((x) << 1)
#define JZ_NAND_CTRL_ASSERT_CHIP(x) BIT(((x) << 1) + 1)
#define JZ_NAND_CTRL_ASSERT_CHIP_MASK 0xaa

#define JZ_NAND_MEM_CMD_OFFSET 0x08000
#define JZ_NAND_MEM_ADDR_OFFSET 0x10000

struct jz_nand {
	struct nand_chip chip;
	void __iomem *base;
	struct resource *mem;

	unsigned char banks[JZ_NAND_NUM_BANKS];
	void __iomem *bank_base[JZ_NAND_NUM_BANKS];
	struct resource *bank_mem[JZ_NAND_NUM_BANKS];

	int selected_bank;

	struct gpio_desc *busy_gpio;
	bool is_reading;
};

static inline struct jz_nand *mtd_to_jz_nand(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct jz_nand, chip);
}

static void jz_nand_select_chip(struct mtd_info *mtd, int chipnr)
{
	struct jz_nand *nand = mtd_to_jz_nand(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);
	uint32_t ctrl;
	int banknr;

	ctrl = readl(nand->base + JZ_REG_NAND_CTRL);
	ctrl &= ~JZ_NAND_CTRL_ASSERT_CHIP_MASK;

	if (chipnr == -1) {
		banknr = -1;
	} else {
		banknr = nand->banks[chipnr] - 1;
		chip->IO_ADDR_R = nand->bank_base[banknr];
		chip->IO_ADDR_W = nand->bank_base[banknr];
	}
	writel(ctrl, nand->base + JZ_REG_NAND_CTRL);

	nand->selected_bank = banknr;
}

static void jz_nand_cmd_ctrl(struct mtd_info *mtd, int dat, unsigned int ctrl)
{
	struct jz_nand *nand = mtd_to_jz_nand(mtd);
	struct nand_chip *chip = mtd_to_nand(mtd);
	uint32_t reg;
	void __iomem *bank_base = nand->bank_base[nand->selected_bank];

	BUG_ON(nand->selected_bank < 0);

	if (ctrl & NAND_CTRL_CHANGE) {
		BUG_ON((ctrl & NAND_ALE) && (ctrl & NAND_CLE));
		if (ctrl & NAND_ALE)
			bank_base += JZ_NAND_MEM_ADDR_OFFSET;
		else if (ctrl & NAND_CLE)
			bank_base += JZ_NAND_MEM_CMD_OFFSET;
		chip->IO_ADDR_W = bank_base;

		reg = readl(nand->base + JZ_REG_NAND_CTRL);
		if (ctrl & NAND_NCE)
			reg |= JZ_NAND_CTRL_ASSERT_CHIP(nand->selected_bank);
		else
			reg &= ~JZ_NAND_CTRL_ASSERT_CHIP(nand->selected_bank);
		writel(reg, nand->base + JZ_REG_NAND_CTRL);
	}
	if (dat != NAND_CMD_NONE)
		writeb(dat, chip->IO_ADDR_W);
}

static int jz_nand_dev_ready(struct mtd_info *mtd)
{
	struct jz_nand *nand = mtd_to_jz_nand(mtd);
	return gpiod_get_value_cansleep(nand->busy_gpio);
}

static void jz_nand_hwctl(struct mtd_info *mtd, int mode)
{
	struct jz_nand *nand = mtd_to_jz_nand(mtd);
	uint32_t reg;

	writel(0, nand->base + JZ_REG_NAND_IRQ_STAT);
	reg = readl(nand->base + JZ_REG_NAND_ECC_CTRL);

	reg |= JZ_NAND_ECC_CTRL_RESET;
	reg |= JZ_NAND_ECC_CTRL_ENABLE;
	reg |= JZ_NAND_ECC_CTRL_RS;

	switch (mode) {
	case NAND_ECC_READ:
		reg &= ~JZ_NAND_ECC_CTRL_ENCODING;
		nand->is_reading = true;
		break;
	case NAND_ECC_WRITE:
		reg |= JZ_NAND_ECC_CTRL_ENCODING;
		nand->is_reading = false;
		break;
	default:
		break;
	}

	writel(reg, nand->base + JZ_REG_NAND_ECC_CTRL);
}

static int jz_nand_calculate_ecc_rs(struct mtd_info *mtd, const uint8_t *dat,
	uint8_t *ecc_code)
{
	struct jz_nand *nand = mtd_to_jz_nand(mtd);
	uint32_t reg, status;
	int i;
	unsigned int timeout = 1000;
	static uint8_t empty_block_ecc[] = {0xcd, 0x9d, 0x90, 0x58, 0xf4,
						0x8b, 0xff, 0xb7, 0x6f};

	if (nand->is_reading)
		return 0;

	do {
		status = readl(nand->base + JZ_REG_NAND_IRQ_STAT);
	} while (!(status & JZ_NAND_STATUS_ENC_FINISH) && --timeout);

	if (timeout == 0)
	    return -1;

	reg = readl(nand->base + JZ_REG_NAND_ECC_CTRL);
	reg &= ~JZ_NAND_ECC_CTRL_ENABLE;
	writel(reg, nand->base + JZ_REG_NAND_ECC_CTRL);

	for (i = 0; i < 9; ++i)
		ecc_code[i] = readb(nand->base + JZ_REG_NAND_PAR0 + i);

	/* If the written data is completly 0xff, we also want to write 0xff as
	 * ecc, otherwise we will get in trouble when doing subpage writes. */
	if (memcmp(ecc_code, empty_block_ecc, 9) == 0)
		memset(ecc_code, 0xff, 9);

	return 0;
}

static void jz_nand_correct_data(uint8_t *dat, int index, int mask)
{
	int offset = index & 0x7;
	uint16_t data;

	index += (index >> 3);

	data = dat[index];
	data |= dat[index+1] << 8;

	mask ^= (data >> offset) & 0x1ff;
	data &= ~(0x1ff << offset);
	data |= (mask << offset);

	dat[index] = data & 0xff;
	dat[index+1] = (data >> 8) & 0xff;
}

static int jz_nand_correct_ecc_rs(struct mtd_info *mtd, uint8_t *dat,
	uint8_t *read_ecc, uint8_t *calc_ecc)
{
	struct jz_nand *nand = mtd_to_jz_nand(mtd);
	int i, error_count, index;
	uint32_t reg, status, error;
	unsigned int timeout = 1000;

	for (i = 0; i < 9; ++i)
		writeb(read_ecc[i], nand->base + JZ_REG_NAND_PAR0 + i);

	reg = readl(nand->base + JZ_REG_NAND_ECC_CTRL);
	reg |= JZ_NAND_ECC_CTRL_PAR_READY;
	writel(reg, nand->base + JZ_REG_NAND_ECC_CTRL);

	do {
		status = readl(nand->base + JZ_REG_NAND_IRQ_STAT);
	} while (!(status & JZ_NAND_STATUS_DEC_FINISH) && --timeout);

	if (timeout == 0)
		return -ETIMEDOUT;

	reg = readl(nand->base + JZ_REG_NAND_ECC_CTRL);
	reg &= ~JZ_NAND_ECC_CTRL_ENABLE;
	writel(reg, nand->base + JZ_REG_NAND_ECC_CTRL);

	if (status & JZ_NAND_STATUS_ERROR) {
		if (status & JZ_NAND_STATUS_UNCOR_ERROR)
			return -EBADMSG;

		error_count = (status & JZ_NAND_STATUS_ERR_COUNT) >> 29;

		for (i = 0; i < error_count; ++i) {
			error = readl(nand->base + JZ_REG_NAND_ERR(i));
			index = ((error >> 16) & 0x1ff) - 1;
			if (index >= 0 && index < 512)
				jz_nand_correct_data(dat, index, error & 0x1ff);
		}

		return error_count;
	}

	return 0;
}

static int jz_nand_ioremap_resource(struct platform_device *pdev,
	const char *name, struct resource **res, void *__iomem *base)
{
	int ret;

	*res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	if (!*res) {
		dev_err(&pdev->dev, "Failed to get platform %s memory\n", name);
		ret = -ENXIO;
		goto err;
	}

	*res = request_mem_region((*res)->start, resource_size(*res),
				pdev->name);
	if (!*res) {
		dev_err(&pdev->dev, "Failed to request %s memory region\n", name);
		ret = -EBUSY;
		goto err;
	}

	*base = ioremap((*res)->start, resource_size(*res));
	if (!*base) {
		dev_err(&pdev->dev, "Failed to ioremap %s memory region\n", name);
		ret = -EBUSY;
		goto err_release_mem;
	}

	return 0;

err_release_mem:
	release_mem_region((*res)->start, resource_size(*res));
err:
	*res = NULL;
	*base = NULL;
	return ret;
}

static inline void jz_nand_iounmap_resource(struct resource *res,
					    void __iomem *base)
{
	iounmap(base);
	release_mem_region(res->start, resource_size(res));
}

static int jz_nand_detect_bank(struct platform_device *pdev,
			       struct jz_nand *nand, unsigned char bank,
			       size_t chipnr, uint8_t *nand_maf_id,
			       uint8_t *nand_dev_id)
{
	int ret;
	char res_name[6];
	uint32_t ctrl;
	struct nand_chip *chip = &nand->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);

	/* Request I/O resource. */
	sprintf(res_name, "bank%d", bank);
	ret = jz_nand_ioremap_resource(pdev, res_name,
					&nand->bank_mem[bank - 1],
					&nand->bank_base[bank - 1]);
	if (ret)
		return ret;

	/* Enable chip in bank. */
	ctrl = readl(nand->base + JZ_REG_NAND_CTRL);
	ctrl |= JZ_NAND_CTRL_ENABLE_CHIP(bank - 1);
	writel(ctrl, nand->base + JZ_REG_NAND_CTRL);

	if (chipnr == 0) {
		/* Detect first chip. */
		ret = nand_scan_ident(mtd, 1, NULL);
		if (ret)
			goto notfound_id;

		/* Retrieve the IDs from the first chip. */
		chip->select_chip(mtd, 0);
		chip->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);
		chip->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);
		*nand_maf_id = chip->read_byte(mtd);
		*nand_dev_id = chip->read_byte(mtd);
	} else {
		/* Detect additional chip. */
		chip->select_chip(mtd, chipnr);
		chip->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);
		chip->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);
		if (*nand_maf_id != chip->read_byte(mtd)
		 || *nand_dev_id != chip->read_byte(mtd)) {
			ret = -ENODEV;
			goto notfound_id;
		}

		/* Update size of the MTD. */
		chip->numchips++;
		mtd->size += chip->chipsize;
	}

	dev_info(&pdev->dev, "Found chip %i on bank %i\n", chipnr, bank);
	return 0;

notfound_id:
	dev_info(&pdev->dev, "No chip found on bank %i\n", bank);
	ctrl &= ~(JZ_NAND_CTRL_ENABLE_CHIP(bank - 1));
	writel(ctrl, nand->base + JZ_REG_NAND_CTRL);
	jz_nand_iounmap_resource(nand->bank_mem[bank - 1],
				 nand->bank_base[bank - 1]);
	return ret;
}

static int jz_nand_probe(struct platform_device *pdev)
{
	int ret;
	struct jz_nand *nand;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	struct jz_nand_platform_data *pdata = dev_get_platdata(&pdev->dev);
	size_t chipnr, bank_idx;
	uint8_t nand_maf_id = 0, nand_dev_id = 0;

	nand = kzalloc(sizeof(*nand), GFP_KERNEL);
	if (!nand)
		return -ENOMEM;

	ret = jz_nand_ioremap_resource(pdev, "mmio", &nand->mem, &nand->base);
	if (ret)
		goto err_free;

	nand->busy_gpio = devm_gpiod_get_optional(&pdev->dev, "busy", GPIOD_IN);
	if (IS_ERR(nand->busy_gpio)) {
		ret = PTR_ERR(nand->busy_gpio);
		dev_err(&pdev->dev, "Failed to request busy gpio %d\n",
		    ret);
		goto err_iounmap_mmio;
	}

	chip		= &nand->chip;
	mtd		= nand_to_mtd(chip);
	mtd->dev.parent = &pdev->dev;
	mtd->name	= "jz4740-nand";

	chip->ecc.hwctl		= jz_nand_hwctl;
	chip->ecc.calculate	= jz_nand_calculate_ecc_rs;
	chip->ecc.correct	= jz_nand_correct_ecc_rs;
	chip->ecc.mode		= NAND_ECC_HW_OOB_FIRST;
	chip->ecc.size		= 512;
	chip->ecc.bytes		= 9;
	chip->ecc.strength	= 4;
	chip->ecc.options	= NAND_ECC_GENERIC_ERASED_CHECK;

	chip->chip_delay = 50;
	chip->cmd_ctrl = jz_nand_cmd_ctrl;
	chip->select_chip = jz_nand_select_chip;

	if (nand->busy_gpio)
		chip->dev_ready = jz_nand_dev_ready;

	platform_set_drvdata(pdev, nand);

	/* We are going to autodetect NAND chips in the banks specified in the
	 * platform data. Although nand_scan_ident() can detect multiple chips,
	 * it requires those chips to be numbered consecuitively, which is not
	 * always the case for external memory banks. And a fixed chip-to-bank
	 * mapping is not practical either, since for example Dingoo units
	 * produced at different times have NAND chips in different banks.
	 */
	chipnr = 0;
	for (bank_idx = 0; bank_idx < JZ_NAND_NUM_BANKS; bank_idx++) {
		unsigned char bank;

		/* If there is no platform data, look for NAND in bank 1,
		 * which is the most likely bank since it is the only one
		 * that can be booted from.
		 */
		bank = pdata ? pdata->banks[bank_idx] : bank_idx ^ 1;
		if (bank == 0)
			break;
		if (bank > JZ_NAND_NUM_BANKS) {
			dev_warn(&pdev->dev,
				"Skipping non-existing bank: %d\n", bank);
			continue;
		}
		/* The detection routine will directly or indirectly call
		 * jz_nand_select_chip(), so nand->banks has to contain the
		 * bank we're checking.
		 */
		nand->banks[chipnr] = bank;
		if (jz_nand_detect_bank(pdev, nand, bank, chipnr,
					&nand_maf_id, &nand_dev_id) == 0)
			chipnr++;
		else
			nand->banks[chipnr] = 0;
	}
	if (chipnr == 0) {
		dev_err(&pdev->dev, "No NAND chips found\n");
		goto err_iounmap_mmio;
	}

	if (pdata && pdata->ident_callback) {
		pdata->ident_callback(pdev, mtd, &pdata->partitions,
					&pdata->num_partitions);
	}

	ret = nand_scan_tail(mtd);
	if (ret) {
		dev_err(&pdev->dev,  "Failed to scan NAND\n");
		goto err_unclaim_banks;
	}

	ret = mtd_device_parse_register(mtd, NULL, NULL,
					pdata ? pdata->partitions : NULL,
					pdata ? pdata->num_partitions : 0);

	if (ret) {
		dev_err(&pdev->dev, "Failed to add mtd device\n");
		goto err_nand_release;
	}

	dev_info(&pdev->dev, "Successfully registered JZ4740 NAND driver\n");

	return 0;

err_nand_release:
	nand_release(mtd);
err_unclaim_banks:
	while (chipnr--) {
		unsigned char bank = nand->banks[chipnr];
		jz_nand_iounmap_resource(nand->bank_mem[bank - 1],
					 nand->bank_base[bank - 1]);
	}
	writel(0, nand->base + JZ_REG_NAND_CTRL);
err_iounmap_mmio:
	jz_nand_iounmap_resource(nand->mem, nand->base);
err_free:
	kfree(nand);
	return ret;
}

static int jz_nand_remove(struct platform_device *pdev)
{
	struct jz_nand *nand = platform_get_drvdata(pdev);
	size_t i;

	nand_release(nand_to_mtd(&nand->chip));

	/* Deassert and disable all chips */
	writel(0, nand->base + JZ_REG_NAND_CTRL);

	for (i = 0; i < JZ_NAND_NUM_BANKS; ++i) {
		unsigned char bank = nand->banks[i];
		if (bank != 0) {
			jz_nand_iounmap_resource(nand->bank_mem[bank - 1],
						 nand->bank_base[bank - 1]);
		}
	}

	jz_nand_iounmap_resource(nand->mem, nand->base);

	kfree(nand);

	return 0;
}

static struct platform_driver jz_nand_driver = {
	.probe = jz_nand_probe,
	.remove = jz_nand_remove,
	.driver = {
		.name = "jz4740-nand",
	},
};

module_platform_driver(jz_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("NAND controller driver for JZ4740 SoC");
MODULE_ALIAS("platform:jz4740-nand");
