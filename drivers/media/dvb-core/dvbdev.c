// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * dvbdev.c
 *
 * Copyright (C) 2000 Ralph  Metzler <ralph@convergence.de>
 *                  & Marcus Metzler <marcus@convergence.de>
 *                    for convergence integrated media GmbH
 */

#define pr_fmt(fmt) "dvbdev: " fmt

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <media/dvbdev.h>

/* Due to enum tuner_pad_index */
#include <media/tuner.h>

static DEFINE_MUTEX(dvbdev_mutex);
static int dvbdev_debug;

module_param(dvbdev_debug, int, 0644);
MODULE_PARM_DESC(dvbdev_debug, "Turn on/off device debugging (default:off).");

#define dprintk(fmt, arg...) do {					\
	if (dvbdev_debug)						\
		printk(KERN_DEBUG pr_fmt("%s: " fmt),			\
		       __func__, ##arg);				\
} while (0)

static LIST_HEAD(dvb_adapter_list);
static DEFINE_MUTEX(dvbdev_register_lock);

static const char * const dnames[] = {
	[DVB_DEVICE_VIDEO] =		"video",
	[DVB_DEVICE_AUDIO] =		"audio",
	[DVB_DEVICE_SEC] =		"sec",
	[DVB_DEVICE_FRONTEND] =		"frontend",
	[DVB_DEVICE_DEMUX] =		"demux",
	[DVB_DEVICE_DVR] =		"dvr",
	[DVB_DEVICE_CA] =		"ca",
	[DVB_DEVICE_NET] =		"net",
	[DVB_DEVICE_OSD] =		"osd"
};

#ifdef CONFIG_DVB_DYNAMIC_MINORS
#define MAX_DVB_MINORS		256
#define DVB_MAX_IDS		MAX_DVB_MINORS
#else
#define DVB_MAX_IDS		4

static const u8 minor_type[] = {
       [DVB_DEVICE_VIDEO]      = 0,
       [DVB_DEVICE_AUDIO]      = 1,
       [DVB_DEVICE_SEC]        = 2,
       [DVB_DEVICE_FRONTEND]   = 3,
       [DVB_DEVICE_DEMUX]      = 4,
       [DVB_DEVICE_DVR]        = 5,
       [DVB_DEVICE_CA]         = 6,
       [DVB_DEVICE_NET]        = 7,
       [DVB_DEVICE_OSD]        = 8,
};

#define nums2minor(num, type, id) \
       (((num) << 6) | ((id) << 4) | minor_type[type])

#define MAX_DVB_MINORS		(DVB_MAX_ADAPTERS*64)
#endif

static struct class *dvb_class;

static struct dvb_device *dvb_minors[MAX_DVB_MINORS];
static DECLARE_RWSEM(minor_rwsem);

static int dvb_device_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev;

	mutex_lock(&dvbdev_mutex);
	down_read(&minor_rwsem);
	dvbdev = dvb_minors[iminor(inode)];

	if (dvbdev && dvbdev->fops) {
		int err = 0;
		const struct file_operations *new_fops;

		new_fops = fops_get(dvbdev->fops);
		if (!new_fops)
			goto fail;
		file->private_data = dvb_device_get(dvbdev);
		replace_fops(file, new_fops);
		if (file->f_op->open)
			err = file->f_op->open(inode, file);
		up_read(&minor_rwsem);
		mutex_unlock(&dvbdev_mutex);
		return err;
	}
fail:
	up_read(&minor_rwsem);
	mutex_unlock(&dvbdev_mutex);
	return -ENODEV;
}


static const struct file_operations dvb_device_fops =
{
	.owner =	THIS_MODULE,
	.open =		dvb_device_open,
	.llseek =	noop_llseek,
};

static struct cdev dvb_device_cdev;

int dvb_generic_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;

	if (!dvbdev)
		return -ENODEV;

	if (!dvbdev->users)
		return -EBUSY;

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if (!dvbdev->readers)
			return -EBUSY;
		dvbdev->readers--;
	} else {
		if (!dvbdev->writers)
			return -EBUSY;
		dvbdev->writers--;
	}

	dvbdev->users--;
	return 0;
}
EXPORT_SYMBOL(dvb_generic_open);


