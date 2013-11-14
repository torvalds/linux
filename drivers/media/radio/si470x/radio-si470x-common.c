/*
 *  drivers/media/radio/si470x/radio-si470x-common.c
 *
 *  Driver for radios with Silicon Labs Si470x FM Radio Receivers
 *
 *  Copyright (c) 2009 Tobias Lorenz <tobias.lorenz@gmx.net>
 *  Copyright (c) 2012 Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


/*
 * History:
 * 2008-01-12	Tobias Lorenz <tobias.lorenz@gmx.net>
 *		Version 1.0.0
 *		- First working version
 * 2008-01-13	Tobias Lorenz <tobias.lorenz@gmx.net>
 *		Version 1.0.1
 *		- Improved error handling, every function now returns errno
 *		- Improved multi user access (start/mute/stop)
 *		- Channel doesn't get lost anymore after start/mute/stop
 *		- RDS support added (polling mode via interrupt EP 1)
 *		- marked default module parameters with *value*
 *		- switched from bit structs to bit masks
 *		- header file cleaned and integrated
 * 2008-01-14	Tobias Lorenz <tobias.lorenz@gmx.net>
 * 		Version 1.0.2
 * 		- hex values are now lower case
 * 		- commented USB ID for ADS/Tech moved on todo list
 * 		- blacklisted si470x in hid-quirks.c
 * 		- rds buffer handling functions integrated into *_work, *_read
 * 		- rds_command in si470x_poll exchanged against simple retval
 * 		- check for firmware version 15
 * 		- code order and prototypes still remain the same
 * 		- spacing and bottom of band codes remain the same
 * 2008-01-16	Tobias Lorenz <tobias.lorenz@gmx.net>
 *		Version 1.0.3
 * 		- code reordered to avoid function prototypes
 *		- switch/case defaults are now more user-friendly
 *		- unified comment style
 *		- applied all checkpatch.pl v1.12 suggestions
 *		  except the warning about the too long lines with bit comments
 *		- renamed FMRADIO to RADIO to cut line length (checkpatch.pl)
 * 2008-01-22	Tobias Lorenz <tobias.lorenz@gmx.net>
 *		Version 1.0.4
 *		- avoid poss. locking when doing copy_to_user which may sleep
 *		- RDS is automatically activated on read now
 *		- code cleaned of unnecessary rds_commands
 *		- USB Vendor/Product ID for ADS/Tech FM Radio Receiver verified
 *		  (thanks to Guillaume RAMOUSSE)
 * 2008-01-27	Tobias Lorenz <tobias.lorenz@gmx.net>
 *		Version 1.0.5
 *		- number of seek_retries changed to tune_timeout
 *		- fixed problem with incomplete tune operations by own buffers
 *		- optimization of variables and printf types
 *		- improved error logging
 * 2008-01-31	Tobias Lorenz <tobias.lorenz@gmx.net>
 *		Oliver Neukum <oliver@neukum.org>
 *		Version 1.0.6
 *		- fixed coverity checker warnings in *_usb_driver_disconnect
 *		- probe()/open() race by correct ordering in probe()
 *		- DMA coherency rules by separate allocation of all buffers
 *		- use of endianness macros
 *		- abuse of spinlock, replaced by mutex
 *		- racy handling of timer in disconnect,
 *		  replaced by delayed_work
 *		- racy interruptible_sleep_on(),
 *		  replaced with wait_event_interruptible()
 *		- handle signals in read()
 * 2008-02-08	Tobias Lorenz <tobias.lorenz@gmx.net>
 *		Oliver Neukum <oliver@neukum.org>
 *		Version 1.0.7
 *		- usb autosuspend support
 *		- unplugging fixed
 * 2008-05-07	Tobias Lorenz <tobias.lorenz@gmx.net>
 *		Version 1.0.8
 *		- hardware frequency seek support
 *		- afc indication
 *		- more safety checks, let si470x_get_freq return errno
 *		- vidioc behavior corrected according to v4l2 spec
 * 2008-10-20	Alexey Klimov <klimov.linux@gmail.com>
 * 		- add support for KWorld USB FM Radio FM700
 * 		- blacklisted KWorld radio in hid-core.c and hid-ids.h
 * 2008-12-03	Mark Lord <mlord@pobox.com>
 *		- add support for DealExtreme USB Radio
 * 2009-01-31	Bob Ross <pigiron@gmx.com>
 *		- correction of stereo detection/setting
 *		- correction of signal strength indicator scaling
 * 2009-01-31	Rick Bronson <rick@efn.org>
 *		Tobias Lorenz <tobias.lorenz@gmx.net>
 *		- add LED status output
 *		- get HW/SW version from scratchpad
 * 2009-06-16   Edouard Lafargue <edouard@lafargue.name>
 *		Version 1.0.10
 *		- add support for interrupt mode for RDS endpoint,
 *                instead of polling.
 *                Improves RDS reception significantly
 */


