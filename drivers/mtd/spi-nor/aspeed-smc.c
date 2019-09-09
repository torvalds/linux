// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ASPEED Static Memory Controller driver
 *
 * Copyright (c) 2015-2016, IBM Corporation.
 */

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/spi-nor.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/sizes.h>
#include <linux/sysfs.h>

#define DEVICE_NAME	"aspeed-smc"

/*
 * The driver only support SPI flash
 */
enum aspeed_smc_flash_type {
	smc_type_nor  = 0,
	smc_type_nand = 1,
	smc_type_spi  = 2,
};

struct aspeed_smc_chip;

struct aspeed_smc_info {
	u32 maxsize;		/* maximum size of chip window */
	u8 nce;			/* number of chip enables */
	bool hastype;		/* flash type field exists in config reg */
	u8 we0;			/* shift for write enable bit for CE0 */
	u8 ctl0;		/* offset in regs of ctl for CE0 */

	void (*set_4b)(struct aspeed_smc_chip *chip);
};

static void aspeed_smc_chip_set_4b_spi_2400(struct aspeed_smc_chip *chip);
static void aspeed_smc_chip_set_4b(struct aspeed_smc_chip *chip);

static const struct aspeed_smc_info fmc_2400_info = {
	.maxsize = 64 * 1024 * 1024,
	.nce = 5,
	.hastype = true,
	.we0 = 16,
	.ctl0 = 0x10,
	.set_4b = aspeed_smc_chip_set_4b,
};

static const struct aspeed_smc_info spi_2400_info = {
	.maxsize = 64 * 1024 * 1024,
	.nce = 1,
	.hastype = false,
	.we0 = 0,
	.ctl0 = 0x04,
	.set_4b = aspeed_smc_chip_set_4b_spi_2400,
};

static const struct aspeed_smc_info fmc_2500_info = {
	.maxsize = 256 * 1024 * 1024,
	.nce = 3,
	.hastype = true,
	.we0 = 16,
	.ctl0 = 0x10,
	.set_4b = aspeed_smc_chip_set_4b,
};

static const struct aspeed_smc_info spi_2500_info = {
	.maxsize = 128 * 1024 * 1024,
	.nce = 2,
	.hastype = false,
	.we0 = 16,
	.ctl0 = 0x10,
	.set_4b = aspeed_smc_chip_set_4b,
};

enum aspeed_smc_ctl_reg_value {
	smc_base,		/* base value without mode for other commands */
	smc_read,		/* command reg for (maybe fast) reads */
	smc_write,		/* command reg for writes */
	smc_max,
};

struct aspeed_smc_controller;

struct aspeed_smc_chip {
	int cs;
	struct aspeed_smc_controller *controller;
	void __iomem *ctl;			/* control register */
	void __iomem *ahb_base;			/* base of chip window */
	u32 ahb_window_size;			/* chip mapping window size */
	u32 ctl_val[smc_max];			/* control settings */
	enum aspeed_smc_flash_type type;	/* what type of flash */
	struct spi_nor nor;
};

struct aspeed_smc_controller {
	struct device *dev;

	struct mutex mutex;			/* controller access mutex */
	const struct aspeed_smc_info *info;	/* type info of controller */
	void __iomem *regs;			/* controller registers */
	void __iomem *ahb_base;			/* per-chip windows resource */
	u32 ahb_window_size;			/* full mapping window size */

	struct aspeed_smc_chip *chips[0];	/* pointers to attached chips */
};

/*
 * SPI Flash Configuration Register (AST2500 SPI)
 *     or
 * Type setting Register (AST2500 FMC).
 * CE0 and CE1 can only be of type SPI. CE2 can be of type NOR but the
 * driver does not support it.
 */
#define CONFIG_REG			0x0
#define CONFIG_DISABLE_LEGACY		BIT(31) /* 1 */

