// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Synopsys DesignWare I2C adapter driver.
 *
 * Based on the TI DAVINCI I2C adapter driver.
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software Inc.
 * Copyright (C) 2009 Provigent Ltd.
 */

#define DEFAULT_SYMBOL_NAMESPACE	"I2C_DW_COMMON"

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/swab.h>
#include <linux/types.h>
#include <linux/units.h>

#include "i2c-designware-core.h"

#define DW_IC_DEFAULT_BUS_CAPACITANCE_pF	100
#define DW_IC_ABORT_TIMEOUT_US			10
#define DW_IC_BUSY_POLL_TIMEOUT_US		(1 * USEC_PER_MSEC)

static const char *const abort_sources[] = {
	[ABRT_7B_ADDR_NOACK] =
		"slave address not acknowledged (7bit mode)",
	[ABRT_10ADDR1_NOACK] =
		"first address byte not acknowledged (10bit mode)",
	[ABRT_10ADDR2_NOACK] =
		"second address byte not acknowledged (10bit mode)",
	[ABRT_TXDATA_NOACK] =
		"data not acknowledged",
	[ABRT_GCALL_NOACK] =
		"no acknowledgement for a general call",
	[ABRT_GCALL_READ] =
		"read after general call",
	[ABRT_SBYTE_ACKDET] =
		"start byte acknowledged",
	[ABRT_SBYTE_NORSTRT] =
		"trying to send start byte when restart is disabled",
	[ABRT_10B_RD_NORSTRT] =
		"trying to read when restart is disabled (10bit mode)",
	[ABRT_MASTER_DIS] =
		"trying to use disabled adapter",
	[ARB_LOST] =
		"lost arbitration",
	[ABRT_SLAVE_FLUSH_TXFIFO] =
		"read command so flush old data in the TX FIFO",
	[ABRT_SLAVE_ARBLOST] =
		"slave lost the bus while transmitting data to a remote master",
	[ABRT_SLAVE_RD_INTX] =
		"incorrect slave-transmitter mode configuration",
};

static int dw_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct dw_i2c_dev *dev = context;

	*val = readl(dev->base + reg);

	return 0;
}

static int dw_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct dw_i2c_dev *dev = context;

	writel(val, dev->base + reg);

	return 0;
}

static int dw_reg_read_swab(void *context, unsigned int reg, unsigned int *val)
{
	struct dw_i2c_dev *dev = context;

	*val = swab32(readl(dev->base + reg));

	return 0;
}

static int dw_reg_write_swab(void *context, unsigned int reg, unsigned int val)
{
	struct dw_i2c_dev *dev = context;

	writel(swab32(val), dev->base + reg);

	return 0;
}

static int dw_reg_read_word(void *context, unsigned int reg, unsigned int *val)
{
	struct dw_i2c_dev *dev = context;

	*val = readw(dev->base + reg) |
		(readw(dev->base + reg + DW_IC_REG_STEP_BYTES) << DW_IC_REG_WORD_SHIFT);

	return 0;
}

static int dw_reg_write_word(void *context, unsigned int reg, unsigned int val)
{
	struct dw_i2c_dev *dev = context;

	writew(val, dev->base + reg);
	writew(val >> DW_IC_REG_WORD_SHIFT, dev->base + reg + DW_IC_REG_STEP_BYTES);

	return 0;
}

/**
 * i2c_dw_init_regmap() - Initialize registers map
 * @dev: device private data
 *
 * Autodetects needed register access mode and creates the regmap with
 * corresponding read/write callbacks. This must be called before doing any
 * other register access.
 *
 * Return: 0 on success, or negative errno otherwise.
 */
