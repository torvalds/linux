/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _QED_MCP_H
#define _QED_MCP_H

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qed/qed_fcoe_if.h>
#include "qed_hsi.h"
#include "qed_dev_api.h"

struct qed_mcp_link_speed_params {
	bool    autoneg;
	u32     advertised_speeds;      /* bitmask of DRV_SPEED_CAPABILITY */
	u32     forced_speed;	   /* In Mb/s */
};

struct qed_mcp_link_pause_params {
	bool    autoneg;
	bool    forced_rx;
	bool    forced_tx;
};

enum qed_mcp_eee_mode {
	QED_MCP_EEE_DISABLED,
	QED_MCP_EEE_ENABLED,
	QED_MCP_EEE_UNSUPPORTED
};

struct qed_mcp_link_params {
	struct qed_mcp_link_speed_params speed;
	struct qed_mcp_link_pause_params pause;
	u32 loopback_mode;
	struct qed_link_eee_params eee;
};

struct qed_mcp_link_capabilities {
	u32 speed_capabilities;
	bool default_speed_autoneg;
	enum qed_mcp_eee_mode default_eee;
	u32 eee_lpi_timer;
	u8 eee_speed_caps;
};

struct qed_mcp_link_state {
	bool    link_up;

	u32	min_pf_rate;

	/* Actual link speed in Mb/s */
	u32	line_speed;

	/* PF max speed in Mb/s, deduced from line_speed
	 * according to PF max bandwidth configuration.
	 */
	u32     speed;
	bool    full_duplex;

	bool    an;
	bool    an_complete;
	bool    parallel_detection;
	bool    pfc_enabled;

#define QED_LINK_PARTNER_SPEED_1G_HD    BIT(0)
#define QED_LINK_PARTNER_SPEED_1G_FD    BIT(1)
#define QED_LINK_PARTNER_SPEED_10G      BIT(2)
#define QED_LINK_PARTNER_SPEED_20G      BIT(3)
#define QED_LINK_PARTNER_SPEED_25G      BIT(4)
#define QED_LINK_PARTNER_SPEED_40G      BIT(5)
#define QED_LINK_PARTNER_SPEED_50G      BIT(6)
#define QED_LINK_PARTNER_SPEED_100G     BIT(7)
	u32     partner_adv_speed;

	bool    partner_tx_flow_ctrl_en;
	bool    partner_rx_flow_ctrl_en;

#define QED_LINK_PARTNER_SYMMETRIC_PAUSE (1)
#define QED_LINK_PARTNER_ASYMMETRIC_PAUSE (2)
#define QED_LINK_PARTNER_BOTH_PAUSE (3)
	u8      partner_adv_pause;

	bool    sfp_tx_fault;
	bool    eee_active;
	u8      eee_adv_caps;
	u8      eee_lp_adv_caps;
};

struct qed_mcp_function_info {
	u8				pause_on_host;

	enum qed_pci_personality	protocol;

	u8				bandwidth_min;
	u8				bandwidth_max;

	u8				mac[ETH_ALEN];

	u64				wwn_port;
	u64				wwn_node;

#define QED_MCP_VLAN_UNSET              (0xffff)
	u16				ovlan;

	u16				mtu;
};

struct qed_mcp_nvm_common {
	u32	offset;
	u32	param;
	u32	resp;
	u32	cmd;
};

struct qed_mcp_drv_version {
	u32	version;
	u8	name[MCP_DRV_VER_STR_SIZE - 4];
};

struct qed_mcp_lan_stats {
	u64 ucast_rx_pkts;
	u64 ucast_tx_pkts;
	u32 fcs_err;
};

struct qed_mcp_fcoe_stats {
	u64 rx_pkts;
	u64 tx_pkts;
	u32 fcs_err;
	u32 login_failure;
};

struct qed_mcp_iscsi_stats {
	u64 rx_pdus;
	u64 tx_pdus;
	u64 rx_bytes;
	u64 tx_bytes;
};

