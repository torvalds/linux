/*
 * cpcihp_zt5550.c
 *
 * Intel/Ziatech ZT5550 CompactPCI Host Controller driver
 *
 * Copyright 2002 SOMA Networks, Inc.
 * Copyright 2001 Intel San Luis Obispo
 * Copyright 2000,2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <scottm@somanetworks.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/signal.h>	/* IRQF_SHARED */
#include "cpci_hotplug.h"
#include "cpcihp_zt5550.h"

#define DRIVER_VERSION	"0.2"
#define DRIVER_AUTHOR	"Scott Murray <scottm@somanetworks.com>"
#define DRIVER_DESC	"ZT5550 CompactPCI Hot Plug Driver"

#define MY_NAME	"cpcihp_zt5550"

#define dbg(format, arg...)					\
	do {							\
		if (debug)					\
			printk (KERN_DEBUG "%s: " format "\n",	\
				MY_NAME , ## arg);		\
	} while(0)
#define err(format, arg...) printk(KERN_ERR "%s: " format "\n", MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format "\n", MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format "\n", MY_NAME , ## arg)

/* local variables */
static bool debug;
static bool poll;
static struct cpci_hp_controller_ops zt5550_hpc_ops;
static struct cpci_hp_controller zt5550_hpc;

/* Primary cPCI bus bridge device */
static struct pci_dev *bus0_dev;
static struct pci_bus *bus0;

/* Host controller device */
static struct pci_dev *hc_dev;

/* Host controller register addresses */
static void __iomem *hc_registers;
static void __iomem *csr_hc_index;
static void __iomem *csr_hc_data;
static void __iomem *csr_int_status;
static void __iomem *csr_int_mask;


static int zt5550_hc_config(struct pci_dev *pdev)
{
	int ret;

	/* Since we know that no boards exist with two HC chips, treat it as an error */
	if(hc_dev) {
		err("too many host controller devices?");
		return -EBUSY;
	}

	ret = pci_enable_device(pdev);
	if(ret) {
		err("cannot enable %s\n", pci_name(pdev));
		return ret;
	}

	hc_dev = pdev;
	dbg("hc_dev = %p", hc_dev);
	dbg("pci resource start %llx", (unsigned long long)pci_resource_start(hc_dev, 1));
	dbg("pci resource len %llx", (unsigned long long)pci_resource_len(hc_dev, 1));

	if(!request_mem_region(pci_resource_start(hc_dev, 1),
				pci_resource_len(hc_dev, 1), MY_NAME)) {
		err("cannot reserve MMIO region");
		ret = -ENOMEM;
		goto exit_disable_device;
	}

	hc_registers =
	    ioremap(pci_resource_start(hc_dev, 1), pci_resource_len(hc_dev, 1));
	if(!hc_registers) {
		err("cannot remap MMIO region %llx @ %llx",
			(unsigned long long)pci_resource_len(hc_dev, 1),
			(unsigned long long)pci_resource_start(hc_dev, 1));
		ret = -ENODEV;
		goto exit_release_region;
	}

	csr_hc_index = hc_registers + CSR_HCINDEX;
	csr_hc_data = hc_registers + CSR_HCDATA;
	csr_int_status = hc_registers + CSR_INTSTAT;
	csr_int_mask = hc_registers + CSR_INTMASK;

	/*
	 * Disable host control, fault and serial interrupts
	 */
	dbg("disabling host control, fault and serial interrupts");
	writeb((u8) HC_INT_MASK_REG, csr_hc_index);
	writeb((u8) ALL_INDEXED_INTS_MASK, csr_hc_data);
	dbg("disabled host control, fault and serial interrupts");

	/*
	 * Disable timer0, timer1 and ENUM interrupts
	 */
	dbg("disabling timer0, timer1 and ENUM interrupts");
	writeb((u8) ALL_DIRECT_INTS_MASK, csr_int_mask);
	dbg("disabled timer0, timer1 and ENUM interrupts");
	return 0;

exit_release_region:
	release_mem_region(pci_resource_start(hc_dev, 1),
			   pci_resource_len(hc_dev, 1));
exit_disable_device:
	pci_disable_device(hc_dev);
	return ret;
}

static int zt5550_hc_cleanup(void)
{
	if(!hc_dev)
		return -ENODEV;

	iounmap(hc_registers);
	release_mem_region(pci_resource_start(hc_dev, 1),
			   pci_resource_len(hc_dev, 1));
	pci_disable_device(hc_dev);
	return 0;
}

static int zt5550_hc_query_enum(void)
{
	u8 value;

	value = inb_p(ENUM_PORT);
	return ((value & ENUM_MASK) == ENUM_MASK);
}

static int zt5550_hc_check_irq(void *dev_id)
{
	int ret;
	u8 reg;

	ret = 0;
	if(dev_id == zt5550_hpc.dev_id) {
		reg = readb(csr_int_status);
		if(reg)
			ret = 1;
	}
	return ret;
}

static int zt5550_hc_enable_irq(void)
{
	u8 reg;

	if(hc_dev == NULL) {
		return -ENODEV;
	}
	reg = readb(csr_int_mask);
	reg = reg & ~ENUM_INT_MASK;
	writeb(reg, csr_int_mask);
	return 0;
}

static int zt5550_hc_disable_irq(void)
{
	u8 reg;

	if(hc_dev == NULL) {
		return -ENODEV;
	}

	reg = readb(csr_int_mask);
	reg = reg | ENUM_INT_MASK;
	writeb(reg, csr_int_mask);
	return 0;
}

static int zt5550_hc_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int status;

	status = zt5550_hc_config(pdev);
	if(status != 0) {
		return status;
	}
	dbg("returned from zt5550_hc_config");

	memset(&zt5550_hpc, 0, sizeof (struct cpci_hp_controller));
	zt5550_hpc_ops.query_enum = zt5550_hc_query_enum;
	zt5550_hpc.ops = &zt5550_hpc_ops;
	if(!poll) {
		zt5550_hpc.irq = hc_dev->irq;
		zt5550_hpc.irq_flags = IRQF_SHARED;
		zt5550_hpc.dev_id = hc_dev;

		zt5550_hpc_ops.enable_irq = zt5550_hc_enable_irq;
		zt5550_hpc_ops.disable_irq = zt5550_hc_disable_irq;
		zt5550_hpc_ops.check_irq = zt5550_hc_check_irq;
	} else {
		info("using ENUM# polling mode");
	}

	status = cpci_hp_register_controller(&zt5550_hpc);
	if(status != 0) {
		err("could not register cPCI hotplug controller");
		goto init_hc_error;
	}
	dbg("registered controller");

	/* Look for first device matching cPCI bus's bridge vendor and device IDs */
	if(!(bus0_dev = pci_get_device(PCI_VENDOR_ID_DEC,
					 PCI_DEVICE_ID_DEC_21154, NULL))) {
		status = -ENODEV;
		goto init_register_error;
	}
	bus0 = bus0_dev->subordinate;
	pci_dev_put(bus0_dev);

	status = cpci_hp_register_bus(bus0, 0x0a, 0x0f);
	if(status != 0) {
		err("could not register cPCI hotplug bus");
		goto init_register_error;
	}
	dbg("registered bus");

	status = cpci_hp_start();
	if(status != 0) {
		err("could not started cPCI hotplug system");
		cpci_hp_unregister_bus(bus0);
		goto init_register_error;
	}
	dbg("started cpci hp system");

	return 0;
init_register_error:
	cpci_hp_unregister_controller(&zt5550_hpc);
init_hc_error:
	err("status = %d", status);
	zt5550_hc_cleanup();
	return status;

}