int dvb_generic_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;

	if (!dvbdev)
		return -ENODEV;

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		dvbdev->readers++;
	} else {
		dvbdev->writers++;
	}

	dvbdev->users++;

	dvb_device_put(dvbdev);

	return 0;
}
EXPORT_SYMBOL(dvb_generic_release);


long dvb_generic_ioctl(struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	struct dvb_device *dvbdev = file->private_data;

	if (!dvbdev)
		return -ENODEV;

	if (!dvbdev->kernel_ioctl)
		return -EINVAL;

	return dvb_usercopy(file, cmd, arg, dvbdev->kernel_ioctl);
}
EXPORT_SYMBOL(dvb_generic_ioctl);


static int dvbdev_get_free_id (struct dvb_adapter *adap, int type)
{
	u32 id = 0;

	while (id < DVB_MAX_IDS) {
		struct dvb_device *dev;
		list_for_each_entry(dev, &adap->device_list, list_head)
			if (dev->type == type && dev->id == id)
				goto skip;
		return id;
skip:
		id++;
	}
	return -ENFILE;
}

static void dvb_media_device_free(struct dvb_device *dvbdev)
{
#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
	if (dvbdev->entity) {
		media_device_unregister_entity(dvbdev->entity);
		kfree(dvbdev->entity);
		kfree(dvbdev->pads);
		dvbdev->entity = NULL;
		dvbdev->pads = NULL;
	}

	if (dvbdev->tsout_entity) {
		int i;

		for (i = 0; i < dvbdev->tsout_num_entities; i++) {
			media_device_unregister_entity(&dvbdev->tsout_entity[i]);
			kfree(dvbdev->tsout_entity[i].name);
		}
		kfree(dvbdev->tsout_entity);
		kfree(dvbdev->tsout_pads);
		dvbdev->tsout_entity = NULL;
		dvbdev->tsout_pads = NULL;

		dvbdev->tsout_num_entities = 0;
	}

	if (dvbdev->intf_devnode) {
		media_devnode_remove(dvbdev->intf_devnode);
		dvbdev->intf_devnode = NULL;
	}

	if (dvbdev->adapter->conn) {
		media_device_unregister_entity(dvbdev->adapter->conn);
		kfree(dvbdev->adapter->conn);
		dvbdev->adapter->conn = NULL;
		kfree(dvbdev->adapter->conn_pads);
		dvbdev->adapter->conn_pads = NULL;
	}
#endif
}

#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
static int dvb_create_tsout_entity(struct dvb_device *dvbdev,
				    const char *name, int npads)
{
	int i;

	dvbdev->tsout_pads = kcalloc(npads, sizeof(*dvbdev->tsout_pads),
				     GFP_KERNEL);
	if (!dvbdev->tsout_pads)
		return -ENOMEM;

	dvbdev->tsout_entity = kcalloc(npads, sizeof(*dvbdev->tsout_entity),
				       GFP_KERNEL);
	if (!dvbdev->tsout_entity)
		return -ENOMEM;

	dvbdev->tsout_num_entities = npads;

	for (i = 0; i < npads; i++) {
		struct media_pad *pads = &dvbdev->tsout_pads[i];
		struct media_entity *entity = &dvbdev->tsout_entity[i];
		int ret;

		entity->name = kasprintf(GFP_KERNEL, "%s #%d", name, i);
		if (!entity->name)
			return -ENOMEM;

		entity->function = MEDIA_ENT_F_IO_DTV;
		pads->flags = MEDIA_PAD_FL_SINK;

		ret = media_entity_pads_init(entity, 1, pads);
		if (ret < 0)
			return ret;

		ret = media_device_register_entity(dvbdev->adapter->mdev,
						   entity);
		if (ret < 0)
			return ret;
	}
	return 0;
}

#define DEMUX_TSOUT	"demux-tsout"
#define DVR_TSOUT	"dvr-tsout"

