/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_DEBUG_C_

#include <drv_types.h>

u32 GlobalDebugLevel = _drv_err_;

#ifdef CONFIG_DEBUG_RTL871X

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

#endif /* CONFIG_DEBUG_RTL871X */

#include <rtw_version.h>

#ifdef CONFIG_TDLS
#define TDLS_DBG_INFO_SPACE_BTWN_ITEM_AND_VALUE	41
#endif

void dump_drv_version(void *sel)
{
	DBG_871X_SEL_NL(sel, "%s %s\n", DRV_NAME, DRIVERVERSION);
	DBG_871X_SEL_NL(sel, "build time: %s %s\n", __DATE__, __TIME__);
}

void dump_log_level(void *sel)
{
	DBG_871X_SEL_NL(sel, "log_level:%d\n", GlobalDebugLevel);
}

#ifdef CONFIG_SDIO_HCI
void sd_f0_reg_dump(void *sel, _adapter *adapter)
{
	int i;

	for(i=0x0;i<=0xff;i++)
	{	
		if(i%16==0)
			DBG_871X_SEL_NL(sel, "0x%02x ",i);

		DBG_871X_SEL(sel, "%02x ", rtw_sd_f0_read8(adapter, i));

		if(i%16==15)
			DBG_871X_SEL(sel, "\n");
		else if(i%8==7)
			DBG_871X_SEL(sel, "\t");
	}
}
#endif /* CONFIG_SDIO_HCI */

void mac_reg_dump(void *sel, _adapter *adapter)
{
	int i, j = 1;

	DBG_871X_SEL_NL(sel, "======= MAC REG =======\n");

	for(i=0x0;i<0x800;i+=4)
	{
		if(j%4==1)
			DBG_871X_SEL_NL(sel, "0x%03x",i);
		DBG_871X_SEL(sel, " 0x%08x ", rtw_read32(adapter,i));
		if((j++)%4 == 0)
			DBG_871X_SEL(sel, "\n");
	}
	
#ifdef CONFIG_RTL8814A
	{
		for(i=0x1000;i<0x1650;i+=4)
		{
			if(j%4==1)
				DBG_871X_SEL_NL(sel, "0x%03x",i);
			DBG_871X_SEL(sel, " 0x%08x ", rtw_read32(adapter,i));
			if((j++)%4 == 0)
				DBG_871X_SEL(sel, "\n");
		}
	}
#endif /* CONFIG_RTL8814A */
}

void bb_reg_dump(void *sel, _adapter *adapter)
{
	int i, j = 1;

	DBG_871X_SEL_NL(sel, "======= BB REG =======\n");
	for(i=0x800;i<0x1000;i+=4)
	{
		if(j%4==1)
			DBG_871X_SEL_NL(sel, "0x%03x",i);
		DBG_871X_SEL(sel, " 0x%08x ", rtw_read32(adapter,i));
		if((j++)%4 == 0)
			DBG_871X_SEL(sel, "\n");
	}
}

void rf_reg_dump(void *sel, _adapter *adapter)
{
	int i, j = 1, path;
	u32 value;
	u8 rf_type = 0;
	u8 path_nums = 0;

	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	if((RF_1T2R == rf_type) ||(RF_1T1R ==rf_type ))
		path_nums = 1;
	else
		path_nums = 2;

	DBG_871X_SEL_NL(sel, "======= RF REG =======\n");

	for (path=0;path<path_nums;path++) {
		DBG_871X_SEL_NL(sel, "RF_Path(%x)\n",path);
		for (i=0;i<0x100;i++) {
			//value = PHY_QueryRFReg(adapter, (RF90_RADIO_PATH_E)path,i, bMaskDWord);
			value = rtw_hal_read_rfreg(adapter, path, i, 0xffffffff);
			if(j%4==1)
				DBG_871X_SEL_NL(sel, "0x%02x ",i);
			DBG_871X_SEL(sel, " 0x%08x ",value);
			if((j++)%4==0)
				DBG_871X_SEL(sel, "\n");
		}
	}
}

static u8 fwdl_test_chksum_fail = 0;
static u8 fwdl_test_wintint_rdy_fail = 0;

bool rtw_fwdl_test_trigger_chksum_fail(void)
{
	if (fwdl_test_chksum_fail) {
		DBG_871X_LEVEL(_drv_always_, "fwdl test case: trigger chksum_fail\n");
		fwdl_test_chksum_fail--;
		return _TRUE;
	}
	return _FALSE;
}

bool rtw_fwdl_test_trigger_wintint_rdy_fail(void)
{
	if (fwdl_test_wintint_rdy_fail) {
		DBG_871X_LEVEL(_drv_always_, "fwdl test case: trigger wintint_rdy_fail\n");
		fwdl_test_wintint_rdy_fail--;
		return _TRUE;
	}
	return _FALSE;
}

static u32 g_wait_hiq_empty_ms = 0;

u32 rtw_get_wait_hiq_empty_ms(void)
{
	return g_wait_hiq_empty_ms;
}

static u8 del_rx_ampdu_test_no_tx_fail = 0;

bool rtw_del_rx_ampdu_test_trigger_no_tx_fail(void)
{
	if (del_rx_ampdu_test_no_tx_fail) {
		DBG_871X_LEVEL(_drv_always_, "del_rx_ampdu test case: trigger no_tx_fail\n");
		del_rx_ampdu_test_no_tx_fail--;
		return _TRUE;
	}
	return _FALSE;
}

void rtw_sink_rtp_seq_dbg( _adapter *adapter,_pkt *pkt)
{
	struct recv_priv *precvpriv = &(adapter->recvpriv);
	if( precvpriv->sink_udpport > 0)
	{
		if(*((u16*)((pkt->data)+0x24)) == cpu_to_be16(precvpriv->sink_udpport))
		{
			precvpriv->pre_rtp_rxseq= precvpriv->cur_rtp_rxseq;
			precvpriv->cur_rtp_rxseq = be16_to_cpu(*((u16*)((pkt->data)+0x2C)));
			if( precvpriv->pre_rtp_rxseq+1 != precvpriv->cur_rtp_rxseq)
				DBG_871X("%s : RTP Seq num from %d to %d\n",__FUNCTION__,precvpriv->pre_rtp_rxseq,precvpriv->cur_rtp_rxseq);
		}
	}
}

void sta_rx_reorder_ctl_dump(void *sel, struct sta_info *sta)
{
	struct recv_reorder_ctrl *reorder_ctl;
	int i;

	for (i = 0; i < 16; i++) {
		reorder_ctl = &sta->recvreorder_ctrl[i];
		if (reorder_ctl->ampdu_size != RX_AMPDU_SIZE_INVALID || reorder_ctl->indicate_seq != 0xFFFF) {
			DBG_871X_SEL_NL(sel, "tid=%d, enable=%d, ampdu_size=%u, indicate_seq=%u\n"
				, i, reorder_ctl->enable, reorder_ctl->ampdu_size, reorder_ctl->indicate_seq
			);
		}
	}
}

#ifdef CONFIG_PROC_DEBUG
ssize_t proc_set_write_reg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 addr, val, len;

	if (count < 3)
	{
		DBG_871X("argument size is less than 3\n");
		return -EFAULT;
	}	

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%x %x %x", &addr, &val, &len);

		if (num !=  3) {
			DBG_871X("invalid write_reg parameter!\n");
			return count;
		}

		switch(len)
		{
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
				DBG_871X("error write length=%d", len);
				break;
		}			
		
	}
	
	return count;
	
}

static u32 proc_get_read_addr=0xeeeeeeee;
static u32 proc_get_read_len=0x4;

int proc_get_read_reg(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	if (proc_get_read_addr==0xeeeeeeee) {
		DBG_871X_SEL_NL(m, "address not initialized\n");
		return 0;
	}	

	switch(proc_get_read_len)
	{
		case 1:			
			DBG_871X_SEL_NL(m, "rtw_read8(0x%x)=0x%x\n", proc_get_read_addr, rtw_read8(padapter, proc_get_read_addr));
			break;
		case 2:
			DBG_871X_SEL_NL(m, "rtw_read16(0x%x)=0x%x\n", proc_get_read_addr, rtw_read16(padapter, proc_get_read_addr));
			break;
		case 4:
			DBG_871X_SEL_NL(m, "rtw_read32(0x%x)=0x%x\n", proc_get_read_addr, rtw_read32(padapter, proc_get_read_addr));
			break;
		default:
			DBG_871X_SEL_NL(m, "error read length=%d\n", proc_get_read_len);
			break;
	}

	return 0;
}

ssize_t proc_set_read_reg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	char tmp[16];
	u32 addr, len;

	if (count < 2)
	{
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
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	DBG_871X_SEL_NL(m, "fwstate=0x%x\n", get_fwstate(pmlmepriv));

	return 0;
}

int proc_get_sec_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct security_priv *sec = &padapter->securitypriv;

	DBG_871X_SEL_NL(m, "auth_alg=0x%x, enc_alg=0x%x, auth_type=0x%x, enc_type=0x%x\n", 
						sec->dot11AuthAlgrthm, sec->dot11PrivacyAlgrthm,
						sec->ndisauthtype, sec->ndisencryptstatus);

	DBG_871X_SEL_NL(m, "hw_decrypted=%d\n", sec->hw_decrypted);

#ifdef DBG_SW_SEC_CNT
	DBG_871X_SEL_NL(m, "wep_sw_enc_cnt=%llu, %llu, %llu\n"
		, sec->wep_sw_enc_cnt_bc , sec->wep_sw_enc_cnt_mc, sec->wep_sw_enc_cnt_uc);
	DBG_871X_SEL_NL(m, "wep_sw_dec_cnt=%llu, %llu, %llu\n"
		, sec->wep_sw_dec_cnt_bc , sec->wep_sw_dec_cnt_mc, sec->wep_sw_dec_cnt_uc);

	DBG_871X_SEL_NL(m, "tkip_sw_enc_cnt=%llu, %llu, %llu\n"
		, sec->tkip_sw_enc_cnt_bc , sec->tkip_sw_enc_cnt_mc, sec->tkip_sw_enc_cnt_uc);	
	DBG_871X_SEL_NL(m, "tkip_sw_dec_cnt=%llu, %llu, %llu\n"
		, sec->tkip_sw_dec_cnt_bc , sec->tkip_sw_dec_cnt_mc, sec->tkip_sw_dec_cnt_uc);

	DBG_871X_SEL_NL(m, "aes_sw_enc_cnt=%llu, %llu, %llu\n"
		, sec->aes_sw_enc_cnt_bc , sec->aes_sw_enc_cnt_mc, sec->aes_sw_enc_cnt_uc);
	DBG_871X_SEL_NL(m, "aes_sw_dec_cnt=%llu, %llu, %llu\n"
		, sec->aes_sw_dec_cnt_bc , sec->aes_sw_dec_cnt_mc, sec->aes_sw_dec_cnt_uc);
