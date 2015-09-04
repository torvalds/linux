/*
 * Media device
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/compat.h>
#include <linux/export.h>
#include <linux/ioctl.h>
#include <linux/media.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <media/media-device.h>
#include <media/media-devnode.h>
#include <media/media-entity.h>

#ifdef CONFIG_MEDIA_CONTROLLER

/* -----------------------------------------------------------------------------
 * Userspace API
 */

static int media_device_open(struct file *filp)
{
	return 0;
}

static int media_device_close(struct file *filp)
{
	return 0;
}

static int media_device_get_info(struct media_device *dev,
				 struct media_device_info __user *__info)
{
	struct media_device_info info;

	memset(&info, 0, sizeof(info));

	strlcpy(info.driver, dev->dev->driver->name, sizeof(info.driver));
	strlcpy(info.model, dev->model, sizeof(info.model));
	strlcpy(info.serial, dev->serial, sizeof(info.serial));
	strlcpy(info.bus_info, dev->bus_info, sizeof(info.bus_info));

	info.media_version = MEDIA_API_VERSION;
	info.hw_revision = dev->hw_revision;
	info.driver_version = dev->driver_version;

	if (copy_to_user(__info, &info, sizeof(*__info)))
		return -EFAULT;
	return 0;
}

static struct media_entity *find_entity(struct media_device *mdev, u32 id)
{
	struct media_entity *entity;
	int next = id & MEDIA_ENT_ID_FLAG_NEXT;

	id &= ~MEDIA_ENT_ID_FLAG_NEXT;

	spin_lock(&mdev->lock);

	media_device_for_each_entity(entity, mdev) {
		if (((media_entity_id(entity) == id) && !next) ||
		    ((media_entity_id(entity) > id) && next)) {
			spin_unlock(&mdev->lock);
			return entity;
		}
	}

	spin_unlock(&mdev->lock);

	return NULL;
}

static long media_device_enum_entities(struct media_device *mdev,
				       struct media_entity_desc __user *uent)
{
	struct media_entity *ent;
	struct media_entity_desc u_ent;

	memset(&u_ent, 0, sizeof(u_ent));
	if (copy_from_user(&u_ent.id, &uent->id, sizeof(u_ent.id)))
		return -EFAULT;

	ent = find_entity(mdev, u_ent.id);

	if (ent == NULL)
		return -EINVAL;

	u_ent.id = media_entity_id(ent);
	if (ent->name)
		strlcpy(u_ent.name, ent->name, sizeof(u_ent.name));
	u_ent.type = ent->type;
	u_ent.revision = ent->revision;
	u_ent.flags = ent->flags;
	u_ent.group_id = ent->group_id;
	u_ent.pads = ent->num_pads;
	u_ent.links = ent->num_links - ent->num_backlinks;
	memcpy(&u_ent.raw, &ent->info, sizeof(ent->info));
	if (copy_to_user(uent, &u_ent, sizeof(u_ent)))
		return -EFAULT;
	return 0;
}

static void media_device_kpad_to_upad(const struct media_pad *kpad,
				      struct media_pad_desc *upad)
{
	upad->entity = media_entity_id(kpad->entity);
	upad->index = kpad->index;
	upad->flags = kpad->flags;
}

static long __media_device_enum_links(struct media_device *mdev,
				      struct media_links_enum *links)
{
	struct media_entity *entity;

	entity = find_entity(mdev, links->entity);
	if (entity == NULL)
		return -EINVAL;

	if (links->pads) {
		unsigned int p;

		for (p = 0; p < entity->num_pads; p++) {
			struct media_pad_desc pad;

			memset(&pad, 0, sizeof(pad));
			media_device_kpad_to_upad(&entity->pads[p], &pad);
			if (copy_to_user(&links->pads[p], &pad, sizeof(pad)))
				return -EFAULT;
		}
	}