static int dvb_create_media_entity(struct dvb_device *dvbdev,
				   int type, int demux_sink_pads)
{
	int i, ret, npads;

	switch (type) {
	case DVB_DEVICE_FRONTEND:
		npads = 2;
		break;
	case DVB_DEVICE_DVR:
		ret = dvb_create_tsout_entity(dvbdev, DVR_TSOUT,
					      demux_sink_pads);
		return ret;
	case DVB_DEVICE_DEMUX:
		npads = 1 + demux_sink_pads;
		ret = dvb_create_tsout_entity(dvbdev, DEMUX_TSOUT,
					      demux_sink_pads);
		if (ret < 0)
			return ret;
		break;
	case DVB_DEVICE_CA:
		npads = 2;
		break;
	case DVB_DEVICE_NET:
		/*
		 * We should be creating entities for the MPE/ULE
		 * decapsulation hardware (or software implementation).
		 *
		 * However, the number of for the MPE/ULE decaps may not be
		 * fixed. As we don't have yet dynamic support for PADs at
		 * the Media Controller, let's not create the decap
		 * entities yet.
		 */
		return 0;
	default:
		return 0;
	}

	dvbdev->entity = kzalloc(sizeof(*dvbdev->entity), GFP_KERNEL);
	if (!dvbdev->entity)
		return -ENOMEM;

	dvbdev->entity->name = dvbdev->name;

	if (npads) {
		dvbdev->pads = kcalloc(npads, sizeof(*dvbdev->pads),
				       GFP_KERNEL);
		if (!dvbdev->pads) {
			kfree(dvbdev->entity);
			dvbdev->entity = NULL;
			return -ENOMEM;
		}
	}

	switch (type) {
	case DVB_DEVICE_FRONTEND:
		dvbdev->entity->function = MEDIA_ENT_F_DTV_DEMOD;
		dvbdev->pads[0].flags = MEDIA_PAD_FL_SINK;
		dvbdev->pads[1].flags = MEDIA_PAD_FL_SOURCE;
		break;
	case DVB_DEVICE_DEMUX:
		dvbdev->entity->function = MEDIA_ENT_F_TS_DEMUX;
		dvbdev->pads[0].flags = MEDIA_PAD_FL_SINK;
		for (i = 1; i < npads; i++)
			dvbdev->pads[i].flags = MEDIA_PAD_FL_SOURCE;
		break;
	case DVB_DEVICE_CA:
		dvbdev->entity->function = MEDIA_ENT_F_DTV_CA;
		dvbdev->pads[0].flags = MEDIA_PAD_FL_SINK;
		dvbdev->pads[1].flags = MEDIA_PAD_FL_SOURCE;
		break;
	default:
		/* Should never happen, as the first switch prevents it */
		kfree(dvbdev->entity);
		kfree(dvbdev->pads);
		dvbdev->entity = NULL;
		dvbdev->pads = NULL;
		return 0;
	}

	if (npads) {
		ret = media_entity_pads_init(dvbdev->entity, npads, dvbdev->pads);
		if (ret)
			return ret;
	}
	ret = media_device_register_entity(dvbdev->adapter->mdev,
					   dvbdev->entity);
	if (ret)
		return ret;

	pr_info("%s: media entity '%s' registered.\n",
		__func__, dvbdev->entity->name);

	return 0;
}
#endif

static int dvb_register_media_device(struct dvb_device *dvbdev,
				     int type, int minor,
				     unsigned demux_sink_pads)
{
#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
	struct media_link *link;
	u32 intf_type;
	int ret;

	if (!dvbdev->adapter->mdev)
		return 0;

	ret = dvb_create_media_entity(dvbdev, type, demux_sink_pads);
	if (ret)
		return ret;

	switch (type) {
	case DVB_DEVICE_FRONTEND:
		intf_type = MEDIA_INTF_T_DVB_FE;
		break;
	case DVB_DEVICE_DEMUX:
		intf_type = MEDIA_INTF_T_DVB_DEMUX;
		break;
	case DVB_DEVICE_DVR:
		intf_type = MEDIA_INTF_T_DVB_DVR;
		break;
	case DVB_DEVICE_CA:
		intf_type = MEDIA_INTF_T_DVB_CA;
		break;
	case DVB_DEVICE_NET:
		intf_type = MEDIA_INTF_T_DVB_NET;
		break;
	default:
		return 0;
	}

	dvbdev->intf_devnode = media_devnode_create(dvbdev->adapter->mdev,
						    intf_type, 0,
						    DVB_MAJOR, minor);

	if (!dvbdev->intf_devnode)
		return -ENOMEM;

	/*
	 * Create the "obvious" link, e. g. the ones that represent
	 * a direct association between an interface and an entity.
	 * Other links should be created elsewhere, like:
	 *		DVB FE intf    -> tuner
	 *		DVB demux intf -> dvr
	 */

	if (!dvbdev->entity)
		return 0;

	link = media_create_intf_link(dvbdev->entity,
				      &dvbdev->intf_devnode->intf,
				      MEDIA_LNK_FL_ENABLED |
				      MEDIA_LNK_FL_IMMUTABLE);
	if (!link)
		return -ENOMEM;
#endif
	return 0;
}

