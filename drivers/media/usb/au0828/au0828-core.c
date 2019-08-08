// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the Auvitek USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
 */

#include "au0828.h"
#include "au8522.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <linux/mutex.h>

/* Due to enum tuner_pad_index */
#include <media/tuner.h>

/*
 * 1 = General debug messages
 * 2 = USB handling
 * 4 = I2C related
 * 8 = Bridge related
 * 16 = IR related
 */
int au0828_debug;
module_param_named(debug, au0828_debug, int, 0644);
MODULE_PARM_DESC(debug,
		 "set debug bitmask: 1=general, 2=USB, 4=I2C, 8=bridge, 16=IR");

static unsigned int disable_usb_speed_check;
module_param(disable_usb_speed_check, int, 0444);
MODULE_PARM_DESC(disable_usb_speed_check,
		 "override min bandwidth requirement of 480M bps");

#define _AU0828_BULKPIPE 0x03
#define _BULKPIPESIZE 0xffff

static int send_control_msg(struct au0828_dev *dev, u16 request, u32 value,
			    u16 index);
static int recv_control_msg(struct au0828_dev *dev, u16 request, u32 value,
	u16 index, unsigned char *cp, u16 size);

/* USB Direction */
#define CMD_REQUEST_IN		0x00
#define CMD_REQUEST_OUT		0x01

u32 au0828_readreg(struct au0828_dev *dev, u16 reg)
{
	u8 result = 0;

	recv_control_msg(dev, CMD_REQUEST_IN, 0, reg, &result, 1);
	dprintk(8, "%s(0x%04x) = 0x%02x\n", __func__, reg, result);

	return result;
}

u32 au0828_writereg(struct au0828_dev *dev, u16 reg, u32 val)
{
	dprintk(8, "%s(0x%04x, 0x%02x)\n", __func__, reg, val);
	return send_control_msg(dev, CMD_REQUEST_OUT, val, reg);
}

static int send_control_msg(struct au0828_dev *dev, u16 request, u32 value,
	u16 index)
{
	int status = -ENODEV;

	if (dev->usbdev) {

		/* cp must be memory that has been allocated by kmalloc */
		status = usb_control_msg(dev->usbdev,
				usb_sndctrlpipe(dev->usbdev, 0),
				request,
				USB_DIR_OUT | USB_TYPE_VENDOR |
					USB_RECIP_DEVICE,
				value, index, NULL, 0, 1000);

		status = min(status, 0);

		if (status < 0) {
			pr_err("%s() Failed sending control message, error %d.\n",
				__func__, status);
		}

	}

	return status;
}

static int recv_control_msg(struct au0828_dev *dev, u16 request, u32 value,
	u16 index, unsigned char *cp, u16 size)
{
	int status = -ENODEV;
	mutex_lock(&dev->mutex);
	if (dev->usbdev) {
		status = usb_control_msg(dev->usbdev,
				usb_rcvctrlpipe(dev->usbdev, 0),
				request,
				USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				value, index,
				dev->ctrlmsg, size, 1000);

		status = min(status, 0);

		if (status < 0) {
			pr_err("%s() Failed receiving control message, error %d.\n",
				__func__, status);
		}

		/* the host controller requires heap allocated memory, which
		   is why we didn't just pass "cp" into usb_control_msg */
		memcpy(cp, dev->ctrlmsg, size);
	}
	mutex_unlock(&dev->mutex);
	return status;
}

#ifdef CONFIG_MEDIA_CONTROLLER
static void au0828_media_graph_notify(struct media_entity *new,
				      void *notify_data);
#endif

static void au0828_unregister_media_device(struct au0828_dev *dev)
{
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device *mdev = dev->media_dev;
	struct media_entity_notify *notify, *nextp;

	if (!mdev || !media_devnode_is_registered(mdev->devnode))
		return;

	/* Remove au0828 entity_notify callbacks */
	list_for_each_entry_safe(notify, nextp, &mdev->entity_notify, list) {
		if (notify->notify != au0828_media_graph_notify)
			continue;
		media_device_unregister_entity_notify(mdev, notify);
	}

	/* clear enable_source, disable_source */
	mutex_lock(&mdev->graph_mutex);
	dev->media_dev->source_priv = NULL;
	dev->media_dev->enable_source = NULL;
	dev->media_dev->disable_source = NULL;
	mutex_unlock(&mdev->graph_mutex);

	media_device_delete(dev->media_dev, KBUILD_MODNAME, THIS_MODULE);
	dev->media_dev = NULL;
#endif
}

