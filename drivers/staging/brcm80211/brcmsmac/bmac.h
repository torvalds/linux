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
#ifndef _BRCM_BOTTOM_MAC_H_
#define _BRCM_BOTTOM_MAC_H_

#include <brcmu_wifi.h>
#include "types.h"

/* dup state between BMAC(struct brcms_hardware) and HIGH(struct brcms_c_info)
   driver */
struct brcms_b_state {
	u32 machwcap;	/* mac hw capibility */
	u32 preamble_ovr;	/* preamble override */
};

enum {
	IOV_BMAC_DIAG,
	IOV_BMAC_SBGPIOTIMERVAL,
	IOV_BMAC_SBGPIOOUT,
	IOV_BMAC_CCGPIOCTRL,	/* CC GPIOCTRL REG */
	IOV_BMAC_CCGPIOOUT,	/* CC GPIOOUT REG */
	IOV_BMAC_CCGPIOOUTEN,	/* CC GPIOOUTEN REG */
	IOV_BMAC_CCGPIOIN,	/* CC GPIOIN REG */
	IOV_BMAC_WPSGPIO,	/* WPS push button GPIO pin */
	IOV_BMAC_OTPDUMP,
	IOV_BMAC_OTPSTAT,
	IOV_BMAC_PCIEASPM,	/* obfuscation clkreq/aspm control */
	IOV_BMAC_PCIEADVCORRMASK,	/* advanced correctable error mask */
	IOV_BMAC_PCIECLKREQ,	/* PCIE 1.1 clockreq enab support */
	IOV_BMAC_PCIELCREG,	/* PCIE LCREG */
	IOV_BMAC_SBGPIOTIMERMASK,
	IOV_BMAC_RFDISABLEDLY,
	IOV_BMAC_PCIEREG,	/* PCIE REG */
	IOV_BMAC_PCICFGREG,	/* PCI Config register */
	IOV_BMAC_PCIESERDESREG,	/* PCIE SERDES REG (dev, 0}offset) */
	IOV_BMAC_PCIEGPIOOUT,	/* PCIEOUT REG */
	IOV_BMAC_PCIEGPIOOUTEN,	/* PCIEOUTEN REG */
	IOV_BMAC_PCIECLKREQENCTRL,	/* clkreqenctrl REG (PCIE REV > 6.0 */
	IOV_BMAC_DMALPBK,
	IOV_BMAC_CCREG,
	IOV_BMAC_COREREG,
	IOV_BMAC_SDCIS,
	IOV_BMAC_SDIO_DRIVE,
	IOV_BMAC_OTPW,
	IOV_BMAC_NVOTPW,
	IOV_BMAC_SROM,
	IOV_BMAC_SRCRC,
	IOV_BMAC_CIS_SOURCE,
	IOV_BMAC_CISVAR,
	IOV_BMAC_OTPLOCK,
	IOV_BMAC_OTP_CHIPID,
	IOV_BMAC_CUSTOMVAR1,
	IOV_BMAC_BOARDFLAGS,
	IOV_BMAC_BOARDFLAGS2,
	IOV_BMAC_WPSLED,
	IOV_BMAC_NVRAM_SOURCE,
	IOV_BMAC_OTP_RAW_READ,
	IOV_BMAC_LAST
};

extern int brcms_b_attach(struct brcms_c_info *wlc, u16 vendor, u16 device,
			   uint unit, bool piomode, void *regsva, uint bustype,
			   void *btparam);
extern int brcms_b_detach(struct brcms_c_info *wlc);
extern void brcms_b_watchdog(void *arg);

/* up/down, reset, clk */
extern void brcms_b_copyto_objmem(struct brcms_hardware *wlc_hw,
				   uint offset, const void *buf, int len,
				   u32 sel);
extern void brcms_b_copyfrom_objmem(struct brcms_hardware *wlc_hw, uint offset,
				     void *buf, int len, u32 sel);
#define brcms_b_copyfrom_shm(wlc_hw, offset, buf, len)                 \
	brcms_b_copyfrom_objmem(wlc_hw, offset, buf, len, OBJADDR_SHM_SEL)
#define brcms_b_copyto_shm(wlc_hw, offset, buf, len)                   \
	brcms_b_copyto_objmem(wlc_hw, offset, buf, len, OBJADDR_SHM_SEL)

extern void brcms_b_core_phypll_reset(struct brcms_hardware *wlc_hw);
extern void brcms_b_core_phypll_ctl(struct brcms_hardware *wlc_hw, bool on);
extern void brcms_b_phyclk_fgc(struct brcms_hardware *wlc_hw, bool clk);
extern void brcms_b_macphyclk_set(struct brcms_hardware *wlc_hw, bool clk);
extern void brcms_b_phy_reset(struct brcms_hardware *wlc_hw);
extern void brcms_b_corereset(struct brcms_hardware *wlc_hw, u32 flags);
extern void brcms_b_reset(struct brcms_hardware *wlc_hw);
extern void brcms_b_init(struct brcms_hardware *wlc_hw, chanspec_t chanspec,
			  bool mute);
