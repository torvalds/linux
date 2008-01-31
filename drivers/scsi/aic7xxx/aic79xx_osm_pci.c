/*
 * Linux driver attachment glue for PCI based U320 controllers.
 *
 * Copyright (c) 2000-2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic79xx_osm_pci.c#25 $
 */

#include "aic79xx_osm.h"
#include "aic79xx_inline.h"
#include "aic79xx_pci.h"

/* Define the macro locally since it's different for different class of chips.
 */
#define ID(x)            \
	ID2C(x),         \
	ID2C(IDIROC(x))

static struct pci_device_id ahd_linux_pci_id_table[] = {
	/* aic7901 based controllers */
	ID(ID_AHA_29320A),
	ID(ID_AHA_29320ALP),
	ID(ID_AHA_29320LPE),
	/* aic7902 based controllers */
	ID(ID_AHA_29320),
	ID(ID_AHA_29320B),
	ID(ID_AHA_29320LP),
	ID(ID_AHA_39320),
	ID(ID_AHA_39320_B),
	ID(ID_AHA_39320A),
	ID(ID_AHA_39320D),
	ID(ID_AHA_39320D_HP),
	ID(ID_AHA_39320D_B),
	ID(ID_AHA_39320D_B_HP),
	/* Generic chip probes for devices we don't know exactly. */
	ID16(ID_AIC7901 & ID_9005_GENERIC_MASK),
	ID(ID_AIC7901A & ID_DEV_VENDOR_MASK),
	ID16(ID_AIC7902 & ID_9005_GENERIC_MASK),
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, ahd_linux_pci_id_table);

#ifdef CONFIG_PM
static int
ahd_linux_pci_dev_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct ahd_softc *ahd = pci_get_drvdata(pdev);
	int rc;

	if ((rc = ahd_suspend(ahd)))
		return rc;

	ahd_pci_suspend(ahd);

	pci_save_state(pdev);
	pci_disable_device(pdev);

	if (mesg.event == PM_EVENT_SUSPEND)
		pci_set_power_state(pdev, PCI_D3hot);

	return rc;
}

static int
ahd_linux_pci_dev_resume(struct pci_dev *pdev)
{
	struct ahd_softc *ahd = pci_get_drvdata(pdev);
	int rc;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if ((rc = pci_enable_device(pdev))) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "failed to enable device after resume (%d)\n", rc);
		return rc;
	}

	pci_set_master(pdev);

	ahd_pci_resume(ahd);

	ahd_resume(ahd);

	return rc;
}
#endif

static void
ahd_linux_pci_dev_remove(struct pci_dev *pdev)
{
	struct ahd_softc *ahd = pci_get_drvdata(pdev);
	u_long s;

	if (ahd->platform_data && ahd->platform_data->host)
			scsi_remove_host(ahd->platform_data->host);

	ahd_lock(ahd, &s);
	ahd_intr_enable(ahd, FALSE);
	ahd_unlock(ahd, &s);
	ahd_free(ahd);
}

static void
ahd_linux_pci_inherit_flags(struct ahd_softc *ahd)
{
	struct pci_dev *pdev = ahd->dev_softc, *master_pdev;
	unsigned int master_devfn = PCI_DEVFN(PCI_SLOT(pdev->devfn), 0);

	master_pdev = pci_get_slot(pdev->bus, master_devfn);
	if (master_pdev) {
		struct ahd_softc *master = pci_get_drvdata(master_pdev);
		if (master) {
			ahd->flags &= ~AHD_BIOS_ENABLED;
			ahd->flags |= master->flags & AHD_BIOS_ENABLED;
		} else
			printk(KERN_ERR "aic79xx: no multichannel peer found!\n");
		pci_dev_put(master_pdev);
	}
}