int dvb_register_device(struct dvb_adapter *adap, struct dvb_device **pdvbdev,
			const struct dvb_device *template, void *priv,
			enum dvb_device_type type, int demux_sink_pads)
{
	struct dvb_device *dvbdev;
	struct file_operations *dvbdevfops;
	struct device *clsdev;
	int minor;
	int id, ret;

	mutex_lock(&dvbdev_register_lock);

	if ((id = dvbdev_get_free_id (adap, type)) < 0){
		mutex_unlock(&dvbdev_register_lock);
		*pdvbdev = NULL;
		pr_err("%s: couldn't find free device id\n", __func__);
		return -ENFILE;
	}

	*pdvbdev = dvbdev = kzalloc(sizeof(*dvbdev), GFP_KERNEL);

	if (!dvbdev){
		mutex_unlock(&dvbdev_register_lock);
		return -ENOMEM;
	}

	dvbdevfops = kmemdup(template->fops, sizeof(*dvbdevfops), GFP_KERNEL);

	if (!dvbdevfops){
		kfree (dvbdev);
		mutex_unlock(&dvbdev_register_lock);
		return -ENOMEM;
	}

	memcpy(dvbdev, template, sizeof(struct dvb_device));
	kref_init(&dvbdev->ref);
	dvbdev->type = type;
	dvbdev->id = id;
	dvbdev->adapter = adap;
	dvbdev->priv = priv;
	dvbdev->fops = dvbdevfops;
	init_waitqueue_head (&dvbdev->wait_queue);

	dvbdevfops->owner = adap->module;

	list_add_tail (&dvbdev->list_head, &adap->device_list);

	down_write(&minor_rwsem);
#ifdef CONFIG_DVB_DYNAMIC_MINORS
	for (minor = 0; minor < MAX_DVB_MINORS; minor++)
		if (dvb_minors[minor] == NULL)
			break;

	if (minor == MAX_DVB_MINORS) {
		list_del (&dvbdev->list_head);
		kfree(dvbdevfops);
		kfree(dvbdev);
		up_write(&minor_rwsem);
		mutex_unlock(&dvbdev_register_lock);
		return -EINVAL;
	}
#else
	minor = nums2minor(adap->num, type, id);
#endif

	dvbdev->minor = minor;
	dvb_minors[minor] = dvb_device_get(dvbdev);
	up_write(&minor_rwsem);

	ret = dvb_register_media_device(dvbdev, type, minor, demux_sink_pads);
	if (ret) {
		pr_err("%s: dvb_register_media_device failed to create the mediagraph\n",
		      __func__);

		dvb_media_device_free(dvbdev);
		list_del (&dvbdev->list_head);
		kfree(dvbdevfops);
		kfree(dvbdev);
		mutex_unlock(&dvbdev_register_lock);
		return ret;
	}

	mutex_unlock(&dvbdev_register_lock);

	clsdev = device_create(dvb_class, adap->device,
			       MKDEV(DVB_MAJOR, minor),
			       dvbdev, "dvb%d.%s%d", adap->num, dnames[type], id);
	if (IS_ERR(clsdev)) {
		pr_err("%s: failed to create device dvb%d.%s%d (%ld)\n",
		       __func__, adap->num, dnames[type], id, PTR_ERR(clsdev));
		dvb_media_device_free(dvbdev);
		list_del (&dvbdev->list_head);
		kfree(dvbdevfops);
		kfree(dvbdev);
		return PTR_ERR(clsdev);
	}
	dprintk("DVB: register adapter%d/%s%d @ minor: %i (0x%02x)\n",
		adap->num, dnames[type], id, minor, minor);

