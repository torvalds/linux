// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright © 2012 John Crispin <john@phrozen.org>
 *  Copyright © 2016 Hauke Mehrtens <hauke@hauke-m.de>
 */

#include <linux/mtd/rawnand.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>

#include <lantiq_soc.h>

/* nand registers */
#define EBU_ADDSEL1		0x24
#define EBU_NAND_CON		0xB0
#define EBU_NAND_WAIT		0xB4
#define  NAND_WAIT_RD		BIT(0) /* NAND flash status output */
#define  NAND_WAIT_WR_C		BIT(3) /* NAND Write/Read complete */
#define EBU_NAND_ECC0		0xB8
#define EBU_NAND_ECC_AC		0xBC

/*
 * nand commands
 * The pins of the NAND chip are selected based on the address bits of the
 * "register" read and write. There are no special registers, but an
 * address range and the lower address bits are used to activate the
 * correct line. For example when the bit (1 << 2) is set in the address
 * the ALE pin will be activated.
 */
#define NAND_CMD_ALE		BIT(2) /* address latch enable */
#define NAND_CMD_CLE		BIT(3) /* command latch enable */
#define NAND_CMD_CS		BIT(4) /* chip select */
#define NAND_CMD_SE		BIT(5) /* spare area access latch */
#define NAND_CMD_WP		BIT(6) /* write protect */
#define NAND_WRITE_CMD		(NAND_CMD_CS | NAND_CMD_CLE)
#define NAND_WRITE_ADDR		(NAND_CMD_CS | NAND_CMD_ALE)
#define NAND_WRITE_DATA		(NAND_CMD_CS)
#define NAND_READ_DATA		(NAND_CMD_CS)

/* we need to tel the ebu which addr we mapped the nand to */
#define ADDSEL1_MASK(x)		(x << 4)
#define ADDSEL1_REGEN		1

/* we need to tell the EBU that we have nand attached and set it up properly */
#define BUSCON1_SETUP		(1 << 22)
#define BUSCON1_BCGEN_RES	(0x3 << 12)
#define BUSCON1_WAITWRC2	(2 << 8)
#define BUSCON1_WAITRDC2	(2 << 6)
#define BUSCON1_HOLDC1		(1 << 4)
#define BUSCON1_RECOVC1		(1 << 2)
#define BUSCON1_CMULT4		1

#define NAND_CON_CE		(1 << 20)
#define NAND_CON_OUT_CS1	(1 << 10)
#define NAND_CON_IN_CS1		(1 << 8)
#define NAND_CON_PRE_P		(1 << 7)
#define NAND_CON_WP_P		(1 << 6)
#define NAND_CON_SE_P		(1 << 5)
#define NAND_CON_CS_P		(1 << 4)
#define NAND_CON_CSMUX		(1 << 1)
#define NAND_CON_NANDM		1

struct xway_nand_data {
	struct nand_chip	chip;
	unsigned long		csflags;
	void __iomem		*nandaddr;
};

static u8 xway_readb(struct mtd_info *mtd, int op)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct xway_nand_data *data = nand_get_controller_data(chip);

	return readb(data->nandaddr + op);
}

static void xway_writeb(struct mtd_info *mtd, int op, u8 value)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct xway_nand_data *data = nand_get_controller_data(chip);

	writeb(value, data->nandaddr + op);
}

static void xway_select_chip(struct nand_chip *chip, int select)
{
	struct xway_nand_data *data = nand_get_controller_data(chip);

	switch (select) {
	case -1:
		ltq_ebu_w32_mask(NAND_CON_CE, 0, EBU_NAND_CON);
		ltq_ebu_w32_mask(NAND_CON_NANDM, 0, EBU_NAND_CON);
		spin_unlock_irqrestore(&ebu_lock, data->csflags);
		break;
	case 0:
		spin_lock_irqsave(&ebu_lock, data->csflags);
		ltq_ebu_w32_mask(0, NAND_CON_NANDM, EBU_NAND_CON);
		ltq_ebu_w32_mask(0, NAND_CON_CE, EBU_NAND_CON);
		break;
	default:
		BUG();
	}
}

static void xway_cmd_ctrl(struct nand_chip *chip, int cmd, unsigned int ctrl)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		xway_writeb(mtd, NAND_WRITE_CMD, cmd);
	else if (ctrl & NAND_ALE)
		xway_writeb(mtd, NAND_WRITE_ADDR, cmd);

	while ((ltq_ebu_r32(EBU_NAND_WAIT) & NAND_WAIT_WR_C) == 0)
		;
}

