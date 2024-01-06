// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ASPEED FMC/SPI Memory Controller Driver
 *
 * Copyright (c) 2015-2022, IBM Corporation.
 * Copyright (c) 2020, ASPEED Corporation.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#define DEVICE_NAME "spi-aspeed-smc"

/* Type setting Register */
#define CONFIG_REG			0x0
#define   CONFIG_TYPE_SPI		0x2

/* CE Control Register */
#define CE_CTRL_REG			0x4

#define INTR_CTRL_STATUS_REG		0x08
#define   SPI_DMA_STATUS		BIT(11)

#define CMD_CTRL_REG			0xc

/* CEx Control Register */
#define CE0_CTRL_REG			0x10
#define   CTRL_IO_MODE_MASK		GENMASK(30, 28)
#define   CTRL_IO_SINGLE_DATA	        0x0
#define   CTRL_IO_DUAL_DATA		BIT(29)
#define   CTRL_IO_QUAD_DATA		BIT(30)
#define   CTRL_COMMAND_SHIFT		16
#define   CTRL_IO_ADDRESS_4B		BIT(13)	/* AST2400 SPI only */
#define   CTRL_IO_DUMMY_SET(dummy)					\
	(((((dummy) >> 2) & 0x1) << 14) | (((dummy) & 0x3) << 6))
#define   CTRL_FREQ_SEL_SHIFT		8
#define   CTRL_FREQ_SEL_MASK		GENMASK(11, CTRL_FREQ_SEL_SHIFT)
#define   CTRL_CE_STOP_ACTIVE		BIT(2)
#define   CTRL_IO_MODE_CMD_MASK		GENMASK(1, 0)
#define   CTRL_IO_MODE_NORMAL		0x0
#define   CTRL_IO_MODE_READ		0x1
#define   CTRL_IO_MODE_WRITE		0x2
#define   CTRL_IO_MODE_USER		0x3

#define   CTRL_IO_CMD_MASK		0xf0ff40c7

/* CEx Address Decoding Range Register */
#define CE0_SEGMENT_ADDR_REG		0x30

#define HOST_DIRECT_ACCESS_CMD_CTRL4	0x6c
#define HOST_DIRECT_ACCESS_CMD_CTRL2	0x74

#define DMA_CTRL_REG			0x80
#define   SPI_DMA_ENABLE		BIT(0)
#define   SPI_DMA_IRQ_EN		BIT(3)
#define   SPI_DAM_GRANT			BIT(30)
#define   SPI_DAM_REQUEST		BIT(31)
#define   DMA_GET_REQ_MAGIC		0xaeed0000
#define   DMA_DISCARD_REQ_MAGIC		0xdeea0000

#define DMA_FLASH_ADDR_REG		0x84
#define DMA_RAM_ADDR_REG		0x88
#define DMA_LEN_REG			0x8c

/* CEx Read timing compensation register */
#define CE0_TIMING_COMPENSATION_REG	0x94

#define ASPEED_SPI_OP_BUF_LEN		0x4000

static spinlock_t g_lock;

enum aspeed_spi_ctl_reg_value {
	ASPEED_SPI_BASE,
	ASPEED_SPI_READ,
	ASPEED_SPI_WRITE,
	ASPEED_SPI_MAX,
};

struct aspeed_spi;

struct aspeed_spi_chip {
	struct aspeed_spi	*aspi;
	u32			 cs;
	void __iomem		*ctl;
	void __iomem		*ahb_base;
	phys_addr_t		 ahb_base_phy;
	size_t			 ahb_window_sz;
	u32			 ctl_val[ASPEED_SPI_MAX];
	u32			 clk_freq;
};

struct aspeed_spi_data {
	u32	ctl0;
	u32	max_cs;
	bool	hastype;
	u32	mode_bits;
	u32	we0;
	u32	timing;
	u32	hclk_mask;
	u32	hdiv_max;
	size_t	min_window_sz;

	int (*adjust_window)(struct aspeed_spi *aspi);
	u64 (*segment_start)(struct aspeed_spi *aspi, u32 reg);
	u64 (*segment_end)(struct aspeed_spi *aspi, u32 reg);
	u32 (*segment_reg)(struct aspeed_spi *aspi, u64 start, u64 end);
	u32 (*get_clk_div)(struct aspeed_spi_chip *chip, u32 hz);
	int (*calibrate)(struct aspeed_spi_chip *chip, u32 hdiv,
			 const u8 *golden_buf, u8 *test_buf);
	void (*safs_support)(struct aspeed_spi *aspi,
			     enum spi_mem_data_dir dir,
			     u8 cmd, u8 addr_len, u8 bus_width);
};

#define ASPEED_SPI_MAX_NUM_CS	5

#define ASPEED_SPI_NORMAL_MODE		0x00000001
#define ASPEED_SPI_DMA_WRITE_MODE	0x00000002
#define ASPEED_SPI_FIXED_LOW_W_CLK	0x00000004
#define ASPEED_SPI_MIN_WINDOW		0x00000008
#define ASPEED_SPI_DMA_MODE		0x00000010
#define ASPEED_SPI_PURE_USER_MODE	0x00000020
#define ASPEED_SPI_TIMING_CLB_DISABLED	0x00000040
#define ASPEED_SPI_LTPI_SUPPORT		0x00000080

struct aspeed_spi {
	const struct aspeed_spi_data	*data;

	void __iomem		*regs;
	phys_addr_t		 ahb_base_phy;
	u64			 ltpi_base_phy;
	size_t			 ahb_window_sz;
	u32			 num_cs;
	struct device		*dev;

	struct clk		*clk;
	u32			 clk_freq;

	struct aspeed_spi_chip	 chips[ASPEED_SPI_MAX_NUM_CS];

	int			 irq;
	struct completion	 dma_done;
	dma_addr_t		 dma_addr_phy;
	void			*op_buf;
	u32			 flag;
};

static u32 aspeed_spi_get_io_mode(u8 buswidth)
{
	switch (buswidth) {
	case 1:
		return CTRL_IO_SINGLE_DATA;
	case 2:
		return CTRL_IO_DUAL_DATA;
	case 4:
		return CTRL_IO_QUAD_DATA;
	default:
		return CTRL_IO_SINGLE_DATA;
	}
}

static void aspeed_spi_set_io_mode(struct aspeed_spi_chip *chip, u32 io_mode)
{
	u32 ctl;

	if (io_mode > 0) {
		ctl = readl(chip->ctl) & ~CTRL_IO_MODE_MASK;
		ctl |= io_mode;
		writel(ctl, chip->ctl);
	}
}

static void aspeed_spi_start_user(struct aspeed_spi_chip *chip)
{
	u32 ctl = chip->ctl_val[ASPEED_SPI_BASE];

	ctl |= CTRL_IO_MODE_USER | CTRL_CE_STOP_ACTIVE;
	writel(ctl, chip->ctl);

	ctl &= ~CTRL_CE_STOP_ACTIVE;
	writel(ctl, chip->ctl);
}

static void aspeed_spi_stop_user(struct aspeed_spi_chip *chip)
{
	u32 ctl = chip->ctl_val[ASPEED_SPI_READ] |
		CTRL_IO_MODE_USER | CTRL_CE_STOP_ACTIVE;

	writel(ctl, chip->ctl);

	/* Restore defaults */
	writel(chip->ctl_val[ASPEED_SPI_READ], chip->ctl);
}

static int aspeed_spi_read_from_ahb(void *buf, void __iomem *src, size_t len)
{
	size_t offset = 0;

	if (IS_ALIGNED((uintptr_t)src, sizeof(uintptr_t)) &&
	    IS_ALIGNED((uintptr_t)buf, sizeof(uintptr_t))) {
		ioread32_rep(src, buf, len >> 2);
		offset = len & ~0x3;
		len -= offset;
	}
	ioread8_rep(src, (u8 *)buf + offset, len);
	return 0;
}

static int aspeed_spi_write_to_ahb(void __iomem *dst, const void *buf, size_t len)
{
	size_t offset = 0;

	if (IS_ALIGNED((uintptr_t)dst, sizeof(uintptr_t)) &&
	    IS_ALIGNED((uintptr_t)buf, sizeof(uintptr_t))) {
		iowrite32_rep(dst, buf, len >> 2);
		offset = len & ~0x3;
		len -= offset;
	}
	iowrite8_rep(dst, (const u8 *)buf + offset, len);
	return 0;
}

static void aspeed_spi_send_cmd(struct aspeed_spi_chip *chip, u8 opcode)
{
	aspeed_spi_write_to_ahb(chip->ahb_base, &opcode, 1);
}

