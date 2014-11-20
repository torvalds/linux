/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2007-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_ENUM_H
#define EFX_ENUM_H

/**
 * enum efx_loopback_mode - loopback modes
 * @LOOPBACK_NONE: no loopback
 * @LOOPBACK_DATA: data path loopback
 * @LOOPBACK_GMAC: loopback within GMAC
 * @LOOPBACK_XGMII: loopback after XMAC
 * @LOOPBACK_XGXS: loopback within BPX after XGXS
 * @LOOPBACK_XAUI: loopback within BPX before XAUI serdes
 * @LOOPBACK_GMII: loopback within BPX after GMAC
 * @LOOPBACK_SGMII: loopback within BPX within SGMII
 * @LOOPBACK_XGBR: loopback within BPX within XGBR
 * @LOOPBACK_XFI: loopback within BPX before XFI serdes
 * @LOOPBACK_XAUI_FAR: loopback within BPX after XAUI serdes
 * @LOOPBACK_GMII_FAR: loopback within BPX before SGMII
 * @LOOPBACK_SGMII_FAR: loopback within BPX after SGMII
 * @LOOPBACK_XFI_FAR: loopback after XFI serdes
 * @LOOPBACK_GPHY: loopback within 1G PHY at unspecified level
 * @LOOPBACK_PHYXS: loopback within 10G PHY at PHYXS level
 * @LOOPBACK_PCS: loopback within 10G PHY at PCS level
 * @LOOPBACK_PMAPMD: loopback within 10G PHY at PMAPMD level
 * @LOOPBACK_XPORT: cross port loopback
 * @LOOPBACK_XGMII_WS: wireside loopback excluding XMAC
 * @LOOPBACK_XAUI_WS: wireside loopback within BPX within XAUI serdes
 * @LOOPBACK_XAUI_WS_FAR: wireside loopback within BPX including XAUI serdes
 * @LOOPBACK_XAUI_WS_NEAR: wireside loopback within BPX excluding XAUI serdes
 * @LOOPBACK_GMII_WS: wireside loopback excluding GMAC
 * @LOOPBACK_XFI_WS: wireside loopback excluding XFI serdes
 * @LOOPBACK_XFI_WS_FAR: wireside loopback including XFI serdes
 * @LOOPBACK_PHYXS_WS: wireside loopback within 10G PHY at PHYXS level
 */
/* Please keep up-to-date w.r.t the following two #defines */
enum efx_loopback_mode {
	LOOPBACK_NONE = 0,
	LOOPBACK_DATA = 1,
	LOOPBACK_GMAC = 2,
	LOOPBACK_XGMII = 3,
	LOOPBACK_XGXS = 4,
	LOOPBACK_XAUI = 5,
	LOOPBACK_GMII = 6,
	LOOPBACK_SGMII = 7,
	LOOPBACK_XGBR = 8,
	LOOPBACK_XFI = 9,
	LOOPBACK_XAUI_FAR = 10,
	LOOPBACK_GMII_FAR = 11,
	LOOPBACK_SGMII_FAR = 12,
	LOOPBACK_XFI_FAR = 13,
	LOOPBACK_GPHY = 14,
	LOOPBACK_PHYXS = 15,
	LOOPBACK_PCS = 16,
	LOOPBACK_PMAPMD = 17,
	LOOPBACK_XPORT = 18,
	LOOPBACK_XGMII_WS = 19,
	LOOPBACK_XAUI_WS = 20,
	LOOPBACK_XAUI_WS_FAR = 21,
	LOOPBACK_XAUI_WS_NEAR = 22,
	LOOPBACK_GMII_WS = 23,
	LOOPBACK_XFI_WS = 24,
	LOOPBACK_XFI_WS_FAR = 25,
	LOOPBACK_PHYXS_WS = 26,
	LOOPBACK_MAX
};
#define LOOPBACK_TEST_MAX LOOPBACK_PMAPMD

/* These loopbacks occur within the controller */
#define LOOPBACKS_INTERNAL ((1 << LOOPBACK_DATA) |		\
			    (1 << LOOPBACK_GMAC) |		\
			    (1 << LOOPBACK_XGMII)|		\
			    (1 << LOOPBACK_XGXS) |		\
			    (1 << LOOPBACK_XAUI) |		\
			    (1 << LOOPBACK_GMII) |		\
			    (1 << LOOPBACK_SGMII) |		\
			    (1 << LOOPBACK_SGMII) |		\
			    (1 << LOOPBACK_XGBR) |		\
			    (1 << LOOPBACK_XFI) |		\
			    (1 << LOOPBACK_XAUI_FAR) |		\
			    (1 << LOOPBACK_GMII_FAR) |		\
			    (1 << LOOPBACK_SGMII_FAR) |		\
			    (1 << LOOPBACK_XFI_FAR) |		\
			    (1 << LOOPBACK_XGMII_WS) |		\
			    (1 << LOOPBACK_XAUI_WS) |		\
			    (1 << LOOPBACK_XAUI_WS_FAR) |	\
			    (1 << LOOPBACK_XAUI_WS_NEAR) |	\
			    (1 << LOOPBACK_GMII_WS) |		\
			    (1 << LOOPBACK_XFI_WS) |		\
			    (1 << LOOPBACK_XFI_WS_FAR))

