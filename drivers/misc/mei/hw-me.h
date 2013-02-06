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

void mei_read_slots(struct mei_device *dev,
		     unsigned char *buffer,
		     unsigned long buffer_length);

int mei_write_message(struct mei_device *dev,
			struct mei_msg_hdr *header,
			unsigned char *buf);

bool mei_hbuf_is_empty(struct mei_device *dev);

int mei_hbuf_empty_slots(struct mei_device *dev);

static inline size_t mei_hbuf_max_data(const struct mei_device *dev)
{
	return dev->hbuf_depth * sizeof(u32) - sizeof(struct mei_msg_hdr);
}

/* get slots (dwords) from a message length + header (bytes) */
static inline unsigned char mei_data2slots(size_t length)
{
	return DIV_ROUND_UP(sizeof(struct mei_msg_hdr) + length, 4);
}

int mei_count_full_read_slots(struct mei_device *dev);

#endif /* _MEI_INTERFACE_H_ */
