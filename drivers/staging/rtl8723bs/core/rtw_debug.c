// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_DEBUG_C_

#include <drv_types.h>
#include <rtw_debug.h>

u32 GlobalDebugLevel = _drv_err_;

#ifdef DEBUG_RTL871X

	u64 GlobalDebugComponents = \
			_module_rtl871x_xmit_c_ |
			_module_xmit_osdep_c_ |
			_module_rtl871x_recv_c_ |
			_module_recv_osdep_c_ |
			_module_rtl871x_mlme_c_ |
			_module_mlme_osdep_c_ |
			_module_rtl871x_sta_mgt_c_ |
			_module_rtl871x_cmd_c_ |
			_module_cmd_osdep_c_ |
			_module_rtl871x_io_c_ |
			_module_io_osdep_c_ |
			_module_os_intfs_c_|
			_module_rtl871x_security_c_|
			_module_rtl871x_eeprom_c_|
			_module_hal_init_c_|
			_module_hci_hal_init_c_|
			_module_rtl871x_ioctl_c_|
			_module_rtl871x_ioctl_set_c_|
			_module_rtl871x_ioctl_query_c_|
			_module_rtl871x_pwrctrl_c_|
			_module_hci_intfs_c_|
			_module_hci_ops_c_|
			_module_hci_ops_os_c_|
			_module_rtl871x_ioctl_os_c|
			_module_rtl8712_cmd_c_|
			_module_hal_xmit_c_|
			_module_rtl8712_recv_c_ |
			_module_mp_ |
			_module_efuse_;

#endif /* DEBUG_RTL871X */

#include <rtw_version.h>

void dump_drv_version(void *sel)
{
	DBG_871X_SEL_NL(sel, "%s %s\n", "rtl8723bs", DRIVERVERSION);
}

void dump_log_level(void *sel)
{
	DBG_871X_SEL_NL(sel, "log_level:%d\n", GlobalDebugLevel);
}

void sd_f0_reg_dump(void *sel, struct adapter *adapter)
{
	int i;

	for (i = 0x0; i <= 0xff; i++) {
		if (i%16 == 0)
			DBG_871X_SEL_NL(sel, "0x%02x ", i);

		DBG_871X_SEL(sel, "%02x ", rtw_sd_f0_read8(adapter, i));

		if (i%16 == 15)
			DBG_871X_SEL(sel, "\n");
		else if (i%8 == 7)
			DBG_871X_SEL(sel, "\t");
	}
}

void mac_reg_dump(void *sel, struct adapter *adapter)
{
	int i, j = 1;

	DBG_871X_SEL_NL(sel, "======= MAC REG =======\n");

	for (i = 0x0; i < 0x800; i += 4) {
		if (j%4 == 1)
			DBG_871X_SEL_NL(sel, "0x%03x", i);
		DBG_871X_SEL(sel, " 0x%08x ", rtw_read32(adapter, i));
		if ((j++)%4 == 0)
			DBG_871X_SEL(sel, "\n");
	}
}

void bb_reg_dump(void *sel, struct adapter *adapter)
{
	int i, j = 1;

	DBG_871X_SEL_NL(sel, "======= BB REG =======\n");
	for (i = 0x800; i < 0x1000 ; i += 4) {
		if (j%4 == 1)
			DBG_871X_SEL_NL(sel, "0x%03x", i);
		DBG_871X_SEL(sel, " 0x%08x ", rtw_read32(adapter, i));
		if ((j++)%4 == 0)
			DBG_871X_SEL(sel, "\n");
	}
}

void rf_reg_dump(void *sel, struct adapter *adapter)
{
	int i, j = 1, path;
	u32 value;
	u8 rf_type = 0;
	u8 path_nums = 0;

	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	if ((RF_1T2R == rf_type) || (RF_1T1R == rf_type))
		path_nums = 1;
	else
		path_nums = 2;

	DBG_871X_SEL_NL(sel, "======= RF REG =======\n");

	for (path = 0; path < path_nums; path++) {
		DBG_871X_SEL_NL(sel, "RF_Path(%x)\n", path);
		for (i = 0; i < 0x100; i++) {
			value = rtw_hal_read_rfreg(adapter, path, i, 0xffffffff);
			if (j%4 == 1)
				DBG_871X_SEL_NL(sel, "0x%02x ", i);
			DBG_871X_SEL(sel, " 0x%08x ", value);
			if ((j++)%4 == 0)
				DBG_871X_SEL(sel, "\n");
		}
	}
}

#ifdef PROC_DEBUG
ssize_t proc_set_write_reg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 addr, val, len;

	if (count < 3) {
		DBG_871X("argument size is less than 3\n");
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%x %x %x", &addr, &val, &len);

		if (num !=  3) {
			DBG_871X("invalid write_reg parameter!\n");
			return count;
		}

		switch (len) {
		case 1:
			rtw_write8(padapter, addr, (u8)val);
			break;
		case 2:
			rtw_write16(padapter, addr, (u16)val);
			break;
		case 4:
			rtw_write32(padapter, addr, val);
			break;
		default:
			DBG_871X("error write length =%d", len);
			break;
		}

	}

	return count;

}

static u32 proc_get_read_addr = 0xeeeeeeee;
static u32 proc_get_read_len = 0x4;

int proc_get_read_reg(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	if (proc_get_read_addr == 0xeeeeeeee) {
		DBG_871X_SEL_NL(m, "address not initialized\n");
		return 0;
	}

	switch (proc_get_read_len) {
	case 1:
		DBG_871X_SEL_NL(m, "rtw_read8(0x%x) = 0x%x\n", proc_get_read_addr, rtw_read8(padapter, proc_get_read_addr));
		break;
	case 2:
		DBG_871X_SEL_NL(m, "rtw_read16(0x%x) = 0x%x\n", proc_get_read_addr, rtw_read16(padapter, proc_get_read_addr));
		break;
	case 4:
		DBG_871X_SEL_NL(m, "rtw_read32(0x%x) = 0x%x\n", proc_get_read_addr, rtw_read32(padapter, proc_get_read_addr));
		break;
	default:
		DBG_871X_SEL_NL(m, "error read length =%d\n", proc_get_read_len);
		break;
	}

	return 0;
}

