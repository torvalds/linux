/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#ifndef __MANTIS_DVB_H
#define __MANTIS_DVB_H

enum mantis_power {
	POWER_OFF	= 0,
	POWER_ON	= 1
};

extern int mantis_frontend_power(struct mantis_pci *mantis, enum mantis_power power);
extern void mantis_frontend_soft_reset(struct mantis_pci *mantis);

extern int mantis_dvb_init(struct mantis_pci *mantis);
extern int mantis_dvb_exit(struct mantis_pci *mantis);

#endif /* __MANTIS_DVB_H */
