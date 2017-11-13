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
#ifndef _RTW_IOCTL_H_
#define _RTW_IOCTL_H_

#ifndef PLATFORM_WINDOWS
/*	00 - Success
*	11 - Error */
#define STATUS_SUCCESS				(0x00000000L)
#define STATUS_PENDING				(0x00000103L)

#define STATUS_UNSUCCESSFUL			(0xC0000001L)
#define STATUS_INSUFFICIENT_RESOURCES		(0xC000009AL)
#define STATUS_NOT_SUPPORTED			(0xC00000BBL)

#define NDIS_STATUS_SUCCESS			((NDIS_STATUS)STATUS_SUCCESS)
#define NDIS_STATUS_PENDING			((NDIS_STATUS)STATUS_PENDING)
#define NDIS_STATUS_NOT_RECOGNIZED		((NDIS_STATUS)0x00010001L)
#define NDIS_STATUS_NOT_COPIED			((NDIS_STATUS)0x00010002L)
#define NDIS_STATUS_NOT_ACCEPTED		((NDIS_STATUS)0x00010003L)
#define NDIS_STATUS_CALL_ACTIVE			((NDIS_STATUS)0x00010007L)

#define NDIS_STATUS_FAILURE			((NDIS_STATUS)STATUS_UNSUCCESSFUL)
#define NDIS_STATUS_RESOURCES			((NDIS_STATUS)STATUS_INSUFFICIENT_RESOURCES)
#define NDIS_STATUS_CLOSING			((NDIS_STATUS)0xC0010002L)
#define NDIS_STATUS_BAD_VERSION			((NDIS_STATUS)0xC0010004L)
#define NDIS_STATUS_BAD_CHARACTERISTICS		((NDIS_STATUS)0xC0010005L)
#define NDIS_STATUS_ADAPTER_NOT_FOUND		((NDIS_STATUS)0xC0010006L)
#define NDIS_STATUS_OPEN_FAILED			((NDIS_STATUS)0xC0010007L)
#define NDIS_STATUS_DEVICE_FAILED		((NDIS_STATUS)0xC0010008L)
#define NDIS_STATUS_MULTICAST_FULL		((NDIS_STATUS)0xC0010009L)
#define NDIS_STATUS_MULTICAST_EXISTS		((NDIS_STATUS)0xC001000AL)
#define NDIS_STATUS_MULTICAST_NOT_FOUND		((NDIS_STATUS)0xC001000BL)
#define NDIS_STATUS_REQUEST_ABORTED		((NDIS_STATUS)0xC001000CL)
#define NDIS_STATUS_RESET_IN_PROGRESS		((NDIS_STATUS)0xC001000DL)
#define NDIS_STATUS_CLOSING_INDICATING		((NDIS_STATUS)0xC001000EL)
#define NDIS_STATUS_NOT_SUPPORTED		((NDIS_STATUS)STATUS_NOT_SUPPORTED)
#define NDIS_STATUS_INVALID_PACKET		((NDIS_STATUS)0xC001000FL)
#define NDIS_STATUS_OPEN_LIST_FULL		((NDIS_STATUS)0xC0010010L)
#define NDIS_STATUS_ADAPTER_NOT_READY		((NDIS_STATUS)0xC0010011L)
#define NDIS_STATUS_ADAPTER_NOT_OPEN		((NDIS_STATUS)0xC0010012L)
#define NDIS_STATUS_NOT_INDICATING		((NDIS_STATUS)0xC0010013L)
#define NDIS_STATUS_INVALID_LENGTH		((NDIS_STATUS)0xC0010014L)
#define NDIS_STATUS_INVALID_DATA		((NDIS_STATUS)0xC0010015L)
#define NDIS_STATUS_BUFFER_TOO_SHORT		((NDIS_STATUS)0xC0010016L)
#define NDIS_STATUS_INVALID_OID			((NDIS_STATUS)0xC0010017L)
#define NDIS_STATUS_ADAPTER_REMOVED		((NDIS_STATUS)0xC0010018L)
#define NDIS_STATUS_UNSUPPORTED_MEDIA		((NDIS_STATUS)0xC0010019L)
#define NDIS_STATUS_GROUP_ADDRESS_IN_USE	((NDIS_STATUS)0xC001001AL)
#define NDIS_STATUS_FILE_NOT_FOUND		((NDIS_STATUS)0xC001001BL)
#define NDIS_STATUS_ERROR_READING_FILE		((NDIS_STATUS)0xC001001CL)
#define NDIS_STATUS_ALREADY_MAPPED		((NDIS_STATUS)0xC001001DL)
#define NDIS_STATUS_RESOURCE_CONFLICT		((NDIS_STATUS)0xC001001EL)
#define NDIS_STATUS_NO_CABLE			((NDIS_STATUS)0xC001001FL)

