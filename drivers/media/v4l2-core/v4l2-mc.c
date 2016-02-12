/*
 * Media Controller ancillary functions
 *
 * (c) 2016 Mauro Carvalho Chehab <mchehab@osg.samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/usb.h>
#include <media/media-entity.h>
#include <media/v4l2-mc.h>


struct media_device *v4l2_mc_pci_media_device_init(struct pci_dev *pci_dev,
						   const char *name)
{
#ifdef CONFIG_PCI
	struct media_device *mdev;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return NULL;

	mdev->dev = &pci_dev->dev;

	if (name)
		strlcpy(mdev->model, name, sizeof(mdev->model));
	else
		strlcpy(mdev->model, pci_name(pci_dev), sizeof(mdev->model));

	sprintf(mdev->bus_info, "PCI:%s", pci_name(pci_dev));

	mdev->hw_revision = pci_dev->subsystem_vendor << 16
			    || pci_dev->subsystem_device;

	mdev->driver_version = LINUX_VERSION_CODE;

	media_device_init(mdev);

	return mdev;
#else
	return NULL;
#endif
}
EXPORT_SYMBOL_GPL(v4l2_mc_pci_media_device_init);

struct media_device *__v4l2_mc_usb_media_device_init(struct usb_device *udev,
						     const char *board_name,
						     const char *driver_name)
{
#ifdef CONFIG_USB
	struct media_device *mdev;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return NULL;

	mdev->dev = &udev->dev;

	if (driver_name)
		strlcpy(mdev->driver_name, driver_name,
			sizeof(mdev->driver_name));

	if (board_name)
		strlcpy(mdev->model, board_name, sizeof(mdev->model));
	else if (udev->product)
		strlcpy(mdev->model, udev->product, sizeof(mdev->model));
	else
		strlcpy(mdev->model, "unknown model", sizeof(mdev->model));
	if (udev->serial)
		strlcpy(mdev->serial, udev->serial, sizeof(mdev->serial));
	usb_make_path(udev, mdev->bus_info, sizeof(mdev->bus_info));
	mdev->hw_revision = le16_to_cpu(udev->descriptor.bcdDevice);
	mdev->driver_version = LINUX_VERSION_CODE;

	media_device_init(mdev);

	return mdev;
#else
	return NULL;
#endif
}
EXPORT_SYMBOL_GPL(__v4l2_mc_usb_media_device_init);

int v4l2_mc_create_media_graph(struct media_device *mdev)

{
	struct media_entity *entity;
	struct media_entity *if_vid = NULL, *if_aud = NULL;
	struct media_entity *tuner = NULL, *decoder = NULL;
	struct media_entity *io_v4l = NULL, *io_vbi = NULL, *io_swradio = NULL;
	bool is_webcam = false;
	u32 flags;
	int ret;

	if (!mdev)
		return 0;

	media_device_for_each_entity(entity, mdev) {
		switch (entity->function) {
		case MEDIA_ENT_F_IF_VID_DECODER:
			if_vid = entity;
			break;
		case MEDIA_ENT_F_IF_AUD_DECODER:
			if_aud = entity;
			break;
		case MEDIA_ENT_F_TUNER:
			tuner = entity;
			break;
		case MEDIA_ENT_F_ATV_DECODER:
			decoder = entity;
			break;
		case MEDIA_ENT_F_IO_V4L:
			io_v4l = entity;
			break;
		case MEDIA_ENT_F_IO_VBI:
			io_vbi = entity;
			break;
		case MEDIA_ENT_F_IO_SWRADIO:
			io_swradio = entity;
			break;
		case MEDIA_ENT_F_CAM_SENSOR:
			is_webcam = true;
			break;
		}
	}

	/* It should have at least one I/O entity */
	if (!io_v4l && !io_vbi && !io_swradio)
		return -EINVAL;

	/*
	 * Here, webcams are modelled on a very simple way: the sensor is
	 * connected directly to the I/O entity. All dirty details, like
	 * scaler and crop HW are hidden. While such mapping is not enough
	 * for mc-centric hardware, it is enough for v4l2 interface centric
	 * PC-consumer's hardware.
	 */
	if (is_webcam) {
		if (!io_v4l)
			return -EINVAL;

		media_device_for_each_entity(entity, mdev) {
			if (entity->function != MEDIA_ENT_F_CAM_SENSOR)
				continue;
			ret = media_create_pad_link(entity, 0,
						    io_v4l, 0,
						    MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
		}
		if (!decoder)
			return 0;
	}

	/* The device isn't a webcam. So, it should have a decoder */
	if (!decoder)
		return -EINVAL;

	/* Link the tuner and IF video output pads */
	if (tuner) {
		if (if_vid) {
			ret = media_create_pad_link(tuner, TUNER_PAD_OUTPUT,
						    if_vid,
						    IF_VID_DEC_PAD_IF_INPUT,
						    MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
			ret = media_create_pad_link(if_vid, IF_VID_DEC_PAD_OUT,
						decoder, DEMOD_PAD_IF_INPUT,
						MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
		} else {
			ret = media_create_pad_link(tuner, TUNER_PAD_OUTPUT,
						decoder, DEMOD_PAD_IF_INPUT,
						MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
		}

		if (if_aud) {
			ret = media_create_pad_link(tuner, TUNER_PAD_AUD_OUT,
						    if_aud,
						    IF_AUD_DEC_PAD_IF_INPUT,
						    MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;
		} else {
			if_aud = tuner;
		}

	}

	/* Create demod to V4L, VBI and SDR radio links */
	if (io_v4l) {
		ret = media_create_pad_link(decoder, DEMOD_PAD_VID_OUT,
					io_v4l, 0,
					MEDIA_LNK_FL_ENABLED);
		if (ret)
			return ret;
	}

	if (io_swradio) {
		ret = media_create_pad_link(decoder, DEMOD_PAD_VID_OUT,
					io_swradio, 0,
					MEDIA_LNK_FL_ENABLED);
		if (ret)
			return ret;
	}

	if (io_vbi) {
		ret = media_create_pad_link(decoder, DEMOD_PAD_VBI_OUT,
					    io_vbi, 0,
					    MEDIA_LNK_FL_ENABLED);
		if (ret)
			return ret;
	}

	/* Create links for the media connectors */
	flags = MEDIA_LNK_FL_ENABLED;
	media_device_for_each_entity(entity, mdev) {
		switch (entity->function) {
		case MEDIA_ENT_F_CONN_RF:
			if (!tuner)
				continue;

			ret = media_create_pad_link(entity, 0, tuner,
						    TUNER_PAD_RF_INPUT,
						    flags);
			break;
		case MEDIA_ENT_F_CONN_SVIDEO:
		case MEDIA_ENT_F_CONN_COMPOSITE:
			ret = media_create_pad_link(entity, 0, decoder,
						    DEMOD_PAD_IF_INPUT,
						    flags);
			break;
		default:
			continue;
		}
		if (ret)
			return ret;

		flags = 0;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_mc_create_media_graph);
