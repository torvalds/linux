/*
 * drivers/mtd/devices/goldfish_nand.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (C) 2012 Intel, Inc.
 * Copyright (C) 2013 Intel, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/io.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/vmalloc.h>
#include <linux/mtd/mtd.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/goldfish.h>
#include <asm/div64.h>

#include "goldfish_nand_reg.h"

struct goldfish_nand {
	/* lock protects access to the device registers */
	struct mutex            lock;
	unsigned char __iomem  *base;
	struct cmd_params       *cmd_params;
	size_t                  mtd_count;
	struct mtd_info         mtd[0];
};

static u32 goldfish_nand_cmd_with_params(struct mtd_info *mtd,
					 enum nand_cmd cmd, u64 addr, u32 len,
					 void *ptr, u32 *rv)
{
	u32 cmdp;
	struct goldfish_nand *nand = mtd->priv;
	struct cmd_params *cps = nand->cmd_params;
	unsigned char __iomem  *base = nand->base;

	if (cps == NULL)
		return -1;

	switch (cmd) {
	case NAND_CMD_ERASE:
		cmdp = NAND_CMD_ERASE_WITH_PARAMS;
		break;
	case NAND_CMD_READ:
		cmdp = NAND_CMD_READ_WITH_PARAMS;
		break;
	case NAND_CMD_WRITE:
		cmdp = NAND_CMD_WRITE_WITH_PARAMS;
		break;
	default:
		return -1;
	}
	cps->dev = mtd - nand->mtd;
	cps->addr_high = (u32)(addr >> 32);
	cps->addr_low = (u32)addr;
	cps->transfer_size = len;
	cps->data = (unsigned long)ptr;
	writel(cmdp, base + NAND_COMMAND);
	*rv = cps->result;
	return 0;
}

static u32 goldfish_nand_cmd(struct mtd_info *mtd, enum nand_cmd cmd,
			     u64 addr, u32 len, void *ptr)
{
	struct goldfish_nand *nand = mtd->priv;
	u32 rv;
	unsigned char __iomem  *base = nand->base;

	mutex_lock(&nand->lock);
	if (goldfish_nand_cmd_with_params(mtd, cmd, addr, len, ptr, &rv)) {
		writel(mtd - nand->mtd, base + NAND_DEV);
		writel((u32)(addr >> 32), base + NAND_ADDR_HIGH);
		writel((u32)addr, base + NAND_ADDR_LOW);
		writel(len, base + NAND_TRANSFER_SIZE);
		gf_write_ptr(ptr, base + NAND_DATA, base + NAND_DATA_HIGH);
		writel(cmd, base + NAND_COMMAND);
		rv = readl(base + NAND_RESULT);
	}
	mutex_unlock(&nand->lock);
	return rv;
}

static int goldfish_nand_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	loff_t ofs = instr->addr;
	u32 len = instr->len;
	u32 rem;

	if (ofs + len > mtd->size)
		goto invalid_arg;
	rem = do_div(ofs, mtd->writesize);
	if (rem)
		goto invalid_arg;
	ofs *= (mtd->writesize + mtd->oobsize);

	if (len % mtd->writesize)
		goto invalid_arg;
	len = len / mtd->writesize * (mtd->writesize + mtd->oobsize);

	if (goldfish_nand_cmd(mtd, NAND_CMD_ERASE, ofs, len, NULL) != len) {
		pr_err("goldfish_nand_erase: erase failed, start %llx, len %x, dev_size %llx, erase_size %x\n",
		       ofs, len, mtd->size, mtd->erasesize);
		return -EIO;
	}

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;

invalid_arg:
	pr_err("goldfish_nand_erase: invalid erase, start %llx, len %x, dev_size %llx, erase_size %x\n",
	       ofs, len, mtd->size, mtd->erasesize);
	return -EINVAL;
}

static int goldfish_nand_read_oob(struct mtd_info *mtd, loff_t ofs,
				  struct mtd_oob_ops *ops)
{
	u32 rem;

	if (ofs + ops->len > mtd->size)
		goto invalid_arg;
	if (ops->datbuf && ops->len && ops->len != mtd->writesize)
		goto invalid_arg;
	if (ops->ooblen + ops->ooboffs > mtd->oobsize)
		goto invalid_arg;

	rem = do_div(ofs, mtd->writesize);
	if (rem)
		goto invalid_arg;
	ofs *= (mtd->writesize + mtd->oobsize);

	if (ops->datbuf)
		ops->retlen = goldfish_nand_cmd(mtd, NAND_CMD_READ, ofs,
						ops->len, ops->datbuf);
	ofs += mtd->writesize + ops->ooboffs;
	if (ops->oobbuf)
		ops->oobretlen = goldfish_nand_cmd(mtd, NAND_CMD_READ, ofs,
						ops->ooblen, ops->oobbuf);
	return 0;

invalid_arg:
	pr_err("goldfish_nand_read_oob: invalid read, start %llx, len %zx, ooblen %zx, dev_size %llx, write_size %x\n",
	       ofs, ops->len, ops->ooblen, mtd->size, mtd->writesize);
	return -EINVAL;
}