#define NDIS_STATUS_INVALID_SAP			((NDIS_STATUS)0xC0010020L)
#define NDIS_STATUS_SAP_IN_USE			((NDIS_STATUS)0xC0010021L)
#define NDIS_STATUS_INVALID_ADDRESS		((NDIS_STATUS)0xC0010022L)
#define NDIS_STATUS_VC_NOT_ACTIVATED		((NDIS_STATUS)0xC0010023L)
#define NDIS_STATUS_DEST_OUT_OF_ORDER		((NDIS_STATUS)0xC0010024L)  /* cause 27 */
#define NDIS_STATUS_VC_NOT_AVAILABLE		((NDIS_STATUS)0xC0010025L)  /* cause 35, 45 */
#define NDIS_STATUS_CELLRATE_NOT_AVAILABLE	((NDIS_STATUS)0xC0010026L)  /* cause 37 */
#define NDIS_STATUS_INCOMPATABLE_QOS		((NDIS_STATUS)0xC0010027L)  /* cause 49 */
#define NDIS_STATUS_AAL_PARAMS_UNSUPPORTED	((NDIS_STATUS)0xC0010028L)  /* cause 93 */
#define NDIS_STATUS_NO_ROUTE_TO_DESTINATION	((NDIS_STATUS)0xC0010029L)  /* cause 3 */
#endif /* #ifndef PLATFORM_WINDOWS */


#ifndef OID_802_11_CAPABILITY
	#define OID_802_11_CAPABILITY                   0x0d010122
#endif

#ifndef OID_802_11_PMKID
	#define OID_802_11_PMKID                        0x0d010123
#endif


/* For DDK-defined OIDs */
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

#define SZ_OID_NDIS_SEG1		  23
#define SZ_OID_NDIS_SEG2		    3
#define SZ_OID_NDIS_SEG3		    6
#define SZ_OID_NDIS_SEG4		    6
#define SZ_OID_NDIS_SEG5		    4
#define SZ_OID_NDIS_SEG6		    8
#define SZ_OID_NDIS_SEG7		    7
#define SZ_OID_NDIS_SEG8		  36
#define SZ_OID_NDIS_SEG9		  24
#define SZ_OID_NDIS_SEG10		  19

/* For Realtek-defined OIDs */
#define OID_MP_SEG1		0xFF871100
#define OID_MP_SEG2		0xFF818000

#define OID_MP_SEG3		0xFF818700
#define OID_MP_SEG4		0xFF011100

enum oid_type {
	QUERY_OID,
	SET_OID
};

struct oid_funs_node {
	unsigned int oid_start; /* the starting number for OID */
	unsigned int oid_end; /* the ending number for OID */
	struct oid_obj_priv *node_array;
	unsigned int array_sz; /* the size of node_array */
	int query_counter; /* count the number of query hits for this segment  */
	int set_counter; /* count the number of set hits for this segment  */
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
	unsigned char	dbg; /* 0: without OID debug message  1: with OID debug message */
	NDIS_STATUS(*oidfuns)(struct oid_par_priv *poid_par_priv);
};

#if (defined(CONFIG_MP_INCLUDED) && defined(_RTW_MP_IOCTL_C_)) || \
	(defined(PLATFORM_WINDOWS) && defined(_RTW_IOCTL_RTL_C_))
static NDIS_STATUS oid_null_function(struct oid_par_priv *poid_par_priv)
{
	return NDIS_STATUS_SUCCESS;
}
#endif

#ifdef PLATFORM_WINDOWS

int TranslateNdisPsToRtPs(IN NDIS_802_11_POWER_MODE	ndisPsMode);

/* OID Handler for Segment 1 */
NDIS_STATUS oid_gen_supported_list_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_hardware_status_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_media_supported_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_media_in_use_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_maximum_lookahead_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_maximum_frame_size_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_link_speed_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_transmit_buffer_space_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_receive_buffer_space_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_transmit_block_size_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_receive_block_size_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_vendor_id_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_vendor_description_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_current_packet_filter_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_current_lookahead_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_driver_version_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_maximum_total_size_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_protocol_options_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_mac_options_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_media_connect_status_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_maximum_send_packets_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_vendor_driver_version_hdl(struct oid_par_priv *poid_par_priv);