ssize_t proc_set_read_reg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	char tmp[16];
	u32 addr, len;

	if (count < 2) {
		DBG_871X("argument size is less than 2\n");
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%x %x", &addr, &len);

		if (num !=  2) {
			DBG_871X("invalid read_reg parameter!\n");
			return count;
		}

		proc_get_read_addr = addr;

		proc_get_read_len = len;
	}

	return count;

}

int proc_get_fwstate(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	DBG_871X_SEL_NL(m, "fwstate = 0x%x\n", get_fwstate(pmlmepriv));

	return 0;
}

int proc_get_sec_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct security_priv *sec = &padapter->securitypriv;

	DBG_871X_SEL_NL(m, "auth_alg = 0x%x, enc_alg = 0x%x, auth_type = 0x%x, enc_type = 0x%x\n",
						sec->dot11AuthAlgrthm, sec->dot11PrivacyAlgrthm,
						sec->ndisauthtype, sec->ndisencryptstatus);

	DBG_871X_SEL_NL(m, "hw_decrypted =%d\n", sec->hw_decrypted);

#ifdef DBG_SW_SEC_CNT
	DBG_871X_SEL_NL(m, "wep_sw_enc_cnt =%llu, %llu, %llu\n"
		, sec->wep_sw_enc_cnt_bc, sec->wep_sw_enc_cnt_mc, sec->wep_sw_enc_cnt_uc);
	DBG_871X_SEL_NL(m, "wep_sw_dec_cnt =%llu, %llu, %llu\n"
		, sec->wep_sw_dec_cnt_bc, sec->wep_sw_dec_cnt_mc, sec->wep_sw_dec_cnt_uc);

	DBG_871X_SEL_NL(m, "tkip_sw_enc_cnt =%llu, %llu, %llu\n"
		, sec->tkip_sw_enc_cnt_bc, sec->tkip_sw_enc_cnt_mc, sec->tkip_sw_enc_cnt_uc);
	DBG_871X_SEL_NL(m, "tkip_sw_dec_cnt =%llu, %llu, %llu\n"
		, sec->tkip_sw_dec_cnt_bc, sec->tkip_sw_dec_cnt_mc, sec->tkip_sw_dec_cnt_uc);

	DBG_871X_SEL_NL(m, "aes_sw_enc_cnt =%llu, %llu, %llu\n"
		, sec->aes_sw_enc_cnt_bc, sec->aes_sw_enc_cnt_mc, sec->aes_sw_enc_cnt_uc);
	DBG_871X_SEL_NL(m, "aes_sw_dec_cnt =%llu, %llu, %llu\n"
		, sec->aes_sw_dec_cnt_bc, sec->aes_sw_dec_cnt_mc, sec->aes_sw_dec_cnt_uc);
#endif /* DBG_SW_SEC_CNT */

	return 0;
}

int proc_get_mlmext_state(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	DBG_871X_SEL_NL(m, "pmlmeinfo->state = 0x%x\n", pmlmeinfo->state);

	return 0;
}

int proc_get_roam_flags(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *adapter = (struct adapter *)rtw_netdev_priv(dev);

	DBG_871X_SEL_NL(m, "0x%02x\n", rtw_roam_flags(adapter));

	return 0;
}

ssize_t proc_set_roam_flags(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *adapter = (struct adapter *)rtw_netdev_priv(dev);

	char tmp[32];
	u8 flags;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%hhx", &flags);

		if (num == 1)
			rtw_assign_roam_flags(adapter, flags);
	}

	return count;

}

int proc_get_roam_param(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *adapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *mlme = &adapter->mlmepriv;

	DBG_871X_SEL_NL(m, "%12s %12s %11s\n", "rssi_diff_th", "scanr_exp_ms", "scan_int_ms");
	DBG_871X_SEL_NL(m, "%-12u %-12u %-11u\n"
		, mlme->roam_rssi_diff_th
		, mlme->roam_scanr_exp_ms
		, mlme->roam_scan_int_ms
	);

	return 0;
}

ssize_t proc_set_roam_param(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *adapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *mlme = &adapter->mlmepriv;

	char tmp[32];
	u8 rssi_diff_th;
	u32 scanr_exp_ms;
	u32 scan_int_ms;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%hhu %u %u", &rssi_diff_th, &scanr_exp_ms, &scan_int_ms);

		if (num >= 1)
			mlme->roam_rssi_diff_th = rssi_diff_th;
		if (num >= 2)
			mlme->roam_scanr_exp_ms = scanr_exp_ms;
		if (num >= 3)
			mlme->roam_scan_int_ms = scan_int_ms;
	}

	return count;

}

ssize_t proc_set_roam_tgt_addr(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *adapter = (struct adapter *)rtw_netdev_priv(dev);

	char tmp[32];
	u8 addr[ETH_ALEN];

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", addr, addr+1, addr+2, addr+3, addr+4, addr+5);
		if (num == 6)
			memcpy(adapter->mlmepriv.roam_tgt_addr, addr, ETH_ALEN);

		DBG_871X("set roam_tgt_addr to "MAC_FMT"\n", MAC_ARG(adapter->mlmepriv.roam_tgt_addr));
	}

	return count;
}

int proc_get_qos_option(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	DBG_871X_SEL_NL(m, "qos_option =%d\n", pmlmepriv->qospriv.qos_option);

	return 0;
}

int proc_get_ht_option(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	DBG_871X_SEL_NL(m, "ht_option =%d\n", pmlmepriv->htpriv.ht_option);

	return 0;
}

