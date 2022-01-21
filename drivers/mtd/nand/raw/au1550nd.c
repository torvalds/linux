// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2004 Embedded Edge, LLC
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1550nd.h>


struct au1550nd_ctx {
	struct nand_controller controller;
	struct nand_chip chip;

	int cs;
	void __iomem *base;
};

static struct au1550nd_ctx *chip_to_au_ctx(struct nand_chip *this)
{
	return container_of(this, struct au1550nd_ctx, chip);
}

/**
 * au_write_buf -  write buffer to chip
 * @this:	NAND chip object
 * @buf:	data buffer
 * @len:	number of bytes to write
 *
 * write function for 8bit buswidth
 */
static void au_write_buf(struct nand_chip *this, const void *buf,
			 unsigned int len)
{
	struct au1550nd_ctx *ctx = chip_to_au_ctx(this);
	const u8 *p = buf;
	int i;

	for (i = 0; i < len; i++) {
		writeb(p[i], ctx->base + MEM_STNAND_DATA);
		wmb(); /* drain writebuffer */
	}
}

/**
 * au_read_buf -  read chip data into buffer
 * @this:	NAND chip object
 * @buf:	buffer to store date
 * @len:	number of bytes to read
 *
 * read function for 8bit buswidth
 */
static void au_read_buf(struct nand_chip *this, void *buf,
			unsigned int len)
{
	struct au1550nd_ctx *ctx = chip_to_au_ctx(this);
	u8 *p = buf;
	int i;

	for (i = 0; i < len; i++) {
		p[i] = readb(ctx->base + MEM_STNAND_DATA);
		wmb(); /* drain writebuffer */
	}
}

/**
 * au_write_buf16 -  write buffer to chip
 * @this:	NAND chip object
 * @buf:	data buffer
 * @len:	number of bytes to write
 *
 * write function for 16bit buswidth
 */
static void au_write_buf16(struct nand_chip *this, const void *buf,
			   unsigned int len)
{
	struct au1550nd_ctx *ctx = chip_to_au_ctx(this);
	const u16 *p = buf;
	unsigned int i;

	len >>= 1;
	for (i = 0; i < len; i++) {
		writew(p[i], ctx->base + MEM_STNAND_DATA);
		wmb(); /* drain writebuffer */
	}
}

/**
 * au_read_buf16 -  read chip data into buffer
 * @this:	NAND chip object
 * @buf:	buffer to store date
 * @len:	number of bytes to read
 *
 * read function for 16bit buswidth
 */
static void au_read_buf16(struct nand_chip *this, void *buf, unsigned int len)
{
	struct au1550nd_ctx *ctx = chip_to_au_ctx(this);
	unsigned int i;
	u16 *p = buf;

	len >>= 1;
	for (i = 0; i < len; i++) {
		p[i] = readw(ctx->base + MEM_STNAND_DATA);
		wmb(); /* drain writebuffer */
	}
}

static int find_nand_cs(unsigned long nand_base)
{
	void __iomem *base =
			(void __iomem *)KSEG1ADDR(AU1000_STATIC_MEM_PHYS_ADDR);
	unsigned long addr, staddr, start, mask, end;
	int i;

	for (i = 0; i < 4; i++) {
		addr = 0x1000 + (i * 0x10);			/* CSx */
		staddr = __raw_readl(base + addr + 0x08);	/* STADDRx */
		/* figure out the decoded range of this CS */
		start = (staddr << 4) & 0xfffc0000;
		mask = (staddr << 18) & 0xfffc0000;
		end = (start | (start - 1)) & ~(start ^ mask);
		if ((nand_base >= start) && (nand_base < end))
			return i;
	}

	return -ENODEV;
}

static int au1550nd_waitrdy(struct nand_chip *this, unsigned int timeout_ms)
{
	unsigned long timeout_jiffies = jiffies;

	timeout_jiffies += msecs_to_jiffies(timeout_ms) + 1;
	do {
		if (alchemy_rdsmem(AU1000_MEM_STSTAT) & 0x1)
			return 0;

		usleep_range(10, 100);
	} while (time_before(jiffies, timeout_jiffies));

	return -ETIMEDOUT;
}

static int au1550nd_exec_instr(struct nand_chip *this,
			       const struct nand_op_instr *instr)
{
	struct au1550nd_ctx *ctx = chip_to_au_ctx(this);
	unsigned int i;
	int ret = 0;

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		writeb(instr->ctx.cmd.opcode,
		       ctx->base + MEM_STNAND_CMD);
		/* Drain the writebuffer */
		wmb();
		break;

	case NAND_OP_ADDR_INSTR:
		for (i = 0; i < instr->ctx.addr.naddrs; i++) {
			writeb(instr->ctx.addr.addrs[i],
			       ctx->base + MEM_STNAND_ADDR);
			/* Drain the writebuffer */
			wmb();
		}
		break;

	case NAND_OP_DATA_IN_INSTR:
		if ((this->options & NAND_BUSWIDTH_16) &&
		    !instr->ctx.data.force_8bit)
			au_read_buf16(this, instr->ctx.data.buf.in,
				      instr->ctx.data.len);
		else
			au_read_buf(this, instr->ctx.data.buf.in,
				    instr->ctx.data.len);
		break;

	case NAND_OP_DATA_OUT_INSTR:
		if ((this->options & NAND_BUSWIDTH_16) &&
		    !instr->ctx.data.force_8bit)
			au_write_buf16(this, instr->ctx.data.buf.out,
				       instr->ctx.data.len);
		else
			au_write_buf(this, instr->ctx.data.buf.out,
				     instr->ctx.data.len);
		break;

	case NAND_OP_WAITRDY_INSTR:
		ret = au1550nd_waitrdy(this, instr->ctx.waitrdy.timeout_ms);
		break;
	default:
		return -EINVAL;
	}

	if (instr->delay_ns)
		ndelay(instr->delay_ns);

	return ret;
}

