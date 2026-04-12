// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Based upon the MaxLinear SDK driver
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 * Copyright (C) 2025 John Crispin <john@phrozen.org>
 * Copyright (C) 2024 MaxLinear Inc.
 */

#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/crc16.h>
#include <linux/iopoll.h>
#include <linux/limits.h>
#include <net/dsa.h>
#include "mxl862xx.h"
#include "mxl862xx-host.h"

#define CTRL_BUSY_MASK			BIT(15)
#define CTRL_CRC_FLAG			BIT(14)

#define LEN_RET_LEN_MASK		GENMASK(9, 0)

#define MXL862XX_MMD_REG_CTRL		0
#define MXL862XX_MMD_REG_LEN_RET	1
#define MXL862XX_MMD_REG_DATA_FIRST	2
#define MXL862XX_MMD_REG_DATA_LAST	95
#define MXL862XX_MMD_REG_DATA_MAX_SIZE \
		(MXL862XX_MMD_REG_DATA_LAST - MXL862XX_MMD_REG_DATA_FIRST + 1)

#define MMD_API_SET_DATA_0		2
#define MMD_API_GET_DATA_0		5
#define MMD_API_RST_DATA		8

#define MXL862XX_SWITCH_RESET		0x9907

static void mxl862xx_crc_err_work_fn(struct work_struct *work)
{
	struct mxl862xx_priv *priv = container_of(work, struct mxl862xx_priv,
						  crc_err_work);
	struct dsa_port *dp;

	dev_warn(&priv->mdiodev->dev,
		 "MDIO CRC error detected, shutting down all ports\n");

	rtnl_lock();
	dsa_switch_for_each_cpu_port(dp, priv->ds)
		dev_close(dp->conduit);
	rtnl_unlock();

	clear_bit(MXL862XX_FLAG_CRC_ERR, &priv->flags);
}

/* Firmware CRC error codes (outside normal Zephyr errno range). */
#define MXL862XX_FW_CRC6_ERR		(-1024)
#define MXL862XX_FW_CRC16_ERR		(-1023)

/* 3GPP CRC-6 lookup table (polynomial 0x6F).
 * Matches the firmware's default CRC-6 implementation.
 */
static const u8 mxl862xx_crc6_table[256] = {
	0x00, 0x2f, 0x31, 0x1e, 0x0d, 0x22, 0x3c, 0x13,
	0x1a, 0x35, 0x2b, 0x04, 0x17, 0x38, 0x26, 0x09,
	0x34, 0x1b, 0x05, 0x2a, 0x39, 0x16, 0x08, 0x27,
	0x2e, 0x01, 0x1f, 0x30, 0x23, 0x0c, 0x12, 0x3d,
	0x07, 0x28, 0x36, 0x19, 0x0a, 0x25, 0x3b, 0x14,
	0x1d, 0x32, 0x2c, 0x03, 0x10, 0x3f, 0x21, 0x0e,
	0x33, 0x1c, 0x02, 0x2d, 0x3e, 0x11, 0x0f, 0x20,
	0x29, 0x06, 0x18, 0x37, 0x24, 0x0b, 0x15, 0x3a,
	0x0e, 0x21, 0x3f, 0x10, 0x03, 0x2c, 0x32, 0x1d,
	0x14, 0x3b, 0x25, 0x0a, 0x19, 0x36, 0x28, 0x07,
	0x3a, 0x15, 0x0b, 0x24, 0x37, 0x18, 0x06, 0x29,
	0x20, 0x0f, 0x11, 0x3e, 0x2d, 0x02, 0x1c, 0x33,
	0x09, 0x26, 0x38, 0x17, 0x04, 0x2b, 0x35, 0x1a,
	0x13, 0x3c, 0x22, 0x0d, 0x1e, 0x31, 0x2f, 0x00,
	0x3d, 0x12, 0x0c, 0x23, 0x30, 0x1f, 0x01, 0x2e,
	0x27, 0x08, 0x16, 0x39, 0x2a, 0x05, 0x1b, 0x34,
	0x1c, 0x33, 0x2d, 0x02, 0x11, 0x3e, 0x20, 0x0f,
	0x06, 0x29, 0x37, 0x18, 0x0b, 0x24, 0x3a, 0x15,
	0x28, 0x07, 0x19, 0x36, 0x25, 0x0a, 0x14, 0x3b,
	0x32, 0x1d, 0x03, 0x2c, 0x3f, 0x10, 0x0e, 0x21,
	0x1b, 0x34, 0x2a, 0x05, 0x16, 0x39, 0x27, 0x08,
	0x01, 0x2e, 0x30, 0x1f, 0x0c, 0x23, 0x3d, 0x12,
	0x2f, 0x00, 0x1e, 0x31, 0x22, 0x0d, 0x13, 0x3c,
	0x35, 0x1a, 0x04, 0x2b, 0x38, 0x17, 0x09, 0x26,
	0x12, 0x3d, 0x23, 0x0c, 0x1f, 0x30, 0x2e, 0x01,
	0x08, 0x27, 0x39, 0x16, 0x05, 0x2a, 0x34, 0x1b,
	0x26, 0x09, 0x17, 0x38, 0x2b, 0x04, 0x1a, 0x35,
	0x3c, 0x13, 0x0d, 0x22, 0x31, 0x1e, 0x00, 0x2f,
	0x15, 0x3a, 0x24, 0x0b, 0x18, 0x37, 0x29, 0x06,
	0x0f, 0x20, 0x3e, 0x11, 0x02, 0x2d, 0x33, 0x1c,
	0x21, 0x0e, 0x10, 0x3f, 0x2c, 0x03, 0x1d, 0x32,
	0x3b, 0x14, 0x0a, 0x25, 0x36, 0x19, 0x07, 0x28,
};

