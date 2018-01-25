/*
 * ipmi_si_pci.c
 *
 * Handling for IPMI devices on the PCI bus.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include "ipmi_si.h"

#define PFX "ipmi_pci: "

static bool pci_registered;

static bool si_trypci = true;

module_param_named(trypci, si_trypci, bool, 0);
MODULE_PARM_DESC(trypci, "Setting this to zero will disable the"
		 " default scan of the interfaces identified via pci");

#define PCI_ERMC_CLASSCODE		0x0C0700
#define PCI_ERMC_CLASSCODE_MASK		0xffffff00
#define PCI_ERMC_CLASSCODE_TYPE_MASK	0xff
#define PCI_ERMC_CLASSCODE_TYPE_SMIC	0x00
#define PCI_ERMC_CLASSCODE_TYPE_KCS	0x01
#define PCI_ERMC_CLASSCODE_TYPE_BT	0x02

#define PCI_HP_VENDOR_ID    0x103C
#define PCI_MMC_DEVICE_ID   0x121A
#define PCI_MMC_ADDR_CW     0x10

static void ipmi_pci_cleanup(struct si_sm_io *io)
{
	struct pci_dev *pdev = io->addr_source_data;

	pci_disable_device(pdev);
}

static int ipmi_pci_probe_regspacing(struct si_sm_io *io)
{
	if (io->si_type == SI_KCS) {
		unsigned char	status;
		int		regspacing;

		io->regsize = DEFAULT_REGSIZE;
		io->regshift = 0;

		/* detect 1, 4, 16byte spacing */
		for (regspacing = DEFAULT_REGSPACING; regspacing <= 16;) {
			io->regspacing = regspacing;
			if (io->io_setup(io)) {
				dev_err(io->dev,
					"Could not setup I/O space\n");
				return DEFAULT_REGSPACING;
			}
			/* write invalid cmd */
			io->outputb(io, 1, 0x10);
			/* read status back */
			status = io->inputb(io, 1);
			io->io_cleanup(io);
			if (status)
				return regspacing;
			regspacing *= 4;
		}
	}
	return DEFAULT_REGSPACING;
}

static int ipmi_pci_probe(struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	int rv;
	int class_type = pdev->class & PCI_ERMC_CLASSCODE_TYPE_MASK;
	struct si_sm_io io;

	memset(&io, 0, sizeof(io));
	io.addr_source = SI_PCI;
	dev_info(&pdev->dev, "probing via PCI");

	switch (class_type) {
	case PCI_ERMC_CLASSCODE_TYPE_SMIC:
		io.si_type = SI_SMIC;
		break;

	case PCI_ERMC_CLASSCODE_TYPE_KCS:
		io.si_type = SI_KCS;
		break;

	case PCI_ERMC_CLASSCODE_TYPE_BT:
		io.si_type = SI_BT;
		break;

	default:
		dev_info(&pdev->dev, "Unknown IPMI type: %d\n", class_type);
		return -ENOMEM;
	}

	rv = pci_enable_device(pdev);
	if (rv) {
		dev_err(&pdev->dev, "couldn't enable PCI device\n");
		return rv;
	}

	io.addr_source_cleanup = ipmi_pci_cleanup;
	io.addr_source_data = pdev;

	if (pci_resource_flags(pdev, 0) & IORESOURCE_IO)
		io.addr_type = IPMI_IO_ADDR_SPACE;
	else
		io.addr_type = IPMI_MEM_ADDR_SPACE;
	io.addr_data = pci_resource_start(pdev, 0);

	io.regspacing = ipmi_pci_probe_regspacing(&io);
	io.regsize = DEFAULT_REGSIZE;
	io.regshift = 0;

	io.irq = pdev->irq;
	if (io.irq)
		io.irq_setup = ipmi_std_irq_setup;

	io.dev = &pdev->dev;

	dev_info(&pdev->dev, "%pR regsize %d spacing %d irq %d\n",
		&pdev->resource[0], io.regsize, io.regspacing, io.irq);

	rv = ipmi_si_add_smi(&io);
	if (rv)
		pci_disable_device(pdev);

	return rv;
}

static void ipmi_pci_remove(struct pci_dev *pdev)
{
	ipmi_si_remove_by_dev(&pdev->dev);
}

static const struct pci_device_id ipmi_pci_devices[] = {
	{ PCI_DEVICE(PCI_HP_VENDOR_ID, PCI_MMC_DEVICE_ID) },
	{ PCI_DEVICE_CLASS(PCI_ERMC_CLASSCODE, PCI_ERMC_CLASSCODE_MASK) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ipmi_pci_devices);

static struct pci_driver ipmi_pci_driver = {
	.name =         DEVICE_NAME,
	.id_table =     ipmi_pci_devices,
	.probe =        ipmi_pci_probe,
	.remove =       ipmi_pci_remove,
};

void ipmi_si_pci_init(void)
{
	if (si_trypci) {
		int rv = pci_register_driver(&ipmi_pci_driver);
		if (rv)
			pr_err(PFX "Unable to register PCI driver: %d\n", rv);
		else
			pci_registered = true;
	}
}

void ipmi_si_pci_shutdown(void)
{
	if (pci_registered)
		pci_unregister_driver(&ipmi_pci_driver);
}
