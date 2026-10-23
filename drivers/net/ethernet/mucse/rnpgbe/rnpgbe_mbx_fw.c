// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#include <linux/if_ether.h>
#include <linux/bitfield.h>
#include <linux/pci.h>

#include "rnpgbe.h"
#include "rnpgbe_mbx.h"
#include "rnpgbe_mbx_fw.h"

/**
 * mucse_fw_send_cmd_wait_resp - Send cmd req and wait for response
 * @hw: pointer to the HW structure
 * @req: pointer to the cmd req structure
 * @reply: pointer to the fw reply structure
 *
 * mucse_fw_send_cmd_wait_resp sends req to pf-fw mailbox and wait
 * reply from fw.
 *
 * Return: 0 on success, negative errno on failure
 **/
static int mucse_fw_send_cmd_wait_resp(struct mucse_hw *hw,
				       union mbx_fw_cmd_req_u *req,
				       union mbx_fw_cmd_reply_u *reply)
{
	int len = le16_to_cpu(req->r.datalen);
	int retry_cnt = 3;
	int err;

	mutex_lock(&hw->mbx.lock);
	err = mucse_write_and_wait_ack_mbx(hw, req->dwords, len);
	if (err)
		goto out;
	do {
		err = mucse_poll_and_read_mbx(hw, reply->dwords,
					      sizeof(reply->r));
		if (err)
			goto out;
		/* mucse_write_and_wait_ack_mbx return 0 means fw has
		 * received request, wait for the expect opcode
		 * reply with 'retry_cnt' times.
		 */
	} while (--retry_cnt >= 0 && reply->r.opcode != req->r.opcode);
out:
	mutex_unlock(&hw->mbx.lock);
	if (!err && retry_cnt < 0)
		return -ETIMEDOUT;
	if (!err && reply->r.error_code)
		return -EIO;

	return err;
}

/**
 * mucse_mbx_get_info - Get hw info from fw
 * @hw: pointer to the HW structure
 *
 * mucse_mbx_get_info tries to get hw info from hw.
 *
 * Return: 0 on success, negative errno on failure
 **/
static int mucse_mbx_get_info(struct mucse_hw *hw)
{
	union mbx_fw_cmd_req_u req = {
		.r = {
			.datalen = cpu_to_le16(MUCSE_MBX_REQ_HDR_LEN),
			.opcode  = cpu_to_le16(GET_HW_INFO),
		},
	};
	union mbx_fw_cmd_reply_u reply = {};
	int err;

	err = mucse_fw_send_cmd_wait_resp(hw, &req, &reply);
	if (!err)
		hw->pfvfnum = FIELD_GET(GENMASK_U16(7, 0),
					le16_to_cpu(reply.r.hw_info.pfnum));

	return err;
}

/**
 * mucse_mbx_sync_fw - Try to sync with fw
 * @hw: pointer to the HW structure
 *
 * mucse_mbx_sync_fw tries to sync with fw. It is only called in
 * probe. Nothing (register network) todo if failed.
 * Try more times to do sync.
 *
 * Return: 0 on success, negative errno on failure
 **/
int mucse_mbx_sync_fw(struct mucse_hw *hw)
{
	int try_cnt = 3;
	int err;

	do {
		err = mucse_mbx_get_info(hw);
	} while (err == -ETIMEDOUT && try_cnt--);

	return err;
}

/**
 * mucse_mbx_powerup - Echo fw to powerup
 * @hw: pointer to the HW structure
 * @is_powerup: true for powerup, false for powerdown
 *
 * mucse_mbx_powerup echo fw to change working frequency
 * to normal after received true, and reduce working frequency
 * if false.
 *
 * Return: 0 on success, negative errno on failure
 **/
int mucse_mbx_powerup(struct mucse_hw *hw, bool is_powerup)
{
	union mbx_fw_cmd_req_u req = {
		.r = {
			.datalen = cpu_to_le16(sizeof(req.r.powerup) +
					       MUCSE_MBX_REQ_HDR_LEN),
			.opcode  = cpu_to_le16(POWER_UP),
			.powerup = {
				/* fw needs this to reply correct cmd */
				.version = cpu_to_le32(GENMASK_U32(31, 0)),
				.status  = cpu_to_le32(is_powerup ? 1 : 0),
			},
		},
	};
	int len, err;

	len = le16_to_cpu(req.r.datalen);
	mutex_lock(&hw->mbx.lock);
	err = mucse_write_and_wait_ack_mbx(hw, req.dwords, len);
	mutex_unlock(&hw->mbx.lock);

	return err;
}

