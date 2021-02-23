// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/unistd.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/tuner.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>

#include "go7007-priv.h"

/*
 * Wait for an interrupt to be delivered from the GO7007SB and return
 * the associated value and data.
 *
 * Must be called with the hw_lock held.
 */
int go7007_read_interrupt(struct go7007 *go, u16 *value, u16 *data)
{
	go->interrupt_available = 0;
	go->hpi_ops->read_interrupt(go);
	if (wait_event_timeout(go->interrupt_waitq,
				go->interrupt_available, 5*HZ) < 0) {
		v4l2_err(&go->v4l2_dev, "timeout waiting for read interrupt\n");
		return -1;
	}
	if (!go->interrupt_available)
		return -1;
	go->interrupt_available = 0;
	*value = go->interrupt_value & 0xfffe;
	*data = go->interrupt_data;
	return 0;
}
EXPORT_SYMBOL(go7007_read_interrupt);

/*
 * Read a register/address on the GO7007SB.
 *
 * Must be called with the hw_lock held.
 */
int go7007_read_addr(struct go7007 *go, u16 addr, u16 *data)
{
	int count = 100;
	u16 value;

	if (go7007_write_interrupt(go, 0x0010, addr) < 0)
		return -EIO;
	while (count-- > 0) {
		if (go7007_read_interrupt(go, &value, data) == 0 &&
				value == 0xa000)
			return 0;
	}
	return -EIO;
}
EXPORT_SYMBOL(go7007_read_addr);

/*
 * Send the boot firmware to the encoder, which just wakes it up and lets
 * us talk to the GPIO pins and on-board I2C adapter.
 *
 * Must be called with the hw_lock held.
 */
static int go7007_load_encoder(struct go7007 *go)
{
	const struct firmware *fw_entry;
	char fw_name[] = "go7007/go7007fw.bin";
	void *bounce;
	int fw_len, rv = 0;
	u16 intr_val, intr_data;

	if (go->boot_fw == NULL) {
		if (request_firmware(&fw_entry, fw_name, go->dev)) {
			v4l2_err(go, "unable to load firmware from file \"%s\"\n", fw_name);
			return -1;
		}
		if (fw_entry->size < 16 || memcmp(fw_entry->data, "WISGO7007FW", 11)) {
			v4l2_err(go, "file \"%s\" does not appear to be go7007 firmware\n", fw_name);
			release_firmware(fw_entry);
			return -1;
		}
		fw_len = fw_entry->size - 16;
		bounce = kmemdup(fw_entry->data + 16, fw_len, GFP_KERNEL);
		if (bounce == NULL) {
			v4l2_err(go, "unable to allocate %d bytes for firmware transfer\n", fw_len);
			release_firmware(fw_entry);
			return -1;
		}
		release_firmware(fw_entry);
		go->boot_fw_len = fw_len;
		go->boot_fw = bounce;
	}
	if (go7007_interface_reset(go) < 0 ||
	    go7007_send_firmware(go, go->boot_fw, go->boot_fw_len) < 0 ||
	    go7007_read_interrupt(go, &intr_val, &intr_data) < 0 ||
			(intr_val & ~0x1) != 0x5a5a) {
		v4l2_err(go, "error transferring firmware\n");
		rv = -1;
	}
	return rv;
}

MODULE_FIRMWARE("go7007/go7007fw.bin");

/*
 * Boot the encoder and register the I2C adapter if requested.  Do the
 * minimum initialization necessary, since the board-specific code may
 * still need to probe the board ID.
 *
 * Must NOT be called with the hw_lock held.
 */
int go7007_boot_encoder(struct go7007 *go, int init_i2c)
{
	int ret;

	mutex_lock(&go->hw_lock);
	ret = go7007_load_encoder(go);
	mutex_unlock(&go->hw_lock);
	if (ret < 0)
		return -1;
	if (!init_i2c)
		return 0;
	if (go7007_i2c_init(go) < 0)
		return -1;
	go->i2c_adapter_online = 1;
	return 0;
}
EXPORT_SYMBOL(go7007_boot_encoder);

