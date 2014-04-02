/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */



#ifndef _MEI_INTERFACE_H_
#define _MEI_INTERFACE_H_

#include <linux/mei.h>
#include <linux/irqreturn.h>
#include "mei_dev.h"
#include "client.h"

struct mei_me_hw {
	void __iomem *mem_addr;
	/*
	 * hw states of host and fw(ME)
	 */
	u32 host_hw_state;
	u32 me_hw_state;
};

#define to_me_hw(dev) (struct mei_me_hw *)((dev)->hw)

struct mei_device *mei_me_dev_init(struct pci_dev *pdev);

irqreturn_t mei_me_irq_quick_handler(int irq, void *dev_id);
irqreturn_t mei_me_irq_thread_handler(int irq, void *dev_id);

#endif /* _MEI_INTERFACE_H_ */
