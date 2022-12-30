#include "ecrnx_debugfs_func.h"
#include "ecrnx_debugfs_custom.h"

#ifdef CONFIG_ECRNX_DEBUGFS

debugfs_info_t debugfs_info;
debugfs_resp_t debugfs_resp;

u32 reg_buf[512]={0};

REG_MAP_ST mac_reg_table[]={
    {"INTC_IRQ_STATUS_ADDR",              0x00510000},
    {"INTC_IRQ_UNMASK_SET_ADDR",          0x00510010},
    {"INTC_IRQ_INDEX_ADDR",               0x00510040},
    {"NXMAC_MAC_ADDR_LOW_ADDR",           0x00610010},
    {"NXMAC_MAC_ADDR_HI_ADDR",            0x00610014},
    {"NXMAC_STATE_CNTRL_ADDR",            0x00610038},
    {"NXMAC_MAC_CNTRL_1_ADDR",            0x0061004c},
    {"NXMAC_RX_CNTRL_ADDR",               0x00610060},
    {"NXMAC_MAX_POWER_LEVEL_ADDR",        0x006100a0},
    {"NXMAC_TIMINGS_1_ADDR",              0x006100e4},
    {"NXMAC_RX_CNTRL_2_ADDR",             0x0061010c},
    {"NXMAC_MONOTONIC_COUNTER_2_LO_ADDR", 0x00610120},
    {"NXMAC_MONOTONIC_COUNTER_2_HI_ADDR", 0x00610124},
    {"NXMAC_MAX_RX_LENGTH_ADDR",          0x00610150},
    {"NXMAC_GEN_INT_STATUS_ADDR",         0x0061806c},
    {"NXMAC_GEN_INT_ENABLE_ADDR",         0x00618074},
    {"NXMAC_TX_RX_INT_STATUS_ADDR",       0x00618078},
    {"NXMAC_TX_RX_INT_ENABLE_ADDR",       0x00618080},
    {"NXMAC_DMA_CNTRL_SET_ADDR",          0x00618180},
    {"NXMAC_DMA_STATUS_1_ADDR",           0x00618188},
    {"NXMAC_DMA_STATUS_2_ADDR",           0x0061818c},
    {"NXMAC_DMA_STATUS_3_ADDR",           0x00618190},
    {"NXMAC_DMA_STATUS_4_ADDR",           0x00618194},
    {"NXMAC_TX_BCN_HEAD_PTR_ADDR",        0x00618198},
    {"NXMAC_TX_AC_0_HEAD_PTR_ADDR",       0x0061819c},
    {"NXMAC_TX_AC_1_HEAD_PTR_ADDR",       0x006181a0},
    {"NXMAC_TX_AC_2_HEAD_PTR_ADDR",       0x006181a4},
    {"NXMAC_TX_AC_3_HEAD_PTR_ADDR",       0x006181a8},
    {"NXMAC_RX_BUF_1_START_PTR_ADDR",     0x006181c8},
    {"NXMAC_RX_BUF_1_END_PTR_ADDR",       0x006181cc},
    {"NXMAC_RX_BUF_1_RD_PTR_ADDR",        0x006181d0},
    {"NXMAC_RX_BUF_1_WR_PTR_ADDR",        0x006181d4},
    {"NXMAC_RX_BUF_CONFIG_ADDR",          0x006181e8},
    {"MDM_HDMCONFIG_ADDR",                0x00700000},
};

REG_MAP_ST rf_reg_table[]={
    {"RF_REG_EXAMPLE",                    0x00510000},
};

REG_MAP_ST bb_reg_table[]={
    {"BB_REG_EXAMPLE",                    0x00510000},
};

REG_MAP_ST efuse_map_table[]={
    {"EFUSE_REG_EXAMPLE",                 0x00510000},
};

int argc;
char *argv[MAX_ARGV];
char slave_cli_cmd[50];
int arg_val[MAX_ARGV];