	if (links->links) {
		struct media_link *ent_link;
		struct media_link_desc __user *ulink = links->links;

		list_for_each_entry(ent_link, &entity->links, list) {
			struct media_link_desc link;

			/* Ignore backlinks. */
			if (ent_link->source->entity != entity)
				continue;
			memset(&link, 0, sizeof(link));
			media_device_kpad_to_upad(ent_link->source,
						  &link.source);
			media_device_kpad_to_upad(ent_link->sink,
						  &link.sink);
			link.flags = ent_link->flags;
			if (copy_to_user(ulink, &link, sizeof(*ulink)))
				return -EFAULT;
			ulink++;
		}
	}

	return 0;
}

static long media_device_enum_links(struct media_device *mdev,
				    struct media_links_enum __user *ulinks)
{
	struct media_links_enum links;
	int rval;

	if (copy_from_user(&links, ulinks, sizeof(links)))
		return -EFAULT;

	rval = __media_device_enum_links(mdev, &links);
	if (rval < 0)
		return rval;

	if (copy_to_user(ulinks, &links, sizeof(*ulinks)))
		return -EFAULT;

	return 0;
}

static long media_device_setup_link(struct media_device *mdev,
				    struct media_link_desc __user *_ulink)
{
	struct media_link *link = NULL;
	struct media_link_desc ulink;
	struct media_entity *source;
	struct media_entity *sink;
	int ret;

	if (copy_from_user(&ulink, _ulink, sizeof(ulink)))
		return -EFAULT;

	/* Find the source and sink entities and link.
	 */
	source = find_entity(mdev, ulink.source.entity);
	sink = find_entity(mdev, ulink.sink.entity);

	if (source == NULL || sink == NULL)
		return -EINVAL;

	if (ulink.source.index >= source->num_pads ||
	    ulink.sink.index >= sink->num_pads)
		return -EINVAL;

	link = media_entity_find_link(&source->pads[ulink.source.index],
				      &sink->pads[ulink.sink.index]);
	if (link == NULL)
		return -EINVAL;

	/* Setup the link on both entities. */
	ret = __media_entity_setup_link(link, ulink.flags);

	if (copy_to_user(_ulink, &ulink, sizeof(ulink)))
		return -EFAULT;

	return ret;
}

static long __media_device_get_topology(struct media_device *mdev,
				      struct media_v2_topology *topo)
{
	struct media_entity *entity;
	struct media_interface *intf;
	struct media_pad *pad;
	struct media_link *link;
	struct media_v2_entity uentity;
	struct media_v2_interface uintf;
	struct media_v2_pad upad;
	struct media_v2_link ulink;
	int ret = 0, i;

	topo->topology_version = mdev->topology_version;

	/* Get entities and number of entities */
	i = 0;
	media_device_for_each_entity(entity, mdev) {
		i++;

		if (ret || !topo->entities)
			continue;

		if (i > topo->num_entities) {
			ret = -ENOSPC;
			continue;
		}

		/* Copy fields to userspace struct if not error */
		memset(&uentity, 0, sizeof(uentity));
		uentity.id = entity->graph_obj.id;
		strncpy(uentity.name, entity->name,
			sizeof(uentity.name));

		if (copy_to_user(&topo->entities[i - 1], &uentity, sizeof(uentity)))
			ret = -EFAULT;
	}
	topo->num_entities = i;

	/* Get interfaces and number of interfaces */
	i = 0;
	media_device_for_each_intf(intf, mdev) {
		i++;

		if (ret || !topo->interfaces)
			continue;

		if (i > topo->num_interfaces) {
			ret = -ENOSPC;
			continue;
		}

		memset(&uintf, 0, sizeof(uintf));

		/* Copy intf fields to userspace struct */
		uintf.id = intf->graph_obj.id;
		uintf.intf_type = intf->type;
		uintf.flags = intf->flags;

		if (media_type(&intf->graph_obj) == MEDIA_GRAPH_INTF_DEVNODE) {
			struct media_intf_devnode *devnode;

			devnode = intf_to_devnode(intf);

			uintf.devnode.major = devnode->major;
			uintf.devnode.minor = devnode->minor;
		}

		if (copy_to_user(&topo->interfaces[i - 1], &uintf, sizeof(uintf)))
			ret = -EFAULT;
	}
	topo->num_interfaces = i;

