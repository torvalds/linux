/*
 * This file contains the handling of command.
 * It prepares command and sends it to firmware when it is ready.
 */

#include <linux/hardirq.h>
#include <linux/kfifo.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/if_arp.h>
#include <linux/export.h>

#include "decl.h"
#include "cfg.h"
#include "cmd.h"

#define CAL_NF(nf)		((s32)(-(s32)(nf)))
#define CAL_RSSI(snr, nf)	((s32)((s32)(snr) + CAL_NF(nf)))

/**
 * lbs_cmd_copyback - Simple callback that copies response back into command
 *
 * @priv:	A pointer to &struct lbs_private structure
 * @extra:	A pointer to the original command structure for which
 *		'resp' is a response
 * @resp:	A pointer to the command response
 *
 * returns:	0 on success, error on failure
 */
int lbs_cmd_copyback(struct lbs_private *priv, unsigned long extra,
		     struct cmd_header *resp)
{
	struct cmd_header *buf = (void *)extra;
	uint16_t copy_len;

	copy_len = min(le16_to_cpu(buf->size), le16_to_cpu(resp->size));
	memcpy(buf, resp, copy_len);
	return 0;
}
EXPORT_SYMBOL_GPL(lbs_cmd_copyback);

/**
 *  lbs_cmd_async_callback - Simple callback that ignores the result.
 *  Use this if you just want to send a command to the hardware, but don't
 *  care for the result.
 *
 *  @priv:	ignored
 *  @extra:	ignored
 *  @resp:	ignored
 *
 *  returns:	0 for success
 */
static int lbs_cmd_async_callback(struct lbs_private *priv, unsigned long extra,
		     struct cmd_header *resp)
{
	return 0;
}


/**
 *  is_command_allowed_in_ps - tests if a command is allowed in Power Save mode
 *
 *  @cmd:	the command ID
 *
 *  returns:	1 if allowed, 0 if not allowed
 */
static u8 is_command_allowed_in_ps(u16 cmd)
{
	switch (cmd) {
	case CMD_802_11_RSSI:
		return 1;
	case CMD_802_11_HOST_SLEEP_CFG:
		return 1;
	default:
		break;
	}
	return 0;
}

/**
 *  lbs_update_hw_spec - Updates the hardware details like MAC address
 *  and regulatory region
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *
 *  returns:	0 on success, error on failure
 */
int lbs_update_hw_spec(struct lbs_private *priv)
{
	struct cmd_ds_get_hw_spec cmd;
	int ret = -1;
	u32 i;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	memcpy(cmd.permanentaddr, priv->current_addr, ETH_ALEN);
	ret = lbs_cmd_with_response(priv, CMD_GET_HW_SPEC, &cmd);
	if (ret)
		goto out;

	priv->fwcapinfo = le32_to_cpu(cmd.fwcapinfo);

	/* The firmware release is in an interesting format: the patch
	 * level is in the most significant nibble ... so fix that: */
	priv->fwrelease = le32_to_cpu(cmd.fwrelease);
	priv->fwrelease = (priv->fwrelease << 8) |
		(priv->fwrelease >> 24 & 0xff);

	/* Some firmware capabilities:
	 * CF card    firmware 5.0.16p0:   cap 0x00000303
	 * USB dongle firmware 5.110.17p2: cap 0x00000303
	 */
	netdev_info(priv->dev, "%pM, fw %u.%u.%up%u, cap 0x%08x\n",
		cmd.permanentaddr,
		priv->fwrelease >> 24 & 0xff,
		priv->fwrelease >> 16 & 0xff,
		priv->fwrelease >>  8 & 0xff,
		priv->fwrelease       & 0xff,
		priv->fwcapinfo);
	lbs_deb_cmd("GET_HW_SPEC: hardware interface 0x%x, hardware spec 0x%04x\n",
		    cmd.hwifversion, cmd.version);

	/* Clamp region code to 8-bit since FW spec indicates that it should
	 * only ever be 8-bit, even though the field size is 16-bit.  Some firmware
	 * returns non-zero high 8 bits here.
	 *
	 * Firmware version 4.0.102 used in CF8381 has region code shifted.  We
	 * need to check for this problem and handle it properly.
	 */
	if (MRVL_FW_MAJOR_REV(priv->fwrelease) == MRVL_FW_V4)
		priv->regioncode = (le16_to_cpu(cmd.regioncode) >> 8) & 0xFF;
	else
		priv->regioncode = le16_to_cpu(cmd.regioncode) & 0xFF;

	for (i = 0; i < MRVDRV_MAX_REGION_CODE; i++) {
		/* use the region code to search for the index */
		if (priv->regioncode == lbs_region_code_to_index[i])
			break;
	}

	/* if it's unidentified region code, use the default (USA) */
	if (i >= MRVDRV_MAX_REGION_CODE) {
		priv->regioncode = 0x10;
		netdev_info(priv->dev,
			    "unidentified region code; using the default (USA)\n");
	}

	if (priv->current_addr[0] == 0xff)
		memmove(priv->current_addr, cmd.permanentaddr, ETH_ALEN);

	if (!priv->copied_hwaddr) {
		memcpy(priv->dev->dev_addr, priv->current_addr, ETH_ALEN);
		if (priv->mesh_dev)
			memcpy(priv->mesh_dev->dev_addr,
				priv->current_addr, ETH_ALEN);
		priv->copied_hwaddr = 1;
	}

out:
	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}

static int lbs_ret_host_sleep_cfg(struct lbs_private *priv, unsigned long dummy,
			struct cmd_header *resp)
{
	lbs_deb_enter(LBS_DEB_CMD);
	if (priv->is_host_sleep_activated) {
		priv->is_host_sleep_configured = 0;
		if (priv->psstate == PS_STATE_FULL_POWER) {
			priv->is_host_sleep_activated = 0;
			wake_up_interruptible(&priv->host_sleep_q);
		}
	} else {
		priv->is_host_sleep_configured = 1;
	}
	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

int lbs_host_sleep_cfg(struct lbs_private *priv, uint32_t criteria,
		struct wol_config *p_wol_config)
{
	struct cmd_ds_host_sleep cmd_config;
	int ret;

	/*
	 * Certain firmware versions do not support EHS_REMOVE_WAKEUP command
	 * and the card will return a failure.  Since we need to be
	 * able to reset the mask, in those cases we set a 0 mask instead.
	 */
	if (criteria == EHS_REMOVE_WAKEUP && !priv->ehs_remove_supported)
		criteria = 0;

	cmd_config.hdr.size = cpu_to_le16(sizeof(cmd_config));
	cmd_config.criteria = cpu_to_le32(criteria);
	cmd_config.gpio = priv->wol_gpio;
	cmd_config.gap = priv->wol_gap;

