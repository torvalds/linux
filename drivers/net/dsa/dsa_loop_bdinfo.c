#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/phy.h>
#include <net/dsa.h>

#include "dsa_loop.h"

static struct dsa_loop_pdata dsa_loop_pdata = {
	.cd = {
		.port_names[0] = "lan1",
		.port_names[1] = "lan2",
		.port_names[2] = "lan3",
		.port_names[3] = "lan4",
		.port_names[DSA_LOOP_CPU_PORT] = "cpu",
	},
	.name = "DSA mockup driver",
	.enabled_ports = 0x1f,
	.netdev = "eth0",
};

static const struct mdio_board_info bdinfo = {
	.bus_id	= "fixed-0",
	.modalias = "dsa-loop",
	.mdio_addr = 31,
	.platform_data = &dsa_loop_pdata,
};

static int __init dsa_loop_bdinfo_init(void)
{
	return mdiobus_register_board_info(&bdinfo, 1);
}
arch_initcall(dsa_loop_bdinfo_init)

MODULE_LICENSE("GPL");