#endif /* DBG_SW_SEC_CNT */

	return 0;
}

int proc_get_mlmext_state(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	DBG_871X_SEL_NL(m, "pmlmeinfo->state=0x%x\n", pmlmeinfo->state);

	return 0;
}

#ifdef CONFIG_LAYER2_ROAMING
int proc_get_roam_flags(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	DBG_871X_SEL_NL(m, "0x%02x\n", rtw_roam_flags(adapter));

	return 0;
}

ssize_t proc_set_roam_flags(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

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
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
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
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
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
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	char tmp[32];
	u8 addr[ETH_ALEN];

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", addr, addr+1, addr+2, addr+3, addr+4, addr+5);
		if (num == 6)
			_rtw_memcpy(adapter->mlmepriv.roam_tgt_addr, addr, ETH_ALEN);

		DBG_871X("set roam_tgt_addr to "MAC_FMT"\n", MAC_ARG(adapter->mlmepriv.roam_tgt_addr));
	}

	return count;
}
#endif /* CONFIG_LAYER2_ROAMING */

int proc_get_qos_option(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	DBG_871X_SEL_NL(m, "qos_option=%d\n", pmlmepriv->qospriv.qos_option);

	return 0;
}

int proc_get_ht_option(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	
#ifdef CONFIG_80211N_HT
	DBG_871X_SEL_NL(m, "ht_option=%d\n", pmlmepriv->htpriv.ht_option);
#endif //CONFIG_80211N_HT

	return 0;
}

int proc_get_rf_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;	

	DBG_871X_SEL_NL(m, "cur_ch=%d, cur_bw=%d, cur_ch_offet=%d\n", 
					pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);
	
	DBG_871X_SEL_NL(m, "oper_ch=%d, oper_bw=%d, oper_ch_offet=%d\n", 
					rtw_get_oper_ch(padapter), rtw_get_oper_bw(padapter),  rtw_get_oper_choffset(padapter));

	return 0;
}

int proc_get_survey_info(struct seq_file *m, void *v)
{
	_irqL irqL;
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	struct wlan_network	*pnetwork = NULL;
	_list	*plist, *phead;
	s32 notify_signal;
	s16 notify_noise = 0;
	u16  index = 0;

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);	
	phead = get_list_head(queue);
	if(!phead)
		return 0;
	plist = get_next(phead);
	if (!plist)
		return 0;

	DBG_871X_SEL_NL(m, "%5s  %-17s  %3s  %-3s  %-4s  %-4s  %5s  %s\n","index", "bssid", "ch", "RSSI", "SdBm", "Noise", "age", "ssid");
	while(1)
	{
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);
                if (!pnetwork)
			break;
	
		if ( check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE &&
			is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network, 0)) {
			notify_signal = translate_percentage_to_dbm(padapter->recvpriv.signal_strength);//dbm
		} else {
			notify_signal = translate_percentage_to_dbm(pnetwork->network.PhyInfo.SignalStrength);//dbm
		}

		#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
		rtw_hal_get_odm_var(padapter, HAL_ODM_NOISE_MONITOR,&(pnetwork->network.Configuration.DSConfig), &(notify_noise));
		#endif
	
		DBG_871X_SEL_NL(m, "%5d  "MAC_FMT"  %3d  %3d  %4d  %4d  %5d  %s\n", 
			++index,
			MAC_ARG(pnetwork->network.MacAddress), 
			pnetwork->network.Configuration.DSConfig,
			(int)pnetwork->network.Rssi,
			notify_signal,
			notify_noise,
			rtw_get_passing_time_ms((u32)pnetwork->last_scanned),
			//translate_percentage_to_dbm(pnetwork->network.PhyInfo.SignalStrength),
			pnetwork->network.Ssid.Ssid);
		plist = get_next(plist);
	}
	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	return 0;
}

int proc_get_ap_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct sta_info *psta;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct sta_priv *pstapriv = &padapter->stapriv;

	psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
	if(psta)
	{
		int i;
		struct recv_reorder_ctrl *preorder_ctrl;
					
		DBG_871X_SEL_NL(m, "SSID=%s\n", cur_network->network.Ssid.Ssid);		
		DBG_871X_SEL_NL(m, "sta's macaddr:" MAC_FMT "\n", MAC_ARG(psta->hwaddr));
		DBG_871X_SEL_NL(m, "cur_channel=%d, cur_bwmode=%d, cur_ch_offset=%d\n", pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);		
		DBG_871X_SEL_NL(m, "wireless_mode=0x%x, rtsen=%d, cts2slef=%d\n", psta->wireless_mode, psta->rtsen, psta->cts2self);
		DBG_871X_SEL_NL(m, "state=0x%x, aid=%d, macid=%d, raid=%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
#ifdef CONFIG_80211N_HT
		DBG_871X_SEL_NL(m, "qos_en=%d, ht_en=%d, init_rate=%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);		
		DBG_871X_SEL_NL(m, "bwmode=%d, ch_offset=%d, sgi_20m=%d,sgi_40m=%d\n", psta->bw_mode, psta->htpriv.ch_offset, psta->htpriv.sgi_20m, psta->htpriv.sgi_40m);
		DBG_871X_SEL_NL(m, "ampdu_enable = %d\n", psta->htpriv.ampdu_enable);	
		DBG_871X_SEL_NL(m, "agg_enable_bitmap=%x, candidate_tid_bitmap=%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
		DBG_871X_SEL_NL(m, "ldpc_cap=0x%x, stbc_cap=0x%x, beamform_cap=0x%x\n", psta->htpriv.ldpc_cap, psta->htpriv.stbc_cap, psta->htpriv.beamform_cap);
#endif //CONFIG_80211N_HT
#ifdef CONFIG_80211AC_VHT
		DBG_871X_SEL_NL(m, "vht_en=%d, vht_sgi_80m=%d\n", psta->vhtpriv.vht_option, psta->vhtpriv.sgi_80m);
		DBG_871X_SEL_NL(m, "vht_ldpc_cap=0x%x, vht_stbc_cap=0x%x, vht_beamform_cap=0x%x\n", psta->vhtpriv.ldpc_cap, psta->vhtpriv.stbc_cap, psta->vhtpriv.beamform_cap);
		DBG_871X_SEL_NL(m, "vht_mcs_map=0x%x, vht_highest_rate=0x%x, vht_ampdu_len=%d\n", *(u16*)psta->vhtpriv.vht_mcs_map, psta->vhtpriv.vht_highest_rate, psta->vhtpriv.ampdu_len);
#endif

		sta_rx_reorder_ctl_dump(m, psta);
	}
	else
	{							
		DBG_871X_SEL_NL(m, "can't get sta's macaddr, cur_network's macaddr:" MAC_FMT "\n", MAC_ARG(cur_network->network.MacAddress));
	}

	return 0;
}

int proc_get_adapter_state(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

#ifdef CONFIG_CONCURRENT_MODE
	DBG_871X_SEL_NL(m, "name=%s, iface_type=%d, bSurpriseRemoved=%d, bDriverStopped=%d\n",
					dev->name, padapter->iface_type,
					padapter->bSurpriseRemoved, padapter->bDriverStopped);
#else
	DBG_871X_SEL_NL(m, "name=%s, bSurpriseRemoved=%d, bDriverStopped=%d\n",
					dev->name, padapter->bSurpriseRemoved, padapter->bDriverStopped);
#endif

	return 0;
}

ssize_t proc_reset_trx_info(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	char cmd[32];
	if (buffer && !copy_from_user(cmd, buffer, sizeof(cmd))) {
		if('0' == cmd[0]){
			pdbgpriv->dbg_rx_ampdu_drop_count = 0;
			pdbgpriv->dbg_rx_ampdu_forced_indicate_count = 0;
			pdbgpriv->dbg_rx_ampdu_loss_count = 0;
			pdbgpriv->dbg_rx_dup_mgt_frame_drop_count = 0;
			pdbgpriv->dbg_rx_ampdu_window_shift_cnt = 0;
			pdbgpriv->dbg_rx_conflic_mac_addr_cnt = 0;
		}
	}

	return count;
}
	
int proc_get_trx_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	int i;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct recv_priv  *precvpriv = &padapter->recvpriv;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct hw_xmit *phwxmit;

	dump_os_queue(m, padapter);

	DBG_871X_SEL_NL(m, "free_xmitbuf_cnt=%d, free_xmitframe_cnt=%d\n"
		, pxmitpriv->free_xmitbuf_cnt, pxmitpriv->free_xmitframe_cnt);
	DBG_871X_SEL_NL(m, "free_ext_xmitbuf_cnt=%d, free_xframe_ext_cnt=%d\n"
		, pxmitpriv->free_xmit_extbuf_cnt, pxmitpriv->free_xframe_ext_cnt);
	DBG_871X_SEL_NL(m, "free_recvframe_cnt=%d\n"
		, precvpriv->free_recvframe_cnt);

	for(i = 0; i < 4; i++) 
	{
		phwxmit = pxmitpriv->hwxmits + i;
		DBG_871X_SEL_NL(m, "%d, hwq.accnt=%d\n", i, phwxmit->accnt);
	}

#ifdef CONFIG_USB_HCI
	DBG_871X_SEL_NL(m, "rx_urb_pending_cn=%d\n", ATOMIC_READ(&(precvpriv->rx_pending_cnt)));
#endif

	//Folowing are RX info
	//Counts of packets whose seq_num is less than preorder_ctrl->indicate_seq, Ex delay, retransmission, redundant packets and so on
	DBG_871X_SEL_NL(m,"Rx: Counts of Packets Whose Seq_Num Less Than Reorder Control Seq_Num: %llu\n",(unsigned long long)pdbgpriv->dbg_rx_ampdu_drop_count);
	//How many times the Rx Reorder Timer is triggered.
	DBG_871X_SEL_NL(m,"Rx: Reorder Time-out Trigger Counts: %llu\n",(unsigned long long)pdbgpriv->dbg_rx_ampdu_forced_indicate_count);
	//Total counts of packets loss
	DBG_871X_SEL_NL(m,"Rx: Packet Loss Counts: %llu\n",(unsigned long long)pdbgpriv->dbg_rx_ampdu_loss_count);
	DBG_871X_SEL_NL(m,"Rx: Duplicate Management Frame Drop Count: %llu\n",(unsigned long long)pdbgpriv->dbg_rx_dup_mgt_frame_drop_count);
	DBG_871X_SEL_NL(m,"Rx: AMPDU BA window shift Count: %llu\n",(unsigned long long)pdbgpriv->dbg_rx_ampdu_window_shift_cnt);
	/*The same mac addr counts*/
	DBG_871X_SEL_NL(m, "Rx: Conflict MAC Address Frames Count: %llu\n", (unsigned long long)pdbgpriv->dbg_rx_conflic_mac_addr_cnt);
	return 0;
}

int proc_get_dis_pwt(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	u8 dis_pwt = 0;
	rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DIS_PWT, &(dis_pwt));
	DBG_871X_SEL_NL(m, " Tx Power training mode:%s \n",(dis_pwt==_TRUE)?"Disable":"Enable");
	return 0;
}
ssize_t proc_set_dis_pwt(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[4]={0};
	u8 dis_pwt = 0;
	
	if (count < 1)
		return -EFAULT;
	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%hhx", &dis_pwt);
		DBG_871X("Set Tx Power training mode:%s \n",(dis_pwt==_TRUE)?"Disable":"Enable");
		
		if (num >= 1)
			rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DIS_PWT, &(dis_pwt));
	}

	return count;
	
}