static int aspeed_spi_send_addr(struct aspeed_spi_chip *chip, u8 addr_nbytes,
				u64 offset)
{
	__be32 temp;
	u32 cmdaddr;

	switch (addr_nbytes) {
	case 3:
		cmdaddr = offset & 0xFFFFFF;
		temp = cpu_to_be32(cmdaddr) >> 8;
		aspeed_spi_write_to_ahb(chip->ahb_base, &temp, 3);
		break;
	case 4:
		temp = cpu_to_be32(offset);
		aspeed_spi_write_to_ahb(chip->ahb_base, &temp, 4);
		break;
	default:
		WARN_ONCE(1, "Unexpected address width %u", addr_nbytes);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int aspeed_spi_read_reg(struct aspeed_spi_chip *chip,
			       const struct spi_mem_op *op)
{
	aspeed_spi_start_user(chip);
	aspeed_spi_write_to_ahb(chip->ahb_base, &op->cmd.opcode, 1);
	aspeed_spi_read_from_ahb(op->data.buf.in,
				 chip->ahb_base, op->data.nbytes);
	aspeed_spi_stop_user(chip);
	return 0;
}

static int aspeed_spi_write_reg(struct aspeed_spi_chip *chip,
				const struct spi_mem_op *op)
{
	aspeed_spi_start_user(chip);
	aspeed_spi_write_to_ahb(chip->ahb_base, &op->cmd.opcode, 1);
	aspeed_spi_write_to_ahb(chip->ahb_base, op->data.buf.out,
				op->data.nbytes);
	aspeed_spi_stop_user(chip);
	return 0;
}

static ssize_t aspeed_spi_read_user(struct aspeed_spi_chip *chip,
				    const struct spi_mem_op *op,
				    u64 offset, size_t len, void *buf)
{
	int io_mode;
	u8 dummy = 0xFF;
	int i;
	int ret;

	aspeed_spi_start_user(chip);

	aspeed_spi_send_cmd(chip, op->cmd.opcode);

	io_mode = aspeed_spi_get_io_mode(op->addr.buswidth);
	aspeed_spi_set_io_mode(chip, io_mode);
	ret = aspeed_spi_send_addr(chip, op->addr.nbytes, op->addr.val);
	if (ret < 0)
		return ret;

	if (op->dummy.buswidth && op->dummy.nbytes) {
		for (i = 0; i < op->dummy.nbytes; i++)
			aspeed_spi_write_to_ahb(chip->ahb_base,
						&dummy,	sizeof(dummy));
	}

	io_mode = aspeed_spi_get_io_mode(op->data.buswidth);
	aspeed_spi_set_io_mode(chip, io_mode);
	aspeed_spi_read_from_ahb(buf, chip->ahb_base, len);

	aspeed_spi_stop_user(chip);

	return 0;
}

static ssize_t aspeed_spi_write_user(struct aspeed_spi_chip *chip,
				     const struct spi_mem_op *op)
{
	int ret;
	int io_mode;

	aspeed_spi_start_user(chip);

	aspeed_spi_send_cmd(chip, op->cmd.opcode);

	io_mode = aspeed_spi_get_io_mode(op->addr.buswidth);
	aspeed_spi_set_io_mode(chip, io_mode);
	ret = aspeed_spi_send_addr(chip, op->addr.nbytes, op->addr.val);
	if (ret < 0)
		return ret;

	io_mode = aspeed_spi_get_io_mode(op->data.buswidth);
	aspeed_spi_set_io_mode(chip, io_mode);
	aspeed_spi_write_to_ahb(chip->ahb_base, op->data.buf.out, op->data.nbytes);

	aspeed_spi_stop_user(chip);

	return 0;
}

/* support for 1-1-1, 1-1-2 or 1-1-4 */
static bool aspeed_spi_supports_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	if (op->cmd.buswidth > 1)
		return false;

	if (op->addr.nbytes != 0) {
		if (op->addr.buswidth > 1)
			return false;
		if (op->addr.nbytes < 3 || op->addr.nbytes > 4)
			return false;
	}

	if (op->dummy.nbytes != 0) {
		if (op->dummy.buswidth > 1 || op->dummy.nbytes > 7)
			return false;
	}

	if (op->data.nbytes != 0 && op->data.buswidth > 4)
		return false;

	return spi_mem_default_supports_op(mem, op);
}

static const struct aspeed_spi_data ast2400_spi_data;

static int do_aspeed_spi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct aspeed_spi *aspi = spi_controller_get_devdata(mem->spi->controller);
	struct aspeed_spi_chip *chip = &aspi->chips[spi_get_chipselect(mem->spi, 0)];
	int ret = 0;

	dev_dbg(aspi->dev,
		"CE%d %s OP %#x mode:%d.%d.%d.%d naddr:%#x ndummies:%#x len:%#x",
		chip->cs, op->data.dir == SPI_MEM_DATA_IN ? "read" : "write",
		op->cmd.opcode, op->cmd.buswidth, op->addr.buswidth,
		op->dummy.buswidth, op->data.buswidth,
		op->addr.nbytes, op->dummy.nbytes, op->data.nbytes);

	if (op->data.dir == SPI_MEM_DATA_IN) {
		if (!op->addr.nbytes)
			ret = aspeed_spi_read_reg(chip, op);
		else
			ret = aspeed_spi_read_user(chip, op, op->addr.val,
						   op->data.nbytes, op->data.buf.in);
	} else {
		if (!op->addr.nbytes)
			ret = aspeed_spi_write_reg(chip, op);
		else
			ret = aspeed_spi_write_user(chip, op);
	}

	/* Restore defaults */
	writel(chip->ctl_val[ASPEED_SPI_READ], chip->ctl);
	return ret;
}

/*
 * If the slave device is SPI NOR flash, there are two types
 * of command mode for ASPEED SPI memory controller used to
 * transfer data. The first one is user mode and the other is
 * normal read/write mode. With user mode, SPI NOR flash
 * command, address and data processes are all handled by CPU.
 * With normal read/write mode, we can easily read/write data
 * to flash by reading or writing related remapped address,
 * then, SPI NOR flash command and address will be transferred
 * to flash by controller automatically. Besides, ASPEED SPI
 * memory controller can also block address or data bytes by
 * configure FMC0C/SPIR0C address and data mask register in
 * order to satisfy the following SPI flash operation sequences:
 * (command) only, (command and address) only or
 * (coommand and data) only.
 */
static int aspeed_spi_exec_op_normal_mode(struct spi_mem *mem,
					  const struct spi_mem_op *op)
{
	struct aspeed_spi *aspi = spi_controller_get_devdata(mem->spi->controller);
	struct device *dev = aspi->dev;
	struct aspeed_spi_chip *chip = &aspi->chips[spi_get_chipselect(mem->spi, 0)];
	u32 cs = spi_get_chipselect(mem->spi, 0);
	u32 ctrl_val;
	u32 addr_mode_reg, addr_mode_reg_backup;
	u32 addr_data_mask = 0;
	void __iomem *op_addr;
	const void *data_buf;
	u32 data_byte = 0;
	u32 dummy_data = 0;
	unsigned long flags;

	dev_dbg(dev, "cs:%d, cmd:%x(%d),addr:%llx(%d),dummy:%d(%d),data_len:%x(%d)\n",
		cs, op->cmd.opcode, op->cmd.buswidth, op->addr.val,
		op->addr.buswidth, op->dummy.nbytes, op->dummy.buswidth,
		op->data.nbytes, op->data.buswidth);

	addr_mode_reg = readl(aspi->regs + CE_CTRL_REG);
	addr_mode_reg_backup = addr_mode_reg;
	addr_data_mask = readl(aspi->regs + CMD_CTRL_REG);

	ctrl_val = chip->ctl_val[ASPEED_SPI_BASE];
	ctrl_val &= ~CTRL_IO_CMD_MASK;

	/* configure opcode */
	ctrl_val |= op->cmd.opcode << 16;

	/* configure operation address, address length and address mask */
	if (op->addr.nbytes != 0) {
		if (op->addr.nbytes == 3)
			addr_mode_reg &= ~(0x11 << cs);
		else
			addr_mode_reg |= (0x11 << cs);

		addr_data_mask &= 0x0f;
		op_addr = chip->ahb_base + op->addr.val;
	} else {
		addr_data_mask |= 0xf0;
		op_addr = chip->ahb_base;
	}

	if (op->dummy.nbytes != 0) {
		ctrl_val |= ((op->dummy.nbytes & 0x3) << 6 |
			     ((op->dummy.nbytes & 0x4) >> 2) << 14);
	}

	/* configure data io mode and data mask */
	if (op->data.nbytes != 0) {
		addr_data_mask &= 0xF0;
		data_byte = op->data.nbytes;
		if (op->data.dir == SPI_MEM_DATA_OUT) {
			if (data_byte % 4 != 0) {
				memset(aspi->op_buf, 0xff,
				       (((data_byte + 3) / 4) * 4));
				memcpy(aspi->op_buf, op->data.buf.out, data_byte);
				data_buf = aspi->op_buf;
				data_byte = (((data_byte + 3) / 4) * 4);
			} else {
				data_buf = op->data.buf.out;
			}
		} else {
			data_buf = op->data.buf.in;
		}

		if (op->data.buswidth)
			ctrl_val |= aspeed_spi_get_io_mode(op->data.buswidth);

	} else {
		addr_data_mask |= 0x0f;
		data_byte = 1;
		data_buf = &dummy_data;
	}

	/* configure command mode */
	if (op->data.dir == SPI_MEM_DATA_OUT)
		ctrl_val |= CTRL_IO_MODE_WRITE;
	else
		ctrl_val |= CTRL_IO_MODE_READ;

	/* set controller registers */
	writel(ctrl_val, chip->ctl);
	writel(addr_mode_reg, aspi->regs + CE_CTRL_REG);
	writel(addr_data_mask, aspi->regs + CMD_CTRL_REG);

	dev_dbg(dev, "ctrl: 0x%08x, addr_mode: 0x%x, mask: 0x%x, addr:0x%p\n",
		ctrl_val, addr_mode_reg, addr_data_mask, op_addr);

	/* trigger spi transmission or reception sequence */
	spin_lock_irqsave(&g_lock, flags);

	if (op->data.dir == SPI_MEM_DATA_OUT)
		memcpy_toio(op_addr, data_buf, data_byte);
	else
		memcpy_fromio((void *)data_buf, op_addr, data_byte);

	spin_unlock_irqrestore(&g_lock, flags);

	/* restore controller setting */
	writel(chip->ctl_val[ASPEED_SPI_READ], chip->ctl);
	writel(addr_mode_reg_backup, aspi->regs + CE_CTRL_REG);
	writel(0x0, aspi->regs + CMD_CTRL_REG);

	return 0;
}

#define MAX_READ_SZ_ONCE	0x3000 /* 12KB */

/*
 * When DMA memory mode is enabled, there is a limitation for AST2600,
 * both DMA source and destination address should be 4-byte aligned.
 * Thus, a 4-byte aligned buffer should be allocated previously and
 * CPU needs to copy data from it after DMA done.
 */
