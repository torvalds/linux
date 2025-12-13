// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence MHDP8546 DP bridge driver.
 *
 * Copyright (C) 2020 Cadence Design Systems, Inc.
 *
 * Authors: Quentin Schulz <quentin.schulz@free-electrons.com>
 *          Swapnil Jakhade <sjakhade@cadence.com>
 *          Yuti Amonkar <yamonkar@cadence.com>
 *          Tomi Valkeinen <tomi.valkeinen@ti.com>
 *          Jyri Sarha <jsarha@ti.com>
 *
 * TODO:
 *     - Implement optimized mailbox communication using mailbox interrupts
 *     - Add support for power management
 *     - Add support for features like audio, MST and fast link training
 *     - Implement request_fw_cancel to handle HW_STATE
 *     - Fix asynchronous loading of firmware implementation
 *     - Add DRM helper function for cdns_mhdp_lower_link_rate
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-dp.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_hdcp_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <linux/unaligned.h>

#include "cdns-mhdp8546-core.h"
#include "cdns-mhdp8546-hdcp.h"
#include "cdns-mhdp8546-j721e.h"

static void cdns_mhdp_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct cdns_mhdp_device *mhdp = bridge_to_mhdp(bridge);

	/* Enable SW event interrupts */
	if (mhdp->bridge_attached)
		writel(readl(mhdp->regs + CDNS_APB_INT_MASK) &
		       ~CDNS_APB_INT_MASK_SW_EVENT_INT,
		       mhdp->regs + CDNS_APB_INT_MASK);
}

static void cdns_mhdp_bridge_hpd_disable(struct drm_bridge *bridge)
{
	struct cdns_mhdp_device *mhdp = bridge_to_mhdp(bridge);

	writel(readl(mhdp->regs + CDNS_APB_INT_MASK) |
	       CDNS_APB_INT_MASK_SW_EVENT_INT,
	       mhdp->regs + CDNS_APB_INT_MASK);
}

static int cdns_mhdp_mailbox_read(struct cdns_mhdp_device *mhdp)
{
	int ret, empty;

	WARN_ON(!mutex_is_locked(&mhdp->mbox_mutex));

	ret = readx_poll_timeout(readl, mhdp->regs + CDNS_MAILBOX_EMPTY,
				 empty, !empty, MAILBOX_RETRY_US,
				 MAILBOX_TIMEOUT_US);
	if (ret < 0)
		return ret;

	return readl(mhdp->regs + CDNS_MAILBOX_RX_DATA) & 0xff;
}

static int cdns_mhdp_mailbox_write(struct cdns_mhdp_device *mhdp, u8 val)
{
	int ret, full;

	WARN_ON(!mutex_is_locked(&mhdp->mbox_mutex));

	ret = readx_poll_timeout(readl, mhdp->regs + CDNS_MAILBOX_FULL,
				 full, !full, MAILBOX_RETRY_US,
				 MAILBOX_TIMEOUT_US);
	if (ret < 0)
		return ret;

	writel(val, mhdp->regs + CDNS_MAILBOX_TX_DATA);

	return 0;
}

static int cdns_mhdp_mailbox_recv_header(struct cdns_mhdp_device *mhdp,
					 u8 module_id, u8 opcode,
					 u16 req_size)
{
	u32 mbox_size, i;
	u8 header[4];
	int ret;

	/* read the header of the message */
	for (i = 0; i < sizeof(header); i++) {
		ret = cdns_mhdp_mailbox_read(mhdp);
		if (ret < 0)
			return ret;

		header[i] = ret;
	}

	mbox_size = get_unaligned_be16(header + 2);

	if (opcode != header[0] || module_id != header[1] ||
	    req_size != mbox_size) {
		/*
		 * If the message in mailbox is not what we want, we need to
		 * clear the mailbox by reading its contents.
		 */
		for (i = 0; i < mbox_size; i++)
			if (cdns_mhdp_mailbox_read(mhdp) < 0)
				break;

		return -EINVAL;
	}

	return 0;
}

static int cdns_mhdp_mailbox_recv_data(struct cdns_mhdp_device *mhdp,
				       u8 *buff, u16 buff_size)
{
	u32 i;
	int ret;

	for (i = 0; i < buff_size; i++) {
		ret = cdns_mhdp_mailbox_read(mhdp);
		if (ret < 0)
			return ret;

		buff[i] = ret;
	}

	return 0;
}