void au0828_usb_release(struct au0828_dev *dev)
{
	au0828_unregister_media_device(dev);

	/* I2C */
	au0828_i2c_unregister(dev);

	kfree(dev);
}

static void au0828_usb_disconnect(struct usb_interface *interface)
{
	struct au0828_dev *dev = usb_get_intfdata(interface);

	dprintk(1, "%s()\n", __func__);

	/* there is a small window after disconnect, before
	   dev->usbdev is NULL, for poll (e.g: IR) try to access
	   the device and fill the dmesg with error messages.
	   Set the status so poll routines can check and avoid
	   access after disconnect.
	*/
	set_bit(DEV_DISCONNECTED, &dev->dev_state);

	au0828_rc_unregister(dev);
	/* Digital TV */
	au0828_dvb_unregister(dev);

	usb_set_intfdata(interface, NULL);
	mutex_lock(&dev->mutex);
	dev->usbdev = NULL;
	mutex_unlock(&dev->mutex);
	if (au0828_analog_unregister(dev)) {
		/*
		 * No need to call au0828_usb_release() if V4L2 is enabled,
		 * as this is already called via au0828_usb_v4l2_release()
		 */
		return;
	}
	au0828_usb_release(dev);
}

static int au0828_media_device_init(struct au0828_dev *dev,
				    struct usb_device *udev)
{
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device *mdev;

	mdev = media_device_usb_allocate(udev, KBUILD_MODNAME, THIS_MODULE);
	if (!mdev)
		return -ENOMEM;

	dev->media_dev = mdev;
#endif
	return 0;
}

#ifdef CONFIG_MEDIA_CONTROLLER
static void au0828_media_graph_notify(struct media_entity *new,
				      void *notify_data)
{
	struct au0828_dev *dev = (struct au0828_dev *) notify_data;
	int ret;
	struct media_entity *entity, *mixer = NULL, *decoder = NULL;

	if (!new) {
		/*
		 * Called during au0828 probe time to connect
		 * entities that were created prior to registering
		 * the notify handler. Find mixer and decoder.
		*/
		media_device_for_each_entity(entity, dev->media_dev) {
			if (entity->function == MEDIA_ENT_F_AUDIO_MIXER)
				mixer = entity;
			else if (entity->function == MEDIA_ENT_F_ATV_DECODER)
				decoder = entity;
		}
		goto create_link;
	}

	switch (new->function) {
	case MEDIA_ENT_F_AUDIO_MIXER:
		mixer = new;
		if (dev->decoder)
			decoder = dev->decoder;
		break;
	case MEDIA_ENT_F_ATV_DECODER:
		/* In case, Mixer is added first, find mixer and create link */
		media_device_for_each_entity(entity, dev->media_dev) {
			if (entity->function == MEDIA_ENT_F_AUDIO_MIXER)
				mixer = entity;
		}
		decoder = new;
		break;
	default:
		break;
	}

create_link:
	if (decoder && mixer) {
		ret = media_get_pad_index(decoder, false,
					  PAD_SIGNAL_AUDIO);
		if (ret >= 0)
			ret = media_create_pad_link(decoder, ret,
						    mixer, 0,
						    MEDIA_LNK_FL_ENABLED);
		if (ret < 0)
			dev_err(&dev->usbdev->dev,
				"Mixer Pad Link Create Error: %d\n", ret);
	}
}

static bool au0828_is_link_shareable(struct media_entity *owner,
				     struct media_entity *entity)
{
	bool shareable = false;

	/* Tuner link can be shared by audio, video, and VBI */
	switch (owner->function) {
	case MEDIA_ENT_F_IO_V4L:
	case MEDIA_ENT_F_AUDIO_CAPTURE:
	case MEDIA_ENT_F_IO_VBI:
		if (entity->function == MEDIA_ENT_F_IO_V4L ||
		    entity->function == MEDIA_ENT_F_AUDIO_CAPTURE ||
		    entity->function == MEDIA_ENT_F_IO_VBI)
			shareable = true;
		break;
	case MEDIA_ENT_F_DTV_DEMOD:
	default:
		break;
	}
	return shareable;
}

