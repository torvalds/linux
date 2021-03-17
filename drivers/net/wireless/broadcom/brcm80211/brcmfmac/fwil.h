// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2012 Broadcom Corporation
 */

#ifndef _fwil_h_
#define _fwil_h_

/*******************************************************************************
 * Dongle command codes that are interpreted by firmware
 ******************************************************************************/
#define BRCMF_C_GET_VERSION			1
#define BRCMF_C_UP				2
#define BRCMF_C_DOWN				3
#define BRCMF_C_SET_PROMISC			10
#define BRCMF_C_GET_RATE			12
#define BRCMF_C_GET_INFRA			19
#define BRCMF_C_SET_INFRA			20
#define BRCMF_C_GET_AUTH			21
#define BRCMF_C_SET_AUTH			22
#define BRCMF_C_GET_BSSID			23
#define BRCMF_C_GET_SSID			25
#define BRCMF_C_SET_SSID			26
#define BRCMF_C_TERMINATED			28
#define BRCMF_C_GET_CHANNEL			29
#define BRCMF_C_SET_CHANNEL			30
#define BRCMF_C_GET_SRL				31
#define BRCMF_C_SET_SRL				32
#define BRCMF_C_GET_LRL				33
#define BRCMF_C_SET_LRL				34
#define BRCMF_C_GET_RADIO			37
#define BRCMF_C_SET_RADIO			38
#define BRCMF_C_GET_PHYTYPE			39
#define BRCMF_C_SET_KEY				45
#define BRCMF_C_GET_REGULATORY			46
#define BRCMF_C_SET_REGULATORY			47
#define BRCMF_C_SET_PASSIVE_SCAN		49
#define BRCMF_C_SCAN				50
#define BRCMF_C_SCAN_RESULTS			51
#define BRCMF_C_DISASSOC			52
#define BRCMF_C_REASSOC				53
#define BRCMF_C_SET_ROAM_TRIGGER		55
#define BRCMF_C_SET_ROAM_DELTA			57
#define BRCMF_C_GET_BCNPRD			75
#define BRCMF_C_SET_BCNPRD			76
#define BRCMF_C_GET_DTIMPRD			77
#define BRCMF_C_SET_DTIMPRD			78
#define BRCMF_C_SET_COUNTRY			84
#define BRCMF_C_GET_PM				85
#define BRCMF_C_SET_PM				86
#define BRCMF_C_GET_REVINFO			98
#define BRCMF_C_GET_MONITOR			107
#define BRCMF_C_SET_MONITOR			108
#define BRCMF_C_GET_CURR_RATESET		114
#define BRCMF_C_GET_AP				117
#define BRCMF_C_SET_AP				118
#define BRCMF_C_SET_SCB_AUTHORIZE		121
#define BRCMF_C_SET_SCB_DEAUTHORIZE		122
#define BRCMF_C_GET_RSSI			127
#define BRCMF_C_GET_WSEC			133
#define BRCMF_C_SET_WSEC			134
#define BRCMF_C_GET_PHY_NOISE			135
#define BRCMF_C_GET_BSS_INFO			136
#define BRCMF_C_GET_GET_PKTCNTS			137
#define BRCMF_C_GET_BANDLIST			140
#define BRCMF_C_SET_SCB_TIMEOUT			158
#define BRCMF_C_GET_ASSOCLIST			159
#define BRCMF_C_GET_PHYLIST			180
#define BRCMF_C_SET_SCAN_CHANNEL_TIME		185
#define BRCMF_C_SET_SCAN_UNASSOC_TIME		187
#define BRCMF_C_SCB_DEAUTHENTICATE_FOR_REASON	201
#define BRCMF_C_SET_ASSOC_PREFER		205
#define BRCMF_C_GET_VALID_CHANNELS		217
#define BRCMF_C_SET_FAKEFRAG			219
#define BRCMF_C_GET_KEY_PRIMARY			235
#define BRCMF_C_SET_KEY_PRIMARY			236
#define BRCMF_C_SET_SCAN_PASSIVE_TIME		258
#define BRCMF_C_GET_VAR				262
#define BRCMF_C_SET_VAR				263
#define BRCMF_C_SET_WSEC_PMK			268

s32 brcmf_fil_cmd_data_set(struct brcmf_if *ifp, u32 cmd, void *data, u32 len);
s32 brcmf_fil_cmd_data_get(struct brcmf_if *ifp, u32 cmd, void *data, u32 len);
s32 brcmf_fil_cmd_int_set(struct brcmf_if *ifp, u32 cmd, u32 data);
s32 brcmf_fil_cmd_int_get(struct brcmf_if *ifp, u32 cmd, u32 *data);

s32 brcmf_fil_iovar_data_set(struct brcmf_if *ifp, char *name, const void *data,
			     u32 len);
s32 brcmf_fil_iovar_data_get(struct brcmf_if *ifp, char *name, void *data,
			     u32 len);
s32 brcmf_fil_iovar_int_set(struct brcmf_if *ifp, char *name, u32 data);
s32 brcmf_fil_iovar_int_get(struct brcmf_if *ifp, char *name, u32 *data);

s32 brcmf_fil_bsscfg_data_set(struct brcmf_if *ifp, char *name, void *data,
			      u32 len);
s32 brcmf_fil_bsscfg_data_get(struct brcmf_if *ifp, char *name, void *data,
			      u32 len);
s32 brcmf_fil_bsscfg_int_set(struct brcmf_if *ifp, char *name, u32 data);
s32 brcmf_fil_bsscfg_int_get(struct brcmf_if *ifp, char *name, u32 *data);

#endif /* _fwil_h_ */