static int i2c_dw_init_regmap(struct dw_i2c_dev *dev)
{
	struct regmap_config map_cfg = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.disable_locking = true,
		.reg_read = dw_reg_read,
		.reg_write = dw_reg_write,
		.max_register = DW_IC_COMP_TYPE,
	};
	u32 reg;
	int ret;

	/*
	 * Skip detecting the registers map configuration if the regmap has
	 * already been provided by a higher code.
	 */
	if (dev->map)
		return 0;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	reg = readl(dev->base + DW_IC_COMP_TYPE);
	i2c_dw_release_lock(dev);

	if ((dev->flags & MODEL_MASK) == MODEL_AMD_NAVI_GPU)
		map_cfg.max_register = AMD_UCSI_INTR_REG;

	if (reg == swab32(DW_IC_COMP_TYPE_VALUE)) {
		map_cfg.reg_read = dw_reg_read_swab;
		map_cfg.reg_write = dw_reg_write_swab;
	} else if (reg == lower_16_bits(DW_IC_COMP_TYPE_VALUE)) {
		map_cfg.reg_read = dw_reg_read_word;
		map_cfg.reg_write = dw_reg_write_word;
	} else if (reg != DW_IC_COMP_TYPE_VALUE) {
		dev_err(dev->dev,
			"Unknown Synopsys component type: 0x%08x\n", reg);
		return -ENODEV;
	}

	/*
	 * Note we'll check the return value of the regmap IO accessors only
	 * at the probe stage. The rest of the code won't do this because
	 * basically we have MMIO-based regmap, so none of the read/write methods
	 * can fail.
	 */
	dev->map = devm_regmap_init(dev->dev, NULL, dev, &map_cfg);
	if (IS_ERR(dev->map)) {
		dev_err(dev->dev, "Failed to init the registers map\n");
		return PTR_ERR(dev->map);
	}

	return 0;
}

static const u32 supported_speeds[] = {
	I2C_MAX_HIGH_SPEED_MODE_FREQ,
	I2C_MAX_FAST_MODE_PLUS_FREQ,
	I2C_MAX_FAST_MODE_FREQ,
	I2C_MAX_STANDARD_MODE_FREQ,
};

static int i2c_dw_validate_speed(struct dw_i2c_dev *dev)
{
	struct i2c_timings *t = &dev->timings;
	unsigned int i;

	/*
	 * Only standard mode at 100kHz, fast mode at 400kHz,
	 * fast mode plus at 1MHz and high speed mode at 3.4MHz are supported.
	 */
	for (i = 0; i < ARRAY_SIZE(supported_speeds); i++) {
		if (t->bus_freq_hz == supported_speeds[i])
			return 0;
	}

	dev_err(dev->dev,
		"%d Hz is unsupported, only 100kHz, 400kHz, 1MHz and 3.4MHz are supported\n",
		t->bus_freq_hz);

	return -EINVAL;
}

#ifdef CONFIG_OF

#include <linux/platform_device.h>

#define MSCC_ICPU_CFG_TWI_DELAY		0x0
#define MSCC_ICPU_CFG_TWI_DELAY_ENABLE	BIT(0)
#define MSCC_ICPU_CFG_TWI_SPIKE_FILTER	0x4

static int mscc_twi_set_sda_hold_time(struct dw_i2c_dev *dev)
{
	writel((dev->sda_hold_time << 1) | MSCC_ICPU_CFG_TWI_DELAY_ENABLE,
	       dev->ext + MSCC_ICPU_CFG_TWI_DELAY);

	return 0;
}

static void i2c_dw_of_configure(struct device *device)
{
	struct platform_device *pdev = to_platform_device(device);
	struct dw_i2c_dev *dev = dev_get_drvdata(device);

	if (device_is_compatible(dev->dev, "mscc,ocelot-i2c")) {
		dev->ext = devm_platform_ioremap_resource(pdev, 1);
		if (!IS_ERR(dev->ext))
			dev->set_sda_hold_time = mscc_twi_set_sda_hold_time;
	}
}

#else	/* CONFIG_OF */

static inline void i2c_dw_of_configure(struct device *device) { }

#endif	/* CONFIG_OF */

#ifdef CONFIG_ACPI

#include <linux/dmi.h>

/*
 * The HCNT/LCNT information coming from ACPI should be the most accurate
 * for given platform. However, some systems get it wrong. On such systems
 * we get better results by calculating those based on the input clock.
 */
static const struct dmi_system_id i2c_dw_no_acpi_params[] = {
	{
		.ident = "Dell Inspiron 7348",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 7348"),
		},
	},
	{}
};

static void i2c_dw_acpi_params(struct device *device, char method[],
			       u16 *hcnt, u16 *lcnt, u32 *sda_hold)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER };
	acpi_handle handle = ACPI_HANDLE(device);
	union acpi_object *obj;

	if (dmi_check_system(i2c_dw_no_acpi_params))
		return;

	if (ACPI_FAILURE(acpi_evaluate_object(handle, method, NULL, &buf)))
		return;

	obj = (union acpi_object *)buf.pointer;
	if (obj->type == ACPI_TYPE_PACKAGE && obj->package.count == 3) {
		const union acpi_object *objs = obj->package.elements;

		*hcnt = (u16)objs[0].integer.value;
		*lcnt = (u16)objs[1].integer.value;
		*sda_hold = (u32)objs[2].integer.value;
	}

	kfree(buf.pointer);
}

