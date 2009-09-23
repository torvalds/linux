/*
 *  Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data-com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* Header file for AMCC  s 5933 */

#ifndef _AMCC_S5933_H_
#define _AMCC_S5933_H_

#include "../../comedidev.h"

#include "../comedi_pci.h"

#ifdef PCI_SUPPORT_VER1
#error     No support for 2.1.55 and older
#endif

/* written on base0 */
#define FIFO_ADVANCE_ON_BYTE_2	0x20000000

/* added for step 6 dma written on base2 */
#define AMWEN_ENABLE		0x02

#define A2P_FIFO_WRITE_ENABLE	0x01

/* for transfer count enable bit */
#define AGCSTS_TC_ENABLE	0x10000000

/*
 * ADDON RELATED ADDITIONS
 */
/* Constant */
#define APCI3120_ENABLE_TRANSFER_ADD_ON_LOW		0x00
#define APCI3120_ENABLE_TRANSFER_ADD_ON_HIGH		0x1200
#define APCI3120_A2P_FIFO_MANAGEMENT			0x04000400L
#define APCI3120_AMWEN_ENABLE				0x02
#define APCI3120_A2P_FIFO_WRITE_ENABLE			0x01
#define APCI3120_FIFO_ADVANCE_ON_BYTE_2			0x20000000L
#define APCI3120_ENABLE_WRITE_TC_INT			0x00004000L
#define APCI3120_CLEAR_WRITE_TC_INT			0x00040000L
#define APCI3120_DISABLE_AMWEN_AND_A2P_FIFO_WRITE	0x0
#define APCI3120_DISABLE_BUS_MASTER_ADD_ON		0x0
#define APCI3120_DISABLE_BUS_MASTER_PCI			0x0

/* ADD_ON ::: this needed since apci supports 16 bit interface to add on */
#define APCI3120_ADD_ON_AGCSTS_LOW	0x3C
#define APCI3120_ADD_ON_AGCSTS_HIGH	(APCI3120_ADD_ON_AGCSTS_LOW + 2)
#define APCI3120_ADD_ON_MWAR_LOW	0x24
#define APCI3120_ADD_ON_MWAR_HIGH	(APCI3120_ADD_ON_MWAR_LOW + 2)
#define APCI3120_ADD_ON_MWTC_LOW	0x058
#define APCI3120_ADD_ON_MWTC_HIGH	(APCI3120_ADD_ON_MWTC_LOW + 2)

/* AMCC */
#define APCI3120_AMCC_OP_MCSR		0x3C
#define APCI3120_AMCC_OP_REG_INTCSR	0x38

/*
 * AMCC Operation Register Offsets - PCI
 */
#define AMCC_OP_REG_OMB1		0x00
#define AMCC_OP_REG_OMB2		0x04
#define AMCC_OP_REG_OMB3		0x08
#define AMCC_OP_REG_OMB4		0x0c
#define AMCC_OP_REG_IMB1		0x10
#define AMCC_OP_REG_IMB2		0x14
#define AMCC_OP_REG_IMB3		0x18
#define AMCC_OP_REG_IMB4		0x1c
#define AMCC_OP_REG_FIFO		0x20
#define AMCC_OP_REG_MWAR		0x24
#define AMCC_OP_REG_MWTC		0x28
#define AMCC_OP_REG_MRAR		0x2c
#define AMCC_OP_REG_MRTC		0x30
#define AMCC_OP_REG_MBEF		0x34
#define AMCC_OP_REG_INTCSR		0x38
/* int source */
#define  AMCC_OP_REG_INTCSR_SRC		(AMCC_OP_REG_INTCSR + 2)
/* FIFO ctrl */
#define  AMCC_OP_REG_INTCSR_FEC		(AMCC_OP_REG_INTCSR + 3)
#define AMCC_OP_REG_MCSR		0x3c
/* Data in byte 2 */
#define  AMCC_OP_REG_MCSR_NVDATA	(AMCC_OP_REG_MCSR + 2)
/* Command in byte 3 */
#define  AMCC_OP_REG_MCSR_NVCMD		(AMCC_OP_REG_MCSR + 3)

#define AMCC_FIFO_DEPTH_DWORD	8
#define AMCC_FIFO_DEPTH_BYTES	(8 * sizeof(u32))

/*
 * AMCC Operation Registers Size - PCI
 */
