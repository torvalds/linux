/*
        Power management interface routines.
	Written by Mariusz Matuszek.
	This code is currently just a placeholder for later work and
	does not do anything useful.

	This is part of rtl8180 OpenSource driver.
	Copyright (C) Andrea Merello 2004  <andreamrl@tiscali.it>
	Released under the terms of GPL (General Public Licence)

*/

#ifdef CONFIG_RTL8192_PM

#ifndef R8192_PM_H
#define R8192_PM_H

#include <linux/types.h>
#include <linux/usb.h>

int rtl8192U_save_tate (struct pci_dev *dev, u32 state);
int rtl8192U_suspend(struct usb_interface *intf, pm_message_t state);
int rtl8192U_resume (struct usb_interface *intf);
int rtl8192U_enable_wake (struct pci_dev *dev, u32 state, int enable);

#endif //R8192U_PM_H
#endif // CONFIG_RTL8192_PM
