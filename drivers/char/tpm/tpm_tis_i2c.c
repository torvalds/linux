// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014-2021 Nuvoton Technology corporation
 * Copyright (C) 2019-2022 Infineon Technologies AG
 *
 * This device driver implements the TPM interface as defined in the TCG PC
 * Client Platform TPM Profile (PTP) Specification for TPM 2.0 v1.04
 * Revision 14.
 *
 * It is based on the tpm_tis_spi device driver.
 */

#include <linux/i2c.h>
#include <linux/crc-ccitt.h>
#include "tpm_tis_core.h"

/* TPM registers */
#define TPM_I2C_LOC_SEL 0x00
#define TPM_I2C_ACCESS 0x04
#define TPM_I2C_INTERFACE_CAPABILITY 0x30
#define TPM_I2C_DEVICE_ADDRESS 0x38
#define TPM_I2C_DATA_CSUM_ENABLE 0x40
#define TPM_DATA_CSUM 0x44
#define TPM_I2C_DID_VID 0x48
#define TPM_I2C_RID 0x4C

/* TIS-compatible register address to avoid clash with TPM_ACCESS (0x00) */
#define TPM_LOC_SEL 0x0FFF

/* Mask to extract the I2C register from TIS register addresses */
#define TPM_TIS_REGISTER_MASK 0x0FFF

/* Default Guard Time of 250µs until interface capability register is read */
#define GUARD_TIME_DEFAULT_MIN 250
#define GUARD_TIME_DEFAULT_MAX 300

/* Guard Time of 250µs after I2C slave NACK */
#define GUARD_TIME_ERR_MIN 250
#define GUARD_TIME_ERR_MAX 300

/* Guard Time bit masks; SR is repeated start, RW is read then write, etc. */
#define TPM_GUARD_TIME_SR_MASK 0x40000000
#define TPM_GUARD_TIME_RR_MASK 0x00100000
#define TPM_GUARD_TIME_RW_MASK 0x00080000
#define TPM_GUARD_TIME_WR_MASK 0x00040000
#define TPM_GUARD_TIME_WW_MASK 0x00020000
#define TPM_GUARD_TIME_MIN_MASK 0x0001FE00
#define TPM_GUARD_TIME_MIN_SHIFT 9

/* Masks with bits that must be read zero */
#define TPM_ACCESS_READ_ZERO 0x48
#define TPM_INT_ENABLE_ZERO 0x7FFFFF60
#define TPM_STS_READ_ZERO 0x23
#define TPM_INTF_CAPABILITY_ZERO 0x0FFFF000
#define TPM_I2C_INTERFACE_CAPABILITY_ZERO 0x80000000

struct tpm_tis_i2c_phy {
	struct tpm_tis_data priv;
	struct i2c_client *i2c_client;
	bool guard_time_read;
	bool guard_time_write;
	u16 guard_time_min;
	u16 guard_time_max;
	u8 *io_buf;
};

static inline struct tpm_tis_i2c_phy *
to_tpm_tis_i2c_phy(struct tpm_tis_data *data)
{
	return container_of(data, struct tpm_tis_i2c_phy, priv);
}

/*
 * tpm_tis_core uses the register addresses as defined in Table 19 "Allocation
 * of Register Space for FIFO TPM Access" of the TCG PC Client PTP
 * Specification. In order for this code to work together with tpm_tis_core,
 * those addresses need to mapped to the registers defined for I2C TPMs in
 * Table 51 "I2C-TPM Register Overview".
 *
 * For most addresses this can be done by simply stripping off the locality
 * information from the address. A few addresses need to be mapped explicitly,
 * since the corresponding I2C registers have been moved around. TPM_LOC_SEL is
 * only defined for I2C TPMs and is also mapped explicitly here to distinguish
 * it from TPM_ACCESS(0).
 *
 * Locality information is ignored, since this driver assumes exclusive access
 * to the TPM and always uses locality 0.
 */
static u8 tpm_tis_i2c_address_to_register(u32 addr)
{
	addr &= TPM_TIS_REGISTER_MASK;

	switch (addr) {
	case TPM_ACCESS(0):
		return TPM_I2C_ACCESS;
	case TPM_LOC_SEL:
		return TPM_I2C_LOC_SEL;
	case TPM_DID_VID(0):
		return TPM_I2C_DID_VID;
	case TPM_RID(0):
		return TPM_I2C_RID;
	default:
		return addr;
	}
}

static int tpm_tis_i2c_retry_transfer_until_ack(struct tpm_tis_data *data,
						struct i2c_msg *msg)
{
	struct tpm_tis_i2c_phy *phy = to_tpm_tis_i2c_phy(data);
	bool guard_time;
	int i = 0;
	int ret;

	if (msg->flags & I2C_M_RD)
		guard_time = phy->guard_time_read;
	else
		guard_time = phy->guard_time_write;

