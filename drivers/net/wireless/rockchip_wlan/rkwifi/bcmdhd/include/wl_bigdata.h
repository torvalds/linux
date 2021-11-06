/*
 * Bigdata logging and report. None EWP and Hang event.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */
#ifndef __WL_BIGDATA_H_
#define __WL_BIGDATA_H_

#include <802.11.h>
#include <bcmevent.h>
#include <bcmwifi_channels.h>

#define MAX_STA_INFO_AP_CNT	20

#define DOT11_11B_MAX_RATE 11
#define DOT11_2GHZ_MAX_CH_NUM 14
#define DOT11_HT_MCS_RATE_MASK 0xFF

enum {
	BIGDATA_DOT11_11B_MODE = 0,
	BIGDATA_DOT11_11G_MODE = 1,
	BIGDATA_DOT11_11N_MODE = 2,
	BIGDATA_DOT11_11A_MODE = 3,
	BIGDATA_DOT11_11AC_MODE = 4,
	BIGDATA_DOT11_11AX_MODE = 5,
	BIGDATA_DOT11_MODE_MAX
};

typedef struct wl_ap_sta_data
{
	struct ether_addr mac;
	uint32 mode_80211;
	uint32 nss;
	chanspec_t chanspec;
	int16 rssi;
	uint32 rate;
	uint8 channel;
	uint32 mimo;
	uint32 disconnected;
	uint32 is_empty;
	uint32 reason_code;
} wl_ap_sta_data_t;

typedef struct ap_sta_wq_data
{
	wl_event_msg_t e;
	void *dhdp;
	void *bcm_cfg;
	void *ndev;
} ap_sta_wq_data_t;

typedef struct wl_ap_sta_info
{
	wl_ap_sta_data_t *ap_sta_data;
	uint32 sta_list_cnt;
	struct mutex wq_data_sync;
} wl_ap_sta_info_t;

int wl_attach_ap_stainfo(void *bcm_cfg);
int wl_detach_ap_stainfo(void *bcm_cfg);
int wl_ap_stainfo_init(void *bcm_cfg);
void wl_gather_ap_stadata(void *handle, void *event_info, u8 event);
int wl_get_ap_stadata(void *bcm_cfg, struct ether_addr *sta_mac, void **data);
#endif /* __WL_BIGDATA_H_ */