#define CONFIG_CE2_WRITE		BIT(18)
#define CONFIG_CE1_WRITE		BIT(17)
#define CONFIG_CE0_WRITE		BIT(16)

#define CONFIG_CE2_TYPE			BIT(4) /* AST2500 FMC only */
#define CONFIG_CE1_TYPE			BIT(2) /* AST2500 FMC only */
#define CONFIG_CE0_TYPE			BIT(0) /* AST2500 FMC only */

/*
 * CE Control Register
 */
#define CE_CONTROL_REG			0x4

/*
 * CEx Control Register
 */
#define CONTROL_AAF_MODE		BIT(31)
#define CONTROL_IO_MODE_MASK		GENMASK(30, 28)
#define CONTROL_IO_DUAL_DATA		BIT(29)
#define CONTROL_IO_DUAL_ADDR_DATA	(BIT(29) | BIT(28))
#define CONTROL_IO_QUAD_DATA		BIT(30)
#define CONTROL_IO_QUAD_ADDR_DATA	(BIT(30) | BIT(28))
#define CONTROL_CE_INACTIVE_SHIFT	24
#define CONTROL_CE_INACTIVE_MASK	GENMASK(27, \
					CONTROL_CE_INACTIVE_SHIFT)
/* 0 = 16T ... 15 = 1T   T=HCLK */
#define CONTROL_COMMAND_SHIFT		16
#define CONTROL_DUMMY_COMMAND_OUT	BIT(15)
#define CONTROL_IO_DUMMY_HI		BIT(14)
#define CONTROL_IO_DUMMY_HI_SHIFT	14
#define CONTROL_CLK_DIV4		BIT(13) /* others */
#define CONTROL_IO_ADDRESS_4B		BIT(13) /* AST2400 SPI */
#define CONTROL_RW_MERGE		BIT(12)
#define CONTROL_IO_DUMMY_LO_SHIFT	6
#define CONTROL_IO_DUMMY_LO		GENMASK(7, \
						CONTROL_IO_DUMMY_LO_SHIFT)
#define CONTROL_IO_DUMMY_MASK		(CONTROL_IO_DUMMY_HI | \
					 CONTROL_IO_DUMMY_LO)
#define CONTROL_IO_DUMMY_SET(dummy)				 \
	(((((dummy) >> 2) & 0x1) << CONTROL_IO_DUMMY_HI_SHIFT) | \
	 (((dummy) & 0x3) << CONTROL_IO_DUMMY_LO_SHIFT))

#define CONTROL_CLOCK_FREQ_SEL_SHIFT	8
#define CONTROL_CLOCK_FREQ_SEL_MASK	GENMASK(11, \
						CONTROL_CLOCK_FREQ_SEL_SHIFT)
#define CONTROL_LSB_FIRST		BIT(5)
#define CONTROL_CLOCK_MODE_3		BIT(4)
#define CONTROL_IN_DUAL_DATA		BIT(3)
#define CONTROL_CE_STOP_ACTIVE_CONTROL	BIT(2)
#define CONTROL_COMMAND_MODE_MASK	GENMASK(1, 0)
#define CONTROL_COMMAND_MODE_NORMAL	0
#define CONTROL_COMMAND_MODE_FREAD	1
#define CONTROL_COMMAND_MODE_WRITE	2
#define CONTROL_COMMAND_MODE_USER	3

#define CONTROL_KEEP_MASK						\
	(CONTROL_AAF_MODE | CONTROL_CE_INACTIVE_MASK | CONTROL_CLK_DIV4 | \
	 CONTROL_CLOCK_FREQ_SEL_MASK | CONTROL_LSB_FIRST | CONTROL_CLOCK_MODE_3)

/*
 * The Segment Register uses a 8MB unit to encode the start address
 * and the end address of the mapping window of a flash SPI slave :
 *
 *        | byte 1 | byte 2 | byte 3 | byte 4 |
 *        +--------+--------+--------+--------+
 *        |  end   |  start |   0    |   0    |
 */
