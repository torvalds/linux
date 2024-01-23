// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/qrtr.h>
#include <linux/slab.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>

#include "ipa.h"

/* Request/response/indication QMI message ids used for IPA.  Receiving
 * end issues a response for requests; indications require no response.
 */
#define IPA_QMI_INDICATION_REGISTER	0x20	/* modem -> AP request */
#define IPA_QMI_INIT_DRIVER		0x21	/* AP -> modem request */
#define IPA_QMI_INIT_COMPLETE		0x22	/* AP -> modem indication */

/* The maximum size required for message types.  These sizes include
 * the message data, along with type (1 byte) and length (2 byte)
 * information for each field.  The qmi_send_*() interfaces require
 * the message size to be provided.
 */
#define IPA_QMI_INDICATION_REGISTER_REQ_SZ	20	/* -> server handle */
#define IPA_QMI_INDICATION_REGISTER_RSP_SZ	7	/* <- server handle */
#define IPA_QMI_INIT_DRIVER_REQ_SZ		162	/* client handle -> */
#define IPA_QMI_INIT_DRIVER_RSP_SZ		25	/* client handle <- */
#define IPA_QMI_INIT_COMPLETE_IND_SZ		7	/* <- server handle */

/* Maximum size of messages we expect the AP to receive (max of above) */
#define IPA_QMI_SERVER_MAX_RCV_SZ		8
#define IPA_QMI_CLIENT_MAX_RCV_SZ		25

/* Request message for the IPA_QMI_INDICATION_REGISTER request */
struct ipa_indication_register_req {
	u8 master_driver_init_complete_valid;
	u8 master_driver_init_complete;
	u8 data_usage_quota_reached_valid;
	u8 data_usage_quota_reached;
	u8 ipa_mhi_ready_ind_valid;
	u8 ipa_mhi_ready_ind;
	u8 endpoint_desc_ind_valid;
	u8 endpoint_desc_ind;
	u8 bw_change_ind_valid;
	u8 bw_change_ind;
};

/* The response to a IPA_QMI_INDICATION_REGISTER request consists only of
 * a standard QMI response.
 */
struct ipa_indication_register_rsp {
	struct qmi_response_type_v01 rsp;
};

/* The message for the IPA_QMI_INIT_COMPLETE_IND indication consists
 * only of a standard QMI response.
 */
struct ipa_init_complete_ind {
	struct qmi_response_type_v01 status;
};

/* The AP tells the modem its platform type.  We assume Android. */
enum ipa_platform_type {
	IPA_QMI_PLATFORM_TYPE_INVALID		= 0x0,	/* Invalid */
	IPA_QMI_PLATFORM_TYPE_TN		= 0x1,	/* Data card */
	IPA_QMI_PLATFORM_TYPE_LE		= 0x2,	/* Data router */
	IPA_QMI_PLATFORM_TYPE_MSM_ANDROID	= 0x3,	/* Android MSM */
	IPA_QMI_PLATFORM_TYPE_MSM_WINDOWS	= 0x4,	/* Windows MSM */
	IPA_QMI_PLATFORM_TYPE_MSM_QNX_V01	= 0x5,	/* QNX MSM */
};

/* This defines the start and end offset of a range of memory.  The start
 * value is a byte offset relative to the start of IPA shared memory.  The
 * end value is the last addressable unit *within* the range.  Typically
 * the end value is in units of bytes, however it can also be a maximum
 * array index value.
 */
struct ipa_mem_bounds {
	u32 start;
	u32 end;
};

/* This defines the location and size of an array.  The start value
 * is an offset relative to the start of IPA shared memory.  The
 * size of the array is implied by the number of entries (the entry
 * size is assumed to be known).
 */
struct ipa_mem_array {
	u32 start;
	u32 count;
};

/* This defines the location and size of a range of memory.  The
 * start is an offset relative to the start of IPA shared memory.
 * This differs from the ipa_mem_bounds structure in that the size
 * (in bytes) of the memory region is specified rather than the
 * offset of its last byte.
 */
struct ipa_mem_range {
	u32 start;
	u32 size;
};

/* The message for the IPA_QMI_INIT_DRIVER request contains information
 * from the AP that affects modem initialization.
 */
struct ipa_init_modem_driver_req {
	u8			platform_type_valid;
	u32			platform_type;	/* enum ipa_platform_type */