/* Callers should hold graph_mutex */
static int au0828_enable_source(struct media_entity *entity,
				struct media_pipeline *pipe)
{
	struct media_entity  *source, *find_source;
	struct media_entity *sink;
	struct media_link *link, *found_link = NULL;
	int ret = 0;
	struct media_device *mdev = entity->graph_obj.mdev;
	struct au0828_dev *dev;

	if (!mdev)
		return -ENODEV;

	dev = mdev->source_priv;

	/*
	 * For Audio and V4L2 entity, find the link to which decoder
	 * is the sink. Look for an active link between decoder and
	 * source (tuner/s-video/Composite), if one exists, nothing
	 * to do. If not, look for any  active links between source
	 * and any other entity. If one exists, source is busy. If
	 * source is free, setup link and start pipeline from source.
	 * For DVB FE entity, the source for the link is the tuner.
	 * Check if tuner is available and setup link and start
	 * pipeline.
	*/
	if (entity->function == MEDIA_ENT_F_DTV_DEMOD) {
		sink = entity;
		find_source = dev->tuner;
	} else {
		/* Analog isn't configured or register failed */
		if (!dev->decoder) {
			ret = -ENODEV;
			goto end;
		}

		sink = dev->decoder;

		/*
		 * Default input is tuner and default input_type
		 * is AU0828_VMUX_TELEVISION.
		 *
		 * There is a problem when s_input is called to
		 * change the default input. s_input will try to
		 * enable_source before attempting to change the
		 * input on the device, and will end up enabling
		 * default source which is tuner.
		 *
		 * Additional logic is necessary in au0828 to detect
		 * that the input has changed and enable the right
		 * source. au0828 handles this case in its s_input.
		 * It will disable the old source and enable the new
		 * source.
		 *
		*/
		if (dev->input_type == AU0828_VMUX_TELEVISION)
			find_source = dev->tuner;
		else if (dev->input_type == AU0828_VMUX_SVIDEO ||
			 dev->input_type == AU0828_VMUX_COMPOSITE)
			find_source = &dev->input_ent[dev->input_type];
		else {
			/* unknown input - let user select input */
			ret = 0;
			goto end;
		}
	}

	/* Is there an active link between sink and source */
	if (dev->active_link) {
		if (dev->active_link_owner == entity) {
			/* This check is necessary to handle multiple
			 * enable_source calls from v4l_ioctls during
			 * the course of video/vbi application run-time.
			*/
			pr_debug("%s already owns the tuner\n", entity->name);
			ret = 0;
			goto end;
		} else if (au0828_is_link_shareable(dev->active_link_owner,
			   entity)) {
			/* Either ALSA or Video own tuner. Sink is the same
			 * for both. Allow sharing the active link between
			 * their common source (tuner) and sink (decoder).
			 * Starting pipeline between sharing entity and sink
			 * will fail with pipe mismatch, while owner has an
			 * active pipeline. Switch pipeline ownership from
			 * user to owner when owner disables the source.
			 */
			dev->active_link_shared = true;
			/* save the user info to use from disable */
			dev->active_link_user = entity;
			dev->active_link_user_pipe = pipe;
			pr_debug("%s owns the tuner %s can share!\n",
				 dev->active_link_owner->name,
				 entity->name);
			ret = 0;
			goto end;
		} else {
			ret = -EBUSY;
			goto end;
		}
	}

	list_for_each_entry(link, &sink->links, list) {
		/* Check sink, and source */
		if (link->sink->entity == sink &&
		    link->source->entity == find_source) {
			found_link = link;
			break;
		}
	}

	if (!found_link) {
		ret = -ENODEV;
		goto end;
	}

	/* activate link between source and sink and start pipeline */
	source = found_link->source->entity;
	ret = __media_entity_setup_link(found_link, MEDIA_LNK_FL_ENABLED);
	if (ret) {
		pr_err("Activate link from %s->%s. Error %d\n",
			source->name, sink->name, ret);
		goto end;
	}

	ret = __media_pipeline_start(entity, pipe);
	if (ret) {
		pr_err("Start Pipeline: %s->%s Error %d\n",
			source->name, entity->name, ret);
		ret = __media_entity_setup_link(found_link, 0);
		if (ret)
			pr_err("Deactivate link Error %d\n", ret);
		goto end;
	}