#define SEGMENT_ADDR_REG0		0x30
#define SEGMENT_ADDR_START(_r)		((((_r) >> 16) & 0xFF) << 23)
#define SEGMENT_ADDR_END(_r)		((((_r) >> 24) & 0xFF) << 23)
#define SEGMENT_ADDR_VALUE(start, end)					\
	(((((start) >> 23) & 0xFF) << 16) | ((((end) >> 23) & 0xFF) << 24))
#define SEGMENT_ADDR_REG(controller, cs)	\
	((controller)->regs + SEGMENT_ADDR_REG0 + (cs) * 4)

/*
 * In user mode all data bytes read or written to the chip decode address
 * range are transferred to or from the SPI bus. The range is treated as a
 * fifo of arbitratry 1, 2, or 4 byte width but each write has to be aligned
 * to its size. The address within the multiple 8kB range is ignored when
 * sending bytes to the SPI bus.
 *
 * On the arm architecture, as of Linux version 4.3, memcpy_fromio and
 * memcpy_toio on little endian targets use the optimized memcpy routines
 * that were designed for well behavied memory storage. These routines
 * have a stutter if the source and destination are not both word aligned,
 * once with a duplicate access to the source after aligning to the
 * destination to a word boundary, and again with a duplicate access to
 * the source when the final byte count is not word aligned.
 *
 * When writing or reading the fifo this stutter discards data or sends
 * too much data to the fifo and can not be used by this driver.
 *
 * While the low level io string routines that implement the insl family do
 * the desired accesses and memory increments, the cross architecture io
 * macros make them essentially impossible to use on a memory mapped address
 * instead of a a token from the call to iomap of an io port.
 *
 * These fifo routines use readl and friends to a constant io port and update
 * the memory buffer pointer and count via explicit code. The final updates
 * to len are optimistically suppressed.
 */
static int aspeed_smc_read_from_ahb(void *buf, void __iomem *src, size_t len)
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

static int aspeed_smc_write_to_ahb(void __iomem *dst, const void *buf,
				   size_t len)
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

static inline u32 aspeed_smc_chip_write_bit(struct aspeed_smc_chip *chip)
{
	return BIT(chip->controller->info->we0 + chip->cs);
}

static void aspeed_smc_chip_check_config(struct aspeed_smc_chip *chip)
{
	struct aspeed_smc_controller *controller = chip->controller;
	u32 reg;

	reg = readl(controller->regs + CONFIG_REG);

	if (reg & aspeed_smc_chip_write_bit(chip))
		return;

	dev_dbg(controller->dev, "config write is not set ! @%p: 0x%08x\n",
		controller->regs + CONFIG_REG, reg);
	reg |= aspeed_smc_chip_write_bit(chip);
	writel(reg, controller->regs + CONFIG_REG);
}

static void aspeed_smc_start_user(struct spi_nor *nor)
{
	struct aspeed_smc_chip *chip = nor->priv;
	u32 ctl = chip->ctl_val[smc_base];

	/*
	 * When the chip is controlled in user mode, we need write
	 * access to send the opcodes to it. So check the config.
	 */
	aspeed_smc_chip_check_config(chip);

	ctl |= CONTROL_COMMAND_MODE_USER |
		CONTROL_CE_STOP_ACTIVE_CONTROL;
	writel(ctl, chip->ctl);

	ctl &= ~CONTROL_CE_STOP_ACTIVE_CONTROL;
	writel(ctl, chip->ctl);
}

static void aspeed_smc_stop_user(struct spi_nor *nor)
{
	struct aspeed_smc_chip *chip = nor->priv;

	u32 ctl = chip->ctl_val[smc_read];
	u32 ctl2 = ctl | CONTROL_COMMAND_MODE_USER |
		CONTROL_CE_STOP_ACTIVE_CONTROL;

	writel(ctl2, chip->ctl);	/* stop user CE control */
	writel(ctl, chip->ctl);		/* default to fread or read mode */
}