int proc_get_rf_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	DBG_871X_SEL_NL(m, "cur_ch =%d, cur_bw =%d, cur_ch_offet =%d\n",
					pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);

	DBG_871X_SEL_NL(m, "oper_ch =%d, oper_bw =%d, oper_ch_offet =%d\n",
					rtw_get_oper_ch(padapter), rtw_get_oper_bw(padapter),  rtw_get_oper_choffset(padapter));

	return 0;
}

int proc_get_survey_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct __queue	*queue	= &(pmlmepriv->scanned_queue);
	struct wlan_network	*pnetwork = NULL;
	struct list_head	*plist, *phead;
	s32 notify_signal;
	s16 notify_noise = 0;
	u16  index = 0;

	spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
	phead = get_list_head(queue);
	plist = phead ? get_next(phead) : NULL;
	if ((!phead) || (!plist)) {
		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
		return 0;
	}

	DBG_871X_SEL_NL(m, "%5s  %-17s  %3s  %-3s  %-4s  %-4s  %5s  %s\n", "index", "bssid", "ch", "RSSI", "SdBm", "Noise", "age", "ssid");
	while (1) {
		if (phead == plist)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		if (!pnetwork)
			break;

		if (check_fwstate(pmlmepriv, _FW_LINKED) == true &&
			is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network, 0)) {
			notify_signal = translate_percentage_to_dbm(padapter->recvpriv.signal_strength);/*dbm*/
		} else {
			notify_signal = translate_percentage_to_dbm(pnetwork->network.PhyInfo.SignalStrength);/*dbm*/
		}

		#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
		rtw_hal_get_odm_var(padapter, HAL_ODM_NOISE_MONITOR, &(pnetwork->network.Configuration.DSConfig), &(notify_noise));
		#endif

		DBG_871X_SEL_NL(m, "%5d  "MAC_FMT"  %3d  %3d  %4d  %4d  %5d  %s\n",
			++index,
			MAC_ARG(pnetwork->network.MacAddress),
			pnetwork->network.Configuration.DSConfig,
			(int)pnetwork->network.Rssi,
			notify_signal,
			notify_noise,
			jiffies_to_msecs(jiffies - pnetwork->last_scanned),
			/*translate_percentage_to_dbm(pnetwork->network.PhyInfo.SignalStrength),*/
			pnetwork->network.Ssid.Ssid);
		plist = get_next(plist);
	}
	spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

	return 0;
}

int proc_get_ap_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct sta_info *psta;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct sta_priv *pstapriv = &padapter->stapriv;

	psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
	if (psta) {
		int i;
		struct recv_reorder_ctrl *preorder_ctrl;

		DBG_871X_SEL_NL(m, "SSID =%s\n", cur_network->network.Ssid.Ssid);
		DBG_871X_SEL_NL(m, "sta's macaddr:" MAC_FMT "\n", MAC_ARG(psta->hwaddr));
		DBG_871X_SEL_NL(m, "cur_channel =%d, cur_bwmode =%d, cur_ch_offset =%d\n", pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);
		DBG_871X_SEL_NL(m, "wireless_mode = 0x%x, rtsen =%d, cts2slef =%d\n", psta->wireless_mode, psta->rtsen, psta->cts2self);
		DBG_871X_SEL_NL(m, "state = 0x%x, aid =%d, macid =%d, raid =%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
		DBG_871X_SEL_NL(m, "qos_en =%d, ht_en =%d, init_rate =%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);
		DBG_871X_SEL_NL(m, "bwmode =%d, ch_offset =%d, sgi_20m =%d, sgi_40m =%d\n", psta->bw_mode, psta->htpriv.ch_offset, psta->htpriv.sgi_20m, psta->htpriv.sgi_40m);
		DBG_871X_SEL_NL(m, "ampdu_enable = %d\n", psta->htpriv.ampdu_enable);
		DBG_871X_SEL_NL(m, "agg_enable_bitmap =%x, candidate_tid_bitmap =%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
		DBG_871X_SEL_NL(m, "ldpc_cap = 0x%x, stbc_cap = 0x%x, beamform_cap = 0x%x\n", psta->htpriv.ldpc_cap, psta->htpriv.stbc_cap, psta->htpriv.beamform_cap);

		for (i = 0; i < 16; i++) {
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			if (preorder_ctrl->enable) {
				DBG_871X_SEL_NL(m, "tid =%d, indicate_seq =%d\n", i, preorder_ctrl->indicate_seq);
			}
		}

	} else {
		DBG_871X_SEL_NL(m, "can't get sta's macaddr, cur_network's macaddr:" MAC_FMT "\n", MAC_ARG(cur_network->network.MacAddress));
	}

	return 0;
}

int proc_get_adapter_state(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	DBG_871X_SEL_NL(m, "name =%s, bSurpriseRemoved =%d, bDriverStopped =%d\n",
					dev->name, padapter->bSurpriseRemoved, padapter->bDriverStopped);

	return 0;
}

int proc_get_trx_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	int i;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct recv_priv  *precvpriv = &padapter->recvpriv;
	struct hw_xmit *phwxmit;

	DBG_871X_SEL_NL(m, "free_xmitbuf_cnt =%d, free_xmitframe_cnt =%d\n"
		, pxmitpriv->free_xmitbuf_cnt, pxmitpriv->free_xmitframe_cnt);
	DBG_871X_SEL_NL(m, "free_ext_xmitbuf_cnt =%d, free_xframe_ext_cnt =%d\n"
		, pxmitpriv->free_xmit_extbuf_cnt, pxmitpriv->free_xframe_ext_cnt);
	DBG_871X_SEL_NL(m, "free_recvframe_cnt =%d\n"
		, precvpriv->free_recvframe_cnt);

	for (i = 0; i < 4; i++) {
		phwxmit = pxmitpriv->hwxmits + i;
		DBG_871X_SEL_NL(m, "%d, hwq.accnt =%d\n", i, phwxmit->accnt);
	}

	return 0;
}

int proc_get_rate_ctl(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *adapter = (struct adapter *)rtw_netdev_priv(dev);

	if (adapter->fix_rate != 0xff) {
		DBG_871X_SEL_NL(m, "FIX\n");
		DBG_871X_SEL_NL(m, "0x%02x\n", adapter->fix_rate);
	} else {
		DBG_871X_SEL_NL(m, "RA\n");
	}

	return 0;
}

ssize_t proc_set_rate_ctl(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *adapter = (struct adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u8 fix_rate;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%hhx", &fix_rate);

		if (num >= 1)
			adapter->fix_rate = fix_rate;
	}

	return count;
}

ssize_t proc_set_fwdl_test_case(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	char tmp[32];

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		sscanf(tmp, "%hhu %hhu", &g_fwdl_chksum_fail, &g_fwdl_wintint_rdy_fail);
	}

	return count;
}

