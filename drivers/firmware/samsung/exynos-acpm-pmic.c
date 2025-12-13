// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Samsung Electronics Co., Ltd.
 * Copyright 2020 Google LLC.
 * Copyright 2024 Linaro Ltd.
 */
#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/firmware/samsung/exynos-acpm-protocol.h>
#include <linux/ktime.h>
#include <linux/types.h>

#include "exynos-acpm.h"
#include "exynos-acpm-pmic.h"

#define ACPM_PMIC_CHANNEL		GENMASK(15, 12)
#define ACPM_PMIC_TYPE			GENMASK(11, 8)
#define ACPM_PMIC_REG			GENMASK(7, 0)

#define ACPM_PMIC_RETURN		GENMASK(31, 24)
#define ACPM_PMIC_MASK			GENMASK(23, 16)
#define ACPM_PMIC_VALUE			GENMASK(15, 8)
#define ACPM_PMIC_FUNC			GENMASK(7, 0)

#define ACPM_PMIC_BULK_SHIFT		8
#define ACPM_PMIC_BULK_MASK		GENMASK(7, 0)
#define ACPM_PMIC_BULK_MAX_COUNT	8

enum exynos_acpm_pmic_func {
	ACPM_PMIC_READ,
	ACPM_PMIC_WRITE,
	ACPM_PMIC_UPDATE,
	ACPM_PMIC_BULK_READ,
	ACPM_PMIC_BULK_WRITE,
};

static const int acpm_pmic_linux_errmap[] = {
	[0] = 0, /* ACPM_PMIC_SUCCESS */
	[1] = -EACCES, /* Read register can't be accessed or issues to access it. */
	[2] = -EACCES, /* Write register can't be accessed or issues to access it. */
};

static int acpm_pmic_to_linux_err(int err)
{
	if (err >= 0 && err < ARRAY_SIZE(acpm_pmic_linux_errmap))
		return acpm_pmic_linux_errmap[err];
	return -EIO;
}

static inline u32 acpm_pmic_set_bulk(u32 data, unsigned int i)
{
	return (data & ACPM_PMIC_BULK_MASK) << (ACPM_PMIC_BULK_SHIFT * i);
}

static inline u32 acpm_pmic_get_bulk(u32 data, unsigned int i)
{
	return (data >> (ACPM_PMIC_BULK_SHIFT * i)) & ACPM_PMIC_BULK_MASK;
}

static void acpm_pmic_set_xfer(struct acpm_xfer *xfer, u32 *cmd, size_t cmdlen,
			       unsigned int acpm_chan_id)
{
	xfer->txd = cmd;
	xfer->rxd = cmd;
	xfer->txlen = cmdlen;
	xfer->rxlen = cmdlen;
	xfer->acpm_chan_id = acpm_chan_id;
}

static void acpm_pmic_init_read_cmd(u32 cmd[4], u8 type, u8 reg, u8 chan)
{
	cmd[0] = FIELD_PREP(ACPM_PMIC_TYPE, type) |
		 FIELD_PREP(ACPM_PMIC_REG, reg) |
		 FIELD_PREP(ACPM_PMIC_CHANNEL, chan);
	cmd[1] = FIELD_PREP(ACPM_PMIC_FUNC, ACPM_PMIC_READ);
	cmd[3] = ktime_to_ms(ktime_get());
}

int acpm_pmic_read_reg(const struct acpm_handle *handle,
		       unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
		       u8 *buf)
{
	struct acpm_xfer xfer;
	u32 cmd[4] = {0};
	int ret;

	acpm_pmic_init_read_cmd(cmd, type, reg, chan);
	acpm_pmic_set_xfer(&xfer, cmd, sizeof(cmd), acpm_chan_id);

	ret = acpm_do_xfer(handle, &xfer);
	if (ret)
		return ret;

	*buf = FIELD_GET(ACPM_PMIC_VALUE, xfer.rxd[1]);

	return acpm_pmic_to_linux_err(FIELD_GET(ACPM_PMIC_RETURN, xfer.rxd[1]));
}

static void acpm_pmic_init_bulk_read_cmd(u32 cmd[4], u8 type, u8 reg, u8 chan,
					 u8 count)
{
	cmd[0] = FIELD_PREP(ACPM_PMIC_TYPE, type) |
		 FIELD_PREP(ACPM_PMIC_REG, reg) |
		 FIELD_PREP(ACPM_PMIC_CHANNEL, chan);
	cmd[1] = FIELD_PREP(ACPM_PMIC_FUNC, ACPM_PMIC_BULK_READ) |
		 FIELD_PREP(ACPM_PMIC_VALUE, count);
}