static int aspeed_smc_prep(struct spi_nor *nor, enum spi_nor_ops ops)
{
	struct aspeed_smc_chip *chip = nor->priv;

	mutex_lock(&chip->controller->mutex);
	return 0;
}

static void aspeed_smc_unprep(struct spi_nor *nor, enum spi_nor_ops ops)
{
	struct aspeed_smc_chip *chip = nor->priv;

	mutex_unlock(&chip->controller->mutex);
}

static int aspeed_smc_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct aspeed_smc_chip *chip = nor->priv;

	aspeed_smc_start_user(nor);
	aspeed_smc_write_to_ahb(chip->ahb_base, &opcode, 1);
	aspeed_smc_read_from_ahb(buf, chip->ahb_base, len);
	aspeed_smc_stop_user(nor);
	return 0;
}

static int aspeed_smc_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf,
				int len)
{
	struct aspeed_smc_chip *chip = nor->priv;

	aspeed_smc_start_user(nor);
	aspeed_smc_write_to_ahb(chip->ahb_base, &opcode, 1);
	aspeed_smc_write_to_ahb(chip->ahb_base, buf, len);
	aspeed_smc_stop_user(nor);
	return 0;
}

static void aspeed_smc_send_cmd_addr(struct spi_nor *nor, u8 cmd, u32 addr)
{
	struct aspeed_smc_chip *chip = nor->priv;
	__be32 temp;
	u32 cmdaddr;

	switch (nor->addr_width) {
	default:
		WARN_ONCE(1, "Unexpected address width %u, defaulting to 3\n",
			  nor->addr_width);
		/* FALLTHROUGH */
	case 3:
		cmdaddr = addr & 0xFFFFFF;
		cmdaddr |= cmd << 24;

		temp = cpu_to_be32(cmdaddr);
		aspeed_smc_write_to_ahb(chip->ahb_base, &temp, 4);
		break;
	case 4:
		temp = cpu_to_be32(addr);
		aspeed_smc_write_to_ahb(chip->ahb_base, &cmd, 1);
		aspeed_smc_write_to_ahb(chip->ahb_base, &temp, 4);
		break;
	}
}

static ssize_t aspeed_smc_read_user(struct spi_nor *nor, loff_t from,
				    size_t len, u_char *read_buf)
{
	struct aspeed_smc_chip *chip = nor->priv;
	int i;
	u8 dummy = 0xFF;

	aspeed_smc_start_user(nor);
	aspeed_smc_send_cmd_addr(nor, nor->read_opcode, from);
	for (i = 0; i < chip->nor.read_dummy / 8; i++)
		aspeed_smc_write_to_ahb(chip->ahb_base, &dummy, sizeof(dummy));

	aspeed_smc_read_from_ahb(read_buf, chip->ahb_base, len);
	aspeed_smc_stop_user(nor);
	return len;
}

static ssize_t aspeed_smc_write_user(struct spi_nor *nor, loff_t to,
				     size_t len, const u_char *write_buf)
{
	struct aspeed_smc_chip *chip = nor->priv;

	aspeed_smc_start_user(nor);
	aspeed_smc_send_cmd_addr(nor, nor->program_opcode, to);
	aspeed_smc_write_to_ahb(chip->ahb_base, write_buf, len);
	aspeed_smc_stop_user(nor);
	return len;
}

static int aspeed_smc_unregister(struct aspeed_smc_controller *controller)
{
	struct aspeed_smc_chip *chip;
	int n;

	for (n = 0; n < controller->info->nce; n++) {
		chip = controller->chips[n];
		if (chip)
			mtd_device_unregister(&chip->nor.mtd);
	}

	return 0;
}

static int aspeed_smc_remove(struct platform_device *dev)
{
	return aspeed_smc_unregister(platform_get_drvdata(dev));
}