	/* Modem header table information.  This defines the IPA shared
	 * memory in which the modem may insert header table entries.
	 */
	u8			hdr_tbl_info_valid;
	struct ipa_mem_bounds	hdr_tbl_info;

	/* Routing table information.  These define the location and maximum
	 * *index* (not byte) for the modem portion of non-hashable IPv4 and
	 * IPv6 routing tables.  The start values are byte offsets relative
	 * to the start of IPA shared memory.
	 */
	u8			v4_route_tbl_info_valid;
	struct ipa_mem_bounds	v4_route_tbl_info;
	u8			v6_route_tbl_info_valid;
	struct ipa_mem_bounds	v6_route_tbl_info;

	/* Filter table information.  These define the location of the
	 * non-hashable IPv4 and IPv6 filter tables.  The start values are
	 * byte offsets relative to the start of IPA shared memory.
	 */
	u8			v4_filter_tbl_start_valid;
	u32			v4_filter_tbl_start;
	u8			v6_filter_tbl_start_valid;
	u32			v6_filter_tbl_start;

	/* Modem memory information.  This defines the location and
	 * size of memory available for the modem to use.
	 */
	u8			modem_mem_info_valid;
	struct ipa_mem_range	modem_mem_info;

	/* This defines the destination endpoint on the AP to which
	 * the modem driver can send control commands.  Must be less
	 * than ipa_endpoint_max().
	 */
	u8			ctrl_comm_dest_end_pt_valid;
	u32			ctrl_comm_dest_end_pt;

	/* This defines whether the modem should load the microcontroller
	 * or not.  It is unnecessary to reload it if the modem is being
	 * restarted.
	 *
	 * NOTE: this field is named "is_ssr_bootup" elsewhere.
	 */
	u8			skip_uc_load_valid;
	u8			skip_uc_load;

	/* Processing context memory information.  This defines the memory in
	 * which the modem may insert header processing context table entries.
	 */
	u8			hdr_proc_ctx_tbl_info_valid;
	struct ipa_mem_bounds	hdr_proc_ctx_tbl_info;

	/* Compression command memory information.  This defines the memory
	 * in which the modem may insert compression/decompression commands.
	 */
	u8			zip_tbl_info_valid;
	struct ipa_mem_bounds	zip_tbl_info;

	/* Routing table information.  These define the location and maximum
	 * *index* (not byte) for the modem portion of hashable IPv4 and IPv6
	 * routing tables (if supported by hardware).  The start values are
	 * byte offsets relative to the start of IPA shared memory.
	 */
	u8			v4_hash_route_tbl_info_valid;
	struct ipa_mem_bounds	v4_hash_route_tbl_info;
	u8			v6_hash_route_tbl_info_valid;
	struct ipa_mem_bounds	v6_hash_route_tbl_info;

	/* Filter table information.  These define the location and size
	 * of hashable IPv4 and IPv6 filter tables (if supported by hardware).
	 * The start values are byte offsets relative to the start of IPA
	 * shared memory.
	 */
	u8			v4_hash_filter_tbl_start_valid;
	u32			v4_hash_filter_tbl_start;
	u8			v6_hash_filter_tbl_start_valid;
	u32			v6_hash_filter_tbl_start;

	/* Statistics information.  These define the locations of the
	 * first and last statistics sub-regions.  (IPA v4.0 and above)
	 */
	u8			hw_stats_quota_base_addr_valid;
	u32			hw_stats_quota_base_addr;
	u8			hw_stats_quota_size_valid;
	u32			hw_stats_quota_size;
	u8			hw_stats_drop_base_addr_valid;
	u32			hw_stats_drop_base_addr;
	u8			hw_stats_drop_size_valid;
	u32			hw_stats_drop_size;
};

/* The response to a IPA_QMI_INIT_DRIVER request begins with a standard
 * QMI response, but contains other information as well.  Currently we
 * simply wait for the INIT_DRIVER transaction to complete and
 * ignore any other data that might be returned.
 */
struct ipa_init_modem_driver_rsp {
	struct qmi_response_type_v01	rsp;

	/* This defines the destination endpoint on the modem to which
	 * the AP driver can send control commands.  Must be less than
	 * ipa_endpoint_max().
	 */
	u8				ctrl_comm_dest_end_pt_valid;
	u32				ctrl_comm_dest_end_pt;

