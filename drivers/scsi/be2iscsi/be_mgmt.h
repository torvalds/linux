/**
 * Copyright (C) 2005 - 2009 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohank@serverengines.com)
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 *
 */

#ifndef _BEISCSI_MGMT_
#define _BEISCSI_MGMT_

#include <linux/types.h>
#include <linux/list.h>
#include "be_iscsi.h"
#include "be_main.h"

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_mcc_sge {
	u8 pa_lo[32];		/* dword 0 */
	u8 pa_hi[32];		/* dword 1 */
	u8 length[32];		/* DWORD 2 */
} __packed;

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_mcc_wrb_payload {
	union {
		struct amap_mcc_sge sgl[19];
		u8 embedded[59 * 32];	/* DWORDS 57 to 115 */
	} u;
} __packed;

/**
 * Pseudo amap definition in which each bit of the actual structure is defined
 * as a byte: used to calculate offset/shift/mask of each field
 */
struct amap_mcc_wrb {
	u8 embedded;		/* DWORD 0 */
	u8 rsvd0[2];		/* DWORD 0 */
	u8 sge_count[5];	/* DWORD 0 */
	u8 rsvd1[16];		/* DWORD 0 */
	u8 special[8];		/* DWORD 0 */
	u8 payload_length[32];
	u8 tag[64];		/* DWORD 2 */
	u8 rsvd2[32];		/* DWORD 4 */
	struct amap_mcc_wrb_payload payload;
};

struct mcc_sge {
	u32 pa_lo;		/* dword 0 */
	u32 pa_hi;		/* dword 1 */
	u32 length;		/* DWORD 2 */
} __packed;

struct mcc_wrb_payload {
	union {
		struct mcc_sge sgl[19];
		u32 embedded[59];	/* DWORDS 57 to 115 */
	} u;
} __packed;

#define MCC_WRB_EMBEDDED_MASK                0x00000001

struct mcc_wrb {
	u32 dw[0];		/* DWORD 0 */
	u32 payload_length;
	u32 tag[2];		/* DWORD 2 */
	u32 rsvd2[1];		/* DWORD 4 */
	struct mcc_wrb_payload payload;
};

unsigned char mgmt_epfw_cleanup(struct beiscsi_hba *phba, unsigned short chute);
int mgmt_open_connection(struct beiscsi_hba *phba, struct sockaddr *dst_addr,
			 struct beiscsi_endpoint *beiscsi_ep);

unsigned char mgmt_upload_connection(struct beiscsi_hba *phba,
				     unsigned short cid,
				     unsigned int upload_flag);
unsigned char mgmt_invalidate_icds(struct beiscsi_hba *phba,
				   unsigned int icd, unsigned int cid);

struct iscsi_invalidate_connection_params_in {
	struct be_cmd_req_hdr hdr;
	unsigned int session_handle;
	unsigned short cid;
	unsigned short unused;
	unsigned short cleanup_type;
	unsigned short save_cfg;
} __packed;

struct iscsi_invalidate_connection_params_out {
	unsigned int session_handle;
	unsigned short cid;
	unsigned short unused;
} __packed;

union iscsi_invalidate_connection_params {
	struct iscsi_invalidate_connection_params_in request;
	struct iscsi_invalidate_connection_params_out response;
} __packed;

struct invalidate_command_table {
	unsigned short icd;
	unsigned short cid;
} __packed;

struct invalidate_commands_params_in {
	struct be_cmd_req_hdr hdr;
	unsigned int ref_handle;
	unsigned int icd_count;
	struct invalidate_command_table table[128];
	unsigned short cleanup_type;
	unsigned short unused;
} __packed;

struct invalidate_commands_params_out {
	unsigned int ref_handle;
	unsigned int icd_count;
	unsigned int icd_status[128];
} __packed;

union invalidate_commands_params {
	struct invalidate_commands_params_in request;
	struct invalidate_commands_params_out response;
} __packed;