static ssize_t aspeed_spi_dirmap_dma_read(struct spi_mem_dirmap_desc *desc,
					  u64 offs, size_t len, void *buf)
{
	int ret = 0;
	u32 timeout = 0;
	struct aspeed_spi *aspi = spi_controller_get_devdata(desc->mem->spi->controller);
	struct device *dev = aspi->dev;
	struct aspeed_spi_chip *chip = &aspi->chips[spi_get_chipselect(desc->mem->spi, 0)];
	struct spi_mem_op op_tmpl = desc->info.op_tmpl;
	u32 reg_val;
	u32 extra;
	u32 tb_read_len = len;
	u32 read_len;
	u32 buf_offs = 0;
	u32 flash_offs = (u32)offs;

	if (chip->ahb_window_sz < offs + len) {
		dev_err(dev, "read range exceeds flash remapping size\n");
		return 0;
	}

	dev_dbg(dev, "read op:0x%x, addr:0x%llx, len:0x%zx\n",
		op_tmpl.cmd.opcode, offs, len);

	while (tb_read_len > 0) {
		/* read max 10KB bytes once */
		read_len = MAX_READ_SZ_ONCE - (flash_offs % MAX_READ_SZ_ONCE);
		if (tb_read_len < read_len)
			read_len = tb_read_len;

		/* For AST2600 SPI DMA, flash offset should be 4 byte aligned */
		extra = flash_offs % 4;
		if (extra != 0) {
			flash_offs = (flash_offs / 4) * 4;
			read_len += extra;
		}

		writel(DMA_GET_REQ_MAGIC, aspi->regs + DMA_CTRL_REG);
		if (readl(aspi->regs + DMA_CTRL_REG) & SPI_DAM_REQUEST) {
			while (!(readl(aspi->regs + DMA_CTRL_REG) &
				 SPI_DAM_GRANT))
				;
		}

		writel(chip->ctl_val[ASPEED_SPI_READ], chip->ctl);

		/*
		 * don't use dma_map_single here, since we cannot make sure the buf's
		 * start address is 4-byte-aligned.
		 */
		writel(0x0, aspi->regs + DMA_CTRL_REG);
		writel(aspi->dma_addr_phy, aspi->regs + DMA_RAM_ADDR_REG);
		writel(chip->ahb_base_phy + flash_offs, aspi->regs + DMA_FLASH_ADDR_REG);
		writel(read_len - 1, aspi->regs + DMA_LEN_REG);

		/* enable DMA irq */
		reg_val = readl(aspi->regs + INTR_CTRL_STATUS_REG);
		reg_val |= SPI_DMA_IRQ_EN;
		writel(reg_val, aspi->regs + INTR_CTRL_STATUS_REG);

		reinit_completion(&aspi->dma_done);

		/* enable read DMA */
		writel(0x1, aspi->regs + DMA_CTRL_REG);
		timeout = wait_for_completion_timeout(&aspi->dma_done,
						      msecs_to_jiffies(2000));
		if (timeout == 0) {
			writel(0x0, aspi->regs + DMA_CTRL_REG);
			writel(DMA_DISCARD_REQ_MAGIC, aspi->regs + DMA_CTRL_REG);
			dev_err(dev, "read data timeout %d\n", ret);
			ret = -1;
			goto end;
		} else {
			memcpy(buf + buf_offs, aspi->op_buf + extra, read_len - extra);
		}

		read_len -= extra;

		buf_offs += read_len;
		flash_offs += read_len;
		tb_read_len -= read_len;
	}

end:
	return ret ? 0 : len;
}

static ssize_t aspeed_spi_dirmap_dma_write(struct spi_mem_dirmap_desc *desc,
					   u64 offs, size_t len, const void *buf)
{
	int ret = 0;
	struct aspeed_spi *aspi = spi_controller_get_devdata(desc->mem->spi->controller);
	struct device *dev = aspi->dev;
	struct aspeed_spi_chip *chip = &aspi->chips[spi_get_chipselect(desc->mem->spi, 0)];
	u32 timeout = 0;
	u32 reg_val;
	struct spi_mem_op op_tmpl = desc->info.op_tmpl;

	if (chip->ahb_window_sz < offs + len) {
		dev_info(dev, "write range exceeds flash remapping size\n");
		return 0;
	}

	if (len < 1)
		return 0;

	if (len > ASPEED_SPI_OP_BUF_LEN) {
		dev_info(dev,
			 "written length exceeds expected value (0x%zx)\n",
			 len);
		return 0;
	}

	dev_dbg(dev, "write op:0x%x, addr:0x%llx, len:0x%zx\n",
		op_tmpl.cmd.opcode, offs, len);

	writel(DMA_GET_REQ_MAGIC, aspi->regs + DMA_CTRL_REG);
	if (readl(aspi->regs + DMA_CTRL_REG) & SPI_DAM_REQUEST) {
		while (!(readl(aspi->regs + DMA_CTRL_REG) &
			 SPI_DAM_GRANT))
			;
	}

	writel(chip->ctl_val[ASPEED_SPI_WRITE], chip->ctl);

	/*
	 * don't use dma_map_single here, since we cannot make sure the buf's
	 * start address is 4-byte-aligned.
	 */
	memcpy(aspi->op_buf, buf, len);

	writel(0x0, aspi->regs + DMA_CTRL_REG);
	writel(aspi->dma_addr_phy, aspi->regs + DMA_RAM_ADDR_REG);
	writel(chip->ahb_base_phy + offs, aspi->regs + DMA_FLASH_ADDR_REG);
	writel(len - 1, aspi->regs + DMA_LEN_REG);

	/* enable DMA irq */
	reg_val = readl(aspi->regs + INTR_CTRL_STATUS_REG);
	reg_val |= SPI_DMA_IRQ_EN;
	writel(reg_val, aspi->regs + INTR_CTRL_STATUS_REG);

	reinit_completion(&aspi->dma_done);

	/* enable write DMA */
	writel(0x3, aspi->regs + DMA_CTRL_REG);
	timeout = wait_for_completion_timeout(&aspi->dma_done, msecs_to_jiffies(2000));
	if (timeout == 0) {
		writel(0x0, aspi->regs + DMA_CTRL_REG);
		writel(DMA_DISCARD_REQ_MAGIC, aspi->regs + DMA_CTRL_REG);
		dev_err(dev, "write data timeout %d\n", ret);
		ret = -1;
	}

	writel(chip->ctl_val[ASPEED_SPI_READ], chip->ctl);

	return ret ? 0 : len;
}

static irqreturn_t aspeed_spi_dma_isr(int irq, void *dev_id)
{
	struct aspeed_spi *aspi = (struct aspeed_spi *)dev_id;
	u32 reg_val;

	if (!(readl(aspi->regs + INTR_CTRL_STATUS_REG) & SPI_DMA_STATUS))
		return IRQ_NONE;

	reg_val = readl(aspi->regs + INTR_CTRL_STATUS_REG);
	reg_val &= ~SPI_DMA_IRQ_EN;
	writel(reg_val, aspi->regs + INTR_CTRL_STATUS_REG);

	writel(0x0, aspi->regs + DMA_CTRL_REG);
	writel(DMA_DISCARD_REQ_MAGIC, aspi->regs + DMA_CTRL_REG);

	complete(&aspi->dma_done);

	return IRQ_HANDLED;
}

static int aspeed_spi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	int ret;

	ret = do_aspeed_spi_exec_op(mem, op);
	if (ret)
		dev_err(&mem->spi->dev, "operation failed: %d\n", ret);
	return ret;
}

static const char *aspeed_spi_get_name(struct spi_mem *mem)
{
	struct aspeed_spi *aspi = spi_controller_get_devdata(mem->spi->controller);
	struct device *dev = aspi->dev;

	return devm_kasprintf(dev, GFP_KERNEL, "%s.%d", dev_name(dev),
			      spi_get_chipselect(mem->spi, 0));
}

static const struct aspeed_spi_data ast2500_fmc_data;
static const struct aspeed_spi_data ast2500_spi_data;
static const struct aspeed_spi_data ast2600_spi_data;
static const struct aspeed_spi_data ast2600_fmc_data;

static int aspeed_spi_set_window(struct aspeed_spi *aspi)
{
	struct device *dev = aspi->dev;
	off_t offset = 0;
	phys_addr_t start;
	phys_addr_t ltpi_start;
	phys_addr_t end;
	void __iomem *seg_reg_base = aspi->regs + CE0_SEGMENT_ADDR_REG;
	void __iomem *seg_reg;
	u32 seg_val;
	u32 cs;
	size_t win_sz;

	for (cs = 0; cs < aspi->data->max_cs; cs++) {
		if (aspi->chips[cs].ahb_base)
			devm_iounmap(dev, aspi->chips[cs].ahb_base);
	}

	for (cs = 0; cs < aspi->data->max_cs; cs++) {
		seg_reg = seg_reg_base + cs * 4;
		start = aspi->ahb_base_phy + offset;
		ltpi_start = aspi->ltpi_base_phy + offset;
		win_sz = aspi->chips[cs].ahb_window_sz;
		end = start + win_sz;

		seg_val = aspi->data->segment_reg(aspi, start, end);
		if (win_sz == 0)
			seg_val = 0;

		writel(seg_val, seg_reg);

		if (seg_val != readl(seg_reg)) {
			dev_warn(dev, "CE%d expected window [ 0x%.9llx - 0x%.9llx ] %zdMB",
				 cs, (u64)start, (u64)end - 1, win_sz >> 20);
			dev_warn(dev, "seg_val = 0x%x, readl(seg_reg) = 0x%x\n",
				 seg_val, readl(seg_reg));

			seg_val = readl(seg_reg);
			dev_warn(dev, "restore to 0x%x\n", seg_val);

			win_sz = aspi->data->segment_end(aspi, seg_val) -
				 aspi->data->segment_start(aspi, seg_val);

			if (win_sz < 0)
				return -ERANGE;

			end = start + win_sz;
		}

		if (win_sz != 0)
			dev_dbg(dev, "CE%d new window [ 0x%.9llx - 0x%.9llx ] %zdMB",
				cs, (u64)start, (u64)end - 1,  win_sz >> 20);
		else
			dev_dbg(dev, "CE%d window closed", cs);

		aspi->chips[cs].ahb_base_phy = start;
		offset += win_sz;

		if (offset > aspi->ahb_window_sz) {
			dev_err(dev, "offset value 0x%llx is too large.\n", (u64)offset);
			return -ENOSPC;
		}

		if (win_sz == 0)
			continue;

		if ((aspi->flag & ASPEED_SPI_MIN_WINDOW) != 0) {
			aspi->chips[cs].ahb_base = devm_ioremap(dev,
								start,
								aspi->data->min_window_sz);
		} else {
			if ((aspi->flag & ASPEED_SPI_LTPI_SUPPORT) != 0)
				aspi->chips[cs].ahb_base = devm_ioremap(dev,
									ltpi_start,
									win_sz);
			else
				aspi->chips[cs].ahb_base = devm_ioremap(dev,
									start,
									win_sz);
		}

		if (!aspi->chips[cs].ahb_base) {
			dev_err(dev, "fail to remap window [0x%.9llx - 0x%.9llx]\n",
				(u64)start, (u64)end - 1);
			return -ENOMEM;
		}
	}

	return 0;
}

