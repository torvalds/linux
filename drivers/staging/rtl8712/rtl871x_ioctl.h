/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __IOCTL_H
#define __IOCTL_H

#include "osdep_service.h"
#include "drv_types.h"

#ifndef OID_802_11_CAPABILITY
	#define OID_802_11_CAPABILITY                   0x0d010122
#endif

#ifndef OID_802_11_PMKID
	#define OID_802_11_PMKID                        0x0d010123
#endif

/* For DDK-defined OIDs*/
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

/* For Realtek-defined OIDs*/
#define OID_MP_SEG1	0xFF871100
#define OID_MP_SEG2	0xFF818000
#define OID_MP_SEG3	0xFF818700
#define OID_MP_SEG4	0xFF011100

enum oid_type {
	QUERY_OID,
	SET_OID
};

struct oid_funs_node {
	unsigned int oid_start; /*the starting number for OID*/
	unsigned int oid_end; /*the ending number for OID*/
	struct oid_obj_priv *node_array;
	unsigned int array_sz; /*the size of node_array*/
	int query_counter; /*count the number of query hits for this segment*/
	int set_counter; /*count the number of set hits for this segment*/
};

struct oid_par_priv {
	void	*adapter_context;
	uint oid;
	void *information_buf;
	unsigned long information_buf_len;
	unsigned long *bytes_rw;
	unsigned long *bytes_needed;
	enum oid_type	type_of_oid;
	unsigned int dbg;
};

struct oid_obj_priv {
	unsigned char	dbg; /* 0: without OID debug message
			      * 1: with OID debug message
			      */
	uint (*oidfuns)(struct oid_par_priv *poid_par_priv);
};

uint oid_null_function(struct oid_par_priv *poid_par_priv);

extern struct iw_handler_def  r871x_handlers_def;

uint drv_query_info(struct net_device *MiniportAdapterContext,
		    uint Oid,
		    void *InformationBuffer,
		    u32 InformationBufferLength,
		    u32 *BytesWritten,
		    u32 *BytesNeeded);

uint drv_set_info(struct net_device *MiniportAdapterContext,
		  uint Oid,
		  void *InformationBuffer,
		  u32 InformationBufferLength,
		  u32 *BytesRead,
		  u32 *BytesNeeded);

#endif
