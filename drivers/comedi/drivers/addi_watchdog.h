/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ADDI_WATCHDOG_H
#define _ADDI_WATCHDOG_H

struct comedi_subdevice;

void addi_watchdog_reset(unsigned long iobase);
int addi_watchdog_init(struct comedi_subdevice *s, unsigned long iobase);

#endif