/*
 * Usually, the decoding address is not configured at the u-boot stage.
 * Or, the existing decoding address configuration is wrong.
 * Thus, force to assign a default decoding address during driver probe.
 */
static int aspeed_spi_chip_set_default_window(struct aspeed_spi *aspi)
{
	u32 cs;

	/* No segment registers for the AST2400 SPI controller */
	if (aspi->data == &ast2400_spi_data) {
		aspi->chips[0].ahb_window_sz = aspi->ahb_window_sz;
		return aspeed_spi_set_window(aspi);
	}

	for (cs = 0; cs < aspi->num_cs; cs++)
		aspi->chips[cs].ahb_window_sz = aspi->data->min_window_sz;

	/* Close unused CS */
	for (cs = aspi->num_cs; cs < aspi->data->max_cs; cs++)
		aspi->chips[cs].ahb_window_sz = 0;

	if (aspi->data->adjust_window)
		aspi->data->adjust_window(aspi);

	return aspeed_spi_set_window(aspi);
}

/*
 * As the flash size grows up, we need to trim some decoding
 * size if needed for the sake of conforming the maximum
 * decoding size. We trim the decoding size from the largest
 * CS in order to avoid affecting the default boot up sequence
 * from CS0 where command mode or normal mode is used.
 * Notice, if a CS decoding size is trimmed, command mode may
 * not work perfectly on that CS.
 */
static int aspeed_spi_trim_window_size(struct aspeed_spi *aspi)
{
	struct aspeed_spi_chip *chips = aspi->chips;
	size_t total_sz;
	int cs = aspi->data->max_cs - 1;
	u32 i;
	bool trimed = false;

	do {
		total_sz = 0;
		for (i = 0; i < aspi->data->max_cs; i++)
			total_sz += chips[i].ahb_window_sz;

		if (cs < 0)
			return -ENOMEM;

		if (chips[cs].ahb_window_sz <= aspi->data->min_window_sz) {
			cs--;
			continue;
		}

		if (total_sz > aspi->ahb_window_sz) {
			chips[cs].ahb_window_sz -= aspi->data->min_window_sz;
			total_sz -= aspi->data->min_window_sz;
			trimed = true;
		}
	} while (total_sz > aspi->ahb_window_sz);

	if (trimed) {
		dev_warn(aspi->dev, "trimed window size:\n");
		for (cs = 0; cs < aspi->data->max_cs; cs++) {
			dev_warn(aspi->dev, "CE%d: 0x%08zx\n",
				 cs, chips[cs].ahb_window_sz);
		}
	}

	return 0;
}

static int aspeed_adjust_window_ast2400(struct aspeed_spi *aspi)
{
	int ret;
	int cs;
	struct aspeed_spi_chip *chips = aspi->chips;

	/* Close unused CS. */
	for (cs = aspi->num_cs; cs < aspi->data->max_cs; cs++)
		chips[cs].ahb_window_sz = 0;

	ret = aspeed_spi_trim_window_size(aspi);
	if (ret != 0)
		return ret;

	return 0;
}

/*
 * For AST2500, the minimum address decoding size for each CS
 * is 8MB instead of zero. This address decoding size is
 * mandatory for each CS no matter whether it will be used.
 * This is a HW limitation.
 */
static int aspeed_adjust_window_ast2500(struct aspeed_spi *aspi)
{
	int ret;
	int i;
	int cs;
	size_t pre_sz;
	size_t extra_sz;
	struct aspeed_spi_chip *chips = aspi->chips;

	/* Assign min_window_sz to unused CS. */
	for (cs = aspi->num_cs; cs < aspi->data->max_cs; cs++) {
		if (chips[cs].ahb_window_sz < aspi->data->min_window_sz)
			chips[cs].ahb_window_sz = aspi->data->min_window_sz;
	}

	/*
	 * If commnad mode or normal mode is used, the start address of a
	 * decoding range should be multiple of its related flash size.
	 * Namely, the total decoding size from flash 0 to flash N should
	 * be multiple of the size of flash (N + 1).
	 */
	for (cs = aspi->num_cs - 1; cs >= 0; cs--) {
		pre_sz = 0;
		for (i = 0; i < cs; i++)
			pre_sz += chips[i].ahb_window_sz;

		if (chips[cs].ahb_window_sz != 0 &&
		    (pre_sz % chips[cs].ahb_window_sz) != 0) {
			extra_sz = chips[cs].ahb_window_sz -
				   (pre_sz % chips[cs].ahb_window_sz);
			chips[0].ahb_window_sz += extra_sz;
		}
	}

	ret = aspeed_spi_trim_window_size(aspi);
	if (ret != 0)
		return ret;

	if (aspi->data == &ast2500_spi_data)
		chips[1].ahb_window_sz = 0x08000000 - chips[0].ahb_window_sz;

	return 0;
}

static int aspeed_adjust_window_ast2600(struct aspeed_spi *aspi)
{
	int ret;
	int i;
	int cs;
	size_t pre_sz;
	size_t extra_sz;
	struct aspeed_spi_chip *chips = aspi->chips;

	/* Close unused CS. */
	for (cs = aspi->num_cs; cs < aspi->data->max_cs; cs++)
		chips[cs].ahb_window_sz = 0;

	/*
	 * If commnad mode or normal mode is used, the start address of a
	 * decoding range should be multiple of its related flash size.
	 * Namely, the total decoding size from flash 0 to flash N should
	 * be multiple of the size of flash (N + 1).
	 */
	for (cs = aspi->num_cs - 1; cs >= 0; cs--) {
		pre_sz = 0;
		for (i = 0; i < cs; i++)
			pre_sz += chips[i].ahb_window_sz;

		if (chips[cs].ahb_window_sz != 0 &&
		    (pre_sz % chips[cs].ahb_window_sz) != 0) {
			extra_sz = chips[cs].ahb_window_sz -
				   (pre_sz % chips[cs].ahb_window_sz);
			chips[0].ahb_window_sz += extra_sz;
		}
	}

	ret = aspeed_spi_trim_window_size(aspi);
	if (ret != 0)
		return ret;

	return 0;
}

/*
 * Yet to be done when possible :
 * - ioremap each window, not strictly necessary since the overall window
 *   is correct.
 */

static int aspeed_spi_chip_adjust_window(struct aspeed_spi_chip *chip,
					 size_t size)
{
	struct aspeed_spi *aspi = chip->aspi;
	int ret;

	/* No segment registers for the AST2400 SPI controller */
	if (aspi->data == &ast2400_spi_data)
		return 0;

	/* Adjust this chip window */
	aspi->chips[chip->cs].ahb_window_sz = size;

	if (aspi->data->adjust_window)
		aspi->data->adjust_window(aspi);

	ret = aspeed_spi_set_window(aspi);
	if (ret)
		return ret;

	return 0;
}

static int aspeed_spi_do_calibration(struct aspeed_spi_chip *chip);

static int aspeed_spi_dirmap_create(struct spi_mem_dirmap_desc *desc)
{
	struct aspeed_spi *aspi = spi_controller_get_devdata(desc->mem->spi->controller);
	struct aspeed_spi_chip *chip = &aspi->chips[spi_get_chipselect(desc->mem->spi, 0)];
	struct spi_mem_op *op = &desc->info.op_tmpl;
	u32 ctl_val;
	u32 div = 0;
	int i;
	int ret = 0;

	dev_dbg(aspi->dev,
		"CE%d %s dirmap [ 0x%.8llx - 0x%.8llx ] OP %#x mode:%d.%d.%d.%d naddr:%#x ndummies:%#x\n",
		chip->cs, op->data.dir == SPI_MEM_DATA_IN ? "read" : "write",
		desc->info.offset, desc->info.offset + desc->info.length,
		op->cmd.opcode, op->cmd.buswidth, op->addr.buswidth,
		op->dummy.buswidth, op->data.buswidth,
		op->addr.nbytes, op->dummy.nbytes);

	chip->clk_freq = desc->mem->spi->max_speed_hz;

	if (aspi->data->safs_support) {
		aspi->data->safs_support(aspi, op->data.dir,
					 op->cmd.opcode,
					 op->addr.nbytes,
					 op->data.buswidth);
	}

	/* Only for reads */
	if (op->data.dir == SPI_MEM_DATA_IN) {
		ret = aspeed_spi_chip_adjust_window(chip, desc->info.length);
		if (ret)
			return ret;

		if (desc->info.length > chip->ahb_window_sz)
			dev_warn(aspi->dev, "CE%d window (%zdMB) too small for mapping",
				 chip->cs, chip->ahb_window_sz >> 20);

		/* Define the default IO read settings */
		ctl_val = chip->ctl_val[ASPEED_SPI_BASE] & ~CTRL_IO_CMD_MASK;
		ctl_val |= aspeed_spi_get_io_mode(op->data.buswidth) |
			op->cmd.opcode << CTRL_COMMAND_SHIFT |
			CTRL_IO_MODE_READ;

		if (op->dummy.nbytes)
			ctl_val |= CTRL_IO_DUMMY_SET(op->dummy.nbytes / op->dummy.buswidth);

		/* Tune 4BYTE address mode */
		if (op->addr.nbytes) {
			u32 addr_mode = readl(aspi->regs + CE_CTRL_REG);

			if (op->addr.nbytes == 4)
				addr_mode |= (0x11 << chip->cs);
			else
				addr_mode &= ~(0x11 << chip->cs);
			writel(addr_mode, aspi->regs + CE_CTRL_REG);

			/* AST2400 SPI controller sets 4BYTE address mode in
			 * CE0 Control Register
			 */
			if (op->addr.nbytes == 4 && chip->aspi->data == &ast2400_spi_data)
				ctl_val |= CTRL_IO_ADDRESS_4B;
		}

		/* READ mode is the controller default setting */
		chip->ctl_val[ASPEED_SPI_READ] = ctl_val;
		writel(chip->ctl_val[ASPEED_SPI_READ], chip->ctl);

		/* assign SPI clock frequency division */
		if (chip->clk_freq < aspi->clk_freq / 5) {
			if (aspi->data->get_clk_div)
				div = aspi->data->get_clk_div(chip, chip->clk_freq);

			for (i = 0; i < ASPEED_SPI_MAX; i++)
				chip->ctl_val[i] = (chip->ctl_val[i] &
						    aspi->data->hclk_mask) |
						   div;
		} else {
			ret = aspeed_spi_do_calibration(chip);
		}

		dev_info(aspi->dev, "CE%d read buswidth: %d [0x%08x]\n",
			 chip->cs, op->data.buswidth, chip->ctl_val[ASPEED_SPI_READ]);
		dev_dbg(aspi->dev, "spi clock frequency: %dMHz\n",
			chip->clk_freq / 1000000);

		/*
		 * aspeed_spi_dirmap_read is not created in
		 * current spi_controller_mem_ops.
		 */
		if (!desc->mem->spi->controller->mem_ops->dirmap_read)
			return -EOPNOTSUPP;

	} else if (op->data.dir == SPI_MEM_DATA_OUT) {
		/* record some information for normal mode. */
		ctl_val = chip->ctl_val[ASPEED_SPI_BASE] & (~CTRL_IO_CMD_MASK);
		ctl_val |= aspeed_spi_get_io_mode(op->data.buswidth) |
			   op->cmd.opcode << 16 | CTRL_IO_MODE_WRITE;

		if ((aspi->flag & ASPEED_SPI_FIXED_LOW_W_CLK) != 0) {
			/* adjust spi clk for write */
			ctl_val = (ctl_val & (~0x0f000f00)) | 0x03000000;
		}

		chip->ctl_val[ASPEED_SPI_WRITE] = ctl_val;

		dev_info(aspi->dev, "CE%d write buswidth: %d [0x%08x]\n",
			 chip->cs, op->data.buswidth, chip->ctl_val[ASPEED_SPI_WRITE]);

		/*
		 * aspeed_spi_dirmap_write is not created in
		 * current spi_controller_mem_ops.
		 */
		if (!desc->mem->spi->controller->mem_ops->dirmap_write)
			return -EOPNOTSUPP;
	} else {
		return -EOPNOTSUPP;
	}

	return ret;
}