/* Compute 3GPP CRC-6 over the ctrl register (16 bits) and the lower
 * 10 bits of the len_ret register. The 26-bit input is packed as
 * { len_ret[9:0], ctrl[15:0] } and processed LSB-first through the
 * lookup table.
 */
static u8 mxl862xx_crc6(u16 ctrl, u16 len_ret)
{
	u32 data = ((u32)(len_ret & LEN_RET_LEN_MASK) << 16) | ctrl;
	u8 crc = 0;
	int i;

	for (i = 0; i < sizeof(data); i++, data >>= 8)
		crc = mxl862xx_crc6_table[(crc << 2) ^ (data & 0xff)] & 0x3f;

	return crc;
}

/* Encode CRC-6 into the ctrl and len_ret registers before writing them
 * to MDIO. The caller must set ctrl = API_ID | CTRL_BUSY_MASK |
 * CTRL_CRC_FLAG, and len_ret = parameter length (bits 0-9 only).
 *
 * After encoding:
 *   ctrl[12:0]     = API ID (unchanged)
 *   ctrl[14:13]    = CRC-6 bits 5-4
 *   ctrl[15]       = busy flag (unchanged)
 *   len_ret[9:0]   = parameter length (unchanged)
 *   len_ret[13:10] = CRC-6 bits 3-0
 *   len_ret[14]    = original ctrl[14] (CRC check flag, forwarded to FW)
 *   len_ret[15]    = original ctrl[13] (magic bit, always 1)
 */
static void mxl862xx_crc6_encode(u16 *pctrl, u16 *plen_ret)
{
	u16 crc, ctrl, len_ret;

	/* Set magic bit before CRC computation */
	*pctrl |= BIT(13);

	crc = mxl862xx_crc6(*pctrl, *plen_ret);

	/* Place CRC MSB (bits 5-4) into ctrl bits 13-14 */
	ctrl = (*pctrl & ~GENMASK(14, 13));
	ctrl |= (crc & 0x30) << 9;

	/* Place CRC LSB (bits 3-0) into len_ret bits 10-13 */
	len_ret = *plen_ret | ((crc & 0x0f) << 10);

	/* Forward ctrl[14] (CRC check flag) to len_ret[14],
	 * and ctrl[13] (magic, always 1) to len_ret[15].
	 */
	len_ret |= (*pctrl & BIT(14)) | ((*pctrl & BIT(13)) << 2);

	*pctrl = ctrl;
	*plen_ret = len_ret;
}