ECRNX_CLI_TABLE_ST ecrnx_cli_tab[]={
    {"macbyp start ", "macbyp_tx_start", 6, {{0,32},{0,32},{0,32},{0,32},{0,32},{0,32}},    cli_macbyp_start},
    {"macbyp stop",   "macbyp_tx_stop",  0, {},                                             cli_macbyp_stop},
    {"rf txgain ",    "rf_set_txgain",   1, {{0,32}},                                       cli_rf_txgain},
    {"rf rxgain ",    "rf_set_rxgain",   4, {{0,32},{0,32},{0,32},{0,32}},                  cli_rf_rxgain},
    {"rf chan ",      "rf_set_chan",     2, {{0,32},{0,32}},                                cli_rf_chan},
};


void ecrnx_debugfs_param_send(debugfs_info_t *req)
{
    if(debugfs_info.debugfs_type == SLAVE_LOG_LEVEL){
        if (req->u.slave_log_level_t.debug_level < DBG_TYPE_D || req->u.slave_log_level_t.debug_level > DBG_TYPE_O) {
            ECRNX_ERR("ecrnx debug param error!!!\n");
        }
    }

    ECRNX_DBG("%s: fstype:%d, level:%d, dir:%d \n", __func__, req->debugfs_type, req->u.slave_log_level_t.debug_level, req->u.slave_log_level_t.debug_dir);
    //print_hex_dump(KERN_INFO, "ecrnx_debugfs_send ", DUMP_PREFIX_ADDRESS, 32, 1,
    //    (u8*)req, sizeof(debugfs_info_t), false);
    host_send(req, sizeof(debugfs_info_t), TX_FLAG_MSG_DEBUGFS_IE);
}

int ecrnx_log_level_get(LOG_CTL_ST *log)
{
    if (log == NULL)
        return -1;

    *log = log_ctl;

    return 0;
}

int ecrnx_fw_log_level_set(u32 level, u32 dir)
{
    log_ctl.level = level;
    log_ctl.dir = dir;

    debugfs_info.debugfs_type = SLAVE_LOG_LEVEL;
    debugfs_info.u.slave_log_level_t.debug_level = level;
    debugfs_info.u.slave_log_level_t.debug_dir = dir;

    ecrnx_debugfs_param_send(&debugfs_info);

    return 0;
}

bool ecrnx_log_host_enable(void)
{
    if (log_ctl.dir)
        return true;

    return false;
}

int ecrnx_host_ver_get(u8 *ver)
{
    if (ver == NULL)
        return -1;

    sprintf(ver, "v%s", HOST_DRIVER_VERSION);

    return 0;
}

int ecrnx_fw_ver_get(u8 *ver)
{
    if (ver == NULL)
        return -1;

    debugfs_info.debugfs_type = SLAVE_FW_INFO;
    ecrnx_debugfs_param_send(&debugfs_info);

    //wait for confirm
    debugfs_resp.rxdatas = 0;
    wait_event_interruptible_timeout(debugfs_resp.rxdataq, debugfs_resp.rxdatas, 1*HZ);

    if (debugfs_resp.rxdatas)
        memcpy(ver, debugfs_resp.rxdata, debugfs_resp.rxlen);
    else
        return -1;

    return 0;
}

int ecrnx_build_time_get(u8 *build_time)
{
    if (build_time == NULL)
        return -1;

    sprintf(build_time, "%s %s", __DATE__, __TIME__);

    return 0;
}

static int ecrnx_wdevs_get(struct seq_file *seq, struct wireless_dev **wdev)
{
    struct ecrnx_hw *ecrnx_hw;
    u32 i;

    if (seq->private == NULL)
        return -1;

    ecrnx_hw = seq->private;

    for (i=0; i<NX_VIRT_STA_MAX; i++)
    {
        if (ecrnx_hw->vif_table[i] != NULL)
            wdev[i] = &ecrnx_hw->vif_table[i]->wdev;
        else
            wdev[i] = NULL;
    }

    return 0;
}