#define AMCC_OP_REG_SIZE	 64	/* in bytes */

/*
 * AMCC Operation Register Offsets - Add-on
 */
#define AMCC_OP_REG_AIMB1	0x00
#define AMCC_OP_REG_AIMB2	0x04
#define AMCC_OP_REG_AIMB3	0x08
#define AMCC_OP_REG_AIMB4	0x0c
#define AMCC_OP_REG_AOMB1	0x10
#define AMCC_OP_REG_AOMB2	0x14
#define AMCC_OP_REG_AOMB3	0x18
#define AMCC_OP_REG_AOMB4	0x1c
#define AMCC_OP_REG_AFIFO	0x20
#define AMCC_OP_REG_AMWAR	0x24
#define AMCC_OP_REG_APTA	0x28
#define AMCC_OP_REG_APTD	0x2c
#define AMCC_OP_REG_AMRAR	0x30
#define AMCC_OP_REG_AMBEF	0x34
#define AMCC_OP_REG_AINT	0x38
#define AMCC_OP_REG_AGCSTS	0x3c
#define AMCC_OP_REG_AMWTC	0x58
#define AMCC_OP_REG_AMRTC	0x5c

/*
 * AMCC - Add-on General Control/Status Register
 */
#define AGCSTS_CONTROL_MASK	0xfffff000
#define  AGCSTS_NV_ACC_MASK	0xe0000000
#define  AGCSTS_RESET_MASK	0x0e000000
#define  AGCSTS_NV_DA_MASK	0x00ff0000
#define  AGCSTS_BIST_MASK	0x0000f000
#define AGCSTS_STATUS_MASK	0x000000ff
#define  AGCSTS_TCZERO_MASK	0x000000c0
#define  AGCSTS_FIFO_ST_MASK	0x0000003f

#define AGCSTS_RESET_MBFLAGS	0x08000000
#define AGCSTS_RESET_P2A_FIFO	0x04000000
#define AGCSTS_RESET_A2P_FIFO	0x02000000
#define AGCSTS_RESET_FIFOS	(AGCSTS_RESET_A2P_FIFO | AGCSTS_RESET_P2A_FIFO)

#define AGCSTS_A2P_TCOUNT	0x00000080
#define AGCSTS_P2A_TCOUNT	0x00000040

#define AGCSTS_FS_P2A_EMPTY	0x00000020
#define AGCSTS_FS_P2A_HALF	0x00000010
#define AGCSTS_FS_P2A_FULL	0x00000008

#define AGCSTS_FS_A2P_EMPTY	0x00000004
#define AGCSTS_FS_A2P_HALF	0x00000002
#define AGCSTS_FS_A2P_FULL	0x00000001

/*
 * AMCC - Add-on Interrupt Control/Status Register
 */
#define AINT_INT_MASK		0x00ff0000
#define AINT_SEL_MASK		0x0000ffff
#define  AINT_IS_ENSEL_MASK	0x00001f1f

#define AINT_INT_ASSERTED	0x00800000
#define AINT_BM_ERROR		0x00200000
#define AINT_BIST_INT		0x00100000

#define AINT_RT_COMPLETE	0x00080000
#define AINT_WT_COMPLETE	0x00040000

#define AINT_OUT_MB_INT		0x00020000
#define AINT_IN_MB_INT		0x00010000

#define AINT_READ_COMPL		0x00008000
#define AINT_WRITE_COMPL	0x00004000

#define AINT_OMB_ENABLE 	0x00001000
#define AINT_OMB_SELECT 	0x00000c00
#define AINT_OMB_BYTE		0x00000300

#define AINT_IMB_ENABLE 	0x00000010
#define AINT_IMB_SELECT 	0x0000000c
#define AINT_IMB_BYTE		0x00000003

/* Enable Bus Mastering */
#define EN_A2P_TRANSFERS	0x00000400
/* FIFO Flag Reset */
#define RESET_A2P_FLAGS		0x04000000L
/* FIFO Relative Priority */
#define A2P_HI_PRIORITY		0x00000100L
/* Identify Interrupt Sources */
#define ANY_S593X_INT		0x00800000L
#define READ_TC_INT		0x00080000L
#define WRITE_TC_INT		0x00040000L
#define IN_MB_INT		0x00020000L
#define MASTER_ABORT_INT	0x00100000L
#define TARGET_ABORT_INT	0x00200000L
#define BUS_MASTER_INT		0x00200000L

