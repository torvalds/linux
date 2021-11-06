/*
 * Monitor Mode routines.
 * This header file housing the define and function use by DHD
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
#ifndef _BCMWIFI_MONITOR_H_
#define _BCMWIFI_MONITOR_H_

#include <monitor.h>

typedef struct monitor_info monitor_info_t;

typedef struct monitor_pkt_ts {
	union {
		uint32	ts_low; /* time stamp low 32 bits */
		uint32	reserved; /* If timestamp not used */
	};
	union {
		uint32  ts_high; /* time stamp high 28 bits */
		union {
			uint32  ts_high_ext :28; /* time stamp high 28 bits */
			uint32  clk_id_ext :3; /* clock ID source  */
			uint32  phase :1; /* Phase bit */
			uint32	marker_ext;
		};
	};
} monitor_pkt_ts_t;

typedef struct monitor_pkt_info {
	uint32	marker;
	/* timestamp */
	monitor_pkt_ts_t ts;
} monitor_pkt_info_t;

typedef struct monitor_pkt_rssi {
	int8 dBm;  /* number of full dBms */
	/* sub-dbm resolution */
	int8  decidBm; /* sub dBms : value after the decimal point */
} monitor_pkt_rssi_t;

/* structure to add specific information to rxsts structure
 * otherwise non available to all modules like core RSSI and qdbm resolution
*/

typedef struct monitor_pkt_rxsts {
	wl_rxsts_t *rxsts;
	uint8   corenum; /* number of cores/antennas */
	monitor_pkt_rssi_t rxpwr[4];
} monitor_pkt_rxsts_t;

#define HE_EXTRACT_FROM_PLCP(plcp, ppdu_type, field)						\
		(getbits(plcp, D11_PHY_HDR_LEN,							\
			HE_ ## ppdu_type ## _PPDU_ ## field ## _IDX,				\
			HE_ ## ppdu_type ## _PPDU_ ## field ## _FSZ))

#define HE_PACK_RTAP_FROM_PLCP(plcp, ppdu_type, field)						\
		(HE_EXTRACT_FROM_PLCP(plcp, ppdu_type, field) <<				\
			HE_RADIOTAP_ ## field ## _SHIFT)

#define HE_PACK_RTAP_GI_LTF_FROM_PLCP(plcp, ppdu_type, field, member)				\
		((he_plcp2ltf_gi[HE_EXTRACT_FROM_PLCP(plcp, ppdu_type, field)].member) <<	\
			HE_RADIOTAP_ ## field ## _SHIFT)

#define HE_PACK_RTAP_FROM_VAL(val, field)							\
		((val) << HE_RADIOTAP_ ## field ## _SHIFT)

#define HE_PACK_RTAP_FROM_PRXS(rxh, corerev, corerev_minor, field)				\
		(HE_PACK_RTAP_FROM_VAL(D11PPDU_ ## field(rxh, corerev, corerev_minor), field))

/* channel bandwidth */
#define WLC_20_MHZ	20	/**< 20Mhz channel bandwidth */
#define WLC_40_MHZ	40	/**< 40Mhz channel bandwidth */
#define WLC_80_MHZ	80	/**< 80Mhz channel bandwidth */
#define WLC_160_MHZ	160	/**< 160Mhz channel bandwidth */
#define WLC_240_MHZ	240	/**< 240Mhz channel bandwidth */
#define WLC_320_MHZ	320	/**< 320Mhz channel bandwidth */

extern uint16 bcmwifi_monitor_create(monitor_info_t**);
extern void bcmwifi_set_corerev_major(monitor_info_t* info, int8 corerev);
extern void bcmwifi_set_corerev_minor(monitor_info_t* info, int8 corerev);
extern void bcmwifi_monitor_delete(monitor_info_t* info);
extern uint16 bcmwifi_monitor(monitor_info_t* info,
	monitor_pkt_info_t* pkt_info, void *pdata, uint16 len, void* pout,
	uint16* offset, uint16 pad_req, void *wrxh_in, void *wrxh_last);
extern uint16 wl_rxsts_to_rtap(monitor_pkt_rxsts_t* pkt_rxsts, void *pdata,
                               uint16 len, void* pout, uint16 pad_req);

#endif /* _BCMWIFI_MONITOR_H_ */
