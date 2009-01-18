/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
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
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <asm/system.h>
#include <linux/videodev2.h>
#include <media/tuner.h>
#include <media/v4l2-common.h>

#include "go7007-priv.h"
#include "wis-i2c.h"

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
		printk(KERN_ERR "go7007: timeout waiting for read interrupt\n");
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
	char fw_name[] = "go7007fw.bin";
	void *bounce;
	int fw_len, rv = 0;
	u16 intr_val, intr_data;

	if (request_firmware(&fw_entry, fw_name, go->dev)) {
		printk(KERN_ERR
			"go7007: unable to load firmware from file \"%s\"\n",
			fw_name);
		return -1;
	}
	if (fw_entry->size < 16 || memcmp(fw_entry->data, "WISGO7007FW", 11)) {
		printk(KERN_ERR "go7007: file \"%s\" does not appear to be "
				"go7007 firmware\n", fw_name);
		release_firmware(fw_entry);
		return -1;
	}
	fw_len = fw_entry->size - 16;
	bounce = kmalloc(fw_len, GFP_KERNEL);
	if (bounce == NULL) {
		printk(KERN_ERR "go7007: unable to allocate %d bytes for "
				"firmware transfer\n", fw_len);
		release_firmware(fw_entry);
		return -1;
	}
	memcpy(bounce, fw_entry->data + 16, fw_len);
	release_firmware(fw_entry);
	if (go7007_interface_reset(go) < 0 ||
			go7007_send_firmware(go, bounce, fw_len) < 0 ||
			go7007_read_interrupt(go, &intr_val, &intr_data) < 0 ||
			(intr_val & ~0x1) != 0x5a5a) {
		printk(KERN_ERR "go7007: error transferring firmware\n");
		rv = -1;
	}
	kfree(bounce);
	return rv;
}

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

	down(&go->hw_lock);
	ret = go7007_load_encoder(go);
	up(&go->hw_lock);
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
	if (go->board_id == GO7007_BOARDID_MATRIX_REV) {
		/* Set GPIO pin 0 to be an output (audio clock control) */
		go7007_write_addr(go, 0x3c82, 0x0001);
		go7007_write_addr(go, 0x3c80, 0x00fe);
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
static int init_i2c_module(struct i2c_adapter *adapter, int id, int addr)
{
	char *modname;

	switch (id) {
	case I2C_DRIVERID_WIS_SAA7115:
		modname = "wis-saa7115";
		break;
	case I2C_DRIVERID_WIS_SAA7113:
		modname = "wis-saa7113";
		break;
	case I2C_DRIVERID_WIS_UDA1342:
		modname = "wis-uda1342";
		break;
	case I2C_DRIVERID_WIS_SONY_TUNER:
		modname = "wis-sony-tuner";
		break;
	case I2C_DRIVERID_WIS_TW9903:
		modname = "wis-tw9903";
		break;
	case I2C_DRIVERID_WIS_TW2804:
		modname = "wis-tw2804";
		break;
	case I2C_DRIVERID_WIS_OV7640:
		modname = "wis-ov7640";
		break;
	case I2C_DRIVERID_S2250:
		modname = "s2250-board";
		break;
	default:
		modname = NULL;
		break;
	}
	if (modname != NULL)
		request_module(modname);
	if (wis_i2c_probe_device(adapter, id, addr) == 1)
		return 0;
	if (modname != NULL)
		printk(KERN_INFO
			"go7007: probing for module %s failed\n", modname);
	else
		printk(KERN_INFO
			"go7007: sensor %u seems to be unsupported!\n", id);
	return -1;
}

/*
 * Finalize the GO7007 hardware setup, register the on-board I2C adapter
 * (if used on this board), load the I2C client driver for the sensor
 * (SAA7115 or whatever) and other devices, and register the ALSA and V4L2
 * interfaces.
 *
 * Must NOT be called with the hw_lock held.
 */
int go7007_register_encoder(struct go7007 *go)
{
	int i, ret;

	printk(KERN_INFO "go7007: registering new %s\n", go->name);

	down(&go->hw_lock);
	ret = go7007_init_encoder(go);
	up(&go->hw_lock);
	if (ret < 0)
		return -1;

	if (!go->i2c_adapter_online &&
			go->board_info->flags & GO7007_BOARD_USE_ONBOARD_I2C) {
		if (go7007_i2c_init(go) < 0)
			return -1;
		go->i2c_adapter_online = 1;
	}
	if (go->i2c_adapter_online) {
		for (i = 0; i < go->board_info->num_i2c_devs; ++i)
			init_i2c_module(&go->i2c_adapter,
					go->board_info->i2c_devs[i].id,
					go->board_info->i2c_devs[i].addr);
#ifdef TUNER_SET_TYPE_ADDR
		if (go->tuner_type >= 0) {
			struct tuner_setup tun_setup = {
				.mode_mask	= T_ANALOG_TV,
				.addr		= ADDR_UNSET,
				.type		= go->tuner_type
			};
			i2c_clients_command(&go->i2c_adapter,
				TUNER_SET_TYPE_ADDR, &tun_setup);
		}
#else
		if (go->tuner_type >= 0)
			i2c_clients_command(&go->i2c_adapter,
				TUNER_SET_TYPE, &go->tuner_type);
#endif
		if (go->board_id == GO7007_BOARDID_ADLINK_MPG24)
			i2c_clients_command(&go->i2c_adapter,
				DECODER_SET_CHANNEL, &go->channel_number);
	}
	if (go->board_info->flags & GO7007_BOARD_HAS_AUDIO) {
		go->audio_enabled = 1;
		go7007_snd_init(go);
	}
	return go7007_v4l2_init(go);
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
	int fw_len, rv = 0, i;
	u16 intr_val, intr_data;

	go->modet_enable = 0;
	if (!go->dvd_mode)
		for (i = 0; i < 4; ++i) {
			if (go->modet[i].enable) {
				go->modet_enable = 1;
				continue;
			}
			go->modet[i].pixel_threshold = 32767;
			go->modet[i].motion_threshold = 32767;
			go->modet[i].mb_threshold = 32767;
		}

	if (go7007_construct_fw_image(go, &fw, &fw_len) < 0)
		return -1;

	if (go7007_send_firmware(go, fw, fw_len) < 0 ||
			go7007_read_interrupt(go, &intr_val, &intr_data) < 0) {
		printk(KERN_ERR "go7007: error transferring firmware\n");
		rv = -1;
		goto start_error;
	}

	go->state = STATE_DATA;
	go->parse_length = 0;
	go->seen_frame = 0;
	if (go7007_stream_start(go) < 0) {
		printk(KERN_ERR "go7007: error starting stream transfer\n");
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
static inline void store_byte(struct go7007_buffer *gobuf, u8 byte)
{
	if (gobuf != NULL && gobuf->bytesused < GO7007_BUF_SIZE) {
		unsigned int pgidx = gobuf->offset >> PAGE_SHIFT;
		unsigned int pgoff = gobuf->offset & ~PAGE_MASK;

		*((u8 *)page_address(gobuf->pages[pgidx]) + pgoff) = byte;
		++gobuf->offset;
		++gobuf->bytesused;
	}
}

/*
 * Deliver the last video buffer and get a new one to start writing to.
 */
static void frame_boundary(struct go7007 *go)
{
	struct go7007_buffer *gobuf;
	int i;

	if (go->active_buf) {
		if (go->active_buf->modet_active) {
			if (go->active_buf->bytesused + 216 < GO7007_BUF_SIZE) {
				for (i = 0; i < 216; ++i)
					store_byte(go->active_buf,
							go->active_map[i]);
				go->active_buf->bytesused -= 216;
			} else
				go->active_buf->modet_active = 0;
		}
		go->active_buf->state = BUF_STATE_DONE;
		wake_up_interruptible(&go->frame_waitq);
		go->active_buf = NULL;
	}
	list_for_each_entry(gobuf, &go->stream, stream)
		if (gobuf->state == BUF_STATE_QUEUED) {
			gobuf->seq = go->next_seq;
			do_gettimeofday(&gobuf->timestamp);
			go->active_buf = gobuf;
			break;
		}
	++go->next_seq;
}

static void write_bitmap_word(struct go7007 *go)
{
	int x, y, i, stride = ((go->width >> 4) + 7) >> 3;

	for (i = 0; i < 16; ++i) {
		y = (((go->parse_length - 1) << 3) + i) / (go->width >> 4);
		x = (((go->parse_length - 1) << 3) + i) % (go->width >> 4);
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
	int i, seq_start_code = -1, frame_start_code = -1;

	spin_lock(&go->spinlock);

	switch (go->format) {
	case GO7007_FORMAT_MPEG4:
		seq_start_code = 0xB0;
		frame_start_code = 0xB6;
		break;
	case GO7007_FORMAT_MPEG1:
	case GO7007_FORMAT_MPEG2:
		seq_start_code = 0xB3;
		frame_start_code = 0x00;
		break;
	}

	for (i = 0; i < length; ++i) {
		if (go->active_buf != NULL &&
			    go->active_buf->bytesused >= GO7007_BUF_SIZE - 3) {
			printk(KERN_DEBUG "go7007: dropping oversized frame\n");
			go->active_buf->offset -= go->active_buf->bytesused;
			go->active_buf->bytesused = 0;
			go->active_buf->modet_active = 0;
			go->active_buf = NULL;
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
				store_byte(go->active_buf, buf[i]);
				break;
			}
			break;
		case STATE_00:
			switch (buf[i]) {
			case 0x00:
				go->state = STATE_00_00;
				break;
			case 0xFF:
				store_byte(go->active_buf, 0x00);
				go->state = STATE_FF;
				break;
			default:
				store_byte(go->active_buf, 0x00);
				store_byte(go->active_buf, buf[i]);
				go->state = STATE_DATA;
				break;
			}
			break;
		case STATE_00_00:
			switch (buf[i]) {
			case 0x00:
				store_byte(go->active_buf, 0x00);
				/* go->state remains STATE_00_00 */
				break;
			case 0x01:
				go->state = STATE_00_00_01;
				break;
			case 0xFF:
				store_byte(go->active_buf, 0x00);
				store_byte(go->active_buf, 0x00);
				go->state = STATE_FF;
				break;
			default:
				store_byte(go->active_buf, 0x00);
				store_byte(go->active_buf, 0x00);
				store_byte(go->active_buf, buf[i]);
				go->state = STATE_DATA;
				break;
			}
			break;
		case STATE_00_00_01:
			/* If this is the start of a new MPEG frame,
			 * get a new buffer */
			if ((go->format == GO7007_FORMAT_MPEG1 ||
					go->format == GO7007_FORMAT_MPEG2 ||
					go->format == GO7007_FORMAT_MPEG4) &&
					(buf[i] == seq_start_code ||
						buf[i] == 0xB8 || /* GOP code */
						buf[i] == frame_start_code)) {
				if (go->active_buf == NULL || go->seen_frame)
					frame_boundary(go);
				if (buf[i] == frame_start_code) {
					if (go->active_buf != NULL)
						go->active_buf->frame_offset =
							go->active_buf->offset;
					go->seen_frame = 1;
				} else {
					go->seen_frame = 0;
				}
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
				store_byte(go->active_buf, 0x00);
				store_byte(go->active_buf, 0x00);
				store_byte(go->active_buf, 0x01);
				go->state = STATE_FF;
				break;
			default:
				store_byte(go->active_buf, 0x00);
				store_byte(go->active_buf, 0x00);
				store_byte(go->active_buf, 0x01);
				store_byte(go->active_buf, buf[i]);
				go->state = STATE_DATA;
				break;
			}
			break;
		case STATE_FF:
			switch (buf[i]) {
			case 0x00:
				store_byte(go->active_buf, 0xFF);
				go->state = STATE_00;
				break;
			case 0xFF:
				store_byte(go->active_buf, 0xFF);
				/* go->state remains STATE_FF */
				break;
			case 0xD8:
				if (go->format == GO7007_FORMAT_MJPEG)
					frame_boundary(go);
				/* fall through */
			default:
				store_byte(go->active_buf, 0xFF);
				store_byte(go->active_buf, buf[i]);
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
			} else if (go->parse_length == 207 && go->active_buf) {
				go->active_buf->modet_active = buf[i];
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

	spin_unlock(&go->spinlock);
}
EXPORT_SYMBOL(go7007_parse_video_stream);

/*
 * Allocate a new go7007 struct.  Used by the hardware-specific probe.
 */
struct go7007 *go7007_alloc(struct go7007_board_info *board, struct device *dev)
{
	struct go7007 *go;
	int i;

	go = kmalloc(sizeof(struct go7007), GFP_KERNEL);
	if (go == NULL)
		return NULL;
	go->dev = dev;
	go->board_info = board;
	go->board_id = 0;
	go->tuner_type = -1;
	go->channel_number = 0;
	go->name[0] = 0;
	init_MUTEX(&go->hw_lock);
	init_waitqueue_head(&go->frame_waitq);
	spin_lock_init(&go->spinlock);
	go->video_dev = NULL;
	go->ref_count = 0;
	go->status = STATUS_INIT;
	memset(&go->i2c_adapter, 0, sizeof(go->i2c_adapter));
	go->i2c_adapter_online = 0;
	go->interrupt_available = 0;
	init_waitqueue_head(&go->interrupt_waitq);
	go->in_use = 0;
	go->input = 0;
	if (board->sensor_flags & GO7007_SENSOR_TV) {
		go->standard = GO7007_STD_NTSC;
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
	go->encoder_h_halve = 0;
	go->encoder_v_halve = 0;
	go->encoder_subsample = 0;
	go->streaming = 0;
	go->format = GO7007_FORMAT_MJPEG;
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
		go->modet[i].enable = 0;;
	for (i = 0; i < 1624; ++i)
		go->modet_map[i] = 0;
	go->audio_deliver = NULL;
	go->audio_enabled = 0;
	INIT_LIST_HEAD(&go->stream);

	return go;
}
EXPORT_SYMBOL(go7007_alloc);

/*
 * Detach and unregister the encoder.  The go7007 struct won't be freed
 * until v4l2 finishes releasing its resources and all associated fds are
 * closed by applications.
 */
void go7007_remove(struct go7007 *go)
{
	if (go->i2c_adapter_online) {
		if (i2c_del_adapter(&go->i2c_adapter) == 0)
			go->i2c_adapter_online = 0;
		else
			printk(KERN_ERR
				"go7007: error removing I2C adapter!\n");
	}

	if (go->audio_enabled)
		go7007_snd_remove(go);
	go7007_v4l2_remove(go);
}
EXPORT_SYMBOL(go7007_remove);

MODULE_LICENSE("GPL v2");
