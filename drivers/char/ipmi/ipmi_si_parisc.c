// SPDX-License-Identifier: GPL-2.0+

#include <linux/module.h>
#include <asm/hardware.h>	/* for register_parisc_driver() stuff */
#include <asm/parisc-device.h>
#include "ipmi_si.h"

static bool parisc_registered;

static int __init ipmi_parisc_probe(struct parisc_device *dev)
{
	struct si_sm_io io;

	memset(&io, 0, sizeof(io));

	io.si_info	= &ipmi_kcs_si_info;
	io.addr_source	= SI_DEVICETREE;
	io.addr_space	= IPMI_MEM_ADDR_SPACE;
	io.addr_data	= dev->hpa.start;
	io.regsize	= 1;
	io.regspacing	= 1;
	io.regshift	= 0;
	io.irq		= 0; /* no interrupt */
	io.irq_setup	= NULL;
	io.dev		= &dev->dev;

	dev_dbg(&dev->dev, "addr 0x%lx\n", io.addr_data);

	return ipmi_si_add_smi(&io);
}

static void __exit ipmi_parisc_remove(struct parisc_device *dev)
{
	ipmi_si_remove_by_dev(&dev->dev);
}

static const struct parisc_device_id ipmi_parisc_tbl[] __initconst = {
	{ HPHW_MC, HVERSION_REV_ANY_ID, 0x004, 0xC0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(parisc, ipmi_parisc_tbl);

static struct parisc_driver ipmi_parisc_driver __refdata = {
	.name =		"ipmi",
	.id_table =	ipmi_parisc_tbl,
	.probe =	ipmi_parisc_probe,
	.remove =	__exit_p(ipmi_parisc_remove),
};

void ipmi_si_parisc_init(void)
{
	register_parisc_driver(&ipmi_parisc_driver);
	parisc_registered = true;
}

void ipmi_si_parisc_shutdown(void)
{
	if (parisc_registered)
		unregister_parisc_driver(&ipmi_parisc_driver);
}
