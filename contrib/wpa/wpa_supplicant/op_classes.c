/*
 * Operating classes
 * Copyright(c) 2015 Intel Deutschland GmbH
 * Contact Information:
 * Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_common.h"
#include "wpa_supplicant_i.h"


static enum chan_allowed allow_channel(struct hostapd_hw_modes *mode, u8 chan,
				       unsigned int *flags)
{
	int i;

	for (i = 0; i < mode->num_channels; i++) {
		if (mode->channels[i].chan == chan)
			break;
	}

	if (i == mode->num_channels ||
	    (mode->channels[i].flag & HOSTAPD_CHAN_DISABLED))
		return NOT_ALLOWED;

	if (flags)
		*flags = mode->channels[i].flag;

	if (mode->channels[i].flag & HOSTAPD_CHAN_NO_IR)
		return NO_IR;

	return ALLOWED;
}


static int get_center_80mhz(struct hostapd_hw_modes *mode, u8 channel)
{
	u8 center_channels[] = { 42, 58, 106, 122, 138, 155 };
	size_t i;

	if (mode->mode != HOSTAPD_MODE_IEEE80211A)
		return 0;

	for (i = 0; i < ARRAY_SIZE(center_channels); i++) {
		/*
		 * In 80 MHz, the bandwidth "spans" 12 channels (e.g., 36-48),
		 * so the center channel is 6 channels away from the start/end.
		 */
		if (channel >= center_channels[i] - 6 &&
		    channel <= center_channels[i] + 6)
			return center_channels[i];
	}

	return 0;
}


static enum chan_allowed verify_80mhz(struct hostapd_hw_modes *mode, u8 channel)
{
	u8 center_chan;
	unsigned int i;
	unsigned int no_ir = 0;

	center_chan = get_center_80mhz(mode, channel);
	if (!center_chan)
		return NOT_ALLOWED;

	/* check all the channels are available */
	for (i = 0; i < 4; i++) {
		unsigned int flags;
		u8 adj_chan = center_chan - 6 + i * 4;

		if (allow_channel(mode, adj_chan, &flags) == NOT_ALLOWED)
			return NOT_ALLOWED;

		if ((i == 0 && !(flags & HOSTAPD_CHAN_VHT_10_70)) ||
		    (i == 1 && !(flags & HOSTAPD_CHAN_VHT_30_50)) ||
		    (i == 2 && !(flags & HOSTAPD_CHAN_VHT_50_30)) ||
		    (i == 3 && !(flags & HOSTAPD_CHAN_VHT_70_10)))
			return NOT_ALLOWED;

		if (flags & HOSTAPD_CHAN_NO_IR)
			no_ir = 1;
	}

	if (no_ir)
		return NO_IR;

	return ALLOWED;
}


static int get_center_160mhz(struct hostapd_hw_modes *mode, u8 channel)
{
	u8 center_channels[] = { 50, 114 };
	unsigned int i;

	if (mode->mode != HOSTAPD_MODE_IEEE80211A)
		return 0;

	for (i = 0; i < ARRAY_SIZE(center_channels); i++) {
		/*
		 * In 160 MHz, the bandwidth "spans" 28 channels (e.g., 36-64),
		 * so the center channel is 14 channels away from the start/end.
		 */
		if (channel >= center_channels[i] - 14 &&
		    channel <= center_channels[i] + 14)
			return center_channels[i];
	}

	return 0;
}


static enum chan_allowed verify_160mhz(struct hostapd_hw_modes *mode,
				       u8 channel)
{
	u8 center_chan;
	unsigned int i;
	unsigned int no_ir = 0;

	center_chan = get_center_160mhz(mode, channel);
	if (!center_chan)
		return NOT_ALLOWED;

	/* Check all the channels are available */
	for (i = 0; i < 8; i++) {
		unsigned int flags;
		u8 adj_chan = center_chan - 14 + i * 4;

		if (allow_channel(mode, adj_chan, &flags) == NOT_ALLOWED)
			return NOT_ALLOWED;

		if ((i == 0 && !(flags & HOSTAPD_CHAN_VHT_10_150)) ||
		    (i == 1 && !(flags & HOSTAPD_CHAN_VHT_30_130)) ||
		    (i == 2 && !(flags & HOSTAPD_CHAN_VHT_50_110)) ||
		    (i == 3 && !(flags & HOSTAPD_CHAN_VHT_70_90)) ||
		    (i == 4 && !(flags & HOSTAPD_CHAN_VHT_90_70)) ||
		    (i == 5 && !(flags & HOSTAPD_CHAN_VHT_110_50)) ||
		    (i == 6 && !(flags & HOSTAPD_CHAN_VHT_130_30)) ||
		    (i == 7 && !(flags & HOSTAPD_CHAN_VHT_150_10)))
			return NOT_ALLOWED;

		if (flags & HOSTAPD_CHAN_NO_IR)
			no_ir = 1;
	}

	if (no_ir)
		return NO_IR;

	return ALLOWED;
}


