// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MPS MPQ8646 step-down converter.
 *
 * Copyright (c) 2026 Free Mobile - Vincent Jardin <vjardin@free.fr>
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/pmbus.h>
#include <linux/property.h>
#include <linux/regulator/driver.h>
#include <linux/seq_file.h>
#include <linux/workqueue.h>
#include "pmbus.h"

/* Default cadence for the in-driver alarm-poll fallback (ms) */
#define MPQ8646_ALARM_POLL_MS_DEFAULT	1000

/* MPS vendor-extended command codes (NOT in PMBus 1.3 Part II) */
#define MPS_CLEAR_LAST_FAULT		0x08
#define MPS_MFR_CFG_EXT			0xF5
#define MPS_MFR_CFG_EXT_CLR_LAST_EN	BIT(6)
#define MPS_PROTECTION_LAST		0xFB

/*
 * PMBus 1.3 NVM commit / revert commands. MPS equivalent of
 * STORE_ALL (15h) and RESTORE_ALL (16h).
 */
#define PMBUS_STORE_USER_ALL		0x15
#define PMBUS_RESTORE_USER_ALL		0x16

/* PMBus 1.3 timing / UVLO command codes */
#define PMBUS_VIN_ON			0x35
#define PMBUS_VIN_OFF			0x36
#define PMBUS_TON_DELAY			0x60
#define PMBUS_TON_RISE			0x61
#define PMBUS_TOFF_DELAY		0x64
#define PMBUS_TOFF_FALL			0x65

/* MPS vendor-extended observability / identity registers */
#define MPS_MFR_CONFIG_ID		0xC0
#define MPS_MFR_CONFIG_CODE_REV		0xC1
#define MPS_MFR_PRODUCT_REV_USER	0xC2
#define MPS_MFR_SILICON_REV		0xC3
#define MPS_MFR_RETRY_TIMES		0xF4
#define MPS_MFR_VBOOT_CFG		0xFC

/*
 * MPS_MFR_PMBUS_LOCK (EEh): 16-bit WORD whose low two bits gate
 * subsequent PMBus writes
 *   bits[1:0] = 00  -- unlocked (POR default)
 *               01  -- lock all writes EXCEPT VOUT_COMMAND (0x21)
 *                      so the operator can still DVFS the rail
 *               11  -- lock all writes
 * A negative-going PG edge resets these bits to 00, the lock
 * is operationally reversible without a full chip POR.
 */
#define MPS_MFR_PMBUS_LOCK		0xEE

/*
 * Retry parameters for the MFR_CFG_EXT gate-close write after
 * CLEAR_LAST_FAULT. Bench-observed NVM-busy NACK window on this
 * silicon is about 1 ms; the datasheet does not have information.
 */
#define MPQ8646_NVM_RETRY_MAX		5
#define MPQ8646_NVM_RETRY_DELAY_US_MIN	2000
#define MPQ8646_NVM_RETRY_DELAY_US_MAX	4000

#define MPQ8646_DEBUG(client, fmt, ...) \
	dev_dbg(&(client)->dev, fmt, ##__VA_ARGS__)

/*
 * Maximum legal value for VOUT_SCALE_LOOP (0x29) on the MPQ8646 silicon
 * The chip uses an 11-bit VOUT feedback-divider scale register.
 */
#define MPQ8646_VOUT_SCALE_LOOP_MAX	GENMASK(10, 0)

/* Forward declaration */
struct mpq8646_dbg_reg_ctx;

/* Per-instance state */
struct mpq8646_priv {
	struct pmbus_driver_info info;	/* must be first, container_of target */
	struct i2c_client	*client;

	/* Serialises CLEAR_LAST_FAULT sequences and PROTECTION_LAST reads */
	struct mutex		mps_lock;

	/* Set alarm_poll_interval_ms = 0 to disable. */
	struct delayed_work	alarm_poll_work;
	u32			alarm_poll_interval_ms;

#ifdef CONFIG_DEBUG_FS
	/* the only debugfs file that can re-arm the poll worker */
	struct dentry		*dbg_poll;
	struct mpq8646_dbg_reg_ctx	*dbg_reg_ctx;
#endif
};

static inline struct mpq8646_priv *mpq8646_priv_from_client(struct i2c_client *client)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);

	return container_of(info, struct mpq8646_priv, info);
}

/*
 * VID-mode m/b/R coefficients, same values as the mpq8785 driver for
 * the MPS VID encoding. Per the PMBus Direct formula used by
 * pmbus_core, X = (Y * 10^-R - b) / m, so m=64 / b=0 / R=1 yields
 * 1.5625 mV per LSB.
 */
