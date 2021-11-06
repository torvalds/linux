/*
 * Generic functions for d11 access
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 */

#ifndef	_hndd11_h_
#define	_hndd11_h_

#include <typedefs.h>
#include <osl_decl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <d11.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

#ifndef WL_RSSI_ANT_MAX
#define WL_RSSI_ANT_MAX		4	/**< max possible rx antennas */
#elif WL_RSSI_ANT_MAX != 4
#error "WL_RSSI_ANT_MAX does not match"
#endif

BWL_PRE_PACKED_STRUCT struct wl_d11rxrssi {
	int8 dBm;  /* number of full dBms */
	/* sub-dbm resolution */
	int8  decidBm; /* sub dBms : value after the decimal point */
} BWL_POST_PACKED_STRUCT;

typedef struct wl_d11rxrssi  wlc_d11rxrssi_t;

BWL_PRE_PACKED_STRUCT struct wlc_d11rxhdr {
	/* SW header */
	uint32  tsf_l;       /**< TSF_L reading */
	int8    rssi;        /**< computed instantaneous rssi */
	int8    rssi_qdb;    /**< qdB portion of the computed rssi */
	int16   snr;         /**< computed snginal-to-noise instantaneous snr */
	int8	rxpwr[ROUNDUP(WL_RSSI_ANT_MAX,2)];	/**< rssi for supported antennas */
	/**
	 * Even though rxhdr can be in short or long format, always declare it here
	 * to be in long format. So the offsets for the other fields are always the same.
	 */
	d11rxhdr_t rxhdr;
} BWL_POST_PACKED_STRUCT;

/* SW RXHDR + HW RXHDR */
typedef struct wlc_d11rxhdr wlc_d11rxhdr_t;

/* extension of wlc_d11rxhdr..
 * This extra block can be used to store extra internal information that cannot fit into
 *  wlc_d11rxhdr.
 *  At the moment, it is only used to store and possibly transmit the per-core quater dbm rssi
 *  information produced by the phy.
 * NOTE: To avoid header overhead and amsdu handling complexities this usage is limited to
 * only in case that host need to get the extra info. e.g., monitoring mode packet.
 */

BWL_PRE_PACKED_STRUCT struct wlc_d11rxhdr_ext {
#ifdef BCM_MON_QDBM_RSSI
	wlc_d11rxrssi_t rxpwr[WL_RSSI_ANT_MAX];
#endif
	wlc_d11rxhdr_t wlc_d11rx;
} BWL_POST_PACKED_STRUCT;

typedef struct wlc_d11rxhdr_ext wlc_d11rxhdr_ext_t;

/* Length of software rx header extension */
#define WLC_SWRXHDR_EXT_LEN  (OFFSETOF(wlc_d11rxhdr_ext_t, wlc_d11rx))

/* Length of SW header (12 bytes) */
#define WLC_RXHDR_LEN		(OFFSETOF(wlc_d11rxhdr_t, rxhdr))
/* Length of RX headers - SW header + HW/ucode/PHY RX status */
#define WL_RXHDR_LEN(corerev, corerev_minor) \
	(WLC_RXHDR_LEN + D11_RXHDR_LEN(corerev, corerev_minor))
#define WL_RXHDR_LEN_TMP(corerev, corerev_minor) \
	(WLC_RXHDR_LEN + D11_RXHDR_LEN_TMP(corerev, corerev_minor))

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

/* Structure to hold d11 corerev information */
typedef struct d11_info d11_info_t;
struct d11_info {
	uint major_revid;
	uint minor_revid;
};

/* ulp dbg macro */
#define HNDD11_DBG(x)
#define HNDD11_ERR(x) printf x

/* d11 slice index */
#define DUALMAC_MAIN	0
#define DUALMAC_AUX	1
#define DUALMAC_SCAN	2

extern void hndd11_read_shm(si_t *sih, uint coreunit, uint offset, void* buf);
extern void hndd11_write_shm(si_t *sih, uint coreunit, uint offset, const void* buf);

extern void hndd11_copyfrom_shm(si_t *sih, uint coreunit, uint offset, void* buf, int len);
extern void hndd11_copyto_shm(si_t *sih, uint coreunit, uint offset, const void* buf, int len);

extern uint32 hndd11_bm_read(osl_t *osh, d11regs_info_t *regsinfo, uint32 offset, uint32 len,
	uint32 *buf);
extern uint32 hndd11_bm_write(osl_t *osh, d11regs_info_t *regsinfo, uint32 offset, uint32 len,
	const uint32 *buf);
extern void hndd11_bm_dump(osl_t *osh, d11regs_info_t *regsinfo, uint32 offset, uint32 len);

extern int hndd11_get_reginfo(si_t *sih, d11regs_info_t *regsinfo, uint coreunit);

#endif	/* _hndd11_h_ */