/**
 * mucse_mbx_reset_hw - Posts a mbx req to reset hw
 * @hw: pointer to the HW structure
 *
 * mucse_mbx_reset_hw posts a mbx req to firmware to reset hw.
 * We use mucse_fw_send_cmd_wait_resp to wait hw reset ok.
 *
 * Return: 0 on success, negative errno on failure
 **/
int mucse_mbx_reset_hw(struct mucse_hw *hw)
{
	union mbx_fw_cmd_req_u req = {
		.r = {
			.datalen = cpu_to_le16(MUCSE_MBX_REQ_HDR_LEN),
			.opcode  = cpu_to_le16(RESET_HW),
		},
	};
	union mbx_fw_cmd_reply_u reply = {};

	return mucse_fw_send_cmd_wait_resp(hw, &req, &reply);
}

/**
 * mucse_mbx_get_macaddr - Posts a mbx req to request macaddr
 * @hw: pointer to the HW structure
 * @pfvfnum: index of pf/vf num
 * @mac_addr: pointer to store mac_addr
 * @port: port index
 *
 * mucse_mbx_get_macaddr posts a mbx req to firmware to get mac_addr.
 *
 * Return: 0 on success, negative errno on failure
 **/
int mucse_mbx_get_macaddr(struct mucse_hw *hw, int pfvfnum,
			  u8 *mac_addr,
			  int port)
{
	union mbx_fw_cmd_req_u req = {
		.r = {
			.datalen      = cpu_to_le16(sizeof(req.r.get_mac_addr) +
						    MUCSE_MBX_REQ_HDR_LEN),
			.opcode       = cpu_to_le16(GET_MAC_ADDRESS),
			.get_mac_addr = {
				.port_mask = cpu_to_le32(BIT(port)),
				.pfvf_num  = cpu_to_le32(pfvfnum),
			},
		},
	};
	union mbx_fw_cmd_reply_u reply = {};
	int err;

	err = mucse_fw_send_cmd_wait_resp(hw, &req, &reply);
	if (err)
		return err;

	if (le32_to_cpu(reply.r.mac_addr.ports) & BIT(port))
		memcpy(mac_addr, reply.r.mac_addr.addrs[port].mac, ETH_ALEN);
	else
		return -ENODATA;

	return 0;
}

/**
 * mucse_mbx_set_link - Configure firmware link settings
 * @hw: pointer to the HW structure
 * @advertising: firmware link-mode advertisement mask
 * @autoneg: whether to enable autonegotiation
 * @speed: forced link speed when autonegotiation is disabled
 * @duplex: forced duplex mode when autonegotiation is disabled
 * @mdix_ctrl: MDI/MDI-X control mode
 *
 * Firmware ignores @speed, @duplex when @autoneg is true.
 *
 * Return: 0 on success, negative errno on failure
 **/
int mucse_mbx_set_link(struct mucse_hw *hw, u32 advertising, bool autoneg,
		       u32 speed, u32 duplex, u32 mdix_ctrl)
{
	union mbx_fw_cmd_req_u req = {
		.r = {
			.datalen = cpu_to_le16(sizeof(req.r.phy_link_set) +
					       MUCSE_MBX_REQ_HDR_LEN),
			.opcode = cpu_to_le16(PHY_LINK_SET),
			.phy_link_set = {
				.adv_speed_mask = cpu_to_le32(advertising),
				.autoneg = cpu_to_le32(autoneg),
				.speed = cpu_to_le32(speed),
				.duplex = cpu_to_le32(duplex),
				.nr_lane = cpu_to_le32(hw->port),
				.tp_mdix_ctrl = cpu_to_le32(mdix_ctrl),
			},
		},
	};
	int len, err;

	len = le16_to_cpu(req.r.datalen);
	mutex_lock(&hw->mbx.lock);
	err = mucse_write_and_wait_ack_mbx(hw, req.dwords, len);
	mutex_unlock(&hw->mbx.lock);

	return err;
}

