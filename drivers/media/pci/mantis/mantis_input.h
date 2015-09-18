/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
*/

#ifndef __MANTIS_INPUT_H
#define __MANTIS_INPUT_H

int mantis_input_init(struct mantis_pci *mantis);
void mantis_input_exit(struct mantis_pci *mantis);
void mantis_input_process(struct mantis_pci *mantis, int scancode);

#endif /* __MANTIS_UART_H */
