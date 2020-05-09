// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include <linux/iopoll.h>

#include "aq_hw_utils.h"
#include "hw_atl/hw_atl_utils.h"
#include "hw_atl2_utils.h"
#include "hw_atl2_llh.h"
#include "hw_atl2_llh_internal.h"

#define HW_ATL2_FW_VER_1X          0x01000000U

#define AQ_A2_BOOT_STARTED         BIT(0x18)
#define AQ_A2_CRASH_INIT           BIT(0x1B)
#define AQ_A2_BOOT_CODE_FAILED     BIT(0x1C)
#define AQ_A2_FW_INIT_FAILED       BIT(0x1D)
#define AQ_A2_FW_INIT_COMP_SUCCESS BIT(0x1F)

#define AQ_A2_FW_BOOT_FAILED_MASK (AQ_A2_CRASH_INIT | \
				   AQ_A2_BOOT_CODE_FAILED | \
				   AQ_A2_FW_INIT_FAILED)
#define AQ_A2_FW_BOOT_COMPLETE_MASK (AQ_A2_FW_BOOT_FAILED_MASK | \
				     AQ_A2_FW_INIT_COMP_SUCCESS)

#define AQ_A2_FW_BOOT_REQ_REBOOT        BIT(0x0)
#define AQ_A2_FW_BOOT_REQ_HOST_BOOT     BIT(0x8)
#define AQ_A2_FW_BOOT_REQ_MAC_FAST_BOOT BIT(0xA)
#define AQ_A2_FW_BOOT_REQ_PHY_FAST_BOOT BIT(0xB)

int hw_atl2_utils_initfw(struct aq_hw_s *self, const struct aq_fw_ops **fw_ops)
{
	int err;

	self->fw_ver_actual = hw_atl2_utils_get_fw_version(self);

	if (hw_atl_utils_ver_match(HW_ATL2_FW_VER_1X,
				   self->fw_ver_actual) == 0) {
		*fw_ops = &aq_a2_fw_ops;
	} else {
		aq_pr_err("Bad FW version detected: %x, but continue\n",
			  self->fw_ver_actual);
		*fw_ops = &aq_a2_fw_ops;
	}
	aq_pr_trace("Detect ATL2FW %x\n", self->fw_ver_actual);
	self->aq_fw_ops = *fw_ops;
	err = self->aq_fw_ops->init(self);

	self->chip_features |= ATL_HW_CHIP_ANTIGUA;

	return err;
}

static bool hw_atl2_mcp_boot_complete(struct aq_hw_s *self)
{
	u32 rbl_status;

	rbl_status = hw_atl2_mif_mcp_boot_reg_get(self);
	if (rbl_status & AQ_A2_FW_BOOT_COMPLETE_MASK)
		return true;

	/* Host boot requested */
	if (hw_atl2_mif_host_req_int_get(self) & HW_ATL2_MCP_HOST_REQ_INT_READY)
		return true;

	return false;
}

int hw_atl2_utils_soft_reset(struct aq_hw_s *self)
{
	bool rbl_complete = false;
	u32 rbl_status = 0;
	u32 rbl_request;
	int err;

	hw_atl2_mif_host_req_int_clr(self, 0x01);
	rbl_request = AQ_A2_FW_BOOT_REQ_REBOOT;
#ifdef AQ_CFG_FAST_START
	rbl_request |= AQ_A2_FW_BOOT_REQ_MAC_FAST_BOOT;
#endif
	hw_atl2_mif_mcp_boot_reg_set(self, rbl_request);

	/* Wait for RBL boot */
	err = readx_poll_timeout_atomic(hw_atl2_mif_mcp_boot_reg_get, self,
				rbl_status,
				((rbl_status & AQ_A2_BOOT_STARTED) &&
				 (rbl_status != 0xFFFFFFFFu)),
				10, 200000);
	if (err) {
		aq_pr_err("Boot code hanged");
		goto err_exit;
	}

	err = readx_poll_timeout_atomic(hw_atl2_mcp_boot_complete, self,
					rbl_complete,
					rbl_complete,
					10, 2000000);

	if (err) {
		aq_pr_err("FW Restart timed out");
		goto err_exit;
	}

	rbl_status = hw_atl2_mif_mcp_boot_reg_get(self);

	if (rbl_status & AQ_A2_FW_BOOT_FAILED_MASK) {
		err = -EIO;
		aq_pr_err("FW Restart failed");
		goto err_exit;
	}

	if (hw_atl2_mif_host_req_int_get(self) &
	    HW_ATL2_MCP_HOST_REQ_INT_READY) {
		err = -EIO;
		aq_pr_err("No FW detected. Dynamic FW load not implemented");
		goto err_exit;
	}

	if (self->aq_fw_ops) {
		err = self->aq_fw_ops->init(self);
		if (err) {
			aq_pr_err("FW Init failed");
			goto err_exit;
		}
	}

err_exit:
	return err;
}
