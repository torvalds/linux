/*
	The all defines and part of code (such as cs461x_*) are
	contributed from ALSA 0.5.8 sources.
	See http://www.alsa-project.org/ for sources

	Tested on Linux 686 2.4.0-test9, ALSA 0.5.8a and CS4610
*/

#include <asm/io.h>

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/slab.h>
#include <linux/pci.h>

MODULE_AUTHOR("Victor Krapivin");
MODULE_LICENSE("GPL");

/*
	These options are experimental

#define CS461X_FULL_MAP
*/


#ifndef PCI_VENDOR_ID_CIRRUS
#define PCI_VENDOR_ID_CIRRUS            0x1013
#endif
#ifndef PCI_DEVICE_ID_CIRRUS_4610
#define PCI_DEVICE_ID_CIRRUS_4610       0x6001
#endif
#ifndef PCI_DEVICE_ID_CIRRUS_4612
#define PCI_DEVICE_ID_CIRRUS_4612       0x6003
#endif
#ifndef PCI_DEVICE_ID_CIRRUS_4615
#define PCI_DEVICE_ID_CIRRUS_4615       0x6004
#endif

/* Registers */

#define BA0_JSPT                                0x00000480
#define BA0_JSCTL                               0x00000484
#define BA0_JSC1                                0x00000488
#define BA0_JSC2                                0x0000048C
#define BA0_JSIO                                0x000004A0

/* Bits for JSPT */

#define JSPT_CAX                                0x00000001
#define JSPT_CAY                                0x00000002
#define JSPT_CBX                                0x00000004
#define JSPT_CBY                                0x00000008
#define JSPT_BA1                                0x00000010
#define JSPT_BA2                                0x00000020
#define JSPT_BB1                                0x00000040
#define JSPT_BB2                                0x00000080

/* Bits for JSCTL */

#define JSCTL_SP_MASK                           0x00000003
#define JSCTL_SP_SLOW                           0x00000000
#define JSCTL_SP_MEDIUM_SLOW                    0x00000001
#define JSCTL_SP_MEDIUM_FAST                    0x00000002
#define JSCTL_SP_FAST                           0x00000003
#define JSCTL_ARE                               0x00000004

/* Data register pairs masks */

#define JSC1_Y1V_MASK                           0x0000FFFF
#define JSC1_X1V_MASK                           0xFFFF0000
#define JSC1_Y1V_SHIFT                          0
#define JSC1_X1V_SHIFT                          16
#define JSC2_Y2V_MASK                           0x0000FFFF
#define JSC2_X2V_MASK                           0xFFFF0000
#define JSC2_Y2V_SHIFT                          0
#define JSC2_X2V_SHIFT                          16

/* JS GPIO */

#define JSIO_DAX                                0x00000001
#define JSIO_DAY                                0x00000002
#define JSIO_DBX                                0x00000004
#define JSIO_DBY                                0x00000008
#define JSIO_AXOE                               0x00000010
#define JSIO_AYOE                               0x00000020
#define JSIO_BXOE                               0x00000040
#define JSIO_BYOE                               0x00000080

/*
   The card initialization code is obfuscated; the module cs461x
   need to be loaded after ALSA modules initialized and something
   played on the CS 4610 chip (see sources for details of CS4610
   initialization code from ALSA)
*/

/* Card specific definitions */

#define CS461X_BA0_SIZE         0x2000
#define CS461X_BA1_DATA0_SIZE   0x3000
#define CS461X_BA1_DATA1_SIZE   0x3800
#define CS461X_BA1_PRG_SIZE     0x7000
#define CS461X_BA1_REG_SIZE     0x0100

#define BA1_SP_DMEM0                            0x00000000
#define BA1_SP_DMEM1                            0x00010000
#define BA1_SP_PMEM                             0x00020000
#define BA1_SP_REG                              0x00030000

#define BA1_DWORD_SIZE          (13 * 1024 + 512)
#define BA1_MEMORY_COUNT        3

/*
   Only one CS461x card is still suppoted; the code requires
   redesign to avoid this limitatuion.
*/

static unsigned long ba0_addr;
static unsigned int __iomem *ba0;

#ifdef CS461X_FULL_MAP
static unsigned long ba1_addr;
static union ba1_t {
        struct {
                unsigned int __iomem *data0;
                unsigned int __iomem *data1;
                unsigned int __iomem *pmem;
                unsigned int __iomem *reg;
        } name;
        unsigned int __iomem *idx[4];
} ba1;

static void cs461x_poke(unsigned long reg, unsigned int val)
{
        writel(val, &ba1.idx[(reg >> 16) & 3][(reg >> 2) & 0x3fff]);
}

static unsigned int cs461x_peek(unsigned long reg)
{
        return readl(&ba1.idx[(reg >> 16) & 3][(reg >> 2) & 0x3fff]);
}

#endif

static void cs461x_pokeBA0(unsigned long reg, unsigned int val)
{
        writel(val, &ba0[reg >> 2]);
}

static unsigned int cs461x_peekBA0(unsigned long reg)
{
        return readl(&ba0[reg >> 2]);
}

static int cs461x_free(struct pci_dev *pdev)
{
	struct gameport *port = pci_get_drvdata(pdev);

	if (port)
	    gameport_unregister_port(port);

	if (ba0) iounmap(ba0);
#ifdef CS461X_FULL_MAP
	if (ba1.name.data0) iounmap(ba1.name.data0);
	if (ba1.name.data1) iounmap(ba1.name.data1);
	if (ba1.name.pmem)  iounmap(ba1.name.pmem);
	if (ba1.name.reg)   iounmap(ba1.name.reg);
#endif
	return 0;
}