struct qed_mcp_rdma_stats {
	u64 rx_pkts;
	u64 tx_pkts;
	u64 rx_bytes;
	u64 tx_byts;
};

enum qed_mcp_protocol_type {
	QED_MCP_LAN_STATS,
	QED_MCP_FCOE_STATS,
	QED_MCP_ISCSI_STATS,
	QED_MCP_RDMA_STATS
};

union qed_mcp_protocol_stats {
	struct qed_mcp_lan_stats lan_stats;
	struct qed_mcp_fcoe_stats fcoe_stats;
	struct qed_mcp_iscsi_stats iscsi_stats;
	struct qed_mcp_rdma_stats rdma_stats;
};

enum qed_ov_eswitch {
	QED_OV_ESWITCH_NONE,
	QED_OV_ESWITCH_VEB,
	QED_OV_ESWITCH_VEPA
};

enum qed_ov_client {
	QED_OV_CLIENT_DRV,
	QED_OV_CLIENT_USER,
	QED_OV_CLIENT_VENDOR_SPEC
};

enum qed_ov_driver_state {
	QED_OV_DRIVER_STATE_NOT_LOADED,
	QED_OV_DRIVER_STATE_DISABLED,
	QED_OV_DRIVER_STATE_ACTIVE
};

enum qed_ov_wol {
	QED_OV_WOL_DEFAULT,
	QED_OV_WOL_DISABLED,
	QED_OV_WOL_ENABLED
};

enum qed_mfw_tlv_type {
	QED_MFW_TLV_GENERIC = 0x1,	/* Core driver TLVs */
	QED_MFW_TLV_ETH = 0x2,		/* L2 driver TLVs */
	QED_MFW_TLV_FCOE = 0x4,		/* FCoE protocol TLVs */
	QED_MFW_TLV_ISCSI = 0x8,	/* SCSI protocol TLVs */
	QED_MFW_TLV_MAX = 0x16,
};

struct qed_mfw_tlv_generic {
#define QED_MFW_TLV_FLAGS_SIZE	2
	struct {
		u8 ipv4_csum_offload;
		u8 lso_supported;
		bool b_set;
	} flags;

#define QED_MFW_TLV_MAC_COUNT 3
	/* First entry for primary MAC, 2 secondary MACs possible */
	u8 mac[QED_MFW_TLV_MAC_COUNT][6];
	bool mac_set[QED_MFW_TLV_MAC_COUNT];

	u64 rx_frames;
	bool rx_frames_set;
	u64 rx_bytes;
	bool rx_bytes_set;
	u64 tx_frames;
	bool tx_frames_set;
	u64 tx_bytes;
	bool tx_bytes_set;
};

union qed_mfw_tlv_data {
	struct qed_mfw_tlv_generic generic;
	struct qed_mfw_tlv_eth eth;
	struct qed_mfw_tlv_fcoe fcoe;
	struct qed_mfw_tlv_iscsi iscsi;
};

/**
 * @brief - returns the link params of the hw function
 *
 * @param p_hwfn
 *
 * @returns pointer to link params
 */
struct qed_mcp_link_params *qed_mcp_get_link_params(struct qed_hwfn *);

/**
 * @brief - return the link state of the hw function
 *
 * @param p_hwfn
 *
 * @returns pointer to link state
 */
struct qed_mcp_link_state *qed_mcp_get_link_state(struct qed_hwfn *);

/**
 * @brief - return the link capabilities of the hw function
 *
 * @param p_hwfn
 *
 * @returns pointer to link capabilities
 */
struct qed_mcp_link_capabilities
	*qed_mcp_get_link_capabilities(struct qed_hwfn *p_hwfn);

/**
 * @brief Request the MFW to set the the link according to 'link_input'.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param b_up - raise link if `true'. Reset link if `false'.
 *
 * @return int
 */
int qed_mcp_set_link(struct qed_hwfn   *p_hwfn,
		     struct qed_ptt     *p_ptt,
		     bool               b_up);