	if (p_wol_config != NULL)
		memcpy((uint8_t *)&cmd_config.wol_conf, (uint8_t *)p_wol_config,
				sizeof(struct wol_config));
	else
		cmd_config.wol_conf.action = CMD_ACT_ACTION_NONE;

	ret = __lbs_cmd(priv, CMD_802_11_HOST_SLEEP_CFG, &cmd_config.hdr,
			le16_to_cpu(cmd_config.hdr.size),
			lbs_ret_host_sleep_cfg, 0);
	if (!ret) {
		if (p_wol_config)
			memcpy((uint8_t *) p_wol_config,
					(uint8_t *)&cmd_config.wol_conf,
					sizeof(struct wol_config));
	} else {
		netdev_info(priv->dev, "HOST_SLEEP_CFG failed %d\n", ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(lbs_host_sleep_cfg);

/**
 *  lbs_set_ps_mode - Sets the Power Save mode
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *  @cmd_action: The Power Save operation (PS_MODE_ACTION_ENTER_PS or
 *                         PS_MODE_ACTION_EXIT_PS)
 *  @block:	Whether to block on a response or not
 *
 *  returns:	0 on success, error on failure
 */
int lbs_set_ps_mode(struct lbs_private *priv, u16 cmd_action, bool block)
{
	struct cmd_ds_802_11_ps_mode cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(cmd_action);

	if (cmd_action == PS_MODE_ACTION_ENTER_PS) {
		lbs_deb_cmd("PS_MODE: action ENTER_PS\n");
		cmd.multipledtim = cpu_to_le16(1);  /* Default DTIM multiple */
	} else if (cmd_action == PS_MODE_ACTION_EXIT_PS) {
		lbs_deb_cmd("PS_MODE: action EXIT_PS\n");
	} else {
		/* We don't handle CONFIRM_SLEEP here because it needs to
		 * be fastpathed to the firmware.
		 */
		lbs_deb_cmd("PS_MODE: unknown action 0x%X\n", cmd_action);
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (block)
		ret = lbs_cmd_with_response(priv, CMD_802_11_PS_MODE, &cmd);
	else
		lbs_cmd_async(priv, CMD_802_11_PS_MODE, &cmd.hdr, sizeof (cmd));

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

int lbs_cmd_802_11_sleep_params(struct lbs_private *priv, uint16_t cmd_action,
				struct sleep_params *sp)
{
	struct cmd_ds_802_11_sleep_params cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_CMD);

	if (cmd_action == CMD_ACT_GET) {
		memset(&cmd, 0, sizeof(cmd));
	} else {
		cmd.error = cpu_to_le16(sp->sp_error);
		cmd.offset = cpu_to_le16(sp->sp_offset);
		cmd.stabletime = cpu_to_le16(sp->sp_stabletime);
		cmd.calcontrol = sp->sp_calcontrol;
		cmd.externalsleepclk = sp->sp_extsleepclk;
		cmd.reserved = cpu_to_le16(sp->sp_reserved);
	}
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(cmd_action);

	ret = lbs_cmd_with_response(priv, CMD_802_11_SLEEP_PARAMS, &cmd);

	if (!ret) {
		lbs_deb_cmd("error 0x%x, offset 0x%x, stabletime 0x%x, "
			    "calcontrol 0x%x extsleepclk 0x%x\n",
			    le16_to_cpu(cmd.error), le16_to_cpu(cmd.offset),
			    le16_to_cpu(cmd.stabletime), cmd.calcontrol,
			    cmd.externalsleepclk);

		sp->sp_error = le16_to_cpu(cmd.error);
		sp->sp_offset = le16_to_cpu(cmd.offset);
		sp->sp_stabletime = le16_to_cpu(cmd.stabletime);
		sp->sp_calcontrol = cmd.calcontrol;
		sp->sp_extsleepclk = cmd.externalsleepclk;
		sp->sp_reserved = le16_to_cpu(cmd.reserved);
	}

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return 0;
}

static int lbs_wait_for_ds_awake(struct lbs_private *priv)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	if (priv->is_deep_sleep) {
		if (!wait_event_interruptible_timeout(priv->ds_awake_q,
					!priv->is_deep_sleep, (10 * HZ))) {
			netdev_err(priv->dev, "ds_awake_q: timer expired\n");
			ret = -1;
		}
	}

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

int lbs_set_deep_sleep(struct lbs_private *priv, int deep_sleep)
{
	int ret =  0;

	lbs_deb_enter(LBS_DEB_CMD);

	if (deep_sleep) {
		if (priv->is_deep_sleep != 1) {
			lbs_deb_cmd("deep sleep: sleep\n");
			BUG_ON(!priv->enter_deep_sleep);
			ret = priv->enter_deep_sleep(priv);
			if (!ret) {
				netif_stop_queue(priv->dev);
				netif_carrier_off(priv->dev);
			}
		} else {
			netdev_err(priv->dev, "deep sleep: already enabled\n");
		}
	} else {
		if (priv->is_deep_sleep) {
			lbs_deb_cmd("deep sleep: wakeup\n");
			BUG_ON(!priv->exit_deep_sleep);
			ret = priv->exit_deep_sleep(priv);
			if (!ret) {
				ret = lbs_wait_for_ds_awake(priv);
				if (ret)
					netdev_err(priv->dev,
						   "deep sleep: wakeup failed\n");
			}
		}
	}

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static int lbs_ret_host_sleep_activate(struct lbs_private *priv,
		unsigned long dummy,
		struct cmd_header *cmd)
{
	lbs_deb_enter(LBS_DEB_FW);
	priv->is_host_sleep_activated = 1;
	wake_up_interruptible(&priv->host_sleep_q);
	lbs_deb_leave(LBS_DEB_FW);
	return 0;
}

int lbs_set_host_sleep(struct lbs_private *priv, int host_sleep)
{
	struct cmd_header cmd;
	int ret = 0;
	uint32_t criteria = EHS_REMOVE_WAKEUP;

	lbs_deb_enter(LBS_DEB_CMD);

	if (host_sleep) {
		if (priv->is_host_sleep_activated != 1) {
			memset(&cmd, 0, sizeof(cmd));
			ret = lbs_host_sleep_cfg(priv, priv->wol_criteria,
					(struct wol_config *)NULL);
			if (ret) {
				netdev_info(priv->dev,
					    "Host sleep configuration failed: %d\n",
					    ret);
				return ret;
			}
			if (priv->psstate == PS_STATE_FULL_POWER) {
				ret = __lbs_cmd(priv,
						CMD_802_11_HOST_SLEEP_ACTIVATE,
						&cmd,
						sizeof(cmd),
						lbs_ret_host_sleep_activate, 0);
				if (ret)
					netdev_info(priv->dev,
						    "HOST_SLEEP_ACTIVATE failed: %d\n",
						    ret);
			}

			if (!wait_event_interruptible_timeout(
						priv->host_sleep_q,
						priv->is_host_sleep_activated,
						(10 * HZ))) {
				netdev_err(priv->dev,
					   "host_sleep_q: timer expired\n");
				ret = -1;
			}
		} else {
			netdev_err(priv->dev, "host sleep: already enabled\n");
		}
	} else {
		if (priv->is_host_sleep_activated)
			ret = lbs_host_sleep_cfg(priv, criteria,
					(struct wol_config *)NULL);
	}

	return ret;
}

/**
 *  lbs_set_snmp_mib - Set an SNMP MIB value
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *  @oid:	The OID to set in the firmware
 *  @val:	Value to set the OID to
 *
 *  returns: 	   	0 on success, error on failure
 */
int lbs_set_snmp_mib(struct lbs_private *priv, u32 oid, u16 val)
{
	struct cmd_ds_802_11_snmp_mib cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof (cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	cmd.oid = cpu_to_le16((u16) oid);

	switch (oid) {
	case SNMP_MIB_OID_BSS_TYPE:
		cmd.bufsize = cpu_to_le16(sizeof(u8));
		cmd.value[0] = val;
		break;
	case SNMP_MIB_OID_11D_ENABLE:
	case SNMP_MIB_OID_FRAG_THRESHOLD:
	case SNMP_MIB_OID_RTS_THRESHOLD:
	case SNMP_MIB_OID_SHORT_RETRY_LIMIT:
	case SNMP_MIB_OID_LONG_RETRY_LIMIT:
		cmd.bufsize = cpu_to_le16(sizeof(u16));
		*((__le16 *)(&cmd.value)) = cpu_to_le16(val);
		break;
	default:
		lbs_deb_cmd("SNMP_CMD: (set) unhandled OID 0x%x\n", oid);
		ret = -EINVAL;
		goto out;
	}

	lbs_deb_cmd("SNMP_CMD: (set) oid 0x%x, oid size 0x%x, value 0x%x\n",
		    le16_to_cpu(cmd.oid), le16_to_cpu(cmd.bufsize), val);

	ret = lbs_cmd_with_response(priv, CMD_802_11_SNMP_MIB, &cmd);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 *  lbs_get_snmp_mib - Get an SNMP MIB value
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *  @oid:	The OID to retrieve from the firmware
 *  @out_val:	Location for the returned value
 *
 *  returns:	0 on success, error on failure
 */
int lbs_get_snmp_mib(struct lbs_private *priv, u32 oid, u16 *out_val)
{
	struct cmd_ds_802_11_snmp_mib cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof (cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_GET);
	cmd.oid = cpu_to_le16(oid);

	ret = lbs_cmd_with_response(priv, CMD_802_11_SNMP_MIB, &cmd);
	if (ret)
		goto out;

	switch (le16_to_cpu(cmd.bufsize)) {
	case sizeof(u8):
		*out_val = cmd.value[0];
		break;
	case sizeof(u16):
		*out_val = le16_to_cpu(*((__le16 *)(&cmd.value)));
		break;
	default:
		lbs_deb_cmd("SNMP_CMD: (get) unhandled OID 0x%x size %d\n",
		            oid, le16_to_cpu(cmd.bufsize));
		break;
	}

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 *  lbs_get_tx_power - Get the min, max, and current TX power
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *  @curlevel:	Current power level in dBm
 *  @minlevel:	Minimum supported power level in dBm (optional)
 *  @maxlevel:	Maximum supported power level in dBm (optional)
 *
 *  returns:	0 on success, error on failure
 */
int lbs_get_tx_power(struct lbs_private *priv, s16 *curlevel, s16 *minlevel,
		     s16 *maxlevel)
{
	struct cmd_ds_802_11_rf_tx_power cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_GET);

	ret = lbs_cmd_with_response(priv, CMD_802_11_RF_TX_POWER, &cmd);
	if (ret == 0) {
		*curlevel = le16_to_cpu(cmd.curlevel);
		if (minlevel)
			*minlevel = cmd.minlevel;
		if (maxlevel)
			*maxlevel = cmd.maxlevel;
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}

/**
 *  lbs_set_tx_power - Set the TX power
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *  @dbm:	The desired power level in dBm
 *
 *  returns: 	   	0 on success, error on failure
 */
int lbs_set_tx_power(struct lbs_private *priv, s16 dbm)
{
	struct cmd_ds_802_11_rf_tx_power cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	cmd.curlevel = cpu_to_le16(dbm);

	lbs_deb_cmd("SET_RF_TX_POWER: %d dBm\n", dbm);

	ret = lbs_cmd_with_response(priv, CMD_802_11_RF_TX_POWER, &cmd);

	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}

/**
 *  lbs_set_monitor_mode - Enable or disable monitor mode
 *  (only implemented on OLPC usb8388 FW)
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *  @enable:	1 to enable monitor mode, 0 to disable
 *
 *  returns:	0 on success, error on failure
 */
int lbs_set_monitor_mode(struct lbs_private *priv, int enable)
{
	struct cmd_ds_802_11_monitor_mode cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	if (enable)
		cmd.mode = cpu_to_le16(0x1);

	lbs_deb_cmd("SET_MONITOR_MODE: %d\n", enable);

	ret = lbs_cmd_with_response(priv, CMD_802_11_MONITOR_MODE, &cmd);
	if (ret == 0) {
		priv->dev->type = enable ? ARPHRD_IEEE80211_RADIOTAP :
						ARPHRD_ETHER;
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}

/**
 *  lbs_get_channel - Get the radio channel
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *
 *  returns:	The channel on success, error on failure
 */
static int lbs_get_channel(struct lbs_private *priv)
{
	struct cmd_ds_802_11_rf_channel cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_OPT_802_11_RF_CHANNEL_GET);

	ret = lbs_cmd_with_response(priv, CMD_802_11_RF_CHANNEL, &cmd);
	if (ret)
		goto out;

	ret = le16_to_cpu(cmd.channel);
	lbs_deb_cmd("current radio channel is %d\n", ret);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

int lbs_update_channel(struct lbs_private *priv)
{
	int ret;

	/* the channel in f/w could be out of sync; get the current channel */
	lbs_deb_enter(LBS_DEB_ASSOC);

	ret = lbs_get_channel(priv);
	if (ret > 0) {
		priv->channel = ret;
		ret = 0;
	}
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

/**
 *  lbs_set_channel - Set the radio channel
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *  @channel:	The desired channel, or 0 to clear a locked channel
 *
 *  returns:	0 on success, error on failure
 */
int lbs_set_channel(struct lbs_private *priv, u8 channel)
{
	struct cmd_ds_802_11_rf_channel cmd;
#ifdef DEBUG
	u8 old_channel = priv->channel;
#endif
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_OPT_802_11_RF_CHANNEL_SET);
	cmd.channel = cpu_to_le16(channel);

	ret = lbs_cmd_with_response(priv, CMD_802_11_RF_CHANNEL, &cmd);
	if (ret)
		goto out;

	priv->channel = (uint8_t) le16_to_cpu(cmd.channel);
	lbs_deb_cmd("channel switch from %d to %d\n", old_channel,
		priv->channel);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 * lbs_get_rssi - Get current RSSI and noise floor
 *
 * @priv:	A pointer to &struct lbs_private structure
 * @rssi:	On successful return, signal level in mBm
 * @nf:		On successful return, Noise floor
 *
 * returns:	The channel on success, error on failure
 */
int lbs_get_rssi(struct lbs_private *priv, s8 *rssi, s8 *nf)
{
	struct cmd_ds_802_11_rssi cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	BUG_ON(rssi == NULL);
	BUG_ON(nf == NULL);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	/* Average SNR over last 8 beacons */
	cmd.n_or_snr = cpu_to_le16(8);

	ret = lbs_cmd_with_response(priv, CMD_802_11_RSSI, &cmd);
	if (ret == 0) {
		*nf = CAL_NF(le16_to_cpu(cmd.nf));
		*rssi = CAL_RSSI(le16_to_cpu(cmd.n_or_snr), le16_to_cpu(cmd.nf));
	}

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 *  lbs_set_11d_domain_info - Send regulatory and 802.11d domain information
 *  to the firmware
 *
 *  @priv:	pointer to &struct lbs_private
 *
 *  returns:	0 on success, error code on failure
*/
int lbs_set_11d_domain_info(struct lbs_private *priv)
{
	struct wiphy *wiphy = priv->wdev->wiphy;
	struct ieee80211_supported_band **bands = wiphy->bands;
	struct cmd_ds_802_11d_domain_info cmd;
	struct mrvl_ie_domain_param_set *domain = &cmd.domain;
	struct ieee80211_country_ie_triplet *t;
	enum nl80211_band band;
	struct ieee80211_channel *ch;
	u8 num_triplet = 0;
	u8 num_parsed_chan = 0;
	u8 first_channel = 0, next_chan = 0, max_pwr = 0;
	u8 i, flag = 0;
	size_t triplet_size;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_11D);
	if (!priv->country_code[0])
		goto out;

	memset(&cmd, 0, sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);

	lbs_deb_11d("Setting country code '%c%c'\n",
		    priv->country_code[0], priv->country_code[1]);

	domain->header.type = cpu_to_le16(TLV_TYPE_DOMAIN);

	/* Set country code */
	domain->country_code[0] = priv->country_code[0];
	domain->country_code[1] = priv->country_code[1];
	domain->country_code[2] = ' ';

	/* Now set up the channel triplets; firmware is somewhat picky here
	 * and doesn't validate channel numbers and spans; hence it would
	 * interpret a triplet of (36, 4, 20) as channels 36, 37, 38, 39.  Since
	 * the last 3 aren't valid channels, the driver is responsible for
	 * splitting that up into 4 triplet pairs of (36, 1, 20) + (40, 1, 20)
	 * etc.
	 */
	for (band = 0;
	     (band < NUM_NL80211_BANDS) && (num_triplet < MAX_11D_TRIPLETS);
	     band++) {

		if (!bands[band])
			continue;

		for (i = 0;
		     (i < bands[band]->n_channels) && (num_triplet < MAX_11D_TRIPLETS);
		     i++) {
			ch = &bands[band]->channels[i];
			if (ch->flags & IEEE80211_CHAN_DISABLED)
				continue;

			if (!flag) {
				flag = 1;
				next_chan = first_channel = (u32) ch->hw_value;
				max_pwr = ch->max_power;
				num_parsed_chan = 1;
				continue;
			}

			if ((ch->hw_value == next_chan + 1) &&
					(ch->max_power == max_pwr)) {
				/* Consolidate adjacent channels */
				next_chan++;
				num_parsed_chan++;
			} else {
				/* Add this triplet */
				lbs_deb_11d("11D triplet (%d, %d, %d)\n",
					first_channel, num_parsed_chan,
					max_pwr);
				t = &domain->triplet[num_triplet];
				t->chans.first_channel = first_channel;
				t->chans.num_channels = num_parsed_chan;
				t->chans.max_power = max_pwr;
				num_triplet++;
				flag = 0;
			}
		}

		if (flag) {
			/* Add last triplet */
			lbs_deb_11d("11D triplet (%d, %d, %d)\n", first_channel,
				num_parsed_chan, max_pwr);
			t = &domain->triplet[num_triplet];
			t->chans.first_channel = first_channel;
			t->chans.num_channels = num_parsed_chan;
			t->chans.max_power = max_pwr;
			num_triplet++;
		}
	}

	lbs_deb_11d("# triplets %d\n", num_triplet);

	/* Set command header sizes */
	triplet_size = num_triplet * sizeof(struct ieee80211_country_ie_triplet);
	domain->header.len = cpu_to_le16(sizeof(domain->country_code) +
					triplet_size);

	lbs_deb_hex(LBS_DEB_11D, "802.11D domain param set",
			(u8 *) &cmd.domain.country_code,
			le16_to_cpu(domain->header.len));

	cmd.hdr.size = cpu_to_le16(sizeof(cmd.hdr) +
				   sizeof(cmd.action) +
				   sizeof(cmd.domain.header) +
				   sizeof(cmd.domain.country_code) +
				   triplet_size);

	ret = lbs_cmd_with_response(priv, CMD_802_11D_DOMAIN_INFO, &cmd);

out:
	lbs_deb_leave_args(LBS_DEB_11D, "ret %d", ret);
	return ret;
}

/**
 *  lbs_get_reg - Read a MAC, Baseband, or RF register
 *
 *  @priv:	pointer to &struct lbs_private
 *  @reg:	register command, one of CMD_MAC_REG_ACCESS,
 *		CMD_BBP_REG_ACCESS, or CMD_RF_REG_ACCESS
 *  @offset:	byte offset of the register to get
 *  @value:	on success, the value of the register at 'offset'
 *
 *  returns:	0 on success, error code on failure
*/
int lbs_get_reg(struct lbs_private *priv, u16 reg, u16 offset, u32 *value)
{
	struct cmd_ds_reg_access cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	BUG_ON(value == NULL);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_GET);
	cmd.offset = cpu_to_le16(offset);

	if (reg != CMD_MAC_REG_ACCESS &&
	    reg != CMD_BBP_REG_ACCESS &&
	    reg != CMD_RF_REG_ACCESS) {
		ret = -EINVAL;
		goto out;
	}

	ret = lbs_cmd_with_response(priv, reg, &cmd);
	if (!ret) {
		if (reg == CMD_BBP_REG_ACCESS || reg == CMD_RF_REG_ACCESS)
			*value = cmd.value.bbp_rf;
		else if (reg == CMD_MAC_REG_ACCESS)
			*value = le32_to_cpu(cmd.value.mac);
	}

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 *  lbs_set_reg - Write a MAC, Baseband, or RF register
 *
 *  @priv:	pointer to &struct lbs_private
 *  @reg:	register command, one of CMD_MAC_REG_ACCESS,
 *		CMD_BBP_REG_ACCESS, or CMD_RF_REG_ACCESS
 *  @offset:	byte offset of the register to set
 *  @value:	the value to write to the register at 'offset'
 *
 *  returns:	0 on success, error code on failure
*/
int lbs_set_reg(struct lbs_private *priv, u16 reg, u16 offset, u32 value)
{
	struct cmd_ds_reg_access cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	cmd.offset = cpu_to_le16(offset);

	if (reg == CMD_BBP_REG_ACCESS || reg == CMD_RF_REG_ACCESS)
		cmd.value.bbp_rf = (u8) (value & 0xFF);
	else if (reg == CMD_MAC_REG_ACCESS)
		cmd.value.mac = cpu_to_le32(value);
	else {
		ret = -EINVAL;
		goto out;
	}

	ret = lbs_cmd_with_response(priv, reg, &cmd);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static void lbs_queue_cmd(struct lbs_private *priv,
			  struct cmd_ctrl_node *cmdnode)
{
	unsigned long flags;
	int addtail = 1;

	lbs_deb_enter(LBS_DEB_HOST);

	if (!cmdnode) {
		lbs_deb_host("QUEUE_CMD: cmdnode is NULL\n");
		goto done;
	}
	if (!cmdnode->cmdbuf->size) {
		lbs_deb_host("DNLD_CMD: cmd size is zero\n");
		goto done;
	}
	cmdnode->result = 0;

	/* Exit_PS command needs to be queued in the header always. */
	if (le16_to_cpu(cmdnode->cmdbuf->command) == CMD_802_11_PS_MODE) {
		struct cmd_ds_802_11_ps_mode *psm = (void *)cmdnode->cmdbuf;

		if (psm->action == cpu_to_le16(PS_MODE_ACTION_EXIT_PS)) {
			if (priv->psstate != PS_STATE_FULL_POWER)
				addtail = 0;
		}
	}

	if (le16_to_cpu(cmdnode->cmdbuf->command) == CMD_802_11_WAKEUP_CONFIRM)
		addtail = 0;

	spin_lock_irqsave(&priv->driver_lock, flags);

	if (addtail)
		list_add_tail(&cmdnode->list, &priv->cmdpendingq);
	else
		list_add(&cmdnode->list, &priv->cmdpendingq);

	spin_unlock_irqrestore(&priv->driver_lock, flags);

	lbs_deb_host("QUEUE_CMD: inserted command 0x%04x into cmdpendingq\n",
		     le16_to_cpu(cmdnode->cmdbuf->command));

done:
	lbs_deb_leave(LBS_DEB_HOST);
}

static void lbs_submit_command(struct lbs_private *priv,
			       struct cmd_ctrl_node *cmdnode)
{
	unsigned long flags;
	struct cmd_header *cmd;
	uint16_t cmdsize;
	uint16_t command;
	int timeo = 3 * HZ;
	int ret;

	lbs_deb_enter(LBS_DEB_HOST);

	cmd = cmdnode->cmdbuf;

	spin_lock_irqsave(&priv->driver_lock, flags);
	priv->seqnum++;
	cmd->seqnum = cpu_to_le16(priv->seqnum);
	priv->cur_cmd = cmdnode;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	cmdsize = le16_to_cpu(cmd->size);
	command = le16_to_cpu(cmd->command);

	/* These commands take longer */
	if (command == CMD_802_11_SCAN || command == CMD_802_11_ASSOCIATE)
		timeo = 5 * HZ;

	lbs_deb_cmd("DNLD_CMD: command 0x%04x, seq %d, size %d\n",
		     command, le16_to_cpu(cmd->seqnum), cmdsize);
	lbs_deb_hex(LBS_DEB_CMD, "DNLD_CMD", (void *) cmdnode->cmdbuf, cmdsize);

	ret = priv->hw_host_to_card(priv, MVMS_CMD, (u8 *) cmd, cmdsize);

	if (ret) {
		netdev_info(priv->dev, "DNLD_CMD: hw_host_to_card failed: %d\n",
			    ret);
		/* Reset dnld state machine, report failure */
		priv->dnld_sent = DNLD_RES_RECEIVED;
		lbs_complete_command(priv, cmdnode, ret);
	}

	if (command == CMD_802_11_DEEP_SLEEP) {
		if (priv->is_auto_deep_sleep_enabled) {
			priv->wakeup_dev_required = 1;
			priv->dnld_sent = 0;
		}
		priv->is_deep_sleep = 1;
		lbs_complete_command(priv, cmdnode, 0);
	} else {
		/* Setup the timer after transmit command */
		mod_timer(&priv->command_timer, jiffies + timeo);
	}

	lbs_deb_leave(LBS_DEB_HOST);
}

/*
 *  This function inserts command node to cmdfreeq
 *  after cleans it. Requires priv->driver_lock held.
 */
static void __lbs_cleanup_and_insert_cmd(struct lbs_private *priv,
					 struct cmd_ctrl_node *cmdnode)
{
	lbs_deb_enter(LBS_DEB_HOST);

	if (!cmdnode)
		goto out;

	cmdnode->callback = NULL;
	cmdnode->callback_arg = 0;

	memset(cmdnode->cmdbuf, 0, LBS_CMD_BUFFER_SIZE);

	list_add_tail(&cmdnode->list, &priv->cmdfreeq);
 out:
	lbs_deb_leave(LBS_DEB_HOST);
}

static void lbs_cleanup_and_insert_cmd(struct lbs_private *priv,
	struct cmd_ctrl_node *ptempcmd)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->driver_lock, flags);
	__lbs_cleanup_and_insert_cmd(priv, ptempcmd);
	spin_unlock_irqrestore(&priv->driver_lock, flags);
}

void __lbs_complete_command(struct lbs_private *priv, struct cmd_ctrl_node *cmd,
			    int result)
{
	/*
	 * Normally, commands are removed from cmdpendingq before being
	 * submitted. However, we can arrive here on alternative codepaths
	 * where the command is still pending. Make sure the command really
	 * isn't part of a list at this point.
	 */
	list_del_init(&cmd->list);

	cmd->result = result;
	cmd->cmdwaitqwoken = 1;
	wake_up(&cmd->cmdwait_q);

	if (!cmd->callback || cmd->callback == lbs_cmd_async_callback)
		__lbs_cleanup_and_insert_cmd(priv, cmd);
	priv->cur_cmd = NULL;
	wake_up(&priv->waitq);
}

void lbs_complete_command(struct lbs_private *priv, struct cmd_ctrl_node *cmd,
			  int result)
{
	unsigned long flags;
	spin_lock_irqsave(&priv->driver_lock, flags);
	__lbs_complete_command(priv, cmd, result);
	spin_unlock_irqrestore(&priv->driver_lock, flags);
}

int lbs_set_radio(struct lbs_private *priv, u8 preamble, u8 radio_on)
{
	struct cmd_ds_802_11_radio_control cmd;
	int ret = -EINVAL;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	cmd.control = 0;

	/* Only v8 and below support setting the preamble */
	if (priv->fwrelease < 0x09000000) {
		switch (preamble) {
		case RADIO_PREAMBLE_SHORT:
		case RADIO_PREAMBLE_AUTO:
		case RADIO_PREAMBLE_LONG:
			cmd.control = cpu_to_le16(preamble);
			break;
		default:
			goto out;
		}
	}

	if (radio_on)
		cmd.control |= cpu_to_le16(0x1);
	else {
		cmd.control &= cpu_to_le16(~0x1);
		priv->txpower_cur = 0;
	}

	lbs_deb_cmd("RADIO_CONTROL: radio %s, preamble %d\n",
		    radio_on ? "ON" : "OFF", preamble);

	priv->radio_on = radio_on;

	ret = lbs_cmd_with_response(priv, CMD_802_11_RADIO_CONTROL, &cmd);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

void lbs_set_mac_control(struct lbs_private *priv)
{
	struct cmd_ds_mac_control cmd;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(priv->mac_control);
	cmd.reserved = 0;

	lbs_cmd_async(priv, CMD_MAC_CONTROL, &cmd.hdr, sizeof(cmd));

	lbs_deb_leave(LBS_DEB_CMD);
}

int lbs_set_mac_control_sync(struct lbs_private *priv)
{
	struct cmd_ds_mac_control cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(priv->mac_control);
	cmd.reserved = 0;
	ret = lbs_cmd_with_response(priv, CMD_MAC_CONTROL, &cmd);

	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}

/**
 *  lbs_allocate_cmd_buffer - allocates the command buffer and links
 *  it to command free queue
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *
 *  returns:	0 for success or -1 on error
 */
int lbs_allocate_cmd_buffer(struct lbs_private *priv)
{
	int ret = 0;
	u32 bufsize;
	u32 i;
	struct cmd_ctrl_node *cmdarray;

	lbs_deb_enter(LBS_DEB_HOST);

	/* Allocate and initialize the command array */
	bufsize = sizeof(struct cmd_ctrl_node) * LBS_NUM_CMD_BUFFERS;
	if (!(cmdarray = kzalloc(bufsize, GFP_KERNEL))) {
		lbs_deb_host("ALLOC_CMD_BUF: tempcmd_array is NULL\n");
		ret = -1;
		goto done;
	}
	priv->cmd_array = cmdarray;

	/* Allocate and initialize each command buffer in the command array */
	for (i = 0; i < LBS_NUM_CMD_BUFFERS; i++) {
		cmdarray[i].cmdbuf = kzalloc(LBS_CMD_BUFFER_SIZE, GFP_KERNEL);
		if (!cmdarray[i].cmdbuf) {
			lbs_deb_host("ALLOC_CMD_BUF: ptempvirtualaddr is NULL\n");
			ret = -1;
			goto done;
		}
	}

	for (i = 0; i < LBS_NUM_CMD_BUFFERS; i++) {
		init_waitqueue_head(&cmdarray[i].cmdwait_q);
		lbs_cleanup_and_insert_cmd(priv, &cmdarray[i]);
	}
	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_HOST, "ret %d", ret);
	return ret;
}

/**
 *  lbs_free_cmd_buffer - free the command buffer
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *
 *  returns:	0 for success
 */
int lbs_free_cmd_buffer(struct lbs_private *priv)
{
	struct cmd_ctrl_node *cmdarray;
	unsigned int i;

	lbs_deb_enter(LBS_DEB_HOST);

	/* need to check if cmd array is allocated or not */
	if (priv->cmd_array == NULL) {
		lbs_deb_host("FREE_CMD_BUF: cmd_array is NULL\n");
		goto done;
	}

	cmdarray = priv->cmd_array;

	/* Release shared memory buffers */
	for (i = 0; i < LBS_NUM_CMD_BUFFERS; i++) {
		if (cmdarray[i].cmdbuf) {
			kfree(cmdarray[i].cmdbuf);
			cmdarray[i].cmdbuf = NULL;
		}
	}

	/* Release cmd_ctrl_node */
	if (priv->cmd_array) {
		kfree(priv->cmd_array);
		priv->cmd_array = NULL;
	}

done:
	lbs_deb_leave(LBS_DEB_HOST);
	return 0;
}

/**
 *  lbs_get_free_cmd_node - gets a free command node if available in
 *  command free queue
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *
 *  returns:	A pointer to &cmd_ctrl_node structure on success
 *		or %NULL on error
 */
static struct cmd_ctrl_node *lbs_get_free_cmd_node(struct lbs_private *priv)
{
	struct cmd_ctrl_node *tempnode;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_HOST);

	if (!priv)
		return NULL;

	spin_lock_irqsave(&priv->driver_lock, flags);

	if (!list_empty(&priv->cmdfreeq)) {
		tempnode = list_first_entry(&priv->cmdfreeq,
					    struct cmd_ctrl_node, list);
		list_del_init(&tempnode->list);
	} else {
		lbs_deb_host("GET_CMD_NODE: cmd_ctrl_node is not available\n");
		tempnode = NULL;
	}

	spin_unlock_irqrestore(&priv->driver_lock, flags);

	lbs_deb_leave(LBS_DEB_HOST);
	return tempnode;
}

/**
 *  lbs_execute_next_command - execute next command in command
 *  pending queue. Will put firmware back to PS mode if applicable.
 *
 *  @priv:	A pointer to &struct lbs_private structure
 *
 *  returns:	0 on success or -1 on error
 */
int lbs_execute_next_command(struct lbs_private *priv)
{
	struct cmd_ctrl_node *cmdnode = NULL;
	struct cmd_header *cmd;
	unsigned long flags;
	int ret = 0;

	/* Debug group is LBS_DEB_THREAD and not LBS_DEB_HOST, because the
	 * only caller to us is lbs_thread() and we get even when a
	 * data packet is received */
	lbs_deb_enter(LBS_DEB_THREAD);

	spin_lock_irqsave(&priv->driver_lock, flags);

	if (priv->cur_cmd) {
		netdev_alert(priv->dev,
			     "EXEC_NEXT_CMD: already processing command!\n");
		spin_unlock_irqrestore(&priv->driver_lock, flags);
		ret = -1;
		goto done;
	}

	if (!list_empty(&priv->cmdpendingq)) {
		cmdnode = list_first_entry(&priv->cmdpendingq,
					   struct cmd_ctrl_node, list);
	}

	spin_unlock_irqrestore(&priv->driver_lock, flags);

	if (cmdnode) {
		cmd = cmdnode->cmdbuf;

		if (is_command_allowed_in_ps(le16_to_cpu(cmd->command))) {
			if ((priv->psstate == PS_STATE_SLEEP) ||
			    (priv->psstate == PS_STATE_PRE_SLEEP)) {
				lbs_deb_host(
				       "EXEC_NEXT_CMD: cannot send cmd 0x%04x in psstate %d\n",
				       le16_to_cpu(cmd->command),
				       priv->psstate);
				ret = -1;
				goto done;
			}
			lbs_deb_host("EXEC_NEXT_CMD: OK to send command "
				     "0x%04x in psstate %d\n",
				     le16_to_cpu(cmd->command), priv->psstate);
		} else if (priv->psstate != PS_STATE_FULL_POWER) {
			/*
			 * 1. Non-PS command:
			 * Queue it. set needtowakeup to TRUE if current state
			 * is SLEEP, otherwise call send EXIT_PS.
			 * 2. PS command but not EXIT_PS:
			 * Ignore it.
			 * 3. PS command EXIT_PS:
			 * Set needtowakeup to TRUE if current state is SLEEP,
			 * otherwise send this command down to firmware
			 * immediately.
			 */
			if (cmd->command != cpu_to_le16(CMD_802_11_PS_MODE)) {
				/*  Prepare to send Exit PS,
				 *  this non PS command will be sent later */
				if ((priv->psstate == PS_STATE_SLEEP)
				    || (priv->psstate == PS_STATE_PRE_SLEEP)
				    ) {
					/* w/ new scheme, it will not reach here.
					   since it is blocked in main_thread. */
					priv->needtowakeup = 1;
				} else {
					lbs_set_ps_mode(priv,
							PS_MODE_ACTION_EXIT_PS,
							false);
				}

				ret = 0;
				goto done;
			} else {
				/*
				 * PS command. Ignore it if it is not Exit_PS.
				 * otherwise send it down immediately.
				 */
				struct cmd_ds_802_11_ps_mode *psm = (void *)cmd;

				lbs_deb_host(
				       "EXEC_NEXT_CMD: PS cmd, action 0x%02x\n",
				       psm->action);
				if (psm->action !=
				    cpu_to_le16(PS_MODE_ACTION_EXIT_PS)) {
					lbs_deb_host(
					       "EXEC_NEXT_CMD: ignore ENTER_PS cmd\n");
					lbs_complete_command(priv, cmdnode, 0);

					ret = 0;
					goto done;
				}

				if ((priv->psstate == PS_STATE_SLEEP) ||
				    (priv->psstate == PS_STATE_PRE_SLEEP)) {
					lbs_deb_host(
					       "EXEC_NEXT_CMD: ignore EXIT_PS cmd in sleep\n");
					lbs_complete_command(priv, cmdnode, 0);
					priv->needtowakeup = 1;

					ret = 0;
					goto done;
				}

				lbs_deb_host(
				       "EXEC_NEXT_CMD: sending EXIT_PS\n");
			}
		}
		spin_lock_irqsave(&priv->driver_lock, flags);
		list_del_init(&cmdnode->list);
		spin_unlock_irqrestore(&priv->driver_lock, flags);
		lbs_deb_host("EXEC_NEXT_CMD: sending command 0x%04x\n",
			    le16_to_cpu(cmd->command));
		lbs_submit_command(priv, cmdnode);
	} else {
		/*
		 * check if in power save mode, if yes, put the device back
		 * to PS mode
		 */
		if ((priv->psmode != LBS802_11POWERMODECAM) &&
		    (priv->psstate == PS_STATE_FULL_POWER) &&
		    (priv->connect_status == LBS_CONNECTED)) {
			lbs_deb_host(
				"EXEC_NEXT_CMD: cmdpendingq empty, go back to PS_SLEEP");
			lbs_set_ps_mode(priv, PS_MODE_ACTION_ENTER_PS,
					false);
		}
	}

	ret = 0;
done:
	lbs_deb_leave(LBS_DEB_THREAD);
	return ret;
}

static void lbs_send_confirmsleep(struct lbs_private *priv)
{
	unsigned long flags;
	int ret;

	lbs_deb_enter(LBS_DEB_HOST);
	lbs_deb_hex(LBS_DEB_HOST, "sleep confirm", (u8 *) &confirm_sleep,
		sizeof(confirm_sleep));

	ret = priv->hw_host_to_card(priv, MVMS_CMD, (u8 *) &confirm_sleep,
		sizeof(confirm_sleep));
	if (ret) {
		netdev_alert(priv->dev, "confirm_sleep failed\n");
		goto out;
	}

	spin_lock_irqsave(&priv->driver_lock, flags);

	/* We don't get a response on the sleep-confirmation */
	priv->dnld_sent = DNLD_RES_RECEIVED;

	if (priv->is_host_sleep_configured) {
		priv->is_host_sleep_activated = 1;
		wake_up_interruptible(&priv->host_sleep_q);
	}

	/* If nothing to do, go back to sleep (?) */
	if (!kfifo_len(&priv->event_fifo) && !priv->resp_len[priv->resp_idx])
		priv->psstate = PS_STATE_SLEEP;

	spin_unlock_irqrestore(&priv->driver_lock, flags);

out:
	lbs_deb_leave(LBS_DEB_HOST);
}

/**
 * lbs_ps_confirm_sleep - checks condition and prepares to
 * send sleep confirm command to firmware if ok
 *
 * @priv:	A pointer to &struct lbs_private structure
 *
 * returns:	n/a
 */
void lbs_ps_confirm_sleep(struct lbs_private *priv)
{
	unsigned long flags =0;
	int allowed = 1;

	lbs_deb_enter(LBS_DEB_HOST);

	spin_lock_irqsave(&priv->driver_lock, flags);
	if (priv->dnld_sent) {
		allowed = 0;
		lbs_deb_host("dnld_sent was set\n");
	}

	/* In-progress command? */
	if (priv->cur_cmd) {
		allowed = 0;
		lbs_deb_host("cur_cmd was set\n");
	}

	/* Pending events or command responses? */
	if (kfifo_len(&priv->event_fifo) || priv->resp_len[priv->resp_idx]) {
		allowed = 0;
		lbs_deb_host("pending events or command responses\n");
	}
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	if (allowed) {
		lbs_deb_host("sending lbs_ps_confirm_sleep\n");
		lbs_send_confirmsleep(priv);
	} else {
		lbs_deb_host("sleep confirm has been delayed\n");
	}

	lbs_deb_leave(LBS_DEB_HOST);
}


/**
 * lbs_set_tpc_cfg - Configures the transmission power control functionality
 *
 * @priv:	A pointer to &struct lbs_private structure
 * @enable:	Transmission power control enable
 * @p0:		Power level when link quality is good (dBm).
 * @p1:		Power level when link quality is fair (dBm).
 * @p2:		Power level when link quality is poor (dBm).
 * @usesnr:	Use Signal to Noise Ratio in TPC
 *
 * returns:	0 on success
 */
int lbs_set_tpc_cfg(struct lbs_private *priv, int enable, int8_t p0, int8_t p1,
		int8_t p2, int usesnr)
{
	struct cmd_ds_802_11_tpc_cfg cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	cmd.enable = !!enable;
	cmd.usesnr = !!usesnr;
	cmd.P0 = p0;
	cmd.P1 = p1;
	cmd.P2 = p2;

	ret = lbs_cmd_with_response(priv, CMD_802_11_TPC_CFG, &cmd);

	return ret;
}

/**
 * lbs_set_power_adapt_cfg - Configures the power adaptation settings
 *
 * @priv:	A pointer to &struct lbs_private structure
 * @enable:	Power adaptation enable
 * @p0:		Power level for 1, 2, 5.5 and 11 Mbps (dBm).
 * @p1:		Power level for 6, 9, 12, 18, 22, 24 and 36 Mbps (dBm).
 * @p2:		Power level for 48 and 54 Mbps (dBm).
 *
 * returns:	0 on Success
 */

int lbs_set_power_adapt_cfg(struct lbs_private *priv, int enable, int8_t p0,
		int8_t p1, int8_t p2)
{
	struct cmd_ds_802_11_pa_cfg cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	cmd.enable = !!enable;
	cmd.P0 = p0;
	cmd.P1 = p1;
	cmd.P2 = p2;

	ret = lbs_cmd_with_response(priv, CMD_802_11_PA_CFG , &cmd);

	return ret;
}


struct cmd_ctrl_node *__lbs_cmd_async(struct lbs_private *priv,
	uint16_t command, struct cmd_header *in_cmd, int in_cmd_size,
	int (*callback)(struct lbs_private *, unsigned long, struct cmd_header *),
	unsigned long callback_arg)
{
	struct cmd_ctrl_node *cmdnode;

	lbs_deb_enter(LBS_DEB_HOST);

	if (priv->surpriseremoved) {
		lbs_deb_host("PREP_CMD: card removed\n");
		cmdnode = ERR_PTR(-ENOENT);
		goto done;
	}

	/* No commands are allowed in Deep Sleep until we toggle the GPIO
	 * to wake up the card and it has signaled that it's ready.
	 */
	if (!priv->is_auto_deep_sleep_enabled) {
		if (priv->is_deep_sleep) {
			lbs_deb_cmd("command not allowed in deep sleep\n");
			cmdnode = ERR_PTR(-EBUSY);
			goto done;
		}
	}

	cmdnode = lbs_get_free_cmd_node(priv);
	if (cmdnode == NULL) {
		lbs_deb_host("PREP_CMD: cmdnode is NULL\n");

		/* Wake up main thread to execute next command */
		wake_up(&priv->waitq);
		cmdnode = ERR_PTR(-ENOBUFS);
		goto done;
	}

	cmdnode->callback = callback;
	cmdnode->callback_arg = callback_arg;

	/* Copy the incoming command to the buffer */
	memcpy(cmdnode->cmdbuf, in_cmd, in_cmd_size);

	/* Set command, clean result, move to buffer */
	cmdnode->cmdbuf->command = cpu_to_le16(command);
	cmdnode->cmdbuf->size    = cpu_to_le16(in_cmd_size);
	cmdnode->cmdbuf->result  = 0;

	lbs_deb_host("PREP_CMD: command 0x%04x\n", command);

	cmdnode->cmdwaitqwoken = 0;
	lbs_queue_cmd(priv, cmdnode);
	wake_up(&priv->waitq);

 done:
	lbs_deb_leave_args(LBS_DEB_HOST, "ret %p", cmdnode);
	return cmdnode;
}

void lbs_cmd_async(struct lbs_private *priv, uint16_t command,
	struct cmd_header *in_cmd, int in_cmd_size)
{
	lbs_deb_enter(LBS_DEB_CMD);
	__lbs_cmd_async(priv, command, in_cmd, in_cmd_size,
		lbs_cmd_async_callback, 0);
	lbs_deb_leave(LBS_DEB_CMD);
}

int __lbs_cmd(struct lbs_private *priv, uint16_t command,
	      struct cmd_header *in_cmd, int in_cmd_size,
	      int (*callback)(struct lbs_private *, unsigned long, struct cmd_header *),
	      unsigned long callback_arg)
{
	struct cmd_ctrl_node *cmdnode;
	unsigned long flags;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_HOST);

	cmdnode = __lbs_cmd_async(priv, command, in_cmd, in_cmd_size,
				  callback, callback_arg);
	if (IS_ERR(cmdnode)) {
		ret = PTR_ERR(cmdnode);
		goto done;
	}

	might_sleep();

	/*
	 * Be careful with signals here. A signal may be received as the system
	 * goes into suspend or resume. We do not want this to interrupt the
	 * command, so we perform an uninterruptible sleep.
	 */
	wait_event(cmdnode->cmdwait_q, cmdnode->cmdwaitqwoken);

	spin_lock_irqsave(&priv->driver_lock, flags);
	ret = cmdnode->result;
	if (ret)
		netdev_info(priv->dev, "PREP_CMD: command 0x%04x failed: %d\n",
			    command, ret);

	__lbs_cleanup_and_insert_cmd(priv, cmdnode);
	spin_unlock_irqrestore(&priv->driver_lock, flags);

done:
	lbs_deb_leave_args(LBS_DEB_HOST, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(__lbs_cmd);
