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

/* XXXXX this interface is under wlc.c by design
 * http://hwnbu-twiki.broadcom.com/bin/view/Mwgroup/WlBmacDesign
 *
 *        high driver files(e.g. wlc_ampdu.c etc)
 *             wlc.h/wlc.c
 *         wlc_bmac.h/wlc_bmac.c
 *
 *  So don't include this in files other than wlc.c, wlc_bmac* wl_rte.c(dongle port) and wl_phy.c
 *  create wrappers in wlc.c if needed
 */

/* Revision and other info required from BMAC driver for functioning of high ONLY driver */
typedef struct wlc_bmac_revinfo {
	uint vendorid;		/* PCI vendor id */
	uint deviceid;		/* device id of chip */

	uint boardrev;		/* version # of particular board */
	uint corerev;		/* core revision */
	uint sromrev;		/* srom revision */
	uint chiprev;		/* chip revision */
	uint chip;		/* chip number */
	uint chippkg;		/* chip package */
	uint boardtype;		/* board type */
	uint boardvendor;	/* board vendor */
	uint bustype;		/* SB_BUS, PCI_BUS  */
	uint buscoretype;	/* PCI_CORE_ID, PCIE_CORE_ID, PCMCIA_CORE_ID */
	uint buscorerev;	/* buscore rev */
	u32 issim;		/* chip is in simulation or emulation */

	uint nbands;

	struct band_info {
		uint bandunit;	/* To match on both sides */
		uint bandtype;	/* To match on both sides */
		uint radiorev;
		uint phytype;
		uint phyrev;
		uint anarev;
		uint radioid;
		bool abgphy_encore;
	} band[MAXBANDS];
} wlc_bmac_revinfo_t;

/* dup state between BMAC(struct wlc_hw_info) and HIGH(struct wlc_info)
   driver */
typedef struct wlc_bmac_state {
	u32 machwcap;	/* mac hw capibility */
	u32 preamble_ovr;	/* preamble override */
} wlc_bmac_state_t;

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

typedef enum {
	BMAC_DUMP_GPIO_ID,
	BMAC_DUMP_SI_ID,
	BMAC_DUMP_SIREG_ID,
	BMAC_DUMP_SICLK_ID,
	BMAC_DUMP_CCREG_ID,
	BMAC_DUMP_PCIEREG_ID,
	BMAC_DUMP_PHYREG_ID,
	BMAC_DUMP_PHYTBL_ID,
	BMAC_DUMP_PHYTBL2_ID,
	BMAC_DUMP_PHY_RADIOREG_ID,
	BMAC_DUMP_LAST
} wlc_bmac_dump_id_t;

typedef enum {
	WLCHW_STATE_ATTACH,
	WLCHW_STATE_CLK,
	WLCHW_STATE_UP,
	WLCHW_STATE_ASSOC,
	WLCHW_STATE_LAST
} wlc_bmac_state_id_t;

extern int wlc_bmac_attach(struct wlc_info *wlc, u16 vendor, u16 device,
			   uint unit, bool piomode, struct osl_info *osh,
			   void *regsva, uint bustype, void *btparam);
extern int wlc_bmac_detach(struct wlc_info *wlc);
extern void wlc_bmac_watchdog(void *arg);
extern void wlc_bmac_info_init(struct wlc_hw_info *wlc_hw);

/* up/down, reset, clk */
extern void wlc_bmac_xtal(struct wlc_hw_info *wlc_hw, bool want);

extern void wlc_bmac_copyto_objmem(struct wlc_hw_info *wlc_hw,
				   uint offset, const void *buf, int len,
				   u32 sel);
extern void wlc_bmac_copyfrom_objmem(struct wlc_hw_info *wlc_hw, uint offset,
				     void *buf, int len, u32 sel);
#define wlc_bmac_copyfrom_shm(wlc_hw, offset, buf, len)                 \
	wlc_bmac_copyfrom_objmem(wlc_hw, offset, buf, len, OBJADDR_SHM_SEL)
#define wlc_bmac_copyto_shm(wlc_hw, offset, buf, len)                   \
	wlc_bmac_copyto_objmem(wlc_hw, offset, buf, len, OBJADDR_SHM_SEL)

