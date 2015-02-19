/* Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Micky Ching <micky_ching@realsil.com.cn>
 */

#include <linux/mfd/rtsx_pci.h>
#include "rtsx_pcr.h"

int rtsx_gops_pm_reset(struct rtsx_pcr *pcr)
{
	int err;

	/* init aspm */
	rtsx_pci_write_register(pcr, ASPM_FORCE_CTL, 0xFF, 0x00);
	err = rtsx_pci_update_cfg_byte(pcr, LCTLR, ~LCTLR_ASPM_CTL_MASK, 0x00);
	if (err < 0)
		return err;

	/* reset PM_CTRL3 before send buffer cmd */
	return rtsx_pci_write_register(pcr, PM_CTRL3, D3_DELINK_MODE_EN, 0x00);
}