/*
 * Configure any hardware-related registers in the GO7007, such as GPIO
 * pins and bus parameters, which are board-specific.  This assumes
 * the boot firmware has already been downloaded.
 *
 * Must be called with the hw_lock held.
 */
static int go7007_init_encoder(struct go7007 *go)
{
	if (go->board_info->audio_flags & GO7007_AUDIO_I2S_MASTER) {
		go7007_write_addr(go, 0x1000, 0x0811);
		go7007_write_addr(go, 0x1000, 0x0c11);
	}
	switch (go->board_id) {
	case GO7007_BOARDID_MATRIX_REV:
		/* Set GPIO pin 0 to be an output (audio clock control) */
		go7007_write_addr(go, 0x3c82, 0x0001);
		go7007_write_addr(go, 0x3c80, 0x00fe);
		break;
	case GO7007_BOARDID_ADLINK_MPG24:
		/* set GPIO5 to be an output, currently low */
		go7007_write_addr(go, 0x3c82, 0x0000);
		go7007_write_addr(go, 0x3c80, 0x00df);
		break;
	case GO7007_BOARDID_ADS_USBAV_709:
		/* GPIO pin 0: audio clock control */
		/*      pin 2: TW9906 reset */
		/*      pin 3: capture LED */
		go7007_write_addr(go, 0x3c82, 0x000d);
		go7007_write_addr(go, 0x3c80, 0x00f2);
		break;
	}
	return 0;
}

/*
 * Send the boot firmware to the GO7007 and configure the registers.  This
 * is the only way to stop the encoder once it has started streaming video.
 *
 * Must be called with the hw_lock held.
 */
int go7007_reset_encoder(struct go7007 *go)
{
	if (go7007_load_encoder(go) < 0)
		return -1;
	return go7007_init_encoder(go);
}

/*
 * Attempt to instantiate an I2C client by ID, probably loading a module.
 */
static int init_i2c_module(struct i2c_adapter *adapter, const struct go_i2c *const i2c)
{
	struct go7007 *go = i2c_get_adapdata(adapter);
	struct v4l2_device *v4l2_dev = &go->v4l2_dev;
	struct v4l2_subdev *sd;
	struct i2c_board_info info;

	memset(&info, 0, sizeof(info));
	strscpy(info.type, i2c->type, sizeof(info.type));
	info.addr = i2c->addr;
	info.flags = i2c->flags;

	sd = v4l2_i2c_new_subdev_board(v4l2_dev, adapter, &info, NULL);
	if (sd) {
		if (i2c->is_video)
			go->sd_video = sd;
		if (i2c->is_audio)
			go->sd_audio = sd;
		return 0;
	}

	pr_info("go7007: probing for module i2c:%s failed\n", i2c->type);
	return -EINVAL;
}

/*
 * Detach and unregister the encoder.  The go7007 struct won't be freed
 * until v4l2 finishes releasing its resources and all associated fds are
 * closed by applications.
 */
static void go7007_remove(struct v4l2_device *v4l2_dev)
{
	struct go7007 *go = container_of(v4l2_dev, struct go7007, v4l2_dev);

	v4l2_device_unregister(v4l2_dev);
	if (go->hpi_ops->release)
		go->hpi_ops->release(go);
	if (go->i2c_adapter_online) {
		i2c_del_adapter(&go->i2c_adapter);
		go->i2c_adapter_online = 0;
	}

	kfree(go->boot_fw);
	go7007_v4l2_remove(go);
	kfree(go);
}

/*
 * Finalize the GO7007 hardware setup, register the on-board I2C adapter
 * (if used on this board), load the I2C client driver for the sensor
 * (SAA7115 or whatever) and other devices, and register the ALSA and V4L2
 * interfaces.
 *
 * Must NOT be called with the hw_lock held.
 */
