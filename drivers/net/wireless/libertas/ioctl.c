/**
  * This file contains ioctl functions
  */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>

#include <net/iw_handler.h>
#include <net/ieee80211.h>

#include "host.h"
#include "radiotap.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "join.h"
#include "wext.h"

#define MAX_SCAN_CELL_SIZE      (IW_EV_ADDR_LEN + \
				IW_ESSID_MAX_SIZE + \
				IW_EV_UINT_LEN + IW_EV_FREQ_LEN + \
				IW_EV_QUAL_LEN + IW_ESSID_MAX_SIZE + \
				IW_EV_PARAM_LEN + 40)	/* 40 for WPAIE */

#define WAIT_FOR_SCAN_RRESULT_MAX_TIME (10 * HZ)

static int setrxantenna(wlan_private * priv, int mode)
{
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;

	if (mode != RF_ANTENNA_1 && mode != RF_ANTENNA_2
	    && mode != RF_ANTENNA_AUTO) {
		return -EINVAL;
	}

	adapter->rxantennamode = mode;

	lbs_pr_debug(1, "SET RX Antenna mode to 0x%04x\n", adapter->rxantennamode);

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_rf_antenna,
				    cmd_act_set_rx,
				    cmd_option_waitforrsp, 0,
				    &adapter->rxantennamode);
	return ret;
}

static int settxantenna(wlan_private * priv, int mode)
{
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;

	if ((mode != RF_ANTENNA_1) && (mode != RF_ANTENNA_2)
	    && (mode != RF_ANTENNA_AUTO)) {
		return -EINVAL;
	}

	adapter->txantennamode = mode;

	lbs_pr_debug(1, "SET TX Antenna mode to 0x%04x\n", adapter->txantennamode);

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_rf_antenna,
				    cmd_act_set_tx,
				    cmd_option_waitforrsp, 0,
				    &adapter->txantennamode);

	return ret;
}

static int getrxantenna(wlan_private * priv, char *buf)
{
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;

	// clear it, so we will know if the value
	// returned below is correct or not.
	adapter->rxantennamode = 0;

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_rf_antenna,
				    cmd_act_get_rx,
				    cmd_option_waitforrsp, 0, NULL);

	if (ret) {
		LEAVE();
		return ret;
	}

	lbs_pr_debug(1, "Get Rx Antenna mode:0x%04x\n", adapter->rxantennamode);

	return sprintf(buf, "0x%04x", adapter->rxantennamode) + 1;
}

static int gettxantenna(wlan_private * priv, char *buf)
{
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;

	// clear it, so we will know if the value
	// returned below is correct or not.
	adapter->txantennamode = 0;

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_rf_antenna,
				    cmd_act_get_tx,
				    cmd_option_waitforrsp, 0, NULL);

	if (ret) {
		LEAVE();
		return ret;
	}

	lbs_pr_debug(1, "Get Tx Antenna mode:0x%04x\n", adapter->txantennamode);

	return sprintf(buf, "0x%04x", adapter->txantennamode) + 1;
}

static int wlan_set_region(wlan_private * priv, u16 region_code)
{
	int i;

	for (i = 0; i < MRVDRV_MAX_REGION_CODE; i++) {
		// use the region code to search for the index
		if (region_code == libertas_region_code_to_index[i]) {
			priv->adapter->regiontableindex = (u16) i;
			priv->adapter->regioncode = region_code;
			break;
		}
	}

	// if it's unidentified region code
	if (i >= MRVDRV_MAX_REGION_CODE) {
		lbs_pr_debug(1, "region Code not identified\n");
		LEAVE();
		return -1;
	}

	if (libertas_set_regiontable(priv, priv->adapter->regioncode, 0)) {
		LEAVE();
		return -EINVAL;
	}

	return 0;
}

/**
 *  @brief Get/Set Firmware wakeup method
 *
 *  @param priv		A pointer to wlan_private structure
 *  @param wrq	   	A pointer to user data
 *  @return 	   	0--success, otherwise fail
 */
static int wlan_txcontrol(wlan_private * priv, struct iwreq *wrq)
{
	wlan_adapter *adapter = priv->adapter;
	int data;
	ENTER();

	if ((int)wrq->u.data.length == 0) {
		if (copy_to_user
		    (wrq->u.data.pointer, &adapter->pkttxctrl, sizeof(u32))) {
			lbs_pr_alert("copy_to_user failed!\n");
			return -EFAULT;
		}
	} else {
		if ((int)wrq->u.data.length > 1) {
			lbs_pr_alert("ioctl too many args!\n");
			return -EFAULT;
		}
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			lbs_pr_alert("Copy from user failed\n");
			return -EFAULT;
		}

		adapter->pkttxctrl = (u32) data;
	}

	wrq->u.data.length = 1;

	LEAVE();
	return 0;
}

/**
 *  @brief Get/Set NULL Package generation interval
 *
 *  @param priv		A pointer to wlan_private structure
 *  @param wrq	   	A pointer to user data
 *  @return 	   	0--success, otherwise fail
 */
static int wlan_null_pkt_interval(wlan_private * priv, struct iwreq *wrq)
{
	wlan_adapter *adapter = priv->adapter;
	int data;
	ENTER();

	if ((int)wrq->u.data.length == 0) {
		data = adapter->nullpktinterval;

		if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
			lbs_pr_alert( "copy_to_user failed!\n");
			return -EFAULT;
		}
	} else {
		if ((int)wrq->u.data.length > 1) {
			lbs_pr_alert( "ioctl too many args!\n");
			return -EFAULT;
		}
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			lbs_pr_debug(1, "Copy from user failed\n");
			return -EFAULT;
		}

		adapter->nullpktinterval = data;
	}

	wrq->u.data.length = 1;

	LEAVE();
	return 0;
}

static int wlan_get_rxinfo(wlan_private * priv, struct iwreq *wrq)
{
	wlan_adapter *adapter = priv->adapter;
	int data[2];
	ENTER();
	data[0] = adapter->SNR[TYPE_RXPD][TYPE_NOAVG];
	data[1] = adapter->rxpd_rate;
	if (copy_to_user(wrq->u.data.pointer, data, sizeof(int) * 2)) {
		lbs_pr_debug(1, "Copy to user failed\n");
		return -EFAULT;
	}
	wrq->u.data.length = 2;
	LEAVE();
	return 0;
}

static int wlan_get_snr(wlan_private * priv, struct iwreq *wrq)
{
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;
	int data[4];

	ENTER();
	memset(data, 0, sizeof(data));
	if (wrq->u.data.length) {
		if (copy_from_user(data, wrq->u.data.pointer,
		     min_t(size_t, wrq->u.data.length, 4) * sizeof(int)))
			return -EFAULT;
	}
	if ((wrq->u.data.length == 0) || (data[0] == 0) || (data[0] == 1)) {
		if (adapter->connect_status == libertas_connected) {
			ret = libertas_prepare_and_send_command(priv,
						    cmd_802_11_rssi,
						    0,
						    cmd_option_waitforrsp,
						    0, NULL);

			if (ret) {
				LEAVE();
				return ret;
			}
		}
	}

	if (wrq->u.data.length == 0) {
		data[0] = adapter->SNR[TYPE_BEACON][TYPE_NOAVG];
		data[1] = adapter->SNR[TYPE_BEACON][TYPE_AVG];
		data[2] = adapter->SNR[TYPE_RXPD][TYPE_NOAVG];
		data[3] = adapter->SNR[TYPE_RXPD][TYPE_AVG] / AVG_SCALE;
		if (copy_to_user(wrq->u.data.pointer, data, sizeof(int) * 4))
			return -EFAULT;
		wrq->u.data.length = 4;
	} else if (data[0] == 0) {
		data[0] = adapter->SNR[TYPE_BEACON][TYPE_NOAVG];
		if (copy_to_user(wrq->u.data.pointer, data, sizeof(int)))
			return -EFAULT;
		wrq->u.data.length = 1;
	} else if (data[0] == 1) {
		data[0] = adapter->SNR[TYPE_BEACON][TYPE_AVG];
		if (copy_to_user(wrq->u.data.pointer, data, sizeof(int)))
			return -EFAULT;
		wrq->u.data.length = 1;
	} else if (data[0] == 2) {
		data[0] = adapter->SNR[TYPE_RXPD][TYPE_NOAVG];
		if (copy_to_user(wrq->u.data.pointer, data, sizeof(int)))
			return -EFAULT;
		wrq->u.data.length = 1;
	} else if (data[0] == 3) {
		data[0] = adapter->SNR[TYPE_RXPD][TYPE_AVG] / AVG_SCALE;
		if (copy_to_user(wrq->u.data.pointer, data, sizeof(int)))
			return -EFAULT;
		wrq->u.data.length = 1;
	} else
		return -ENOTSUPP;

	LEAVE();
	return 0;
}

