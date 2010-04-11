/******************************************************************************

  Copyright(c) 2005 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  Intel Linux Wireless <ilw@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

******************************************************************************/
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <asm/uaccess.h>

#include "libipw.h"

int libipw_is_valid_channel(struct libipw_device *ieee, u8 channel)
{
	int i;

	/* Driver needs to initialize the geography map before using
	 * these helper functions */
	if (ieee->geo.bg_channels == 0 && ieee->geo.a_channels == 0)
		return 0;

	if (ieee->freq_band & LIBIPW_24GHZ_BAND)
		for (i = 0; i < ieee->geo.bg_channels; i++)
			/* NOTE: If G mode is currently supported but
			 * this is a B only channel, we don't see it
			 * as valid. */
			if ((ieee->geo.bg[i].channel == channel) &&
			    !(ieee->geo.bg[i].flags & LIBIPW_CH_INVALID) &&
			    (!(ieee->mode & IEEE_G) ||
			     !(ieee->geo.bg[i].flags & LIBIPW_CH_B_ONLY)))
				return LIBIPW_24GHZ_BAND;

	if (ieee->freq_band & LIBIPW_52GHZ_BAND)
		for (i = 0; i < ieee->geo.a_channels; i++)
			if ((ieee->geo.a[i].channel == channel) &&
			    !(ieee->geo.a[i].flags & LIBIPW_CH_INVALID))
				return LIBIPW_52GHZ_BAND;

	return 0;
}

int libipw_channel_to_index(struct libipw_device *ieee, u8 channel)
{
	int i;

	/* Driver needs to initialize the geography map before using
	 * these helper functions */
	if (ieee->geo.bg_channels == 0 && ieee->geo.a_channels == 0)
		return -1;

	if (ieee->freq_band & LIBIPW_24GHZ_BAND)
		for (i = 0; i < ieee->geo.bg_channels; i++)
			if (ieee->geo.bg[i].channel == channel)
				return i;

	if (ieee->freq_band & LIBIPW_52GHZ_BAND)
		for (i = 0; i < ieee->geo.a_channels; i++)
			if (ieee->geo.a[i].channel == channel)
				return i;

	return -1;
}

u32 libipw_channel_to_freq(struct libipw_device * ieee, u8 channel)
{
	const struct libipw_channel * ch;

	/* Driver needs to initialize the geography map before using
	 * these helper functions */
	if (ieee->geo.bg_channels == 0 && ieee->geo.a_channels == 0)
		return 0;

	ch = libipw_get_channel(ieee, channel);
	if (!ch->channel)
		return 0;
	return ch->freq;
}

u8 libipw_freq_to_channel(struct libipw_device * ieee, u32 freq)
{
	int i;

	/* Driver needs to initialize the geography map before using
	 * these helper functions */
	if (ieee->geo.bg_channels == 0 && ieee->geo.a_channels == 0)
		return 0;

	freq /= 100000;

	if (ieee->freq_band & LIBIPW_24GHZ_BAND)
		for (i = 0; i < ieee->geo.bg_channels; i++)
			if (ieee->geo.bg[i].freq == freq)
				return ieee->geo.bg[i].channel;

	if (ieee->freq_band & LIBIPW_52GHZ_BAND)
		for (i = 0; i < ieee->geo.a_channels; i++)
			if (ieee->geo.a[i].freq == freq)
				return ieee->geo.a[i].channel;

	return 0;
}

int libipw_set_geo(struct libipw_device *ieee,
		      const struct libipw_geo *geo)
{
	memcpy(ieee->geo.name, geo->name, 3);
	ieee->geo.name[3] = '\0';
	ieee->geo.bg_channels = geo->bg_channels;
	ieee->geo.a_channels = geo->a_channels;
	memcpy(ieee->geo.bg, geo->bg, geo->bg_channels *
	       sizeof(struct libipw_channel));
	memcpy(ieee->geo.a, geo->a, ieee->geo.a_channels *
	       sizeof(struct libipw_channel));
	return 0;
}

const struct libipw_geo *libipw_get_geo(struct libipw_device *ieee)
{
	return &ieee->geo;
}

u8 libipw_get_channel_flags(struct libipw_device * ieee, u8 channel)
{
	int index = libipw_channel_to_index(ieee, channel);

	if (index == -1)
		return LIBIPW_CH_INVALID;

	if (channel <= LIBIPW_24GHZ_CHANNELS)
		return ieee->geo.bg[index].flags;

	return ieee->geo.a[index].flags;
}

static const struct libipw_channel bad_channel = {
	.channel = 0,
	.flags = LIBIPW_CH_INVALID,
	.max_power = 0,
};

const struct libipw_channel *libipw_get_channel(struct libipw_device
						      *ieee, u8 channel)
{
	int index = libipw_channel_to_index(ieee, channel);

	if (index == -1)
		return &bad_channel;

	if (channel <= LIBIPW_24GHZ_CHANNELS)
		return &ieee->geo.bg[index];

	return &ieee->geo.a[index];
}

EXPORT_SYMBOL(libipw_get_channel);
EXPORT_SYMBOL(libipw_get_channel_flags);
EXPORT_SYMBOL(libipw_is_valid_channel);
EXPORT_SYMBOL(libipw_freq_to_channel);
EXPORT_SYMBOL(libipw_channel_to_freq);
EXPORT_SYMBOL(libipw_channel_to_index);
EXPORT_SYMBOL(libipw_set_geo);
EXPORT_SYMBOL(libipw_get_geo);
