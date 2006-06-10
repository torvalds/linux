/*
 * The USB Monitor, inspired by Dave Harding's USBMon.
 *
 * Copyright (C) 2005 Pete Zaitcev (zaitcev@redhat.com)
 */

#ifndef __USB_MON_H
#define __USB_MON_H

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/kref.h>
/* #include <linux/usb.h> */	/* We use struct pointers only in this header */

#define TAG "usbmon"

struct mon_bus {
	struct list_head bus_link;
	spinlock_t lock;
	struct dentry *dent_s;		/* Debugging file */
	struct dentry *dent_t;		/* Text interface file */
	struct usb_bus *u_bus;

	/* Ref */
	int nreaders;			/* Under mon_lock AND mbus->lock */
	struct list_head r_list;	/* Chain of readers (usually one) */
	struct kref ref;		/* Under mon_lock */

	/* Stats */
	unsigned int cnt_events;
	unsigned int cnt_text_lost;
};

/*
 * An instance of a process which opened a file (but can fork later)
 */
struct mon_reader {
	struct list_head r_link;
	struct mon_bus *m_bus;
	void *r_data;		/* Use container_of instead? */

	void (*rnf_submit)(void *data, struct urb *urb);
	void (*rnf_complete)(void *data, struct urb *urb);
};

void mon_reader_add(struct mon_bus *mbus, struct mon_reader *r);
void mon_reader_del(struct mon_bus *mbus, struct mon_reader *r);

/*
 */
extern char mon_dmapeek(unsigned char *dst, dma_addr_t dma_addr, int len);

extern struct mutex mon_lock;

extern struct file_operations mon_fops_text;
extern struct file_operations mon_fops_stat;

#endif /* __USB_MON_H */
