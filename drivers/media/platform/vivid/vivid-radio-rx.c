/*
 * vivid-radio-rx.c - radio receiver support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/sched/signal.h>

#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-dv-timings.h>

#include "vivid-core.h"
#include "vivid-ctrls.h"
#include "vivid-radio-common.h"
#include "vivid-rds-gen.h"
#include "vivid-radio-rx.h"

ssize_t vivid_radio_rx_read(struct file *file, char __user *buf,
			 size_t size, loff_t *offset)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_rds_data *data = dev->rds_gen.data;
	bool use_alternates;
	ktime_t timestamp;
	unsigned blk;
	int perc;
	int i;

	if (dev->radio_rx_rds_controls)
		return -EINVAL;
	if (size < sizeof(*data))
		return 0;
	size = sizeof(*data) * (size / sizeof(*data));

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	if (dev->radio_rx_rds_owner &&
	    file->private_data != dev->radio_rx_rds_owner) {
		mutex_unlock(&dev->mutex);
		return -EBUSY;
	}
	if (dev->radio_rx_rds_owner == NULL) {
		vivid_radio_rds_init(dev);
		dev->radio_rx_rds_owner = file->private_data;
	}

retry:
	timestamp = ktime_sub(ktime_get(), dev->radio_rds_init_time);
	blk = ktime_divns(timestamp, VIVID_RDS_NSEC_PER_BLK);
	use_alternates = (blk % VIVID_RDS_GEN_BLOCKS) & 1;

	if (dev->radio_rx_rds_last_block == 0 ||
	    dev->radio_rx_rds_use_alternates != use_alternates) {
		dev->radio_rx_rds_use_alternates = use_alternates;
		/* Re-init the RDS generator */
		vivid_radio_rds_init(dev);
	}
	if (blk >= dev->radio_rx_rds_last_block + VIVID_RDS_GEN_BLOCKS)
		dev->radio_rx_rds_last_block = blk - VIVID_RDS_GEN_BLOCKS + 1;

	/*
	 * No data is available if there hasn't been time to get new data,
	 * or if the RDS receiver has been disabled, or if we use the data
	 * from the RDS transmitter and that RDS transmitter has been disabled,
	 * or if the signal quality is too weak.
	 */
	if (blk == dev->radio_rx_rds_last_block || !dev->radio_rx_rds_enabled ||
	    (dev->radio_rds_loop && !(dev->radio_tx_subchans & V4L2_TUNER_SUB_RDS)) ||
	    abs(dev->radio_rx_sig_qual) > 200) {
		mutex_unlock(&dev->mutex);
		if (file->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;
		if (msleep_interruptible(20) && signal_pending(current))
			return -EINTR;
		if (mutex_lock_interruptible(&dev->mutex))
			return -ERESTARTSYS;
		goto retry;
	}

	/* abs(dev->radio_rx_sig_qual) <= 200, map that to a 0-50% range */
	perc = abs(dev->radio_rx_sig_qual) / 4;

	for (i = 0; i < size && blk > dev->radio_rx_rds_last_block;
			dev->radio_rx_rds_last_block++) {
		unsigned data_blk = dev->radio_rx_rds_last_block % VIVID_RDS_GEN_BLOCKS;
		struct v4l2_rds_data rds = data[data_blk];

		if (data_blk == 0 && dev->radio_rds_loop)
			vivid_radio_rds_init(dev);
		if (perc && prandom_u32_max(100) < perc) {
			switch (prandom_u32_max(4)) {
			case 0:
				rds.block |= V4L2_RDS_BLOCK_CORRECTED;
				break;
			case 1:
				rds.block |= V4L2_RDS_BLOCK_INVALID;
				break;
			case 2:
				rds.block |= V4L2_RDS_BLOCK_ERROR;
				rds.lsb = prandom_u32_max(256);
				rds.msb = prandom_u32_max(256);
				break;
			case 3: /* Skip block altogether */
				if (i)
					continue;
				/*
				 * Must make sure at least one block is
				 * returned, otherwise the application
				 * might think that end-of-file occurred.
				 */
				break;
			}
		}
		if (copy_to_user(buf + i, &rds, sizeof(rds))) {
			i = -EFAULT;
			break;
		}
		i += sizeof(rds);
	}
	mutex_unlock(&dev->mutex);
	return i;
}

__poll_t vivid_radio_rx_poll(struct file *file, struct poll_table_struct *wait)
{
	return POLLIN | POLLRDNORM | v4l2_ctrl_poll(file, wait);
}

int vivid_radio_rx_enum_freq_bands(struct file *file, void *fh, struct v4l2_frequency_band *band)
{
	if (band->tuner != 0)
		return -EINVAL;

	if (band->index >= TOT_BANDS)
		return -EINVAL;

	*band = vivid_radio_bands[band->index];
	return 0;
}

