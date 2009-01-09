/*
 * Wireless Host Controller (WHC) WUSB operations.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
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
#include <linux/init.h>
#include <linux/uwb/umc.h>

#include "../../wusbcore/wusbhc.h"

#include "whcd.h"

static int whc_update_di(struct whc *whc, int idx)
{
	int offset = idx / 32;
	u32 bit = 1 << (idx % 32);

	le_writel(bit, whc->base + WUSBDIBUPDATED + offset);

	return whci_wait_for(&whc->umc->dev,
			     whc->base + WUSBDIBUPDATED + offset, bit, 0,
			     100, "DI update");
}

/*
 * WHCI starts MMCs based on there being a valid GTK so these need
 * only start/stop the asynchronous and periodic schedules and send a
 * channel stop command.
 */

int whc_wusbhc_start(struct wusbhc *wusbhc)
{
	struct whc *whc = wusbhc_to_whc(wusbhc);

	asl_start(whc);
	pzl_start(whc);

	return 0;
}

void whc_wusbhc_stop(struct wusbhc *wusbhc, int delay)
{
	struct whc *whc = wusbhc_to_whc(wusbhc);
	u32 stop_time, now_time;
	int ret;

	pzl_stop(whc);
	asl_stop(whc);

	now_time = le_readl(whc->base + WUSBTIME) & WUSBTIME_CHANNEL_TIME_MASK;
	stop_time = (now_time + ((delay * 8) << 7)) & 0x00ffffff;
	ret = whc_do_gencmd(whc, WUSBGENCMDSTS_CHAN_STOP, stop_time, NULL, 0);
	if (ret == 0)
		msleep(delay);
}

int whc_mmcie_add(struct wusbhc *wusbhc, u8 interval, u8 repeat_cnt,
		  u8 handle, struct wuie_hdr *wuie)
{
	struct whc *whc = wusbhc_to_whc(wusbhc);
	u32 params;

	params = (interval << 24)
		| (repeat_cnt << 16)
		| (wuie->bLength << 8)
		| handle;

	return whc_do_gencmd(whc, WUSBGENCMDSTS_MMCIE_ADD, params, wuie, wuie->bLength);
}

int whc_mmcie_rm(struct wusbhc *wusbhc, u8 handle)
{
	struct whc *whc = wusbhc_to_whc(wusbhc);
	u32 params;

	params = handle;

	return whc_do_gencmd(whc, WUSBGENCMDSTS_MMCIE_RM, params, NULL, 0);
}

int whc_bwa_set(struct wusbhc *wusbhc, s8 stream_index, const struct uwb_mas_bm *mas_bm)
{
	struct whc *whc = wusbhc_to_whc(wusbhc);

	if (stream_index >= 0)
		whc_write_wusbcmd(whc, WUSBCMD_WUSBSI_MASK, WUSBCMD_WUSBSI(stream_index));

	return whc_do_gencmd(whc, WUSBGENCMDSTS_SET_MAS, 0, (void *)mas_bm, sizeof(*mas_bm));
}

int whc_dev_info_set(struct wusbhc *wusbhc, struct wusb_dev *wusb_dev)
{
	struct whc *whc = wusbhc_to_whc(wusbhc);
	int idx = wusb_dev->port_idx;
	struct di_buf_entry *di = &whc->di_buf[idx];
	int ret;

	mutex_lock(&whc->mutex);

	uwb_mas_bm_copy_le(di->availability_info, &wusb_dev->availability);
	di->addr_sec_info &= ~(WHC_DI_DISABLE | WHC_DI_DEV_ADDR_MASK);
	di->addr_sec_info |= WHC_DI_DEV_ADDR(wusb_dev->addr);

	ret = whc_update_di(whc, idx);

	mutex_unlock(&whc->mutex);

	return ret;
}

/*
 * Set the number of Device Notification Time Slots (DNTS) and enable
 * device notifications.
 */
int whc_set_num_dnts(struct wusbhc *wusbhc, u8 interval, u8 slots)
{
	struct whc *whc = wusbhc_to_whc(wusbhc);
	u32 dntsctrl;

	dntsctrl = WUSBDNTSCTRL_ACTIVE
		| WUSBDNTSCTRL_INTERVAL(interval)
		| WUSBDNTSCTRL_SLOTS(slots);

	le_writel(dntsctrl, whc->base + WUSBDNTSCTRL);

	return 0;
}

static int whc_set_key(struct whc *whc, u8 key_index, uint32_t tkid,
		       const void *key, size_t key_size, bool is_gtk)
{
	uint32_t setkeycmd;
	uint32_t seckey[4];
	int i;
	int ret;

	memcpy(seckey, key, key_size);
	setkeycmd = WUSBSETSECKEYCMD_SET | WUSBSETSECKEYCMD_IDX(key_index);
	if (is_gtk)
		setkeycmd |= WUSBSETSECKEYCMD_GTK;

	le_writel(tkid, whc->base + WUSBTKID);
	for (i = 0; i < 4; i++)
		le_writel(seckey[i], whc->base + WUSBSECKEY + 4*i);
	le_writel(setkeycmd, whc->base + WUSBSETSECKEYCMD);

	ret = whci_wait_for(&whc->umc->dev, whc->base + WUSBSETSECKEYCMD,
			    WUSBSETSECKEYCMD_SET, 0, 100, "set key");

	return ret;
}

/**
 * whc_set_ptk - set the PTK to use for a device.
 *
 * The index into the key table for this PTK is the same as the
 * device's port index.
 */
int whc_set_ptk(struct wusbhc *wusbhc, u8 port_idx, u32 tkid,
		const void *ptk, size_t key_size)
{
	struct whc *whc = wusbhc_to_whc(wusbhc);
	struct di_buf_entry *di = &whc->di_buf[port_idx];
	int ret;

	mutex_lock(&whc->mutex);

	if (ptk) {
		ret = whc_set_key(whc, port_idx, tkid, ptk, key_size, false);
		if (ret)
			goto out;

		di->addr_sec_info &= ~WHC_DI_KEY_IDX_MASK;
		di->addr_sec_info |= WHC_DI_SECURE | WHC_DI_KEY_IDX(port_idx);
	} else
		di->addr_sec_info &= ~WHC_DI_SECURE;

	ret = whc_update_di(whc, port_idx);
out:
	mutex_unlock(&whc->mutex);
	return ret;
}

/**
 * whc_set_gtk - set the GTK for subsequent broadcast packets
 *
 * The GTK is stored in the last entry in the key table (the previous
 * N_DEVICES entries are for the per-device PTKs).
 */
int whc_set_gtk(struct wusbhc *wusbhc, u32 tkid,
		const void *gtk, size_t key_size)
{
	struct whc *whc = wusbhc_to_whc(wusbhc);
	int ret;

	mutex_lock(&whc->mutex);

	ret = whc_set_key(whc, whc->n_devices, tkid, gtk, key_size, true);

	mutex_unlock(&whc->mutex);

	return ret;
}

int whc_set_cluster_id(struct whc *whc, u8 bcid)
{
	whc_write_wusbcmd(whc, WUSBCMD_BCID_MASK, WUSBCMD_BCID(bcid));
	return 0;
}