static int goldfish_nand_write_oob(struct mtd_info *mtd, loff_t ofs,
				   struct mtd_oob_ops *ops)
{
	u32 rem;

	if (ofs + ops->len > mtd->size)
		goto invalid_arg;
	if (ops->len && ops->len != mtd->writesize)
		goto invalid_arg;
	if (ops->ooblen + ops->ooboffs > mtd->oobsize)
		goto invalid_arg;

	rem = do_div(ofs, mtd->writesize);
	if (rem)
		goto invalid_arg;
	ofs *= (mtd->writesize + mtd->oobsize);

	if (ops->datbuf)
		ops->retlen = goldfish_nand_cmd(mtd, NAND_CMD_WRITE, ofs,
						ops->len, ops->datbuf);
	ofs += mtd->writesize + ops->ooboffs;
	if (ops->oobbuf)
		ops->oobretlen = goldfish_nand_cmd(mtd, NAND_CMD_WRITE, ofs,
						ops->ooblen, ops->oobbuf);
	return 0;

invalid_arg:
	pr_err("goldfish_nand_write_oob: invalid write, start %llx, len %zx, ooblen %zx, dev_size %llx, write_size %x\n",
	       ofs, ops->len, ops->ooblen, mtd->size, mtd->writesize);
	return -EINVAL;
}

static int goldfish_nand_read(struct mtd_info *mtd, loff_t from, size_t len,
			      size_t *retlen, u_char *buf)
{
	u32 rem;

	if (from + len > mtd->size)
		goto invalid_arg;

	rem = do_div(from, mtd->writesize);
	if (rem)
		goto invalid_arg;
	from *= (mtd->writesize + mtd->oobsize);

	*retlen = goldfish_nand_cmd(mtd, NAND_CMD_READ, from, len, buf);
	return 0;

invalid_arg:
	pr_err("goldfish_nand_read: invalid read, start %llx, len %zx, dev_size %llx, write_size %x\n",
	       from, len, mtd->size, mtd->writesize);
	return -EINVAL;
}

static int goldfish_nand_write(struct mtd_info *mtd, loff_t to, size_t len,
			       size_t *retlen, const u_char *buf)
{
	u32 rem;

	if (to + len > mtd->size)
		goto invalid_arg;

	rem = do_div(to, mtd->writesize);
	if (rem)
		goto invalid_arg;
	to *= (mtd->writesize + mtd->oobsize);

	*retlen = goldfish_nand_cmd(mtd, NAND_CMD_WRITE, to, len, (void *)buf);
	return 0;

invalid_arg:
	pr_err("goldfish_nand_write: invalid write, start %llx, len %zx, dev_size %llx, write_size %x\n",
	       to, len, mtd->size, mtd->writesize);
	return -EINVAL;
}

static int goldfish_nand_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	u32 rem;

	if (ofs >= mtd->size)
		goto invalid_arg;

	rem = do_div(ofs, mtd->erasesize);
	if (rem)
		goto invalid_arg;
	ofs *= mtd->erasesize / mtd->writesize;
	ofs *= (mtd->writesize + mtd->oobsize);

	return goldfish_nand_cmd(mtd, NAND_CMD_BLOCK_BAD_GET, ofs, 0, NULL);

invalid_arg:
	pr_err("goldfish_nand_block_isbad: invalid arg, ofs %llx, dev_size %llx, write_size %x\n",
	       ofs, mtd->size, mtd->writesize);
	return -EINVAL;
}

static int goldfish_nand_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	u32 rem;

	if (ofs >= mtd->size)
		goto invalid_arg;

	rem = do_div(ofs, mtd->erasesize);
	if (rem)
		goto invalid_arg;
	ofs *= mtd->erasesize / mtd->writesize;
	ofs *= (mtd->writesize + mtd->oobsize);

	if (goldfish_nand_cmd(mtd, NAND_CMD_BLOCK_BAD_SET, ofs, 0, NULL) != 1)
		return -EIO;
	return 0;

invalid_arg:
	pr_err("goldfish_nand_block_markbad: invalid arg, ofs %llx, dev_size %llx, write_size %x\n",
	       ofs, mtd->size, mtd->writesize);
	return -EINVAL;
}

static int nand_setup_cmd_params(struct platform_device *pdev,
				 struct goldfish_nand *nand)
{
	u64 paddr;
	unsigned char __iomem  *base = nand->base;

	nand->cmd_params = devm_kzalloc(&pdev->dev,
					sizeof(struct cmd_params), GFP_KERNEL);
	if (!nand->cmd_params)
		return -1;

	paddr = __pa(nand->cmd_params);
	writel((u32)(paddr >> 32), base + NAND_CMD_PARAMS_ADDR_HIGH);
	writel((u32)paddr, base + NAND_CMD_PARAMS_ADDR_LOW);
	return 0;
}

