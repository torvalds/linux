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
	struct usb_bus *u_bus;

	int text_inited;
	int bin_inited;
	struct dentry *dent_s;		/* Debugging file */
	struct dentry *dent_t;		/* Text interface file */
	struct dentry *dent_u;		/* Second text interface file */
	struct device *classdev;	/* Device in usbmon class */

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
	void (*rnf_error)(void *data, struct urb *urb, int error);
	void (*rnf_complete)(void *data, struct urb *urb);
};

void mon_reader_add(struct mon_bus *mbus, struct mon_reader *r);
void mon_reader_del(struct mon_bus *mbus, struct mon_reader *r);

struct mon_bus *mon_bus_lookup(unsigned int num);

int /*bool*/ mon_text_add(struct mon_bus *mbus, const struct usb_bus *ubus);
void mon_text_del(struct mon_bus *mbus);
int /*bool*/ mon_bin_add(struct mon_bus *mbus, const struct usb_bus *ubus);
void mon_bin_del(struct mon_bus *mbus);

int __init mon_text_init(void);
void mon_text_exit(void);
int __init mon_bin_init(void);
void mon_bin_exit(void);

/*
 * DMA interface.
 *
 * XXX The vectored side needs a serious re-thinking. Abstracting vectors,
 * like in Paolo's original patch, produces a double pkmap. We need an idea.
*/
extern char mon_dmapeek(unsigned char *dst, dma_addr_t dma_addr, int len);

struct mon_reader_bin;
extern void mon_dmapeek_vec(const struct mon_reader_bin *rp,
    unsigned int offset, dma_addr_t dma_addr, unsigned int len);
extern unsigned int mon_copy_to_buff(const struct mon_reader_bin *rp,
    unsigned int offset, const unsigned char *from, unsigned int len);

/*
 */
extern struct mutex mon_lock;

extern const struct file_operations mon_fops_stat;

extern struct mon_bus mon_bus0;		/* Only for redundant checks */

#endif /* __USB_MON_H */