#define MPQ8646_VID_M			64
#define MPQ8646_VID_B			0
#define MPQ8646_VID_R			1

static int mpq8646_identify(struct i2c_client *client,
			    struct pmbus_driver_info *info)
{
	int vout_mode;

	vout_mode = pmbus_read_byte_data(client, 0, PMBUS_VOUT_MODE);
	if (vout_mode < 0)
		return vout_mode;

	switch (vout_mode & PB_VOUT_MODE_MODE_MASK) {
	case PB_VOUT_MODE_LINEAR:
		info->format[PSC_VOLTAGE_OUT] = linear;
		break;
	case PB_VOUT_MODE_VID:
	case PB_VOUT_MODE_DIRECT:
		info->format[PSC_VOLTAGE_OUT] = direct;
		info->m[PSC_VOLTAGE_OUT] = MPQ8646_VID_M;
		info->b[PSC_VOLTAGE_OUT] = MPQ8646_VID_B;
		info->R[PSC_VOLTAGE_OUT] = MPQ8646_VID_R;
		break;
	default:
		return -ENODEV;
	}

	return 0;
};

static int mpq8646_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	MPQ8646_DEBUG(client, "read_byte_data page=%d reg=0x%02x\n", page, reg);

	switch (reg) {
	case PMBUS_VOUT_MODE:
		ret = pmbus_read_byte_data(client, page, reg);
		MPQ8646_DEBUG(client, "  VOUT_MODE raw=0x%02x ret=%d\n", ret, ret);
		if (ret < 0)
			return ret;

		if ((ret & PB_VOUT_MODE_MODE_MASK) == PB_VOUT_MODE_VID)
			return PB_VOUT_MODE_DIRECT;

		return ret;
	case PMBUS_STATUS_BYTE:
	case PMBUS_STATUS_CML:
	case PMBUS_STATUS_OTHER:
	case PMBUS_STATUS_MFR_SPECIFIC:
	case PMBUS_STATUS_FAN_12:
	case PMBUS_STATUS_FAN_34:
		return -ENXIO;
	case PMBUS_MFR_LOCATION:
	case PMBUS_MFR_DATE:
	case PMBUS_MFR_SERIAL:
	case PMBUS_IC_DEVICE_ID:
	case PMBUS_IC_DEVICE_REV:
		MPQ8646_DEBUG(client, "  unsupported mfr-info reg 0x%02x -> ENXIO\n", reg);
		return -ENXIO;
	default:
		return -ENODATA;
	}
}

/*
 * Reference: ltc2978.c::ltc2978_write_word_data which uses the
 * same virtual-register channel for its real chip-side peak reset.
 */
static int mpq8646_write_word_data(struct i2c_client *client, int page,
				   int reg, u16 word)
{
	struct mpq8646_priv *priv = mpq8646_priv_from_client(client);
	int rc;

	switch (reg) {
	case PMBUS_VIRT_RESET_VIN_HISTORY:
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		MPQ8646_DEBUG(client, "reset_history virt reg=0x%04x -> CLEAR_FAULTS\n",
			      reg);
		rc = i2c_smbus_write_byte(priv->client, PMBUS_CLEAR_FAULTS);
		if (rc < 0)
			MPQ8646_DEBUG(client, "  CLEAR_FAULTS rc=%d\n", rc);
		return rc < 0 ? rc : 0;
	default:
		return -ENODATA;	/* let pmbus_core do the direct write */
	}
}

static int mpq8646_read_word_data(struct i2c_client *client, int page,
				  int phase, int reg)
{
	int rc;

	MPQ8646_DEBUG(client, "read_word_data page=%d phase=%d reg=0x%02x\n",
		      page, phase, reg);

	switch (reg) {
	case PMBUS_READ_VIN:
	case PMBUS_READ_VOUT:
	case PMBUS_READ_IOUT:
	case PMBUS_READ_TEMPERATURE_1:
	case PMBUS_STATUS_WORD:
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_OV_WARN_LIMIT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_MFR_VIN_MAX:
	case PMBUS_MFR_VOUT_MAX:
	case PMBUS_MFR_IOUT_MAX:
	case PMBUS_MFR_MAX_TEMP_1:
		break;
	case PMBUS_VIRT_RESET_VIN_HISTORY:
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		return 0;
	default:
		return -ENODATA;
	}

	rc = i2c_smbus_read_word_data(client, reg);

	MPQ8646_DEBUG(client, "  reg=0x%02x rc=%d\n", reg, rc);
	return rc;
}