/**
 * @brief Get the management firmware version value
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_mfw_ver    - mfw version value
 * @param p_running_bundle_id	- image id in nvram; Optional.
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_get_mfw_ver(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt,
			u32 *p_mfw_ver, u32 *p_running_bundle_id);

/**
 * @brief Get the MBI version value
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_mbi_ver - A pointer to a variable to be filled with the MBI version.
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_get_mbi_ver(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt, u32 *p_mbi_ver);

/**
 * @brief Get media type value of the port.
 *
 * @param cdev      - qed dev pointer
 * @param p_ptt
 * @param mfw_ver    - media type value
 *
 * @return int -
 *      0 - Operation was successul.
 *      -EBUSY - Operation failed
 */
int qed_mcp_get_media_type(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, u32 *media_type);

/**
 * @brief General function for sending commands to the MCP
 *        mailbox. It acquire mutex lock for the entire
 *        operation, from sending the request until the MCP
 *        response. Waiting for MCP response will be checked up
 *        to 5 seconds every 5ms.
 *
 * @param p_hwfn     - hw function
 * @param p_ptt      - PTT required for register access
 * @param cmd        - command to be sent to the MCP.
 * @param param      - Optional param
 * @param o_mcp_resp - The MCP response code (exclude sequence).
 * @param o_mcp_param- Optional parameter provided by the MCP
 *                     response
 * @return int - 0 - operation
 * was successul.
 */
int qed_mcp_cmd(struct qed_hwfn *p_hwfn,
		struct qed_ptt *p_ptt,
		u32 cmd,
		u32 param,
		u32 *o_mcp_resp,
		u32 *o_mcp_param);

/**
 * @brief - drains the nig, allowing completion to pass in case of pauses.
 *          (Should be called only from sleepable context)
 *
 * @param p_hwfn
 * @param p_ptt
 */
int qed_mcp_drain(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt);

/**
 * @brief Get the flash size value
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_flash_size  - flash size in bytes to be filled.
 *
 * @return int - 0 - operation was successul.
 */
int qed_mcp_get_flash_size(struct qed_hwfn     *p_hwfn,
			   struct qed_ptt       *p_ptt,
			   u32 *p_flash_size);

/**
 * @brief Send driver version to MFW
 *
 * @param p_hwfn
 * @param p_ptt
 * @param version - Version value
 * @param name - Protocol driver name
 *
 * @return int - 0 - operation was successul.
 */
int
qed_mcp_send_drv_version(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct qed_mcp_drv_version *p_ver);

/**
 * @brief Notify MFW about the change in base device properties
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param client - qed client type
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_ov_update_current_config(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     enum qed_ov_client client);

/**
 * @brief Notify MFW about the driver state
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param drv_state - Driver state
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_ov_update_driver_state(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   enum qed_ov_driver_state drv_state);

/**
 * @brief Send MTU size to MFW
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param mtu - MTU size
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_ov_update_mtu(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u16 mtu);

/**
 * @brief Send MAC address to MFW
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param mac - MAC address
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_ov_update_mac(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u8 *mac);

/**
 * @brief Send WOL mode to MFW
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param wol - WOL mode
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_ov_update_wol(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  enum qed_ov_wol wol);

/**
 * @brief Set LED status
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param mode - LED mode
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_set_led(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    enum qed_led_mode mode);

/**
 * @brief Read from nvm
 *
 *  @param cdev
 *  @param addr - nvm offset
 *  @param p_buf - nvm read buffer
 *  @param len - buffer len
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_nvm_read(struct qed_dev *cdev, u32 addr, u8 *p_buf, u32 len);

/**
 * @brief Write to nvm
 *
 *  @param cdev
 *  @param addr - nvm offset
 *  @param cmd - nvm command
 *  @param p_buf - nvm write buffer
 *  @param len - buffer len
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_nvm_write(struct qed_dev *cdev,
		      u32 cmd, u32 addr, u8 *p_buf, u32 len);

/**
 * @brief Put file begin
 *
 *  @param cdev
 *  @param addr - nvm offset
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_nvm_put_file_begin(struct qed_dev *cdev, u32 addr);

/**
 * @brief Check latest response
 *
 *  @param cdev
 *  @param p_buf - nvm write buffer
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_nvm_resp(struct qed_dev *cdev, u8 *p_buf);

struct qed_nvm_image_att {
	u32 start_addr;
	u32 length;
};

/**
 * @brief Allows reading a whole nvram image
 *
 * @param p_hwfn
 * @param image_id - image to get attributes for
 * @param p_image_att - image attributes structure into which to fill data
 *
 * @return int - 0 - operation was successful.
 */
