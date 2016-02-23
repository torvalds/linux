/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_MCP_H
#define _QED_MCP_H

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "qed_hsi.h"

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

struct qed_mcp_link_params {
	struct qed_mcp_link_speed_params	speed;
	struct qed_mcp_link_pause_params	pause;
	u32				     loopback_mode;
};

struct qed_mcp_link_capabilities {
	u32 speed_capabilities;
};

struct qed_mcp_link_state {
	bool    link_up;

	u32     speed; /* In Mb/s */
	bool    full_duplex;

	bool    an;
	bool    an_complete;
	bool    parallel_detection;
	bool    pfc_enabled;

#define QED_LINK_PARTNER_SPEED_1G_HD    BIT(0)
#define QED_LINK_PARTNER_SPEED_1G_FD    BIT(1)
#define QED_LINK_PARTNER_SPEED_10G      BIT(2)
#define QED_LINK_PARTNER_SPEED_20G      BIT(3)
#define QED_LINK_PARTNER_SPEED_40G      BIT(4)
#define QED_LINK_PARTNER_SPEED_50G      BIT(5)
#define QED_LINK_PARTNER_SPEED_100G     BIT(6)
	u32     partner_adv_speed;

	bool    partner_tx_flow_ctrl_en;
	bool    partner_rx_flow_ctrl_en;

#define QED_LINK_PARTNER_SYMMETRIC_PAUSE (1)
#define QED_LINK_PARTNER_ASYMMETRIC_PAUSE (2)
#define QED_LINK_PARTNER_BOTH_PAUSE (3)
	u8      partner_adv_pause;

	bool    sfp_tx_fault;
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
 * @param cdev       - qed dev pointer
 * @param mfw_ver    - mfw version value
 *
 * @return int - 0 - operation was successul.
 */
int qed_mcp_get_mfw_ver(struct qed_dev *cdev,
			u32 *mfw_ver);

/**
 * @brief Get media type value of the port.
 *
 * @param cdev      - qed dev pointer
 * @param mfw_ver    - media type value
 *
 * @return int -
 *      0 - Operation was successul.
 *      -EBUSY - Operation failed
 */
int qed_mcp_get_media_type(struct qed_dev      *cdev,
			   u32                  *media_type);

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

/* TODO - this is only correct as long as only BB is supported, and
 * no port-swapping is implemented; Afterwards we'll need to fix it.
 */
#define MFW_PORT(_p_hwfn)       ((_p_hwfn)->abs_pf_id %	\
				 ((_p_hwfn)->cdev->num_ports_in_engines * 2))
struct qed_mcp_info {
	struct mutex				mutex; /* MCP access lock */
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
	u16					mcp_hist;
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

/**
 * @brief Sends a LOAD_REQ to the MFW, and in case operation
 *        succeed, returns whether this PF is the first on the
 *        chip/engine/port or function. This function should be
 *        called when driver is ready to accept MFW events after
 *        Storms initializations are done.
 *
 * @param p_hwfn       - hw function
 * @param p_ptt        - PTT required for register access
 * @param p_load_code  - The MCP response param containing one
 *      of the following:
 *      FW_MSG_CODE_DRV_LOAD_ENGINE
 *      FW_MSG_CODE_DRV_LOAD_PORT
 *      FW_MSG_CODE_DRV_LOAD_FUNCTION
 * @return int -
 *      0 - Operation was successul.
 *      -EBUSY - Operation failed
 */
int qed_mcp_load_req(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     u32 *p_load_code);

/**
 * @brief Read the MFW mailbox into Current buffer.
 *
 * @param p_hwfn
 * @param p_ptt
 */
void qed_mcp_read_mb(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt);

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
 * @brief indicates whether the MFW objects [under mcp_info] are accessible
 *
 * @param p_hwfn
 *
 * @return true iff MFW is running and mcp_info is initialized
 */
bool qed_mcp_is_init(struct qed_hwfn *p_hwfn);

#endif
