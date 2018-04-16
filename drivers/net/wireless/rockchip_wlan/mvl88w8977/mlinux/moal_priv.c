/** @file  moal_priv.c
  *
  * @brief This file contains standard ioctl functions
  *
  * Copyright (C) 2008-2017, Marvell International Ltd.
  *
  * This software file (the "File") is distributed by Marvell International
  * Ltd. under the terms of the GNU General Public License Version 2, June 1991
  * (the "License").  You may use, redistribute and/or modify this File in
  * accordance with the terms and conditions of the License, a copy of which
  * is available by writing to the Free Software Foundation, Inc.,
  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
  * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
  *
  * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
  * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
  * this warranty disclaimer.
  *
  */

/************************************************************************
Change log:
    10/30/2008: initial version
************************************************************************/

#include	"moal_main.h"
#include	"moal_sdio.h"

#include    "moal_eth_ioctl.h"

/********************************************************
			Local Variables
********************************************************/
/** Bands supported in Infra mode */
static t_u8 SupportedInfraBand[] = {
	BAND_B,
	BAND_B | BAND_G, BAND_G,
	BAND_GN, BAND_B | BAND_G | BAND_GN, BAND_G | BAND_GN,
	BAND_A, BAND_B | BAND_A, BAND_B | BAND_G | BAND_A, BAND_G | BAND_A,
	BAND_A | BAND_B | BAND_G | BAND_AN | BAND_GN,
		BAND_A | BAND_G | BAND_AN | BAND_GN, BAND_A | BAND_AN,
};

/** Bands supported in Ad-Hoc mode */
static t_u8 SupportedAdhocBand[] = {
	BAND_B, BAND_B | BAND_G, BAND_G,
	BAND_GN, BAND_B | BAND_G | BAND_GN, BAND_G | BAND_GN,
	BAND_A,
	BAND_AN, BAND_A | BAND_AN,
};

/********************************************************
			Global Variables
********************************************************/

extern int cfg80211_wext;

/********************************************************
			Local Functions
********************************************************/

/**
 * @brief Associated to a specific indexed entry in the ScanTable
 *
 * @param priv         A pointer to moal_private structure
 * @param wrq          A pointer to iwreq structure
 *
 * @return             0 --success, otherwise fail
 */
static int
woal_associate_ssid_bssid(moal_private *priv, struct iwreq *wrq)
{
	mlan_ssid_bssid ssid_bssid;
#ifdef REASSOCIATION
	mlan_bss_info bss_info;
#endif
	char buf[64];
	t_u8 buflen;
	t_u8 mac_idx;
	t_u8 i;

	ENTER();

	memset(&ssid_bssid, 0, sizeof(ssid_bssid));
	mac_idx = 0;
	buflen = MIN(wrq->u.data.length, (sizeof(buf) - 1));
	memset(buf, 0, sizeof(buf));

	if (buflen < (3 * ETH_ALEN) + 2) {
		PRINTM(MERROR,
		       "Associate: Insufficient length in IOCTL input\n");

		/* buffer should be at least 3 characters per BSSID octet "00:"
		 **   plus a space separater and at least 1 char in the SSID
		 */
		LEAVE();
		return -EINVAL;
	}

	if (copy_from_user(buf, wrq->u.data.pointer, buflen) != 0) {
		/* copy_from_user failed  */
		PRINTM(MERROR, "Associate: copy from user failed\n");
		LEAVE();
		return -EINVAL;
	}

	for (i = 0; (i < buflen) && (buf[i] == ' '); i++) {
		/* Skip white space */
	}

	/* Copy/Convert the BSSID */
	for (; (i < buflen) && (mac_idx < ETH_ALEN) && (buf[i] != ' '); i++) {
		if (buf[i] == ':') {
			mac_idx++;
		} else {
			if (mac_idx < ETH_ALEN)
				ssid_bssid.bssid[mac_idx] =
					(t_u8)woal_atox(buf + i);

			while ((i < buflen) && (isxdigit(buf[i + 1]))) {
				/* Skip entire hex value */
				i++;
			}
		}
	}

	/* Skip one space between the BSSID and start of the SSID */
	i++;

	/* Copy the SSID */
	ssid_bssid.ssid.ssid_len = buflen - i - 1;
	memcpy(ssid_bssid.ssid.ssid, buf + i, sizeof(ssid_bssid.ssid.ssid));

	PRINTM(MCMND, "iwpriv assoc: AP=[" MACSTR "], ssid(%d)=[%s]\n",
	       MAC2STR(ssid_bssid.bssid),
	       (int)ssid_bssid.ssid.ssid_len, ssid_bssid.ssid.ssid);

	if (MLAN_STATUS_SUCCESS != woal_bss_start(priv,
						  MOAL_IOCTL_WAIT,
						  &ssid_bssid)) {
		LEAVE();
		return -EFAULT;
	}
#ifdef REASSOCIATION
	memset(&bss_info, 0x00, sizeof(bss_info));
	if (MLAN_STATUS_SUCCESS == woal_get_bss_info(priv,
						     MOAL_IOCTL_WAIT,
						     &bss_info)) {
		memcpy(&priv->prev_ssid_bssid.ssid,
		       &bss_info.ssid, sizeof(mlan_802_11_ssid));
		memcpy(&priv->prev_ssid_bssid.bssid,
		       &bss_info.bssid, MLAN_MAC_ADDR_LENGTH);
	}
#endif /* REASSOCIATION */

	LEAVE();
	return 0;
}

/**
 *  @brief Copy Rates
 *
 *  @param dest    A pointer to destination buffer
 *  @param pos     The position for copy
 *  @param src     A pointer to source buffer
 *  @param len     Length of the source buffer
 *
 *  @return        Number of rates copied
 */
static inline int
woal_copy_rates(t_u8 *dest, int pos, t_u8 *src, int len)
{
	int i;

	for (i = 0; i < len && src[i]; i++, pos++) {
		if (pos >= MLAN_SUPPORTED_RATES)
			break;
		dest[pos] = src[i];
	}
	return pos;
}

