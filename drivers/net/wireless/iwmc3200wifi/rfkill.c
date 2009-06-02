/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/rfkill.h>

#include "iwm.h"

static int iwm_rfkill_set_block(void *data, bool blocked)
{
	struct iwm_priv *iwm = data;

	if (!blocked) {
		if (test_bit(IWM_RADIO_RFKILL_HW, &iwm->radio))
			return -EBUSY;

		if (test_and_clear_bit(IWM_RADIO_RFKILL_SW, &iwm->radio) &&
		    (iwm_to_ndev(iwm)->flags & IFF_UP))
			return iwm_up(iwm);
	} else {
		if (!test_and_set_bit(IWM_RADIO_RFKILL_SW, &iwm->radio))
			return iwm_down(iwm);
	}

	return 0;
}

static const struct rfkill_ops iwm_rfkill_ops = {
	.set_block = iwm_rfkill_set_block,
};

int iwm_rfkill_init(struct iwm_priv *iwm)
{
	int ret;

	iwm->rfkill = rfkill_alloc(KBUILD_MODNAME,
				   iwm_to_dev(iwm),
				   RFKILL_TYPE_WLAN,
				   &iwm_rfkill_ops, iwm);
	if (!iwm->rfkill) {
		IWM_ERR(iwm, "Unable to allocate rfkill device\n");
		return -ENOMEM;
	}

	ret = rfkill_register(iwm->rfkill);
	if (ret) {
		IWM_ERR(iwm, "Failed to register rfkill device\n");
		goto fail;
	}

	return 0;
 fail:
	rfkill_destroy(iwm->rfkill);
	return ret;
}

void iwm_rfkill_exit(struct iwm_priv *iwm)
{
	if (iwm->rfkill) {
		rfkill_unregister(iwm->rfkill);
		rfkill_destroy(iwm->rfkill);
	}
	iwm->rfkill = NULL;
}