enum chan_allowed verify_channel(struct hostapd_hw_modes *mode, u8 channel,
				 u8 bw)
{
	unsigned int flag = 0;
	enum chan_allowed res, res2;

	res2 = res = allow_channel(mode, channel, &flag);
	if (bw == BW40MINUS) {
		if (!(flag & HOSTAPD_CHAN_HT40MINUS))
			return NOT_ALLOWED;
		res2 = allow_channel(mode, channel - 4, NULL);
	} else if (bw == BW40PLUS) {
		if (!(flag & HOSTAPD_CHAN_HT40PLUS))
			return NOT_ALLOWED;
		res2 = allow_channel(mode, channel + 4, NULL);
	} else if (bw == BW80) {
		/*
		 * channel is a center channel and as such, not necessarily a
		 * valid 20 MHz channels. Override earlier allow_channel()
		 * result and use only the 80 MHz specific version.
		 */
		res2 = res = verify_80mhz(mode, channel);
	} else if (bw == BW160) {
		/*
		 * channel is a center channel and as such, not necessarily a
		 * valid 20 MHz channels. Override earlier allow_channel()
		 * result and use only the 160 MHz specific version.
		 */
		res2 = res = verify_160mhz(mode, channel);
	} else if (bw == BW80P80) {
		/*
		 * channel is a center channel and as such, not necessarily a
		 * valid 20 MHz channels. Override earlier allow_channel()
		 * result and use only the 80 MHz specific version.
		 */
		res2 = res = verify_80mhz(mode, channel);
	}

	if (res == NOT_ALLOWED || res2 == NOT_ALLOWED)
		return NOT_ALLOWED;

	if (res == NO_IR || res2 == NO_IR)
		return NO_IR;

	return ALLOWED;
}


static int wpas_op_class_supported(struct wpa_supplicant *wpa_s,
				   const struct oper_class_map *op_class)
{
	int chan;
	size_t i;
	struct hostapd_hw_modes *mode;
	int found;

	mode = get_mode(wpa_s->hw.modes, wpa_s->hw.num_modes, op_class->mode);
	if (!mode)
		return 0;

	if (op_class->op_class == 128) {
		u8 channels[] = { 42, 58, 106, 122, 138, 155 };

		for (i = 0; i < ARRAY_SIZE(channels); i++) {
			if (verify_channel(mode, channels[i], op_class->bw) !=
			    NOT_ALLOWED)
				return 1;
		}

		return 0;
	}

	if (op_class->op_class == 129) {
		/* Check if either 160 MHz channels is allowed */
		return verify_channel(mode, 50, op_class->bw) != NOT_ALLOWED ||
			verify_channel(mode, 114, op_class->bw) != NOT_ALLOWED;
	}

	if (op_class->op_class == 130) {
		/* Need at least two non-contiguous 80 MHz segments */
		found = 0;

		if (verify_channel(mode, 42, op_class->bw) != NOT_ALLOWED ||
		    verify_channel(mode, 58, op_class->bw) != NOT_ALLOWED)
			found++;
		if (verify_channel(mode, 106, op_class->bw) != NOT_ALLOWED ||
		    verify_channel(mode, 122, op_class->bw) != NOT_ALLOWED ||
		    verify_channel(mode, 138, op_class->bw) != NOT_ALLOWED)
			found++;
		if (verify_channel(mode, 106, op_class->bw) != NOT_ALLOWED &&
		    verify_channel(mode, 138, op_class->bw) != NOT_ALLOWED)
			found++;
		if (verify_channel(mode, 155, op_class->bw) != NOT_ALLOWED)
			found++;

		if (found >= 2)
			return 1;

		return 0;
	}

	found = 0;
	for (chan = op_class->min_chan; chan <= op_class->max_chan;
	     chan += op_class->inc) {
		if (verify_channel(mode, chan, op_class->bw) != NOT_ALLOWED) {
			found = 1;
			break;
		}
	}

	return found;
}


size_t wpas_supp_op_class_ie(struct wpa_supplicant *wpa_s, int freq, u8 *pos,
			      size_t len)
{
	struct wpabuf *buf;
	u8 op, current, chan;
	u8 *ie_len;
	size_t res;

	/*
	 * Assume 20 MHz channel for now.
	 * TODO: Use the secondary channel and VHT channel width that will be
	 * used after association.
	 */
	if (ieee80211_freq_to_channel_ext(freq, 0, VHT_CHANWIDTH_USE_HT,
					  &current, &chan) == NUM_HOSTAPD_MODES)
		return 0;

	/*
	 * Need 3 bytes for EID, length, and current operating class, plus
	 * 1 byte for every other supported operating class.
	 */
	buf = wpabuf_alloc(global_op_class_size + 3);
	if (!buf)
		return 0;

	wpabuf_put_u8(buf, WLAN_EID_SUPPORTED_OPERATING_CLASSES);
	/* Will set the length later, putting a placeholder */
	ie_len = wpabuf_put(buf, 1);
	wpabuf_put_u8(buf, current);

	for (op = 0; global_op_class[op].op_class; op++) {
		if (wpas_op_class_supported(wpa_s, &global_op_class[op]))
			wpabuf_put_u8(buf, global_op_class[op].op_class);
	}

	*ie_len = wpabuf_len(buf) - 2;
	if (*ie_len < 2 || wpabuf_len(buf) > len) {
		wpa_printf(MSG_ERROR,
			   "Failed to add supported operating classes IE");
		res = 0;
	} else {
		os_memcpy(pos, wpabuf_head(buf), wpabuf_len(buf));
		res = wpabuf_len(buf);
		wpa_hexdump_buf(MSG_DEBUG,
				"Added supported operating classes IE", buf);
	}

	wpabuf_free(buf);
	return res;
}
