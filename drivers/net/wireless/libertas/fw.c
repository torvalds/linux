/**
  * This file contains the initialization for FW and HW
  */
#include <linux/firmware.h>

#include "host.h"
#include "defs.h"
#include "decl.h"
#include "dev.h"
#include "wext.h"
#include "if_usb.h"

/**
 *  @brief This function checks the validity of Boot2/FW image.
 *
 *  @param data              pointer to image
 *         len               image length
 *  @return     0 or -1
 */
static int check_fwfile_format(u8 *data, u32 totlen)
{
	u32 bincmd, exit;
	u32 blksize, offset, len;
	int ret;

	ret = 1;
	exit = len = 0;

	do {
		struct fwheader *fwh = (void *)data;

		bincmd = le32_to_cpu(fwh->dnldcmd);
		blksize = le32_to_cpu(fwh->datalength);
		switch (bincmd) {
		case FW_HAS_DATA_TO_RECV:
			offset = sizeof(struct fwheader) + blksize;
			data += offset;
			len += offset;
			if (len >= totlen)
				exit = 1;
			break;
		case FW_HAS_LAST_BLOCK:
			exit = 1;
			ret = 0;
			break;
		default:
			exit = 1;
			break;
		}
	} while (!exit);

	if (ret)
		lbs_pr_err("firmware file format check FAIL\n");
	else
		lbs_deb_fw("firmware file format check PASS\n");

	return ret;
}

/**
 *  @brief This function downloads firmware image, gets
 *  HW spec from firmware and set basic parameters to
 *  firmware.
 *
 *  @param priv    A pointer to wlan_private structure
 *  @return 	   0 or -1
 */
static int wlan_setup_station_hw(wlan_private * priv, char *fw_name)
{
	int ret = -1;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_FW);

	if ((ret = request_firmware(&priv->firmware, fw_name,
				    priv->hotplug_device)) < 0) {
		lbs_pr_err("request_firmware() failed with %#x\n", ret);
		lbs_pr_err("firmware %s not found\n", fw_name);
		goto done;
	}

	if (check_fwfile_format(priv->firmware->data, priv->firmware->size)) {
		release_firmware(priv->firmware);
		goto done;
	}

	ret = priv->hw_prog_firmware(priv);

	release_firmware(priv->firmware);

	if (ret) {
		lbs_deb_fw("bootloader in invalid state\n");
		ret = -1;
		goto done;
	}

	/*
	 * Read MAC address from HW
	 */
	memset(adapter->current_addr, 0xff, ETH_ALEN);

	ret = libertas_prepare_and_send_command(priv, cmd_get_hw_spec,
				    0, cmd_option_waitforrsp, 0, NULL);

	if (ret) {
		ret = -1;
		goto done;
	}

	libertas_set_mac_packet_filter(priv);

	/* Get the supported Data rates */
	ret = libertas_prepare_and_send_command(priv, cmd_802_11_data_rate,
				    cmd_act_get_tx_rate,
				    cmd_option_waitforrsp, 0, NULL);

	if (ret) {
		ret = -1;
		goto done;
	}

	ret = 0;
done:
	lbs_deb_leave_args(LBS_DEB_FW, "ret %d", ret);
	return ret;
}

static int wlan_allocate_adapter(wlan_private * priv)
{
	size_t bufsize;
	wlan_adapter *adapter = priv->adapter;

	/* Allocate buffer to store the BSSID list */
	bufsize = MAX_NETWORK_COUNT * sizeof(struct bss_descriptor);
	adapter->networks = kzalloc(bufsize, GFP_KERNEL);
	if (!adapter->networks) {
		lbs_pr_err("Out of memory allocating beacons\n");
		libertas_free_adapter(priv);
		return -ENOMEM;
	}

	/* Allocate the command buffers */
	libertas_allocate_cmd_buffer(priv);

	memset(&adapter->libertas_ps_confirm_sleep, 0, sizeof(struct PS_CMD_ConfirmSleep));
	adapter->libertas_ps_confirm_sleep.seqnum = cpu_to_le16(++adapter->seqnum);
	adapter->libertas_ps_confirm_sleep.command =
	    cpu_to_le16(cmd_802_11_ps_mode);
	adapter->libertas_ps_confirm_sleep.size =
	    cpu_to_le16(sizeof(struct PS_CMD_ConfirmSleep));
	adapter->libertas_ps_confirm_sleep.result = 0;
	adapter->libertas_ps_confirm_sleep.action =
	    cpu_to_le16(cmd_subcmd_sleep_confirmed);

	return 0;
}