int proc_get_rate_ctl(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	int i;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	u8 data_rate = 0, sgi=0, data_fb = 0;
		
	if (adapter->fix_rate != 0xff) {
		data_rate = adapter->fix_rate & 0x7F;
		sgi = adapter->fix_rate >>7;
		data_fb = adapter->data_fb?1:0;
		DBG_871X_SEL_NL(m, "FIXED %s%s%s\n"
			, HDATA_RATE(data_rate)
			, data_rate>DESC_RATE54M?(sgi?" SGI":" LGI"):""
			, data_fb?" FB":""
		);
		DBG_871X_SEL_NL(m, "0x%02x %u\n", adapter->fix_rate, adapter->data_fb);
	} else {
		DBG_871X_SEL_NL(m, "RA\n");
	}

	return 0;
}

ssize_t proc_set_rate_ctl(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u8 fix_rate;
	u8 data_fb;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%hhx %hhu", &fix_rate, &data_fb);

		if (num >= 1)
			adapter->fix_rate = fix_rate;
		if (num >= 2)
			adapter->data_fb = data_fb?1:0;
	}

	return count;
}
#ifdef DBG_RX_COUNTER_DUMP
int proc_get_rx_cnt_dump(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	int i;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	DBG_871X_SEL_NL(m, "BIT0- Dump RX counters of DRV \n");
	DBG_871X_SEL_NL(m, "BIT1- Dump RX counters of MAC \n");
	DBG_871X_SEL_NL(m, "BIT2- Dump RX counters of PHY \n");
	DBG_871X_SEL_NL(m, "BIT3- Dump TRX data frame of DRV \n");
	DBG_871X_SEL_NL(m, "dump_rx_cnt_mode = 0x%02x \n", adapter->dump_rx_cnt_mode);

	return 0;
}
ssize_t proc_set_rx_cnt_dump(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u8 dump_rx_cnt_mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%hhx", &dump_rx_cnt_mode);

		rtw_dump_phy_rxcnts_preprocess(adapter,dump_rx_cnt_mode);
		adapter->dump_rx_cnt_mode = dump_rx_cnt_mode;
		
	}

	return count;
}
#endif
ssize_t proc_set_fwdl_test_case(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		
		int num = sscanf(tmp, "%hhu %hhu", &fwdl_test_chksum_fail, &fwdl_test_wintint_rdy_fail);
	}

	return count;
}

ssize_t proc_set_del_rx_ampdu_test_case(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	int num;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp)))
		num = sscanf(tmp, "%hhu", &del_rx_ampdu_test_no_tx_fail);

	return count;
}

ssize_t proc_set_wait_hiq_empty(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		int num = sscanf(tmp, "%u", &g_wait_hiq_empty_ms);
	}

	return count;
}

int proc_get_suspend_resume_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *dvobj = padapter->dvobj;
	struct debug_priv *pdbgpriv = &dvobj->drv_dbg;

	DBG_871X_SEL_NL(m, "dbg_sdio_alloc_irq_cnt=%d\n", pdbgpriv->dbg_sdio_alloc_irq_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_free_irq_cnt=%d\n", pdbgpriv->dbg_sdio_free_irq_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_alloc_irq_error_cnt=%d\n",pdbgpriv->dbg_sdio_alloc_irq_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_free_irq_error_cnt=%d\n", pdbgpriv->dbg_sdio_free_irq_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_init_error_cnt=%d\n",pdbgpriv->dbg_sdio_init_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_sdio_deinit_error_cnt=%d\n", pdbgpriv->dbg_sdio_deinit_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_suspend_error_cnt=%d\n", pdbgpriv->dbg_suspend_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_suspend_cnt=%d\n",pdbgpriv->dbg_suspend_cnt);
	DBG_871X_SEL_NL(m, "dbg_resume_cnt=%d\n", pdbgpriv->dbg_resume_cnt);
	DBG_871X_SEL_NL(m, "dbg_resume_error_cnt=%d\n", pdbgpriv->dbg_resume_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_deinit_fail_cnt=%d\n",pdbgpriv->dbg_deinit_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_carddisable_cnt=%d\n", pdbgpriv->dbg_carddisable_cnt);
	DBG_871X_SEL_NL(m, "dbg_ps_insuspend_cnt=%d\n",pdbgpriv->dbg_ps_insuspend_cnt);
	DBG_871X_SEL_NL(m, "dbg_dev_unload_inIPS_cnt=%d\n", pdbgpriv->dbg_dev_unload_inIPS_cnt);
	DBG_871X_SEL_NL(m, "dbg_scan_pwr_state_cnt=%d\n", pdbgpriv->dbg_scan_pwr_state_cnt);
	DBG_871X_SEL_NL(m, "dbg_downloadfw_pwr_state_cnt=%d\n", pdbgpriv->dbg_downloadfw_pwr_state_cnt);
	DBG_871X_SEL_NL(m, "dbg_carddisable_error_cnt=%d\n", pdbgpriv->dbg_carddisable_error_cnt);
	DBG_871X_SEL_NL(m, "dbg_fw_read_ps_state_fail_cnt=%d\n", pdbgpriv->dbg_fw_read_ps_state_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_leave_ips_fail_cnt=%d\n", pdbgpriv->dbg_leave_ips_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_leave_lps_fail_cnt=%d\n", pdbgpriv->dbg_leave_lps_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_h2c_leave32k_fail_cnt=%d\n", pdbgpriv->dbg_h2c_leave32k_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_diswow_dload_fw_fail_cnt=%d\n", pdbgpriv->dbg_diswow_dload_fw_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_enwow_dload_fw_fail_cnt=%d\n", pdbgpriv->dbg_enwow_dload_fw_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_ips_drvopen_fail_cnt=%d\n", pdbgpriv->dbg_ips_drvopen_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_poll_fail_cnt=%d\n", pdbgpriv->dbg_poll_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_rpwm_toogle_cnt=%d\n", pdbgpriv->dbg_rpwm_toogle_cnt);
	DBG_871X_SEL_NL(m, "dbg_rpwm_timeout_fail_cnt=%d\n", pdbgpriv->dbg_rpwm_timeout_fail_cnt);
	DBG_871X_SEL_NL(m, "dbg_sreset_cnt=%d\n", pdbgpriv->dbg_sreset_cnt);

	return 0;
}

#ifdef CONFIG_DBG_COUNTER