int go7007_register_encoder(struct go7007 *go, unsigned num_i2c_devs)
{
	int i, ret;

	dev_info(go->dev, "go7007: registering new %s\n", go->name);

	go->v4l2_dev.release = go7007_remove;
	ret = v4l2_device_register(go->dev, &go->v4l2_dev);
	if (ret < 0)
		return ret;

	mutex_lock(&go->hw_lock);
	ret = go7007_init_encoder(go);
	mutex_unlock(&go->hw_lock);
	if (ret < 0)
		return ret;

	ret = go7007_v4l2_ctrl_init(go);
	if (ret < 0)
		return ret;

	if (!go->i2c_adapter_online &&
			go->board_info->flags & GO7007_BOARD_USE_ONBOARD_I2C) {
		ret = go7007_i2c_init(go);
		if (ret < 0)
			return ret;
		go->i2c_adapter_online = 1;
	}
	if (go->i2c_adapter_online) {
		if (go->board_id == GO7007_BOARDID_ADS_USBAV_709) {
			/* Reset the TW9906 */
			go7007_write_addr(go, 0x3c82, 0x0009);
			msleep(50);
			go7007_write_addr(go, 0x3c82, 0x000d);
		}
		for (i = 0; i < num_i2c_devs; ++i)
			init_i2c_module(&go->i2c_adapter, &go->board_info->i2c_devs[i]);

		if (go->tuner_type >= 0) {
			struct tuner_setup setup = {
				.addr = ADDR_UNSET,
				.type = go->tuner_type,
				.mode_mask = T_ANALOG_TV,
			};

			v4l2_device_call_all(&go->v4l2_dev, 0, tuner,
				s_type_addr, &setup);
		}
		if (go->board_id == GO7007_BOARDID_ADLINK_MPG24)
			v4l2_subdev_call(go->sd_video, video, s_routing,
					0, 0, go->channel_number + 1);
	}

	ret = go7007_v4l2_init(go);
	if (ret < 0)
		return ret;

	if (go->board_info->flags & GO7007_BOARD_HAS_AUDIO) {
		go->audio_enabled = 1;
		go7007_snd_init(go);
	}
	return 0;
}
EXPORT_SYMBOL(go7007_register_encoder);

/*
 * Send the encode firmware to the encoder, which will cause it
 * to immediately start delivering the video and audio streams.
 *
 * Must be called with the hw_lock held.
 */
int go7007_start_encoder(struct go7007 *go)
{
	u8 *fw;
	int fw_len, rv = 0, i, x, y;
	u16 intr_val, intr_data;

	go->modet_enable = 0;
	for (i = 0; i < 4; i++)
		go->modet[i].enable = 0;

	switch (v4l2_ctrl_g_ctrl(go->modet_mode)) {
	case V4L2_DETECT_MD_MODE_GLOBAL:
		memset(go->modet_map, 0, sizeof(go->modet_map));
		go->modet[0].enable = 1;
		go->modet_enable = 1;
		break;
	case V4L2_DETECT_MD_MODE_REGION_GRID:
		for (y = 0; y < go->height / 16; y++) {
			for (x = 0; x < go->width / 16; x++) {
				int idx = y * go->width / 16 + x;

				go->modet[go->modet_map[idx]].enable = 1;
			}
		}
		go->modet_enable = 1;
		break;
	}

	if (go->dvd_mode)
		go->modet_enable = 0;

	if (go7007_construct_fw_image(go, &fw, &fw_len) < 0)
		return -1;

	if (go7007_send_firmware(go, fw, fw_len) < 0 ||
			go7007_read_interrupt(go, &intr_val, &intr_data) < 0) {
		v4l2_err(&go->v4l2_dev, "error transferring firmware\n");
		rv = -1;
		goto start_error;
	}

	go->state = STATE_DATA;
	go->parse_length = 0;
	go->seen_frame = 0;
	if (go7007_stream_start(go) < 0) {
		v4l2_err(&go->v4l2_dev, "error starting stream transfer\n");
		rv = -1;
		goto start_error;
	}

start_error:
	kfree(fw);
	return rv;
}

/*
 * Store a byte in the current video buffer, if there is one.
 */
static inline void store_byte(struct go7007_buffer *vb, u8 byte)
{
	if (vb && vb->vb.vb2_buf.planes[0].bytesused < GO7007_BUF_SIZE) {
		u8 *ptr = vb2_plane_vaddr(&vb->vb.vb2_buf, 0);

		ptr[vb->vb.vb2_buf.planes[0].bytesused++] = byte;
	}
}

