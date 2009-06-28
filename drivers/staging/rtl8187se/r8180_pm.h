/*
        Power management interface routines.
	Written by Mariusz Matuszek.
	This code is currently just a placeholder for later work and
	does not do anything useful.

	This is part of rtl8180 OpenSource driver.
	Copyright (C) Andrea Merello 2004  <andreamrl@tiscali.it>
	Released under the terms of GPL (General Public Licence)

*/


#ifndef R8180_PM_H
#define R8180_PM_H

#include <linux/types.h>
#include <linux/pci.h>

int rtl8180_suspend (struct pci_dev *pdev, pm_message_t state);
int rtl8180_resume (struct pci_dev *pdev);

#endif //R8180_PM_H