/* Verify CRC-6 on a firmware response and extract the return value.
 *
 * The firmware encodes the return value as a signed 11-bit integer:
 *   - Sign bit (bit 10) in ctrl[14]
 *   - Magnitude (bits 9-0) in len_ret[9:0]
 * These are recoverable after CRC-6 verification by restoring the
 * original ctrl from the auxiliary copies in len_ret[15:14].
 *
 * Return: 0 on CRC match (with *result set), or -EIO on mismatch.
 */
static int mxl862xx_crc6_verify(u16 ctrl, u16 len_ret, int *result)
{
	u16 crc_recv, crc_calc;

	/* Extract the received CRC-6 */
	crc_recv = ((ctrl >> 9) & 0x30) | ((len_ret >> 10) & 0x0f);

	/* Reconstruct the original ctrl for re-computation:
	 *   ctrl[14] = len_ret[14] (sign bit / CRC check flag)
	 *   ctrl[13] = len_ret[15] >> 2 (magic bit)
	 */
	ctrl &= ~GENMASK(14, 13);
	ctrl |= len_ret & BIT(14);
	ctrl |= (len_ret & BIT(15)) >> 2;

	crc_calc = mxl862xx_crc6(ctrl, len_ret);
	if (crc_recv != crc_calc)
		return -EIO;

	/* Extract signed 11-bit return value:
	 *   bit 10 (sign) from ctrl[14], bits 9-0 from len_ret[9:0]
	 */
	*result = sign_extend32((len_ret & LEN_RET_LEN_MASK) |
				((ctrl & CTRL_CRC_FLAG) >> 4), 10);

	return 0;
}

static int mxl862xx_reg_read(struct mxl862xx_priv *priv, u32 addr)
{
	return __mdiodev_c45_read(priv->mdiodev, MDIO_MMD_VEND1, addr);
}

static int mxl862xx_reg_write(struct mxl862xx_priv *priv, u32 addr, u16 data)
{
	return __mdiodev_c45_write(priv->mdiodev, MDIO_MMD_VEND1, addr, data);
}

static int mxl862xx_ctrl_read(struct mxl862xx_priv *priv)
{
	return mxl862xx_reg_read(priv, MXL862XX_MMD_REG_CTRL);
}

static int mxl862xx_busy_wait(struct mxl862xx_priv *priv)
{
	int val;

	return readx_poll_timeout(mxl862xx_ctrl_read, priv, val,
				  !(val & CTRL_BUSY_MASK), 15, 500000);
}

/* Issue a firmware command with CRC-6 protection on the ctrl and len_ret
 * registers, wait for completion, and verify the response CRC-6.
 *
 * Return: firmware result value (>= 0) on success, or negative errno.
 */
static int mxl862xx_issue_cmd(struct mxl862xx_priv *priv, u16 cmd, u16 len)
{
	u16 ctrl_enc, len_enc;
	int ret, fw_result;

	ctrl_enc = cmd | CTRL_BUSY_MASK | CTRL_CRC_FLAG;
	len_enc = len;
	mxl862xx_crc6_encode(&ctrl_enc, &len_enc);

	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_LEN_RET, len_enc);
	if (ret < 0)
		return ret;

	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_CTRL, ctrl_enc);
	if (ret < 0)
		return ret;

	ret = mxl862xx_busy_wait(priv);
	if (ret < 0)
		return ret;

	ret = mxl862xx_reg_read(priv, MXL862XX_MMD_REG_CTRL);
	if (ret < 0)
		return ret;
	ctrl_enc = ret;

	ret = mxl862xx_reg_read(priv, MXL862XX_MMD_REG_LEN_RET);
	if (ret < 0)
		return ret;
	len_enc = ret;

	ret = mxl862xx_crc6_verify(ctrl_enc, len_enc, &fw_result);
	if (ret) {
		if (!test_and_set_bit(MXL862XX_FLAG_CRC_ERR, &priv->flags))
			schedule_work(&priv->crc_err_work);
		return -EIO;
	}

	return fw_result;
}