static int
ahd_linux_pci_dev_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	char		 buf[80];
	struct		 ahd_softc *ahd;
	ahd_dev_softc_t	 pci;
	struct		 ahd_pci_identity *entry;
	char		*name;
	int		 error;
	struct device	*dev = &pdev->dev;

	pci = pdev;
	entry = ahd_find_pci_device(pci);
	if (entry == NULL)
		return (-ENODEV);

	/*
	 * Allocate a softc for this card and
	 * set it up for attachment by our
	 * common detect routine.
	 */
	sprintf(buf, "ahd_pci:%d:%d:%d",
		ahd_get_pci_bus(pci),
		ahd_get_pci_slot(pci),
		ahd_get_pci_function(pci));
	name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		return (-ENOMEM);
	strcpy(name, buf);
	ahd = ahd_alloc(NULL, name);
	if (ahd == NULL)
		return (-ENOMEM);
	if (pci_enable_device(pdev)) {
		ahd_free(ahd);
		return (-ENODEV);
	}
	pci_set_master(pdev);

	if (sizeof(dma_addr_t) > 4) {
		const u64 required_mask = dma_get_required_mask(dev);

		if (required_mask > DMA_39BIT_MASK &&
		    dma_set_mask(dev, DMA_64BIT_MASK) == 0)
			ahd->flags |= AHD_64BIT_ADDRESSING;
		else if (required_mask > DMA_32BIT_MASK &&
			 dma_set_mask(dev, DMA_39BIT_MASK) == 0)
			ahd->flags |= AHD_39BIT_ADDRESSING;
		else
			dma_set_mask(dev, DMA_32BIT_MASK);
	} else {
		dma_set_mask(dev, DMA_32BIT_MASK);
	}
	ahd->dev_softc = pci;
	error = ahd_pci_config(ahd, entry);
	if (error != 0) {
		ahd_free(ahd);
		return (-error);
	}

	/*
	 * Second Function PCI devices need to inherit some
	 * * settings from function 0.
	 */
	if ((ahd->features & AHD_MULTI_FUNC) && PCI_FUNC(pdev->devfn) != 0)
		ahd_linux_pci_inherit_flags(ahd);

	pci_set_drvdata(pdev, ahd);

	ahd_linux_register_host(ahd, &aic79xx_driver_template);
	return (0);
}

static struct pci_driver aic79xx_pci_driver = {
	.name		= "aic79xx",
	.probe		= ahd_linux_pci_dev_probe,
#ifdef CONFIG_PM
	.suspend	= ahd_linux_pci_dev_suspend,
	.resume		= ahd_linux_pci_dev_resume,
#endif
	.remove		= ahd_linux_pci_dev_remove,
	.id_table	= ahd_linux_pci_id_table
};

int
ahd_linux_pci_init(void)
{
	return pci_register_driver(&aic79xx_pci_driver);
}

void
ahd_linux_pci_exit(void)
{
	pci_unregister_driver(&aic79xx_pci_driver);
}

static int
ahd_linux_pci_reserve_io_regions(struct ahd_softc *ahd, u_long *base,
				 u_long *base2)
{
	*base = pci_resource_start(ahd->dev_softc, 0);
	/*
	 * This is really the 3rd bar and should be at index 2,
	 * but the Linux PCI code doesn't know how to "count" 64bit
	 * bars.
	 */
	*base2 = pci_resource_start(ahd->dev_softc, 3);
	if (*base == 0 || *base2 == 0)
		return (ENOMEM);
	if (!request_region(*base, 256, "aic79xx"))
		return (ENOMEM);
	if (!request_region(*base2, 256, "aic79xx")) {
		release_region(*base, 256);
		return (ENOMEM);
	}
	return (0);
}

static int
ahd_linux_pci_reserve_mem_region(struct ahd_softc *ahd,
				 u_long *bus_addr,
				 uint8_t __iomem **maddr)
{
	u_long	start;
	u_long	base_page;
	u_long	base_offset;
	int	error = 0;

	if (aic79xx_allow_memio == 0)
		return (ENOMEM);

	if ((ahd->bugs & AHD_PCIX_MMAPIO_BUG) != 0)
		return (ENOMEM);

	start = pci_resource_start(ahd->dev_softc, 1);
	base_page = start & PAGE_MASK;
	base_offset = start - base_page;
	if (start != 0) {
		*bus_addr = start;
		if (!request_mem_region(start, 0x1000, "aic79xx"))
			error = ENOMEM;
		if (!error) {
			*maddr = ioremap_nocache(base_page, base_offset + 512);
			if (*maddr == NULL) {
				error = ENOMEM;
				release_mem_region(start, 0x1000);
			} else
				*maddr += base_offset;
		}
	} else
		error = ENOMEM;
	return (error);
}

