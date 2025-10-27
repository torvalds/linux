// SPDX-License-Identifier: GPL-2.0-only

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/regmap.h>
#include <linux/sprintf.h>
#include <linux/string_choices.h>
#include <linux/unaligned.h>
#include <net/devlink.h>

#include "core.h"
#include "devlink.h"
#include "dpll.h"
#include "regs.h"

/* Chip IDs for zl30731 */
static const u16 zl30731_ids[] = {
	0x0E93,
	0x1E93,
	0x2E93,
};

const struct zl3073x_chip_info zl30731_chip_info = {
	.ids = zl30731_ids,
	.num_ids = ARRAY_SIZE(zl30731_ids),
	.num_channels = 1,
};
EXPORT_SYMBOL_NS_GPL(zl30731_chip_info, "ZL3073X");

/* Chip IDs for zl30732 */
static const u16 zl30732_ids[] = {
	0x0E30,
	0x0E94,
	0x1E94,
	0x1F60,
	0x2E94,
	0x3FC4,
};

const struct zl3073x_chip_info zl30732_chip_info = {
	.ids = zl30732_ids,
	.num_ids = ARRAY_SIZE(zl30732_ids),
	.num_channels = 2,
};
EXPORT_SYMBOL_NS_GPL(zl30732_chip_info, "ZL3073X");

/* Chip IDs for zl30733 */
static const u16 zl30733_ids[] = {
	0x0E95,
	0x1E95,
	0x2E95,
};

const struct zl3073x_chip_info zl30733_chip_info = {
	.ids = zl30733_ids,
	.num_ids = ARRAY_SIZE(zl30733_ids),
	.num_channels = 3,
};
EXPORT_SYMBOL_NS_GPL(zl30733_chip_info, "ZL3073X");

/* Chip IDs for zl30734 */
static const u16 zl30734_ids[] = {
	0x0E96,
	0x1E96,
	0x2E96,
};

const struct zl3073x_chip_info zl30734_chip_info = {
	.ids = zl30734_ids,
	.num_ids = ARRAY_SIZE(zl30734_ids),
	.num_channels = 4,
};
EXPORT_SYMBOL_NS_GPL(zl30734_chip_info, "ZL3073X");

/* Chip IDs for zl30735 */
static const u16 zl30735_ids[] = {
	0x0E97,
	0x1E97,
	0x2E97,
};

const struct zl3073x_chip_info zl30735_chip_info = {
	.ids = zl30735_ids,
	.num_ids = ARRAY_SIZE(zl30735_ids),
	.num_channels = 5,
};
EXPORT_SYMBOL_NS_GPL(zl30735_chip_info, "ZL3073X");

#define ZL_RANGE_OFFSET		0x80
#define ZL_PAGE_SIZE		0x80
#define ZL_NUM_PAGES		256
#define ZL_PAGE_SEL		0x7F
#define ZL_PAGE_SEL_MASK	GENMASK(7, 0)
#define ZL_NUM_REGS		(ZL_NUM_PAGES * ZL_PAGE_SIZE)

/* Regmap range configuration */
static const struct regmap_range_cfg zl3073x_regmap_range = {
	.range_min	= ZL_RANGE_OFFSET,
	.range_max	= ZL_RANGE_OFFSET + ZL_NUM_REGS - 1,
	.selector_reg	= ZL_PAGE_SEL,
	.selector_mask	= ZL_PAGE_SEL_MASK,
	.selector_shift	= 0,
	.window_start	= 0,
	.window_len	= ZL_PAGE_SIZE,
};

static bool
zl3073x_is_volatile_reg(struct device *dev __maybe_unused, unsigned int reg)
{
	/* Only page selector is non-volatile */
	return reg != ZL_PAGE_SEL;
}

const struct regmap_config zl3073x_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= ZL_RANGE_OFFSET + ZL_NUM_REGS - 1,
	.ranges		= &zl3073x_regmap_range,
	.num_ranges	= 1,
	.cache_type	= REGCACHE_MAPLE,
	.volatile_reg	= zl3073x_is_volatile_reg,
};
EXPORT_SYMBOL_NS_GPL(zl3073x_regmap_config, "ZL3073X");

/**
 * zl3073x_ref_freq_factorize - factorize given frequency
 * @freq: input frequency
 * @base: base frequency
 * @mult: multiplier
 *
 * Checks if the given frequency can be factorized using one of the
 * supported base frequencies. If so the base frequency and multiplier
 * are stored into appropriate parameters if they are not NULL.
 *
 * Return: 0 on success, -EINVAL if the frequency cannot be factorized
 */