static void go7007_set_motion_regions(struct go7007 *go, struct go7007_buffer *vb,
		u32 motion_regions)
{
	if (motion_regions != go->modet_event_status) {
		struct v4l2_event ev = {
			.type = V4L2_EVENT_MOTION_DET,
			.u.motion_det = {
				.flags = V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ,
				.frame_sequence = vb->vb.sequence,
				.region_mask = motion_regions,
			},
		};

		v4l2_event_queue(&go->vdev, &ev);
		go->modet_event_status = motion_regions;
	}
}

/*
 * Determine regions with motion and send a motion detection event
 * in case of changes.
 */
static void go7007_motion_regions(struct go7007 *go, struct go7007_buffer *vb)
{
	u32 *bytesused = &vb->vb.vb2_buf.planes[0].bytesused;
	unsigned motion[4] = { 0, 0, 0, 0 };
	u32 motion_regions = 0;
	unsigned stride = (go->width + 7) >> 3;
	unsigned x, y;
	int i;

	for (i = 0; i < 216; ++i)
		store_byte(vb, go->active_map[i]);
	for (y = 0; y < go->height / 16; y++) {
		for (x = 0; x < go->width / 16; x++) {
			if (!(go->active_map[y * stride + (x >> 3)] & (1 << (x & 7))))
				continue;
			motion[go->modet_map[y * (go->width / 16) + x]]++;
		}
	}
	motion_regions = ((motion[0] > 0) << 0) |
			 ((motion[1] > 0) << 1) |
			 ((motion[2] > 0) << 2) |
			 ((motion[3] > 0) << 3);
	*bytesused -= 216;
	go7007_set_motion_regions(go, vb, motion_regions);
}

/*
 * Deliver the last video buffer and get a new one to start writing to.
 */
static struct go7007_buffer *frame_boundary(struct go7007 *go, struct go7007_buffer *vb)
{
	u32 *bytesused;
	struct go7007_buffer *vb_tmp = NULL;
	unsigned long flags;

	if (vb == NULL) {
		spin_lock_irqsave(&go->spinlock, flags);
		if (!list_empty(&go->vidq_active))
			vb = go->active_buf =
				list_first_entry(&go->vidq_active, struct go7007_buffer, list);
		spin_unlock_irqrestore(&go->spinlock, flags);
		go->next_seq++;
		return vb;
	}
	bytesused = &vb->vb.vb2_buf.planes[0].bytesused;

	vb->vb.sequence = go->next_seq++;
	if (vb->modet_active && *bytesused + 216 < GO7007_BUF_SIZE)
		go7007_motion_regions(go, vb);
	else
		go7007_set_motion_regions(go, vb, 0);

	vb->vb.vb2_buf.timestamp = ktime_get_ns();
	vb_tmp = vb;
	spin_lock_irqsave(&go->spinlock, flags);
	list_del(&vb->list);
	if (list_empty(&go->vidq_active))
		vb = NULL;
	else
		vb = list_first_entry(&go->vidq_active,
				struct go7007_buffer, list);
	go->active_buf = vb;
	spin_unlock_irqrestore(&go->spinlock, flags);
	vb2_buffer_done(&vb_tmp->vb.vb2_buf, VB2_BUF_STATE_DONE);
	return vb;
}

static void write_bitmap_word(struct go7007 *go)
{
	int x, y, i, stride = ((go->width >> 4) + 7) >> 3;

	for (i = 0; i < 16; ++i) {
		y = (((go->parse_length - 1) << 3) + i) / (go->width >> 4);
		x = (((go->parse_length - 1) << 3) + i) % (go->width >> 4);
		if (stride * y + (x >> 3) < sizeof(go->active_map))
			go->active_map[stride * y + (x >> 3)] |=
					(go->modet_word & 1) << (x & 0x7);
		go->modet_word >>= 1;
	}
}

/*
 * Parse a chunk of the video stream into frames.  The frames are not
 * delimited by the hardware, so we have to parse the frame boundaries
 * based on the type of video stream we're receiving.
 */
