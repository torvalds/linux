/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
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

#define QED_MFW_REPORT_STR_SIZE	256

struct qed_mcp_link_speed_params {
	bool					autoneg;

	u32					advertised_speeds;
#define QED_EXT_SPEED_MASK_RES			0x1
#define QED_EXT_SPEED_MASK_1G			0x2
#define QED_EXT_SPEED_MASK_10G			0x4
#define QED_EXT_SPEED_MASK_20G			0x8
#define QED_EXT_SPEED_MASK_25G			0x10
#define QED_EXT_SPEED_MASK_40G			0x20
#define QED_EXT_SPEED_MASK_50G_R		0x40
#define QED_EXT_SPEED_MASK_50G_R2		0x80
#define QED_EXT_SPEED_MASK_100G_R2		0x100
#define QED_EXT_SPEED_MASK_100G_R4		0x200
#define QED_EXT_SPEED_MASK_100G_P4		0x400

	u32					forced_speed;	   /* In Mb/s */
#define QED_EXT_SPEED_1G			0x1
#define QED_EXT_SPEED_10G			0x2
#define QED_EXT_SPEED_20G			0x4
#define QED_EXT_SPEED_25G			0x8
#define QED_EXT_SPEED_40G			0x10
#define QED_EXT_SPEED_50G_R			0x20
#define QED_EXT_SPEED_50G_R2			0x40
#define QED_EXT_SPEED_100G_R2			0x80
#define QED_EXT_SPEED_100G_R4			0x100
#define QED_EXT_SPEED_100G_P4			0x200
};

struct qed_mcp_link_pause_params {
	bool					autoneg;
	bool					forced_rx;
	bool					forced_tx;
};

enum qed_mcp_eee_mode {
	QED_MCP_EEE_DISABLED,
	QED_MCP_EEE_ENABLED,
	QED_MCP_EEE_UNSUPPORTED
};

struct qed_mcp_link_params {
	struct qed_mcp_link_speed_params	speed;
	struct qed_mcp_link_pause_params	pause;
	u32					loopback_mode;
	struct qed_link_eee_params		eee;
	u32					fec;

	struct qed_mcp_link_speed_params	ext_speed;
	u32					ext_fec_mode;
};

struct qed_mcp_link_capabilities {
	u32					speed_capabilities;
	bool					default_speed_autoneg;
	u32					fec_default;
	enum qed_mcp_eee_mode			default_eee;
	u32					eee_lpi_timer;
	u8					eee_speed_caps;

	u32					default_ext_speed_caps;
	u32					default_ext_autoneg;
	u32					default_ext_speed;
	u32					default_ext_fec;
};

struct qed_mcp_link_state {
	bool					link_up;
	u32					min_pf_rate;

	/* Actual link speed in Mb/s */
	u32					line_speed;

	/* PF max speed in Mb/s, deduced from line_speed
	 * according to PF max bandwidth configuration.
	 */
	u32					speed;

	bool					full_duplex;
	bool					an;
	bool					an_complete;
	bool					parallel_detection;
	bool					pfc_enabled;

	u32					partner_adv_speed;
#define QED_LINK_PARTNER_SPEED_1G_HD		BIT(0)
#define QED_LINK_PARTNER_SPEED_1G_FD		BIT(1)
#define QED_LINK_PARTNER_SPEED_10G		BIT(2)
#define QED_LINK_PARTNER_SPEED_20G		BIT(3)
#define QED_LINK_PARTNER_SPEED_25G		BIT(4)
#define QED_LINK_PARTNER_SPEED_40G		BIT(5)
#define QED_LINK_PARTNER_SPEED_50G		BIT(6)
#define QED_LINK_PARTNER_SPEED_100G		BIT(7)

	bool					partner_tx_flow_ctrl_en;
	bool					partner_rx_flow_ctrl_en;

	u8					partner_adv_pause;
#define QED_LINK_PARTNER_SYMMETRIC_PAUSE	0x1
#define QED_LINK_PARTNER_ASYMMETRIC_PAUSE	0x2
#define QED_LINK_PARTNER_BOTH_PAUSE		0x3

	bool					sfp_tx_fault;
	bool					eee_active;
	u8					eee_adv_caps;
	u8					eee_lp_adv_caps;

