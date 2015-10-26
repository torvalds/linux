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