static const struct of_device_id aspeed_smc_matches[] = {
	{ .compatible = "aspeed,ast2400-fmc", .data = &fmc_2400_info },
	{ .compatible = "aspeed,ast2400-spi", .data = &spi_2400_info },
	{ .compatible = "aspeed,ast2500-fmc", .data = &fmc_2500_info },
	{ .compatible = "aspeed,ast2500-spi", .data = &spi_2500_info },
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_smc_matches);

/*
 * Each chip has a mapping window defined by a segment address
 * register defining a start and an end address on the AHB bus. These
 * addresses can be configured to fit the chip size and offer a
 * contiguous memory region across chips. For the moment, we only
 * check that each chip segment is valid.
 */
static void __iomem *aspeed_smc_chip_base(struct aspeed_smc_chip *chip,
					  struct resource *res)
{
	struct aspeed_smc_controller *controller = chip->controller;
	u32 offset = 0;
	u32 reg;

	if (controller->info->nce > 1) {
		reg = readl(SEGMENT_ADDR_REG(controller, chip->cs));

		if (SEGMENT_ADDR_START(reg) >= SEGMENT_ADDR_END(reg))
			return NULL;

		offset = SEGMENT_ADDR_START(reg) - res->start;
	}

	return controller->ahb_base + offset;
}

static u32 aspeed_smc_ahb_base_phy(struct aspeed_smc_controller *controller)
{
	u32 seg0_val = readl(SEGMENT_ADDR_REG(controller, 0));

	return SEGMENT_ADDR_START(seg0_val);
}

static u32 chip_set_segment(struct aspeed_smc_chip *chip, u32 cs, u32 start,
			    u32 size)
{
	struct aspeed_smc_controller *controller = chip->controller;
	void __iomem *seg_reg;
	u32 seg_oldval, seg_newval, ahb_base_phy, end;

	ahb_base_phy = aspeed_smc_ahb_base_phy(controller);

	seg_reg = SEGMENT_ADDR_REG(controller, cs);
	seg_oldval = readl(seg_reg);

	/*
	 * If the chip size is not specified, use the default segment
	 * size, but take into account the possible overlap with the
	 * previous segment
	 */
	if (!size)
		size = SEGMENT_ADDR_END(seg_oldval) - start;

	/*
	 * The segment cannot exceed the maximum window size of the
	 * controller.
	 */
	if (start + size > ahb_base_phy + controller->ahb_window_size) {
		size = ahb_base_phy + controller->ahb_window_size - start;
		dev_warn(chip->nor.dev, "CE%d window resized to %dMB",
			 cs, size >> 20);
	}

	end = start + size;
	seg_newval = SEGMENT_ADDR_VALUE(start, end);
	writel(seg_newval, seg_reg);

	/*
	 * Restore default value if something goes wrong. The chip
	 * might have set some bogus value and we would loose access
	 * to the chip.
	 */
	if (seg_newval != readl(seg_reg)) {
		dev_err(chip->nor.dev, "CE%d window invalid", cs);
		writel(seg_oldval, seg_reg);
		start = SEGMENT_ADDR_START(seg_oldval);
		end = SEGMENT_ADDR_END(seg_oldval);
		size = end - start;
	}

	dev_info(chip->nor.dev, "CE%d window [ 0x%.8x - 0x%.8x ] %dMB",
		 cs, start, end, size >> 20);

	return size;
}

/*
 * The segment register defines the mapping window on the AHB bus and
 * it needs to be configured depending on the chip size. The segment
 * register of the following CE also needs to be tuned in order to
 * provide a contiguous window across multiple chips.
 *
 * This is expected to be called in increasing CE order
 */