static void zt5550_hc_remove_one(struct pci_dev *pdev)
{
	cpci_hp_stop();
	cpci_hp_unregister_bus(bus0);
	cpci_hp_unregister_controller(&zt5550_hpc);
	zt5550_hc_cleanup();
}


static struct pci_device_id zt5550_hc_pci_tbl[] = {
	{ PCI_VENDOR_ID_ZIATECH, PCI_DEVICE_ID_ZIATECH_5550_HC, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, zt5550_hc_pci_tbl);

static struct pci_driver zt5550_hc_driver = {
	.name		= "zt5550_hc",
	.id_table	= zt5550_hc_pci_tbl,
	.probe		= zt5550_hc_init_one,
	.remove		= zt5550_hc_remove_one,
};

static int __init zt5550_init(void)
{
	struct resource* r;
	int rc;

	info(DRIVER_DESC " version: " DRIVER_VERSION);
	r = request_region(ENUM_PORT, 1, "#ENUM hotswap signal register");
	if(!r)
		return -EBUSY;

	rc = pci_register_driver(&zt5550_hc_driver);
	if(rc < 0)
		release_region(ENUM_PORT, 1);
	return rc;
}

static void __exit
zt5550_exit(void)
{
	pci_unregister_driver(&zt5550_hc_driver);
	release_region(ENUM_PORT, 1);
}

module_init(zt5550_init);
module_exit(zt5550_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");
module_param(poll, bool, 0644);
MODULE_PARM_DESC(poll, "#ENUM polling mode enabled or not");
