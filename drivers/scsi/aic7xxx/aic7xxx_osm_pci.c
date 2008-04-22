/*
 * Linux driver attachment glue for PCI based controllers.
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
 * $Id: //depot/aic7xxx/linux/drivers/scsi/aic7xxx/aic7xxx_osm_pci.c#47 $
 */

#include "aic7xxx_osm.h"
#include "aic7xxx_pci.h"

/* Define the macro locally since it's different for different class of chips.
*/
#define ID(x)	ID_C(x, PCI_CLASS_STORAGE_SCSI)

static struct pci_device_id ahc_linux_pci_id_table[] = {
	/* aic7850 based controllers */
	ID(ID_AHA_2902_04_10_15_20C_30C),
	/* aic7860 based controllers */
	ID(ID_AHA_2930CU),
	ID(ID_AHA_1480A & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_2940AU_0 & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_2940AU_CN & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_2930C_VAR & ID_DEV_VENDOR_MASK),
	/* aic7870 based controllers */
	ID(ID_AHA_2940),
	ID(ID_AHA_3940),
	ID(ID_AHA_398X),
	ID(ID_AHA_2944),
	ID(ID_AHA_3944),
	ID(ID_AHA_4944),
	/* aic7880 based controllers */
	ID(ID_AHA_2940U & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_3940U & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_2944U & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_3944U & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_398XU & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_4944U & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_2930U & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_2940U_PRO & ID_DEV_VENDOR_MASK),
	ID(ID_AHA_2940U_CN & ID_DEV_VENDOR_MASK),
	/* aic7890 based controllers */
	ID(ID_AHA_2930U2),
	ID(ID_AHA_2940U2B),
	ID(ID_AHA_2940U2_OEM),
	ID(ID_AHA_2940U2),
	ID(ID_AHA_2950U2B),
	ID16(ID_AIC7890_ARO & ID_AIC7895_ARO_MASK),
	ID(ID_AAA_131U2),
	/* aic7890 based controllers */
	ID(ID_AHA_29160),
	ID(ID_AHA_29160_CPQ),
	ID(ID_AHA_29160N),
	ID(ID_AHA_29160C),
	ID(ID_AHA_29160B),
	ID(ID_AHA_19160B),
	ID(ID_AIC7892_ARO),
	/* aic7892 based controllers */
	ID(ID_AHA_2940U_DUAL),
	ID(ID_AHA_3940AU),
	ID(ID_AHA_3944AU),
	ID(ID_AIC7895_ARO),
	ID(ID_AHA_3950U2B_0),
	ID(ID_AHA_3950U2B_1),
	ID(ID_AHA_3950U2D_0),
	ID(ID_AHA_3950U2D_1),
	ID(ID_AIC7896_ARO),
	/* aic7899 based controllers */
	ID(ID_AHA_3960D),
	ID(ID_AHA_3960D_CPQ),
	ID(ID_AIC7899_ARO),
	/* Generic chip probes for devices we don't know exactly. */
	ID(ID_AIC7850 & ID_DEV_VENDOR_MASK),
	ID(ID_AIC7855 & ID_DEV_VENDOR_MASK),
	ID(ID_AIC7859 & ID_DEV_VENDOR_MASK),
	ID(ID_AIC7860 & ID_DEV_VENDOR_MASK),
	ID(ID_AIC7870 & ID_DEV_VENDOR_MASK),
	ID(ID_AIC7880 & ID_DEV_VENDOR_MASK),
 	ID16(ID_AIC7890 & ID_9005_GENERIC_MASK),
 	ID16(ID_AIC7892 & ID_9005_GENERIC_MASK),
	ID(ID_AIC7895 & ID_DEV_VENDOR_MASK),
	ID16(ID_AIC7896 & ID_9005_GENERIC_MASK),
	ID16(ID_AIC7899 & ID_9005_GENERIC_MASK),
	ID(ID_AIC7810 & ID_DEV_VENDOR_MASK),
	ID(ID_AIC7815 & ID_DEV_VENDOR_MASK),
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, ahc_linux_pci_id_table);

#ifdef CONFIG_PM
static int
ahc_linux_pci_dev_suspend(struct pci_dev *pdev, pm_message_t mesg)
{
	struct ahc_softc *ahc = pci_get_drvdata(pdev);
	int rc;

	if ((rc = ahc_suspend(ahc)))
		return rc;

	pci_save_state(pdev);
	pci_disable_device(pdev);

	if (mesg.event & PM_EVENT_SLEEP)
		pci_set_power_state(pdev, PCI_D3hot);

	return rc;
}