static u32 aspeed_smc_chip_set_segment(struct aspeed_smc_chip *chip)
{
	struct aspeed_smc_controller *controller = chip->controller;
	u32 ahb_base_phy, start;
	u32 size = chip->nor.mtd.size;

	/*
	 * Each controller has a chip size limit for direct memory
	 * access
	 */
	if (size > controller->info->maxsize)
		size = controller->info->maxsize;

	/*
	 * The AST2400 SPI controller only handles one chip and does
	 * not have segment registers. Let's use the chip size for the
	 * AHB window.
	 */
	if (controller->info == &spi_2400_info)
		goto out;

	/*
	 * The AST2500 SPI controller has a HW bug when the CE0 chip
	 * size reaches 128MB. Enforce a size limit of 120MB to
	 * prevent the controller from using bogus settings in the
	 * segment register.
	 */
	if (chip->cs == 0 && controller->info == &spi_2500_info &&
	    size == SZ_128M) {
		size = 120 << 20;
		dev_info(chip->nor.dev,
			 "CE%d window resized to %dMB (AST2500 HW quirk)",
			 chip->cs, size >> 20);
	}

	ahb_base_phy = aspeed_smc_ahb_base_phy(controller);

	/*
	 * As a start address for the current segment, use the default
	 * start address if we are handling CE0 or use the previous
	 * segment ending address
	 */
	if (chip->cs) {
		u32 prev = readl(SEGMENT_ADDR_REG(controller, chip->cs - 1));

		start = SEGMENT_ADDR_END(prev);
	} else {
		start = ahb_base_phy;
	}

	size = chip_set_segment(chip, chip->cs, start, size);

	/* Update chip base address on the AHB bus */
	chip->ahb_base = controller->ahb_base + (start - ahb_base_phy);

	/*
	 * Now, make sure the next segment does not overlap with the
	 * current one we just configured, even if there is no
	 * available chip. That could break access in Command Mode.
	 */
	if (chip->cs < controller->info->nce - 1)
		chip_set_segment(chip, chip->cs + 1, start + size, 0);

out:
	if (size < chip->nor.mtd.size)
		dev_warn(chip->nor.dev,
			 "CE%d window too small for chip %dMB",
			 chip->cs, (u32)chip->nor.mtd.size >> 20);

	return size;
}

static void aspeed_smc_chip_enable_write(struct aspeed_smc_chip *chip)
{
	struct aspeed_smc_controller *controller = chip->controller;
	u32 reg;

	reg = readl(controller->regs + CONFIG_REG);

	reg |= aspeed_smc_chip_write_bit(chip);
	writel(reg, controller->regs + CONFIG_REG);
}

static void aspeed_smc_chip_set_type(struct aspeed_smc_chip *chip, int type)
{
	struct aspeed_smc_controller *controller = chip->controller;
	u32 reg;

	chip->type = type;

	reg = readl(controller->regs + CONFIG_REG);
	reg &= ~(3 << (chip->cs * 2));
	reg |= chip->type << (chip->cs * 2);
	writel(reg, controller->regs + CONFIG_REG);
}

/*
 * The first chip of the AST2500 FMC flash controller is strapped by
 * hardware, or autodetected, but other chips need to be set. Enforce
 * the 4B setting for all chips.
 */
static void aspeed_smc_chip_set_4b(struct aspeed_smc_chip *chip)
{
	struct aspeed_smc_controller *controller = chip->controller;
	u32 reg;

	reg = readl(controller->regs + CE_CONTROL_REG);
	reg |= 1 << chip->cs;
	writel(reg, controller->regs + CE_CONTROL_REG);
}

/*
 * The AST2400 SPI flash controller does not have a CE Control
 * register. It uses the CE0 control register to set 4Byte mode at the
 * controller level.
 */
static void aspeed_smc_chip_set_4b_spi_2400(struct aspeed_smc_chip *chip)
{
	chip->ctl_val[smc_base] |= CONTROL_IO_ADDRESS_4B;
	chip->ctl_val[smc_read] |= CONTROL_IO_ADDRESS_4B;
}