extern void wlc_bmac_core_phy_clk(struct wlc_hw_info *wlc_hw, bool clk);
extern void wlc_bmac_core_phypll_reset(struct wlc_hw_info *wlc_hw);
extern void wlc_bmac_core_phypll_ctl(struct wlc_hw_info *wlc_hw, bool on);
extern void wlc_bmac_phyclk_fgc(struct wlc_hw_info *wlc_hw, bool clk);
extern void wlc_bmac_macphyclk_set(struct wlc_hw_info *wlc_hw, bool clk);
extern void wlc_bmac_phy_reset(struct wlc_hw_info *wlc_hw);
extern void wlc_bmac_corereset(struct wlc_hw_info *wlc_hw, u32 flags);
extern void wlc_bmac_reset(struct wlc_hw_info *wlc_hw);
extern void wlc_bmac_init(struct wlc_hw_info *wlc_hw, chanspec_t chanspec,
			  bool mute);
extern int wlc_bmac_up_prep(struct wlc_hw_info *wlc_hw);
extern int wlc_bmac_up_finish(struct wlc_hw_info *wlc_hw);
extern int wlc_bmac_down_prep(struct wlc_hw_info *wlc_hw);
extern int wlc_bmac_down_finish(struct wlc_hw_info *wlc_hw);
extern void wlc_bmac_corereset(struct wlc_hw_info *wlc_hw, u32 flags);
extern void wlc_bmac_switch_macfreq(struct wlc_hw_info *wlc_hw, u8 spurmode);

/* chanspec, ucode interface */
extern int wlc_bmac_bandtype(struct wlc_hw_info *wlc_hw);
extern void wlc_bmac_set_chanspec(struct wlc_hw_info *wlc_hw,
				  chanspec_t chanspec,
				  bool mute, struct txpwr_limits *txpwr);

extern void wlc_bmac_txfifo(struct wlc_hw_info *wlc_hw, uint fifo, void *p,
			    bool commit, u16 frameid, u8 txpktpend);
extern int wlc_bmac_xmtfifo_sz_get(struct wlc_hw_info *wlc_hw, uint fifo,
				   uint *blocks);
extern void wlc_bmac_mhf(struct wlc_hw_info *wlc_hw, u8 idx, u16 mask,
			 u16 val, int bands);
extern void wlc_bmac_mctrl(struct wlc_hw_info *wlc_hw, u32 mask, u32 val);
extern u16 wlc_bmac_mhf_get(struct wlc_hw_info *wlc_hw, u8 idx, int bands);
extern int wlc_bmac_xmtfifo_sz_set(struct wlc_hw_info *wlc_hw, uint fifo,
				   uint blocks);
extern void wlc_bmac_txant_set(struct wlc_hw_info *wlc_hw, u16 phytxant);
extern u16 wlc_bmac_get_txant(struct wlc_hw_info *wlc_hw);
extern void wlc_bmac_antsel_type_set(struct wlc_hw_info *wlc_hw,
				     u8 antsel_type);
extern int wlc_bmac_revinfo_get(struct wlc_hw_info *wlc_hw,
				wlc_bmac_revinfo_t *revinfo);
extern int wlc_bmac_state_get(struct wlc_hw_info *wlc_hw,
			      wlc_bmac_state_t *state);
extern void wlc_bmac_write_shm(struct wlc_hw_info *wlc_hw, uint offset, u16 v);
extern u16 wlc_bmac_read_shm(struct wlc_hw_info *wlc_hw, uint offset);
extern void wlc_bmac_set_shm(struct wlc_hw_info *wlc_hw, uint offset, u16 v,
			     int len);
extern void wlc_bmac_write_template_ram(struct wlc_hw_info *wlc_hw, int offset,
					int len, void *buf);
extern void wlc_bmac_copyfrom_vars(struct wlc_hw_info *wlc_hw, char **buf,
				   uint *len);

extern void wlc_bmac_process_ps_switch(struct wlc_hw_info *wlc,
				       struct ether_addr *ea, s8 ps_on);
extern void wlc_bmac_hw_etheraddr(struct wlc_hw_info *wlc_hw,
				  struct ether_addr *ea);
extern void wlc_bmac_set_hw_etheraddr(struct wlc_hw_info *wlc_hw,
				      struct ether_addr *ea);
extern bool wlc_bmac_validate_chip_access(struct wlc_hw_info *wlc_hw);

