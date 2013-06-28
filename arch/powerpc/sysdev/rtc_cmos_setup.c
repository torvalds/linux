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
#include <linux/module.h>
#include <linux/mc146818rtc.h>

#include <asm/prom.h>

static int  __init add_rtc(void)
{
	struct device_node *np;
	struct platform_device *pd;
	struct resource res[2];
	unsigned int num_res = 1;
	int ret;

	memset(&res, 0, sizeof(res));

	np = of_find_compatible_node(NULL, NULL, "pnpPNP,b00");
	if (!np)
		return -ENODEV;

	ret = of_address_to_resource(np, 0, &res[0]);
	of_node_put(np);
	if (ret)
		return ret;

	/*
	 * RTC_PORT(x) is hardcoded in asm/mc146818rtc.h.  Verify that the
	 * address provided by the device node matches.
	 */
	if (res[0].start != RTC_PORT(0))
		return -EINVAL;

	np = of_find_compatible_node(NULL, NULL, "chrp,iic");
	if (!np)
		np = of_find_compatible_node(NULL, NULL, "pnpPNP,000");
	if (np) {
		of_node_put(np);
		/*
		 * Use a fixed interrupt value of 8 since on PPC if we are
		 * using this its off an i8259 which we ensure has interrupt
		 * numbers 0..15.
		 */
		res[1].start = 8;
		res[1].end = 8;
		res[1].flags = IORESOURCE_IRQ;
		num_res++;
	}

	pd = platform_device_register_simple("rtc_cmos", -1,
					     &res[0], num_res);

	return PTR_RET(pd);
}
fs_initcall(add_rtc);

MODULE_LICENSE("GPL");