struct mgmt_hba_attributes {
	u8 flashrom_version_string[32];
	u8 manufacturer_name[32];
	u32 supported_modes;
	u8 seeprom_version_lo;
	u8 seeprom_version_hi;
	u8 rsvd0[2];
	u32 fw_cmd_data_struct_version;
	u32 ep_fw_data_struct_version;
	u32 future_reserved[12];
	u32 default_extended_timeout;
	u8 controller_model_number[32];
	u8 controller_description[64];
	u8 controller_serial_number[32];
	u8 ip_version_string[32];
	u8 firmware_version_string[32];
	u8 bios_version_string[32];
	u8 redboot_version_string[32];
	u8 driver_version_string[32];
	u8 fw_on_flash_version_string[32];
	u32 functionalities_supported;
	u16 max_cdblength;
	u8 asic_revision;
	u8 generational_guid[16];
	u8 hba_port_count;
	u16 default_link_down_timeout;
	u8 iscsi_ver_min_max;
	u8 multifunction_device;
	u8 cache_valid;
	u8 hba_status;
	u8 max_domains_supported;
	u8 phy_port;
	u32 firmware_post_status;
	u32 hba_mtu[8];
	u8 iscsi_features;
	u8 future_u8[3];
	u32 future_u32[3];
} __packed;

struct mgmt_controller_attributes {
	struct mgmt_hba_attributes hba_attribs;
	u16 pci_vendor_id;
	u16 pci_device_id;
	u16 pci_sub_vendor_id;
	u16 pci_sub_system_id;
	u8 pci_bus_number;
	u8 pci_device_number;
	u8 pci_function_number;
	u8 interface_type;
	u64 unique_identifier;
	u8 netfilters;
	u8 rsvd0[3];
	u8 future_u32[4];
} __packed;

struct be_mgmt_controller_attributes {
	struct be_cmd_req_hdr hdr;
	struct mgmt_controller_attributes params;
} __packed;

struct be_mgmt_controller_attributes_resp {
	struct be_cmd_resp_hdr hdr;
	struct mgmt_controller_attributes params;
} __packed;

/* configuration management */

#define GET_MGMT_CONTROLLER_WS(phba)    (phba->pmgmt_ws)

/* MGMT CMD flags */

#define MGMT_CMDH_FREE                (1<<0)

/*  --- MGMT_ERROR_CODES --- */
/*  Error Codes returned in the status field of the CMD response header */
#define MGMT_STATUS_SUCCESS 0	/* The CMD completed without errors */
#define MGMT_STATUS_FAILED 1	/* Error status in the Status field of */
				/* the CMD_RESPONSE_HEADER  */

#define ISCSI_GET_PDU_TEMPLATE_ADDRESS(pc, pa) {\
    pa->lo = phba->init_mem[ISCSI_MEM_GLOBAL_HEADER].mem_array[0].\
					bus_address.u.a32.address_lo;  \
    pa->hi = phba->init_mem[ISCSI_MEM_GLOBAL_HEADER].mem_array[0].\
					bus_address.u.a32.address_hi;  \
}

struct beiscsi_endpoint {
	struct beiscsi_hba *phba;
	struct beiscsi_sess *sess;
	struct beiscsi_conn *conn;
	unsigned short ip_type;
	char dst6_addr[ISCSI_ADDRESS_BUF_LEN];
	unsigned long dst_addr;
	unsigned short ep_cid;
	unsigned int fw_handle;
	u16 dst_tcpport;
	u16 cid_vld;
};

unsigned char mgmt_get_fw_config(struct be_ctrl_info *ctrl,
				 struct beiscsi_hba *phba);

unsigned char mgmt_invalidate_connection(struct beiscsi_hba *phba,
					 struct beiscsi_endpoint *beiscsi_ep,
					 unsigned short cid,
					 unsigned short issue_reset,
					 unsigned short savecfg_flag);

unsigned char mgmt_fw_cmd(struct be_ctrl_info *ctrl,
			  struct beiscsi_hba *phba,
			  char *buf, unsigned int len);
#endif
