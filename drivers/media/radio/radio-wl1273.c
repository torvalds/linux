/*
 * Driver for the Texas Instruments WL1273 FM radio.
 *
 * Copyright (C) 2011 Nokia Corporation
 * Author: Matti J. Aaltonen <matti.j.aaltonen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/mfd/wl1273-core.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#define DRIVER_DESC "Wl1273 FM Radio"

#define WL1273_POWER_SET_OFF		0
#define WL1273_POWER_SET_FM		BIT(0)
#define WL1273_POWER_SET_RDS		BIT(1)
#define WL1273_POWER_SET_RETENTION	BIT(4)

#define WL1273_PUPD_SET_OFF		0x00
#define WL1273_PUPD_SET_ON		0x01
#define WL1273_PUPD_SET_RETENTION	0x10

#define WL1273_FREQ(x)		(x * 10000 / 625)
#define WL1273_INV_FREQ(x)	(x * 625 / 10000)

/*
 * static int radio_nr - The number of the radio device
 *
 * The default is 0.
 */
static int radio_nr;
module_param(radio_nr, int, 0);
MODULE_PARM_DESC(radio_nr, "The number of the radio device. Default = 0");

struct wl1273_device {
	char *bus_type;

	u8 forbidden;
	unsigned int preemphasis;
	unsigned int spacing;
	unsigned int tx_power;
	unsigned int rx_frequency;
	unsigned int tx_frequency;
	unsigned int rangelow;
	unsigned int rangehigh;
	unsigned int band;
	bool stereo;

	/* RDS */
	unsigned int rds_on;

	wait_queue_head_t read_queue;
	struct mutex lock; /* for serializing fm radio operations */
	struct completion busy;

	unsigned char *buffer;
	unsigned int buf_size;
	unsigned int rd_index;
	unsigned int wr_index;

	/* Selected interrupts */
	u16 irq_flags;
	u16 irq_received;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_device v4l2dev;
	struct video_device videodev;
	struct device *dev;
	struct wl1273_core *core;
	struct file *owner;
	char *write_buf;
	unsigned int rds_users;
};

#define WL1273_IRQ_MASK	 (WL1273_FR_EVENT		|	\
			  WL1273_POW_ENB_EVENT)

/*
 * static unsigned int rds_buf - the number of RDS buffer blocks used.
 *
 * The default number is 100.
 */
static unsigned int rds_buf = 100;
module_param(rds_buf, uint, 0);
MODULE_PARM_DESC(rds_buf, "Number of RDS buffer entries. Default = 100");

static int wl1273_fm_write_fw(struct wl1273_core *core,
			      __u8 *fw, int len)
{
	struct i2c_client *client = core->client;
	struct i2c_msg msg;
	int i, r = 0;

	msg.addr = client->addr;
	msg.flags = 0;

	for (i = 0; i <= len; i++) {
		msg.len = fw[0];
		msg.buf = fw + 1;

		fw += msg.len + 1;
		dev_dbg(&client->dev, "%s:len[%d]: %d\n", __func__, i, msg.len);

		r = i2c_transfer(client->adapter, &msg, 1);
		if (r < 0 && i < len + 1)
			break;
	}

	dev_dbg(&client->dev, "%s: i: %d\n", __func__, i);
	dev_dbg(&client->dev, "%s: len + 1: %d\n", __func__, len + 1);

	/* Last transfer always fails. */
	if (i == len || r == 1)
		r = 0;

	return r;
}

#define WL1273_FIFO_HAS_DATA(status)	(1 << 5 & status)
#define WL1273_RDS_CORRECTABLE_ERROR	(1 << 3)
#define WL1273_RDS_UNCORRECTABLE_ERROR	(1 << 4)

static int wl1273_fm_rds(struct wl1273_device *radio)
{
	struct wl1273_core *core = radio->core;
	struct i2c_client *client = core->client;
	u16 val;
	u8 b0 = WL1273_RDS_DATA_GET, status;
	struct v4l2_rds_data rds = { 0, 0, 0 };
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = &b0,
			.len = 1,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = (u8 *) &rds,
			.len = sizeof(rds),
		}
	};
	int r;

	if (core->mode != WL1273_MODE_RX)
		return 0;

	r = core->read(core, WL1273_RDS_SYNC_GET, &val);
	if (r)
		return r;

	if ((val & 0x01) == 0) {
		/* RDS decoder not synchronized */
		return -EAGAIN;
	}

	/* copy all four RDS blocks to internal buffer */
	do {
		r = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
		if (r != ARRAY_SIZE(msg)) {
			dev_err(radio->dev, WL1273_FM_DRIVER_NAME
				": %s: read_rds error r == %i)\n",
				__func__, r);
		}

		status = rds.block;

		if (!WL1273_FIFO_HAS_DATA(status))
			break;

		/* copy bits 0-2 (the block ID) to bits 3-5 */
		rds.block = V4L2_RDS_BLOCK_MSK & status;
		rds.block |= rds.block << 3;

		/* copy the error bits to standard positions */
		if (WL1273_RDS_UNCORRECTABLE_ERROR & status) {
			rds.block |= V4L2_RDS_BLOCK_ERROR;
			rds.block &= ~V4L2_RDS_BLOCK_CORRECTED;
		} else if  (WL1273_RDS_CORRECTABLE_ERROR & status) {
			rds.block &= ~V4L2_RDS_BLOCK_ERROR;
			rds.block |= V4L2_RDS_BLOCK_CORRECTED;
		}

		/* copy RDS block to internal buffer */
		memcpy(&radio->buffer[radio->wr_index], &rds, RDS_BLOCK_SIZE);
		radio->wr_index += 3;

		/* wrap write pointer */
		if (radio->wr_index >= radio->buf_size)
			radio->wr_index = 0;

		/* check for overflow & start over */
		if (radio->wr_index == radio->rd_index) {
			dev_dbg(radio->dev, "RDS OVERFLOW");

			radio->rd_index = 0;
			radio->wr_index = 0;
			break;
		}
	} while (WL1273_FIFO_HAS_DATA(status));

	/* wake up read queue */
	if (radio->wr_index != radio->rd_index)
		wake_up_interruptible(&radio->read_queue);

	return 0;
}

static irqreturn_t wl1273_fm_irq_thread_handler(int irq, void *dev_id)
{
	struct wl1273_device *radio = dev_id;
	struct wl1273_core *core = radio->core;
	u16 flags;
	int r;

	r = core->read(core, WL1273_FLAG_GET, &flags);
	if (r)
		goto out;

	if (flags & WL1273_BL_EVENT) {
		radio->irq_received = flags;
		dev_dbg(radio->dev, "IRQ: BL\n");
	}

	if (flags & WL1273_RDS_EVENT) {
		msleep(200);

		wl1273_fm_rds(radio);
	}

	if (flags & WL1273_BBLK_EVENT)
		dev_dbg(radio->dev, "IRQ: BBLK\n");

	if (flags & WL1273_LSYNC_EVENT)
		dev_dbg(radio->dev, "IRQ: LSYNC\n");

	if (flags & WL1273_LEV_EVENT) {
		u16 level;

		r = core->read(core, WL1273_RSSI_LVL_GET, &level);
		if (r)
			goto out;

		if (level > 14)
			dev_dbg(radio->dev, "IRQ: LEV: 0x%x04\n", level);
	}

	if (flags & WL1273_IFFR_EVENT)
		dev_dbg(radio->dev, "IRQ: IFFR\n");

	if (flags & WL1273_PI_EVENT)
		dev_dbg(radio->dev, "IRQ: PI\n");

	if (flags & WL1273_PD_EVENT)
		dev_dbg(radio->dev, "IRQ: PD\n");

	if (flags & WL1273_STIC_EVENT)
		dev_dbg(radio->dev, "IRQ: STIC\n");

	if (flags & WL1273_MAL_EVENT)
		dev_dbg(radio->dev, "IRQ: MAL\n");

	if (flags & WL1273_POW_ENB_EVENT) {
		complete(&radio->busy);
		dev_dbg(radio->dev, "NOT BUSY\n");
		dev_dbg(radio->dev, "IRQ: POW_ENB\n");
	}

	if (flags & WL1273_SCAN_OVER_EVENT)
		dev_dbg(radio->dev, "IRQ: SCAN_OVER\n");

	if (flags & WL1273_ERROR_EVENT)
		dev_dbg(radio->dev, "IRQ: ERROR\n");

	if (flags & WL1273_FR_EVENT) {
		u16 freq;

		dev_dbg(radio->dev, "IRQ: FR:\n");

		if (core->mode == WL1273_MODE_RX) {
			r = core->write(core, WL1273_TUNER_MODE_SET,
					TUNER_MODE_STOP_SEARCH);
			if (r) {
				dev_err(radio->dev,
					"%s: TUNER_MODE_SET fails: %d\n",
					__func__, r);
				goto out;
			}

			r = core->read(core, WL1273_FREQ_SET, &freq);
			if (r)
				goto out;

			if (radio->band == WL1273_BAND_JAPAN)
				radio->rx_frequency = WL1273_BAND_JAPAN_LOW +
					freq * 50;
			else
				radio->rx_frequency = WL1273_BAND_OTHER_LOW +
					freq * 50;
			/*
			 *  The driver works better with this msleep,
			 *  the documentation doesn't mention it.
			 */
			usleep_range(10000, 15000);

			dev_dbg(radio->dev, "%dkHz\n", radio->rx_frequency);

		} else {
			r = core->read(core, WL1273_CHANL_SET, &freq);
			if (r)
				goto out;

			dev_dbg(radio->dev, "%dkHz\n", freq);
		}
		dev_dbg(radio->dev, "%s: NOT BUSY\n", __func__);
	}

out:
	core->write(core, WL1273_INT_MASK_SET, radio->irq_flags);
	complete(&radio->busy);

	return IRQ_HANDLED;
}