static int cdns_mhdp_mailbox_send(struct cdns_mhdp_device *mhdp, u8 module_id,
				  u8 opcode, u16 size, u8 *message)
{
	u8 header[4];
	int ret, i;

	header[0] = opcode;
	header[1] = module_id;
	put_unaligned_be16(size, header + 2);

	for (i = 0; i < sizeof(header); i++) {
		ret = cdns_mhdp_mailbox_write(mhdp, header[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < size; i++) {
		ret = cdns_mhdp_mailbox_write(mhdp, message[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static
int cdns_mhdp_reg_read(struct cdns_mhdp_device *mhdp, u32 addr, u32 *value)
{
	u8 msg[4], resp[8];
	int ret;

	put_unaligned_be32(addr, msg);

	mutex_lock(&mhdp->mbox_mutex);

	ret = cdns_mhdp_mailbox_send(mhdp, MB_MODULE_ID_GENERAL,
				     GENERAL_REGISTER_READ,
				     sizeof(msg), msg);
	if (ret)
		goto out;

	ret = cdns_mhdp_mailbox_recv_header(mhdp, MB_MODULE_ID_GENERAL,
					    GENERAL_REGISTER_READ,
					    sizeof(resp));
	if (ret)
		goto out;

	ret = cdns_mhdp_mailbox_recv_data(mhdp, resp, sizeof(resp));
	if (ret)
		goto out;

	/* Returned address value should be the same as requested */
	if (memcmp(msg, resp, sizeof(msg))) {
		ret = -EINVAL;
		goto out;
	}

	*value = get_unaligned_be32(resp + 4);

out:
	mutex_unlock(&mhdp->mbox_mutex);
	if (ret) {
		dev_err(mhdp->dev, "Failed to read register\n");
		*value = 0;
	}

	return ret;
}

static
int cdns_mhdp_reg_write(struct cdns_mhdp_device *mhdp, u16 addr, u32 val)
{
	u8 msg[6];
	int ret;

	put_unaligned_be16(addr, msg);
	put_unaligned_be32(val, msg + 2);

	mutex_lock(&mhdp->mbox_mutex);

	ret = cdns_mhdp_mailbox_send(mhdp, MB_MODULE_ID_DP_TX,
				     DPTX_WRITE_REGISTER, sizeof(msg), msg);

	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static
int cdns_mhdp_reg_write_bit(struct cdns_mhdp_device *mhdp, u16 addr,
			    u8 start_bit, u8 bits_no, u32 val)
{
	u8 field[8];
	int ret;

	put_unaligned_be16(addr, field);
	field[2] = start_bit;
	field[3] = bits_no;
	put_unaligned_be32(val, field + 4);

	mutex_lock(&mhdp->mbox_mutex);

	ret = cdns_mhdp_mailbox_send(mhdp, MB_MODULE_ID_DP_TX,
				     DPTX_WRITE_FIELD, sizeof(field), field);

	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static
int cdns_mhdp_dpcd_read(struct cdns_mhdp_device *mhdp,
			u32 addr, u8 *data, u16 len)
{
	u8 msg[5], reg[5];
	int ret;

	put_unaligned_be16(len, msg);
	put_unaligned_be24(addr, msg + 2);

	mutex_lock(&mhdp->mbox_mutex);

	ret = cdns_mhdp_mailbox_send(mhdp, MB_MODULE_ID_DP_TX,
				     DPTX_READ_DPCD, sizeof(msg), msg);
	if (ret)
		goto out;

	ret = cdns_mhdp_mailbox_recv_header(mhdp, MB_MODULE_ID_DP_TX,
					    DPTX_READ_DPCD,
					    sizeof(reg) + len);
	if (ret)
		goto out;

	ret = cdns_mhdp_mailbox_recv_data(mhdp, reg, sizeof(reg));
	if (ret)
		goto out;

	ret = cdns_mhdp_mailbox_recv_data(mhdp, data, len);

out:
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static
int cdns_mhdp_dpcd_write(struct cdns_mhdp_device *mhdp, u32 addr, u8 value)
{
	u8 msg[6], reg[5];
	int ret;

	put_unaligned_be16(1, msg);
	put_unaligned_be24(addr, msg + 2);
	msg[5] = value;

	mutex_lock(&mhdp->mbox_mutex);

	ret = cdns_mhdp_mailbox_send(mhdp, MB_MODULE_ID_DP_TX,
				     DPTX_WRITE_DPCD, sizeof(msg), msg);
	if (ret)
		goto out;

	ret = cdns_mhdp_mailbox_recv_header(mhdp, MB_MODULE_ID_DP_TX,
					    DPTX_WRITE_DPCD, sizeof(reg));
	if (ret)
		goto out;

	ret = cdns_mhdp_mailbox_recv_data(mhdp, reg, sizeof(reg));
	if (ret)
		goto out;

	if (addr != get_unaligned_be24(reg + 2))
		ret = -EINVAL;

out:
	mutex_unlock(&mhdp->mbox_mutex);

	if (ret)
		dev_err(mhdp->dev, "dpcd write failed: %d\n", ret);
	return ret;
}

static
int cdns_mhdp_set_firmware_active(struct cdns_mhdp_device *mhdp, bool enable)
{
	u8 msg[5];
	int ret, i;

	msg[0] = GENERAL_MAIN_CONTROL;
	msg[1] = MB_MODULE_ID_GENERAL;
	msg[2] = 0;
	msg[3] = 1;
	msg[4] = enable ? FW_ACTIVE : FW_STANDBY;

	mutex_lock(&mhdp->mbox_mutex);

	for (i = 0; i < sizeof(msg); i++) {
		ret = cdns_mhdp_mailbox_write(mhdp, msg[i]);
		if (ret)
			goto out;
	}

	/* read the firmware state */
	ret = cdns_mhdp_mailbox_recv_data(mhdp, msg, sizeof(msg));
	if (ret)
		goto out;

	ret = 0;

out:
	mutex_unlock(&mhdp->mbox_mutex);

	if (ret < 0)
		dev_err(mhdp->dev, "set firmware active failed\n");
	return ret;
}

static
int cdns_mhdp_get_hpd_status(struct cdns_mhdp_device *mhdp)
{
	u8 status;
	int ret;

	mutex_lock(&mhdp->mbox_mutex);

	ret = cdns_mhdp_mailbox_send(mhdp, MB_MODULE_ID_DP_TX,
				     DPTX_HPD_STATE, 0, NULL);
	if (ret)
		goto err_get_hpd;

	ret = cdns_mhdp_mailbox_recv_header(mhdp, MB_MODULE_ID_DP_TX,
					    DPTX_HPD_STATE,
					    sizeof(status));
	if (ret)
		goto err_get_hpd;

	ret = cdns_mhdp_mailbox_recv_data(mhdp, &status, sizeof(status));
	if (ret)
		goto err_get_hpd;

	mutex_unlock(&mhdp->mbox_mutex);

	dev_dbg(mhdp->dev, "%s: HPD %splugged\n", __func__,
		status ? "" : "un");

	return status;

err_get_hpd:
	mutex_unlock(&mhdp->mbox_mutex);

	return ret;
}

static
int cdns_mhdp_get_edid_block(void *data, u8 *edid,
			     unsigned int block, size_t length)
{
	struct cdns_mhdp_device *mhdp = data;
	u8 msg[2], reg[2], i;
	int ret;

	mutex_lock(&mhdp->mbox_mutex);

	for (i = 0; i < 4; i++) {
		msg[0] = block / 2;
		msg[1] = block % 2;

		ret = cdns_mhdp_mailbox_send(mhdp, MB_MODULE_ID_DP_TX,
					     DPTX_GET_EDID, sizeof(msg), msg);
		if (ret)
			continue;

		ret = cdns_mhdp_mailbox_recv_header(mhdp, MB_MODULE_ID_DP_TX,
						    DPTX_GET_EDID,
						    sizeof(reg) + length);
		if (ret)
			continue;

		ret = cdns_mhdp_mailbox_recv_data(mhdp, reg, sizeof(reg));
		if (ret)
			continue;

		ret = cdns_mhdp_mailbox_recv_data(mhdp, edid, length);
		if (ret)
			continue;

		if (reg[0] == length && reg[1] == block / 2)
			break;
	}

	mutex_unlock(&mhdp->mbox_mutex);

	if (ret)
		dev_err(mhdp->dev, "get block[%d] edid failed: %d\n",
			block, ret);

	return ret;
}

static
int cdns_mhdp_read_hpd_event(struct cdns_mhdp_device *mhdp)
{
	u8 event = 0;
	int ret;

	mutex_lock(&mhdp->mbox_mutex);

	ret = cdns_mhdp_mailbox_send(mhdp, MB_MODULE_ID_DP_TX,
				     DPTX_READ_EVENT, 0, NULL);
	if (ret)
		goto out;

	ret = cdns_mhdp_mailbox_recv_header(mhdp, MB_MODULE_ID_DP_TX,
					    DPTX_READ_EVENT, sizeof(event));
	if (ret < 0)
		goto out;

	ret = cdns_mhdp_mailbox_recv_data(mhdp, &event, sizeof(event));
out:
	mutex_unlock(&mhdp->mbox_mutex);

	if (ret < 0)
		return ret;

	dev_dbg(mhdp->dev, "%s: %s%s%s%s\n", __func__,
		(event & DPTX_READ_EVENT_HPD_TO_HIGH) ? "TO_HIGH " : "",
		(event & DPTX_READ_EVENT_HPD_TO_LOW) ? "TO_LOW " : "",
		(event & DPTX_READ_EVENT_HPD_PULSE) ? "PULSE " : "",
		(event & DPTX_READ_EVENT_HPD_STATE) ? "HPD_STATE " : "");

	return event;
}

static
int cdns_mhdp_adjust_lt(struct cdns_mhdp_device *mhdp, unsigned int nlanes,
			unsigned int udelay, const u8 *lanes_data,
			u8 link_status[DP_LINK_STATUS_SIZE])
{
	u8 payload[7];
	u8 hdr[5]; /* For DPCD read response header */
	u32 addr;
	int ret;

	if (nlanes != 4 && nlanes != 2 && nlanes != 1) {
		dev_err(mhdp->dev, "invalid number of lanes: %u\n", nlanes);
		ret = -EINVAL;
		goto out;
	}

	payload[0] = nlanes;
	put_unaligned_be16(udelay, payload + 1);
	memcpy(payload + 3, lanes_data, nlanes);

	mutex_lock(&mhdp->mbox_mutex);

	ret = cdns_mhdp_mailbox_send(mhdp, MB_MODULE_ID_DP_TX,
				     DPTX_ADJUST_LT,
				     sizeof(payload), payload);
	if (ret)
		goto out;

	/* Yes, read the DPCD read command response */
	ret = cdns_mhdp_mailbox_recv_header(mhdp, MB_MODULE_ID_DP_TX,
					    DPTX_READ_DPCD,
					    sizeof(hdr) + DP_LINK_STATUS_SIZE);
	if (ret)
		goto out;

	ret = cdns_mhdp_mailbox_recv_data(mhdp, hdr, sizeof(hdr));
	if (ret)
		goto out;

	addr = get_unaligned_be24(hdr + 2);
	if (addr != DP_LANE0_1_STATUS)
		goto out;

	ret = cdns_mhdp_mailbox_recv_data(mhdp, link_status,
					  DP_LINK_STATUS_SIZE);

out:
	mutex_unlock(&mhdp->mbox_mutex);

	if (ret)
		dev_err(mhdp->dev, "Failed to adjust Link Training.\n");

	return ret;
}

/**
 * cdns_mhdp_link_configure() - configure a DisplayPort link
 * @aux: DisplayPort AUX channel
 * @link: pointer to a structure containing the link configuration
 *
 * Returns 0 on success or a negative error code on failure.
 */
static
int cdns_mhdp_link_configure(struct drm_dp_aux *aux,
			     struct cdns_mhdp_link *link)
{
	u8 values[2];
	int err;

	values[0] = drm_dp_link_rate_to_bw_code(link->rate);
	values[1] = link->num_lanes;

	if (link->capabilities & DP_LINK_CAP_ENHANCED_FRAMING)
		values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
	if (err < 0)
		return err;

	return 0;
}

static unsigned int cdns_mhdp_max_link_rate(struct cdns_mhdp_device *mhdp)
{
	return min(mhdp->host.link_rate, mhdp->sink.link_rate);
}

static u8 cdns_mhdp_max_num_lanes(struct cdns_mhdp_device *mhdp)
{
	return min(mhdp->sink.lanes_cnt, mhdp->host.lanes_cnt);
}

static u8 cdns_mhdp_eq_training_pattern_supported(struct cdns_mhdp_device *mhdp)
{
	return fls(mhdp->host.pattern_supp & mhdp->sink.pattern_supp);
}

static bool cdns_mhdp_get_ssc_supported(struct cdns_mhdp_device *mhdp)
{
	/* Check if SSC is supported by both sides */
	return mhdp->host.ssc && mhdp->sink.ssc;
}

static enum drm_connector_status cdns_mhdp_detect(struct cdns_mhdp_device *mhdp)
{
	dev_dbg(mhdp->dev, "%s: %d\n", __func__, mhdp->plugged);

	if (mhdp->plugged)
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static int cdns_mhdp_check_fw_version(struct cdns_mhdp_device *mhdp)
{
	u32 major_num, minor_num, revision;
	u32 fw_ver, lib_ver;

	fw_ver = (readl(mhdp->regs + CDNS_VER_H) << 8)
	       | readl(mhdp->regs + CDNS_VER_L);

	lib_ver = (readl(mhdp->regs + CDNS_LIB_H_ADDR) << 8)
		| readl(mhdp->regs + CDNS_LIB_L_ADDR);

	if (lib_ver < 33984) {
		/*
		 * Older FW versions with major number 1, used to store FW
		 * version information by storing repository revision number
		 * in registers. This is for identifying these FW versions.
		 */
		major_num = 1;
		minor_num = 2;
		if (fw_ver == 26098) {
			revision = 15;
		} else if (lib_ver == 0 && fw_ver == 0) {
			revision = 17;
		} else {
			dev_err(mhdp->dev, "Unsupported FW version: fw_ver = %u, lib_ver = %u\n",
				fw_ver, lib_ver);
			return -ENODEV;
		}
	} else {
		/* To identify newer FW versions with major number 2 onwards. */
		major_num = fw_ver / 10000;
		minor_num = (fw_ver / 100) % 100;
		revision = (fw_ver % 10000) % 100;
	}

	dev_dbg(mhdp->dev, "FW version: v%u.%u.%u\n", major_num, minor_num,
		revision);
	return 0;
}

static int cdns_mhdp_fw_activate(const struct firmware *fw,
				 struct cdns_mhdp_device *mhdp)
{
	unsigned int reg;
	int ret;

	/* Release uCPU reset and stall it. */
	writel(CDNS_CPU_STALL, mhdp->regs + CDNS_APB_CTRL);

	memcpy_toio(mhdp->regs + CDNS_MHDP_IMEM, fw->data, fw->size);

	/* Leave debug mode, release stall */
	writel(0, mhdp->regs + CDNS_APB_CTRL);

	/*
	 * Wait for the KEEP_ALIVE "message" on the first 8 bits.
	 * Updated each sched "tick" (~2ms)
	 */
	ret = readl_poll_timeout(mhdp->regs + CDNS_KEEP_ALIVE, reg,
				 reg & CDNS_KEEP_ALIVE_MASK, 500,
				 CDNS_KEEP_ALIVE_TIMEOUT);
	if (ret) {
		dev_err(mhdp->dev,
			"device didn't give any life sign: reg %d\n", reg);
		return ret;
	}

	ret = cdns_mhdp_check_fw_version(mhdp);
	if (ret)
		return ret;

	/* Init events to 0 as it's not cleared by FW at boot but on read */
	readl(mhdp->regs + CDNS_SW_EVENT0);
	readl(mhdp->regs + CDNS_SW_EVENT1);
	readl(mhdp->regs + CDNS_SW_EVENT2);
	readl(mhdp->regs + CDNS_SW_EVENT3);

	/* Activate uCPU */
	ret = cdns_mhdp_set_firmware_active(mhdp, true);
	if (ret)
		return ret;

	spin_lock(&mhdp->start_lock);

	mhdp->hw_state = MHDP_HW_READY;

	/*
	 * Here we must keep the lock while enabling the interrupts
	 * since it would otherwise be possible that interrupt enable
	 * code is executed after the bridge is detached. The similar
	 * situation is not possible in attach()/detach() callbacks
	 * since the hw_state changes from MHDP_HW_READY to
	 * MHDP_HW_STOPPED happens only due to driver removal when
	 * bridge should already be detached.
	 */
	cdns_mhdp_bridge_hpd_enable(&mhdp->bridge);

	spin_unlock(&mhdp->start_lock);

	wake_up(&mhdp->fw_load_wq);
	dev_dbg(mhdp->dev, "DP FW activated\n");

	return 0;
}

static void cdns_mhdp_fw_cb(const struct firmware *fw, void *context)
{
	struct cdns_mhdp_device *mhdp = context;
	bool bridge_attached;
	int ret;

	dev_dbg(mhdp->dev, "firmware callback\n");

	if (!fw || !fw->data) {
		dev_err(mhdp->dev, "%s: No firmware.\n", __func__);
		return;
	}

	ret = cdns_mhdp_fw_activate(fw, mhdp);

	release_firmware(fw);

	if (ret)
		return;

	/*
	 *  XXX how to make sure the bridge is still attached when
	 *      calling drm_kms_helper_hotplug_event() after releasing
	 *      the lock? We should not hold the spin lock when
	 *      calling drm_kms_helper_hotplug_event() since it may
	 *      cause a dead lock. FB-dev console calls detect from the
	 *      same thread just down the call stack started here.
	 */
	spin_lock(&mhdp->start_lock);
	bridge_attached = mhdp->bridge_attached;
	spin_unlock(&mhdp->start_lock);
	if (bridge_attached) {
		if (mhdp->connector.dev)
			drm_kms_helper_hotplug_event(mhdp->bridge.dev);
		else
			drm_bridge_hpd_notify(&mhdp->bridge, cdns_mhdp_detect(mhdp));
	}
}

static int cdns_mhdp_load_firmware(struct cdns_mhdp_device *mhdp)
{
	int ret;

	ret = request_firmware_nowait(THIS_MODULE, true, FW_NAME, mhdp->dev,
				      GFP_KERNEL, mhdp, cdns_mhdp_fw_cb);
	if (ret) {
		dev_err(mhdp->dev, "failed to load firmware (%s), ret: %d\n",
			FW_NAME, ret);
		return ret;
	}

	return 0;
}

static ssize_t cdns_mhdp_transfer(struct drm_dp_aux *aux,
				  struct drm_dp_aux_msg *msg)
{
	struct cdns_mhdp_device *mhdp = dev_get_drvdata(aux->dev);
	int ret;

	if (msg->request != DP_AUX_NATIVE_WRITE &&
	    msg->request != DP_AUX_NATIVE_READ)
		return -EOPNOTSUPP;

	if (msg->request == DP_AUX_NATIVE_WRITE) {
		const u8 *buf = msg->buffer;
		unsigned int i;

		for (i = 0; i < msg->size; ++i) {
			ret = cdns_mhdp_dpcd_write(mhdp,
						   msg->address + i, buf[i]);
			if (!ret)
				continue;

			dev_err(mhdp->dev,
				"Failed to write DPCD addr %u\n",
				msg->address + i);

			return ret;
		}
	} else {
		ret = cdns_mhdp_dpcd_read(mhdp, msg->address,
					  msg->buffer, msg->size);
		if (ret) {
			dev_err(mhdp->dev,
				"Failed to read DPCD addr %u\n",
				msg->address);

			return ret;
		}
	}

	return msg->size;
}

static int cdns_mhdp_link_training_init(struct cdns_mhdp_device *mhdp)
{
	union phy_configure_opts phy_cfg;
	u32 reg32;
	int ret;

	drm_dp_dpcd_writeb(&mhdp->aux, DP_TRAINING_PATTERN_SET,
			   DP_TRAINING_PATTERN_DISABLE);

	/* Reset PHY configuration */
	reg32 = CDNS_PHY_COMMON_CONFIG | CDNS_PHY_TRAINING_TYPE(1);
	if (!mhdp->host.scrambler)
		reg32 |= CDNS_PHY_SCRAMBLER_BYPASS;

	cdns_mhdp_reg_write(mhdp, CDNS_DPTX_PHY_CONFIG, reg32);

	cdns_mhdp_reg_write(mhdp, CDNS_DP_ENHNCD,
			    mhdp->sink.enhanced & mhdp->host.enhanced);

	cdns_mhdp_reg_write(mhdp, CDNS_DP_LANE_EN,
			    CDNS_DP_LANE_EN_LANES(mhdp->link.num_lanes));

	cdns_mhdp_link_configure(&mhdp->aux, &mhdp->link);
	phy_cfg.dp.link_rate = mhdp->link.rate / 100;
	phy_cfg.dp.lanes = mhdp->link.num_lanes;

	memset(phy_cfg.dp.voltage, 0, sizeof(phy_cfg.dp.voltage));
	memset(phy_cfg.dp.pre, 0, sizeof(phy_cfg.dp.pre));

	phy_cfg.dp.ssc = cdns_mhdp_get_ssc_supported(mhdp);
	phy_cfg.dp.set_lanes = true;
	phy_cfg.dp.set_rate = true;
	phy_cfg.dp.set_voltages = true;
	ret = phy_configure(mhdp->phy,  &phy_cfg);
	if (ret) {
		dev_err(mhdp->dev, "%s: phy_configure() failed: %d\n",
			__func__, ret);
		return ret;
	}

	cdns_mhdp_reg_write(mhdp, CDNS_DPTX_PHY_CONFIG,
			    CDNS_PHY_COMMON_CONFIG |
			    CDNS_PHY_TRAINING_EN |
			    CDNS_PHY_TRAINING_TYPE(1) |
			    CDNS_PHY_SCRAMBLER_BYPASS);

	drm_dp_dpcd_writeb(&mhdp->aux, DP_TRAINING_PATTERN_SET,
			   DP_TRAINING_PATTERN_1 | DP_LINK_SCRAMBLING_DISABLE);

	return 0;
}

static void cdns_mhdp_get_adjust_train(struct cdns_mhdp_device *mhdp,
				       u8 link_status[DP_LINK_STATUS_SIZE],
				       u8 lanes_data[CDNS_DP_MAX_NUM_LANES],
				       union phy_configure_opts *phy_cfg)
{
	u8 adjust, max_pre_emph, max_volt_swing;
	u8 set_volt, set_pre;
	unsigned int i;

	max_pre_emph = CDNS_PRE_EMPHASIS(mhdp->host.pre_emphasis)
			   << DP_TRAIN_PRE_EMPHASIS_SHIFT;
	max_volt_swing = CDNS_VOLT_SWING(mhdp->host.volt_swing);

	for (i = 0; i < mhdp->link.num_lanes; i++) {
		/* Check if Voltage swing and pre-emphasis are within limits */
		adjust = drm_dp_get_adjust_request_voltage(link_status, i);
		set_volt = min(adjust, max_volt_swing);

		adjust = drm_dp_get_adjust_request_pre_emphasis(link_status, i);
		set_pre = min(adjust, max_pre_emph)
			  >> DP_TRAIN_PRE_EMPHASIS_SHIFT;

		/*
		 * Voltage swing level and pre-emphasis level combination is
		 * not allowed: leaving pre-emphasis as-is, and adjusting
		 * voltage swing.
		 */
		if (set_volt + set_pre > 3)
			set_volt = 3 - set_pre;

		phy_cfg->dp.voltage[i] = set_volt;
		lanes_data[i] = set_volt;

		if (set_volt == max_volt_swing)
			lanes_data[i] |= DP_TRAIN_MAX_SWING_REACHED;

		phy_cfg->dp.pre[i] = set_pre;
		lanes_data[i] |= (set_pre << DP_TRAIN_PRE_EMPHASIS_SHIFT);

		if (set_pre == (max_pre_emph >> DP_TRAIN_PRE_EMPHASIS_SHIFT))
			lanes_data[i] |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
	}
}

static
void cdns_mhdp_set_adjust_request_voltage(u8 link_status[DP_LINK_STATUS_SIZE],
					  unsigned int lane, u8 volt)
{
	unsigned int s = ((lane & 1) ?
			  DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
			  DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	unsigned int idx = DP_ADJUST_REQUEST_LANE0_1 - DP_LANE0_1_STATUS + (lane >> 1);

	link_status[idx] &= ~(DP_ADJUST_VOLTAGE_SWING_LANE0_MASK << s);
	link_status[idx] |= volt << s;
}

static
void cdns_mhdp_set_adjust_request_pre_emphasis(u8 link_status[DP_LINK_STATUS_SIZE],
					       unsigned int lane, u8 pre_emphasis)
{
	unsigned int s = ((lane & 1) ?
			  DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
			  DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	unsigned int idx = DP_ADJUST_REQUEST_LANE0_1 - DP_LANE0_1_STATUS + (lane >> 1);

	link_status[idx] &= ~(DP_ADJUST_PRE_EMPHASIS_LANE0_MASK << s);
	link_status[idx] |= pre_emphasis << s;
}

static void cdns_mhdp_adjust_requested_eq(struct cdns_mhdp_device *mhdp,
					  u8 link_status[DP_LINK_STATUS_SIZE])
{
	u8 max_pre = CDNS_PRE_EMPHASIS(mhdp->host.pre_emphasis);
	u8 max_volt = CDNS_VOLT_SWING(mhdp->host.volt_swing);
	unsigned int i;
	u8 volt, pre;

	for (i = 0; i < mhdp->link.num_lanes; i++) {
		volt = drm_dp_get_adjust_request_voltage(link_status, i);
		pre = drm_dp_get_adjust_request_pre_emphasis(link_status, i);
		if (volt + pre > 3)
			cdns_mhdp_set_adjust_request_voltage(link_status, i,
							     3 - pre);
		if (mhdp->host.volt_swing & CDNS_FORCE_VOLT_SWING)
			cdns_mhdp_set_adjust_request_voltage(link_status, i,
							     max_volt);
		if (mhdp->host.pre_emphasis & CDNS_FORCE_PRE_EMPHASIS)
			cdns_mhdp_set_adjust_request_pre_emphasis(link_status,
								  i, max_pre);
	}
}

static void cdns_mhdp_print_lt_status(const char *prefix,
				      struct cdns_mhdp_device *mhdp,
				      union phy_configure_opts *phy_cfg)
{
	char vs[8] = "0/0/0/0";
	char pe[8] = "0/0/0/0";
	unsigned int i;

	for (i = 0; i < mhdp->link.num_lanes; i++) {
		vs[i * 2] = '0' + phy_cfg->dp.voltage[i];
		pe[i * 2] = '0' + phy_cfg->dp.pre[i];
	}

	vs[i * 2 - 1] = '\0';
	pe[i * 2 - 1] = '\0';

	dev_dbg(mhdp->dev, "%s, %u lanes, %u Mbps, vs %s, pe %s\n",
		prefix,
		mhdp->link.num_lanes, mhdp->link.rate / 100,
		vs, pe);
}

static bool cdns_mhdp_link_training_channel_eq(struct cdns_mhdp_device *mhdp,
					       u8 eq_tps,
					       unsigned int training_interval)
{
	u8 lanes_data[CDNS_DP_MAX_NUM_LANES], fail_counter_short = 0;
	u8 link_status[DP_LINK_STATUS_SIZE];
	union phy_configure_opts phy_cfg;
	u32 reg32;
	int ret;
	bool r;

	dev_dbg(mhdp->dev, "Starting EQ phase\n");

	/* Enable link training TPS[eq_tps] in PHY */
	reg32 = CDNS_PHY_COMMON_CONFIG | CDNS_PHY_TRAINING_EN |
		CDNS_PHY_TRAINING_TYPE(eq_tps);
	if (eq_tps != 4)
		reg32 |= CDNS_PHY_SCRAMBLER_BYPASS;
	cdns_mhdp_reg_write(mhdp, CDNS_DPTX_PHY_CONFIG, reg32);

	drm_dp_dpcd_writeb(&mhdp->aux, DP_TRAINING_PATTERN_SET,
			   (eq_tps != 4) ? eq_tps | DP_LINK_SCRAMBLING_DISABLE :
			   CDNS_DP_TRAINING_PATTERN_4);

	drm_dp_dpcd_read_link_status(&mhdp->aux, link_status);

	do {
		cdns_mhdp_get_adjust_train(mhdp, link_status, lanes_data,
					   &phy_cfg);
		phy_cfg.dp.lanes = mhdp->link.num_lanes;
		phy_cfg.dp.ssc = cdns_mhdp_get_ssc_supported(mhdp);
		phy_cfg.dp.set_lanes = false;
		phy_cfg.dp.set_rate = false;
		phy_cfg.dp.set_voltages = true;
		ret = phy_configure(mhdp->phy,  &phy_cfg);
		if (ret) {
			dev_err(mhdp->dev, "%s: phy_configure() failed: %d\n",
				__func__, ret);
			goto err;
		}

		cdns_mhdp_adjust_lt(mhdp, mhdp->link.num_lanes,
				    training_interval, lanes_data, link_status);

		r = drm_dp_clock_recovery_ok(link_status, mhdp->link.num_lanes);
		if (!r)
			goto err;

		if (drm_dp_channel_eq_ok(link_status, mhdp->link.num_lanes)) {
			cdns_mhdp_print_lt_status("EQ phase ok", mhdp,
						  &phy_cfg);
			return true;
		}

		fail_counter_short++;

		cdns_mhdp_adjust_requested_eq(mhdp, link_status);
	} while (fail_counter_short < 5);

err:
	cdns_mhdp_print_lt_status("EQ phase failed", mhdp, &phy_cfg);

	return false;
}

static void cdns_mhdp_adjust_requested_cr(struct cdns_mhdp_device *mhdp,
					  u8 link_status[DP_LINK_STATUS_SIZE],
					  u8 *req_volt, u8 *req_pre)
{
	const u8 max_volt = CDNS_VOLT_SWING(mhdp->host.volt_swing);
	const u8 max_pre = CDNS_PRE_EMPHASIS(mhdp->host.pre_emphasis);
	unsigned int i;

	for (i = 0; i < mhdp->link.num_lanes; i++) {
		u8 val;

		val = mhdp->host.volt_swing & CDNS_FORCE_VOLT_SWING ?
		      max_volt : req_volt[i];
		cdns_mhdp_set_adjust_request_voltage(link_status, i, val);

		val = mhdp->host.pre_emphasis & CDNS_FORCE_PRE_EMPHASIS ?
		      max_pre : req_pre[i];
		cdns_mhdp_set_adjust_request_pre_emphasis(link_status, i, val);
	}
}

static
void cdns_mhdp_validate_cr(struct cdns_mhdp_device *mhdp, bool *cr_done,
			   bool *same_before_adjust, bool *max_swing_reached,
			   u8 before_cr[CDNS_DP_MAX_NUM_LANES],
			   u8 after_cr[DP_LINK_STATUS_SIZE], u8 *req_volt,
			   u8 *req_pre)
{
	const u8 max_volt = CDNS_VOLT_SWING(mhdp->host.volt_swing);
	const u8 max_pre = CDNS_PRE_EMPHASIS(mhdp->host.pre_emphasis);
	bool same_pre, same_volt;
	unsigned int i;
	u8 adjust;

	*same_before_adjust = false;
	*max_swing_reached = false;
	*cr_done = drm_dp_clock_recovery_ok(after_cr, mhdp->link.num_lanes);

	for (i = 0; i < mhdp->link.num_lanes; i++) {
		adjust = drm_dp_get_adjust_request_voltage(after_cr, i);
		req_volt[i] = min(adjust, max_volt);

		adjust = drm_dp_get_adjust_request_pre_emphasis(after_cr, i) >>
		      DP_TRAIN_PRE_EMPHASIS_SHIFT;
		req_pre[i] = min(adjust, max_pre);

		same_pre = (before_cr[i] & DP_TRAIN_PRE_EMPHASIS_MASK) ==
			   req_pre[i] << DP_TRAIN_PRE_EMPHASIS_SHIFT;
		same_volt = (before_cr[i] & DP_TRAIN_VOLTAGE_SWING_MASK) ==
			    req_volt[i];
		if (same_pre && same_volt)
			*same_before_adjust = true;

		/* 3.1.5.2 in DP Standard v1.4. Table 3-1 */
		if (!*cr_done && req_volt[i] + req_pre[i] >= 3) {
			*max_swing_reached = true;
			return;
		}
	}
}

static bool cdns_mhdp_link_training_cr(struct cdns_mhdp_device *mhdp)
{
	u8 lanes_data[CDNS_DP_MAX_NUM_LANES],
	fail_counter_short = 0, fail_counter_cr_long = 0;
	u8 link_status[DP_LINK_STATUS_SIZE];
	bool cr_done;
	union phy_configure_opts phy_cfg;
	int ret;

	dev_dbg(mhdp->dev, "Starting CR phase\n");

	ret = cdns_mhdp_link_training_init(mhdp);
	if (ret)
		goto err;

	drm_dp_dpcd_read_link_status(&mhdp->aux, link_status);

	do {
		u8 requested_adjust_volt_swing[CDNS_DP_MAX_NUM_LANES] = {};
		u8 requested_adjust_pre_emphasis[CDNS_DP_MAX_NUM_LANES] = {};
		bool same_before_adjust, max_swing_reached;

		cdns_mhdp_get_adjust_train(mhdp, link_status, lanes_data,
					   &phy_cfg);
		phy_cfg.dp.lanes = mhdp->link.num_lanes;
		phy_cfg.dp.ssc = cdns_mhdp_get_ssc_supported(mhdp);
		phy_cfg.dp.set_lanes = false;
		phy_cfg.dp.set_rate = false;
		phy_cfg.dp.set_voltages = true;
		ret = phy_configure(mhdp->phy,  &phy_cfg);
		if (ret) {
			dev_err(mhdp->dev, "%s: phy_configure() failed: %d\n",
				__func__, ret);
			goto err;
		}

		cdns_mhdp_adjust_lt(mhdp, mhdp->link.num_lanes, 100,
				    lanes_data, link_status);

		cdns_mhdp_validate_cr(mhdp, &cr_done, &same_before_adjust,
				      &max_swing_reached, lanes_data,
				      link_status,
				      requested_adjust_volt_swing,
				      requested_adjust_pre_emphasis);

		if (max_swing_reached) {
			dev_err(mhdp->dev, "CR: max swing reached\n");
			goto err;
		}

		if (cr_done) {
			cdns_mhdp_print_lt_status("CR phase ok", mhdp,
						  &phy_cfg);
			return true;
		}

		/* Not all CR_DONE bits set */
		fail_counter_cr_long++;

		if (same_before_adjust) {
			fail_counter_short++;
			continue;
		}

		fail_counter_short = 0;
		/*
		 * Voltage swing/pre-emphasis adjust requested
		 * during CR phase
		 */
		cdns_mhdp_adjust_requested_cr(mhdp, link_status,
					      requested_adjust_volt_swing,
					      requested_adjust_pre_emphasis);
	} while (fail_counter_short < 5 && fail_counter_cr_long < 10);

err:
	cdns_mhdp_print_lt_status("CR phase failed", mhdp, &phy_cfg);

	return false;
}

static void cdns_mhdp_lower_link_rate(struct cdns_mhdp_link *link)
{
	switch (drm_dp_link_rate_to_bw_code(link->rate)) {
	case DP_LINK_BW_2_7:
		link->rate = drm_dp_bw_code_to_link_rate(DP_LINK_BW_1_62);
		break;
	case DP_LINK_BW_5_4:
		link->rate = drm_dp_bw_code_to_link_rate(DP_LINK_BW_2_7);
		break;
	case DP_LINK_BW_8_1:
		link->rate = drm_dp_bw_code_to_link_rate(DP_LINK_BW_5_4);
		break;
	}
}

static int cdns_mhdp_link_training(struct cdns_mhdp_device *mhdp,
				   unsigned int training_interval)
{
	u32 reg32;
	const u8 eq_tps = cdns_mhdp_eq_training_pattern_supported(mhdp);
	int ret;

	while (1) {
		if (!cdns_mhdp_link_training_cr(mhdp)) {
			if (drm_dp_link_rate_to_bw_code(mhdp->link.rate) !=
			    DP_LINK_BW_1_62) {
				dev_dbg(mhdp->dev,
					"Reducing link rate during CR phase\n");
				cdns_mhdp_lower_link_rate(&mhdp->link);

				continue;
			} else if (mhdp->link.num_lanes > 1) {
				dev_dbg(mhdp->dev,
					"Reducing lanes number during CR phase\n");
				mhdp->link.num_lanes >>= 1;
				mhdp->link.rate = cdns_mhdp_max_link_rate(mhdp);

				continue;
			}

			dev_err(mhdp->dev,
				"Link training failed during CR phase\n");
			goto err;
		}

		if (cdns_mhdp_link_training_channel_eq(mhdp, eq_tps,
						       training_interval))
			break;

		if (mhdp->link.num_lanes > 1) {
			dev_dbg(mhdp->dev,
				"Reducing lanes number during EQ phase\n");
			mhdp->link.num_lanes >>= 1;

			continue;
		} else if (drm_dp_link_rate_to_bw_code(mhdp->link.rate) !=
			   DP_LINK_BW_1_62) {
			dev_dbg(mhdp->dev,
				"Reducing link rate during EQ phase\n");
			cdns_mhdp_lower_link_rate(&mhdp->link);
			mhdp->link.num_lanes = cdns_mhdp_max_num_lanes(mhdp);

			continue;
		}

		dev_err(mhdp->dev, "Link training failed during EQ phase\n");
		goto err;
	}

	dev_dbg(mhdp->dev, "Link training ok. Lanes: %u, Rate %u Mbps\n",
		mhdp->link.num_lanes, mhdp->link.rate / 100);

	drm_dp_dpcd_writeb(&mhdp->aux, DP_TRAINING_PATTERN_SET,
			   mhdp->host.scrambler ? 0 :
			   DP_LINK_SCRAMBLING_DISABLE);

	ret = cdns_mhdp_reg_read(mhdp, CDNS_DP_FRAMER_GLOBAL_CONFIG, &reg32);
	if (ret < 0) {
		dev_err(mhdp->dev,
			"Failed to read CDNS_DP_FRAMER_GLOBAL_CONFIG %d\n",
			ret);
		return ret;
	}
	reg32 &= ~GENMASK(1, 0);
	reg32 |= CDNS_DP_NUM_LANES(mhdp->link.num_lanes);
	reg32 |= CDNS_DP_WR_FAILING_EDGE_VSYNC;
	reg32 |= CDNS_DP_FRAMER_EN;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_FRAMER_GLOBAL_CONFIG, reg32);

	/* Reset PHY config */
	reg32 = CDNS_PHY_COMMON_CONFIG | CDNS_PHY_TRAINING_TYPE(1);
	if (!mhdp->host.scrambler)
		reg32 |= CDNS_PHY_SCRAMBLER_BYPASS;
	cdns_mhdp_reg_write(mhdp, CDNS_DPTX_PHY_CONFIG, reg32);

	return 0;
err:
	/* Reset PHY config */
	reg32 = CDNS_PHY_COMMON_CONFIG | CDNS_PHY_TRAINING_TYPE(1);
	if (!mhdp->host.scrambler)
		reg32 |= CDNS_PHY_SCRAMBLER_BYPASS;
	cdns_mhdp_reg_write(mhdp, CDNS_DPTX_PHY_CONFIG, reg32);

	drm_dp_dpcd_writeb(&mhdp->aux, DP_TRAINING_PATTERN_SET,
			   DP_TRAINING_PATTERN_DISABLE);

	return -EIO;
}

static u32 cdns_mhdp_get_training_interval_us(struct cdns_mhdp_device *mhdp,
					      u32 interval)
{
	if (interval == 0)
		return 400;
	if (interval < 5)
		return 4000 << (interval - 1);
	dev_err(mhdp->dev,
		"wrong training interval returned by DPCD: %d\n", interval);
	return 0;
}

static void cdns_mhdp_fill_host_caps(struct cdns_mhdp_device *mhdp)
{
	unsigned int link_rate;

	/* Get source capabilities based on PHY attributes */

	mhdp->host.lanes_cnt = mhdp->phy->attrs.bus_width;
	if (!mhdp->host.lanes_cnt)
		mhdp->host.lanes_cnt = 4;

	link_rate = mhdp->phy->attrs.max_link_rate;
	if (!link_rate)
		link_rate = drm_dp_bw_code_to_link_rate(DP_LINK_BW_8_1);
	else
		/* PHY uses Mb/s, DRM uses tens of kb/s. */
		link_rate *= 100;

	mhdp->host.link_rate = link_rate;
	mhdp->host.volt_swing = CDNS_VOLT_SWING(3);
	mhdp->host.pre_emphasis = CDNS_PRE_EMPHASIS(3);
	mhdp->host.pattern_supp = CDNS_SUPPORT_TPS(1) |
				  CDNS_SUPPORT_TPS(2) | CDNS_SUPPORT_TPS(3) |
				  CDNS_SUPPORT_TPS(4);
	mhdp->host.lane_mapping = CDNS_LANE_MAPPING_NORMAL;
	mhdp->host.fast_link = false;
	mhdp->host.enhanced = true;
	mhdp->host.scrambler = true;
	mhdp->host.ssc = false;
}

static void cdns_mhdp_fill_sink_caps(struct cdns_mhdp_device *mhdp,
				     u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	mhdp->sink.link_rate = mhdp->link.rate;
	mhdp->sink.lanes_cnt = mhdp->link.num_lanes;
	mhdp->sink.enhanced = !!(mhdp->link.capabilities &
				 DP_LINK_CAP_ENHANCED_FRAMING);

	/* Set SSC support */
	mhdp->sink.ssc = !!(dpcd[DP_MAX_DOWNSPREAD] &
				  DP_MAX_DOWNSPREAD_0_5);

	/* Set TPS support */
	mhdp->sink.pattern_supp = CDNS_SUPPORT_TPS(1) | CDNS_SUPPORT_TPS(2);
	if (drm_dp_tps3_supported(dpcd))
		mhdp->sink.pattern_supp |= CDNS_SUPPORT_TPS(3);
	if (drm_dp_tps4_supported(dpcd))
		mhdp->sink.pattern_supp |= CDNS_SUPPORT_TPS(4);

	/* Set fast link support */
	mhdp->sink.fast_link = !!(dpcd[DP_MAX_DOWNSPREAD] &
				  DP_NO_AUX_HANDSHAKE_LINK_TRAINING);
}

static int cdns_mhdp_link_up(struct cdns_mhdp_device *mhdp)
{
	u8 dpcd[DP_RECEIVER_CAP_SIZE], amp[2];
	u32 resp, interval, interval_us;
	u8 ext_cap_chk = 0;
	unsigned int addr;
	int err;

	WARN_ON(!mutex_is_locked(&mhdp->link_mutex));

	drm_dp_dpcd_readb(&mhdp->aux, DP_TRAINING_AUX_RD_INTERVAL,
			  &ext_cap_chk);

	if (ext_cap_chk & DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT)
		addr = DP_DP13_DPCD_REV;
	else
		addr = DP_DPCD_REV;

	err = drm_dp_dpcd_read(&mhdp->aux, addr, dpcd, DP_RECEIVER_CAP_SIZE);
	if (err < 0) {
		dev_err(mhdp->dev, "Failed to read receiver capabilities\n");
		return err;
	}

	mhdp->link.revision = dpcd[0];
	mhdp->link.rate = drm_dp_bw_code_to_link_rate(dpcd[1]);
	mhdp->link.num_lanes = dpcd[2] & DP_MAX_LANE_COUNT_MASK;

	if (dpcd[2] & DP_ENHANCED_FRAME_CAP)
		mhdp->link.capabilities |= DP_LINK_CAP_ENHANCED_FRAMING;

	dev_dbg(mhdp->dev, "Set sink device power state via DPCD\n");
	drm_dp_link_power_up(&mhdp->aux, mhdp->link.revision);

	cdns_mhdp_fill_sink_caps(mhdp, dpcd);

	mhdp->link.rate = cdns_mhdp_max_link_rate(mhdp);
	mhdp->link.num_lanes = cdns_mhdp_max_num_lanes(mhdp);

	/* Disable framer for link training */
	err = cdns_mhdp_reg_read(mhdp, CDNS_DP_FRAMER_GLOBAL_CONFIG, &resp);
	if (err < 0) {
		dev_err(mhdp->dev,
			"Failed to read CDNS_DP_FRAMER_GLOBAL_CONFIG %d\n",
			err);
		return err;
	}

	resp &= ~CDNS_DP_FRAMER_EN;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_FRAMER_GLOBAL_CONFIG, resp);

	/* Spread AMP if required, enable 8b/10b coding */
	amp[0] = cdns_mhdp_get_ssc_supported(mhdp) ? DP_SPREAD_AMP_0_5 : 0;
	amp[1] = DP_SET_ANSI_8B10B;
	drm_dp_dpcd_write(&mhdp->aux, DP_DOWNSPREAD_CTRL, amp, 2);

	if (mhdp->host.fast_link & mhdp->sink.fast_link) {
		dev_err(mhdp->dev, "fastlink not supported\n");
		return -EOPNOTSUPP;
	}

	interval = dpcd[DP_TRAINING_AUX_RD_INTERVAL] & DP_TRAINING_AUX_RD_MASK;
	interval_us = cdns_mhdp_get_training_interval_us(mhdp, interval);
	if (!interval_us ||
	    cdns_mhdp_link_training(mhdp, interval_us)) {
		dev_err(mhdp->dev, "Link training failed. Exiting.\n");
		return -EIO;
	}

	mhdp->link_up = true;

	return 0;
}

static void cdns_mhdp_link_down(struct cdns_mhdp_device *mhdp)
{
	WARN_ON(!mutex_is_locked(&mhdp->link_mutex));

	if (mhdp->plugged)
		drm_dp_link_power_down(&mhdp->aux, mhdp->link.revision);

	mhdp->link_up = false;
}

static const struct drm_edid *cdns_mhdp_edid_read(struct cdns_mhdp_device *mhdp,
						  struct drm_connector *connector)
{
	if (!mhdp->plugged)
		return NULL;

	return drm_edid_read_custom(connector, cdns_mhdp_get_edid_block, mhdp);
}

static int cdns_mhdp_get_modes(struct drm_connector *connector)
{
	struct cdns_mhdp_device *mhdp = connector_to_mhdp(connector);
	const struct drm_edid *drm_edid;
	int num_modes;

	if (!mhdp->plugged)
		return 0;

	drm_edid = cdns_mhdp_edid_read(mhdp, connector);

	drm_edid_connector_update(connector, drm_edid);

	if (!drm_edid) {
		dev_err(mhdp->dev, "Failed to read EDID\n");
		return 0;
	}

	num_modes = drm_edid_connector_add_modes(connector);
	drm_edid_free(drm_edid);

	/*
	 * HACK: Warn about unsupported display formats until we deal
	 *       with them correctly.
	 */
	if (connector->display_info.color_formats &&
	    !(connector->display_info.color_formats &
	      mhdp->display_fmt.color_format))
		dev_warn(mhdp->dev,
			 "%s: No supported color_format found (0x%08x)\n",
			__func__, connector->display_info.color_formats);

	if (connector->display_info.bpc &&
	    connector->display_info.bpc < mhdp->display_fmt.bpc)
		dev_warn(mhdp->dev, "%s: Display bpc only %d < %d\n",
			 __func__, connector->display_info.bpc,
			 mhdp->display_fmt.bpc);

	return num_modes;
}

static int cdns_mhdp_connector_detect(struct drm_connector *conn,
				      struct drm_modeset_acquire_ctx *ctx,
				      bool force)
{
	struct cdns_mhdp_device *mhdp = connector_to_mhdp(conn);

	return cdns_mhdp_detect(mhdp);
}

static u32 cdns_mhdp_get_bpp(struct cdns_mhdp_display_fmt *fmt)
{
	u32 bpp;

	if (fmt->y_only)
		return fmt->bpc;

	switch (fmt->color_format) {
	case DRM_COLOR_FORMAT_RGB444:
	case DRM_COLOR_FORMAT_YCBCR444:
		bpp = fmt->bpc * 3;
		break;
	case DRM_COLOR_FORMAT_YCBCR422:
		bpp = fmt->bpc * 2;
		break;
	case DRM_COLOR_FORMAT_YCBCR420:
		bpp = fmt->bpc * 3 / 2;
		break;
	default:
		bpp = fmt->bpc * 3;
		WARN_ON(1);
	}
	return bpp;
}

static
bool cdns_mhdp_bandwidth_ok(struct cdns_mhdp_device *mhdp,
			    const struct drm_display_mode *mode,
			    unsigned int lanes, unsigned int rate)
{
	u32 max_bw, req_bw, bpp;

	/*
	 * mode->clock is expressed in kHz. Multiplying by bpp and dividing by 8
	 * we get the number of kB/s. DisplayPort applies a 8b-10b encoding, the
	 * value thus equals the bandwidth in 10kb/s units, which matches the
	 * units of the rate parameter.
	 */

	bpp = cdns_mhdp_get_bpp(&mhdp->display_fmt);
	req_bw = mode->clock * bpp / 8;
	max_bw = lanes * rate;
	if (req_bw > max_bw) {
		dev_dbg(mhdp->dev,
			"Unsupported Mode: %s, Req BW: %u, Available Max BW:%u\n",
			mode->name, req_bw, max_bw);

		return false;
	}

	return true;
}

static
enum drm_mode_status cdns_mhdp_mode_valid(struct drm_connector *conn,
					  const struct drm_display_mode *mode)
{
	struct cdns_mhdp_device *mhdp = connector_to_mhdp(conn);

	mutex_lock(&mhdp->link_mutex);

	if (!cdns_mhdp_bandwidth_ok(mhdp, mode, mhdp->link.num_lanes,
				    mhdp->link.rate)) {
		mutex_unlock(&mhdp->link_mutex);
		return MODE_CLOCK_HIGH;
	}

	mutex_unlock(&mhdp->link_mutex);
	return MODE_OK;
}

static int cdns_mhdp_connector_atomic_check(struct drm_connector *conn,
					    struct drm_atomic_state *state)
{
	struct cdns_mhdp_device *mhdp = connector_to_mhdp(conn);
	struct drm_connector_state *old_state, *new_state;
	struct drm_crtc_state *crtc_state;
	u64 old_cp, new_cp;

	if (!mhdp->hdcp_supported)
		return 0;

	old_state = drm_atomic_get_old_connector_state(state, conn);
	new_state = drm_atomic_get_new_connector_state(state, conn);
	old_cp = old_state->content_protection;
	new_cp = new_state->content_protection;

	if (old_state->hdcp_content_type != new_state->hdcp_content_type &&
	    new_cp != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
		new_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		goto mode_changed;
	}

	if (!new_state->crtc) {
		if (old_cp == DRM_MODE_CONTENT_PROTECTION_ENABLED)
			new_state->content_protection = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		return 0;
	}

	if (old_cp == new_cp ||
	    (old_cp == DRM_MODE_CONTENT_PROTECTION_DESIRED &&
	     new_cp == DRM_MODE_CONTENT_PROTECTION_ENABLED))
		return 0;

mode_changed:
	crtc_state = drm_atomic_get_new_crtc_state(state, new_state->crtc);
	crtc_state->mode_changed = true;

	return 0;
}

static const struct drm_connector_helper_funcs cdns_mhdp_conn_helper_funcs = {
	.detect_ctx = cdns_mhdp_connector_detect,
	.get_modes = cdns_mhdp_get_modes,
	.mode_valid = cdns_mhdp_mode_valid,
	.atomic_check = cdns_mhdp_connector_atomic_check,
};

static const struct drm_connector_funcs cdns_mhdp_conn_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.reset = drm_atomic_helper_connector_reset,
	.destroy = drm_connector_cleanup,
};

static int cdns_mhdp_connector_init(struct cdns_mhdp_device *mhdp)
{
	u32 bus_format = MEDIA_BUS_FMT_RGB121212_1X36;
	struct drm_connector *conn = &mhdp->connector;
	struct drm_bridge *bridge = &mhdp->bridge;
	int ret;

	conn->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(bridge->dev, conn, &cdns_mhdp_conn_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		dev_err(mhdp->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(conn, &cdns_mhdp_conn_helper_funcs);

	ret = drm_display_info_set_bus_formats(&conn->display_info,
					       &bus_format, 1);
	if (ret)
		return ret;

	ret = drm_connector_attach_encoder(conn, bridge->encoder);
	if (ret) {
		dev_err(mhdp->dev, "Failed to attach connector to encoder\n");
		return ret;
	}

	if (mhdp->hdcp_supported)
		ret = drm_connector_attach_content_protection_property(conn, true);

	return ret;
}

static int cdns_mhdp_attach(struct drm_bridge *bridge,
			    struct drm_encoder *encoder,
			    enum drm_bridge_attach_flags flags)
{
	struct cdns_mhdp_device *mhdp = bridge_to_mhdp(bridge);
	bool hw_ready;
	int ret;

	dev_dbg(mhdp->dev, "%s\n", __func__);

	mhdp->aux.drm_dev = bridge->dev;
	ret = drm_dp_aux_register(&mhdp->aux);
	if (ret < 0)
		return ret;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		ret = cdns_mhdp_connector_init(mhdp);
		if (ret)
			goto aux_unregister;
	}

	spin_lock(&mhdp->start_lock);

	mhdp->bridge_attached = true;
	hw_ready = mhdp->hw_state == MHDP_HW_READY;

	spin_unlock(&mhdp->start_lock);

	/* Enable SW event interrupts */
	if (hw_ready)
		cdns_mhdp_bridge_hpd_enable(bridge);

	return 0;
aux_unregister:
	drm_dp_aux_unregister(&mhdp->aux);
	return ret;
}

static void cdns_mhdp_configure_video(struct cdns_mhdp_device *mhdp,
				      const struct drm_display_mode *mode)
{
	unsigned int dp_framer_sp = 0, msa_horizontal_1,
		msa_vertical_1, bnd_hsync2vsync, hsync2vsync_pol_ctrl,
		misc0 = 0, misc1 = 0, pxl_repr,
		front_porch, back_porch, msa_h0, msa_v0, hsync, vsync,
		dp_vertical_1;
	u8 stream_id = mhdp->stream_id;
	u32 bpp, bpc, pxlfmt, framer;
	int ret;

	pxlfmt = mhdp->display_fmt.color_format;
	bpc = mhdp->display_fmt.bpc;

	/*
	 * If YCBCR supported and stream not SD, use ITU709
	 * Need to handle ITU version with YCBCR420 when supported
	 */
	if ((pxlfmt == DRM_COLOR_FORMAT_YCBCR444 ||
	     pxlfmt == DRM_COLOR_FORMAT_YCBCR422) && mode->crtc_vdisplay >= 720)
		misc0 = DP_YCBCR_COEFFICIENTS_ITU709;

	bpp = cdns_mhdp_get_bpp(&mhdp->display_fmt);

	switch (pxlfmt) {
	case DRM_COLOR_FORMAT_RGB444:
		pxl_repr = CDNS_DP_FRAMER_RGB << CDNS_DP_FRAMER_PXL_FORMAT;
		misc0 |= DP_COLOR_FORMAT_RGB;
		break;
	case DRM_COLOR_FORMAT_YCBCR444:
		pxl_repr = CDNS_DP_FRAMER_YCBCR444 << CDNS_DP_FRAMER_PXL_FORMAT;
		misc0 |= DP_COLOR_FORMAT_YCbCr444 | DP_TEST_DYNAMIC_RANGE_CEA;
		break;
	case DRM_COLOR_FORMAT_YCBCR422:
		pxl_repr = CDNS_DP_FRAMER_YCBCR422 << CDNS_DP_FRAMER_PXL_FORMAT;
		misc0 |= DP_COLOR_FORMAT_YCbCr422 | DP_TEST_DYNAMIC_RANGE_CEA;
		break;
	case DRM_COLOR_FORMAT_YCBCR420:
		pxl_repr = CDNS_DP_FRAMER_YCBCR420 << CDNS_DP_FRAMER_PXL_FORMAT;
		break;
	default:
		pxl_repr = CDNS_DP_FRAMER_Y_ONLY << CDNS_DP_FRAMER_PXL_FORMAT;
	}

	switch (bpc) {
	case 6:
		misc0 |= DP_TEST_BIT_DEPTH_6;
		pxl_repr |= CDNS_DP_FRAMER_6_BPC;
		break;
	case 8:
		misc0 |= DP_TEST_BIT_DEPTH_8;
		pxl_repr |= CDNS_DP_FRAMER_8_BPC;
		break;
	case 10:
		misc0 |= DP_TEST_BIT_DEPTH_10;
		pxl_repr |= CDNS_DP_FRAMER_10_BPC;
		break;
	case 12:
		misc0 |= DP_TEST_BIT_DEPTH_12;
		pxl_repr |= CDNS_DP_FRAMER_12_BPC;
		break;
	case 16:
		misc0 |= DP_TEST_BIT_DEPTH_16;
		pxl_repr |= CDNS_DP_FRAMER_16_BPC;
		break;
	}

	bnd_hsync2vsync = CDNS_IP_BYPASS_V_INTERFACE;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		bnd_hsync2vsync |= CDNS_IP_DET_INTERLACE_FORMAT;

	cdns_mhdp_reg_write(mhdp, CDNS_BND_HSYNC2VSYNC(stream_id),
			    bnd_hsync2vsync);

	hsync2vsync_pol_ctrl = 0;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		hsync2vsync_pol_ctrl |= CDNS_H2V_HSYNC_POL_ACTIVE_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		hsync2vsync_pol_ctrl |= CDNS_H2V_VSYNC_POL_ACTIVE_LOW;
	cdns_mhdp_reg_write(mhdp, CDNS_HSYNC2VSYNC_POL_CTRL(stream_id),
			    hsync2vsync_pol_ctrl);

	cdns_mhdp_reg_write(mhdp, CDNS_DP_FRAMER_PXL_REPR(stream_id), pxl_repr);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		dp_framer_sp |= CDNS_DP_FRAMER_INTERLACE;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		dp_framer_sp |= CDNS_DP_FRAMER_HSYNC_POL_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		dp_framer_sp |= CDNS_DP_FRAMER_VSYNC_POL_LOW;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_FRAMER_SP(stream_id), dp_framer_sp);

	front_porch = mode->crtc_hsync_start - mode->crtc_hdisplay;
	back_porch = mode->crtc_htotal - mode->crtc_hsync_end;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_FRONT_BACK_PORCH(stream_id),
			    CDNS_DP_FRONT_PORCH(front_porch) |
			    CDNS_DP_BACK_PORCH(back_porch));

	cdns_mhdp_reg_write(mhdp, CDNS_DP_BYTE_COUNT(stream_id),
			    mode->crtc_hdisplay * bpp / 8);

	msa_h0 = mode->crtc_htotal - mode->crtc_hsync_start;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_MSA_HORIZONTAL_0(stream_id),
			    CDNS_DP_MSAH0_H_TOTAL(mode->crtc_htotal) |
			    CDNS_DP_MSAH0_HSYNC_START(msa_h0));

	hsync = mode->crtc_hsync_end - mode->crtc_hsync_start;
	msa_horizontal_1 = CDNS_DP_MSAH1_HSYNC_WIDTH(hsync) |
			   CDNS_DP_MSAH1_HDISP_WIDTH(mode->crtc_hdisplay);
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		msa_horizontal_1 |= CDNS_DP_MSAH1_HSYNC_POL_LOW;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_MSA_HORIZONTAL_1(stream_id),
			    msa_horizontal_1);

	msa_v0 = mode->crtc_vtotal - mode->crtc_vsync_start;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_MSA_VERTICAL_0(stream_id),
			    CDNS_DP_MSAV0_V_TOTAL(mode->crtc_vtotal) |
			    CDNS_DP_MSAV0_VSYNC_START(msa_v0));

	vsync = mode->crtc_vsync_end - mode->crtc_vsync_start;
	msa_vertical_1 = CDNS_DP_MSAV1_VSYNC_WIDTH(vsync) |
			 CDNS_DP_MSAV1_VDISP_WIDTH(mode->crtc_vdisplay);
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		msa_vertical_1 |= CDNS_DP_MSAV1_VSYNC_POL_LOW;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_MSA_VERTICAL_1(stream_id),
			    msa_vertical_1);

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) &&
	    mode->crtc_vtotal % 2 == 0)
		misc1 = DP_TEST_INTERLACED;
	if (mhdp->display_fmt.y_only)
		misc1 |= CDNS_DP_TEST_COLOR_FORMAT_RAW_Y_ONLY;
	/* Use VSC SDP for Y420 */
	if (pxlfmt == DRM_COLOR_FORMAT_YCBCR420)
		misc1 = CDNS_DP_TEST_VSC_SDP;

	cdns_mhdp_reg_write(mhdp, CDNS_DP_MSA_MISC(stream_id),
			    misc0 | (misc1 << 8));

	cdns_mhdp_reg_write(mhdp, CDNS_DP_HORIZONTAL(stream_id),
			    CDNS_DP_H_HSYNC_WIDTH(hsync) |
			    CDNS_DP_H_H_TOTAL(mode->crtc_hdisplay));

	cdns_mhdp_reg_write(mhdp, CDNS_DP_VERTICAL_0(stream_id),
			    CDNS_DP_V0_VHEIGHT(mode->crtc_vdisplay) |
			    CDNS_DP_V0_VSTART(msa_v0));

	dp_vertical_1 = CDNS_DP_V1_VTOTAL(mode->crtc_vtotal);
	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) &&
	    mode->crtc_vtotal % 2 == 0)
		dp_vertical_1 |= CDNS_DP_V1_VTOTAL_EVEN;

	cdns_mhdp_reg_write(mhdp, CDNS_DP_VERTICAL_1(stream_id), dp_vertical_1);

	cdns_mhdp_reg_write_bit(mhdp, CDNS_DP_VB_ID(stream_id), 2, 1,
				(mode->flags & DRM_MODE_FLAG_INTERLACE) ?
				CDNS_DP_VB_ID_INTERLACED : 0);

	ret = cdns_mhdp_reg_read(mhdp, CDNS_DP_FRAMER_GLOBAL_CONFIG, &framer);
	if (ret < 0) {
		dev_err(mhdp->dev,
			"Failed to read CDNS_DP_FRAMER_GLOBAL_CONFIG %d\n",
			ret);
		return;
	}
	framer |= CDNS_DP_FRAMER_EN;
	framer &= ~CDNS_DP_NO_VIDEO_MODE;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_FRAMER_GLOBAL_CONFIG, framer);
}