int acpm_pmic_bulk_read(const struct acpm_handle *handle,
			unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
			u8 count, u8 *buf)
{
	struct acpm_xfer xfer;
	u32 cmd[4] = {0};
	int i, ret;

	if (count > ACPM_PMIC_BULK_MAX_COUNT)
		return -EINVAL;

	acpm_pmic_init_bulk_read_cmd(cmd, type, reg, chan, count);
	acpm_pmic_set_xfer(&xfer, cmd, sizeof(cmd), acpm_chan_id);

	ret = acpm_do_xfer(handle, &xfer);
	if (ret)
		return ret;

	ret = acpm_pmic_to_linux_err(FIELD_GET(ACPM_PMIC_RETURN, xfer.rxd[1]));
	if (ret)
		return ret;

	for (i = 0; i < count; i++) {
		if (i < 4)
			buf[i] = acpm_pmic_get_bulk(xfer.rxd[2], i);
		else
			buf[i] = acpm_pmic_get_bulk(xfer.rxd[3], i - 4);
	}

	return 0;
}

static void acpm_pmic_init_write_cmd(u32 cmd[4], u8 type, u8 reg, u8 chan,
				     u8 value)
{
	cmd[0] = FIELD_PREP(ACPM_PMIC_TYPE, type) |
		 FIELD_PREP(ACPM_PMIC_REG, reg) |
		 FIELD_PREP(ACPM_PMIC_CHANNEL, chan);
	cmd[1] = FIELD_PREP(ACPM_PMIC_FUNC, ACPM_PMIC_WRITE) |
		 FIELD_PREP(ACPM_PMIC_VALUE, value);
	cmd[3] = ktime_to_ms(ktime_get());
}

int acpm_pmic_write_reg(const struct acpm_handle *handle,
			unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
			u8 value)
{
	struct acpm_xfer xfer;
	u32 cmd[4] = {0};
	int ret;

	acpm_pmic_init_write_cmd(cmd, type, reg, chan, value);
	acpm_pmic_set_xfer(&xfer, cmd, sizeof(cmd), acpm_chan_id);

	ret = acpm_do_xfer(handle, &xfer);
	if (ret)
		return ret;

	return acpm_pmic_to_linux_err(FIELD_GET(ACPM_PMIC_RETURN, xfer.rxd[1]));
}

static void acpm_pmic_init_bulk_write_cmd(u32 cmd[4], u8 type, u8 reg, u8 chan,
					  u8 count, const u8 *buf)
{
	int i;

	cmd[0] = FIELD_PREP(ACPM_PMIC_TYPE, type) |
		 FIELD_PREP(ACPM_PMIC_REG, reg) |
		 FIELD_PREP(ACPM_PMIC_CHANNEL, chan);
	cmd[1] = FIELD_PREP(ACPM_PMIC_FUNC, ACPM_PMIC_BULK_WRITE) |
		 FIELD_PREP(ACPM_PMIC_VALUE, count);

	for (i = 0; i < count; i++) {
		if (i < 4)
			cmd[2] |= acpm_pmic_set_bulk(buf[i], i);
		else
			cmd[3] |= acpm_pmic_set_bulk(buf[i], i - 4);
	}
}

int acpm_pmic_bulk_write(const struct acpm_handle *handle,
			 unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
			 u8 count, const u8 *buf)
{
	struct acpm_xfer xfer;
	u32 cmd[4] = {0};
	int ret;

	if (count > ACPM_PMIC_BULK_MAX_COUNT)
		return -EINVAL;

	acpm_pmic_init_bulk_write_cmd(cmd, type, reg, chan, count, buf);
	acpm_pmic_set_xfer(&xfer, cmd, sizeof(cmd), acpm_chan_id);

	ret = acpm_do_xfer(handle, &xfer);
	if (ret)
		return ret;

	return acpm_pmic_to_linux_err(FIELD_GET(ACPM_PMIC_RETURN, xfer.rxd[1]));
}

static void acpm_pmic_init_update_cmd(u32 cmd[4], u8 type, u8 reg, u8 chan,
				      u8 value, u8 mask)
{
	cmd[0] = FIELD_PREP(ACPM_PMIC_TYPE, type) |
		 FIELD_PREP(ACPM_PMIC_REG, reg) |
		 FIELD_PREP(ACPM_PMIC_CHANNEL, chan);
	cmd[1] = FIELD_PREP(ACPM_PMIC_FUNC, ACPM_PMIC_UPDATE) |
		 FIELD_PREP(ACPM_PMIC_VALUE, value) |
		 FIELD_PREP(ACPM_PMIC_MASK, mask);
	cmd[3] = ktime_to_ms(ktime_get());
}

int acpm_pmic_update_reg(const struct acpm_handle *handle,
			 unsigned int acpm_chan_id, u8 type, u8 reg, u8 chan,
			 u8 value, u8 mask)
{
	struct acpm_xfer xfer;
	u32 cmd[4] = {0};
	int ret;

	acpm_pmic_init_update_cmd(cmd, type, reg, chan, value, mask);
	acpm_pmic_set_xfer(&xfer, cmd, sizeof(cmd), acpm_chan_id);

	ret = acpm_do_xfer(handle, &xfer);
	if (ret)
		return ret;

	return acpm_pmic_to_linux_err(FIELD_GET(ACPM_PMIC_RETURN, xfer.rxd[1]));
}
