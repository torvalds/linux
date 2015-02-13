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

#include <rtw_debug.h>

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

void dump_drv_version(void *sel)
{
	DBG_871X_SEL_NL(sel, "%s %s\n", DRV_NAME, DRIVERVERSION);
	DBG_871X_SEL_NL(sel, "build time: %s %s\n", __DATE__, __TIME__);
}

void dump_log_level(void *sel)
{
	DBG_871X_SEL_NL(sel, "log_level:%d\n", GlobalDebugLevel);
}

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

bool rtw_fwdl_test_trigger_chksum_fail()
{
	if (fwdl_test_chksum_fail) {
		DBG_871X_LEVEL(_drv_always_, "fwdl test case: trigger chksum_fail\n");
		fwdl_test_chksum_fail--;
		return _TRUE;
	}
	return _FALSE;
}

bool rtw_fwdl_test_trigger_wintint_rdy_fail()
{
	if (fwdl_test_wintint_rdy_fail) {
		DBG_871X_LEVEL(_drv_always_, "fwdl test case: trigger wintint_rdy_fail\n");
		fwdl_test_wintint_rdy_fail--;
		return _TRUE;
	}
	return _FALSE;
}

static u32 g_wait_hiq_empty_ms = 0;

u32 rtw_get_wait_hiq_empty_ms()
{
	return g_wait_hiq_empty_ms;
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
		DBG_871X_SEL_NL(m, "rtsen=%d, cts2slef=%d\n", psta->rtsen, psta->cts2self);
		DBG_871X_SEL_NL(m, "state=0x%x, aid=%d, macid=%d, raid=%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
#ifdef CONFIG_80211N_HT
		DBG_871X_SEL_NL(m, "qos_en=%d, ht_en=%d, init_rate=%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);		
		DBG_871X_SEL_NL(m, "bwmode=%d, ch_offset=%d, sgi=%d\n", psta->htpriv.bwmode, psta->htpriv.ch_offset, psta->htpriv.sgi);
		DBG_871X_SEL_NL(m, "ampdu_enable = %d\n", psta->htpriv.ampdu_enable);	
		DBG_871X_SEL_NL(m, "agg_enable_bitmap=%x, candidate_tid_bitmap=%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
#endif //CONFIG_80211N_HT
					
		for(i=0;i<16;i++)
		{							
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			if(preorder_ctrl->enable)
			{
				DBG_871X_SEL_NL(m, "tid=%d, indicate_seq=%d\n", i, preorder_ctrl->indicate_seq);
			}
		}	
							
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
	
int proc_get_trx_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	int i;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct recv_priv  *precvpriv = &padapter->recvpriv;
	struct hw_xmit *phwxmit;

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
	DBG_871X_SEL_NL(m, "rx_urb_pending_cn=%d\n", precvpriv->rx_pending_cnt);
#endif

	return 0;
}

#ifdef CONFIG_DBG_COUNTER
int proc_get_rx_logs(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct rx_logs *rx_logs = &padapter->rx_logs;

	DBG_871X_SEL_NL(m, "intf_rx=%d\n", rx_logs->intf_rx);
	DBG_871X_SEL_NL(m, "intf_rx_err_recvframe=%d\n", rx_logs->intf_rx_err_recvframe);
	DBG_871X_SEL_NL(m, "intf_rx_err_skb=%d\n", rx_logs->intf_rx_err_skb);
	DBG_871X_SEL_NL(m, "intf_rx_report=%d\n", rx_logs->intf_rx_report);
	DBG_871X_SEL_NL(m, "core_rx=%d\n", rx_logs->core_rx);
	DBG_871X_SEL_NL(m, "core_rx_pre=%d\n", rx_logs->core_rx_pre);
	DBG_871X_SEL_NL(m, "core_rx_pre_ver_err=%d\n", rx_logs->core_rx_pre_ver_err);
	DBG_871X_SEL_NL(m, "core_rx_pre_mgmt=%d\n", rx_logs->core_rx_pre_mgmt);
	DBG_871X_SEL_NL(m, "core_rx_pre_mgmt_err_80211w=%d\n", rx_logs->core_rx_pre_mgmt_err_80211w);
	DBG_871X_SEL_NL(m, "core_rx_pre_mgmt_err=%d\n", rx_logs->core_rx_pre_mgmt_err);
	DBG_871X_SEL_NL(m, "core_rx_pre_ctrl=%d\n", rx_logs->core_rx_pre_ctrl);
	DBG_871X_SEL_NL(m, "core_rx_pre_ctrl_err=%d\n", rx_logs->core_rx_pre_ctrl_err);
	DBG_871X_SEL_NL(m, "core_rx_pre_data=%d\n", rx_logs->core_rx_pre_data);
	DBG_871X_SEL_NL(m, "core_rx_pre_data_wapi_seq_err=%d\n", rx_logs->core_rx_pre_data_wapi_seq_err);
	DBG_871X_SEL_NL(m, "core_rx_pre_data_wapi_key_err=%d\n", rx_logs->core_rx_pre_data_wapi_key_err);
	DBG_871X_SEL_NL(m, "core_rx_pre_data_handled=%d\n", rx_logs->core_rx_pre_data_handled);
	DBG_871X_SEL_NL(m, "core_rx_pre_data_err=%d\n", rx_logs->core_rx_pre_data_err);
	DBG_871X_SEL_NL(m, "core_rx_pre_data_unknown=%d\n", rx_logs->core_rx_pre_data_unknown);
	DBG_871X_SEL_NL(m, "core_rx_pre_unknown=%d\n", rx_logs->core_rx_pre_unknown);
	DBG_871X_SEL_NL(m, "core_rx_enqueue=%d\n", rx_logs->core_rx_enqueue);
	DBG_871X_SEL_NL(m, "core_rx_dequeue=%d\n", rx_logs->core_rx_dequeue);
	DBG_871X_SEL_NL(m, "core_rx_post=%d\n", rx_logs->core_rx_post);
	DBG_871X_SEL_NL(m, "core_rx_post_decrypt=%d\n", rx_logs->core_rx_post_decrypt);
	DBG_871X_SEL_NL(m, "core_rx_post_decrypt_wep=%d\n", rx_logs->core_rx_post_decrypt_wep);
	DBG_871X_SEL_NL(m, "core_rx_post_decrypt_tkip=%d\n", rx_logs->core_rx_post_decrypt_tkip);
	DBG_871X_SEL_NL(m, "core_rx_post_decrypt_aes=%d\n", rx_logs->core_rx_post_decrypt_aes);
	DBG_871X_SEL_NL(m, "core_rx_post_decrypt_wapi=%d\n", rx_logs->core_rx_post_decrypt_wapi);
	DBG_871X_SEL_NL(m, "core_rx_post_decrypt_hw=%d\n", rx_logs->core_rx_post_decrypt_hw);
	DBG_871X_SEL_NL(m, "core_rx_post_decrypt_unknown=%d\n", rx_logs->core_rx_post_decrypt_unknown);
	DBG_871X_SEL_NL(m, "core_rx_post_decrypt_err=%d\n", rx_logs->core_rx_post_decrypt_err);
	DBG_871X_SEL_NL(m, "core_rx_post_defrag_err=%d\n", rx_logs->core_rx_post_defrag_err);
	DBG_871X_SEL_NL(m, "core_rx_post_portctrl_err=%d\n", rx_logs->core_rx_post_portctrl_err);
	DBG_871X_SEL_NL(m, "core_rx_post_indicate=%d\n", rx_logs->core_rx_post_indicate);
	DBG_871X_SEL_NL(m, "core_rx_post_indicate_in_oder=%d\n", rx_logs->core_rx_post_indicate_in_oder);
	DBG_871X_SEL_NL(m, "core_rx_post_indicate_reoder=%d\n", rx_logs->core_rx_post_indicate_reoder);
	DBG_871X_SEL_NL(m, "core_rx_post_indicate_err=%d\n", rx_logs->core_rx_post_indicate_err);
	DBG_871X_SEL_NL(m, "os_indicate=%d\n", rx_logs->os_indicate);
	DBG_871X_SEL_NL(m, "os_indicate_ap_mcast=%d\n", rx_logs->os_indicate_ap_mcast);
	DBG_871X_SEL_NL(m, "os_indicate_ap_forward=%d\n", rx_logs->os_indicate_ap_forward);
	DBG_871X_SEL_NL(m, "os_indicate_ap_self=%d\n", rx_logs->os_indicate_ap_self);
	DBG_871X_SEL_NL(m, "os_indicate_err=%d\n", rx_logs->os_indicate_err);
	DBG_871X_SEL_NL(m, "os_netif_ok=%d\n", rx_logs->os_netif_ok);
	DBG_871X_SEL_NL(m, "os_netif_err=%d\n", rx_logs->os_netif_err);

	return 0;
}

int proc_get_tx_logs(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct tx_logs *tx_logs = &padapter->tx_logs;

	DBG_871X_SEL_NL(m, "os_tx=%d\n", tx_logs->os_tx);
	DBG_871X_SEL_NL(m, "os_tx_err_up=%d\n", tx_logs->os_tx_err_up);
	DBG_871X_SEL_NL(m, "os_tx_err_xmit=%d\n", tx_logs->os_tx_err_xmit);
	DBG_871X_SEL_NL(m, "os_tx_m2u=%d\n", tx_logs->os_tx_m2u);
	DBG_871X_SEL_NL(m, "os_tx_m2u_ignore_fw_linked=%d\n", tx_logs->os_tx_m2u_ignore_fw_linked);
	DBG_871X_SEL_NL(m, "os_tx_m2u_ignore_self=%d\n", tx_logs->os_tx_m2u_ignore_self);
	DBG_871X_SEL_NL(m, "os_tx_m2u_entry=%d\n", tx_logs->os_tx_m2u_entry);
	DBG_871X_SEL_NL(m, "os_tx_m2u_entry_err_xmit=%d\n", tx_logs->os_tx_m2u_entry_err_xmit);
	DBG_871X_SEL_NL(m, "os_tx_m2u_entry_err_skb=%d\n", tx_logs->os_tx_m2u_entry_err_skb);
	DBG_871X_SEL_NL(m, "os_tx_m2u_stop=%d\n", tx_logs->os_tx_m2u_stop);
	DBG_871X_SEL_NL(m, "core_tx=%d\n", tx_logs->core_tx);
	DBG_871X_SEL_NL(m, "core_tx_err_pxmitframe=%d\n", tx_logs->core_tx_err_pxmitframe);
	DBG_871X_SEL_NL(m, "core_tx_err_brtx=%d\n", tx_logs->core_tx_err_brtx);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib=%d\n", tx_logs->core_tx_upd_attrib);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_adhoc=%d\n", tx_logs->core_tx_upd_attrib_adhoc);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_sta=%d\n", tx_logs->core_tx_upd_attrib_sta);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_ap=%d\n", tx_logs->core_tx_upd_attrib_ap);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_unknown=%d\n", tx_logs->core_tx_upd_attrib_unknown);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_dhcp=%d\n", tx_logs->core_tx_upd_attrib_dhcp);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_icmp=%d\n", tx_logs->core_tx_upd_attrib_icmp);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_active=%d\n", tx_logs->core_tx_upd_attrib_active);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_err_ucast_sta=%d\n", tx_logs->core_tx_upd_attrib_err_ucast_sta);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_err_ucast_ap_link=%d\n", tx_logs->core_tx_upd_attrib_err_ucast_ap_link);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_err_sta=%d\n", tx_logs->core_tx_upd_attrib_err_sta);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_err_link=%d\n", tx_logs->core_tx_upd_attrib_err_link);
	DBG_871X_SEL_NL(m, "core_tx_upd_attrib_err_sec=%d\n", tx_logs->core_tx_upd_attrib_err_sec);
	DBG_871X_SEL_NL(m, "core_tx_ap_enqueue_warn_fwstate=%d\n", tx_logs->core_tx_ap_enqueue_warn_fwstate);
	DBG_871X_SEL_NL(m, "core_tx_ap_enqueue_warn_sta=%d\n", tx_logs->core_tx_ap_enqueue_warn_sta);
	DBG_871X_SEL_NL(m, "core_tx_ap_enqueue_warn_nosta=%d\n", tx_logs->core_tx_ap_enqueue_warn_nosta);
	DBG_871X_SEL_NL(m, "core_tx_ap_enqueue_warn_link=%d\n", tx_logs->core_tx_ap_enqueue_warn_link);
	DBG_871X_SEL_NL(m, "core_tx_ap_enqueue_warn_trigger=%d\n", tx_logs->core_tx_ap_enqueue_warn_trigger);
	DBG_871X_SEL_NL(m, "core_tx_ap_enqueue_mcast=%d\n", tx_logs->core_tx_ap_enqueue_mcast);
	DBG_871X_SEL_NL(m, "core_tx_ap_enqueue_ucast=%d\n", tx_logs->core_tx_ap_enqueue_ucast);
	DBG_871X_SEL_NL(m, "core_tx_ap_enqueue=%d\n", tx_logs->core_tx_ap_enqueue);
	DBG_871X_SEL_NL(m, "intf_tx=%d\n", tx_logs->intf_tx);
	DBG_871X_SEL_NL(m, "intf_tx_pending_ac=%d\n", tx_logs->intf_tx_pending_ac);
	DBG_871X_SEL_NL(m, "intf_tx_pending_fw_under_survey=%d\n", tx_logs->intf_tx_pending_fw_under_survey);
	DBG_871X_SEL_NL(m, "intf_tx_pending_fw_under_linking=%d\n", tx_logs->intf_tx_pending_fw_under_linking);
	DBG_871X_SEL_NL(m, "intf_tx_pending_xmitbuf=%d\n", tx_logs->intf_tx_pending_xmitbuf);
	DBG_871X_SEL_NL(m, "intf_tx_enqueue=%d\n", tx_logs->intf_tx_enqueue);
	DBG_871X_SEL_NL(m, "core_tx_enqueue=%d\n", tx_logs->core_tx_enqueue);
	DBG_871X_SEL_NL(m, "core_tx_enqueue_class=%d\n", tx_logs->core_tx_enqueue_class);
	DBG_871X_SEL_NL(m, "core_tx_enqueue_class_err_sta=%d\n", tx_logs->core_tx_enqueue_class_err_sta);
	DBG_871X_SEL_NL(m, "core_tx_enqueue_class_err_nosta=%d\n", tx_logs->core_tx_enqueue_class_err_nosta);
	DBG_871X_SEL_NL(m, "core_tx_enqueue_class_err_fwlink=%d\n", tx_logs->core_tx_enqueue_class_err_fwlink);
	DBG_871X_SEL_NL(m, "intf_tx_direct=%d\n", tx_logs->intf_tx_direct);
	DBG_871X_SEL_NL(m, "intf_tx_direct_err_coalesce=%d\n", tx_logs->intf_tx_direct_err_coalesce);
	DBG_871X_SEL_NL(m, "intf_tx_dequeue=%d\n", tx_logs->intf_tx_dequeue);
	DBG_871X_SEL_NL(m, "intf_tx_dequeue_err_coalesce=%d\n", tx_logs->intf_tx_dequeue_err_coalesce);
	DBG_871X_SEL_NL(m, "intf_tx_dump_xframe=%d\n", tx_logs->intf_tx_dump_xframe);
	DBG_871X_SEL_NL(m, "intf_tx_dump_xframe_err_txdesc=%d\n", tx_logs->intf_tx_dump_xframe_err_txdesc);
	DBG_871X_SEL_NL(m, "intf_tx_dump_xframe_err_port=%d\n", tx_logs->intf_tx_dump_xframe_err_port);

	return 0;
}