ssize_t proc_set_wait_hiq_empty(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	char tmp[32];

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		sscanf(tmp, "%u", &g_wait_hiq_empty);
	}

	return count;
}

int proc_get_suspend_resume_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *dvobj = padapter->dvobj;
	struct debug_priv *pdbgpriv = &dvobj->drv_dbg;

	DBG_871X_SEL_NL(m, "dbg_sdio_alloc_irq_cnt =%d\n", pdbgpriv->dbg_sdio_alloc_irq_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_free_irq_cnt =%d\n", pdbgpriv->dbg_sdio_free_irq_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_alloc_irq_error_cnt =%d\n", pdbgpriv->dbg_sdio_alloc_irq_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_free_irq_error_cnt =%d\n", pdbgpriv->dbg_sdio_free_irq_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_init_error_cnt =%d\n", pdbgpriv->dbg_sdio_init_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_deinit_error_cnt =%d\n", pdbgpriv->dbg_sdio_deinit_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_suspend_error_cnt =%d\n", pdbgpriv->dbg_suspend_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_suspend_cnt =%d\n", pdbgpriv->dbg_suspend_cnt);
	DBG_871X_SEL_NL(m, "dbg_resume_cnt =%d\n", pdbgpriv->dbg_resume_cnt);
	DBG_871X_SEL_NL(m, "dbg_resume_error_cnt =%d\n", pdbgpriv->dbg_resume_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_deinit_fail_cnt =%d\n", pdbgpriv->dbg_deinit_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_carddisable_cnt =%d\n", pdbgpriv->dbg_carddisable_cnt);
	DBG_871X_SEL_NL(m, "dbg_ps_insuspend_cnt =%d\n", pdbgpriv->dbg_ps_insuspend_cnt);
	DBG_871X_SEL_NL(m, "dbg_dev_unload_inIPS_cnt =%d\n", pdbgpriv->dbg_dev_unload_inIPS_cnt);
	DBG_871X_SEL_NL(m, "dbg_scan_pwr_state_cnt =%d\n", pdbgpriv->dbg_scan_pwr_state_cnt);
	DBG_871X_SEL_NL(m, "dbg_downloadfw_pwr_state_cnt =%d\n", pdbgpriv->dbg_downloadfw_pwr_state_cnt);
	DBG_871X_SEL_NL(m, "dbg_carddisable_error_cnt =%d\n", pdbgpriv->dbg_carddisable_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_fw_read_ps_state_fail_cnt =%d\n", pdbgpriv->dbg_fw_read_ps_state_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_leave_ips_fail_cnt =%d\n", pdbgpriv->dbg_leave_ips_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_leave_lps_fail_cnt =%d\n", pdbgpriv->dbg_leave_lps_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_h2c_leave32k_fail_cnt =%d\n", pdbgpriv->dbg_h2c_leave32k_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_diswow_dload_fw_fail_cnt =%d\n", pdbgpriv->dbg_diswow_dload_fw_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_enwow_dload_fw_fail_cnt =%d\n", pdbgpriv->dbg_enwow_dload_fw_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_ips_drvopen_fail_cnt =%d\n", pdbgpriv->dbg_ips_drvopen_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_poll_fail_cnt =%d\n", pdbgpriv->dbg_poll_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_rpwm_toggle_cnt =%d\n", pdbgpriv->dbg_rpwm_toggle_cnt);
	DBG_871X_SEL_NL(m, "dbg_rpwm_timeout_fail_cnt =%d\n", pdbgpriv->dbg_rpwm_timeout_fail_cnt);

	return 0;
}

#ifdef CONFIG_DBG_COUNTER

int proc_get_rx_logs(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct rx_logs *rx_logs = &padapter->rx_logs;

	DBG_871X_SEL_NL(m,
		"intf_rx =%d\n"
		"intf_rx_err_recvframe =%d\n"
		"intf_rx_err_skb =%d\n"
		"intf_rx_report =%d\n"
		"core_rx =%d\n"
		"core_rx_pre =%d\n"
		"core_rx_pre_ver_err =%d\n"
		"core_rx_pre_mgmt =%d\n"
		"core_rx_pre_mgmt_err_80211w =%d\n"
		"core_rx_pre_mgmt_err =%d\n"
		"core_rx_pre_ctrl =%d\n"
		"core_rx_pre_ctrl_err =%d\n"
		"core_rx_pre_data =%d\n"
		"core_rx_pre_data_wapi_seq_err =%d\n"
		"core_rx_pre_data_wapi_key_err =%d\n"
		"core_rx_pre_data_handled =%d\n"
		"core_rx_pre_data_err =%d\n"
		"core_rx_pre_data_unknown =%d\n"
		"core_rx_pre_unknown =%d\n"
		"core_rx_enqueue =%d\n"
		"core_rx_dequeue =%d\n"
		"core_rx_post =%d\n"
		"core_rx_post_decrypt =%d\n"
		"core_rx_post_decrypt_wep =%d\n"
		"core_rx_post_decrypt_tkip =%d\n"
		"core_rx_post_decrypt_aes =%d\n"
		"core_rx_post_decrypt_wapi =%d\n"
		"core_rx_post_decrypt_hw =%d\n"
		"core_rx_post_decrypt_unknown =%d\n"
		"core_rx_post_decrypt_err =%d\n"
		"core_rx_post_defrag_err =%d\n"
		"core_rx_post_portctrl_err =%d\n"
		"core_rx_post_indicate =%d\n"
		"core_rx_post_indicate_in_oder =%d\n"
		"core_rx_post_indicate_reoder =%d\n"
		"core_rx_post_indicate_err =%d\n"
		"os_indicate =%d\n"
		"os_indicate_ap_mcast =%d\n"
		"os_indicate_ap_forward =%d\n"
		"os_indicate_ap_self =%d\n"
		"os_indicate_err =%d\n"
		"os_netif_ok =%d\n"
		"os_netif_err =%d\n",
		rx_logs->intf_rx,
		rx_logs->intf_rx_err_recvframe,
		rx_logs->intf_rx_err_skb,
		rx_logs->intf_rx_report,
		rx_logs->core_rx,
		rx_logs->core_rx_pre,
		rx_logs->core_rx_pre_ver_err,
		rx_logs->core_rx_pre_mgmt,
		rx_logs->core_rx_pre_mgmt_err_80211w,
		rx_logs->core_rx_pre_mgmt_err,
		rx_logs->core_rx_pre_ctrl,
		rx_logs->core_rx_pre_ctrl_err,
		rx_logs->core_rx_pre_data,
		rx_logs->core_rx_pre_data_wapi_seq_err,
		rx_logs->core_rx_pre_data_wapi_key_err,
		rx_logs->core_rx_pre_data_handled,
		rx_logs->core_rx_pre_data_err,
		rx_logs->core_rx_pre_data_unknown,
		rx_logs->core_rx_pre_unknown,
		rx_logs->core_rx_enqueue,
		rx_logs->core_rx_dequeue,
		rx_logs->core_rx_post,
		rx_logs->core_rx_post_decrypt,
		rx_logs->core_rx_post_decrypt_wep,
		rx_logs->core_rx_post_decrypt_tkip,
		rx_logs->core_rx_post_decrypt_aes,
		rx_logs->core_rx_post_decrypt_wapi,
		rx_logs->core_rx_post_decrypt_hw,
		rx_logs->core_rx_post_decrypt_unknown,
		rx_logs->core_rx_post_decrypt_err,
		rx_logs->core_rx_post_defrag_err,
		rx_logs->core_rx_post_portctrl_err,
		rx_logs->core_rx_post_indicate,
		rx_logs->core_rx_post_indicate_in_oder,
		rx_logs->core_rx_post_indicate_reoder,
		rx_logs->core_rx_post_indicate_err,
		rx_logs->os_indicate,
		rx_logs->os_indicate_ap_mcast,
		rx_logs->os_indicate_ap_forward,
		rx_logs->os_indicate_ap_self,
		rx_logs->os_indicate_err,
		rx_logs->os_netif_ok,
		rx_logs->os_netif_err
	);

	return 0;
}

int proc_get_tx_logs(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct tx_logs *tx_logs = &padapter->tx_logs;

	DBG_871X_SEL_NL(m,
		"os_tx =%d\n"
		"os_tx_err_up =%d\n"
		"os_tx_err_xmit =%d\n"
		"os_tx_m2u =%d\n"
		"os_tx_m2u_ignore_fw_linked =%d\n"
		"os_tx_m2u_ignore_self =%d\n"
		"os_tx_m2u_entry =%d\n"
		"os_tx_m2u_entry_err_xmit =%d\n"
		"os_tx_m2u_entry_err_skb =%d\n"
		"os_tx_m2u_stop =%d\n"
		"core_tx =%d\n"
		"core_tx_err_pxmitframe =%d\n"
		"core_tx_err_brtx =%d\n"
		"core_tx_upd_attrib =%d\n"
		"core_tx_upd_attrib_adhoc =%d\n"
		"core_tx_upd_attrib_sta =%d\n"
		"core_tx_upd_attrib_ap =%d\n"
		"core_tx_upd_attrib_unknown =%d\n"
		"core_tx_upd_attrib_dhcp =%d\n"
		"core_tx_upd_attrib_icmp =%d\n"
		"core_tx_upd_attrib_active =%d\n"
		"core_tx_upd_attrib_err_ucast_sta =%d\n"
		"core_tx_upd_attrib_err_ucast_ap_link =%d\n"
		"core_tx_upd_attrib_err_sta =%d\n"
		"core_tx_upd_attrib_err_link =%d\n"
		"core_tx_upd_attrib_err_sec =%d\n"
		"core_tx_ap_enqueue_warn_fwstate =%d\n"
		"core_tx_ap_enqueue_warn_sta =%d\n"
		"core_tx_ap_enqueue_warn_nosta =%d\n"
		"core_tx_ap_enqueue_warn_link =%d\n"
		"core_tx_ap_enqueue_warn_trigger =%d\n"
		"core_tx_ap_enqueue_mcast =%d\n"
		"core_tx_ap_enqueue_ucast =%d\n"
		"core_tx_ap_enqueue =%d\n"
		"intf_tx =%d\n"
		"intf_tx_pending_ac =%d\n"
		"intf_tx_pending_fw_under_survey =%d\n"
		"intf_tx_pending_fw_under_linking =%d\n"
		"intf_tx_pending_xmitbuf =%d\n"
		"intf_tx_enqueue =%d\n"
		"core_tx_enqueue =%d\n"
		"core_tx_enqueue_class =%d\n"
		"core_tx_enqueue_class_err_sta =%d\n"
		"core_tx_enqueue_class_err_nosta =%d\n"
		"core_tx_enqueue_class_err_fwlink =%d\n"
		"intf_tx_direct =%d\n"
		"intf_tx_direct_err_coalesce =%d\n"
		"intf_tx_dequeue =%d\n"
		"intf_tx_dequeue_err_coalesce =%d\n"
		"intf_tx_dump_xframe =%d\n"
		"intf_tx_dump_xframe_err_txdesc =%d\n"
		"intf_tx_dump_xframe_err_port =%d\n",
		tx_logs->os_tx,
		tx_logs->os_tx_err_up,
		tx_logs->os_tx_err_xmit,
		tx_logs->os_tx_m2u,
		tx_logs->os_tx_m2u_ignore_fw_linked,
		tx_logs->os_tx_m2u_ignore_self,
		tx_logs->os_tx_m2u_entry,
		tx_logs->os_tx_m2u_entry_err_xmit,
		tx_logs->os_tx_m2u_entry_err_skb,
		tx_logs->os_tx_m2u_stop,
		tx_logs->core_tx,
		tx_logs->core_tx_err_pxmitframe,
		tx_logs->core_tx_err_brtx,
		tx_logs->core_tx_upd_attrib,
		tx_logs->core_tx_upd_attrib_adhoc,
		tx_logs->core_tx_upd_attrib_sta,
		tx_logs->core_tx_upd_attrib_ap,
		tx_logs->core_tx_upd_attrib_unknown,
		tx_logs->core_tx_upd_attrib_dhcp,
		tx_logs->core_tx_upd_attrib_icmp,
		tx_logs->core_tx_upd_attrib_active,
		tx_logs->core_tx_upd_attrib_err_ucast_sta,
		tx_logs->core_tx_upd_attrib_err_ucast_ap_link,
		tx_logs->core_tx_upd_attrib_err_sta,
		tx_logs->core_tx_upd_attrib_err_link,
		tx_logs->core_tx_upd_attrib_err_sec,
		tx_logs->core_tx_ap_enqueue_warn_fwstate,
		tx_logs->core_tx_ap_enqueue_warn_sta,
		tx_logs->core_tx_ap_enqueue_warn_nosta,
		tx_logs->core_tx_ap_enqueue_warn_link,
		tx_logs->core_tx_ap_enqueue_warn_trigger,
		tx_logs->core_tx_ap_enqueue_mcast,
		tx_logs->core_tx_ap_enqueue_ucast,
		tx_logs->core_tx_ap_enqueue,
		tx_logs->intf_tx,
		tx_logs->intf_tx_pending_ac,
		tx_logs->intf_tx_pending_fw_under_survey,
		tx_logs->intf_tx_pending_fw_under_linking,
		tx_logs->intf_tx_pending_xmitbuf,
		tx_logs->intf_tx_enqueue,
		tx_logs->core_tx_enqueue,
		tx_logs->core_tx_enqueue_class,
		tx_logs->core_tx_enqueue_class_err_sta,
		tx_logs->core_tx_enqueue_class_err_nosta,
		tx_logs->core_tx_enqueue_class_err_fwlink,
		tx_logs->intf_tx_direct,
		tx_logs->intf_tx_direct_err_coalesce,
		tx_logs->intf_tx_dequeue,
		tx_logs->intf_tx_dequeue_err_coalesce,
		tx_logs->intf_tx_dump_xframe,
		tx_logs->intf_tx_dump_xframe_err_txdesc,
		tx_logs->intf_tx_dump_xframe_err_port
	);

	return 0;
}

int proc_get_int_logs(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	DBG_871X_SEL_NL(m,
		"all =%d\n"
		"err =%d\n"
		"tbdok =%d\n"
		"tbder =%d\n"
		"bcnderr =%d\n"
		"bcndma =%d\n"
		"bcndma_e =%d\n"
		"rx =%d\n"
		"rx_rdu =%d\n"
		"rx_fovw =%d\n"
		"txfovw =%d\n"
		"mgntok =%d\n"
		"highdok =%d\n"
		"bkdok =%d\n"
		"bedok =%d\n"
		"vidok =%d\n"
		"vodok =%d\n",
		padapter->int_logs.all,
		padapter->int_logs.err,
		padapter->int_logs.tbdok,
		padapter->int_logs.tbder,
		padapter->int_logs.bcnderr,
		padapter->int_logs.bcndma,
		padapter->int_logs.bcndma_e,
		padapter->int_logs.rx,
		padapter->int_logs.rx_rdu,
		padapter->int_logs.rx_fovw,
		padapter->int_logs.txfovw,
		padapter->int_logs.mgntok,
		padapter->int_logs.highdok,
		padapter->int_logs.bkdok,
		padapter->int_logs.bedok,
		padapter->int_logs.vidok,
		padapter->int_logs.vodok
	);

	return 0;
}

#endif /* CONFIG_DBG_COUNTER*/

int proc_get_rx_signal(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	DBG_871X_SEL_NL(m, "rssi:%d\n", padapter->recvpriv.rssi);
	/*DBG_871X_SEL_NL(m, "rxpwdb:%d\n", padapter->recvpriv.rxpwdb);*/
	DBG_871X_SEL_NL(m, "signal_strength:%u\n", padapter->recvpriv.signal_strength);
	DBG_871X_SEL_NL(m, "signal_qual:%u\n", padapter->recvpriv.signal_qual);
	DBG_871X_SEL_NL(m, "noise:%d\n", padapter->recvpriv.noise);
	rtw_odm_get_perpkt_rssi(m, padapter);
	#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
	rtw_get_raw_rssi_info(m, padapter);
	#endif
	return 0;
}


int proc_get_hw_status(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *dvobj = padapter->dvobj;
	struct debug_priv *pdbgpriv = &dvobj->drv_dbg;

	DBG_871X_SEL_NL(m, "RX FIFO full count: last_time =%lld, current_time =%lld, differential =%lld\n"
	, pdbgpriv->dbg_rx_fifo_last_overflow, pdbgpriv->dbg_rx_fifo_curr_overflow, pdbgpriv->dbg_rx_fifo_diff_overflow);

	return 0;
}

ssize_t proc_set_rx_signal(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 is_signal_dbg, signal_strength;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%u %u", &is_signal_dbg, &signal_strength);

		is_signal_dbg = is_signal_dbg == 0?0:1;

		if (is_signal_dbg && num != 2)
			return count;

		signal_strength = signal_strength > 100?100:signal_strength;

		padapter->recvpriv.is_signal_dbg = is_signal_dbg;
		padapter->recvpriv.signal_strength_dbg =  signal_strength;

		if (is_signal_dbg)
			DBG_871X("set %s %u\n", "DBG_SIGNAL_STRENGTH", signal_strength);
		else
			DBG_871X("set %s\n", "HW_SIGNAL_STRENGTH");

	}

	return count;

}