int
zl3073x_ref_freq_factorize(u32 freq, u16 *base, u16 *mult)
{
	static const u16 base_freqs[] = {
		1, 2, 4, 5, 8, 10, 16, 20, 25, 32, 40, 50, 64, 80, 100, 125,
		128, 160, 200, 250, 256, 320, 400, 500, 625, 640, 800, 1000,
		1250, 1280, 1600, 2000, 2500, 3125, 3200, 4000, 5000, 6250,
		6400, 8000, 10000, 12500, 15625, 16000, 20000, 25000, 31250,
		32000, 40000, 50000, 62500,
	};
	u32 div;
	int i;

	for (i = 0; i < ARRAY_SIZE(base_freqs); i++) {
		div = freq / base_freqs[i];

		if (div <= U16_MAX && (freq % base_freqs[i]) == 0) {
			if (base)
				*base = base_freqs[i];
			if (mult)
				*mult = div;

			return 0;
		}
	}

	return -EINVAL;
}

static bool
zl3073x_check_reg(struct zl3073x_dev *zldev, unsigned int reg, size_t size)
{
	/* Check that multiop lock is held when accessing registers
	 * from page 10 and above except the page 255 that does not
	 * need this protection.
	 */
	if (ZL_REG_PAGE(reg) >= 10 && ZL_REG_PAGE(reg) < 255)
		lockdep_assert_held(&zldev->multiop_lock);

	/* Check the index is in valid range for indexed register */
	if (ZL_REG_OFFSET(reg) > ZL_REG_MAX_OFFSET(reg)) {
		dev_err(zldev->dev, "Index out of range for reg 0x%04lx\n",
			ZL_REG_ADDR(reg));
		return false;
	}
	/* Check the requested size corresponds to register size */
	if (ZL_REG_SIZE(reg) != size) {
		dev_err(zldev->dev, "Invalid size %zu for reg 0x%04lx\n",
			size, ZL_REG_ADDR(reg));
		return false;
	}

	return true;
}

static int
zl3073x_read_reg(struct zl3073x_dev *zldev, unsigned int reg, void *val,
		 size_t size)
{
	int rc;

	if (!zl3073x_check_reg(zldev, reg, size))
		return -EINVAL;

	/* Map the register address to virtual range */
	reg = ZL_REG_ADDR(reg) + ZL_RANGE_OFFSET;

	rc = regmap_bulk_read(zldev->regmap, reg, val, size);
	if (rc) {
		dev_err(zldev->dev, "Failed to read reg 0x%04x: %pe\n", reg,
			ERR_PTR(rc));
		return rc;
	}

	return 0;
}

static int
zl3073x_write_reg(struct zl3073x_dev *zldev, unsigned int reg, const void *val,
		  size_t size)
{
	int rc;

	if (!zl3073x_check_reg(zldev, reg, size))
		return -EINVAL;

	/* Map the register address to virtual range */
	reg = ZL_REG_ADDR(reg) + ZL_RANGE_OFFSET;

	rc = regmap_bulk_write(zldev->regmap, reg, val, size);
	if (rc) {
		dev_err(zldev->dev, "Failed to write reg 0x%04x: %pe\n", reg,
			ERR_PTR(rc));
		return rc;
	}

	return 0;
}

/**
 * zl3073x_read_u8 - read value from 8bit register
 * @zldev: zl3073x device pointer
 * @reg: register to write to
 * @val: value to write
 *
 * Reads value from given 8bit register.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_read_u8(struct zl3073x_dev *zldev, unsigned int reg, u8 *val)
{
	return zl3073x_read_reg(zldev, reg, val, sizeof(*val));
}

/**
 * zl3073x_write_u8 - write value to 16bit register
 * @zldev: zl3073x device pointer
 * @reg: register to write to
 * @val: value to write
 *
 * Writes value into given 8bit register.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_write_u8(struct zl3073x_dev *zldev, unsigned int reg, u8 val)
{
	return zl3073x_write_reg(zldev, reg, &val, sizeof(val));
}

/**
 * zl3073x_read_u16 - read value from 16bit register
 * @zldev: zl3073x device pointer
 * @reg: register to write to
 * @val: value to write
 *
 * Reads value from given 16bit register.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_read_u16(struct zl3073x_dev *zldev, unsigned int reg, u16 *val)
{
	int rc;

	rc = zl3073x_read_reg(zldev, reg, val, sizeof(*val));
	if (!rc)
		be16_to_cpus(val);

	return rc;
}

/**
 * zl3073x_write_u16 - write value to 16bit register
 * @zldev: zl3073x device pointer
 * @reg: register to write to
 * @val: value to write
 *
 * Writes value into given 16bit register.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_write_u16(struct zl3073x_dev *zldev, unsigned int reg, u16 val)
{
	cpu_to_be16s(&val);

	return zl3073x_write_reg(zldev, reg, &val, sizeof(val));
}

/**
 * zl3073x_read_u32 - read value from 32bit register
 * @zldev: zl3073x device pointer
 * @reg: register to write to
 * @val: value to write
 *
 * Reads value from given 32bit register.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_read_u32(struct zl3073x_dev *zldev, unsigned int reg, u32 *val)
{
	int rc;

	rc = zl3073x_read_reg(zldev, reg, val, sizeof(*val));
	if (!rc)
		be32_to_cpus(val);

	return rc;
}

/**
 * zl3073x_write_u32 - write value to 32bit register
 * @zldev: zl3073x device pointer
 * @reg: register to write to
 * @val: value to write
 *
 * Writes value into given 32bit register.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_write_u32(struct zl3073x_dev *zldev, unsigned int reg, u32 val)
{
	cpu_to_be32s(&val);

	return zl3073x_write_reg(zldev, reg, &val, sizeof(val));
}

/**
 * zl3073x_read_u48 - read value from 48bit register
 * @zldev: zl3073x device pointer
 * @reg: register to write to
 * @val: value to write
 *
 * Reads value from given 48bit register.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_read_u48(struct zl3073x_dev *zldev, unsigned int reg, u64 *val)
{
	u8 buf[6];
	int rc;

	rc = zl3073x_read_reg(zldev, reg, buf, sizeof(buf));
	if (!rc)
		*val = get_unaligned_be48(buf);

	return rc;
}

/**
 * zl3073x_write_u48 - write value to 48bit register
 * @zldev: zl3073x device pointer
 * @reg: register to write to
 * @val: value to write
 *
 * Writes value into given 48bit register.
 * The value must be from the interval -S48_MIN to U48_MAX.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_write_u48(struct zl3073x_dev *zldev, unsigned int reg, u64 val)
{
	u8 buf[6];

	/* Check the value belongs to <S48_MIN, U48_MAX>
	 * Any value >= S48_MIN has bits 47..63 set.
	 */
	if (val > GENMASK_ULL(47, 0) && val < GENMASK_ULL(63, 47)) {
		dev_err(zldev->dev, "Value 0x%0llx out of range\n", val);
		return -EINVAL;
	}

	put_unaligned_be48(val, buf);

	return zl3073x_write_reg(zldev, reg, buf, sizeof(buf));
}