static int xway_dev_ready(struct nand_chip *chip)
{
	return ltq_ebu_r32(EBU_NAND_WAIT) & NAND_WAIT_RD;
}

static unsigned char xway_read_byte(struct nand_chip *chip)
{
	return xway_readb(nand_to_mtd(chip), NAND_READ_DATA);
}

static void xway_read_buf(struct nand_chip *chip, u_char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		buf[i] = xway_readb(nand_to_mtd(chip), NAND_WRITE_DATA);
}

static void xway_write_buf(struct nand_chip *chip, const u_char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		xway_writeb(nand_to_mtd(chip), NAND_WRITE_DATA, buf[i]);
}

/*
 * Probe for the NAND device.
 */
static int xway_nand_probe(struct platform_device *pdev)
{
	struct xway_nand_data *data;
	struct mtd_info *mtd;
	struct resource *res;
	int err;
	u32 cs;
	u32 cs_flag = 0;

	/* Allocate memory for the device structure (and zero it) */
	data = devm_kzalloc(&pdev->dev, sizeof(struct xway_nand_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->nandaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->nandaddr))
		return PTR_ERR(data->nandaddr);

	nand_set_flash_node(&data->chip, pdev->dev.of_node);
	mtd = nand_to_mtd(&data->chip);
	mtd->dev.parent = &pdev->dev;

	data->chip.legacy.cmd_ctrl = xway_cmd_ctrl;
	data->chip.legacy.dev_ready = xway_dev_ready;
	data->chip.legacy.select_chip = xway_select_chip;
	data->chip.legacy.write_buf = xway_write_buf;
	data->chip.legacy.read_buf = xway_read_buf;
	data->chip.legacy.read_byte = xway_read_byte;
	data->chip.legacy.chip_delay = 30;

	data->chip.ecc.mode = NAND_ECC_SOFT;
	data->chip.ecc.algo = NAND_ECC_HAMMING;

	platform_set_drvdata(pdev, data);
	nand_set_controller_data(&data->chip, data);

	/* load our CS from the DT. Either we find a valid 1 or default to 0 */
	err = of_property_read_u32(pdev->dev.of_node, "lantiq,cs", &cs);
	if (!err && cs == 1)
		cs_flag = NAND_CON_IN_CS1 | NAND_CON_OUT_CS1;

	/* setup the EBU to run in NAND mode on our base addr */
	ltq_ebu_w32(CPHYSADDR(data->nandaddr)
		    | ADDSEL1_MASK(3) | ADDSEL1_REGEN, EBU_ADDSEL1);

	ltq_ebu_w32(BUSCON1_SETUP | BUSCON1_BCGEN_RES | BUSCON1_WAITWRC2
		    | BUSCON1_WAITRDC2 | BUSCON1_HOLDC1 | BUSCON1_RECOVC1
		    | BUSCON1_CMULT4, LTQ_EBU_BUSCON1);

	ltq_ebu_w32(NAND_CON_NANDM | NAND_CON_CSMUX | NAND_CON_CS_P
		    | NAND_CON_SE_P | NAND_CON_WP_P | NAND_CON_PRE_P
		    | cs_flag, EBU_NAND_CON);

	/* Scan to find existence of the device */
	err = nand_scan(&data->chip, 1);
	if (err)
		return err;

	err = mtd_device_register(mtd, NULL, 0);
	if (err)
		nand_cleanup(&data->chip);

	return err;
}

/*
 * Remove a NAND device.
 */
static int xway_nand_remove(struct platform_device *pdev)
{
	struct xway_nand_data *data = platform_get_drvdata(pdev);
	struct nand_chip *chip = &data->chip;
	int ret;

	ret = mtd_device_unregister(mtd);
	WARN_ON(ret);
	nand_cleanup(chip);

	return 0;
}

static const struct of_device_id xway_nand_match[] = {
	{ .compatible = "lantiq,nand-xway" },
	{},
};

static struct platform_driver xway_nand_driver = {
	.probe	= xway_nand_probe,
	.remove	= xway_nand_remove,
	.driver	= {
		.name		= "lantiq,nand-xway",
		.of_match_table = xway_nand_match,
	},
};

builtin_platform_driver(xway_nand_driver);