static int wl1273_fm_set_tx_freq(struct wl1273_device *radio, unsigned int freq)
{
	struct wl1273_core *core = radio->core;
	int r = 0;

	if (freq < WL1273_BAND_TX_LOW) {
		dev_err(radio->dev,
			"Frequency out of range: %d < %d\n", freq,
			WL1273_BAND_TX_LOW);
		return -ERANGE;
	}

	if (freq > WL1273_BAND_TX_HIGH) {
		dev_err(radio->dev,
			"Frequency out of range: %d > %d\n", freq,
			WL1273_BAND_TX_HIGH);
		return -ERANGE;
	}

	/*
	 *  The driver works better with this sleep,
	 *  the documentation doesn't mention it.
	 */
	usleep_range(5000, 10000);

	dev_dbg(radio->dev, "%s: freq: %d kHz\n", __func__, freq);

	/* Set the current tx channel */
	r = core->write(core, WL1273_CHANL_SET, freq / 10);
	if (r)
		return r;

	reinit_completion(&radio->busy);

	/* wait for the FR IRQ */
	r = wait_for_completion_timeout(&radio->busy, msecs_to_jiffies(2000));
	if (!r)
		return -ETIMEDOUT;

	dev_dbg(radio->dev, "WL1273_CHANL_SET: %d\n", r);

	/* Enable the output power */
	r = core->write(core, WL1273_POWER_ENB_SET, 1);
	if (r)
		return r;

	reinit_completion(&radio->busy);

	/* wait for the POWER_ENB IRQ */
	r = wait_for_completion_timeout(&radio->busy, msecs_to_jiffies(1000));
	if (!r)
		return -ETIMEDOUT;

	radio->tx_frequency = freq;
	dev_dbg(radio->dev, "WL1273_POWER_ENB_SET: %d\n", r);

	return	0;
}

static int wl1273_fm_set_rx_freq(struct wl1273_device *radio, unsigned int freq)
{
	struct wl1273_core *core = radio->core;
	int r, f;

	if (freq < radio->rangelow) {
		dev_err(radio->dev,
			"Frequency out of range: %d < %d\n", freq,
			radio->rangelow);
		r = -ERANGE;
		goto err;
	}

	if (freq > radio->rangehigh) {
		dev_err(radio->dev,
			"Frequency out of range: %d > %d\n", freq,
			radio->rangehigh);
		r = -ERANGE;
		goto err;
	}

	dev_dbg(radio->dev, "%s: %dkHz\n", __func__, freq);

	core->write(core, WL1273_INT_MASK_SET, radio->irq_flags);

	if (radio->band == WL1273_BAND_JAPAN)
		f = (freq - WL1273_BAND_JAPAN_LOW) / 50;
	else
		f = (freq - WL1273_BAND_OTHER_LOW) / 50;

	r = core->write(core, WL1273_FREQ_SET, f);
	if (r) {
		dev_err(radio->dev, "FREQ_SET fails\n");
		goto err;
	}

	r = core->write(core, WL1273_TUNER_MODE_SET, TUNER_MODE_PRESET);
	if (r) {
		dev_err(radio->dev, "TUNER_MODE_SET fails\n");
		goto err;
	}

	reinit_completion(&radio->busy);

	r = wait_for_completion_timeout(&radio->busy, msecs_to_jiffies(2000));
	if (!r) {
		dev_err(radio->dev, "%s: TIMEOUT\n", __func__);
		return -ETIMEDOUT;
	}

	radio->rd_index = 0;
	radio->wr_index = 0;
	radio->rx_frequency = freq;
	return 0;
err:
	return r;
}

static int wl1273_fm_get_freq(struct wl1273_device *radio)
{
	struct wl1273_core *core = radio->core;
	unsigned int freq;
	u16 f;
	int r;

	if (core->mode == WL1273_MODE_RX) {
		r = core->read(core, WL1273_FREQ_SET, &f);
		if (r)
			return r;

		dev_dbg(radio->dev, "Freq get: 0x%04x\n", f);
		if (radio->band == WL1273_BAND_JAPAN)
			freq = WL1273_BAND_JAPAN_LOW + 50 * f;
		else
			freq = WL1273_BAND_OTHER_LOW + 50 * f;
	} else {
		r = core->read(core, WL1273_CHANL_SET, &f);
		if (r)
			return r;

		freq = f * 10;
	}

	return freq;
}

/**
 * wl1273_fm_upload_firmware_patch() -	Upload the firmware.
 * @radio:				A pointer to the device struct.
 *
 * The firmware file consists of arrays of bytes where the first byte
 * gives the array length. The first byte in the file gives the
 * number of these arrays.
 */
static int wl1273_fm_upload_firmware_patch(struct wl1273_device *radio)
{
	struct wl1273_core *core = radio->core;
	unsigned int packet_num;
	const struct firmware *fw_p;
	const char *fw_name = "radio-wl1273-fw.bin";
	struct device *dev = radio->dev;
	__u8 *ptr;
	int r;

	dev_dbg(dev, "%s:\n", __func__);

	/*
	 * Uploading the firmware patch is not always necessary,
	 * so we only print an info message.
	 */
	if (request_firmware(&fw_p, fw_name, dev)) {
		dev_info(dev, "%s - %s not found\n", __func__, fw_name);

		return 0;
	}

	ptr = (__u8 *) fw_p->data;
	packet_num = ptr[0];
	dev_dbg(dev, "%s: packets: %d\n", __func__, packet_num);

	r = wl1273_fm_write_fw(core, ptr + 1, packet_num);
	if (r) {
		dev_err(dev, "FW upload error: %d\n", r);
		goto out;
	}

	/* ignore possible error here */
	core->write(core, WL1273_RESET, 0);

	dev_dbg(dev, "%s - download OK, r: %d\n", __func__, r);
out:
	release_firmware(fw_p);
	return r;
}

static int wl1273_fm_stop(struct wl1273_device *radio)
{
	struct wl1273_core *core = radio->core;

	if (core->mode == WL1273_MODE_RX) {
		int r = core->write(core, WL1273_POWER_SET,
				    WL1273_POWER_SET_OFF);
		if (r)
			dev_err(radio->dev, "%s: POWER_SET fails: %d\n",
				__func__, r);
	} else if (core->mode == WL1273_MODE_TX) {
		int r = core->write(core, WL1273_PUPD_SET,
				    WL1273_PUPD_SET_OFF);
		if (r)
			dev_err(radio->dev,
				"%s: PUPD_SET fails: %d\n", __func__, r);
	}

	if (core->pdata->disable) {
		core->pdata->disable();
		dev_dbg(radio->dev, "Back to reset\n");
	}

	return 0;
}

