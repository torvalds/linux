// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/dev_printk.h>
#include <linux/string.h>
#include <linux/string_choices.h>
#include <linux/types.h>

#include "core.h"
#include "out.h"

/**
 * zl3073x_out_state_fetch - fetch output state from hardware
 * @zldev: pointer to zl3073x_dev structure
 * @index: output index to fetch state for
 *
 * Function fetches state of the given output from hardware and stores it
 * for later use.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_out_state_fetch(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_out *out = &zldev->out[index];
	int rc;

	/* Read output configuration */
	rc = zl3073x_read_u8(zldev, ZL_REG_OUTPUT_CTRL(index), &out->ctrl);
	if (rc)
		return rc;

	dev_dbg(zldev->dev, "OUT%u is %s and connected to SYNTH%u\n", index,
		str_enabled_disabled(zl3073x_out_is_enabled(out)),
		zl3073x_out_synth_get(out));

	guard(mutex)(&zldev->multiop_lock);

	/* Read output configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_RD,
			   ZL_REG_OUTPUT_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Read output mode */
	rc = zl3073x_read_u8(zldev, ZL_REG_OUTPUT_MODE, &out->mode);
	if (rc)
		return rc;

	dev_dbg(zldev->dev, "OUT%u has signal format 0x%02x\n", index,
		zl3073x_out_signal_format_get(out));

	return rc;
}

/**
 * zl3073x_out_state_get - get current output state
 * @zldev: pointer to zl3073x_dev structure
 * @index: output index to get state for
 *
 * Return: pointer to given output state
 */
const struct zl3073x_out *zl3073x_out_state_get(struct zl3073x_dev *zldev,
						u8 index)
{
	return &zldev->out[index];
}