/**
 * zl3073x_poll_zero_u8 - wait for register to be cleared by device
 * @zldev: zl3073x device pointer
 * @reg: register to poll (has to be 8bit register)
 * @mask: bit mask for polling
 *
 * Waits for bits specified by @mask in register @reg value to be cleared
 * by the device.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_poll_zero_u8(struct zl3073x_dev *zldev, unsigned int reg, u8 mask)
{
	/* Register polling sleep & timeout */
#define ZL_POLL_SLEEP_US   10
#define ZL_POLL_TIMEOUT_US 2000000
	unsigned int val;

	/* Check the register is 8bit */
	if (ZL_REG_SIZE(reg) != 1) {
		dev_err(zldev->dev, "Invalid reg 0x%04lx size for polling\n",
			ZL_REG_ADDR(reg));
		return -EINVAL;
	}

	/* Map the register address to virtual range */
	reg = ZL_REG_ADDR(reg) + ZL_RANGE_OFFSET;

	return regmap_read_poll_timeout(zldev->regmap, reg, val, !(val & mask),
					ZL_POLL_SLEEP_US, ZL_POLL_TIMEOUT_US);
}

int zl3073x_mb_op(struct zl3073x_dev *zldev, unsigned int op_reg, u8 op_val,
		  unsigned int mask_reg, u16 mask_val)
{
	int rc;

	/* Set mask for the operation */
	rc = zl3073x_write_u16(zldev, mask_reg, mask_val);
	if (rc)
		return rc;

	/* Trigger the operation */
	rc = zl3073x_write_u8(zldev, op_reg, op_val);
	if (rc)
		return rc;

	/* Wait for the operation to actually finish */
	return zl3073x_poll_zero_u8(zldev, op_reg, op_val);
}

/**
 * zl3073x_do_hwreg_op - Perform HW register read/write operation
 * @zldev: zl3073x device pointer
 * @op: operation to perform
 *
 * Performs requested operation and waits for its completion.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_do_hwreg_op(struct zl3073x_dev *zldev, u8 op)
{
	int rc;

	/* Set requested operation and set pending bit */
	rc = zl3073x_write_u8(zldev, ZL_REG_HWREG_OP, op | ZL_HWREG_OP_PENDING);
	if (rc)
		return rc;

	/* Poll for completion - pending bit cleared */
	return zl3073x_poll_zero_u8(zldev, ZL_REG_HWREG_OP,
				    ZL_HWREG_OP_PENDING);
}

/**
 * zl3073x_read_hwreg - Read HW register
 * @zldev: zl3073x device pointer
 * @addr: HW register address
 * @value: Value of the HW register
 *
 * Reads HW register value and stores it into @value.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_read_hwreg(struct zl3073x_dev *zldev, u32 addr, u32 *value)
{
	int rc;

	/* Set address to read data from */
	rc = zl3073x_write_u32(zldev, ZL_REG_HWREG_ADDR, addr);
	if (rc)
		return rc;

	/* Perform the read operation */
	rc = zl3073x_do_hwreg_op(zldev, ZL_HWREG_OP_READ);
	if (rc)
		return rc;

	/* Read the received data */
	return zl3073x_read_u32(zldev, ZL_REG_HWREG_READ_DATA, value);
}