static int wl1273_fm_start(struct wl1273_device *radio, int new_mode)
{
	struct wl1273_core *core = radio->core;
	struct wl1273_fm_platform_data *pdata = core->pdata;
	struct device *dev = radio->dev;
	int r = -EINVAL;

	if (pdata->enable && core->mode == WL1273_MODE_OFF) {
		dev_dbg(radio->dev, "Out of reset\n");

		pdata->enable();
		msleep(250);
	}

	if (new_mode == WL1273_MODE_RX) {
		u16 val = WL1273_POWER_SET_FM;

		if (radio->rds_on)
			val |= WL1273_POWER_SET_RDS;

		/* If this fails try again */
		r = core->write(core, WL1273_POWER_SET, val);
		if (r) {
			msleep(100);

			r = core->write(core, WL1273_POWER_SET, val);
			if (r) {
				dev_err(dev, "%s: POWER_SET fails\n", __func__);
				goto fail;
			}
		}

		/* rds buffer configuration */
		radio->wr_index = 0;
		radio->rd_index = 0;

	} else if (new_mode == WL1273_MODE_TX) {
		/* If this fails try again once */
		r = core->write(core, WL1273_PUPD_SET, WL1273_PUPD_SET_ON);
		if (r) {
			msleep(100);
			r = core->write(core, WL1273_PUPD_SET,
					WL1273_PUPD_SET_ON);
			if (r) {
				dev_err(dev, "%s: PUPD_SET fails\n", __func__);
				goto fail;
			}
		}

		if (radio->rds_on)
			r = core->write(core, WL1273_RDS_DATA_ENB, 1);
		else
			r = core->write(core, WL1273_RDS_DATA_ENB, 0);
	} else {
		dev_warn(dev, "%s: Illegal mode.\n", __func__);
	}

	if (core->mode == WL1273_MODE_OFF) {
		r = wl1273_fm_upload_firmware_patch(radio);
		if (r)
			dev_warn(dev, "Firmware upload failed.\n");

		/*
		 * Sometimes the chip is in a wrong power state at this point.
		 * So we set the power once again.
		 */
		if (new_mode == WL1273_MODE_RX) {
			u16 val = WL1273_POWER_SET_FM;

			if (radio->rds_on)
				val |= WL1273_POWER_SET_RDS;

			r = core->write(core, WL1273_POWER_SET, val);
			if (r) {
				dev_err(dev, "%s: POWER_SET fails\n", __func__);
				goto fail;
			}
		} else if (new_mode == WL1273_MODE_TX) {
			r = core->write(core, WL1273_PUPD_SET,
					WL1273_PUPD_SET_ON);
			if (r) {
				dev_err(dev, "%s: PUPD_SET fails\n", __func__);
				goto fail;
			}
		}
	}

	return 0;
fail:
	if (pdata->disable)
		pdata->disable();

	dev_dbg(dev, "%s: return: %d\n", __func__, r);
	return r;
}

static int wl1273_fm_suspend(struct wl1273_device *radio)
{
	struct wl1273_core *core = radio->core;
	int r = 0;

	/* Cannot go from OFF to SUSPENDED */
	if (core->mode == WL1273_MODE_RX)
		r = core->write(core, WL1273_POWER_SET,
				WL1273_POWER_SET_RETENTION);
	else if (core->mode == WL1273_MODE_TX)
		r = core->write(core, WL1273_PUPD_SET,
				WL1273_PUPD_SET_RETENTION);
	else
		r = -EINVAL;

	if (r) {
		dev_err(radio->dev, "%s: POWER_SET fails: %d\n", __func__, r);
		goto out;
	}

out:
	return r;
}

static int wl1273_fm_set_mode(struct wl1273_device *radio, int mode)
{
	struct wl1273_core *core = radio->core;
	struct device *dev = radio->dev;
	int old_mode;
	int r;

	dev_dbg(dev, "%s\n", __func__);
	dev_dbg(dev, "Forbidden modes: 0x%02x\n", radio->forbidden);

	old_mode = core->mode;
	if (mode & radio->forbidden) {
		r = -EPERM;
		goto out;
	}

	switch (mode) {
	case WL1273_MODE_RX:
	case WL1273_MODE_TX:
		r = wl1273_fm_start(radio, mode);
		if (r) {
			dev_err(dev, "%s: Cannot start.\n", __func__);
			wl1273_fm_stop(radio);
			goto out;
		}

		core->mode = mode;
		r = core->write(core, WL1273_INT_MASK_SET, radio->irq_flags);
		if (r) {
			dev_err(dev, "INT_MASK_SET fails.\n");
			goto out;
		}

		/* remember previous settings */
		if (mode == WL1273_MODE_RX) {
			r = wl1273_fm_set_rx_freq(radio, radio->rx_frequency);
			if (r) {
				dev_err(dev, "set freq fails: %d.\n", r);
				goto out;
			}

			r = core->set_volume(core, core->volume);
			if (r) {
				dev_err(dev, "set volume fails: %d.\n", r);
				goto out;
			}

			dev_dbg(dev, "%s: Set vol: %d.\n", __func__,
				core->volume);
		} else {
			r = wl1273_fm_set_tx_freq(radio, radio->tx_frequency);
			if (r) {
				dev_err(dev, "set freq fails: %d.\n", r);
				goto out;
			}
		}

		dev_dbg(radio->dev, "%s: Set audio mode.\n", __func__);

		r = core->set_audio(core, core->audio_mode);
		if (r)
			dev_err(dev, "Cannot set audio mode.\n");
		break;

	case WL1273_MODE_OFF:
		r = wl1273_fm_stop(radio);
		if (r)
			dev_err(dev, "%s: Off fails: %d\n", __func__, r);
		else
			core->mode = WL1273_MODE_OFF;

		break;

	case WL1273_MODE_SUSPENDED:
		r = wl1273_fm_suspend(radio);
		if (r)
			dev_err(dev, "%s: Suspend fails: %d\n", __func__, r);
		else
			core->mode = WL1273_MODE_SUSPENDED;

		break;

	default:
		dev_err(dev, "%s: Unknown mode: %d\n", __func__, mode);
		r = -EINVAL;
		break;
	}
out:
	if (r)
		core->mode = old_mode;

	return r;
}

static int wl1273_fm_set_seek(struct wl1273_device *radio,
			      unsigned int wrap_around,
			      unsigned int seek_upward,
			      int level)
{
	struct wl1273_core *core = radio->core;
	int r = 0;
	unsigned int dir = (seek_upward == 0) ? 0 : 1;
	unsigned int f;

	f = radio->rx_frequency;
	dev_dbg(radio->dev, "rx_frequency: %d\n", f);

	if (dir && f + radio->spacing <= radio->rangehigh)
		r = wl1273_fm_set_rx_freq(radio, f + radio->spacing);
	else if (dir && wrap_around)
		r = wl1273_fm_set_rx_freq(radio, radio->rangelow);
	else if (f - radio->spacing >= radio->rangelow)
		r = wl1273_fm_set_rx_freq(radio, f - radio->spacing);
	else if (wrap_around)
		r = wl1273_fm_set_rx_freq(radio, radio->rangehigh);

	if (r)
		goto out;

	if (level < SCHAR_MIN || level > SCHAR_MAX)
		return -EINVAL;

	reinit_completion(&radio->busy);
	dev_dbg(radio->dev, "%s: BUSY\n", __func__);

	r = core->write(core, WL1273_INT_MASK_SET, radio->irq_flags);
	if (r)
		goto out;

	dev_dbg(radio->dev, "%s\n", __func__);

	r = core->write(core, WL1273_SEARCH_LVL_SET, level);
	if (r)
		goto out;

	r = core->write(core, WL1273_SEARCH_DIR_SET, dir);
	if (r)
		goto out;

	r = core->write(core, WL1273_TUNER_MODE_SET, TUNER_MODE_AUTO_SEEK);
	if (r)
		goto out;

	wait_for_completion_timeout(&radio->busy, msecs_to_jiffies(1000));
	if (!(radio->irq_received & WL1273_BL_EVENT))
		goto out;

	radio->irq_received &= ~WL1273_BL_EVENT;

	if (!wrap_around)
		goto out;

	/* Wrap around */
	dev_dbg(radio->dev, "Wrap around in HW seek.\n");

	if (seek_upward)
		f = radio->rangelow;
	else
		f = radio->rangehigh;

	r = wl1273_fm_set_rx_freq(radio, f);
	if (r)
		goto out;

	reinit_completion(&radio->busy);
	dev_dbg(radio->dev, "%s: BUSY\n", __func__);

	r = core->write(core, WL1273_TUNER_MODE_SET, TUNER_MODE_AUTO_SEEK);
	if (r)
		goto out;

	wait_for_completion_timeout(&radio->busy, msecs_to_jiffies(1000));
out:
	dev_dbg(radio->dev, "%s: Err: %d\n", __func__, r);
	return r;
}