static int wlan_beacon_interval(wlan_private * priv, struct iwreq *wrq)
{
	int data;
	wlan_adapter *adapter = priv->adapter;

	if (wrq->u.data.length > 0) {
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int)))
			return -EFAULT;

		lbs_pr_debug(1, "WLAN SET BEACON INTERVAL: %d\n", data);
		if ((data > MRVDRV_MAX_BEACON_INTERVAL)
		    || (data < MRVDRV_MIN_BEACON_INTERVAL))
			return -ENOTSUPP;
		adapter->beaconperiod = data;
	}
	data = adapter->beaconperiod;
	if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int)))
		return -EFAULT;

	wrq->u.data.length = 1;

	return 0;
}

static int wlan_get_rssi(wlan_private * priv, struct iwreq *wrq)
{
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;
	int temp;
	int data = 0;
	int *val;

	ENTER();
	data = SUBCMD_DATA(wrq);
	if ((data == 0) || (data == 1)) {
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_rssi,
					    0, cmd_option_waitforrsp,
					    0, NULL);
		if (ret) {
			LEAVE();
			return ret;
		}
	}

	switch (data) {
	case 0:

		temp = CAL_RSSI(adapter->SNR[TYPE_BEACON][TYPE_NOAVG],
				adapter->NF[TYPE_BEACON][TYPE_NOAVG]);
		break;
	case 1:
		temp = CAL_RSSI(adapter->SNR[TYPE_BEACON][TYPE_AVG],
				adapter->NF[TYPE_BEACON][TYPE_AVG]);
		break;
	case 2:
		temp = CAL_RSSI(adapter->SNR[TYPE_RXPD][TYPE_NOAVG],
				adapter->NF[TYPE_RXPD][TYPE_NOAVG]);
		break;
	case 3:
		temp = CAL_RSSI(adapter->SNR[TYPE_RXPD][TYPE_AVG] / AVG_SCALE,
				adapter->NF[TYPE_RXPD][TYPE_AVG] / AVG_SCALE);
		break;
	default:
		return -ENOTSUPP;
	}
	val = (int *)wrq->u.name;
	*val = temp;

	LEAVE();
	return 0;
}

static int wlan_get_nf(wlan_private * priv, struct iwreq *wrq)
{
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;
	int temp;
	int data = 0;
	int *val;

	data = SUBCMD_DATA(wrq);
	if ((data == 0) || (data == 1)) {
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_rssi,
					    0, cmd_option_waitforrsp,
					    0, NULL);

		if (ret) {
			LEAVE();
			return ret;
		}
	}

	switch (data) {
	case 0:
		temp = adapter->NF[TYPE_BEACON][TYPE_NOAVG];
		break;
	case 1:
		temp = adapter->NF[TYPE_BEACON][TYPE_AVG];
		break;
	case 2:
		temp = adapter->NF[TYPE_RXPD][TYPE_NOAVG];
		break;
	case 3:
		temp = adapter->NF[TYPE_RXPD][TYPE_AVG] / AVG_SCALE;
		break;
	default:
		return -ENOTSUPP;
	}

	temp = CAL_NF(temp);

	lbs_pr_debug(1, "%s: temp = %d\n", __FUNCTION__, temp);
	val = (int *)wrq->u.name;
	*val = temp;
	return 0;
}

static int wlan_get_txrate_ioctl(wlan_private * priv, struct ifreq *req)
{
	wlan_adapter *adapter = priv->adapter;
	int *pdata;
	struct iwreq *wrq = (struct iwreq *)req;
	int ret = 0;
	adapter->txrate = 0;
	lbs_pr_debug(1, "wlan_get_txrate_ioctl\n");
	ret = libertas_prepare_and_send_command(priv, cmd_802_11_tx_rate_query,
				    cmd_act_get, cmd_option_waitforrsp,
				    0, NULL);
	if (ret)
		return ret;

	pdata = (int *)wrq->u.name;
	*pdata = (int)adapter->txrate;
	return 0;
}

static int wlan_get_adhoc_status_ioctl(wlan_private * priv, struct iwreq *wrq)
{
	char status[64];
	wlan_adapter *adapter = priv->adapter;

	memset(status, 0, sizeof(status));

	switch (adapter->inframode) {
	case wlan802_11ibss:
		if (adapter->connect_status == libertas_connected) {
			if (adapter->adhoccreate)
				memcpy(&status, "AdhocStarted", sizeof(status));
			else
				memcpy(&status, "AdhocJoined", sizeof(status));
		} else {
			memcpy(&status, "AdhocIdle", sizeof(status));
		}
		break;
	case wlan802_11infrastructure:
		memcpy(&status, "Inframode", sizeof(status));
		break;
	default:
		memcpy(&status, "AutoUnknownmode", sizeof(status));
		break;
	}

	lbs_pr_debug(1, "status = %s\n", status);
	wrq->u.data.length = strlen(status) + 1;

	if (wrq->u.data.pointer) {
		if (copy_to_user(wrq->u.data.pointer,
				 &status, wrq->u.data.length))
			return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief Set/Get WPA IE
 *  @param priv                 A pointer to wlan_private structure
 *  @param req			A pointer to ifreq structure
 *  @return 	   		0 --success, otherwise fail
 */
static int wlan_setwpaie_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	ENTER();

	if (wrq->u.data.length) {
		if (wrq->u.data.length > sizeof(adapter->wpa_ie)) {
			lbs_pr_debug(1, "failed to copy WPA IE, too big \n");
			return -EFAULT;
		}
		if (copy_from_user(adapter->wpa_ie, wrq->u.data.pointer,
				   wrq->u.data.length)) {
			lbs_pr_debug(1, "failed to copy WPA IE \n");
			return -EFAULT;
		}
		adapter->wpa_ie_len = wrq->u.data.length;
		lbs_pr_debug(1, "Set wpa_ie_len=%d IE=%#x\n", adapter->wpa_ie_len,
		       adapter->wpa_ie[0]);
		lbs_dbg_hex("wpa_ie", adapter->wpa_ie, adapter->wpa_ie_len);
		if (adapter->wpa_ie[0] == WPA_IE)
			adapter->secinfo.WPAenabled = 1;
		else if (adapter->wpa_ie[0] == WPA2_IE)
			adapter->secinfo.WPA2enabled = 1;
		else {
			adapter->secinfo.WPAenabled = 0;
			adapter->secinfo.WPA2enabled = 0;
		}
	} else {
		memset(adapter->wpa_ie, 0, sizeof(adapter->wpa_ie));
		adapter->wpa_ie_len = wrq->u.data.length;
		lbs_pr_debug(1, "Reset wpa_ie_len=%d IE=%#x\n",
		       adapter->wpa_ie_len, adapter->wpa_ie[0]);
		adapter->secinfo.WPAenabled = 0;
		adapter->secinfo.WPA2enabled = 0;
	}

	// enable/disable RSN in firmware if WPA is enabled/disabled
	// depending on variable adapter->secinfo.WPAenabled is set or not
	ret = libertas_prepare_and_send_command(priv, cmd_802_11_enable_rsn,
				    cmd_act_set, cmd_option_waitforrsp,
				    0, NULL);

	LEAVE();
	return ret;
}

/**
 *  @brief Set Auto prescan
 *  @param priv                 A pointer to wlan_private structure
 *  @param wrq			A pointer to iwreq structure
 *  @return 	   		0 --success, otherwise fail
 */
static int wlan_subcmd_setprescan_ioctl(wlan_private * priv, struct iwreq *wrq)
{
	int data;
	wlan_adapter *adapter = priv->adapter;
	int *val;

	data = SUBCMD_DATA(wrq);
	lbs_pr_debug(1, "WLAN_SUBCMD_SET_PRESCAN %d\n", data);
	adapter->prescan = data;

	val = (int *)wrq->u.name;
	*val = data;
	return 0;
}

static int wlan_set_multiple_dtim_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	u32 mdtim;
	int idata;
	int ret = -EINVAL;

	ENTER();

	idata = SUBCMD_DATA(wrq);
	mdtim = (u32) idata;
	if (((mdtim >= MRVDRV_MIN_MULTIPLE_DTIM)
	     && (mdtim <= MRVDRV_MAX_MULTIPLE_DTIM))
	    || (mdtim == MRVDRV_IGNORE_MULTIPLE_DTIM)) {
		priv->adapter->multipledtim = mdtim;
		ret = 0;
	}
	if (ret)
		lbs_pr_debug(1, "Invalid parameter, multipledtim not changed.\n");

	LEAVE();
	return ret;
}

/**
 *  @brief Set authentication mode
 *  @param priv                 A pointer to wlan_private structure
 *  @param req			A pointer to ifreq structure
 *  @return 	   		0 --success, otherwise fail
 */
static int wlan_setauthalg_ioctl(wlan_private * priv, struct ifreq *req)
{
	int alg;
	struct iwreq *wrq = (struct iwreq *)req;
	wlan_adapter *adapter = priv->adapter;

	if (wrq->u.data.flags == 0) {
		//from iwpriv subcmd
		alg = SUBCMD_DATA(wrq);
	} else {
		//from wpa_supplicant subcmd
		if (copy_from_user(&alg, wrq->u.data.pointer, sizeof(alg))) {
			lbs_pr_debug(1, "Copy from user failed\n");
			return -EFAULT;
		}
	}

	lbs_pr_debug(1, "auth alg is %#x\n", alg);

	switch (alg) {
	case AUTH_ALG_SHARED_KEY:
		adapter->secinfo.authmode = wlan802_11authmodeshared;
		break;
	case AUTH_ALG_NETWORK_EAP:
		adapter->secinfo.authmode =
		    wlan802_11authmodenetworkEAP;
		break;
	case AUTH_ALG_OPEN_SYSTEM:
	default:
		adapter->secinfo.authmode = wlan802_11authmodeopen;
		break;
	}
	return 0;
}