int
qed_mcp_get_nvm_image_att(struct qed_hwfn *p_hwfn,
			  enum qed_nvm_images image_id,
			  struct qed_nvm_image_att *p_image_att);

/**
 * @brief Allows reading a whole nvram image
 *
 * @param p_hwfn
 * @param image_id - image requested for reading
 * @param p_buffer - allocated buffer into which to fill data
 * @param buffer_len - length of the allocated buffer.
 *
 * @return 0 iff p_buffer now contains the nvram image.
 */
int qed_mcp_get_nvm_image(struct qed_hwfn *p_hwfn,
			  enum qed_nvm_images image_id,
			  u8 *p_buffer, u32 buffer_len);

/**
 * @brief Bist register test
 *
 *  @param p_hwfn    - hw function
 *  @param p_ptt     - PTT required for register access
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_bist_register_test(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt);

/**
 * @brief Bist clock test
 *
 *  @param p_hwfn    - hw function
 *  @param p_ptt     - PTT required for register access
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_bist_clock_test(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt);

/**
 * @brief Bist nvm test - get number of images
 *
 *  @param p_hwfn       - hw function
 *  @param p_ptt        - PTT required for register access
 *  @param num_images   - number of images if operation was
 *			  successful. 0 if not.
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_bist_nvm_get_num_images(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    u32 *num_images);

/**
 * @brief Bist nvm test - get image attributes by index
 *
 *  @param p_hwfn      - hw function
 *  @param p_ptt       - PTT required for register access
 *  @param p_image_att - Attributes of image
 *  @param image_index - Index of image to get information for
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_bist_nvm_get_image_att(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct bist_nvm_image_att *p_image_att,
				   u32 image_index);

/**
 * @brief - Processes the TLV request from MFW i.e., get the required TLV info
 *          from the qed client and send it to the MFW.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return 0 upon success.
 */
int qed_mfw_process_tlv_req(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/* Using hwfn number (and not pf_num) is required since in CMT mode,
 * same pf_num may be used by two different hwfn
 * TODO - this shouldn't really be in .h file, but until all fields
 * required during hw-init will be placed in their correct place in shmem
 * we need it in qed_dev.c [for readin the nvram reflection in shmem].
 */
#define MCP_PF_ID_BY_REL(p_hwfn, rel_pfid) (QED_IS_BB((p_hwfn)->cdev) ?	       \
					    ((rel_pfid) |		       \
					     ((p_hwfn)->abs_pf_id & 1) << 3) : \
					    rel_pfid)
#define MCP_PF_ID(p_hwfn) MCP_PF_ID_BY_REL(p_hwfn, (p_hwfn)->rel_pf_id)

#define MFW_PORT(_p_hwfn)       ((_p_hwfn)->abs_pf_id %			  \
				 ((_p_hwfn)->cdev->num_ports_in_engine * \
				  qed_device_num_engines((_p_hwfn)->cdev)))

struct qed_mcp_info {
	/* List for mailbox commands which were sent and wait for a response */
	struct list_head			cmd_list;

	/* Spinlock used for protecting the access to the mailbox commands list
	 * and the sending of the commands.
	 */
	spinlock_t				cmd_lock;

