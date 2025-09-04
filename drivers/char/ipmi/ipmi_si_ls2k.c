// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Loongson-2K BMC IPMI interface
 *
 * Copyright (C) 2024-2025 Loongson Technology Corporation Limited.
 *
 * Authors:
 *	Chong Qiao <qiaochong@loongson.cn>
 *	Binbin Zhou <zhoubinbin@loongson.cn>
 */

#include <linux/bitfield.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/types.h>

#include "ipmi_si.h"

#define LS2K_KCS_FIFO_IBFH	0x0
#define LS2K_KCS_FIFO_IBFT	0x1
#define LS2K_KCS_FIFO_OBFH	0x2
#define LS2K_KCS_FIFO_OBFT	0x3

/* KCS registers */
#define LS2K_KCS_REG_STS	0x4
#define LS2K_KCS_REG_DATA_OUT	0x5
#define LS2K_KCS_REG_DATA_IN	0x6
#define LS2K_KCS_REG_CMD	0x8

#define LS2K_KCS_CMD_DATA	0xa
#define LS2K_KCS_VERSION	0xb
#define LS2K_KCS_WR_REQ		0xc
#define LS2K_KCS_WR_ACK		0x10

#define LS2K_KCS_STS_OBF	BIT(0)
#define LS2K_KCS_STS_IBF	BIT(1)
#define LS2K_KCS_STS_SMS_ATN	BIT(2)
#define LS2K_KCS_STS_CMD	BIT(3)

#define LS2K_KCS_DATA_MASK	(LS2K_KCS_STS_OBF | LS2K_KCS_STS_IBF | LS2K_KCS_STS_CMD)

static bool ls2k_registered;

static unsigned char ls2k_mem_inb_v0(const struct si_sm_io *io, unsigned int offset)
{
	void __iomem *addr = io->addr;
	int reg_offset;

	if (offset & BIT(0)) {
		reg_offset = LS2K_KCS_REG_STS;
	} else {
		writeb(readb(addr + LS2K_KCS_REG_STS) & ~LS2K_KCS_STS_OBF, addr + LS2K_KCS_REG_STS);
		reg_offset = LS2K_KCS_REG_DATA_OUT;
	}

	return readb(addr + reg_offset);
}

static unsigned char ls2k_mem_inb_v1(const struct si_sm_io *io, unsigned int offset)
{
	void __iomem *addr = io->addr;
	unsigned char inb = 0, cmd;
	bool obf, ibf;

	obf = readb(addr + LS2K_KCS_FIFO_OBFH) ^ readb(addr + LS2K_KCS_FIFO_OBFT);
	ibf = readb(addr + LS2K_KCS_FIFO_IBFH) ^ readb(addr + LS2K_KCS_FIFO_IBFT);
	cmd = readb(addr + LS2K_KCS_CMD_DATA);

	if (offset & BIT(0)) {
		inb = readb(addr + LS2K_KCS_REG_STS) & ~LS2K_KCS_DATA_MASK;
		inb |= FIELD_PREP(LS2K_KCS_STS_OBF, obf)
		    | FIELD_PREP(LS2K_KCS_STS_IBF, ibf)
		    | FIELD_PREP(LS2K_KCS_STS_CMD, cmd);
	} else {
		inb = readb(addr + LS2K_KCS_REG_DATA_OUT);
		writeb(readb(addr + LS2K_KCS_FIFO_OBFH), addr + LS2K_KCS_FIFO_OBFT);
	}

	return inb;
}

static void ls2k_mem_outb_v0(const struct si_sm_io *io, unsigned int offset,
			     unsigned char val)
{
	void __iomem *addr = io->addr;
	unsigned char sts = readb(addr + LS2K_KCS_REG_STS);
	int reg_offset;

	if (sts & LS2K_KCS_STS_IBF)
		return;

	if (offset & BIT(0)) {
		reg_offset = LS2K_KCS_REG_CMD;
		sts |= LS2K_KCS_STS_CMD;
	} else {
		reg_offset = LS2K_KCS_REG_DATA_IN;
		sts &= ~LS2K_KCS_STS_CMD;
	}

	writew(val, addr + reg_offset);
	writeb(sts | LS2K_KCS_STS_IBF, addr + LS2K_KCS_REG_STS);
	writel(readl(addr + LS2K_KCS_WR_REQ) + 1, addr + LS2K_KCS_WR_REQ);
}

static void ls2k_mem_outb_v1(const struct si_sm_io *io, unsigned int offset,
			     unsigned char val)
{
	void __iomem *addr = io->addr;
	unsigned char ibfh, ibft;
	int reg_offset;

	ibfh = readb(addr + LS2K_KCS_FIFO_IBFH);
	ibft = readb(addr + LS2K_KCS_FIFO_IBFT);

	if (ibfh ^ ibft)
		return;

	reg_offset = (offset & BIT(0)) ? LS2K_KCS_REG_CMD : LS2K_KCS_REG_DATA_IN;
	writew(val, addr + reg_offset);

	writeb(offset & BIT(0), addr + LS2K_KCS_CMD_DATA);
	writeb(!ibft, addr + LS2K_KCS_FIFO_IBFH);
	writel(readl(addr + LS2K_KCS_WR_REQ) + 1, addr + LS2K_KCS_WR_REQ);
}

static void ls2k_mem_cleanup(struct si_sm_io *io)
{
	if (io->addr)
		iounmap(io->addr);
}

static int ipmi_ls2k_mem_setup(struct si_sm_io *io)
{
	unsigned char version;

	io->addr = ioremap(io->addr_data, io->regspacing);
	if (!io->addr)
		return -EIO;

	version = readb(io->addr + LS2K_KCS_VERSION);

	io->inputb = version ? ls2k_mem_inb_v1 : ls2k_mem_inb_v0;
	io->outputb = version ? ls2k_mem_outb_v1 : ls2k_mem_outb_v0;
	io->io_cleanup = ls2k_mem_cleanup;

	return 0;
}

static int ipmi_ls2k_probe(struct platform_device *pdev)
{
	struct si_sm_io io;

	memset(&io, 0, sizeof(io));

	io.si_info	= &ipmi_kcs_si_info;
	io.io_setup	= ipmi_ls2k_mem_setup;
	io.addr_data	= pdev->resource[0].start;
	io.regspacing	= resource_size(&pdev->resource[0]);
	io.dev		= &pdev->dev;

	dev_dbg(&pdev->dev, "addr 0x%lx, spacing %d.\n", io.addr_data, io.regspacing);

	return ipmi_si_add_smi(&io);
}

static void ipmi_ls2k_remove(struct platform_device *pdev)
{
	ipmi_si_remove_by_dev(&pdev->dev);
}

struct platform_driver ipmi_ls2k_platform_driver = {
	.driver = {
		.name = "ls2k-ipmi-si",
	},
	.probe	= ipmi_ls2k_probe,
	.remove	= ipmi_ls2k_remove,
};

void ipmi_si_ls2k_init(void)
{
	platform_driver_register(&ipmi_ls2k_platform_driver);
	ls2k_registered = true;
}

void ipmi_si_ls2k_shutdown(void)
{
	if (ls2k_registered)
		platform_driver_unregister(&ipmi_ls2k_platform_driver);
}