void go7007_parse_video_stream(struct go7007 *go, u8 *buf, int length)
{
	struct go7007_buffer *vb = go->active_buf;
	int i, seq_start_code = -1, gop_start_code = -1, frame_start_code = -1;

	switch (go->format) {
	case V4L2_PIX_FMT_MPEG4:
		seq_start_code = 0xB0;
		gop_start_code = 0xB3;
		frame_start_code = 0xB6;
		break;
	case V4L2_PIX_FMT_MPEG1:
	case V4L2_PIX_FMT_MPEG2:
		seq_start_code = 0xB3;
		gop_start_code = 0xB8;
		frame_start_code = 0x00;
		break;
	}

	for (i = 0; i < length; ++i) {
		if (vb && vb->vb.vb2_buf.planes[0].bytesused >=
				GO7007_BUF_SIZE - 3) {
			v4l2_info(&go->v4l2_dev, "dropping oversized frame\n");
			vb->vb.vb2_buf.planes[0].bytesused = 0;
			vb->frame_offset = 0;
			vb->modet_active = 0;
			vb = go->active_buf = NULL;
		}

		switch (go->state) {
		case STATE_DATA:
			switch (buf[i]) {
			case 0x00:
				go->state = STATE_00;
				break;
			case 0xFF:
				go->state = STATE_FF;
				break;
			default:
				store_byte(vb, buf[i]);
				break;
			}
			break;
		case STATE_00:
			switch (buf[i]) {
			case 0x00:
				go->state = STATE_00_00;
				break;
			case 0xFF:
				store_byte(vb, 0x00);
				go->state = STATE_FF;
				break;
			default:
				store_byte(vb, 0x00);
				store_byte(vb, buf[i]);
				go->state = STATE_DATA;
				break;
			}
			break;
		case STATE_00_00:
			switch (buf[i]) {
			case 0x00:
				store_byte(vb, 0x00);
				/* go->state remains STATE_00_00 */
				break;
			case 0x01:
				go->state = STATE_00_00_01;
				break;
			case 0xFF:
				store_byte(vb, 0x00);
				store_byte(vb, 0x00);
				go->state = STATE_FF;
				break;
			default:
				store_byte(vb, 0x00);
				store_byte(vb, 0x00);
				store_byte(vb, buf[i]);
				go->state = STATE_DATA;
				break;
			}
			break;
		case STATE_00_00_01:
			if (buf[i] == 0xF8 && go->modet_enable == 0) {
				/* MODET start code, but MODET not enabled */
				store_byte(vb, 0x00);
				store_byte(vb, 0x00);
				store_byte(vb, 0x01);
				store_byte(vb, 0xF8);
				go->state = STATE_DATA;
				break;
			}
			/* If this is the start of a new MPEG frame,
			 * get a new buffer */
			if ((go->format == V4L2_PIX_FMT_MPEG1 ||
			     go->format == V4L2_PIX_FMT_MPEG2 ||
			     go->format == V4L2_PIX_FMT_MPEG4) &&
			    (buf[i] == seq_start_code ||
			     buf[i] == gop_start_code ||
			     buf[i] == frame_start_code)) {
				if (vb == NULL || go->seen_frame)
					vb = frame_boundary(go, vb);
				go->seen_frame = buf[i] == frame_start_code;
				if (vb && go->seen_frame)
					vb->frame_offset =
					vb->vb.vb2_buf.planes[0].bytesused;
			}
			/* Handle any special chunk types, or just write the
			 * start code to the (potentially new) buffer */
			switch (buf[i]) {
			case 0xF5: /* timestamp */
				go->parse_length = 12;
				go->state = STATE_UNPARSED;
				break;
			case 0xF6: /* vbi */
				go->state = STATE_VBI_LEN_A;
				break;
			case 0xF8: /* MD map */
				go->parse_length = 0;
				memset(go->active_map, 0,
						sizeof(go->active_map));
				go->state = STATE_MODET_MAP;
				break;
			case 0xFF: /* Potential JPEG start code */
				store_byte(vb, 0x00);
				store_byte(vb, 0x00);
				store_byte(vb, 0x01);
				go->state = STATE_FF;
				break;
			default:
				store_byte(vb, 0x00);
				store_byte(vb, 0x00);
				store_byte(vb, 0x01);
				store_byte(vb, buf[i]);
				go->state = STATE_DATA;
				break;
			}
			break;
		case STATE_FF:
			switch (buf[i]) {
			case 0x00:
				store_byte(vb, 0xFF);
				go->state = STATE_00;
				break;
			case 0xFF:
				store_byte(vb, 0xFF);
				/* go->state remains STATE_FF */
				break;
			case 0xD8:
				if (go->format == V4L2_PIX_FMT_MJPEG)
					vb = frame_boundary(go, vb);
				fallthrough;
			default:
				store_byte(vb, 0xFF);
				store_byte(vb, buf[i]);
				go->state = STATE_DATA;
				break;
			}
			break;
		case STATE_VBI_LEN_A:
			go->parse_length = buf[i] << 8;
			go->state = STATE_VBI_LEN_B;
			break;
		case STATE_VBI_LEN_B:
			go->parse_length |= buf[i];
			if (go->parse_length > 0)
				go->state = STATE_UNPARSED;
			else
				go->state = STATE_DATA;
			break;
		case STATE_MODET_MAP:
			if (go->parse_length < 204) {
				if (go->parse_length & 1) {
					go->modet_word |= buf[i];
					write_bitmap_word(go);
				} else
					go->modet_word = buf[i] << 8;
			} else if (go->parse_length == 207 && vb) {
				vb->modet_active = buf[i];
			}
			if (++go->parse_length == 208)
				go->state = STATE_DATA;
			break;
		case STATE_UNPARSED:
			if (--go->parse_length == 0)
				go->state = STATE_DATA;
			break;
		}
	}
}
EXPORT_SYMBOL(go7007_parse_video_stream);