	/* This defines the default endpoint.  The AP driver is not
	 * required to configure the hardware with this value.  Must
	 * be less than ipa_endpoint_max().
	 */
	u8				default_end_pt_valid;
	u32				default_end_pt;

	/* This defines whether a second handshake is required to complete
	 * initialization.
	 */
	u8				modem_driver_init_pending_valid;
	u8				modem_driver_init_pending;
};

/* QMI message structure definition for struct ipa_indication_register_req */
static struct qmi_elem_info ipa_indication_register_req_ei[] = {
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     master_driver_init_complete_valid),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_indication_register_req,
					   master_driver_init_complete_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     master_driver_init_complete),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_indication_register_req,
					   master_driver_init_complete),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     data_usage_quota_reached_valid),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_indication_register_req,
					   data_usage_quota_reached_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     data_usage_quota_reached),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_indication_register_req,
					   data_usage_quota_reached),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     ipa_mhi_ready_ind_valid),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_indication_register_req,
					   ipa_mhi_ready_ind_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     ipa_mhi_ready_ind),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_indication_register_req,
					   ipa_mhi_ready_ind),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     endpoint_desc_ind_valid),
		.tlv_type	= 0x13,
		.offset		= offsetof(struct ipa_indication_register_req,
					   endpoint_desc_ind_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     endpoint_desc_ind),
		.tlv_type	= 0x13,
		.offset		= offsetof(struct ipa_indication_register_req,
					   endpoint_desc_ind),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     bw_change_ind_valid),
		.tlv_type	= 0x14,
		.offset		= offsetof(struct ipa_indication_register_req,
					   bw_change_ind_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     bw_change_ind),
		.tlv_type	= 0x14,
		.offset		= offsetof(struct ipa_indication_register_req,
					   bw_change_ind),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_indication_register_rsp */
static struct qmi_elem_info ipa_indication_register_rsp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_rsp,
				     rsp),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_indication_register_rsp,
					   rsp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_init_complete_ind */
static struct qmi_elem_info ipa_init_complete_ind_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_complete_ind,
				     status),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_init_complete_ind,
					   status),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_mem_bounds */
static struct qmi_elem_info ipa_mem_bounds_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_bounds, start),
		.offset		= offsetof(struct ipa_mem_bounds, start),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_bounds, end),
		.offset		= offsetof(struct ipa_mem_bounds, end),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_mem_range */
static struct qmi_elem_info ipa_mem_range_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_range, start),
		.offset		= offsetof(struct ipa_mem_range, start),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_range, size),
		.offset		= offsetof(struct ipa_mem_range, size),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_init_modem_driver_req */