	/* Get pads and number of pads */
	i = 0;
	media_device_for_each_pad(pad, mdev) {
		i++;

		if (ret || !topo->pads)
			continue;

		if (i > topo->num_pads) {
			ret = -ENOSPC;
			continue;
		}

		memset(&upad, 0, sizeof(upad));

		/* Copy pad fields to userspace struct */
		upad.id = pad->graph_obj.id;
		upad.entity_id = pad->entity->graph_obj.id;
		upad.flags = pad->flags;

		if (copy_to_user(&topo->pads[i - 1], &upad, sizeof(upad)))
			ret = -EFAULT;
	}
	topo->num_pads = i;

	/* Get links and number of links */
	i = 0;
	media_device_for_each_link(link, mdev) {
		i++;

		if (ret || !topo->links)
			continue;

		if (i > topo->num_links) {
			ret = -ENOSPC;
			continue;
		}

		memset(&ulink, 0, sizeof(ulink));

		/* Copy link fields to userspace struct */
		ulink.id = link->graph_obj.id;
		ulink.source_id = link->gobj0->id;
		ulink.sink_id = link->gobj1->id;
		ulink.flags = link->flags;

		if (media_type(link->gobj0) != MEDIA_GRAPH_PAD)
			ulink.flags |= MEDIA_LNK_FL_INTERFACE_LINK;

		if (copy_to_user(&topo->links[i - 1], &ulink, sizeof(ulink)))
			ret = -EFAULT;
	}
	topo->num_links = i;

	return ret;
}

static long media_device_get_topology(struct media_device *mdev,
				      struct media_v2_topology __user *utopo)
{
	struct media_v2_topology ktopo;
	int ret;

	ret = copy_from_user(&ktopo, utopo, sizeof(ktopo));

	if (ret < 0)
		return ret;

	ret = __media_device_get_topology(mdev, &ktopo);
	if (ret < 0)
		return ret;

	ret = copy_to_user(utopo, &ktopo, sizeof(*utopo));

	return ret;
}

