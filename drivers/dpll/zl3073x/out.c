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

	/* Read output divisor */
	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_DIV, &out->div);
	if (rc)
		return rc;

	if (!out->div) {
		dev_err(zldev->dev, "Zero divisor for OUT%u got from device\n",
			index);
		return -EINVAL;
	}

	dev_dbg(zldev->dev, "OUT%u divisor: %u\n", index, out->div);

	/* Read output width */
	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_WIDTH, &out->width);
	if (rc)
		return rc;

	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_ESYNC_PERIOD,
			      &out->esync_n_period);
	if (rc)
		return rc;

	if (!out->esync_n_period) {
		dev_err(zldev->dev,
			"Zero esync divisor for OUT%u got from device\n",
			index);
		return -EINVAL;
	}

	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_ESYNC_WIDTH,
			      &out->esync_n_width);
	if (rc)
		return rc;

	rc = zl3073x_read_u32(zldev, ZL_REG_OUTPUT_PHASE_COMP,
			      &out->phase_comp);
	if (rc)
		return rc;

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

int zl3073x_out_state_set(struct zl3073x_dev *zldev, u8 index,
			  const struct zl3073x_out *out)
{
	struct zl3073x_out *dout = &zldev->out[index];
	int rc;

	guard(mutex)(&zldev->multiop_lock);

	/* Read output configuration into mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_RD,
			   ZL_REG_OUTPUT_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Update mailbox with changed values */
	if (dout->div != out->div)
		rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_DIV, out->div);
	if (!rc && dout->width != out->width)
		rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_WIDTH, out->width);
	if (!rc && dout->esync_n_period != out->esync_n_period)
		rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_ESYNC_PERIOD,
				       out->esync_n_period);
	if (!rc && dout->esync_n_width != out->esync_n_width)
		rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_ESYNC_WIDTH,
				       out->esync_n_width);
	if (!rc && dout->mode != out->mode)
		rc = zl3073x_write_u8(zldev, ZL_REG_OUTPUT_MODE, out->mode);
	if (!rc && dout->phase_comp != out->phase_comp)
		rc = zl3073x_write_u32(zldev, ZL_REG_OUTPUT_PHASE_COMP,
				       out->phase_comp);
	if (rc)
		return rc;

	/* Commit output configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_OUTPUT_MB_SEM, ZL_OUTPUT_MB_SEM_WR,
			   ZL_REG_OUTPUT_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* After successful commit store new state */
	dout->div = out->div;
	dout->width = out->width;
	dout->esync_n_period = out->esync_n_period;
	dout->esync_n_width = out->esync_n_width;
	dout->mode = out->mode;
	dout->phase_comp = out->phase_comp;

	return 0;
}