/**
 *  @brief Set 802.1x authentication mode
 *  @param priv                 A pointer to wlan_private structure
 *  @param req			A pointer to ifreq structure
 *  @return 	   		0 --success, otherwise fail
 */
static int wlan_set8021xauthalg_ioctl(wlan_private * priv, struct ifreq *req)
{
	int alg;
	struct iwreq *wrq = (struct iwreq *)req;

	if (wrq->u.data.flags == 0) {
		//from iwpriv subcmd
		alg = SUBCMD_DATA(wrq);
	} else {
		//from wpa_supplicant subcmd
		if (copy_from_user(&alg, wrq->u.data.pointer, sizeof(int))) {
			lbs_pr_debug(1, "Copy from user failed\n");
			return -EFAULT;
		}
	}
	lbs_pr_debug(1, "802.1x auth alg is %#x\n", alg);
	priv->adapter->secinfo.auth1xalg = alg;
	return 0;
}

static int wlan_setencryptionmode_ioctl(wlan_private * priv, struct ifreq *req)
{
	int mode;
	struct iwreq *wrq = (struct iwreq *)req;

	ENTER();

	if (wrq->u.data.flags == 0) {
		//from iwpriv subcmd
		mode = SUBCMD_DATA(wrq);
	} else {
		//from wpa_supplicant subcmd
		if (copy_from_user(&mode, wrq->u.data.pointer, sizeof(int))) {
			lbs_pr_debug(1, "Copy from user failed\n");
			return -EFAULT;
		}
	}
	lbs_pr_debug(1, "encryption mode is %#x\n", mode);
	priv->adapter->secinfo.Encryptionmode = mode;

	LEAVE();
	return 0;
}

static void adjust_mtu(wlan_private * priv)
{
	int mtu_increment = 0;

	if (priv->adapter->linkmode == WLAN_LINKMODE_802_11)
		mtu_increment += sizeof(struct ieee80211_hdr_4addr);

	if (priv->adapter->radiomode == WLAN_RADIOMODE_RADIOTAP)
		mtu_increment += max(sizeof(struct tx_radiotap_hdr),
				     sizeof(struct rx_radiotap_hdr));
	priv->wlan_dev.netdev->mtu = ETH_FRAME_LEN
	    - sizeof(struct ethhdr)
	    + mtu_increment;
}

/**
 *  @brief Set Link-Layer Layer mode
 *  @param priv                 A pointer to wlan_private structure
 *  @param req			A pointer to ifreq structure
 *  @return 	   		0 --success, otherwise fail
 */
static int wlan_set_linkmode_ioctl(wlan_private * priv, struct ifreq *req)
{
	int mode;

	mode = (int)((struct ifreq *)((u8 *) req + 4))->ifr_data;

	switch (mode) {
	case WLAN_LINKMODE_802_3:
		priv->adapter->linkmode = mode;
		break;
	case WLAN_LINKMODE_802_11:
		priv->adapter->linkmode = mode;
		break;
	default:
		lbs_pr_info("usb8388-5: invalid link-layer mode (%#x)\n",
		       mode);
		return -EINVAL;
		break;
	}
	lbs_pr_debug(1, "usb8388-5: link-layer mode is %#x\n", mode);

	adjust_mtu(priv);

	return 0;
}

/**
 *  @brief Set Radio header mode
 *  @param priv                 A pointer to wlan_private structure
 *  @param req			A pointer to ifreq structure
 *  @return 	   		0 --success, otherwise fail
 */
static int wlan_set_radiomode_ioctl(wlan_private * priv, struct ifreq *req)
{
	int mode;

	mode = (int)((struct ifreq *)((u8 *) req + 4))->ifr_data;

	switch (mode) {
	case WLAN_RADIOMODE_NONE:
		priv->adapter->radiomode = mode;
		break;
	case WLAN_RADIOMODE_RADIOTAP:
		priv->adapter->radiomode = mode;
		break;
	default:
		lbs_pr_debug(1, "usb8388-5: invalid radio header mode (%#x)\n",
		       mode);
		return -EINVAL;
	}
	lbs_pr_debug(1, "usb8388-5: radio-header mode is %#x\n", mode);

	adjust_mtu(priv);
	return 0;
}

/**
 *  @brief Set Debug header mode
 *  @param priv                 A pointer to wlan_private structure
 *  @param req			A pointer to ifreq structure
 *  @return 	   		0 --success, otherwise fail
 */
static int wlan_set_debugmode_ioctl(wlan_private * priv, struct ifreq *req)
{
	priv->adapter->debugmode = (int)((struct ifreq *)
					 ((u8 *) req + 4))->ifr_data;
	return 0;
}

static int wlan_subcmd_getrxantenna_ioctl(wlan_private * priv,
					  struct ifreq *req)
{
	int len;
	char buf[8];
	struct iwreq *wrq = (struct iwreq *)req;

	lbs_pr_debug(1, "WLAN_SUBCMD_GETRXANTENNA\n");
	len = getrxantenna(priv, buf);

	wrq->u.data.length = len;
	if (wrq->u.data.pointer) {
		if (copy_to_user(wrq->u.data.pointer, &buf, len)) {
			lbs_pr_debug(1, "CopyToUser failed\n");
			return -EFAULT;
		}
	}

	return 0;
}

static int wlan_subcmd_gettxantenna_ioctl(wlan_private * priv,
					  struct ifreq *req)
{
	int len;
	char buf[8];
	struct iwreq *wrq = (struct iwreq *)req;

	lbs_pr_debug(1, "WLAN_SUBCMD_GETTXANTENNA\n");
	len = gettxantenna(priv, buf);

	wrq->u.data.length = len;
	if (wrq->u.data.pointer) {
		if (copy_to_user(wrq->u.data.pointer, &buf, len)) {
			lbs_pr_debug(1, "CopyToUser failed\n");
			return -EFAULT;
		}
	}
	return 0;
}

/**
 *  @brief Get the MAC TSF value from the firmware
 *
 *  @param priv         A pointer to wlan_private structure
 *  @param wrq          A pointer to iwreq structure containing buffer
 *                      space to store a TSF value retrieved from the firmware
 *
 *  @return             0 if successful; IOCTL error code otherwise
 */
static int wlan_get_tsf_ioctl(wlan_private * priv, struct iwreq *wrq)
{
	u64 tsfval;
	int ret;

	ret = libertas_prepare_and_send_command(priv,
				    cmd_get_tsf,
				    0, cmd_option_waitforrsp, 0, &tsfval);

	lbs_pr_debug(1, "IOCTL: Get TSF = 0x%016llx\n", tsfval);

	if (ret != 0) {
		lbs_pr_debug(1, "IOCTL: Get TSF; command exec failed\n");
		ret = -EFAULT;
	} else {
		if (copy_to_user(wrq->u.data.pointer,
				 &tsfval,
				 min_t(size_t, wrq->u.data.length,
				     sizeof(tsfval))) != 0) {

			lbs_pr_debug(1, "IOCTL: Get TSF; Copy to user failed\n");
			ret = -EFAULT;
		} else {
			ret = 0;
		}
	}
	return ret;
}

/**
 *  @brief Get/Set adapt rate
 *  @param priv                 A pointer to wlan_private structure
 *  @param wrq			A pointer to iwreq structure
 *  @return 	   		0 --success, otherwise fail
 */
static int wlan_adapt_rateset(wlan_private * priv, struct iwreq *wrq)
{
	int ret;
	wlan_adapter *adapter = priv->adapter;
	int data[2];

	memset(data, 0, sizeof(data));
	if (!wrq->u.data.length) {
		lbs_pr_debug(1, "Get ADAPT RATE SET\n");
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_rate_adapt_rateset,
					    cmd_act_get,
					    cmd_option_waitforrsp, 0, NULL);
		data[0] = adapter->enablehwauto;
		data[1] = adapter->ratebitmap;
		if (copy_to_user(wrq->u.data.pointer, data, sizeof(int) * 2)) {
			lbs_pr_debug(1, "Copy to user failed\n");
			return -EFAULT;
		}
#define GET_TWO_INT	2
		wrq->u.data.length = GET_TWO_INT;
	} else {
		lbs_pr_debug(1, "Set ADAPT RATE SET\n");
		if (wrq->u.data.length > 2)
			return -EINVAL;
		if (copy_from_user
		    (data, wrq->u.data.pointer,
		     sizeof(int) * wrq->u.data.length)) {
			lbs_pr_debug(1, "Copy from user failed\n");
			return -EFAULT;
		}

		adapter->enablehwauto = data[0];
		adapter->ratebitmap = data[1];
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_rate_adapt_rateset,
					    cmd_act_set,
					    cmd_option_waitforrsp, 0, NULL);
	}
	return ret;
}

