/*
 * Wi-Fi Multimedia Admission Control (WMM-AC)
 * Copyright(c) 2014, Intel Mobile Communication GmbH.
 * Copyright(c) 2014, Intel Corporation. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WMM_AC_H
#define WMM_AC_H

#include "common/ieee802_11_defs.h"
#include "drivers/driver.h"

struct wpa_supplicant;

#define WMM_AC_ACCESS_POLICY_EDCA 1
#define WMM_AC_FIXED_MSDU_SIZE BIT(15)

#define WMM_AC_MAX_TID 7
#define WMM_AC_MAX_USER_PRIORITY 7
#define WMM_AC_MIN_SBA_UNITY 0x2000
#define WMM_AC_MAX_NOMINAL_MSDU 32767

/**
 * struct wmm_ac_assoc_data - WMM Admission Control Association Data
 *
 * This struct will store any relevant WMM association data needed by WMM AC.
 * In case there is a valid WMM association, an instance of this struct will be
 * created. In case there is no instance of this struct, the station is not
 * associated to a valid WMM BSS and hence, WMM AC will not be used.
 */
struct wmm_ac_assoc_data {
	struct {
		/*
		 * acm - Admission Control Mandatory
		 * In case an access category is ACM, the traffic will have
		 * to be admitted by WMM-AC's admission mechanism before use.
		 */
		unsigned int acm:1;

		/*
		 * uapsd_queues - Unscheduled Automatic Power Save Delivery
		 *		  queues.
		 * Indicates whether ACs are configured for U-APSD (or legacy
		 * PS). Storing this value is necessary in order to set the
		 * Power Save Bit (PSB) in ADDTS request Action frames (if not
		 * given).
		 */
		unsigned int uapsd:1;
	} ac_params[WMM_AC_NUM];
};

/**
 * wmm_ac_dir - WMM Admission Control Direction
 */
enum wmm_ac_dir {
	WMM_AC_DIR_UPLINK = 0,
	WMM_AC_DIR_DOWNLINK = 1,
	WMM_AC_DIR_BIDIRECTIONAL = 3
};

/**
 * ts_dir_idx - indices of internally saved tspecs
 *
 * we can have multiple tspecs (downlink + uplink) per ac.
 * save them in array, and use the enum to directly access
 * the respective tspec slot (according to the direction).
 */
enum ts_dir_idx {
	TS_DIR_IDX_UPLINK,
	TS_DIR_IDX_DOWNLINK,
	TS_DIR_IDX_BIDI,

	TS_DIR_IDX_COUNT
};
#define TS_DIR_IDX_ALL (BIT(TS_DIR_IDX_COUNT) - 1)

/**
 * struct wmm_ac_addts_request - ADDTS Request Information
 *
 * The last sent ADDTS request(s) will be saved as element(s) of this struct in
 * order to be compared with the received ADDTS response in ADDTS response
 * action frame handling and should be stored until that point.
 * In case a new traffic stream will be created/replaced/updated, only its
 * relevant traffic stream information will be stored as a wmm_ac_ts struct.
 */
struct wmm_ac_addts_request {
	/*
	 * dialog token - Used to link the received ADDTS response with this
	 * saved ADDTS request when ADDTS response is being handled
	 */
	u8 dialog_token;

	/*
	 * address - The alleged traffic stream's receiver/transmitter address
	 * Address and TID are used to identify the TS (TID is contained in
	 * TSPEC)
	 */
	u8 address[ETH_ALEN];

	/*
	 * tspec - Traffic Stream Specification, will be used to compare the
	 * sent TSPEC in ADDTS request to the received TSPEC in ADDTS response
	 * and act accordingly in ADDTS response handling
	 */
	struct wmm_tspec_element tspec;
};


/**
 * struct wmm_ac_ts_setup_params - TS setup parameters
 *
 * This struct holds parameters which should be provided
 * to wmm_ac_ts_setup in order to setup a traffic stream
 */
struct wmm_ac_ts_setup_params {
	/*
	 * tsid - Traffic ID
	 * TID and address are used to identify the TS
	 */
	int tsid;

	/*
	 * direction - Traffic Stream's direction
	 */
	enum wmm_ac_dir direction;

	/*
	 * user_priority - Traffic Stream's user priority
	 */
	int user_priority;

	/*
	 * nominal_msdu_size - Nominal MAC service data unit size
	 */
	int nominal_msdu_size;

	/*
	 * fixed_nominal_msdu - Whether the size is fixed
	 * 0 = Nominal MSDU size is not fixed
	 * 1 = Nominal MSDU size is fixed
	 */
	int fixed_nominal_msdu;

	/*
	 * surplus_bandwidth_allowance - Specifies excess time allocation
	 */
	int mean_data_rate;

	/*
	 * minimum_phy_rate - Specifies the minimum supported PHY rate in bps
	 */
	int minimum_phy_rate;

	/*
	 * surplus_bandwidth_allowance - Specifies excess time allocation
	 */
	int surplus_bandwidth_allowance;
};

void wmm_ac_notify_assoc(struct wpa_supplicant *wpa_s, const u8 *ies,
			 size_t ies_len, const struct wmm_params *wmm_params);
void wmm_ac_notify_disassoc(struct wpa_supplicant *wpa_s);
int wpas_wmm_ac_addts(struct wpa_supplicant *wpa_s,
		      struct wmm_ac_ts_setup_params *params);
int wpas_wmm_ac_delts(struct wpa_supplicant *wpa_s, u8 tsid);
void wmm_ac_rx_action(struct wpa_supplicant *wpa_s, const u8 *da,
			const u8 *sa, const u8 *data, size_t len);
int wpas_wmm_ac_status(struct wpa_supplicant *wpa_s, char *buf, size_t buflen);
void wmm_ac_save_tspecs(struct wpa_supplicant *wpa_s);
void wmm_ac_clear_saved_tspecs(struct wpa_supplicant *wpa_s);
int wmm_ac_restore_tspecs(struct wpa_supplicant *wpa_s);

#endif /* WMM_AC_H */
