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
#include <media/media-device.h>

#define DVB_MAJOR 212

#if defined(CONFIG_DVB_MAX_ADAPTERS) && CONFIG_DVB_MAX_ADAPTERS > 0
  #define DVB_MAX_ADAPTERS CONFIG_DVB_MAX_ADAPTERS
#else
  #define DVB_MAX_ADAPTERS 8
#endif

#define DVB_UNSET (-1)

#define DVB_DEVICE_VIDEO      0
#define DVB_DEVICE_AUDIO      1
#define DVB_DEVICE_SEC        2
#define DVB_DEVICE_FRONTEND   3
#define DVB_DEVICE_DEMUX      4
#define DVB_DEVICE_DVR        5
#define DVB_DEVICE_CA         6
#define DVB_DEVICE_NET        7
#define DVB_DEVICE_OSD        8

#define DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr) \
	static short adapter_nr[] = \
		{[0 ... (DVB_MAX_ADAPTERS - 1)] = DVB_UNSET }; \
	module_param_array(adapter_nr, short, NULL, 0444); \
	MODULE_PARM_DESC(adapter_nr, "DVB adapter numbers")

struct dvb_frontend;

/**
 * struct dvb_adapter - represents a Digital TV adapter using Linux DVB API
 *
 * @num:		Number of the adapter
 * @list_head:		List with the DVB adapters
 * @device_list:	List with the DVB devices
 * @name:		Name of the adapter
 * @proposed_mac:	proposed MAC address for the adapter
 * @priv:		private data
 * @device:		pointer to struct device
 * @module:		pointer to struct module
 * @mfe_shared:		mfe shared: indicates mutually exclusive frontends
 *			Thie usage of this flag is currently deprecated
 * @mfe_dvbdev:		Frontend device in use, in the case of MFE
 * @mfe_lock:		Lock to prevent using the other frontends when MFE is
 *			used.
 * @mdev:		pointer to struct media_device, used when the media
 *			controller is used.
 */
struct dvb_adapter {
	int num;
	struct list_head list_head;
	struct list_head device_list;
	const char *name;
	u8 proposed_mac [6];
	void* priv;

	struct device *device;

	struct module *module;

	int mfe_shared;			/* indicates mutually exclusive frontends */
	struct dvb_device *mfe_dvbdev;	/* frontend device in use */
	struct mutex mfe_lock;		/* access lock for thread creation */

#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
	struct media_device *mdev;
#endif
};

/**
 * struct dvb_device - represents a DVB device node
 *
 * @list_head:	List head with all DVB devices
 * @fops:	pointer to struct file_operations
 * @adapter:	pointer to the adapter that holds this device node
 * @type:	type of the device: DVB_DEVICE_SEC, DVB_DEVICE_FRONTEND,
 *		DVB_DEVICE_DEMUX, DVB_DEVICE_DVR, DVB_DEVICE_CA, DVB_DEVICE_NET
 * @minor:	devnode minor number. Major number is always DVB_MAJOR.
 * @id:		device ID number, inside the adapter
 * @readers:	Initialized by the caller. Each call to open() in Read Only mode
 *		decreases this counter by one.
 * @writers:	Initialized by the caller. Each call to open() in Read/Write
 *		mode decreases this counter by one.
 * @users:	Initialized by the caller. Each call to open() in any mode
 *		decreases this counter by one.
 * @wait_queue:	wait queue, used to wait for certain events inside one of
 *		the DVB API callers
 * @kernel_ioctl: callback function used to handle ioctl calls from userspace.
 * @name:	Name to be used for the device at the Media Controller
 * @entity:	pointer to struct media_entity associated with the device node
 * @pads:	pointer to struct media_pad associated with @entity;
 * @priv:	private data
 *
 * This structure is used by the DVB core (frontend, CA, net, demux) in
 * order to create the device nodes. Usually, driver should not initialize
 * this struct diretly.
 */
struct dvb_device {
	struct list_head list_head;
	const struct file_operations *fops;
	struct dvb_adapter *adapter;
	int type;
	int minor;
	u32 id;

	/* in theory, 'users' can vanish now,
	   but I don't want to change too much now... */
	int readers;
	int writers;
	int users;

	wait_queue_head_t	  wait_queue;
	/* don't really need those !? -- FIXME: use video_usercopy  */
	int (*kernel_ioctl)(struct file *file, unsigned int cmd, void *arg);

	/* Needed for media controller register/unregister */
#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
	const char *name;

	/* Allocated and filled inside dvbdev.c */
	struct media_entity *entity;
	struct media_pad *pads;
#endif

	void *priv;
};

/**
 * dvb_register_adapter - Registers a new DVB adapter
 *
 * @adap:	pointer to struct dvb_adapter
 * @name:	Adapter's name
 * @module:	initialized with THIS_MODULE at the caller
 * @device:	pointer to struct device that corresponds to the device driver
 * @adapter_nums: Array with a list of the numbers for @dvb_register_adapter;
 * 		to select among them. Typically, initialized with:
 *		DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nums)
 */
int dvb_register_adapter(struct dvb_adapter *adap, const char *name,
			 struct module *module, struct device *device,
			 short *adapter_nums);

/**
 * dvb_unregister_adapter - Unregisters a DVB adapter
 *
 * @adap:	pointer to struct dvb_adapter
 */
int dvb_unregister_adapter(struct dvb_adapter *adap);

/**
 * dvb_register_device - Registers a new DVB device
 *
 * @adap:	pointer to struct dvb_adapter
 * @pdvbdev:	pointer to the place where the new struct dvb_device will be
 *		stored
 * @template:	Template used to create &pdvbdev;
 * @priv:	private data
 * @type:	type of the device: DVB_DEVICE_SEC, DVB_DEVICE_FRONTEND,
 *		DVB_DEVICE_DEMUX, DVB_DEVICE_DVR, DVB_DEVICE_CA, DVB_DEVICE_NET
 */
int dvb_register_device(struct dvb_adapter *adap,
			struct dvb_device **pdvbdev,
			const struct dvb_device *template,
			void *priv,
			int type);

/**
 * dvb_unregister_device - Unregisters a DVB device
 *
 * @dvbdev:	pointer to struct dvb_device
 */
void dvb_unregister_device(struct dvb_device *dvbdev);

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
void dvb_create_media_graph(struct dvb_adapter *adap);
static inline void dvb_register_media_controller(struct dvb_adapter *adap,
						 struct media_device *mdev)
{
	adap->mdev = mdev;
}

#else
static inline void dvb_create_media_graph(struct dvb_adapter *adap) {}
#define dvb_register_media_controller(a, b) {}
#endif

int dvb_generic_open (struct inode *inode, struct file *file);
int dvb_generic_release (struct inode *inode, struct file *file);
long dvb_generic_ioctl (struct file *file,
			      unsigned int cmd, unsigned long arg);

/* we don't mess with video_usercopy() any more,
we simply define out own dvb_usercopy(), which will hopefully become
generic_usercopy()  someday... */

int dvb_usercopy(struct file *file, unsigned int cmd, unsigned long arg,
		 int (*func)(struct file *file, unsigned int cmd, void *arg));

/** generic DVB attach function. */
#ifdef CONFIG_MEDIA_ATTACH
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

#define dvb_detach(FUNC)	symbol_put_addr(FUNC)

#else
#define dvb_attach(FUNCTION, ARGS...) ({ \
	FUNCTION(ARGS); \
})

#define dvb_detach(FUNC)	{}

#endif

#endif /* #ifndef _DVBDEV_H_ */
