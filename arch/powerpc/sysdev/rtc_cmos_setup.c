/*
 * Setup code for PC-style Real-Time Clock.
 *
 * Author: Wade Farnsworth <wfarnsworth@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>

#include <asm/prom.h>

static int  __init add_rtc(void)
{
	struct device_node *np;
	struct platform_device *pd;
	struct resource res;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "pnpPNP,b00");
	if (!np)
		return -ENODEV;

	ret = of_address_to_resource(np, 0, &res);
	of_node_put(np);
	if (ret)
		return ret;

	/*
	 * RTC_PORT(x) is hardcoded in asm/mc146818rtc.h.  Verify that the
	 * address provided by the device node matches.
	 */
	if (res.start != RTC_PORT(0))
		return -EINVAL;

	pd = platform_device_register_simple("rtc_cmos", -1,
					     &res, 1);
	if (IS_ERR(pd))
		return PTR_ERR(pd);

	return 0;
}
fs_initcall(add_rtc);