/**
 * wl1273_fm_get_tx_ctune() -	Get the TX tuning capacitor value.
 * @radio:			A pointer to the device struct.
 */
static unsigned int wl1273_fm_get_tx_ctune(struct wl1273_device *radio)
{
	struct wl1273_core *core = radio->core;
	struct device *dev = radio->dev;
	u16 val;
	int r;

	if (core->mode == WL1273_MODE_OFF ||
	    core->mode == WL1273_MODE_SUSPENDED)
		return -EPERM;

	r = core->read(core, WL1273_READ_FMANT_TUNE_VALUE, &val);
	if (r) {
		dev_err(dev, "%s: read error: %d\n", __func__, r);
		goto out;
	}

out:
	return val;
}

/**
 * wl1273_fm_set_preemphasis() - Set the TX pre-emphasis value.
 * @radio:			 A pointer to the device struct.
 * @preemphasis:		 The new pre-amphasis value.
 *
 * Possible pre-emphasis values are: V4L2_PREEMPHASIS_DISABLED,
 * V4L2_PREEMPHASIS_50_uS and V4L2_PREEMPHASIS_75_uS.
 */
static int wl1273_fm_set_preemphasis(struct wl1273_device *radio,
				     unsigned int preemphasis)
{
	struct wl1273_core *core = radio->core;
	int r;
	u16 em;

	if (core->mode == WL1273_MODE_OFF ||
	    core->mode == WL1273_MODE_SUSPENDED)
		return -EPERM;

	mutex_lock(&core->lock);

	switch (preemphasis) {
	case V4L2_PREEMPHASIS_DISABLED:
		em = 1;
		break;
	case V4L2_PREEMPHASIS_50_uS:
		em = 0;
		break;
	case V4L2_PREEMPHASIS_75_uS:
		em = 2;
		break;
	default:
		r = -EINVAL;
		goto out;
	}

	r = core->write(core, WL1273_PREMPH_SET, em);
	if (r)
		goto out;

	radio->preemphasis = preemphasis;

out:
	mutex_unlock(&core->lock);
	return r;
}

static int wl1273_fm_rds_on(struct wl1273_device *radio)
{
	struct wl1273_core *core = radio->core;
	int r;

	dev_dbg(radio->dev, "%s\n", __func__);
	if (radio->rds_on)
		return 0;

	r = core->write(core, WL1273_POWER_SET,
			WL1273_POWER_SET_FM | WL1273_POWER_SET_RDS);
	if (r)
		goto out;

	r = wl1273_fm_set_rx_freq(radio, radio->rx_frequency);
	if (r)
		dev_err(radio->dev, "set freq fails: %d.\n", r);
out:
	return r;
}

static int wl1273_fm_rds_off(struct wl1273_device *radio)
{
	struct wl1273_core *core = radio->core;
	int r;

	if (!radio->rds_on)
		return 0;

	radio->irq_flags &= ~WL1273_RDS_EVENT;

	r = core->write(core, WL1273_INT_MASK_SET, radio->irq_flags);
	if (r)
		goto out;

	/* Service pending read */
	wake_up_interruptible(&radio->read_queue);

	dev_dbg(radio->dev, "%s\n", __func__);

	r = core->write(core, WL1273_POWER_SET, WL1273_POWER_SET_FM);
	if (r)
		goto out;

	r = wl1273_fm_set_rx_freq(radio, radio->rx_frequency);
	if (r)
		dev_err(radio->dev, "set freq fails: %d.\n", r);
out:
	dev_dbg(radio->dev, "%s: exiting...\n", __func__);

	return r;
}

static int wl1273_fm_set_rds(struct wl1273_device *radio, unsigned int new_mode)
{
	int r = 0;
	struct wl1273_core *core = radio->core;

	if (core->mode == WL1273_MODE_OFF ||
	    core->mode == WL1273_MODE_SUSPENDED)
		return -EPERM;

	if (new_mode == WL1273_RDS_RESET) {
		r = core->write(core, WL1273_RDS_CNTRL_SET, 1);
		return r;
	}

	if (core->mode == WL1273_MODE_TX && new_mode == WL1273_RDS_OFF) {
		r = core->write(core, WL1273_RDS_DATA_ENB, 0);
	} else if (core->mode == WL1273_MODE_TX && new_mode == WL1273_RDS_ON) {
		r = core->write(core, WL1273_RDS_DATA_ENB, 1);
	} else if (core->mode == WL1273_MODE_RX && new_mode == WL1273_RDS_OFF) {
		r = wl1273_fm_rds_off(radio);
	} else if (core->mode == WL1273_MODE_RX && new_mode == WL1273_RDS_ON) {
		r = wl1273_fm_rds_on(radio);
	} else {
		dev_err(radio->dev, "%s: Unknown mode: %d\n",
			__func__, new_mode);
		r = -EINVAL;
	}

	if (!r)
		radio->rds_on = (new_mode == WL1273_RDS_ON) ? true : false;

	return r;
}

static ssize_t wl1273_fm_fops_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	u16 val;
	int r;

	dev_dbg(radio->dev, "%s\n", __func__);

	if (core->mode != WL1273_MODE_TX)
		return count;

	if (radio->rds_users == 0) {
		dev_warn(radio->dev, "%s: RDS not on.\n", __func__);
		return 0;
	}

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;
	/*
	 * Multiple processes can open the device, but only
	 * one gets to write to it.
	 */
	if (radio->owner && radio->owner != file) {
		r = -EBUSY;
		goto out;
	}
	radio->owner = file;

	/* Manual Mode */
	if (count > 255)
		val = 255;
	else
		val = count;

	core->write(core, WL1273_RDS_CONFIG_DATA_SET, val);

	if (copy_from_user(radio->write_buf + 1, buf, val)) {
		r = -EFAULT;
		goto out;
	}

	dev_dbg(radio->dev, "Count: %d\n", val);
	dev_dbg(radio->dev, "From user: \"%s\"\n", radio->write_buf);

	radio->write_buf[0] = WL1273_RDS_DATA_SET;
	core->write_data(core, radio->write_buf, val + 1);

	r = val;
out:
	mutex_unlock(&core->lock);

	return r;
}

static unsigned int wl1273_fm_fops_poll(struct file *file,
					struct poll_table_struct *pts)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;

	if (radio->owner && radio->owner != file)
		return -EBUSY;

	radio->owner = file;

	if (core->mode == WL1273_MODE_RX) {
		poll_wait(file, &radio->read_queue, pts);

		if (radio->rd_index != radio->wr_index)
			return POLLIN | POLLRDNORM;

	} else if (core->mode == WL1273_MODE_TX) {
		return POLLOUT | POLLWRNORM;
	}

	return 0;
}

static int wl1273_fm_fops_open(struct file *file)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	int r = 0;

	dev_dbg(radio->dev, "%s\n", __func__);

	if (core->mode == WL1273_MODE_RX && radio->rds_on &&
	    !radio->rds_users) {
		dev_dbg(radio->dev, "%s: Mode: %d\n", __func__, core->mode);

		if (mutex_lock_interruptible(&core->lock))
			return -EINTR;

		radio->irq_flags |= WL1273_RDS_EVENT;

		r = core->write(core, WL1273_INT_MASK_SET,
				radio->irq_flags);
		if (r) {
			mutex_unlock(&core->lock);
			goto out;
		}

		radio->rds_users++;

		mutex_unlock(&core->lock);
	}
out:
	return r;
}

