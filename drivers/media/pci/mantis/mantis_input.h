/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#ifndef __MANTIS_INPUT_H
#define __MANTIS_INPUT_H

int mantis_input_init(struct mantis_pci *mantis);
void mantis_input_exit(struct mantis_pci *mantis);
void mantis_input_process(struct mantis_pci *mantis, int scancode);

#endif /* __MANTIS_UART_H */