static void i2c_dw_acpi_configure(struct device *device)
{
	struct dw_i2c_dev *dev = dev_get_drvdata(device);
	struct i2c_timings *t = &dev->timings;
	u32 ss_ht = 0, fp_ht = 0, hs_ht = 0, fs_ht = 0;

	/*
	 * Try to get SDA hold time and *CNT values from an ACPI method for
	 * selected speed modes.
	 */
	i2c_dw_acpi_params(device, "SSCN", &dev->ss_hcnt, &dev->ss_lcnt, &ss_ht);
	i2c_dw_acpi_params(device, "FMCN", &dev->fs_hcnt, &dev->fs_lcnt, &fs_ht);
	i2c_dw_acpi_params(device, "FPCN", &dev->fp_hcnt, &dev->fp_lcnt, &fp_ht);
	i2c_dw_acpi_params(device, "HSCN", &dev->hs_hcnt, &dev->hs_lcnt, &hs_ht);

	switch (t->bus_freq_hz) {
	case I2C_MAX_STANDARD_MODE_FREQ:
		dev->sda_hold_time = ss_ht;
		break;
	case I2C_MAX_FAST_MODE_PLUS_FREQ:
		dev->sda_hold_time = fp_ht;
		break;
	case I2C_MAX_HIGH_SPEED_MODE_FREQ:
		dev->sda_hold_time = hs_ht;
		break;
	case I2C_MAX_FAST_MODE_FREQ:
	default:
		dev->sda_hold_time = fs_ht;
		break;
	}
}

static u32 i2c_dw_acpi_round_bus_speed(struct device *device)
{
	u32 acpi_speed;
	int i;

	acpi_speed = i2c_acpi_find_bus_speed(device);
	/*
	 * Some DSDTs use a non standard speed, round down to the lowest
	 * standard speed.
	 */
	for (i = 0; i < ARRAY_SIZE(supported_speeds); i++) {
		if (acpi_speed >= supported_speeds[i])
			return supported_speeds[i];
	}

	return 0;
}

#else	/* CONFIG_ACPI */

static inline void i2c_dw_acpi_configure(struct device *device) { }

static inline u32 i2c_dw_acpi_round_bus_speed(struct device *device) { return 0; }

#endif	/* CONFIG_ACPI */

static void i2c_dw_configure_mode(struct dw_i2c_dev *dev, int mode)
{
	switch (mode) {
	case DW_IC_MASTER:
		regmap_write(dev->map, DW_IC_TX_TL, dev->tx_fifo_depth / 2);
		regmap_write(dev->map, DW_IC_RX_TL, 0);
		regmap_write(dev->map, DW_IC_CON, dev->master_cfg);
		break;
	case DW_IC_SLAVE:
		dev->status = 0;
		regmap_write(dev->map, DW_IC_TX_TL, 0);
		regmap_write(dev->map, DW_IC_RX_TL, 0);
		regmap_write(dev->map, DW_IC_CON, dev->slave_cfg);
		regmap_write(dev->map, DW_IC_SAR, dev->slave->addr);
		regmap_write(dev->map, DW_IC_INTR_MASK, DW_IC_INTR_SLAVE_MASK);
		__i2c_dw_enable(dev);
		break;
	default:
		WARN(1, "Invalid mode %d\n", mode);
		return;
	}
}

static void i2c_dw_write_timings(struct dw_i2c_dev *dev)
{
	/* Write standard speed timing parameters */
	regmap_write(dev->map, DW_IC_SS_SCL_HCNT, dev->ss_hcnt);
	regmap_write(dev->map, DW_IC_SS_SCL_LCNT, dev->ss_lcnt);

	/* Write fast mode/fast mode plus timing parameters */
	regmap_write(dev->map, DW_IC_FS_SCL_HCNT, dev->fs_hcnt);
	regmap_write(dev->map, DW_IC_FS_SCL_LCNT, dev->fs_lcnt);

	/* Write high speed timing parameters */
	regmap_write(dev->map, DW_IC_HS_SCL_HCNT, dev->hs_hcnt);
	regmap_write(dev->map, DW_IC_HS_SCL_LCNT, dev->hs_lcnt);
}