/**
 *  @brief Get/Set inactivity timeout
 *  @param priv                 A pointer to wlan_private structure
 *  @param wrq			A pointer to iwreq structure
 *  @return 	   		0 --success, otherwise fail
 */
static int wlan_inactivity_timeout(wlan_private * priv, struct iwreq *wrq)
{
	int ret;
	int data = 0;
	u16 timeout = 0;

	ENTER();
	if (wrq->u.data.length > 1)
		return -ENOTSUPP;

	if (wrq->u.data.length == 0) {
		/* Get */
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_inactivity_timeout,
					    cmd_act_get,
					    cmd_option_waitforrsp, 0,
					    &timeout);
		data = timeout;
		if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
			lbs_pr_debug(1, "Copy to user failed\n");
			return -EFAULT;
		}
	} else {
		/* Set */
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			lbs_pr_debug(1, "Copy from user failed\n");
			return -EFAULT;
		}

		timeout = data;
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_inactivity_timeout,
					    cmd_act_set,
					    cmd_option_waitforrsp, 0,
					    &timeout);
	}

	wrq->u.data.length = 1;

	LEAVE();
	return ret;
}

static int wlan_do_getlog_ioctl(wlan_private * priv, struct iwreq *wrq)
{
	int ret;
	char buf[GETLOG_BUFSIZE - 1];
	wlan_adapter *adapter = priv->adapter;

	lbs_pr_debug(1, " GET STATS\n");

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_get_log,
				    0, cmd_option_waitforrsp, 0, NULL);

	if (ret) {
		return ret;
	}

	if (wrq->u.data.pointer) {
		sprintf(buf, "\n  mcasttxframe %u failed %u retry %u "
			"multiretry %u framedup %u "
			"rtssuccess %u rtsfailure %u ackfailure %u\n"
			"rxfrag %u mcastrxframe %u fcserror %u "
			"txframe %u wepundecryptable %u ",
			adapter->logmsg.mcasttxframe,
			adapter->logmsg.failed,
			adapter->logmsg.retry,
			adapter->logmsg.multiretry,
			adapter->logmsg.framedup,
			adapter->logmsg.rtssuccess,
			adapter->logmsg.rtsfailure,
			adapter->logmsg.ackfailure,
			adapter->logmsg.rxfrag,
			adapter->logmsg.mcastrxframe,
			adapter->logmsg.fcserror,
			adapter->logmsg.txframe,
			adapter->logmsg.wepundecryptable);
		wrq->u.data.length = strlen(buf) + 1;
		if (copy_to_user(wrq->u.data.pointer, buf, wrq->u.data.length)) {
			lbs_pr_debug(1, "Copy to user failed\n");
			return -EFAULT;
		}
	}

	return 0;
}