static int wl1273_fm_fops_release(struct file *file)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	int r = 0;

	dev_dbg(radio->dev, "%s\n", __func__);

	if (radio->rds_users > 0) {
		radio->rds_users--;
		if (radio->rds_users == 0) {
			if (mutex_lock_interruptible(&core->lock))
				return -EINTR;

			radio->irq_flags &= ~WL1273_RDS_EVENT;

			if (core->mode == WL1273_MODE_RX) {
				r = core->write(core,
						WL1273_INT_MASK_SET,
						radio->irq_flags);
				if (r) {
					mutex_unlock(&core->lock);
					goto out;
				}
			}
			mutex_unlock(&core->lock);
		}
	}

	if (file == radio->owner)
		radio->owner = NULL;
out:
	return r;
}

static ssize_t wl1273_fm_fops_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	int r = 0;
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	unsigned int block_count = 0;
	u16 val;

	dev_dbg(radio->dev, "%s\n", __func__);

	if (core->mode != WL1273_MODE_RX)
		return 0;

	if (radio->rds_users == 0) {
		dev_warn(radio->dev, "%s: RDS not on.\n", __func__);
		return 0;
	}

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;

	/*
	 * Multiple processes can open the device, but only
	 * one at a time gets read access.
	 */
	if (radio->owner && radio->owner != file) {
		r = -EBUSY;
		goto out;
	}
	radio->owner = file;

	r = core->read(core, WL1273_RDS_SYNC_GET, &val);
	if (r) {
		dev_err(radio->dev, "%s: Get RDS_SYNC fails.\n", __func__);
		goto out;
	} else if (val == 0) {
		dev_info(radio->dev, "RDS_SYNC: Not synchronized\n");
		r = -ENODATA;
		goto out;
	}

	/* block if no new data available */
	while (radio->wr_index == radio->rd_index) {
		if (file->f_flags & O_NONBLOCK) {
			r = -EWOULDBLOCK;
			goto out;
		}

		dev_dbg(radio->dev, "%s: Wait for RDS data.\n", __func__);
		if (wait_event_interruptible(radio->read_queue,
					     radio->wr_index !=
					     radio->rd_index) < 0) {
			r = -EINTR;
			goto out;
		}
	}

	/* calculate block count from byte count */
	count /= RDS_BLOCK_SIZE;

	/* copy RDS blocks from the internal buffer and to user buffer */
	while (block_count < count) {
		if (radio->rd_index == radio->wr_index)
			break;

		/* always transfer complete RDS blocks */
		if (copy_to_user(buf, &radio->buffer[radio->rd_index],
				 RDS_BLOCK_SIZE))
			break;

		/* increment and wrap the read pointer */
		radio->rd_index += RDS_BLOCK_SIZE;
		if (radio->rd_index >= radio->buf_size)
			radio->rd_index = 0;

		/* increment counters */
		block_count++;
		buf += RDS_BLOCK_SIZE;
		r += RDS_BLOCK_SIZE;
	}

out:
	dev_dbg(radio->dev, "%s: exit\n", __func__);
	mutex_unlock(&core->lock);

	return r;
}

static const struct v4l2_file_operations wl1273_fops = {
	.owner		= THIS_MODULE,
	.read		= wl1273_fm_fops_read,
	.write		= wl1273_fm_fops_write,
	.poll		= wl1273_fm_fops_poll,
	.unlocked_ioctl	= video_ioctl2,
	.open		= wl1273_fm_fops_open,
	.release	= wl1273_fm_fops_release,
};

static int wl1273_fm_vidioc_querycap(struct file *file, void *priv,
				     struct v4l2_capability *capability)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));

	dev_dbg(radio->dev, "%s\n", __func__);

	strlcpy(capability->driver, WL1273_FM_DRIVER_NAME,
		sizeof(capability->driver));
	strlcpy(capability->card, "Texas Instruments Wl1273 FM Radio",
		sizeof(capability->card));
	strlcpy(capability->bus_info, radio->bus_type,
		sizeof(capability->bus_info));

	capability->device_caps = V4L2_CAP_HW_FREQ_SEEK |
		V4L2_CAP_TUNER | V4L2_CAP_RADIO | V4L2_CAP_AUDIO |
		V4L2_CAP_RDS_CAPTURE | V4L2_CAP_MODULATOR |
		V4L2_CAP_RDS_OUTPUT;
	capability->capabilities = capability->device_caps |
		V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int wl1273_fm_vidioc_g_input(struct file *file, void *priv,
				    unsigned int *i)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));

	dev_dbg(radio->dev, "%s\n", __func__);

	*i = 0;

	return 0;
}

static int wl1273_fm_vidioc_s_input(struct file *file, void *priv,
				    unsigned int i)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));

	dev_dbg(radio->dev, "%s\n", __func__);

	if (i != 0)
		return -EINVAL;

	return 0;
}

/**
 * wl1273_fm_set_tx_power() -	Set the transmission power value.
 * @core:			A pointer to the device struct.
 * @power:			The new power value.
 */
static int wl1273_fm_set_tx_power(struct wl1273_device *radio, u16 power)
{
	struct wl1273_core *core = radio->core;
	int r;

	if (core->mode == WL1273_MODE_OFF ||
	    core->mode == WL1273_MODE_SUSPENDED)
		return -EPERM;

	mutex_lock(&core->lock);

	/* Convert the dBuV value to chip presentation */
	r = core->write(core, WL1273_POWER_LEV_SET, 122 - power);
	if (r)
		goto out;

	radio->tx_power = power;

out:
	mutex_unlock(&core->lock);
	return r;
}

#define WL1273_SPACING_50kHz	1
#define WL1273_SPACING_100kHz	2
#define WL1273_SPACING_200kHz	4

static int wl1273_fm_tx_set_spacing(struct wl1273_device *radio,
				    unsigned int spacing)
{
	struct wl1273_core *core = radio->core;
	int r;

	if (spacing == 0) {
		r = core->write(core, WL1273_SCAN_SPACING_SET,
				WL1273_SPACING_100kHz);
		radio->spacing = 100;
	} else if (spacing - 50000 < 25000) {
		r = core->write(core, WL1273_SCAN_SPACING_SET,
				WL1273_SPACING_50kHz);
		radio->spacing = 50;
	} else if (spacing - 100000 < 50000) {
		r = core->write(core, WL1273_SCAN_SPACING_SET,
				WL1273_SPACING_100kHz);
		radio->spacing = 100;
	} else {
		r = core->write(core, WL1273_SCAN_SPACING_SET,
				WL1273_SPACING_200kHz);
		radio->spacing = 200;
	}

	return r;
}

static int wl1273_fm_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct wl1273_device *radio = ctrl->priv;
	struct wl1273_core *core = radio->core;

	dev_dbg(radio->dev, "%s\n", __func__);

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;

	switch (ctrl->id) {
	case  V4L2_CID_TUNE_ANTENNA_CAPACITOR:
		ctrl->val = wl1273_fm_get_tx_ctune(radio);
		break;

	default:
		dev_warn(radio->dev, "%s: Unknown IOCTL: %d\n",
			 __func__, ctrl->id);
		break;
	}

	mutex_unlock(&core->lock);

	return 0;
}

#define WL1273_MUTE_SOFT_ENABLE    (1 << 0)
#define WL1273_MUTE_AC             (1 << 1)
#define WL1273_MUTE_HARD_LEFT      (1 << 2)
#define WL1273_MUTE_HARD_RIGHT     (1 << 3)
#define WL1273_MUTE_SOFT_FORCE     (1 << 4)

static inline struct wl1273_device *to_radio(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct wl1273_device, ctrl_handler);
}