static int mxl862xx_set_data(struct mxl862xx_priv *priv, u16 words)
{
	u16 cmd;

	cmd = words / MXL862XX_MMD_REG_DATA_MAX_SIZE - 1;
	if (!(cmd < 2))
		return -EINVAL;

	cmd += MMD_API_SET_DATA_0;

	return mxl862xx_issue_cmd(priv, cmd,
				  MXL862XX_MMD_REG_DATA_MAX_SIZE * sizeof(u16));
}

static int mxl862xx_get_data(struct mxl862xx_priv *priv, u16 words)
{
	u16 cmd;

	cmd = words / MXL862XX_MMD_REG_DATA_MAX_SIZE;
	if (!(cmd > 0 && cmd < 3))
		return -EINVAL;

	cmd += MMD_API_GET_DATA_0;

	return mxl862xx_issue_cmd(priv, cmd,
				  MXL862XX_MMD_REG_DATA_MAX_SIZE * sizeof(u16));
}

static int mxl862xx_rst_data(struct mxl862xx_priv *priv)
{
	return mxl862xx_issue_cmd(priv, MMD_API_RST_DATA, 0);
}

/* Minimum number of zero words in the data payload before issuing a
 * RST_DATA command is worthwhile.  RST_DATA costs one full command
 * round-trip (~5 MDIO transactions), so the threshold must offset that.
 */
#define RST_DATA_THRESHOLD	5

static int mxl862xx_send_cmd(struct mxl862xx_priv *priv, u16 cmd, u16 size,
			     bool quiet)
{
	int ret;

	ret = mxl862xx_issue_cmd(priv, cmd, size);

	/* Handle errors returned by the firmware as -EIO.
	 * The firmware is based on Zephyr OS and uses the errors as
	 * defined in errno.h of Zephyr OS. See
	 * https://github.com/zephyrproject-rtos/zephyr/blob/v3.7.0/lib/libc/minimal/include/errno.h
	 *
	 * The firmware signals CRC validation failures with dedicated
	 * error codes outside the normal Zephyr errno range:
	 *   -1024: CRC-6 mismatch on ctrl/len_ret registers
	 *   -1023: CRC-16 mismatch on data payload
	 */
	if (ret < 0) {
		if ((ret == MXL862XX_FW_CRC6_ERR ||
		     ret == MXL862XX_FW_CRC16_ERR) &&
		    !test_and_set_bit(MXL862XX_FLAG_CRC_ERR, &priv->flags))
			schedule_work(&priv->crc_err_work);
		if (!quiet)
			dev_err(&priv->mdiodev->dev,
				"CMD %04x returned error %d\n", cmd, ret);
		return -EIO;
	}

	return ret;
}