static void cdns_mhdp_sst_enable(struct cdns_mhdp_device *mhdp,
				 const struct drm_display_mode *mode)
{
	u32 rate, vs, required_bandwidth, available_bandwidth;
	s32 line_thresh1, line_thresh2, line_thresh = 0;
	int pxlclock = mode->crtc_clock;
	u32 tu_size = 64;
	u32 bpp;

	/* Get rate in MSymbols per second per lane */
	rate = mhdp->link.rate / 1000;

	bpp = cdns_mhdp_get_bpp(&mhdp->display_fmt);

	required_bandwidth = pxlclock * bpp / 8;
	available_bandwidth = mhdp->link.num_lanes * rate;

	vs = tu_size * required_bandwidth / available_bandwidth;
	vs /= 1000;

	if (vs == tu_size)
		vs = tu_size - 1;

	line_thresh1 = ((vs + 1) << 5) * 8 / bpp;
	line_thresh2 = (pxlclock << 5) / 1000 / rate * (vs + 1) - (1 << 5);
	line_thresh = line_thresh1 - line_thresh2 / (s32)mhdp->link.num_lanes;
	line_thresh = (line_thresh >> 5) + 2;

	mhdp->stream_id = 0;

	cdns_mhdp_reg_write(mhdp, CDNS_DP_FRAMER_TU,
			    CDNS_DP_FRAMER_TU_VS(vs) |
			    CDNS_DP_FRAMER_TU_SIZE(tu_size) |
			    CDNS_DP_FRAMER_TU_CNT_RST_EN);

	cdns_mhdp_reg_write(mhdp, CDNS_DP_LINE_THRESH(0),
			    line_thresh & GENMASK(5, 0));

	cdns_mhdp_reg_write(mhdp, CDNS_DP_STREAM_CONFIG_2(0),
			    CDNS_DP_SC2_TU_VS_DIFF((tu_size - vs > 3) ?
						   0 : tu_size - vs));

	cdns_mhdp_configure_video(mhdp, mode);
}