	return 0;
}
EXPORT_SYMBOL(dvb_register_device);


void dvb_remove_device(struct dvb_device *dvbdev)
{
	if (!dvbdev)
		return;

	down_write(&minor_rwsem);
	dvb_minors[dvbdev->minor] = NULL;
	dvb_device_put(dvbdev);
	up_write(&minor_rwsem);

	dvb_media_device_free(dvbdev);

	device_destroy(dvb_class, MKDEV(DVB_MAJOR, dvbdev->minor));

	list_del (&dvbdev->list_head);
}
EXPORT_SYMBOL(dvb_remove_device);


static void dvb_free_device(struct kref *ref)
{
	struct dvb_device *dvbdev = container_of(ref, struct dvb_device, ref);

	kfree (dvbdev->fops);
	kfree (dvbdev);
}


struct dvb_device *dvb_device_get(struct dvb_device *dvbdev)
{
	kref_get(&dvbdev->ref);
	return dvbdev;
}
EXPORT_SYMBOL(dvb_device_get);


void dvb_device_put(struct dvb_device *dvbdev)
{
	if (dvbdev)
		kref_put(&dvbdev->ref, dvb_free_device);
}


void dvb_unregister_device(struct dvb_device *dvbdev)
{
	dvb_remove_device(dvbdev);
	dvb_device_put(dvbdev);
}
EXPORT_SYMBOL(dvb_unregister_device);


#ifdef CONFIG_MEDIA_CONTROLLER_DVB

static int dvb_create_io_intf_links(struct dvb_adapter *adap,
				    struct media_interface *intf,
				    char *name)
{
	struct media_device *mdev = adap->mdev;
	struct media_entity *entity;
	struct media_link *link;

	media_device_for_each_entity(entity, mdev) {
		if (entity->function == MEDIA_ENT_F_IO_DTV) {
			if (strncmp(entity->name, name, strlen(name)))
				continue;
			link = media_create_intf_link(entity, intf,
						      MEDIA_LNK_FL_ENABLED |
						      MEDIA_LNK_FL_IMMUTABLE);
			if (!link)
				return -ENOMEM;
		}
	}
	return 0;
}