/* kernel includes */
#include "radio-si470x.h"



/**************************************************************************
 * Module Parameters
 **************************************************************************/

/* Spacing (kHz) */
/* 0: 200 kHz (USA, Australia) */
/* 1: 100 kHz (Europe, Japan) */
/* 2:  50 kHz */
static unsigned short space = 2;
module_param(space, ushort, 0444);
MODULE_PARM_DESC(space, "Spacing: 0=200kHz 1=100kHz *2=50kHz*");

/* De-emphasis */
/* 0: 75 us (USA) */
/* 1: 50 us (Europe, Australia, Japan) */
static unsigned short de = 1;
module_param(de, ushort, 0444);
MODULE_PARM_DESC(de, "De-emphasis: 0=75us *1=50us*");

/* Tune timeout */
static unsigned int tune_timeout = 3000;
module_param(tune_timeout, uint, 0644);
MODULE_PARM_DESC(tune_timeout, "Tune timeout: *3000*");

/* Seek timeout */
static unsigned int seek_timeout = 5000;
module_param(seek_timeout, uint, 0644);
MODULE_PARM_DESC(seek_timeout, "Seek timeout: *5000*");

static const struct v4l2_frequency_band bands[] = {
	{
		.type = V4L2_TUNER_RADIO,
		.index = 0,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			    V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO |
			    V4L2_TUNER_CAP_FREQ_BANDS |
			    V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			    V4L2_TUNER_CAP_HWSEEK_WRAP,
		.rangelow   =  87500 * 16,
		.rangehigh  = 108000 * 16,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
	{
		.type = V4L2_TUNER_RADIO,
		.index = 1,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			    V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO |
			    V4L2_TUNER_CAP_FREQ_BANDS |
			    V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			    V4L2_TUNER_CAP_HWSEEK_WRAP,
		.rangelow   =  76000 * 16,
		.rangehigh  = 108000 * 16,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
	{
		.type = V4L2_TUNER_RADIO,
		.index = 2,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			    V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO |
			    V4L2_TUNER_CAP_FREQ_BANDS |
			    V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			    V4L2_TUNER_CAP_HWSEEK_WRAP,
		.rangelow   =  76000 * 16,
		.rangehigh  =  90000 * 16,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
};

/**************************************************************************
 * Generic Functions
 **************************************************************************/

/*
 * si470x_set_band - set the band
 */
static int si470x_set_band(struct si470x_device *radio, int band)
{
	if (radio->band == band)
		return 0;

	radio->band = band;
	radio->registers[SYSCONFIG2] &= ~SYSCONFIG2_BAND;
	radio->registers[SYSCONFIG2] |= radio->band << 6;
	return si470x_set_register(radio, SYSCONFIG2);
}

/*
 * si470x_set_chan - set the channel
 */
static int si470x_set_chan(struct si470x_device *radio, unsigned short chan)
{
	int retval;
	bool timed_out = 0;

	/* start tuning */
	radio->registers[CHANNEL] &= ~CHANNEL_CHAN;
	radio->registers[CHANNEL] |= CHANNEL_TUNE | chan;
	retval = si470x_set_register(radio, CHANNEL);
	if (retval < 0)
		goto done;

	/* wait till tune operation has completed */
	reinit_completion(&radio->completion);
	retval = wait_for_completion_timeout(&radio->completion,
			msecs_to_jiffies(tune_timeout));
	if (!retval)
		timed_out = true;

	if ((radio->registers[STATUSRSSI] & STATUSRSSI_STC) == 0)
		dev_warn(&radio->videodev.dev, "tune does not complete\n");
	if (timed_out)
		dev_warn(&radio->videodev.dev,
			"tune timed out after %u ms\n", tune_timeout);

	/* stop tuning */
	radio->registers[CHANNEL] &= ~CHANNEL_TUNE;
	retval = si470x_set_register(radio, CHANNEL);

done:
	return retval;
}

/*
 * si470x_get_step - get channel spacing
 */
static unsigned int si470x_get_step(struct si470x_device *radio)
{
	/* Spacing (kHz) */
	switch ((radio->registers[SYSCONFIG2] & SYSCONFIG2_SPACE) >> 4) {
	/* 0: 200 kHz (USA, Australia) */
	case 0:
		return 200 * 16;
	/* 1: 100 kHz (Europe, Japan) */
	case 1:
		return 100 * 16;
	/* 2:  50 kHz */
	default:
		return 50 * 16;
	};
}


/*
 * si470x_get_freq - get the frequency
 */
static int si470x_get_freq(struct si470x_device *radio, unsigned int *freq)
{
	int chan, retval;

	/* read channel */
	retval = si470x_get_register(radio, READCHAN);
	chan = radio->registers[READCHAN] & READCHAN_READCHAN;

	/* Frequency (MHz) = Spacing (kHz) x Channel + Bottom of Band (MHz) */
	*freq = chan * si470x_get_step(radio) + bands[radio->band].rangelow;

	return retval;
}


/*
 * si470x_set_freq - set the frequency
 */
int si470x_set_freq(struct si470x_device *radio, unsigned int freq)
{
	unsigned short chan;

	freq = clamp(freq, bands[radio->band].rangelow,
			   bands[radio->band].rangehigh);
	/* Chan = [ Freq (Mhz) - Bottom of Band (MHz) ] / Spacing (kHz) */
	chan = (freq - bands[radio->band].rangelow) / si470x_get_step(radio);

	return si470x_set_chan(radio, chan);
}


/*
 * si470x_set_seek - set seek
 */
static int si470x_set_seek(struct si470x_device *radio,
			   const struct v4l2_hw_freq_seek *seek)
{
	int band, retval;
	unsigned int freq;
	bool timed_out = 0;

	/* set band */
	if (seek->rangelow || seek->rangehigh) {
		for (band = 0; band < ARRAY_SIZE(bands); band++) {
			if (bands[band].rangelow  == seek->rangelow &&
			    bands[band].rangehigh == seek->rangehigh)
				break;
		}
		if (band == ARRAY_SIZE(bands))
			return -EINVAL; /* No matching band found */
	} else
		band = 1; /* If nothing is specified seek 76 - 108 Mhz */

	if (radio->band != band) {
		retval = si470x_get_freq(radio, &freq);
		if (retval)
			return retval;
		retval = si470x_set_band(radio, band);
		if (retval)
			return retval;
		retval = si470x_set_freq(radio, freq);
		if (retval)
			return retval;
	}

	/* start seeking */
	radio->registers[POWERCFG] |= POWERCFG_SEEK;
	if (seek->wrap_around)
		radio->registers[POWERCFG] &= ~POWERCFG_SKMODE;
	else
		radio->registers[POWERCFG] |= POWERCFG_SKMODE;
	if (seek->seek_upward)
		radio->registers[POWERCFG] |= POWERCFG_SEEKUP;
	else
		radio->registers[POWERCFG] &= ~POWERCFG_SEEKUP;
	retval = si470x_set_register(radio, POWERCFG);
	if (retval < 0)
		return retval;

	/* wait till tune operation has completed */
	reinit_completion(&radio->completion);
	retval = wait_for_completion_timeout(&radio->completion,
			msecs_to_jiffies(seek_timeout));
	if (!retval)
		timed_out = true;

	if ((radio->registers[STATUSRSSI] & STATUSRSSI_STC) == 0)
		dev_warn(&radio->videodev.dev, "seek does not complete\n");
	if (radio->registers[STATUSRSSI] & STATUSRSSI_SF)
		dev_warn(&radio->videodev.dev,
			"seek failed / band limit reached\n");

	/* stop seeking */
	radio->registers[POWERCFG] &= ~POWERCFG_SEEK;
	retval = si470x_set_register(radio, POWERCFG);

	/* try again, if timed out */
	if (retval == 0 && timed_out)
		return -ENODATA;
	return retval;
}


/*
 * si470x_start - switch on radio
 */
int si470x_start(struct si470x_device *radio)
{
	int retval;

	/* powercfg */
	radio->registers[POWERCFG] =
		POWERCFG_DMUTE | POWERCFG_ENABLE | POWERCFG_RDSM;
	retval = si470x_set_register(radio, POWERCFG);
	if (retval < 0)
		goto done;

	/* sysconfig 1 */
	radio->registers[SYSCONFIG1] =
		(de << 11) & SYSCONFIG1_DE;		/* DE*/
	retval = si470x_set_register(radio, SYSCONFIG1);
	if (retval < 0)
		goto done;

	/* sysconfig 2 */
	radio->registers[SYSCONFIG2] =
		(0x1f  << 8) |				/* SEEKTH */
		((radio->band << 6) & SYSCONFIG2_BAND) |/* BAND */
		((space << 4) & SYSCONFIG2_SPACE) |	/* SPACE */
		15;					/* VOLUME (max) */
	retval = si470x_set_register(radio, SYSCONFIG2);
	if (retval < 0)
		goto done;

	/* reset last channel */
	retval = si470x_set_chan(radio,
		radio->registers[CHANNEL] & CHANNEL_CHAN);

done:
	return retval;
}


/*
 * si470x_stop - switch off radio
 */
int si470x_stop(struct si470x_device *radio)
{
	int retval;

	/* sysconfig 1 */
	radio->registers[SYSCONFIG1] &= ~SYSCONFIG1_RDS;
	retval = si470x_set_register(radio, SYSCONFIG1);
	if (retval < 0)
		goto done;

	/* powercfg */
	radio->registers[POWERCFG] &= ~POWERCFG_DMUTE;
	/* POWERCFG_ENABLE has to automatically go low */
	radio->registers[POWERCFG] |= POWERCFG_ENABLE |	POWERCFG_DISABLE;
	retval = si470x_set_register(radio, POWERCFG);

done:
	return retval;
}


/*
 * si470x_rds_on - switch on rds reception
 */
static int si470x_rds_on(struct si470x_device *radio)
{
	int retval;

	/* sysconfig 1 */
	radio->registers[SYSCONFIG1] |= SYSCONFIG1_RDS;
	retval = si470x_set_register(radio, SYSCONFIG1);
	if (retval < 0)
		radio->registers[SYSCONFIG1] &= ~SYSCONFIG1_RDS;

	return retval;
}



/**************************************************************************
 * File Operations Interface
 **************************************************************************/

/*
 * si470x_fops_read - read RDS data
 */
static ssize_t si470x_fops_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct si470x_device *radio = video_drvdata(file);
	int retval = 0;
	unsigned int block_count = 0;

	/* switch on rds reception */
	if ((radio->registers[SYSCONFIG1] & SYSCONFIG1_RDS) == 0)
		si470x_rds_on(radio);

	/* block if no new data available */
	while (radio->wr_index == radio->rd_index) {
		if (file->f_flags & O_NONBLOCK) {
			retval = -EWOULDBLOCK;
			goto done;
		}
		if (wait_event_interruptible(radio->read_queue,
			radio->wr_index != radio->rd_index) < 0) {
			retval = -EINTR;
			goto done;
		}
	}

	/* calculate block count from byte count */
	count /= 3;

	/* copy RDS block out of internal buffer and to user buffer */
	while (block_count < count) {
		if (radio->rd_index == radio->wr_index)
			break;

		/* always transfer rds complete blocks */
		if (copy_to_user(buf, &radio->buffer[radio->rd_index], 3))
			/* retval = -EFAULT; */
			break;

		/* increment and wrap read pointer */
		radio->rd_index += 3;
		if (radio->rd_index >= radio->buf_size)
			radio->rd_index = 0;

		/* increment counters */
		block_count++;
		buf += 3;
		retval += 3;
	}

done:
	return retval;
}


/*
 * si470x_fops_poll - poll RDS data
 */
static unsigned int si470x_fops_poll(struct file *file,
		struct poll_table_struct *pts)
{
	struct si470x_device *radio = video_drvdata(file);
	unsigned long req_events = poll_requested_events(pts);
	int retval = v4l2_ctrl_poll(file, pts);

	if (req_events & (POLLIN | POLLRDNORM)) {
		/* switch on rds reception */
		if ((radio->registers[SYSCONFIG1] & SYSCONFIG1_RDS) == 0)
			si470x_rds_on(radio);

		poll_wait(file, &radio->read_queue, pts);

		if (radio->rd_index != radio->wr_index)
			retval |= POLLIN | POLLRDNORM;
	}

	return retval;
}


/*
 * si470x_fops - file operations interface
 */
static const struct v4l2_file_operations si470x_fops = {
	.owner			= THIS_MODULE,
	.read			= si470x_fops_read,
	.poll			= si470x_fops_poll,
	.unlocked_ioctl		= video_ioctl2,
	.open			= si470x_fops_open,
	.release		= si470x_fops_release,
};



/**************************************************************************
 * Video4Linux Interface
 **************************************************************************/


static int si470x_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct si470x_device *radio =
		container_of(ctrl->handler, struct si470x_device, hdl);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_VOLUME:
		radio->registers[SYSCONFIG2] &= ~SYSCONFIG2_VOLUME;
		radio->registers[SYSCONFIG2] |= ctrl->val;
		return si470x_set_register(radio, SYSCONFIG2);
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->val)
			radio->registers[POWERCFG] &= ~POWERCFG_DMUTE;
		else
			radio->registers[POWERCFG] |= POWERCFG_DMUTE;
		return si470x_set_register(radio, POWERCFG);
	default:
		return -EINVAL;
	}
}