static struct qmi_elem_info ipa_init_modem_driver_req_ei[] = {
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     platform_type_valid),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   platform_type_valid),
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     platform_type),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   platform_type),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hdr_tbl_info_valid),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hdr_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hdr_tbl_info),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hdr_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_route_tbl_info_valid),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_route_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_route_tbl_info),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_route_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_route_tbl_info_valid),
		.tlv_type	= 0x13,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_route_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_route_tbl_info),
		.tlv_type	= 0x13,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_route_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_filter_tbl_start_valid),
		.tlv_type	= 0x14,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_filter_tbl_start_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_filter_tbl_start),
		.tlv_type	= 0x14,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_filter_tbl_start),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_filter_tbl_start_valid),
		.tlv_type	= 0x15,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_filter_tbl_start_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_filter_tbl_start),
		.tlv_type	= 0x15,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_filter_tbl_start),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     modem_mem_info_valid),
		.tlv_type	= 0x16,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   modem_mem_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     modem_mem_info),
		.tlv_type	= 0x16,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   modem_mem_info),
		.ei_array	= ipa_mem_range_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     ctrl_comm_dest_end_pt_valid),
		.tlv_type	= 0x17,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   ctrl_comm_dest_end_pt_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     ctrl_comm_dest_end_pt),
		.tlv_type	= 0x17,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   ctrl_comm_dest_end_pt),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     skip_uc_load_valid),
		.tlv_type	= 0x18,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   skip_uc_load_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     skip_uc_load),
		.tlv_type	= 0x18,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   skip_uc_load),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hdr_proc_ctx_tbl_info_valid),
		.tlv_type	= 0x19,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hdr_proc_ctx_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hdr_proc_ctx_tbl_info),
		.tlv_type	= 0x19,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hdr_proc_ctx_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     zip_tbl_info_valid),
		.tlv_type	= 0x1a,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   zip_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     zip_tbl_info),
		.tlv_type	= 0x1a,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   zip_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_hash_route_tbl_info_valid),
		.tlv_type	= 0x1b,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_hash_route_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_hash_route_tbl_info),
		.tlv_type	= 0x1b,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_hash_route_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_hash_route_tbl_info_valid),
		.tlv_type	= 0x1c,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_hash_route_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_hash_route_tbl_info),
		.tlv_type	= 0x1c,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_hash_route_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_hash_filter_tbl_start_valid),
		.tlv_type	= 0x1d,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_hash_filter_tbl_start_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_hash_filter_tbl_start),
		.tlv_type	= 0x1d,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_hash_filter_tbl_start),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_hash_filter_tbl_start_valid),
		.tlv_type	= 0x1e,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_hash_filter_tbl_start_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_hash_filter_tbl_start),
		.tlv_type	= 0x1e,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_hash_filter_tbl_start),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_quota_base_addr_valid),
		.tlv_type	= 0x1f,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_quota_base_addr_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_quota_base_addr),
		.tlv_type	= 0x1f,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_quota_base_addr),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_quota_size_valid),
		.tlv_type	= 0x20,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_quota_size_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_quota_size),
		.tlv_type	= 0x20,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_quota_size),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_drop_base_addr_valid),
		.tlv_type	= 0x21,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_drop_base_addr_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_drop_base_addr),
		.tlv_type	= 0x21,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_drop_base_addr),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_drop_size_valid),
		.tlv_type	= 0x22,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_drop_size_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_drop_size),
		.tlv_type	= 0x22,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_drop_size),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_init_modem_driver_rsp */
static struct qmi_elem_info ipa_init_modem_driver_rsp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     rsp),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   rsp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     ctrl_comm_dest_end_pt_valid),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   ctrl_comm_dest_end_pt_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     ctrl_comm_dest_end_pt),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   ctrl_comm_dest_end_pt),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     default_end_pt_valid),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   default_end_pt_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     default_end_pt),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   default_end_pt),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     modem_driver_init_pending_valid),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   modem_driver_init_pending_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     modem_driver_init_pending),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   modem_driver_init_pending),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/**
 * struct ipa_qmi - QMI state associated with an IPA
 * @client_handle:	Used to send an QMI requests to the modem
 * @server_handle:	Used to handle QMI requests from the modem
 * @modem_sq:		QMAP socket address for the modem QMI server
 * @init_driver_work:	Work structure used for INIT_DRIVER message handling
 * @initial_boot:	True if first boot has not yet completed
 * @uc_ready:		True once DRIVER_INIT_COMPLETE request received
 * @modem_ready:	True when INIT_DRIVER response received
 * @indication_requested: True when INDICATION_REGISTER request received
 * @indication_sent:	True when INIT_COMPLETE indication sent
 */
struct ipa_qmi {
	struct device *dev;
	const struct ipa_partition *mem_layout;
	struct qmi_handle client_handle;
	struct qmi_handle server_handle;

	/* Information used for the client handle */
	struct sockaddr_qrtr modem_sq;
	struct work_struct init_driver_work;

	/* Flags used in negotiating readiness */
	bool initial_boot   :1;
	bool uc_loaded	    :1;
	bool modem_ready    :1;
	bool indication_requested :1;
	bool indication_sent :1;
};