/**
 * zl3073x_write_hwreg - Write value to HW register
 * @zldev: zl3073x device pointer
 * @addr: HW registers address
 * @value: Value to be written to HW register
 *
 * Stores the requested value into HW register.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_write_hwreg(struct zl3073x_dev *zldev, u32 addr, u32 value)
{
	int rc;

	/* Set address to write data to */
	rc = zl3073x_write_u32(zldev, ZL_REG_HWREG_ADDR, addr);
	if (rc)
		return rc;

	/* Set data to be written */
	rc = zl3073x_write_u32(zldev, ZL_REG_HWREG_WRITE_DATA, value);
	if (rc)
		return rc;

	/* Perform the write operation */
	return zl3073x_do_hwreg_op(zldev, ZL_HWREG_OP_WRITE);
}

/**
 * zl3073x_update_hwreg - Update certain bits in HW register
 * @zldev: zl3073x device pointer
 * @addr: HW register address
 * @value: Value to be written into HW register
 * @mask: Bitmask indicating bits to be updated
 *
 * Reads given HW register, updates requested bits specified by value and
 * mask and writes result back to HW register.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_update_hwreg(struct zl3073x_dev *zldev, u32 addr, u32 value,
			 u32 mask)
{
	u32 tmp;
	int rc;

	rc = zl3073x_read_hwreg(zldev, addr, &tmp);
	if (rc)
		return rc;

	tmp &= ~mask;
	tmp |= value & mask;

	return zl3073x_write_hwreg(zldev, addr, tmp);
}

/**
 * zl3073x_write_hwreg_seq - Write HW registers sequence
 * @zldev: pointer to device structure
 * @seq: pointer to first sequence item
 * @num_items: number of items in sequence
 *
 * Writes given HW registers sequence.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_write_hwreg_seq(struct zl3073x_dev *zldev,
			    const struct zl3073x_hwreg_seq_item *seq,
			    size_t num_items)
{
	int i, rc = 0;

	for (i = 0; i < num_items; i++) {
		dev_dbg(zldev->dev, "Write 0x%0x [0x%0x] to 0x%0x",
			seq[i].value, seq[i].mask, seq[i].addr);

		if (seq[i].mask == U32_MAX)
			/* Write value directly */
			rc = zl3073x_write_hwreg(zldev, seq[i].addr,
						 seq[i].value);
		else
			/* Update only bits specified by the mask */
			rc = zl3073x_update_hwreg(zldev, seq[i].addr,
						  seq[i].value, seq[i].mask);
		if (rc)
			return rc;

		if (seq->wait)
			msleep(seq->wait);
	}

	return rc;
}

/**
 * zl3073x_ref_state_fetch - get input reference state
 * @zldev: pointer to zl3073x_dev structure
 * @index: input reference index to fetch state for
 *
 * Function fetches information for the given input reference that are
 * invariant and stores them for later use.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_ref_state_fetch(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_ref *input = &zldev->ref[index];
	u8 ref_config;
	int rc;

	/* If the input is differential then the configuration for N-pin
	 * reference is ignored and P-pin config is used for both.
	 */
	if (zl3073x_is_n_pin(index) &&
	    zl3073x_ref_is_diff(zldev, index - 1)) {
		input->enabled = zl3073x_ref_is_enabled(zldev, index - 1);
		input->diff = true;

		return 0;
	}

	guard(mutex)(&zldev->multiop_lock);

	/* Read reference configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_REF_MB_SEM, ZL_REF_MB_SEM_RD,
			   ZL_REG_REF_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Read ref_config register */
	rc = zl3073x_read_u8(zldev, ZL_REG_REF_CONFIG, &ref_config);
	if (rc)
		return rc;

	input->enabled = FIELD_GET(ZL_REF_CONFIG_ENABLE, ref_config);
	input->diff = FIELD_GET(ZL_REF_CONFIG_DIFF_EN, ref_config);

	dev_dbg(zldev->dev, "REF%u is %s and configured as %s\n", index,
		str_enabled_disabled(input->enabled),
		input->diff ? "differential" : "single-ended");

	return rc;
}

/**
 * zl3073x_out_state_fetch - get output state
 * @zldev: pointer to zl3073x_dev structure
 * @index: output index to fetch state for
 *
 * Function fetches information for the given output (not output pin)
 * that are invariant and stores them for later use.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_out_state_fetch(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_out *out = &zldev->out[index];
	u8 output_ctrl, output_mode;
	int rc;

	/* Read output configuration */
	rc = zl3073x_read_u8(zldev, ZL_REG_OUTPUT_CTRL(index), &output_ctrl);
	if (rc)
		return rc;

	/* Store info about output enablement and synthesizer the output
	 * is connected to.
	 */
	out->enabled = FIELD_GET(ZL_OUTPUT_CTRL_EN, output_ctrl);
	out->synth = FIELD_GET(ZL_OUTPUT_CTRL_SYNTH_SEL, output_ctrl);

	dev_dbg(zldev->dev, "OUT%u is %s and connected to SYNTH%u\n", index,
		str_enabled_disabled(out->enabled), out->synth);

	guard(mutex)(&zldev->multiop_lock);

	/* Read output configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_RD,
			   ZL_REG_OUTPUT_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Read output_mode */
	rc = zl3073x_read_u8(zldev, ZL_REG_OUTPUT_MODE, &output_mode);
	if (rc)
		return rc;

	/* Extract and store output signal format */
	out->signal_format = FIELD_GET(ZL_OUTPUT_MODE_SIGNAL_FORMAT,
				       output_mode);

	dev_dbg(zldev->dev, "OUT%u has signal format 0x%02x\n", index,
		out->signal_format);

	return rc;
}