static void cdns_mhdp_atomic_enable(struct drm_bridge *bridge,
				    struct drm_atomic_state *state)
{
	struct cdns_mhdp_device *mhdp = bridge_to_mhdp(bridge);
	struct cdns_mhdp_bridge_state *mhdp_state;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	struct drm_bridge_state *new_state;
	const struct drm_display_mode *mode;
	u32 resp;
	int ret;

	dev_dbg(mhdp->dev, "bridge enable\n");

	mutex_lock(&mhdp->link_mutex);

	if (mhdp->plugged && !mhdp->link_up) {
		ret = cdns_mhdp_link_up(mhdp);
		if (ret < 0)
			goto out;
	}

	if (mhdp->info && mhdp->info->ops && mhdp->info->ops->enable)
		mhdp->info->ops->enable(mhdp);

	/* Enable VIF clock for stream 0 */
	ret = cdns_mhdp_reg_read(mhdp, CDNS_DPTX_CAR, &resp);
	if (ret < 0) {
		dev_err(mhdp->dev, "Failed to read CDNS_DPTX_CAR %d\n", ret);
		goto out;
	}

	cdns_mhdp_reg_write(mhdp, CDNS_DPTX_CAR,
			    resp | CDNS_VIF_CLK_EN | CDNS_VIF_CLK_RSTN);

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	if (WARN_ON(!connector))
		goto out;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		goto out;