/**
 *  @brief Performs warm reset
 *
 *  @param priv         A pointer to moal_private structure
 *
 *  @return             0/MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static int
woal_warm_reset(moal_private *priv)
{
	int ret = 0;
	int intf_num;
	moal_handle *handle = priv->phandle;
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
#if defined(STA_WEXT) || defined(UAP_WEXT)
	t_u8 bss_role = MLAN_BSS_ROLE_STA;
#endif
#endif
#endif /* WIFI_DIRECT_SUPPORT && V14_FEATURE */
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	woal_cancel_cac_block(priv);

	/* Reset all interfaces */
	ret = woal_reset_intf(priv, MOAL_IOCTL_WAIT, MTRUE);

	/* Initialize private structures */
	for (intf_num = 0; intf_num < handle->priv_num; intf_num++) {
		woal_init_priv(handle->priv[intf_num], MOAL_IOCTL_WAIT);
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
#if defined(STA_WEXT) || defined(UAP_WEXT)
		if ((handle->priv[intf_num]->bss_type ==
		     MLAN_BSS_TYPE_WIFIDIRECT) &&
		    (GET_BSS_ROLE(handle->priv[intf_num]) ==
		     MLAN_BSS_ROLE_UAP)) {
			if (MLAN_STATUS_SUCCESS !=
			    woal_bss_role_cfg(handle->priv[intf_num],
					      MLAN_ACT_SET, MOAL_IOCTL_WAIT,
					      &bss_role)) {
				ret = -EFAULT;
				goto done;
			}
		}
#endif /* STA_WEXT || UAP_WEXT */
#endif /* STA_SUPPORT && UAP_SUPPORT */
#endif /* WIFI_DIRECT_SUPPORT && V14_FEATURE */
	}

	/* Restart the firmware */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req) {
		misc = (mlan_ds_misc_cfg *)req->pbuf;
		misc->sub_command = MLAN_OID_MISC_WARM_RESET;
		req->req_id = MLAN_IOCTL_MISC_CFG;
		req->action = MLAN_ACT_SET;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			if (status != MLAN_STATUS_PENDING)
				kfree(req);
			goto done;
		}
		kfree(req);
	}

	/* Enable interfaces */
	for (intf_num = 0; intf_num < handle->priv_num; intf_num++) {
		netif_device_attach(handle->priv[intf_num]->netdev);
		woal_start_queue(handle->priv[intf_num]->netdev);
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief Get signal
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_get_signal(moal_private *priv, struct iwreq *wrq)
{
/** Input data size */
#define IN_DATA_SIZE	2
/** Output data size */
#define OUT_DATA_SIZE	12
	int ret = 0;
	int in_data[IN_DATA_SIZE];
	int out_data[OUT_DATA_SIZE];
	mlan_ds_get_signal signal;
	int data_length = 0;
	int buflen = 0;

	ENTER();

	memset(in_data, 0, sizeof(in_data));
	memset(out_data, 0, sizeof(out_data));
	buflen = MIN(wrq->u.data.length, IN_DATA_SIZE);

	if (priv->media_connected == MFALSE) {
		PRINTM(MERROR, "Can not get RSSI in disconnected state\n");
		ret = -ENOTSUPP;
		goto done;
	}

	if (wrq->u.data.length) {
		if (sizeof(int) * wrq->u.data.length > sizeof(in_data)) {
			PRINTM(MERROR, "Too many arguments\n");
			ret = -EINVAL;
			goto done;
		}
		if (copy_from_user
		    (in_data, wrq->u.data.pointer, sizeof(int) * buflen)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}

	switch (wrq->u.data.length) {
	case 0:		/* No checking, get everything */
		break;
	case 2:		/* Check subtype range */
		if (in_data[1] < 1 || in_data[1] > 4) {
			ret = -EINVAL;
			goto done;
		}
		/* Fall through */
	case 1:		/* Check type range */
		if (in_data[0] < 1 || in_data[0] > 3) {
			ret = -EINVAL;
			goto done;
		}
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	memset(&signal, 0, sizeof(mlan_ds_get_signal));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_signal_info(priv, MOAL_IOCTL_WAIT, &signal)) {
		ret = -EFAULT;
		goto done;
	}
	PRINTM(MINFO, "RSSI Beacon Last   : %d\n", (int)signal.bcn_rssi_last);
	PRINTM(MINFO, "RSSI Beacon Average: %d\n", (int)signal.bcn_rssi_avg);
	PRINTM(MINFO, "RSSI Data Last     : %d\n", (int)signal.data_rssi_last);
	PRINTM(MINFO, "RSSI Data Average  : %d\n", (int)signal.data_rssi_avg);
	PRINTM(MINFO, "SNR Beacon Last    : %d\n", (int)signal.bcn_snr_last);
	PRINTM(MINFO, "SNR Beacon Average : %d\n", (int)signal.bcn_snr_avg);
	PRINTM(MINFO, "SNR Data Last      : %d\n", (int)signal.data_snr_last);
	PRINTM(MINFO, "SNR Data Average   : %d\n", (int)signal.data_snr_avg);
	PRINTM(MINFO, "NF Beacon Last     : %d\n", (int)signal.bcn_nf_last);
	PRINTM(MINFO, "NF Beacon Average  : %d\n", (int)signal.bcn_nf_avg);
	PRINTM(MINFO, "NF Data Last       : %d\n", (int)signal.data_nf_last);
	PRINTM(MINFO, "NF Data Average    : %d\n", (int)signal.data_nf_avg);

	/* Check type */
	switch (in_data[0]) {
	case 0:		/* Send everything */
		out_data[data_length++] = signal.bcn_rssi_last;
		out_data[data_length++] = signal.bcn_rssi_avg;
		out_data[data_length++] = signal.data_rssi_last;
		out_data[data_length++] = signal.data_rssi_avg;
		out_data[data_length++] = signal.bcn_snr_last;
		out_data[data_length++] = signal.bcn_snr_avg;
		out_data[data_length++] = signal.data_snr_last;
		out_data[data_length++] = signal.data_snr_avg;
		out_data[data_length++] = signal.bcn_nf_last;
		out_data[data_length++] = signal.bcn_nf_avg;
		out_data[data_length++] = signal.data_nf_last;
		out_data[data_length++] = signal.data_nf_avg;
		break;
	case 1:		/* RSSI */
		/* Check subtype */
		switch (in_data[1]) {
		case 0:	/* Everything */
			out_data[data_length++] = signal.bcn_rssi_last;
			out_data[data_length++] = signal.bcn_rssi_avg;
			out_data[data_length++] = signal.data_rssi_last;
			out_data[data_length++] = signal.data_rssi_avg;
			break;
		case 1:	/* bcn last */
			out_data[data_length++] = signal.bcn_rssi_last;
			break;
		case 2:	/* bcn avg */
			out_data[data_length++] = signal.bcn_rssi_avg;
			break;
		case 3:	/* data last */
			out_data[data_length++] = signal.data_rssi_last;
			break;
		case 4:	/* data avg */
			out_data[data_length++] = signal.data_rssi_avg;
			break;
		default:
			break;
		}
		break;
	case 2:		/* SNR */
		/* Check subtype */
		switch (in_data[1]) {
		case 0:	/* Everything */
			out_data[data_length++] = signal.bcn_snr_last;
			out_data[data_length++] = signal.bcn_snr_avg;
			out_data[data_length++] = signal.data_snr_last;
			out_data[data_length++] = signal.data_snr_avg;
			break;
		case 1:	/* bcn last */
			out_data[data_length++] = signal.bcn_snr_last;
			break;
		case 2:	/* bcn avg */
			out_data[data_length++] = signal.bcn_snr_avg;
			break;
		case 3:	/* data last */
			out_data[data_length++] = signal.data_snr_last;
			break;
		case 4:	/* data avg */
			out_data[data_length++] = signal.data_snr_avg;
			break;
		default:
			break;
		}
		break;
	case 3:		/* NF */
		/* Check subtype */
		switch (in_data[1]) {
		case 0:	/* Everything */
			out_data[data_length++] = signal.bcn_nf_last;
			out_data[data_length++] = signal.bcn_nf_avg;
			out_data[data_length++] = signal.data_nf_last;
			out_data[data_length++] = signal.data_nf_avg;
			break;
		case 1:	/* bcn last */
			out_data[data_length++] = signal.bcn_nf_last;
			break;
		case 2:	/* bcn avg */
			out_data[data_length++] = signal.bcn_nf_avg;
			break;
		case 3:	/* data last */
			out_data[data_length++] = signal.data_nf_last;
			break;
		case 4:	/* data avg */
			out_data[data_length++] = signal.data_nf_avg;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	wrq->u.data.length = data_length;
	if (copy_to_user(wrq->u.data.pointer, out_data,
			 wrq->u.data.length * sizeof(out_data[0]))) {
		PRINTM(MERROR, "Copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Get/Set DeepSleep mode
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wreq	    A pointer to iwreq structure
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_deep_sleep_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	int user_data_len;
	t_u32 deep_sleep = DEEP_SLEEP_OFF;
	t_u32 data[2];
	int copy_len;
	t_u16 idletime = DEEP_SLEEP_IDLE_TIME;

	ENTER();

	user_data_len = wrq->u.data.length;
	copy_len = MIN(sizeof(data), sizeof(int) * user_data_len);
	if (user_data_len == 1 || user_data_len == 2) {
		if (copy_from_user(&data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			LEAVE();
			return -EFAULT;
		}
		deep_sleep = data[0];
		if (deep_sleep == DEEP_SLEEP_OFF) {
			PRINTM(MINFO, "Exit Deep Sleep Mode\n");
			ret = woal_set_deep_sleep(priv, MOAL_IOCTL_WAIT, MFALSE,
						  0);
			if (ret != MLAN_STATUS_SUCCESS) {
				LEAVE();
				return -EINVAL;
			}
		} else if (deep_sleep == DEEP_SLEEP_ON) {
			PRINTM(MINFO, "Enter Deep Sleep Mode\n");
			if (user_data_len == 2)
				idletime = data[1];
			else
				idletime = 0;
			ret = woal_set_deep_sleep(priv, MOAL_IOCTL_WAIT, MTRUE,
						  idletime);
			if (ret != MLAN_STATUS_SUCCESS) {
				LEAVE();
				return -EINVAL;
			}
		} else {
			PRINTM(MERROR, "Unknown option = %u\n", deep_sleep);
			LEAVE();
			return -EINVAL;
		}
	} else if (user_data_len > 2) {
		PRINTM(MERROR, "Invalid number of arguments %d\n",
		       user_data_len);
		LEAVE();
		return -EINVAL;
	} else {		/* Display Deep Sleep settings */
		PRINTM(MINFO, "Get Deep Sleep Mode\n");
		if (MLAN_STATUS_SUCCESS != woal_get_deep_sleep(priv, data)) {
			LEAVE();
			return -EFAULT;
		}
		if (data[0] == 0)
			wrq->u.data.length = 1;
		else
			wrq->u.data.length = 2;
	}

	/* Copy the Deep Sleep setting to user */
	if (copy_to_user
	    (wrq->u.data.pointer, data, wrq->u.data.length * sizeof(int))) {
		PRINTM(MERROR, "Copy to user failed\n");
		LEAVE();
		return -EINVAL;
	}

	LEAVE();
	return 0;
}

/**
 *  @brief Set/Get Usr 11n configuration request
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_11n_htcap_cfg(moal_private *priv, struct iwreq *wrq)
{
	int data[2], copy_len;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11n_cfg *cfg_11n = NULL;
	int ret = 0;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (data_length > 2) {
		PRINTM(MERROR, "Invalid number of arguments\n");
		ret = -EINVAL;
		goto done;
	}

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	cfg_11n = (mlan_ds_11n_cfg *)req->pbuf;
	cfg_11n->sub_command = MLAN_OID_11N_HTCAP_CFG;
	req->req_id = MLAN_IOCTL_11N_CFG;

	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	if (data_length == 0) {
		/* Get 11n tx parameters from MLAN */
		req->action = MLAN_ACT_GET;
		cfg_11n->param.htcap_cfg.misc_cfg = BAND_SELECT_BG;
	} else {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		cfg_11n->param.htcap_cfg.htcap = data[0];
		PRINTM(MINFO, "SET: htcapinfo:0x%x\n", data[0]);
		cfg_11n->param.htcap_cfg.misc_cfg = BAND_SELECT_BOTH;
		if (data_length == 2) {
			if (data[1] != BAND_SELECT_BG &&
			    data[1] != BAND_SELECT_A &&
			    data[1] != BAND_SELECT_BOTH) {
				PRINTM(MERROR, "Invalid band selection\n");
				ret = -EINVAL;
				goto done;
			}
			cfg_11n->param.htcap_cfg.misc_cfg = data[1];
			PRINTM(MINFO, "SET: htcapinfo band:0x%x\n", data[1]);
		}
		/* Update 11n tx parameters in MLAN */
		req->action = MLAN_ACT_SET;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	data[0] = cfg_11n->param.htcap_cfg.htcap;

	if (req->action == MLAN_ACT_GET) {
		data_length = 1;
		cfg_11n->param.htcap_cfg.htcap = 0;
		cfg_11n->param.htcap_cfg.misc_cfg = BAND_SELECT_A;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}
		if (cfg_11n->param.htcap_cfg.htcap != data[0]) {
			data_length = 2;
			data[1] = cfg_11n->param.htcap_cfg.htcap;
			PRINTM(MINFO, "GET: htcapinfo for 2.4GHz:0x%x\n",
			       data[0]);
			PRINTM(MINFO, "GET: htcapinfo for 5GHz:0x%x\n",
			       data[1]);
		} else
			PRINTM(MINFO, "GET: htcapinfo:0x%x\n", data[0]);
	}

	if (copy_to_user(wrq->u.data.pointer, data, sizeof(data))) {
		PRINTM(MERROR, "Copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}

	wrq->u.data.length = data_length;

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Enable/Disable amsdu_aggr_ctrl
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_11n_amsdu_aggr_ctrl(moal_private *priv, struct iwreq *wrq)
{
	int data[2], copy_len;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11n_cfg *cfg_11n = NULL;
	int ret = 0;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if ((data_length != 0) && (data_length != 1)) {
		PRINTM(MERROR, "Invalid number of arguments\n");
		ret = -EINVAL;
		goto done;
	}
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	cfg_11n = (mlan_ds_11n_cfg *)req->pbuf;
	cfg_11n->sub_command = MLAN_OID_11N_CFG_AMSDU_AGGR_CTRL;
	req->req_id = MLAN_IOCTL_11N_CFG;

	if (data_length == 0) {
		/* Get 11n tx parameters from MLAN */
		req->action = MLAN_ACT_GET;
	} else if (data_length == 1) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		cfg_11n->param.amsdu_aggr_ctrl.enable = data[0];
		/* Update 11n tx parameters in MLAN */
		req->action = MLAN_ACT_SET;
	}
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	data[0] = cfg_11n->param.amsdu_aggr_ctrl.enable;
	data[1] = cfg_11n->param.amsdu_aggr_ctrl.curr_buf_size;

	if (copy_to_user(wrq->u.data.pointer, data, sizeof(data))) {
		PRINTM(MERROR, "Copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 2;
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get 11n configuration request
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_11n_tx_cfg(moal_private *priv, struct iwreq *wrq)
{
	int data[2], copy_len;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11n_cfg *cfg_11n = NULL;
	int ret = 0;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (data_length > 2) {
		PRINTM(MERROR, "Invalid number of arguments\n");
		ret = -EINVAL;
		goto done;
	}
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	cfg_11n = (mlan_ds_11n_cfg *)req->pbuf;
	cfg_11n->sub_command = MLAN_OID_11N_CFG_TX;
	req->req_id = MLAN_IOCTL_11N_CFG;

	if (data_length == 0) {
		/* Get 11n tx parameters from MLAN */
		req->action = MLAN_ACT_GET;
		cfg_11n->param.tx_cfg.misc_cfg = BAND_SELECT_BG;
	} else {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		cfg_11n->param.tx_cfg.httxcap = data[0];
		PRINTM(MINFO, "SET: httxcap:0x%x\n", data[0]);
		cfg_11n->param.tx_cfg.misc_cfg = BAND_SELECT_BOTH;
		if (data_length == 2) {
			if (data[1] != BAND_SELECT_BG &&
			    data[1] != BAND_SELECT_A &&
			    data[1] != BAND_SELECT_BOTH) {
				PRINTM(MERROR, "Invalid band selection\n");
				ret = -EINVAL;
				goto done;
			}
			cfg_11n->param.tx_cfg.misc_cfg = data[1];
			PRINTM(MINFO, "SET: httxcap band:0x%x\n", data[1]);
		}
		/* Update 11n tx parameters in MLAN */
		req->action = MLAN_ACT_SET;
	}
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	data[0] = cfg_11n->param.tx_cfg.httxcap;

	if (req->action == MLAN_ACT_GET) {
		data_length = 1;
		cfg_11n->param.tx_cfg.httxcap = 0;
		cfg_11n->param.tx_cfg.misc_cfg = BAND_SELECT_A;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}
		if (cfg_11n->param.tx_cfg.httxcap != data[0]) {
			data_length = 2;
			data[1] = cfg_11n->param.tx_cfg.httxcap;
			PRINTM(MINFO, "GET: httxcap for 2.4GHz:0x%x\n",
			       data[0]);
			PRINTM(MINFO, "GET: httxcap for 5GHz:0x%x\n", data[1]);
		} else
			PRINTM(MINFO, "GET: httxcap:0x%x\n", data[0]);
	}

	if (copy_to_user(wrq->u.data.pointer, data, sizeof(data))) {
		PRINTM(MERROR, "Copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = data_length;

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Enable/Disable TX Aggregation
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_11n_prio_tbl(moal_private *priv, struct iwreq *wrq)
{
	int data[MAX_NUM_TID * 2], i, j, copy_len;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11n_cfg *cfg_11n = NULL;
	int ret = 0;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if ((wrq->u.data.pointer == NULL)) {
		LEAVE();
		return -EINVAL;
	}
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
	if (req == NULL) {
		LEAVE();
		return -ENOMEM;
	}
	cfg_11n = (mlan_ds_11n_cfg *)req->pbuf;
	cfg_11n->sub_command = MLAN_OID_11N_CFG_AGGR_PRIO_TBL;
	req->req_id = MLAN_IOCTL_11N_CFG;

	if (data_length == 0) {
		/* Get aggr priority table from MLAN */
		req->action = MLAN_ACT_GET;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto error;
		}
		wrq->u.data.length = MAX_NUM_TID * 2;
		for (i = 0, j = 0; i < (wrq->u.data.length); i = i + 2, ++j) {
			data[i] = cfg_11n->param.aggr_prio_tbl.ampdu[j];
			data[i + 1] = cfg_11n->param.aggr_prio_tbl.amsdu[j];
		}

		if (copy_to_user(wrq->u.data.pointer, data,
				 sizeof(int) * wrq->u.data.length)) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto error;
		}
	} else if (data_length == 16) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto error;
		}
		for (i = 0, j = 0; i < (data_length); i = i + 2, ++j) {
			if ((data[i] > 7 && data[i] != 0xff) ||
			    (data[i + 1] > 7 && data[i + 1] != 0xff)) {
				PRINTM(MERROR,
				       "Invalid priority, valid value 0-7 or 0xff.\n");
				ret = -EFAULT;
				goto error;
			}
			cfg_11n->param.aggr_prio_tbl.ampdu[j] = data[i];
			cfg_11n->param.aggr_prio_tbl.amsdu[j] = data[i + 1];
		}

		/* Update aggr priority table in MLAN */
		req->action = MLAN_ACT_SET;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto error;
		}
	} else {
		PRINTM(MERROR, "Invalid number of arguments\n");
		ret = -EINVAL;
		goto error;
	}

error:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get add BA Reject parameters
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_addba_reject(moal_private *priv, struct iwreq *wrq)
{
	int data[MAX_NUM_TID], ret = 0, i, copy_len;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11n_cfg *cfg_11n = NULL;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();

	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
	if (req == NULL) {
		LEAVE();
		return -ENOMEM;
	}
	cfg_11n = (mlan_ds_11n_cfg *)req->pbuf;
	cfg_11n->sub_command = MLAN_OID_11N_CFG_ADDBA_REJECT;
	req->req_id = MLAN_IOCTL_11N_CFG;

	if (data_length == 0) {
		PRINTM(MERROR, "Addba reject moal\n");
		/* Get aggr priority table from MLAN */
		req->action = MLAN_ACT_GET;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto error;
		}

		wrq->u.data.length = MAX_NUM_TID;
		for (i = 0; i < (wrq->u.data.length); ++i)
			data[i] = cfg_11n->param.addba_reject[i];

		if (copy_to_user(wrq->u.data.pointer, data,
				 sizeof(int) * wrq->u.data.length)) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto error;
		}
	} else if (data_length == 8) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto error;
		}
		for (i = 0; i < (data_length); ++i) {
			if (data[i] != 0 && data[i] != 1) {
				PRINTM(MERROR,
				       "addba reject only takes argument as 0 or 1\n");
				ret = -EFAULT;
				goto error;
			}
			cfg_11n->param.addba_reject[i] = data[i];
		}

		/* Update aggr priority table in MLAN */
		req->action = MLAN_ACT_SET;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto error;
		}
	} else {
		PRINTM(MERROR, "Invalid number of arguments\n");
		ret = -EINVAL;
		goto error;
	}
error:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get add BA parameters
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_addba_para_updt(moal_private *priv, struct iwreq *wrq)
{
	int data[5], ret = 0, copy_len;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11n_cfg *cfg_11n = NULL;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
	if (req == NULL) {
		LEAVE();
		return -ENOMEM;
	}
	cfg_11n = (mlan_ds_11n_cfg *)req->pbuf;
	cfg_11n->sub_command = MLAN_OID_11N_CFG_ADDBA_PARAM;
	req->req_id = MLAN_IOCTL_11N_CFG;

	if (data_length == 0) {
		/* Get Add BA parameters from MLAN */
		req->action = MLAN_ACT_GET;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto error;
		}
		data[0] = cfg_11n->param.addba_param.timeout;
		data[1] = cfg_11n->param.addba_param.txwinsize;
		data[2] = cfg_11n->param.addba_param.rxwinsize;
		data[3] = cfg_11n->param.addba_param.txamsdu;
		data[4] = cfg_11n->param.addba_param.rxamsdu;
		PRINTM(MINFO,
		       "GET: timeout:%d txwinsize:%d rxwinsize:%d txamsdu=%d, rxamsdu=%d\n",
		       data[0], data[1], data[2], data[3], data[4]);
		wrq->u.data.length = 5;
		if (copy_to_user(wrq->u.data.pointer, data,
				 wrq->u.data.length * sizeof(int))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto error;
		}
	} else if (data_length == 5) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto error;
		}
		if (data[0] < 0 || data[0] > MLAN_DEFAULT_BLOCK_ACK_TIMEOUT) {
			PRINTM(MERROR, "Incorrect addba timeout value.\n");
			ret = -EFAULT;
			goto error;
		}
		if (data[1] <= 0 || data[1] > MLAN_AMPDU_MAX_TXWINSIZE ||
		    data[2] <= 0 || data[2] > MLAN_AMPDU_MAX_RXWINSIZE) {
			PRINTM(MERROR, "Incorrect Tx/Rx window size.\n");
			ret = -EFAULT;
			goto error;
		}
		cfg_11n->param.addba_param.timeout = data[0];
		cfg_11n->param.addba_param.txwinsize = data[1];
		cfg_11n->param.addba_param.rxwinsize = data[2];
		if (data[3] < 0 || data[3] > 1 || data[4] < 0 || data[4] > 1) {
			PRINTM(MERROR, "Incorrect Tx/Rx amsdu.\n");
			ret = -EFAULT;
			goto error;
		}
		cfg_11n->param.addba_param.txamsdu = data[3];
		cfg_11n->param.addba_param.rxamsdu = data[4];
		PRINTM(MINFO,
		       "SET: timeout:%d txwinsize:%d rxwinsize:%d txamsdu=%d rxamsdu=%d\n",
		       data[0], data[1], data[2], data[3], data[4]);
		/* Update Add BA parameters in MLAN */
		req->action = MLAN_ACT_SET;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = MLAN_STATUS_FAILURE;
			goto error;
		}
	} else {
		PRINTM(MERROR, "Invalid number of arguments\n");
		ret = -EINVAL;
		goto error;
	}

error:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Transmit buffer size
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_txbuf_cfg(moal_private *priv, struct iwreq *wrq)
{
	int buf_size;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11n_cfg *cfg_11n = NULL;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	cfg_11n = (mlan_ds_11n_cfg *)req->pbuf;
	cfg_11n->sub_command = MLAN_OID_11N_CFG_MAX_TX_BUF_SIZE;
	req->req_id = MLAN_IOCTL_11N_CFG;

	if (wrq->u.data.length == 0) {
		/* Get Tx buffer size from MLAN */
		req->action = MLAN_ACT_GET;
	} else {
		ret = -EINVAL;
		PRINTM(MERROR,
		       "Don't support set Tx buffer size after driver loaded!\n");
		goto done;
	}
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	buf_size = cfg_11n->param.tx_buf_size;
	if (copy_to_user(wrq->u.data.pointer, &buf_size, sizeof(buf_size))) {
		PRINTM(MERROR, "Copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 1;
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Host Sleep configuration
 *
 *  @param priv             A pointer to moal_private structure
 *  @param wrq              A pointer to iwreq structure
 *  @param invoke_hostcmd   MTRUE --invoke HostCmd, otherwise MFALSE
 *
 *  @return                 0 --success, otherwise fail
 */
static int
woal_hs_cfg(moal_private *priv, struct iwreq *wrq, BOOLEAN invoke_hostcmd)
{
	int data[3], copy_len;
	int ret = 0;
	mlan_ds_hs_cfg hscfg;
	t_u16 action;
	mlan_bss_info bss_info;
	int data_length = wrq->u.data.length;

	ENTER();

	memset(data, 0, sizeof(data));
	memset(&hscfg, 0, sizeof(mlan_ds_hs_cfg));
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	if (data_length == 0) {
		action = MLAN_ACT_GET;
	} else {
		action = MLAN_ACT_SET;
		if (data_length >= 1 && data_length <= 3) {
			if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
				PRINTM(MERROR, "Copy from user failed\n");
				ret = -EFAULT;
				goto done;
			}
		} else {
			PRINTM(MERROR, "Invalid arguments\n");
			ret = -EINVAL;
			goto done;
		}
	}

	/* HS config is blocked if HS is already activated */
	if (data_length &&
	    (data[0] != HOST_SLEEP_CFG_CANCEL || invoke_hostcmd == MFALSE)) {
		memset(&bss_info, 0, sizeof(bss_info));
		woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
		if (bss_info.is_hs_configured) {
			PRINTM(MERROR, "HS already configured\n");
			ret = -EFAULT;
			goto done;
		}
	}

	/* Do a GET first if some arguments are not provided */
	if (data_length >= 1 && data_length < 3) {
		woal_set_get_hs_params(priv, MLAN_ACT_GET, MOAL_IOCTL_WAIT,
				       &hscfg);
	}

	if (data_length)
		hscfg.conditions = data[0];
	if (data_length >= 2)
		hscfg.gpio = data[1];
	if (data_length == 3)
		hscfg.gap = data[2];

	if ((invoke_hostcmd == MTRUE) && (action == MLAN_ACT_SET)) {
		/* Need to issue an extra IOCTL first to set up parameters */
		hscfg.is_invoke_hostcmd = MFALSE;
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_get_hs_params(priv, MLAN_ACT_SET, MOAL_IOCTL_WAIT,
					   &hscfg)) {
			ret = -EFAULT;
			goto done;
		}
	}
	hscfg.is_invoke_hostcmd = invoke_hostcmd;
	if (MLAN_STATUS_SUCCESS !=
	    woal_set_get_hs_params(priv, action, MOAL_IOCTL_WAIT, &hscfg)) {
		ret = -EFAULT;
		goto done;
	}

	if (action == MLAN_ACT_GET) {
		data[0] = hscfg.conditions;
		data[1] = hscfg.gpio;
		data[2] = hscfg.gap;
		wrq->u.data.length = 3;
		if (copy_to_user
		    (wrq->u.data.pointer, data,
		     sizeof(int) * wrq->u.data.length)) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Set Host Sleep parameters
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_hs_setpara(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	int data_length = wrq->u.data.length;

	ENTER();

	if (data_length >= 1 && data_length <= 3) {
		ret = woal_hs_cfg(priv, wrq, MFALSE);
		goto done;
	} else {
		PRINTM(MERROR, "Invalid arguments\n");
		ret = -EINVAL;
		goto done;
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Get/Set inactivity timeout extend
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_inactivity_timeout_ext(moal_private *priv, struct iwreq *wrq)
{
	int data[4], copy_len;
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_pm_cfg *pmcfg = NULL;
	pmlan_ds_inactivity_to inac_to = NULL;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	memset(data, 0, sizeof(data));
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_pm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	pmcfg = (mlan_ds_pm_cfg *)req->pbuf;
	inac_to = &pmcfg->param.inactivity_to;
	pmcfg->sub_command = MLAN_OID_PM_CFG_INACTIVITY_TO;
	req->req_id = MLAN_IOCTL_PM_CFG;

	if ((data_length != 0 && data_length != 3 && data_length != 4) ||
	    sizeof(int) * data_length > sizeof(data)) {
		PRINTM(MERROR, "Invalid number of parameters\n");
		ret = -EINVAL;
		goto done;
	}

	req->action = MLAN_ACT_GET;
	if (data_length) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		req->action = MLAN_ACT_SET;
		inac_to->timeout_unit = data[0];
		inac_to->unicast_timeout = data[1];
		inac_to->mcast_timeout = data[2];
		inac_to->ps_entry_timeout = data[3];
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	/* Copy back current values regardless of GET/SET */
	data[0] = inac_to->timeout_unit;
	data[1] = inac_to->unicast_timeout;
	data[2] = inac_to->mcast_timeout;
	data[3] = inac_to->ps_entry_timeout;

	if (req->action == MLAN_ACT_GET) {
		wrq->u.data.length = 4;
		if (copy_to_user
		    (wrq->u.data.pointer, data,
		     wrq->u.data.length * sizeof(int))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get system clock
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_ecl_sys_clock(moal_private *priv, struct iwreq *wrq)
{
	int data[64], copy_len;
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *cfg = NULL;
	int data_length = wrq->u.data.length;
	int i = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	memset(data, 0, sizeof(data));
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	cfg = (mlan_ds_misc_cfg *)req->pbuf;
	cfg->sub_command = MLAN_OID_MISC_SYS_CLOCK;
	req->req_id = MLAN_IOCTL_MISC_CFG;

	if (!data_length)
		req->action = MLAN_ACT_GET;
	else if (data_length <= MLAN_MAX_CLK_NUM) {
		req->action = MLAN_ACT_SET;
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
	} else {
		PRINTM(MERROR, "Invalid arguments\n");
		ret = -EINVAL;
		goto done;
	}

	if (req->action == MLAN_ACT_GET) {
		/* Get configurable clocks */
		cfg->param.sys_clock.sys_clk_type = MLAN_CLK_CONFIGURABLE;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}

		/* Current system clock */
		data[0] = (int)cfg->param.sys_clock.cur_sys_clk;
		wrq->u.data.length = 1;

		data_length =
			MIN(cfg->param.sys_clock.sys_clk_num, MLAN_MAX_CLK_NUM);

		/* Configurable clocks */
		for (i = 0; i < data_length; i++) {
			data[i + wrq->u.data.length] =
				(int)cfg->param.sys_clock.sys_clk[i];
		}
		wrq->u.data.length += data_length;

		/* Get supported clocks */
		cfg->param.sys_clock.sys_clk_type = MLAN_CLK_SUPPORTED;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}

		data_length =
			MIN(cfg->param.sys_clock.sys_clk_num, MLAN_MAX_CLK_NUM);

		/* Supported clocks */
		for (i = 0; i < data_length; i++) {
			data[i + wrq->u.data.length] =
				(int)cfg->param.sys_clock.sys_clk[i];
		}

		wrq->u.data.length += data_length;

		if (copy_to_user
		    (wrq->u.data.pointer, data,
		     sizeof(int) * wrq->u.data.length)) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
	} else {
		/* Set configurable clocks */
		cfg->param.sys_clock.sys_clk_type = MLAN_CLK_CONFIGURABLE;
		cfg->param.sys_clock.sys_clk_num =
			MIN(MLAN_MAX_CLK_NUM, data_length);
		for (i = 0; i < cfg->param.sys_clock.sys_clk_num; i++)
			cfg->param.sys_clock.sys_clk[i] = (t_u16)data[i];

		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Band and Adhoc-band setting
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_band_cfg(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	unsigned int i;
	int data[4];
	int user_data_len = wrq->u.data.length, copy_len;
	t_u32 infra_band = 0;
	t_u32 adhoc_band = 0;
	t_u32 adhoc_channel = 0;
	t_u32 adhoc_chan_bandwidth = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_radio_cfg *radio_cfg = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (sizeof(int) * user_data_len > sizeof(data)) {
		PRINTM(MERROR, "Too many arguments\n");
		LEAVE();
		return -EINVAL;
	}

	if (user_data_len > 0) {
		if (priv->media_connected == MTRUE) {
			LEAVE();
			return -EOPNOTSUPP;
		}
	}

	copy_len = MIN(sizeof(data), sizeof(int) * user_data_len);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_radio_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	radio_cfg = (mlan_ds_radio_cfg *)req->pbuf;
	radio_cfg->sub_command = MLAN_OID_BAND_CFG;
	req->req_id = MLAN_IOCTL_RADIO_CFG;

	if (wrq->u.data.length == 0) {
		/* Get config_bands, adhoc_start_band and adhoc_channel values from MLAN */
		req->action = MLAN_ACT_GET;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto error;
		}
		/* Infra Band */
		data[0] = radio_cfg->param.band_cfg.config_bands;
		/* Adhoc Band */
		data[1] = radio_cfg->param.band_cfg.adhoc_start_band;
		/* Adhoc Channel */
		data[2] = radio_cfg->param.band_cfg.adhoc_channel;
		wrq->u.data.length = 3;
		if (radio_cfg->param.band_cfg.adhoc_start_band & BAND_GN
		    || radio_cfg->param.band_cfg.adhoc_start_band & BAND_AN) {
			/* secondary bandwidth */
			data[3] =
				radio_cfg->param.band_cfg.adhoc_chan_bandwidth;
			wrq->u.data.length = 4;
		}

		if (copy_to_user
		    (wrq->u.data.pointer, data,
		     sizeof(int) * wrq->u.data.length)) {
			ret = -EFAULT;
			goto error;
		}
	} else {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto error;
		}

		/* To support only <b/bg/bgn/n> */
		infra_band = data[0];
		for (i = 0; i < sizeof(SupportedInfraBand); i++)
			if (infra_band == SupportedInfraBand[i])
				break;
		if (i == sizeof(SupportedInfraBand)) {
			ret = -EINVAL;
			goto error;
		}

		/* Set Adhoc band */
		if (user_data_len >= 2) {
			adhoc_band = data[1];
			for (i = 0; i < sizeof(SupportedAdhocBand); i++)
				if (adhoc_band == SupportedAdhocBand[i])
					break;
			if (i == sizeof(SupportedAdhocBand)) {
				ret = -EINVAL;
				goto error;
			}
		}

		/* Set Adhoc channel */
		if (user_data_len >= 3) {
			adhoc_channel = data[2];
			if (adhoc_channel == 0) {
				/* Check if specified adhoc channel is non-zero */
				ret = -EINVAL;
				goto error;
			}
		}
		if (user_data_len == 4) {
			if (!(adhoc_band & (BAND_GN | BAND_AN))) {
				PRINTM(MERROR,
				       "11n is not enabled for adhoc, can not set HT/VHT channel bandwidth\n");
				ret = -EINVAL;
				goto error;
			}
			adhoc_chan_bandwidth = data[3];
			if ((adhoc_chan_bandwidth != CHANNEL_BW_20MHZ) &&
			    (adhoc_chan_bandwidth != CHANNEL_BW_40MHZ_ABOVE) &&
			    (adhoc_chan_bandwidth != CHANNEL_BW_40MHZ_BELOW)
				) {
				PRINTM(MERROR,
				       "Invalid secondary channel bandwidth, only allowed 0, 1, 3 or 4\n");
				ret = -EINVAL;
				goto error;
			}
		}
		/* Set config_bands and adhoc_start_band values to MLAN */
		req->action = MLAN_ACT_SET;
		radio_cfg->param.band_cfg.config_bands = infra_band;
		radio_cfg->param.band_cfg.adhoc_start_band = adhoc_band;
		radio_cfg->param.band_cfg.adhoc_channel = adhoc_channel;
		radio_cfg->param.band_cfg.adhoc_chan_bandwidth =
			adhoc_chan_bandwidth;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto error;
		}
	}

error:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Read/Write adapter registers value
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_reg_read_write(moal_private *priv, struct iwreq *wrq)
{
	int data[3], copy_len;
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_reg_mem *reg = NULL;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	memset(data, 0, sizeof(data));
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_reg_mem));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	reg = (mlan_ds_reg_mem *)req->pbuf;
	reg->sub_command = MLAN_OID_REG_RW;
	req->req_id = MLAN_IOCTL_REG_MEM;

	if (data_length == 2) {
		req->action = MLAN_ACT_GET;
	} else if (data_length == 3) {
		req->action = MLAN_ACT_SET;
	} else {
		ret = -EINVAL;
		goto done;
	}
	if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
		PRINTM(MERROR, "Copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}
	reg->param.reg_rw.type = (t_u32)data[0];
	reg->param.reg_rw.offset = (t_u32)data[1];
	if (data_length == 3)
		reg->param.reg_rw.value = (t_u32)data[2];

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (req->action == MLAN_ACT_GET) {
		if (copy_to_user
		    (wrq->u.data.pointer, &reg->param.reg_rw.value,
		     sizeof(int))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Read the EEPROM contents of the card
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_read_eeprom(moal_private *priv, struct iwreq *wrq)
{
	int data[2], copy_len;
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_reg_mem *reg = NULL;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	memset(data, 0, sizeof(data));
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_reg_mem));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	reg = (mlan_ds_reg_mem *)req->pbuf;
	reg->sub_command = MLAN_OID_EEPROM_RD;
	req->req_id = MLAN_IOCTL_REG_MEM;

	if (data_length == 2) {
		req->action = MLAN_ACT_GET;
	} else {
		ret = -EINVAL;
		goto done;
	}
	if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
		PRINTM(MERROR, "Copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}

	reg->param.rd_eeprom.offset = (t_u16)data[0];
	reg->param.rd_eeprom.byte_count = (t_u16)data[1];

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (req->action == MLAN_ACT_GET) {
		wrq->u.data.length = reg->param.rd_eeprom.byte_count;
		if (copy_to_user
		    (wrq->u.data.pointer, reg->param.rd_eeprom.value,
		     MIN(wrq->u.data.length, MAX_EEPROM_DATA))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Read/Write device memory value
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_mem_read_write(moal_private *priv, struct iwreq *wrq)
{
	t_u32 data[2];
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_reg_mem *reg_mem = NULL;
	int data_length = wrq->u.data.length, copy_len;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	memset(data, 0, sizeof(data));
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_reg_mem));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	reg_mem = (mlan_ds_reg_mem *)req->pbuf;
	reg_mem->sub_command = MLAN_OID_MEM_RW;
	req->req_id = MLAN_IOCTL_REG_MEM;

	if (data_length == 1) {
		PRINTM(MINFO, "MEM_RW: GET\n");
		req->action = MLAN_ACT_GET;
	} else if (data_length == 2) {
		PRINTM(MINFO, "MEM_RW: SET\n");
		req->action = MLAN_ACT_SET;
	} else {
		ret = -EINVAL;
		goto done;
	}
	if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
		PRINTM(MERROR, "Copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}

	reg_mem->param.mem_rw.addr = (t_u32)data[0];
	if (data_length == 2)
		reg_mem->param.mem_rw.value = (t_u32)data[1];

	PRINTM(MINFO, "MEM_RW: Addr=0x%x, Value=0x%x\n",
	       (int)reg_mem->param.mem_rw.addr,
	       (int)reg_mem->param.mem_rw.value);

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (req->action == MLAN_ACT_GET) {
		if (copy_to_user
		    (wrq->u.data.pointer, &reg_mem->param.mem_rw.value,
		     sizeof(int))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get network monitor configurations
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_net_monitor_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int user_data_len = wrq->u.data.length;
	int data[5] = { 0 }, copy_len;
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
	mlan_ds_misc_net_monitor *net_mon = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	copy_len = MIN(sizeof(data), sizeof(int) * user_data_len);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		LEAVE();
		return -ENOMEM;
	}
	misc = (mlan_ds_misc_cfg *)req->pbuf;
	net_mon = (mlan_ds_misc_net_monitor *)&misc->param.net_mon;
	misc->sub_command = MLAN_OID_MISC_NET_MONITOR;
	req->req_id = MLAN_IOCTL_MISC_CFG;

	if (!user_data_len) {
		req->action = MLAN_ACT_GET;
	} else if (user_data_len == 1 || user_data_len == 4
		   || user_data_len == 5) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if (data[0] != MTRUE && data[0] != MFALSE) {
			PRINTM(MERROR,
			       "NET_MON: Activity should be enable(=1)/disable(=0)\n");
			ret = -EINVAL;
			goto done;
		}
		net_mon->enable_net_mon = data[0];
		if (data[0] == MTRUE) {
			int i;
			if (user_data_len != 4 && user_data_len != 5) {
				PRINTM(MERROR,
				       "NET_MON: Invalid number of args!\n");
				ret = -EINVAL;
				goto done;
			}
			/* Supported filter flags */
			if (!data[1] || data[1] &
			    ~(MLAN_NETMON_DATA_FRAME |
			      MLAN_NETMON_MANAGEMENT_FRAME |
			      MLAN_NETMON_CONTROL_FRAME)) {
				PRINTM(MERROR,
				       "NET_MON: Invalid filter flag\n");
				ret = -EINVAL;
				goto done;
			}
			/* Supported bands */
			for (i = 0; i < sizeof(SupportedAdhocBand); i++)
				if (data[2] == SupportedAdhocBand[i])
					break;
			if (i == sizeof(SupportedAdhocBand)) {
				PRINTM(MERROR, "NET_MON: Invalid band\n");
				ret = -EINVAL;
				goto done;
			}
			/* Supported channel */
			if (data[3] < 1 || data[3] > MLAN_MAX_CHANNEL) {
				PRINTM(MERROR,
				       "NET_MON: Invalid channel number\n");
				ret = -EINVAL;
				goto done;
			}
			if (user_data_len == 5) {
				/* Secondary channel offset */
				if (!(data[2] & (BAND_GN | BAND_AN))) {
					PRINTM(MERROR,
					       "No 11n in band, can not set "
					       "secondary channel offset\n");
					ret = -EINVAL;
					goto done;
				}
				if ((data[4] != CHANNEL_BW_20MHZ) &&
				    (data[4] != CHANNEL_BW_40MHZ_ABOVE) &&
				    (data[4] != CHANNEL_BW_40MHZ_BELOW)
					) {
					PRINTM(MERROR,
					       "Invalid secondary channel bandwidth, "
					       "only allowed 0, 1, 3 or 4\n");
					ret = -EINVAL;
					goto done;
				}
				net_mon->chan_bandwidth = data[4];
			}
			net_mon->filter_flag = data[1];
			net_mon->band = data[2];
			net_mon->channel = data[3];
		}
		req->action = MLAN_ACT_SET;
	} else {
		PRINTM(MERROR, "NET_MON: Invalid number of args!\n");
		ret = -EINVAL;
		goto done;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	data[0] = net_mon->enable_net_mon;
	data[1] = net_mon->filter_flag;
	data[2] = net_mon->band;
	data[3] = net_mon->channel;
	data[4] = net_mon->chan_bandwidth;
	wrq->u.data.length = 5;
	if (copy_to_user
	    (wrq->u.data.pointer, data, sizeof(int) * wrq->u.data.length)) {
		PRINTM(MERROR, "Copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Get LOG
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_get_log(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_get_stats stats;
	char *buf = NULL;
	int i = 0;

	ENTER();

	PRINTM(MINFO, " GET STATS\n");
	buf = kmalloc(GETLOG_BUFSIZE, GFP_KERNEL);
	if (!buf) {
		PRINTM(MERROR, "kmalloc failed!\n");
		ret = -ENOMEM;
		goto done;
	}

	memset(&stats, 0, sizeof(mlan_ds_get_stats));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_stats_info(priv, MOAL_IOCTL_WAIT, &stats)) {
		ret = -EFAULT;
		goto done;
	}

	if (wrq->u.data.pointer) {
		sprintf(buf, "\n"
			"mcasttxframe     %u\n"
			"failed           %u\n"
			"retry            %u\n"
			"multiretry       %u\n"
			"framedup         %u\n"
			"rtssuccess       %u\n"
			"rtsfailure       %u\n"
			"ackfailure       %u\n"
			"rxfrag           %u\n"
			"mcastrxframe     %u\n"
			"fcserror         %u\n"
			"txframe          %u\n"
			"wepicverrcnt-1   %u\n"
			"wepicverrcnt-2   %u\n"
			"wepicverrcnt-3   %u\n"
			"wepicverrcnt-4   %u\n"
			"beacon_rcnt      %u\n"
			"beacon_mcnt      %u\n",
			stats.mcast_tx_frame,
			stats.failed,
			stats.retry,
			stats.multi_retry,
			stats.frame_dup,
			stats.rts_success,
			stats.rts_failure,
			stats.ack_failure,
			stats.rx_frag,
			stats.mcast_rx_frame,
			stats.fcs_error,
			stats.tx_frame,
			stats.wep_icv_error[0],
			stats.wep_icv_error[1],
			stats.wep_icv_error[2],
			stats.wep_icv_error[3],
			stats.bcn_rcv_cnt, stats.bcn_miss_cnt);
		if (priv->phandle->fw_getlog_enable) {
			sprintf(buf + strlen(buf), "tx_frag_cnt       %u\n",
				stats.tx_frag_cnt);
			sprintf(buf + strlen(buf), "qos_tx_frag_cnt        ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_tx_frag_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_failed_cnt         ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_failed_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_retry_cnt          ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_retry_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_multi_retry_cnt    ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_multi_retry_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_frm_dup_cnt        ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_frm_dup_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_rts_suc_cnt        ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_rts_suc_cnt[i]);
			}
			sprintf(buf + strlen(buf),
				"\nqos_rts_failure_cnt        ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_rts_failure_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_ack_failure_cnt    ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_ack_failure_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_rx_frag_cnt        ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_rx_frag_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_tx_frm_cnt         ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_tx_frm_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_discarded_frm_cnt  ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_discarded_frm_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_mpdus_rx_cnt       ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_mpdus_rx_cnt[i]);
			}
			sprintf(buf + strlen(buf), "\nqos_retries_rx_cnt     ");
			for (i = 0; i < 8; i++) {
				sprintf(buf + strlen(buf), "%u ",
					stats.qos_retries_rx_cnt[i]);
			}
			sprintf(buf + strlen(buf),
				"\nmgmt_ccmp_replays      %u\n"
				"tx_amsdu_cnt           %u\n"
				"failed_amsdu_cnt       %u\n"
				"retry_amsdu_cnt        %u\n"
				"multi_retry_amsdu_cnt  %u\n"
				"tx_octets_in_amsdu_cnt %llu\n"
				"amsdu_ack_failure_cnt  %u\n"
				"rx_amsdu_cnt           %u\n"
				"rx_octets_in_amsdu_cnt %llu\n"
				"tx_ampdu_cnt           %u\n"
				"tx_mpdus_in_ampdu_cnt  %u\n"
				"tx_octets_in_ampdu_cnt %llu\n"
				"ampdu_rx_cnt           %u\n"
				"mpdu_in_rx_ampdu_cnt   %u\n"
				"rx_octets_in_ampdu_cnt %llu\n"
				"ampdu_delimiter_crc_error_cnt      %u\n",
				stats.mgmt_ccmp_replays, stats.tx_amsdu_cnt,
				stats.failed_amsdu_cnt, stats.retry_amsdu_cnt,
				stats.multi_retry_amsdu_cnt,
				stats.tx_octets_in_amsdu_cnt,
				stats.amsdu_ack_failure_cnt, stats.rx_amsdu_cnt,
				stats.rx_octets_in_amsdu_cnt,
				stats.tx_ampdu_cnt, stats.tx_mpdus_in_ampdu_cnt,
				stats.tx_octets_in_ampdu_cnt,
				stats.ampdu_rx_cnt, stats.mpdu_in_rx_ampdu_cnt,
				stats.rx_octets_in_ampdu_cnt,
				stats.ampdu_delimiter_crc_error_cnt);

		}
		wrq->u.data.length = strlen(buf) + 1;
		if (copy_to_user(wrq->u.data.pointer, buf, wrq->u.data.length)) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
		}
	}
done:
	kfree(buf);
	LEAVE();
	return ret;
}

/**
 *  @brief Deauthenticate
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_deauth(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	struct sockaddr saddr;

	ENTER();
	if (wrq->u.data.length) {
		/* Deauth mentioned BSSID */
		if (copy_from_user(&saddr, wrq->u.data.pointer, sizeof(saddr))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if (MLAN_STATUS_SUCCESS !=
		    woal_disconnect(priv, MOAL_IOCTL_WAIT,
				    (t_u8 *)saddr.sa_data,
				    DEF_DEAUTH_REASON_CODE)) {
			ret = -EFAULT;
			goto done;
		}
	} else {
		if (MLAN_STATUS_SUCCESS !=
		    woal_disconnect(priv, MOAL_IOCTL_WAIT, NULL,
				    DEF_DEAUTH_REASON_CODE))
			ret = -EFAULT;
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get TX power configurations
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_tx_power_cfg(moal_private *priv, struct iwreq *wrq)
{
	int data[5], user_data_len, copy_len;
	int ret = 0;
	mlan_bss_info bss_info;
	mlan_ds_power_cfg *pcfg = NULL;
	mlan_ioctl_req *req = NULL;
	int power_data[MAX_POWER_TABLE_SIZE];
	int i, power_ext_len = 0;
	int *ptr = power_data;
	mlan_power_group *pwr_grp = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();

	memset(&bss_info, 0, sizeof(bss_info));
	woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);

	memset(data, 0, sizeof(data));
	user_data_len = wrq->u.data.length;
	copy_len = MIN(sizeof(data), sizeof(int) * user_data_len);

	if (user_data_len) {
		if (sizeof(int) * user_data_len > sizeof(data)) {
			PRINTM(MERROR, "Too many arguments\n");
			ret = -EINVAL;
			goto done;
		}
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		switch (user_data_len) {
		case 1:
			if (data[0] != 0xFF)
				ret = -EINVAL;
			break;
		case 2:
		case 4:
			if (data[0] == 0xFF) {
				ret = -EINVAL;
				break;
			}
			if (data[1] < bss_info.min_power_level) {
				PRINTM(MERROR,
				       "The set powercfg rate value %d dBm is out of range (%d dBm-%d dBm)!\n",
				       data[1], (int)bss_info.min_power_level,
				       (int)bss_info.max_power_level);
				ret = -EINVAL;
				break;
			}
			if (user_data_len == 4) {
				if (data[1] > data[2]) {
					PRINTM(MERROR,
					       "Min power should be less than maximum!\n");
					ret = -EINVAL;
					break;
				}
				if (data[3] < 0) {
					PRINTM(MERROR,
					       "Step should not less than 0!\n");
					ret = -EINVAL;
					break;
				}
				if (data[2] > bss_info.max_power_level) {
					PRINTM(MERROR,
					       "The set powercfg rate value %d dBm is out of range (%d dBm-%d dBm)!\n",
					       data[2],
					       (int)bss_info.min_power_level,
					       (int)bss_info.max_power_level);
					ret = -EINVAL;
					break;
				}
				if (data[3] > data[2] - data[1]) {
					PRINTM(MERROR,
					       "Step should not greater than power difference!\n");
					ret = -EINVAL;
					break;
				}
			}
			break;
		default:
			ret = -EINVAL;
			break;
		}
		if (ret)
			goto done;
	}

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_power_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	pcfg = (mlan_ds_power_cfg *)req->pbuf;
	pcfg->sub_command = MLAN_OID_POWER_CFG_EXT;
	req->req_id = MLAN_IOCTL_POWER_CFG;
	if (!user_data_len)
		req->action = MLAN_ACT_GET;
	else {
		req->action = MLAN_ACT_SET;
		if (data[0] == 0xFF)
			pcfg->param.power_ext.power_group[0].rate_format =
				TX_PWR_CFG_AUTO_CTRL_OFF;
		else {
			pcfg->param.power_ext.power_group[0].power_step = 0;
			pcfg->param.power_ext.power_group[0].first_rate_ind =
				data[0];
			pcfg->param.power_ext.power_group[0].last_rate_ind =
				data[0];
			if (data[0] <= 11) {
				pcfg->param.power_ext.power_group[0].
					rate_format = MLAN_RATE_FORMAT_LG;
				pcfg->param.power_ext.power_group[0].bandwidth =
					MLAN_HT_BW20;
			} else if (data[0] <= 27) {
				pcfg->param.power_ext.power_group[0].
					rate_format = MLAN_RATE_FORMAT_HT;
				pcfg->param.power_ext.power_group[0].bandwidth =
					MLAN_HT_BW20;
				pcfg->param.power_ext.power_group[0].
					first_rate_ind -= 12;
				pcfg->param.power_ext.power_group[0].
					last_rate_ind -= 12;
			} else if ((140 <= data[0]) && (data[0] <= 155)) {
				pcfg->param.power_ext.power_group[0].
					rate_format = MLAN_RATE_FORMAT_HT;
				pcfg->param.power_ext.power_group[0].bandwidth =
					MLAN_HT_BW40;
				pcfg->param.power_ext.power_group[0].
					first_rate_ind -= 140;
				pcfg->param.power_ext.power_group[0].
					last_rate_ind -= 140;
			}
			if (user_data_len == 2) {
				pcfg->param.power_ext.power_group[0].power_min =
					data[1];
				pcfg->param.power_ext.power_group[0].power_max =
					data[1];
			} else if (user_data_len == 4) {
				pcfg->param.power_ext.power_group[0].power_min =
					data[1];
				pcfg->param.power_ext.power_group[0].power_max =
					data[2];
				pcfg->param.power_ext.power_group[0].
					power_step = data[3];
			}
			pcfg->param.power_ext.num_pwr_grp = 1;
		}
	}
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	if (!user_data_len) {
		/* GET operation */
		i = 0;
		power_ext_len = 0;
		ptr = power_data;
		while ((i < pcfg->param.power_ext.num_pwr_grp) &&
		       (power_ext_len < MAX_POWER_TABLE_SIZE)) {
			pwr_grp = &pcfg->param.power_ext.power_group[i];
			if (pwr_grp->rate_format == MLAN_RATE_FORMAT_HT) {
				if (pwr_grp->bandwidth == MLAN_HT_BW20) {
					pwr_grp->first_rate_ind += 12;
					pwr_grp->last_rate_ind += 12;
				} else if (pwr_grp->bandwidth == MLAN_HT_BW40) {
					pwr_grp->first_rate_ind += 140;
					pwr_grp->last_rate_ind += 140;
				}
			}

			if ((pwr_grp->rate_format == MLAN_RATE_FORMAT_LG) ||
			    (pwr_grp->rate_format == MLAN_RATE_FORMAT_HT)) {
				*ptr = pwr_grp->first_rate_ind;
				ptr++;
				*ptr = pwr_grp->last_rate_ind;
				ptr++;
				*ptr = pwr_grp->power_min;
				ptr++;
				*ptr = pwr_grp->power_max;
				ptr++;
				*ptr = pwr_grp->power_step;
				ptr++;
				power_ext_len += 5;
			}
			i++;
		}
		if (copy_to_user(wrq->u.data.pointer, (t_u8 *)power_data,
				 sizeof(int) * power_ext_len)) {
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = power_ext_len;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Get Tx/Rx data rates
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_get_txrx_rate(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_rate *rate = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_rate));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	rate = (mlan_ds_rate *)req->pbuf;
	rate->sub_command = MLAN_OID_GET_DATA_RATE;
	req->req_id = MLAN_IOCTL_RATE;
	req->action = MLAN_ACT_GET;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (copy_to_user
	    (wrq->u.data.pointer, (t_u8 *)&rate->param.data_rate,
	     sizeof(int) * 2)) {
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 2;
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Turn on/off the sdio clock
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0/MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static int
woal_sdio_clock_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	int data = 2;
	/* Initialize the clock state as on */
	static int clock_state = 1;

	ENTER();

	if (wrq->u.data.length) {
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
	} else {
		wrq->u.data.length = sizeof(clock_state) / sizeof(int);
		if (copy_to_user
		    (wrq->u.data.pointer, &clock_state, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		goto done;
	}
	switch (data) {
	case CMD_DISABLED:
		PRINTM(MINFO, "SDIO clock is turned off\n");
		ret = woal_sdio_set_bus_clock(priv->phandle, MFALSE);
		clock_state = data;
		break;
	case CMD_ENABLED:
		PRINTM(MINFO, "SDIO clock is turned on\n");
		ret = woal_sdio_set_bus_clock(priv->phandle, MTRUE);
		clock_state = data;
		break;
	default:
		ret = -EINVAL;
		PRINTM(MINFO, "sdioclock: wrong parameter\n");
		break;
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get beacon interval
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_beacon_interval(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_bss *bss = NULL;
	mlan_ioctl_req *req = NULL;
	int bcn = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (wrq->u.data.length) {
		if (copy_from_user(&bcn, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if ((bcn < MLAN_MIN_BEACON_INTERVAL) ||
		    (bcn > MLAN_MAX_BEACON_INTERVAL)) {
			ret = -EINVAL;
			goto done;
		}
	}

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	bss = (mlan_ds_bss *)req->pbuf;
	bss->sub_command = MLAN_OID_IBSS_BCN_INTERVAL;
	req->req_id = MLAN_IOCTL_BSS;
	if (!wrq->u.data.length)
		req->action = MLAN_ACT_GET;
	else {
		req->action = MLAN_ACT_SET;
		bss->param.bcn_interval = bcn;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (copy_to_user
	    (wrq->u.data.pointer, (t_u8 *)&bss->param.bcn_interval,
	     sizeof(int))) {
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 1;
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get ATIM window
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_atim_window(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_bss *bss = NULL;
	mlan_ioctl_req *req = NULL;
	int atim = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (wrq->u.data.length) {
		if (copy_from_user(&atim, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if ((atim < 0) || (atim > MLAN_MAX_ATIM_WINDOW)) {
			ret = -EINVAL;
			goto done;
		}
	}

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	bss = (mlan_ds_bss *)req->pbuf;
	bss->sub_command = MLAN_OID_IBSS_ATIM_WINDOW;
	req->req_id = MLAN_IOCTL_BSS;
	if (!wrq->u.data.length)
		req->action = MLAN_ACT_GET;
	else {
		req->action = MLAN_ACT_SET;
		bss->param.atim_window = atim;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (copy_to_user
	    (wrq->u.data.pointer, (t_u8 *)&bss->param.atim_window,
	     sizeof(int))) {
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 1;
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Set/Get TX data rate
 *
 * @param priv      A pointer to moal_private structure
 * @param wrq       A pointer to iwreq structure
 *
 * @return          0 --success, otherwise fail
 */
static int
woal_set_get_txrate(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_rate *rate = NULL;
	mlan_ioctl_req *req = NULL;
	int rateindex = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();
	if (wrq->u.data.length) {
		if (copy_from_user
		    (&rateindex, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_rate));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	rate = (mlan_ds_rate *)req->pbuf;
	rate->param.rate_cfg.rate_type = MLAN_RATE_INDEX;
	rate->sub_command = MLAN_OID_RATE_CFG;
	req->req_id = MLAN_IOCTL_RATE;
	if (!wrq->u.data.length)
		req->action = MLAN_ACT_GET;
	else {
		req->action = MLAN_ACT_SET;
		if (rateindex == AUTO_RATE)
			rate->param.rate_cfg.is_rate_auto = 1;
		else {
			if ((rateindex != MLAN_RATE_INDEX_MCS32) &&
			    ((rateindex < 0) ||
			     (rateindex > MLAN_RATE_INDEX_MCS7))) {
				ret = -EINVAL;
				goto done;
			}
		}
		rate->param.rate_cfg.rate = rateindex;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	} else {
		if (wrq->u.data.length)
			priv->rate_index = rateindex;
	}
	if (!wrq->u.data.length) {
		if (rate->param.rate_cfg.is_rate_auto)
			rateindex = AUTO_RATE;
		else
			rateindex = rate->param.rate_cfg.rate;
		wrq->u.data.length = 1;
		if (copy_to_user(wrq->u.data.pointer, &rateindex, sizeof(int)))
			ret = -EFAULT;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Set/Get region code
 *
 * @param priv      A pointer to moal_private structure
 * @param wrq       A pointer to iwreq structure
 *
 * @return          0 --success, otherwise fail
 */
static int
woal_set_get_regioncode(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_misc_cfg *cfg = NULL;
	mlan_ioctl_req *req = NULL;
	int region = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (wrq->u.data.length) {
		if (copy_from_user(&region, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	cfg = (mlan_ds_misc_cfg *)req->pbuf;
	cfg->sub_command = MLAN_OID_MISC_REGION;
	req->req_id = MLAN_IOCTL_MISC_CFG;
	if (!wrq->u.data.length)
		req->action = MLAN_ACT_GET;
	else {
		req->action = MLAN_ACT_SET;
		cfg->param.region_code = region;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (!wrq->u.data.length) {
		wrq->u.data.length = 1;
		if (copy_to_user
		    (wrq->u.data.pointer, &cfg->param.region_code, sizeof(int)))
			ret = -EFAULT;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Set/Get radio
 *
 * @param priv      A pointer to moal_private structure
 * @param wrq       A pointer to iwreq structure
 *
 * @return          0 --success, otherwise fail
 */
static int
woal_set_get_radio(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_bss_info bss_info;
	int option = 0;

	ENTER();

	memset(&bss_info, 0, sizeof(bss_info));

	if (wrq->u.data.length) {
		/* Set radio */
		if (copy_from_user(&option, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if (MLAN_STATUS_SUCCESS != woal_set_radio(priv, (t_u8)option))
			ret = -EFAULT;
	} else {
		/* Get radio status */
		woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
		wrq->u.data.length = 1;
		if (copy_to_user
		    (wrq->u.data.pointer, &bss_info.radio_on,
		     sizeof(bss_info.radio_on))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
		}
	}
done:
	LEAVE();
	return ret;
}

#ifdef DEBUG_LEVEL1
/**
 *  @brief Get/Set the bit mask of driver debug message control
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to wrq structure
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_drv_dbg(moal_private *priv, struct iwreq *wrq)
{
	int data[4], copy_len;
	int ret = 0;
	int data_length = wrq->u.data.length;
	ENTER();

	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	if (!data_length) {
		data[0] = drvdbg;
		/* Return the current driver debug bit masks */
		if (copy_to_user(wrq->u.data.pointer, data, sizeof(int))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto drvdbgexit;
		}
		wrq->u.data.length = 1;
	} else if (data_length < 3) {
		/* Get the driver debug bit masks from user */
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto drvdbgexit;
		}
		drvdbg = data[0];
		/* Set the driver debug bit masks into mlan */
		woal_set_drvdbg(priv, drvdbg);
	} else {
		PRINTM(MERROR, "Invalid parameter number\n");
		goto drvdbgexit;
	}

	printk(KERN_ALERT "drvdbg = 0x%08x\n", drvdbg);
#ifdef DEBUG_LEVEL2
	printk(KERN_ALERT "MINFO  (%08x) %s\n", MINFO,
	       (drvdbg & MINFO) ? "X" : "");
	printk(KERN_ALERT "MWARN  (%08x) %s\n", MWARN,
	       (drvdbg & MWARN) ? "X" : "");
	printk(KERN_ALERT "MENTRY (%08x) %s\n", MENTRY,
	       (drvdbg & MENTRY) ? "X" : "");
#endif
	printk(KERN_ALERT "MMPA_D (%08x) %s\n", MMPA_D,
	       (drvdbg & MMPA_D) ? "X" : "");
	printk(KERN_ALERT "MIF_D  (%08x) %s\n", MIF_D,
	       (drvdbg & MIF_D) ? "X" : "");
	printk(KERN_ALERT "MFW_D  (%08x) %s\n", MFW_D,
	       (drvdbg & MFW_D) ? "X" : "");
	printk(KERN_ALERT "MEVT_D (%08x) %s\n", MEVT_D,
	       (drvdbg & MEVT_D) ? "X" : "");
	printk(KERN_ALERT "MCMD_D (%08x) %s\n", MCMD_D,
	       (drvdbg & MCMD_D) ? "X" : "");
	printk(KERN_ALERT "MDAT_D (%08x) %s\n", MDAT_D,
	       (drvdbg & MDAT_D) ? "X" : "");
	printk(KERN_ALERT "MIOCTL (%08x) %s\n", MIOCTL,
	       (drvdbg & MIOCTL) ? "X" : "");
	printk(KERN_ALERT "MINTR  (%08x) %s\n", MINTR,
	       (drvdbg & MINTR) ? "X" : "");
	printk(KERN_ALERT "MEVENT (%08x) %s\n", MEVENT,
	       (drvdbg & MEVENT) ? "X" : "");
	printk(KERN_ALERT "MCMND  (%08x) %s\n", MCMND,
	       (drvdbg & MCMND) ? "X" : "");
	printk(KERN_ALERT "MDATA  (%08x) %s\n", MDATA,
	       (drvdbg & MDATA) ? "X" : "");
	printk(KERN_ALERT "MERROR (%08x) %s\n", MERROR,
	       (drvdbg & MERROR) ? "X" : "");
	printk(KERN_ALERT "MFATAL (%08x) %s\n", MFATAL,
	       (drvdbg & MFATAL) ? "X" : "");
	printk(KERN_ALERT "MMSG   (%08x) %s\n", MMSG,
	       (drvdbg & MMSG) ? "X" : "");

drvdbgexit:
	LEAVE();
	return ret;
}
#endif /* DEBUG_LEVEL1 */

/**
 * @brief Set/Get QoS configuration
 *
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 *
 * @return         0 --success, otherwise fail
 */
static int
woal_set_get_qos_cfg(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_wmm_cfg *cfg = NULL;
	mlan_ioctl_req *req = NULL;
	int data = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	cfg = (mlan_ds_wmm_cfg *)req->pbuf;
	cfg->sub_command = MLAN_OID_WMM_CFG_QOS;
	req->req_id = MLAN_IOCTL_WMM_CFG;
	if (wrq->u.data.length) {
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		req->action = MLAN_ACT_SET;
		cfg->param.qos_cfg = (t_u8)data;
	} else
		req->action = MLAN_ACT_GET;
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	if (!wrq->u.data.length) {
		data = (int)cfg->param.qos_cfg;
		if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Set/Get WWS mode
 *
 * @param priv      A pointer to moal_private structure
 * @param wrq       A pointer to iwreq structure
 *
 * @return          0 --success, otherwise fail
 */
static int
woal_wws_cfg(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_misc_cfg *wws = NULL;
	mlan_ioctl_req *req = NULL;
	int data = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	wws = (mlan_ds_misc_cfg *)req->pbuf;
	wws->sub_command = MLAN_OID_MISC_WWS;
	req->req_id = MLAN_IOCTL_MISC_CFG;
	if (wrq->u.data.length) {
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if (data != CMD_DISABLED && data != CMD_ENABLED) {
			ret = -EINVAL;
			goto done;
		}
		req->action = MLAN_ACT_SET;
		wws->param.wws_cfg = data;
	} else
		req->action = MLAN_ACT_GET;
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	if (!wrq->u.data.length) {
		data = wws->param.wws_cfg;
		if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Set/Get sleep period
 *
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 *
 * @return         0 --success, otherwise fail
 */
static int
woal_sleep_pd(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_pm_cfg *pm_cfg = NULL;
	mlan_ioctl_req *req = NULL;
	int data = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_pm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	pm_cfg = (mlan_ds_pm_cfg *)req->pbuf;
	pm_cfg->sub_command = MLAN_OID_PM_CFG_SLEEP_PD;
	req->req_id = MLAN_IOCTL_PM_CFG;
	if (wrq->u.data.length) {
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if ((data <= MAX_SLEEP_PERIOD && data >= MIN_SLEEP_PERIOD) ||
		    (data == 0)
		    || (data == SLEEP_PERIOD_RESERVED_FF)
			) {
			req->action = MLAN_ACT_SET;
			pm_cfg->param.sleep_period = data;
		} else {
			ret = -EINVAL;
			goto done;
		}
	} else
		req->action = MLAN_ACT_GET;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	if (!wrq->u.data.length) {
		data = pm_cfg->param.sleep_period;
		if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Set/Get module configuration
 *
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 *
 * @return         0 --success, otherwise fail
 */
static int
woal_fw_wakeup_method(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0, data[2];
	mlan_ds_pm_cfg *pm_cfg = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_pm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	pm_cfg = (mlan_ds_pm_cfg *)req->pbuf;

	if (wrq->u.data.length > 2) {
		ret = -EINVAL;
		goto done;
	}
	if (!wrq->u.data.length) {
		req->action = MLAN_ACT_GET;
	} else {
		req->action = MLAN_ACT_SET;
		if (copy_from_user
		    (data, wrq->u.data.pointer,
		     sizeof(int) * wrq->u.data.length)) {
			PRINTM(MINFO, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if (data[0] != FW_WAKEUP_METHOD_INTERFACE &&
		    data[0] != FW_WAKEUP_METHOD_GPIO) {
			PRINTM(MERROR, "Invalid FW wake up method:%d\n",
			       data[0]);
			ret = -EINVAL;
			goto done;
		}
		if (data[0] == FW_WAKEUP_METHOD_GPIO) {
			if (wrq->u.data.length == 1) {
				PRINTM(MERROR,
				       "Please provide gpio pin number for FW_WAKEUP_METHOD gpio\n");
				ret = -EINVAL;
				goto done;
			}
			pm_cfg->param.fw_wakeup_params.gpio_pin = data[1];
		}

		pm_cfg->param.fw_wakeup_params.method = data[0];
	}

	pm_cfg->sub_command = MLAN_OID_PM_CFG_FW_WAKEUP_METHOD;
	req->req_id = MLAN_IOCTL_PM_CFG;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	data[0] = ((mlan_ds_pm_cfg *)req->pbuf)->param.fw_wakeup_params.method;
	data[1] =
		((mlan_ds_pm_cfg *)req->pbuf)->param.fw_wakeup_params.gpio_pin;

	if (data[0] == FW_WAKEUP_METHOD_INTERFACE)
		wrq->u.data.length = 1;
	else
		wrq->u.data.length = 2;
	if (copy_to_user
	    (wrq->u.data.pointer, data, sizeof(int) * wrq->u.data.length)) {
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Configure sleep parameters
 *
 * @param priv         A pointer to moal_private structure
 * @param wrq         A pointer to iwreq structure
 *
 * @return             0 --success, otherwise fail
 */
static int
woal_sleep_params_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_pm_cfg *pm = NULL;
	mlan_ds_sleep_params *psleep_params = NULL;
	int data[6] = { 0 }, i, copy_len;
	int data_length = wrq->u.data.length;
#ifdef DEBUG_LEVEL1
	char err_str[][35] = { {"sleep clock error in ppm"},
	{"wakeup offset in usec"},
	{"clock stabilization time in usec"},
	{"value of reserved for debug"}
	};
#endif
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_pm_cfg));
	if (req == NULL) {
		LEAVE();
		return -ENOMEM;
	}

	pm = (mlan_ds_pm_cfg *)req->pbuf;
	pm->sub_command = MLAN_OID_PM_CFG_SLEEP_PARAMS;
	req->req_id = MLAN_IOCTL_PM_CFG;
	psleep_params = (pmlan_ds_sleep_params)&pm->param.sleep_params;

	if (data_length == 0) {
		req->action = MLAN_ACT_GET;
	} else if (data_length == 6) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			/* copy_from_user failed  */
			PRINTM(MERROR, "S_PARAMS: copy from user failed\n");
			LEAVE();
			return -EINVAL;
		}
#define MIN_VAL 0x0000
#define MAX_VAL 0xFFFF
		for (i = 0; i < 6; i++) {
			if ((i == 3) || (i == 4)) {
				/* These two cases are handled below the loop */
				continue;
			}
			if (data[i] < MIN_VAL || data[i] > MAX_VAL) {
				PRINTM(MERROR, "Invalid %s (0-65535)!\n",
				       err_str[i]);
				ret = -EINVAL;
				goto done;
			}
		}
		if (data[3] < 0 || data[3] > 2) {
			PRINTM(MERROR,
			       "Invalid control periodic calibration (0-2)!\n");
			ret = -EINVAL;
			goto done;
		}
		if (data[4] < 0 || data[4] > 2) {
			PRINTM(MERROR,
			       "Invalid control of external sleep clock (0-2)!\n");
			ret = -EINVAL;
			goto done;
		}
		req->action = MLAN_ACT_SET;
		psleep_params->error = data[0];
		psleep_params->offset = data[1];
		psleep_params->stable_time = data[2];
		psleep_params->cal_control = data[3];
		psleep_params->ext_sleep_clk = data[4];
		psleep_params->reserved = data[5];
	} else {
		ret = -EINVAL;
		goto done;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	data[0] = psleep_params->error;
	data[1] = psleep_params->offset;
	data[2] = psleep_params->stable_time;
	data[3] = psleep_params->cal_control;
	data[4] = psleep_params->ext_sleep_clk;
	data[5] = psleep_params->reserved;
	wrq->u.data.length = 6;

	if (copy_to_user(wrq->u.data.pointer, data, sizeof(int) *
			 wrq->u.data.length)) {
		PRINTM(MERROR, "QCONFIG: copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Control Coalescing status Enable/Disable
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      Pointer to user data
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_coalescing_status_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_misc_cfg *pcoal = NULL;
	mlan_ioctl_req *req = NULL;
	char buf[8];
	struct iwreq *wreq = (struct iwreq *)wrq;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	pcoal = (mlan_ds_misc_cfg *)req->pbuf;

	memset(buf, 0, sizeof(buf));
	if (!wrq->u.data.length) {
		req->action = MLAN_ACT_GET;
	} else {
		req->action = MLAN_ACT_SET;
		if (copy_from_user(buf, wrq->u.data.pointer,
				   MIN(sizeof(buf) - 1, wreq->u.data.length))) {
			PRINTM(MINFO, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if (buf[0] == 1)
			pcoal->param.coalescing_status =
				MLAN_MISC_COALESCING_ENABLE;
		else
			pcoal->param.coalescing_status =
				MLAN_MISC_COALESCING_DISABLE;
	}

	req->req_id = MLAN_IOCTL_MISC_CFG;
	pcoal->sub_command = MLAN_OID_MISC_COALESCING_STATUS;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	buf[0] = ((mlan_ds_misc_cfg *)req->pbuf)->param.coalescing_status;

	if (copy_to_user(wrq->u.data.pointer, buf, wrq->u.data.length)) {
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/get user provisioned local power constraint
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *  @return         0 --success, otherwise fail
 */
static int
woal_set_get_11h_local_pwr_constraint(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0, data = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11h_cfg *ds_11hcfg = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11h_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	ds_11hcfg = (mlan_ds_11h_cfg *)req->pbuf;
	if (wrq->u.data.length) {
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MINFO, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		ds_11hcfg->param.usr_local_power_constraint = (t_s8)data;
		req->action = MLAN_ACT_SET;
	} else
		req->action = MLAN_ACT_GET;

	ds_11hcfg->sub_command = MLAN_OID_11H_LOCAL_POWER_CONSTRAINT;
	req->req_id = MLAN_IOCTL_11H_CFG;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	/* Copy response to user */
	if (req->action == MLAN_ACT_GET) {
		data = (int)ds_11hcfg->param.usr_local_power_constraint;
		if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
			PRINTM(MINFO, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/get MAC control configuration
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *  @return         0 --success, otherwise fail
 */
static int
woal_mac_control_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0, data = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *cfg = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	cfg = (mlan_ds_misc_cfg *)req->pbuf;
	if (wrq->u.data.length) {
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MINFO, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		/* Validation will be done later */
		cfg->param.mac_ctrl = data;
		req->action = MLAN_ACT_SET;
	} else
		req->action = MLAN_ACT_GET;

	cfg->sub_command = MLAN_OID_MISC_MAC_CONTROL;
	req->req_id = MLAN_IOCTL_MISC_CFG;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	/* Copy response to user */
	data = (int)cfg->param.mac_ctrl;
	if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
		PRINTM(MINFO, "Copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 1;

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Get thermal reading
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *  @return         0 --success, otherwise fail
 */
static int
woal_thermal_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0, data = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *cfg = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	cfg = (mlan_ds_misc_cfg *)req->pbuf;
	if (wrq->u.data.length) {
		PRINTM(MERROR, "Set is not supported for this command\n");
		ret = -EINVAL;
		goto done;
	}
	req->action = MLAN_ACT_GET;

	cfg->sub_command = MLAN_OID_MISC_THERMAL;
	req->req_id = MLAN_IOCTL_MISC_CFG;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	/* Copy response to user */
	data = (int)cfg->param.thermal;
	if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
		PRINTM(MINFO, "Copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 1;

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

#if defined(REASSOCIATION)
/**
 * @brief Set/Get reassociation settings
 *
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 *
 * @return         0 --success, otherwise fail
 */
static int
woal_set_get_reassoc(moal_private *priv, struct iwreq *wrq)
{
	moal_handle *handle = priv->phandle;
	int ret = 0;
	int data = 0;

	ENTER();

	if (wrq->u.data.length) {
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if (data == 0) {
			handle->reassoc_on &= ~MBIT(priv->bss_index);
			priv->reassoc_on = MFALSE;
			priv->reassoc_required = MFALSE;
			if (!handle->reassoc_on &&
			    handle->is_reassoc_timer_set == MTRUE) {
				woal_cancel_timer(&handle->reassoc_timer);
				handle->is_reassoc_timer_set = MFALSE;
			}
		} else {
			handle->reassoc_on |= MBIT(priv->bss_index);
			priv->reassoc_on = MTRUE;
		}
	} else {
		data = (int)(priv->reassoc_on);
		if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int))) {
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}

done:
	LEAVE();
	return ret;
}
#endif /* REASSOCIATION */

/**
 *  @brief implement WMM enable command
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      Pointer to the iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_wmm_enable_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_wmm_cfg *wmm = NULL;
	mlan_ioctl_req *req = NULL;
	int data = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	wmm = (mlan_ds_wmm_cfg *)req->pbuf;
	req->req_id = MLAN_IOCTL_WMM_CFG;
	wmm->sub_command = MLAN_OID_WMM_CFG_ENABLE;

	if (wrq->u.data.length) {
		/* Set WMM configuration */
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if ((data < CMD_DISABLED) || (data > CMD_ENABLED)) {
			ret = -EINVAL;
			goto done;
		}
		req->action = MLAN_ACT_SET;
		if (data == CMD_DISABLED)
			wmm->param.wmm_enable = MFALSE;
		else
			wmm->param.wmm_enable = MTRUE;
	} else {
		/* Get WMM status */
		req->action = MLAN_ACT_GET;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (wrq->u.data.pointer) {
		if (copy_to_user
		    (wrq->u.data.pointer, &wmm->param.wmm_enable,
		     sizeof(wmm->param.wmm_enable))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Implement 802.11D enable command
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      Pointer to the iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_11d_enable_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_11d_cfg *pcfg_11d = NULL;
	mlan_ioctl_req *req = NULL;
	int data = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11d_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	pcfg_11d = (mlan_ds_11d_cfg *)req->pbuf;
	req->req_id = MLAN_IOCTL_11D_CFG;
	pcfg_11d->sub_command = MLAN_OID_11D_CFG_ENABLE;
	if (wrq->u.data.length) {
		/* Set 11D configuration */
		if (copy_from_user(&data, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if ((data < CMD_DISABLED) || (data > CMD_ENABLED)) {
			ret = -EINVAL;
			goto done;
		}
		if (data == CMD_DISABLED)
			pcfg_11d->param.enable_11d = MFALSE;
		else
			pcfg_11d->param.enable_11d = MTRUE;
		req->action = MLAN_ACT_SET;
	} else {
		req->action = MLAN_ACT_GET;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (wrq->u.data.pointer) {
		if (copy_to_user
		    (wrq->u.data.pointer, &pcfg_11d->param.enable_11d,
		     sizeof(pcfg_11d->param.enable_11d))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Implement 802.11D clear chan table command
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      Pointer to the iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_11d_clr_chan_table(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_11d_cfg *pcfg_11d = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11d_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	pcfg_11d = (mlan_ds_11d_cfg *)req->pbuf;
	req->req_id = MLAN_IOCTL_11D_CFG;
	pcfg_11d->sub_command = MLAN_OID_11D_CLR_CHAN_TABLE;
	req->action = MLAN_ACT_SET;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Control WPS Session Enable/Disable
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      Pointer to the iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_wps_cfg_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_wps_cfg *pwps = NULL;
	mlan_ioctl_req *req = NULL;
	char buf[8];
	struct iwreq *wreq = (struct iwreq *)wrq;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	PRINTM(MINFO, "WOAL_WPS_SESSION\n");

	memset(buf, 0, sizeof(buf));
	if (copy_from_user(buf, wreq->u.data.pointer,
			   MIN(sizeof(buf) - 1, wreq->u.data.length))) {
		PRINTM(MERROR, "Copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wps_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	pwps = (mlan_ds_wps_cfg *)req->pbuf;
	req->req_id = MLAN_IOCTL_WPS_CFG;
	req->action = MLAN_ACT_SET;
	pwps->sub_command = MLAN_OID_WPS_CFG_SESSION;
	if (buf[0] == 1)
		pwps->param.wps_session = MLAN_WPS_CFG_SESSION_START;
	else
		pwps->param.wps_session = MLAN_WPS_CFG_SESSION_END;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set WPA passphrase and SSID
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      Pointer to the iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_passphrase(moal_private *priv, struct iwreq *wrq)
{
	t_u16 len = 0;
	char buf[256];
	char *begin = NULL, *end = NULL, *opt = NULL;
	int ret = 0, action = -1, i;
	mlan_ds_sec_cfg *sec = NULL;
	mlan_ioctl_req *req = NULL;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };
	t_u8 *mac = NULL;
	int data_length = wrq->u.data.length, copy_len;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (!data_length || data_length >= sizeof(buf)) {
		PRINTM(MERROR,
		       "Argument missing or too long for setpassphrase\n");
		ret = -EINVAL;
		goto done;
	}
	memset(buf, 0, sizeof(buf));
	copy_len = data_length;

	if (copy_from_user(buf, wrq->u.data.pointer, copy_len)) {
		PRINTM(MERROR, "Copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}

	/* Parse the buf to get the cmd_action */
	begin = buf;
	end = woal_strsep(&begin, ';', '/');
	if (!end) {
		PRINTM(MERROR, "Invalid option\n");
		ret = -EINVAL;
		goto done;
	}
	action = woal_atox(end);
	if (action < 0 || action > 2 || end[1] != '\0') {
		PRINTM(MERROR, "Invalid action argument %s\n", end);
		ret = -EINVAL;
		goto done;
	}
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	sec = (mlan_ds_sec_cfg *)req->pbuf;
	sec->sub_command = MLAN_OID_SEC_CFG_PASSPHRASE;
	req->req_id = MLAN_IOCTL_SEC_CFG;
	if (action == 0)
		req->action = MLAN_ACT_GET;
	else
		req->action = MLAN_ACT_SET;
	while (begin) {
		end = woal_strsep(&begin, ';', '/');
		opt = woal_strsep(&end, '=', '/');
		if (!opt || !end || !end[0]) {
			PRINTM(MERROR, "Invalid option\n");
			ret = -EINVAL;
			break;
		} else if (!strnicmp(opt, "ssid", strlen(opt))) {
			if (strlen(end) > MLAN_MAX_SSID_LENGTH) {
				PRINTM(MERROR,
				       "SSID length exceeds max length\n");
				ret = -EFAULT;
				break;
			}
			sec->param.passphrase.ssid.ssid_len = strlen(end);
			strncpy((char *)sec->param.passphrase.ssid.ssid, end,
				MIN(strlen(end), MLAN_MAX_SSID_LENGTH));
			PRINTM(MINFO, "ssid=%s, len=%d\n",
			       sec->param.passphrase.ssid.ssid,
			       (int)sec->param.passphrase.ssid.ssid_len);
		} else if (!strnicmp(opt, "bssid", strlen(opt))) {
			woal_mac2u8(sec->param.passphrase.bssid, end);
		} else if (!strnicmp(opt, "psk", strlen(opt)) &&
			   req->action == MLAN_ACT_SET) {
			if (strlen(end) != MLAN_PMK_HEXSTR_LENGTH) {
				PRINTM(MERROR, "Invalid PMK length\n");
				ret = -EINVAL;
				break;
			}
			woal_ascii2hex((t_u8 *)(sec->param.passphrase.psk.pmk.
						pmk), end,
				       MLAN_PMK_HEXSTR_LENGTH / 2);
			sec->param.passphrase.psk_type = MLAN_PSK_PMK;
		} else if (!strnicmp(opt, "passphrase", strlen(opt)) &&
			   req->action == MLAN_ACT_SET) {
			if (strlen(end) < MLAN_MIN_PASSPHRASE_LENGTH ||
			    strlen(end) > MLAN_MAX_PASSPHRASE_LENGTH) {
				PRINTM(MERROR,
				       "Invalid length for passphrase\n");
				ret = -EINVAL;
				break;
			}
			sec->param.passphrase.psk_type = MLAN_PSK_PASSPHRASE;
			memcpy(sec->param.passphrase.psk.passphrase.passphrase,
			       end,
			       sizeof(sec->param.passphrase.psk.passphrase.
				      passphrase));
			sec->param.passphrase.psk.passphrase.passphrase_len =
				strlen(end);
			PRINTM(MINFO, "passphrase=%s, len=%d\n",
			       sec->param.passphrase.psk.passphrase.passphrase,
			       (int)sec->param.passphrase.psk.passphrase.
			       passphrase_len);
		} else {
			PRINTM(MERROR, "Invalid option %s\n", opt);
			ret = -EINVAL;
			break;
		}
	}
	if (ret)
		goto done;

	if (action == 2)
		sec->param.passphrase.psk_type = MLAN_PSK_CLEAR;
	else if (action == 0)
		sec->param.passphrase.psk_type = MLAN_PSK_QUERY;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	if (action == 0) {
		memset(buf, 0, sizeof(buf));
		if (sec->param.passphrase.ssid.ssid_len) {
			len += sprintf(buf + len, "ssid:");
			memcpy(buf + len, sec->param.passphrase.ssid.ssid,
			       sec->param.passphrase.ssid.ssid_len);
			len += sec->param.passphrase.ssid.ssid_len;
			len += sprintf(buf + len, " ");
		}
		if (memcmp
		    (&sec->param.passphrase.bssid, zero_mac,
		     sizeof(zero_mac))) {
			mac = (t_u8 *)&sec->param.passphrase.bssid;
			len += sprintf(buf + len, "bssid:");
			for (i = 0; i < ETH_ALEN - 1; ++i)
				len += sprintf(buf + len, "%02x:", mac[i]);
			len += sprintf(buf + len, "%02x ", mac[i]);
		}
		if (sec->param.passphrase.psk_type == MLAN_PSK_PMK) {
			len += sprintf(buf + len, "psk:");
			for (i = 0; i < MLAN_MAX_KEY_LENGTH; ++i)
				len += sprintf(buf + len, "%02x",
					       sec->param.passphrase.psk.pmk.
					       pmk[i]);
			len += sprintf(buf + len, "\n");
		}
		if (sec->param.passphrase.psk_type == MLAN_PSK_PASSPHRASE) {
			len += sprintf(buf + len, "passphrase:%s\n",
				       sec->param.passphrase.psk.passphrase.
				       passphrase);
		}
		if (wrq->u.data.pointer) {
			if (copy_to_user
			    (wrq->u.data.pointer, buf, MIN(len, sizeof(buf)))) {
				PRINTM(MERROR, "Copy to user failed, len %d\n",
				       len);
				ret = -EFAULT;
				goto done;
			}
			wrq->u.data.length = len;
		}

	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Get esupp mode
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_get_esupp_mode(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_sec_cfg *sec = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	sec = (mlan_ds_sec_cfg *)req->pbuf;
	sec->sub_command = MLAN_OID_SEC_CFG_ESUPP_MODE;
	req->req_id = MLAN_IOCTL_SEC_CFG;
	req->action = MLAN_ACT_GET;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (copy_to_user
	    (wrq->u.data.pointer, (t_u8 *)&sec->param.esupp_mode,
	     sizeof(int) * 3)) {
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 3;
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/** AES key length */
#define AES_KEY_LEN 16
/**
 *  @brief Adhoc AES control
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to the iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_adhoc_aes_ioctl(moal_private *priv, struct iwreq *wrq)
{
	static char buf[256];
	int ret = 0, action = -1;
	unsigned int i;
	t_u8 key_ascii[32];
	t_u8 key_hex[16];
	t_u8 *tmp = NULL;
	mlan_bss_info bss_info;
	mlan_ds_sec_cfg *sec = NULL;
	mlan_ioctl_req *req = NULL;
	t_u8 bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	int data_length = wrq->u.data.length, copy_len;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();

	memset(key_ascii, 0x00, sizeof(key_ascii));
	memset(key_hex, 0x00, sizeof(key_hex));
	memset(buf, 0x00, sizeof(buf));

	/* Get current BSS information */
	memset(&bss_info, 0, sizeof(bss_info));
	woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
	if (bss_info.bss_mode != MLAN_BSS_MODE_IBSS ||
	    bss_info.media_connected == MTRUE) {
		PRINTM(MERROR, "STA is connected or not in IBSS mode.\n");
		ret = -EOPNOTSUPP;
		goto done;
	}

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	copy_len = data_length;

	if (data_length > 0) {
		if (data_length >= sizeof(buf)) {
			PRINTM(MERROR, "Too many arguments\n");
			ret = -EINVAL;
			goto done;
		}
		if (copy_from_user(buf, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		if (data_length == 1) {
			/* Get Adhoc AES Key */
			req->req_id = MLAN_IOCTL_SEC_CFG;
			req->action = MLAN_ACT_GET;
			sec = (mlan_ds_sec_cfg *)req->pbuf;
			sec->sub_command = MLAN_OID_SEC_CFG_ENCRYPT_KEY;
			sec->param.encrypt_key.key_len = AES_KEY_LEN;
			sec->param.encrypt_key.key_index =
				MLAN_KEY_INDEX_UNICAST;
			status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
			if (status != MLAN_STATUS_SUCCESS) {
				ret = -EFAULT;
				goto done;
			}

			memcpy(key_hex, sec->param.encrypt_key.key_material,
			       sizeof(key_hex));
			HEXDUMP("Adhoc AES Key (HEX)", key_hex,
				sizeof(key_hex));

			wrq->u.data.length = sizeof(key_ascii) + 1;

			tmp = key_ascii;
			for (i = 0; i < sizeof(key_hex); i++)
				tmp += sprintf((char *)tmp, "%02x", key_hex[i]);
		} else if (data_length >= 2) {
			/* Parse the buf to get the cmd_action */
			action = woal_atox(buf);
			if (action < 1 || action > 2) {
				PRINTM(MERROR, "Invalid action argument %d\n",
				       action);
				ret = -EINVAL;
				goto done;
			}

			req->req_id = MLAN_IOCTL_SEC_CFG;
			req->action = MLAN_ACT_SET;
			sec = (mlan_ds_sec_cfg *)req->pbuf;
			sec->sub_command = MLAN_OID_SEC_CFG_ENCRYPT_KEY;

			if (action == 1) {
				/* Set Adhoc AES Key  */
				memcpy(key_ascii, &buf[2], sizeof(key_ascii));
				woal_ascii2hex(key_hex, (char *)key_ascii,
					       sizeof(key_hex));
				HEXDUMP("Adhoc AES Key (HEX)", key_hex,
					sizeof(key_hex));

				sec->param.encrypt_key.key_len = AES_KEY_LEN;
				sec->param.encrypt_key.key_index =
					MLAN_KEY_INDEX_UNICAST;
				sec->param.encrypt_key.key_flags =
					KEY_FLAG_SET_TX_KEY |
					KEY_FLAG_GROUP_KEY;
				memcpy(sec->param.encrypt_key.mac_addr,
				       (u8 *)bcast_addr, ETH_ALEN);
				memcpy(sec->param.encrypt_key.key_material,
				       key_hex, sec->param.encrypt_key.key_len);

				status = woal_request_ioctl(priv, req,
							    MOAL_IOCTL_WAIT);
				if (status != MLAN_STATUS_SUCCESS) {
					ret = -EFAULT;
					goto done;
				}
			} else {
				/* Clear Adhoc AES Key */
				sec->param.encrypt_key.key_len = AES_KEY_LEN;
				sec->param.encrypt_key.key_index =
					MLAN_KEY_INDEX_UNICAST;
				sec->param.encrypt_key.key_flags =
					KEY_FLAG_REMOVE_KEY;
				memcpy(sec->param.encrypt_key.mac_addr,
				       (u8 *)bcast_addr, ETH_ALEN);
				memset(sec->param.encrypt_key.key_material, 0,
				       sizeof(sec->param.encrypt_key.
					      key_material));

				status = woal_request_ioctl(priv, req,
							    MOAL_IOCTL_WAIT);
				if (status != MLAN_STATUS_SUCCESS) {
					ret = -EFAULT;
					goto done;
				}
			}
		}

		HEXDUMP("Adhoc AES Key (ASCII)", key_ascii, sizeof(key_ascii));
		wrq->u.data.length = sizeof(key_ascii);
		if (wrq->u.data.pointer) {
			if (copy_to_user(wrq->u.data.pointer, &key_ascii,
					 sizeof(key_ascii))) {
				PRINTM(MERROR, "copy_to_user failed\n");
				ret = -EFAULT;
				goto done;
			}
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief arpfilter ioctl function
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *  @return         0 --success, otherwise fail
 */
static int
woal_arp_filter(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_misc_cfg *misc = NULL;
	mlan_ioctl_req *req = NULL;
	int data_length = wrq->u.data.length, copy_len;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	copy_len =
		MIN(sizeof(misc->param.gen_ie.ie_data),
		    sizeof(int) * data_length);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	misc = (mlan_ds_misc_cfg *)req->pbuf;
	misc->sub_command = MLAN_OID_MISC_GEN_IE;
	req->req_id = MLAN_IOCTL_MISC_CFG;
	req->action = MLAN_ACT_SET;
	misc->param.gen_ie.type = MLAN_IE_TYPE_ARP_FILTER;
	misc->param.gen_ie.len = data_length;

	/* get the whole command from user */
	if (copy_from_user
	    (misc->param.gen_ie.ie_data, wrq->u.data.pointer, copy_len)) {
		PRINTM(MERROR, "copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/get IP address
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *  @return             0 --success, otherwise fail
 */
static int
woal_set_get_ip_addr(moal_private *priv, struct iwreq *wrq)
{
	char buf[IPADDR_MAX_BUF];
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
	int ret = 0, op_code = 0, data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	memset(buf, 0, IPADDR_MAX_BUF);
	ioctl_req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (ioctl_req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	misc = (mlan_ds_misc_cfg *)ioctl_req->pbuf;

	if (data_length <= 1) {	/* GET */
		ioctl_req->action = MLAN_ACT_GET;
	} else {
		if (copy_from_user(buf, wrq->u.data.pointer,
				   MIN(IPADDR_MAX_BUF - 1, data_length))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		/* Make sure we have the operation argument */
		if (data_length > 2 && buf[1] != ';') {
			PRINTM(MERROR,
			       "No operation argument. Separate with ';'\n");
			ret = -EINVAL;
			goto done;
		} else {
			buf[1] = '\0';
		}
		ioctl_req->action = MLAN_ACT_SET;
		/* only one IP is supported in current firmware */
		memset(misc->param.ipaddr_cfg.ip_addr[0], 0, IPADDR_LEN);
		in4_pton(&buf[2], MIN((IPADDR_MAX_BUF - 3), (data_length - 2)),
			 misc->param.ipaddr_cfg.ip_addr[0], ' ', NULL);
		/* only one IP is supported in current firmware */
		misc->param.ipaddr_cfg.ip_addr_num = 1;
		misc->param.ipaddr_cfg.ip_addr_type = IPADDR_TYPE_IPV4;
	}
	if (woal_atoi(&op_code, buf) != MLAN_STATUS_SUCCESS) {
		ret = -EINVAL;
		goto done;
	}
	misc->param.ipaddr_cfg.op_code = (t_u32)op_code;
	ioctl_req->req_id = MLAN_IOCTL_MISC_CFG;
	misc->sub_command = MLAN_OID_MISC_IP_ADDR;

	/* Send ioctl to mlan */
	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (ioctl_req->action == MLAN_ACT_GET) {
		snprintf(buf, IPADDR_MAX_BUF, "%d;%d.%d.%d.%d",
			 misc->param.ipaddr_cfg.op_code,
			 misc->param.ipaddr_cfg.ip_addr[0][0],
			 misc->param.ipaddr_cfg.ip_addr[0][1],
			 misc->param.ipaddr_cfg.ip_addr[0][2],
			 misc->param.ipaddr_cfg.ip_addr[0][3]);
		wrq->u.data.length = IPADDR_MAX_BUF;
		if (copy_to_user(wrq->u.data.pointer, buf, IPADDR_MAX_BUF)) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Transmit beamforming capabilities
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 -- success, otherwise fail
 */
static int
woal_tx_bf_cap_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0, data_length = wrq->u.data.length;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11n_cfg *bf_cfg = NULL;
	int bf_cap = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (data_length > 1) {
		PRINTM(MERROR, "Invalid no of arguments!\n");
		ret = -EINVAL;
		goto done;
	}
	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	bf_cfg = (mlan_ds_11n_cfg *)req->pbuf;
	req->req_id = MLAN_IOCTL_11N_CFG;
	bf_cfg->sub_command = MLAN_OID_11N_CFG_TX_BF_CAP;
	req->action = MLAN_ACT_GET;
	if (data_length) {	/* SET */
		if (copy_from_user(&bf_cap, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		bf_cfg->param.tx_bf_cap = bf_cap;
		req->action = MLAN_ACT_SET;
	}

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	bf_cap = bf_cfg->param.tx_bf_cap;
	if (copy_to_user(wrq->u.data.pointer, &bf_cap, sizeof(int))) {
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 1;

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/* Maximum input output characters in group WOAL_SET_GET_256_CHAR */
#define MAX_IN_OUT_CHAR     256
/** Tx BF Global conf argument index */
#define BF_ENABLE_PARAM     1
#define SOUND_ENABLE_PARAM  2
#define FB_TYPE_PARAM       3
#define SNR_THRESHOLD_PARAM 4
#define SOUND_INTVL_PARAM   5
#define BF_MODE_PARAM       6
#define MAX_TX_BF_GLOBAL_ARGS   6
#define BF_CFG_ACT_GET      0
#define BF_CFG_ACT_SET      1

/**
 *  @brief Set/Get Transmit beamforming configuration
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 -- success, otherwise fail
 */
static int
woal_tx_bf_cfg_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0, data_length = wrq->u.data.length;
	int bf_action = 0, interval = 0;
	int snr = 0, i, tmp_val;
	t_u8 buf[MAX_IN_OUT_CHAR], char_count = 0;
	t_u8 *str, *token, *pos;
	t_u16 action = 0;

	mlan_ds_11n_tx_bf_cfg bf_cfg;
	mlan_trigger_sound_args *bf_sound = NULL;
	mlan_tx_bf_peer_args *tx_bf_peer = NULL;
	mlan_snr_thr_args *bf_snr = NULL;
	mlan_bf_periodicity_args *bf_periodicity = NULL;
	mlan_bf_global_cfg_args *bf_global = NULL;

	ENTER();

	memset(&bf_cfg, 0, sizeof(bf_cfg));
	/* Pointer to corresponding buffer */
	bf_sound = bf_cfg.body.bf_sound;
	tx_bf_peer = bf_cfg.body.tx_bf_peer;
	bf_snr = bf_cfg.body.bf_snr;
	bf_periodicity = bf_cfg.body.bf_periodicity;
	bf_global = &bf_cfg.body.bf_global_cfg;

	/* Total characters in buffer */
	char_count = data_length - 1;
	memset(buf, 0, sizeof(buf));
	if (char_count) {
		if (data_length > sizeof(buf)) {
			PRINTM(MERROR, "Too many arguments\n");
			ret = -EINVAL;
			goto done;
		}
		if (copy_from_user(buf, wrq->u.data.pointer, data_length)) {
			PRINTM(MERROR, "copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		if (char_count > 1 && buf[1] != ';') {
			PRINTM(MERROR,
			       "No action argument. Separate with ';'\n");
			ret = -EINVAL;
			goto done;
		}
		/* Replace ';' with NULL in the string to separate args */
		for (i = 0; i < char_count; i++) {
			if (buf[i] == ';')
				buf[i] = '\0';
		}
		/* The first byte represents the beamforming action */
		if (woal_atoi(&bf_action, &buf[0]) != MLAN_STATUS_SUCCESS) {
			ret = -EINVAL;
			goto done;
		}
		switch (bf_action) {
		case BF_GLOBAL_CONFIGURATION:
			if (char_count == 1) {
				action = MLAN_ACT_GET;
				bf_cfg.action = BF_CFG_ACT_GET;
			} else {
				action = MLAN_ACT_SET;
				bf_cfg.action = BF_CFG_ACT_SET;
				/* Eliminate action field */
				token = &buf[2];
				for (i = 1, str = &buf[2]; token != NULL; i++) {
					token = strstr(str, " ");
					pos = str;
					if (token != NULL) {
						*token = '\0';
						str = token + 1;
					}
					woal_atoi(&tmp_val, pos);
					switch (i) {
					case BF_ENABLE_PARAM:
						bf_global->bf_enbl =
							(t_u8)tmp_val;
						break;
					case SOUND_ENABLE_PARAM:
						bf_global->sounding_enbl =
							(t_u8)tmp_val;
						break;
					case FB_TYPE_PARAM:
						bf_global->fb_type =
							(t_u8)tmp_val;
						break;
					case SNR_THRESHOLD_PARAM:
						bf_global->snr_threshold =
							(t_u8)tmp_val;
						break;
					case SOUND_INTVL_PARAM:
						bf_global->sounding_interval =
							(t_u16)tmp_val;
						break;
					case BF_MODE_PARAM:
						bf_global->bf_mode =
							(t_u8)tmp_val;
						break;
					default:
						PRINTM(MERROR,
						       "Invalid Argument\n");
						ret = -EINVAL;
						goto done;
					}
				}
			}
			break;
		case TRIGGER_SOUNDING_FOR_PEER:
			/*
			 * First arg  = 2   BfAction
			 * Second arg = 17  MAC "00:50:43:20:BF:64"
			 */
			if (char_count != 19) {
				PRINTM(MERROR, "Invalid argument\n");
				ret = -EINVAL;
				goto done;
			}
			woal_mac2u8(bf_sound->peer_mac, &buf[2]);
			action = MLAN_ACT_SET;
			bf_cfg.action = BF_CFG_ACT_SET;
			break;
		case SET_GET_BF_PERIODICITY:
			/*
			 * First arg  = 2   BfAction
			 * Second arg = 18  MAC "00:50:43:20:BF:64;"
			 * Third arg =  1  (min char)  TX BF interval
			 *              10 (max char)  u32 maximum value 4294967295
			 */
			if (char_count < 19 || char_count > 30) {
				PRINTM(MERROR, "Invalid argument\n");
				ret = -EINVAL;
				goto done;
			}

			woal_mac2u8(bf_periodicity->peer_mac, &buf[2]);
			if (char_count == 19) {
				action = MLAN_ACT_GET;
				bf_cfg.action = BF_CFG_ACT_GET;
			} else {
				action = MLAN_ACT_SET;
				bf_cfg.action = BF_CFG_ACT_SET;
				if (woal_atoi(&interval, &buf[20]) !=
				    MLAN_STATUS_SUCCESS) {
					ret = -EINVAL;
					goto done;
				}
				bf_periodicity->interval = interval;
			}
			break;
		case TX_BF_FOR_PEER_ENBL:
			/*
			 * Handle only SET operation here
			 * First arg  = 2   BfAction
			 * Second arg = 18  MAC "00:50:43:20:BF:64;"
			 * Third arg  = 2   enable/disable bf
			 * Fourth arg = 2   enable/disable sounding
			 * Fifth arg  = 1   FB Type
			 */
			if (char_count != 25 && char_count != 1) {
				PRINTM(MERROR, "Invalid argument\n");
				ret = -EINVAL;
				goto done;
			}
			if (char_count == 1) {
				action = MLAN_ACT_GET;
				bf_cfg.action = BF_CFG_ACT_GET;
			} else {
				woal_mac2u8(tx_bf_peer->peer_mac, &buf[2]);
				woal_atoi(&tmp_val, &buf[20]);
				tx_bf_peer->bf_enbl = (t_u8)tmp_val;
				woal_atoi(&tmp_val, &buf[22]);
				tx_bf_peer->sounding_enbl = (t_u8)tmp_val;
				woal_atoi(&tmp_val, &buf[24]);
				tx_bf_peer->fb_type = (t_u8)tmp_val;
				action = MLAN_ACT_SET;
				bf_cfg.action = BF_CFG_ACT_SET;
			}
			break;
		case SET_SNR_THR_PEER:
			/*
			 * First arg  = 2   BfAction
			 * Second arg = 18  MAC "00:50:43:20:BF:64;"
			 * Third arg  = 1/2 SNR u8 - can be 1/2 charerters
			 */
			if (char_count != 1 &&
			    !(char_count == 21 || char_count == 22)) {
				PRINTM(MERROR, "Invalid argument\n");
				ret = -EINVAL;
				goto done;
			}
			if (char_count == 1) {
				action = MLAN_ACT_GET;
				bf_cfg.action = BF_CFG_ACT_GET;
			} else {
				woal_mac2u8(bf_snr->peer_mac, &buf[2]);
				if (woal_atoi(&snr, &buf[20]) !=
				    MLAN_STATUS_SUCCESS) {
					ret = -EINVAL;
					goto done;
				}
				bf_snr->snr = snr;
				action = MLAN_ACT_SET;
				bf_cfg.action = BF_CFG_ACT_SET;
			}
			break;
		default:
			ret = -EINVAL;
			goto done;
		}

		/* Save the value */
		bf_cfg.bf_action = bf_action;
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_get_tx_bf_cfg(priv, action, &bf_cfg)) {
			ret = -EFAULT;
			goto done;
		}
	} else {
		ret = -EINVAL;
		goto done;
	}

	if (action == MLAN_ACT_GET) {
		data_length = 0;
		memset(buf, 0, sizeof(buf));
		switch (bf_action) {
		case BF_GLOBAL_CONFIGURATION:
			data_length +=
				sprintf(buf + data_length, "%d ",
					(int)bf_global->bf_enbl);
			data_length +=
				sprintf(buf + data_length, "%d ",
					(int)bf_global->sounding_enbl);
			data_length +=
				sprintf(buf + data_length, "%d ",
					(int)bf_global->fb_type);
			data_length +=
				sprintf(buf + data_length, "%d ",
					(int)bf_global->snr_threshold);
			data_length +=
				sprintf(buf + data_length, "%d ",
					(int)bf_global->sounding_interval);
			data_length +=
				sprintf(buf + data_length, "%d ",
					(int)bf_global->bf_mode);
			break;
		case SET_GET_BF_PERIODICITY:
			data_length += sprintf(buf + data_length,
					       "%02x:%02x:%02x:%02x:%02x:%02x",
					       bf_periodicity->peer_mac[0],
					       bf_periodicity->peer_mac[1],
					       bf_periodicity->peer_mac[2],
					       bf_periodicity->peer_mac[3],
					       bf_periodicity->peer_mac[4],
					       bf_periodicity->peer_mac[5]);
			data_length += sprintf(buf + data_length, "%c", ' ');
			data_length +=
				sprintf(buf + data_length, "%d",
					bf_periodicity->interval);
			break;
		case TX_BF_FOR_PEER_ENBL:
			for (i = 0; i < bf_cfg.no_of_peers; i++) {
				data_length += sprintf(buf + data_length,
						       "%02x:%02x:%02x:%02x:%02x:%02x",
						       tx_bf_peer->peer_mac[0],
						       tx_bf_peer->peer_mac[1],
						       tx_bf_peer->peer_mac[2],
						       tx_bf_peer->peer_mac[3],
						       tx_bf_peer->peer_mac[4],
						       tx_bf_peer->peer_mac[5]);
				data_length +=
					sprintf(buf + data_length, "%c", ' ');
				data_length +=
					sprintf(buf + data_length, "%d;",
						tx_bf_peer->bf_enbl);
				data_length +=
					sprintf(buf + data_length, "%d;",
						tx_bf_peer->sounding_enbl);
				data_length +=
					sprintf(buf + data_length, "%d ",
						tx_bf_peer->fb_type);
				tx_bf_peer++;
			}
			break;
		case SET_SNR_THR_PEER:
			for (i = 0; i < bf_cfg.no_of_peers; i++) {
				data_length += sprintf(buf + data_length,
						       "%02x:%02x:%02x:%02x:%02x:%02x",
						       bf_snr->peer_mac[0],
						       bf_snr->peer_mac[1],
						       bf_snr->peer_mac[2],
						       bf_snr->peer_mac[3],
						       bf_snr->peer_mac[4],
						       bf_snr->peer_mac[5]);
				data_length +=
					sprintf(buf + data_length, "%c", ';');
				data_length +=
					sprintf(buf + data_length, "%d",
						bf_snr->snr);
				data_length +=
					sprintf(buf + data_length, "%c", ' ');
				bf_snr++;
			}
			break;
		}
		buf[data_length] = '\0';
	}

	wrq->u.data.length = data_length;
	if (copy_to_user(wrq->u.data.pointer, buf, wrq->u.data.length)) {
		ret = -EFAULT;
		goto done;
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief Retrieve the scan response/beacon table
 *
 *  @param wrq          A pointer to iwreq structure
 *  @param scan_resp    A pointer to mlan_scan_resp structure
 *  @param scan_start   argument
 *
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static int
moal_ret_get_scan_table_ioctl(struct iwreq *wrq,
			      mlan_scan_resp *scan_resp, t_u32 scan_start)
{
	pBSSDescriptor_t pbss_desc, scan_table;
	wlan_ioctl_get_scan_table_info *prsp_info;
	int ret_code;
	int ret_len;
	int space_left;
	t_u8 *pcurrent;
	t_u8 *pbuffer_end;
	t_u32 num_scans_done;

	ENTER();

	num_scans_done = 0;
	ret_code = MLAN_STATUS_SUCCESS;

	prsp_info = (wlan_ioctl_get_scan_table_info *)wrq->u.data.pointer;
	pcurrent = (t_u8 *)prsp_info->scan_table_entry_buf;

	pbuffer_end = wrq->u.data.pointer + wrq->u.data.length - 1;
	space_left = pbuffer_end - pcurrent;
	scan_table = (BSSDescriptor_t *)(scan_resp->pscan_table);

	PRINTM(MINFO, "GetScanTable: scan_start req = %d\n", scan_start);
	PRINTM(MINFO, "GetScanTable: length avail = %d\n", wrq->u.data.length);

	if (!scan_start) {
		PRINTM(MINFO, "GetScanTable: get current BSS Descriptor\n");

		/* Use to get current association saved descriptor */
		pbss_desc = scan_table;

		ret_code = wlan_get_scan_table_ret_entry(pbss_desc,
							 &pcurrent,
							 &space_left);

		if (ret_code == MLAN_STATUS_SUCCESS)
			num_scans_done = 1;
	} else {
		scan_start--;

		while (space_left
		       && (scan_start + num_scans_done <
			   scan_resp->num_in_scan_table)
		       && (ret_code == MLAN_STATUS_SUCCESS)) {

			pbss_desc =
				(scan_table + (scan_start + num_scans_done));

			PRINTM(MINFO,
			       "GetScanTable: get current BSS Descriptor [%d]\n",
			       scan_start + num_scans_done);

			ret_code = wlan_get_scan_table_ret_entry(pbss_desc,
								 &pcurrent,
								 &space_left);

			if (ret_code == MLAN_STATUS_SUCCESS)
				num_scans_done++;
		}
	}

	prsp_info->scan_number = num_scans_done;
	ret_len = pcurrent - (t_u8 *)wrq->u.data.pointer;

	wrq->u.data.length = ret_len;

	/* Return ret_code (EFAULT or E2BIG) in the case where no scan results were
	 *   successfully encoded.
	 */
	LEAVE();
	return num_scans_done ? MLAN_STATUS_SUCCESS : ret_code;
}

/**
 *  @brief Get scan table ioctl
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING -- success, otherwise fail
 */
static mlan_status
woal_get_scan_table_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_scan *scan = NULL;
	int scan_start = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_scan));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	scan = (mlan_ds_scan *)req->pbuf;
	req->req_id = MLAN_IOCTL_SCAN;
	req->action = MLAN_ACT_GET;

	/* get the whole command from user */
	if (copy_from_user
	    (&scan_start, wrq->u.data.pointer, sizeof(scan_start))) {
		PRINTM(MERROR, "copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}
	if (scan_start > 0)
		scan->sub_command = MLAN_OID_SCAN_NORMAL;
	else
		scan->sub_command = MLAN_OID_SCAN_GET_CURRENT_BSS;
	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status == MLAN_STATUS_SUCCESS) {
		status = moal_ret_get_scan_table_ioctl(wrq,
						       &scan->param.scan_resp,
						       scan_start);
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return status;
}

/**
 *  @brief Set user scan ext -- Async mode, without wait
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 -- success, otherwise fail
 */
static int
woal_set_user_scan_ext_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	wlan_user_scan_cfg scan_req;
	ENTER();
	memset(&scan_req, 0x00, sizeof(scan_req));
	if (copy_from_user
	    (&scan_req, wrq->u.data.pointer,
	     MIN(wrq->u.data.length, sizeof(scan_req)))) {
		PRINTM(MINFO, "Copy from user failed\n");
		LEAVE();
		return -EFAULT;
	}
	if (MLAN_STATUS_FAILURE == woal_do_scan(priv, &scan_req))
		ret = -EFAULT;
	LEAVE();
	return ret;
}

/**
 *  @brief Set user scan
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING -- success, otherwise fail
 */
static mlan_status
woal_set_user_scan_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_scan *scan = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	union iwreq_data wrqu;
	moal_handle *handle = priv->phandle;

	ENTER();

	if (handle->scan_pending_on_block == MTRUE) {
		PRINTM(MINFO, "scan already in processing...\n");
		LEAVE();
		return ret;
	}
	if (MOAL_ACQ_SEMAPHORE_BLOCK(&handle->async_sem)) {
		PRINTM(MERROR, "Acquire semaphore error, request_scan\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	handle->scan_pending_on_block = MTRUE;
	handle->scan_priv = priv;

	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_scan) +
					wrq->u.data.length);
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	scan = (mlan_ds_scan *)req->pbuf;
	scan->sub_command = MLAN_OID_SCAN_USER_CONFIG;
	req->req_id = MLAN_IOCTL_SCAN;
	req->action = MLAN_ACT_SET;

	if (copy_from_user(scan->param.user_scan.scan_cfg_buf,
			   wrq->u.data.pointer, wrq->u.data.length)) {
		PRINTM(MINFO, "Copy from user failed\n");
		LEAVE();
		return -EFAULT;
	}

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status == MLAN_STATUS_SUCCESS) {
		memset(&wrqu, 0, sizeof(union iwreq_data));
		wireless_send_event(priv->netdev, SIOCGIWSCAN, &wrqu, NULL);
	}
	handle->scan_pending_on_block = MFALSE;
	handle->scan_priv = NULL;
	MOAL_REL_SEMAPHORE(&handle->async_sem);

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return status;
}

/**
 *  @brief Cmd52 read/write register
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static int
woal_cmd52rdwr_ioctl(moal_private *priv, struct iwreq *wrq)
{
	t_u8 rw = 0, func, data = 0;
	int buf[3], reg, ret = MLAN_STATUS_SUCCESS;
	int data_length = wrq->u.data.length;

	ENTER();

	if (data_length < 2 || data_length > 3) {
		PRINTM(MERROR, "Invalid number of arguments\n");
		ret = -EINVAL;
		goto done;
	}

	if (copy_from_user(buf, wrq->u.data.pointer, sizeof(int) * data_length)) {
		PRINTM(MERROR, "Copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}

	func = (t_u8)buf[0];
	if (func > 7) {
		PRINTM(MERROR, "Invalid function number!\n");
		ret = -EINVAL;
		goto done;
	}
	reg = (t_u32)buf[1];
	if (data_length == 2) {
		rw = 0;		/* CMD52 read */
		PRINTM(MINFO, "Cmd52 read, func=%d, reg=0x%08X\n", func, reg);
	}
	if (data_length == 3) {
		rw = 1;		/* CMD52 write */
		data = (t_u8)buf[2];
		PRINTM(MINFO, "Cmd52 write, func=%d, reg=0x%08X, data=0x%02X\n",
		       func, reg, data);
	}

	if (!rw) {
		sdio_claim_host(((struct sdio_mmc_card *)priv->phandle->card)->
				func);
		if (func)
			data = sdio_readb(((struct sdio_mmc_card *)priv->
					   phandle->card)->func, reg, &ret);
		else
			data = sdio_f0_readb(((struct sdio_mmc_card *)priv->
					      phandle->card)->func, reg, &ret);
		sdio_release_host(((struct sdio_mmc_card *)priv->phandle->
				   card)->func);
		if (ret) {
			PRINTM(MERROR,
			       "sdio_readb: reading register 0x%X failed\n",
			       reg);
			goto done;
		}
	} else {
		sdio_claim_host(((struct sdio_mmc_card *)priv->phandle->card)->
				func);
		if (func)
			sdio_writeb(((struct sdio_mmc_card *)priv->phandle->
				     card)->func, data, reg, &ret);
		else
			sdio_f0_writeb(((struct sdio_mmc_card *)priv->phandle->
					card)->func, data, reg, &ret);
		sdio_release_host(((struct sdio_mmc_card *)priv->phandle->
				   card)->func);
		if (ret) {
			PRINTM(MERROR,
			       "sdio_writeb: writing register 0x%X failed\n",
			       reg);
			goto done;
		}
	}

	buf[0] = data;
	wrq->u.data.length = 1;
	if (copy_to_user(wrq->u.data.pointer, buf, sizeof(int))) {
		PRINTM(MERROR, "Copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief Cmd53 read/write register
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wrq          A pointer to iwreq structure
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static int
woal_cmd53rdwr_ioctl(moal_private *priv, struct iwreq *wrq)
{
	t_u8 *buf = NULL;
	t_u8 rw, func, mode;
	t_u16 blklen = 0, blknum = 0;
	int reg = 0, pattern_len = 0, pos = 0, ret = MLAN_STATUS_SUCCESS;
	t_u32 total_len = 0;
	t_u8 *data = NULL;

	ENTER();

	buf = kmalloc(WOAL_2K_BYTES, GFP_KERNEL);
	if (!buf) {
		PRINTM(MERROR, "Cannot allocate buffer for command!\n");
		ret = -EFAULT;
		goto done;
	}
	data = kmalloc(WOAL_2K_BYTES, GFP_KERNEL);
	if (!data) {
		PRINTM(MERROR, "Cannot allocate buffer for command!\n");
		ret = -EFAULT;
		goto done;
	}
	if (wrq->u.data.length > WOAL_2K_BYTES) {
		PRINTM(MERROR, "Data lengh is too large!\n");
		ret = -EINVAL;
		goto done;
	}
	if (copy_from_user(buf, wrq->u.data.pointer, wrq->u.data.length)) {
		PRINTM(MINFO, "Copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}

	rw = buf[0];		/* read/write (0/1) */
	func = buf[1];		/* func (0/1/2) */
	reg = buf[5];		/* address */
	reg = (reg << 8) + buf[4];
	reg = (reg << 8) + buf[3];
	reg = (reg << 8) + buf[2];
	mode = buf[6];		/* byte mode/block mode (0/1) */
	blklen = buf[8];	/* block size */
	blklen = (blklen << 8) + buf[7];
	blknum = buf[10];	/* block number or byte number */
	blknum = (blknum << 8) + buf[9];

	if (mode != BYTE_MODE)
		mode = BLOCK_MODE;
	total_len = (mode == BLOCK_MODE) ? blknum * blklen : blknum;
	if (total_len > WOAL_2K_BYTES) {
		PRINTM(MERROR, "Total data length is too large!\n");
		ret = -EINVAL;
		goto done;
	}
	PRINTM(MINFO,
	       "CMD53 read/write, func = %d, addr = %#x, mode = %d, block size = %d, block(byte) number = %d\n",
	       func, reg, mode, blklen, blknum);

	if (!rw) {
		sdio_claim_host(((struct sdio_mmc_card *)priv->phandle->card)->
				func);
		if (sdio_readsb
		    (((struct sdio_mmc_card *)priv->phandle->card)->func, data,
		     reg, total_len))
			PRINTM(MERROR,
			       "sdio_readsb: reading memory 0x%x failed\n",
			       reg);
		sdio_release_host(((struct sdio_mmc_card *)priv->phandle->
				   card)->func);

		if (copy_to_user(wrq->u.data.pointer, data, total_len)) {
			PRINTM(MINFO, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = total_len;
	} else {
		pattern_len = wrq->u.data.length - 11;
		if (pattern_len > total_len)
			pattern_len = total_len;
		memset(data, 0, WOAL_2K_BYTES);

		/* Copy/duplicate the pattern to data buffer */
		for (pos = 0; pos < total_len; pos++)
			data[pos] = buf[11 + (pos % pattern_len)];

		sdio_claim_host(((struct sdio_mmc_card *)priv->phandle->card)->
				func);
		if (sdio_writesb
		    (((struct sdio_mmc_card *)priv->phandle->card)->func, reg,
		     data, total_len))
			PRINTM(MERROR,
			       "sdio_writesb: writing memory 0x%x failed\n",
			       reg);
		sdio_release_host(((struct sdio_mmc_card *)priv->phandle->
				   card)->func);
	}

done:
	kfree(buf);
	kfree(data);
	LEAVE();
	return ret;
}

#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
/**
 * @brief Set SDIO Multi-point aggregation control parameters
 *
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 *
 * @return         0/MLAN_STATUS_PENDING --success, otherwise fail
 */
static int
woal_do_sdio_mpa_ctrl(moal_private *priv, struct iwreq *wrq)
{
	int data[6], data_length = wrq->u.data.length, copy_len;
	int ret = 0;
	mlan_ds_misc_cfg *misc = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (sizeof(int) * wrq->u.data.length > sizeof(data)) {
		PRINTM(MERROR, "Too many arguments\n");
		ret = -EINVAL;
		goto done;
	}
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	misc = (mlan_ds_misc_cfg *)req->pbuf;
	memset(misc, 0, sizeof(mlan_ds_misc_cfg));

	misc->sub_command = MLAN_OID_MISC_SDIO_MPA_CTRL;
	req->req_id = MLAN_IOCTL_MISC_CFG;

	/* Get the values first, then modify these values if
	 * user had modified them */

	req->action = MLAN_ACT_GET;
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "woal_request_ioctl returned %d\n", ret);
		ret = -EFAULT;
		goto done;
	}

	if (data_length == 0) {
		data[0] = misc->param.mpa_ctrl.tx_enable;
		data[1] = misc->param.mpa_ctrl.rx_enable;
		data[2] = misc->param.mpa_ctrl.tx_buf_size;
		data[3] = misc->param.mpa_ctrl.rx_buf_size;
		data[4] = misc->param.mpa_ctrl.tx_max_ports;
		data[5] = misc->param.mpa_ctrl.rx_max_ports;

		PRINTM(MINFO, "Get Param: %d %d %d %d %d %d\n", data[0],
		       data[1], data[2], data[3], data[4], data[5]);

		if (copy_to_user(wrq->u.data.pointer, data, sizeof(data))) {
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = ARRAY_SIZE(data);
		goto done;
	}

	if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
		PRINTM(MINFO, "Copy from user failed\n");
		ret = -EFAULT;
		goto done;
	}

	switch (data_length) {
	case 6:
		misc->param.mpa_ctrl.rx_max_ports = data[5];
	case 5:
		misc->param.mpa_ctrl.tx_max_ports = data[4];
	case 4:
		misc->param.mpa_ctrl.rx_buf_size = data[3];
	case 3:
		misc->param.mpa_ctrl.tx_buf_size = data[2];
	case 2:
		misc->param.mpa_ctrl.rx_enable = data[1];
	case 1:
		/* Set cmd */
		req->action = MLAN_ACT_SET;

		PRINTM(MINFO, "Set Param: %d %d %d %d %d %d\n", data[0],
		       data[1], data[2], data[3], data[4], data[5]);

		misc->param.mpa_ctrl.tx_enable = data[0];
		break;
	default:
		PRINTM(MERROR, "Default case error\n");
		ret = -EINVAL;
		goto done;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "woal_request_ioctl returned %d\n", ret);
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}
#endif /* SDIO_MULTI_PORT_TX_AGGR || SDIO_MULTI_PORT_RX_AGGR */

/**
 * @brief Set/Get scan configuration parameters
 *
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 *
 * @return         0 --success, otherwise fail
 */
static int
woal_set_get_scan_cfg(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	int arg_len = 7;
	int data[arg_len], copy_len;
	mlan_ds_scan *scan = NULL;
	mlan_ioctl_req *req = NULL;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_scan));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	if (data_length > arg_len) {
		ret = -EINVAL;
		goto done;
	}
	scan = (mlan_ds_scan *)req->pbuf;
	scan->sub_command = MLAN_OID_SCAN_CONFIG;
	req->req_id = MLAN_IOCTL_SCAN;
	memset(data, 0, sizeof(data));

	if (data_length) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if ((data[0] < 0) || (data[0] > MLAN_SCAN_TYPE_PASSIVE)) {
			PRINTM(MERROR, "Invalid argument for scan type\n");
			ret = -EINVAL;
			goto done;
		}
		if ((data[1] < 0) || (data[1] > MLAN_SCAN_MODE_ANY)) {
			PRINTM(MERROR, "Invalid argument for scan mode\n");
			ret = -EINVAL;
			goto done;
		}
		if ((data[2] < 0) || (data[2] > MAX_PROBES)) {
			PRINTM(MERROR, "Invalid argument for scan probes\n");
			ret = -EINVAL;
			goto done;
		}
		if (((data[3] < 0) ||
		     (data[3] > MRVDRV_MAX_ACTIVE_SCAN_CHAN_TIME)) ||
		    ((data[4] < 0) ||
		     (data[4] > MRVDRV_MAX_ACTIVE_SCAN_CHAN_TIME)) ||
		    ((data[5] < 0) ||
		     (data[5] > MRVDRV_MAX_PASSIVE_SCAN_CHAN_TIME))) {
			PRINTM(MERROR, "Invalid argument for scan time\n");
			ret = -EINVAL;
			goto done;
		}
		if ((data[6] < 0) || (data[6] > 1)) {
			PRINTM(MERROR, "Invalid argument for extended scan\n");
			ret = -EINVAL;
			goto done;
		}
		req->action = MLAN_ACT_SET;
		memcpy(&scan->param.scan_cfg, data, sizeof(data));
	} else
		req->action = MLAN_ACT_GET;
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	if (!data_length) {
		memcpy(data, &scan->param.scan_cfg, sizeof(data));
		if (copy_to_user(wrq->u.data.pointer, data, sizeof(data))) {
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = ARRAY_SIZE(data);
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Set/Get PS configuration parameters
 *
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 *
 * @return         0 --success, otherwise fail
 */
static int
woal_set_get_ps_cfg(moal_private *priv, struct iwreq *wrq)
{
	int data[7], copy_len, ret = 0;
	mlan_ds_pm_cfg *pm_cfg = NULL;
	mlan_ioctl_req *req = NULL;
	int allowed = 3;
	int i = 3;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	allowed++;		/* For ad-hoc awake period parameter */
	allowed++;		/* For beacon missing timeout parameter */
	allowed += 2;		/* For delay to PS and PS mode parameters */
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_pm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	if (data_length > allowed) {
		ret = -EINVAL;
		goto done;
	}
	pm_cfg = (mlan_ds_pm_cfg *)req->pbuf;
	pm_cfg->sub_command = MLAN_OID_PM_CFG_PS_CFG;
	req->req_id = MLAN_IOCTL_PM_CFG;
	memset(data, 0, sizeof(data));

	if (data_length) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if ((data[0] < PS_NULL_DISABLE)) {
			PRINTM(MERROR,
			       "Invalid argument for PS null interval\n");
			ret = -EINVAL;
			goto done;
		}
		if ((data[1] != MRVDRV_IGNORE_MULTIPLE_DTIM)
		    && (data[1] != MRVDRV_MATCH_CLOSEST_DTIM)
		    && ((data[1] < MRVDRV_MIN_MULTIPLE_DTIM)
			|| (data[1] > MRVDRV_MAX_MULTIPLE_DTIM))) {
			PRINTM(MERROR, "Invalid argument for multiple DTIM\n");
			ret = -EINVAL;
			goto done;
		}

		if ((data[2] < MRVDRV_MIN_LISTEN_INTERVAL)
		    && (data[2] != MRVDRV_LISTEN_INTERVAL_DISABLE)) {
			PRINTM(MERROR,
			       "Invalid argument for listen interval\n");
			ret = -EINVAL;
			goto done;
		}

		if ((data[i] != SPECIAL_ADHOC_AWAKE_PD) &&
		    ((data[i] < MIN_ADHOC_AWAKE_PD) ||
		     (data[i] > MAX_ADHOC_AWAKE_PD))) {
			PRINTM(MERROR,
			       "Invalid argument for adhoc awake period\n");
			ret = -EINVAL;
			goto done;
		}
		i++;
		if ((data[i] != DISABLE_BCN_MISS_TO) &&
		    ((data[i] < MIN_BCN_MISS_TO) ||
		     (data[i] > MAX_BCN_MISS_TO))) {
			PRINTM(MERROR,
			       "Invalid argument for beacon miss timeout\n");
			ret = -EINVAL;
			goto done;
		}
		i++;
		if (data_length < allowed - 1)
			data[i] = DELAY_TO_PS_UNCHANGED;
		else if ((data[i] < MIN_DELAY_TO_PS) ||
			 (data[i] > MAX_DELAY_TO_PS)) {
			PRINTM(MERROR, "Invalid argument for delay to PS\n");
			ret = -EINVAL;
			goto done;
		}
		i++;
		if ((data[i] != PS_MODE_UNCHANGED) && (data[i] != PS_MODE_AUTO)
		    && (data[i] != PS_MODE_POLL) && (data[i] != PS_MODE_NULL)) {
			PRINTM(MERROR, "Invalid argument for PS mode\n");
			ret = -EINVAL;
			goto done;
		}
		i++;
		req->action = MLAN_ACT_SET;
		memcpy(&pm_cfg->param.ps_cfg, data,
		       MIN(sizeof(pm_cfg->param.ps_cfg), sizeof(data)));
	} else
		req->action = MLAN_ACT_GET;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	memcpy(data, &pm_cfg->param.ps_cfg,
	       MIN((sizeof(int) * allowed), sizeof(pm_cfg->param.ps_cfg)));
	if (copy_to_user(wrq->u.data.pointer, data, sizeof(int) * allowed)) {
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = allowed;

	if (req->action == MLAN_ACT_SET) {
		pm_cfg = (mlan_ds_pm_cfg *)req->pbuf;
		pm_cfg->sub_command = MLAN_OID_PM_CFG_IEEE_PS;
		pm_cfg->param.ps_mode = 1;
		req->req_id = MLAN_IOCTL_PM_CFG;
		req->action = MLAN_ACT_SET;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Private IOCTL entry to send an ADDTS TSPEC
 *
 *  Receive a ADDTS command from the application.  The command structure
 *    contains a TSPEC and timeout in milliseconds.  The timeout is performed
 *    in the firmware after the ADDTS command frame is sent.
 *
 *  The TSPEC is received in the API as an opaque block. The firmware will
 *    send the entire data block, including the bytes after the TSPEC.  This
 *    is done to allow extra IEs to be packaged with the TSPEC in the ADDTS
 *    action frame.
 *
 *  The IOCTL structure contains two return fields:
 *    - The firmware command result, which indicates failure and timeouts
 *    - The IEEE Status code which contains the corresponding value from
 *      any ADDTS response frame received.
 *
 *  In addition, the opaque TSPEC data block passed in is replaced with the
 *    TSPEC received in the ADDTS response frame.  In case of failure, the
 *    AP may modify the TSPEC on return and in the case of success, the
 *    medium time is returned as calculated by the AP.  Along with the TSPEC,
 *    any IEs that are sent in the ADDTS response are also returned and can be
 *    parsed using the IOCTL length as an indicator of extra elements.
 *
 *  The return value to the application layer indicates a driver execution
 *    success or failure.  A successful return could still indicate a firmware
 *    failure or AP negotiation failure via the commandResult field copied
 *    back to the application.
 *
 *  @param priv    Pointer to the mlan_private driver data struct
 *  @param wrq     A pointer to iwreq structure containing the
 *                 wlan_ioctl_wmm_addts_req_t struct for this ADDTS request
 *
 *  @return        0 if successful; IOCTL error code otherwise
 */
static int
woal_wmm_addts_req_ioctl(moal_private *priv, struct iwreq *wrq)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_wmm_cfg *cfg = NULL;
	wlan_ioctl_wmm_addts_req_t addts_ioctl;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	req->req_id = MLAN_IOCTL_WMM_CFG;
	cfg = (mlan_ds_wmm_cfg *)req->pbuf;
	cfg->sub_command = MLAN_OID_WMM_CFG_ADDTS;

	memset(&addts_ioctl, 0x00, sizeof(addts_ioctl));

	if (wrq->u.data.length) {
		if (copy_from_user(&addts_ioctl, wrq->u.data.pointer,
				   MIN(wrq->u.data.length,
				       sizeof(addts_ioctl)))) {
			PRINTM(MERROR, "TSPEC: ADDTS copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		cfg->param.addts.timeout = addts_ioctl.timeout_ms;
		cfg->param.addts.ie_data_len = addts_ioctl.ie_data_len;

		if (cfg->param.addts.ie_data_len >
		    sizeof(cfg->param.addts.ie_data)) {
			PRINTM(MERROR, "IE data length too large\n");
			ret = -EFAULT;
			goto done;
		}

		memcpy(cfg->param.addts.ie_data,
		       addts_ioctl.ie_data, cfg->param.addts.ie_data_len);

		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}
		addts_ioctl.cmd_result = cfg->param.addts.result;
		addts_ioctl.ieee_status_code =
			(t_u8)cfg->param.addts.status_code;
		addts_ioctl.ie_data_len = cfg->param.addts.ie_data_len;

		memcpy(addts_ioctl.ie_data,
		       cfg->param.addts.ie_data, cfg->param.addts.ie_data_len);

		wrq->u.data.length = (sizeof(addts_ioctl)
				      - sizeof(addts_ioctl.ie_data)
				      + cfg->param.addts.ie_data_len);

		if (copy_to_user(wrq->u.data.pointer,
				 &addts_ioctl, wrq->u.data.length)) {
			PRINTM(MERROR, "TSPEC: ADDTS copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Private IOCTL entry to send a DELTS TSPEC
 *
 *  Receive a DELTS command from the application.  The command structure
 *    contains a TSPEC and reason code along with space for a command result
 *    to be returned.  The information is packaged is sent to the wlan_cmd.c
 *    firmware command prep and send routines for execution in the firmware.
 *
 *  The reason code is not used for WMM implementations but is indicated in
 *    the 802.11e specification.
 *
 *  The return value to the application layer indicates a driver execution
 *    success or failure.  A successful return could still indicate a firmware
 *    failure via the cmd_result field copied back to the application.
 *
 *  @param priv    Pointer to the mlan_private driver data struct
 *  @param wrq     A pointer to iwreq structure containing the
 *                 wlan_ioctl_wmm_delts_req_t struct for this DELTS request
 *
 *  @return        0 if successful; IOCTL error code otherwise
 */
static int
woal_wmm_delts_req_ioctl(moal_private *priv, struct iwreq *wrq)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_wmm_cfg *cfg = NULL;
	wlan_ioctl_wmm_delts_req_t delts_ioctl;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	req->req_id = MLAN_IOCTL_WMM_CFG;
	cfg = (mlan_ds_wmm_cfg *)req->pbuf;
	cfg->sub_command = MLAN_OID_WMM_CFG_DELTS;

	memset(&delts_ioctl, 0x00, sizeof(delts_ioctl));

	if (wrq->u.data.length) {
		if (copy_from_user(&delts_ioctl, wrq->u.data.pointer,
				   MIN(wrq->u.data.length,
				       sizeof(delts_ioctl)))) {
			PRINTM(MERROR, "TSPEC: DELTS copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		cfg->param.delts.status_code =
			(t_u32)delts_ioctl.ieee_reason_code;
		cfg->param.delts.ie_data_len = (t_u8)delts_ioctl.ie_data_len;

		if ((cfg->param.delts.ie_data_len) >
		    sizeof(cfg->param.delts.ie_data)) {
			PRINTM(MERROR, "IE data length too large\n");
			ret = -EFAULT;
			goto done;
		}

		memcpy(cfg->param.delts.ie_data,
		       delts_ioctl.ie_data, cfg->param.delts.ie_data_len);

		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}

		/* Return the firmware command result back to the application layer */
		delts_ioctl.cmd_result = cfg->param.delts.result;
		wrq->u.data.length = sizeof(delts_ioctl);

		if (copy_to_user(wrq->u.data.pointer,
				 &delts_ioctl, wrq->u.data.length)) {
			PRINTM(MERROR, "TSPEC: DELTS copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Private IOCTL entry to get/set a specified AC Queue's parameters
 *
 *  Receive a AC Queue configuration command which is used to get, set, or
 *    default the parameters associated with a specific WMM AC Queue.
 *
 *  @param priv    Pointer to the mlan_private driver data struct
 *  @param wrq     A pointer to iwreq structure containing the
 *                 wlan_ioctl_wmm_queue_config_t struct
 *
 *  @return        0 if successful; IOCTL error code otherwise
 */
static int
woal_wmm_queue_config_ioctl(moal_private *priv, struct iwreq *wrq)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_wmm_cfg *pwmm = NULL;
	mlan_ds_wmm_queue_config *pqcfg = NULL;
	wlan_ioctl_wmm_queue_config_t qcfg_ioctl;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	req->req_id = MLAN_IOCTL_WMM_CFG;
	pwmm = (mlan_ds_wmm_cfg *)req->pbuf;
	pwmm->sub_command = MLAN_OID_WMM_CFG_QUEUE_CONFIG;

	memset(&qcfg_ioctl, 0x00, sizeof(qcfg_ioctl));
	pqcfg = (mlan_ds_wmm_queue_config *)&pwmm->param.q_cfg;

	if (wrq->u.data.length) {
		if (copy_from_user(&qcfg_ioctl, wrq->u.data.pointer,
				   MIN(wrq->u.data.length,
				       sizeof(qcfg_ioctl)))) {
			PRINTM(MERROR, "QCONFIG: copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		pqcfg->action = qcfg_ioctl.action;
		pqcfg->access_category = qcfg_ioctl.access_category;
		pqcfg->msdu_lifetime_expiry = qcfg_ioctl.msdu_lifetime_expiry;

		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}
		memset(&qcfg_ioctl, 0x00, sizeof(qcfg_ioctl));
		qcfg_ioctl.action = pqcfg->action;
		qcfg_ioctl.access_category = pqcfg->access_category;
		qcfg_ioctl.msdu_lifetime_expiry = pqcfg->msdu_lifetime_expiry;
		wrq->u.data.length = sizeof(qcfg_ioctl);

		if (copy_to_user
		    (wrq->u.data.pointer, &qcfg_ioctl, wrq->u.data.length)) {
			PRINTM(MERROR, "QCONFIG: copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Private IOCTL entry to get and start/stop queue stats on a WMM AC
 *
 *  Receive a AC Queue statistics command from the application for a specific
 *    WMM AC.  The command can:
 *         - Turn stats on
 *         - Turn stats off
 *         - Collect and clear the stats
 *
 *  @param priv    Pointer to the moal_private driver data struct
 *  @param wrq     A pointer to iwreq structure containing the
 *                 wlan_ioctl_wmm_queue_stats_t struct
 *
 *  @return        0 if successful; IOCTL error code otherwise
 */
static int
woal_wmm_queue_stats_ioctl(moal_private *priv, struct iwreq *wrq)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_wmm_cfg *pwmm = NULL;
	mlan_ds_wmm_queue_stats *pqstats = NULL;
	wlan_ioctl_wmm_queue_stats_t qstats_ioctl;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	req->req_id = MLAN_IOCTL_WMM_CFG;
	pwmm = (mlan_ds_wmm_cfg *)req->pbuf;
	pwmm->sub_command = MLAN_OID_WMM_CFG_QUEUE_STATS;

	memset(&qstats_ioctl, 0x00, sizeof(qstats_ioctl));
	pqstats = (mlan_ds_wmm_queue_stats *)&pwmm->param.q_stats;

	if (wrq->u.data.length) {
		if (copy_from_user(&qstats_ioctl, wrq->u.data.pointer,
				   MIN(wrq->u.data.length,
				       sizeof(qstats_ioctl)))) {
			PRINTM(MERROR, "QSTATS: copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		memcpy((void *)pqstats, (void *)&qstats_ioctl,
		       sizeof(qstats_ioctl));
		PRINTM(MINFO, "QSTATS: IOCTL [%d,%d]\n", qstats_ioctl.action,
		       qstats_ioctl.user_priority);

		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}

		memset(&qstats_ioctl, 0x00, sizeof(qstats_ioctl));
		memcpy((void *)&qstats_ioctl, (void *)pqstats,
		       sizeof(qstats_ioctl));
		wrq->u.data.length = sizeof(qstats_ioctl);

		if (copy_to_user
		    (wrq->u.data.pointer, &qstats_ioctl, wrq->u.data.length)) {
			PRINTM(MERROR, "QSTATS: copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Private IOCTL entry to get the status of the WMM queues
 *
 *  Return the following information for each WMM AC:
 *        - WMM IE Acm Required
 *        - Firmware Flow Required
 *        - Firmware Flow Established
 *        - Firmware Queue Enabled
 *        - Firmware Delivery Enabled
 *        - Firmware Trigger Enabled
 *
 *  @param priv    Pointer to the moal_private driver data struct
 *  @param wrq     A pointer to iwreq structure containing the
 *                 wlan_ioctl_wmm_queue_status_t struct for request
 *
 *  @return        0 if successful; IOCTL error code otherwise
 */
static int
woal_wmm_queue_status_ioctl(moal_private *priv, struct iwreq *wrq)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_wmm_cfg *pwmm = NULL;
	wlan_ioctl_wmm_queue_status_t qstatus_ioctl;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	req->req_id = MLAN_IOCTL_WMM_CFG;
	pwmm = (mlan_ds_wmm_cfg *)req->pbuf;
	pwmm->sub_command = MLAN_OID_WMM_CFG_QUEUE_STATUS;

	if (wrq->u.data.length == sizeof(qstatus_ioctl)) {
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}

		memset(&qstatus_ioctl, 0x00, sizeof(qstatus_ioctl));
		memcpy((void *)&qstatus_ioctl, (void *)&pwmm->param.q_status,
		       sizeof(qstatus_ioctl));
		wrq->u.data.length = sizeof(qstatus_ioctl);
	} else {
		wrq->u.data.length = 0;
	}

	if (copy_to_user
	    (wrq->u.data.pointer, &qstatus_ioctl, wrq->u.data.length)) {
		PRINTM(MERROR, "QSTATUS: copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Private IOCTL entry to get the status of the WMM Traffic Streams
 *
 *  @param priv    Pointer to the moal_private driver data struct
 *  @param wrq     A pointer to iwreq structure containing the
 *                 wlan_ioctl_wmm_ts_status_t struct for request
 *
 *  @return        0 if successful; IOCTL error code otherwise
 */
static int
woal_wmm_ts_status_ioctl(moal_private *priv, struct iwreq *wrq)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_wmm_cfg *pwmm = NULL;
	wlan_ioctl_wmm_ts_status_t ts_status_ioctl;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wmm_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	req->req_id = MLAN_IOCTL_WMM_CFG;
	pwmm = (mlan_ds_wmm_cfg *)req->pbuf;
	pwmm->sub_command = MLAN_OID_WMM_CFG_TS_STATUS;

	memset(&ts_status_ioctl, 0x00, sizeof(ts_status_ioctl));

	if (wrq->u.data.length == sizeof(ts_status_ioctl)) {
		if (copy_from_user(&ts_status_ioctl, wrq->u.data.pointer,
				   MIN(wrq->u.data.length,
				       sizeof(ts_status_ioctl)))) {
			PRINTM(MERROR, "TS_STATUS: copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		memset(&pwmm->param.ts_status, 0x00, sizeof(ts_status_ioctl));
		pwmm->param.ts_status.tid = ts_status_ioctl.tid;

		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}

		memset(&ts_status_ioctl, 0x00, sizeof(ts_status_ioctl));
		memcpy((void *)&ts_status_ioctl, (void *)&pwmm->param.ts_status,
		       sizeof(ts_status_ioctl));
		wrq->u.data.length = sizeof(ts_status_ioctl);
	} else {
		wrq->u.data.length = 0;
	}

	if (copy_to_user
	    (wrq->u.data.pointer, &ts_status_ioctl, wrq->u.data.length)) {
		PRINTM(MERROR, "TS_STATUS: copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Private IOCTL entry to get the By-passed TX packet from upper layer
 *
 *  @param priv    Pointer to the moal_private driver data struct
 *  @param wrq     A pointer to iwreq structure containing the packet
 *
 *  @return        0 if successful; IOCTL error code otherwise
 */
static int
woal_bypassed_packet_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	struct sk_buff *skb = NULL;
	struct ethhdr *eth;
	t_u16 moreLen = 0, copyLen = 0;
	ENTER();

#define MLAN_BYPASS_PKT_EXTRA_OFFSET        (4)

	copyLen = wrq->u.data.length;
	moreLen = MLAN_MIN_DATA_HEADER_LEN + MLAN_BYPASS_PKT_EXTRA_OFFSET
		+ sizeof(mlan_buffer);

	skb = alloc_skb(copyLen + moreLen, GFP_KERNEL);
	if (skb == NULL) {
		PRINTM(MERROR, "kmalloc no memory !!\n");
		LEAVE();
		return -ENOMEM;
	}

	skb_reserve(skb, moreLen);

	if (copy_from_user(skb_put(skb, copyLen), wrq->u.data.pointer, copyLen)) {
		PRINTM(MERROR, "PortBlock: copy from user failed\n");
		dev_kfree_skb_any(skb);
		ret = -EFAULT;
		goto done;
	}

	eth = (struct ethhdr *)skb->data;
	eth->h_proto = __constant_htons(eth->h_proto);
	skb->dev = priv->netdev;

	HEXDUMP("Bypass TX Data", skb->data, MIN(skb->len, 100));

	woal_hard_start_xmit(skb, priv->netdev);
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get auth type
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_auth_type(moal_private *priv, struct iwreq *wrq)
{
	int auth_type;
	t_u32 auth_mode;
	int ret = 0;

	ENTER();
	if (wrq->u.data.length == 0) {
		if (MLAN_STATUS_SUCCESS !=
		    woal_get_auth_mode(priv, MOAL_IOCTL_WAIT, &auth_mode)) {
			ret = -EFAULT;
			goto done;
		}
		auth_type = auth_mode;
		if (copy_to_user
		    (wrq->u.data.pointer, &auth_type, sizeof(auth_type))) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	} else {
		if (copy_from_user
		    (&auth_type, wrq->u.data.pointer, sizeof(auth_type))) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		PRINTM(MINFO, "SET: auth_type %d\n", auth_type);
		if (((auth_type < MLAN_AUTH_MODE_OPEN) ||
		     (auth_type > MLAN_AUTH_MODE_SHARED))
		    && (auth_type != MLAN_AUTH_MODE_AUTO)) {
			ret = -EINVAL;
			goto done;
		}
		auth_mode = auth_type;
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_auth_mode(priv, MOAL_IOCTL_WAIT, auth_mode)) {
			ret = -EFAULT;
			goto done;
		}
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Port Control mode
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_port_ctrl(moal_private *priv, struct iwreq *wrq)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_sec_cfg *sec = NULL;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();

	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	sec = (mlan_ds_sec_cfg *)req->pbuf;
	sec->sub_command = MLAN_OID_SEC_CFG_PORT_CTRL_ENABLED;
	req->req_id = MLAN_IOCTL_SEC_CFG;

	if (wrq->u.data.length) {
		if (copy_from_user(&sec->param.port_ctrl_enabled,
				   wrq->u.data.pointer, sizeof(int)) != 0) {
			PRINTM(MERROR, "port_ctrl:Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		req->action = MLAN_ACT_SET;
	} else {
		req->action = MLAN_ACT_GET;
	}

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (!wrq->u.data.length) {
		if (copy_to_user
		    (wrq->u.data.pointer, &sec->param.port_ctrl_enabled,
		     sizeof(int))) {
			PRINTM(MERROR, "port_ctrl:Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 1;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

#if defined(DFS_TESTING_SUPPORT)
/**
 *  @brief Set/Get DFS Testing settings
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_dfs_testing(moal_private *priv, struct iwreq *wrq)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_11h_cfg *ds_11hcfg = NULL;
	int ret = 0;
	int data[4], copy_len;
	int data_length = wrq->u.data.length;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();

	copy_len = MIN(sizeof(data), sizeof(int) * data_length);
	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11h_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	ds_11hcfg = (mlan_ds_11h_cfg *)req->pbuf;
	ds_11hcfg->sub_command = MLAN_OID_11H_DFS_TESTING;
	req->req_id = MLAN_IOCTL_11H_CFG;

	if (!data_length) {
		req->action = MLAN_ACT_GET;
	} else if (data_length == 4) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		if ((unsigned)data[0] > 0xFFFF) {
			PRINTM(MERROR, "The maximum user CAC is 65535 msec.\n");
			ret = -EINVAL;
			goto done;
		}
		if ((unsigned)data[1] > 0xFFFF) {
			PRINTM(MERROR, "The maximum user NOP is 65535 sec.\n");
			ret = -EINVAL;
			goto done;
		}
		if ((unsigned)data[3] > 0xFF) {
			PRINTM(MERROR,
			       "The maximum user fixed channel is 255.\n");
			ret = -EINVAL;
			goto done;
		}
		ds_11hcfg->param.dfs_testing.usr_cac_period_msec =
			(t_u16)data[0];
		ds_11hcfg->param.dfs_testing.usr_nop_period_sec =
			(t_u16)data[1];
		ds_11hcfg->param.dfs_testing.usr_no_chan_change =
			data[2] ? 1 : 0;
		ds_11hcfg->param.dfs_testing.usr_fixed_new_chan = (t_u8)data[3];
		priv->phandle->cac_period_jiffies = (t_u16)data[0] * HZ / 1000;
		req->action = MLAN_ACT_SET;
	} else {
		PRINTM(MERROR, "Invalid number of args!\n");
		ret = -EINVAL;
		goto done;
	}

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (!data_length) {
		data[0] = ds_11hcfg->param.dfs_testing.usr_cac_period_msec;
		data[1] = ds_11hcfg->param.dfs_testing.usr_nop_period_sec;
		data[2] = ds_11hcfg->param.dfs_testing.usr_no_chan_change;
		data[3] = ds_11hcfg->param.dfs_testing.usr_fixed_new_chan;
		if (copy_to_user(wrq->u.data.pointer, &data, sizeof(int) * 4)) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = 4;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}
#endif /* DFS_SUPPORT && DFS_TESTING_SUPPORT */

/**
 *  @brief Set/Get Mgmt Frame passthru mask
 *
 *  @param priv     A pointer to moal_private structure
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 -- success, otherwise fail
 */
static int
woal_mgmt_frame_passthru_ctrl(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0, data_length = wrq->u.data.length;
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *mgmt_cfg = NULL;
	int mask = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (data_length > 1) {
		PRINTM(MERROR, "Invalid no of arguments!\n");
		ret = -EINVAL;
		goto done;
	}
	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	mgmt_cfg = (mlan_ds_misc_cfg *)req->pbuf;
	req->req_id = MLAN_IOCTL_MISC_CFG;
	mgmt_cfg->sub_command = MLAN_OID_MISC_RX_MGMT_IND;

	if (data_length) {	/* SET */
		if (copy_from_user(&mask, wrq->u.data.pointer, sizeof(int))) {
			PRINTM(MERROR, "copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		mgmt_cfg->param.mgmt_subtype_mask = mask;
		req->action = MLAN_ACT_SET;
	} else {
		req->action = MLAN_ACT_GET;
	}

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	mask = mgmt_cfg->param.mgmt_subtype_mask;
	if (copy_to_user(wrq->u.data.pointer, &mask, sizeof(int))) {
		ret = -EFAULT;
		goto done;
	}
	wrq->u.data.length = 1;

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get CFP table codes
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param wrq      A pointer to iwreq structure
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_cfp_code(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	int data[2], copy_len;
	int data_length = wrq->u.data.length;
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *misc_cfg = NULL;
	mlan_ds_misc_cfp_code *cfp_code = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (data_length > 2) {
		PRINTM(MERROR, "Invalid number of argument!\n");
		ret = -EINVAL;
		goto done;
	}
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	misc_cfg = (mlan_ds_misc_cfg *)req->pbuf;
	cfp_code = &misc_cfg->param.cfp_code;
	misc_cfg->sub_command = MLAN_OID_MISC_CFP_CODE;
	req->req_id = MLAN_IOCTL_MISC_CFG;

	if (!data_length) {
		req->action = MLAN_ACT_GET;
	} else {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}
		cfp_code->cfp_code_bg = data[0];
		if (data_length == 2)
			cfp_code->cfp_code_a = data[1];
		req->action = MLAN_ACT_SET;
	}

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	if (!data_length) {
		data[0] = cfp_code->cfp_code_bg;
		data[1] = cfp_code->cfp_code_a;
		data_length = 2;
		if (copy_to_user
		    (wrq->u.data.pointer, &data, sizeof(int) * data_length)) {
			PRINTM(MERROR, "Copy to user failed\n");
			ret = -EFAULT;
			goto done;
		}
		wrq->u.data.length = data_length;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Set/Get Tx/Rx antenna
 *
 * @param priv     A pointer to moal_private structure
 * @param wrq      A pointer to iwreq structure
 *
 * @return         0 --success, otherwise fail
 */
static int
woal_set_get_tx_rx_ant(moal_private *priv, struct iwreq *wrq)
{
	int ret = 0;
	mlan_ds_radio_cfg *radio = NULL;
	mlan_ioctl_req *req = NULL;
	int data[3] = { 0 };
	mlan_status status = MLAN_STATUS_SUCCESS;
	int copy_len;

	ENTER();

	if (wrq->u.data.length * sizeof(int) > sizeof(data)) {
		PRINTM(MERROR, "Too many arguments\n");
		ret = -EFAULT;
		goto done;
	}
	copy_len = MIN(sizeof(data), wrq->u.data.length * sizeof(int));

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_radio_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	radio = (mlan_ds_radio_cfg *)req->pbuf;
	radio->sub_command = MLAN_OID_ANT_CFG;
	req->req_id = MLAN_IOCTL_RADIO_CFG;
	if (wrq->u.data.length) {
		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			PRINTM(MERROR, "Copy from user failed\n");
			ret = -EFAULT;
			goto done;
		}

		radio->param.ant_cfg_1x1.antenna = data[0];
		if (wrq->u.data.length == 2)
			radio->param.ant_cfg_1x1.evaluate_time = data[1];
		req->action = MLAN_ACT_SET;
	} else
		req->action = MLAN_ACT_GET;
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	if (!wrq->u.data.length) {
		wrq->u.data.length = 1;
		data[0] = (int)radio->param.ant_cfg_1x1.antenna;
		data[1] = (int)radio->param.ant_cfg_1x1.evaluate_time;
		data[2] = (int)radio->param.ant_cfg_1x1.current_antenna;
		if (data[0] == 0xffff && data[2] > 0)
			wrq->u.data.length = 3;
		else if (data[0] == 0xffff)
			wrq->u.data.length = 2;
		if (copy_to_user
		    (wrq->u.data.pointer, data,
		     wrq->u.data.length * sizeof(int))) {
			ret = -EFAULT;
			goto done;
		}
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Configure gpio independent reset
 *
 * @param priv         A pointer to moal_private structure
 * @param wrq          A pointer to iwreq structure
 *
 * @return             0 --success, otherwise fail
 */
static int
woal_ind_rst_ioctl(moal_private *priv, struct iwreq *wrq)
{
	int data[2], data_length = wrq->u.data.length, copy_len;
	int ret = 0;
	mlan_ds_misc_cfg *misc = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (sizeof(int) * wrq->u.data.length > sizeof(data)) {
		PRINTM(MERROR, "Too many arguments\n");
		ret = -EINVAL;
		goto done;
	}
	copy_len = MIN(sizeof(data), sizeof(int) * data_length);

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	misc = (mlan_ds_misc_cfg *)req->pbuf;
	memset(misc, 0, sizeof(mlan_ds_misc_cfg));

	misc->sub_command = MLAN_OID_MISC_IND_RST_CFG;
	req->req_id = MLAN_IOCTL_MISC_CFG;

	if (data_length == 0) {
		req->action = MLAN_ACT_GET;
	} else if ((data_length == 1) || (data_length == 2)) {
		req->action = MLAN_ACT_SET;

		if (copy_from_user(data, wrq->u.data.pointer, copy_len)) {
			/* copy_from_user failed  */
			PRINTM(MERROR, "S_PARAMS: copy from user failed\n");
			LEAVE();
			return -EINVAL;
		}

		/* ir_mode */
		if (data[0] < 0 || data[0] > 2) {
			PRINTM(MERROR, "Invalid ir mode parameter (0/1/2)!\n");
			ret = -EINVAL;
			goto done;
		}
		misc->param.ind_rst_cfg.ir_mode = data[0];

		/* gpio_pin */
		if (data_length == 2) {
			if ((data[1] != 0xFF) && (data[1] < 0 || data[1] > 15)) {
				PRINTM(MERROR,
				       "Invalid gpio pin no (0-15 , 0xFF for default)!\n");
				ret = -EINVAL;
				goto done;
			}
			misc->param.ind_rst_cfg.gpio_pin = data[1];
		}

	} else {
		ret = -EINVAL;
		goto done;
	}

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	data[0] = misc->param.ind_rst_cfg.ir_mode;
	data[1] = misc->param.ind_rst_cfg.gpio_pin;
	wrq->u.data.length = 2;

	if (copy_to_user(wrq->u.data.pointer, data, sizeof(int) *
			 wrq->u.data.length)) {
		PRINTM(MERROR, "QCONFIG: copy to user failed\n");
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/********************************************************
			Global Functions
********************************************************/
/**
 *  @brief ioctl function - entry point
 *
 *  @param dev      A pointer to net_device structure
 *  @param req      A pointer to ifreq structure
 *  @param cmd      Command
 *
 *  @return         0 --success, otherwise fail
 */
int
woal_wext_do_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	struct iwreq *wrq = (struct iwreq *)req;
	int ret = 0;

	if (!IS_STA_WEXT(cfg80211_wext))
		return -EOPNOTSUPP;

	ENTER();

	PRINTM(MINFO, "woal_wext_do_ioctl: ioctl cmd = 0x%x\n", cmd);
	switch (cmd) {
	case WOAL_SETONEINT_GETWORDCHAR:
		switch (wrq->u.data.flags) {
		case WOAL_VERSION:	/* Get driver version */
			ret = woal_get_driver_version(priv, req);
			break;
		case WOAL_VEREXT:	/* Get extended driver version */
			ret = woal_get_driver_verext(priv, req);
			break;
		default:
			ret = -EOPNOTSUPP;
			break;
		}
		break;
	case WOAL_SETNONE_GETNONE:
		switch (wrq->u.data.flags) {
		case WOAL_WARMRESET:
			ret = woal_warm_reset(priv);
			break;
		case WOAL_11D_CLR_CHAN_TABLE:
			ret = woal_11d_clr_chan_table(priv, wrq);
			break;
		default:
			ret = -EOPNOTSUPP;
			break;
		}
		break;
	case WOAL_SETONEINT_GETONEINT:
		switch (wrq->u.data.flags) {
		case WOAL_SET_GET_TXRATE:
			ret = woal_set_get_txrate(priv, wrq);
			break;
		case WOAL_SET_GET_REGIONCODE:
			ret = woal_set_get_regioncode(priv, wrq);
			break;
		case WOAL_SET_RADIO:
			ret = woal_set_get_radio(priv, wrq);
			break;
		case WOAL_WMM_ENABLE:
			ret = woal_wmm_enable_ioctl(priv, wrq);
			break;
		case WOAL_11D_ENABLE:
			ret = woal_11d_enable_ioctl(priv, wrq);
			break;
		case WOAL_SET_GET_QOS_CFG:
			ret = woal_set_get_qos_cfg(priv, wrq);
			break;
#if defined(REASSOCIATION)
		case WOAL_SET_GET_REASSOC:
			ret = woal_set_get_reassoc(priv, wrq);
			break;
#endif /* REASSOCIATION */
		case WOAL_TXBUF_CFG:
			ret = woal_txbuf_cfg(priv, wrq);
			break;
		case WOAL_SET_GET_WWS_CFG:
			ret = woal_wws_cfg(priv, wrq);
			break;
		case WOAL_SLEEP_PD:
			ret = woal_sleep_pd(priv, wrq);
			break;
		case WOAL_FW_WAKEUP_METHOD:
			ret = woal_fw_wakeup_method(priv, wrq);
			break;
		case WOAL_AUTH_TYPE:
			ret = woal_auth_type(priv, wrq);
			break;
		case WOAL_PORT_CTRL:
			ret = woal_port_ctrl(priv, wrq);
			break;
		case WOAL_COALESCING_STATUS:
			ret = woal_coalescing_status_ioctl(priv, wrq);
			break;
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
		case WOAL_SET_GET_BSS_ROLE:
			ret = woal_set_get_bss_role(priv, wrq);
			break;
#endif
#endif
		case WOAL_SET_GET_11H_LOCAL_PWR_CONSTRAINT:
			ret = woal_set_get_11h_local_pwr_constraint(priv, wrq);
			break;
		case WOAL_MAC_CONTROL:
			ret = woal_mac_control_ioctl(priv, wrq);
			break;
		case WOAL_THERMAL:
			ret = woal_thermal_ioctl(priv, wrq);
			break;
		default:
			ret = -EOPNOTSUPP;
			break;
		}
		break;

	case WOAL_SET_GET_SIXTEEN_INT:
		switch ((int)wrq->u.data.flags) {
		case WOAL_TX_POWERCFG:
			ret = woal_tx_power_cfg(priv, wrq);
			break;
#ifdef DEBUG_LEVEL1
		case WOAL_DRV_DBG:
			ret = woal_drv_dbg(priv, wrq);
			break;
#endif
		case WOAL_BEACON_INTERVAL:
			ret = woal_beacon_interval(priv, wrq);
			break;
		case WOAL_ATIM_WINDOW:
			ret = woal_atim_window(priv, wrq);
			break;
		case WOAL_SIGNAL:
			ret = woal_get_signal(priv, wrq);
			break;
		case WOAL_DEEP_SLEEP:
			ret = woal_deep_sleep_ioctl(priv, wrq);
			break;
		case WOAL_11N_TX_CFG:
			ret = woal_11n_tx_cfg(priv, wrq);
			break;
		case WOAL_11N_AMSDU_AGGR_CTRL:
			ret = woal_11n_amsdu_aggr_ctrl(priv, wrq);
			break;
		case WOAL_11N_HTCAP_CFG:
			ret = woal_11n_htcap_cfg(priv, wrq);
			break;
		case WOAL_PRIO_TBL:
			ret = woal_11n_prio_tbl(priv, wrq);
			break;
		case WOAL_ADDBA_UPDT:
			ret = woal_addba_para_updt(priv, wrq);
			break;
		case WOAL_ADDBA_REJECT:
			ret = woal_addba_reject(priv, wrq);
			break;
		case WOAL_TX_BF_CAP:
			ret = woal_tx_bf_cap_ioctl(priv, wrq);
			break;
		case WOAL_HS_CFG:
			ret = woal_hs_cfg(priv, wrq, MTRUE);
			break;
		case WOAL_HS_SETPARA:
			ret = woal_hs_setpara(priv, wrq);
			break;
		case WOAL_REG_READ_WRITE:
			ret = woal_reg_read_write(priv, wrq);
			break;
		case WOAL_INACTIVITY_TIMEOUT_EXT:
			ret = woal_inactivity_timeout_ext(priv, wrq);
			break;
		case WOAL_SDIO_CLOCK:
			ret = woal_sdio_clock_ioctl(priv, wrq);
			break;
		case WOAL_CMD_52RDWR:
			ret = woal_cmd52rdwr_ioctl(priv, wrq);
			break;
		case WOAL_BAND_CFG:
			ret = woal_band_cfg(priv, wrq);
			break;
		case WOAL_SCAN_CFG:
			ret = woal_set_get_scan_cfg(priv, wrq);
			break;
		case WOAL_PS_CFG:
			ret = woal_set_get_ps_cfg(priv, wrq);
			break;
		case WOAL_MEM_READ_WRITE:
			ret = woal_mem_read_write(priv, wrq);
			break;
#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
		case WOAL_SDIO_MPA_CTRL:
			ret = woal_do_sdio_mpa_ctrl(priv, wrq);
			break;
#endif
		case WOAL_SLEEP_PARAMS:
			ret = woal_sleep_params_ioctl(priv, wrq);
			break;
		case WOAL_NET_MONITOR:
			ret = woal_net_monitor_ioctl(priv, wrq);
			break;
#if defined(DFS_TESTING_SUPPORT)
		case WOAL_DFS_TESTING:
			ret = woal_dfs_testing(priv, wrq);
			break;
#endif
		case WOAL_MGMT_FRAME_CTRL:
			ret = woal_mgmt_frame_passthru_ctrl(priv, wrq);
			break;
		case WOAL_CFP_CODE:
			ret = woal_cfp_code(priv, wrq);
			break;
		case WOAL_SET_GET_TX_RX_ANT:
			ret = woal_set_get_tx_rx_ant(priv, wrq);
			break;
		case WOAL_IND_RST_CFG:
			ret = woal_ind_rst_ioctl(priv, wrq);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;

	case WOALGETLOG:
		ret = woal_get_log(priv, wrq);
		break;

	case WOAL_SET_GET_256_CHAR:
		switch (wrq->u.data.flags) {
		case WOAL_PASSPHRASE:
			ret = woal_passphrase(priv, wrq);
			break;
		case WOAL_ADHOC_AES:
			ret = woal_adhoc_aes_ioctl(priv, wrq);
			break;
		case WOAL_ASSOCIATE:
			ret = woal_associate_ssid_bssid(priv, wrq);
			break;
		case WOAL_WMM_QUEUE_STATUS:
			ret = woal_wmm_queue_status_ioctl(priv, wrq);
			break;

		case WOAL_WMM_TS_STATUS:
			ret = woal_wmm_ts_status_ioctl(priv, wrq);
			break;
		case WOAL_IP_ADDRESS:
			ret = woal_set_get_ip_addr(priv, wrq);
			break;
		case WOAL_TX_BF_CFG:
			ret = woal_tx_bf_cfg_ioctl(priv, wrq);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;

	case WOAL_SETADDR_GETNONE:
		switch ((int)wrq->u.data.flags) {
		case WOAL_DEAUTH:
			ret = woal_deauth(priv, wrq);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;

	case WOAL_SETNONE_GETTWELVE_CHAR:
		/*
		 * We've not used IW_PRIV_TYPE_FIXED so sub-ioctl number is
		 * in flags of iwreq structure, otherwise it will be in
		 * mode member of iwreq structure.
		 */
		switch ((int)wrq->u.data.flags) {
		case WOAL_WPS_SESSION:
			ret = woal_wps_cfg_ioctl(priv, wrq);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case WOAL_SETNONE_GET_FOUR_INT:
		switch ((int)wrq->u.data.flags) {
		case WOAL_DATA_RATE:
			ret = woal_get_txrx_rate(priv, wrq);
			break;
		case WOAL_ESUPP_MODE:
			ret = woal_get_esupp_mode(priv, wrq);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;

	case WOAL_SET_GET_64_INT:
		switch ((int)wrq->u.data.flags) {
		case WOAL_ECL_SYS_CLOCK:
			ret = woal_ecl_sys_clock(priv, wrq);
			break;
		}

		break;

	case WOAL_HOST_CMD:
		ret = woal_host_command(priv, wrq);
		break;
	case WOAL_ARP_FILTER:
		ret = woal_arp_filter(priv, wrq);
		break;
	case WOAL_SET_INTS_GET_CHARS:
		switch ((int)wrq->u.data.flags) {
		case WOAL_READ_EEPROM:
			ret = woal_read_eeprom(priv, wrq);
			break;
		}
		break;
	case WOAL_SET_GET_2K_BYTES:
		switch ((int)wrq->u.data.flags) {
		case WOAL_CMD_53RDWR:
			ret = woal_cmd53rdwr_ioctl(priv, wrq);
			break;
		case WOAL_SET_USER_SCAN:
			ret = woal_set_user_scan_ioctl(priv, wrq);
			break;
		case WOAL_GET_SCAN_TABLE:
			ret = woal_get_scan_table_ioctl(priv, wrq);
			break;
		case WOAL_SET_USER_SCAN_EXT:
			ret = woal_set_user_scan_ext_ioctl(priv, wrq);
			break;
		case WOAL_WMM_ADDTS:
			ret = woal_wmm_addts_req_ioctl(priv, wrq);
			break;
		case WOAL_WMM_DELTS:
			ret = woal_wmm_delts_req_ioctl(priv, wrq);
			break;
		case WOAL_WMM_QUEUE_CONFIG:
			ret = woal_wmm_queue_config_ioctl(priv, wrq);
			break;
		case WOAL_WMM_QUEUE_STATS:
			ret = woal_wmm_queue_stats_ioctl(priv, wrq);
			break;
		case WOAL_BYPASSED_PACKET:
			ret = woal_bypassed_packet_ioctl(priv, wrq);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;

#ifdef UAP_WEXT
	case WOAL_FROYO_START:
		break;
	case WOAL_FROYO_WL_FW_RELOAD:
		break;
	case WOAL_FROYO_STOP:
		if (IS_UAP_WEXT(cfg80211_wext) && MLAN_STATUS_SUCCESS !=
		    woal_disconnect(priv, MOAL_IOCTL_WAIT, NULL,
				    DEF_DEAUTH_REASON_CODE)) {
			ret = -EFAULT;
		}
		break;
#endif
	default:
		ret = -EINVAL;
		break;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Get data rates
 *
 *  @param priv          A pointer to moal_private structure
 *  @param wait_option   Wait option
 *  @param m_rates       A pointer to moal_802_11_rates structure
 *
 *  @return              MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING -- success, otherwise fail
 */
mlan_status
woal_get_data_rates(moal_private *priv, t_u8 wait_option,
		    moal_802_11_rates *m_rates)
{
	int ret = 0;
	mlan_ds_rate *rate = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();

	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_rate));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	rate = (mlan_ds_rate *)req->pbuf;
	rate->sub_command = MLAN_OID_SUPPORTED_RATES;
	req->req_id = MLAN_IOCTL_RATE;
	req->action = MLAN_ACT_GET;

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, wait_option);
	if (status == MLAN_STATUS_SUCCESS) {
		if (m_rates)
			m_rates->num_of_rates =
				woal_copy_rates(m_rates->rates,
						m_rates->num_of_rates,
						rate->param.rates,
						MLAN_SUPPORTED_RATES);
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return status;
}

/**
 *  @brief Get channel list
 *
 *  @param priv            A pointer to moal_private structure
 *  @param wait_option     Wait option
 *  @param chan_list       A pointer to mlan_chan_list structure
 *
 *  @return                MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING -- success, otherwise fail
 */
mlan_status
woal_get_channel_list(moal_private *priv, t_u8 wait_option,
		      mlan_chan_list *chan_list)
{
	int ret = 0;
	mlan_ds_bss *bss = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	ENTER();

	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	bss = (mlan_ds_bss *)req->pbuf;
	bss->sub_command = MLAN_OID_BSS_CHANNEL_LIST;
	req->req_id = MLAN_IOCTL_BSS;
	req->action = MLAN_ACT_GET;

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, wait_option);
	if (status == MLAN_STATUS_SUCCESS) {
		if (chan_list) {
			memcpy(chan_list, &bss->param.chanlist,
			       sizeof(mlan_chan_list));
		}
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return status;
}

/**
 *  @brief Handle get info resp
 *
 *  @param priv     Pointer to moal_private structure
 *  @param info     Pointer to mlan_ds_get_info structure
 *
 *  @return         N/A
 */
void
woal_ioctl_get_info_resp(moal_private *priv, mlan_ds_get_info *info)
{
	ENTER();
	switch (info->sub_command) {
	case MLAN_OID_GET_STATS:
		priv->w_stats.discard.fragment = info->param.stats.fcs_error;
		priv->w_stats.discard.retries = info->param.stats.retry;
		priv->w_stats.discard.misc = info->param.stats.ack_failure;
		break;
	case MLAN_OID_GET_SIGNAL:
		if (info->param.signal.selector & BCN_RSSI_AVG_MASK)
			priv->w_stats.qual.level =
				info->param.signal.bcn_rssi_avg;
		if (info->param.signal.selector & BCN_NF_AVG_MASK)
			priv->w_stats.qual.noise =
				info->param.signal.bcn_nf_avg;
		break;
	default:
		break;
	}
	LEAVE();
}

/**
 *  @brief Handle get BSS resp
 *
 *  @param priv     Pointer to moal_private structure
 *  @param bss      Pointer to mlan_ds_bss structure
 *
 *  @return         N/A
 */
void
woal_ioctl_get_bss_resp(moal_private *priv, mlan_ds_bss *bss)
{
	t_u32 mode = 0;

	ENTER();

	switch (bss->sub_command) {
	case MLAN_OID_BSS_MODE:
		if (bss->param.bss_mode == MLAN_BSS_MODE_INFRA)
			mode = IW_MODE_INFRA;
		else if (bss->param.bss_mode == MLAN_BSS_MODE_IBSS)
			mode = IW_MODE_ADHOC;
		else
			mode = IW_MODE_AUTO;
		priv->w_stats.status = mode;
		break;
	default:
		break;
	}

	LEAVE();
	return;
}
