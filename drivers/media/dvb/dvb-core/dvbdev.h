/*
 * dvbdev.h
 *
 * Copyright (C) 2000 Ralph Metzler & Marcus Metzler
 *                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Lesser Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVBDEV_H_
#define _DVBDEV_H_

#include <linux/types.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/smp_lock.h>

#define DVB_MAJOR 212

#define DVB_DEVICE_VIDEO      0
#define DVB_DEVICE_AUDIO      1
#define DVB_DEVICE_SEC        2
#define DVB_DEVICE_FRONTEND   3
#define DVB_DEVICE_DEMUX      4
#define DVB_DEVICE_DVR        5
#define DVB_DEVICE_CA         6
#define DVB_DEVICE_NET        7
#define DVB_DEVICE_OSD        8


struct dvb_adapter {
	int num;
	struct list_head list_head;
	struct list_head device_list;
	const char *name;
	u8 proposed_mac [6];
	void* priv;

	struct device *device;

	struct module *module;
};


struct dvb_device {
	struct list_head list_head;
	struct file_operations *fops;
	struct dvb_adapter *adapter;
	int type;
	u32 id;

	/* in theory, 'users' can vanish now,
	   but I don't want to change too much now... */
	int readers;
	int writers;
	int users;

	/* don't really need those !? -- FIXME: use video_usercopy  */
	int (*kernel_ioctl)(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg);

	void *priv;
};


extern int dvb_register_adapter (struct dvb_adapter *adap, const char *name, struct module *module, struct device *device);
extern int dvb_unregister_adapter (struct dvb_adapter *adap);

extern int dvb_register_device (struct dvb_adapter *adap,
				struct dvb_device **pdvbdev,
				const struct dvb_device *template,
				void *priv,
				int type);

extern void dvb_unregister_device (struct dvb_device *dvbdev);

extern int dvb_generic_open (struct inode *inode, struct file *file);
extern int dvb_generic_release (struct inode *inode, struct file *file);
extern int dvb_generic_ioctl (struct inode *inode, struct file *file,
			      unsigned int cmd, unsigned long arg);

/* we don't mess with video_usercopy() any more,
we simply define out own dvb_usercopy(), which will hopefully become
generic_usercopy()  someday... */

extern int dvb_usercopy(struct inode *inode, struct file *file,
			    unsigned int cmd, unsigned long arg,
			    int (*func)(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg));

/** generic DVB attach function. */
#ifdef CONFIG_DVB_CORE_ATTACH
#define dvb_attach(FUNCTION, ARGS...) ({ \
	void *__r = NULL; \
	typeof(&FUNCTION) __a = symbol_request(FUNCTION); \
	if (__a) { \
		__r = (void *) __a(ARGS); \
		if (__r == NULL) \
			symbol_put(FUNCTION); \
	} else { \
		printk(KERN_ERR "DVB: Unable to find symbol "#FUNCTION"()\n"); \
	} \
	__r; \
})

#else
#define dvb_attach(FUNCTION, ARGS...) ({ \
	FUNCTION(ARGS); \
})

#endif

#endif /* #ifndef _DVBDEV_H_ */
