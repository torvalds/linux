/**
 * tpci200.h
 *
 * driver for the carrier TEWS TPCI-200
 * Copyright (c) 2009 Nicolas Serafini, EIC2 SA
 * Copyright (c) 2010,2011 Samuel Iglesias Gonsalvez <siglesia@cern.ch>, CERN
 * Copyright (c) 2012 Samuel Iglesias Gonsalvez <siglesias@igalia.com>, Igalia
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#ifndef _TPCI200_H_
#define _TPCI200_H_

#include <linux/limits.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/swab.h>
#include <linux/io.h>

#include "../ipack.h"

#define TPCI200_NB_SLOT               0x4
#define TPCI200_NB_BAR                0x6

#define TPCI200_VENDOR_ID             0x1498
#define TPCI200_DEVICE_ID             0x30C8
#define TPCI200_SUBVENDOR_ID          0x1498
#define TPCI200_SUBDEVICE_ID          0x300A

#define TPCI200_CFG_MEM_BAR           0
#define TPCI200_IP_INTERFACE_BAR      2
#define TPCI200_IO_ID_INT_SPACES_BAR  3
#define TPCI200_MEM16_SPACE_BAR       4
#define TPCI200_MEM8_SPACE_BAR        5

struct tpci200_regs {
	__le16	revision;
	/* writes to control should occur with the mutex held to protect
	 * read-modify-write operations */
	__le16  control[4];
	__le16	reset;
	__le16	status;
	u8	reserved[242];
} __packed;

#define TPCI200_IFACE_SIZE            0x100

#define TPCI200_IO_SPACE_OFF          0x0000
#define TPCI200_IO_SPACE_GAP          0x0100
#define TPCI200_IO_SPACE_SIZE         0x0080
#define TPCI200_ID_SPACE_OFF          0x0080
#define TPCI200_ID_SPACE_GAP          0x0100
#define TPCI200_ID_SPACE_SIZE         0x0040
#define TPCI200_INT_SPACE_OFF         0x00C0
#define TPCI200_INT_SPACE_GAP         0x0100
#define TPCI200_INT_SPACE_SIZE        0x0040
#define TPCI200_IOIDINT_SIZE          0x0400

#define TPCI200_MEM8_GAP              0x00400000
#define TPCI200_MEM8_SIZE             0x00400000
#define TPCI200_MEM16_GAP             0x00800000
#define TPCI200_MEM16_SIZE            0x00800000

/* control field in tpci200_regs */
#define TPCI200_INT0_EN               0x0040
#define TPCI200_INT1_EN               0x0080
#define TPCI200_INT0_EDGE             0x0010
#define TPCI200_INT1_EDGE             0x0020
#define TPCI200_ERR_INT_EN            0x0008
#define TPCI200_TIME_INT_EN           0x0004
#define TPCI200_RECOVER_EN            0x0002
#define TPCI200_CLK32                 0x0001

/* reset field in tpci200_regs */
#define TPCI200_A_RESET               0x0001
#define TPCI200_B_RESET               0x0002
#define TPCI200_C_RESET               0x0004
#define TPCI200_D_RESET               0x0008

/* status field in tpci200_regs */
#define TPCI200_A_TIMEOUT             0x1000
#define TPCI200_B_TIMEOUT             0x2000
#define TPCI200_C_TIMEOUT             0x4000
#define TPCI200_D_TIMEOUT             0x8000

#define TPCI200_A_ERROR               0x0100
#define TPCI200_B_ERROR               0x0200
#define TPCI200_C_ERROR               0x0400
#define TPCI200_D_ERROR               0x0800

#define TPCI200_A_INT0                0x0001
#define TPCI200_A_INT1                0x0002
#define TPCI200_B_INT0                0x0004
#define TPCI200_B_INT1                0x0008
#define TPCI200_C_INT0                0x0010
#define TPCI200_C_INT1                0x0020
#define TPCI200_D_INT0                0x0040
#define TPCI200_D_INT1                0x0080

#define TPCI200_SLOT_INT_MASK         0x00FF

/* PCI Configuration registers. The PCI bridge is a PLX Technology PCI9030. */
#define LAS1_DESC		      0x2C
#define LAS2_DESC		      0x30

/* Bits in the LAS?_DESC registers */
#define LAS_BIT_BIGENDIAN	      24

#define VME_IOID_SPACE  "IOID"
#define VME_MEM_SPACE  "MEM"

/**
 * struct slot_irq - slot IRQ definition.
 * @vector	Vector number
 * @handler	Handler called when IRQ arrives
 * @arg		Handler argument
 *
 */
struct slot_irq {
	struct ipack_device *holder;
	int		vector;
	irqreturn_t	(*handler)(void *);
	void		*arg;
};

/**
 * struct tpci200_slot - data specific to the tpci200 slot.
 * @slot_id	Slot identification gived to external interface
 * @irq		Slot IRQ infos
 * @io_phys	IO physical base address register of the slot
 * @id_phys	ID physical base address register of the slot
 * @mem_phys	MEM physical base address register of the slot
 *
 */
struct tpci200_slot {
	struct slot_irq		*irq;
	struct ipack_addr_space io_phys;
	struct ipack_addr_space id_phys;
	struct ipack_addr_space mem_phys;
};

/**
 * struct tpci200_infos - informations specific of the TPCI200 tpci200.
 * @pci_dev		PCI device
 * @interface_regs	Pointer to IP interface space (Bar 2)
 * @ioidint_space	Pointer to IP ID, IO and INT space (Bar 3)
 * @mem8_space		Pointer to MEM space (Bar 4)
 *
 */
struct tpci200_infos {
	struct pci_dev			*pdev;
	struct pci_device_id		*id_table;
	struct tpci200_regs __iomem	*interface_regs;
	void __iomem			*ioidint_space;
	void __iomem			*mem8_space;
	void __iomem			*cfg_regs;
	struct ipack_bus_device		*ipack_bus;
};
struct tpci200_board {
	unsigned int		number;
	struct mutex		mutex;
	spinlock_t		regs_lock;
	struct tpci200_slot	*slots;
	struct tpci200_infos	*info;
};

#endif /* _TPCI200_H_ */