int mxl862xx_api_wrap(struct mxl862xx_priv *priv, u16 cmd, void *_data,
		      u16 size, bool read, bool quiet)
{
	__le16 *data = _data;
	bool use_rst = false;
	unsigned int zeros;
	int ret, cmd_ret;
	u16 max, crc, i;

	dev_dbg(&priv->mdiodev->dev, "CMD %04x DATA %*ph\n", cmd, size, data);

	mutex_lock_nested(&priv->mdiodev->bus->mdio_lock, MDIO_MUTEX_NESTED);

	max = (size + 1) / 2;

	ret = mxl862xx_busy_wait(priv);
	if (ret < 0)
		goto out;

	/* If the data contains enough zero words, issue RST_DATA to zero
	 * both the firmware buffer and MMD registers, then skip writing
	 * zero words individually.
	 */
	for (i = 0, zeros = 0; i < size / 2 && zeros < RST_DATA_THRESHOLD; i++)
		if (!data[i])
			zeros++;

	if (zeros < RST_DATA_THRESHOLD && (size & 1) && !*(u8 *)&data[i])
		zeros++;

	if (zeros >= RST_DATA_THRESHOLD) {
		ret = mxl862xx_rst_data(priv);
		if (ret < 0)
			goto out;
		use_rst = true;
	}

	/* Compute CRC-16 over the data payload; written as an extra word
	 * after the data so the firmware can verify the transfer.
	 */
	crc = crc16(0xffff, (const u8 *)data, size);

	for (i = 0; i < max + 1; i++) {
		u16 off = i % MXL862XX_MMD_REG_DATA_MAX_SIZE;
		u16 val;

		if (i && off == 0) {
			/* Send command to set data when every
			 * MXL862XX_MMD_REG_DATA_MAX_SIZE of WORDs are written.
			  */
			ret = mxl862xx_set_data(priv, i);
			if (ret < 0)
				goto out;
		}

		if (i == max) {
			/* Even size: full CRC word.
			 * Odd size: only CRC high byte remains (low byte
			 * was packed into the previous word).
			 */
			val = (size & 1) ? crc >> 8 : crc;
		} else if ((i * 2 + 1) == size) {
			/* Special handling for last BYTE if it's not WORD
			 * aligned to avoid reading beyond the allocated data
			 * structure.  Pack the CRC low byte into the high
			 * byte of this word so it sits at byte offset 'size'
			 * in the firmware's contiguous buffer.
			 */
			val = *(u8 *)&data[i] | ((crc & 0xff) << 8);
		} else {
			val = le16_to_cpu(data[i]);
		}

		/* After RST_DATA, skip zero data words as the registers
		 * already contain zeros, but never skip the CRC word at the
		 * final word.
		 */
		if (use_rst && i < max && val == 0)
			continue;

		ret = mxl862xx_reg_write(priv,
					 MXL862XX_MMD_REG_DATA_FIRST + off,
					 val);
		if (ret < 0)
			goto out;
	}

	ret = mxl862xx_send_cmd(priv, cmd, size, quiet);
	if (ret < 0 || !read)
		goto out;

	/* store result of mxl862xx_send_cmd() */
	cmd_ret = ret;

	for (i = 0; i < max + 1; i++) {
		u16 off = i % MXL862XX_MMD_REG_DATA_MAX_SIZE;

		if (i && off == 0) {
			/* Send command to fetch next batch of data when every
			 * MXL862XX_MMD_REG_DATA_MAX_SIZE of WORDs are read.
			  */
			ret = mxl862xx_get_data(priv, i);
			if (ret < 0)
				goto out;
		}

		ret = mxl862xx_reg_read(priv, MXL862XX_MMD_REG_DATA_FIRST + off);
		if (ret < 0)
			goto out;

		if (i == max) {
			/* Even size: full CRC word.
			 * Odd size: only CRC high byte remains (low byte
			 * was in the previous word).
			 */
			if (size & 1)
				crc = (crc & 0x00ff) |
				      (((u16)ret & 0xff) << 8);
			else
				crc = (u16)ret;
		} else if ((i * 2 + 1) == size) {
			/* Special handling for last BYTE if it's not WORD
			 * aligned to avoid writing beyond the allocated data
			 * structure.  The high byte carries the CRC low byte.
			 */
			*(uint8_t *)&data[i] = ret & 0xff;
			crc = (ret >> 8) & 0xff;
		} else {
			data[i] = cpu_to_le16((u16)ret);
		}
	}

	if (crc16(0xffff, (const u8 *)data, size) != crc) {
		if (!test_and_set_bit(MXL862XX_FLAG_CRC_ERR, &priv->flags))
			schedule_work(&priv->crc_err_work);
		ret = -EIO;
		goto out;
	}

	/* on success return the result of the mxl862xx_send_cmd() */
	ret = cmd_ret;

	dev_dbg(&priv->mdiodev->dev, "RET %d DATA %*ph\n", ret, size, data);

out:
	mutex_unlock(&priv->mdiodev->bus->mdio_lock);

	return ret;
}

int mxl862xx_reset(struct mxl862xx_priv *priv)
{
	int ret;

	mutex_lock_nested(&priv->mdiodev->bus->mdio_lock, MDIO_MUTEX_NESTED);

	/* Software reset */
	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_LEN_RET, 0);
	if (ret)
		goto out;

	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_CTRL, MXL862XX_SWITCH_RESET);
out:
	mutex_unlock(&priv->mdiodev->bus->mdio_lock);

	return ret;
}

void mxl862xx_host_init(struct mxl862xx_priv *priv)
{
	INIT_WORK(&priv->crc_err_work, mxl862xx_crc_err_work_fn);
}

void mxl862xx_host_shutdown(struct mxl862xx_priv *priv)
{
	cancel_work_sync(&priv->crc_err_work);
}
