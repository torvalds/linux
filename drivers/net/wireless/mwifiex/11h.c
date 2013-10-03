/*
 * Marvell Wireless LAN device driver: 802.11h
 *
 * Copyright (C) 2013, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "main.h"
#include "fw.h"


/* This function appends 11h info to a buffer while joining an
 * infrastructure BSS
 */
static void
mwifiex_11h_process_infra_join(struct mwifiex_private *priv, u8 **buffer,
			       struct mwifiex_bssdescriptor *bss_desc)
{
	struct mwifiex_ie_types_header *ie_header;
	struct mwifiex_ie_types_pwr_capability *cap;
	struct mwifiex_ie_types_local_pwr_constraint *constraint;
	struct ieee80211_supported_band *sband;
	u8 radio_type;
	int i;

	if (!buffer || !(*buffer))
		return;

	radio_type = mwifiex_band_to_radio_type((u8) bss_desc->bss_band);
	sband = priv->wdev->wiphy->bands[radio_type];

	cap = (struct mwifiex_ie_types_pwr_capability *)*buffer;
	cap->header.type = cpu_to_le16(WLAN_EID_PWR_CAPABILITY);
	cap->header.len = cpu_to_le16(2);
	cap->min_pwr = 0;
	cap->max_pwr = 0;
	*buffer += sizeof(*cap);

	constraint = (struct mwifiex_ie_types_local_pwr_constraint *)*buffer;
	constraint->header.type = cpu_to_le16(WLAN_EID_PWR_CONSTRAINT);
	constraint->header.len = cpu_to_le16(2);
	constraint->chan = bss_desc->channel;
	constraint->constraint = bss_desc->local_constraint;
	*buffer += sizeof(*constraint);

	ie_header = (struct mwifiex_ie_types_header *)*buffer;
	ie_header->type = cpu_to_le16(TLV_TYPE_PASSTHROUGH);
	ie_header->len  = cpu_to_le16(2 * sband->n_channels + 2);
	*buffer += sizeof(*ie_header);
	*(*buffer)++ = WLAN_EID_SUPPORTED_CHANNELS;
	*(*buffer)++ = 2 * sband->n_channels;
	for (i = 0; i < sband->n_channels; i++) {
		*(*buffer)++ = ieee80211_frequency_to_channel(
					sband->channels[i].center_freq);
		*(*buffer)++ = 1; /* one channel in the subband */
	}
}

/* Enable or disable the 11h extensions in the firmware */
static int mwifiex_11h_activate(struct mwifiex_private *priv, bool flag)
{
	u32 enable = flag;

	return mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_SNMP_MIB,
				     HostCmd_ACT_GEN_SET, DOT11H_I, &enable);
}

/* This functions processes TLV buffer for a pending BSS Join command.
 *
 * Activate 11h functionality in the firmware if the spectrum management
 * capability bit is found in the network we are joining. Also, necessary
 * TLVs are set based on requested network's 11h capability.
 */
void mwifiex_11h_process_join(struct mwifiex_private *priv, u8 **buffer,
			      struct mwifiex_bssdescriptor *bss_desc)
{
	if (bss_desc->sensed_11h) {
		/* Activate 11h functions in firmware, turns on capability
		 * bit
		 */
		mwifiex_11h_activate(priv, true);
		bss_desc->cap_info_bitmap |= WLAN_CAPABILITY_SPECTRUM_MGMT;
		mwifiex_11h_process_infra_join(priv, buffer, bss_desc);
	} else {
		/* Deactivate 11h functions in the firmware */
		mwifiex_11h_activate(priv, false);
		bss_desc->cap_info_bitmap &= ~WLAN_CAPABILITY_SPECTRUM_MGMT;
	}
}
