/*
 * common EDAC components that must be in kernel
 *
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc.
 * 2010 (c) Advanced Micro Devices Inc.
 *	    Borislav Petkov <bp@alien8.de>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 */
#include <linux/module.h>
#include <linux/edac.h>
#include <linux/atomic.h>
#include <linux/device.h>

int edac_report_status = EDAC_REPORTING_ENABLED;
EXPORT_SYMBOL_GPL(edac_report_status);

static int __init edac_report_setup(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strncmp(str, "on", 2))
		set_edac_report_status(EDAC_REPORTING_ENABLED);
	else if (!strncmp(str, "off", 3))
		set_edac_report_status(EDAC_REPORTING_DISABLED);
	else if (!strncmp(str, "force", 5))
		set_edac_report_status(EDAC_REPORTING_FORCE);

	return 0;
}
__setup("edac_report=", edac_report_setup);