	do {
		ret = i2c_transfer(phy->i2c_client->adapter, msg, 1);
		if (ret < 0)
			usleep_range(GUARD_TIME_ERR_MIN, GUARD_TIME_ERR_MAX);
		else if (guard_time)
			usleep_range(phy->guard_time_min, phy->guard_time_max);
		/* retry on TPM NACK */
	} while (ret < 0 && i++ < TPM_RETRY);

	return ret;
}

/* Check that bits which must be read zero are not set */
static int tpm_tis_i2c_sanity_check_read(u8 reg, u16 len, u8 *buf)
{
	u32 zero_mask;
	u32 value;

	switch (len) {
	case sizeof(u8):
		value = buf[0];
		break;
	case sizeof(u16):
		value = le16_to_cpup((__le16 *)buf);
		break;
	case sizeof(u32):
		value = le32_to_cpup((__le32 *)buf);
		break;
	default:
		/* unknown length, skip check */
		return 0;
	}

	switch (reg) {
	case TPM_I2C_ACCESS:
		zero_mask = TPM_ACCESS_READ_ZERO;
		break;
	case TPM_INT_ENABLE(0) & TPM_TIS_REGISTER_MASK:
		zero_mask = TPM_INT_ENABLE_ZERO;
		break;
	case TPM_STS(0) & TPM_TIS_REGISTER_MASK:
		zero_mask = TPM_STS_READ_ZERO;
		break;
	case TPM_INTF_CAPS(0) & TPM_TIS_REGISTER_MASK:
		zero_mask = TPM_INTF_CAPABILITY_ZERO;
		break;
	case TPM_I2C_INTERFACE_CAPABILITY:
		zero_mask = TPM_I2C_INTERFACE_CAPABILITY_ZERO;
		break;
	default:
		/* unknown register, skip check */
		return 0;
	}

	if (unlikely((value & zero_mask) != 0x00)) {
		pr_debug("TPM I2C read of register 0x%02x failed sanity check: 0x%x\n", reg, value);
		return -EIO;
	}

	return 0;
}

static int tpm_tis_i2c_read_bytes(struct tpm_tis_data *data, u32 addr, u16 len,
				  u8 *result, enum tpm_tis_io_mode io_mode)
{
	struct tpm_tis_i2c_phy *phy = to_tpm_tis_i2c_phy(data);
	struct i2c_msg msg = { .addr = phy->i2c_client->addr };
	u8 reg = tpm_tis_i2c_address_to_register(addr);
	int i;
	int ret;

	for (i = 0; i < TPM_RETRY; i++) {
		/* write register */
		msg.len = sizeof(reg);
		msg.buf = &reg;
		msg.flags = 0;
		ret = tpm_tis_i2c_retry_transfer_until_ack(data, &msg);
		if (ret < 0)
			return ret;

		/* read data */
		msg.buf = result;
		msg.len = len;
		msg.flags = I2C_M_RD;
		ret = tpm_tis_i2c_retry_transfer_until_ack(data, &msg);
		if (ret < 0)
			return ret;

		ret = tpm_tis_i2c_sanity_check_read(reg, len, result);
		if (ret == 0)
			return 0;

		usleep_range(GUARD_TIME_ERR_MIN, GUARD_TIME_ERR_MAX);
	}

	return ret;
}

static int tpm_tis_i2c_write_bytes(struct tpm_tis_data *data, u32 addr, u16 len,
				   const u8 *value,
				   enum tpm_tis_io_mode io_mode)
{
	struct tpm_tis_i2c_phy *phy = to_tpm_tis_i2c_phy(data);
	struct i2c_msg msg = { .addr = phy->i2c_client->addr };
	u8 reg = tpm_tis_i2c_address_to_register(addr);
	int ret;

	if (len > TPM_BUFSIZE - 1)
		return -EIO;

	/* write register and data in one go */
	phy->io_buf[0] = reg;
	memcpy(phy->io_buf + sizeof(reg), value, len);

	msg.len = sizeof(reg) + len;
	msg.buf = phy->io_buf;
	ret = tpm_tis_i2c_retry_transfer_until_ack(data, &msg);
	if (ret < 0)
		return ret;

	return 0;
}

static int tpm_tis_i2c_verify_crc(struct tpm_tis_data *data, size_t len,
				  const u8 *value)
{
	u16 crc_tpm, crc_host;
	int rc;

	rc = tpm_tis_read16(data, TPM_DATA_CSUM, &crc_tpm);
	if (rc < 0)
		return rc;

	/* reflect crc result, regardless of host endianness */
	crc_host = swab16(crc_ccitt(0, value, len));
	if (crc_tpm != crc_host)
		return -EIO;

	return 0;
}

