/*
 * arch/arm/mach-kirkwood/nsa-310-setup.c
 *
 * ZyXEL NSA-310 Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <mach/kirkwood.h>
#include <linux/of.h>
#include "common.h"
#include "mpp.h"

static unsigned int nsa310_mpp_config[] __initdata = {
	MPP12_GPIO, /* led esata green */
	MPP13_GPIO, /* led esata red */
	MPP15_GPIO, /* led usb green */
	MPP16_GPIO, /* led usb red */
	MPP21_GPIO, /* control usb power off */
	MPP28_GPIO, /* led sys green */
	MPP29_GPIO, /* led sys red */
	MPP36_GPIO, /* key reset */
	MPP37_GPIO, /* key copy */
	MPP39_GPIO, /* led copy green */
	MPP40_GPIO, /* led copy red */
	MPP41_GPIO, /* led hdd green */
	MPP42_GPIO, /* led hdd red */
	MPP44_GPIO, /* ?? */
	MPP46_GPIO, /* key power */
	MPP48_GPIO, /* control power off */
	0
};

void __init nsa310_init(void)
{
	kirkwood_mpp_conf(nsa310_mpp_config);
}

static int __init nsa310_pci_init(void)
{
	if (of_machine_is_compatible("zyxel,nsa310"))
		kirkwood_pcie_init(KW_PCIE0);

	return 0;
}

subsys_initcall(nsa310_pci_init);