/****************************************************************************/

struct pcilst_struct {
	struct pcilst_struct *next;
	int used;
	struct pci_dev *pcidev;
	unsigned short vendor;
	unsigned short device;
	unsigned char pci_bus;
	unsigned char pci_slot;
	unsigned char pci_func;
	resource_size_t io_addr[5];
	unsigned int irq;
};

/* ptr to root list of all amcc devices */
struct pcilst_struct *amcc_devices;

static const int i_ADDIDATADeviceID[] = { 0x15B8, 0x10E8 };

/****************************************************************************/

void v_pci_card_list_init(unsigned short pci_vendor, char display);
void v_pci_card_list_cleanup(unsigned short pci_vendor);
struct pcilst_struct *ptr_find_free_pci_card_by_device(unsigned short vendor_id,
						       unsigned short
						       device_id);
int i_find_free_pci_card_by_position(unsigned short vendor_id,
				     unsigned short device_id,
				     unsigned short pci_bus,
				     unsigned short pci_slot,
				     struct pcilst_struct **card);
struct pcilst_struct *ptr_select_and_alloc_pci_card(unsigned short vendor_id,
						    unsigned short device_id,
						    unsigned short pci_bus,
						    unsigned short pci_slot,
						    int i_Master);

int pci_card_alloc(struct pcilst_struct *amcc, int master);
int i_pci_card_free(struct pcilst_struct *amcc);
void v_pci_card_list_display(void);
int i_pci_card_data(struct pcilst_struct *amcc,
		    unsigned char *pci_bus, unsigned char *pci_slot,
		    unsigned char *pci_func, resource_size_t * io_addr,
		    unsigned int *irq);

/****************************************************************************/

/* build list of amcc cards in this system */
void v_pci_card_list_init(unsigned short pci_vendor, char display)
{
	struct pci_dev *pcidev;
	struct pcilst_struct *amcc, *last;
	int i;
	int i_Count = 0;
	amcc_devices = NULL;
	last = NULL;

	for (pcidev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, NULL);
	     pcidev != NULL;
	     pcidev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pcidev)) {
		for (i_Count = 0; i_Count < 2; i_Count++) {
			pci_vendor = i_ADDIDATADeviceID[i_Count];
			if (pcidev->vendor == pci_vendor) {
				amcc = kmalloc(sizeof(*amcc), GFP_KERNEL);
				if (amcc == NULL)
					continue;

				memset(amcc, 0, sizeof(*amcc));

				amcc->pcidev = pcidev;
				if (last)
					last->next = amcc;
				else
					amcc_devices = amcc;
				last = amcc;

				amcc->vendor = pcidev->vendor;
				amcc->device = pcidev->device;
				amcc->pci_bus = pcidev->bus->number;
				amcc->pci_slot = PCI_SLOT(pcidev->devfn);
				amcc->pci_func = PCI_FUNC(pcidev->devfn);
				/* Note: resources may be invalid if PCI device
				 * not enabled, but they are corrected in
				 * pci_card_alloc. */
				for (i = 0; i < 5; i++)
					amcc->io_addr[i] =
					    pci_resource_start(pcidev, i);
				amcc->irq = pcidev->irq;

			}
		}
	}

	if (display)
		v_pci_card_list_display();
}

/****************************************************************************/
/* free up list of amcc cards in this system */
void v_pci_card_list_cleanup(unsigned short pci_vendor)
{
	struct pcilst_struct *amcc, *next;

	for (amcc = amcc_devices; amcc; amcc = next) {
		next = amcc->next;
		kfree(amcc);
	}

	amcc_devices = NULL;
}

/****************************************************************************/
/* find first unused card with this device_id */
struct pcilst_struct *ptr_find_free_pci_card_by_device(unsigned short vendor_id,
						       unsigned short device_id)
{
	struct pcilst_struct *amcc, *next;

	for (amcc = amcc_devices; amcc; amcc = next) {
		next = amcc->next;
		if ((!amcc->used) && (amcc->device == device_id)
		    && (amcc->vendor == vendor_id))
			return amcc;

	}

	return NULL;
}

