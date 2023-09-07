// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ASPEED FMC/SPI Memory Controller Driver
 *
 * Copyright (c) 2015-2022, IBM Corporation.
 * Copyright (c) 2020, ASPEED Corporation.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#define DEVICE_NAME "spi-aspeed-smc"

/* Type setting Register */
#define CONFIG_REG			0x0
#define   CONFIG_TYPE_SPI		0x2

/* CE Control Register */
#define CE_CTRL_REG			0x4

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

#define   CTRL_IO_CMD_MASK		0xf0ff40c3

/* CEx Address Decoding Range Register */
#define CE0_SEGMENT_ADDR_REG		0x30

/* CEx Read timing compensation register */
#define CE0_TIMING_COMPENSATION_REG	0x94

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
	int (*calibrate)(struct aspeed_spi_chip *chip, u32 hdiv,
			 const u8 *golden_buf, u8 *test_buf);
};

#define ASPEED_SPI_MAX_NUM_CS	5

struct aspeed_spi {
	const struct aspeed_spi_data	*data;

	void __iomem		*regs;
	void __iomem		*ahb_base;
	phys_addr_t		 ahb_base_phy;
	size_t			 ahb_window_sz;
	u32			 num_cs;
	struct device		*dev;

	struct clk		*clk;
	u32			 clk_freq;

	struct aspeed_spi_chip	 chips[ASPEED_SPI_MAX_NUM_CS];
};