int
ahd_pci_map_registers(struct ahd_softc *ahd)
{
	uint32_t command;
	u_long	 base;
	uint8_t	__iomem *maddr;
	int	 error;

	/*
	 * If its allowed, we prefer memory mapped access.
	 */
	command = ahd_pci_read_config(ahd->dev_softc, PCIR_COMMAND, 4);
	command &= ~(PCIM_CMD_PORTEN|PCIM_CMD_MEMEN);
	base = 0;
	maddr = NULL;
	error = ahd_linux_pci_reserve_mem_region(ahd, &base, &maddr);
	if (error == 0) {
		ahd->platform_data->mem_busaddr = base;
		ahd->tags[0] = BUS_SPACE_MEMIO;
		ahd->bshs[0].maddr = maddr;
		ahd->tags[1] = BUS_SPACE_MEMIO;
		ahd->bshs[1].maddr = maddr + 0x100;
		ahd_pci_write_config(ahd->dev_softc, PCIR_COMMAND,
				     command | PCIM_CMD_MEMEN, 4);

		if (ahd_pci_test_register_access(ahd) != 0) {

			printf("aic79xx: PCI Device %d:%d:%d "
			       "failed memory mapped test.  Using PIO.\n",
			       ahd_get_pci_bus(ahd->dev_softc),
			       ahd_get_pci_slot(ahd->dev_softc),
			       ahd_get_pci_function(ahd->dev_softc));
			iounmap(maddr);
			release_mem_region(ahd->platform_data->mem_busaddr,
					   0x1000);
			ahd->bshs[0].maddr = NULL;
			maddr = NULL;
		} else
			command |= PCIM_CMD_MEMEN;
	} else if (bootverbose) {
		printf("aic79xx: PCI%d:%d:%d MEM region 0x%lx "
		       "unavailable. Cannot memory map device.\n",
		       ahd_get_pci_bus(ahd->dev_softc),
		       ahd_get_pci_slot(ahd->dev_softc),
		       ahd_get_pci_function(ahd->dev_softc),
		       base);
	}

	if (maddr == NULL) {
		u_long	 base2;

		error = ahd_linux_pci_reserve_io_regions(ahd, &base, &base2);
		if (error == 0) {
			ahd->tags[0] = BUS_SPACE_PIO;
			ahd->tags[1] = BUS_SPACE_PIO;
			ahd->bshs[0].ioport = base;
			ahd->bshs[1].ioport = base2;
			command |= PCIM_CMD_PORTEN;
		} else {
			printf("aic79xx: PCI%d:%d:%d IO regions 0x%lx and 0x%lx"
			       "unavailable. Cannot map device.\n",
			       ahd_get_pci_bus(ahd->dev_softc),
			       ahd_get_pci_slot(ahd->dev_softc),
			       ahd_get_pci_function(ahd->dev_softc),
			       base, base2);
		}
	}
	ahd_pci_write_config(ahd->dev_softc, PCIR_COMMAND, command, 4);
	return (error);
}

int
ahd_pci_map_int(struct ahd_softc *ahd)
{
	int error;

	error = request_irq(ahd->dev_softc->irq, ahd_linux_isr,
			    IRQF_SHARED, "aic79xx", ahd);
	if (!error)
		ahd->platform_data->irq = ahd->dev_softc->irq;
	
	return (-error);
}

void
ahd_power_state_change(struct ahd_softc *ahd, ahd_power_state new_state)
{
	pci_set_power_state(ahd->dev_softc, new_state);
}