int proc_get_int_logs(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct tx_logs *tx_logs = &padapter->tx_logs;

	DBG_871X_SEL_NL(m, "all=%d\n", padapter->int_logs.all);
	DBG_871X_SEL_NL(m, "err=%d\n", padapter->int_logs.err);
	DBG_871X_SEL_NL(m, "tbdok=%d\n", padapter->int_logs.tbdok);
	DBG_871X_SEL_NL(m, "tbder=%d\n", padapter->int_logs.tbder);
	DBG_871X_SEL_NL(m, "bcnderr=%d\n", padapter->int_logs.bcnderr);
	DBG_871X_SEL_NL(m, "bcndma=%d\n", padapter->int_logs.bcndma);
	DBG_871X_SEL_NL(m, "bcndma_e=%d\n", padapter->int_logs.bcndma_e);
	DBG_871X_SEL_NL(m, "rx=%d\n", padapter->int_logs.rx);
	DBG_871X_SEL_NL(m, "txfovw=%d\n", padapter->int_logs.txfovw);
	DBG_871X_SEL_NL(m, "mgntok=%d\n", padapter->int_logs.mgntok);
	DBG_871X_SEL_NL(m, "highdok=%d\n", padapter->int_logs.highdok);
	DBG_871X_SEL_NL(m, "bkdok=%d\n", padapter->int_logs.bkdok);
	DBG_871X_SEL_NL(m, "bedok=%d\n", padapter->int_logs.bedok);
	DBG_871X_SEL_NL(m, "vidok=%d\n", padapter->int_logs.vidok);
	DBG_871X_SEL_NL(m, "vodok=%d\n", padapter->int_logs.vodok);

	return 0;
}
#endif /* CONFIG_DBG_COUNTER */