int vivid_radio_rx_s_hw_freq_seek(struct file *file, void *fh, const struct v4l2_hw_freq_seek *a)
{
	struct vivid_dev *dev = video_drvdata(file);
	unsigned low, high;
	unsigned freq;
	unsigned spacing;
	unsigned band;

	if (a->tuner)
		return -EINVAL;
	if (a->wrap_around && dev->radio_rx_hw_seek_mode == VIVID_HW_SEEK_BOUNDED)
		return -EINVAL;

	if (!a->wrap_around && dev->radio_rx_hw_seek_mode == VIVID_HW_SEEK_WRAP)
		return -EINVAL;
	if (!a->rangelow ^ !a->rangehigh)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK)
		return -EWOULDBLOCK;

	if (a->rangelow) {
		for (band = 0; band < TOT_BANDS; band++)
			if (a->rangelow >= vivid_radio_bands[band].rangelow &&
			    a->rangehigh <= vivid_radio_bands[band].rangehigh)
				break;
		if (band == TOT_BANDS)
			return -EINVAL;
		if (!dev->radio_rx_hw_seek_prog_lim &&
		    (a->rangelow != vivid_radio_bands[band].rangelow ||
		     a->rangehigh != vivid_radio_bands[band].rangehigh))
			return -EINVAL;
		low = a->rangelow;
		high = a->rangehigh;
	} else {
		for (band = 0; band < TOT_BANDS; band++)
			if (dev->radio_rx_freq >= vivid_radio_bands[band].rangelow &&
			    dev->radio_rx_freq <= vivid_radio_bands[band].rangehigh)
				break;
		if (band == TOT_BANDS)
			return -EINVAL;
		low = vivid_radio_bands[band].rangelow;
		high = vivid_radio_bands[band].rangehigh;
	}
	spacing = band == BAND_AM ? 1600 : 16000;
	freq = clamp(dev->radio_rx_freq, low, high);

	if (a->seek_upward) {
		freq = spacing * (freq / spacing) + spacing;
		if (freq > high) {
			if (!a->wrap_around)
				return -ENODATA;
			freq = spacing * (low / spacing) + spacing;
			if (freq >= dev->radio_rx_freq)
				return -ENODATA;
		}
	} else {
		freq = spacing * ((freq + spacing - 1) / spacing) - spacing;
		if (freq < low) {
			if (!a->wrap_around)
				return -ENODATA;
			freq = spacing * ((high + spacing - 1) / spacing) - spacing;
			if (freq <= dev->radio_rx_freq)
				return -ENODATA;
		}
	}
	return 0;
}

int vivid_radio_rx_g_tuner(struct file *file, void *fh, struct v4l2_tuner *vt)
{
	struct vivid_dev *dev = video_drvdata(file);
	int delta = 800;
	int sig_qual;

	if (vt->index > 0)
		return -EINVAL;

	strlcpy(vt->name, "AM/FM/SW Receiver", sizeof(vt->name));
	vt->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			 V4L2_TUNER_CAP_FREQ_BANDS | V4L2_TUNER_CAP_RDS |
			 (dev->radio_rx_rds_controls ?
				V4L2_TUNER_CAP_RDS_CONTROLS :
				V4L2_TUNER_CAP_RDS_BLOCK_IO) |
			 (dev->radio_rx_hw_seek_prog_lim ?
				V4L2_TUNER_CAP_HWSEEK_PROG_LIM : 0);
	switch (dev->radio_rx_hw_seek_mode) {
	case VIVID_HW_SEEK_BOUNDED:
		vt->capability |= V4L2_TUNER_CAP_HWSEEK_BOUNDED;
		break;
	case VIVID_HW_SEEK_WRAP:
		vt->capability |= V4L2_TUNER_CAP_HWSEEK_WRAP;
		break;
	case VIVID_HW_SEEK_BOTH:
		vt->capability |= V4L2_TUNER_CAP_HWSEEK_WRAP |
				  V4L2_TUNER_CAP_HWSEEK_BOUNDED;
		break;
	}
	vt->rangelow = AM_FREQ_RANGE_LOW;
	vt->rangehigh = FM_FREQ_RANGE_HIGH;
	sig_qual = dev->radio_rx_sig_qual;
	vt->signal = abs(sig_qual) > delta ? 0 :
		     0xffff - (abs(sig_qual) * 0xffff) / delta;
	vt->afc = sig_qual > delta ? 0 : sig_qual;
	if (abs(sig_qual) > delta)
		vt->rxsubchans = 0;
	else if (dev->radio_rx_freq < FM_FREQ_RANGE_LOW || vt->signal < 0x8000)
		vt->rxsubchans = V4L2_TUNER_SUB_MONO;
	else if (dev->radio_rds_loop && !(dev->radio_tx_subchans & V4L2_TUNER_SUB_STEREO))
		vt->rxsubchans = V4L2_TUNER_SUB_MONO;
	else
		vt->rxsubchans = V4L2_TUNER_SUB_STEREO;
	if (dev->radio_rx_rds_enabled &&
	    (!dev->radio_rds_loop || (dev->radio_tx_subchans & V4L2_TUNER_SUB_RDS)) &&
	    dev->radio_rx_freq >= FM_FREQ_RANGE_LOW && vt->signal >= 0xc000)
		vt->rxsubchans |= V4L2_TUNER_SUB_RDS;
	if (dev->radio_rx_rds_controls)
		vivid_radio_rds_init(dev);
	vt->audmode = dev->radio_rx_audmode;
	return 0;
}

int vivid_radio_rx_s_tuner(struct file *file, void *fh, const struct v4l2_tuner *vt)
{
	struct vivid_dev *dev = video_drvdata(file);

	if (vt->index)
		return -EINVAL;
	dev->radio_rx_audmode = vt->audmode >= V4L2_TUNER_MODE_STEREO;
	return 0;
}
