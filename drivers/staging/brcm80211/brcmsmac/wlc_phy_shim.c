/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This is "two-way" interface, acting as the SHIM layer between WL and PHY layer.
 *   WL driver can optinally call this translation layer to do some preprocessing, then reach PHY.
 *   On the PHY->WL driver direction, all calls go through this layer since PHY doesn't have the
 *   access to wlc_hw pointer.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <proto/802.11.h>
#include <bcmdefs.h>
#include <bcmutils.h>
#include <bcmwifi.h>
#include <siutils.h>
#include <wlioctl.h>
#include <sbconfig.h>
#include <sbchipc.h>
#include <pcicfg.h>
#include <sbhnddma.h>
#include <hnddma.h>
#include <hndpmu.h>

#include "wlc_types.h"
#include "wl_dbg.h"
#include "wlc_cfg.h"
#include "d11.h"
#include "wlc_rate.h"
#include "wlc_scb.h"
#include "wlc_pub.h"
#include "phy/wlc_phy_hal.h"
#include "wlc_channel.h"
#include "bcmsrom.h"
#include "wlc_key.h"
#include "wlc_bmac.h"
#include "wlc_phy_hal.h"
#include "wl_export.h"
#include "wlc_main.h"
#include "wlc_phy_shim.h"

/* PHY SHIM module specific state */
struct wlc_phy_shim_info {
	struct wlc_hw_info *wlc_hw;	/* pointer to main wlc_hw structure */
	void *wlc;		/* pointer to main wlc structure */
	void *wl;		/* pointer to os-specific private state */
};

wlc_phy_shim_info_t *wlc_phy_shim_attach(struct wlc_hw_info *wlc_hw,
						       void *wl, void *wlc) {
	wlc_phy_shim_info_t *physhim = NULL;

	physhim = kzalloc(sizeof(wlc_phy_shim_info_t), GFP_ATOMIC);
	if (!physhim) {
		WL_ERROR("wl%d: wlc_phy_shim_attach: out of mem\n",
			 wlc_hw->unit);
		return NULL;
	}
	physhim->wlc_hw = wlc_hw;
	physhim->wlc = wlc;
	physhim->wl = wl;

	return physhim;
}

void wlc_phy_shim_detach(wlc_phy_shim_info_t *physhim)
{
	kfree(physhim);
}

struct wlapi_timer *wlapi_init_timer(wlc_phy_shim_info_t *physhim,
				     void (*fn) (void *arg), void *arg,
				     const char *name)
{
	return (struct wlapi_timer *)wl_init_timer(physhim->wl, fn, arg, name);
}

void wlapi_free_timer(wlc_phy_shim_info_t *physhim, struct wlapi_timer *t)
{
	wl_free_timer(physhim->wl, (struct wl_timer *)t);
}

void
wlapi_add_timer(wlc_phy_shim_info_t *physhim, struct wlapi_timer *t, uint ms,
		int periodic)
{
	wl_add_timer(physhim->wl, (struct wl_timer *)t, ms, periodic);
}

bool wlapi_del_timer(wlc_phy_shim_info_t *physhim, struct wlapi_timer *t)
{
	return wl_del_timer(physhim->wl, (struct wl_timer *)t);
}

void wlapi_intrson(wlc_phy_shim_info_t *physhim)
{
	wl_intrson(physhim->wl);
}

u32 wlapi_intrsoff(wlc_phy_shim_info_t *physhim)
{
	return wl_intrsoff(physhim->wl);
}

void wlapi_intrsrestore(wlc_phy_shim_info_t *physhim, u32 macintmask)
{
	wl_intrsrestore(physhim->wl, macintmask);
}

void wlapi_bmac_write_shm(wlc_phy_shim_info_t *physhim, uint offset, u16 v)
{
	wlc_bmac_write_shm(physhim->wlc_hw, offset, v);
}

u16 wlapi_bmac_read_shm(wlc_phy_shim_info_t *physhim, uint offset)
{
	return wlc_bmac_read_shm(physhim->wlc_hw, offset);
}