static int
ahc_linux_pci_dev_resume(struct pci_dev *pdev)
{
	struct ahc_softc *ahc = pci_get_drvdata(pdev);
	int rc;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if ((rc = pci_enable_device(pdev))) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "failed to enable device after resume (%d)\n", rc);
		return rc;
	}

	pci_set_master(pdev);

	ahc_pci_resume(ahc);

	return (ahc_resume(ahc));
}
#endif

static void
ahc_linux_pci_dev_remove(struct pci_dev *pdev)
{
	struct ahc_softc *ahc = pci_get_drvdata(pdev);
	u_long s;

	if (ahc->platform_data && ahc->platform_data->host)
			scsi_remove_host(ahc->platform_data->host);

	ahc_lock(ahc, &s);
	ahc_intr_enable(ahc, FALSE);
	ahc_unlock(ahc, &s);
	ahc_free(ahc);
}

static void
ahc_linux_pci_inherit_flags(struct ahc_softc *ahc)
{
	struct pci_dev *pdev = ahc->dev_softc, *master_pdev;
	unsigned int master_devfn = PCI_DEVFN(PCI_SLOT(pdev->devfn), 0);

	master_pdev = pci_get_slot(pdev->bus, master_devfn);
	if (master_pdev) {
		struct ahc_softc *master = pci_get_drvdata(master_pdev);
		if (master) {
			ahc->flags &= ~AHC_BIOS_ENABLED; 
			ahc->flags |= master->flags & AHC_BIOS_ENABLED;

			ahc->flags &= ~AHC_PRIMARY_CHANNEL; 
			ahc->flags |= master->flags & AHC_PRIMARY_CHANNEL;
		} else
			printk(KERN_ERR "aic7xxx: no multichannel peer found!\n");
		pci_dev_put(master_pdev);
	} 
}

static int
ahc_linux_pci_dev_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	char		 buf[80];
	const uint64_t	 mask_39bit = 0x7FFFFFFFFFULL;
	struct		 ahc_softc *ahc;
	ahc_dev_softc_t	 pci;
	struct		 ahc_pci_identity *entry;
	char		*name;
	int		 error;
	struct device	*dev = &pdev->dev;

	pci = pdev;
	entry = ahc_find_pci_device(pci);
	if (entry == NULL)
		return (-ENODEV);

	/*
	 * Allocate a softc for this card and
	 * set it up for attachment by our
	 * common detect routine.
	 */
	sprintf(buf, "ahc_pci:%d:%d:%d",
		ahc_get_pci_bus(pci),
		ahc_get_pci_slot(pci),
		ahc_get_pci_function(pci));
	name = malloc(strlen(buf) + 1, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		return (-ENOMEM);
	strcpy(name, buf);
	ahc = ahc_alloc(NULL, name);
	if (ahc == NULL)
		return (-ENOMEM);
	if (pci_enable_device(pdev)) {
		ahc_free(ahc);
		return (-ENODEV);
	}
	pci_set_master(pdev);

	if (sizeof(dma_addr_t) > 4
	    && ahc->features & AHC_LARGE_SCBS
	    && dma_set_mask(dev, mask_39bit) == 0
	    && dma_get_required_mask(dev) > DMA_32BIT_MASK) {
		ahc->flags |= AHC_39BIT_ADDRESSING;
	} else {
		if (dma_set_mask(dev, DMA_32BIT_MASK)) {
			ahc_free(ahc);
			printk(KERN_WARNING "aic7xxx: No suitable DMA available.\n");
                	return (-ENODEV);
		}
	}
	ahc->dev_softc = pci;
	error = ahc_pci_config(ahc, entry);
	if (error != 0) {
		ahc_free(ahc);
		return (-error);
	}

	/*
	 * Second Function PCI devices need to inherit some
	 * settings from function 0.
	 */
	if ((ahc->features & AHC_MULTI_FUNC) && PCI_FUNC(pdev->devfn) != 0)
		ahc_linux_pci_inherit_flags(ahc);

	pci_set_drvdata(pdev, ahc);
	ahc_linux_register_host(ahc, &aic7xxx_driver_template);
	return (0);
}

static struct pci_driver aic7xxx_pci_driver = {
	.name		= "aic7xxx",
	.probe		= ahc_linux_pci_dev_probe,
#ifdef CONFIG_PM
	.suspend	= ahc_linux_pci_dev_suspend,
	.resume		= ahc_linux_pci_dev_resume,
#endif
	.remove		= ahc_linux_pci_dev_remove,
	.id_table	= ahc_linux_pci_id_table
};