	if (mhdp->hdcp_supported &&
	    mhdp->hw_state == MHDP_HW_READY &&
	    conn_state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		mutex_unlock(&mhdp->link_mutex);
		cdns_mhdp_hdcp_enable(mhdp, conn_state->hdcp_content_type);
		mutex_lock(&mhdp->link_mutex);
	}

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		goto out;

	mode = &crtc_state->adjusted_mode;

	new_state = drm_atomic_get_new_bridge_state(state, bridge);
	if (WARN_ON(!new_state))
		goto out;

	if (!cdns_mhdp_bandwidth_ok(mhdp, mode, mhdp->link.num_lanes,
				    mhdp->link.rate)) {
		ret = -EINVAL;
		goto out;
	}

	cdns_mhdp_sst_enable(mhdp, mode);

	mhdp_state = to_cdns_mhdp_bridge_state(new_state);

	mhdp_state->current_mode = drm_mode_duplicate(bridge->dev, mode);
	if (!mhdp_state->current_mode) {
		ret = -EINVAL;
		goto out;
	}

	drm_mode_set_name(mhdp_state->current_mode);

	dev_dbg(mhdp->dev, "%s: Enabling mode %s\n", __func__, mode->name);

	mhdp->bridge_enabled = true;

out:
	mutex_unlock(&mhdp->link_mutex);
	if (ret < 0)
		schedule_work(&mhdp->modeset_retry_work);
}