int proc_get_ht_enable(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;

	if (pregpriv)
		DBG_871X_SEL_NL(m, "%d\n", pregpriv->ht_enable);

	return 0;
}

ssize_t proc_set_ht_enable(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		sscanf(tmp, "%d ", &mode);

		if (pregpriv && mode < 2) {
			pregpriv->ht_enable = mode;
			printk("ht_enable =%d\n", pregpriv->ht_enable);
		}
	}

	return count;

}

int proc_get_bw_mode(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;

	if (pregpriv)
		DBG_871X_SEL_NL(m, "0x%02x\n", pregpriv->bw_mode);

	return 0;
}

ssize_t proc_set_bw_mode(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		sscanf(tmp, "%d ", &mode);

		if (pregpriv &&  mode < 2) {

			pregpriv->bw_mode = mode;
			printk("bw_mode =%d\n", mode);

		}
	}

	return count;

}

int proc_get_ampdu_enable(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;

	if (pregpriv)
		DBG_871X_SEL_NL(m, "%d\n", pregpriv->ampdu_enable);

	return 0;
}

ssize_t proc_set_ampdu_enable(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		sscanf(tmp, "%d ", &mode);

		if (pregpriv && mode < 3) {
			pregpriv->ampdu_enable = mode;
			printk("ampdu_enable =%d\n", mode);
		}

	}

	return count;

}