static ssize_t aspeed_spi_dirmap_read(struct spi_mem_dirmap_desc *desc,
				      u64 offset, size_t len, void *buf)
{
	struct aspeed_spi *aspi = spi_controller_get_devdata(desc->mem->spi->controller);
	struct aspeed_spi_chip *chip = &aspi->chips[spi_get_chipselect(desc->mem->spi, 0)];

	/* Switch to USER command mode if mapping window is too small */
	if (chip->ahb_window_sz < offset + len) {
		int ret;

		ret = aspeed_spi_read_user(chip, &desc->info.op_tmpl, offset, len, buf);
		if (ret < 0)
			return ret;
	} else {
		memcpy_fromio(buf, chip->ahb_base + offset, len);
	}

	return len;
}

static const struct spi_controller_mem_ops aspeed_spi_mem_ops = {
	.supports_op = aspeed_spi_supports_op,
	.exec_op = aspeed_spi_exec_op,
	.get_name = aspeed_spi_get_name,
	.dirmap_create = aspeed_spi_dirmap_create,
	.dirmap_read = aspeed_spi_dirmap_read,
};

static const struct spi_controller_mem_ops aspeed_spi_mem_ops_pure_user = {
	.supports_op = aspeed_spi_supports_op,
	.exec_op = aspeed_spi_exec_op,
	.get_name = aspeed_spi_get_name,
	.dirmap_create = aspeed_spi_dirmap_create,
};

static const struct spi_controller_mem_ops aspeed_spi_mem_ops_normal_mode = {
	.supports_op = aspeed_spi_supports_op,
	.exec_op = aspeed_spi_exec_op_normal_mode,
	.get_name = aspeed_spi_get_name,
	.dirmap_create = aspeed_spi_dirmap_create,
	.dirmap_read = aspeed_spi_dirmap_read,
};

static const struct spi_controller_mem_ops aspeed_spi_ops_normal_read_dma_write = {
	.get_name = aspeed_spi_get_name,
	.exec_op = aspeed_spi_exec_op_normal_mode,
	.supports_op = aspeed_spi_supports_op,
	.dirmap_create = aspeed_spi_dirmap_create,
	.dirmap_read = aspeed_spi_dirmap_read,
	.dirmap_write = aspeed_spi_dirmap_dma_write,
};

static const struct spi_controller_mem_ops aspeed_spi_ops_dma_mode = {
	.get_name = aspeed_spi_get_name,
	.exec_op = aspeed_spi_exec_op,
	.supports_op = aspeed_spi_supports_op,
	.dirmap_create = aspeed_spi_dirmap_create,
	.dirmap_read = aspeed_spi_dirmap_dma_read,
	.dirmap_write = aspeed_spi_dirmap_dma_write,
};

static void aspeed_spi_chip_set_type(struct aspeed_spi *aspi, unsigned int cs, int type)
{
	u32 reg;

	reg = readl(aspi->regs + CONFIG_REG);
	reg &= ~(0x3 << (cs * 2));
	reg |= type << (cs * 2);
	writel(reg, aspi->regs + CONFIG_REG);
}

static void aspeed_spi_chip_enable(struct aspeed_spi *aspi, unsigned int cs, bool enable)
{
	u32 we_bit = BIT(aspi->data->we0 + cs);
	u32 reg = readl(aspi->regs + CONFIG_REG);

	if (enable)
		reg |= we_bit;
	else
		reg &= ~we_bit;
	writel(reg, aspi->regs + CONFIG_REG);
}

static int aspeed_spi_setup(struct spi_device *spi)
{
	struct aspeed_spi *aspi = spi_controller_get_devdata(spi->controller);
	const struct aspeed_spi_data *data = aspi->data;
	unsigned int cs = spi_get_chipselect(spi, 0);
	struct aspeed_spi_chip *chip = &aspi->chips[cs];
	u32 clk_div = 0;
	u32 i;

	chip->aspi = aspi;
	chip->cs = cs;
	chip->ctl = aspi->regs + data->ctl0 + cs * 4;

	/* The driver only supports SPI type flash */
	if (data->hastype)
		aspeed_spi_chip_set_type(aspi, cs, CONFIG_TYPE_SPI);

	aspeed_spi_chip_enable(aspi, cs, true);

	chip->ctl_val[ASPEED_SPI_BASE] = CTRL_CE_STOP_ACTIVE | CTRL_IO_MODE_USER;

	if ((aspi->flag & ASPEED_SPI_TIMING_CLB_DISABLED) != 0) {
		if (aspi->data->get_clk_div)
			clk_div = aspi->data->get_clk_div(chip, spi->max_speed_hz);

		for (i = 0; i < ASPEED_SPI_MAX; i++) {
			chip->ctl_val[i] = (chip->ctl_val[i] & aspi->data->hclk_mask) |
					   clk_div;
		}
	}

	dev_dbg(aspi->dev, "CE%d setup done\n", cs);
	return 0;
}

static void aspeed_spi_cleanup(struct spi_device *spi)
{
	struct aspeed_spi *aspi = spi_controller_get_devdata(spi->controller);
	unsigned int cs = spi_get_chipselect(spi, 0);

	aspeed_spi_chip_enable(aspi, cs, false);

	dev_dbg(aspi->dev, "CE%d cleanup done\n", cs);
}

static void aspeed_spi_enable(struct aspeed_spi *aspi, bool enable)
{
	int cs;

	for (cs = 0; cs < aspi->data->max_cs; cs++)
		aspeed_spi_chip_enable(aspi, cs, enable);
}