static int aspeed_smc_chip_setup_init(struct aspeed_smc_chip *chip,
				      struct resource *res)
{
	struct aspeed_smc_controller *controller = chip->controller;
	const struct aspeed_smc_info *info = controller->info;
	u32 reg, base_reg;

	/*
	 * Always turn on the write enable bit to allow opcodes to be
	 * sent in user mode.
	 */
	aspeed_smc_chip_enable_write(chip);

	/* The driver only supports SPI type flash */
	if (info->hastype)
		aspeed_smc_chip_set_type(chip, smc_type_spi);

	/*
	 * Configure chip base address in memory
	 */
	chip->ahb_base = aspeed_smc_chip_base(chip, res);
	if (!chip->ahb_base) {
		dev_warn(chip->nor.dev, "CE%d window closed", chip->cs);
		return -EINVAL;
	}

	/*
	 * Get value of the inherited control register. U-Boot usually
	 * does some timing calibration on the FMC chip, so it's good
	 * to keep them. In the future, we should handle calibration
	 * from Linux.
	 */
	reg = readl(chip->ctl);
	dev_dbg(controller->dev, "control register: %08x\n", reg);

	base_reg = reg & CONTROL_KEEP_MASK;
	if (base_reg != reg) {
		dev_dbg(controller->dev,
			"control register changed to: %08x\n",
			base_reg);
	}
	chip->ctl_val[smc_base] = base_reg;

	/*
	 * Retain the prior value of the control register as the
	 * default if it was normal access mode. Otherwise start with
	 * the sanitized base value set to read mode.
	 */
	if ((reg & CONTROL_COMMAND_MODE_MASK) ==
	    CONTROL_COMMAND_MODE_NORMAL)
		chip->ctl_val[smc_read] = reg;
	else
		chip->ctl_val[smc_read] = chip->ctl_val[smc_base] |
			CONTROL_COMMAND_MODE_NORMAL;

	dev_dbg(controller->dev, "default control register: %08x\n",
		chip->ctl_val[smc_read]);
	return 0;
}

static int aspeed_smc_chip_setup_finish(struct aspeed_smc_chip *chip)
{
	struct aspeed_smc_controller *controller = chip->controller;
	const struct aspeed_smc_info *info = controller->info;
	u32 cmd;

	if (chip->nor.addr_width == 4 && info->set_4b)
		info->set_4b(chip);

	/* This is for direct AHB access when using Command Mode. */
	chip->ahb_window_size = aspeed_smc_chip_set_segment(chip);

	/*
	 * base mode has not been optimized yet. use it for writes.
	 */
	chip->ctl_val[smc_write] = chip->ctl_val[smc_base] |
		chip->nor.program_opcode << CONTROL_COMMAND_SHIFT |
		CONTROL_COMMAND_MODE_WRITE;

	dev_dbg(controller->dev, "write control register: %08x\n",
		chip->ctl_val[smc_write]);

	/*
	 * TODO: Adjust clocks if fast read is supported and interpret
	 * SPI-NOR flags to adjust controller settings.
	 */
	if (chip->nor.read_proto == SNOR_PROTO_1_1_1) {
		if (chip->nor.read_dummy == 0)
			cmd = CONTROL_COMMAND_MODE_NORMAL;
		else
			cmd = CONTROL_COMMAND_MODE_FREAD;
	} else {
		dev_err(chip->nor.dev, "unsupported SPI read mode\n");
		return -EINVAL;
	}

	chip->ctl_val[smc_read] |= cmd |
		CONTROL_IO_DUMMY_SET(chip->nor.read_dummy / 8);

	dev_dbg(controller->dev, "base control register: %08x\n",
		chip->ctl_val[smc_read]);
	return 0;
}