int dvb_create_media_graph(struct dvb_adapter *adap,
			   bool create_rf_connector)
{
	struct media_device *mdev = adap->mdev;
	struct media_entity *entity, *tuner = NULL, *demod = NULL, *conn;
	struct media_entity *demux = NULL, *ca = NULL;
	struct media_link *link;
	struct media_interface *intf;
	unsigned demux_pad = 0;
	unsigned dvr_pad = 0;
	unsigned ntuner = 0, ndemod = 0;
	int ret, pad_source, pad_sink;
	static const char *connector_name = "Television";

	if (!mdev)
		return 0;

	media_device_for_each_entity(entity, mdev) {
		switch (entity->function) {
		case MEDIA_ENT_F_TUNER:
			tuner = entity;
			ntuner++;
			break;
		case MEDIA_ENT_F_DTV_DEMOD:
			demod = entity;
			ndemod++;
			break;
		case MEDIA_ENT_F_TS_DEMUX:
			demux = entity;
			break;
		case MEDIA_ENT_F_DTV_CA:
			ca = entity;
			break;
		}
	}

	/*
	 * Prepare to signalize to media_create_pad_links() that multiple
	 * entities of the same type exists and a 1:n or n:1 links need to be
	 * created.
	 * NOTE: if both tuner and demod have multiple instances, it is up
	 * to the caller driver to create such links.
	 */
	if (ntuner > 1)
		tuner = NULL;
	if (ndemod > 1)
		demod = NULL;

	if (create_rf_connector) {
		conn = kzalloc(sizeof(*conn), GFP_KERNEL);
		if (!conn)
			return -ENOMEM;
		adap->conn = conn;

		adap->conn_pads = kzalloc(sizeof(*adap->conn_pads), GFP_KERNEL);
		if (!adap->conn_pads)
			return -ENOMEM;

		conn->flags = MEDIA_ENT_FL_CONNECTOR;
		conn->function = MEDIA_ENT_F_CONN_RF;
		conn->name = connector_name;
		adap->conn_pads->flags = MEDIA_PAD_FL_SOURCE;

		ret = media_entity_pads_init(conn, 1, adap->conn_pads);
		if (ret)
			return ret;

		ret = media_device_register_entity(mdev, conn);
		if (ret)
			return ret;

		if (!ntuner) {
			ret = media_create_pad_links(mdev,
						     MEDIA_ENT_F_CONN_RF,
						     conn, 0,
						     MEDIA_ENT_F_DTV_DEMOD,
						     demod, 0,
						     MEDIA_LNK_FL_ENABLED,
						     false);
		} else {
			pad_sink = media_get_pad_index(tuner, true,
						       PAD_SIGNAL_ANALOG);
			if (pad_sink < 0)
				return -EINVAL;
			ret = media_create_pad_links(mdev,
						     MEDIA_ENT_F_CONN_RF,
						     conn, 0,
						     MEDIA_ENT_F_TUNER,
						     tuner, pad_sink,
						     MEDIA_LNK_FL_ENABLED,
						     false);
		}
		if (ret)
			return ret;
	}

	if (ntuner && ndemod) {
		/* NOTE: first found tuner source pad presumed correct */
		pad_source = media_get_pad_index(tuner, false,
						 PAD_SIGNAL_ANALOG);
		if (pad_source < 0)
			return -EINVAL;
		ret = media_create_pad_links(mdev,
					     MEDIA_ENT_F_TUNER,
					     tuner, pad_source,
					     MEDIA_ENT_F_DTV_DEMOD,
					     demod, 0, MEDIA_LNK_FL_ENABLED,
					     false);
		if (ret)
			return ret;
	}

	if (ndemod && demux) {
		ret = media_create_pad_links(mdev,
					     MEDIA_ENT_F_DTV_DEMOD,
					     demod, 1,
					     MEDIA_ENT_F_TS_DEMUX,
					     demux, 0, MEDIA_LNK_FL_ENABLED,
					     false);
		if (ret)
			return ret;
	}
	if (demux && ca) {
		ret = media_create_pad_link(demux, 1, ca,
					    0, MEDIA_LNK_FL_ENABLED);
		if (ret)
			return ret;
	}

	/* Create demux links for each ringbuffer/pad */
	if (demux) {
		media_device_for_each_entity(entity, mdev) {
			if (entity->function == MEDIA_ENT_F_IO_DTV) {
				if (!strncmp(entity->name, DVR_TSOUT,
				    strlen(DVR_TSOUT))) {
					ret = media_create_pad_link(demux,
								++dvr_pad,
							    entity, 0, 0);
					if (ret)
						return ret;
				}
				if (!strncmp(entity->name, DEMUX_TSOUT,
				    strlen(DEMUX_TSOUT))) {
					ret = media_create_pad_link(demux,
							      ++demux_pad,
							    entity, 0, 0);
					if (ret)
						return ret;
				}
			}
		}
	}

	/* Create interface links for FE->tuner, DVR->demux and CA->ca */
	media_device_for_each_intf(intf, mdev) {
		if (intf->type == MEDIA_INTF_T_DVB_CA && ca) {
			link = media_create_intf_link(ca, intf,
						      MEDIA_LNK_FL_ENABLED |
						      MEDIA_LNK_FL_IMMUTABLE);
			if (!link)
				return -ENOMEM;
		}

		if (intf->type == MEDIA_INTF_T_DVB_FE && tuner) {
			link = media_create_intf_link(tuner, intf,
						      MEDIA_LNK_FL_ENABLED |
						      MEDIA_LNK_FL_IMMUTABLE);
			if (!link)
				return -ENOMEM;
		}
#if 0
		/*
		 * Indirect link - let's not create yet, as we don't know how
		 *		   to handle indirect links, nor if this will
		 *		   actually be needed.
		 */
		if (intf->type == MEDIA_INTF_T_DVB_DVR && demux) {
			link = media_create_intf_link(demux, intf,
						      MEDIA_LNK_FL_ENABLED |
						      MEDIA_LNK_FL_IMMUTABLE);
			if (!link)
				return -ENOMEM;
		}
#endif
		if (intf->type == MEDIA_INTF_T_DVB_DVR) {
			ret = dvb_create_io_intf_links(adap, intf, DVR_TSOUT);
			if (ret)
				return ret;
		}
		if (intf->type == MEDIA_INTF_T_DVB_DEMUX) {
			ret = dvb_create_io_intf_links(adap, intf, DEMUX_TSOUT);
			if (ret)
				return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(dvb_create_media_graph);
#endif

static int dvbdev_check_free_adapter_num(int num)
{
	struct list_head *entry;
	list_for_each(entry, &dvb_adapter_list) {
		struct dvb_adapter *adap;
		adap = list_entry(entry, struct dvb_adapter, list_head);
		if (adap->num == num)
			return 0;
	}
	return 1;
}

static int dvbdev_get_free_adapter_num (void)
{
	int num = 0;

	while (num < DVB_MAX_ADAPTERS) {
		if (dvbdev_check_free_adapter_num(num))
			return num;
		num++;
	}

	return -ENFILE;
}


int dvb_register_adapter(struct dvb_adapter *adap, const char *name,
			 struct module *module, struct device *device,
			 short *adapter_nums)
{
	int i, num;

	mutex_lock(&dvbdev_register_lock);

	for (i = 0; i < DVB_MAX_ADAPTERS; ++i) {
		num = adapter_nums[i];
		if (num >= 0  &&  num < DVB_MAX_ADAPTERS) {
		/* use the one the driver asked for */
			if (dvbdev_check_free_adapter_num(num))
				break;
		} else {
			num = dvbdev_get_free_adapter_num();
			break;
		}
		num = -1;
	}

	if (num < 0) {
		mutex_unlock(&dvbdev_register_lock);
		return -ENFILE;
	}

	memset (adap, 0, sizeof(struct dvb_adapter));
	INIT_LIST_HEAD (&adap->device_list);

	pr_info("DVB: registering new adapter (%s)\n", name);

	adap->num = num;
	adap->name = name;
	adap->module = module;
	adap->device = device;
	adap->mfe_shared = 0;
	adap->mfe_dvbdev = NULL;
	mutex_init (&adap->mfe_lock);

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	mutex_init(&adap->mdev_lock);
#endif

	list_add_tail (&adap->list_head, &dvb_adapter_list);

	mutex_unlock(&dvbdev_register_lock);

	return num;
}
EXPORT_SYMBOL(dvb_register_adapter);


int dvb_unregister_adapter(struct dvb_adapter *adap)
{
	mutex_lock(&dvbdev_register_lock);
	list_del (&adap->list_head);
	mutex_unlock(&dvbdev_register_lock);
	return 0;
}
EXPORT_SYMBOL(dvb_unregister_adapter);

/* if the miracle happens and "generic_usercopy()" is included into
   the kernel, then this can vanish. please don't make the mistake and
   define this as video_usercopy(). this will introduce a dependency
   to the v4l "videodev.o" module, which is unnecessary for some
   cards (ie. the budget dvb-cards don't need the v4l module...) */
int dvb_usercopy(struct file *file,
		     unsigned int cmd, unsigned long arg,
		     int (*func)(struct file *file,
		     unsigned int cmd, void *arg))
{
	char    sbuf[128];
	void    *mbuf = NULL;
	void    *parg = NULL;
	int     err  = -EINVAL;

	/*  Copy arguments into temp kernel buffer  */
	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:
		/*
		 * For this command, the pointer is actually an integer
		 * argument.
		 */
		parg = (void *) arg;
		break;
	case _IOC_READ: /* some v4l ioctls are marked wrong ... */
	case _IOC_WRITE:
	case (_IOC_WRITE | _IOC_READ):
		if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
			parg = sbuf;
		} else {
			/* too big to allocate from stack */
			mbuf = kmalloc(_IOC_SIZE(cmd), GFP_KERNEL);
			if (NULL == mbuf)
				return -ENOMEM;
			parg = mbuf;
		}

		err = -EFAULT;
		if (copy_from_user(parg, (void __user *)arg, _IOC_SIZE(cmd)))
			goto out;
		break;
	}

	/* call driver */
	if ((err = func(file, cmd, parg)) == -ENOIOCTLCMD)
		err = -ENOTTY;

	if (err < 0)
		goto out;

	/*  Copy results into user buffer  */
	switch (_IOC_DIR(cmd))
	{
	case _IOC_READ:
	case (_IOC_WRITE | _IOC_READ):
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			err = -EFAULT;
		break;
	}

out:
	kfree(mbuf);
	return err;
}

#if IS_ENABLED(CONFIG_I2C)
struct i2c_client *dvb_module_probe(const char *module_name,
				    const char *name,
				    struct i2c_adapter *adap,
				    unsigned char addr,
				    void *platform_data)
{
	struct i2c_client *client;
	struct i2c_board_info *board_info;

	board_info = kzalloc(sizeof(*board_info), GFP_KERNEL);
	if (!board_info)
		return NULL;

	if (name)
		strscpy(board_info->type, name, I2C_NAME_SIZE);
	else
		strscpy(board_info->type, module_name, I2C_NAME_SIZE);

	board_info->addr = addr;
	board_info->platform_data = platform_data;
	request_module(module_name);
	client = i2c_new_client_device(adap, board_info);
	if (!i2c_client_has_driver(client)) {
		kfree(board_info);
		return NULL;
	}

	if (!try_module_get(client->dev.driver->owner)) {
		i2c_unregister_device(client);
		client = NULL;
	}

	kfree(board_info);
	return client;
}
EXPORT_SYMBOL_GPL(dvb_module_probe);

void dvb_module_release(struct i2c_client *client)
{
	if (!client)
		return;

	module_put(client->dev.driver->owner);
	i2c_unregister_device(client);
}
EXPORT_SYMBOL_GPL(dvb_module_release);
#endif

static int dvb_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct dvb_device *dvbdev = dev_get_drvdata(dev);

	add_uevent_var(env, "DVB_ADAPTER_NUM=%d", dvbdev->adapter->num);
	add_uevent_var(env, "DVB_DEVICE_TYPE=%s", dnames[dvbdev->type]);
	add_uevent_var(env, "DVB_DEVICE_NUM=%d", dvbdev->id);
	return 0;
}