static struct pmbus_driver_info mpq8646_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 4,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 1,
	.m[PSC_CURRENT_OUT] = 16,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = 0,
	.m[PSC_TEMPERATURE] = 1,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 0,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT |
		   PMBUS_HAVE_IOUT | PMBUS_HAVE_TEMP |
		   PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_VOUT |
		   PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_TEMP,
};

#if IS_ENABLED(CONFIG_REGULATOR)
static const struct regulator_desc mpq8646_reg_desc[] = {
	PMBUS_REGULATOR("vout", 0),
};
#endif /* CONFIG_REGULATOR */

#if IS_ENABLED(CONFIG_NVMEM)
/*
 * Expose using
 *   /sys/bus/nvmem/devices/<i2c-name>/nvmem
 *
 * Layout (16 bytes):
 *   offset 0..1   PROTECTION_LAST (0xFB)    word, LE     -- NVM
 *   offset 2..3   MFR_RETRY_TIMES (0xF4)    word, LE     -- NVM
 *   offset 4..5   MFR_CONFIG_ID   (0xC0)    word, LE     -- NVM
 *   offset 6..7   MFR_VBOOT_CFG   (0xFC)    word, LE     -- NVM
 *   offset 8      MFR_SILICON_REV (0xC3)    byte         -- NVM
 *   offset 9..15  reserved (zero-fill, leaves room for additions)
 */
#define MPQ8646_NVMEM_SIZE	16

struct mpq8646_nvmem_entry {
	unsigned int	off;
	u8		reg;
	bool		is_word;
};

static const struct mpq8646_nvmem_entry mpq8646_nvmem_map[] = {
	{ 0, MPS_PROTECTION_LAST, true  },
	{ 2, MPS_MFR_RETRY_TIMES, true  },
	{ 4, MPS_MFR_CONFIG_ID,   true  },
	{ 6, MPS_MFR_VBOOT_CFG,   true  },
	{ 8, MPS_MFR_SILICON_REV, false },
};

static int mpq8646_nvmem_read(void *data, unsigned int offset, void *val,
			      size_t bytes)
{
	struct mpq8646_priv *priv = data;
	u8 *out = val;
	size_t i;

	if (offset >= MPQ8646_NVMEM_SIZE)
		return -EINVAL;
	if (offset + bytes > MPQ8646_NVMEM_SIZE)
		bytes = MPQ8646_NVMEM_SIZE - offset;

	memset(out, 0, bytes);

	guard(mutex)(&priv->mps_lock);
	for (i = 0; i < ARRAY_SIZE(mpq8646_nvmem_map); i++) {
		const struct mpq8646_nvmem_entry *e = &mpq8646_nvmem_map[i];
		unsigned int e_start = e->off;
		unsigned int e_end = e_start + (e->is_word ? 2 : 1);
		u8 raw[2];
		int rc;
		unsigned int j;

		if (e_end <= offset || e_start >= offset + bytes)
			continue;	/* outside requested slice */

		if (e->is_word)
			rc = i2c_smbus_read_word_data(priv->client, e->reg);
		else
			rc = i2c_smbus_read_byte_data(priv->client, e->reg);
		if (rc < 0)
			continue;	/* leave the zero-fill in place */

		raw[0] = rc & 0xff;
		raw[1] = (rc >> 8) & 0xff;

		for (j = 0; j < e_end - e_start; j++) {
			unsigned int abs = e_start + j;

			if (abs >= offset && abs < offset + bytes)
				out[abs - offset] = raw[j];
		}
	}
	return 0;
}
#endif /* CONFIG_NVMEM */