/**
 * zl3073x_synth_state_fetch - get synth state
 * @zldev: pointer to zl3073x_dev structure
 * @index: synth index to fetch state for
 *
 * Function fetches information for the given synthesizer that are
 * invariant and stores them for later use.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_synth_state_fetch(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_synth *synth = &zldev->synth[index];
	u16 base, m, n;
	u8 synth_ctrl;
	u32 mult;
	int rc;

	/* Read synth control register */
	rc = zl3073x_read_u8(zldev, ZL_REG_SYNTH_CTRL(index), &synth_ctrl);
	if (rc)
		return rc;

	/* Store info about synth enablement and DPLL channel the synth is
	 * driven by.
	 */
	synth->enabled = FIELD_GET(ZL_SYNTH_CTRL_EN, synth_ctrl);
	synth->dpll = FIELD_GET(ZL_SYNTH_CTRL_DPLL_SEL, synth_ctrl);

	dev_dbg(zldev->dev, "SYNTH%u is %s and driven by DPLL%u\n", index,
		str_enabled_disabled(synth->enabled), synth->dpll);

	guard(mutex)(&zldev->multiop_lock);

	/* Read synth configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_SYNTH_MB_SEM, ZL_SYNTH_MB_SEM_RD,
			   ZL_REG_SYNTH_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* The output frequency is determined by the following formula:
	 * base * multiplier * numerator / denominator
	 *
	 * Read registers with these values
	 */
	rc = zl3073x_read_u16(zldev, ZL_REG_SYNTH_FREQ_BASE, &base);
	if (rc)
		return rc;

	rc = zl3073x_read_u32(zldev, ZL_REG_SYNTH_FREQ_MULT, &mult);
	if (rc)
		return rc;

	rc = zl3073x_read_u16(zldev, ZL_REG_SYNTH_FREQ_M, &m);
	if (rc)
		return rc;

	rc = zl3073x_read_u16(zldev, ZL_REG_SYNTH_FREQ_N, &n);
	if (rc)
		return rc;

	/* Check denominator for zero to avoid div by 0 */
	if (!n) {
		dev_err(zldev->dev,
			"Zero divisor for SYNTH%u retrieved from device\n",
			index);
		return -EINVAL;
	}

	/* Compute and store synth frequency */
	zldev->synth[index].freq = div_u64(mul_u32_u32(base * m, mult), n);

	dev_dbg(zldev->dev, "SYNTH%u frequency: %u Hz\n", index,
		zldev->synth[index].freq);

	return rc;
}

static int
zl3073x_dev_state_fetch(struct zl3073x_dev *zldev)
{
	int rc;
	u8 i;

	for (i = 0; i < ZL3073X_NUM_REFS; i++) {
		rc = zl3073x_ref_state_fetch(zldev, i);
		if (rc) {
			dev_err(zldev->dev,
				"Failed to fetch input state: %pe\n",
				ERR_PTR(rc));
			return rc;
		}
	}

	for (i = 0; i < ZL3073X_NUM_SYNTHS; i++) {
		rc = zl3073x_synth_state_fetch(zldev, i);
		if (rc) {
			dev_err(zldev->dev,
				"Failed to fetch synth state: %pe\n",
				ERR_PTR(rc));
			return rc;
		}
	}

	for (i = 0; i < ZL3073X_NUM_OUTS; i++) {
		rc = zl3073x_out_state_fetch(zldev, i);
		if (rc) {
			dev_err(zldev->dev,
				"Failed to fetch output state: %pe\n",
				ERR_PTR(rc));
			return rc;
		}
	}

	return rc;
}