/**
 * DOC: AP/Modem QMI Handshake
 *
 * The AP and modem perform a "handshake" at initialization time to ensure
 * both sides know when everything is ready to begin operating.  The AP
 * driver (this code) uses two QMI handles (endpoints) for this; a client
 * using a service on the modem, and server to service modem requests (and
 * to supply an indication message from the AP).  Once the handshake is
 * complete, the AP and modem may begin IPA operation.  This occurs
 * only when the AP IPA driver, modem IPA driver, and IPA microcontroller
 * are ready.
 *
 * The QMI service on the modem expects to receive an INIT_DRIVER request from
 * the AP, which contains parameters used by the modem during initialization.
 * The AP sends this request as soon as it is knows the modem side service
 * is available.  The modem responds to this request, and if this response
 * contains a success result, the AP knows the modem IPA driver is ready.
 *
 * The modem is responsible for loading firmware on the IPA microcontroller.
 * This occurs only during the initial modem boot.  The modem sends a
 * separate DRIVER_INIT_COMPLETE request to the AP to report that the
 * microcontroller is ready.  The AP may assume the microcontroller is
 * ready and remain so (even if the modem reboots) once it has received
 * and responded to this request.
 *
 * There is one final exchange involved in the handshake.  It is required
 * on the initial modem boot, but optional (but in practice does occur) on
 * subsequent boots.  The modem expects to receive a final INIT_COMPLETE
 * indication message from the AP when it is about to begin its normal
 * operation.  The AP will only send this message after it has received
 * and responded to an INDICATION_REGISTER request from the modem.
 *
 * So in summary:
 * - Whenever the AP learns the modem has booted and its IPA QMI service
 *   is available, it sends an INIT_DRIVER request to the modem.  The
 *   modem supplies a success response when it is ready to operate.
 * - On the initial boot, the modem sets up the IPA microcontroller, and
 *   sends a DRIVER_INIT_COMPLETE request to the AP when this is done.
 * - When the modem is ready to receive an INIT_COMPLETE indication from
 *   the AP, it sends an INDICATION_REGISTER request to the AP.
 * - On the initial modem boot, everything is ready when:
 *	- AP has received a success response from its INIT_DRIVER request
 *	- AP has responded to a DRIVER_INIT_COMPLETE request
 *	- AP has responded to an INDICATION_REGISTER request from the modem
 *	- AP has sent an INIT_COMPLETE indication to the modem
 * - On subsequent modem boots, everything is ready when:
 *	- AP has received a success response from its INIT_DRIVER request
 *	- AP has responded to a DRIVER_INIT_COMPLETE request
 * - The INDICATION_REGISTER request and INIT_COMPLETE indication are
 *   optional for non-initial modem boots, and have no bearing on the
 *   determination of when things are "ready"
 *
 * Note that on IPA v2.x, the modem doesn't send a DRIVER_INIT_COMPLETE
 * request. Thus, we rely on the uc's IPA_UC_RESPONSE_INIT_COMPLETED to know
 * when the uc is ready. The rest of the process is the same on IPA v2.x and
 * later IPA versions
 */

#define IPA_HOST_SERVICE_SVC_ID		0x31
#define IPA_HOST_SVC_VERS		1
#define IPA_HOST_SERVICE_INS_ID		1

#define IPA_MODEM_SERVICE_SVC_ID	0x31
#define IPA_MODEM_SERVICE_INS_ID	2
#define IPA_MODEM_SVC_VERS		1

#define QMI_INIT_DRIVER_TIMEOUT		60000	/* A minute in milliseconds */

/* Send an INIT_COMPLETE indication message to the modem */
static void ipa_server_init_complete(struct ipa_qmi *ipa_qmi)
{
	struct qmi_handle *qmi = &ipa_qmi->server_handle;
	struct sockaddr_qrtr *sq = &ipa_qmi->modem_sq;
	struct ipa_init_complete_ind ind = { };
	int ret;

	ind.status.result = QMI_RESULT_SUCCESS_V01;
	ind.status.error = QMI_ERR_NONE_V01;

	ret = qmi_send_indication(qmi, sq, IPA_QMI_INIT_COMPLETE,
				  IPA_QMI_INIT_COMPLETE_IND_SZ,
				  ipa_init_complete_ind_ei, &ind);
	if (ret)
		dev_err(ipa_qmi->dev,
			"error %d sending init complete indication\n", ret);
	else
		ipa_qmi->indication_sent = true;
}

/* Determine whether everything is ready to start normal operation.
 * We know everything (else) is ready when we know the IPA driver on
 * the modem is ready, and the microcontroller is ready.
 *
 * When the modem boots (or reboots), the handshake sequence starts
 * with the AP sending the modem an INIT_DRIVER request.  Within
 * that request, the uc_loaded flag will be zero (false) for an
 * initial boot, non-zero (true) for a subsequent (SSR) boot.
 */