/*
 * Allocate a new go7007 struct.  Used by the hardware-specific probe.
 */
struct go7007 *go7007_alloc(const struct go7007_board_info *board,
						struct device *dev)
{
	struct go7007 *go;
	int i;

	go = kzalloc(sizeof(struct go7007), GFP_KERNEL);
	if (go == NULL)
		return NULL;
	go->dev = dev;
	go->board_info = board;
	go->board_id = 0;
	go->tuner_type = -1;
	go->channel_number = 0;
	go->name[0] = 0;
	mutex_init(&go->hw_lock);
	init_waitqueue_head(&go->frame_waitq);
	spin_lock_init(&go->spinlock);
	go->status = STATUS_INIT;
	memset(&go->i2c_adapter, 0, sizeof(go->i2c_adapter));
	go->i2c_adapter_online = 0;
	go->interrupt_available = 0;
	init_waitqueue_head(&go->interrupt_waitq);
	go->input = 0;
	go7007_update_board(go);
	go->encoder_h_halve = 0;
	go->encoder_v_halve = 0;
	go->encoder_subsample = 0;
	go->format = V4L2_PIX_FMT_MJPEG;
	go->bitrate = 1500000;
	go->fps_scale = 1;
	go->pali = 0;
	go->aspect_ratio = GO7007_RATIO_1_1;
	go->gop_size = 0;
	go->ipb = 0;
	go->closed_gop = 0;
	go->repeat_seqhead = 0;
	go->seq_header_enable = 0;
	go->gop_header_enable = 0;
	go->dvd_mode = 0;
	go->interlace_coding = 0;
	for (i = 0; i < 4; ++i)
		go->modet[i].enable = 0;
	for (i = 0; i < 1624; ++i)
		go->modet_map[i] = 0;
	go->audio_deliver = NULL;
	go->audio_enabled = 0;

	return go;
}
EXPORT_SYMBOL(go7007_alloc);

void go7007_update_board(struct go7007 *go)
{
	const struct go7007_board_info *board = go->board_info;

	if (board->sensor_flags & GO7007_SENSOR_TV) {
		go->standard = GO7007_STD_NTSC;
		go->std = V4L2_STD_NTSC_M;
		go->width = 720;
		go->height = 480;
		go->sensor_framerate = 30000;
	} else {
		go->standard = GO7007_STD_OTHER;
		go->width = board->sensor_width;
		go->height = board->sensor_height;
		go->sensor_framerate = board->sensor_framerate;
	}
	go->encoder_v_offset = board->sensor_v_offset;
	go->encoder_h_offset = board->sensor_h_offset;
}
EXPORT_SYMBOL(go7007_update_board);

MODULE_LICENSE("GPL v2");