static int aspeed_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct aspeed_spi_data *data;
	struct spi_controller *ctlr;
	struct aspeed_spi *aspi;
	struct resource *res;
	struct reset_control *reset;
	int ret = 0;

	data = of_device_get_match_data(&pdev->dev);
	if (!data)
		return -ENODEV;

	ctlr = devm_spi_alloc_host(dev, sizeof(*aspi));
	if (!ctlr)
		return -ENOMEM;

	aspi = spi_controller_get_devdata(ctlr);
	platform_set_drvdata(pdev, aspi);
	aspi->data = data;
	aspi->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	aspi->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(aspi->regs))
		return PTR_ERR(aspi->regs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	aspi->ahb_window_sz = resource_size(res);
	aspi->ahb_base_phy = res->start;

	aspi->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(aspi->clk)) {
		dev_err(dev, "missing clock\n");
		return PTR_ERR(aspi->clk);
	}

	aspi->clk_freq = clk_get_rate(aspi->clk);
	if (!aspi->clk_freq) {
		dev_err(dev, "invalid clock\n");
		return -EINVAL;
	}

	reset = devm_reset_control_get_exclusive(dev, NULL);
	if (!IS_ERR(reset))
		reset_control_deassert(reset);

	aspi->flag = 0;
	if (of_property_read_bool(dev->of_node, "fmc-spi-normal-mode"))
		aspi->flag |= ASPEED_SPI_NORMAL_MODE;
	else if (of_property_read_bool(dev->of_node, "fmc-spi-dma-write"))
		aspi->flag |= ASPEED_SPI_DMA_WRITE_MODE;
	else if (of_property_read_bool(dev->of_node, "fmc-spi-dma-mode"))
		aspi->flag |= ASPEED_SPI_DMA_MODE;
	else if (of_property_read_bool(dev->of_node, "pure-spi-mode-only"))
		aspi->flag |= ASPEED_SPI_PURE_USER_MODE;

	if (of_property_read_bool(dev->of_node, "timing-calibration-disabled"))
		aspi->flag |= ASPEED_SPI_TIMING_CLB_DISABLED;

	/* Should be set on AST2600-A1/A2 for errata 65 */
	if (of_property_read_bool(dev->of_node, "low-spi-clk-write")) {
		dev_info(dev, "adopt low spi clk for write\n");
		aspi->flag |= ASPEED_SPI_FIXED_LOW_W_CLK;
	}

	if (!of_property_read_u64(dev->of_node, "ltpi-base", &aspi->ltpi_base_phy)) {
		dev_info(dev, "ltpi support\n");
		aspi->flag |= ASPEED_SPI_LTPI_SUPPORT;
	}

	if ((aspi->flag & ASPEED_SPI_NORMAL_MODE) != 0 ||
	    (aspi->flag & ASPEED_SPI_DMA_WRITE_MODE) != 0 ||
	    (aspi->flag & ASPEED_SPI_DMA_MODE) != 0) {
		aspi->op_buf = dma_alloc_coherent(dev,
						  ASPEED_SPI_OP_BUF_LEN,
						  &aspi->dma_addr_phy,
						  GFP_DMA | GFP_KERNEL);
		if (!aspi->op_buf) {
			ret = -ENOMEM;
			goto end;
		}
	}

	if ((aspi->flag & ASPEED_SPI_DMA_WRITE_MODE) != 0 ||
	    (aspi->flag & ASPEED_SPI_DMA_MODE) != 0) {
		aspi->irq = platform_get_irq(pdev, 0);
		if (aspi->irq < 0) {
			dev_err(dev, "fail to get irq (%d)\n", aspi->irq);
			return aspi->irq;
		}

		ret = devm_request_irq(dev, aspi->irq, aspeed_spi_dma_isr,
				       IRQF_SHARED, dev_name(dev), aspi);

		if (ret < 0) {
			dev_err(dev, "fail to request irq (%d)\n", ret);
			goto end;
		}

		init_completion(&aspi->dma_done);
	}

	if ((aspi->flag & ASPEED_SPI_DMA_MODE) != 0 &&
	    (aspi->flag & ASPEED_SPI_PURE_USER_MODE) != 0)
		aspi->flag |= ASPEED_SPI_MIN_WINDOW;

	/* IRQ is for DMA, which the driver doesn't support yet */

	ctlr->mode_bits = SPI_RX_DUAL | SPI_TX_DUAL | data->mode_bits;
	ctlr->bus_num = pdev->id;
	ctlr->setup = aspeed_spi_setup;
	ctlr->cleanup = aspeed_spi_cleanup;
	ctlr->num_chipselect = of_get_available_child_count(dev->of_node);
	ctlr->dev.of_node = dev->of_node;

	if ((aspi->flag & ASPEED_SPI_NORMAL_MODE) != 0) {
		dev_info(&pdev->dev, "normal mode is used\n");
		ctlr->mem_ops = &aspeed_spi_mem_ops_normal_mode;
	} else if ((aspi->flag & ASPEED_SPI_DMA_WRITE_MODE) != 0) {
		dev_info(&pdev->dev, "normal read and dma write mode are used\n");
		ctlr->mem_ops = &aspeed_spi_ops_normal_read_dma_write;
	} else if ((aspi->flag & ASPEED_SPI_DMA_MODE) != 0) {
		dev_info(&pdev->dev, "dma mode is used\n");
		ctlr->mem_ops = &aspeed_spi_ops_dma_mode;
	} else if ((aspi->flag & ASPEED_SPI_PURE_USER_MODE) != 0) {
		dev_info(&pdev->dev, "user mode is used\n");
		ctlr->mem_ops = &aspeed_spi_mem_ops_pure_user;
	} else {
		dev_info(&pdev->dev, "user mode and normal read are used\n");
		ctlr->mem_ops = &aspeed_spi_mem_ops;
	}

	if (ctlr->num_chipselect == 0) {
		dev_warn(&pdev->dev, "Force num_chipselect to 1\n");
		ctlr->num_chipselect = 1;
	}

	aspi->num_cs = ctlr->num_chipselect;

	ret = aspeed_spi_chip_set_default_window(aspi);
	if (ret) {
		dev_err(&pdev->dev, "fail to set default window\n");
		goto end;
	}

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_controller failed\n");
		goto end;
	}

end:
	return ret;
}

static void aspeed_spi_remove(struct platform_device *pdev)
{
	struct aspeed_spi *aspi = platform_get_drvdata(pdev);

	if (aspi->op_buf) {
		dma_free_coherent(aspi->dev,
				  ASPEED_SPI_OP_BUF_LEN,
				  aspi->op_buf,
				  aspi->dma_addr_phy);
	}

	aspeed_spi_enable(aspi, false);
}

/*
 * AHB mappings
 */

/*
 * The Segment Registers of the AST2400 and AST2500 use a 8MB unit.
 * The address range is encoded with absolute addresses in the overall
 * mapping window.
 */
static u64 aspeed_spi_segment_start(struct aspeed_spi *aspi, u32 reg)
{
	return ((reg >> 16) & 0xFF) << 23;
}

static u64 aspeed_spi_segment_end(struct aspeed_spi *aspi, u32 reg)
{
	return ((reg >> 24) & 0xFF) << 23;
}

static u32 aspeed_spi_segment_reg(struct aspeed_spi *aspi, u64 start, u64 end)
{
	return (u32)((((start >> 23) & 0xFF) << 16) |
		     (((end >> 23) & 0xFF) << 24));
}

/*
 * The Segment Registers of the AST2600 use a 1MB unit. The address
 * range is encoded with offsets in the overall mapping window.
 */

#define AST2600_SEG_ADDR_MASK 0x0ff00000

static u64 aspeed_spi_segment_ast2600_start(struct aspeed_spi *aspi,
					    u32 reg)
{
	u64 start_offset = (reg << 16) & AST2600_SEG_ADDR_MASK;

	return aspi->ahb_base_phy + start_offset;
}

static u64 aspeed_spi_segment_ast2600_end(struct aspeed_spi *aspi,
					  u32 reg)
{
	u64 end_offset = reg & AST2600_SEG_ADDR_MASK;

	/* segment is disabled */
	if (!end_offset)
		return aspi->ahb_base_phy;

	return aspi->ahb_base_phy + end_offset + 0x100000;
}

static u32 aspeed_spi_segment_ast2600_reg(struct aspeed_spi *aspi,
					  u64 start, u64 end)
{
	/* disable zero size segments */
	if (start == end)
		return 0;

	return (u32)((start & AST2600_SEG_ADDR_MASK) >> 16) |
		     ((end - 1) & AST2600_SEG_ADDR_MASK);
}

static u64 aspeed_spi_segment_ast2700_start(struct aspeed_spi *aspi,
					    u32 reg)
{
	u64 start_offset = (((reg) & 0x0000ffff) << 16);

	if (start_offset == 0)
		return aspi->ahb_base_phy;

	return aspi->ahb_base_phy + start_offset;
}

static u64 aspeed_spi_segment_ast2700_end(struct aspeed_spi *aspi,
					  u32 reg)
{
	u64 end_offset = reg & 0xffff0000;

	/* Meaningless end_offset, set to physical ahb base. */
	if (end_offset == 0)
		return aspi->ahb_base_phy;

	return aspi->ahb_base_phy + end_offset;
}

static u32 aspeed_spi_segment_ast2700_reg(struct aspeed_spi *aspi,
					  u64 start, u64 end)
{
	if (start == end)
		return 0;

	return (u32)((((start) >> 16) & 0x7fff) |
		     ((end + 1) & 0x7fff0000));
}

static const u32 aspeed_spi_hclk_divs[] = {
	/* HCLK, HCLK/2, HCLK/3, HCLK/4, HCLK/5, ..., HCLK/16 */
	0xf, 0x7, 0xe, 0x6, 0xd,
	0x5, 0xc, 0x4, 0xb, 0x3,
	0xa, 0x2, 0x9, 0x1, 0x8,
	0x0
};

#define ASPEED_SPI_HCLK_DIV(i) \
	(aspeed_spi_hclk_divs[(i)] << CTRL_FREQ_SEL_SHIFT)

/* Transfer maximum clock frequency to register setting */
static u32 apseed_get_clk_div_ast2400(struct aspeed_spi_chip *chip,
				      u32 max_hz)
{
	struct device *dev = chip->aspi->dev;
	u32 hclk_clk = chip->aspi->clk_freq;
	u32 hclk_div = 0;
	u32 i;
	bool found = false;

	/* FMC/SPIR10[11:8] */
	for (i = 0; i < ARRAY_SIZE(aspeed_spi_hclk_divs); i++) {
		if (hclk_clk / (i + 1) <= max_hz) {
			found = true;
			break;
		}
	}

	if (found) {
		hclk_div = ASPEED_SPI_HCLK_DIV(i);
		chip->clk_freq = hclk_clk / (i + 1);
	}

	dev_dbg(dev, "found: %s, hclk: %d, max_clk: %d\n",
		found ? "yes" : "no", hclk_clk, max_hz);

	if (found) {
		dev_dbg(dev, "h_div: %d (mask 0x%08x), speed: %d\n",
			i + 1, hclk_div, chip->clk_freq);
	}

	return hclk_div;
}

static u32 apseed_get_clk_div_ast2500(struct aspeed_spi_chip *chip,
				      u32 max_hz)
{
	struct device *dev = chip->aspi->dev;
	u32 hclk_clk = chip->aspi->clk_freq;
	u32 hclk_div = 0;
	u32 i;
	bool found = false;

	/* FMC/SPIR10[11:8] */
	for (i = 0; i < ARRAY_SIZE(aspeed_spi_hclk_divs); i++) {
		if (hclk_clk / (i + 1) <= max_hz) {
			found = true;
			chip->clk_freq = hclk_clk / (i + 1);
			break;
		}
	}

	if (found) {
		hclk_div = ASPEED_SPI_HCLK_DIV(i);
		goto end;
	}

	for (i = 0; i < ARRAY_SIZE(aspeed_spi_hclk_divs); i++) {
		if (hclk_clk / ((i + 1) * 4) <= max_hz) {
			found = true;
			chip->clk_freq = hclk_clk / ((i + 1) * 4);
			break;
		}
	}

	if (found)
		hclk_div = BIT(13) | ASPEED_SPI_HCLK_DIV(i);

end:
	dev_dbg(dev, "found: %s, hclk: %d, max_clk: %d\n",
		found ? "yes" : "no", hclk_clk, max_hz);

	if (found) {
		dev_dbg(dev, "h_div: %d (mask %x), speed: %d\n",
			i + 1, hclk_div, chip->clk_freq);
	}

	return hclk_div;
}