static int aspeed_smc_setup_flash(struct aspeed_smc_controller *controller,
				  struct device_node *np, struct resource *r)
{
	const struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ |
			SNOR_HWCAPS_READ_FAST |
			SNOR_HWCAPS_PP,
	};
	const struct aspeed_smc_info *info = controller->info;
	struct device *dev = controller->dev;
	struct device_node *child;
	unsigned int cs;
	int ret = -ENODEV;

	for_each_available_child_of_node(np, child) {
		struct aspeed_smc_chip *chip;
		struct spi_nor *nor;
		struct mtd_info *mtd;

		/* This driver does not support NAND or NOR flash devices. */
		if (!of_device_is_compatible(child, "jedec,spi-nor"))
			continue;

		ret = of_property_read_u32(child, "reg", &cs);
		if (ret) {
			dev_err(dev, "Couldn't not read chip select.\n");
			break;
		}

		if (cs >= info->nce) {
			dev_err(dev, "Chip select %d out of range.\n",
				cs);
			ret = -ERANGE;
			break;
		}

		if (controller->chips[cs]) {
			dev_err(dev, "Chip select %d already in use by %s\n",
				cs, dev_name(controller->chips[cs]->nor.dev));
			ret = -EBUSY;
			break;
		}

		chip = devm_kzalloc(controller->dev, sizeof(*chip), GFP_KERNEL);
		if (!chip) {
			ret = -ENOMEM;
			break;
		}

		chip->controller = controller;
		chip->ctl = controller->regs + info->ctl0 + cs * 4;
		chip->cs = cs;

		nor = &chip->nor;
		mtd = &nor->mtd;

		nor->dev = dev;
		nor->priv = chip;
		spi_nor_set_flash_node(nor, child);
		nor->read = aspeed_smc_read_user;
		nor->write = aspeed_smc_write_user;
		nor->read_reg = aspeed_smc_read_reg;
		nor->write_reg = aspeed_smc_write_reg;
		nor->prepare = aspeed_smc_prep;
		nor->unprepare = aspeed_smc_unprep;

		ret = aspeed_smc_chip_setup_init(chip, r);
		if (ret)
			break;

		/*
		 * TODO: Add support for Dual and Quad SPI protocols
		 * attach when board support is present as determined
		 * by of property.
		 */
		ret = spi_nor_scan(nor, NULL, &hwcaps);
		if (ret)
			break;

		ret = aspeed_smc_chip_setup_finish(chip);
		if (ret)
			break;

		ret = mtd_device_register(mtd, NULL, 0);
		if (ret)
			break;

		controller->chips[cs] = chip;
	}

	if (ret)
		aspeed_smc_unregister(controller);

	return ret;
}

static int aspeed_smc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct aspeed_smc_controller *controller;
	const struct of_device_id *match;
	const struct aspeed_smc_info *info;
	struct resource *res;
	int ret;

	match = of_match_device(aspeed_smc_matches, &pdev->dev);
	if (!match || !match->data)
		return -ENODEV;
	info = match->data;

	controller = devm_kzalloc(&pdev->dev,
				  struct_size(controller, chips, info->nce),
				  GFP_KERNEL);
	if (!controller)
		return -ENOMEM;
	controller->info = info;
	controller->dev = dev;

	mutex_init(&controller->mutex);
	platform_set_drvdata(pdev, controller);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	controller->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(controller->regs))
		return PTR_ERR(controller->regs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	controller->ahb_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(controller->ahb_base))
		return PTR_ERR(controller->ahb_base);

	controller->ahb_window_size = resource_size(res);

	ret = aspeed_smc_setup_flash(controller, np, res);
	if (ret)
		dev_err(dev, "Aspeed SMC probe failed %d\n", ret);

	return ret;
}

static struct platform_driver aspeed_smc_driver = {
	.probe = aspeed_smc_probe,
	.remove = aspeed_smc_remove,
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = aspeed_smc_matches,
	}
};

module_platform_driver(aspeed_smc_driver);

MODULE_DESCRIPTION("ASPEED Static Memory Controller Driver");
MODULE_AUTHOR("Cedric Le Goater <clg@kaod.org>");
MODULE_LICENSE("GPL v2");