static void cdns_mhdp_atomic_disable(struct drm_bridge *bridge,
				     struct drm_atomic_state *state)
{
	struct cdns_mhdp_device *mhdp = bridge_to_mhdp(bridge);
	u32 resp;

	dev_dbg(mhdp->dev, "%s\n", __func__);

	mutex_lock(&mhdp->link_mutex);

	if (mhdp->hdcp_supported)
		cdns_mhdp_hdcp_disable(mhdp);

	mhdp->bridge_enabled = false;
	cdns_mhdp_reg_read(mhdp, CDNS_DP_FRAMER_GLOBAL_CONFIG, &resp);
	resp &= ~CDNS_DP_FRAMER_EN;
	resp |= CDNS_DP_NO_VIDEO_MODE;
	cdns_mhdp_reg_write(mhdp, CDNS_DP_FRAMER_GLOBAL_CONFIG, resp);

	cdns_mhdp_link_down(mhdp);

	/* Disable VIF clock for stream 0 */
	cdns_mhdp_reg_read(mhdp, CDNS_DPTX_CAR, &resp);
	cdns_mhdp_reg_write(mhdp, CDNS_DPTX_CAR,
			    resp & ~(CDNS_VIF_CLK_EN | CDNS_VIF_CLK_RSTN));

	if (mhdp->info && mhdp->info->ops && mhdp->info->ops->disable)
		mhdp->info->ops->disable(mhdp);