static void ipa_qmi_ready(struct ipa_qmi *ipa_qmi)
{
	/* We aren't ready until the modem and microcontroller are */
	if (!ipa_qmi->modem_ready || !ipa_qmi->uc_loaded)
		return;

	/* Send the indication message if it was requested */
	if (ipa_qmi->indication_requested && !ipa_qmi->indication_sent)
		ipa_server_init_complete(ipa_qmi);

	/* The initial boot requires us to send the indication. */
	if (ipa_qmi->initial_boot) {
		if (!ipa_qmi->indication_sent)
			return;

		/* The initial modem boot completed successfully */
		ipa_qmi->initial_boot = false;
	}

	ipa_modem_set_present(ipa_qmi->dev, true);
}

/* All QMI clients from the modem node are gone (modem shut down or crashed). */
static void ipa_server_bye(struct qmi_handle *qmi, unsigned int node)
{
	struct ipa_qmi *ipa_qmi;

	ipa_qmi = container_of(qmi, struct ipa_qmi, server_handle);

	/* The modem client and server go away at the same time */
	memset(&ipa_qmi->modem_sq, 0, sizeof(ipa_qmi->modem_sq));

	/* initial_boot doesn't change when modem reboots */
	/* uc_ready doesn't change when modem reboots */
	ipa_qmi->modem_ready = false;
	ipa_qmi->indication_requested = false;
	ipa_qmi->indication_sent = false;

	ipa_modem_set_present(ipa_qmi->dev, false);
}

static const struct qmi_ops ipa_server_ops = {
	.bye		= ipa_server_bye,
};

/* Callback function to handle an INDICATION_REGISTER request message from the
 * modem.  This informs the AP that the modem is now ready to receive the
 * INIT_COMPLETE indication message.
 */
static void ipa_server_indication_register(struct qmi_handle *qmi,
					   struct sockaddr_qrtr *sq,
					   struct qmi_txn *txn,
					   const void *decoded)
{
	struct ipa_indication_register_rsp rsp = { };
	struct ipa_qmi *ipa_qmi;
	int ret;

	ipa_qmi = container_of(qmi, struct ipa_qmi, server_handle);

	rsp.rsp.result = QMI_RESULT_SUCCESS_V01;
	rsp.rsp.error = QMI_ERR_NONE_V01;

	ret = qmi_send_response(qmi, sq, txn, IPA_QMI_INDICATION_REGISTER,
				IPA_QMI_INDICATION_REGISTER_RSP_SZ,
				ipa_indication_register_rsp_ei, &rsp);
	if (!ret) {
		ipa_qmi->indication_requested = true;
		ipa_qmi_ready(ipa_qmi);		/* We might be ready now */
	} else {
		dev_err(ipa_qmi->dev,
			"error %d sending register indication response\n", ret);
	}
}

/* The server handles two request message types sent by the modem. */
static const struct qmi_msg_handler ipa_server_msg_handlers[] = {
	{
		.type		= QMI_REQUEST,
		.msg_id		= IPA_QMI_INDICATION_REGISTER,
		.ei		= ipa_indication_register_req_ei,
		.decoded_size	= IPA_QMI_INDICATION_REGISTER_REQ_SZ,
		.fn		= ipa_server_indication_register,
	},
	{ },
};

/* Handle an INIT_DRIVER response message from the modem. */
static void ipa_client_init_driver(struct qmi_handle *qmi,
				   struct sockaddr_qrtr *sq,
				   struct qmi_txn *txn, const void *decoded)
{
	txn->result = 0;	/* IPA_QMI_INIT_DRIVER request was successful */
	complete(&txn->completion);
}

/* The client handles one response message type sent by the modem. */
static const struct qmi_msg_handler ipa_client_msg_handlers[] = {
	{
		.type		= QMI_RESPONSE,
		.msg_id		= IPA_QMI_INIT_DRIVER,
		.ei		= ipa_init_modem_driver_rsp_ei,
		.decoded_size	= IPA_QMI_INIT_DRIVER_RSP_SZ,
		.fn		= ipa_client_init_driver,
	},
	{ },
};

/* Return a pointer to an init modem driver request structure, which contains
 * configuration parameters for the modem.  The modem may be started multiple
 * times, but generally these parameters don't change so we can reuse the
 * request structure once it's initialized.  The only exception is the
 * skip_uc_load field, which will be set only after the microcontroller has
 * reported it has completed its initialization.
 */