/*
 * si470x_vidioc_g_tuner - get tuner attributes
 */
static int si470x_vidioc_g_tuner(struct file *file, void *priv,
		struct v4l2_tuner *tuner)
{
	struct si470x_device *radio = video_drvdata(file);
	int retval = 0;

	if (tuner->index != 0)
		return -EINVAL;

	if (!radio->status_rssi_auto_update) {
		retval = si470x_get_register(radio, STATUSRSSI);
		if (retval < 0)
			return retval;
	}

	/* driver constants */
	strcpy(tuner->name, "FM");
	tuner->type = V4L2_TUNER_RADIO;
	tuner->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			    V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO |
			    V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			    V4L2_TUNER_CAP_HWSEEK_WRAP;
	tuner->rangelow  =  76 * FREQ_MUL;
	tuner->rangehigh = 108 * FREQ_MUL;

	/* stereo indicator == stereo (instead of mono) */
	if ((radio->registers[STATUSRSSI] & STATUSRSSI_ST) == 0)
		tuner->rxsubchans = V4L2_TUNER_SUB_MONO;
	else
		tuner->rxsubchans = V4L2_TUNER_SUB_STEREO;
	/* If there is a reliable method of detecting an RDS channel,
	   then this code should check for that before setting this
	   RDS subchannel. */
	tuner->rxsubchans |= V4L2_TUNER_SUB_RDS;