static int ecrnx_wdev_match(struct wireless_dev **wdev, IF_TYPE_EN iftype, u32 *index)
{
    struct ecrnx_vif *ecrnx_vif;
    u32 i;

    for (i=0; i<NX_VIRT_STA_MAX; i++)
    {
        // 1. valid
        if (wdev[i] == NULL)
            continue;

        // 2. up
        ecrnx_vif = netdev_priv(wdev[i]->netdev);
        if (ecrnx_vif->up == false)
            continue;

        // 3. type
        switch(wdev[i]->iftype)
        {
            case NL80211_IFTYPE_STATION:
            {
                if (iftype == IF_STA)
                {
                    *index = i;
                    return 0;
                }
                break;
            }

            case NL80211_IFTYPE_AP:
            case NL80211_IFTYPE_AP_VLAN:
            {
                if (iftype == IF_AP)
                {
                    *index = i;
                    return 0;
                }
                break;
            }

            case NL80211_IFTYPE_P2P_CLIENT:
            case NL80211_IFTYPE_P2P_GO:
            case NL80211_IFTYPE_P2P_DEVICE:
            {
                if (iftype == IF_P2P)
                {
                    *index = i;
                    return 0;
                }
                break;
            }

            default :
                break;
        }
    }

    return -1;
}

int ecrnx_rf_info_get(struct seq_file *seq, IF_TYPE_EN iftype,RF_INFO_ST *cur, RF_INFO_ST *oper)
{
    struct wireless_dev *wdev[NX_VIRT_STA_MAX];
    struct cfg80211_chan_def chandef;
    u32 index;

    if (0 != ecrnx_wdevs_get(seq, wdev))
        return -1;

    if ((cur == NULL) || (oper == NULL))
        return -1;

    if (0 != ecrnx_wdev_match(wdev, iftype, &index))
        return -1;

    if (0 != ecrnx_cfg80211_get_channel(wdev[index]->wiphy, wdev[index], &chandef))
        return -1;

    cur->ch = (chandef.center_freq1 - 2412)/5 + 1;
    cur->bw = chandef.width;
    cur->ch_offset = 0;

    oper->ch = (chandef.center_freq1 - 2412)/5 + 1;
    oper->bw = chandef.width;
    oper->ch_offset = 0;

    return 0;
}

int ecrnx_country_code_get(struct seq_file *seq, char *alpha2)
{
    struct ecrnx_hw *ecrnx_hw;

    if (seq->private != NULL)
        ecrnx_hw = seq->private;
    else
        return -1;

    if (alpha2 == NULL)
        return -1;

    if (ecrnx_hw->wiphy == NULL)
        return -1;

    if (ecrnx_hw->wiphy->regd == NULL)
        return -1;

    memcpy(alpha2, ecrnx_hw->wiphy->regd->alpha2, sizeof(ecrnx_hw->wiphy->regd->alpha2));
    alpha2[2] = '\0';

    return 0;
}

int ecrnx_mac_addr_get(struct seq_file *seq, IF_TYPE_EN iftype, u8 *mac_addr)
{
    struct wireless_dev *wdev[NX_VIRT_STA_MAX];
    u32 index;

    if (0 != ecrnx_wdevs_get(seq, wdev))
        return -1;

    if (mac_addr == NULL)
        return -1;

    if (0 != ecrnx_wdev_match(wdev, iftype, &index))
        return -1;

    memcpy(mac_addr, wdev[index]->netdev->perm_addr, ETH_ALEN);

    return 0;
}

int ecrnx_mac_addr_get_ex(struct seq_file *seq, u8 *mac_addr_info)
{
    struct wireless_dev *wdev[NX_VIRT_STA_MAX];
    u32 i;

    if (0 != ecrnx_wdevs_get(seq, wdev))
        return -1;

    if (mac_addr_info == NULL)
        return -1;

    mac_addr_info[0] = '\0';
    for (i=0; i<NX_VIRT_STA_MAX; i++)
    {
        if (wdev[i] != NULL)
        {
            sprintf(mac_addr_info+strlen(mac_addr_info), "%s hw_port(%d) mac_addr=%02X:%02X:%02X:%02X:%02X:%02X\n", wdev[i]->netdev->name, wdev[i]->netdev->ifindex,
                wdev[i]->netdev->perm_addr[0], wdev[i]->netdev->perm_addr[1], wdev[i]->netdev->perm_addr[2],
                wdev[i]->netdev->perm_addr[3], wdev[i]->netdev->perm_addr[4], wdev[i]->netdev->perm_addr[5]);
        }
    }

    return 0;
}