static void
init_modem_driver_req(struct ipa_qmi *ipa_qmi, struct ipa_init_modem_driver_req *req)
{
	const struct ipa_partition *mem = ipa_qmi->mem_layout;

	/* The microcontroller is initialized on the first boot */
	req->skip_uc_load_valid = 1;
	req->skip_uc_load = ipa_qmi->uc_loaded ? 1 : 0;

	req->platform_type_valid = 1;
	req->platform_type = IPA_QMI_PLATFORM_TYPE_MSM_ANDROID;

	req->ctrl_comm_dest_end_pt = 5;
	req->ctrl_comm_dest_end_pt_valid = 1;

	req->modem_mem_info.start = mem[MEM_MDM].offset;
	req->modem_mem_info.size = mem[MEM_MDM].size;
	req->modem_mem_info_valid = !!mem[MEM_MDM].size;

	req->zip_tbl_info.start = mem[MEM_MDM_COMP].offset;
	req->zip_tbl_info.end = mem[MEM_MDM_COMP].offset + mem[MEM_MDM_COMP].size - 1;
	req->zip_tbl_info_valid = !!mem[MEM_MDM_COMP].size;

	req->hdr_tbl_info.start = mem[MEM_MDM_HDR].offset;
	req->hdr_tbl_info.end = mem[MEM_MDM_HDR].offset + mem[MEM_MDM_HDR].size - 1;
	req->hdr_tbl_info_valid = !!mem[MEM_MDM_HDR].size;

	req->v4_route_tbl_info.start = mem[MEM_RT_V4].offset;
	req->v4_route_tbl_info.end = mem[MEM_RT_V4].size / 4 - 1;
	req->v4_route_tbl_info_valid = 1;

	req->v6_route_tbl_info.start = mem[MEM_RT_V6].offset;
	req->v6_route_tbl_info.end = mem[MEM_RT_V6].size / 4 - 1;
	req->v6_route_tbl_info_valid = 1;

	req->v4_filter_tbl_start = mem[MEM_FT_V4].offset;
	req->v4_filter_tbl_start_valid = 1;

	req->v6_filter_tbl_start = mem[MEM_FT_V6].offset;
	req->v6_filter_tbl_start_valid = 1;
}

/* Send an INIT_DRIVER request to the modem, and wait for it to complete. */
static void ipa_client_init_driver_work(struct work_struct *work)
{
	unsigned long timeout = msecs_to_jiffies(QMI_INIT_DRIVER_TIMEOUT);
	struct ipa_init_modem_driver_req req;
	struct ipa_qmi *ipa_qmi;
	struct qmi_handle *qmi;
	struct qmi_txn txn;
	struct device *dev;
	int ret;

	ipa_qmi = container_of(work, struct ipa_qmi, init_driver_work);
	qmi = &ipa_qmi->client_handle;
	dev = ipa_qmi->dev;

	ret = qmi_txn_init(qmi, &txn, NULL, NULL);
	if (ret < 0) {
		dev_err(dev, "error %d preparing init driver request\n", ret);
		return;
	}

	/* Send the request, and if successful wait for its response */
	init_modem_driver_req(ipa_qmi, &req);
	ret = qmi_send_request(qmi, &ipa_qmi->modem_sq, &txn,
			       IPA_QMI_INIT_DRIVER, IPA_QMI_INIT_DRIVER_REQ_SZ,
			       ipa_init_modem_driver_req_ei, &req);
	if (ret) {
		dev_err(dev, "error %d sending init driver request\n", ret);
	} else {
		ret = qmi_txn_wait(&txn, timeout);
		if (ret)
			dev_err(dev, "error %d awaiting init driver response\n", ret);
	}

	if (!ret) {
		ipa_qmi->modem_ready = true;
		ipa_qmi_ready(ipa_qmi);		/* We might be ready now */
	} else {
		/* If any error occurs we need to cancel the transaction */
		qmi_txn_cancel(&txn);
	}
}

/* The modem server is now available.  We will send an INIT_DRIVER request
 * to the modem, but can't wait for it to complete in this callback thread.
 * Schedule a worker on the global workqueue to do that for us.
 */