static u32 apseed_get_clk_div_ast2600(struct aspeed_spi_chip *chip,
				      u32 max_hz)
{
	struct device *dev = chip->aspi->dev;
	u32 hclk_clk = chip->aspi->clk_freq;
	u32 hclk_div = 0;
	u32 i, j;
	bool found = false;

	/* FMC/SPIR10[27:24] */
	for (j = 0; j < 16; j++) {
		/* FMC/SPIR10[11:8] */
		for (i = 0; i < ARRAY_SIZE(aspeed_spi_hclk_divs); i++) {
			if (i == 0 && j == 0)
				continue;

			if (hclk_clk / (i + 1 + (j * 16)) <= max_hz) {
				found = true;
				break;
			}
		}

		if (found) {
			hclk_div = ((j << 24) | ASPEED_SPI_HCLK_DIV(i));
			chip->clk_freq = hclk_clk / (i + 1 + j * 16);
			break;
		}
	}

	dev_dbg(dev, "found: %s, hclk: %d, max_clk: %d\n",
		found ? "yes" : "no", hclk_clk, max_hz);

	if (found) {
		dev_dbg(dev, "base_clk: %d, h_div: %d (mask %x), speed: %d\n",
			j, i + 1, hclk_div, chip->clk_freq);
	}

	return hclk_div;
}

/*
 * Read timing compensation sequences
 */

#define CALIBRATE_BUF_SIZE SZ_4K
#define CALIBRATE_REPEAT_COUNT 1

static bool aspeed_spi_check_reads(struct aspeed_spi_chip *chip,
				   const u8 *golden_buf, u8 *test_buf)
{
	int i;

	for (i = 0; i < CALIBRATE_REPEAT_COUNT; i++) {
		memcpy_fromio(test_buf, chip->ahb_base, CALIBRATE_BUF_SIZE);
		if (memcmp(test_buf, golden_buf, CALIBRATE_BUF_SIZE) != 0) {
#if defined(VERBOSE_DEBUG)
			print_hex_dump_bytes(DEVICE_NAME "  fail: ", DUMP_PREFIX_NONE,
					     test_buf, 0x100);
#endif
			return false;
		}
	}
	return true;
}

static inline u32 FREAD_TPASS(int i)
{
	return (((i) / 2) | (((i) & 1) ? 8 : 0));
}

/*
 * The timing register is shared by all devices. Only update for CE0.
 */
static int aspeed_spi_calibrate(struct aspeed_spi_chip *chip, u32 hdiv,
				const u8 *golden_buf, u8 *test_buf)
{
	struct aspeed_spi *aspi = chip->aspi;
	const struct aspeed_spi_data *data = aspi->data;
	int i;
	int good_pass = -1, pass_count = 0;
	u32 shift = (hdiv - 1) << 2;
	u32 mask = ~(0xfu << shift);
	u32 fread_timing_val = 0;

	/* Try HCLK delay 0..5, each one with/without delay and look for a
	 * good pair.
	 */
	for (i = 0; i < 12; i++) {
		bool pass;

		if (chip->cs == 0) {
			fread_timing_val &= mask;
			fread_timing_val |= FREAD_TPASS(i) << shift;
			writel(fread_timing_val, aspi->regs + data->timing);
		}
		pass = aspeed_spi_check_reads(chip, golden_buf, test_buf);
		dev_dbg(aspi->dev,
			"  * [%08x] %d HCLK delay, %dns DI delay : %s",
			fread_timing_val, i / 2, (i & 1) ? 4 : 0,
			pass ? "PASS" : "FAIL");
		if (pass) {
			pass_count++;
			if (pass_count == 3) {
				good_pass = i - 1;
				break;
			}
		} else {
			pass_count = 0;
		}
	}

	/* No good setting for this frequency */
	if (good_pass < 0)
		return -1;

	/* We have at least one pass of margin, let's use first pass */
	if (chip->cs == 0) {
		fread_timing_val &= mask;
		fread_timing_val |= FREAD_TPASS(good_pass) << shift;
		writel(fread_timing_val, aspi->regs + data->timing);
	}
	dev_dbg(aspi->dev, " * -> good is pass %d [0x%08x]",
		good_pass, fread_timing_val);
	return 0;
}

static bool aspeed_spi_check_calib_data(const u8 *test_buf, u32 size)
{
	const u32 *tb32 = (const u32 *)test_buf;
	u32 i, cnt = 0;

	/* We check if we have enough words that are neither all 0
	 * nor all 1's so the calibration can be considered valid.
	 *
	 * I use an arbitrary threshold for now of 64
	 */
	size >>= 2;
	for (i = 0; i < size; i++) {
		if (tb32[i] != 0 && tb32[i] != 0xffffffff)
			cnt++;
	}
	return cnt >= 64;
}

static int aspeed_spi_do_calibration(struct aspeed_spi_chip *chip)
{
	struct aspeed_spi *aspi = chip->aspi;
	const struct aspeed_spi_data *data = aspi->data;
	u32 ahb_freq = aspi->clk_freq;
	u32 max_freq = chip->clk_freq;
	u32 ctl_val;
	u8 *golden_buf = NULL;
	u8 *test_buf = NULL;
	int i, rc, best_div = -1;
	u32 clk_div_config = 0;
	u32 freq = 0;

	dev_dbg(aspi->dev, "calculate timing compensation - AHB freq: %d MHz",
		ahb_freq / 1000000);

	if ((aspi->flag & ASPEED_SPI_TIMING_CLB_DISABLED) != 0) {
		dev_info(aspi->dev, "timing calibration is disabled\n");
		return 0;
	}

	/*
	 * use the related low frequency to get check calibration data
	 * and get golden data.
	 */
	ctl_val = chip->ctl_val[ASPEED_SPI_READ] & data->hclk_mask;
	writel(ctl_val, chip->ctl);

	test_buf = kzalloc(CALIBRATE_BUF_SIZE * 2, GFP_KERNEL);
	if (!test_buf)
		return -ENOMEM;

	golden_buf = test_buf + CALIBRATE_BUF_SIZE;

	memcpy_fromio(golden_buf, chip->ahb_base, CALIBRATE_BUF_SIZE);
	if (!aspeed_spi_check_calib_data(golden_buf, CALIBRATE_BUF_SIZE)) {
		dev_info(aspi->dev, "Calibration area too uniform\n");
		goto no_calib;
	}

#if defined(VERBOSE_DEBUG)
	print_hex_dump_bytes(DEVICE_NAME "  good: ", DUMP_PREFIX_NONE,
			     golden_buf, 0x100);
#endif

	/* Now we iterate the HCLK dividers until we find our breaking point */
	for (i = data->hdiv_max; i <= 5; i++) {
		u32 tv;

		freq = ahb_freq / i;
		if (freq > max_freq)
			continue;

		/* Set the timing */
		tv = chip->ctl_val[ASPEED_SPI_READ] & data->hclk_mask;
		tv |= ASPEED_SPI_HCLK_DIV(i - 1);
		writel(tv, chip->ctl);
		dev_dbg(aspi->dev, "Trying HCLK/%d [%08x] ...", i, tv);
		rc = data->calibrate(chip, i, golden_buf, test_buf);
		if (rc == 0) {
			best_div = i;
			break;
		}
	}

no_calib:

	/* Nothing found ? */
	if (best_div < 0) {
		if (data->get_clk_div)
			clk_div_config = data->get_clk_div(chip, max_freq);
	} else {
		dev_dbg(aspi->dev, "Found good read timings at HCLK/%d", best_div);
		chip->clk_freq = freq;
		clk_div_config = ASPEED_SPI_HCLK_DIV(best_div - 1);
	}

	/* Record the freq */
	for (i = 0; i < ASPEED_SPI_MAX; i++)
		chip->ctl_val[i] = (chip->ctl_val[i] & data->hclk_mask) |
				   clk_div_config;

	writel(chip->ctl_val[ASPEED_SPI_READ], chip->ctl);

	kfree(test_buf);

	return 0;
}

static int get_mid_point_of_longest_one(u8 *buf, u32 len)
{
	int i;
	int start = 0, mid_point = 0;
	int max_cnt = 0, cnt = 0;

	for (i = 0; i < len; i++) {
		if (buf[i] == 1) {
			cnt++;
		} else {
			cnt = 0;
			start = i;
		}

		if (max_cnt < cnt) {
			max_cnt = cnt;
			mid_point = start + (cnt / 2);
		}
	}

	/*
	 * In order to get a stable SPI read timing,
	 * abandon the result if the length of longest
	 * consecutive good points is too short.
	 */
	if (max_cnt < 4)
		return -1;

	return mid_point;
}

#define TIMING_DELAY_DI		BIT(3)
#define TIMING_REG_AST2600(chip)				\
	((chip)->aspi->regs + (chip)->aspi->data->timing +	\
	 (chip)->cs * 4)

static int aspeed_spi_ast2600_calibrate(struct aspeed_spi_chip *chip, u32 hdiv,
					const u8 *golden_buf, u8 *test_buf)
{
	struct device *dev = chip->aspi->dev;
	struct aspeed_spi *aspi = chip->aspi;
	int hcycle;
	u32 shift = (hdiv - 2) << 3;
	u32 mask = ~(0xffu << shift);
	u32 fread_timing_val = 0;
	u8 *calib_res = NULL;
	int calib_point;
	u32 final_delay;
	int delay_ns;
	bool pass;

	calib_res = kzalloc(6 * 17, GFP_KERNEL);
	if (!calib_res)
		return -ENOMEM;

	for (hcycle = 0; hcycle <= 5; hcycle++) {
		fread_timing_val &= mask;
		fread_timing_val |= (TIMING_DELAY_DI | hcycle) << shift;

		for (delay_ns = 0; delay_ns < 16; delay_ns++) {
			fread_timing_val &= ~(0xfu << (4 + shift));
			fread_timing_val |= delay_ns << (4 + shift);

			writel(fread_timing_val, TIMING_REG_AST2600(chip));
			pass = aspeed_spi_check_reads(chip, golden_buf, test_buf);
			dev_dbg(aspi->dev,
				"  * [%08x] %d HCLK delay, DI delay %d.%dns : %s",
				fread_timing_val, hcycle, delay_ns / 2,
				(delay_ns & 1) ? 5 : 0, pass ? "PASS" : "FAIL");

			calib_res[hcycle * 17 + delay_ns] = pass;
		}
	}

	calib_point = get_mid_point_of_longest_one(calib_res, 6 * 17);

	if (calib_point < 0) {
		dev_info(dev, "[HCLK/%d] cannot get good calibration point.\n",
			 hdiv);
		kfree(calib_res);

		return -1;
	}

	hcycle = calib_point / 17;
	delay_ns = calib_point % 17;

	dev_dbg(dev, "final hcycle: %d, delay_ns: %d\n", hcycle,
		delay_ns);

	final_delay = (TIMING_DELAY_DI | hcycle | (delay_ns << 4)) << shift;
	writel(final_delay, TIMING_REG_AST2600(chip));

	kfree(calib_res);

	return 0;
}