static int au1550nd_exec_op(struct nand_chip *this,
			    const struct nand_operation *op,
			    bool check_only)
{
	struct au1550nd_ctx *ctx = chip_to_au_ctx(this);
	unsigned int i;
	int ret;

	if (check_only)
		return 0;

	/* assert (force assert) chip enable */
	alchemy_wrsmem((1 << (4 + ctx->cs)), AU1000_MEM_STNDCTL);
	/* Drain the writebuffer */
	wmb();

	for (i = 0; i < op->ninstrs; i++) {
		ret = au1550nd_exec_instr(this, &op->instrs[i]);
		if (ret)
			break;
	}

	/* deassert chip enable */
	alchemy_wrsmem(0, AU1000_MEM_STNDCTL);
	/* Drain the writebuffer */
	wmb();

	return ret;
}

static int au1550nd_attach_chip(struct nand_chip *chip)
{
	if (chip->ecc.engine_type == NAND_ECC_ENGINE_TYPE_SOFT &&
	    chip->ecc.algo == NAND_ECC_ALGO_UNKNOWN)
		chip->ecc.algo = NAND_ECC_ALGO_HAMMING;

	return 0;
}

static const struct nand_controller_ops au1550nd_ops = {
	.exec_op = au1550nd_exec_op,
	.attach_chip = au1550nd_attach_chip,
};

static int au1550nd_probe(struct platform_device *pdev)
{
	struct au1550nd_platdata *pd;
	struct au1550nd_ctx *ctx;
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct resource *r;
	int ret, cs;

	pd = dev_get_platdata(&pdev->dev);
	if (!pd) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -ENODEV;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no NAND memory resource\n");
		ret = -ENODEV;
		goto out1;
	}
	if (request_mem_region(r->start, resource_size(r), "au1550-nand")) {
		dev_err(&pdev->dev, "cannot claim NAND memory area\n");
		ret = -ENOMEM;
		goto out1;
	}

	ctx->base = ioremap(r->start, 0x1000);
	if (!ctx->base) {
		dev_err(&pdev->dev, "cannot remap NAND memory area\n");
		ret = -ENODEV;
		goto out2;
	}

	this = &ctx->chip;
	mtd = nand_to_mtd(this);
	mtd->dev.parent = &pdev->dev;

	/* figure out which CS# r->start belongs to */
	cs = find_nand_cs(r->start);
	if (cs < 0) {
		dev_err(&pdev->dev, "cannot detect NAND chipselect\n");
		ret = -ENODEV;
		goto out3;
	}
	ctx->cs = cs;

	nand_controller_init(&ctx->controller);
	ctx->controller.ops = &au1550nd_ops;
	this->controller = &ctx->controller;

	if (pd->devwidth)
		this->options |= NAND_BUSWIDTH_16;

	/*
	 * This driver assumes that the default ECC engine should be TYPE_SOFT.
	 * Set ->engine_type before registering the NAND devices in order to
	 * provide a driver specific default value.
	 */
	this->ecc.engine_type = NAND_ECC_ENGINE_TYPE_SOFT;

	ret = nand_scan(this, 1);
	if (ret) {
		dev_err(&pdev->dev, "NAND scan failed with %d\n", ret);
		goto out3;
	}

	mtd_device_register(mtd, pd->parts, pd->num_parts);

	platform_set_drvdata(pdev, ctx);

	return 0;

out3:
	iounmap(ctx->base);
out2:
	release_mem_region(r->start, resource_size(r));
out1:
	kfree(ctx);
	return ret;
}

static int au1550nd_remove(struct platform_device *pdev)
{
	struct au1550nd_ctx *ctx = platform_get_drvdata(pdev);
	struct resource *r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct nand_chip *chip = &ctx->chip;
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);
	iounmap(ctx->base);
	release_mem_region(r->start, 0x1000);
	kfree(ctx);
	return 0;
}

static struct platform_driver au1550nd_driver = {
	.driver = {
		.name	= "au1550-nand",
	},
	.probe		= au1550nd_probe,
	.remove		= au1550nd_remove,
};

module_platform_driver(au1550nd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Edge, LLC");
MODULE_DESCRIPTION("Board-specific glue layer for NAND flash on Pb1550 board");
