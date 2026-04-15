// SPDX-License-Identifier: GPL-2.0-only

#include <linux/cleanup.h>
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
 * Reads the mode_refsel register and reference priority registers for
 * the given DPLL channel and stores the raw values for later use.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_chan_state_fetch(struct zl3073x_dev *zldev, u8 index)
{
	struct zl3073x_chan *chan = &zldev->chan[index];
	int rc, i;

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

	guard(mutex)(&zldev->multiop_lock);

	/* Read DPLL configuration from mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_RD,
			   ZL_REG_DPLL_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Read reference priority registers */
	for (i = 0; i < ARRAY_SIZE(chan->ref_prio); i++) {
		rc = zl3073x_read_u8(zldev, ZL_REG_DPLL_REF_PRIO(i),
				     &chan->ref_prio[i]);
		if (rc)
			return rc;
	}

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
 * writes only the changed registers to hardware. The mode_refsel register
 * is written directly, while the reference priority registers are written
 * via the DPLL mailbox interface.
 *
 * Return: 0 on success, <0 on HW error
 */
int zl3073x_chan_state_set(struct zl3073x_dev *zldev, u8 index,
			   const struct zl3073x_chan *chan)
{
	struct zl3073x_chan *dchan = &zldev->chan[index];
	int rc, i;

	/* Skip HW write if configuration hasn't changed */
	if (!memcmp(&dchan->cfg, &chan->cfg, sizeof(chan->cfg)))
		return 0;

	/* Direct register write for mode_refsel */
	if (dchan->mode_refsel != chan->mode_refsel) {
		rc = zl3073x_write_u8(zldev, ZL_REG_DPLL_MODE_REFSEL(index),
				      chan->mode_refsel);
		if (rc)
			return rc;
		dchan->mode_refsel = chan->mode_refsel;
	}

	/* Mailbox write for ref_prio if changed */
	if (!memcmp(dchan->ref_prio, chan->ref_prio, sizeof(chan->ref_prio))) {
		dchan->cfg = chan->cfg;
		return 0;
	}

	guard(mutex)(&zldev->multiop_lock);

	/* Read DPLL configuration into mailbox */
	rc = zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_RD,
			   ZL_REG_DPLL_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* Update changed ref_prio registers */
	for (i = 0; i < ARRAY_SIZE(chan->ref_prio); i++) {
		if (dchan->ref_prio[i] != chan->ref_prio[i]) {
			rc = zl3073x_write_u8(zldev,
					      ZL_REG_DPLL_REF_PRIO(i),
					      chan->ref_prio[i]);
			if (rc)
				return rc;
		}
	}

	/* Commit DPLL configuration */
	rc = zl3073x_mb_op(zldev, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_WR,
			   ZL_REG_DPLL_MB_MASK, BIT(index));
	if (rc)
		return rc;

	/* After successful write store new state */
	dchan->cfg = chan->cfg;

	return 0;
}