	/* Flag to indicate whether sending a MFW mailbox command is blocked */
	bool					b_block_cmd;

	/* Spinlock used for syncing SW link-changes and link-changes
	 * originating from attention context.
	 */
	spinlock_t				link_lock;

	u32					public_base;
	u32					drv_mb_addr;
	u32					mfw_mb_addr;
	u32					port_addr;
	u16					drv_mb_seq;
	u16					drv_pulse_seq;
	struct qed_mcp_link_params		link_input;
	struct qed_mcp_link_state		link_output;
	struct qed_mcp_link_capabilities	link_capabilities;
	struct qed_mcp_function_info		func_info;
	u8					*mfw_mb_cur;
	u8					*mfw_mb_shadow;
	u16					mfw_mb_length;
	u32					mcp_hist;

	/* Capabilties negotiated with the MFW */
	u32					capabilities;
};

struct qed_mcp_mb_params {
	u32 cmd;
	u32 param;
	void *p_data_src;
	void *p_data_dst;
	u8 data_src_size;
	u8 data_dst_size;
	u32 mcp_resp;
	u32 mcp_param;
	u32 flags;
#define QED_MB_FLAG_CAN_SLEEP	(0x1 << 0)
#define QED_MB_FLAG_AVOID_BLOCK	(0x1 << 1)
#define QED_MB_FLAGS_IS_SET(params, flag) \
	({ typeof(params) __params = (params); \
	   (__params && (__params->flags & QED_MB_FLAG_ ## flag)); })
};

struct qed_drv_tlv_hdr {
	u8 tlv_type;
	u8 tlv_length;	/* In dwords - not including this header */
	u8 tlv_reserved;
#define QED_DRV_TLV_FLAGS_CHANGED 0x01
	u8 tlv_flags;
};

/**
 * @brief Initialize the interface with the MCP
 *
 * @param p_hwfn - HW func
 * @param p_ptt - PTT required for register access
 *
 * @return int
 */
int qed_mcp_cmd_init(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt);

/**
 * @brief Initialize the port interface with the MCP
 *
 * @param p_hwfn
 * @param p_ptt
 * Can only be called after `num_ports_in_engines' is set
 */
void qed_mcp_cmd_port_init(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt);
/**
 * @brief Releases resources allocated during the init process.
 *
 * @param p_hwfn - HW func
 * @param p_ptt - PTT required for register access
 *
 * @return int
 */

int qed_mcp_free(struct qed_hwfn *p_hwfn);

/**
 * @brief This function is called from the DPC context. After
 * pointing PTT to the mfw mb, check for events sent by the MCP
 * to the driver and ack them. In case a critical event
 * detected, it will be handled here, otherwise the work will be
 * queued to a sleepable work-queue.
 *
 * @param p_hwfn - HW function
 * @param p_ptt - PTT required for register access
 * @return int - 0 - operation
 * was successul.
 */
int qed_mcp_handle_events(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt);

enum qed_drv_role {
	QED_DRV_ROLE_OS,
	QED_DRV_ROLE_KDUMP,
};

struct qed_load_req_params {
	/* Input params */
	enum qed_drv_role drv_role;
	u8 timeout_val;
	bool avoid_eng_reset;
	enum qed_override_force_load override_force_load;

	/* Output params */
	u32 load_code;
};

/**
 * @brief Sends a LOAD_REQ to the MFW, and in case the operation succeeds,
 *        returns whether this PF is the first on the engine/port or function.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_params
 *
 * @return int - 0 - Operation was successful.
 */
int qed_mcp_load_req(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     struct qed_load_req_params *p_params);

/**
 * @brief Sends a UNLOAD_REQ message to the MFW
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return int - 0 - Operation was successful.
 */
int qed_mcp_unload_req(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * @brief Sends a UNLOAD_DONE message to the MFW
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return int - 0 - Operation was successful.
 */
int qed_mcp_unload_done(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * @brief Read the MFW mailbox into Current buffer.
 *
 * @param p_hwfn
 * @param p_ptt
 */
void qed_mcp_read_mb(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt);

/**
 * @brief Ack to mfw that driver finished FLR process for VFs
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vfs_to_ack - bit mask of all engine VFs for which the PF acks.
 *
 * @param return int - 0 upon success.
 */
int qed_mcp_ack_vf_flr(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt, u32 *vfs_to_ack);

/**
 * @brief - calls during init to read shmem of all function-related info.
 *
 * @param p_hwfn
 *
 * @param return 0 upon success.
 */
int qed_mcp_fill_shmem_func_info(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt);

/**
 * @brief - Reset the MCP using mailbox command.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return 0 upon success.
 */
int qed_mcp_reset(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt);

/**
 * @brief - Sends an NVM read command request to the MFW to get
 *        a buffer.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param cmd - Command: DRV_MSG_CODE_NVM_GET_FILE_DATA or
 *            DRV_MSG_CODE_NVM_READ_NVRAM commands
 * @param param - [0:23] - Offset [24:31] - Size
 * @param o_mcp_resp - MCP response
 * @param o_mcp_param - MCP response param
 * @param o_txn_size -  Buffer size output
 * @param o_buf - Pointer to the buffer returned by the MFW.
 *
 * @param return 0 upon success.
 */
int qed_mcp_nvm_rd_cmd(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       u32 cmd,
		       u32 param,
		       u32 *o_mcp_resp,
		       u32 *o_mcp_param, u32 *o_txn_size, u32 *o_buf);

/**
 * @brief Read from sfp
 *
 *  @param p_hwfn - hw function
 *  @param p_ptt  - PTT required for register access
 *  @param port   - transceiver port
 *  @param addr   - I2C address
 *  @param offset - offset in sfp
 *  @param len    - buffer length
 *  @param p_buf  - buffer to read into
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_phy_sfp_read(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			 u32 port, u32 addr, u32 offset, u32 len, u8 *p_buf);

/**
 * @brief indicates whether the MFW objects [under mcp_info] are accessible
 *
 * @param p_hwfn
 *
 * @return true iff MFW is running and mcp_info is initialized
 */
bool qed_mcp_is_init(struct qed_hwfn *p_hwfn);

/**
 * @brief request MFW to configure MSI-X for a VF
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vf_id - absolute inside engine
 * @param num_sbs - number of entries to request
 *
 * @return int
 */
int qed_mcp_config_vf_msix(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, u8 vf_id, u8 num);

/**
 * @brief - Halt the MCP.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return 0 upon success.
 */
int qed_mcp_halt(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * @brief - Wake up the MCP.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return 0 upon success.
 */
int qed_mcp_resume(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

int qed_configure_pf_min_bandwidth(struct qed_dev *cdev, u8 min_bw);
int qed_configure_pf_max_bandwidth(struct qed_dev *cdev, u8 max_bw);
int __qed_configure_pf_max_bandwidth(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_mcp_link_state *p_link,
				     u8 max_bw);
int __qed_configure_pf_min_bandwidth(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_mcp_link_state *p_link,
				     u8 min_bw);

int qed_mcp_mask_parities(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u32 mask_parities);

/**
 * @brief - Sets the MFW's max value for the given resource
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param res_id
 *  @param resc_max_val
 *  @param p_mcp_resp
 *
 * @return int - 0 - operation was successful.
 */
int
qed_mcp_set_resc_max_val(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 enum qed_resources res_id,
			 u32 resc_max_val, u32 *p_mcp_resp);

/**
 * @brief - Gets the MFW allocation info for the given resource
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param res_id
 *  @param p_mcp_resp
 *  @param p_resc_num
 *  @param p_resc_start
 *
 * @return int - 0 - operation was successful.
 */
int
qed_mcp_get_resc_info(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      enum qed_resources res_id,
		      u32 *p_mcp_resp, u32 *p_resc_num, u32 *p_resc_start);

/**
 * @brief Send eswitch mode to MFW
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param eswitch - eswitch mode
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_ov_update_eswitch(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      enum qed_ov_eswitch eswitch);

#define QED_MCP_RESC_LOCK_MIN_VAL       RESOURCE_DUMP
#define QED_MCP_RESC_LOCK_MAX_VAL       31

enum qed_resc_lock {
	QED_RESC_LOCK_DBG_DUMP = QED_MCP_RESC_LOCK_MIN_VAL,
	QED_RESC_LOCK_PTP_PORT0,
	QED_RESC_LOCK_PTP_PORT1,
	QED_RESC_LOCK_PTP_PORT2,
	QED_RESC_LOCK_PTP_PORT3,
	QED_RESC_LOCK_RESC_ALLOC = QED_MCP_RESC_LOCK_MAX_VAL,
	QED_RESC_LOCK_RESC_INVALID
};

/**
 * @brief - Initiates PF FLR
 *
 *  @param p_hwfn
 *  @param p_ptt
 *
 * @return int - 0 - operation was successful.
 */
int qed_mcp_initiate_pf_flr(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
struct qed_resc_lock_params {
	/* Resource number [valid values are 0..31] */
	u8 resource;

	/* Lock timeout value in seconds [default, none or 1..254] */
	u8 timeout;
#define QED_MCP_RESC_LOCK_TO_DEFAULT    0
#define QED_MCP_RESC_LOCK_TO_NONE       255

	/* Number of times to retry locking */
	u8 retry_num;
#define QED_MCP_RESC_LOCK_RETRY_CNT_DFLT        10

	/* The interval in usec between retries */
	u16 retry_interval;
#define QED_MCP_RESC_LOCK_RETRY_VAL_DFLT        10000

	/* Use sleep or delay between retries */
	bool sleep_b4_retry;

	/* Will be set as true if the resource is free and granted */
	bool b_granted;

	/* Will be filled with the resource owner.
	 * [0..15 = PF0-15, 16 = MFW]
	 */
	u8 owner;
};

/**
 * @brief Acquires MFW generic resource lock
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param p_params
 *
 * @return int - 0 - operation was successful.
 */
int
qed_mcp_resc_lock(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt, struct qed_resc_lock_params *p_params);

struct qed_resc_unlock_params {
	/* Resource number [valid values are 0..31] */
	u8 resource;

	/* Allow to release a resource even if belongs to another PF */
	bool b_force;

	/* Will be set as true if the resource is released */
	bool b_released;
};

/**
 * @brief Releases MFW generic resource lock
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param p_params
 *
 * @return int - 0 - operation was successful.
 */
int
qed_mcp_resc_unlock(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_resc_unlock_params *p_params);

/**
 * @brief - default initialization for lock/unlock resource structs
 *
 * @param p_lock - lock params struct to be initialized; Can be NULL
 * @param p_unlock - unlock params struct to be initialized; Can be NULL
 * @param resource - the requested resource
 * @paral b_is_permanent - disable retries & aging when set
 */
void qed_mcp_resc_lock_default_init(struct qed_resc_lock_params *p_lock,
				    struct qed_resc_unlock_params *p_unlock,
				    enum qed_resc_lock
				    resource, bool b_is_permanent);
/**
 * @brief Learn of supported MFW features; To be done during early init
 *
 * @param p_hwfn
 * @param p_ptt
 */
int qed_mcp_get_capabilities(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * @brief Inform MFW of set of features supported by driver. Should be done
 * inside the content of the LOAD_REQ.
 *
 * @param p_hwfn
 * @param p_ptt
 */
int qed_mcp_set_capabilities(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * @brief Read ufp config from the shared memory.
 *
 * @param p_hwfn
 * @param p_ptt
 */
void qed_mcp_read_ufp_config(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * @brief Populate the nvm info shadow in the given hardware function
 *
 * @param p_hwfn
 */
int qed_mcp_nvm_info_populate(struct qed_hwfn *p_hwfn);

#endif