int proc_get_rx_signal(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	DBG_871X_SEL_NL(m, "rssi:%d\n", padapter->recvpriv.rssi);
	DBG_871X_SEL_NL(m, "rxpwdb:%d\n", padapter->recvpriv.rxpwdb);
	DBG_871X_SEL_NL(m, "signal_strength:%u\n", padapter->recvpriv.signal_strength);
	DBG_871X_SEL_NL(m, "signal_qual:%u\n", padapter->recvpriv.signal_qual);
	DBG_871X_SEL_NL(m, "noise:%u\n", padapter->recvpriv.noise);

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

		if( pregpriv && mode >= 0 && mode < 2 )
		{
			pregpriv->ht_enable= mode;
			printk("ht_enable=%d\n", pregpriv->ht_enable);
		}
	}
	
	return count;
	
}

int proc_get_cbw40_enable(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	if(pregpriv)
		DBG_871X_SEL_NL(m, "%d\n", pregpriv->cbw40_enable);

	return 0;
}

ssize_t proc_set_cbw40_enable(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
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

			pregpriv->cbw40_enable= mode;
			printk("cbw40_enable=%d\n", mode);

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

int proc_get_two_path_rssi(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	if(padapter)
		DBG_871X_SEL_NL(m, "%d %d\n",padapter->recvpriv.RxRssi[0], padapter->recvpriv.RxRssi[1]);

	return 0;
}

ssize_t proc_set_rssi_disp(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
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
			DBG_8192C("Turn On Rx RSSI Display Function\n");
			padapter->bRxRSSIDisplay = enable ;			
		}
		else
		{
			DBG_8192C("Turn Off Rx RSSI Display Function\n");
			padapter->bRxRSSIDisplay = 0 ;
		}
	
	}
	
	return count;
	
}	
		