	/* mono/stereo selector */
	if ((radio->registers[POWERCFG] & POWERCFG_MONO) == 0)
		tuner->audmode = V4L2_TUNER_MODE_STEREO;
	else
		tuner->audmode = V4L2_TUNER_MODE_MONO;

	/* min is worst, max is best; signal:0..0xffff; rssi: 0..0xff */
	/* measured in units of dbµV in 1 db increments (max at ~75 dbµV) */
	tuner->signal = (radio->registers[STATUSRSSI] & STATUSRSSI_RSSI);
	/* the ideal factor is 0xffff/75 = 873,8 */
	tuner->signal = (tuner->signal * 873) + (8 * tuner->signal / 10);
	if (tuner->signal > 0xffff)
		tuner->signal = 0xffff;

	/* automatic frequency control: -1: freq to low, 1 freq to high */
	/* AFCRL does only indicate that freq. differs, not if too low/high */
	tuner->afc = (radio->registers[STATUSRSSI] & STATUSRSSI_AFCRL) ? 1 : 0;

	return retval;
}


/*
 * si470x_vidioc_s_tuner - set tuner attributes
 */
static int si470x_vidioc_s_tuner(struct file *file, void *priv,
		const struct v4l2_tuner *tuner)
{
	struct si470x_device *radio = video_drvdata(file);

	if (tuner->index != 0)
		return -EINVAL;

	/* mono/stereo selector */
	switch (tuner->audmode) {
	case V4L2_TUNER_MODE_MONO:
		radio->registers[POWERCFG] |= POWERCFG_MONO;  /* force mono */
		break;
	case V4L2_TUNER_MODE_STEREO:
	default:
		radio->registers[POWERCFG] &= ~POWERCFG_MONO; /* try stereo */
		break;
	}

	return si470x_set_register(radio, POWERCFG);
}