/**
 * i2c_dw_set_mode() - Select the controller mode of operation - master or slave
 * @dev: device private data
 * @mode: I2C mode of operation
 *
 * Configures the controller to operate in @mode. This function needs to be
 * called when ever a mode swap is required.
 *
 * Setting the slave mode does not have an effect before a slave device is
 * registered. So before the slave device is registered, the controller is kept
 * in master mode regardless of @mode.
 *
 * The controller must be disabled before this function is called.
 */
void i2c_dw_set_mode(struct dw_i2c_dev *dev, int mode)
{
	if (mode == DW_IC_SLAVE && !dev->slave)
		mode = DW_IC_MASTER;
	if (dev->mode == mode)
		return;

	i2c_dw_configure_mode(dev, mode);
	dev->mode = mode;
}

/**
 * i2c_dw_init() - Initialize the DesignWare I2C hardware
 * @dev: device private data
 *
 * This functions configures and enables the DesigWare I2C hardware.
 *
 * Return: 0 on success, or negative errno otherwise.
 */
int i2c_dw_init(struct dw_i2c_dev *dev)
{
	int ret;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	/* Disable the adapter */
	__i2c_dw_disable(dev);

	/*
	 * Mask SMBus interrupts to block storms from broken
	 * firmware that leaves IC_SMBUS=1; the handler never
	 * services them.
	 */
	regmap_write(dev->map, DW_IC_SMBUS_INTR_MASK, 0);

	i2c_dw_write_timings(dev);

	/* Write SDA hold time if supported */
	if (dev->sda_hold_time)
		regmap_write(dev->map, DW_IC_SDA_HOLD, dev->sda_hold_time);

	i2c_dw_configure_mode(dev, dev->mode);

	i2c_dw_release_lock(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(i2c_dw_init);

static void i2c_dw_adjust_bus_speed(struct dw_i2c_dev *dev)
{
	u32 acpi_speed = i2c_dw_acpi_round_bus_speed(dev->dev);
	struct i2c_timings *t = &dev->timings;

	/*
	 * Find bus speed from the "clock-frequency" device property, ACPI
	 * or by using fast mode if neither is set.
	 */
	if (acpi_speed && t->bus_freq_hz)
		t->bus_freq_hz = min(t->bus_freq_hz, acpi_speed);
	else if (acpi_speed || t->bus_freq_hz)
		t->bus_freq_hz = max(t->bus_freq_hz, acpi_speed);
	else
		t->bus_freq_hz = I2C_MAX_FAST_MODE_FREQ;
}

int i2c_dw_fw_parse_and_configure(struct dw_i2c_dev *dev)
{
	struct i2c_timings *t = &dev->timings;
	struct device *device = dev->dev;
	struct fwnode_handle *fwnode = dev_fwnode(device);

	i2c_parse_fw_timings(device, t, false);

	if (device_property_read_u32(device, "snps,bus-capacitance-pf", &dev->bus_capacitance_pF))
		dev->bus_capacitance_pF = DW_IC_DEFAULT_BUS_CAPACITANCE_pF;

	dev->clk_freq_optimized = device_property_read_bool(device, "snps,clk-freq-optimized");

	/* Mobileye controllers do not hold the clock on empty FIFO */
	if (device_is_compatible(device, "mobileye,eyeq6lplus-i2c"))
		dev->emptyfifo_hold_master = false;
	else
		dev->emptyfifo_hold_master = true;

	i2c_dw_adjust_bus_speed(dev);

	if (is_of_node(fwnode))
		i2c_dw_of_configure(device);
	else if (is_acpi_node(fwnode))
		i2c_dw_acpi_configure(device);

	return i2c_dw_validate_speed(dev);
}
EXPORT_SYMBOL_GPL(i2c_dw_fw_parse_and_configure);

static u32 i2c_dw_read_scl_reg(struct dw_i2c_dev *dev, u32 reg)
{
	u32 val;
	int ret;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return 0;

	ret = regmap_read(dev->map, reg, &val);
	i2c_dw_release_lock(dev);

	return ret ? 0 : val;
}

u32 i2c_dw_scl_hcnt(struct dw_i2c_dev *dev, unsigned int reg, u32 ic_clk,
		    u32 tSYMBOL, u32 tf, int offset)
{
	if (!ic_clk)
		return i2c_dw_read_scl_reg(dev, reg);

	/*
	 * Conditional expression:
	 *
	 *   IC_[FS]S_SCL_HCNT + 3 >= IC_CLK * (tHD;STA + tf)
	 *
	 * This is just experimental rule; the tHD;STA period turned
	 * out to be proportinal to (_HCNT + 3).  With this setting,
	 * we could meet both tHIGH and tHD;STA timing specs.
	 *
	 * If unsure, you'd better to take this alternative.
	 *
	 * The reason why we need to take into account "tf" here,
	 * is the same as described in i2c_dw_scl_lcnt().
	 */
	return DIV_ROUND_CLOSEST_ULL((u64)ic_clk * (tSYMBOL + tf), MICRO) - 3 + offset;
}

u32 i2c_dw_scl_lcnt(struct dw_i2c_dev *dev, unsigned int reg, u32 ic_clk,
		    u32 tLOW, u32 tf, int offset)
{
	if (!ic_clk)
		return i2c_dw_read_scl_reg(dev, reg);

	/*
	 * Conditional expression:
	 *
	 *   IC_[FS]S_SCL_LCNT + 1 >= IC_CLK * (tLOW + tf)
	 *
	 * DW I2C core starts counting the SCL CNTs for the LOW period
	 * of the SCL clock (tLOW) as soon as it pulls the SCL line.
	 * In order to meet the tLOW timing spec, we need to take into
	 * account the fall time of SCL signal (tf).  Default tf value
	 * should be 0.3 us, for safety.
	 */
	return DIV_ROUND_CLOSEST_ULL((u64)ic_clk * (tLOW + tf), MICRO) - 1 + offset;
}

static int i2c_dw_set_sda_hold(struct dw_i2c_dev *dev)
{
	unsigned int reg;
	int ret;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	/* Configure SDA Hold Time if required */
	ret = regmap_read(dev->map, DW_IC_COMP_VERSION, &reg);
	if (ret)
		goto err_release_lock;

	if (reg >= DW_IC_SDA_HOLD_MIN_VERS) {
		if (!dev->sda_hold_time) {
			/* Keep previous hold time setting if no one set it */
			ret = regmap_read(dev->map, DW_IC_SDA_HOLD,
					  &dev->sda_hold_time);
			if (ret)
				goto err_release_lock;
		}

		/*
		 * Workaround for avoiding TX arbitration lost in case I2C
		 * slave pulls SDA down "too quickly" after falling edge of
		 * SCL by enabling non-zero SDA RX hold. Specification says it
		 * extends incoming SDA low to high transition while SCL is
		 * high but it appears to help also above issue.
		 */
		if (!(dev->sda_hold_time & DW_IC_SDA_HOLD_RX_MASK))
			dev->sda_hold_time |= 1 << DW_IC_SDA_HOLD_RX_SHIFT;

		dev_dbg(dev->dev, "SDA Hold Time TX:RX = %d:%d\n",
			dev->sda_hold_time & ~(u32)DW_IC_SDA_HOLD_RX_MASK,
			dev->sda_hold_time >> DW_IC_SDA_HOLD_RX_SHIFT);
	} else if (dev->set_sda_hold_time) {
		dev->set_sda_hold_time(dev);
	} else if (dev->sda_hold_time) {
		dev_warn(dev->dev,
			"Hardware too old to adjust SDA hold time.\n");
		dev->sda_hold_time = 0;
	}

err_release_lock:
	i2c_dw_release_lock(dev);

	return ret;
}

void __i2c_dw_disable(struct dw_i2c_dev *dev)
{
	struct i2c_timings *t = &dev->timings;
	unsigned int raw_intr_stats, ic_stats;
	unsigned int enable;
	int timeout = 100;
	bool abort_needed;
	unsigned int status;
	int ret;

	regmap_read(dev->map, DW_IC_RAW_INTR_STAT, &raw_intr_stats);
	regmap_read(dev->map, DW_IC_STATUS, &ic_stats);
	regmap_read(dev->map, DW_IC_ENABLE, &enable);

	abort_needed = (raw_intr_stats & DW_IC_INTR_MST_ON_HOLD) ||
			(ic_stats & DW_IC_STATUS_MASTER_HOLD_TX_FIFO_EMPTY);
	if (abort_needed) {
		if (!(enable & DW_IC_ENABLE_ENABLE)) {
			regmap_write(dev->map, DW_IC_ENABLE, DW_IC_ENABLE_ENABLE);
			/*
			 * Wait 10 times the signaling period of the highest I2C
			 * transfer supported by the driver (for 400KHz this is
			 * 25us) to ensure the I2C ENABLE bit is already set
			 * as described in the DesignWare I2C databook.
			 */
			fsleep(DIV_ROUND_CLOSEST_ULL(10 * MICRO, t->bus_freq_hz));
			/* Set ENABLE bit before setting ABORT */
			enable |= DW_IC_ENABLE_ENABLE;
		}

		regmap_write(dev->map, DW_IC_ENABLE, enable | DW_IC_ENABLE_ABORT);
		ret = regmap_read_poll_timeout(dev->map, DW_IC_ENABLE, enable,
					       !(enable & DW_IC_ENABLE_ABORT),
					       DW_IC_ABORT_TIMEOUT_US,
					       10 * DW_IC_ABORT_TIMEOUT_US);
		if (ret)
			dev_err(dev->dev, "timeout while trying to abort current transfer\n");
	}

	do {
		__i2c_dw_disable_nowait(dev);
		/*
		 * The enable status register may be unimplemented, but
		 * in that case this test reads zero and exits the loop.
		 */
		regmap_read(dev->map, DW_IC_ENABLE_STATUS, &status);
		if (!(status & 1))
			return;

		/*
		 * Wait 10 times the signaling period of the highest I2C
		 * transfer supported by the driver (for 400kHz this is
		 * 25us) as described in the DesignWare I2C databook.
		 */
		usleep_range(25, 250);
	} while (timeout--);

	dev_warn(dev->dev, "timeout in disabling adapter\n");
}

u32 i2c_dw_clk_rate(struct dw_i2c_dev *dev)
{
	/*
	 * Clock is not necessary if we got LCNT/HCNT values directly from
	 * the platform code.
	 */
	if (!dev->get_clk_rate_khz) {
		dev_dbg_once(dev->dev, "Callback get_clk_rate_khz() is not defined\n");
		return 0;
	}
	return dev->get_clk_rate_khz(dev);
}

int i2c_dw_prepare_clk(struct dw_i2c_dev *dev, bool prepare)
{
	int ret;

	if (prepare) {
		/* Optional interface clock */
		ret = clk_prepare_enable(dev->pclk);
		if (ret)
			return ret;

		ret = clk_prepare_enable(dev->clk);
		if (ret)
			clk_disable_unprepare(dev->pclk);

		return ret;
	}

	clk_disable_unprepare(dev->clk);
	clk_disable_unprepare(dev->pclk);

	return 0;
}
EXPORT_SYMBOL_GPL(i2c_dw_prepare_clk);

int i2c_dw_acquire_lock(struct dw_i2c_dev *dev)
{
	int ret;

	if (!dev->acquire_lock)
		return 0;

	ret = dev->acquire_lock();
	if (!ret)
		return 0;

	dev_err(dev->dev, "couldn't acquire bus ownership\n");

	return ret;
}

void i2c_dw_release_lock(struct dw_i2c_dev *dev)
{
	if (dev->release_lock)
		dev->release_lock();
}

/*
 * Waiting for bus not busy
 */
int i2c_dw_wait_bus_not_busy(struct dw_i2c_dev *dev)
{
	unsigned int status;
	int ret;

	ret = regmap_read_poll_timeout(dev->map, DW_IC_STATUS, status,
				       !(status & DW_IC_STATUS_ACTIVITY),
				       DW_IC_BUSY_POLL_TIMEOUT_US,
				       20 * DW_IC_BUSY_POLL_TIMEOUT_US);
	if (ret) {
		dev_warn(dev->dev, "timeout waiting for bus ready\n");

		i2c_recover_bus(&dev->adapter);

		regmap_read(dev->map, DW_IC_STATUS, &status);
		if (!(status & DW_IC_STATUS_ACTIVITY))
			ret = 0;
	}

	return ret;
}

int i2c_dw_handle_tx_abort(struct dw_i2c_dev *dev)
{
	unsigned long abort_source = dev->abort_source;
	int i;

	if (abort_source & DW_IC_TX_ABRT_NOACK) {
		for_each_set_bit(i, &abort_source, ARRAY_SIZE(abort_sources))
			dev_dbg(dev->dev,
				"%s: %s\n", __func__, abort_sources[i]);
		return -EREMOTEIO;
	}

	for_each_set_bit(i, &abort_source, ARRAY_SIZE(abort_sources))
		dev_err(dev->dev, "%s: %s\n", __func__, abort_sources[i]);

	if (abort_source & DW_IC_TX_ARB_LOST)
		return -EAGAIN;
	if (abort_source & DW_IC_TX_ABRT_GCALL_READ)
		return -EINVAL; /* wrong msgs[] data */

	return -EIO;
}

static int i2c_dw_set_fifo_size(struct dw_i2c_dev *dev)
{
	u32 tx_fifo_depth, rx_fifo_depth;
	unsigned int param;
	int ret;

	/* DW_IC_COMP_PARAM_1 not implement for IP issue */
	if ((dev->flags & MODEL_MASK) == MODEL_WANGXUN_SP) {
		dev->tx_fifo_depth = TXGBE_TX_FIFO_DEPTH;
		dev->rx_fifo_depth = TXGBE_RX_FIFO_DEPTH;

		return 0;
	}

	/*
	 * Try to detect the FIFO depth if not set by interface driver,
	 * the depth could be from 2 to 256 from HW spec.
	 */
	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	ret = regmap_read(dev->map, DW_IC_COMP_PARAM_1, &param);
	i2c_dw_release_lock(dev);
	if (ret)
		return ret;

	tx_fifo_depth = FIELD_GET(DW_IC_FIFO_TX_FIELD, param) + 1;
	rx_fifo_depth = FIELD_GET(DW_IC_FIFO_RX_FIELD, param) + 1;
	if (!dev->tx_fifo_depth) {
		dev->tx_fifo_depth = tx_fifo_depth;
		dev->rx_fifo_depth = rx_fifo_depth;
	} else if (tx_fifo_depth >= DW_IC_FIFO_MIN_DEPTH) {
		dev->tx_fifo_depth = min_t(u32, dev->tx_fifo_depth,
				tx_fifo_depth);
		dev->rx_fifo_depth = min_t(u32, dev->rx_fifo_depth,
				rx_fifo_depth);
	}

	return 0;
}

u32 i2c_dw_func(struct i2c_adapter *adap)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);

	return dev->functionality;
}