/*
 * Guard Time:
 * After each I2C operation, the TPM might require the master to wait.
 * The time period is vendor-specific and must be read from the
 * TPM_I2C_INTERFACE_CAPABILITY register.
 *
 * Before the Guard Time is read (or after the TPM failed to send an I2C NACK),
 * a Guard Time of 250µs applies.
 *
 * Various flags in the same register indicate if a guard time is needed:
 *  - SR: <I2C read with repeated start> <guard time> <I2C read>
 *  - RR: <I2C read> <guard time> <I2C read>
 *  - RW: <I2C read> <guard time> <I2C write>
 *  - WR: <I2C write> <guard time> <I2C read>
 *  - WW: <I2C write> <guard time> <I2C write>
 *
 * See TCG PC Client PTP Specification v1.04, 8.1.10 GUARD_TIME
 */
static int tpm_tis_i2c_init_guard_time(struct tpm_tis_i2c_phy *phy)
{
	u32 i2c_caps;
	int ret;

	phy->guard_time_read = true;
	phy->guard_time_write = true;
	phy->guard_time_min = GUARD_TIME_DEFAULT_MIN;
	phy->guard_time_max = GUARD_TIME_DEFAULT_MAX;

	ret = tpm_tis_i2c_read_bytes(&phy->priv, TPM_I2C_INTERFACE_CAPABILITY,
				     sizeof(i2c_caps), (u8 *)&i2c_caps,
				     TPM_TIS_PHYS_32);
	if (ret)
		return ret;

	phy->guard_time_read = (i2c_caps & TPM_GUARD_TIME_RR_MASK) ||
			       (i2c_caps & TPM_GUARD_TIME_RW_MASK);
	phy->guard_time_write = (i2c_caps & TPM_GUARD_TIME_WR_MASK) ||
				(i2c_caps & TPM_GUARD_TIME_WW_MASK);
	phy->guard_time_min = (i2c_caps & TPM_GUARD_TIME_MIN_MASK) >>
			      TPM_GUARD_TIME_MIN_SHIFT;
	/* guard_time_max = guard_time_min * 1.2 */
	phy->guard_time_max = phy->guard_time_min + phy->guard_time_min / 5;

	return 0;
}

static SIMPLE_DEV_PM_OPS(tpm_tis_pm, tpm_pm_suspend, tpm_tis_resume);

static const struct tpm_tis_phy_ops tpm_i2c_phy_ops = {
	.read_bytes = tpm_tis_i2c_read_bytes,
	.write_bytes = tpm_tis_i2c_write_bytes,
	.verify_crc = tpm_tis_i2c_verify_crc,
};

static int tpm_tis_i2c_probe(struct i2c_client *dev)
{
	struct tpm_tis_i2c_phy *phy;
	const u8 crc_enable = 1;
	const u8 locality = 0;
	int ret;

	phy = devm_kzalloc(&dev->dev, sizeof(struct tpm_tis_i2c_phy),
			   GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->io_buf = devm_kzalloc(&dev->dev, TPM_BUFSIZE, GFP_KERNEL);
	if (!phy->io_buf)
		return -ENOMEM;

	set_bit(TPM_TIS_DEFAULT_CANCELLATION, &phy->priv.flags);
	phy->i2c_client = dev;

	/* must precede all communication with the tpm */
	ret = tpm_tis_i2c_init_guard_time(phy);
	if (ret)
		return ret;

	ret = tpm_tis_i2c_write_bytes(&phy->priv, TPM_LOC_SEL, sizeof(locality),
				      &locality, TPM_TIS_PHYS_8);
	if (ret)
		return ret;

	ret = tpm_tis_i2c_write_bytes(&phy->priv, TPM_I2C_DATA_CSUM_ENABLE,
				      sizeof(crc_enable), &crc_enable,
				      TPM_TIS_PHYS_8);
	if (ret)
		return ret;

	return tpm_tis_core_init(&dev->dev, &phy->priv, -1, &tpm_i2c_phy_ops,
				 NULL);
}

static void tpm_tis_i2c_remove(struct i2c_client *client)
{
	struct tpm_chip *chip = i2c_get_clientdata(client);

	tpm_chip_unregister(chip);
	tpm_tis_remove(chip);
}

static const struct i2c_device_id tpm_tis_i2c_id[] = {
	{ "tpm_tis_i2c", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, tpm_tis_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id of_tis_i2c_match[] = {
	{ .compatible = "infineon,slb9673", },
	{}
};
MODULE_DEVICE_TABLE(of, of_tis_i2c_match);
#endif

static struct i2c_driver tpm_tis_i2c_driver = {
	.driver = {
		.name = "tpm_tis_i2c",
		.pm = &tpm_tis_pm,
		.of_match_table = of_match_ptr(of_tis_i2c_match),
	},
	.probe_new = tpm_tis_i2c_probe,
	.remove = tpm_tis_i2c_remove,
	.id_table = tpm_tis_i2c_id,
};
module_i2c_driver(tpm_tis_i2c_driver);

MODULE_DESCRIPTION("TPM Driver for native I2C access");
MODULE_LICENSE("GPL");
