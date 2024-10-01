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
 * This is "two-way" interface, acting as the SHIM layer between driver
 * and PHY layer. The driver can optionally call this translation layer
 * to do some preprocessing, then reach PHY. On the PHY->driver direction,
 * all calls go through this layer since PHY doesn't have access to the
 * driver's brcms_hardware pointer.
 */
#include <linux/slab.h>
#include <net/mac80211.h>

#include "main.h"
#include "mac80211_if.h"
#include "phy_shim.h"

/* PHY SHIM module specific state */
struct phy_shim_info {
	struct brcms_hardware *wlc_hw;	/* pointer to main wlc_hw structure */
	struct brcms_c_info *wlc;	/* pointer to main wlc structure */
	struct brcms_info *wl; /* pointer to os-specific private state */
};

struct phy_shim_info *wlc_phy_shim_attach(struct brcms_hardware *wlc_hw,
					  struct brcms_info *wl,
					  struct brcms_c_info *wlc) {
	struct phy_shim_info *physhim;

	physhim = kzalloc(sizeof(*physhim), GFP_ATOMIC);
	if (!physhim)
		return NULL;

	physhim->wlc_hw = wlc_hw;
	physhim->wlc = wlc;
	physhim->wl = wl;

	return physhim;
}

void wlc_phy_shim_detach(struct phy_shim_info *physhim)
{
	kfree(physhim);
}

struct wlapi_timer *wlapi_init_timer(struct phy_shim_info *physhim,
				     void (*fn)(void *pi),
				     void *arg, const char *name)
{
	return (struct wlapi_timer *)
			brcms_init_timer(physhim->wl, fn, arg, name);
}

void wlapi_free_timer(struct wlapi_timer *t)
{
	brcms_free_timer((struct brcms_timer *)t);
}

void
wlapi_add_timer(struct wlapi_timer *t, uint ms, int periodic)
{
	brcms_add_timer((struct brcms_timer *)t, ms, periodic);
}

bool wlapi_del_timer(struct wlapi_timer *t)
{
	return brcms_del_timer((struct brcms_timer *)t);
}

void wlapi_intrson(struct phy_shim_info *physhim)
{
	brcms_intrson(physhim->wl);
}

u32 wlapi_intrsoff(struct phy_shim_info *physhim)
{
	return brcms_intrsoff(physhim->wl);
}

void wlapi_intrsrestore(struct phy_shim_info *physhim, u32 macintmask)
{
	brcms_intrsrestore(physhim->wl, macintmask);
}

void wlapi_bmac_write_shm(struct phy_shim_info *physhim, uint offset, u16 v)
{
	brcms_b_write_shm(physhim->wlc_hw, offset, v);
}

u16 wlapi_bmac_read_shm(struct phy_shim_info *physhim, uint offset)
{
	return brcms_b_read_shm(physhim->wlc_hw, offset);
}

void
wlapi_bmac_mhf(struct phy_shim_info *physhim, u8 idx, u16 mask,
	       u16 val, int bands)
{
	brcms_b_mhf(physhim->wlc_hw, idx, mask, val, bands);
}

void wlapi_bmac_corereset(struct phy_shim_info *physhim, u32 flags)
{
	brcms_b_corereset(physhim->wlc_hw, flags);
}

void wlapi_suspend_mac_and_wait(struct phy_shim_info *physhim)
{
	brcms_c_suspend_mac_and_wait(physhim->wlc);
}

void wlapi_switch_macfreq(struct phy_shim_info *physhim, u8 spurmode)
{
	brcms_b_switch_macfreq(physhim->wlc_hw, spurmode);
}

void wlapi_enable_mac(struct phy_shim_info *physhim)
{
	brcms_c_enable_mac(physhim->wlc);
}

void wlapi_bmac_mctrl(struct phy_shim_info *physhim, u32 mask, u32 val)
{
	brcms_b_mctrl(physhim->wlc_hw, mask, val);
}

void wlapi_bmac_phy_reset(struct phy_shim_info *physhim)
{
	brcms_b_phy_reset(physhim->wlc_hw);
}

void wlapi_bmac_bw_set(struct phy_shim_info *physhim, u16 bw)
{
	brcms_b_bw_set(physhim->wlc_hw, bw);
}

u16 wlapi_bmac_get_txant(struct phy_shim_info *physhim)
{
	return brcms_b_get_txant(physhim->wlc_hw);
}

void wlapi_bmac_phyclk_fgc(struct phy_shim_info *physhim, bool clk)
{
	brcms_b_phyclk_fgc(physhim->wlc_hw, clk);
}

void wlapi_bmac_macphyclk_set(struct phy_shim_info *physhim, bool clk)
{
	brcms_b_macphyclk_set(physhim->wlc_hw, clk);
}

void wlapi_bmac_core_phypll_ctl(struct phy_shim_info *physhim, bool on)
{
	brcms_b_core_phypll_ctl(physhim->wlc_hw, on);
}

void wlapi_bmac_core_phypll_reset(struct phy_shim_info *physhim)
{
	brcms_b_core_phypll_reset(physhim->wlc_hw);
}

void wlapi_bmac_ucode_wake_override_phyreg_set(struct phy_shim_info *physhim)
{
	brcms_c_ucode_wake_override_set(physhim->wlc_hw,
					BRCMS_WAKE_OVERRIDE_PHYREG);
}

void wlapi_bmac_ucode_wake_override_phyreg_clear(struct phy_shim_info *physhim)
{
	brcms_c_ucode_wake_override_clear(physhim->wlc_hw,
					  BRCMS_WAKE_OVERRIDE_PHYREG);
}

void
wlapi_bmac_write_template_ram(struct phy_shim_info *physhim, int offset,
			      int len, void *buf)
{
	brcms_b_write_template_ram(physhim->wlc_hw, offset, len, buf);
}

u16 wlapi_bmac_rate_shm_offset(struct phy_shim_info *physhim, u8 rate)
{
	return brcms_b_rate_shm_offset(physhim->wlc_hw, rate);
}

void wlapi_ucode_sample_init(struct phy_shim_info *physhim)
{
}

void
wlapi_copyfrom_objmem(struct phy_shim_info *physhim, uint offset, void *buf,
		      int len, u32 sel)
{
	brcms_b_copyfrom_objmem(physhim->wlc_hw, offset, buf, len, sel);
}

void
wlapi_copyto_objmem(struct phy_shim_info *physhim, uint offset, const void *buf,
		    int l, u32 sel)
{
	brcms_b_copyto_objmem(physhim->wlc_hw, offset, buf, l, sel);
}
