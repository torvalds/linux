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

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


#ifndef OID_802_11_CAPABILITY
	#define OID_802_11_CAPABILITY                   0x0d010122
#endif

#ifndef OID_802_11_PMKID
	#define OID_802_11_PMKID                        0x0d010123
#endif


// For DDK-defined OIDs
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

// For Realtek-defined OIDs
#define OID_MP_SEG1		0xFF871100
#define OID_MP_SEG2		0xFF818000

#define OID_MP_SEG3		0xFF818700
#define OID_MP_SEG4		0xFF011100

#define DEBUG_OID(dbg, str)     		\
       if((!dbg))				    			\
      	{					    			\
	   RT_TRACE(_module_rtl871x_ioctl_c_,_drv_info_,("%s(%d): %s", __FUNCTION__, __LINE__, str));	\
      	}			


enum oid_type
{
	QUERY_OID,
	SET_OID
};

struct oid_funs_node {
	unsigned int oid_start; //the starting number for OID
	unsigned int oid_end; //the ending number for OID
	struct oid_obj_priv *node_array; 
	unsigned int array_sz; //the size of node_array
	int query_counter; //count the number of query hits for this segment  
	int set_counter; //count the number of set hits for this segment  
};

struct oid_par_priv
{
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
	unsigned char	dbg; // 0: without OID debug message  1: with OID debug message 
	NDIS_STATUS (*oidfuns)(struct oid_par_priv *poid_par_priv);	
};

#if (defined(CONFIG_MP_INCLUDED) && defined(_RTW_MP_IOCTL_C_)) || \
	(defined(PLATFORM_WINDOWS) && defined(_RTW_IOCTL_RTL_C_))
static NDIS_STATUS oid_null_function(struct oid_par_priv* poid_par_priv)
{
	_func_enter_;
	_func_exit_;
	return NDIS_STATUS_SUCCESS;
}
#endif

#ifdef PLATFORM_WINDOWS

int TranslateNdisPsToRtPs(IN NDIS_802_11_POWER_MODE	ndisPsMode);

//OID Handler for Segment 1
NDIS_STATUS oid_gen_supported_list_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_hardware_status_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_media_supported_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_media_in_use_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_maximum_lookahead_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_maximum_frame_size_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_link_speed_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_transmit_buffer_space_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_receive_buffer_space_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_transmit_block_size_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_receive_block_size_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_vendor_id_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_vendor_description_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_current_packet_filter_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_current_lookahead_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_driver_version_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_maximum_total_size_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_protocol_options_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_mac_options_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_media_connect_status_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_maximum_send_packets_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_vendor_driver_version_hdl(struct oid_par_priv* poid_par_priv);


//OID Handler for Segment 2
NDIS_STATUS oid_gen_physical_medium_hdl(struct oid_par_priv* poid_par_priv);

//OID Handler for Segment 3
NDIS_STATUS oid_gen_xmit_ok_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_rcv_ok_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_xmit_error_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_rcv_error_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_gen_rcv_no_buffer_hdl(struct oid_par_priv* poid_par_priv);


//OID Handler for Segment 4
NDIS_STATUS oid_802_3_permanent_address_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_current_address_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_multicast_list_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_maximum_list_size_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_mac_options_hdl(struct oid_par_priv* poid_par_priv);



//OID Handler for Segment 5
NDIS_STATUS oid_802_3_rcv_error_alignment_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_xmit_one_collision_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_xmit_more_collisions_hdl(struct oid_par_priv* poid_par_priv);


//OID Handler for Segment 6
NDIS_STATUS oid_802_3_xmit_deferred_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_xmit_max_collisions_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_rcv_overrun_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_xmit_underrun_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_xmit_heartbeat_failure_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_xmit_times_crs_lost_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_3_xmit_late_collisions_hdl(struct oid_par_priv* poid_par_priv);



//OID Handler for Segment 7
NDIS_STATUS oid_pnp_capabilities_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_pnp_set_power_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_pnp_query_power_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_pnp_add_wake_up_pattern_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_pnp_remove_wake_up_pattern_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_pnp_wake_up_pattern_list_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_pnp_enable_wake_up_hdl(struct oid_par_priv* poid_par_priv);



//OID Handler for Segment 8
NDIS_STATUS oid_802_11_bssid_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_ssid_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_infrastructure_mode_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_add_wep_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_remove_wep_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_disassociate_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_authentication_mode_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_privacy_filter_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_bssid_list_scan_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_encryption_status_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_reload_defaults_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_add_key_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_remove_key_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_association_information_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_test_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_media_stream_mode_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_capability_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_pmkid_hdl(struct oid_par_priv* poid_par_priv);





//OID Handler for Segment 9
NDIS_STATUS oid_802_11_network_types_supported_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_network_type_in_use_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_tx_power_level_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_rssi_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_rssi_trigger_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_fragmentation_threshold_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_rts_threshold_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_number_of_antennas_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_rx_antenna_selected_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_tx_antenna_selected_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_supported_rates_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_desired_rates_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_configuration_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_power_mode_hdl(struct oid_par_priv* poid_par_priv);
NDIS_STATUS oid_802_11_bssid_list_hdl(struct oid_par_priv* poid_par_priv);


//OID Handler for Segment 10
NDIS_STATUS oid_802_11_statistics_hdl(struct oid_par_priv* poid_par_priv);


//OID Handler for Segment ED 
NDIS_STATUS oid_rt_mh_vender_id_hdl(struct oid_par_priv* poid_par_priv);

void Set_802_3_MULTICAST_LIST(ADAPTER *pAdapter, UCHAR *MCListbuf, ULONG MCListlen, BOOLEAN bAcceptAllMulticast);

#endif// end of PLATFORM_WINDOWS

#if defined(PLATFORM_LINUX) && defined(CONFIG_WIRELESS_EXT)
extern struct iw_handler_def  rtw_handlers_def;
#endif

extern	NDIS_STATUS drv_query_info(
	IN	_nic_hdl		MiniportAdapterContext,
	IN	NDIS_OID		Oid,
	IN	void *			InformationBuffer,
	IN	u32			InformationBufferLength,
	OUT	u32*			BytesWritten,
	OUT	u32*			BytesNeeded
	);

extern	NDIS_STATUS 	drv_set_info(
	IN	_nic_hdl		MiniportAdapterContext,
	IN	NDIS_OID		Oid,
	IN	void *			InformationBuffer,
	IN	u32			InformationBufferLength,
	OUT	u32*			BytesRead,
	OUT	u32*			BytesNeeded
	);

#endif // #ifndef __INC_CEINFO_

