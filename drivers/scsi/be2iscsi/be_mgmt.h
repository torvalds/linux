/**
 * Copyright (C) 2005 - 2014 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohan.kallickal@emulex.com)
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#ifndef _BEISCSI_MGMT_
#define _BEISCSI_MGMT_

#include <scsi/scsi_bsg_iscsi.h>
#include "be_iscsi.h"
#include "be_main.h"

#define IP_ACTION_ADD	0x01
#define IP_ACTION_DEL	0x02

#define IP_V6_LEN	16
#define IP_V4_LEN	4

/* UE Status and Mask register */
#define PCICFG_UE_STATUS_LOW            0xA0
#define PCICFG_UE_STATUS_HIGH           0xA4
#define PCICFG_UE_STATUS_MASK_LOW       0xA8
#define PCICFG_UE_STATUS_MASK_HI        0xAC

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

int mgmt_epfw_cleanup(struct beiscsi_hba *phba, unsigned short chute);
int mgmt_open_connection(struct beiscsi_hba *phba,
			 struct sockaddr *dst_addr,
			 struct beiscsi_endpoint *beiscsi_ep,
			 struct be_dma_mem *nonemb_cmd);

unsigned int mgmt_upload_connection(struct beiscsi_hba *phba,
				     unsigned short cid,
				     unsigned int upload_flag);
unsigned int mgmt_invalidate_icds(struct beiscsi_hba *phba,
				struct invalidate_command_table *inv_tbl,
				unsigned int num_invalidate, unsigned int cid,
				struct be_dma_mem *nonemb_cmd);
unsigned int mgmt_vendor_specific_fw_cmd(struct be_ctrl_info *ctrl,
					 struct beiscsi_hba *phba,
					 struct bsg_job *job,
					 struct be_dma_mem *nonemb_cmd);

#define BEISCSI_NO_RST_ISSUE	0
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
	u8 flashrom_version_string[BEISCSI_VER_STRLEN];
	u8 manufacturer_name[BEISCSI_VER_STRLEN];
	u32 supported_modes;
	u8 seeprom_version_lo;
	u8 seeprom_version_hi;
	u8 rsvd0[2];
	u32 fw_cmd_data_struct_version;
	u32 ep_fw_data_struct_version;
	u8 ncsi_version_string[12];
	u32 default_extended_timeout;
	u8 controller_model_number[BEISCSI_VER_STRLEN];
	u8 controller_description[64];
	u8 controller_serial_number[BEISCSI_VER_STRLEN];
	u8 ip_version_string[BEISCSI_VER_STRLEN];
	u8 firmware_version_string[BEISCSI_VER_STRLEN];
	u8 bios_version_string[BEISCSI_VER_STRLEN];
	u8 redboot_version_string[BEISCSI_VER_STRLEN];
	u8 driver_version_string[BEISCSI_VER_STRLEN];
	u8 fw_on_flash_version_string[BEISCSI_VER_STRLEN];
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
	u8 asic_generation;
	u8 future_u8[2];
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
	u32 future_u32[4];
} __packed;

struct be_mgmt_controller_attributes {
	struct be_cmd_req_hdr hdr;
	struct mgmt_controller_attributes params;
} __packed;

struct be_mgmt_controller_attributes_resp {
	struct be_cmd_resp_hdr hdr;
	struct mgmt_controller_attributes params;
} __packed;

struct be_bsg_vendor_cmd {
	struct be_cmd_req_hdr hdr;
	unsigned short region;
	unsigned short offset;
	unsigned short sector;
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

#define BEISCSI_WRITE_FLASH 0
#define BEISCSI_READ_FLASH 1

struct beiscsi_endpoint {
	struct beiscsi_hba *phba;
	struct beiscsi_sess *sess;
	struct beiscsi_conn *conn;
	struct iscsi_endpoint *openiscsi_ep;
	unsigned short ip_type;
	char dst6_addr[ISCSI_ADDRESS_BUF_LEN];
	unsigned long dst_addr;
	unsigned short ep_cid;
	unsigned int fw_handle;
	u16 dst_tcpport;
	u16 cid_vld;
};

int mgmt_get_fw_config(struct be_ctrl_info *ctrl,
				 struct beiscsi_hba *phba);

unsigned int mgmt_invalidate_connection(struct beiscsi_hba *phba,
					 struct beiscsi_endpoint *beiscsi_ep,
					 unsigned short cid,
					 unsigned short issue_reset,
					 unsigned short savecfg_flag);

int mgmt_set_ip(struct beiscsi_hba *phba,
		struct iscsi_iface_param_info *ip_param,
		struct iscsi_iface_param_info *subnet_param,
		uint32_t boot_proto);

unsigned int mgmt_get_boot_target(struct beiscsi_hba *phba);

unsigned int mgmt_reopen_session(struct beiscsi_hba *phba,
				  unsigned int reopen_type,
				  unsigned sess_handle);

unsigned int mgmt_get_session_info(struct beiscsi_hba *phba,
				   u32 boot_session_handle,
				   struct be_dma_mem *nonemb_cmd);

int mgmt_get_nic_conf(struct beiscsi_hba *phba,
		      struct be_cmd_get_nic_conf_resp *mac);

int mgmt_get_if_info(struct beiscsi_hba *phba, int ip_type,
		     struct be_cmd_get_if_info_resp **if_info);

int mgmt_get_gateway(struct beiscsi_hba *phba, int ip_type,
		     struct be_cmd_get_def_gateway_resp *gateway);

int mgmt_set_gateway(struct beiscsi_hba *phba,
		     struct iscsi_iface_param_info *gateway_param);

int be_mgmt_get_boot_shandle(struct beiscsi_hba *phba,
			      unsigned int *s_handle);

unsigned int mgmt_get_all_if_id(struct beiscsi_hba *phba);

int mgmt_set_vlan(struct beiscsi_hba *phba, uint16_t vlan_tag);

ssize_t beiscsi_drvr_ver_disp(struct device *dev,
			       struct device_attribute *attr, char *buf);

ssize_t beiscsi_fw_ver_disp(struct device *dev,
			     struct device_attribute *attr, char *buf);

ssize_t beiscsi_active_session_disp(struct device *dev,
				     struct device_attribute *attr, char *buf);

ssize_t beiscsi_adap_family_disp(struct device *dev,
				  struct device_attribute *attr, char *buf);


ssize_t beiscsi_free_session_disp(struct device *dev,
				   struct device_attribute *attr, char *buf);

ssize_t beiscsi_phys_port_disp(struct device *dev,
				struct device_attribute *attr, char *buf);

void beiscsi_offload_cxn_v0(struct beiscsi_offload_params *params,
			     struct wrb_handle *pwrb_handle,
			     struct be_mem_descriptor *mem_descr);

void beiscsi_offload_cxn_v2(struct beiscsi_offload_params *params,
			     struct wrb_handle *pwrb_handle);
void beiscsi_ue_detect(struct beiscsi_hba *phba);
int be_cmd_modify_eq_delay(struct beiscsi_hba *phba,
			 struct be_set_eqd *, int num);

#endif
