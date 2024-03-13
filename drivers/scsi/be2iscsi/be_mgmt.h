/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2017 Broadcom. All Rights Reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Contact Information:
 * linux-drivers@broadcom.com
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

int mgmt_open_connection(struct beiscsi_hba *phba,
			 struct sockaddr *dst_addr,
			 struct beiscsi_endpoint *beiscsi_ep,
			 struct be_dma_mem *nonemb_cmd);

unsigned int mgmt_vendor_specific_fw_cmd(struct be_ctrl_info *ctrl,
					 struct beiscsi_hba *phba,
					 struct bsg_job *job,
					 struct be_dma_mem *nonemb_cmd);

#define BE_INVLDT_CMD_TBL_SZ	128
struct invldt_cmd_tbl {
	unsigned short icd;
	unsigned short cid;
} __packed;

struct invldt_cmds_params_in {
	struct be_cmd_req_hdr hdr;
	unsigned int ref_handle;
	unsigned int icd_count;
	struct invldt_cmd_tbl table[BE_INVLDT_CMD_TBL_SZ];
	unsigned short cleanup_type;
	unsigned short unused;
} __packed;

struct invldt_cmds_params_out {
	struct be_cmd_resp_hdr hdr;
	unsigned int ref_handle;
	unsigned int icd_count;
	unsigned int icd_status[BE_INVLDT_CMD_TBL_SZ];
} __packed;

union be_invldt_cmds_params {
	struct invldt_cmds_params_in request;
	struct invldt_cmds_params_out response;
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

int beiscsi_mgmt_invalidate_icds(struct beiscsi_hba *phba,
				 struct invldt_cmd_tbl *inv_tbl,
				 unsigned int nents);

int beiscsi_get_initiator_name(struct beiscsi_hba *phba, char *name, bool cfg);

int beiscsi_if_en_dhcp(struct beiscsi_hba *phba, u32 ip_type);

int beiscsi_if_en_static(struct beiscsi_hba *phba, u32 ip_type,
			 u8 *ip, u8 *subnet);

int beiscsi_if_set_gw(struct beiscsi_hba *phba, u32 ip_type, u8 *gw);

int beiscsi_if_get_gw(struct beiscsi_hba *phba, u32 ip_type,
		      struct be_cmd_get_def_gateway_resp *resp);

int mgmt_get_nic_conf(struct beiscsi_hba *phba,
		      struct be_cmd_get_nic_conf_resp *mac);

int beiscsi_if_get_info(struct beiscsi_hba *phba, int ip_type,
			struct be_cmd_get_if_info_resp **if_info);

unsigned int beiscsi_if_get_handle(struct beiscsi_hba *phba);

int beiscsi_if_set_vlan(struct beiscsi_hba *phba, uint16_t vlan_tag);

unsigned int beiscsi_boot_logout_sess(struct beiscsi_hba *phba);

unsigned int beiscsi_boot_reopen_sess(struct beiscsi_hba *phba);

unsigned int beiscsi_boot_get_sinfo(struct beiscsi_hba *phba);

unsigned int __beiscsi_boot_get_shandle(struct beiscsi_hba *phba, int async);

int beiscsi_boot_get_shandle(struct beiscsi_hba *phba, unsigned int *s_handle);

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
			     struct be_mem_descriptor *mem_descr,
			     struct hwi_wrb_context *pwrb_context);

void beiscsi_offload_cxn_v2(struct beiscsi_offload_params *params,
			     struct wrb_handle *pwrb_handle,
			     struct hwi_wrb_context *pwrb_context);

unsigned int beiscsi_invalidate_cxn(struct beiscsi_hba *phba,
				    struct beiscsi_endpoint *beiscsi_ep);

unsigned int beiscsi_upload_cxn(struct beiscsi_hba *phba,
				struct beiscsi_endpoint *beiscsi_ep);

int be_cmd_modify_eq_delay(struct beiscsi_hba *phba,
			 struct be_set_eqd *, int num);

int beiscsi_logout_fw_sess(struct beiscsi_hba *phba,
			    uint32_t fw_sess_handle);

#endif
