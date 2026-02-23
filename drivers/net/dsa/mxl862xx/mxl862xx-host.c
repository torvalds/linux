// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Based upon the MaxLinear SDK driver
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 * Copyright (C) 2025 John Crispin <john@phrozen.org>
 * Copyright (C) 2024 MaxLinear Inc.
 */

#include <linux/bits.h>
#include <linux/iopoll.h>
#include <linux/limits.h>
#include <net/dsa.h>
#include "mxl862xx.h"
#include "mxl862xx-host.h"

#define CTRL_BUSY_MASK			BIT(15)

#define MXL862XX_MMD_REG_CTRL		0
#define MXL862XX_MMD_REG_LEN_RET	1
#define MXL862XX_MMD_REG_DATA_FIRST	2
#define MXL862XX_MMD_REG_DATA_LAST	95
#define MXL862XX_MMD_REG_DATA_MAX_SIZE \
		(MXL862XX_MMD_REG_DATA_LAST - MXL862XX_MMD_REG_DATA_FIRST + 1)

#define MMD_API_SET_DATA_0		2
#define MMD_API_GET_DATA_0		5
#define MMD_API_RST_DATA		8

#define MXL862XX_SWITCH_RESET 0x9907

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

static int mxl862xx_set_data(struct mxl862xx_priv *priv, u16 words)
{
	int ret;
	u16 cmd;

	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_LEN_RET,
				 MXL862XX_MMD_REG_DATA_MAX_SIZE * sizeof(u16));
	if (ret < 0)
		return ret;

	cmd = words / MXL862XX_MMD_REG_DATA_MAX_SIZE - 1;
	if (!(cmd < 2))
		return -EINVAL;

	cmd += MMD_API_SET_DATA_0;
	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_CTRL,
				 cmd | CTRL_BUSY_MASK);
	if (ret < 0)
		return ret;

	return mxl862xx_busy_wait(priv);
}

static int mxl862xx_get_data(struct mxl862xx_priv *priv, u16 words)
{
	int ret;
	u16 cmd;

	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_LEN_RET,
				 MXL862XX_MMD_REG_DATA_MAX_SIZE * sizeof(u16));
	if (ret < 0)
		return ret;

	cmd = words / MXL862XX_MMD_REG_DATA_MAX_SIZE;
	if (!(cmd > 0 && cmd < 3))
		return -EINVAL;

	cmd += MMD_API_GET_DATA_0;
	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_CTRL,
				 cmd | CTRL_BUSY_MASK);
	if (ret < 0)
		return ret;

	return mxl862xx_busy_wait(priv);
}

static int mxl862xx_firmware_return(int ret)
{
	/* Only 16-bit values are valid. */
	if (WARN_ON(ret & GENMASK(31, 16)))
		return -EINVAL;

	/* Interpret value as signed 16-bit integer. */
	return (s16)ret;
}

static int mxl862xx_send_cmd(struct mxl862xx_priv *priv, u16 cmd, u16 size,
			     bool quiet)
{
	int ret;

	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_LEN_RET, size);
	if (ret)
		return ret;

	ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_CTRL,
				 cmd | CTRL_BUSY_MASK);
	if (ret)
		return ret;

	ret = mxl862xx_busy_wait(priv);
	if (ret)
		return ret;

	ret = mxl862xx_reg_read(priv, MXL862XX_MMD_REG_LEN_RET);
	if (ret < 0)
		return ret;

	/* handle errors returned by the firmware as -EIO
	 * The firmware is based on Zephyr OS and uses the errors as
	 * defined in errno.h of Zephyr OS. See
	 * https://github.com/zephyrproject-rtos/zephyr/blob/v3.7.0/lib/libc/minimal/include/errno.h
	 */
	ret = mxl862xx_firmware_return(ret);
	if (ret < 0) {
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
	int ret, cmd_ret;
	u16 max, i;

	dev_dbg(&priv->mdiodev->dev, "CMD %04x DATA %*ph\n", cmd, size, data);

	mutex_lock_nested(&priv->mdiodev->bus->mdio_lock, MDIO_MUTEX_NESTED);

	max = (size + 1) / 2;

	ret = mxl862xx_busy_wait(priv);
	if (ret < 0)
		goto out;

	for (i = 0; i < max; i++) {
		u16 off = i % MXL862XX_MMD_REG_DATA_MAX_SIZE;

		if (i && off == 0) {
			/* Send command to set data when every
			 * MXL862XX_MMD_REG_DATA_MAX_SIZE of WORDs are written.
			 */
			ret = mxl862xx_set_data(priv, i);
			if (ret < 0)
				goto out;
		}

		ret = mxl862xx_reg_write(priv, MXL862XX_MMD_REG_DATA_FIRST + off,
					 le16_to_cpu(data[i]));
		if (ret < 0)
			goto out;
	}

	ret = mxl862xx_send_cmd(priv, cmd, size, quiet);
	if (ret < 0 || !read)
		goto out;

	/* store result of mxl862xx_send_cmd() */
	cmd_ret = ret;

	for (i = 0; i < max; i++) {
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

		if ((i * 2 + 1) == size) {
			/* Special handling for last BYTE if it's not WORD
			 * aligned to avoid writing beyond the allocated data
			 * structure.
			 */
			*(uint8_t *)&data[i] = ret & 0xff;
		} else {
			data[i] = cpu_to_le16((u16)ret);
		}
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
