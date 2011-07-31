/*
   Power management interface routines.
   Written by Mariusz Matuszek.
   This code is currently just a placeholder for later work and
   does not do anything useful.

   This is part of rtl8180 OpenSource driver.
   Copyright (C) Andrea Merello 2004  <andreamrl@tiscali.it>
   Released under the terms of GPL (General Public Licence)
*/

#ifdef CONFIG_RTL8180_PM


#include "r8180_hw.h"
#include "r8180_pm.h"

int rtl8180_save_state (struct pci_dev *dev, u32 state)
{
	printk(KERN_NOTICE "r8180 save state call (state %u).\n", state);
	return(-EAGAIN);
}


int rtl8180_suspend (struct pci_dev *dev, u32 state)
{
	printk(KERN_NOTICE "r8180 suspend call (state %u).\n", state);
	return(-EAGAIN);
}


int rtl8180_resume (struct pci_dev *dev)
{
	printk(KERN_NOTICE "r8180 resume call.\n");
	return(-EAGAIN);
}


int rtl8180_enable_wake (struct pci_dev *dev, u32 state, int enable)
{
	printk(KERN_NOTICE "r8180 enable wake call (state %u, enable %d).\n",
	       state, enable);
	return(-EAGAIN);
}



#endif //CONFIG_RTL8180_PM