	mutex_unlock(&mhdp->link_mutex);
}

static void cdns_mhdp_detach(struct drm_bridge *bridge)
{
	struct cdns_mhdp_device *mhdp = bridge_to_mhdp(bridge);

	dev_dbg(mhdp->dev, "%s\n", __func__);

	drm_dp_aux_unregister(&mhdp->aux);

	spin_lock(&mhdp->start_lock);

	mhdp->bridge_attached = false;

	spin_unlock(&mhdp->start_lock);

	writel(~0, mhdp->regs + CDNS_APB_INT_MASK);
}

static struct drm_bridge_state *
cdns_mhdp_bridge_atomic_duplicate_state(struct drm_bridge *bridge)
{
	struct cdns_mhdp_bridge_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_bridge_duplicate_state(bridge, &state->base);

	return &state->base;
}

static void
cdns_mhdp_bridge_atomic_destroy_state(struct drm_bridge *bridge,
				      struct drm_bridge_state *state)
{
	struct cdns_mhdp_bridge_state *cdns_mhdp_state;

	cdns_mhdp_state = to_cdns_mhdp_bridge_state(state);

	if (cdns_mhdp_state->current_mode) {
		drm_mode_destroy(bridge->dev, cdns_mhdp_state->current_mode);
		cdns_mhdp_state->current_mode = NULL;
	}

	kfree(cdns_mhdp_state);
}

static struct drm_bridge_state *
cdns_mhdp_bridge_atomic_reset(struct drm_bridge *bridge)
{
	struct cdns_mhdp_bridge_state *cdns_mhdp_state;

	cdns_mhdp_state = kzalloc(sizeof(*cdns_mhdp_state), GFP_KERNEL);
	if (!cdns_mhdp_state)
		return NULL;

	__drm_atomic_helper_bridge_reset(bridge, &cdns_mhdp_state->base);

	return &cdns_mhdp_state->base;
}