static int
ipa_client_new_server(struct qmi_handle *qmi, struct qmi_service *svc)
{
	struct ipa_qmi *ipa_qmi;

	ipa_qmi = container_of(qmi, struct ipa_qmi, client_handle);

	ipa_qmi->modem_sq.sq_family = AF_QIPCRTR;
	ipa_qmi->modem_sq.sq_node = svc->node;
	ipa_qmi->modem_sq.sq_port = svc->port;

	schedule_work(&ipa_qmi->init_driver_work);

	return 0;
}

static const struct qmi_ops ipa_client_ops = {
	.new_server	= ipa_client_new_server,
};

/* Set up for QMI message exchange */
struct ipa_qmi *ipa_qmi_setup(struct device *dev, const struct ipa_partition *layout)
{
	struct ipa_qmi *ipa_qmi;
	int ret;

	ipa_qmi = devm_kzalloc(dev, sizeof(ipa_qmi[0]), GFP_KERNEL);
	if (!ipa_qmi)
		return ERR_PTR(-ENOMEM);

	ipa_qmi->dev = dev;
	ipa_qmi->initial_boot = true;
	ipa_qmi->mem_layout = layout;

	/* The server handle is used to handle the DRIVER_INIT_COMPLETE
	 * request on the first modem boot.  It also receives the
	 * INDICATION_REGISTER request on the first boot and (optionally)
	 * subsequent boots.  The INIT_COMPLETE indication message is
	 * sent over the server handle if requested.
	 */
	ret = qmi_handle_init(&ipa_qmi->server_handle,
			      IPA_QMI_SERVER_MAX_RCV_SZ, &ipa_server_ops,
			      ipa_server_msg_handlers);
	if (ret)
		goto err_free;

	ret = qmi_add_server(&ipa_qmi->server_handle, IPA_HOST_SERVICE_SVC_ID,
			     IPA_HOST_SVC_VERS, IPA_HOST_SERVICE_INS_ID);
	if (ret)
		goto err_server_handle_release;

	/* The client handle is only used for sending an INIT_DRIVER request
	 * to the modem, and receiving its response message.
	 */
	ret = qmi_handle_init(&ipa_qmi->client_handle,
			      IPA_QMI_CLIENT_MAX_RCV_SZ, &ipa_client_ops,
			      ipa_client_msg_handlers);
	if (ret)
		goto err_server_handle_release;

	/* We need this ready before the service lookup is added */
	INIT_WORK(&ipa_qmi->init_driver_work, ipa_client_init_driver_work);

	ret = qmi_add_lookup(&ipa_qmi->client_handle, IPA_MODEM_SERVICE_SVC_ID,
			     IPA_MODEM_SVC_VERS, IPA_MODEM_SERVICE_INS_ID);
	if (ret)
		goto err_client_handle_release;

	return ipa_qmi;

err_client_handle_release:
	/* Releasing the handle also removes registered lookups */
	qmi_handle_release(&ipa_qmi->client_handle);
	memset(&ipa_qmi->client_handle, 0, sizeof(ipa_qmi->client_handle));
err_server_handle_release:
	/* Releasing the handle also removes registered services */
	qmi_handle_release(&ipa_qmi->server_handle);
	memset(&ipa_qmi->server_handle, 0, sizeof(ipa_qmi->server_handle));
err_free:
	devm_kfree(dev, ipa_qmi);

	return ERR_PTR(ret);
}

/* With IPA v2 modem is not required to send DRIVER_INIT_COMPLETE request to AP.
 * We start operation as soon as IPA_UC_RESPONSE_INIT_COMPLETED irq is triggered.
 */
void ipa_qmi_uc_loaded(struct ipa_qmi *ipa_qmi)
{
	ipa_qmi->uc_loaded = true;
	ipa_qmi_ready(ipa_qmi);
}

bool ipa_qmi_is_modem_ready(struct ipa_qmi *ipa_qmi)
{
	return ipa_qmi->modem_ready;
}

/* Tear down IPA QMI handles */
void ipa_qmi_teardown(struct ipa_qmi *ipa_qmi)
{
	cancel_work_sync(&ipa_qmi->init_driver_work);

	qmi_handle_release(&ipa_qmi->client_handle);
	memset(&ipa_qmi->client_handle, 0, sizeof(ipa_qmi->client_handle));

	qmi_handle_release(&ipa_qmi->server_handle);
	memset(&ipa_qmi->server_handle, 0, sizeof(ipa_qmi->server_handle));
}
