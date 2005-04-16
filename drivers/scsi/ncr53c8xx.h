/******************************************************************************
**  Device driver for the PCI-SCSI NCR538XX controller family.
**
**  Copyright (C) 1994  Wolfgang Stanglmeier
**
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
**
**  This driver has been ported to Linux from the FreeBSD NCR53C8XX driver
**  and is currently maintained by
**
**          Gerard Roudier              <groudier@free.fr>
**
**  Being given that this driver originates from the FreeBSD version, and
**  in order to keep synergy on both, any suggested enhancements and corrections
**  received on Linux are automatically a potential candidate for the FreeBSD 
**  version.
**
**  The original driver has been written for 386bsd and FreeBSD by
**          Wolfgang Stanglmeier        <wolf@cologne.de>
**          Stefan Esser                <se@mi.Uni-Koeln.de>
**
**  And has been ported to NetBSD by
**          Charles M. Hannum           <mycroft@gnu.ai.mit.edu>
**
*******************************************************************************
*/

#ifndef NCR53C8XX_H
#define NCR53C8XX_H

#include <scsi/scsi_host.h>

#include "sym53c8xx_defs.h"

/*
	Build a scatter/gather entry.
	see sym53c8xx_2/sym_hipd.h for more detailed sym_build_sge()
	implementation ;)
 */

#define ncr_build_sge(np, data, badd, len)	\
do {						\
	(data)->addr = cpu_to_scr(badd);	\
	(data)->size = cpu_to_scr(len);		\
} while (0)

/*==========================================================
**
**	Structures used by the detection routine to transmit 
**	device configuration to the attach function.
**
**==========================================================
*/
struct ncr_slot {
	u_long	base;
	u_long	base_2;
	u_long	base_c;
	u_long	base_2_c;
	void __iomem *base_v;
	void __iomem *base_2_v;
	int	irq;
/* port and reg fields to use INB, OUTB macros */
	volatile struct ncr_reg	__iomem *reg;
};

/*==========================================================
**
**	Structure used by detection routine to save data on 
**	each detected board for attach.
**
**==========================================================
*/
struct ncr_device {
	struct device  *dev;
	struct ncr_slot  slot;
	struct ncr_chip  chip;
	u_char host_id;
	u8 differential;
};

extern struct Scsi_Host *ncr_attach(struct scsi_host_template *tpnt, int unit, struct ncr_device *device);
extern int ncr53c8xx_release(struct Scsi_Host *host);
irqreturn_t ncr53c8xx_intr(int irq, void *dev_id, struct pt_regs * regs);
extern int ncr53c8xx_init(void);
extern void ncr53c8xx_exit(void);

#endif /* NCR53C8XX_H */