void aspeed_spi_ast2600_fill_safs_cmd(struct aspeed_spi *aspi,
				      enum spi_mem_data_dir dir,
				      u8 cmd, u8 addr_len, u8 bus_width)
{
	u32 tmp_val;

	if (dir == SPI_MEM_DATA_IN) {
		tmp_val = readl(aspi->regs + HOST_DIRECT_ACCESS_CMD_CTRL4);
		if (addr_len == 4)
			tmp_val = (tmp_val & 0xffff00ff) | (cmd << 8);
		else
			tmp_val = (tmp_val & 0xffffff00) | cmd;

		tmp_val = (tmp_val & 0x0fffffff) | aspeed_spi_get_io_mode(bus_width);

		writel(tmp_val, aspi->regs + HOST_DIRECT_ACCESS_CMD_CTRL4);

	} else if (dir == SPI_MEM_DATA_OUT) {
		tmp_val = readl(aspi->regs + HOST_DIRECT_ACCESS_CMD_CTRL4);
		tmp_val = (tmp_val & 0xf0ffffff) |
			  (aspeed_spi_get_io_mode(bus_width) >> 4);

		writel(tmp_val, aspi->regs + HOST_DIRECT_ACCESS_CMD_CTRL4);

		tmp_val = readl(aspi->regs + HOST_DIRECT_ACCESS_CMD_CTRL2);
		if (addr_len == 4)
			tmp_val = (tmp_val & 0xffff00ff) | (cmd << 8);
		else
			tmp_val = (tmp_val & 0xffffff00) | cmd;

		writel(tmp_val, aspi->regs + HOST_DIRECT_ACCESS_CMD_CTRL2);
	}
}

/*
 * Platform definitions
 */
static const struct aspeed_spi_data ast2400_fmc_data = {
	.max_cs	       = 5,
	.hastype       = true,
	.we0	       = 16,
	.ctl0	       = CE0_CTRL_REG,
	.timing	       = CE0_TIMING_COMPENSATION_REG,
	.hclk_mask     = 0xfffff0ff,
	.hdiv_max      = 1,
	.min_window_sz = 0x800000,
	.get_clk_div   = apseed_get_clk_div_ast2400,
	.calibrate     = aspeed_spi_calibrate,
	.segment_start = aspeed_spi_segment_start,
	.segment_end   = aspeed_spi_segment_end,
	.segment_reg   = aspeed_spi_segment_reg,
	.adjust_window  = aspeed_adjust_window_ast2400,
};

static const struct aspeed_spi_data ast2400_spi_data = {
	.max_cs	       = 1,
	.hastype       = false,
	.we0	       = 0,
	.ctl0	       = 0x04,
	.timing	       = 0x14,
	.hclk_mask     = 0xfffff0ff,
	.hdiv_max      = 1,
	.get_clk_div   = apseed_get_clk_div_ast2400,
	.calibrate     = aspeed_spi_calibrate,
	/* No segment registers */
};

static const struct aspeed_spi_data ast2500_fmc_data = {
	.max_cs	       = 3,
	.hastype       = true,
	.we0	       = 16,
	.ctl0	       = CE0_CTRL_REG,
	.timing	       = CE0_TIMING_COMPENSATION_REG,
	.hclk_mask     = 0xffffd0ff,
	.hdiv_max      = 1,
	.min_window_sz = 0x800000,
	.get_clk_div   = apseed_get_clk_div_ast2500,
	.calibrate     = aspeed_spi_calibrate,
	.segment_start = aspeed_spi_segment_start,
	.segment_end   = aspeed_spi_segment_end,
	.segment_reg   = aspeed_spi_segment_reg,
	.adjust_window = aspeed_adjust_window_ast2500,
};

static const struct aspeed_spi_data ast2500_spi_data = {
	.max_cs	       = 2,
	.hastype       = false,
	.we0	       = 16,
	.ctl0	       = CE0_CTRL_REG,
	.timing	       = CE0_TIMING_COMPENSATION_REG,
	.hclk_mask     = 0xffffd0ff,
	.hdiv_max      = 1,
	.min_window_sz = 0x800000,
	.get_clk_div   = apseed_get_clk_div_ast2500,
	.calibrate     = aspeed_spi_calibrate,
	.segment_start = aspeed_spi_segment_start,
	.segment_end   = aspeed_spi_segment_end,
	.segment_reg   = aspeed_spi_segment_reg,
	.adjust_window = aspeed_adjust_window_ast2500,
};

static const struct aspeed_spi_data ast2600_fmc_data = {
	.max_cs	       = 3,
	.hastype       = false,
	.mode_bits     = SPI_RX_QUAD | SPI_TX_QUAD,
	.we0	       = 16,
	.ctl0	       = CE0_CTRL_REG,
	.timing	       = CE0_TIMING_COMPENSATION_REG,
	.hclk_mask     = 0xf0fff0ff,
	.hdiv_max      = 2,
	.min_window_sz = 0x200000,
	.get_clk_div   = apseed_get_clk_div_ast2600,
	.calibrate     = aspeed_spi_ast2600_calibrate,
	.segment_start = aspeed_spi_segment_ast2600_start,
	.segment_end   = aspeed_spi_segment_ast2600_end,
	.segment_reg   = aspeed_spi_segment_ast2600_reg,
	.adjust_window = aspeed_adjust_window_ast2600,
};

static const struct aspeed_spi_data ast2600_spi_data = {
	.max_cs	       = 2,
	.hastype       = false,
	.mode_bits     = SPI_RX_QUAD | SPI_TX_QUAD,
	.we0	       = 16,
	.ctl0	       = CE0_CTRL_REG,
	.timing	       = CE0_TIMING_COMPENSATION_REG,
	.hclk_mask     = 0xf0fff0ff,
	.hdiv_max      = 2,
	.min_window_sz = 0x200000,
	.get_clk_div   = apseed_get_clk_div_ast2600,
	.calibrate     = aspeed_spi_ast2600_calibrate,
	.segment_start = aspeed_spi_segment_ast2600_start,
	.segment_end   = aspeed_spi_segment_ast2600_end,
	.segment_reg   = aspeed_spi_segment_ast2600_reg,
	.adjust_window = aspeed_adjust_window_ast2600,
	.safs_support  = aspeed_spi_ast2600_fill_safs_cmd,
};

static const struct aspeed_spi_data ast2700_fmc_data = {
	.max_cs	       = 3,
	.hastype       = false,
	.mode_bits     = SPI_RX_QUAD | SPI_TX_QUAD,
	.we0	       = 16,
	.ctl0	       = CE0_CTRL_REG,
	.timing	       = CE0_TIMING_COMPENSATION_REG,
	.hclk_mask     = 0xf0fff0ff,
	.hdiv_max      = 2,
	.min_window_sz = 0x10000,
	.get_clk_div   = apseed_get_clk_div_ast2600,
	.calibrate     = aspeed_spi_ast2600_calibrate,
	.segment_start = aspeed_spi_segment_ast2700_start,
	.segment_end   = aspeed_spi_segment_ast2700_end,
	.segment_reg   = aspeed_spi_segment_ast2700_reg,
};

static const struct aspeed_spi_data ast2700_spi_data = {
	.max_cs	       = 2,
	.hastype       = false,
	.mode_bits     = SPI_RX_QUAD | SPI_TX_QUAD,
	.we0	       = 16,
	.ctl0	       = CE0_CTRL_REG,
	.timing	       = CE0_TIMING_COMPENSATION_REG,
	.hclk_mask     = 0xf0fff0ff,
	.hdiv_max      = 2,
	.min_window_sz = 0x10000,
	.get_clk_div   = apseed_get_clk_div_ast2600,
	.calibrate     = aspeed_spi_ast2600_calibrate,
	.segment_start = aspeed_spi_segment_ast2700_start,
	.segment_end   = aspeed_spi_segment_ast2700_end,
	.segment_reg   = aspeed_spi_segment_ast2700_reg,
};

static const struct of_device_id aspeed_spi_matches[] = {
	{ .compatible = "aspeed,ast2400-fmc", .data = &ast2400_fmc_data },
	{ .compatible = "aspeed,ast2400-spi", .data = &ast2400_spi_data },
	{ .compatible = "aspeed,ast2500-fmc", .data = &ast2500_fmc_data },
	{ .compatible = "aspeed,ast2500-spi", .data = &ast2500_spi_data },
	{ .compatible = "aspeed,ast2600-fmc", .data = &ast2600_fmc_data },
	{ .compatible = "aspeed,ast2600-spi", .data = &ast2600_spi_data },
	{ .compatible = "aspeed,ast2700-fmc", .data = &ast2700_fmc_data },
	{ .compatible = "aspeed,ast2700-spi", .data = &ast2700_spi_data },
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_spi_matches);

static struct platform_driver aspeed_spi_driver = {
	.probe			= aspeed_spi_probe,
	.remove_new		= aspeed_spi_remove,
	.driver	= {
		.name		= DEVICE_NAME,
		.of_match_table = aspeed_spi_matches,
	}
};

module_platform_driver(aspeed_spi_driver);

MODULE_DESCRIPTION("ASPEED Static Memory Controller Driver");
MODULE_AUTHOR("Chin-Ting Kuo <chin-ting_kuo@aspeedtech.com>");
MODULE_AUTHOR("Cedric Le Goater <clg@kaod.org>");
MODULE_LICENSE("GPL v2");
