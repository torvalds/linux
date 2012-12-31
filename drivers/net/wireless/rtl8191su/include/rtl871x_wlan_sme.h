/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/ 
#ifndef _RTL871X_WLAN_SME_H_
#define _RTL871X_WLAN_SME_H_


#define MSR_APMODE		0x0C
#define MSR_STAMODE	0x08
#define MSR_ADHOCMODE	0x04
#define MSR_NOLINKMODE	0x00

#define		_1M_RATE_	0
#define		_2M_RATE_	1
#define		_5M_RATE_	2
#define		_11M_RATE_	3
#define		_6M_RATE_	4
#define		_9M_RATE_	5
#define		_12M_RATE_	6
#define		_18M_RATE_	7
#define		_24M_RATE_	8
#define		_36M_RATE_	9
#define		_48M_RATE_	10
#define		_54M_RATE_	11

void get_bssrate_set(_adapter *padapter, int bssrate_ie, unsigned char *pbssrate, int *bssrate_len);

void write_cam(_adapter *padapter, unsigned char entry, unsigned short ctrl, unsigned char *mac, unsigned char *key);
void invalidate_cam_all(_adapter *padapter);

int check_basic_rate(struct mlme_ext_priv *pmlmeext, unsigned char *pRate, int pLen);
void get_matched_rate(struct mlme_ext_priv *pmlmeext, unsigned char *pRate, int *pLen, int which);
void update_support_rate(struct sta_info *pstat, unsigned char* buf, int len);

void add_rate_id(struct mlme_ext_priv *pmlmeext, struct sta_info *pstat);

#endif