int ecrnx_channel_get(struct seq_file *seq, IF_TYPE_EN iftype, struct cfg80211_chan_def *chandef)
{
    struct wireless_dev *wdev[NX_VIRT_STA_MAX];
    u32 index;

    if (0 != ecrnx_wdevs_get(seq, wdev))
        return -1;

    if (chandef == NULL)
        return -1;

    if (0 != ecrnx_wdev_match(wdev, iftype, &index))
        return -1;

    return ecrnx_cfg80211_get_channel(wdev[index]->wiphy, wdev[index], chandef);
}

int ecrnx_p2p_role_get(struct seq_file *seq, IF_TYPE_EN iftype)
{
    struct wireless_dev *wdev[NX_VIRT_STA_MAX];
    u32 index;

    if (0 != ecrnx_wdevs_get(seq, wdev))
        return -1;

    if (0 != ecrnx_wdev_match(wdev, iftype, &index))
        return -1;

    return wdev[index]->iftype;
}

int ecrnx_bssid_get(struct seq_file *seq, IF_TYPE_EN iftype, u8 *bssid)
{
    struct station_info sinfo;
    struct wireless_dev *wdev[NX_VIRT_STA_MAX];
    u32 index;

    if (0 != ecrnx_wdevs_get(seq, wdev))
        return -1;

    if (bssid == NULL)
        return -1;

    if (0 != ecrnx_wdev_match(wdev, iftype, &index))
        return -1;

    return ecrnx_cfg80211_dump_station(wdev[index]->wiphy, wdev[index]->netdev, 0, bssid, &sinfo);
}

int ecrnx_signal_level_get(struct seq_file *seq, IF_TYPE_EN iftype, s8 *noise_dbm)
{
    struct wireless_dev *wdev[NX_VIRT_STA_MAX];
    struct ecrnx_hw *ecrnx_hw;
    u32 index;

    if (0 != ecrnx_wdevs_get(seq, wdev))
        return -1;

    if (noise_dbm == NULL)
        return -1;

    if (0 != ecrnx_wdev_match(wdev, iftype, &index))
        return -1;

    ecrnx_hw = wdev_priv(wdev[index]);
    *noise_dbm = ecrnx_hw->survey[0].noise_dbm;

    return 0;
}

int ecrnx_flags_get(struct seq_file *seq, IF_TYPE_EN iftype, u32 *flags)
{
    struct wireless_dev *wdev[NX_VIRT_STA_MAX];
    u32 index;

    if (0 != ecrnx_wdevs_get(seq, wdev))
        return -1;

    if (flags == NULL)
        return -1;

    if (0 != ecrnx_wdev_match(wdev, iftype, &index))
        return -1;

    *flags = wdev[index]->wiphy->flags;

    return 0;
}

int ecrnx_ssid_get(struct seq_file *seq, IF_TYPE_EN iftype, char *ssid)
{
    struct wireless_dev *wdev[NX_VIRT_STA_MAX];
    u32 index;

    if (0 != ecrnx_wdevs_get(seq, wdev))
        return -1;

    if (ssid == NULL)
        return -1;

    if (0 != ecrnx_wdev_match(wdev, iftype, &index))
        return -1;

    memcpy(ssid, wdev[index]->ssid, IEEE80211_MAX_SSID_LEN);

    return 0;
}

int ecrnx_sta_mac_get(struct seq_file *seq, IF_TYPE_EN iftype, u8 sta_mac[][ETH_ALEN+1])
{
    struct ecrnx_hw *ecrnx_hw;
    u32 i;
    u8 mac[ETH_ALEN] = {0};

    if (seq->private != NULL)
        ecrnx_hw = seq->private;
    else
        return -1;

    // station table
    for (i=0; i<NX_REMOTE_STA_MAX; i++)
    {
        if (ecrnx_hw->sta_table[i].valid == true)
        {
            if (0 == memcmp(mac, ecrnx_hw->sta_table[i].mac_addr, ETH_ALEN))
            {
                sta_mac[i][0] = 0xFF;
                continue;
            }

            sta_mac[i][0] = 0xFF;
            memcpy(&sta_mac[i][1], ecrnx_hw->sta_table[i].mac_addr, ETH_ALEN);

        }
    }

    return 0;

}