/* OID Handler for Segment 2 */
NDIS_STATUS oid_gen_physical_medium_hdl(struct oid_par_priv *poid_par_priv);

/* OID Handler for Segment 3 */
NDIS_STATUS oid_gen_xmit_ok_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_rcv_ok_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_xmit_error_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_rcv_error_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_gen_rcv_no_buffer_hdl(struct oid_par_priv *poid_par_priv);


/* OID Handler for Segment 4 */
NDIS_STATUS oid_802_3_permanent_address_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_current_address_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_multicast_list_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_maximum_list_size_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_mac_options_hdl(struct oid_par_priv *poid_par_priv);



/* OID Handler for Segment 5 */
NDIS_STATUS oid_802_3_rcv_error_alignment_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_xmit_one_collision_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_xmit_more_collisions_hdl(struct oid_par_priv *poid_par_priv);


/* OID Handler for Segment 6 */
NDIS_STATUS oid_802_3_xmit_deferred_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_xmit_max_collisions_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_rcv_overrun_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_xmit_underrun_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_xmit_heartbeat_failure_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_xmit_times_crs_lost_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_3_xmit_late_collisions_hdl(struct oid_par_priv *poid_par_priv);



/* OID Handler for Segment 7 */
NDIS_STATUS oid_pnp_capabilities_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_pnp_set_power_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_pnp_query_power_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_pnp_add_wake_up_pattern_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_pnp_remove_wake_up_pattern_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_pnp_wake_up_pattern_list_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_pnp_enable_wake_up_hdl(struct oid_par_priv *poid_par_priv);



/* OID Handler for Segment 8 */
NDIS_STATUS oid_802_11_bssid_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_ssid_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_infrastructure_mode_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_add_wep_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_remove_wep_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_disassociate_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_authentication_mode_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_privacy_filter_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_bssid_list_scan_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_encryption_status_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_reload_defaults_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_add_key_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_remove_key_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_association_information_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_test_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_media_stream_mode_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_capability_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_pmkid_hdl(struct oid_par_priv *poid_par_priv);





/* OID Handler for Segment 9 */
NDIS_STATUS oid_802_11_network_types_supported_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_network_type_in_use_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_tx_power_level_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_rssi_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_rssi_trigger_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_fragmentation_threshold_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_rts_threshold_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_number_of_antennas_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_rx_antenna_selected_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_tx_antenna_selected_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_supported_rates_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_desired_rates_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_configuration_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_power_mode_hdl(struct oid_par_priv *poid_par_priv);
NDIS_STATUS oid_802_11_bssid_list_hdl(struct oid_par_priv *poid_par_priv);


/* OID Handler for Segment 10 */
NDIS_STATUS oid_802_11_statistics_hdl(struct oid_par_priv *poid_par_priv);


/* OID Handler for Segment ED */
NDIS_STATUS oid_rt_mh_vender_id_hdl(struct oid_par_priv *poid_par_priv);

void Set_802_3_MULTICAST_LIST(ADAPTER *pAdapter, UCHAR *MCListbuf, ULONG MCListlen, BOOLEAN bAcceptAllMulticast);

#endif/* end of PLATFORM_WINDOWS */

#if defined(PLATFORM_LINUX) && defined(CONFIG_WIRELESS_EXT)
extern struct iw_handler_def  rtw_handlers_def;
#endif

extern void rtw_request_wps_pbc_event(_adapter *padapter);

extern	NDIS_STATUS drv_query_info(
	IN	_nic_hdl		MiniportAdapterContext,
	IN	NDIS_OID		Oid,
	IN	void			*InformationBuffer,
	IN	u32			InformationBufferLength,
	OUT	u32			*BytesWritten,
	OUT	u32			*BytesNeeded
);

extern	NDIS_STATUS	drv_set_info(
	IN	_nic_hdl		MiniportAdapterContext,
	IN	NDIS_OID		Oid,
	IN	void			*InformationBuffer,
	IN	u32			InformationBufferLength,
	OUT	u32			*BytesRead,
	OUT	u32			*BytesNeeded
);

#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
extern int rtw_vendor_ie_get_raw_data(struct net_device *, u32, char *, u32);
extern int rtw_vendor_ie_get_data(struct net_device*, int , char*);
extern int rtw_vendor_ie_get(struct net_device *, struct iw_request_info *, union iwreq_data *, char *);
extern int rtw_vendor_ie_set(struct net_device*, struct iw_request_info*, union iwreq_data*, char*);
#endif

#endif /*  #ifndef __INC_CEINFO_ */