/*
 * si470x_vidioc_g_frequency - get tuner or modulator radio frequency
 */
static int si470x_vidioc_g_frequency(struct file *file, void *priv,
		struct v4l2_frequency *freq)
{
	struct si470x_device *radio = video_drvdata(file);

	if (freq->tuner != 0)
		return -EINVAL;

	freq->type = V4L2_TUNER_RADIO;
	return si470x_get_freq(radio, &freq->frequency);
}


/*
 * si470x_vidioc_s_frequency - set tuner or modulator radio frequency
 */
static int si470x_vidioc_s_frequency(struct file *file, void *priv,
		const struct v4l2_frequency *freq)
{
	struct si470x_device *radio = video_drvdata(file);
	int retval;

	if (freq->tuner != 0)
		return -EINVAL;

	if (freq->frequency < bands[radio->band].rangelow ||
	    freq->frequency > bands[radio->band].rangehigh) {
		/* Switch to band 1 which covers everything we support */
		retval = si470x_set_band(radio, 1);
		if (retval)
			return retval;
	}
	return si470x_set_freq(radio, freq->frequency);
}


/*
 * si470x_vidioc_s_hw_freq_seek - set hardware frequency seek
 */
static int si470x_vidioc_s_hw_freq_seek(struct file *file, void *priv,
		const struct v4l2_hw_freq_seek *seek)
{
	struct si470x_device *radio = video_drvdata(file);

	if (seek->tuner != 0)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK)
		return -EWOULDBLOCK;

	return si470x_set_seek(radio, seek);
}