static int wlan_scan_type_ioctl(wlan_private * priv, struct iwreq *wrq)
{
	u8 buf[12];
	u8 *option[] = { "active", "passive", "get", };
	int i, max_options = (sizeof(option) / sizeof(option[0]));
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;

	if (priv->adapter->enable11d) {
		lbs_pr_debug(1, "11D: Cannot set scantype when 11D enabled\n");
		return -EFAULT;
	}

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(buf, wrq->u.data.pointer, min_t(size_t, sizeof(buf),
							 wrq->u.data.length)))
		return -EFAULT;

	lbs_pr_debug(1, "Scan type Option = %s\n", buf);

	buf[sizeof(buf) - 1] = '\0';

	for (i = 0; i < max_options; i++) {
		if (!strcmp(buf, option[i]))
			break;
	}

	switch (i) {
	case 0:
		adapter->scantype = cmd_scan_type_active;
		break;
	case 1:
		adapter->scantype = cmd_scan_type_passive;
		break;
	case 2:
		wrq->u.data.length = strlen(option[adapter->scantype]) + 1;

		if (copy_to_user(wrq->u.data.pointer,
				 option[adapter->scantype],
				 wrq->u.data.length)) {
			lbs_pr_debug(1, "Copy to user failed\n");
			ret = -EFAULT;
		}

		break;
	default:
		lbs_pr_debug(1, "Invalid Scan type Ioctl Option\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int wlan_scan_mode_ioctl(wlan_private * priv, struct iwreq *wrq)
{
	wlan_adapter *adapter = priv->adapter;
	u8 buf[12];
	u8 *option[] = { "bss", "ibss", "any", "get" };
	int i, max_options = (sizeof(option) / sizeof(option[0]));
	int ret = 0;

	ENTER();

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(buf, wrq->u.data.pointer, min_t(size_t, sizeof(buf),
							 wrq->u.data.length))) {
		lbs_pr_debug(1, "Copy from user failed\n");
		return -EFAULT;
	}

	lbs_pr_debug(1, "Scan mode Option = %s\n", buf);

	buf[sizeof(buf) - 1] = '\0';

	for (i = 0; i < max_options; i++) {
		if (!strcmp(buf, option[i]))
			break;
	}

	switch (i) {

	case 0:
		adapter->scanmode = cmd_bss_type_bss;
		break;
	case 1:
		adapter->scanmode = cmd_bss_type_ibss;
		break;
	case 2:
		adapter->scanmode = cmd_bss_type_any;
		break;
	case 3:

		wrq->u.data.length = strlen(option[adapter->scanmode - 1]) + 1;

		lbs_pr_debug(1, "Get Scan mode Option = %s\n",
		       option[adapter->scanmode - 1]);

		lbs_pr_debug(1, "Scan mode length %d\n", wrq->u.data.length);

		if (copy_to_user(wrq->u.data.pointer,
				 option[adapter->scanmode - 1],
				 wrq->u.data.length)) {
			lbs_pr_debug(1, "Copy to user failed\n");
			ret = -EFAULT;
		}
		lbs_pr_debug(1, "GET Scan type Option after copy = %s\n",
		       (char *)wrq->u.data.pointer);

		break;

	default:
		lbs_pr_debug(1, "Invalid Scan mode Ioctl Option\n");
		ret = -EINVAL;
		break;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Get/Set Adhoc G Rate
 *
 *  @param priv		A pointer to wlan_private structure
 *  @param wrq	   	A pointer to user data
 *  @return 	   	0--success, otherwise fail
 */
static int wlan_do_set_grate_ioctl(wlan_private * priv, struct iwreq *wrq)
{
	wlan_adapter *adapter = priv->adapter;
	int data, data1;
	int *val;

	ENTER();

	data1 = SUBCMD_DATA(wrq);
	switch (data1) {
	case 0:
		adapter->adhoc_grate_enabled = 0;
		break;
	case 1:
		adapter->adhoc_grate_enabled = 1;
		break;
	case 2:
		break;
	default:
		return -EINVAL;
	}
	data = adapter->adhoc_grate_enabled;
	val = (int *)wrq->u.name;
	*val = data;
	LEAVE();
	return 0;
}

static inline int hex2int(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return -1;
}

/* Convert a string representation of a MAC address ("xx:xx:xx:xx:xx:xx")
   into binary format (6 bytes).

   This function expects that each byte is represented with 2 characters
   (e.g., 11:2:11:11:11:11 is invalid)

 */
static char *eth_str2addr(char *ethstr, u8 * addr)
{
	int i, val, val2;
	char *pos = ethstr;

	/* get rid of initial blanks */
	while (*pos == ' ' || *pos == '\t')
		++pos;

	for (i = 0; i < 6; i++) {
		val = hex2int(*pos++);
		if (val < 0)
			return NULL;
		val2 = hex2int(*pos++);
		if (val2 < 0)
			return NULL;
		addr[i] = (val * 16 + val2) & 0xff;

		if (i < 5 && *pos++ != ':')
			return NULL;
	}
	return pos;
}

/* this writes xx:xx:xx:xx:xx:xx into ethstr
   (ethstr must have space for 18 chars) */
static int eth_addr2str(u8 * addr, char *ethstr)
{
	int i;
	char *pos = ethstr;

	for (i = 0; i < 6; i++) {
		sprintf(pos, "%02x", addr[i] & 0xff);
		pos += 2;
		if (i < 5)
			*pos++ = ':';
	}
	return 17;
}

/**
 *  @brief          Add an entry to the BT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_bt_add_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char ethaddrs_str[18];
	char *pos;
	u8 ethaddr[ETH_ALEN];

	ENTER();
	if (copy_from_user(ethaddrs_str, wrq->u.data.pointer,
			   sizeof(ethaddrs_str)))
		return -EFAULT;

	if ((pos = eth_str2addr(ethaddrs_str, ethaddr)) == NULL) {
		lbs_pr_info("BT_ADD: Invalid MAC address\n");
		return -EINVAL;
	}

	lbs_pr_debug(1, "BT: adding %s\n", ethaddrs_str);
	LEAVE();
	return (libertas_prepare_and_send_command(priv, cmd_bt_access,
				      cmd_act_bt_access_add,
				      cmd_option_waitforrsp, 0, ethaddr));
}

/**
 *  @brief          Delete an entry from the BT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_bt_del_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char ethaddrs_str[18];
	u8 ethaddr[ETH_ALEN];
	char *pos;

	ENTER();
	if (copy_from_user(ethaddrs_str, wrq->u.data.pointer,
			   sizeof(ethaddrs_str)))
		return -EFAULT;

	if ((pos = eth_str2addr(ethaddrs_str, ethaddr)) == NULL) {
		lbs_pr_info("Invalid MAC address\n");
		return -EINVAL;
	}

	lbs_pr_debug(1, "BT: deleting %s\n", ethaddrs_str);

	return (libertas_prepare_and_send_command(priv,
				      cmd_bt_access,
				      cmd_act_bt_access_del,
				      cmd_option_waitforrsp, 0, ethaddr));
	LEAVE();
	return 0;
}

/**
 *  @brief          Reset all entries from the BT table
 *  @param priv     A pointer to wlan_private structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_bt_reset_ioctl(wlan_private * priv)
{
	ENTER();

	lbs_pr_alert( "BT: resetting\n");

	return (libertas_prepare_and_send_command(priv,
				      cmd_bt_access,
				      cmd_act_bt_access_reset,
				      cmd_option_waitforrsp, 0, NULL));

	LEAVE();
	return 0;
}

/**
 *  @brief          List an entry from the BT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_bt_list_ioctl(wlan_private * priv, struct ifreq *req)
{
	int pos;
	char *addr1;
	struct iwreq *wrq = (struct iwreq *)req;
	/* used to pass id and store the bt entry returned by the FW */
	union {
		int id;
		char addr1addr2[2 * ETH_ALEN];
	} param;
	static char outstr[64];
	char *pbuf = outstr;
	int ret;

	ENTER();

	if (copy_from_user(outstr, wrq->u.data.pointer, sizeof(outstr))) {
		lbs_pr_debug(1, "Copy from user failed\n");
		return -1;
	}
	param.id = simple_strtoul(outstr, NULL, 10);
	pos = sprintf(pbuf, "%d: ", param.id);
	pbuf += pos;

	ret = libertas_prepare_and_send_command(priv, cmd_bt_access,
				    cmd_act_bt_access_list,
				    cmd_option_waitforrsp, 0,
				    (char *)&param);

	if (ret == 0) {
		addr1 = param.addr1addr2;

		pos = sprintf(pbuf, "ignoring traffic from ");
		pbuf += pos;
		pos = eth_addr2str(addr1, pbuf);
		pbuf += pos;
	} else {
		sprintf(pbuf, "(null)");
		pbuf += pos;
	}

	wrq->u.data.length = strlen(outstr);
	if (copy_to_user(wrq->u.data.pointer, (char *)outstr,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "BT_LIST: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          Find the next parameter in an input string
 *  @param ptr      A pointer to the input parameter string
 *  @return         A pointer to the next parameter, or 0 if no parameters left.
 */
static char * next_param(char * ptr)
{
	if (!ptr) return NULL;
	while (*ptr == ' ' || *ptr == '\t') ++ptr;
	return (*ptr == '\0') ? NULL : ptr;
}

/**
 *  @brief          Add an entry to the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_add_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[128];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	if ((ptr = eth_str2addr(in_str, fwt_access.da)) == NULL) {
		lbs_pr_alert( "FWT_ADD: Invalid MAC address 1\n");
		return -EINVAL;
	}

	if ((ptr = eth_str2addr(ptr, fwt_access.ra)) == NULL) {
		lbs_pr_alert( "FWT_ADD: Invalid MAC address 2\n");
		return -EINVAL;
	}

	if ((ptr = next_param(ptr)))
		fwt_access.metric =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.metric = FWT_DEFAULT_METRIC;

	if ((ptr = next_param(ptr)))
		fwt_access.dir = (u8)simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.dir = FWT_DEFAULT_DIR;

	if ((ptr = next_param(ptr)))
		fwt_access.ssn =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.ssn = FWT_DEFAULT_SSN;

	if ((ptr = next_param(ptr)))
		fwt_access.dsn =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.dsn = FWT_DEFAULT_DSN;

	if ((ptr = next_param(ptr)))
		fwt_access.hopcount = simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.hopcount = FWT_DEFAULT_HOPCOUNT;

	if ((ptr = next_param(ptr)))
		fwt_access.ttl = simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.ttl = FWT_DEFAULT_TTL;

	if ((ptr = next_param(ptr)))
		fwt_access.expiration =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.expiration = FWT_DEFAULT_EXPIRATION;

	if ((ptr = next_param(ptr)))
		fwt_access.sleepmode = (u8)simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.sleepmode = FWT_DEFAULT_SLEEPMODE;

	if ((ptr = next_param(ptr)))
		fwt_access.snr =
			cpu_to_le32(simple_strtoul(ptr, &ptr, 10));
	else
		fwt_access.snr = FWT_DEFAULT_SNR;

#ifdef DEBUG
	{
		char ethaddr1_str[18], ethaddr2_str[18];
		eth_addr2str(fwt_access.da, ethaddr1_str);
		eth_addr2str(fwt_access.ra, ethaddr2_str);
		lbs_pr_debug(1, "FWT_ADD: adding (da:%s,%i,ra:%s)\n", ethaddr1_str,
		       fwt_access.dir, ethaddr2_str);
		lbs_pr_debug(1, "FWT_ADD: ssn:%u dsn:%u met:%u hop:%u ttl:%u exp:%u slp:%u snr:%u\n",
		       fwt_access.ssn, fwt_access.dsn, fwt_access.metric,
		       fwt_access.hopcount, fwt_access.ttl, fwt_access.expiration,
		       fwt_access.sleepmode, fwt_access.snr);
	}
#endif

	LEAVE();
	return (libertas_prepare_and_send_command(priv, cmd_fwt_access,
						  cmd_act_fwt_access_add,
						  cmd_option_waitforrsp, 0,
						  (void *)&fwt_access));
}

/**
 *  @brief          Delete an entry from the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_del_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[64];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	if ((ptr = eth_str2addr(in_str, fwt_access.da)) == NULL) {
		lbs_pr_alert( "FWT_DEL: Invalid MAC address 1\n");
		return -EINVAL;
	}

	if ((ptr = eth_str2addr(ptr, fwt_access.ra)) == NULL) {
		lbs_pr_alert( "FWT_DEL: Invalid MAC address 2\n");
		return -EINVAL;
	}

	if ((ptr = next_param(ptr)))
		fwt_access.dir = (u8)simple_strtoul(ptr, &ptr, 10);
	else
		fwt_access.dir = FWT_DEFAULT_DIR;

#ifdef DEBUG
	{
		char ethaddr1_str[18], ethaddr2_str[18];
		lbs_pr_debug(1, "FWT_DEL: line is %s\n", in_str);
		eth_addr2str(fwt_access.da, ethaddr1_str);
		eth_addr2str(fwt_access.ra, ethaddr2_str);
		lbs_pr_debug(1, "FWT_DEL: removing (da:%s,ra:%s,dir:%d)\n", ethaddr1_str,
		       ethaddr2_str, fwt_access.dir);
	}
#endif

	LEAVE();
	return (libertas_prepare_and_send_command(priv,
						  cmd_fwt_access,
						  cmd_act_fwt_access_del,
						  cmd_option_waitforrsp, 0,
						  (void *)&fwt_access));
}


/**
 *  @brief             Print route parameters
 *  @param fwt_access  struct cmd_ds_fwt_access with route info
 *  @param buf         destination buffer for route info
 */
static void print_route(struct cmd_ds_fwt_access fwt_access, char *buf)
{
	buf += sprintf(buf, " ");
	buf += eth_addr2str(fwt_access.da, buf);
	buf += sprintf(buf, " ");
	buf += eth_addr2str(fwt_access.ra, buf);
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.metric));
	buf += sprintf(buf, " %u", fwt_access.dir);
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.ssn));
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.dsn));
	buf += sprintf(buf, " %u", fwt_access.hopcount);
	buf += sprintf(buf, " %u", fwt_access.ttl);
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.expiration));
	buf += sprintf(buf, " %u", fwt_access.sleepmode);
	buf += sprintf(buf, " %u", le32_to_cpu(fwt_access.snr));
}