/**
 * mucse_mbx_phyup - Request that firmware bring the PHY up or down
 * @hw: pointer to the HW structure
 * @is_phyup: true for up, false for down
 *
 * mucse_mbx_phyup echo fw to change phy status
 *
 * Return: 0 on success, negative errno on failure
 **/
int mucse_mbx_phyup(struct mucse_hw *hw, bool is_phyup)
{
	union mbx_fw_cmd_req_u req = {
		.r = {
			.datalen = cpu_to_le16(sizeof(req.r.phy_status) +
					       MUCSE_MBX_REQ_HDR_LEN),
			.opcode  = cpu_to_le16(SET_PHY_UP),
			.phy_status = {
				.port_mask = cpu_to_le32(BIT(hw->port)),
				.status  = cpu_to_le32(is_phyup ? 1 : 0),
			},
		},
	};
	int len, err;

	len = le16_to_cpu(req.r.datalen);
	mutex_lock(&hw->mbx.lock);
	err = mucse_write_mbx_coalesce_event(hw, req.dwords, len);
	mutex_unlock(&hw->mbx.lock);

	return err;
}

/**
 * mucse_mbx_link_report - Configure firmware link-change event reporting
 * @hw: pointer to the HW structure
 * @is_report: true for report, false for no
 *
 * mucse_mbx_link_report echo fw to change event report state
 *
 * Return: 0 on success, negative errno on failure
 **/
int mucse_mbx_link_report(struct mucse_hw *hw, bool is_report)
{
	union mbx_fw_cmd_req_u req = {
		.r = {
			.datalen = cpu_to_le16(sizeof(req.r.report_status) +
					       MUCSE_MBX_REQ_HDR_LEN),
			.opcode  = cpu_to_le16(LINK_REPORT_EN),
			.report_status = {
				.port_mask = cpu_to_le16(BIT(hw->port)),
				.status  = cpu_to_le16(is_report ? 1 : 0),
			},
		},
	};
	int len, err;

	len = le16_to_cpu(req.r.datalen);
	mutex_lock(&hw->mbx.lock);
	err = mucse_write_mbx_coalesce_event(hw, req.dwords, len);
	mutex_unlock(&hw->mbx.lock);

	return err;
}

static bool mucse_link_speed_valid(const struct mbx_fw_cmd_req *req)
{
	switch (le16_to_cpu(req->link_stat.st.speed)) {
	case 10:
	case 100:
	case 1000:
		return true;
	default:
		return false;
	}
}

static bool mucse_link_is_up(const struct mucse_hw *hw,
			     const struct mbx_fw_cmd_req *req)
{
	return le16_to_cpu(req->link_stat.port_status) & BIT(hw->port);
}

/**
 * mucse_update_link_status_reg - update driver speed inf to reg
 * @hw: pointer to the HW structure
 * @req: pointer to req data
 *
 * Update the driver's link-state snapshot exported to firmware. Firmware
 * sends a new event when this snapshot differs from the hardware state.
 * The default snapshot clears the driver-reported fields;
 * a valid event then repopulates them, including the LLDP status in bit 6.
 *
 **/
static void mucse_update_link_status_reg(struct mucse_hw *hw,
					 struct mbx_fw_cmd_req *req)
{
	u16 status = le16_to_cpu(req->link_stat.st.status);
	u16 speed = le16_to_cpu(req->link_stat.st.speed);
	u32 value;

	value = M_DEFAULT_ST;

	if (mucse_link_is_up(hw, req)) {
		value |= BIT(0);
		switch (speed) {
		case 10:
			value |= (mucse_speed_10 << 8);
			break;
		case 100:
			value |= (mucse_speed_100 << 8);
			break;
		case 1000:
			value |= (mucse_speed_1000 << 8);
			break;
		default:
			break;
		}

		value |= FIELD_PREP(BIT(4),
				    !!(req->link_stat.st.flags & DUPLEX_BIT));
		value |= FIELD_PREP(GENMASK_U32(25, 24),
				    status & GENMASK(1, 0));
	} else {
		value &= ~BIT(0);
	}