static u32 aspeed_spi_get_io_mode(const struct spi_mem_op *op)
{
	switch (op->data.buswidth) {
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

static int aspeed_spi_send_cmd_addr(struct aspeed_spi_chip *chip, u8 addr_nbytes,
				    u64 offset, u32 opcode)
{
	__be32 temp;
	u32 cmdaddr;

	switch (addr_nbytes) {
	case 3:
		cmdaddr = offset & 0xFFFFFF;
		cmdaddr |= opcode << 24;

		temp = cpu_to_be32(cmdaddr);
		aspeed_spi_write_to_ahb(chip->ahb_base, &temp, 4);
		break;
	case 4:
		temp = cpu_to_be32(offset);
		aspeed_spi_write_to_ahb(chip->ahb_base, &opcode, 1);
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
	int io_mode = aspeed_spi_get_io_mode(op);
	u8 dummy = 0xFF;
	int i;
	int ret;

	aspeed_spi_start_user(chip);

	ret = aspeed_spi_send_cmd_addr(chip, op->addr.nbytes, offset, op->cmd.opcode);
	if (ret < 0)
		return ret;

	if (op->dummy.buswidth && op->dummy.nbytes) {
		for (i = 0; i < op->dummy.nbytes / op->dummy.buswidth; i++)
			aspeed_spi_write_to_ahb(chip->ahb_base, &dummy,	sizeof(dummy));
	}

	aspeed_spi_set_io_mode(chip, io_mode);

	aspeed_spi_read_from_ahb(buf, chip->ahb_base, len);
	aspeed_spi_stop_user(chip);
	return 0;
}

static ssize_t aspeed_spi_write_user(struct aspeed_spi_chip *chip,
				     const struct spi_mem_op *op)
{
	int ret;
	int io_mode = aspeed_spi_get_io_mode(op);

	aspeed_spi_start_user(chip);
	ret = aspeed_spi_send_cmd_addr(chip, op->addr.nbytes, op->addr.val, op->cmd.opcode);
	if (ret < 0)
		return ret;

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
	u32 addr_mode, addr_mode_backup;
	u32 ctl_val;
	int ret = 0;

	dev_dbg(aspi->dev,
		"CE%d %s OP %#x mode:%d.%d.%d.%d naddr:%#x ndummies:%#x len:%#x",
		chip->cs, op->data.dir == SPI_MEM_DATA_IN ? "read" : "write",
		op->cmd.opcode, op->cmd.buswidth, op->addr.buswidth,
		op->dummy.buswidth, op->data.buswidth,
		op->addr.nbytes, op->dummy.nbytes, op->data.nbytes);

	addr_mode = readl(aspi->regs + CE_CTRL_REG);
	addr_mode_backup = addr_mode;

	ctl_val = chip->ctl_val[ASPEED_SPI_BASE];
	ctl_val &= ~CTRL_IO_CMD_MASK;

	ctl_val |= op->cmd.opcode << CTRL_COMMAND_SHIFT;

	/* 4BYTE address mode */
	if (op->addr.nbytes) {
		if (op->addr.nbytes == 4)
			addr_mode |= (0x11 << chip->cs);
		else
			addr_mode &= ~(0x11 << chip->cs);

		if (op->addr.nbytes == 4 && chip->aspi->data == &ast2400_spi_data)
			ctl_val |= CTRL_IO_ADDRESS_4B;
	}

	if (op->dummy.nbytes)
		ctl_val |= CTRL_IO_DUMMY_SET(op->dummy.nbytes / op->dummy.buswidth);

	if (op->data.nbytes)
		ctl_val |= aspeed_spi_get_io_mode(op);

	if (op->data.dir == SPI_MEM_DATA_OUT)
		ctl_val |= CTRL_IO_MODE_WRITE;
	else
		ctl_val |= CTRL_IO_MODE_READ;

	if (addr_mode != addr_mode_backup)
		writel(addr_mode, aspi->regs + CE_CTRL_REG);
	writel(ctl_val, chip->ctl);

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
	if (addr_mode != addr_mode_backup)
		writel(addr_mode_backup, aspi->regs + CE_CTRL_REG);
	writel(chip->ctl_val[ASPEED_SPI_READ], chip->ctl);
	return ret;
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

static int aspeed_spi_set_window(struct aspeed_spi *aspi)
{
	off_t offset = 0;
	phys_addr_t start;
	phys_addr_t end;
	void __iomem *seg_reg_base = aspi->regs + CE0_SEGMENT_ADDR_REG;
	void __iomem *seg_reg;
	u32 seg_val_backup;
	u32 seg_val;
	u32 cs;
	size_t win_sz;

	for (cs = 0; cs < aspi->data->max_cs; cs++) {
		seg_reg = seg_reg_base + cs * 4;
		seg_val_backup = readl(seg_reg);

		start = aspi->ahb_base_phy + offset;
		win_sz = aspi->chips[cs].ahb_window_sz;
		end = start + win_sz;

		seg_val = aspi->data->segment_reg(aspi, start, end);

		if (seg_val == seg_val_backup) {
			aspi->chips[cs].ahb_base = aspi->ahb_base + offset;
			offset += win_sz;
			continue;
		}

		writel(seg_val, seg_reg);

		/*
		 * Restore initial value if something goes wrong else we could
		 * loose access to the chip.
		 */
		if (seg_val != readl(seg_reg)) {
			dev_err(aspi->dev, "CE%d invalid window [ 0x%.9llx - 0x%.9llx ] %zdMB",
				cs, (u64)start, (u64)end - 1, win_sz >> 20);
			writel(seg_val_backup, seg_reg);
			return -EIO;
		}

		if (win_sz != 0)
			dev_dbg(aspi->dev, "CE%d new window [ 0x%.9llx - 0x%.9llx ] %zdMB",
				cs, (u64)start, (u64)end - 1,  win_sz >> 20);
		else
			dev_dbg(aspi->dev, "CE%d window closed", cs);

		aspi->chips[cs].ahb_base = aspi->ahb_base + offset;
		offset += win_sz;
	}

	return 0;
}

static const struct aspeed_spi_data ast2500_spi_data;
static const struct aspeed_spi_data ast2600_spi_data;
static const struct aspeed_spi_data ast2600_fmc_data;

/*
 * Usually, the decoding address is not configured at the u-boot stage.
 * Or, the existing decoding address configuration is wrong.
 * Thus, force to assign a default decoding address during driver probe.
 */
static void aspeed_spi_chip_set_default_window(struct aspeed_spi *aspi)
{
	u32 cs;

	/* No segment registers for the AST2400 SPI controller */
	if (aspi->data == &ast2400_spi_data) {
		aspi->chips[0].ahb_base = aspi->ahb_base;
		aspi->chips[0].ahb_window_sz = aspi->ahb_window_sz;
		return;
	}

	for (cs = 0; cs < aspi->num_cs; cs++) {
		if (cs == 0)
			aspi->chips[cs].ahb_base = aspi->ahb_base;
		else
			aspi->chips[cs].ahb_base =
				aspi->chips[cs - 1].ahb_base +
				aspi->chips[cs - 1].ahb_window_sz;

		aspi->chips[cs].ahb_window_sz = aspi->data->min_window_sz;

		dev_dbg(aspi->dev, "CE%d default window [ 0x%.9llx - 0x%.9llx ]",
			cs, (u64)(aspi->ahb_base_phy +
				  aspi->data->min_window_sz * cs),
			(u64)(aspi->ahb_base_phy +
			      aspi->data->min_window_sz * (cs + 1) - 1));
	}

	/* Close unused CS */
	for (cs = aspi->num_cs; cs < aspi->data->max_cs; cs++) {
		aspi->chips[cs].ahb_base = aspi->ahb_base;
		aspi->chips[cs].ahb_window_sz = 0;
	}

	aspeed_spi_set_window(aspi);
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
	for (cs = aspi->num_cs; cs < aspi->data->max_cs; cs++)
		chips[cs].ahb_window_sz = aspi->data->min_window_sz;

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
	int ret = 0;

	dev_dbg(aspi->dev,
		"CE%d %s dirmap [ 0x%.8llx - 0x%.8llx ] OP %#x mode:%d.%d.%d.%d naddr:%#x ndummies:%#x\n",
		chip->cs, op->data.dir == SPI_MEM_DATA_IN ? "read" : "write",
		desc->info.offset, desc->info.offset + desc->info.length,
		op->cmd.opcode, op->cmd.buswidth, op->addr.buswidth,
		op->dummy.buswidth, op->data.buswidth,
		op->addr.nbytes, op->dummy.nbytes);

	chip->clk_freq = desc->mem->spi->max_speed_hz;

	/* Only for reads */
	if (op->data.dir != SPI_MEM_DATA_IN)
		return -EOPNOTSUPP;

	aspeed_spi_chip_adjust_window(chip, desc->info.length);

	if (desc->info.length > chip->ahb_window_sz)
		dev_warn(aspi->dev, "CE%d window (%zdMB) too small for mapping",
			 chip->cs, chip->ahb_window_sz >> 20);

	/* Define the default IO read settings */
	ctl_val = readl(chip->ctl) & ~CTRL_IO_CMD_MASK;
	ctl_val |= aspeed_spi_get_io_mode(op) |
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

	ret = aspeed_spi_do_calibration(chip);

	dev_info(aspi->dev, "CE%d read buswidth:%d [0x%08x]\n",
		 chip->cs, op->data.buswidth, chip->ctl_val[ASPEED_SPI_READ]);

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

	chip->aspi = aspi;
	chip->cs = cs;
	chip->ctl = aspi->regs + data->ctl0 + cs * 4;

	/* The driver only supports SPI type flash */
	if (data->hastype)
		aspeed_spi_chip_set_type(aspi, cs, CONFIG_TYPE_SPI);

	aspeed_spi_chip_enable(aspi, cs, true);

	chip->ctl_val[ASPEED_SPI_BASE] = CTRL_CE_STOP_ACTIVE | CTRL_IO_MODE_USER;

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
	int ret;

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

	aspi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(aspi->regs))
		return PTR_ERR(aspi->regs);

	aspi->ahb_base = devm_platform_get_and_ioremap_resource(pdev, 1, &res);
	if (IS_ERR(aspi->ahb_base)) {
		dev_err(dev, "missing AHB mapping window\n");
		return PTR_ERR(aspi->ahb_base);
	}

	aspi->ahb_window_sz = resource_size(res);
	aspi->ahb_base_phy = res->start;

	aspi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(aspi->clk)) {
		dev_err(dev, "missing clock\n");
		return PTR_ERR(aspi->clk);
	}

	aspi->clk_freq = clk_get_rate(aspi->clk);
	if (!aspi->clk_freq) {
		dev_err(dev, "invalid clock\n");
		return -EINVAL;
	}

	ret = clk_prepare_enable(aspi->clk);
	if (ret) {
		dev_err(dev, "can not enable the clock\n");
		return ret;
	}

	/* IRQ is for DMA, which the driver doesn't support yet */

	ctlr->mode_bits = SPI_RX_DUAL | SPI_TX_DUAL | data->mode_bits;
	ctlr->bus_num = pdev->id;
	ctlr->mem_ops = &aspeed_spi_mem_ops;
	ctlr->setup = aspeed_spi_setup;
	ctlr->cleanup = aspeed_spi_cleanup;
	ctlr->num_chipselect = of_get_available_child_count(dev->of_node);
	ctlr->dev.of_node = dev->of_node;

	aspi->num_cs = ctlr->num_chipselect;

	aspeed_spi_chip_set_default_window(aspi);

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret) {
		dev_err(&pdev->dev, "spi_register_controller failed\n");
		goto disable_clk;
	}
	return 0;

disable_clk:
	clk_disable_unprepare(aspi->clk);
	return ret;
}

static void aspeed_spi_remove(struct platform_device *pdev)
{
	struct aspeed_spi *aspi = platform_get_drvdata(pdev);

	aspeed_spi_enable(aspi, false);
	clk_disable_unprepare(aspi->clk);
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
	return (u32)((((start) >> 16) & 0xffff) |
		     ((end + 1) & 0xffff0000));
}

/*
 * Read timing compensation sequences
 */

#define CALIBRATE_BUF_SIZE SZ_16K

static bool aspeed_spi_check_reads(struct aspeed_spi_chip *chip,
				   const u8 *golden_buf, u8 *test_buf)
{
	int i;

	for (i = 0; i < 10; i++) {
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

static const u32 aspeed_spi_hclk_divs[] = {
	0xf, /* HCLK */
	0x7, /* HCLK/2 */
	0xe, /* HCLK/3 */
	0x6, /* HCLK/4 */
	0xd, /* HCLK/5 */
};

#define ASPEED_SPI_HCLK_DIV(i) \
	(aspeed_spi_hclk_divs[(i) - 1] << CTRL_FREQ_SEL_SHIFT)

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

	dev_dbg(aspi->dev, "calculate timing compensation - AHB freq: %d MHz",
		ahb_freq / 1000000);

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
		dev_info(aspi->dev, "Calibration area too uniform, using low speed");
		goto no_calib;
	}

#if defined(VERBOSE_DEBUG)
	print_hex_dump_bytes(DEVICE_NAME "  good: ", DUMP_PREFIX_NONE,
			     golden_buf, 0x100);
#endif

	/* Now we iterate the HCLK dividers until we find our breaking point */
	for (i = data->hdiv_max; i <= ARRAY_SIZE(aspeed_spi_hclk_divs); i++) {
		u32 tv, freq;

		freq = ahb_freq / i;
		if (freq > max_freq)
			continue;

		/* Set the timing */
		tv = chip->ctl_val[ASPEED_SPI_READ] | ASPEED_SPI_HCLK_DIV(i - 1);
		writel(tv, chip->ctl);
		dev_dbg(aspi->dev, "Trying HCLK/%d [%08x] ...", i, tv);
		rc = data->calibrate(chip, i, golden_buf, test_buf);
		if (rc == 0) {
			best_div = i;
			break;
		}
	}

	/* Nothing found ? */
	if (best_div < 0) {
		dev_warn(aspi->dev, "No good frequency, using dumb slow");
	} else {
		dev_dbg(aspi->dev, "Found good read timings at HCLK/%d", best_div);

		/* Record the freq */
		for (i = 0; i < ASPEED_SPI_MAX; i++)
			chip->ctl_val[i] = (chip->ctl_val[i] & data->hclk_mask) |
				ASPEED_SPI_HCLK_DIV(best_div);
	}

no_calib:
	writel(chip->ctl_val[ASPEED_SPI_READ], chip->ctl);
	kfree(test_buf);
	return 0;
}

#define TIMING_DELAY_DI		BIT(3)
#define TIMING_DELAY_HCYCLE_MAX	5
#define TIMING_REG_AST2600(chip)				\
	((chip)->aspi->regs + (chip)->aspi->data->timing +	\
	 (chip)->cs * 4)

static int aspeed_spi_ast2600_calibrate(struct aspeed_spi_chip *chip, u32 hdiv,
					const u8 *golden_buf, u8 *test_buf)
{
	struct aspeed_spi *aspi = chip->aspi;
	int hcycle;
	u32 shift = (hdiv - 2) << 3;
	u32 mask = ~(0xfu << shift);
	u32 fread_timing_val = 0;

	for (hcycle = 0; hcycle <= TIMING_DELAY_HCYCLE_MAX; hcycle++) {
		int delay_ns;
		bool pass = false;

		fread_timing_val &= mask;
		fread_timing_val |= hcycle << shift;

		/* no DI input delay first  */
		writel(fread_timing_val, TIMING_REG_AST2600(chip));
		pass = aspeed_spi_check_reads(chip, golden_buf, test_buf);
		dev_dbg(aspi->dev,
			"  * [%08x] %d HCLK delay, DI delay none : %s",
			fread_timing_val, hcycle, pass ? "PASS" : "FAIL");
		if (pass)
			return 0;

		/* Add DI input delays  */
		fread_timing_val &= mask;
		fread_timing_val |= (TIMING_DELAY_DI | hcycle) << shift;

		for (delay_ns = 0; delay_ns < 0x10; delay_ns++) {
			fread_timing_val &= ~(0xf << (4 + shift));
			fread_timing_val |= delay_ns << (4 + shift);

			writel(fread_timing_val, TIMING_REG_AST2600(chip));
			pass = aspeed_spi_check_reads(chip, golden_buf, test_buf);
			dev_dbg(aspi->dev,
				"  * [%08x] %d HCLK delay, DI delay %d.%dns : %s",
				fread_timing_val, hcycle, (delay_ns + 1) / 2,
				(delay_ns + 1) & 1 ? 5 : 5, pass ? "PASS" : "FAIL");
			/*
			 * TODO: This is optimistic. We should look
			 * for a working interval and save the middle
			 * value in the read timing register.
			 */
			if (pass)
				return 0;
		}
	}

	/* No good setting for this frequency */
	return -1;
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
	.calibrate     = aspeed_spi_ast2600_calibrate,
	.segment_start = aspeed_spi_segment_ast2600_start,
	.segment_end   = aspeed_spi_segment_ast2600_end,
	.segment_reg   = aspeed_spi_segment_ast2600_reg,
	.adjust_window = aspeed_adjust_window_ast2600,
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
