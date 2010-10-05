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

#ifndef _wlc_phy_shim_h_
#define _wlc_phy_shim_h_

#define RADAR_TYPE_NONE		0	/* Radar type None */
#define RADAR_TYPE_ETSI_1	1	/* ETSI 1 Radar type */
#define RADAR_TYPE_ETSI_2	2	/* ETSI 2 Radar type */
#define RADAR_TYPE_ETSI_3	3	/* ETSI 3 Radar type */
#define RADAR_TYPE_ITU_E	4	/* ITU E Radar type */
#define RADAR_TYPE_ITU_K	5	/* ITU K Radar type */
#define RADAR_TYPE_UNCLASSIFIED	6	/* Unclassified Radar type  */
#define RADAR_TYPE_BIN5		7	/* long pulse radar type */
#define RADAR_TYPE_STG2 	8	/* staggered-2 radar */
#define RADAR_TYPE_STG3 	9	/* staggered-3 radar */
#define RADAR_TYPE_FRA		10	/* French radar */

/* French radar pulse widths */
#define FRA_T1_20MHZ	52770
#define FRA_T2_20MHZ	61538
#define FRA_T3_20MHZ	66002
#define FRA_T1_40MHZ	105541
#define FRA_T2_40MHZ	123077
#define FRA_T3_40MHZ	132004
#define FRA_ERR_20MHZ	60
#define FRA_ERR_40MHZ	120

#define ANTSEL_NA		0	/* No boardlevel selection available */
#define ANTSEL_2x4		1	/* 2x4 boardlevel selection available */
#define ANTSEL_2x3		2	/* 2x3 CB2 boardlevel selection available */

/* Rx Antenna diversity control values */
#define	ANT_RX_DIV_FORCE_0		0	/* Use antenna 0 */
#define	ANT_RX_DIV_FORCE_1		1	/* Use antenna 1 */
#define	ANT_RX_DIV_START_1		2	/* Choose starting with 1 */
#define	ANT_RX_DIV_START_0		3	/* Choose starting with 0 */
#define	ANT_RX_DIV_ENABLE		3	/* APHY bbConfig Enable RX Diversity */
#define ANT_RX_DIV_DEF		ANT_RX_DIV_START_0	/* default antdiv setting */

/* Forward declarations */
struct wlc_hw_info;
typedef struct wlc_phy_shim_info wlc_phy_shim_info_t;

extern wlc_phy_shim_info_t *wlc_phy_shim_attach(struct wlc_hw_info *wlc_hw,
						void *wl, void *wlc);
extern void wlc_phy_shim_detach(wlc_phy_shim_info_t *physhim);

/* PHY to WL utility functions */
struct wlapi_timer;
extern struct wlapi_timer *wlapi_init_timer(wlc_phy_shim_info_t *physhim,
					    void (*fn) (void *arg), void *arg,
					    const char *name);
extern void wlapi_free_timer(wlc_phy_shim_info_t *physhim,
			     struct wlapi_timer *t);
extern void wlapi_add_timer(wlc_phy_shim_info_t *physhim,
			    struct wlapi_timer *t, uint ms, int periodic);
extern bool wlapi_del_timer(wlc_phy_shim_info_t *physhim,
			    struct wlapi_timer *t);
extern void wlapi_intrson(wlc_phy_shim_info_t *physhim);
extern uint32 wlapi_intrsoff(wlc_phy_shim_info_t *physhim);
extern void wlapi_intrsrestore(wlc_phy_shim_info_t *physhim,
			       uint32 macintmask);

extern void wlapi_bmac_write_shm(wlc_phy_shim_info_t *physhim, uint offset,
				 uint16 v);
extern uint16 wlapi_bmac_read_shm(wlc_phy_shim_info_t *physhim, uint offset);
extern void wlapi_bmac_mhf(wlc_phy_shim_info_t *physhim, u8 idx,
			   uint16 mask, uint16 val, int bands);
extern void wlapi_bmac_corereset(wlc_phy_shim_info_t *physhim, uint32 flags);
extern void wlapi_suspend_mac_and_wait(wlc_phy_shim_info_t *physhim);
extern void wlapi_switch_macfreq(wlc_phy_shim_info_t *physhim, u8 spurmode);
extern void wlapi_enable_mac(wlc_phy_shim_info_t *physhim);
extern void wlapi_bmac_mctrl(wlc_phy_shim_info_t *physhim, uint32 mask,
			     uint32 val);
extern void wlapi_bmac_phy_reset(wlc_phy_shim_info_t *physhim);
extern void wlapi_bmac_bw_set(wlc_phy_shim_info_t *physhim, uint16 bw);
extern void wlapi_bmac_phyclk_fgc(wlc_phy_shim_info_t *physhim, bool clk);
extern void wlapi_bmac_macphyclk_set(wlc_phy_shim_info_t *physhim, bool clk);
extern void wlapi_bmac_core_phypll_ctl(wlc_phy_shim_info_t *physhim, bool on);
extern void wlapi_bmac_core_phypll_reset(wlc_phy_shim_info_t *physhim);
extern void wlapi_bmac_ucode_wake_override_phyreg_set(wlc_phy_shim_info_t *
						      physhim);
extern void wlapi_bmac_ucode_wake_override_phyreg_clear(wlc_phy_shim_info_t *
							physhim);
extern void wlapi_bmac_write_template_ram(wlc_phy_shim_info_t *physhim, int o,
					  int len, void *buf);
extern uint16 wlapi_bmac_rate_shm_offset(wlc_phy_shim_info_t *physhim,
					 u8 rate);
extern void wlapi_ucode_sample_init(wlc_phy_shim_info_t *physhim);
extern void wlapi_copyfrom_objmem(wlc_phy_shim_info_t *physhim, uint,
				  void *buf, int, uint32 sel);
extern void wlapi_copyto_objmem(wlc_phy_shim_info_t *physhim, uint,
				const void *buf, int, uint32);

extern void wlapi_high_update_phy_mode(wlc_phy_shim_info_t *physhim,
				       uint32 phy_mode);
extern void wlapi_bmac_pktengtx(wlc_phy_shim_info_t *physhim,
				wl_pkteng_t *pkteng, u8 rate,
				struct ether_addr *sa, uint32 wait_delay);
extern uint16 wlapi_bmac_get_txant(wlc_phy_shim_info_t *physhim);
#endif				/* _wlc_phy_shim_h_ */