static u32 *cdns_mhdp_get_input_bus_fmts(struct drm_bridge *bridge,
					 struct drm_bridge_state *bridge_state,
					 struct drm_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state,
					 u32 output_fmt,
					 unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kzalloc(sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	*num_input_fmts = 1;
	input_fmts[0] = MEDIA_BUS_FMT_RGB121212_1X36;

	return input_fmts;
}

static int cdns_mhdp_atomic_check(struct drm_bridge *bridge,
				  struct drm_bridge_state *bridge_state,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{
	struct cdns_mhdp_device *mhdp = bridge_to_mhdp(bridge);
	const struct drm_display_mode *mode = &crtc_state->adjusted_mode;

	mutex_lock(&mhdp->link_mutex);

	if (!cdns_mhdp_bandwidth_ok(mhdp, mode, mhdp->link.num_lanes,
				    mhdp->link.rate)) {
		dev_err(mhdp->dev, "%s: Not enough BW for %s (%u lanes at %u Mbps)\n",
			__func__, mode->name, mhdp->link.num_lanes,
			mhdp->link.rate / 100);
		mutex_unlock(&mhdp->link_mutex);
		return -EINVAL;
	}

	/*
	 * There might be flags negotiation supported in future.
	 * Set the bus flags in atomic_check statically for now.
	 */
	if (mhdp->info)
		bridge_state->input_bus_cfg.flags = *mhdp->info->input_bus_flags;

	mutex_unlock(&mhdp->link_mutex);
	return 0;
}

static enum drm_connector_status
cdns_mhdp_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct cdns_mhdp_device *mhdp = bridge_to_mhdp(bridge);

	return cdns_mhdp_detect(mhdp);
}

static const struct drm_edid *cdns_mhdp_bridge_edid_read(struct drm_bridge *bridge,
							 struct drm_connector *connector)
{
	struct cdns_mhdp_device *mhdp = bridge_to_mhdp(bridge);

	return cdns_mhdp_edid_read(mhdp, connector);
}

static const struct drm_bridge_funcs cdns_mhdp_bridge_funcs = {
	.atomic_enable = cdns_mhdp_atomic_enable,
	.atomic_disable = cdns_mhdp_atomic_disable,
	.atomic_check = cdns_mhdp_atomic_check,
	.attach = cdns_mhdp_attach,
	.detach = cdns_mhdp_detach,
	.atomic_duplicate_state = cdns_mhdp_bridge_atomic_duplicate_state,
	.atomic_destroy_state = cdns_mhdp_bridge_atomic_destroy_state,
	.atomic_reset = cdns_mhdp_bridge_atomic_reset,
	.atomic_get_input_bus_fmts = cdns_mhdp_get_input_bus_fmts,
	.detect = cdns_mhdp_bridge_detect,
	.edid_read = cdns_mhdp_bridge_edid_read,
	.hpd_enable = cdns_mhdp_bridge_hpd_enable,
	.hpd_disable = cdns_mhdp_bridge_hpd_disable,
};

static bool cdns_mhdp_detect_hpd(struct cdns_mhdp_device *mhdp, bool *hpd_pulse)
{
	int hpd_event, hpd_status;

	*hpd_pulse = false;

	hpd_event = cdns_mhdp_read_hpd_event(mhdp);

	/* Getting event bits failed, bail out */
	if (hpd_event < 0) {
		dev_warn(mhdp->dev, "%s: read event failed: %d\n",
			 __func__, hpd_event);
		return false;
	}

	hpd_status = cdns_mhdp_get_hpd_status(mhdp);
	if (hpd_status < 0) {
		dev_warn(mhdp->dev, "%s: get hpd status failed: %d\n",
			 __func__, hpd_status);
		return false;
	}

	if (hpd_event & DPTX_READ_EVENT_HPD_PULSE)
		*hpd_pulse = true;

	return !!hpd_status;
}

static int cdns_mhdp_update_link_status(struct cdns_mhdp_device *mhdp)
{
	struct cdns_mhdp_bridge_state *cdns_bridge_state;
	struct drm_display_mode *current_mode;
	bool old_plugged = mhdp->plugged;
	struct drm_bridge_state *state;
	u8 status[DP_LINK_STATUS_SIZE];
	bool hpd_pulse;
	int ret = 0;

	mutex_lock(&mhdp->link_mutex);

	mhdp->plugged = cdns_mhdp_detect_hpd(mhdp, &hpd_pulse);

	if (!mhdp->plugged) {
		cdns_mhdp_link_down(mhdp);
		mhdp->link.rate = mhdp->host.link_rate;
		mhdp->link.num_lanes = mhdp->host.lanes_cnt;
		goto out;
	}

	/*
	 * If we get a HPD pulse event and we were and still are connected,
	 * check the link status. If link status is ok, there's nothing to do
	 * as we don't handle DP interrupts. If link status is bad, continue
	 * with full link setup.
	 */
	if (hpd_pulse && old_plugged == mhdp->plugged) {
		ret = drm_dp_dpcd_read_link_status(&mhdp->aux, status);

		/*
		 * If everything looks fine, just return, as we don't handle
		 * DP IRQs.
		 */
		if (!ret &&
		    drm_dp_channel_eq_ok(status, mhdp->link.num_lanes) &&
		    drm_dp_clock_recovery_ok(status, mhdp->link.num_lanes))
			goto out;

		/* If link is bad, mark link as down so that we do a new LT */
		mhdp->link_up = false;
	}

	if (!mhdp->link_up) {
		ret = cdns_mhdp_link_up(mhdp);
		if (ret < 0)
			goto out;
	}

	if (mhdp->bridge_enabled) {
		state = drm_priv_to_bridge_state(mhdp->bridge.base.state);
		if (!state) {
			ret = -EINVAL;
			goto out;
		}

		cdns_bridge_state = to_cdns_mhdp_bridge_state(state);
		if (!cdns_bridge_state) {
			ret = -EINVAL;
			goto out;
		}

		current_mode = cdns_bridge_state->current_mode;
		if (!current_mode) {
			ret = -EINVAL;
			goto out;
		}

		if (!cdns_mhdp_bandwidth_ok(mhdp, current_mode, mhdp->link.num_lanes,
					    mhdp->link.rate)) {
			ret = -EINVAL;
			goto out;
		}

		dev_dbg(mhdp->dev, "%s: Enabling mode %s\n", __func__,
			current_mode->name);

		cdns_mhdp_sst_enable(mhdp, current_mode);
	}
out:
	mutex_unlock(&mhdp->link_mutex);
	return ret;
}

static void cdns_mhdp_modeset_retry_fn(struct work_struct *work)
{
	struct cdns_mhdp_device *mhdp;
	struct drm_connector *conn;

	mhdp = container_of(work, typeof(*mhdp), modeset_retry_work);

	conn = &mhdp->connector;

	/* Grab the locks before changing connector property */
	mutex_lock(&conn->dev->mode_config.mutex);

	/*
	 * Set connector link status to BAD and send a Uevent to notify
	 * userspace to do a modeset.
	 */
	drm_connector_set_link_status_property(conn, DRM_MODE_LINK_STATUS_BAD);
	mutex_unlock(&conn->dev->mode_config.mutex);

	/* Send Hotplug uevent so userspace can reprobe */
	drm_kms_helper_hotplug_event(mhdp->bridge.dev);
}

static irqreturn_t cdns_mhdp_irq_handler(int irq, void *data)
{
	struct cdns_mhdp_device *mhdp = data;
	u32 apb_stat, sw_ev0;
	bool bridge_attached;

	apb_stat = readl(mhdp->regs + CDNS_APB_INT_STATUS);
	if (!(apb_stat & CDNS_APB_INT_MASK_SW_EVENT_INT))
		return IRQ_NONE;

	sw_ev0 = readl(mhdp->regs + CDNS_SW_EVENT0);

	/*
	 *  Calling drm_kms_helper_hotplug_event() when not attached
	 *  to drm device causes an oops because the drm_bridge->dev
	 *  is NULL. See cdns_mhdp_fw_cb() comments for details about the
	 *  problems related drm_kms_helper_hotplug_event() call.
	 */
	spin_lock(&mhdp->start_lock);
	bridge_attached = mhdp->bridge_attached;
	spin_unlock(&mhdp->start_lock);

	if (bridge_attached && (sw_ev0 & CDNS_DPTX_HPD)) {
		schedule_work(&mhdp->hpd_work);
	}

	if (sw_ev0 & ~CDNS_DPTX_HPD) {
		mhdp->sw_events |= (sw_ev0 & ~CDNS_DPTX_HPD);
		wake_up(&mhdp->sw_events_wq);
	}

	return IRQ_HANDLED;
}

u32 cdns_mhdp_wait_for_sw_event(struct cdns_mhdp_device *mhdp, u32 event)
{
	u32 ret;

	ret = wait_event_timeout(mhdp->sw_events_wq,
				 mhdp->sw_events & event,
				 msecs_to_jiffies(500));
	if (!ret) {
		dev_dbg(mhdp->dev, "SW event 0x%x timeout\n", event);
		goto sw_event_out;
	}

	ret = mhdp->sw_events;
	mhdp->sw_events &= ~event;

sw_event_out:
	return ret;
}

static void cdns_mhdp_hpd_work(struct work_struct *work)
{
	struct cdns_mhdp_device *mhdp = container_of(work,
						     struct cdns_mhdp_device,
						     hpd_work);
	int ret;

	ret = cdns_mhdp_update_link_status(mhdp);
	if (mhdp->connector.dev) {
		if (ret < 0)
			schedule_work(&mhdp->modeset_retry_work);
		else
			drm_kms_helper_hotplug_event(mhdp->bridge.dev);
	} else {
		drm_bridge_hpd_notify(&mhdp->bridge, cdns_mhdp_detect(mhdp));
	}
}

static int cdns_mhdp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cdns_mhdp_device *mhdp;
	unsigned long rate;
	struct clk *clk;
	int ret;
	int irq;

	mhdp = devm_drm_bridge_alloc(dev, struct cdns_mhdp_device, bridge,
				     &cdns_mhdp_bridge_funcs);
	if (IS_ERR(mhdp))
		return PTR_ERR(mhdp);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "couldn't get and enable clk: %ld\n", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	mhdp->clk = clk;
	mhdp->dev = dev;
	mutex_init(&mhdp->mbox_mutex);
	mutex_init(&mhdp->link_mutex);
	spin_lock_init(&mhdp->start_lock);

	drm_dp_aux_init(&mhdp->aux);
	mhdp->aux.dev = dev;
	mhdp->aux.transfer = cdns_mhdp_transfer;

	mhdp->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mhdp->regs)) {
		dev_err(dev, "Failed to get memory resource\n");
		return PTR_ERR(mhdp->regs);
	}

	mhdp->sapb_regs = devm_platform_ioremap_resource_byname(pdev, "mhdptx-sapb");
	if (IS_ERR(mhdp->sapb_regs)) {
		mhdp->hdcp_supported = false;
		dev_warn(dev,
			 "Failed to get SAPB memory resource, HDCP not supported\n");
	} else {
		mhdp->hdcp_supported = true;
	}

	mhdp->phy = devm_of_phy_get_by_index(dev, pdev->dev.of_node, 0);
	if (IS_ERR(mhdp->phy)) {
		dev_err(dev, "no PHY configured\n");
		return PTR_ERR(mhdp->phy);
	}

	platform_set_drvdata(pdev, mhdp);

	mhdp->info = of_device_get_match_data(dev);

	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_resume_and_get failed\n");
		pm_runtime_disable(dev);
		return ret;
	}

	if (mhdp->info && mhdp->info->ops && mhdp->info->ops->init) {
		ret = mhdp->info->ops->init(mhdp);
		if (ret != 0) {
			dev_err(dev, "MHDP platform initialization failed: %d\n",
				ret);
			goto runtime_put;
		}
	}

	rate = clk_get_rate(clk);
	writel(rate % 1000000, mhdp->regs + CDNS_SW_CLK_L);
	writel(rate / 1000000, mhdp->regs + CDNS_SW_CLK_H);

	dev_dbg(dev, "func clk rate %lu Hz\n", rate);

	writel(~0, mhdp->regs + CDNS_APB_INT_MASK);

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_threaded_irq(mhdp->dev, irq, NULL,
					cdns_mhdp_irq_handler, IRQF_ONESHOT,
					"mhdp8546", mhdp);
	if (ret) {
		dev_err(dev, "cannot install IRQ %d\n", irq);
		ret = -EIO;
		goto plat_fini;
	}

	cdns_mhdp_fill_host_caps(mhdp);

	/* Initialize link rate and num of lanes to host values */
	mhdp->link.rate = mhdp->host.link_rate;
	mhdp->link.num_lanes = mhdp->host.lanes_cnt;

	/* The only currently supported format */
	mhdp->display_fmt.y_only = false;
	mhdp->display_fmt.color_format = DRM_COLOR_FORMAT_RGB444;
	mhdp->display_fmt.bpc = 8;

	mhdp->bridge.of_node = pdev->dev.of_node;
	mhdp->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID |
			   DRM_BRIDGE_OP_HPD;
	mhdp->bridge.type = DRM_MODE_CONNECTOR_DisplayPort;

	ret = phy_init(mhdp->phy);
	if (ret) {
		dev_err(mhdp->dev, "Failed to initialize PHY: %d\n", ret);
		goto plat_fini;
	}

	/* Initialize the work for modeset in case of link train failure */
	INIT_WORK(&mhdp->modeset_retry_work, cdns_mhdp_modeset_retry_fn);
	INIT_WORK(&mhdp->hpd_work, cdns_mhdp_hpd_work);

	init_waitqueue_head(&mhdp->fw_load_wq);
	init_waitqueue_head(&mhdp->sw_events_wq);

	ret = cdns_mhdp_load_firmware(mhdp);
	if (ret)
		goto phy_exit;

	if (mhdp->hdcp_supported)
		cdns_mhdp_hdcp_init(mhdp);

	drm_bridge_add(&mhdp->bridge);

	return 0;

phy_exit:
	phy_exit(mhdp->phy);
plat_fini:
	if (mhdp->info && mhdp->info->ops && mhdp->info->ops->exit)
		mhdp->info->ops->exit(mhdp);
runtime_put:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return ret;
}

static void cdns_mhdp_remove(struct platform_device *pdev)
{
	struct cdns_mhdp_device *mhdp = platform_get_drvdata(pdev);
	unsigned long timeout = msecs_to_jiffies(100);
	int ret;

	drm_bridge_remove(&mhdp->bridge);

	ret = wait_event_timeout(mhdp->fw_load_wq,
				 mhdp->hw_state == MHDP_HW_READY,
				 timeout);
	spin_lock(&mhdp->start_lock);
	mhdp->hw_state = MHDP_HW_STOPPED;
	spin_unlock(&mhdp->start_lock);

	if (ret == 0) {
		dev_err(mhdp->dev, "%s: Timeout waiting for fw loading\n",
			__func__);
	} else {
		ret = cdns_mhdp_set_firmware_active(mhdp, false);
		if (ret)
			dev_err(mhdp->dev, "Failed to stop firmware (%pe)\n",
				ERR_PTR(ret));
	}

	phy_exit(mhdp->phy);

	if (mhdp->info && mhdp->info->ops && mhdp->info->ops->exit)
		mhdp->info->ops->exit(mhdp);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	cancel_work_sync(&mhdp->modeset_retry_work);
	flush_work(&mhdp->hpd_work);
	/* Ignoring mhdp->hdcp.check_work and mhdp->hdcp.prop_work here. */
}

static const struct of_device_id mhdp_ids[] = {
	{ .compatible = "cdns,mhdp8546", },
#ifdef CONFIG_DRM_CDNS_MHDP8546_J721E
	{ .compatible = "ti,j721e-mhdp8546",
	  .data = &(const struct cdns_mhdp_platform_info) {
		  .input_bus_flags = &mhdp_ti_j721e_bridge_input_bus_flags,
		  .ops = &mhdp_ti_j721e_ops,
	  },
	},
#endif
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mhdp_ids);

static struct platform_driver mhdp_driver = {
	.driver	= {
		.name		= "cdns-mhdp8546",
		.of_match_table	= mhdp_ids,
	},
	.probe	= cdns_mhdp_probe,
	.remove = cdns_mhdp_remove,
};
module_platform_driver(mhdp_driver);

MODULE_FIRMWARE(FW_NAME);

MODULE_AUTHOR("Quentin Schulz <quentin.schulz@free-electrons.com>");
MODULE_AUTHOR("Swapnil Jakhade <sjakhade@cadence.com>");
MODULE_AUTHOR("Yuti Amonkar <yamonkar@cadence.com>");
MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_AUTHOR("Jyri Sarha <jsarha@ti.com>");
MODULE_DESCRIPTION("Cadence MHDP8546 DP bridge driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cdns-mhdp8546");
