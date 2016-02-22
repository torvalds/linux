/*
 * Media Controller ancillary functions
 *
 * Copyright (c) 2016 Mauro Carvalho Chehab <mchehab@osg.samsung.com>
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
#include <media/media-entity.h>
#include <media/v4l2-mc.h>

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