int proc_get_rx_ampdu(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pregpriv)
		DBG_871X_SEL_NL(m,
			"accept_addba_req = %d , 0:Reject AP's Add BA req, 1:Accept AP's Add BA req.\n",
			pmlmeinfo->accept_addba_req
			);

	return 0;
}

ssize_t proc_set_rx_ampdu(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		sscanf(tmp, "%d ", &mode);

		if (pregpriv && mode < 2) {
			pmlmeinfo->accept_addba_req = mode;
			DBG_871X("pmlmeinfo->accept_addba_req =%d\n",
				 pmlmeinfo->accept_addba_req);
			if (mode == 0) {
				/*tear down Rx AMPDU*/
				send_delba(padapter, 0, get_my_bssid(&(pmlmeinfo->network)));/* recipient*/
			}
		}

	}

	return count;
}

int proc_get_en_fwps(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;

	if (pregpriv)
		DBG_871X_SEL_NL(m, "check_fw_ps = %d , 1:enable get FW PS state , 0: disable get FW PS state\n"
			, pregpriv->check_fw_ps);

	return 0;
}

ssize_t proc_set_en_fwps(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		sscanf(tmp, "%d ", &mode);

		if (pregpriv && mode < 2) {
			pregpriv->check_fw_ps = mode;
			DBG_871X("pregpriv->check_fw_ps =%d\n", pregpriv->check_fw_ps);
		}
	}
	return count;
}