static int wl1273_fm_vidioc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct wl1273_device *radio = to_radio(ctrl);
	struct wl1273_core *core = radio->core;
	int r = 0;

	dev_dbg(radio->dev, "%s\n", __func__);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (mutex_lock_interruptible(&core->lock))
			return -EINTR;

		if (core->mode == WL1273_MODE_RX && ctrl->val)
			r = core->write(core,
					WL1273_MUTE_STATUS_SET,
					WL1273_MUTE_HARD_LEFT |
					WL1273_MUTE_HARD_RIGHT);
		else if (core->mode == WL1273_MODE_RX)
			r = core->write(core,
					WL1273_MUTE_STATUS_SET, 0x0);
		else if (core->mode == WL1273_MODE_TX && ctrl->val)
			r = core->write(core, WL1273_MUTE, 1);
		else if (core->mode == WL1273_MODE_TX)
			r = core->write(core, WL1273_MUTE, 0);

		mutex_unlock(&core->lock);
		break;

	case V4L2_CID_AUDIO_VOLUME:
		if (ctrl->val == 0)
			r = wl1273_fm_set_mode(radio, WL1273_MODE_OFF);
		else
			r =  core->set_volume(core, core->volume);
		break;

	case V4L2_CID_TUNE_PREEMPHASIS:
		r = wl1273_fm_set_preemphasis(radio, ctrl->val);
		break;

	case V4L2_CID_TUNE_POWER_LEVEL:
		r = wl1273_fm_set_tx_power(radio, ctrl->val);
		break;

	default:
		dev_warn(radio->dev, "%s: Unknown IOCTL: %d\n",
			 __func__, ctrl->id);
		break;
	}

	dev_dbg(radio->dev, "%s\n", __func__);
	return r;
}

static int wl1273_fm_vidioc_g_audio(struct file *file, void *priv,
				    struct v4l2_audio *audio)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));

	dev_dbg(radio->dev, "%s\n", __func__);

	if (audio->index > 1)
		return -EINVAL;

	strlcpy(audio->name, "Radio", sizeof(audio->name));
	audio->capability = V4L2_AUDCAP_STEREO;

	return 0;
}

static int wl1273_fm_vidioc_s_audio(struct file *file, void *priv,
				    const struct v4l2_audio *audio)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));

	dev_dbg(radio->dev, "%s\n", __func__);

	if (audio->index != 0)
		return -EINVAL;

	return 0;
}

#define WL1273_RDS_NOT_SYNCHRONIZED 0
#define WL1273_RDS_SYNCHRONIZED 1

static int wl1273_fm_vidioc_g_tuner(struct file *file, void *priv,
				    struct v4l2_tuner *tuner)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	u16 val;
	int r;

	dev_dbg(radio->dev, "%s\n", __func__);

	if (tuner->index > 0)
		return -EINVAL;

	strlcpy(tuner->name, WL1273_FM_DRIVER_NAME, sizeof(tuner->name));
	tuner->type = V4L2_TUNER_RADIO;

	tuner->rangelow	= WL1273_FREQ(WL1273_BAND_JAPAN_LOW);
	tuner->rangehigh = WL1273_FREQ(WL1273_BAND_OTHER_HIGH);

	tuner->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_RDS |
		V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_RDS_BLOCK_IO |
		V4L2_TUNER_CAP_HWSEEK_BOUNDED | V4L2_TUNER_CAP_HWSEEK_WRAP;

	if (radio->stereo)
		tuner->audmode = V4L2_TUNER_MODE_STEREO;
	else
		tuner->audmode = V4L2_TUNER_MODE_MONO;

	if (core->mode != WL1273_MODE_RX)
		return 0;

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;

	r = core->read(core, WL1273_STEREO_GET, &val);
	if (r)
		goto out;

	if (val == 1)
		tuner->rxsubchans = V4L2_TUNER_SUB_STEREO;
	else
		tuner->rxsubchans = V4L2_TUNER_SUB_MONO;

	r = core->read(core, WL1273_RSSI_LVL_GET, &val);
	if (r)
		goto out;

	tuner->signal = (s16) val;
	dev_dbg(radio->dev, "Signal: %d\n", tuner->signal);

	tuner->afc = 0;

	r = core->read(core, WL1273_RDS_SYNC_GET, &val);
	if (r)
		goto out;

	if (val == WL1273_RDS_SYNCHRONIZED)
		tuner->rxsubchans |= V4L2_TUNER_SUB_RDS;
out:
	mutex_unlock(&core->lock);

	return r;
}

static int wl1273_fm_vidioc_s_tuner(struct file *file, void *priv,
				    const struct v4l2_tuner *tuner)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	int r = 0;

	dev_dbg(radio->dev, "%s\n", __func__);
	dev_dbg(radio->dev, "tuner->index: %d\n", tuner->index);
	dev_dbg(radio->dev, "tuner->name: %s\n", tuner->name);
	dev_dbg(radio->dev, "tuner->capability: 0x%04x\n", tuner->capability);
	dev_dbg(radio->dev, "tuner->rxsubchans: 0x%04x\n", tuner->rxsubchans);
	dev_dbg(radio->dev, "tuner->rangelow: %d\n", tuner->rangelow);
	dev_dbg(radio->dev, "tuner->rangehigh: %d\n", tuner->rangehigh);

	if (tuner->index > 0)
		return -EINVAL;

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;

	r = wl1273_fm_set_mode(radio, WL1273_MODE_RX);
	if (r)
		goto out;

	if (tuner->rxsubchans & V4L2_TUNER_SUB_RDS)
		r = wl1273_fm_set_rds(radio, WL1273_RDS_ON);
	else
		r = wl1273_fm_set_rds(radio, WL1273_RDS_OFF);

	if (r)
		dev_warn(radio->dev, "%s: RDS fails: %d\n", __func__, r);

	if (tuner->audmode == V4L2_TUNER_MODE_MONO) {
		r = core->write(core, WL1273_MOST_MODE_SET, WL1273_RX_MONO);
		if (r < 0) {
			dev_warn(radio->dev, "%s: MOST_MODE fails: %d\n",
				 __func__, r);
			goto out;
		}
		radio->stereo = false;
	} else if (tuner->audmode == V4L2_TUNER_MODE_STEREO) {
		r = core->write(core, WL1273_MOST_MODE_SET, WL1273_RX_STEREO);
		if (r < 0) {
			dev_warn(radio->dev, "%s: MOST_MODE fails: %d\n",
				 __func__, r);
			goto out;
		}
		radio->stereo = true;
	} else {
		dev_err(radio->dev, "%s: tuner->audmode: %d\n",
			 __func__, tuner->audmode);
		r = -EINVAL;
		goto out;
	}

out:
	mutex_unlock(&core->lock);

	return r;
}

static int wl1273_fm_vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *freq)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;

	dev_dbg(radio->dev, "%s\n", __func__);

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;

	freq->type = V4L2_TUNER_RADIO;
	freq->frequency = WL1273_FREQ(wl1273_fm_get_freq(radio));

	mutex_unlock(&core->lock);

	return 0;
}

static int wl1273_fm_vidioc_s_frequency(struct file *file, void *priv,
					const struct v4l2_frequency *freq)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	int r;

	dev_dbg(radio->dev, "%s: %d\n", __func__, freq->frequency);

	if (freq->type != V4L2_TUNER_RADIO) {
		dev_dbg(radio->dev,
			"freq->type != V4L2_TUNER_RADIO: %d\n", freq->type);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;

	if (core->mode == WL1273_MODE_RX) {
		dev_dbg(radio->dev, "freq: %d\n", freq->frequency);

		r = wl1273_fm_set_rx_freq(radio,
					  WL1273_INV_FREQ(freq->frequency));
		if (r)
			dev_warn(radio->dev, WL1273_FM_DRIVER_NAME
				 ": set frequency failed with %d\n", r);
	} else {
		r = wl1273_fm_set_tx_freq(radio,
					  WL1273_INV_FREQ(freq->frequency));
		if (r)
			dev_warn(radio->dev, WL1273_FM_DRIVER_NAME
				 ": set frequency failed with %d\n", r);
	}

	mutex_unlock(&core->lock);

	dev_dbg(radio->dev, "wl1273_vidioc_s_frequency: DONE\n");
	return r;
}

#define WL1273_DEFAULT_SEEK_LEVEL	7

static int wl1273_fm_vidioc_s_hw_freq_seek(struct file *file, void *priv,
					   const struct v4l2_hw_freq_seek *seek)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	int r;

	dev_dbg(radio->dev, "%s\n", __func__);

	if (seek->tuner != 0 || seek->type != V4L2_TUNER_RADIO)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK)
		return -EWOULDBLOCK;

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;

	r = wl1273_fm_set_mode(radio, WL1273_MODE_RX);
	if (r)
		goto out;

	r = wl1273_fm_tx_set_spacing(radio, seek->spacing);
	if (r)
		dev_warn(radio->dev, "HW seek failed: %d\n", r);

	r = wl1273_fm_set_seek(radio, seek->wrap_around, seek->seek_upward,
			       WL1273_DEFAULT_SEEK_LEVEL);
	if (r)
		dev_warn(radio->dev, "HW seek failed: %d\n", r);