static int goldfish_nand_init_device(struct platform_device *pdev,
				     struct goldfish_nand *nand, int id)
{
	u32 name_len;
	u32 result;
	u32 flags;
	unsigned char __iomem  *base = nand->base;
	struct mtd_info *mtd = &nand->mtd[id];
	char *name;

	mutex_lock(&nand->lock);
	writel(id, base + NAND_DEV);
	flags = readl(base + NAND_DEV_FLAGS);
	name_len = readl(base + NAND_DEV_NAME_LEN);
	mtd->writesize = readl(base + NAND_DEV_PAGE_SIZE);
	mtd->size = readl(base + NAND_DEV_SIZE_LOW);
	mtd->size |= (u64)readl(base + NAND_DEV_SIZE_HIGH) << 32;
	mtd->oobsize = readl(base + NAND_DEV_EXTRA_SIZE);
	mtd->oobavail = mtd->oobsize;
	mtd->erasesize = readl(base + NAND_DEV_ERASE_SIZE) /
			(mtd->writesize + mtd->oobsize) * mtd->writesize;
	do_div(mtd->size, mtd->writesize + mtd->oobsize);
	mtd->size *= mtd->writesize;
	dev_dbg(&pdev->dev,
		"goldfish nand dev%d: size %llx, page %d, extra %d, erase %d\n",
		       id, mtd->size, mtd->writesize,
		       mtd->oobsize, mtd->erasesize);
	mutex_unlock(&nand->lock);

	mtd->priv = nand;

	name = devm_kzalloc(&pdev->dev, name_len + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	mtd->name = name;

	result = goldfish_nand_cmd(mtd, NAND_CMD_GET_DEV_NAME, 0, name_len,
				   name);
	if (result != name_len) {
		dev_err(&pdev->dev,
			"goldfish_nand_init_device failed to get dev name %d != %d\n",
			       result, name_len);
		return -ENODEV;
	}
	((char *)mtd->name)[name_len] = '\0';

	/* Setup the MTD structure */
	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH;
	if (flags & NAND_DEV_FLAG_READ_ONLY)
		mtd->flags &= ~MTD_WRITEABLE;
	if (flags & NAND_DEV_FLAG_CMD_PARAMS_CAP)
		nand_setup_cmd_params(pdev, nand);

	mtd->owner = THIS_MODULE;
	mtd->_erase = goldfish_nand_erase;
	mtd->_read = goldfish_nand_read;
	mtd->_write = goldfish_nand_write;
	mtd->_read_oob = goldfish_nand_read_oob;
	mtd->_write_oob = goldfish_nand_write_oob;
	mtd->_block_isbad = goldfish_nand_block_isbad;
	mtd->_block_markbad = goldfish_nand_block_markbad;

	if (mtd_device_register(mtd, NULL, 0))
		return -EIO;

	return 0;
}

static int goldfish_nand_probe(struct platform_device *pdev)
{
	u32 num_dev;
	int i;
	int err;
	u32 num_dev_working;
	u32 version;
	struct resource *r;
	struct goldfish_nand *nand;
	unsigned char __iomem  *base;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL)
		return -ENODEV;

	base = devm_ioremap(&pdev->dev, r->start, PAGE_SIZE);
	if (!base)
		return -ENOMEM;

	version = readl(base + NAND_VERSION);
	if (version != NAND_VERSION_CURRENT) {
		dev_err(&pdev->dev,
			"goldfish_nand_init: version mismatch, got %d, expected %d\n",
				version, NAND_VERSION_CURRENT);
		return -ENODEV;
	}
	num_dev = readl(base + NAND_NUM_DEV);
	if (num_dev == 0)
		return -ENODEV;

	nand = devm_kzalloc(&pdev->dev, sizeof(*nand) +
				sizeof(struct mtd_info) * num_dev, GFP_KERNEL);
	if (!nand)
		return -ENOMEM;

	mutex_init(&nand->lock);
	nand->base = base;
	nand->mtd_count = num_dev;
	platform_set_drvdata(pdev, nand);

	num_dev_working = 0;
	for (i = 0; i < num_dev; i++) {
		err = goldfish_nand_init_device(pdev, nand, i);
		if (err == 0)
			num_dev_working++;
	}
	if (num_dev_working == 0)
		return -ENODEV;
	return 0;
}

static int goldfish_nand_remove(struct platform_device *pdev)
{
	struct goldfish_nand *nand = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < nand->mtd_count; i++) {
		if (nand->mtd[i].name)
			mtd_device_unregister(&nand->mtd[i]);
	}
	return 0;
}

static struct platform_driver goldfish_nand_driver = {
	.probe		= goldfish_nand_probe,
	.remove		= goldfish_nand_remove,
	.driver = {
		.name = "goldfish_nand"
	}
};

module_platform_driver(goldfish_nand_driver);
MODULE_LICENSE("GPL");