/**
 *  @brief          Lookup an entry in the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_lookup_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[64];
	char *ptr;
	static struct cmd_ds_fwt_access fwt_access;
	static char out_str[128];
	int ret;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	if ((ptr = eth_str2addr(in_str, fwt_access.da)) == NULL) {
		lbs_pr_alert( "FWT_LOOKUP: Invalid MAC address\n");
		return -EINVAL;
	}

#ifdef DEBUG
	{
		char ethaddr1_str[18];
		lbs_pr_debug(1, "FWT_LOOKUP: line is %s\n", in_str);
		eth_addr2str(fwt_access.da, ethaddr1_str);
		lbs_pr_debug(1, "FWT_LOOKUP: looking for (da:%s)\n", ethaddr1_str);
	}
#endif

	ret = libertas_prepare_and_send_command(priv,
						cmd_fwt_access,
						cmd_act_fwt_access_lookup,
						cmd_option_waitforrsp, 0,
						(void *)&fwt_access);

	if (ret == 0)
		print_route(fwt_access, out_str);
	else
		sprintf(out_str, "(null)");

	wrq->u.data.length = strlen(out_str);
	if (copy_to_user(wrq->u.data.pointer, (char *)out_str,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "FWT_LOOKUP: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          Reset all entries from the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_reset_ioctl(wlan_private * priv)
{
	lbs_pr_debug(1, "FWT: resetting\n");

	return (libertas_prepare_and_send_command(priv,
				      cmd_fwt_access,
				      cmd_act_fwt_access_reset,
				      cmd_option_waitforrsp, 0, NULL));
}

/**
 *  @brief          List an entry from the FWT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_list_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[8];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr = in_str;
	static char out_str[128];
	char *pbuf = out_str;
	int ret;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	fwt_access.id = cpu_to_le32(simple_strtoul(ptr, &ptr, 10));

#ifdef DEBUG
	{
		lbs_pr_debug(1, "FWT_LIST: line is %s\n", in_str);
		lbs_pr_debug(1, "FWT_LIST: listing id:%i\n", le32_to_cpu(fwt_access.id));
	}
#endif

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_list,
				    cmd_option_waitforrsp, 0, (void *)&fwt_access);

	if (ret == 0)
		print_route(fwt_access, pbuf);
	else
		pbuf += sprintf(pbuf, " (null)");

	wrq->u.data.length = strlen(out_str);
	if (copy_to_user(wrq->u.data.pointer, (char *)out_str,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "FWT_LIST: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          List an entry from the FRT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_list_route_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[64];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr = in_str;
	static char out_str[128];
	char *pbuf = out_str;
	int ret;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	fwt_access.id = cpu_to_le32(simple_strtoul(ptr, &ptr, 10));

#ifdef DEBUG
	{
		lbs_pr_debug(1, "FWT_LIST_ROUTE: line is %s\n", in_str);
		lbs_pr_debug(1, "FWT_LIST_ROUTE: listing id:%i\n", le32_to_cpu(fwt_access.id));
	}
#endif

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_list_route,
				    cmd_option_waitforrsp, 0, (void *)&fwt_access);

	if (ret == 0) {
		pbuf += sprintf(pbuf, " ");
		pbuf += eth_addr2str(fwt_access.da, pbuf);
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.metric));
		pbuf += sprintf(pbuf, " %u", fwt_access.dir);
		/* note that the firmware returns the nid in the id field */
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.id));
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.ssn));
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.dsn));
		pbuf += sprintf(pbuf, "  hop %u", fwt_access.hopcount);
		pbuf += sprintf(pbuf, "  ttl %u", fwt_access.ttl);
		pbuf += sprintf(pbuf, " %u", le32_to_cpu(fwt_access.expiration));
	} else
		pbuf += sprintf(pbuf, " (null)");

	wrq->u.data.length = strlen(out_str);
	if (copy_to_user(wrq->u.data.pointer, (char *)out_str,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "FWT_LIST_ROUTE: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          List an entry from the FNT table
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_list_neighbor_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct iwreq *wrq = (struct iwreq *)req;
	char in_str[8];
	static struct cmd_ds_fwt_access fwt_access;
	char *ptr = in_str;
	static char out_str[128];
	char *pbuf = out_str;
	int ret;

	ENTER();
	if (copy_from_user(in_str, wrq->u.data.pointer, sizeof(in_str)))
		return -EFAULT;

	memset(&fwt_access, 0, sizeof(fwt_access));
	fwt_access.id = cpu_to_le32(simple_strtoul(ptr, &ptr, 10));

#ifdef DEBUG
	{
		lbs_pr_debug(1, "FWT_LIST_NEIGHBOR: line is %s\n", in_str);
		lbs_pr_debug(1, "FWT_LIST_NEIGHBOR: listing id:%i\n", le32_to_cpu(fwt_access.id));
	}
#endif

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_list_neighbor,
				    cmd_option_waitforrsp, 0,
				    (void *)&fwt_access);

	if (ret == 0) {
		pbuf += sprintf(pbuf, " ra ");
		pbuf += eth_addr2str(fwt_access.ra, pbuf);
		pbuf += sprintf(pbuf, "  slp %u", fwt_access.sleepmode);
		pbuf += sprintf(pbuf, "  snr %u", le32_to_cpu(fwt_access.snr));
		pbuf += sprintf(pbuf, "  ref %u", le32_to_cpu(fwt_access.references));
	} else
		pbuf += sprintf(pbuf, " (null)");

	wrq->u.data.length = strlen(out_str);
	if (copy_to_user(wrq->u.data.pointer, (char *)out_str,
			 wrq->u.data.length)) {
		lbs_pr_debug(1, "FWT_LIST_NEIGHBOR: Copy to user failed!\n");
		return -EFAULT;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief          Cleans up the route (FRT) and neighbor (FNT) tables
 *                  (Garbage Collection)
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_cleanup_ioctl(wlan_private * priv, struct ifreq *req)
{
	static struct cmd_ds_fwt_access fwt_access;
	int ret;

	ENTER();

	lbs_pr_debug(1, "FWT: cleaning up\n");

	memset(&fwt_access, 0, sizeof(fwt_access));

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_cleanup,
				    cmd_option_waitforrsp, 0,
				    (void *)&fwt_access);

	if (ret == 0)
		req->ifr_data = (char *)(le32_to_cpu(fwt_access.references));
	else
		return -EFAULT;

	LEAVE();
	return 0;
}

/**
 *  @brief          Gets firmware internal time (debug purposes)
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_fwt_time_ioctl(wlan_private * priv, struct ifreq *req)
{
	static struct cmd_ds_fwt_access fwt_access;
	int ret;

	ENTER();

	lbs_pr_debug(1, "FWT: getting time\n");

	memset(&fwt_access, 0, sizeof(fwt_access));

	ret = libertas_prepare_and_send_command(priv, cmd_fwt_access,
				    cmd_act_fwt_access_time,
				    cmd_option_waitforrsp, 0,
				    (void *)&fwt_access);

	if (ret == 0)
		req->ifr_data = (char *)(le32_to_cpu(fwt_access.references));
	else
		return -EFAULT;

	LEAVE();
	return 0;
}

/**
 *  @brief          Gets mesh ttl from firmware
 *  @param priv     A pointer to wlan_private structure
 *  @param req      A pointer to ifreq structure
 *  @return         0 --success, otherwise fail
 */
static int wlan_mesh_get_ttl_ioctl(wlan_private * priv, struct ifreq *req)
{
	struct cmd_ds_mesh_access mesh_access;
	int ret;

	ENTER();

	memset(&mesh_access, 0, sizeof(mesh_access));

	ret = libertas_prepare_and_send_command(priv, cmd_mesh_access,
				    cmd_act_mesh_get_ttl,
				    cmd_option_waitforrsp, 0,
				    (void *)&mesh_access);

	if (ret == 0) {
		req->ifr_data = (char *)(le32_to_cpu(mesh_access.data[0]));
	}
	else
		return -EFAULT;

	LEAVE();
	return 0;
}

/**
 *  @brief          Gets mesh ttl from firmware
 *  @param priv     A pointer to wlan_private structure
 *  @param ttl      New ttl value
 *  @return         0 --success, otherwise fail
 */
static int wlan_mesh_set_ttl_ioctl(wlan_private * priv, int ttl)
{
	struct cmd_ds_mesh_access mesh_access;
	int ret;

	ENTER();

	if( (ttl > 0xff) || (ttl < 0) )
		return -EINVAL;

	memset(&mesh_access, 0, sizeof(mesh_access));
	mesh_access.data[0] = ttl;

	ret = libertas_prepare_and_send_command(priv, cmd_mesh_access,
						cmd_act_mesh_set_ttl,
						cmd_option_waitforrsp, 0,
						(void *)&mesh_access);

	if (ret != 0)
		ret = -EFAULT;

	LEAVE();
	return ret;
}

/**
 *  @brief ioctl function - entry point
 *
 *  @param dev		A pointer to net_device structure
 *  @param req	   	A pointer to ifreq structure
 *  @param cmd 		command
 *  @return 	   	0--success, otherwise fail
 */
int libertas_do_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	int subcmd = 0;
	int idata = 0;
	int *pdata;
	int ret = 0;
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct iwreq *wrq = (struct iwreq *)req;

	ENTER();

	lbs_pr_debug(1, "libertas_do_ioctl: ioctl cmd = 0x%x\n", cmd);
	switch (cmd) {
	case WLANSCAN_TYPE:
		lbs_pr_debug(1, "Scan type Ioctl\n");
		ret = wlan_scan_type_ioctl(priv, wrq);
		break;

	case WLAN_SETNONE_GETNONE:	/* set WPA mode on/off ioctl #20 */
		switch (wrq->u.data.flags) {
		case WLANDEAUTH:
			lbs_pr_debug(1, "Deauth\n");
			libertas_send_deauth(priv);
			break;

		case WLANADHOCSTOP:
			lbs_pr_debug(1, "Adhoc stop\n");
			ret = libertas_do_adhocstop_ioctl(priv);
			break;

		case WLANRADIOON:
			wlan_radio_ioctl(priv, 1);
			break;

		case WLANRADIOOFF:
			wlan_radio_ioctl(priv, 0);
			break;
		case WLANWLANIDLEON:
			libertas_idle_on(priv);
			break;
		case WLANWLANIDLEOFF:
			libertas_idle_off(priv);
			break;
		case WLAN_SUBCMD_BT_RESET:	/* bt_reset */
			wlan_bt_reset_ioctl(priv);
			break;
		case WLAN_SUBCMD_FWT_RESET:	/* fwt_reset */
			wlan_fwt_reset_ioctl(priv);
			break;
		}		/* End of switch */
		break;

	case WLANSETWPAIE:
		ret = wlan_setwpaie_ioctl(priv, req);
		break;
	case WLAN_SETINT_GETINT:
		/* The first 4 bytes of req->ifr_data is sub-ioctl number
		 * after 4 bytes sits the payload.
		 */
		subcmd = (int)req->ifr_data;	//from iwpriv subcmd
		switch (subcmd) {
		case WLANNF:
			ret = wlan_get_nf(priv, wrq);
			break;
		case WLANRSSI:
			ret = wlan_get_rssi(priv, wrq);
			break;
		case WLANENABLE11D:
			ret = libertas_cmd_enable_11d(priv, wrq);
			break;
		case WLANADHOCGRATE:
			ret = wlan_do_set_grate_ioctl(priv, wrq);
			break;
		case WLAN_SUBCMD_SET_PRESCAN:
			ret = wlan_subcmd_setprescan_ioctl(priv, wrq);
			break;
		}
		break;

	case WLAN_SETONEINT_GETONEINT:
		switch (wrq->u.data.flags) {
		case WLAN_BEACON_INTERVAL:
			ret = wlan_beacon_interval(priv, wrq);
			break;

		case WLAN_LISTENINTRVL:
			if (!wrq->u.data.length) {
				int data;
				lbs_pr_debug(1, "Get locallisteninterval value\n");
#define GET_ONE_INT	1
				data = adapter->locallisteninterval;
				if (copy_to_user(wrq->u.data.pointer,
						 &data, sizeof(int))) {
					lbs_pr_debug(1, "Copy to user failed\n");
					return -EFAULT;
				}

				wrq->u.data.length = GET_ONE_INT;
			} else {
				int data;
				if (copy_from_user
				    (&data, wrq->u.data.pointer, sizeof(int))) {
					lbs_pr_debug(1, "Copy from user failed\n");
					return -EFAULT;
				}

				lbs_pr_debug(1, "Set locallisteninterval = %d\n",
				       data);
#define MAX_U16_VAL	65535
				if (data > MAX_U16_VAL) {
					lbs_pr_debug(1, "Exceeds U16 value\n");
					return -EINVAL;
				}
				adapter->locallisteninterval = data;
			}
			break;
		case WLAN_TXCONTROL:
			ret = wlan_txcontrol(priv, wrq);	//adds for txcontrol ioctl
			break;

		case WLAN_NULLPKTINTERVAL:
			ret = wlan_null_pkt_interval(priv, wrq);
			break;

		default:
			ret = -EOPNOTSUPP;
			break;
		}
		break;

	case WLAN_SETONEINT_GETNONE:
		/* The first 4 bytes of req->ifr_data is sub-ioctl number
		 * after 4 bytes sits the payload.
		 */
		subcmd = wrq->u.data.flags;	//from wpa_supplicant subcmd

		if (!subcmd)
			subcmd = (int)req->ifr_data;	//from iwpriv subcmd

		switch (subcmd) {
		case WLAN_SUBCMD_SETRXANTENNA:	/* SETRXANTENNA */
			idata = SUBCMD_DATA(wrq);
			ret = setrxantenna(priv, idata);
			break;
		case WLAN_SUBCMD_SETTXANTENNA:	/* SETTXANTENNA */
			idata = SUBCMD_DATA(wrq);
			ret = settxantenna(priv, idata);
			break;
		case WLAN_SET_ATIM_WINDOW:
			adapter->atimwindow = SUBCMD_DATA(wrq);
			adapter->atimwindow = min_t(__u16, adapter->atimwindow, 50);
			break;
		case WLANSETBCNAVG:
			adapter->bcn_avg_factor = SUBCMD_DATA(wrq);
			if (adapter->bcn_avg_factor == 0)
				adapter->bcn_avg_factor =
				    DEFAULT_BCN_AVG_FACTOR;
			if (adapter->bcn_avg_factor > DEFAULT_BCN_AVG_FACTOR)
				adapter->bcn_avg_factor =
				    DEFAULT_BCN_AVG_FACTOR;
			break;
		case WLANSETDATAAVG:
			adapter->data_avg_factor = SUBCMD_DATA(wrq);
			if (adapter->data_avg_factor == 0)
				adapter->data_avg_factor =
				    DEFAULT_DATA_AVG_FACTOR;
			if (adapter->data_avg_factor > DEFAULT_DATA_AVG_FACTOR)
				adapter->data_avg_factor =
				    DEFAULT_DATA_AVG_FACTOR;
			break;
		case WLANSETREGION:
			idata = SUBCMD_DATA(wrq);
			ret = wlan_set_region(priv, (u16) idata);
			break;

		case WLAN_SET_LISTEN_INTERVAL:
			idata = SUBCMD_DATA(wrq);
			adapter->listeninterval = (u16) idata;
			break;

		case WLAN_SET_MULTIPLE_DTIM:
			ret = wlan_set_multiple_dtim_ioctl(priv, req);
			break;

		case WLANSETAUTHALG:
			ret = wlan_setauthalg_ioctl(priv, req);
			break;

		case WLANSET8021XAUTHALG:
			ret = wlan_set8021xauthalg_ioctl(priv, req);
			break;

		case WLANSETENCRYPTIONMODE:
			ret = wlan_setencryptionmode_ioctl(priv, req);
			break;

		case WLAN_SET_LINKMODE:
			ret = wlan_set_linkmode_ioctl(priv, req);
			break;

		case WLAN_SET_RADIOMODE:
			ret = wlan_set_radiomode_ioctl(priv, req);
			break;

		case WLAN_SET_DEBUGMODE:
			ret = wlan_set_debugmode_ioctl(priv, req);
			break;

		case WLAN_SUBCMD_MESH_SET_TTL:
			idata = SUBCMD_DATA(wrq);
			ret = wlan_mesh_set_ttl_ioctl(priv, idata);
			break;

		default:
			ret = -EOPNOTSUPP;
			break;
		}

		break;

	case WLAN_SETNONE_GETTWELVE_CHAR:	/* Get Antenna settings */
		/*
		 * We've not used IW_PRIV_TYPE_FIXED so sub-ioctl number is
		 * in flags of iwreq structure, otherwise it will be in
		 * mode member of iwreq structure.
		 */
		switch ((int)wrq->u.data.flags) {
		case WLAN_SUBCMD_GETRXANTENNA:	/* Get Rx Antenna */
			ret = wlan_subcmd_getrxantenna_ioctl(priv, req);
			break;

		case WLAN_SUBCMD_GETTXANTENNA:	/* Get Tx Antenna */
			ret = wlan_subcmd_gettxantenna_ioctl(priv, req);
			break;

		case WLAN_GET_TSF:
			ret = wlan_get_tsf_ioctl(priv, wrq);
			break;
		}
		break;

	case WLAN_SET128CHAR_GET128CHAR:
		switch ((int)wrq->u.data.flags) {

		case WLANSCAN_MODE:
			lbs_pr_debug(1, "Scan mode Ioctl\n");
			ret = wlan_scan_mode_ioctl(priv, wrq);
			break;

		case WLAN_GET_ADHOC_STATUS:
			ret = wlan_get_adhoc_status_ioctl(priv, wrq);
			break;
		case WLAN_SUBCMD_BT_ADD:
			ret = wlan_bt_add_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_BT_DEL:
			ret = wlan_bt_del_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_BT_LIST:
			ret = wlan_bt_list_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_ADD:
			ret = wlan_fwt_add_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_DEL:
			ret = wlan_fwt_del_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_LOOKUP:
			ret = wlan_fwt_lookup_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_LIST_NEIGHBOR:
			ret = wlan_fwt_list_neighbor_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_LIST:
			ret = wlan_fwt_list_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_LIST_ROUTE:
			ret = wlan_fwt_list_route_ioctl(priv, req);
			break;
		}
		break;

	case WLAN_SETNONE_GETONEINT:
		switch ((int)req->ifr_data) {
		case WLANGETBCNAVG:
			pdata = (int *)wrq->u.name;
			*pdata = (int)adapter->bcn_avg_factor;
			break;

		case WLANGETREGION:
			pdata = (int *)wrq->u.name;
			*pdata = (int)adapter->regioncode;
			break;

		case WLAN_GET_LISTEN_INTERVAL:
			pdata = (int *)wrq->u.name;
			*pdata = (int)adapter->listeninterval;
			break;

		case WLAN_GET_LINKMODE:
			req->ifr_data = (char *)((u32) adapter->linkmode);
			break;

		case WLAN_GET_RADIOMODE:
			req->ifr_data = (char *)((u32) adapter->radiomode);
			break;

		case WLAN_GET_DEBUGMODE:
			req->ifr_data = (char *)((u32) adapter->debugmode);
			break;

		case WLAN_GET_MULTIPLE_DTIM:
			pdata = (int *)wrq->u.name;
			*pdata = (int)adapter->multipledtim;
			break;
		case WLAN_GET_TX_RATE:
			ret = wlan_get_txrate_ioctl(priv, req);
			break;
		case WLAN_SUBCMD_FWT_CLEANUP:	/* fwt_cleanup */
			ret = wlan_fwt_cleanup_ioctl(priv, req);
			break;

		case WLAN_SUBCMD_FWT_TIME:	/* fwt_time */
			ret = wlan_fwt_time_ioctl(priv, req);
			break;

		case WLAN_SUBCMD_MESH_GET_TTL:
			ret = wlan_mesh_get_ttl_ioctl(priv, req);
			break;

		default:
			ret = -EOPNOTSUPP;

		}

		break;

	case WLANGETLOG:
		ret = wlan_do_getlog_ioctl(priv, wrq);
		break;

	case WLAN_SET_GET_SIXTEEN_INT:
		switch ((int)wrq->u.data.flags) {
		case WLAN_TPCCFG:
			{
				int data[5];
				struct cmd_ds_802_11_tpc_cfg cfg;
				memset(&cfg, 0, sizeof(cfg));
				if ((wrq->u.data.length > 1)
				    && (wrq->u.data.length != 5))
					return -1;

				if (wrq->u.data.length == 0) {
					cfg.action =
					    cpu_to_le16
					    (cmd_act_get);
				} else {
					if (copy_from_user
					    (data, wrq->u.data.pointer,
					     sizeof(int) * 5)) {
						lbs_pr_debug(1,
						       "Copy from user failed\n");
						return -EFAULT;
					}

					cfg.action =
					    cpu_to_le16
					    (cmd_act_set);
					cfg.enable = data[0];
					cfg.usesnr = data[1];
					cfg.P0 = data[2];
					cfg.P1 = data[3];
					cfg.P2 = data[4];
				}

				ret =
				    libertas_prepare_and_send_command(priv,
							  cmd_802_11_tpc_cfg,
							  0,
							  cmd_option_waitforrsp,
							  0, (void *)&cfg);

				data[0] = cfg.enable;
				data[1] = cfg.usesnr;
				data[2] = cfg.P0;
				data[3] = cfg.P1;
				data[4] = cfg.P2;
				if (copy_to_user
				    (wrq->u.data.pointer, data,
				     sizeof(int) * 5)) {
					lbs_pr_debug(1, "Copy to user failed\n");
					return -EFAULT;
				}

				wrq->u.data.length = 5;
			}
			break;

		case WLAN_POWERCFG:
			{
				int data[4];
				struct cmd_ds_802_11_pwr_cfg cfg;
				memset(&cfg, 0, sizeof(cfg));
				if ((wrq->u.data.length > 1)
				    && (wrq->u.data.length != 4))
					return -1;
				if (wrq->u.data.length == 0) {
					cfg.action =
					    cpu_to_le16
					    (cmd_act_get);
				} else {
					if (copy_from_user
					    (data, wrq->u.data.pointer,
					     sizeof(int) * 4)) {
						lbs_pr_debug(1,
						       "Copy from user failed\n");
						return -EFAULT;
					}

					cfg.action =
					    cpu_to_le16
					    (cmd_act_set);
					cfg.enable = data[0];
					cfg.PA_P0 = data[1];
					cfg.PA_P1 = data[2];
					cfg.PA_P2 = data[3];
				}
				ret =
				    libertas_prepare_and_send_command(priv,
							  cmd_802_11_pwr_cfg,
							  0,
							  cmd_option_waitforrsp,
							  0, (void *)&cfg);
				data[0] = cfg.enable;
				data[1] = cfg.PA_P0;
				data[2] = cfg.PA_P1;
				data[3] = cfg.PA_P2;
				if (copy_to_user
				    (wrq->u.data.pointer, data,
				     sizeof(int) * 4)) {
					lbs_pr_debug(1, "Copy to user failed\n");
					return -EFAULT;
				}

				wrq->u.data.length = 4;
			}
			break;
		case WLAN_AUTO_FREQ_SET:
			{
				int data[3];
				struct cmd_ds_802_11_afc afc;
				memset(&afc, 0, sizeof(afc));
				if (wrq->u.data.length != 3)
					return -1;
				if (copy_from_user
				    (data, wrq->u.data.pointer,
				     sizeof(int) * 3)) {
					lbs_pr_debug(1, "Copy from user failed\n");
					return -EFAULT;
				}
				afc.afc_auto = data[0];

				if (afc.afc_auto != 0) {
					afc.threshold = data[1];
					afc.period = data[2];
				} else {
					afc.timing_offset = data[1];
					afc.carrier_offset = data[2];
				}
				ret =
				    libertas_prepare_and_send_command(priv,
							  cmd_802_11_set_afc,
							  0,
							  cmd_option_waitforrsp,
							  0, (void *)&afc);
			}
			break;
		case WLAN_AUTO_FREQ_GET:
			{
				int data[3];
				struct cmd_ds_802_11_afc afc;
				memset(&afc, 0, sizeof(afc));
				ret =
				    libertas_prepare_and_send_command(priv,
							  cmd_802_11_get_afc,
							  0,
							  cmd_option_waitforrsp,
							  0, (void *)&afc);
				data[0] = afc.afc_auto;
				data[1] = afc.timing_offset;
				data[2] = afc.carrier_offset;
				if (copy_to_user
				    (wrq->u.data.pointer, data,
				     sizeof(int) * 3)) {
					lbs_pr_debug(1, "Copy to user failed\n");
					return -EFAULT;
				}

				wrq->u.data.length = 3;
			}
			break;
		case WLAN_SCANPROBES:
			{
				int data;
				if (wrq->u.data.length > 0) {
					if (copy_from_user
					    (&data, wrq->u.data.pointer,
					     sizeof(int))) {
						lbs_pr_debug(1,
						       "Copy from user failed\n");
						return -EFAULT;
					}

					adapter->scanprobes = data;
				} else {
					data = adapter->scanprobes;
					if (copy_to_user
					    (wrq->u.data.pointer, &data,
					     sizeof(int))) {
						lbs_pr_debug(1,
						       "Copy to user failed\n");
						return -EFAULT;
					}
				}
				wrq->u.data.length = 1;
			}
			break;
		case WLAN_LED_GPIO_CTRL:
			{
				int i;
				int data[16];

				struct cmd_ds_802_11_led_ctrl ctrl;
				struct mrvlietypes_ledgpio *gpio =
				    (struct mrvlietypes_ledgpio *) ctrl.data;

				memset(&ctrl, 0, sizeof(ctrl));
				if (wrq->u.data.length > MAX_LEDS * 2)
					return -ENOTSUPP;
				if ((wrq->u.data.length % 2) != 0)
					return -ENOTSUPP;
				if (wrq->u.data.length == 0) {
					ctrl.action =
					    cpu_to_le16
					    (cmd_act_get);
				} else {
					if (copy_from_user
					    (data, wrq->u.data.pointer,
					     sizeof(int) *
					     wrq->u.data.length)) {
						lbs_pr_debug(1,
						       "Copy from user failed\n");
						return -EFAULT;
					}

					ctrl.action =
					    cpu_to_le16
					    (cmd_act_set);
					ctrl.numled = cpu_to_le16(0);
					gpio->header.type =
					    cpu_to_le16(TLV_TYPE_LED_GPIO);
					gpio->header.len = wrq->u.data.length;
					for (i = 0; i < wrq->u.data.length;
					     i += 2) {
						gpio->ledpin[i / 2].led =
						    data[i];
						gpio->ledpin[i / 2].pin =
						    data[i + 1];
					}
				}
				ret =
				    libertas_prepare_and_send_command(priv,
							  cmd_802_11_led_gpio_ctrl,
							  0,
							  cmd_option_waitforrsp,
							  0, (void *)&ctrl);
				for (i = 0; i < gpio->header.len; i += 2) {
					data[i] = gpio->ledpin[i / 2].led;
					data[i + 1] = gpio->ledpin[i / 2].pin;
				}
				if (copy_to_user(wrq->u.data.pointer, data,
						 sizeof(int) *
						 gpio->header.len)) {
					lbs_pr_debug(1, "Copy to user failed\n");
					return -EFAULT;
				}

				wrq->u.data.length = gpio->header.len;
			}
			break;
		case WLAN_ADAPT_RATESET:
			ret = wlan_adapt_rateset(priv, wrq);
			break;
		case WLAN_INACTIVITY_TIMEOUT:
			ret = wlan_inactivity_timeout(priv, wrq);
			break;
		case WLANSNR:
			ret = wlan_get_snr(priv, wrq);
			break;
		case WLAN_GET_RXINFO:
			ret = wlan_get_rxinfo(priv, wrq);
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}
	LEAVE();
	return ret;
}


