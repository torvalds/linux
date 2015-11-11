/*
 * UWB radio (channel) management.
 *
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/uwb.h>
#include <linux/export.h>

#include "uwb-internal.h"


static int uwb_radio_select_channel(struct uwb_rc *rc)
{
	/*
	 * Default to channel 9 (BG1, TFC1) unless the user has
	 * selected a specific channel or there are no active PALs.
	 */
	if (rc->active_pals == 0)
		return -1;
	if (rc->beaconing_forced)
		return rc->beaconing_forced;
	return 9;
}


/*
 * Notify all active PALs that the channel has changed.
 */
static void uwb_radio_channel_changed(struct uwb_rc *rc, int channel)
{
	struct uwb_pal *pal;

	list_for_each_entry(pal, &rc->pals, node) {
		if (pal->channel && channel != pal->channel) {
			pal->channel = channel;
			if (pal->channel_changed)
				pal->channel_changed(pal, pal->channel);
		}
	}
}

/*
 * Change to a new channel and notify any active PALs of the new
 * channel.
 *
 * When stopping the radio, PALs need to be notified first so they can
 * terminate any active reservations.
 */
static int uwb_radio_change_channel(struct uwb_rc *rc, int channel)
{
	int ret = 0;
	struct device *dev = &rc->uwb_dev.dev;

	dev_dbg(dev, "%s: channel = %d, rc->beaconing = %d\n", __func__,
		channel, rc->beaconing);

	if (channel == -1)
		uwb_radio_channel_changed(rc, channel);

	if (channel != rc->beaconing) {
		if (rc->beaconing != -1 && channel != -1) {
			/*
			 * FIXME: should signal the channel change
			 * with a Channel Change IE.
			 */
			ret = uwb_radio_change_channel(rc, -1);
			if (ret < 0)
				return ret;
		}
		ret = uwb_rc_beacon(rc, channel, 0);
	}

	if (channel != -1)
		uwb_radio_channel_changed(rc, rc->beaconing);

	return ret;
}

/**
 * uwb_radio_start - request that the radio be started
 * @pal: the PAL making the request.
 *
 * If the radio is not already active, a suitable channel is selected
 * and beacons are started.
 */
int uwb_radio_start(struct uwb_pal *pal)
{
	struct uwb_rc *rc = pal->rc;
	int ret = 0;

	mutex_lock(&rc->uwb_dev.mutex);

	if (!pal->channel) {
		pal->channel = -1;
		rc->active_pals++;
		ret = uwb_radio_change_channel(rc, uwb_radio_select_channel(rc));
	}

	mutex_unlock(&rc->uwb_dev.mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(uwb_radio_start);

/**
 * uwb_radio_stop - request that the radio be stopped.
 * @pal: the PAL making the request.
 *
 * Stops the radio if no other PAL is making use of it.
 */
void uwb_radio_stop(struct uwb_pal *pal)
{
	struct uwb_rc *rc = pal->rc;

	mutex_lock(&rc->uwb_dev.mutex);

	if (pal->channel) {
		rc->active_pals--;
		uwb_radio_change_channel(rc, uwb_radio_select_channel(rc));
		pal->channel = 0;
	}

	mutex_unlock(&rc->uwb_dev.mutex);
}
EXPORT_SYMBOL_GPL(uwb_radio_stop);

/*
 * uwb_radio_force_channel - force a specific channel to be used
 * @rc: the radio controller.
 * @channel: the channel to use; -1 to force the radio to stop; 0 to
 *   use the default channel selection algorithm.
 */
int uwb_radio_force_channel(struct uwb_rc *rc, int channel)
{
	int ret = 0;

	mutex_lock(&rc->uwb_dev.mutex);

	rc->beaconing_forced = channel;
	ret = uwb_radio_change_channel(rc, uwb_radio_select_channel(rc));

	mutex_unlock(&rc->uwb_dev.mutex);
	return ret;
}

/*
 * uwb_radio_setup - setup the radio manager
 * @rc: the radio controller.
 *
 * The radio controller is reset to ensure it's in a known state
 * before it's used.
 */
int uwb_radio_setup(struct uwb_rc *rc)
{
	return uwb_rc_reset(rc);
}

/*
 * uwb_radio_reset_state - reset any radio manager state
 * @rc: the radio controller.
 *
 * All internal radio manager state is reset to values corresponding
 * to a reset radio controller.
 */
void uwb_radio_reset_state(struct uwb_rc *rc)
{
	struct uwb_pal *pal;

	mutex_lock(&rc->uwb_dev.mutex);

	list_for_each_entry(pal, &rc->pals, node) {
		if (pal->channel) {
			pal->channel = -1;
			if (pal->channel_changed)
				pal->channel_changed(pal, -1);
		}
	}

	rc->beaconing = -1;
	rc->scanning = -1;

	mutex_unlock(&rc->uwb_dev.mutex);
}

/*
 * uwb_radio_shutdown - shutdown the radio manager
 * @rc: the radio controller.
 *
 * The radio controller is reset.
 */
void uwb_radio_shutdown(struct uwb_rc *rc)
{
	uwb_radio_reset_state(rc);
	uwb_rc_reset(rc);
}