	/* save link state to allow audio and video share the link
	 * and not disable the link while the other is using it.
	 * active_link_owner is used to deactivate the link.
	*/
	dev->active_link = found_link;
	dev->active_link_owner = entity;
	dev->active_source = source;
	dev->active_sink = sink;

	pr_info("Enabled Source: %s->%s->%s Ret %d\n",
		 dev->active_source->name, dev->active_sink->name,
		 dev->active_link_owner->name, ret);
end:
	pr_debug("%s end: ent:%s fnc:%d ret %d\n",
		 __func__, entity->name, entity->function, ret);
	return ret;
}

/* Callers should hold graph_mutex */
static void au0828_disable_source(struct media_entity *entity)
{
	int ret = 0;
	struct media_device *mdev = entity->graph_obj.mdev;
	struct au0828_dev *dev;

	if (!mdev)
		return;

	dev = mdev->source_priv;

	if (!dev->active_link)
		return;

	/* link is active - stop pipeline from source
	 * (tuner/s-video/Composite) to the entity
	 * When DVB/s-video/Composite owns tuner, it won't be in
	 * shared state.
	 */
	if (dev->active_link->sink->entity == dev->active_sink &&
	    dev->active_link->source->entity == dev->active_source) {
		/*
		 * Prevent video from deactivating link when audio
		 * has active pipeline and vice versa. In addition
		 * handle the case when more than one video/vbi
		 * application is sharing the link.
		*/
		bool owner_is_audio = false;

		if (dev->active_link_owner->function ==
		    MEDIA_ENT_F_AUDIO_CAPTURE)
			owner_is_audio = true;

		if (dev->active_link_shared) {
			pr_debug("Shared link owner %s user %s %d\n",
				 dev->active_link_owner->name,
				 entity->name, dev->users);

			/* Handle video device users > 1
			 * When audio owns the shared link with
			 * more than one video users, avoid
			 * disabling the source and/or switching
			 * the owner until the last disable_source
			 * call from video _close(). Use dev->users to
			 * determine when to switch/disable.
			 */
			if (dev->active_link_owner != entity) {
				/* video device has users > 1 */
				if (owner_is_audio && dev->users > 1)
					return;

				dev->active_link_user = NULL;
				dev->active_link_user_pipe = NULL;
				dev->active_link_shared = false;
				return;
			}

			/* video owns the link and has users > 1 */
			if (!owner_is_audio && dev->users > 1)
				return;

			/* stop pipeline */
			__media_pipeline_stop(dev->active_link_owner);
			pr_debug("Pipeline stop for %s\n",
				dev->active_link_owner->name);

			ret = __media_pipeline_start(
					dev->active_link_user,
					dev->active_link_user_pipe);
			if (ret) {
				pr_err("Start Pipeline: %s->%s %d\n",
					dev->active_source->name,
					dev->active_link_user->name,
					ret);
				goto deactivate_link;
			}
			/* link user is now the owner */
			dev->active_link_owner = dev->active_link_user;
			dev->active_link_user = NULL;
			dev->active_link_user_pipe = NULL;
			dev->active_link_shared = false;

			pr_debug("Pipeline started for %s\n",
				dev->active_link_owner->name);
			return;
		} else if (!owner_is_audio && dev->users > 1)
			/* video/vbi owns the link and has users > 1 */
			return;

		if (dev->active_link_owner != entity)
			return;

		/* stop pipeline */
		__media_pipeline_stop(dev->active_link_owner);
		pr_debug("Pipeline stop for %s\n",
			dev->active_link_owner->name);

deactivate_link:
		ret = __media_entity_setup_link(dev->active_link, 0);
		if (ret)
			pr_err("Deactivate link Error %d\n", ret);

		pr_info("Disabled Source: %s->%s->%s Ret %d\n",
			 dev->active_source->name, dev->active_sink->name,
			 dev->active_link_owner->name, ret);

		dev->active_link = NULL;
		dev->active_link_owner = NULL;
		dev->active_source = NULL;
		dev->active_sink = NULL;
		dev->active_link_shared = false;
		dev->active_link_user = NULL;
	}
}
#endif

