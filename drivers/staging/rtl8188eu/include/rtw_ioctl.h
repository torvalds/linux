/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#ifndef _RTW_IOCTL_H_
#define _RTW_IOCTL_H_

#include <osdep_service.h>
#include <drv_types.h>


#ifndef OID_802_11_CAPABILITY
	#define OID_802_11_CAPABILITY	0x0d010122
#endif

#ifndef OID_802_11_PMKID
	#define OID_802_11_PMKID	0x0d010123
#endif


/*  For DDK-defined OIDs */
#define OID_NDIS_SEG1	0x00010100
#define OID_NDIS_SEG2	0x00010200
#define OID_NDIS_SEG3	0x00020100
#define OID_NDIS_SEG4	0x01010100
#define OID_NDIS_SEG5	0x01020100
#define OID_NDIS_SEG6	0x01020200
#define OID_NDIS_SEG7	0xFD010100
#define OID_NDIS_SEG8	0x0D010100
#define OID_NDIS_SEG9	0x0D010200
#define OID_NDIS_SEG10	0x0D020200

#define SZ_OID_NDIS_SEG1	23
#define SZ_OID_NDIS_SEG2	3
#define SZ_OID_NDIS_SEG3	6
#define SZ_OID_NDIS_SEG4	6
#define SZ_OID_NDIS_SEG5	4
#define SZ_OID_NDIS_SEG6	8
#define SZ_OID_NDIS_SEG7	7
#define SZ_OID_NDIS_SEG8	36
#define SZ_OID_NDIS_SEG9	24
#define SZ_OID_NDIS_SEG10	19

/*  For Realtek-defined OIDs */
#define OID_MP_SEG1		0xFF871100
#define OID_MP_SEG2		0xFF818000

#define OID_MP_SEG3		0xFF818700
#define OID_MP_SEG4		0xFF011100

#define DEBUG_OID(dbg, str)						\
	if ((!dbg)) {							\
		RT_TRACE(_module_rtl871x_ioctl_c_, _drv_info_,		\
			 ("%s(%d): %s", __func__, __line__, str));	\
	}

enum oid_type {
	QUERY_OID,
	SET_OID
};

struct oid_funs_node {
	unsigned int oid_start; /* the starting number for OID */
	unsigned int oid_end; /* the ending number for OID */
	struct oid_obj_priv *node_array;
	unsigned int array_sz; /* the size of node_array */
	int query_counter; /* count the number of query hits for this segment */
	int set_counter; /* count the number of set hits for this segment */
};

struct oid_par_priv {
	void		*adapter_context;
	NDIS_OID	oid;
	void		*information_buf;
	u32		information_buf_len;
	u32		*bytes_rw;
	u32		*bytes_needed;
	enum oid_type	type_of_oid;
	u32		dbg;
};

struct oid_obj_priv {
	unsigned char	dbg; /*  0: without OID debug message
			      *  1: with OID debug message */
	int (*oidfuns)(struct oid_par_priv *poid_par_priv);
};

#if defined(_RTW_MP_IOCTL_C_)
static int oid_null_function(struct oid_par_priv *poid_par_priv) {
	return NDIS_STATUS_SUCCESS;
}
#endif

extern struct iw_handler_def  rtw_handlers_def;

int drv_query_info(struct  net_device *miniportadaptercontext, NDIS_OID oid,
		   void *informationbuffer, u32 informationbufferlength,
		   u32 *byteswritten, u32 *bytesneeded);

int drv_set_info(struct  net_device *MiniportAdapterContext,
		 NDIS_OID oid, void *informationbuffer,
		 u32 informationbufferlength, u32 *bytesread,
		 u32 *bytesneeded);

#endif /*  #ifndef __INC_CEINFO_ */