static const struct i2c_device_id mpq8646_id[] = {
	{ .name = "mpq8646" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mpq8646_id);

static const struct of_device_id __maybe_unused mpq8646_of_match[] = {
	{ .compatible = "mps,mpq8646" },
	{}
};
MODULE_DEVICE_TABLE(of, mpq8646_of_match);

static struct pmbus_platform_data mpq8646_no_pec_pdata = {
	.flags = PMBUS_NO_CAPABILITY,
};

#ifdef CONFIG_DEBUG_FS
/*
 * Read-only decode cases in the client's pmbus debugfs directory:
 * the MPS-specific STATUS_WORD and PROTECTION_LAST bit decode plus the
 * identity/timing registers.
 */

struct mpq_status_bit {
	u16		mask;
	const char	*name;
};

static const struct mpq_status_bit mpq8646_status_word_bits[] = {
	{ PB_STATUS_VOUT,         "VOUT" },
	{ PB_STATUS_IOUT_POUT,    "IOUT_POUT" },
	{ PB_STATUS_INPUT,        "INPUT" },
	{ PB_STATUS_WORD_MFR,     "NVM_SUMMARY" },
	{ PB_STATUS_POWER_GOOD_N, "POWER_GOOD#" },
	{ PB_STATUS_FANS,         "FANS" },
	{ PB_STATUS_OTHER,        "OTHER" },
	{ PB_STATUS_UNKNOWN,      "WATCH_DOG" },
	{ PB_STATUS_BUSY,         "BUSY" },
	{ PB_STATUS_OFF,          "OFF" },
	{ PB_STATUS_VOUT_OV,      "VOUT_OV_FAULT" },
	{ PB_STATUS_IOUT_OC,      "IOUT_OC_FAULT" },
	{ PB_STATUS_VIN_UV,       "VIN_UV_FAULT" },
	{ PB_STATUS_TEMPERATURE,  "TEMP" },
	{ PB_STATUS_CML,          "CML" },
	{ PB_STATUS_NONE_ABOVE,   "DRMOS_FAULT" },
	{ /* sentinel */ }
};

/* PROTECTION_LAST (0xFB) bit names, it survives chip POR */
static const struct mpq_status_bit mpq8646_protection_last_bits[] = {
	{ BIT(15), "INIT_FAULT" },
	{ BIT(14), "NVM_CRC_ERROR" },
	{ BIT(13), "NVM_FAULT" },
	{ BIT(12), "OC_PHASE_FAULT" },
	{ BIT(11), "OTP_SELF_FAULT" },
	{ BIT(9),  "SWITCH_PRD_FAULT" },
	{ BIT(8),  "VIN_OV_FAULT" },
	{ BIT(7),  "VOUT_OV_FAULT" },
	{ BIT(6),  "VOUT_UV_FAULT" },
	{ BIT(5),  "OC_TOT_FAULT" },
	{ BIT(4),  "VIN_UVLO_FAULT" },
	{ BIT(3),  "DRMOS_OTP" },
	{ /* sentinel */ }
};

static void mpq8646_print_bits(struct seq_file *s, u16 v,
			       const struct mpq_status_bit *tab)
{
	const struct mpq_status_bit *t;
	bool first = true;

	seq_printf(s, "0x%04x", v);
	if (!v) {
		seq_puts(s, " [clean]\n");
		return;
	}
	seq_puts(s, " [");
	for (t = tab; t->mask; t++) {
		if (v & t->mask) {
			if (!first)
				seq_putc(s, ' ');
			seq_puts(s, t->name);
			first = false;
		}
	}
	seq_puts(s, "]\n");
}

static int mpq8646_dbg_status_decoded_show(struct seq_file *s, void *unused)
{
	struct mpq8646_priv *priv = s->private;
	int rc;

	scoped_guard(pmbus_lock, priv->client)
		rc = i2c_smbus_read_word_data(priv->client, PMBUS_STATUS_WORD);
	if (rc < 0) {
		seq_printf(s, "ERROR: STATUS_WORD read failed (%d)\n", rc);
		return 0;
	}
	seq_puts(s, "STATUS_WORD: ");
	mpq8646_print_bits(s, (u16)rc, mpq8646_status_word_bits);
	seq_puts(s, "(MPS extensions: bit12=NVM_SUMMARY, bit8=WATCH_DOG, bit0=DRMOS_FAULT)\n");
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mpq8646_dbg_status_decoded);

static int mpq8646_dbg_protection_last_show(struct seq_file *s, void *unused)
{
	struct mpq8646_priv *priv = s->private;
	int rc;

	guard(pmbus_lock)(priv->client);
	scoped_guard(mutex, &priv->mps_lock)
		rc = i2c_smbus_read_word_data(priv->client, MPS_PROTECTION_LAST);

	if (rc < 0) {
		seq_printf(s, "ERROR: PROTECTION_LAST read failed (%d)\n", rc);
		return 0;
	}
	seq_puts(s, "PROTECTION_LAST: ");
	mpq8646_print_bits(s, (u16)rc, mpq8646_protection_last_bits);
	seq_puts(s, "(NVM-backed, survives chip POR)\n");
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mpq8646_dbg_protection_last);

struct mpq8646_dbg_reg {
	u8		reg;
	bool		is_word;
	const char	*name;
};

static const struct mpq8646_dbg_reg mpq8646_dbg_regs[] = {
	/* MPS vendor extensions (read-only identity / observability) */
	{ MPS_MFR_CONFIG_ID,        true,  "mfr_config_id" },
	{ MPS_MFR_CONFIG_CODE_REV,  true,  "mfr_config_code_rev" },
	{ MPS_MFR_SILICON_REV,      false, "mfr_silicon_rev" },
	{ MPS_MFR_RETRY_TIMES,      true,  "mfr_retry_times" },
	{ MPS_MFR_VBOOT_CFG,        true,  "mfr_vboot_cfg" },
	/* PMBus 1.3 standard timing / UVLO (read-only introspection) */
	{ PMBUS_VIN_ON,             true,  "vin_on" },
	{ PMBUS_VIN_OFF,            true,  "vin_off" },
	{ PMBUS_TON_DELAY,          true,  "ton_delay" },
	{ PMBUS_TON_RISE,           true,  "ton_rise" },
	{ PMBUS_TOFF_DELAY,         true,  "toff_delay" },
	{ PMBUS_TOFF_FALL,          true,  "toff_fall" },
};

struct mpq8646_dbg_reg_ctx {
	struct mpq8646_priv		*priv;
	const struct mpq8646_dbg_reg	*desc;
};

static int mpq8646_dbg_reg_show(struct seq_file *s, void *unused)
{
	struct mpq8646_dbg_reg_ctx *ctx = s->private;
	int rc;

	guard(pmbus_lock)(ctx->priv->client);
	if (ctx->desc->is_word)
		rc = i2c_smbus_read_word_data(ctx->priv->client,
					      ctx->desc->reg);
	else
		rc = i2c_smbus_read_byte_data(ctx->priv->client,
					      ctx->desc->reg);

	if (rc < 0) {
		seq_printf(s, "ERROR: reg 0x%02x (%s) read failed (%d)\n",
			   ctx->desc->reg, ctx->desc->name, rc);
		return 0;
	}
	seq_printf(s, "0x%0*x\n", ctx->desc->is_word ? 4 : 2, rc);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mpq8646_dbg_reg);

#ifdef CONFIG_SENSORS_MPQ8646_DEBUG_UNSAFE
/*
 * Write/provisioning data, disabled by default: NVM commit and
 * revert, the CLEAR_LAST_FAULT sequences and a small set of named
 * writable registers.
 */

static void mpq8646_unsafe_banner(struct mpq8646_priv *priv)
{
	dev_warn(&priv->client->dev,
		 "**********************************************************\n"
		 "**   WARNING WARNING WARNING WARNING WARNING WARNING    **\n"
		 "**                                                      **\n"
		 "** The MPQ8646 provisioning debugfs writes are enabled. **\n"
		 "** Wrong register writes can and likely will physically **\n"
		 "** damage or destroy the chip and/or the board.         **\n"
		 "**                                                      **\n"
		 "** If you see this message and you are not debugging    **\n"
		 "** the kernel, report this immediately to your system   **\n"
		 "** administrator!                                       **\n"
		 "**                                                      **\n"
		 "**   WARNING WARNING WARNING WARNING WARNING WARNING    **\n"
		 "**********************************************************\n");
}

static int mpq8646_dbg_clear_protection_last(void *data, u64 val)
{
	struct mpq8646_priv *priv = data;
	int rc;

	if (!val)
		return 0;

	guard(pmbus_lock)(priv->client);
	scoped_guard(mutex, &priv->mps_lock)
		rc = i2c_smbus_write_byte(priv->client, MPS_CLEAR_LAST_FAULT);
	if (rc < 0)
		dev_warn(&priv->client->dev,
			 "clear_protection_last: CLEAR_LAST_FAULT write failed (%d)\n",
			 rc);
	return rc;
}
DEFINE_DEBUGFS_ATTRIBUTE(mpq8646_dbg_clear_protection_last_fops,
			 NULL, mpq8646_dbg_clear_protection_last, "%llu\n");

static int mpq8646_dbg_clear_protection_last_force(void *data, u64 val)
{
	struct mpq8646_priv *priv = data;
	int rc, ret;
	int wp_orig, cfg_orig;

	if (!val)
		return 0;

	guard(pmbus_lock)(priv->client);
	guard(mutex)(&priv->mps_lock);

	wp_orig = i2c_smbus_read_byte_data(priv->client, PMBUS_WRITE_PROTECT);
	if (wp_orig < 0) {
		dev_warn(&priv->client->dev,
			 "clear_protection_last_force: WRITE_PROTECT read failed (%d), aborting\n",
			 wp_orig);
		return wp_orig;
	}
	cfg_orig = i2c_smbus_read_word_data(priv->client, MPS_MFR_CFG_EXT);
	if (cfg_orig < 0) {
		dev_warn(&priv->client->dev,
			 "clear_protection_last_force: MFR_CFG_EXT read failed (%d), aborting\n",
			 cfg_orig);
		return cfg_orig;
	}

	if (wp_orig != 0) {
		rc = i2c_smbus_write_byte_data(priv->client,
					       PMBUS_WRITE_PROTECT, 0);
		if (rc < 0) {
			dev_warn(&priv->client->dev,
				 "clear_protection_last_force: WP clear failed (%d), aborting\n",
				 rc);
			return rc;
		}
	}

	ret = i2c_smbus_write_word_data(priv->client, MPS_MFR_CFG_EXT,
					(u16)cfg_orig | MPS_MFR_CFG_EXT_CLR_LAST_EN);
	if (ret < 0) {
		dev_warn(&priv->client->dev,
			 "clear_protection_last_force: gate open failed (%d)\n",
			 ret);
	} else {
		ret = i2c_smbus_write_byte(priv->client, MPS_CLEAR_LAST_FAULT);
		if (ret < 0)
			dev_warn(&priv->client->dev,
				 "clear_protection_last_force: CLEAR_LAST_FAULT failed (%d) even with gate open\n",
				 ret);

		for (int attempt = 0; attempt < MPQ8646_NVM_RETRY_MAX; attempt++) {
			rc = i2c_smbus_write_word_data(priv->client,
						       MPS_MFR_CFG_EXT,
						       (u16)cfg_orig);
			if (rc >= 0)
				break;
			usleep_range(MPQ8646_NVM_RETRY_DELAY_US_MIN,
				     MPQ8646_NVM_RETRY_DELAY_US_MAX);
		}
		if (rc < 0) {
			dev_warn(&priv->client->dev,
				 "clear_protection_last_force: MFR_CFG_EXT restore failed after retries (%d): gate may stay open until POR\n",
				 rc);
			if (ret == 0)
				ret = rc;
		}
	}

	if (wp_orig != 0) {
		rc = i2c_smbus_write_byte_data(priv->client,
					       PMBUS_WRITE_PROTECT,
					       (u8)wp_orig);
		if (rc < 0) {
			dev_warn(&priv->client->dev,
				 "clear_protection_last_force: WP restore failed (%d)\n",
				 rc);
			if (ret == 0)
				ret = rc;
		}
	}
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(mpq8646_dbg_clear_protection_last_force_fops,
			 NULL, mpq8646_dbg_clear_protection_last_force, "%llu\n");

static int mpq8646_dbg_store_all(void *data, u64 val)
{
	struct mpq8646_priv *priv = data;
	int rc;

	if (!val)
		return 0;

	guard(pmbus_lock)(priv->client);
	scoped_guard(mutex, &priv->mps_lock)
		rc = i2c_smbus_write_byte(priv->client, PMBUS_STORE_USER_ALL);
	if (rc < 0)
		dev_warn(&priv->client->dev,
			 "store_all: STORE_USER_ALL (0x15) write failed (%d)\n",
			 rc);
	return rc;
}
DEFINE_DEBUGFS_ATTRIBUTE(mpq8646_dbg_store_all_fops,
			 NULL, mpq8646_dbg_store_all, "%llu\n");

static int mpq8646_dbg_restore_all(void *data, u64 val)
{
	struct mpq8646_priv *priv = data;
	int rc;

	if (!val)
		return 0;

	guard(pmbus_lock)(priv->client);
	scoped_guard(mutex, &priv->mps_lock)
		rc = i2c_smbus_write_byte(priv->client, PMBUS_RESTORE_USER_ALL);
	if (rc < 0)
		dev_warn(&priv->client->dev,
			 "restore_all: RESTORE_USER_ALL (0x16) write failed (%d)\n",
			 rc);
	return rc;
}
DEFINE_DEBUGFS_ATTRIBUTE(mpq8646_dbg_restore_all_fops,
			 NULL, mpq8646_dbg_restore_all, "%llu\n");

static const struct mpq8646_dbg_reg mpq8646_dbg_regs_unsafe[] = {
	/* PMBus 1.3 control / margin */
	{ PMBUS_ON_OFF_CONFIG,      false, "on_off_config" },
	{ PMBUS_VOUT_MARGIN_HIGH,   true,  "vout_margin_high" },
	{ PMBUS_VOUT_MARGIN_LOW,    true,  "vout_margin_low" },
	/* MPS PMBus-level write-protect */
	{ MPS_MFR_PMBUS_LOCK,       true,  "mfr_pmbus_lock" },
	/* MPS user-writable product revision */
	{ MPS_MFR_PRODUCT_REV_USER, true,  "mfr_product_rev_user" },
};

static int mpq8646_dbg_reg_get(void *data, u64 *val)
{
	struct mpq8646_dbg_reg_ctx *ctx = data;
	int rc;

	guard(pmbus_lock)(ctx->priv->client);
	if (ctx->desc->is_word)
		rc = i2c_smbus_read_word_data(ctx->priv->client,
					      ctx->desc->reg);
	else
		rc = i2c_smbus_read_byte_data(ctx->priv->client,
					      ctx->desc->reg);
	if (rc < 0)
		return rc;
	*val = rc;
	return 0;
}

static int mpq8646_dbg_reg_set(void *data, u64 val)
{
	struct mpq8646_dbg_reg_ctx *ctx = data;
	int rc;

	guard(pmbus_lock)(ctx->priv->client);
	if (ctx->desc->is_word)
		rc = i2c_smbus_write_word_data(ctx->priv->client,
					       ctx->desc->reg, (u16)val);
	else
		rc = i2c_smbus_write_byte_data(ctx->priv->client,
					       ctx->desc->reg, (u8)val);
	return rc < 0 ? rc : 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(mpq8646_dbg_reg_rw_fops,
			 mpq8646_dbg_reg_get, mpq8646_dbg_reg_set, "0x%llx\n");

static void mpq8646_debugfs_register_unsafe(struct mpq8646_priv *priv,
					    struct dentry *root)
{
	struct mpq8646_dbg_reg_ctx *ctx;
	size_t i;

	mpq8646_unsafe_banner(priv);

	debugfs_create_file_unsafe("clear_protection_last", 0200, root, priv,
				   &mpq8646_dbg_clear_protection_last_fops);
	debugfs_create_file_unsafe("clear_protection_last_force", 0200, root,
				   priv,
				   &mpq8646_dbg_clear_protection_last_force_fops);
	debugfs_create_file_unsafe("store_all", 0200, root, priv,
				   &mpq8646_dbg_store_all_fops);
	debugfs_create_file_unsafe("restore_all", 0200, root, priv,
				   &mpq8646_dbg_restore_all_fops);

	ctx = devm_kcalloc(&priv->client->dev,
			   ARRAY_SIZE(mpq8646_dbg_regs_unsafe),
			   sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return;
	for (i = 0; i < ARRAY_SIZE(mpq8646_dbg_regs_unsafe); i++) {
		ctx[i].priv = priv;
		ctx[i].desc = &mpq8646_dbg_regs_unsafe[i];
		debugfs_create_file_unsafe(mpq8646_dbg_regs_unsafe[i].name,
					   0600, root, &ctx[i],
					   &mpq8646_dbg_reg_rw_fops);
	}
}
#else
static inline void mpq8646_debugfs_register_unsafe(struct mpq8646_priv *priv,
						   struct dentry *root) {}
#endif /* CONFIG_SENSORS_MPQ8646_DEBUG_UNSAFE */

static int mpq8646_dbg_poll_interval_get(void *data, u64 *val)
{
	struct mpq8646_priv *priv = data;

	*val = priv->alarm_poll_interval_ms;
	return 0;
}

static int mpq8646_dbg_poll_interval_set(void *data, u64 val)
{
	struct mpq8646_priv *priv = data;
	bool was_off = !priv->alarm_poll_interval_ms;

	priv->alarm_poll_interval_ms = (u32)val;

	if (val && was_off && !priv->client->irq)
		schedule_delayed_work(&priv->alarm_poll_work,
				      msecs_to_jiffies((u32)val));
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(mpq8646_dbg_poll_interval_fops,
			 mpq8646_dbg_poll_interval_get,
			 mpq8646_dbg_poll_interval_set, "%llu\n");

static void mpq8646_debugfs_register(struct mpq8646_priv *priv)
{
	struct dentry *root;
	size_t i;

	root = pmbus_get_debugfs_dir(priv->client);
	if (!root)
		return;

	/* MPS extensions: status decode + NVM-backed PROTECTION_LAST */
	debugfs_create_file("status_decoded", 0400, root, priv,
			    &mpq8646_dbg_status_decoded_fops);
	debugfs_create_file("protection_last", 0400, root, priv,
			    &mpq8646_dbg_protection_last_fops);
	priv->dbg_poll =
		debugfs_create_file_unsafe("alarm_poll_interval_ms", 0600,
					   root, priv,
					   &mpq8646_dbg_poll_interval_fops);

	priv->dbg_reg_ctx = devm_kcalloc(&priv->client->dev,
					 ARRAY_SIZE(mpq8646_dbg_regs),
					 sizeof(*priv->dbg_reg_ctx),
					 GFP_KERNEL);
	if (!priv->dbg_reg_ctx)
		return;
	for (i = 0; i < ARRAY_SIZE(mpq8646_dbg_regs); i++) {
		priv->dbg_reg_ctx[i].priv = priv;
		priv->dbg_reg_ctx[i].desc = &mpq8646_dbg_regs[i];
		debugfs_create_file(mpq8646_dbg_regs[i].name, 0400,
				    root, &priv->dbg_reg_ctx[i],
				    &mpq8646_dbg_reg_fops);
	}

	mpq8646_debugfs_register_unsafe(priv, root);
}

static void mpq8646_debugfs_unregister(struct mpq8646_priv *priv)
{
	debugfs_remove(priv->dbg_poll);
}
#else
static inline void mpq8646_debugfs_register(struct mpq8646_priv *priv) {}
static inline void mpq8646_debugfs_unregister(struct mpq8646_priv *priv) {}
#endif /* CONFIG_DEBUG_FS */

static void mpq8646_alarm_poll_work(struct work_struct *work)
{
	struct mpq8646_priv *priv = container_of(to_delayed_work(work),
						 struct mpq8646_priv,
						 alarm_poll_work);

	if (priv->client->irq)
		return;	/* SMBALERT# wired; polling not needed */

	if (!priv->alarm_poll_interval_ms)
		return;	/* polling disabled; don't re-arm */

	pmbus_check_and_notify_faults(priv->client);

	schedule_delayed_work(&priv->alarm_poll_work,
			      msecs_to_jiffies(priv->alarm_poll_interval_ms));
}

static int mpq8646_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pmbus_driver_info *info;
	struct mpq8646_priv *priv;
	u32 voltage_scale;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->client = client;
	mutex_init(&priv->mps_lock);
	memcpy(&priv->info, &mpq8646_info, sizeof(priv->info));
	info = &priv->info;

	info->identify = mpq8646_identify;
	info->read_byte_data = mpq8646_read_byte_data;
	info->read_word_data = mpq8646_read_word_data;
	info->write_word_data = mpq8646_write_word_data;
	dev->platform_data = &mpq8646_no_pec_pdata;

#if IS_ENABLED(CONFIG_REGULATOR)
	info->reg_desc = mpq8646_reg_desc;
	info->num_regulators = ARRAY_SIZE(mpq8646_reg_desc);
#endif

	INIT_DELAYED_WORK(&priv->alarm_poll_work, mpq8646_alarm_poll_work);
	priv->alarm_poll_interval_ms = MPQ8646_ALARM_POLL_MS_DEFAULT;

	if (!device_property_read_u32(dev, "mps,vout-fb-divider-ratio-permille",
				      &voltage_scale)) {
		if (voltage_scale > MPQ8646_VOUT_SCALE_LOOP_MAX)
			return -EINVAL;

		ret = i2c_smbus_write_word_data(client, PMBUS_VOUT_SCALE_LOOP,
						voltage_scale);
		if (ret)
			return ret;
	}

	ret = pmbus_do_probe(client, info);
	if (ret)
		return ret;

	mpq8646_debugfs_register(priv);

	if (!client->irq)
		schedule_delayed_work(&priv->alarm_poll_work,
				      msecs_to_jiffies(priv->alarm_poll_interval_ms));

#if IS_ENABLED(CONFIG_NVMEM)
	{
		struct nvmem_config cfg = {
			.dev		= &client->dev,
			.name		= dev_name(&client->dev),
			.owner		= THIS_MODULE,
			.read_only	= true,
			.root_only	= true,
			.word_size	= 1,
			.stride		= 1,
			.size		= MPQ8646_NVMEM_SIZE,
			.reg_read	= mpq8646_nvmem_read,
			.priv		= priv,
		};
		struct nvmem_device *nv = devm_nvmem_register(&client->dev, &cfg);

		if (IS_ERR(nv))
			dev_warn(&client->dev,
				 "nvmem snapshot register failed (%pe)\n",
				 nv);
	}
#endif
	return 0;
};

static void mpq8646_remove(struct i2c_client *client)
{
	struct mpq8646_priv *priv = mpq8646_priv_from_client(client);

	mpq8646_debugfs_unregister(priv);
	cancel_delayed_work_sync(&priv->alarm_poll_work);
}

static struct i2c_driver mpq8646_driver = {
	.driver = {
		   .name = "mpq8646",
		   .of_match_table = of_match_ptr(mpq8646_of_match),
	},
	.probe = mpq8646_probe,
	.remove = mpq8646_remove,
	.id_table = mpq8646_id,
};

module_i2c_driver(mpq8646_driver);

MODULE_AUTHOR("Vincent Jardin <vjardin@free.fr>");
MODULE_DESCRIPTION("PMBus driver for MPS MPQ8646 (extended observability)");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