int ecrnx_mac_reg_dump(struct seq_file *seq)
{
    u32 i, data;

    seq_printf(seq, "%-34s  %-11s %-10s\n", "name", "addr", "value");

    for (i=0; i<(sizeof(mac_reg_table)/sizeof(mac_reg_table[0])); i++)
    {
        ecrnx_slave_reg_read(mac_reg_table[i].addr, &data, 1);
        seq_printf(seq, "%-34s  0x%08X: 0x%08X\n", mac_reg_table[i].name, mac_reg_table[i].addr, data);
    }

    return 0;
}

int ecrnx_rf_reg_dump(struct seq_file *seq)
{
    u32 i, data;

    seq_printf(seq, "%-34s  %-11s %-10s\n", "name", "addr", "value");

    for (i=0; i<(sizeof(rf_reg_table)/sizeof(rf_reg_table[0])); i++)
    {
        ecrnx_slave_reg_read(rf_reg_table[i].addr, &data, 1);
        seq_printf(seq, "%-34s  0x%08X: 0x%08X\n", rf_reg_table[i].name, rf_reg_table[i].addr, data);
    }

    return 0;
}

int ecrnx_bb_reg_dump(struct seq_file *seq)
{
    u32 i, data;

    seq_printf(seq, "%-34s  %-11s %-10s\n", "name", "addr", "value");

    for (i=0; i<(sizeof(bb_reg_table)/sizeof(bb_reg_table[0])); i++)
    {
        ecrnx_slave_reg_read(bb_reg_table[i].addr, &data, 1);
        seq_printf(seq, "%-34s  0x%08X: 0x%08X\n", bb_reg_table[i].name, bb_reg_table[i].addr, data);
    }

    return 0;
}

int ecrnx_efuse_map_dump(struct seq_file *seq)
{
    u32 i, data;

    seq_printf(seq, "%-34s  %-11s %-10s\n", "name", "addr", "value");

    for (i=0; i<(sizeof(efuse_map_table)/sizeof(efuse_map_table[0])); i++)
    {
        ecrnx_slave_reg_read(efuse_map_table[i].addr, &data, 1);
        seq_printf(seq, "%-34s  0x%08X: 0x%08X\n", efuse_map_table[i].name, efuse_map_table[i].addr, data);
    }

    return 0;
}

int ecrnx_slave_reg_read(u32 addr, u32 *data, u32 len)
{
    if (data == NULL)
        return -1;

    debugfs_info.debugfs_type = SLAVE_READ_REG;
    debugfs_info.u.slave_read_reg_t.reg = addr;
    debugfs_info.u.slave_read_reg_t.len = len;
    ecrnx_debugfs_param_send(&debugfs_info);

    //wait for confirm
    debugfs_resp.rxdatas = 0;
    wait_event_interruptible_timeout(debugfs_resp.rxdataq, debugfs_resp.rxdatas, 1*HZ);

    if (debugfs_resp.rxdatas)
        memcpy((u8 *)data, (u8 *)debugfs_resp.rxdata, debugfs_resp.rxlen);

    return 0;
}

int ecrnx_slave_reg_write(u32 addr, u32 data, u32 len)
{
    debugfs_info.debugfs_type = SLAVE_WRITE_REG;
    debugfs_info.u.slave_write_reg_t.reg = addr;
    debugfs_info.u.slave_write_reg_t.value = data;
    ecrnx_debugfs_param_send(&debugfs_info);

    //wait for confirm
    debugfs_resp.rxdatas = 0;
    wait_event_interruptible_timeout(debugfs_resp.rxdataq, debugfs_resp.rxdatas, 1*HZ);

    return 0;
}

int ecrnx_slave_cli_send(u8 *cli, u8 *resp)
{
    if (cli == NULL)
        return -1;

    debugfs_info.debugfs_type = SLAVE_FUNC_INFO;
    strcpy(debugfs_info.u.slave_cli_func_info_t.func_and_param, cli);
    ecrnx_debugfs_param_send(&debugfs_info);

    //wait for confirm
    debugfs_resp.rxdatas = 0;
    wait_event_interruptible_timeout(debugfs_resp.rxdataq, debugfs_resp.rxdatas, 1*HZ);

    if ((debugfs_resp.rxdatas) && (debugfs_resp.rxlen))
    {
        ECRNX_PRINT("%s\n", debugfs_resp.rxdata);
    }

    return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
/**
 * ether_addr_copy - Copy an Ethernet address
 * @dst: Pointer to a six-byte array Ethernet address destination
 * @src: Pointer to a six-byte array Ethernet address source
 *
 * Please note: dst & src must both be aligned to u16.
 */
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	*(u32 *)dst = *(const u32 *)src;
	*(u16 *)(dst + 4) = *(const u16 *)(src + 4);
#else
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
#endif
}