extern int brcms_b_up_prep(struct brcms_hardware *wlc_hw);
extern int brcms_b_up_finish(struct brcms_hardware *wlc_hw);
extern int brcms_b_bmac_down_prep(struct brcms_hardware *wlc_hw);
extern int brcms_b_down_finish(struct brcms_hardware *wlc_hw);
extern void brcms_b_switch_macfreq(struct brcms_hardware *wlc_hw, u8 spurmode);

/* chanspec, ucode interface */
extern void brcms_b_set_chanspec(struct brcms_hardware *wlc_hw,
				  chanspec_t chanspec,
				  bool mute, struct txpwr_limits *txpwr);

extern int brcms_b_xmtfifo_sz_get(struct brcms_hardware *wlc_hw, uint fifo,
				   uint *blocks);
extern void brcms_b_mhf(struct brcms_hardware *wlc_hw, u8 idx, u16 mask,
			 u16 val, int bands);
extern void brcms_b_mctrl(struct brcms_hardware *wlc_hw, u32 mask, u32 val);
extern u16 brcms_b_mhf_get(struct brcms_hardware *wlc_hw, u8 idx, int bands);
extern void brcms_b_txant_set(struct brcms_hardware *wlc_hw, u16 phytxant);
extern u16 brcms_b_get_txant(struct brcms_hardware *wlc_hw);
extern void brcms_b_antsel_type_set(struct brcms_hardware *wlc_hw,
				     u8 antsel_type);
extern int brcms_b_state_get(struct brcms_hardware *wlc_hw,
			      struct brcms_b_state *state);
extern void brcms_b_write_shm(struct brcms_hardware *wlc_hw, uint offset,
			      u16 v);
extern u16 brcms_b_read_shm(struct brcms_hardware *wlc_hw, uint offset);
extern void brcms_b_write_template_ram(struct brcms_hardware *wlc_hw,
				       int offset, int len, void *buf);
extern void brcms_b_copyfrom_vars(struct brcms_hardware *wlc_hw, char **buf,
				   uint *len);

extern void brcms_b_hw_etheraddr(struct brcms_hardware *wlc_hw,
				  u8 *ea);

extern bool brcms_b_radio_read_hwdisabled(struct brcms_hardware *wlc_hw);
extern void brcms_b_set_shortslot(struct brcms_hardware *wlc_hw,
				  bool shortslot);
extern void brcms_b_band_stf_ss_set(struct brcms_hardware *wlc_hw,
				    u8 stf_mode);

extern void brcms_b_wait_for_wake(struct brcms_hardware *wlc_hw);

extern void brcms_c_ucode_wake_override_set(struct brcms_hardware *wlc_hw,
					u32 override_bit);
extern void brcms_c_ucode_wake_override_clear(struct brcms_hardware *wlc_hw,
					  u32 override_bit);

extern void brcms_b_set_addrmatch(struct brcms_hardware *wlc_hw,
				   int match_reg_offset,
				   const u8 *addr);
extern void brcms_b_write_hw_bcntemplates(struct brcms_hardware *wlc_hw,
					   void *bcn, int len, bool both);

extern void brcms_b_read_tsf(struct brcms_hardware *wlc_hw, u32 *tsf_l_ptr,
			      u32 *tsf_h_ptr);
extern void brcms_b_set_cwmin(struct brcms_hardware *wlc_hw, u16 newmin);
extern void brcms_b_set_cwmax(struct brcms_hardware *wlc_hw, u16 newmax);

extern void brcms_b_retrylimit_upd(struct brcms_hardware *wlc_hw, u16 SRL,
				    u16 LRL);

extern void brcms_b_fifoerrors(struct brcms_hardware *wlc_hw);


/* API for BMAC driver (e.g. wlc_phy.c etc) */

extern void brcms_b_bw_set(struct brcms_hardware *wlc_hw, u16 bw);
extern void brcms_b_pllreq(struct brcms_hardware *wlc_hw, bool set,
			    mbool req_bit);
extern void brcms_b_hw_up(struct brcms_hardware *wlc_hw);
extern u16 brcms_b_rate_shm_offset(struct brcms_hardware *wlc_hw, u8 rate);
extern void brcms_b_antsel_set(struct brcms_hardware *wlc_hw,
			       u32 antsel_avail);

#endif /* _BRCM_BOTTOM_MAC_H_ */
