/*
 * NXP Wireless LAN device driver: PCIE and platform specific quirks
 *
 * This software file (the "File") is distributed by NXP
 * under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include <linux/dmi.h>

#include "pcie_quirks.h"

/* quirk table based on DMI matching */
static const struct dmi_system_id mwifiex_quirk_table[] = {
	{}
};

void mwifiex_initialize_quirks(struct pcie_service_card *card)
{
	struct pci_dev *pdev = card->dev;
	const struct dmi_system_id *dmi_id;

	dmi_id = dmi_first_match(mwifiex_quirk_table);
	if (dmi_id)
		card->quirks = (uintptr_t)dmi_id->driver_data;

	if (!card->quirks)
		dev_info(&pdev->dev, "no quirks enabled\n");
}