int proc_get_rx_stbc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;

	if (pregpriv)
		DBG_871X_SEL_NL(m, "%d\n", pregpriv->rx_stbc);

	return 0;
}

ssize_t proc_set_rx_stbc(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregpriv = &padapter->registrypriv;
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		sscanf(tmp, "%d ", &mode);

		if (pregpriv && (mode == 0 || mode == 1 ||
		    mode == 2 || mode == 3)) {
			pregpriv->rx_stbc = mode;
			printk("rx_stbc =%d\n", mode);
		}
	}

	return count;

}

int proc_get_rssi_disp(struct seq_file *m, void *v)
{
	return 0;
}

ssize_t proc_set_rssi_disp(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 enable = 0;

	if (count < 1) {
		DBG_8192C("argument size is less than 1\n");
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		int num = sscanf(tmp, "%x", &enable);

		if (num !=  1) {
			DBG_8192C("invalid set_rssi_disp parameter!\n");
			return count;
		}

		if (enable) {
			DBG_8192C("Linked info Function Enable\n");
			padapter->bLinkInfoDump = enable;
		} else {
			DBG_8192C("Linked info Function Disable\n");
			padapter->bLinkInfoDump = 0;
		}
	}
	return count;
}

int proc_get_all_sta_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct sta_info *psta;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct sta_priv *pstapriv = &padapter->stapriv;
	int i, j;
	struct list_head	*plist, *phead;
	struct recv_reorder_ctrl *preorder_ctrl;

	DBG_871X_SEL_NL(m, "sta_dz_bitmap = 0x%x, tim_bitmap = 0x%x\n", pstapriv->sta_dz_bitmap, pstapriv->tim_bitmap);

	spin_lock_bh(&pstapriv->sta_hash_lock);

	for (i = 0; i < NUM_STA; i++) {
		phead = &(pstapriv->sta_hash[i]);
		plist = get_next(phead);

		while (phead != plist) {
			psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

			plist = get_next(plist);

			DBG_871X_SEL_NL(m, "==============================\n");
			DBG_871X_SEL_NL(m, "sta's macaddr:" MAC_FMT "\n",
					MAC_ARG(psta->hwaddr));
			DBG_871X_SEL_NL(m, "rtsen =%d, cts2slef =%d\n",
					psta->rtsen, psta->cts2self);
			DBG_871X_SEL_NL(m, "state = 0x%x, aid =%d, macid =%d, raid =%d\n",
					psta->state, psta->aid, psta->mac_id,
					psta->raid);
			DBG_871X_SEL_NL(m, "qos_en =%d, ht_en =%d, init_rate =%d\n",
					psta->qos_option,
					psta->htpriv.ht_option,
					psta->init_rate);
			DBG_871X_SEL_NL(m, "bwmode =%d, ch_offset =%d, sgi_20m =%d, sgi_40m =%d\n",
					psta->bw_mode, psta->htpriv.ch_offset,
					psta->htpriv.sgi_20m,
					psta->htpriv.sgi_40m);
			DBG_871X_SEL_NL(m, "ampdu_enable = %d\n",
					psta->htpriv.ampdu_enable);
			DBG_871X_SEL_NL(m, "agg_enable_bitmap =%x, candidate_tid_bitmap =%x\n",
					psta->htpriv.agg_enable_bitmap,
					psta->htpriv.candidate_tid_bitmap);
			DBG_871X_SEL_NL(m, "sleepq_len =%d\n",
					psta->sleepq_len);
			DBG_871X_SEL_NL(m, "sta_xmitpriv.vo_q_qcnt =%d\n",
					psta->sta_xmitpriv.vo_q.qcnt);
			DBG_871X_SEL_NL(m, "sta_xmitpriv.vi_q_qcnt =%d\n",
					psta->sta_xmitpriv.vi_q.qcnt);
			DBG_871X_SEL_NL(m, "sta_xmitpriv.be_q_qcnt =%d\n",
					psta->sta_xmitpriv.be_q.qcnt);
			DBG_871X_SEL_NL(m, "sta_xmitpriv.bk_q_qcnt =%d\n",
					psta->sta_xmitpriv.bk_q.qcnt);

			DBG_871X_SEL_NL(m, "capability = 0x%x\n",
					psta->capability);
			DBG_871X_SEL_NL(m, "flags = 0x%x\n", psta->flags);
			DBG_871X_SEL_NL(m, "wpa_psk = 0x%x\n", psta->wpa_psk);
			DBG_871X_SEL_NL(m, "wpa2_group_cipher = 0x%x\n",
					psta->wpa2_group_cipher);
			DBG_871X_SEL_NL(m, "wpa2_pairwise_cipher = 0x%x\n",
					psta->wpa2_pairwise_cipher);
			DBG_871X_SEL_NL(m, "qos_info = 0x%x\n", psta->qos_info);
			DBG_871X_SEL_NL(m, "dot118021XPrivacy = 0x%x\n",
					psta->dot118021XPrivacy);

			for (j = 0; j < 16; j++) {
				preorder_ctrl = &psta->recvreorder_ctrl[j];
				if (preorder_ctrl->enable)
					DBG_871X_SEL_NL(m, "tid =%d, indicate_seq =%d\n",
							j, preorder_ctrl->indicate_seq);
			}
			DBG_871X_SEL_NL(m, "==============================\n");
		}
	}

	spin_unlock_bh(&pstapriv->sta_hash_lock);

	return 0;
}