extern bool wlc_bmac_radio_read_hwdisabled(struct wlc_hw_info *wlc_hw);
extern void wlc_bmac_set_shortslot(struct wlc_hw_info *wlc_hw, bool shortslot);
extern void wlc_bmac_mute(struct wlc_hw_info *wlc_hw, bool want, mbool flags);
extern void wlc_bmac_set_deaf(struct wlc_hw_info *wlc_hw, bool user_flag);
extern void wlc_bmac_band_stf_ss_set(struct wlc_hw_info *wlc_hw, u8 stf_mode);

extern void wlc_bmac_wait_for_wake(struct wlc_hw_info *wlc_hw);
extern bool wlc_bmac_tx_fifo_suspended(struct wlc_hw_info *wlc_hw,
				       uint tx_fifo);
extern void wlc_bmac_tx_fifo_suspend(struct wlc_hw_info *wlc_hw, uint tx_fifo);
extern void wlc_bmac_tx_fifo_resume(struct wlc_hw_info *wlc_hw, uint tx_fifo);

extern void wlc_ucode_wake_override_set(struct wlc_hw_info *wlc_hw,
					u32 override_bit);
extern void wlc_ucode_wake_override_clear(struct wlc_hw_info *wlc_hw,
					  u32 override_bit);

extern void wlc_bmac_set_rcmta(struct wlc_hw_info *wlc_hw, int idx,
			       const struct ether_addr *addr);
extern void wlc_bmac_set_addrmatch(struct wlc_hw_info *wlc_hw,
				   int match_reg_offset,
				   const struct ether_addr *addr);
extern void wlc_bmac_write_hw_bcntemplates(struct wlc_hw_info *wlc_hw,
					   void *bcn, int len, bool both);

extern void wlc_bmac_read_tsf(struct wlc_hw_info *wlc_hw, u32 *tsf_l_ptr,
			      u32 *tsf_h_ptr);
extern void wlc_bmac_set_cwmin(struct wlc_hw_info *wlc_hw, u16 newmin);
extern void wlc_bmac_set_cwmax(struct wlc_hw_info *wlc_hw, u16 newmax);
extern void wlc_bmac_set_noreset(struct wlc_hw_info *wlc, bool noreset_flag);
extern void wlc_bmac_set_ucode_loaded(struct wlc_hw_info *wlc,
				      bool ucode_loaded);

extern void wlc_bmac_retrylimit_upd(struct wlc_hw_info *wlc_hw, u16 SRL,
				    u16 LRL);

extern void wlc_bmac_fifoerrors(struct wlc_hw_info *wlc_hw);


/* API for BMAC driver (e.g. wlc_phy.c etc) */

extern void wlc_bmac_bw_set(struct wlc_hw_info *wlc_hw, u16 bw);
extern void wlc_bmac_pllreq(struct wlc_hw_info *wlc_hw, bool set,
			    mbool req_bit);
extern void wlc_bmac_set_clk(struct wlc_hw_info *wlc_hw, bool on);
extern bool wlc_bmac_taclear(struct wlc_hw_info *wlc_hw, bool ta_ok);
extern void wlc_bmac_hw_up(struct wlc_hw_info *wlc_hw);

extern void wlc_bmac_dump(struct wlc_hw_info *wlc_hw, struct bcmstrbuf *b,
			  wlc_bmac_dump_id_t dump_id);
extern void wlc_gpio_fast_deinit(struct wlc_hw_info *wlc_hw);

extern bool wlc_bmac_radio_hw(struct wlc_hw_info *wlc_hw, bool enable);
extern u16 wlc_bmac_rate_shm_offset(struct wlc_hw_info *wlc_hw, u8 rate);

extern void wlc_bmac_assert_type_set(struct wlc_hw_info *wlc_hw, u32 type);
extern void wlc_bmac_set_txpwr_percent(struct wlc_hw_info *wlc_hw, u8 val);
extern void wlc_bmac_blink_sync(struct wlc_hw_info *wlc_hw, u32 led_pins);
extern void wlc_bmac_ifsctl_edcrs_set(struct wlc_hw_info *wlc_hw, bool abie,
				      bool isht);

extern void wlc_bmac_antsel_set(struct wlc_hw_info *wlc_hw, u32 antsel_avail);
