/*
 *  ALSA interface to cx18 PCM capture streams
 *
 *  Copyright (C) 2009  Andy Walls <awalls@radix.net>
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

struct snd_card;

struct snd_cx18_card {
	struct v4l2_device *v4l2_dev;
	struct snd_card *sc;
	unsigned int capture_transfer_done;
	unsigned int hwptr_done_capture;
	struct snd_pcm_substream *capture_pcm_substream;
	spinlock_t slock;
};

extern int cx18_alsa_debug;

#define CX18_ALSA_DBGFLG_WARN  (1 << 0)
#define CX18_ALSA_DBGFLG_WARN  (1 << 0)
#define CX18_ALSA_DBGFLG_INFO  (1 << 1)

#define CX18_ALSA_DEBUG(x, type, fmt, args...) \
	do { \
		if ((x) & cx18_alsa_debug) \
			printk(KERN_INFO "%s-alsa: " type ": " fmt, \
				v4l2_dev->name , ## args); \
	} while (0)

#define CX18_ALSA_DEBUG_WARN(fmt, args...) \
	CX18_ALSA_DEBUG(CX18_ALSA_DBGFLG_WARN, "warning", fmt , ## args)

#define CX18_ALSA_DEBUG_INFO(fmt, args...) \
	CX18_ALSA_DEBUG(CX18_ALSA_DBGFLG_INFO, "info", fmt , ## args)

#define CX18_ALSA_ERR(fmt, args...) \
	printk(KERN_ERR "%s-alsa: " fmt, v4l2_dev->name , ## args)

#define CX18_ALSA_WARN(fmt, args...) \
	printk(KERN_WARNING "%s-alsa: " fmt, v4l2_dev->name , ## args)

#define CX18_ALSA_INFO(fmt, args...) \
	printk(KERN_INFO "%s-alsa: " fmt, v4l2_dev->name , ## args)