out:
	mutex_unlock(&core->lock);
	return r;
}

static int wl1273_fm_vidioc_s_modulator(struct file *file, void *priv,
					const struct v4l2_modulator *modulator)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	int r = 0;

	dev_dbg(radio->dev, "%s\n", __func__);

	if (modulator->index > 0)
		return -EINVAL;

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;

	r = wl1273_fm_set_mode(radio, WL1273_MODE_TX);
	if (r)
		goto out;

	if (modulator->txsubchans & V4L2_TUNER_SUB_RDS)
		r = wl1273_fm_set_rds(radio, WL1273_RDS_ON);
	else
		r = wl1273_fm_set_rds(radio, WL1273_RDS_OFF);

	if (modulator->txsubchans & V4L2_TUNER_SUB_MONO)
		r = core->write(core, WL1273_MONO_SET, WL1273_TX_MONO);
	else
		r = core->write(core, WL1273_MONO_SET,
				WL1273_RX_STEREO);
	if (r < 0)
		dev_warn(radio->dev, WL1273_FM_DRIVER_NAME
			 "MONO_SET fails: %d\n", r);
out:
	mutex_unlock(&core->lock);

	return r;
}

static int wl1273_fm_vidioc_g_modulator(struct file *file, void *priv,
					struct v4l2_modulator *modulator)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	u16 val;
	int r;

	dev_dbg(radio->dev, "%s\n", __func__);

	strlcpy(modulator->name, WL1273_FM_DRIVER_NAME,
		sizeof(modulator->name));

	modulator->rangelow = WL1273_FREQ(WL1273_BAND_JAPAN_LOW);
	modulator->rangehigh = WL1273_FREQ(WL1273_BAND_OTHER_HIGH);

	modulator->capability =  V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_RDS |
		V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_RDS_BLOCK_IO;

	if (core->mode != WL1273_MODE_TX)
		return 0;

	if (mutex_lock_interruptible(&core->lock))
		return -EINTR;

	r = core->read(core, WL1273_MONO_SET, &val);
	if (r)
		goto out;

	if (val == WL1273_TX_STEREO)
		modulator->txsubchans = V4L2_TUNER_SUB_STEREO;
	else
		modulator->txsubchans = V4L2_TUNER_SUB_MONO;

	if (radio->rds_on)
		modulator->txsubchans |= V4L2_TUNER_SUB_RDS;
out:
	mutex_unlock(&core->lock);

	return 0;
}

static int wl1273_fm_vidioc_log_status(struct file *file, void *priv)
{
	struct wl1273_device *radio = video_get_drvdata(video_devdata(file));
	struct wl1273_core *core = radio->core;
	struct device *dev = radio->dev;
	u16 val;
	int r;

	dev_info(dev, DRIVER_DESC);

	if (core->mode == WL1273_MODE_OFF) {
		dev_info(dev, "Mode: Off\n");
		return 0;
	}

	if (core->mode == WL1273_MODE_SUSPENDED) {
		dev_info(dev, "Mode: Suspended\n");
		return 0;
	}

	r = core->read(core, WL1273_ASIC_ID_GET, &val);
	if (r)
		dev_err(dev, "%s: Get ASIC_ID fails.\n", __func__);
	else
		dev_info(dev, "ASIC_ID: 0x%04x\n", val);

	r = core->read(core, WL1273_ASIC_VER_GET, &val);
	if (r)
		dev_err(dev, "%s: Get ASIC_VER fails.\n", __func__);
	else
		dev_info(dev, "ASIC Version: 0x%04x\n", val);

	r = core->read(core, WL1273_FIRM_VER_GET, &val);
	if (r)
		dev_err(dev, "%s: Get FIRM_VER fails.\n", __func__);
	else
		dev_info(dev, "FW version: %d(0x%04x)\n", val, val);

	r = core->read(core, WL1273_BAND_SET, &val);
	if (r)
		dev_err(dev, "%s: Get BAND fails.\n", __func__);
	else
		dev_info(dev, "BAND: %d\n", val);

	if (core->mode == WL1273_MODE_TX) {
		r = core->read(core, WL1273_PUPD_SET, &val);
		if (r)
			dev_err(dev, "%s: Get PUPD fails.\n", __func__);
		else
			dev_info(dev, "PUPD: 0x%04x\n", val);

		r = core->read(core, WL1273_CHANL_SET, &val);
		if (r)
			dev_err(dev, "%s: Get CHANL fails.\n", __func__);
		else
			dev_info(dev, "Tx frequency: %dkHz\n", val*10);
	} else if (core->mode == WL1273_MODE_RX) {
		int bf = radio->rangelow;

		r = core->read(core, WL1273_FREQ_SET, &val);
		if (r)
			dev_err(dev, "%s: Get FREQ fails.\n", __func__);
		else
			dev_info(dev, "RX Frequency: %dkHz\n", bf + val*50);

		r = core->read(core, WL1273_MOST_MODE_SET, &val);
		if (r)
			dev_err(dev, "%s: Get MOST_MODE fails.\n",
				__func__);
		else if (val == 0)
			dev_info(dev, "MOST_MODE: Stereo according to blend\n");
		else if (val == 1)
			dev_info(dev, "MOST_MODE: Force mono output\n");
		else
			dev_info(dev, "MOST_MODE: Unexpected value: %d\n", val);

		r = core->read(core, WL1273_MOST_BLEND_SET, &val);
		if (r)
			dev_err(dev, "%s: Get MOST_BLEND fails.\n", __func__);
		else if (val == 0)
			dev_info(dev,
				 "MOST_BLEND: Switched blend & hysteresis.\n");
		else if (val == 1)
			dev_info(dev, "MOST_BLEND: Soft blend.\n");
		else
			dev_info(dev, "MOST_BLEND: Unexpected val: %d\n", val);

		r = core->read(core, WL1273_STEREO_GET, &val);
		if (r)
			dev_err(dev, "%s: Get STEREO fails.\n", __func__);
		else if (val == 0)
			dev_info(dev, "STEREO: Not detected\n");
		else if (val == 1)
			dev_info(dev, "STEREO: Detected\n");
		else
			dev_info(dev, "STEREO: Unexpected value: %d\n", val);

		r = core->read(core, WL1273_RSSI_LVL_GET, &val);
		if (r)
			dev_err(dev, "%s: Get RSSI_LVL fails.\n", __func__);
		else
			dev_info(dev, "RX signal strength: %d\n", (s16) val);

		r = core->read(core, WL1273_POWER_SET, &val);
		if (r)
			dev_err(dev, "%s: Get POWER fails.\n", __func__);
		else
			dev_info(dev, "POWER: 0x%04x\n", val);

		r = core->read(core, WL1273_INT_MASK_SET, &val);
		if (r)
			dev_err(dev, "%s: Get INT_MASK fails.\n", __func__);
		else
			dev_info(dev, "INT_MASK: 0x%04x\n", val);

		r = core->read(core, WL1273_RDS_SYNC_GET, &val);
		if (r)
			dev_err(dev, "%s: Get RDS_SYNC fails.\n",
				__func__);
		else if (val == 0)
			dev_info(dev, "RDS_SYNC: Not synchronized\n");

		else if (val == 1)
			dev_info(dev, "RDS_SYNC: Synchronized\n");
		else
			dev_info(dev, "RDS_SYNC: Unexpected value: %d\n", val);

		r = core->read(core, WL1273_I2S_MODE_CONFIG_SET, &val);
		if (r)
			dev_err(dev, "%s: Get I2S_MODE_CONFIG fails.\n",
				__func__);
		else
			dev_info(dev, "I2S_MODE_CONFIG: 0x%04x\n", val);

		r = core->read(core, WL1273_VOLUME_SET, &val);
		if (r)
			dev_err(dev, "%s: Get VOLUME fails.\n", __func__);
		else
			dev_info(dev, "VOLUME: 0x%04x\n", val);
	}

	return 0;
}

static void wl1273_vdev_release(struct video_device *dev)
{
}

static const struct v4l2_ctrl_ops wl1273_ctrl_ops = {
	.s_ctrl = wl1273_fm_vidioc_s_ctrl,
	.g_volatile_ctrl = wl1273_fm_g_volatile_ctrl,
};