int proc_get_rx_logs(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct rx_logs *rx_logs = &padapter->rx_logs;

	DBG_871X_SEL_NL(m, 
		"intf_rx=%d\n"
		"intf_rx_err_recvframe=%d\n"
		"intf_rx_err_skb=%d\n"
		"intf_rx_report=%d\n"
		"core_rx=%d\n"
		"core_rx_pre=%d\n"
		"core_rx_pre_ver_err=%d\n"
		"core_rx_pre_mgmt=%d\n"
		"core_rx_pre_mgmt_err_80211w=%d\n"
		"core_rx_pre_mgmt_err=%d\n"
		"core_rx_pre_ctrl=%d\n"
		"core_rx_pre_ctrl_err=%d\n"
		"core_rx_pre_data=%d\n"
		"core_rx_pre_data_wapi_seq_err=%d\n"
		"core_rx_pre_data_wapi_key_err=%d\n"
		"core_rx_pre_data_handled=%d\n"
		"core_rx_pre_data_err=%d\n"
		"core_rx_pre_data_unknown=%d\n"
		"core_rx_pre_unknown=%d\n"
		"core_rx_enqueue=%d\n"
		"core_rx_dequeue=%d\n"
		"core_rx_post=%d\n"
		"core_rx_post_decrypt=%d\n"
		"core_rx_post_decrypt_wep=%d\n"
		"core_rx_post_decrypt_tkip=%d\n"
		"core_rx_post_decrypt_aes=%d\n"
		"core_rx_post_decrypt_wapi=%d\n"
		"core_rx_post_decrypt_hw=%d\n"
		"core_rx_post_decrypt_unknown=%d\n"
		"core_rx_post_decrypt_err=%d\n"
		"core_rx_post_defrag_err=%d\n"
		"core_rx_post_portctrl_err=%d\n"
		"core_rx_post_indicate=%d\n"
		"core_rx_post_indicate_in_oder=%d\n"
		"core_rx_post_indicate_reoder=%d\n"
		"core_rx_post_indicate_err=%d\n"
		"os_indicate=%d\n"
		"os_indicate_ap_mcast=%d\n"
		"os_indicate_ap_forward=%d\n"
		"os_indicate_ap_self=%d\n"
		"os_indicate_err=%d\n"
		"os_netif_ok=%d\n"
		"os_netif_err=%d\n",
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
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct tx_logs *tx_logs = &padapter->tx_logs;
	
	DBG_871X_SEL_NL(m,
		"os_tx=%d\n"
		"os_tx_err_up=%d\n"
		"os_tx_err_xmit=%d\n"
		"os_tx_m2u=%d\n"
		"os_tx_m2u_ignore_fw_linked=%d\n"
		"os_tx_m2u_ignore_self=%d\n"
		"os_tx_m2u_entry=%d\n"
		"os_tx_m2u_entry_err_xmit=%d\n"
		"os_tx_m2u_entry_err_skb=%d\n"
		"os_tx_m2u_stop=%d\n"
		"core_tx=%d\n"
		"core_tx_err_pxmitframe=%d\n"
		"core_tx_err_brtx=%d\n"
		"core_tx_upd_attrib=%d\n"
		"core_tx_upd_attrib_adhoc=%d\n"
		"core_tx_upd_attrib_sta=%d\n"
		"core_tx_upd_attrib_ap=%d\n"
		"core_tx_upd_attrib_unknown=%d\n"
		"core_tx_upd_attrib_dhcp=%d\n"
		"core_tx_upd_attrib_icmp=%d\n"
		"core_tx_upd_attrib_active=%d\n"
		"core_tx_upd_attrib_err_ucast_sta=%d\n"
		"core_tx_upd_attrib_err_ucast_ap_link=%d\n"
		"core_tx_upd_attrib_err_sta=%d\n"
		"core_tx_upd_attrib_err_link=%d\n"
		"core_tx_upd_attrib_err_sec=%d\n"
		"core_tx_ap_enqueue_warn_fwstate=%d\n"
		"core_tx_ap_enqueue_warn_sta=%d\n"
		"core_tx_ap_enqueue_warn_nosta=%d\n"
		"core_tx_ap_enqueue_warn_link=%d\n"
		"core_tx_ap_enqueue_warn_trigger=%d\n"
		"core_tx_ap_enqueue_mcast=%d\n"
		"core_tx_ap_enqueue_ucast=%d\n"
		"core_tx_ap_enqueue=%d\n"
		"intf_tx=%d\n"
		"intf_tx_pending_ac=%d\n"
		"intf_tx_pending_fw_under_survey=%d\n"
		"intf_tx_pending_fw_under_linking=%d\n"
		"intf_tx_pending_xmitbuf=%d\n"
		"intf_tx_enqueue=%d\n"
		"core_tx_enqueue=%d\n"
		"core_tx_enqueue_class=%d\n"
		"core_tx_enqueue_class_err_sta=%d\n"
		"core_tx_enqueue_class_err_nosta=%d\n"
		"core_tx_enqueue_class_err_fwlink=%d\n"
		"intf_tx_direct=%d\n"
		"intf_tx_direct_err_coalesce=%d\n"
		"intf_tx_dequeue=%d\n"
		"intf_tx_dequeue_err_coalesce=%d\n"
		"intf_tx_dump_xframe=%d\n"
		"intf_tx_dump_xframe_err_txdesc=%d\n"
		"intf_tx_dump_xframe_err_port=%d\n",
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
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	DBG_871X_SEL_NL(m,
		"all=%d\n"
		"err=%d\n"
		"tbdok=%d\n"
		"tbder=%d\n"
		"bcnderr=%d\n"
		"bcndma=%d\n"
		"bcndma_e=%d\n"
		"rx=%d\n"
		"rx_rdu=%d\n"
		"rx_fovw=%d\n"
		"txfovw=%d\n"
		"mgntok=%d\n"
		"highdok=%d\n"
		"bkdok=%d\n"
		"bedok=%d\n"
		"vidok=%d\n"
		"vodok=%d\n",
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

#endif // CONFIG_DBG_COUNTER

int proc_get_hw_status(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *dvobj = padapter->dvobj;
	struct debug_priv *pdbgpriv = &dvobj->drv_dbg;

	DBG_871X_SEL_NL(m, "RX FIFO full count: last_time=%lld, current_time=%lld, differential=%lld\n"
	, pdbgpriv->dbg_rx_fifo_last_overflow, pdbgpriv->dbg_rx_fifo_curr_overflow, pdbgpriv->dbg_rx_fifo_diff_overflow);

	return 0;
}

int proc_get_rx_signal(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	DBG_871X_SEL_NL(m, "rssi:%d\n", padapter->recvpriv.rssi);
	//DBG_871X_SEL_NL(m, "rxpwdb:%d\n", padapter->recvpriv.rxpwdb);
	DBG_871X_SEL_NL(m, "signal_strength:%u\n", padapter->recvpriv.signal_strength);
	DBG_871X_SEL_NL(m, "signal_qual:%u\n", padapter->recvpriv.signal_qual);

	rtw_get_noise(padapter);
	DBG_871X_SEL_NL(m, "noise:%d\n", padapter->recvpriv.noise);
	#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
	rtw_odm_get_perpkt_rssi(m,padapter);
	rtw_get_raw_rssi_info(m,padapter);
	#endif
	return 0;
}

ssize_t proc_set_rx_signal(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 is_signal_dbg, signal_strength;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%u %u", &is_signal_dbg, &signal_strength);

		is_signal_dbg = is_signal_dbg==0?0:1;
		
		if(is_signal_dbg && num!=2)
			return count;
			
		signal_strength = signal_strength>100?100:signal_strength;

		padapter->recvpriv.is_signal_dbg = is_signal_dbg;
		padapter->recvpriv.signal_strength_dbg=signal_strength;

		if(is_signal_dbg)
			DBG_871X("set %s %u\n", "DBG_SIGNAL_STRENGTH", signal_strength);
		else
			DBG_871X("set %s\n", "HW_SIGNAL_STRENGTH");
		
	}
	
	return count;
	
}
#ifdef CONFIG_80211N_HT

int proc_get_ht_enable(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	if(pregpriv)
		DBG_871X_SEL_NL(m, "%d\n", pregpriv->ht_enable);

	return 0;
}

ssize_t proc_set_ht_enable(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%d ", &mode);

		if( pregpriv && mode < 2 )
		{
			pregpriv->ht_enable= mode;
			DBG_871X("ht_enable=%d\n", pregpriv->ht_enable);
		}
	}
	
	return count;
	
}

int proc_get_bw_mode(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	if(pregpriv)
		DBG_871X_SEL_NL(m, "0x%02x\n", pregpriv->bw_mode);

	return 0;
}

ssize_t proc_set_bw_mode(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%d ", &mode);

		if( pregpriv &&  mode < 2 )
		{

			pregpriv->bw_mode = mode;
			printk("bw_mode=%d\n", mode);

		}
	}
	
	return count;
	
}

int proc_get_ampdu_enable(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	if(pregpriv)
		DBG_871X_SEL_NL(m, "%d\n", pregpriv->ampdu_enable);

	return 0;
}

ssize_t proc_set_ampdu_enable(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%d ", &mode);

		if( pregpriv && mode < 3 )
		{
			pregpriv->ampdu_enable= mode;
			printk("ampdu_enable=%d\n", mode);
		}

	}
	
	return count;
	
}

int proc_get_rx_ampdu(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	DBG_871X_SEL(m, "accept: ");
	if (padapter->fix_rx_ampdu_accept == RX_AMPDU_ACCEPT_INVALID)
		DBG_871X_SEL_NL(m, "%u%s\n", rtw_rx_ampdu_is_accept(padapter), "(auto)");
	else
		DBG_871X_SEL_NL(m, "%u%s\n", padapter->fix_rx_ampdu_accept, "(fixed)");

	DBG_871X_SEL(m, "size: ");
	if (padapter->fix_rx_ampdu_size == RX_AMPDU_SIZE_INVALID)
		DBG_871X_SEL_NL(m, "%u%s\n", rtw_rx_ampdu_size(padapter), "(auto)");
	else
		DBG_871X_SEL_NL(m, "%u%s\n", padapter->fix_rx_ampdu_size, "(fixed)");

	DBG_871X_SEL_NL(m, "%19s %17s\n", "fix_rx_ampdu_accept", "fix_rx_ampdu_size");

	DBG_871X_SEL(m, "%-19d %-17u\n"
		, padapter->fix_rx_ampdu_accept
		, padapter->fix_rx_ampdu_size);

	return 0;
}

ssize_t proc_set_rx_ampdu(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	char tmp[32];
	u8 accept;
	u8 size;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%hhu %hhu", &accept, &size);

		if (num >= 1)
			rtw_rx_ampdu_set_accept(padapter, accept, RX_AMPDU_DRV_FIXED);
		if (num >= 2)
			rtw_rx_ampdu_set_size(padapter, size, RX_AMPDU_DRV_FIXED);

		rtw_rx_ampdu_apply(padapter);
	}

exit:
	return count;
}
int proc_get_rx_ampdu_factor(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);


	if(padapter)
	{
		DBG_871X_SEL_NL(m,"rx ampdu factor = %x\n",padapter->driver_rx_ampdu_factor);
	}
	
	return 0;
}

ssize_t proc_set_rx_ampdu_factor(struct file *file, const char __user *buffer
                                 , size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 factor;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp)))
	{

		int num = sscanf(tmp, "%d ", &factor);

		if( padapter && (num == 1) )
		{
			DBG_871X("padapter->driver_rx_ampdu_factor = %x\n", factor);

			if(factor  > 0x03)
				padapter->driver_rx_ampdu_factor = 0xFF;
			else
				padapter->driver_rx_ampdu_factor = factor;			
		}
	}

	return count;
}

int proc_get_rx_ampdu_density(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);


	if(padapter)
	{
		DBG_871X_SEL_NL(m,"rx ampdu densityg = %x\n",padapter->driver_rx_ampdu_spacing);
	}

	return 0;
}

