/*
 * Bridge between MCE and APEI
 *
 * On some machine, corrected memory errors are reported via APEI
 * generic hardware error source (GHES) instead of corrected Machine
 * Check. These corrected memory errors can be reported to user space
 * through /dev/mcelog via faking a corrected Machine Check, so that
 * the error memory page can be offlined by /sbin/mcelog if the error
 * count for one page is beyond the threshold.
 *
 * Copyright 2010 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/cper.h>
#include <acpi/apei.h>
#include <asm/mce.h>

#include "mce-internal.h"

void apei_mce_report_mem_error(int corrected, struct cper_sec_mem_err *mem_err)
{
	struct mce m;

	/* Only corrected MC is reported */
	if (!corrected)
		return;

	mce_setup(&m);
	m.bank = 1;
	/* Fake a memory read corrected error with unknown channel */
	m.status = MCI_STATUS_VAL | MCI_STATUS_EN | MCI_STATUS_ADDRV | 0x9f;
	m.addr = mem_err->physical_addr;
	mce_log(&m);
	mce_notify_irq();
}
EXPORT_SYMBOL_GPL(apei_mce_report_mem_error);
