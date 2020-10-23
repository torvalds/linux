/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __RTW_RADIOTAP_H_
#define __RTW_RADIOTAP_H_

struct mon_reg_backup {
	/* flags */
	u8	known_rcr:1;
	u8	known_drvinfo:1;
	u8	known_rxfilter:1;
	u8	known_misc0:1;
	/* data */
	u8	drvinfo;
	u16	rxfilter0;
	u16	rxfilter1;
	u16	rxfilter2;
	u32	rcr;
	u32	misc0;
};

struct moinfo {
	union {
		struct  {
			u32	sgi:1;
			u32	ldpc:1;
			u32	stbc:2;
			u32	not_sounding:1;
			u32	ofdm_bw:2;
			u32	vht_group_id:2;
			u32	vht_nsts_aid:12;
			u32	vht_txop_not_allow:1;
			u32	vht_nsym_dis:1;
			u32	vht_ldpc_extra:1;
			u32	vht_su_mcs:12;
			u32	vht_beamformed:1;
		}snif_info;

		struct  {
			u32	A;
			u32	B;
			u32	C;
		}plcp_info;
	}u;
};

sint rtw_fill_radiotap_hdr(_adapter *padapter, struct rx_pkt_attrib *a, u8 *buf);

void rx_query_moinfo(struct rx_pkt_attrib *a, u8 *desc);

#endif /* __RTW_RADIOTAP_H_ */