void
wlapi_bmac_mhf(wlc_phy_shim_info_t *physhim, u8 idx, u16 mask,
	       u16 val, int bands)
{
	wlc_bmac_mhf(physhim->wlc_hw, idx, mask, val, bands);
}

void wlapi_bmac_corereset(wlc_phy_shim_info_t *physhim, u32 flags)
{
	wlc_bmac_corereset(physhim->wlc_hw, flags);
}

void wlapi_suspend_mac_and_wait(wlc_phy_shim_info_t *physhim)
{
	wlc_suspend_mac_and_wait(physhim->wlc);
}

void wlapi_switch_macfreq(wlc_phy_shim_info_t *physhim, u8 spurmode)
{
	wlc_bmac_switch_macfreq(physhim->wlc_hw, spurmode);
}

void wlapi_enable_mac(wlc_phy_shim_info_t *physhim)
{
	wlc_enable_mac(physhim->wlc);
}

void wlapi_bmac_mctrl(wlc_phy_shim_info_t *physhim, u32 mask, u32 val)
{
	wlc_bmac_mctrl(physhim->wlc_hw, mask, val);
}

void wlapi_bmac_phy_reset(wlc_phy_shim_info_t *physhim)
{
	wlc_bmac_phy_reset(physhim->wlc_hw);
}

void wlapi_bmac_bw_set(wlc_phy_shim_info_t *physhim, u16 bw)
{
	wlc_bmac_bw_set(physhim->wlc_hw, bw);
}

u16 wlapi_bmac_get_txant(wlc_phy_shim_info_t *physhim)
{
	return wlc_bmac_get_txant(physhim->wlc_hw);
}

void wlapi_bmac_phyclk_fgc(wlc_phy_shim_info_t *physhim, bool clk)
{
	wlc_bmac_phyclk_fgc(physhim->wlc_hw, clk);
}

void wlapi_bmac_macphyclk_set(wlc_phy_shim_info_t *physhim, bool clk)
{
	wlc_bmac_macphyclk_set(physhim->wlc_hw, clk);
}

void wlapi_bmac_core_phypll_ctl(wlc_phy_shim_info_t *physhim, bool on)
{
	wlc_bmac_core_phypll_ctl(physhim->wlc_hw, on);
}

void wlapi_bmac_core_phypll_reset(wlc_phy_shim_info_t *physhim)
{
	wlc_bmac_core_phypll_reset(physhim->wlc_hw);
}

void wlapi_bmac_ucode_wake_override_phyreg_set(wlc_phy_shim_info_t *physhim)
{
	wlc_ucode_wake_override_set(physhim->wlc_hw, WLC_WAKE_OVERRIDE_PHYREG);
}

void wlapi_bmac_ucode_wake_override_phyreg_clear(wlc_phy_shim_info_t *physhim)
{
	wlc_ucode_wake_override_clear(physhim->wlc_hw,
				      WLC_WAKE_OVERRIDE_PHYREG);
}

void
wlapi_bmac_write_template_ram(wlc_phy_shim_info_t *physhim, int offset,
			      int len, void *buf)
{
	wlc_bmac_write_template_ram(physhim->wlc_hw, offset, len, buf);
}

u16 wlapi_bmac_rate_shm_offset(wlc_phy_shim_info_t *physhim, u8 rate)
{
	return wlc_bmac_rate_shm_offset(physhim->wlc_hw, rate);
}

void wlapi_ucode_sample_init(wlc_phy_shim_info_t *physhim)
{
}

void
wlapi_copyfrom_objmem(wlc_phy_shim_info_t *physhim, uint offset, void *buf,
		      int len, u32 sel)
{
	wlc_bmac_copyfrom_objmem(physhim->wlc_hw, offset, buf, len, sel);
}

void
wlapi_copyto_objmem(wlc_phy_shim_info_t *physhim, uint offset, const void *buf,
		    int l, u32 sel)
{
	wlc_bmac_copyto_objmem(physhim->wlc_hw, offset, buf, l, sel);
}