static void cs461x_gameport_trigger(struct gameport *gameport)
{
	cs461x_pokeBA0(BA0_JSPT, 0xFF);  //outb(gameport->io, 0xFF);
}

static unsigned char cs461x_gameport_read(struct gameport *gameport)
{
	return cs461x_peekBA0(BA0_JSPT); //inb(gameport->io);
}

static int cs461x_gameport_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	unsigned js1, js2, jst;

	js1 = cs461x_peekBA0(BA0_JSC1);
	js2 = cs461x_peekBA0(BA0_JSC2);
	jst = cs461x_peekBA0(BA0_JSPT);

	*buttons = (~jst >> 4) & 0x0F;

	axes[0] = ((js1 & JSC1_Y1V_MASK) >> JSC1_Y1V_SHIFT) & 0xFFFF;
	axes[1] = ((js1 & JSC1_X1V_MASK) >> JSC1_X1V_SHIFT) & 0xFFFF;
	axes[2] = ((js2 & JSC2_Y2V_MASK) >> JSC2_Y2V_SHIFT) & 0xFFFF;
	axes[3] = ((js2 & JSC2_X2V_MASK) >> JSC2_X2V_SHIFT) & 0xFFFF;

	for(jst=0;jst<4;++jst)
		if(axes[jst]==0xFFFF) axes[jst] = -1;
	return 0;
}

static int cs461x_gameport_open(struct gameport *gameport, int mode)
{
	switch (mode) {
		case GAMEPORT_MODE_COOKED:
		case GAMEPORT_MODE_RAW:
			return 0;
		default:
			return -1;
	}
	return 0;
}

static struct pci_device_id cs461x_pci_tbl[] = {
	{ PCI_VENDOR_ID_CIRRUS, 0x6001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* Cirrus CS4610 */
	{ PCI_VENDOR_ID_CIRRUS, 0x6003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* Cirrus CS4612 */
	{ PCI_VENDOR_ID_CIRRUS, 0x6005, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 }, /* Cirrus CS4615 */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, cs461x_pci_tbl);

static int __devinit cs461x_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int rc;
	struct gameport* port;

	rc = pci_enable_device(pdev);
	if (rc) {
		printk(KERN_ERR "cs461x: Cannot enable PCI gameport (bus %d, devfn %d) error=%d\n",
			pdev->bus->number, pdev->devfn, rc);
		return rc;
	}

	ba0_addr = pci_resource_start(pdev, 0);
#ifdef CS461X_FULL_MAP
	ba1_addr = pci_resource_start(pdev, 1);
#endif
	if (ba0_addr == 0 || ba0_addr == ~0
#ifdef CS461X_FULL_MAP
            || ba1_addr == 0 || ba1_addr == ~0
#endif
	    ) {
                printk(KERN_ERR "cs461x: wrong address - ba0 = 0x%lx\n", ba0_addr);
#ifdef CS461X_FULL_MAP
                printk(KERN_ERR "cs461x: wrong address - ba1 = 0x%lx\n", ba1_addr);
#endif
		cs461x_free(pdev);
                return -ENOMEM;
        }

	ba0 = ioremap(ba0_addr, CS461X_BA0_SIZE);
#ifdef CS461X_FULL_MAP
	ba1.name.data0 = ioremap(ba1_addr + BA1_SP_DMEM0, CS461X_BA1_DATA0_SIZE);
	ba1.name.data1 = ioremap(ba1_addr + BA1_SP_DMEM1, CS461X_BA1_DATA1_SIZE);
	ba1.name.pmem  = ioremap(ba1_addr + BA1_SP_PMEM, CS461X_BA1_PRG_SIZE);
	ba1.name.reg   = ioremap(ba1_addr + BA1_SP_REG, CS461X_BA1_REG_SIZE);

	if (ba0 == NULL || ba1.name.data0 == NULL ||
            ba1.name.data1 == NULL || ba1.name.pmem == NULL ||
            ba1.name.reg == NULL) {
		cs461x_free(pdev);
                return -ENOMEM;
        }
#else
	if (ba0 == NULL) {
		cs461x_free(pdev);
		return -ENOMEM;
	}
#endif

	if (!(port = gameport_allocate_port())) {
		printk(KERN_ERR "cs461x: Memory allocation failed\n");
		cs461x_free(pdev);
		return -ENOMEM;
	}

	pci_set_drvdata(pdev, port);

	port->open = cs461x_gameport_open;
	port->trigger = cs461x_gameport_trigger;
	port->read = cs461x_gameport_read;
	port->cooked_read = cs461x_gameport_cooked_read;

	gameport_set_name(port, "CS416x");
	gameport_set_phys(port, "pci%s/gameport0", pci_name(pdev));
	port->dev.parent = &pdev->dev;

	cs461x_pokeBA0(BA0_JSIO, 0xFF); // ?
	cs461x_pokeBA0(BA0_JSCTL, JSCTL_SP_MEDIUM_SLOW);

	gameport_register_port(port);

	return 0;
}

static void __devexit cs461x_pci_remove(struct pci_dev *pdev)
{
	cs461x_free(pdev);
}

static struct pci_driver cs461x_pci_driver = {
        .name =         "CS461x_gameport",
        .id_table =     cs461x_pci_tbl,
        .probe =        cs461x_pci_probe,
        .remove =       __devexit_p(cs461x_pci_remove),
};

static int __init cs461x_init(void)
{
        return pci_register_driver(&cs461x_pci_driver);
}

static void __exit cs461x_exit(void)
{
        pci_unregister_driver(&cs461x_pci_driver);
}

module_init(cs461x_init);
module_exit(cs461x_exit);