	u32					fec_active;
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

#define QED_NVM_CFG_OPTION_ALL		BIT(0)
#define QED_NVM_CFG_OPTION_INIT		BIT(1)
#define QED_NVM_CFG_OPTION_COMMIT       BIT(2)
#define QED_NVM_CFG_OPTION_FREE		BIT(3)
#define QED_NVM_CFG_OPTION_ENTITY_SEL	BIT(4)

/**
 * qed_mcp_get_link_params(): Returns the link params of the hw function.
 *
 * @p_hwfn: HW device data.
 *
 * Returns: Pointer to link params.
 */
struct qed_mcp_link_params *qed_mcp_get_link_params(struct qed_hwfn *p_hwfn);

/**
 * qed_mcp_get_link_state(): Return the link state of the hw function.
 *
 * @p_hwfn: HW device data.
 *
 * Returns: Pointer to link state.
 */
struct qed_mcp_link_state *qed_mcp_get_link_state(struct qed_hwfn *p_hwfn);

/**
 * qed_mcp_get_link_capabilities(): Return the link capabilities of the
 *                                  hw function.
 *
 * @p_hwfn: HW device data.
 *
 * Returns: Pointer to link capabilities.
 */
struct qed_mcp_link_capabilities
	*qed_mcp_get_link_capabilities(struct qed_hwfn *p_hwfn);

/**
 * qed_mcp_set_link(): Request the MFW to set the link according
 *                     to 'link_input'.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @b_up: Raise link if `true'. Reset link if `false'.
 *
 * Return: Int.
 */
int qed_mcp_set_link(struct qed_hwfn   *p_hwfn,
		     struct qed_ptt     *p_ptt,
		     bool               b_up);

/**
 * qed_mcp_get_mfw_ver(): Get the management firmware version value.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_mfw_ver: MFW version value.
 * @p_running_bundle_id: Image id in nvram; Optional.
 *
 * Return: Int - 0 - operation was successful.
 */
int qed_mcp_get_mfw_ver(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt,
			u32 *p_mfw_ver, u32 *p_running_bundle_id);

/**
 * qed_mcp_get_mbi_ver(): Get the MBI version value.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_mbi_ver: A pointer to a variable to be filled with the MBI version.
 *
 * Return: Int - 0 - operation was successful.
 */
int qed_mcp_get_mbi_ver(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt, u32 *p_mbi_ver);

/**
 * qed_mcp_get_media_type(): Get media type value of the port.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @media_type: Media type value
 *
 * Return: Int - 0 - Operation was successul.
 *              -EBUSY - Operation failed
 */
int qed_mcp_get_media_type(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, u32 *media_type);

/**
 * qed_mcp_get_transceiver_data(): Get transceiver data of the port.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_transceiver_state: Transceiver state.
 * @p_tranceiver_type: Media type value.
 *
 * Return: Int - 0 - Operation was successul.
 *              -EBUSY - Operation failed
 */
int qed_mcp_get_transceiver_data(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 u32 *p_transceiver_state,
				 u32 *p_tranceiver_type);

/**
 * qed_mcp_trans_speed_mask(): Get transceiver supported speed mask.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_speed_mask: Bit mask of all supported speeds.
 *
 * Return: Int - 0 - Operation was successul.
 *              -EBUSY - Operation failed
 */

int qed_mcp_trans_speed_mask(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt, u32 *p_speed_mask);

/**
 * qed_mcp_get_board_config(): Get board configuration.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_board_config: Board config.
 *
 * Return: Int - 0 - Operation was successul.
 *              -EBUSY - Operation failed
 */
int qed_mcp_get_board_config(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt, u32 *p_board_config);

/**
 * qed_mcp_cmd(): Sleepable function for sending commands to the MCP
 *                mailbox. It acquire mutex lock for the entire
 *                operation, from sending the request until the MCP
 *                response. Waiting for MCP response will be checked up
 *                to 5 seconds every 10ms. Should not be called from atomic
 *                context.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 * @cmd: command to be sent to the MCP.
 * @param: Optional param
 * @o_mcp_resp: The MCP response code (exclude sequence).
 * @o_mcp_param: Optional parameter provided by the MCP
 *                     response
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_cmd(struct qed_hwfn *p_hwfn,
		struct qed_ptt *p_ptt,
		u32 cmd,
		u32 param,
		u32 *o_mcp_resp,
		u32 *o_mcp_param);

/**
 * qed_mcp_cmd_nosleep(): Function for sending commands to the MCP
 *                        mailbox. It acquire mutex lock for the entire
 *                        operation, from sending the request until the MCP
 *                        response. Waiting for MCP response will be checked up
 *                        to 5 seconds every 10us. Should be called when sleep
 *                        is not allowed.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 * @cmd: command to be sent to the MCP.
 * @param: Optional param
 * @o_mcp_resp: The MCP response code (exclude sequence).
 * @o_mcp_param: Optional parameter provided by the MCP
 *                     response
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_cmd_nosleep(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt,
			u32 cmd,
			u32 param,
			u32 *o_mcp_resp,
			u32 *o_mcp_param);

/**
 * qed_mcp_drain(): drains the nig, allowing completion to pass in
 *                  case of pauses.
 *                  (Should be called only from sleepable context)
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 *
 * Return: Int.
 */
int qed_mcp_drain(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt);

/**
 * qed_mcp_get_flash_size(): Get the flash size value.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 * @p_flash_size: Flash size in bytes to be filled.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_get_flash_size(struct qed_hwfn     *p_hwfn,
			   struct qed_ptt       *p_ptt,
			   u32 *p_flash_size);

/**
 * qed_mcp_send_drv_version(): Send driver version to MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 * @p_ver: Version value.
 *
 * Return: Int - 0 - Operation was successul.
 */
int
qed_mcp_send_drv_version(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct qed_mcp_drv_version *p_ver);

/**
 * qed_get_process_kill_counter(): Read the MFW process kill counter.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 *
 * Return: u32.
 */
u32 qed_get_process_kill_counter(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt);

/**
 * qed_start_recovery_process(): Trigger a recovery process.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 *
 * Return: Int.
 */
int qed_start_recovery_process(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_recovery_prolog(): A recovery handler must call this function
 *                        as its first step.
 *                        It is assumed that the handler is not run from
 *                        an interrupt context.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: int.
 */
int qed_recovery_prolog(struct qed_dev *cdev);

/**
 * qed_mcp_ov_update_current_config(): Notify MFW about the change in base
 *                                    device properties
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @client: Qed client type.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_ov_update_current_config(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     enum qed_ov_client client);

/**
 * qed_mcp_ov_update_driver_state(): Notify MFW about the driver state.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @drv_state: Driver state.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_ov_update_driver_state(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   enum qed_ov_driver_state drv_state);

/**
 * qed_mcp_ov_update_mtu(): Send MTU size to MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @mtu: MTU size.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_ov_update_mtu(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u16 mtu);

/**
 * qed_mcp_ov_update_mac(): Send MAC address to MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @mac: MAC address.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_ov_update_mac(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, const u8 *mac);

/**
 * qed_mcp_ov_update_wol(): Send WOL mode to MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @wol: WOL mode.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_ov_update_wol(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  enum qed_ov_wol wol);

/**
 * qed_mcp_set_led(): Set LED status.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @mode: LED mode.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_set_led(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    enum qed_led_mode mode);

/**
 * qed_mcp_nvm_read(): Read from NVM.
 *
 * @cdev: Qed dev pointer.
 * @addr: NVM offset.
 * @p_buf: NVM read buffer.
 * @len: Buffer len.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_nvm_read(struct qed_dev *cdev, u32 addr, u8 *p_buf, u32 len);

/**
 * qed_mcp_nvm_write(): Write to NVM.
 *
 * @cdev: Qed dev pointer.
 * @addr: NVM offset.
 * @cmd: NVM command.
 * @p_buf: NVM write buffer.
 * @len: Buffer len.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_nvm_write(struct qed_dev *cdev,
		      u32 cmd, u32 addr, u8 *p_buf, u32 len);

/**
 * qed_mcp_nvm_resp(): Check latest response.
 *
 * @cdev: Qed dev pointer.
 * @p_buf: NVM write buffer.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_nvm_resp(struct qed_dev *cdev, u8 *p_buf);

struct qed_nvm_image_att {
	u32 start_addr;
	u32 length;
};

/**
 * qed_mcp_get_nvm_image_att(): Allows reading a whole nvram image.
 *
 * @p_hwfn: HW device data.
 * @image_id: Image to get attributes for.
 * @p_image_att: Image attributes structure into which to fill data.
 *
 * Return: Int - 0 - Operation was successul.
 */
int
qed_mcp_get_nvm_image_att(struct qed_hwfn *p_hwfn,
			  enum qed_nvm_images image_id,
			  struct qed_nvm_image_att *p_image_att);

/**
 * qed_mcp_get_nvm_image(): Allows reading a whole nvram image.
 *
 * @p_hwfn: HW device data.
 * @image_id: image requested for reading.
 * @p_buffer: allocated buffer into which to fill data.
 * @buffer_len: length of the allocated buffer.
 *
 * Return: 0 if p_buffer now contains the nvram image.
 */
int qed_mcp_get_nvm_image(struct qed_hwfn *p_hwfn,
			  enum qed_nvm_images image_id,
			  u8 *p_buffer, u32 buffer_len);

/**
 * qed_mcp_bist_register_test(): Bist register test.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_bist_register_test(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt);

/**
 * qed_mcp_bist_clock_test(): Bist clock test.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_bist_clock_test(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt);

/**
 * qed_mcp_bist_nvm_get_num_images(): Bist nvm test - get number of images.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 * @num_images: number of images if operation was
 *			  successful. 0 if not.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_bist_nvm_get_num_images(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    u32 *num_images);

/**
 * qed_mcp_bist_nvm_get_image_att(): Bist nvm test - get image attributes
 *                                   by index.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 * @p_image_att: Attributes of image.
 * @image_index: Index of image to get information for.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_bist_nvm_get_image_att(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct bist_nvm_image_att *p_image_att,
				   u32 image_index);

/**
 * qed_mfw_process_tlv_req(): Processes the TLV request from MFW i.e.,
 *                            get the required TLV info
 *                            from the qed client and send it to the MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: 0 upon success.
 */
int qed_mfw_process_tlv_req(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_send_raw_debug_data(): Send raw debug data to the MFW
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_buf: raw debug data buffer.
 * @size: Buffer size.
 *
 * Return : Int.
 */
int
qed_mcp_send_raw_debug_data(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u8 *p_buf, u32 size);

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

	/* S/N for debug data mailbox commands */
	atomic_t dbg_data_seq;

	/* Spinlock used to sync the flag mcp_handling_status with
	 * the mfw events handler
	 */
	spinlock_t unload_lock;
	unsigned long mcp_handling_status;
#define QED_MCP_BYPASS_PROC_BIT 0
#define QED_MCP_IN_PROCESSING_BIT       1
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
 * qed_mcp_is_ext_speed_supported() - Check if management firmware supports
 *                                    extended speeds.
 * @p_hwfn: HW device data.
 *
 * Return: true if supported, false otherwise.
 */
static inline bool
qed_mcp_is_ext_speed_supported(const struct qed_hwfn *p_hwfn)
{
	return !!(p_hwfn->mcp_info->capabilities &
		  FW_MB_PARAM_FEATURE_SUPPORT_EXT_SPEED_FEC_CONTROL);
}

/**
 * qed_mcp_cmd_init(): Initialize the interface with the MCP.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 *
 * Return: Int.
 */
int qed_mcp_cmd_init(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt);

/**
 * qed_mcp_cmd_port_init(): Initialize the port interface with the MCP
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Void.
 *
 * Can only be called after `num_ports_in_engines' is set
 */
void qed_mcp_cmd_port_init(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt);
/**
 * qed_mcp_free(): Releases resources allocated during the init process.
 *
 * @p_hwfn: HW function.
 *
 * Return: Int.
 */

int qed_mcp_free(struct qed_hwfn *p_hwfn);

/**
 * qed_mcp_handle_events(): This function is called from the DPC context.
 *           After pointing PTT to the mfw mb, check for events sent by
 *           the MCP to the driver and ack them. In case a critical event
 *           detected, it will be handled here, otherwise the work will be
 *            queued to a sleepable work-queue.
 *
 * @p_hwfn: HW function.
 * @p_ptt: PTT required for register access.
 *
 * Return: Int - 0 - Operation was successul.
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
 * qed_mcp_load_req(): Sends a LOAD_REQ to the MFW, and in case the
 *                     operation succeeds, returns whether this PF is
 *                     the first on the engine/port or function.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_params: Params.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_load_req(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     struct qed_load_req_params *p_params);

/**
 * qed_mcp_load_done(): Sends a LOAD_DONE message to the MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_load_done(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_unload_req(): Sends a UNLOAD_REQ message to the MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_unload_req(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_unload_done(): Sends a UNLOAD_DONE message to the MFW
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_unload_done(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_read_mb(): Read the MFW mailbox into Current buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Void.
 */
void qed_mcp_read_mb(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt);

/**
 * qed_mcp_ack_vf_flr(): Ack to mfw that driver finished FLR process for VFs
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @vfs_to_ack: bit mask of all engine VFs for which the PF acks.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_ack_vf_flr(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt, u32 *vfs_to_ack);

/**
 * qed_mcp_fill_shmem_func_info(): Calls during init to read shmem of
 *                                 all function-related info.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: 0 upon success.
 */
int qed_mcp_fill_shmem_func_info(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt);

/**
 * qed_mcp_reset(): Reset the MCP using mailbox command.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: 0 upon success.
 */
int qed_mcp_reset(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt);

/**
 * qed_mcp_nvm_rd_cmd(): Sends an NVM read command request to the MFW to get
 *                       a buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @cmd: (Command) DRV_MSG_CODE_NVM_GET_FILE_DATA or
 *            DRV_MSG_CODE_NVM_READ_NVRAM commands.
 * @param: [0:23] - Offset [24:31] - Size.
 * @o_mcp_resp: MCP response.
 * @o_mcp_param: MCP response param.
 * @o_txn_size: Buffer size output.
 * @o_buf: Pointer to the buffer returned by the MFW.
 * @b_can_sleep: Can sleep.
 *
 * Return: 0 upon success.
 */
int qed_mcp_nvm_rd_cmd(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       u32 cmd,
		       u32 param,
		       u32 *o_mcp_resp,
		       u32 *o_mcp_param,
		       u32 *o_txn_size, u32 *o_buf, bool b_can_sleep);

/**
 * qed_mcp_phy_sfp_read(): Read from sfp.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: PTT required for register access.
 * @port: transceiver port.
 * @addr: I2C address.
 * @offset: offset in sfp.
 * @len: buffer length.
 * @p_buf: buffer to read into.
 *
 * Return: Int - 0 - Operation was successul.
 */
int qed_mcp_phy_sfp_read(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			 u32 port, u32 addr, u32 offset, u32 len, u8 *p_buf);

/**
 * qed_mcp_is_init(): indicates whether the MFW objects [under mcp_info]
 *                    are accessible
 *
 * @p_hwfn: HW device data.
 *
 * Return: true if MFW is running and mcp_info is initialized.
 */
bool qed_mcp_is_init(struct qed_hwfn *p_hwfn);

/**
 * qed_mcp_config_vf_msix(): Request MFW to configure MSI-X for a VF.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @vf_id: absolute inside engine.
 * @num: number of entries to request.
 *
 * Return: Int.
 */
int qed_mcp_config_vf_msix(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, u8 vf_id, u8 num);

/**
 * qed_mcp_halt(): Halt the MCP.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: 0 upon success.
 */
int qed_mcp_halt(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_resume: Wake up the MCP.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: 0 upon success.
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

/* qed_mcp_mdump_get_retain(): Gets the mdump retained data from the MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_mdump_retain: mdump retain.
 *
 * Return: Int - 0 - Operation was successul.
 */
int
qed_mcp_mdump_get_retain(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct mdump_retain_data_stc *p_mdump_retain);

/**
 * qed_mcp_set_resc_max_val(): Sets the MFW's max value for the given resource.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @res_id: RES ID.
 * @resc_max_val: Resec max val.
 * @p_mcp_resp: MCP Resp
 *
 * Return: Int - 0 - Operation was successul.
 */
int
qed_mcp_set_resc_max_val(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 enum qed_resources res_id,
			 u32 resc_max_val, u32 *p_mcp_resp);

/**
 * qed_mcp_get_resc_info(): Gets the MFW allocation info for the given
 *                          resource.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @res_id: Res ID.
 * @p_mcp_resp: MCP resp.
 * @p_resc_num: Resc num.
 * @p_resc_start: Resc start.
 *
 * Return: Int - 0 - Operation was successul.
 */
int
qed_mcp_get_resc_info(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      enum qed_resources res_id,
		      u32 *p_mcp_resp, u32 *p_resc_num, u32 *p_resc_start);

/**
 * qed_mcp_ov_update_eswitch(): Send eswitch mode to MFW.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @eswitch: eswitch mode.
 *
 * Return: Int - 0 - Operation was successul.
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
 * qed_mcp_initiate_pf_flr(): Initiates PF FLR.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int - 0 - Operation was successul.
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
 * qed_mcp_resc_lock(): Acquires MFW generic resource lock.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_params: Params.
 *
 * Return: Int - 0 - Operation was successul.
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
 * qed_mcp_resc_unlock(): Releases MFW generic resource lock.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_params: Params.
 *
 * Return: Int - 0 - Operation was successul.
 */
int
qed_mcp_resc_unlock(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_resc_unlock_params *p_params);

/**
 * qed_mcp_resc_lock_default_init(): Default initialization for
 *                                   lock/unlock resource structs.
 *
 * @p_lock: lock params struct to be initialized; Can be NULL.
 * @p_unlock: unlock params struct to be initialized; Can be NULL.
 * @resource: the requested resource.
 * @b_is_permanent: disable retries & aging when set.
 *
 * Return: Void.
 */
void qed_mcp_resc_lock_default_init(struct qed_resc_lock_params *p_lock,
				    struct qed_resc_unlock_params *p_unlock,
				    enum qed_resc_lock
				    resource, bool b_is_permanent);

/**
 * qed_mcp_is_smart_an_supported(): Return whether management firmware
 *                                  support smart AN
 *
 * @p_hwfn: HW device data.
 *
 * Return: bool true if feature is supported.
 */
bool qed_mcp_is_smart_an_supported(struct qed_hwfn *p_hwfn);

/**
 * qed_mcp_get_capabilities(): Learn of supported MFW features;
 *                             To be done during early init.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int.
 */
int qed_mcp_get_capabilities(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_set_capabilities(): Inform MFW of set of features supported
 *                             by driver. Should be done inside the content
 *                             of the LOAD_REQ.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int.
 */
int qed_mcp_set_capabilities(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_read_ufp_config(): Read ufp config from the shared memory.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Void.
 */
void qed_mcp_read_ufp_config(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_nvm_info_populate(): Populate the nvm info shadow in the given
 *                              hardware function.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Int.
 */
int qed_mcp_nvm_info_populate(struct qed_hwfn *p_hwfn);

/**
 * qed_mcp_nvm_info_free(): Delete nvm info shadow in the given
 *                          hardware function.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_mcp_nvm_info_free(struct qed_hwfn *p_hwfn);

/**
 * qed_mcp_get_engine_config(): Get the engine affinity configuration.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int.
 */
int qed_mcp_get_engine_config(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_get_ppfid_bitmap(): Get the PPFID bitmap.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 *
 * Return: Int.
 */
int qed_mcp_get_ppfid_bitmap(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * qed_mcp_nvm_get_cfg(): Get NVM config attribute value.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @option_id: Option ID.
 * @entity_id: Entity ID.
 * @flags: Flags.
 * @p_buf: Buf.
 * @p_len: Len.
 *
 * Return: Int.
 */
int qed_mcp_nvm_get_cfg(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			u16 option_id, u8 entity_id, u16 flags, u8 *p_buf,
			u32 *p_len);

/**
 * qed_mcp_nvm_set_cfg(): Set NVM config attribute value.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @option_id: Option ID.
 * @entity_id: Entity ID.
 * @flags: Flags.
 * @p_buf: Buf.
 * @len: Len.
 *
 * Return: Int.
 */
int qed_mcp_nvm_set_cfg(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			u16 option_id, u8 entity_id, u16 flags, u8 *p_buf,
			u32 len);

/**
 * qed_mcp_is_esl_supported(): Return whether management firmware support ESL or not.
 *
 * @p_hwfn: hw function pointer
 *
 * Return: true if esl is supported, otherwise return false
 */
bool qed_mcp_is_esl_supported(struct qed_hwfn *p_hwfn);

/**
 * qed_mcp_get_esl_status(): Get enhanced system lockdown status
 *
 * @p_hwfn: hw function pointer
 * @p_ptt: ptt resource pointer
 * @active: ESL active status data pointer
 *
 * Return: 0 with esl status info on success, otherwise return error
 */
int qed_mcp_get_esl_status(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, bool *active);
#endif