/**
 * zl3073x_ref_phase_offsets_update - update reference phase offsets
 * @zldev: pointer to zl3073x_dev structure
 * @channel: DPLL channel number or -1
 *
 * The function asks device to update phase offsets latch registers with
 * the latest measured values. There are 2 sets of latch registers:
 *
 * 1) Up to 5 DPLL-to-connected-ref registers that contain phase offset
 *    values between particular DPLL channel and its *connected* input
 *    reference.
 *
 * 2) 10 selected-DPLL-to-all-ref registers that contain phase offset values
 *    between selected DPLL channel and all input references.
 *
 * If the caller is interested in 2) then it has to pass DPLL channel number
 * in @channel parameter. If it is interested only in 1) then it should pass
 * @channel parameter with value of -1.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_ref_phase_offsets_update(struct zl3073x_dev *zldev, int channel)
{
	int rc;

	/* Per datasheet we have to wait for 'dpll_ref_phase_err_rqst_rd'
	 * to be zero to ensure that the measured data are coherent.
	 */
	rc = zl3073x_poll_zero_u8(zldev, ZL_REG_REF_PHASE_ERR_READ_RQST,
				  ZL_REF_PHASE_ERR_READ_RQST_RD);
	if (rc)
		return rc;

	/* Select DPLL channel if it is specified */
	if (channel != -1) {
		rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_MEAS_IDX, channel);
		if (rc)
			return rc;
	}

	/* Request to update phase offsets measurement values */
	rc = zl3073x_write_u8(zldev, ZL_REG_REF_PHASE_ERR_READ_RQST,
			      ZL_REF_PHASE_ERR_READ_RQST_RD);
	if (rc)
		return rc;

	/* Wait for finish */
	return zl3073x_poll_zero_u8(zldev, ZL_REG_REF_PHASE_ERR_READ_RQST,
				    ZL_REF_PHASE_ERR_READ_RQST_RD);
}

/**
 * zl3073x_ref_ffo_update - update reference fractional frequency offsets
 * @zldev: pointer to zl3073x_dev structure
 *
 * The function asks device to update fractional frequency offsets latch
 * registers the latest measured values, reads and stores them into
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_ref_ffo_update(struct zl3073x_dev *zldev)
{
	int i, rc;

	/* Per datasheet we have to wait for 'ref_freq_meas_ctrl' to be zero
	 * to ensure that the measured data are coherent.
	 */
	rc = zl3073x_poll_zero_u8(zldev, ZL_REG_REF_FREQ_MEAS_CTRL,
				  ZL_REF_FREQ_MEAS_CTRL);
	if (rc)
		return rc;

	/* Select all references for measurement */
	rc = zl3073x_write_u8(zldev, ZL_REG_REF_FREQ_MEAS_MASK_3_0,
			      GENMASK(7, 0)); /* REF0P..REF3N */
	if (rc)
		return rc;
	rc = zl3073x_write_u8(zldev, ZL_REG_REF_FREQ_MEAS_MASK_4,
			      GENMASK(1, 0)); /* REF4P..REF4N */
	if (rc)
		return rc;

	/* Request frequency offset measurement */
	rc = zl3073x_write_u8(zldev, ZL_REG_REF_FREQ_MEAS_CTRL,
			      ZL_REF_FREQ_MEAS_CTRL_REF_FREQ_OFF);
	if (rc)
		return rc;

	/* Wait for finish */
	rc = zl3073x_poll_zero_u8(zldev, ZL_REG_REF_FREQ_MEAS_CTRL,
				  ZL_REF_FREQ_MEAS_CTRL);
	if (rc)
		return rc;

	/* Read DPLL-to-REFx frequency offset measurements */
	for (i = 0; i < ZL3073X_NUM_REFS; i++) {
		s32 value;

		/* Read value stored in units of 2^-32 signed */
		rc = zl3073x_read_u32(zldev, ZL_REG_REF_FREQ(i), &value);
		if (rc)
			return rc;

		/* Convert to ppm -> ffo = (10^6 * value) / 2^32 */
		zldev->ref[i].ffo = mul_s64_u64_shr(value, 1000000, 32);
	}

	return 0;
}

static void
zl3073x_dev_periodic_work(struct kthread_work *work)
{
	struct zl3073x_dev *zldev = container_of(work, struct zl3073x_dev,
						 work.work);
	struct zl3073x_dpll *zldpll;
	int rc;

	/* Update DPLL-to-connected-ref phase offsets registers */
	rc = zl3073x_ref_phase_offsets_update(zldev, -1);
	if (rc)
		dev_warn(zldev->dev, "Failed to update phase offsets: %pe\n",
			 ERR_PTR(rc));

	/* Update references' fractional frequency offsets */
	rc = zl3073x_ref_ffo_update(zldev);
	if (rc)
		dev_warn(zldev->dev,
			 "Failed to update fractional frequency offsets: %pe\n",
			 ERR_PTR(rc));

	list_for_each_entry(zldpll, &zldev->dplls, list)
		zl3073x_dpll_changes_check(zldpll);

	/* Run twice a second */
	kthread_queue_delayed_work(zldev->kworker, &zldev->work,
				   msecs_to_jiffies(500));
}

int zl3073x_dev_phase_avg_factor_set(struct zl3073x_dev *zldev, u8 factor)
{
	u8 dpll_meas_ctrl, value;
	int rc;

	/* Read DPLL phase measurement control register */
	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MEAS_CTRL, &dpll_meas_ctrl);
	if (rc)
		return rc;

	/* Convert requested factor to register value */
	value = (factor + 1) & 0x0f;

	/* Update phase measurement control register */
	dpll_meas_ctrl &= ~ZL_DPLL_MEAS_CTRL_AVG_FACTOR;
	dpll_meas_ctrl |= FIELD_PREP(ZL_DPLL_MEAS_CTRL_AVG_FACTOR, value);
	rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_MEAS_CTRL, dpll_meas_ctrl);
	if (rc)
		return rc;

	/* Save the new factor */
	zldev->phase_avg_factor = factor;

	return 0;
}