void i2c_dw_disable(struct dw_i2c_dev *dev)
{
	unsigned int dummy;
	int ret;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return;

	/* Disable controller */
	__i2c_dw_disable(dev);

	/* Disable all interrupts */
	__i2c_dw_write_intr_mask(dev, 0);
	regmap_read(dev->map, DW_IC_CLR_INTR, &dummy);

	i2c_dw_release_lock(dev);
}
EXPORT_SYMBOL_GPL(i2c_dw_disable);

static irqreturn_t i2c_dw_isr(int this_irq, void *dev_id)
{
	struct dw_i2c_dev *dev = dev_id;

	if (dev->mode == DW_IC_SLAVE)
		return i2c_dw_isr_slave(dev);

	return i2c_dw_isr_master(dev);
}

static const struct i2c_algorithm i2c_dw_algo = {
	.xfer = i2c_dw_xfer,
	.functionality = i2c_dw_func,
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	.reg_slave = i2c_dw_reg_slave,
	.unreg_slave = i2c_dw_unreg_slave,
#endif
};

static const struct i2c_adapter_quirks i2c_dw_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
};

int i2c_dw_probe(struct dw_i2c_dev *dev)
{
	struct i2c_adapter *adap = &dev->adapter;
	unsigned long irq_flags;
	int ret;

	device_set_node(&dev->adapter.dev, dev_fwnode(dev->dev));

	ret = i2c_dw_init_regmap(dev);
	if (ret)
		return ret;

	ret = i2c_dw_set_sda_hold(dev);
	if (ret)
		return ret;

	ret = i2c_dw_set_fifo_size(dev);
	if (ret)
		return ret;

	ret = i2c_dw_probe_master(dev);
	if (ret)
		return ret;

	ret = i2c_dw_init(dev);
	if (ret)
		return ret;

	if (!adap->name[0])
		strscpy(adap->name, "Synopsys DesignWare I2C adapter");

	adap->retries = 3;
	adap->algo = &i2c_dw_algo;
	adap->quirks = &i2c_dw_quirks;
	adap->dev.parent = dev->dev;
	i2c_set_adapdata(adap, dev);

	/*
	 * REVISIT: The mode check may not be necessary.
	 * For now keeping the flags as they were originally.
	 */
	if (dev->mode == DW_IC_SLAVE)
		irq_flags = IRQF_SHARED;
	else if (dev->flags & ACCESS_NO_IRQ_SUSPEND)
		irq_flags = IRQF_NO_SUSPEND;
	else
		irq_flags = IRQF_SHARED | IRQF_COND_SUSPEND;

	/*
	 * The first writing to TX FIFO buffer causes transmission start.
	 * If IC_EMPTYFIFO_HOLD_MASTER_EN is not set, when TX FIFO gets
	 * empty, I2C controller finishes the transaction. If writing to
	 * FIFO is interrupted, FIFO can get empty and the transaction will
	 * be finished prematurely. FIFO buffer is filled in IRQ handler,
	 * but in PREEMPT_RT kernel IRQ handler by default is executed
	 * in thread that can be preempted with another higher priority
	 * thread or an interrupt. So, IRQF_NO_THREAD flag is required in
	 * order to prevent any preemption when filling the FIFO.
	 */
	if (!dev->emptyfifo_hold_master)
		irq_flags |= IRQF_NO_THREAD;

	ret = i2c_dw_acquire_lock(dev);
	if (ret)
		return ret;

	__i2c_dw_write_intr_mask(dev, 0);
	i2c_dw_release_lock(dev);

	if (!(dev->flags & ACCESS_POLLING)) {
		ret = devm_request_irq(dev->dev, dev->irq, i2c_dw_isr,
				       irq_flags, dev_name(dev->dev), dev);
		if (ret)
			return ret;
	}

	/*
	 * Increment PM usage count during adapter registration in order to
	 * avoid possible spurious runtime suspend when adapter device is
	 * registered to the device core and immediate resume in case bus has
	 * registered I2C slaves that do I2C transfers in their probe.
	 */
	ACQUIRE(pm_runtime_noresume, pm)(dev->dev);
	ret = ACQUIRE_ERR(pm_runtime_noresume, &pm);
	if (ret)
		return ret;

	return i2c_add_numbered_adapter(adap);
}
EXPORT_SYMBOL_GPL(i2c_dw_probe);