#endif
void ecrnx_debugfs_survey_info_update(struct ecrnx_hw *ecrnx_hw, struct cfg80211_bss *bss)
{
	__le16 ie_cap = 0;
	const u8 *ssid_elm;
	const u8 *ie_wpa = NULL, *ie_wpa2 = NULL, *ie_wps = NULL;
	const u8 *ie_p2p = NULL;

	struct ecrnx_debugfs_survey_info_tbl *new_node, *entry, *tmp;
	new_node = kzalloc(sizeof(struct ecrnx_debugfs_survey_info_tbl),
		   GFP_ATOMIC);

	if (bss) {
		const struct cfg80211_bss_ies *ies;

		new_node->ch = ieee80211_frequency_to_channel(bss->channel->center_freq);
		ether_addr_copy(new_node->bssid, bss->bssid);
		new_node->rssi = bss->signal/100;

		rcu_read_lock();
	
		ie_wpa2 = ieee80211_bss_get_ie(bss, WLAN_EID_RSN);

		ies = rcu_dereference(bss->ies);

		ie_wpa = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
						WLAN_OUI_TYPE_MICROSOFT_WPA,
						ies->data,
						ies->len);

		ie_wps = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
						WLAN_OUI_TYPE_MICROSOFT_WPS,
						ies->data,
						ies->len);

		ie_p2p = cfg80211_find_vendor_ie(WLAN_OUI_WFA,
						WLAN_OUI_TYPE_WFA_P2P,
						ies->data,
						ies->len);
		
		ie_cap = cpu_to_le16(bss->capability);

		ssid_elm = cfg80211_find_ie(WLAN_EID_SSID, ies->data, ies->len);
		if (ssid_elm) {
			if (ssid_elm[1] <= IEEE80211_MAX_SSID_LEN)
				memcpy(new_node->ssid, ssid_elm + 2, ssid_elm[1]);
		}
		
		rcu_read_unlock();
	}

		sprintf(new_node->flag, "%s%s%s%s%s%s",
			(ie_wpa) ? "[WPA]" : "",
			(ie_wpa2) ? "[WPA2]" : "",
			(!ie_wpa && !ie_wpa && ie_cap & BIT(4)) ? "[WEP]" : "",
			(ie_wps) ? "[WPS]" : "",
			(ie_cap & BIT(0)) ? "[ESS]" : "",
			(ie_p2p) ? "[P2P]" : "");
				
	INIT_LIST_HEAD(&new_node->list);

	//if (!list_empty(&ecrnx_hw->debugfs_survey_info_tbl_ptr)) {
		list_for_each_entry_safe(entry, tmp, &ecrnx_hw->debugfs_survey_info_tbl_ptr, list) {
			if(memcmp(entry->bssid, new_node->bssid, ETH_ALEN) == 0) {
				list_del(&entry->list);
				kfree(entry);
			}
		}
	//}
	list_add_tail(&new_node->list, &ecrnx_hw->debugfs_survey_info_tbl_ptr);
}

void ecrnx_debugfs_noise_of_survey_info_update(struct ecrnx_hw *ecrnx_hw, struct ecrnx_survey_info *ecrnx_survey, int chan_no)
{
	struct ecrnx_debugfs_survey_info_tbl *entry;
	
	list_for_each_entry(entry, &ecrnx_hw->debugfs_survey_info_tbl_ptr, list) {
		if(entry->ch == chan_no) {
			entry->noise = ecrnx_survey->noise_dbm;
		}
	}
}

void ecrnx_debugfs_add_station_in_ap_mode(struct ecrnx_hw *ecrnx_hw,
                                            struct ecrnx_sta *sta, struct station_parameters *params)
{
    struct ecrnx_debugfs_sta *debugfs_sta;
    //char file_name[18];

    if(!sta || !params)
    {
        ECRNX_ERR("%s-%d:sta(%p) or params(%p) is null, return!\n", __func__, __LINE__, sta, params);
        return;
    }
	