#define LOOPBACKS_WS ((1 << LOOPBACK_XGMII_WS) |		\
		      (1 << LOOPBACK_XAUI_WS) |			\
		      (1 << LOOPBACK_XAUI_WS_FAR) |		\
		      (1 << LOOPBACK_XAUI_WS_NEAR) |		\
		      (1 << LOOPBACK_GMII_WS) |			\
		      (1 << LOOPBACK_XFI_WS) |			\
		      (1 << LOOPBACK_XFI_WS_FAR) |		\
		      (1 << LOOPBACK_PHYXS_WS))

#define LOOPBACKS_EXTERNAL(_efx)					\
	((_efx)->loopback_modes & ~LOOPBACKS_INTERNAL &			\
	 ~(1 << LOOPBACK_NONE))

#define LOOPBACK_MASK(_efx)			\
	(1 << (_efx)->loopback_mode)

#define LOOPBACK_INTERNAL(_efx)				\
	(!!(LOOPBACKS_INTERNAL & LOOPBACK_MASK(_efx)))

#define LOOPBACK_EXTERNAL(_efx)				\
	(!!(LOOPBACK_MASK(_efx) & LOOPBACKS_EXTERNAL(_efx)))

#define LOOPBACK_CHANGED(_from, _to, _mask)				\
	(!!((LOOPBACK_MASK(_from) ^ LOOPBACK_MASK(_to)) & (_mask)))

#define LOOPBACK_OUT_OF(_from, _to, _mask)				\
	((LOOPBACK_MASK(_from) & (_mask)) && !(LOOPBACK_MASK(_to) & (_mask)))

/*****************************************************************************/

/**
 * enum reset_type - reset types
 *
 * %RESET_TYPE_INVSIBLE, %RESET_TYPE_ALL, %RESET_TYPE_WORLD and
 * %RESET_TYPE_DISABLE specify the method/scope of the reset.  The
 * other valuesspecify reasons, which efx_schedule_reset() will choose
 * a method for.
 *
 * Reset methods are numbered in order of increasing scope.
 *
 * @RESET_TYPE_INVISIBLE: Reset datapath and MAC (Falcon only)
 * @RESET_TYPE_RECOVER_OR_ALL: Try to recover. Apply RESET_TYPE_ALL
 * if unsuccessful.
 * @RESET_TYPE_ALL: Reset datapath, MAC and PHY
 * @RESET_TYPE_WORLD: Reset as much as possible
 * @RESET_TYPE_RECOVER_OR_DISABLE: Try to recover. Apply RESET_TYPE_DISABLE if
 * unsuccessful.
 * @RESET_TYPE_MC_BIST: MC entering BIST mode.
 * @RESET_TYPE_DISABLE: Reset datapath, MAC and PHY; leave NIC disabled
 * @RESET_TYPE_TX_WATCHDOG: reset due to TX watchdog
 * @RESET_TYPE_INT_ERROR: reset due to internal error
 * @RESET_TYPE_RX_RECOVERY: reset to recover from RX datapath errors
 * @RESET_TYPE_DMA_ERROR: DMA error
 * @RESET_TYPE_TX_SKIP: hardware completed empty tx descriptors
 * @RESET_TYPE_MC_FAILURE: MC reboot/assertion
 * @RESET_TYPE_MCDI_TIMEOUT: MCDI timeout.
 */
enum reset_type {
	RESET_TYPE_INVISIBLE,
	RESET_TYPE_RECOVER_OR_ALL,
	RESET_TYPE_ALL,
	RESET_TYPE_WORLD,
	RESET_TYPE_RECOVER_OR_DISABLE,
	RESET_TYPE_MC_BIST,
	RESET_TYPE_DISABLE,
	RESET_TYPE_MAX_METHOD,
	RESET_TYPE_TX_WATCHDOG,
	RESET_TYPE_INT_ERROR,
	RESET_TYPE_RX_RECOVERY,
	RESET_TYPE_DMA_ERROR,
	RESET_TYPE_TX_SKIP,
	RESET_TYPE_MC_FAILURE,
	/* RESET_TYPE_MCDI_TIMEOUT is actually a method, not just a reason, but
	 * it doesn't fit the scope hierarchy (not well-ordered by inclusion).
	 * We encode this by having its enum value be greater than
	 * RESET_TYPE_MAX_METHOD. This also prevents issuing it with
	 * efx_ioctl_reset.
	 */
	RESET_TYPE_MCDI_TIMEOUT,
	RESET_TYPE_MAX,
};

#endif /* EFX_ENUM_H */