static int au0828_media_device_register(struct au0828_dev *dev,
					struct usb_device *udev)
{
#ifdef CONFIG_MEDIA_CONTROLLER
	int ret;
	struct media_entity *entity, *demod = NULL;
	struct media_link *link;

	if (!dev->media_dev)
		return 0;

	if (!media_devnode_is_registered(dev->media_dev->devnode)) {

		/* register media device */
		ret = media_device_register(dev->media_dev);
		if (ret) {
			media_device_delete(dev->media_dev, KBUILD_MODNAME,
					    THIS_MODULE);
			dev->media_dev = NULL;
			dev_err(&udev->dev,
				"Media Device Register Error: %d\n", ret);
			return ret;
		}
	} else {
		/*
		 * Call au0828_media_graph_notify() to connect
		 * audio graph to our graph. In this case, audio
		 * driver registered the device and there is no
		 * entity_notify to be called when new entities
		 * are added. Invoke it now.
		*/
		au0828_media_graph_notify(NULL, (void *) dev);
	}

	/*
	 * Find tuner, decoder and demod.
	 *
	 * The tuner and decoder should be cached, as they'll be used by
	 *	au0828_enable_source.
	 *
	 * It also needs to disable the link between tuner and
	 * decoder/demod, to avoid disable step when tuner is requested
	 * by video or audio. Note that this step can't be done until dvb
	 * graph is created during dvb register.
	*/
	media_device_for_each_entity(entity, dev->media_dev) {
		switch (entity->function) {
		case MEDIA_ENT_F_TUNER:
			dev->tuner = entity;
			break;
		case MEDIA_ENT_F_ATV_DECODER:
			dev->decoder = entity;
			break;
		case MEDIA_ENT_F_DTV_DEMOD:
			demod = entity;
			break;
		}
	}

	/* Disable link between tuner->demod and/or tuner->decoder */
	if (dev->tuner) {
		list_for_each_entry(link, &dev->tuner->links, list) {
			if (demod && link->sink->entity == demod)
				media_entity_setup_link(link, 0);
			if (dev->decoder && link->sink->entity == dev->decoder)
				media_entity_setup_link(link, 0);
		}
	}

	/* register entity_notify callback */
	dev->entity_notify.notify_data = (void *) dev;
	dev->entity_notify.notify = (void *) au0828_media_graph_notify;
	ret = media_device_register_entity_notify(dev->media_dev,
						  &dev->entity_notify);
	if (ret) {
		dev_err(&udev->dev,
			"Media Device register entity_notify Error: %d\n",
			ret);
		return ret;
	}
	/* set enable_source */
	mutex_lock(&dev->media_dev->graph_mutex);
	dev->media_dev->source_priv = (void *) dev;
	dev->media_dev->enable_source = au0828_enable_source;
	dev->media_dev->disable_source = au0828_disable_source;
	mutex_unlock(&dev->media_dev->graph_mutex);
#endif
	return 0;
}