    debugfs_sta = kzalloc(sizeof(struct ecrnx_debugfs_sta), GFP_KERNEL);

    if (!debugfs_sta)
        ECRNX_ERR("error debugfs_sta kzalloc!\n");


    debugfs_sta->sta_idx = sta->sta_idx;
    debugfs_sta->aid = sta->aid;
    debugfs_sta->ch_idx = sta->ch_idx;
    debugfs_sta->ht = sta->ht;
    ether_addr_copy(debugfs_sta->mac_addr, sta->mac_addr);
    debugfs_sta->qos = sta->qos;
    debugfs_sta->vht = sta->vht;
    debugfs_sta->width = sta->width;

    if(sta->ht)
    {
        if (params->ht_capa->cap_info & IEEE80211_HT_CAP_TX_STBC)
            debugfs_sta->stbc_cap = 1;
        if (params->ht_capa->cap_info & IEEE80211_HT_CAP_LDPC_CODING)
            debugfs_sta->ldpc_cap = 1;
        if (params->ht_capa->cap_info & IEEE80211_HT_CAP_SGI_20)
            debugfs_sta->sgi_20m = 1;
        if (params->ht_capa->cap_info & IEEE80211_HT_CAP_SGI_40)
            debugfs_sta->sgi_40m = 1;
    }

    ecrnx_debugfs_sta_in_ap_init(debugfs_sta);
}

int cli_parse_args(uint8_t* str, char* argv[])
{
    int i = 0;
    char* ch = (char *)str;

    while(*ch == ' ') ch++;
    if (*ch == '\n')
        return 0;

    while(*ch != '\0') 
    {
        i++;

        if (i > MAX_ARGV)
            return 0;

        argv[i-1] = ch;
        while(*ch != '\0')
        {
            if(*ch == ' ')
                break;
            else
                ch++;
        }

        *ch = '\0';
        ch++;
        while(*ch == ' ') {
            ch++;
        }
    }

    return i;
}

void argv_display(uint32_t argc, char* argv[])
{
    uint32_t i;

    ECRNX_PRINT("argc is %d\n", argc);
    for (i=0; i<argc; i++)
    {
        ECRNX_PRINT("param %d is %s\n", i, argv[i]);
    }
}

int _atoi(char *pstr)
{
    int ret_integer = 0;
    int integer_sign = 1;

    if (pstr == NULL)
    {
        //printk("Pointer is NULL\n");
        return 0;
    }

    while (*pstr == ' ')
    {
        pstr++;
    }

    if (*pstr == '-')
    {
        integer_sign = -1;
    }
    if (*pstr == '-' || *pstr == '+')
    {
        pstr++;
    }

    while (*pstr >= '0' && *pstr <= '9')
    {
        ret_integer = ret_integer * 10 + *pstr - '0';
        pstr++;
    }
    ret_integer = integer_sign * ret_integer;

    return ret_integer;
}

int cli_check_parm_num(uint32_t index, uint8_t *param)
{
    argc = cli_parse_args(param, argv);
    //argv_display(argc, argv);

    if (argc != ecrnx_cli_tab[index].param_num)
        return -1;

    return 0;
}

int cli_check_parm_range(uint32_t start_index, uint32_t index, uint32_t argc, char* argv[])
{
    uint32_t i;

    for (i=start_index; i<argc; i++)
    {
        arg_val[i] = _atoi(argv[i]);
        if ((arg_val[i] < ecrnx_cli_tab[index].param_range[i].range_min)
            || (arg_val[i] > ecrnx_cli_tab[index].param_range[i].range_max))
        {
            ECRNX_PRINT("param %d is out of range! current %d, min %d, max %d\n",
                i, arg_val[i], ecrnx_cli_tab[index].param_range[i].range_min, ecrnx_cli_tab[index].param_range[i].range_max);
            return -1;
        }
    }

    return 0;
}