int
ahc_linux_pci_init(void)
{
	return pci_register_driver(&aic7xxx_pci_driver);
}

void
ahc_linux_pci_exit(void)
{
	pci_unregister_driver(&aic7xxx_pci_driver);
}

static int
ahc_linux_pci_reserve_io_region(struct ahc_softc *ahc, u_long *base)
{
	if (aic7xxx_allow_memio == 0)
		return (ENOMEM);

	*base = pci_resource_start(ahc->dev_softc, 0);
	if (*base == 0)
		return (ENOMEM);
	if (request_region(*base, 256, "aic7xxx") == 0)
		return (ENOMEM);
	return (0);
}

static int
ahc_linux_pci_reserve_mem_region(struct ahc_softc *ahc,
				 u_long *bus_addr,
				 uint8_t __iomem **maddr)
{
	u_long	start;
	int	error;

	error = 0;
	start = pci_resource_start(ahc->dev_softc, 1);
	if (start != 0) {
		*bus_addr = start;
		if (request_mem_region(start, 0x1000, "aic7xxx") == 0)
			error = ENOMEM;
		if (error == 0) {
			*maddr = ioremap_nocache(start, 256);
			if (*maddr == NULL) {
				error = ENOMEM;
				release_mem_region(start, 0x1000);
			}
		}
	} else
		error = ENOMEM;
	return (error);
}

int
ahc_pci_map_registers(struct ahc_softc *ahc)
{
	uint32_t command;
	u_long	 base;
	uint8_t	__iomem *maddr;
	int	 error;

	/*
	 * If its allowed, we prefer memory mapped access.
	 */
	command = ahc_pci_read_config(ahc->dev_softc, PCIR_COMMAND, 4);
	command &= ~(PCIM_CMD_PORTEN|PCIM_CMD_MEMEN);
	base = 0;
	maddr = NULL;
	error = ahc_linux_pci_reserve_mem_region(ahc, &base, &maddr);
	if (error == 0) {
		ahc->platform_data->mem_busaddr = base;
		ahc->tag = BUS_SPACE_MEMIO;
		ahc->bsh.maddr = maddr;
		ahc_pci_write_config(ahc->dev_softc, PCIR_COMMAND,
				     command | PCIM_CMD_MEMEN, 4);

		/*
		 * Do a quick test to see if memory mapped
		 * I/O is functioning correctly.
		 */
		if (ahc_pci_test_register_access(ahc) != 0) {

			printf("aic7xxx: PCI Device %d:%d:%d "
			       "failed memory mapped test.  Using PIO.\n",
			       ahc_get_pci_bus(ahc->dev_softc),
			       ahc_get_pci_slot(ahc->dev_softc),
			       ahc_get_pci_function(ahc->dev_softc));
			iounmap(maddr);
			release_mem_region(ahc->platform_data->mem_busaddr,
					   0x1000);
			ahc->bsh.maddr = NULL;
			maddr = NULL;
		} else
			command |= PCIM_CMD_MEMEN;
	} else {
		printf("aic7xxx: PCI%d:%d:%d MEM region 0x%lx "
		       "unavailable. Cannot memory map device.\n",
		       ahc_get_pci_bus(ahc->dev_softc),
		       ahc_get_pci_slot(ahc->dev_softc),
		       ahc_get_pci_function(ahc->dev_softc),
		       base);
	}

	/*
	 * We always prefer memory mapped access.
	 */
	if (maddr == NULL) {

		error = ahc_linux_pci_reserve_io_region(ahc, &base);
		if (error == 0) {
			ahc->tag = BUS_SPACE_PIO;
			ahc->bsh.ioport = base;
			command |= PCIM_CMD_PORTEN;
		} else {
			printf("aic7xxx: PCI%d:%d:%d IO region 0x%lx[0..255] "
			       "unavailable. Cannot map device.\n",
			       ahc_get_pci_bus(ahc->dev_softc),
			       ahc_get_pci_slot(ahc->dev_softc),
			       ahc_get_pci_function(ahc->dev_softc),
			       base);
		}
	}
	ahc_pci_write_config(ahc->dev_softc, PCIR_COMMAND, command, 4);
	return (error);
}

int
ahc_pci_map_int(struct ahc_softc *ahc)
{
	int error;

	error = request_irq(ahc->dev_softc->irq, ahc_linux_isr,
			    IRQF_SHARED, "aic7xxx", ahc);
	if (error == 0)
		ahc->platform_data->irq = ahc->dev_softc->irq;
	
	return (-error);
}