static void wlan_init_adapter(wlan_private * priv)
{
	wlan_adapter *adapter = priv->adapter;
	int i;

	adapter->scanprobes = 0;

	adapter->bcn_avg_factor = DEFAULT_BCN_AVG_FACTOR;
	adapter->data_avg_factor = DEFAULT_DATA_AVG_FACTOR;

	/* ATIM params */
	adapter->atimwindow = 0;

	adapter->connect_status = libertas_disconnected;
	memset(adapter->current_addr, 0xff, ETH_ALEN);

	/* scan type */
	adapter->scantype = cmd_scan_type_active;

	/* scan mode */
	adapter->scanmode = cmd_bss_type_any;

	/* 802.11 specific */
	adapter->secinfo.wep_enabled = 0;
	for (i = 0; i < sizeof(adapter->wep_keys) / sizeof(adapter->wep_keys[0]);
	     i++)
		memset(&adapter->wep_keys[i], 0, sizeof(struct WLAN_802_11_KEY));
	adapter->wep_tx_keyidx = 0;
	adapter->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
	adapter->mode = IW_MODE_INFRA;

	adapter->pending_assoc_req = NULL;
	adapter->in_progress_assoc_req = NULL;

	/* Initialize scan result lists */
	INIT_LIST_HEAD(&adapter->network_free_list);
	INIT_LIST_HEAD(&adapter->network_list);
	for (i = 0; i < MAX_NETWORK_COUNT; i++) {
		list_add_tail(&adapter->networks[i].list,
			      &adapter->network_free_list);
	}

	mutex_init(&adapter->lock);

	adapter->prescan = 1;

	memset(&adapter->curbssparams, 0, sizeof(adapter->curbssparams));
	adapter->curbssparams.channel = DEFAULT_AD_HOC_CHANNEL;

	/* PnP and power profile */
	adapter->surpriseremoved = 0;

	adapter->currentpacketfilter =
	    cmd_act_mac_rx_on | cmd_act_mac_tx_on;

	adapter->radioon = RADIO_ON;
	adapter->txantenna = RF_ANTENNA_2;
	adapter->rxantenna = RF_ANTENNA_AUTO;

	adapter->is_datarate_auto = 1;
	adapter->beaconperiod = MRVDRV_BEACON_INTERVAL;

	// set default value of capinfo.
#define SHORT_PREAMBLE_ALLOWED		1
	memset(&adapter->capinfo, 0, sizeof(adapter->capinfo));
	adapter->capinfo.shortpreamble = SHORT_PREAMBLE_ALLOWED;

	adapter->psmode = wlan802_11powermodecam;
	adapter->multipledtim = MRVDRV_DEFAULT_MULTIPLE_DTIM;

	adapter->listeninterval = MRVDRV_DEFAULT_LISTEN_INTERVAL;

	adapter->psstate = PS_STATE_FULL_POWER;
	adapter->needtowakeup = 0;
	adapter->locallisteninterval = 0;	/* default value in firmware will be used */

	adapter->datarate = 0;	// Initially indicate the rate as auto

	adapter->adhoc_grate_enabled = 0;

	adapter->intcounter = 0;

	adapter->currenttxskb = NULL;
	adapter->pkttxctrl = 0;

	memset(&adapter->tx_queue_ps, 0, NR_TX_QUEUE*sizeof(struct sk_buff*));
	adapter->tx_queue_idx = 0;
	spin_lock_init(&adapter->txqueue_lock);

	return;
}

static void command_timer_fn(unsigned long data);

int libertas_init_fw(wlan_private * priv, char *fw_name)
{
	int ret = -1;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_FW);

	/* Allocate adapter structure */
	if ((ret = wlan_allocate_adapter(priv)) != 0)
		goto done;

	/* init adapter structure */
	wlan_init_adapter(priv);

	/* init timer etc. */
	setup_timer(&adapter->command_timer, command_timer_fn,
			(unsigned long)priv);

	/* download fimrware etc. */
	if ((ret = wlan_setup_station_hw(priv, fw_name)) != 0) {
		del_timer_sync(&adapter->command_timer);
		goto done;
	}

	/* init 802.11d */
	libertas_init_11d(priv);

	ret = 0;
done:
	lbs_deb_leave_args(LBS_DEB_FW, "ret %d", ret);
	return ret;
}

void libertas_free_adapter(wlan_private * priv)
{
	wlan_adapter *adapter = priv->adapter;

	if (!adapter) {
		lbs_deb_fw("why double free adapter?\n");
		return;
	}

	lbs_deb_fw("free command buffer\n");
	libertas_free_cmd_buffer(priv);

	lbs_deb_fw("free command_timer\n");
	del_timer(&adapter->command_timer);

	lbs_deb_fw("free scan results table\n");
	kfree(adapter->networks);
	adapter->networks = NULL;

	/* Free the adapter object itself */
	lbs_deb_fw("free adapter\n");
	kfree(adapter);
	priv->adapter = NULL;
}

/**
 *  This function handles the timeout of command sending.
 *  It will re-send the same command again.
 */
static void command_timer_fn(unsigned long data)
{
	wlan_private *priv = (wlan_private *)data;
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *ptempnode;
	struct cmd_ds_command *cmd;
	unsigned long flags;

	ptempnode = adapter->cur_cmd;
	if (ptempnode == NULL) {
		lbs_deb_fw("ptempnode empty\n");
		return;
	}

	cmd = (struct cmd_ds_command *)ptempnode->bufvirtualaddr;
	if (!cmd) {
		lbs_deb_fw("cmd is NULL\n");
		return;
	}

	lbs_deb_fw("command_timer_fn fired, cmd %x\n", cmd->command);

	if (!adapter->fw_ready)
		return;

	spin_lock_irqsave(&adapter->driver_lock, flags);
	adapter->cur_cmd = NULL;
	spin_unlock_irqrestore(&adapter->driver_lock, flags);

	lbs_deb_fw("re-sending same command because of timeout\n");
	libertas_queue_cmd(adapter, ptempnode, 0);

	wake_up_interruptible(&priv->mainthread.waitq);

	return;
}