static const struct v4l2_ioctl_ops wl1273_ioctl_ops = {
	.vidioc_querycap	= wl1273_fm_vidioc_querycap,
	.vidioc_g_input		= wl1273_fm_vidioc_g_input,
	.vidioc_s_input		= wl1273_fm_vidioc_s_input,
	.vidioc_g_audio		= wl1273_fm_vidioc_g_audio,
	.vidioc_s_audio		= wl1273_fm_vidioc_s_audio,
	.vidioc_g_tuner		= wl1273_fm_vidioc_g_tuner,
	.vidioc_s_tuner		= wl1273_fm_vidioc_s_tuner,
	.vidioc_g_frequency	= wl1273_fm_vidioc_g_frequency,
	.vidioc_s_frequency	= wl1273_fm_vidioc_s_frequency,
	.vidioc_s_hw_freq_seek	= wl1273_fm_vidioc_s_hw_freq_seek,
	.vidioc_g_modulator	= wl1273_fm_vidioc_g_modulator,
	.vidioc_s_modulator	= wl1273_fm_vidioc_s_modulator,
	.vidioc_log_status      = wl1273_fm_vidioc_log_status,
};

static struct video_device wl1273_viddev_template = {
	.fops			= &wl1273_fops,
	.ioctl_ops		= &wl1273_ioctl_ops,
	.name			= WL1273_FM_DRIVER_NAME,
	.release		= wl1273_vdev_release,
	.vfl_dir		= VFL_DIR_TX,
};

static int wl1273_fm_radio_remove(struct platform_device *pdev)
{
	struct wl1273_device *radio = platform_get_drvdata(pdev);
	struct wl1273_core *core = radio->core;

	dev_info(&pdev->dev, "%s.\n", __func__);

	free_irq(core->client->irq, radio);
	core->pdata->free_resources();

	v4l2_ctrl_handler_free(&radio->ctrl_handler);
	video_unregister_device(&radio->videodev);
	v4l2_device_unregister(&radio->v4l2dev);

	return 0;
}

static int wl1273_fm_radio_probe(struct platform_device *pdev)
{
	struct wl1273_core **core = pdev->dev.platform_data;
	struct wl1273_device *radio;
	struct v4l2_ctrl *ctrl;
	int r = 0;

	pr_debug("%s\n", __func__);

	if (!core) {
		dev_err(&pdev->dev, "No platform data.\n");
		r = -EINVAL;
		goto pdata_err;
	}

	radio = devm_kzalloc(&pdev->dev, sizeof(*radio), GFP_KERNEL);
	if (!radio) {
		r = -ENOMEM;
		goto pdata_err;
	}

	/* RDS buffer allocation */
	radio->buf_size = rds_buf * RDS_BLOCK_SIZE;
	radio->buffer = devm_kzalloc(&pdev->dev, radio->buf_size, GFP_KERNEL);
	if (!radio->buffer) {
		pr_err("Cannot allocate memory for RDS buffer.\n");
		r = -ENOMEM;
		goto pdata_err;
	}

	radio->core = *core;
	radio->irq_flags = WL1273_IRQ_MASK;
	radio->dev = &radio->core->client->dev;
	radio->rds_on = false;
	radio->core->mode = WL1273_MODE_OFF;
	radio->tx_power = 118;
	radio->core->audio_mode = WL1273_AUDIO_ANALOG;
	radio->band = WL1273_BAND_OTHER;
	radio->core->i2s_mode = WL1273_I2S_DEF_MODE;
	radio->core->channel_number = 2;
	radio->core->volume = WL1273_DEFAULT_VOLUME;
	radio->rx_frequency = WL1273_BAND_OTHER_LOW;
	radio->tx_frequency = WL1273_BAND_OTHER_HIGH;
	radio->rangelow = WL1273_BAND_OTHER_LOW;
	radio->rangehigh = WL1273_BAND_OTHER_HIGH;
	radio->stereo = true;
	radio->bus_type = "I2C";

	if (radio->core->pdata->request_resources) {
		r = radio->core->pdata->request_resources(radio->core->client);
		if (r) {
			dev_err(radio->dev, WL1273_FM_DRIVER_NAME
				": Cannot get platform data\n");
			goto pdata_err;
		}

		dev_dbg(radio->dev, "irq: %d\n", radio->core->client->irq);

		r = request_threaded_irq(radio->core->client->irq, NULL,
					 wl1273_fm_irq_thread_handler,
					 IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
					 "wl1273-fm", radio);
		if (r < 0) {
			dev_err(radio->dev, WL1273_FM_DRIVER_NAME
				": Unable to register IRQ handler: %d\n", r);
			goto err_request_irq;
		}
	} else {
		dev_err(radio->dev, WL1273_FM_DRIVER_NAME ": Core WL1273 IRQ"
			" not configured");
		r = -EINVAL;
		goto pdata_err;
	}

	init_completion(&radio->busy);
	init_waitqueue_head(&radio->read_queue);

	radio->write_buf = devm_kzalloc(&pdev->dev, 256, GFP_KERNEL);
	if (!radio->write_buf) {
		r = -ENOMEM;
		goto write_buf_err;
	}

	radio->dev = &pdev->dev;
	radio->v4l2dev.ctrl_handler = &radio->ctrl_handler;
	radio->rds_users = 0;

	r = v4l2_device_register(&pdev->dev, &radio->v4l2dev);
	if (r) {
		dev_err(&pdev->dev, "Cannot register v4l2_device.\n");
		goto write_buf_err;
	}

	/* V4L2 configuration */
	radio->videodev = wl1273_viddev_template;

	radio->videodev.v4l2_dev = &radio->v4l2dev;

	v4l2_ctrl_handler_init(&radio->ctrl_handler, 6);

	/* add in ascending ID order */
	v4l2_ctrl_new_std(&radio->ctrl_handler, &wl1273_ctrl_ops,
			  V4L2_CID_AUDIO_VOLUME, 0, WL1273_MAX_VOLUME, 1,
			  WL1273_DEFAULT_VOLUME);

	v4l2_ctrl_new_std(&radio->ctrl_handler, &wl1273_ctrl_ops,
			  V4L2_CID_AUDIO_MUTE, 0, 1, 1, 1);

	v4l2_ctrl_new_std_menu(&radio->ctrl_handler, &wl1273_ctrl_ops,
			       V4L2_CID_TUNE_PREEMPHASIS,
			       V4L2_PREEMPHASIS_75_uS, 0x03,
			       V4L2_PREEMPHASIS_50_uS);

	v4l2_ctrl_new_std(&radio->ctrl_handler, &wl1273_ctrl_ops,
			  V4L2_CID_TUNE_POWER_LEVEL, 91, 122, 1, 118);

	ctrl = v4l2_ctrl_new_std(&radio->ctrl_handler, &wl1273_ctrl_ops,
				 V4L2_CID_TUNE_ANTENNA_CAPACITOR,
				 0, 255, 1, 255);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (radio->ctrl_handler.error) {
		r = radio->ctrl_handler.error;
		dev_err(&pdev->dev, "Ctrl handler error: %d\n", r);
		goto handler_init_err;
	}

	video_set_drvdata(&radio->videodev, radio);
	platform_set_drvdata(pdev, radio);

	/* register video device */
	r = video_register_device(&radio->videodev, VFL_TYPE_RADIO, radio_nr);
	if (r) {
		dev_err(&pdev->dev, WL1273_FM_DRIVER_NAME
			": Could not register video device\n");
		goto handler_init_err;
	}

	return 0;

handler_init_err:
	v4l2_ctrl_handler_free(&radio->ctrl_handler);
	v4l2_device_unregister(&radio->v4l2dev);
write_buf_err:
	free_irq(radio->core->client->irq, radio);
err_request_irq:
	radio->core->pdata->free_resources();
pdata_err:
	return r;
}

static struct platform_driver wl1273_fm_radio_driver = {
	.probe		= wl1273_fm_radio_probe,
	.remove		= wl1273_fm_radio_remove,
	.driver		= {
		.name	= "wl1273_fm_radio",
	},
};

module_platform_driver(wl1273_fm_radio_driver);

MODULE_AUTHOR("Matti Aaltonen <matti.j.aaltonen@nokia.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wl1273_fm_radio");