ssize_t proc_set_rx_ampdu_density(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 density;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp)))
	{

		int num = sscanf(tmp, "%d ", &density);

		if( padapter && (num == 1) )
		{
			DBG_871X("padapter->driver_rx_ampdu_spacing = %x\n", density);

			if(density > 0x07)
				padapter->driver_rx_ampdu_spacing = 0xFF;
			else
				padapter->driver_rx_ampdu_spacing = density;
		}
	}

	return count;
}

int proc_get_tx_ampdu_density(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);


	if(padapter)
	{
		DBG_871X_SEL_NL(m,"tx ampdu density = %x\n",padapter->driver_ampdu_spacing);
	}

	return 0;
}

ssize_t proc_set_tx_ampdu_density(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 density;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp)))
	{

		int num = sscanf(tmp, "%d ", &density);

		if( padapter && (num == 1) )
		{
			DBG_871X("padapter->driver_ampdu_spacing = %x\n", density);

			if(density > 0x07)
				padapter->driver_ampdu_spacing = 0xFF;
			else
				padapter->driver_ampdu_spacing = density;
		}
	}

	return count;
}
#endif //CONFIG_80211N_HT

int proc_get_en_fwps(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(pregpriv)
		DBG_871X_SEL_NL(m, "check_fw_ps = %d , 1:enable get FW PS state , 0: disable get FW PS state\n"
			, pregpriv->check_fw_ps);

	return 0;
}

ssize_t proc_set_en_fwps(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {

		int num = sscanf(tmp, "%d ", &mode);

		if( pregpriv &&  mode < 2 )
		{
			pregpriv->check_fw_ps = mode;
			DBG_871X("pregpriv->check_fw_ps=%d \n",pregpriv->check_fw_ps);
		}

	}

	return count;
}

/*
int proc_get_two_path_rssi(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	if(padapter)
		DBG_871X_SEL_NL(m, "%d %d\n",
			padapter->recvpriv.RxRssi[0], padapter->recvpriv.RxRssi[1]);

	return 0;
}
*/
#ifdef CONFIG_80211N_HT
int proc_get_rx_stbc(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	if(pregpriv)
		DBG_871X_SEL_NL(m, "%d\n", pregpriv->rx_stbc);

	return 0;
}

ssize_t proc_set_rx_stbc(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	char tmp[32];
	u32 mode;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%d ", &mode);

		if( pregpriv && (mode == 0 || mode == 1|| mode == 2|| mode == 3))
		{
			pregpriv->rx_stbc= mode;
			printk("rx_stbc=%d\n", mode);
		}
	}
	
	return count;
	
}
#endif //CONFIG_80211N_HT

/*int proc_get_rssi_disp(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	return 0;
}
*/

/*ssize_t proc_set_rssi_disp(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 enable=0;

	if (count < 1)
	{
		DBG_8192C("argument size is less than 1\n");
		return -EFAULT;
	}	

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%x", &enable);

		if (num !=  1) {
			DBG_8192C("invalid set_rssi_disp parameter!\n");
			return count;
		}
		
		if(enable)
		{			
			DBG_8192C("Linked info Function Enable\n");
			padapter->bLinkInfoDump = enable ;			
		}
		else
		{
			DBG_8192C("Linked info Function Disable\n");
			padapter->bLinkInfoDump = 0 ;
		}
	
	}
	
	return count;
	
}	

*/		
#ifdef CONFIG_AP_MODE

int proc_get_all_sta_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_irqL irqL;
	struct sta_info *psta;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct sta_priv *pstapriv = &padapter->stapriv;
	int i;
	_list	*plist, *phead;

	DBG_871X_SEL_NL(m, "sta_dz_bitmap=0x%x, tim_bitmap=0x%x\n", pstapriv->sta_dz_bitmap, pstapriv->tim_bitmap);

	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	for(i=0; i< NUM_STA; i++)
	{
		phead = &(pstapriv->sta_hash[i]);
		plist = get_next(phead);
		
		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
		{
			psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

			plist = get_next(plist);

			//if(extra_arg == psta->aid)
			{
				DBG_871X_SEL_NL(m, "==============================\n");
				DBG_871X_SEL_NL(m, "sta's macaddr:" MAC_FMT "\n", MAC_ARG(psta->hwaddr));
				DBG_871X_SEL_NL(m, "rtsen=%d, cts2slef=%d\n", psta->rtsen, psta->cts2self);
				DBG_871X_SEL_NL(m, "state=0x%x, aid=%d, macid=%d, raid=%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
#ifdef CONFIG_80211N_HT
				DBG_871X_SEL_NL(m, "qos_en=%d, ht_en=%d, init_rate=%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);	
				DBG_871X_SEL_NL(m, "bwmode=%d, ch_offset=%d, sgi_20m=%d,sgi_40m=%d\n", psta->bw_mode, psta->htpriv.ch_offset, psta->htpriv.sgi_20m, psta->htpriv.sgi_40m);
				DBG_871X_SEL_NL(m, "ampdu_enable = %d\n", psta->htpriv.ampdu_enable);									
				DBG_871X_SEL_NL(m, "agg_enable_bitmap=%x, candidate_tid_bitmap=%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
#endif //CONFIG_80211N_HT
				DBG_871X_SEL_NL(m, "sleepq_len=%d\n", psta->sleepq_len);
				DBG_871X_SEL_NL(m, "sta_xmitpriv.vo_q_qcnt=%d\n", psta->sta_xmitpriv.vo_q.qcnt);
				DBG_871X_SEL_NL(m, "sta_xmitpriv.vi_q_qcnt=%d\n", psta->sta_xmitpriv.vi_q.qcnt);
				DBG_871X_SEL_NL(m, "sta_xmitpriv.be_q_qcnt=%d\n", psta->sta_xmitpriv.be_q.qcnt);
				DBG_871X_SEL_NL(m, "sta_xmitpriv.bk_q_qcnt=%d\n", psta->sta_xmitpriv.bk_q.qcnt);

				DBG_871X_SEL_NL(m, "capability=0x%x\n", psta->capability);
				DBG_871X_SEL_NL(m, "flags=0x%x\n", psta->flags);
				DBG_871X_SEL_NL(m, "wpa_psk=0x%x\n", psta->wpa_psk);
				DBG_871X_SEL_NL(m, "wpa2_group_cipher=0x%x\n", psta->wpa2_group_cipher);
				DBG_871X_SEL_NL(m, "wpa2_pairwise_cipher=0x%x\n", psta->wpa2_pairwise_cipher);
				DBG_871X_SEL_NL(m, "qos_info=0x%x\n", psta->qos_info);
				DBG_871X_SEL_NL(m, "dot118021XPrivacy=0x%x\n", psta->dot118021XPrivacy);

				sta_rx_reorder_ctl_dump(m, psta);

#ifdef CONFIG_TDLS
				DBG_871X_SEL_NL(m, "tdls_sta_state=0x%08x\n", psta->tdls_sta_state);
				DBG_871X_SEL_NL(m, "PeerKey_Lifetime=%d\n", psta->TDLS_PeerKey_Lifetime);
				DBG_871X_SEL_NL(m, "rx_data_pkts=%llu\n", psta->sta_stats.rx_data_pkts);
				DBG_871X_SEL_NL(m, "rx_bytes=%llu\n", psta->sta_stats.rx_bytes);
				DBG_871X_SEL_NL(m, "tx_data_pkts=%llu\n", psta->sta_stats.tx_pkts);
				DBG_871X_SEL_NL(m, "tx_bytes=%llu\n", psta->sta_stats.tx_bytes);
#endif //CONFIG_TDLS
				DBG_871X_SEL_NL(m, "==============================\n");
			}

		}

	}

	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	return 0;
}
	
#endif		

#ifdef DBG_MEMORY_LEAK
#include <asm/atomic.h>
extern atomic_t _malloc_cnt;;
extern atomic_t _malloc_size;;

int proc_get_malloc_cnt(struct seq_file *m, void *v)
{
	DBG_871X_SEL_NL(m, "_malloc_cnt=%d\n", atomic_read(&_malloc_cnt));
	DBG_871X_SEL_NL(m, "_malloc_size=%d\n", atomic_read(&_malloc_size));

	return 0;
}
#endif /* DBG_MEMORY_LEAK */

#ifdef CONFIG_FIND_BEST_CHANNEL
int proc_get_best_channel(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	u32 i, best_channel_24G = 1, best_channel_5G = 36, index_24G = 0, index_5G = 0;

	for (i=0; pmlmeext->channel_set[i].ChannelNum !=0; i++) {
		if ( pmlmeext->channel_set[i].ChannelNum == 1)
			index_24G = i;
		if ( pmlmeext->channel_set[i].ChannelNum == 36)
			index_5G = i;
	}	
	
	for (i=0; (i < MAX_CHANNEL_NUM) && (pmlmeext->channel_set[i].ChannelNum !=0) ; i++) {
		// 2.4G
		if ( pmlmeext->channel_set[i].ChannelNum == 6 ) {
			if ( pmlmeext->channel_set[i].rx_count < pmlmeext->channel_set[index_24G].rx_count ) {
				index_24G = i;
				best_channel_24G = pmlmeext->channel_set[i].ChannelNum;
			}
		}

		// 5G
		if ( pmlmeext->channel_set[i].ChannelNum >= 36
			&& pmlmeext->channel_set[i].ChannelNum < 140 ) {
			 // Find primary channel
			if ( (( pmlmeext->channel_set[i].ChannelNum - 36) % 8 == 0)
				&& (pmlmeext->channel_set[i].rx_count < pmlmeext->channel_set[index_5G].rx_count) ) {
				index_5G = i;
				best_channel_5G = pmlmeext->channel_set[i].ChannelNum;
			}
		}

		if ( pmlmeext->channel_set[i].ChannelNum >= 149
			&& pmlmeext->channel_set[i].ChannelNum < 165) {
			 // find primary channel
			if ( (( pmlmeext->channel_set[i].ChannelNum - 149) % 8 == 0)
				&& (pmlmeext->channel_set[i].rx_count < pmlmeext->channel_set[index_5G].rx_count) ) {
				index_5G = i;
				best_channel_5G = pmlmeext->channel_set[i].ChannelNum;
			}
		}
#if 1 // debug
		DBG_871X_SEL_NL(m, "The rx cnt of channel %3d = %d\n", 
					pmlmeext->channel_set[i].ChannelNum, pmlmeext->channel_set[i].rx_count);
#endif
	}
	
	DBG_871X_SEL_NL(m, "best_channel_5G = %d\n", best_channel_5G);
	DBG_871X_SEL_NL(m, "best_channel_24G = %d\n", best_channel_24G);

	return 0;
}

ssize_t proc_set_best_channel(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	char tmp[32];

	if(count < 1)
		return -EFAULT;

	if(buffer && !copy_from_user(tmp, buffer, sizeof(tmp)))
	{
		int i;
		for(i = 0; pmlmeext->channel_set[i].ChannelNum != 0; i++)
		{
			pmlmeext->channel_set[i].rx_count = 0;
		}

		DBG_871X("set %s\n", "Clean Best Channel Count");
	}

	return count;
}
#endif /* CONFIG_FIND_BEST_CHANNEL */

#ifdef CONFIG_BT_COEXIST
int proc_get_btcoex_dbg(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	PADAPTER padapter;
	char buf[512] = {0};
	padapter = (PADAPTER)rtw_netdev_priv(dev);

	rtw_btcoex_GetDBG(padapter, buf, 512);

	DBG_871X_SEL(m, "%s", buf);

	return 0;
}

ssize_t proc_set_btcoex_dbg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	PADAPTER padapter;
	u8 tmp[80] = {0};
	u32 module[2] = {0};
	u32 num;

	padapter = (PADAPTER)rtw_netdev_priv(dev);

//	DBG_871X("+" FUNC_ADPT_FMT "\n", FUNC_ADPT_ARG(padapter));

	if (NULL == buffer)
	{
		DBG_871X(FUNC_ADPT_FMT ": input buffer is NULL!\n",
			FUNC_ADPT_ARG(padapter));
		
		return -EFAULT;
	}

	if (count < 1)
	{
		DBG_871X(FUNC_ADPT_FMT ": input length is 0!\n",
			FUNC_ADPT_ARG(padapter));

		return -EFAULT;
	}

	num = count;
	if (num > (sizeof(tmp) - 1))
		num = (sizeof(tmp) - 1);

	if (copy_from_user(tmp, buffer, num))
	{
		DBG_871X(FUNC_ADPT_FMT ": copy buffer from user space FAIL!\n",
			FUNC_ADPT_ARG(padapter));

		return -EFAULT;
	}

	num = sscanf(tmp, "%x %x", module, module+1);
	if (1 == num)
	{
		if (0 == module[0])
			_rtw_memset(module, 0, sizeof(module));
		else
			_rtw_memset(module, 0xFF, sizeof(module));
	}
	else if (2 != num)
	{
		DBG_871X(FUNC_ADPT_FMT ": input(\"%s\") format incorrect!\n",
			FUNC_ADPT_ARG(padapter), tmp);

		if (0 == num)
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
	PADAPTER padapter;
	const u32 bufsize = 30*100;
	u8 *pbuf = NULL;

	padapter = (PADAPTER)rtw_netdev_priv(dev);

	pbuf = rtw_zmalloc(bufsize);
	if (NULL == pbuf) {
		return -ENOMEM;
	}

	rtw_btcoex_DisplayBtCoexInfo(padapter, pbuf, bufsize);

	DBG_871X_SEL(m, "%s\n", pbuf);
	
	rtw_mfree(pbuf, bufsize);

	return 0;
}
#endif /* CONFIG_BT_COEXIST */

#if defined(DBG_CONFIG_ERROR_DETECT)
int proc_get_sreset(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	return 0;
}

ssize_t proc_set_sreset(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	s32 trigger_point;

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		

		int num = sscanf(tmp, "%d", &trigger_point);

		if (trigger_point == SRESET_TGP_NULL)
			rtw_hal_sreset_reset(padapter);
		else
			sreset_set_trigger_point(padapter, trigger_point);
	}
	
	return count;
	
}
#endif /* DBG_CONFIG_ERROR_DETECT */

#ifdef CONFIG_PCI_HCI

int proc_get_rx_ring(struct seq_file *m, void *v)
{
	_irqL irqL;
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *) rtw_netdev_priv(dev);
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct rtw_rx_ring *rx_ring = &precvpriv->rx_ring[RX_MPDU_QUEUE];
	int i, j;

	DBG_871X_SEL_NL(m, "rx ring (%p)\n", rx_ring);
	DBG_871X_SEL_NL(m, "  dma: 0x%08x\n", (int) rx_ring->dma);
	DBG_871X_SEL_NL(m, "  idx: %d\n", rx_ring->idx);

	_enter_critical(&pdvobjpriv->irq_th_lock, &irqL);
	for (i=0; i<precvpriv->rxringcount; i++)
	{
		struct recv_stat *entry = &rx_ring->desc[i];
		struct sk_buff *skb = rx_ring->rx_buf[i];

		DBG_871X_SEL_NL(m, "  desc[%03d]: %p, rx_buf[%03d]: 0x%08x\n",
			i, entry, i, cpu_to_le32(*((dma_addr_t *)skb->cb)));

		for (j=0; j<sizeof(*entry)/4; j++)
		{
			if ((j % 4) == 0)
				DBG_871X_SEL_NL(m, "  0x%03x", j);

			DBG_871X_SEL_NL(m, " 0x%08x ", ((int *) entry)[j]);

			if ((j % 4) == 3)
				DBG_871X_SEL_NL(m, "\n");
		}
	}
	_exit_critical(&pdvobjpriv->irq_th_lock, &irqL);

	return 0;
}