#ifdef CONFIG_AP_MODE
int proc_get_all_sta_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_irqL irqL;
	struct sta_info *psta;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct sta_priv *pstapriv = &padapter->stapriv;
	int i, j;
	_list	*plist, *phead;
	struct recv_reorder_ctrl *preorder_ctrl;

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
				DBG_871X_SEL_NL(m, "bwmode=%d, ch_offset=%d, sgi=%d\n", psta->htpriv.bwmode, psta->htpriv.ch_offset, psta->htpriv.sgi);
				DBG_871X_SEL_NL(m, "ampdu_enable = %d\n", psta->htpriv.ampdu_enable);									
				DBG_871X_SEL_NL(m, "agg_enable_bitmap=%x, candidate_tid_bitmap=%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
#endif //CONFIG_80211N_HT
				DBG_871X_SEL_NL(m, "sleepq_len=%d\n", psta->sleepq_len);
				DBG_871X_SEL_NL(m, "capability=0x%x\n", psta->capability);
				DBG_871X_SEL_NL(m, "flags=0x%x\n", psta->flags);
				DBG_871X_SEL_NL(m, "wpa_psk=0x%x\n", psta->wpa_psk);
				DBG_871X_SEL_NL(m, "wpa2_group_cipher=0x%x\n", psta->wpa2_group_cipher);
				DBG_871X_SEL_NL(m, "wpa2_pairwise_cipher=0x%x\n", psta->wpa2_pairwise_cipher);
				DBG_871X_SEL_NL(m, "qos_info=0x%x\n", psta->qos_info);
				DBG_871X_SEL_NL(m, "dot118021XPrivacy=0x%x\n", psta->dot118021XPrivacy);
								
				for(j=0;j<16;j++)
				{							
					preorder_ctrl = &psta->recvreorder_ctrl[j];
					if(preorder_ctrl->enable)
					{
						DBG_871X_SEL_NL(m, "tid=%d, indicate_seq=%d\n", j, preorder_ctrl->indicate_seq);
					}
				}

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
	
	for (i=0; (pmlmeext->channel_set[i].ChannelNum !=0) && (i < MAX_CHANNEL_NUM); i++) {
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
#define _bt_dbg_off_		0
#define _bt_dbg_on_		1

extern u32 BTCoexDbgLevel;
int proc_get_btcoex_dbg(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	if(pregpriv)
		DBG_871X_SEL_NL(m, "%d\n", BTCoexDbgLevel);

	return 0;
}

ssize_t proc_set_btcoex_dbg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = (struct net_device *)data;
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
			BTCoexDbgLevel= mode;
			printk("btcoex_dbg=%d\n", BTCoexDbgLevel);
		}
	}
	
	return count;
	
}
#endif /* CONFIG_BT_COEXIST */

#if defined(DBG_CONFIG_ERROR_DETECT)
#include <rtw_sreset.h>
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

ssize_t proc_set_fwdl_test_case(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = (struct net_device *)data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];

	if (count < 1)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, sizeof(tmp))) {		
		int num = sscanf(tmp, "%hhu %hhu", &fwdl_test_chksum_fail, &fwdl_test_wintint_rdy_fail);
	}

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

#endif