static char *dvb_devnode(const struct device *dev, umode_t *mode)
{
	const struct dvb_device *dvbdev = dev_get_drvdata(dev);

	return kasprintf(GFP_KERNEL, "dvb/adapter%d/%s%d",
		dvbdev->adapter->num, dnames[dvbdev->type], dvbdev->id);
}


static int __init init_dvbdev(void)
{
	int retval;
	dev_t dev = MKDEV(DVB_MAJOR, 0);

	if ((retval = register_chrdev_region(dev, MAX_DVB_MINORS, "DVB")) != 0) {
		pr_err("dvb-core: unable to get major %d\n", DVB_MAJOR);
		return retval;
	}

	cdev_init(&dvb_device_cdev, &dvb_device_fops);
	if ((retval = cdev_add(&dvb_device_cdev, dev, MAX_DVB_MINORS)) != 0) {
		pr_err("dvb-core: unable register character device\n");
		goto error;
	}

	dvb_class = class_create(THIS_MODULE, "dvb");
	if (IS_ERR(dvb_class)) {
		retval = PTR_ERR(dvb_class);
		goto error;
	}
	dvb_class->dev_uevent = dvb_uevent;
	dvb_class->devnode = dvb_devnode;
	return 0;

error:
	cdev_del(&dvb_device_cdev);
	unregister_chrdev_region(dev, MAX_DVB_MINORS);
	return retval;
}


static void __exit exit_dvbdev(void)
{
	class_destroy(dvb_class);
	cdev_del(&dvb_device_cdev);
	unregister_chrdev_region(MKDEV(DVB_MAJOR, 0), MAX_DVB_MINORS);
}

subsys_initcall(init_dvbdev);
module_exit(exit_dvbdev);

MODULE_DESCRIPTION("DVB Core Driver");
MODULE_AUTHOR("Marcus Metzler, Ralph Metzler, Holger Waechtler");
MODULE_LICENSE("GPL");
