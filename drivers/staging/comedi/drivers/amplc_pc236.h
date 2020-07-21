/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * comedi/drivers/amplc_pc236.h
 * Header for "amplc_pc236", "amplc_pci236" and "amplc_pc236_common".
 *
 * Copyright (C) 2002-2014 MEV Ltd. <https://www.mev.co.uk/>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 */

#ifndef AMPLC_PC236_H_INCLUDED
#define AMPLC_PC236_H_INCLUDED

#include <linux/types.h>

struct comedi_device;

struct pc236_board {
	const char *name;
	void (*intr_update_cb)(struct comedi_device *dev, bool enable);
	bool (*intr_chk_clr_cb)(struct comedi_device *dev);
};

struct pc236_private {
	unsigned long lcr_iobase; /* PLX PCI9052 config registers in PCIBAR1 */
	bool enable_irq;
};

int amplc_pc236_common_attach(struct comedi_device *dev, unsigned long iobase,
			      unsigned int irq, unsigned long req_irq_flags);

#endif