static int au0828_usb_probe(struct usb_interface *interface,
	const struct usb_device_id *id)
{
	int ifnum;
	int retval = 0;

	struct au0828_dev *dev;
	struct usb_device *usbdev = interface_to_usbdev(interface);

	ifnum = interface->altsetting->desc.bInterfaceNumber;

	if (ifnum != 0)
		return -ENODEV;

	dprintk(1, "%s() vendor id 0x%x device id 0x%x ifnum:%d\n", __func__,
		le16_to_cpu(usbdev->descriptor.idVendor),
		le16_to_cpu(usbdev->descriptor.idProduct),
		ifnum);

	/*
	 * Make sure we have 480 Mbps of bandwidth, otherwise things like
	 * video stream wouldn't likely work, since 12 Mbps is generally
	 * not enough even for most Digital TV streams.
	 */
	if (usbdev->speed != USB_SPEED_HIGH && disable_usb_speed_check == 0) {
		pr_err("au0828: Device initialization failed.\n");
		pr_err("au0828: Device must be connected to a high-speed USB 2.0 port.\n");
		return -ENODEV;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		pr_err("%s() Unable to allocate memory\n", __func__);
		return -ENOMEM;
	}

	mutex_init(&dev->lock);
	mutex_lock(&dev->lock);
	mutex_init(&dev->mutex);
	mutex_init(&dev->dvb.lock);
	dev->usbdev = usbdev;
	dev->boardnr = id->driver_info;
	dev->board = au0828_boards[dev->boardnr];

	/* Initialize the media controller */
	retval = au0828_media_device_init(dev, usbdev);
	if (retval) {
		pr_err("%s() au0828_media_device_init failed\n",
		       __func__);
		mutex_unlock(&dev->lock);
		kfree(dev);
		return retval;
	}

	retval = au0828_v4l2_device_register(interface, dev);
	if (retval) {
		au0828_usb_v4l2_media_release(dev);
		mutex_unlock(&dev->lock);
		kfree(dev);
		return retval;
	}

	/* Power Up the bridge */
	au0828_write(dev, REG_600, 1 << 4);

	/* Bring up the GPIO's and supporting devices */
	au0828_gpio_setup(dev);

	/* I2C */
	au0828_i2c_register(dev);

	/* Setup */
	au0828_card_setup(dev);

	/* Analog TV */
	retval = au0828_analog_register(dev, interface);
	if (retval) {
		pr_err("%s() au0828_analog_register failed to register on V4L2\n",
			__func__);
		mutex_unlock(&dev->lock);
		goto done;
	}

	/* Digital TV */
	retval = au0828_dvb_register(dev);
	if (retval)
		pr_err("%s() au0828_dvb_register failed\n",
		       __func__);

	/* Remote controller */
	au0828_rc_register(dev);

	/*
	 * Store the pointer to the au0828_dev so it can be accessed in
	 * au0828_usb_disconnect
	 */
	usb_set_intfdata(interface, dev);

	pr_info("Registered device AU0828 [%s]\n",
		dev->board.name == NULL ? "Unset" : dev->board.name);

	mutex_unlock(&dev->lock);

	retval = au0828_media_device_register(dev, usbdev);

done:
	if (retval < 0)
		au0828_usb_disconnect(interface);

	return retval;
}

static int au0828_suspend(struct usb_interface *interface,
				pm_message_t message)
{
	struct au0828_dev *dev = usb_get_intfdata(interface);

	if (!dev)
		return 0;

	pr_info("Suspend\n");

	au0828_rc_suspend(dev);
	au0828_v4l2_suspend(dev);
	au0828_dvb_suspend(dev);

	/* FIXME: should suspend also ATV/DTV */

	return 0;
}

static int au0828_resume(struct usb_interface *interface)
{
	struct au0828_dev *dev = usb_get_intfdata(interface);
	if (!dev)
		return 0;

	pr_info("Resume\n");

	/* Power Up the bridge */
	au0828_write(dev, REG_600, 1 << 4);

	/* Bring up the GPIO's and supporting devices */
	au0828_gpio_setup(dev);

	au0828_rc_resume(dev);
	au0828_v4l2_resume(dev);
	au0828_dvb_resume(dev);

	/* FIXME: should resume also ATV/DTV */

	return 0;
}

static struct usb_driver au0828_usb_driver = {
	.name		= KBUILD_MODNAME,
	.probe		= au0828_usb_probe,
	.disconnect	= au0828_usb_disconnect,
	.id_table	= au0828_usb_id_table,
	.suspend	= au0828_suspend,
	.resume		= au0828_resume,
	.reset_resume	= au0828_resume,
};

static int __init au0828_init(void)
{
	int ret;

	if (au0828_debug & 1)
		pr_info("%s() Debugging is enabled\n", __func__);

	if (au0828_debug & 2)
		pr_info("%s() USB Debugging is enabled\n", __func__);

	if (au0828_debug & 4)
		pr_info("%s() I2C Debugging is enabled\n", __func__);

	if (au0828_debug & 8)
		pr_info("%s() Bridge Debugging is enabled\n",
		       __func__);

	if (au0828_debug & 16)
		pr_info("%s() IR Debugging is enabled\n",
		       __func__);

	pr_info("au0828 driver loaded\n");

	ret = usb_register(&au0828_usb_driver);
	if (ret)
		pr_err("usb_register failed, error = %d\n", ret);

	return ret;
}

static void __exit au0828_exit(void)
{
	usb_deregister(&au0828_usb_driver);
}

module_init(au0828_init);
module_exit(au0828_exit);

MODULE_DESCRIPTION("Driver for Auvitek AU0828 based products");
MODULE_AUTHOR("Steven Toth <stoth@linuxtv.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.0.3");