int proc_get_tx_ring(struct seq_file *m, void *v)
{
	_irqL irqL;
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *) rtw_netdev_priv(dev);
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	int i, j, k;

	_enter_critical(&pdvobjpriv->irq_th_lock, &irqL);
	for (i = 0; i < PCI_MAX_TX_QUEUE_COUNT; i++)
	{
		struct rtw_tx_ring *tx_ring = &pxmitpriv->tx_ring[i];

		DBG_871X_SEL_NL(m, "tx ring[%d] (%p)\n", i, tx_ring);
		DBG_871X_SEL_NL(m, "  dma: 0x%08x\n", (int) tx_ring->dma);
		DBG_871X_SEL_NL(m, "  idx: %d\n", tx_ring->idx);
		DBG_871X_SEL_NL(m, "  entries: %d\n", tx_ring->entries);
//		DBG_871X_SEL_NL(m, "  queue: %d\n", tx_ring->queue);
		DBG_871X_SEL_NL(m, "  qlen: %d\n", tx_ring->qlen);

		for (j=0; j < pxmitpriv->txringcount[i]; j++)
		{
			struct tx_desc *entry = &tx_ring->desc[j];

			DBG_871X_SEL_NL(m, "  desc[%03d]: %p\n", j, entry);
			for (k=0; k < sizeof(*entry)/4; k++)
			{
				if ((k % 4) == 0)
					DBG_871X_SEL_NL(m, "  0x%03x", k);

				DBG_871X_SEL_NL(m, " 0x%08x ", ((int *) entry)[k]);

				if ((k % 4) == 3)
					DBG_871X_SEL_NL(m, "\n");
			}
		}
	}
	_exit_critical(&pdvobjpriv->irq_th_lock, &irqL);

	return 0;
}
#endif

#ifdef CONFIG_P2P_WOWLAN
int proc_get_p2p_wowlan_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	struct p2p_wowlan_info	 peerinfo = pwdinfo->p2p_wow_info;
	if(_TRUE == peerinfo.is_trigger)
	{
		DBG_871X_SEL_NL(m,"is_trigger: TRUE\n");
		switch(peerinfo.wowlan_recv_frame_type)
		{
			case P2P_WOWLAN_RECV_NEGO_REQ:
				DBG_871X_SEL_NL(m,"Frame Type: Nego Request\n");
				break;
			case P2P_WOWLAN_RECV_INVITE_REQ:
				DBG_871X_SEL_NL(m,"Frame Type: Invitation Request\n");
				break;
			case P2P_WOWLAN_RECV_PROVISION_REQ:
				DBG_871X_SEL_NL(m,"Frame Type: Provision Request\n");
				break;
			default:
				break;
		}
		DBG_871X_SEL_NL(m,"Peer Addr: "MAC_FMT"\n", MAC_ARG(peerinfo.wowlan_peer_addr));
		DBG_871X_SEL_NL(m,"Peer WPS Config: %x\n", peerinfo.wowlan_peer_wpsconfig);
		DBG_871X_SEL_NL(m,"Persistent Group: %d\n", peerinfo.wowlan_peer_is_persistent);
		DBG_871X_SEL_NL(m,"Intivation Type: %d\n", peerinfo.wowlan_peer_invitation_type);
	}
	else
	{
		DBG_871X_SEL_NL(m,"is_trigger: False\n");
	}
	return 0;
}
#endif /* CONFIG_P2P_WOWLAN */

int proc_get_new_bcn_max(struct seq_file *m, void *v)
{
	extern int new_bcn_max;

	DBG_871X_SEL_NL(m, "%d", new_bcn_max);
	return 0;
}

ssize_t proc_set_new_bcn_max(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	char tmp[32];
	extern int new_bcn_max;

	if(count < 1)
		return -EFAULT;

	if(buffer && !copy_from_user(tmp, buffer, sizeof(tmp)))
		sscanf(tmp, "%d ", &new_bcn_max);

	return count;
}

