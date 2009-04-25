/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: iwctl.h
 *
 * Purpose:
 *
 * Author: Lyndon Chen
 *
 * Date: May 21, 2004
 *
 */


#ifndef __IWCTL_H__
#define __IWCTL_H__

#if !defined(__DEVICE_H__)
#include "device.h"
#endif


/*---------------------  Export Definitions -------------------------*/


/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/


#ifdef __cplusplus
extern "C" {                            /* Assume C declarations for C++ */
#endif /* __cplusplus */


#if WIRELESS_EXT < 18


#define SIOCSIWMLME	        0x8B16
#define SIOCSIWGENIE	    0x8B30

// WPA : Authentication mode parameters
#define SIOCSIWAUTH	        0x8B32
#define SIOCGIWAUTH	        0x8B33

// WPA : Extended version of encoding configuration
#define SIOCSIWENCODEEXT    0x8B34
#define SIOCGIWENCODEEXT    0x8B35

#define IW_AUTH_WPA_VERSION		0
#define IW_AUTH_CIPHER_PAIRWISE		1
#define IW_AUTH_CIPHER_GROUP		2
#define IW_AUTH_KEY_MGMT		3
#define IW_AUTH_TKIP_COUNTERMEASURES	4
#define IW_AUTH_DROP_UNENCRYPTED	5
#define IW_AUTH_80211_AUTH_ALG		6
#define IW_AUTH_WPA_ENABLED		7
#define IW_AUTH_RX_UNENCRYPTED_EAPOL	8
#define IW_AUTH_ROAMING_CONTROL		9
#define IW_AUTH_PRIVACY_INVOKED		10

#define IW_AUTH_WPA_VERSION_DISABLED	0x00000001
#define IW_AUTH_WPA_VERSION_WPA		0x00000002
#define IW_AUTH_WPA_VERSION_WPA2	0x00000004

#define IW_AUTH_CIPHER_NONE	    0x00000001
#define IW_AUTH_CIPHER_WEP40	0x00000002
#define IW_AUTH_CIPHER_TKIP	    0x00000004
#define IW_AUTH_CIPHER_CCMP	    0x00000008
#define IW_AUTH_CIPHER_WEP104	0x00000010

#define IW_AUTH_KEY_MGMT_802_1X	1
#define IW_AUTH_KEY_MGMT_PSK	2

#define IW_AUTH_ALG_OPEN_SYSTEM	0x00000001
#define IW_AUTH_ALG_SHARED_KEY	0x00000002
#define IW_AUTH_ALG_LEAP	0x00000004

#define IW_AUTH_ROAMING_ENABLE	0
#define IW_AUTH_ROAMING_DISABLE	1

#define IW_ENCODE_SEQ_MAX_SIZE	8

#define IW_ENCODE_ALG_NONE	0
#define IW_ENCODE_ALG_WEP	1
#define IW_ENCODE_ALG_TKIP	2
#define IW_ENCODE_ALG_CCMP	3


struct	iw_encode_ext
{
	__u32		ext_flags; // IW_ENCODE_EXT_*
	__u8		tx_seq[IW_ENCODE_SEQ_MAX_SIZE]; // LSB first
	__u8		rx_seq[IW_ENCODE_SEQ_MAX_SIZE]; // LSB first
	struct sockaddr	addr; // ff:ff:ff:ff:ff:ff for broadcast/multicast
			              // (group) keys or unicast address for
			              // individual keys
	__u16		alg; // IW_ENCODE_ALG_*
	__u16		key_len;
	__u8		key[0];
};


struct	iw_mlme
{
	__u16		cmd; /* IW_MLME_* */
	__u16		reason_code;
	struct sockaddr	addr;
};

#endif // WIRELESS_EXT < 18



#ifdef WIRELESS_EXT

struct iw_statistics *iwctl_get_wireless_stats (struct net_device *dev);


int iwctl_siwap(struct net_device *dev,
             struct iw_request_info *info,
			 struct sockaddr *wrq,
             char *extra);

int iwctl_giwrange(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra);


int iwctl_giwmode(struct net_device *dev,
             struct iw_request_info *info,
             __u32 *wmode,
             char *extra);

int iwctl_siwmode(struct net_device *dev,
             struct iw_request_info *info,
             __u32 *wmode,
             char *extra);

int iwctl_giwfreq(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_freq *wrq,
             char *extra);

int iwctl_siwfreq(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_freq *wrq,
             char *extra);

int iwctl_giwname(struct net_device *dev,
			 struct iw_request_info *info,
			 char *wrq,
			 char *extra);

int iwctl_giwnwid(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
                   char *extra) ;

int iwctl_giwsens(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *wrq,
			 char *extra);

int iwctl_giwap(struct net_device *dev,
             struct iw_request_info *info,
			 struct sockaddr *wrq,
             char *extra);

int iwctl_giwaplist(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra);

int iwctl_siwessid(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra);

int iwctl_giwessid(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra);

int iwctl_siwrate(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);

int iwctl_giwrate(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_param *wrq,
             char *extra);

int iwctl_siwrts(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);


int iwctl_giwrts(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);

int iwctl_siwfrag(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);

int iwctl_giwfrag(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);

int iwctl_siwretry(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);

int iwctl_giwretry(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);

int iwctl_siwencode(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra);

int iwctl_giwencode(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra);

int iwctl_siwpower(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);

int iwctl_giwpower(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);

int iwctl_giwscan(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_point *wrq,
             char *extra);

int iwctl_siwscan(struct net_device *dev,
             struct iw_request_info *info,
			 struct iw_param *wrq,
             char *extra);

//2008-0409-07, <Add> by Einsn Liu
#ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT
int iwctl_siwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *wrq,
			  char *extra);

int iwctl_giwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *wrq,
			  char *extra);

int iwctl_siwgenie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *wrq,
			  char *extra);

int iwctl_giwgenie(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *wrq,
			  char *extra);

int iwctl_siwencodeext(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra);

int iwctl_giwencodeext(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_point *wrq,
             char *extra);

int iwctl_siwmlme(struct net_device *dev,
			struct iw_request_info * info,
			struct iw_point *wrq,
			char *extra);
#endif // #ifdef WPA_SUPPLICANT_DRIVER_WEXT_SUPPORT


#endif

#if WIRELESS_EXT > 12
extern const struct iw_handler_def	iwctl_handler_def;
extern const struct iw_priv_args	iwctl_private_args;
#else
struct iw_request_info {};
#endif	//WIRELESS_EXT > 12

#ifdef __cplusplus
}                                       /* End of extern "C" { */
#endif /* __cplusplus */




#endif // __IWCTL_H__