int cli_macbyp_start(uint32_t index, uint8_t *param)
{
    uint32_t i;

    // parse parameters
    if (0 != cli_check_parm_num(index, param))
        return -1;

    // check range value
    if (0 != strcmp(argv[0], "11a")
        && 0 != strcmp(argv[0], "11b")
        && 0 != strcmp(argv[0], "11g")
        && 0 != strcmp(argv[0], "11n")
        && 0 != strcmp(argv[0], "11ac")
        && 0 != strcmp(argv[0], "11ax"))
        return -1;

    if (0 != cli_check_parm_range(1, index, argc, argv))
        return -1;

    // package command
    sprintf(slave_cli_cmd, "%s %s", ecrnx_cli_tab[index].map_cmd, argv[0]);
    for (i=1; i<argc; i++)
        sprintf(slave_cli_cmd+strlen(slave_cli_cmd), " %d", arg_val[i]);

    //printk("--->The final command is: %s<-\n", slave_cli_cmd);
    ecrnx_slave_cli_send(slave_cli_cmd, NULL);

    return 0;
}

int cli_macbyp_stop(uint32_t index, uint8_t *param)
{
    // parse parameters
    if (0 != cli_check_parm_num(index, param))
        return -1;

    // package command
    sprintf(slave_cli_cmd, "%s", ecrnx_cli_tab[index].map_cmd);

    //printk("---> The final command is: %s<-\n", slave_cli_cmd);
    ecrnx_slave_cli_send(slave_cli_cmd, NULL);

    return 0;
}

int cli_rf_txgain(uint32_t index, uint8_t *param)
{
    uint32_t i;

    // parse parameters
    if (0 != cli_check_parm_num(index, param))
        return -1;

    // check range value
    if (0 != cli_check_parm_range(0, index, argc, argv))
        return -1;

    // package command
    sprintf(slave_cli_cmd, "%s", ecrnx_cli_tab[index].map_cmd);
    for (i=0; i<argc; i++)
        sprintf(slave_cli_cmd+strlen(slave_cli_cmd), " %d", arg_val[i]);

    //printk("---> The final command is: %s<-\n", slave_cli_cmd);
    ecrnx_slave_cli_send(slave_cli_cmd, NULL);

    return 0;
}

int cli_rf_rxgain(uint32_t index, uint8_t *param)
{
    uint32_t i;

    // parse parameters
    if (0 != cli_check_parm_num(index, param))
        return -1;

    // check range value
    if (0 != cli_check_parm_range(0, index, argc, argv))
        return -1;

    // package command
    sprintf(slave_cli_cmd, "%s", ecrnx_cli_tab[index].map_cmd);
    for (i=0; i<argc; i++)
        sprintf(slave_cli_cmd+strlen(slave_cli_cmd), " %d", arg_val[i]);

    //printk("---> The final command is: %s<-\n", slave_cli_cmd);
    ecrnx_slave_cli_send(slave_cli_cmd, NULL);

    return 0;
}

int cli_rf_chan(uint32_t index, uint8_t *param)
{
    uint32_t i;

    // parse parameters
    if (0 != cli_check_parm_num(index, param))
        return -1;

    // check range value
    if (0 != cli_check_parm_range(0, index, argc, argv))
        return -1;

    // package command
    sprintf(slave_cli_cmd, "%s", ecrnx_cli_tab[index].map_cmd);
    for (i=0; i<argc; i++)
        sprintf(slave_cli_cmd+strlen(slave_cli_cmd), " %d", arg_val[i]);

    //printk("---> The final command is: %s<-\n", slave_cli_cmd);
    ecrnx_slave_cli_send(slave_cli_cmd, NULL);

    return 0;
}

int cli_cmd_parse(uint8_t *cmd)
{
    uint32_t i;
    int ret;

    if (cmd == NULL)
        return -1;

    //printk("cmd is :%s<-\n", cmd);
    for (i=0; i<sizeof(ecrnx_cli_tab)/sizeof(ecrnx_cli_tab[0]); i++)
    {
        if (0 == memcmp(cmd, ecrnx_cli_tab[i].cmd, strlen(ecrnx_cli_tab[i].cmd)))
        {
            //printk("index %d, cmd %s\n", i, ecrnx_cli_tab[i].cmd);
            ret = ecrnx_cli_tab[i].cmd_func(i, cmd + strlen(ecrnx_cli_tab[i].cmd));
            goto exit;
        }
    }

    ECRNX_ERR("No matching commands!\n");

    return -2;

exit:
    return ret;
}
#endif

