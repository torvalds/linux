// SPDX-License-Identifier: GPL-2.0-only

#include <linux/dev_printk.h>
#include <linux/string.h>
#include <linux/types.h>

#include "chan.h"
#include "core.h"

/**
 * zl3073x_chan_state_update - update DPLL channel status from HW
 * @zldev: pointer to zl3073x_dev structure
 * @index: DPLL channel index
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_state_update(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_chan *chan = &zldev->chan[index];
	int rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MON_STATUS(index),
			     &chan->mon_status);
	if (rc)
		return rc;

	return zl3073x_read_u8(zldev, ZL_REG_DPLL_REFSEL_STATUS(index),
			       &chan->refsel_status);
}

/**
 * zl3073x_chan_state_fetch - fetch DPLL channel state from hardware
 * @zldev: pointer to zl3073x_dev structure
 * @index: DPLL channel index to fetch state for
 *
 * Reads the mode_refsel register for the given DPLL channel and stores
 * the raw value for later use.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_state_fetch(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_chan *chan = &zldev->chan[index];
	int rc;

	rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(index),
			     &chan->mode_refsel);
	if (rc)
		return rc;

	dev_dbg(zldev->dev, "DPLL%u mode: %u, ref: %u\n", index,
		zl3073x_chan_mode_get(chan), zl3073x_chan_ref_get(chan));

	rc = zl3073x_chan_state_update(zldev, index);
	if (rc)
		return rc;

	dev_dbg(zldev->dev,
		"DPLL%u lock_state: %u, ho: %u, sel_state: %u, sel_ref: %u\n",
		index, zl3073x_chan_lock_state_get(chan),
		zl3073x_chan_is_ho_ready(chan) ? 1 : 0,
		zl3073x_chan_refsel_state_get(chan),
		zl3073x_chan_refsel_ref_get(chan));

	return 0;
}

/**
 * zl3073x_chan_state_get - get current DPLL channel state
 * @zldev: pointer to zl3073x_dev structure
 * @index: DPLL channel index to get state for
 *
 * Return: pointer to given DPLL channel state
 */
const struct zl3073x_chan *zl3073x_chan_state_get(struct zl3073x_dev *zldev,
						  u8 index)
{
	return &zldev->chan[index];
}

/**
 * zl3073x_chan_state_set - commit DPLL channel state changes to hardware
 * @zldev: pointer to zl3073x_dev structure
 * @index: DPLL channel index to set state for
 * @chan: desired channel state
 *
 * Skips the HW write if the configuration is unchanged, and otherwise
 * writes the mode_refsel register to hardware.
 *
 * Return: 0 on success, <0 on HW error
 */
int zl3073x_chan_state_set(struct zl3073x_dev *zldev, u8 index,
			   const struct zl3073x_chan *chan)
{
	struct zl3073x_chan *dchan = &zldev->chan[index];
	int rc;

	/* Skip HW write if configuration hasn't changed */
	if (!memcmp(&dchan->cfg, &chan->cfg, sizeof(chan->cfg)))
		return 0;

	rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(index),
			      chan->mode_refsel);
	if (rc)
		return rc;

	/* After successful write store new state */
	dchan->cfg = chan->cfg;

	return 0;
}