int proc_get_btcoex_dbg(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter;
	char buf[512] = {0};
	padapter = (struct adapter *)rtw_netdev_priv(dev);

	rtw_btcoex_GetDBG(padapter, buf, 512);

	DBG_871X_SEL(m, "%s", buf);

	return 0;
}

ssize_t proc_set_btcoex_dbg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct adapter *padapter;
	u8 tmp[80] = {0};
	u32 module[2] = {0};
	u32 num;

	padapter = (struct adapter *)rtw_netdev_priv(dev);

/*	DBG_871X("+" FUNC_ADPT_FMT "\n", FUNC_ADPT_ARG(padapter));*/

	if (NULL == buffer) {
		DBG_871X(FUNC_ADPT_FMT ": input buffer is NULL!\n",
			FUNC_ADPT_ARG(padapter));

		return -EFAULT;
	}

	if (count < 1) {
		DBG_871X(FUNC_ADPT_FMT ": input length is 0!\n",
			FUNC_ADPT_ARG(padapter));

		return -EFAULT;
	}

	num = count;
	if (num > (sizeof(tmp) - 1))
		num = (sizeof(tmp) - 1);

	if (copy_from_user(tmp, buffer, num)) {
		DBG_871X(FUNC_ADPT_FMT ": copy buffer from user space FAIL!\n",
			FUNC_ADPT_ARG(padapter));

		return -EFAULT;
	}

	num = sscanf(tmp, "%x %x", module, module+1);
	if (num == 1) {
		if (module[0] == 0)
			memset(module, 0, sizeof(module));
		else
			memset(module, 0xFF, sizeof(module));
	} else if (num != 2) {
		DBG_871X(FUNC_ADPT_FMT ": input(\"%s\") format incorrect!\n",
			FUNC_ADPT_ARG(padapter), tmp);

		if (num == 0)
			return -EFAULT;
	}

	DBG_871X(FUNC_ADPT_FMT ": input 0x%08X 0x%08X\n",
		FUNC_ADPT_ARG(padapter), module[0], module[1]);
	rtw_btcoex_SetDBG(padapter, module);

	return count;
}

int proc_get_btcoex_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct adapter *padapter;
	const u32 bufsize = 30*100;
	u8 *pbuf = NULL;

	padapter = (struct adapter *)rtw_netdev_priv(dev);

	pbuf = rtw_zmalloc(bufsize);
	if (NULL == pbuf) {
		return -ENOMEM;
	}

	rtw_btcoex_DisplayBtCoexInfo(padapter, pbuf, bufsize);

	DBG_871X_SEL(m, "%s\n", pbuf);

	kfree(pbuf);

	return 0;
}

#endif