/**
 * zl3073x_dev_phase_meas_setup - setup phase offset measurement
 * @zldev: pointer to zl3073x_dev structure
 *
 * Enable phase offset measurement block, set measurement averaging factor
 * and enable DPLL-to-its-ref phase measurement for all DPLLs.
 *
 * Returns: 0 on success, <0 on error
 */
static int
zl3073x_dev_phase_meas_setup(struct zl3073x_dev *zldev)
{
	struct zl3073x_dpll *zldpll;
	u8 dpll_meas_ctrl, mask = 0;
	int rc;

	/* Setup phase measurement averaging factor */
	rc = zl3073x_dev_phase_avg_factor_set(zldev, zldev->phase_avg_factor);
	if (rc)
		return rc;

	/* Read DPLL phase measurement control register */
	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MEAS_CTRL, &dpll_meas_ctrl);
	if (rc)
		return rc;

	/* Enable DPLL measurement block */
	dpll_meas_ctrl |= ZL_DPLL_MEAS_CTRL_EN;

	/* Update phase measurement control register */
	rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_MEAS_CTRL, dpll_meas_ctrl);
	if (rc)
		return rc;

	/* Enable DPLL-to-connected-ref measurement for each channel */
	list_for_each_entry(zldpll, &zldev->dplls, list)
		mask |= BIT(zldpll->id);

	return zl3073x_write_u8(zldev, ZL_REG_DPLL_PHASE_ERR_READ_MASK, mask);
}

/**
 * zl3073x_dev_start - Start normal operation
 * @zldev: zl3073x device pointer
 * @full: perform full initialization
 *
 * The function starts normal operation, which means registering all DPLLs and
 * their pins, and starting monitoring. If full initialization is requested,
 * the function additionally initializes the phase offset measurement block and
 * fetches hardware-invariant parameters.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_dev_start(struct zl3073x_dev *zldev, bool full)
{
	struct zl3073x_dpll *zldpll;
	u8 info;
	int rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_INFO, &info);
	if (rc) {
		dev_err(zldev->dev, "Failed to read device status info\n");
		return rc;
	}

	if (!FIELD_GET(ZL_INFO_READY, info)) {
		/* The ready bit indicates that the firmware was successfully
		 * configured and is ready for normal operation. If it is
		 * cleared then the configuration stored in flash is wrong
		 * or missing. In this situation the driver will expose
		 * only devlink interface to give an opportunity to flash
		 * the correct config.
		 */
		dev_info(zldev->dev,
			 "FW not fully ready - missing or corrupted config\n");

		return 0;
	}

	if (full) {
		/* Fetch device state */
		rc = zl3073x_dev_state_fetch(zldev);
		if (rc)
			return rc;

		/* Setup phase offset measurement block */
		rc = zl3073x_dev_phase_meas_setup(zldev);
		if (rc) {
			dev_err(zldev->dev,
				"Failed to setup phase measurement\n");
			return rc;
		}
	}

	/* Register all DPLLs */
	list_for_each_entry(zldpll, &zldev->dplls, list) {
		rc = zl3073x_dpll_register(zldpll);
		if (rc) {
			dev_err_probe(zldev->dev, rc,
				      "Failed to register DPLL%u\n",
				      zldpll->id);
			return rc;
		}
	}

	/* Perform initial firmware fine phase correction */
	rc = zl3073x_dpll_init_fine_phase_adjust(zldev);
	if (rc) {
		dev_err_probe(zldev->dev, rc,
			      "Failed to init fine phase correction\n");
		return rc;
	}

	/* Start monitoring */
	kthread_queue_delayed_work(zldev->kworker, &zldev->work, 0);

	return 0;
}

/**
 * zl3073x_dev_stop - Stop normal operation
 * @zldev: zl3073x device pointer
 *
 * The function stops the normal operation that mean deregistration of all
 * DPLLs and their pins and stop monitoring.
 *
 * Return: 0 on success, <0 on error
 */
void zl3073x_dev_stop(struct zl3073x_dev *zldev)
{
	struct zl3073x_dpll *zldpll;

	/* Stop monitoring */
	kthread_cancel_delayed_work_sync(&zldev->work);

	/* Unregister all DPLLs */
	list_for_each_entry(zldpll, &zldev->dplls, list) {
		if (zldpll->dpll_dev)
			zl3073x_dpll_unregister(zldpll);
	}
}

static void zl3073x_dev_dpll_fini(void *ptr)
{
	struct zl3073x_dpll *zldpll, *next;
	struct zl3073x_dev *zldev = ptr;

	/* Stop monitoring and unregister DPLLs */
	zl3073x_dev_stop(zldev);

	/* Destroy monitoring thread */
	if (zldev->kworker) {
		kthread_destroy_worker(zldev->kworker);
		zldev->kworker = NULL;
	}

	/* Free all DPLLs */
	list_for_each_entry_safe(zldpll, next, &zldev->dplls, list) {
		list_del(&zldpll->list);
		zl3073x_dpll_free(zldpll);
	}
}