#ifdef CONFIG_POWER_SAVING
int proc_get_ps_info(struct seq_file *m, void *v)
{	
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	u8 ips_mode = pwrpriv->ips_mode;
	u8 lps_mode = pwrpriv->power_mgnt;
	char *str = "";

	DBG_871X_SEL_NL(m, "======Power Saving Info:======\n");
	DBG_871X_SEL_NL(m, "*IPS:\n");

	if (ips_mode == IPS_NORMAL) {
#ifdef CONFIG_FWLPS_IN_IPS
		str = "FW_LPS_IN_IPS";
#else
		str = "Card Disable";
#endif
	} else if (ips_mode == IPS_NONE) {
		str = "NO IPS";
	} else if (ips_mode == IPS_LEVEL_2) {
		str = "IPS_LEVEL_2";
	} else {
		str = "invalid ips_mode";
	}

	DBG_871X_SEL_NL(m, " IPS mode: %s\n", str);
	DBG_871X_SEL_NL(m, " IPS enter count:%d, IPS leave count:%d\n",
			pwrpriv->ips_enter_cnts, pwrpriv->ips_leave_cnts);
	DBG_871X_SEL_NL(m, "------------------------------\n");
	DBG_871X_SEL_NL(m, "*LPS:\n");

	if (lps_mode == PS_MODE_ACTIVE) {
		str = "NO LPS";
	} else if (lps_mode == PS_MODE_MIN) {
		str = "MIN";
	} else if (lps_mode == PS_MODE_MAX) {
		str = "MAX";
	} else if (lps_mode == PS_MODE_DTIM) {
		str = "DTIM";
	} else {
		sprintf(str, "%d", lps_mode);
	}

	DBG_871X_SEL_NL(m, " LPS mode: %s\n", str);

	if (pwrpriv->dtim != 0)
		DBG_871X_SEL_NL(m, " DTIM: %d\n", pwrpriv->dtim);
	DBG_871X_SEL_NL(m, " LPS enter count:%d, LPS leave count:%d\n",
			pwrpriv->lps_enter_cnts, pwrpriv->lps_leave_cnts);
	DBG_871X_SEL_NL(m, "=============================\n");
	return 0;
}
#endif //CONFIG_POWER_SAVING

#ifdef CONFIG_TDLS
static int proc_tdls_display_tdls_function_info(struct seq_file *m)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	u8 SpaceBtwnItemAndValue = TDLS_DBG_INFO_SPACE_BTWN_ITEM_AND_VALUE;
	u8 SpaceBtwnItemAndValueTmp = 0;
	BOOLEAN FirstMatchFound = _FALSE;
	int j= 0;
	
	DBG_871X_SEL_NL(m, "============[TDLS Function Info]============\n");
	DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "TDLS Prohibited", (ptdlsinfo->ap_prohibited == _TRUE) ? "_TRUE" : "_FALSE");
	DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "TDLS Channel Switch Prohibited", (ptdlsinfo->ch_switch_prohibited == _TRUE) ? "_TRUE" : "_FALSE");
	DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "TDLS Link Established", (ptdlsinfo->link_established == _TRUE) ? "_TRUE" : "_FALSE");
	DBG_871X_SEL_NL(m, "%-*s = %d/%d\n", SpaceBtwnItemAndValue, "TDLS STA Num (Linked/Allowed)", ptdlsinfo->sta_cnt, MAX_ALLOWED_TDLS_STA_NUM);
	DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "TDLS Allowed STA Num Reached", (ptdlsinfo->sta_maximum == _TRUE) ? "_TRUE" : "_FALSE");

#ifdef CONFIG_TDLS_CH_SW
	DBG_871X_SEL_NL(m, "%-*s =", SpaceBtwnItemAndValue, "TDLS CH SW State");
	if (ptdlsinfo->chsw_info.ch_sw_state == TDLS_STATE_NONE)
	{
		DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_STATE_NONE");
	}
	else
	{
		for (j = 0; j < 32; j++)
		{
			if (ptdlsinfo->chsw_info.ch_sw_state & BIT(j))
			{
				if (FirstMatchFound ==  _FALSE)
				{
					SpaceBtwnItemAndValueTmp = 1;
					FirstMatchFound = _TRUE;
				}
				else
				{
					SpaceBtwnItemAndValueTmp = SpaceBtwnItemAndValue + 3;
				}
				switch (BIT(j))
				{
					case TDLS_INITIATOR_STATE:
						DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_INITIATOR_STATE");
						break;
					case TDLS_RESPONDER_STATE:
						DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_RESPONDER_STATE");
						break;
					case TDLS_LINKED_STATE:
						DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_LINKED_STATE");
						break;
					case TDLS_WAIT_PTR_STATE:		
						DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_WAIT_PTR_STATE");
						break;
					case TDLS_ALIVE_STATE:		
						DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_ALIVE_STATE");
						break;
					case TDLS_CH_SWITCH_ON_STATE:	
						DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_CH_SWITCH_ON_STATE");
						break;
					case TDLS_PEER_AT_OFF_STATE:		
						DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_PEER_AT_OFF_STATE");
						break;
					case TDLS_CH_SW_INITIATOR_STATE:		
						DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_CH_SW_INITIATOR_STATE");
						break;
					case TDLS_WAIT_CH_RSP_STATE:		
						DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValue, " ", "TDLS_WAIT_CH_RSP_STATE");
						break;
					default:
						DBG_871X_SEL_NL(m, "%-*sBIT(%d)\n", SpaceBtwnItemAndValueTmp, " ", j);
						break;
				}
			}
		}
	}

	DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "TDLS CH SW On", (ATOMIC_READ(&ptdlsinfo->chsw_info.chsw_on) == _TRUE) ? "_TRUE" : "_FALSE");
	DBG_871X_SEL_NL(m, "%-*s = %d\n", SpaceBtwnItemAndValue, "TDLS CH SW Off-Channel Num", ptdlsinfo->chsw_info.off_ch_num);
	DBG_871X_SEL_NL(m, "%-*s = %d\n", SpaceBtwnItemAndValue, "TDLS CH SW Channel Offset", ptdlsinfo->chsw_info.ch_offset);
	DBG_871X_SEL_NL(m, "%-*s = %d\n", SpaceBtwnItemAndValue, "TDLS CH SW Current Time", ptdlsinfo->chsw_info.cur_time);
	DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "TDLS CH SW Delay Switch Back", (ptdlsinfo->chsw_info.delay_switch_back == _TRUE) ? "_TRUE" : "_FALSE");
	DBG_871X_SEL_NL(m, "%-*s = %d\n", SpaceBtwnItemAndValue, "TDLS CH SW Dump Back", ptdlsinfo->chsw_info.dump_stack);
#endif

	DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "TDLS Device Discovered", (ptdlsinfo->dev_discovered == _TRUE) ? "_TRUE" : "_FALSE");
	DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "TDLS Enable", (ptdlsinfo->tdls_enable == _TRUE) ? "_TRUE" : "_FALSE");
	DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "TDLS Driver Setup", (ptdlsinfo->driver_setup == _TRUE) ? "_TRUE" : "_FALSE");
	
	return 0;
}

static int proc_tdls_display_network_info(struct seq_file *m)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	int i = 0;
	u8 SpaceBtwnItemAndValue = TDLS_DBG_INFO_SPACE_BTWN_ITEM_AND_VALUE;

	/* Display the linked AP/GO info */
	DBG_871X_SEL_NL(m, "============[Associated AP/GO Info]============\n");
	
	if ((pmlmepriv->fw_state & WIFI_STATION_STATE) && (pmlmepriv->fw_state & _FW_LINKED))
	{
		DBG_871X_SEL_NL(m, "%-*s = %s\n", SpaceBtwnItemAndValue, "BSSID", cur_network->network.Ssid.Ssid);
		DBG_871X_SEL_NL(m, "%-*s = "MAC_FMT"\n", SpaceBtwnItemAndValue, "Mac Address", MAC_ARG(cur_network->network.MacAddress));
		
		DBG_871X_SEL_NL(m, "%-*s = ", SpaceBtwnItemAndValue, "Wireless Mode");
		for (i = 0; i < 8; i++)
		{
			if (pmlmeext->cur_wireless_mode & BIT(i))
			{
				switch (BIT(i))
				{
					case WIRELESS_11B: 
						DBG_871X_SEL_NL(m, "%4s", "11B ");
						break;
					case WIRELESS_11G:
						DBG_871X_SEL_NL(m, "%4s", "11G ");
						break;
					case WIRELESS_11A:
						DBG_871X_SEL_NL(m, "%4s", "11A ");
						break;
					case WIRELESS_11_24N:
						DBG_871X_SEL_NL(m, "%7s", "11_24N ");
						break;
					case WIRELESS_11_5N:
						DBG_871X_SEL_NL(m, "%6s", "11_5N ");
						break;
					case WIRELESS_AUTO:
						DBG_871X_SEL_NL(m, "%5s", "AUTO ");
						break;
					case WIRELESS_11AC:
						DBG_871X_SEL_NL(m, "%5s", "11AC ");
						break;
				}
			}
		}
		DBG_871X_SEL_NL(m, "\n");

		DBG_871X_SEL_NL(m, "%-*s = ", SpaceBtwnItemAndValue, "Privacy");
		switch (padapter->securitypriv.dot11PrivacyAlgrthm)
		{
			case _NO_PRIVACY_:
				DBG_871X_SEL_NL(m, "%s\n", "NO PRIVACY");
				break;
			case _WEP40_:	
				DBG_871X_SEL_NL(m, "%s\n", "WEP 40");
				break;
			case _TKIP_:
				DBG_871X_SEL_NL(m, "%s\n", "TKIP");
				break;
			case _TKIP_WTMIC_:
				DBG_871X_SEL_NL(m, "%s\n", "TKIP WTMIC");
				break;
			case _AES_:				
				DBG_871X_SEL_NL(m, "%s\n", "AES");
				break;
			case _WEP104_:
				DBG_871X_SEL_NL(m, "%s\n", "WEP 104");
				break;
			case _WEP_WPA_MIXED_:
				DBG_871X_SEL_NL(m, "%s\n", "WEP/WPA Mixed");
				break;
			case _SMS4_:
				DBG_871X_SEL_NL(m, "%s\n", "SMS4");
				break;
#ifdef CONFIG_IEEE80211W
			case _BIP_:
				DBG_871X_SEL_NL(m, "%s\n", "BIP");
				break;	
#endif //CONFIG_IEEE80211W
		}
		
		DBG_871X_SEL_NL(m, "%-*s = %d\n", SpaceBtwnItemAndValue, "Channel", pmlmeext->cur_channel);
		DBG_871X_SEL_NL(m, "%-*s = ", SpaceBtwnItemAndValue, "Channel Offset");
		switch (pmlmeext->cur_ch_offset)
		{
			case HAL_PRIME_CHNL_OFFSET_DONT_CARE:
				DBG_871X_SEL_NL(m, "%s\n", "N/A");
				break;
			case HAL_PRIME_CHNL_OFFSET_LOWER:
				DBG_871X_SEL_NL(m, "%s\n", "Lower");
				break;
			case HAL_PRIME_CHNL_OFFSET_UPPER:
				DBG_871X_SEL_NL(m, "%s\n", "Upper");
				break;
		}
		
		DBG_871X_SEL_NL(m, "%-*s = ", SpaceBtwnItemAndValue, "Bandwidth Mode");
		switch (pmlmeext->cur_bwmode)
		{
			case CHANNEL_WIDTH_20:
				DBG_871X_SEL_NL(m, "%s\n", "20MHz");
				break;
			case CHANNEL_WIDTH_40:
				DBG_871X_SEL_NL(m, "%s\n", "40MHz");
				break;
			case CHANNEL_WIDTH_80:
				DBG_871X_SEL_NL(m, "%s\n", "80MHz");
				break;
			case CHANNEL_WIDTH_160:
				DBG_871X_SEL_NL(m, "%s\n", "160MHz");
				break;
			case CHANNEL_WIDTH_80_80:
				DBG_871X_SEL_NL(m, "%s\n", "80MHz + 80MHz");
				break;
		}
	}
	else
	{
		DBG_871X_SEL_NL(m, "No association with AP/GO exists!\n");
	}

	return 0;
}