static long media_device_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct media_devnode *devnode = media_devnode_data(filp);
	struct media_device *dev = to_media_device(devnode);
	long ret;

	switch (cmd) {
	case MEDIA_IOC_DEVICE_INFO:
		ret = media_device_get_info(dev,
				(struct media_device_info __user *)arg);
		break;

	case MEDIA_IOC_ENUM_ENTITIES:
		ret = media_device_enum_entities(dev,
				(struct media_entity_desc __user *)arg);
		break;

	case MEDIA_IOC_ENUM_LINKS:
		mutex_lock(&dev->graph_mutex);
		ret = media_device_enum_links(dev,
				(struct media_links_enum __user *)arg);
		mutex_unlock(&dev->graph_mutex);
		break;

	case MEDIA_IOC_SETUP_LINK:
		mutex_lock(&dev->graph_mutex);
		ret = media_device_setup_link(dev,
				(struct media_link_desc __user *)arg);
		mutex_unlock(&dev->graph_mutex);
		break;

	case MEDIA_IOC_G_TOPOLOGY:
		mutex_lock(&dev->graph_mutex);
		ret = media_device_get_topology(dev,
				(struct media_v2_topology __user *)arg);
		mutex_unlock(&dev->graph_mutex);
		break;

	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

#ifdef CONFIG_COMPAT

struct media_links_enum32 {
	__u32 entity;
	compat_uptr_t pads; /* struct media_pad_desc * */
	compat_uptr_t links; /* struct media_link_desc * */
	__u32 reserved[4];
};

static long media_device_enum_links32(struct media_device *mdev,
				      struct media_links_enum32 __user *ulinks)
{
	struct media_links_enum links;
	compat_uptr_t pads_ptr, links_ptr;

	memset(&links, 0, sizeof(links));

	if (get_user(links.entity, &ulinks->entity)
	    || get_user(pads_ptr, &ulinks->pads)
	    || get_user(links_ptr, &ulinks->links))
		return -EFAULT;

	links.pads = compat_ptr(pads_ptr);
	links.links = compat_ptr(links_ptr);

	return __media_device_enum_links(mdev, &links);
}

#define MEDIA_IOC_ENUM_LINKS32		_IOWR('|', 0x02, struct media_links_enum32)

static long media_device_compat_ioctl(struct file *filp, unsigned int cmd,
				      unsigned long arg)
{
	struct media_devnode *devnode = media_devnode_data(filp);
	struct media_device *dev = to_media_device(devnode);
	long ret;

	switch (cmd) {
	case MEDIA_IOC_DEVICE_INFO:
	case MEDIA_IOC_ENUM_ENTITIES:
	case MEDIA_IOC_SETUP_LINK:
	case MEDIA_IOC_G_TOPOLOGY:
		return media_device_ioctl(filp, cmd, arg);

	case MEDIA_IOC_ENUM_LINKS32:
		mutex_lock(&dev->graph_mutex);
		ret = media_device_enum_links32(dev,
				(struct media_links_enum32 __user *)arg);
		mutex_unlock(&dev->graph_mutex);
		break;

	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}
#endif /* CONFIG_COMPAT */

static const struct media_file_operations media_device_fops = {
	.owner = THIS_MODULE,
	.open = media_device_open,
	.ioctl = media_device_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = media_device_compat_ioctl,
#endif /* CONFIG_COMPAT */
	.release = media_device_close,
};

/* -----------------------------------------------------------------------------
 * sysfs
 */

static ssize_t show_model(struct device *cd,
			  struct device_attribute *attr, char *buf)
{
	struct media_device *mdev = to_media_device(to_media_devnode(cd));

	return sprintf(buf, "%.*s\n", (int)sizeof(mdev->model), mdev->model);
}

static DEVICE_ATTR(model, S_IRUGO, show_model, NULL);

/* -----------------------------------------------------------------------------
 * Registration/unregistration
 */

static void media_device_release(struct media_devnode *mdev)
{
	dev_dbg(mdev->parent, "Media device released\n");
}

/**
 * media_device_register - register a media device
 * @mdev:	The media device
 *
 * The caller is responsible for initializing the media device before
 * registration. The following fields must be set:
 *
 * - dev must point to the parent device
 * - model must be filled with the device model name
 */
int __must_check __media_device_register(struct media_device *mdev,
					 struct module *owner)
{
	int ret;

	if (WARN_ON(mdev->dev == NULL || mdev->model[0] == 0))
		return -EINVAL;

	INIT_LIST_HEAD(&mdev->entities);
	INIT_LIST_HEAD(&mdev->interfaces);
	INIT_LIST_HEAD(&mdev->pads);
	INIT_LIST_HEAD(&mdev->links);
	spin_lock_init(&mdev->lock);
	mutex_init(&mdev->graph_mutex);

	/* Register the device node. */
	mdev->devnode.fops = &media_device_fops;
	mdev->devnode.parent = mdev->dev;
	mdev->devnode.release = media_device_release;
	ret = media_devnode_register(&mdev->devnode, owner);
	if (ret < 0)
		return ret;

	ret = device_create_file(&mdev->devnode.dev, &dev_attr_model);
	if (ret < 0) {
		media_devnode_unregister(&mdev->devnode);
		return ret;
	}

	dev_dbg(mdev->dev, "Media device registered\n");

	return 0;
}
EXPORT_SYMBOL_GPL(__media_device_register);

/**
 * media_device_unregister - unregister a media device
 * @mdev:	The media device
 *
 */
void media_device_unregister(struct media_device *mdev)
{
	struct media_entity *entity;
	struct media_entity *next;
	struct media_interface *intf, *tmp_intf;

	/* Remove all entities from the media device */
	list_for_each_entry_safe(entity, next, &mdev->entities, graph_obj.list)
		media_device_unregister_entity(entity);

	/* Remove all interfaces from the media device */
	spin_lock(&mdev->lock);
	list_for_each_entry_safe(intf, tmp_intf, &mdev->interfaces,
				 graph_obj.list) {
		__media_remove_intf_links(intf);
		media_gobj_remove(&intf->graph_obj);
		kfree(intf);
	}
	spin_unlock(&mdev->lock);

	device_remove_file(&mdev->devnode.dev, &dev_attr_model);
	media_devnode_unregister(&mdev->devnode);

	dev_dbg(mdev->dev, "Media device unregistered\n");
}
EXPORT_SYMBOL_GPL(media_device_unregister);

/**
 * media_device_register_entity - Register an entity with a media device
 * @mdev:	The media device
 * @entity:	The entity
 */
int __must_check media_device_register_entity(struct media_device *mdev,
					      struct media_entity *entity)
{
	int i;

	if (entity->type == MEDIA_ENT_T_V4L2_SUBDEV_UNKNOWN ||
	    entity->type == MEDIA_ENT_T_UNKNOWN)
		dev_warn(mdev->dev,
			 "Entity type for entity %s was not initialized!\n",
			 entity->name);

	/* Warn if we apparently re-register an entity */
	WARN_ON(entity->graph_obj.mdev != NULL);
	entity->graph_obj.mdev = mdev;
	INIT_LIST_HEAD(&entity->links);

	spin_lock(&mdev->lock);
	/* Initialize media_gobj embedded at the entity */
	media_gobj_init(mdev, MEDIA_GRAPH_ENTITY, &entity->graph_obj);

	/* Initialize objects at the pads */
	for (i = 0; i < entity->num_pads; i++)
		media_gobj_init(mdev, MEDIA_GRAPH_PAD,
			       &entity->pads[i].graph_obj);

	spin_unlock(&mdev->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(media_device_register_entity);

/**
 * media_device_unregister_entity - Unregister an entity
 * @entity:	The entity
 *
 * If the entity has never been registered this function will return
 * immediately.
 */
void media_device_unregister_entity(struct media_entity *entity)
{
	int i;
	struct media_device *mdev = entity->graph_obj.mdev;
	struct media_link *link, *tmp;
	struct media_interface *intf;

	if (mdev == NULL)
		return;

	spin_lock(&mdev->lock);

	/* Remove all interface links pointing to this entity */
	list_for_each_entry(intf, &mdev->interfaces, graph_obj.list) {
		list_for_each_entry_safe(link, tmp, &intf->links, list) {
			if (link->entity == entity)
				__media_remove_intf_link(link);
		}
	}

	/* Remove all data links that belong to this entity */
	__media_entity_remove_links(entity);

	/* Remove all pads that belong to this entity */
	for (i = 0; i < entity->num_pads; i++)
		media_gobj_remove(&entity->pads[i].graph_obj);

	/* Remove the entity */
	media_gobj_remove(&entity->graph_obj);

	spin_unlock(&mdev->lock);
	entity->graph_obj.mdev = NULL;
}
EXPORT_SYMBOL_GPL(media_device_unregister_entity);

static void media_device_release_devres(struct device *dev, void *res)
{
}

/*
 * media_device_get_devres() -	get media device as device resource
 *				creates if one doesn't exist
*/
struct media_device *media_device_get_devres(struct device *dev)
{
	struct media_device *mdev;

	mdev = devres_find(dev, media_device_release_devres, NULL, NULL);
	if (mdev)
		return mdev;

	mdev = devres_alloc(media_device_release_devres,
				sizeof(struct media_device), GFP_KERNEL);
	if (!mdev)
		return NULL;
	return devres_get(dev, mdev, NULL, NULL);
}
EXPORT_SYMBOL_GPL(media_device_get_devres);

/*
 * media_device_find_devres() - find media device as device resource
*/
struct media_device *media_device_find_devres(struct device *dev)
{
	return devres_find(dev, media_device_release_devres, NULL, NULL);
}
EXPORT_SYMBOL_GPL(media_device_find_devres);

#endif /* CONFIG_MEDIA_CONTROLLER */