static int
zl3073x_devm_dpll_init(struct zl3073x_dev *zldev, u8 num_dplls)
{
	struct kthread_worker *kworker;
	struct zl3073x_dpll *zldpll;
	unsigned int i;
	int rc;

	INIT_LIST_HEAD(&zldev->dplls);

	/* Allocate all DPLLs */
	for (i = 0; i < num_dplls; i++) {
		zldpll = zl3073x_dpll_alloc(zldev, i);
		if (IS_ERR(zldpll)) {
			dev_err_probe(zldev->dev, PTR_ERR(zldpll),
				      "Failed to alloc DPLL%u\n", i);
			rc = PTR_ERR(zldpll);
			goto error;
		}

		list_add_tail(&zldpll->list, &zldev->dplls);
	}

	/* Initialize monitoring thread */
	kthread_init_delayed_work(&zldev->work, zl3073x_dev_periodic_work);
	kworker = kthread_run_worker(0, "zl3073x-%s", dev_name(zldev->dev));
	if (IS_ERR(kworker)) {
		rc = PTR_ERR(kworker);
		goto error;
	}
	zldev->kworker = kworker;

	/* Start normal operation */
	rc = zl3073x_dev_start(zldev, true);
	if (rc) {
		dev_err_probe(zldev->dev, rc, "Failed to start device\n");
		goto error;
	}

	/* Add devres action to release DPLL related resources */
	rc = devm_add_action_or_reset(zldev->dev, zl3073x_dev_dpll_fini, zldev);
	if (rc)
		goto error;

	return 0;

error:
	zl3073x_dev_dpll_fini(zldev);

	return rc;
}

/**
 * zl3073x_dev_probe - initialize zl3073x device
 * @zldev: pointer to zl3073x device
 * @chip_info: chip info based on compatible
 *
 * Common initialization of zl3073x device structure.
 *
 * Returns: 0 on success, <0 on error
 */
int zl3073x_dev_probe(struct zl3073x_dev *zldev,
		      const struct zl3073x_chip_info *chip_info)
{
	u16 id, revision, fw_ver;
	unsigned int i;
	u32 cfg_ver;
	int rc;

	/* Read chip ID */
	rc = zl3073x_read_u16(zldev, ZL_REG_ID, &id);
	if (rc)
		return rc;

	/* Check it matches */
	for (i = 0; i < chip_info->num_ids; i++) {
		if (id == chip_info->ids[i])
			break;
	}

	if (i == chip_info->num_ids) {
		return dev_err_probe(zldev->dev, -ENODEV,
				     "Unknown or non-match chip ID: 0x%0x\n",
				     id);
	}

	/* Read revision, firmware version and custom config version */
	rc = zl3073x_read_u16(zldev, ZL_REG_REVISION, &revision);
	if (rc)
		return rc;
	rc = zl3073x_read_u16(zldev, ZL_REG_FW_VER, &fw_ver);
	if (rc)
		return rc;
	rc = zl3073x_read_u32(zldev, ZL_REG_CUSTOM_CONFIG_VER, &cfg_ver);
	if (rc)
		return rc;

	dev_dbg(zldev->dev, "ChipID(%X), ChipRev(%X), FwVer(%u)\n", id,
		revision, fw_ver);
	dev_dbg(zldev->dev, "Custom config version: %lu.%lu.%lu.%lu\n",
		FIELD_GET(GENMASK(31, 24), cfg_ver),
		FIELD_GET(GENMASK(23, 16), cfg_ver),
		FIELD_GET(GENMASK(15, 8), cfg_ver),
		FIELD_GET(GENMASK(7, 0), cfg_ver));

	/* Generate random clock ID as the device has not such property that
	 * could be used for this purpose. A user can later change this value
	 * using devlink.
	 */
	zldev->clock_id = get_random_u64();

	/* Default phase offset averaging factor */
	zldev->phase_avg_factor = 2;

	/* Initialize mutex for operations where multiple reads, writes
	 * and/or polls are required to be done atomically.
	 */
	rc = devm_mutex_init(zldev->dev, &zldev->multiop_lock);
	if (rc)
		return dev_err_probe(zldev->dev, rc,
				     "Failed to initialize mutex\n");

	/* Register DPLL channels */
	rc = zl3073x_devm_dpll_init(zldev, chip_info->num_channels);
	if (rc)
		return rc;

	/* Register the devlink instance and parameters */
	rc = zl3073x_devlink_register(zldev);
	if (rc)
		return dev_err_probe(zldev->dev, rc,
				     "Failed to register devlink instance\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(zl3073x_dev_probe, "ZL3073X");

MODULE_AUTHOR("Ivan Vecera <ivecera@redhat.com>");
MODULE_DESCRIPTION("Microchip ZL3073x core driver");
MODULE_LICENSE("GPL");
