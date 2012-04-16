/*
 * Firmware loading and handling functions.
 */

#include <linux/firmware.h>
#include <linux/module.h>

#include "decl.h"

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
