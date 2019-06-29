// SPDX-License-Identifier: GPL-2.0-only
/*
 * Firmware loading and handling functions.
 */

#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/module.h>

#include "dev.h"
#include "decl.h"

static void load_next_firmware_from_table(struct lbs_private *private);

static void lbs_fw_loaded(struct lbs_private *priv, int ret,
	const struct firmware *helper, const struct firmware *mainfw)
{
	unsigned long flags;

	lbs_deb_fw("firmware load complete, code %d\n", ret);

	/* User must free helper/mainfw */
	priv->fw_callback(priv, ret, helper, mainfw);

	spin_lock_irqsave(&priv->driver_lock, flags);
	priv->fw_callback = NULL;
	wake_up(&priv->fw_waitq);
	spin_unlock_irqrestore(&priv->driver_lock, flags);
}

static void do_load_firmware(struct lbs_private *priv, const char *name,
	void (*cb)(const struct firmware *fw, void *context))
{
	int ret;

	lbs_deb_fw("Requesting %s\n", name);
	ret = request_firmware_nowait(THIS_MODULE, true, name,
			priv->fw_device, GFP_KERNEL, priv, cb);
	if (ret) {
		lbs_deb_fw("request_firmware_nowait error %d\n", ret);
		lbs_fw_loaded(priv, ret, NULL, NULL);
	}
}

static void main_firmware_cb(const struct firmware *firmware, void *context)
{
	struct lbs_private *priv = context;

	if (!firmware) {
		/* Failed to find firmware: try next table entry */
		load_next_firmware_from_table(priv);
		return;
	}

	/* Firmware found! */
	lbs_fw_loaded(priv, 0, priv->helper_fw, firmware);
	if (priv->helper_fw) {
		release_firmware (priv->helper_fw);
		priv->helper_fw = NULL;
	}
	release_firmware (firmware);
}

static void helper_firmware_cb(const struct firmware *firmware, void *context)
{
	struct lbs_private *priv = context;

	if (!firmware) {
		/* Failed to find firmware: try next table entry */
		load_next_firmware_from_table(priv);
		return;
	}

	/* Firmware found! */
	if (priv->fw_iter->fwname) {
		priv->helper_fw = firmware;
		do_load_firmware(priv, priv->fw_iter->fwname, main_firmware_cb);
	} else {
		/* No main firmware needed for this helper --> success! */
		lbs_fw_loaded(priv, 0, firmware, NULL);
	}
}

static void load_next_firmware_from_table(struct lbs_private *priv)
{
	const struct lbs_fw_table *iter;

	if (!priv->fw_iter)
		iter = priv->fw_table;
	else
		iter = ++priv->fw_iter;

	if (priv->helper_fw) {
		release_firmware(priv->helper_fw);
		priv->helper_fw = NULL;
	}

next:
	if (!iter->helper) {
		/* End of table hit. */
		lbs_fw_loaded(priv, -ENOENT, NULL, NULL);
		return;
	}

	if (iter->model != priv->fw_model) {
		iter++;
		goto next;
	}

	priv->fw_iter = iter;
	do_load_firmware(priv, iter->helper, helper_firmware_cb);
}

void lbs_wait_for_firmware_load(struct lbs_private *priv)
{
	wait_event(priv->fw_waitq, priv->fw_callback == NULL);
}

/**
 *  lbs_get_firmware_async - Retrieves firmware asynchronously. Can load
 *  either a helper firmware and a main firmware (2-stage), or just the helper.
 *
 *  @priv:      Pointer to lbs_private instance
 *  @dev:     	A pointer to &device structure
 *  @card_model: Bus-specific card model ID used to filter firmware table
 *		elements
 *  @fw_table:	Table of firmware file names and device model numbers
 *		terminated by an entry with a NULL helper name
 *	@callback: User callback to invoke when firmware load succeeds or fails.
 */
int lbs_get_firmware_async(struct lbs_private *priv, struct device *device,
			    u32 card_model, const struct lbs_fw_table *fw_table,
			    lbs_fw_cb callback)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->driver_lock, flags);
	if (priv->fw_callback) {
		lbs_deb_fw("firmware load already in progress\n");
		spin_unlock_irqrestore(&priv->driver_lock, flags);
		return -EBUSY;
	}

	priv->fw_device = device;
	priv->fw_callback = callback;
	priv->fw_table = fw_table;
	priv->fw_iter = NULL;
	priv->fw_model = card_model;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	lbs_deb_fw("Starting async firmware load\n");
	load_next_firmware_from_table(priv);
	return 0;
}
EXPORT_SYMBOL_GPL(lbs_get_firmware_async);

/**
 *  lbs_get_firmware - Retrieves two-stage firmware
 *
 *  @dev:     	A pointer to &device structure
 *  @card_model: Bus-specific card model ID used to filter firmware table
 *		elements
 *  @fw_table:	Table of firmware file names and device model numbers
 *		terminated by an entry with a NULL helper name
 *  @helper:	On success, the helper firmware; caller must free
 *  @mainfw:	On success, the main firmware; caller must free
 *
 * Deprecated: use lbs_get_firmware_async() instead.
 *
 *  returns:		0 on success, non-zero on failure
 */
int lbs_get_firmware(struct device *dev, u32 card_model,
			const struct lbs_fw_table *fw_table,
			const struct firmware **helper,
			const struct firmware **mainfw)
{
	const struct lbs_fw_table *iter;
	int ret;

	BUG_ON(helper == NULL);
	BUG_ON(mainfw == NULL);

	/* Search for firmware to use from the table. */
	iter = fw_table;
	while (iter && iter->helper) {
		if (iter->model != card_model)
			goto next;

		if (*helper == NULL) {
			ret = request_firmware(helper, iter->helper, dev);
			if (ret)
				goto next;

			/* If the device has one-stage firmware (ie cf8305) and
			 * we've got it then we don't need to bother with the
			 * main firmware.
			 */
			if (iter->fwname == NULL)
				return 0;
		}

		if (*mainfw == NULL) {
			ret = request_firmware(mainfw, iter->fwname, dev);
			if (ret) {
				/* Clear the helper to ensure we don't have
				 * mismatched firmware pairs.
				 */
				release_firmware(*helper);
				*helper = NULL;
			}
		}

		if (*helper && *mainfw)
			return 0;

  next:
		iter++;
	}

	/* Failed */
	release_firmware(*helper);
	*helper = NULL;
	release_firmware(*mainfw);
	*mainfw = NULL;

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(lbs_get_firmware);