static int i2c_dw_prepare(struct device *device)
{
	/*
	 * If the ACPI companion device object is present for this device,
	 * it may be accessed during suspend and resume of other devices via
	 * I2C operation regions, so tell the PM core and middle layers to
	 * avoid skipping system suspend/resume callbacks for it in that case.
	 */
	return !has_acpi_companion(device);
}

static int i2c_dw_runtime_suspend(struct device *device)
{
	struct dw_i2c_dev *dev = dev_get_drvdata(device);

	if (dev->shared_with_punit)
		return 0;

	i2c_dw_disable(dev);
	i2c_dw_prepare_clk(dev, false);

	return 0;
}

static int i2c_dw_suspend(struct device *device)
{
	struct dw_i2c_dev *dev = dev_get_drvdata(device);

	i2c_mark_adapter_suspended(&dev->adapter);

	return i2c_dw_runtime_suspend(device);
}

static int i2c_dw_runtime_resume(struct device *device)
{
	struct dw_i2c_dev *dev = dev_get_drvdata(device);

	if (!dev->shared_with_punit)
		i2c_dw_prepare_clk(dev, true);

	i2c_dw_init(dev);

	return 0;
}

static int i2c_dw_resume(struct device *device)
{
	struct dw_i2c_dev *dev = dev_get_drvdata(device);

	i2c_dw_runtime_resume(device);
	i2c_mark_adapter_resumed(&dev->adapter);

	return 0;
}

EXPORT_GPL_DEV_PM_OPS(i2c_dw_dev_pm_ops) = {
	.prepare = pm_sleep_ptr(i2c_dw_prepare),
	LATE_SYSTEM_SLEEP_PM_OPS(i2c_dw_suspend, i2c_dw_resume)
	RUNTIME_PM_OPS(i2c_dw_runtime_suspend, i2c_dw_runtime_resume, NULL)
};

MODULE_DESCRIPTION("Synopsys DesignWare I2C bus adapter core");
MODULE_LICENSE("GPL");