	if (status & ST_STATUS_LLDP_STATUS_MASK)
		value |= BIT(6);
	else
		value &= ~BIT(6);

	mucse_hw_wr32(hw, RNPGBE_LINK_ST, value);
}

/**
 * mucse_mbx_fw_req_handler - Handle fw req
 * @hw: pointer to the HW structure
 * @req: pointer to req data
 *
 * mucse_mbx_fw_req_handler handler fw req, such as a link event req.
 **/
static void mucse_mbx_fw_req_handler(struct mucse_hw *hw,
				     struct mbx_fw_cmd_req *req)
{
	struct mucse *mucse = container_of(hw, struct mucse, hw);
	u32 magic = le32_to_cpu(req->link_stat.port_magic);
	unsigned long flags;

	if (le16_to_cpu(req->opcode) == LINK_CHANGE_EVT) {
		u16 speed = le16_to_cpu(req->link_stat.st.speed);

		spin_lock_irqsave(&mucse->link_lock, flags);
		if (magic != ST_VALID_MAGIC) {
			/* Do not change the cached state for an invalid event.
			 * Use an invalid speed encoding to make firmware report
			 * again.
			 */
			mucse_hw_wr32(hw, RNPGBE_LINK_ST, M_INVALID_ST);
			spin_unlock_irqrestore(&mucse->link_lock, flags);
			return;
		}

		if (mucse_link_is_up(hw, req) &&
		    !mucse_link_speed_valid(req)) {
			/* Do not acknowledge an invalid speed as valid.
			 * Keep the snapshot mismatched so firmware retries it.
			 * Firmware limits link reports to one per 500 ms.
			 */
			mucse_hw_wr32(hw, RNPGBE_LINK_ST, M_INVALID_ST);
			spin_unlock_irqrestore(&mucse->link_lock, flags);
			dev_warn_ratelimited(&hw->pdev->dev,
					     "unsupported link speed %u Mbps\n",
					     speed);
			return;
		}

		if (!mucse->link_event_ready) {
			rnpgbe_set_link(hw, false);
			mucse_update_link_status_reg(hw, req);
			spin_unlock_irqrestore(&mucse->link_lock, flags);
			return;
		}

		if (mucse_link_is_up(hw, req))
			hw->link = true;
		else
			hw->link = false;

		hw->speed = le16_to_cpu(req->link_stat.st.speed);
		hw->duplex = req->link_stat.st.flags & DUPLEX_BIT;
		rnpgbe_set_link(hw, mucse_link_is_up(hw, req));
		/* update regs to notify link info is received */
		mucse_update_link_status_reg(hw, req);
		mucse->link_pending = true;
		/* Run link handling immediately. */
		mod_delayed_work(system_percpu_wq, &mucse->serv_task, 0);
		spin_unlock_irqrestore(&mucse->link_lock, flags);
	}
}

/**
 * mucse_fw_handle_event - Handle one pending firmware event
 * @hw: pointer to the hardware structure
 **/
static void mucse_fw_handle_event(struct mucse_hw *hw)
{
	union mbx_fw_cmd_req_u msg = {};
	int err;

	/* try to check and read fw req */
	mutex_lock(&hw->mbx.lock);
	err = mucse_mbx_event_begin(hw, msg.dwords, sizeof(msg));
	/* A PF command may have coalesced the event before stale mailbox work
	 * runs, in which case there is no event left to handle.
	 */
	if (err) {
		mutex_unlock(&hw->mbx.lock);
		if (err != -ENOMSG)
			dev_warn_ratelimited(&hw->pdev->dev,
					     "failed to read firmware event: %d\n",
					     err);
		return;
	}

	mucse_mbx_fw_req_handler(hw, &msg.r);
	mucse_mbx_event_end(hw);
	mutex_unlock(&hw->mbx.lock);
}

/**
 * mucse_fw_irq_handler - Handle one pending firmware mailbox event
 * @hw: pointer to the HW structure
 *
 * Process at most one event per work-item invocation. The caller requeues
 * mailbox work when a dedicated mailbox interrupt arrives during handling.
 **/
void mucse_fw_irq_handler(struct mucse_hw *hw)
{
	mucse_fw_handle_event(hw);
}