/*
 * si470x_vidioc_enum_freq_bands - enumerate supported bands
 */
static int si470x_vidioc_enum_freq_bands(struct file *file, void *priv,
					 struct v4l2_frequency_band *band)
{
	if (band->tuner != 0)
		return -EINVAL;
	if (band->index >= ARRAY_SIZE(bands))
		return -EINVAL;
	*band = bands[band->index];
	return 0;
}

const struct v4l2_ctrl_ops si470x_ctrl_ops = {
	.s_ctrl = si470x_s_ctrl,
};

/*
 * si470x_ioctl_ops - video device ioctl operations
 */
static const struct v4l2_ioctl_ops si470x_ioctl_ops = {
	.vidioc_querycap	= si470x_vidioc_querycap,
	.vidioc_g_tuner		= si470x_vidioc_g_tuner,
	.vidioc_s_tuner		= si470x_vidioc_s_tuner,
	.vidioc_g_frequency	= si470x_vidioc_g_frequency,
	.vidioc_s_frequency	= si470x_vidioc_s_frequency,
	.vidioc_s_hw_freq_seek	= si470x_vidioc_s_hw_freq_seek,
	.vidioc_enum_freq_bands = si470x_vidioc_enum_freq_bands,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};


/*
 * si470x_viddev_template - video device interface
 */
struct video_device si470x_viddev_template = {
	.fops			= &si470x_fops,
	.name			= DRIVER_NAME,
	.release		= video_device_release_empty,
	.ioctl_ops		= &si470x_ioctl_ops,
};