static int proc_tdls_display_tdls_sta_info(struct seq_file *m)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct sta_info *psta;
	int i = 0, j = 0;
	_irqL irqL;
	_list	*plist, *phead;
	u8 SpaceBtwnItemAndValue = TDLS_DBG_INFO_SPACE_BTWN_ITEM_AND_VALUE;
	u8 SpaceBtwnItemAndValueTmp = 0;
	u8 NumOfTdlsStaToShow = 0;
	BOOLEAN FirstMatchFound = _FALSE;
	
	/* Search for TDLS sta info to display */
	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);
	for (i=0; i< NUM_STA; i++)
	{
		phead = &(pstapriv->sta_hash[i]);
		plist = get_next(phead);	
		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
		{
				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
				plist = get_next(plist);
				if (psta->tdls_sta_state != TDLS_STATE_NONE)
				{
					/* We got one TDLS sta info to show */
					DBG_871X_SEL_NL(m, "============[TDLS Peer STA Info: STA %d]============\n", ++NumOfTdlsStaToShow);
					DBG_871X_SEL_NL(m, "%-*s = "MAC_FMT"\n", SpaceBtwnItemAndValue, "Mac Address", MAC_ARG(psta->hwaddr));
					DBG_871X_SEL_NL(m, "%-*s =", SpaceBtwnItemAndValue, "TDLS STA State");
					SpaceBtwnItemAndValueTmp = 0;
					FirstMatchFound = _FALSE;
					for (j = 0; j < 32; j++)
					{
						if (psta->tdls_sta_state & BIT(j))
						{
							if (FirstMatchFound ==  _FALSE)
							{
								SpaceBtwnItemAndValueTmp = 1;
								FirstMatchFound = _TRUE;
							}
							else
							{
								SpaceBtwnItemAndValueTmp = SpaceBtwnItemAndValue + 3;
							}
							switch (BIT(j))
							{
								case TDLS_INITIATOR_STATE:
									DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_INITIATOR_STATE");
									break;
								case TDLS_RESPONDER_STATE:
									DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_RESPONDER_STATE");
									break;
								case TDLS_LINKED_STATE:
									DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_LINKED_STATE");
									break;
								case TDLS_WAIT_PTR_STATE:		
									DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_WAIT_PTR_STATE");
									break;
								case TDLS_ALIVE_STATE:		
									DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_ALIVE_STATE");
									break;
								case TDLS_CH_SWITCH_ON_STATE:	
									DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_CH_SWITCH_ON_STATE");
									break;
								case TDLS_PEER_AT_OFF_STATE:		
									DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_PEER_AT_OFF_STATE");
									break;
								case TDLS_CH_SW_INITIATOR_STATE:		
									DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValueTmp, " ", "TDLS_CH_SW_INITIATOR_STATE");
									break;
								case TDLS_WAIT_CH_RSP_STATE:		
									DBG_871X_SEL_NL(m, "%-*s%s\n", SpaceBtwnItemAndValue, " ", "TDLS_WAIT_CH_RSP_STATE");
									break;
								default:
									DBG_871X_SEL_NL(m, "%-*sBIT(%d)\n", SpaceBtwnItemAndValueTmp, " ", j);
									break;
							}
						}
					}

					DBG_871X_SEL_NL(m, "%-*s = ", SpaceBtwnItemAndValue, "Wireless Mode");
					for (j = 0; j < 8; j++)
					{
						if (psta->wireless_mode & BIT(j))
						{
							switch (BIT(j))
							{
								case WIRELESS_11B: 
									DBG_871X_SEL_NL(m, "%4s", "11B ");
									break;
								case WIRELESS_11G:
									DBG_871X_SEL_NL(m, "%4s", "11G ");
									break;
								case WIRELESS_11A:
									DBG_871X_SEL_NL(m, "%4s", "11A ");
									break;
								case WIRELESS_11_24N:
									DBG_871X_SEL_NL(m, "%7s", "11_24N ");
									break;
								case WIRELESS_11_5N:
									DBG_871X_SEL_NL(m, "%6s", "11_5N ");
									break;
								case WIRELESS_AUTO:
									DBG_871X_SEL_NL(m, "%5s", "AUTO ");
									break;
								case WIRELESS_11AC:
									DBG_871X_SEL_NL(m, "%5s", "11AC ");
									break;
							}
						}
					}
					DBG_871X_SEL_NL(m, "\n");

					DBG_871X_SEL_NL(m, "%-*s = ", SpaceBtwnItemAndValue, "Bandwidth Mode");
					switch (psta->bw_mode)
					{
						case CHANNEL_WIDTH_20:
							DBG_871X_SEL_NL(m, "%s\n", "20MHz");
							break;
						case CHANNEL_WIDTH_40:
							DBG_871X_SEL_NL(m, "%s\n", "40MHz");
							break;
						case CHANNEL_WIDTH_80:
							DBG_871X_SEL_NL(m, "%s\n", "80MHz");
							break;
						case CHANNEL_WIDTH_160:
							DBG_871X_SEL_NL(m, "%s\n", "160MHz");
							break;
						case CHANNEL_WIDTH_80_80:
							DBG_871X_SEL_NL(m, "%s\n", "80MHz + 80MHz");
							break;
					}

					DBG_871X_SEL_NL(m, "%-*s = ", SpaceBtwnItemAndValue, "Privacy");
					switch (psta->dot118021XPrivacy)
					{
						case _NO_PRIVACY_:
							DBG_871X_SEL_NL(m, "%s\n", "NO PRIVACY");
							break;
						case _WEP40_:	
							DBG_871X_SEL_NL(m, "%s\n", "WEP 40");
							break;
						case _TKIP_:
							DBG_871X_SEL_NL(m, "%s\n", "TKIP");
							break;
						case _TKIP_WTMIC_:
							DBG_871X_SEL_NL(m, "%s\n", "TKIP WTMIC");
							break;
						case _AES_:				
							DBG_871X_SEL_NL(m, "%s\n", "AES");
							break;
						case _WEP104_:
							DBG_871X_SEL_NL(m, "%s\n", "WEP 104");
							break;
						case _WEP_WPA_MIXED_:
							DBG_871X_SEL_NL(m, "%s\n", "WEP/WPA Mixed");
							break;
						case _SMS4_:
							DBG_871X_SEL_NL(m, "%s\n", "SMS4");
							break;
#ifdef CONFIG_IEEE80211W
						case _BIP_:
							DBG_871X_SEL_NL(m, "%s\n", "BIP");
							break;
#endif //CONFIG_IEEE80211W
					}

					DBG_871X_SEL_NL(m, "%-*s = %d sec/%d sec\n", SpaceBtwnItemAndValue, "TPK Lifetime (Current/Expire)", psta->TPK_count, psta->TDLS_PeerKey_Lifetime);
					DBG_871X_SEL_NL(m, "%-*s = %llu\n", SpaceBtwnItemAndValue, "Tx Packets Over Direct Link", psta->sta_stats.tx_pkts);
					DBG_871X_SEL_NL(m, "%-*s = %llu\n", SpaceBtwnItemAndValue, "Rx Packets Over Direct Link", psta->sta_stats.rx_data_pkts);
				}
		}
	}
	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);
	if (NumOfTdlsStaToShow == 0)
	{
		DBG_871X_SEL_NL(m, "============[TDLS Peer STA Info]============\n");
		DBG_871X_SEL_NL(m, "No TDLS direct link exists!\n");
	}

	return 0;
}

int proc_get_tdls_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	struct sta_info *psta;
	int i = 0, j = 0;
	_irqL irqL;
	_list	*plist, *phead;
	u8 SpaceBtwnItemAndValue = 41;
	u8 SpaceBtwnItemAndValueTmp = 0;
	u8 NumOfTdlsStaToShow = 0;
	BOOLEAN FirstMatchFound = _FALSE;

	proc_tdls_display_tdls_function_info(m);
	proc_tdls_display_network_info(m);
	proc_tdls_display_tdls_sta_info(m);	

	return 0;
}
#endif

int proc_get_monitor(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (WIFI_MONITOR_STATE == get_fwstate(pmlmepriv)) {
		DBG_871X_SEL_NL(m, "Monitor mode : Enable\n");

		DBG_871X_SEL_NL(m, "ch=%d, ch_offset=%d, bw=%d\n",
						rtw_get_oper_ch(padapter), rtw_get_oper_choffset(padapter), rtw_get_oper_bw(padapter));
	} else {
		DBG_871X_SEL_NL(m, "Monitor mode : Disable\n");
	}

	return 0;
}

ssize_t proc_set_monitor(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	char tmp[32];
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	u8 target_chan, target_offset, target_bw;

	if (count < 3) {
		DBG_871X("argument size is less than 3\n");
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {
		int num = sscanf(tmp, "%hhu %hhu %hhu", &target_chan, &target_offset, &target_bw);

		if (num != 3) {
			DBG_871X("invalid write_reg parameter!\n");
			return count;
		}

		padapter->mlmeextpriv.cur_channel  = target_chan;
		set_channel_bwmode(padapter, target_chan, target_offset, target_bw);
	}

	return count;
}

#endif