/****************************************************************************/
/* find card on requested position */
int i_find_free_pci_card_by_position(unsigned short vendor_id,
				     unsigned short device_id,
				     unsigned short pci_bus,
				     unsigned short pci_slot,
				     struct pcilst_struct **card)
{
	struct pcilst_struct *amcc, *next;

	*card = NULL;
	for (amcc = amcc_devices; amcc; amcc = next) {
		next = amcc->next;
		if ((amcc->vendor == vendor_id) && (amcc->device == device_id)
		    && (amcc->pci_bus == pci_bus)
		    && (amcc->pci_slot == pci_slot)) {
			if (!(amcc->used)) {
				*card = amcc;
				return 0;	/* ok, card is found */
			} else {
				printk(" - \nCard on requested position is used b:s %d:%d!\n",
					  pci_bus, pci_slot);
				return 2;	/* card exist but is used */
			}
		}
	}

	/* no card found */
	return 1;
}

/****************************************************************************/
/* mark card as used */
int pci_card_alloc(struct pcilst_struct *amcc, int master)
{
	int i;

	if (!amcc)
		return -1;

	if (amcc->used)
		return 1;
	if (comedi_pci_enable(amcc->pcidev, "addi_amcc_s5933"))
		return -1;
	/* Resources will be accurate now. */
	for (i = 0; i < 5; i++)
		amcc->io_addr[i] = pci_resource_start(amcc->pcidev, i);
	if (master)
		pci_set_master(amcc->pcidev);
	amcc->used = 1;

	return 0;
}

/****************************************************************************/
/* mark card as free */
int i_pci_card_free(struct pcilst_struct *amcc)
{
	if (!amcc)
		return -1;

	if (!amcc->used)
		return 1;
	amcc->used = 0;
	comedi_pci_disable(amcc->pcidev);
	return 0;
}

/****************************************************************************/
/* display list of found cards */
void v_pci_card_list_display(void)
{
	struct pcilst_struct *amcc, *next;

	printk(KERN_DEBUG "List of pci cards\n");
	printk(KERN_DEBUG "bus:slot:func vendor device io_amcc io_daq irq used\n");

	for (amcc = amcc_devices; amcc; amcc = next) {
		next = amcc->next;
		printk
		    ("%2d   %2d   %2d  0x%4x 0x%4x   0x%8llx 0x%8llx  %2u  %2d\n",
		     amcc->pci_bus, amcc->pci_slot, amcc->pci_func,
		     amcc->vendor, amcc->device,
		     (unsigned long long)amcc->io_addr[0],
		     (unsigned long long)amcc->io_addr[2], amcc->irq,
		     amcc->used);

	}
}

/****************************************************************************/
/* return all card information for driver */
int i_pci_card_data(struct pcilst_struct *amcc,
		    unsigned char *pci_bus, unsigned char *pci_slot,
		    unsigned char *pci_func, resource_size_t * io_addr,
		    unsigned int *irq)
{
	int i;

	if (!amcc)
		return -1;
	*pci_bus = amcc->pci_bus;
	*pci_slot = amcc->pci_slot;
	*pci_func = amcc->pci_func;
	for (i = 0; i < 5; i++)
		io_addr[i] = amcc->io_addr[i];
	*irq = amcc->irq;
	return 0;
}

/****************************************************************************/
/* select and alloc card */
struct pcilst_struct *ptr_select_and_alloc_pci_card(unsigned short vendor_id,
						    unsigned short device_id,
						    unsigned short pci_bus,
						    unsigned short pci_slot,
						    int i_Master)
{
	struct pcilst_struct *card;

	if ((pci_bus < 1) & (pci_slot < 1)) {
		/* use autodetection */
		card = ptr_find_free_pci_card_by_device(vendor_id, device_id);
		if (card == NULL) {
			printk(" - Unused card not found in system!\n");
			return NULL;
		}
	} else {
		switch (i_find_free_pci_card_by_position(vendor_id, device_id,
							 pci_bus, pci_slot,
							 &card)) {
		case 1:
			printk(" - Card not found on requested position b:s %d:%d!\n",
				  pci_bus, pci_slot);
			return NULL;
		case 2:
			printk(" - Card on requested position is used b:s %d:%d!\n",
				  pci_bus, pci_slot);
			return NULL;
		}
	}

	if (pci_card_alloc(card, i_Master) != 0) {
		printk(" - Can't allocate card!\n");
		return NULL;

	}

	return card;
}
#endif
