/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
#include <linux/export.h>
#else
#include <linux/module.h>
#endif
#include <linux/platform_device.h>
#include <linux/string.h>
#include <ssv_conf_parser.h>
#include <ssv6200_reg.h>
#include <ssv6200.h>
#include <hci/hctrl.h>
#include <smac/dev.h>
#include "ssv_cmd.h"
#include <ssv_version.h>
#ifndef CONFIG_SSV_CABRIO_A
#include <ssv6200_configuration.h>
#endif
#define SSV_CMD_PRINTF() 
struct ssv6xxx_dev_table {
    u32 address;
    u32 val;
};
struct ssv6xxx_debug {
    struct device *dev;
    struct platform_device *pdev;
    struct ssv6xxx_hwif_ops *ifops;
};
static struct ssv6xxx_debug *ssv6xxx_debug_ifops;
static char sg_cmd_buffer[CLI_BUFFER_SIZE+1];
static char *sg_argv[CLI_ARG_SIZE];
static u32 sg_argc;
extern char *ssv6xxx_result_buf;
#if defined (CONFIG_ARM64) || defined (__x86_64__)
u64 ssv6xxx_ifdebug_info[3] = { 0, 0, 0 };
#else
u32 ssv6xxx_ifdebug_info[3] = { 0, 0, 0 };
#endif
EXPORT_SYMBOL(ssv6xxx_ifdebug_info);
struct sk_buff *ssvdevice_skb_alloc(s32 len)
{
    struct sk_buff *skb;
    skb = __dev_alloc_skb(len + SSV6200_ALLOC_RSVD , GFP_KERNEL);
    if (skb != NULL) {
        skb_put(skb,0x20);
        skb_pull(skb,0x20);
    }
    return skb;
}
void ssvdevice_skb_free(struct sk_buff *skb)
{
    dev_kfree_skb_any(skb);
}
static int ssv_cmd_help(int argc, char *argv[])
{
    extern struct ssv_cmd_table cmd_table[];
    struct ssv_cmd_table *sc_tbl;
    char tmpbf[161];
    int total_cmd=0;
    {
        sprintf(ssv6xxx_result_buf, "Usage:\n");
        for( sc_tbl=&cmd_table[3]; sc_tbl->cmd; sc_tbl ++ ) {
            sprintf(tmpbf, "%-20s\t\t%s\n", sc_tbl->cmd, sc_tbl->usage);
            strcat(ssv6xxx_result_buf, tmpbf);
            total_cmd ++;
        }
        sprintf(tmpbf, "Total CMDs: %d\n\nType cli help [CMD] for more detail command.\n\n", total_cmd);
        strcat(ssv6xxx_result_buf, tmpbf);
    }
    return 0;
}
static int ssv_cmd_reg(int argc, char *argv[])
{
    u32 addr, value, count;
    char tmpbf[64], *endp;
    int s;
    if (argc == 4 && strcmp(argv[1], "w")==0) {
        addr = simple_strtoul(argv[2], &endp, 16);
        value = simple_strtoul(argv[3], &endp, 16);
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, addr, value));
        sprintf(ssv6xxx_result_buf, " => write [0x%08x]: 0x%08x\n",
            addr, value);
        return 0;
    }
    else if ((argc==4||argc==3) && strcmp(argv[1], "r")==0) {
        count = (argc==3)? 1: simple_strtoul(argv[3], &endp, 10);
        addr = simple_strtoul(argv[2], &endp, 16);
        sprintf(ssv6xxx_result_buf, "ADDRESS: 0x%08x\n", addr);
        for(s=0; s<count; s++, addr+=4) {
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value));
            sprintf(tmpbf, "%08x ", value);
            strcat(ssv6xxx_result_buf, tmpbf);
            if (((s+1)&0x07) == 0)
                strcat(ssv6xxx_result_buf, "\n");
        }
        strcat(ssv6xxx_result_buf, "\n");
        return 0;
    }
    else
    {
        sprintf(tmpbf, "reg [r|w] [address] [value|word-count]\n\n");
        strcat(ssv6xxx_result_buf, tmpbf);
        return 0;
    }
    return -1;
}
struct ssv6xxx_cfg ssv_cfg;
EXPORT_SYMBOL(ssv_cfg);
#if 0
static int __string2s32(u8 *val_str, void *val)
{
    char *endp;
    int base=10;
    if (val_str[0]=='0' && ((val_str[1]=='x')||(val_str[1]=='X')))
        base = 16;
    *(int *)val = simple_strtoul(val_str, &endp, base);
    return 0;
}
#endif
#if 0
static int __string2bool(u8 *u8str, void *val, u32 arg)
{
    char *endp;
 *(u8 *)val = !!simple_strtoul(u8str, &endp, 10);
    return 0;
}
#endif
static int __string2u32(u8 *u8str, void *val, u32 arg)
{
    char *endp;
    int base=10;
    if (u8str[0]=='0' && ((u8str[1]=='x')||(u8str[1]=='X')))
        base = 16;
    *(u32 *)val = simple_strtoul(u8str, &endp, base);
    return 0;
}
static int __string2flag32(u8 *flag_str, void *flag, u32 arg)
{
    u32 *val=(u32 *)flag;
    if (arg >= (sizeof(u32)<<3))
        return -1;
    if (strcmp(flag_str, "on")==0) {
        *val |= (1<<arg);
        return 0;
    }
    if (strcmp(flag_str, "off")==0) {
        *val &= ~(1<<arg);
        return 0;
    }
    return -1;
}
static int __string2mac(u8 *mac_str, void *val, u32 arg)
{
    int s, macaddr[6];
    u8 *mac=(u8 *)val;
    s = sscanf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
        &macaddr[0], &macaddr[1], &macaddr[2],
        &macaddr[3], &macaddr[4], &macaddr[5]);
    if (s != 6)
        return -1;
    mac[0] = (u8)macaddr[0], mac[1] = (u8)macaddr[1];
    mac[2] = (u8)macaddr[2], mac[3] = (u8)macaddr[3];
    mac[4] = (u8)macaddr[4], mac[5] = (u8)macaddr[5];
    return 0;
}
static int __string2str(u8 *path, void *val, u32 arg)
{
    u8 *temp=(u8 *)val;
    sprintf(temp,"%s",path);
    return 0;
}
static int __string2configuration(u8 *mac_str, void *val, u32 arg)
{
    unsigned int address,value;
    int i;
    i = sscanf(mac_str, "%08x:%08x", &address, &value);
    if (i != 2)
        return -1;
    for(i=0; i<EXTERNEL_CONFIG_SUPPORT; i++)
    {
        if(ssv_cfg.configuration[i][0] == 0x0)
        {
            ssv_cfg.configuration[i][0] = address;
            ssv_cfg.configuration[i][1] = value;
            return 0;
        }
    }
    return 0;
}
struct ssv6xxx_cfg_cmd_table cfg_cmds[] = {
    { "hw_mac", (void *)&ssv_cfg.maddr[0][0], 0, __string2mac },
    { "hw_mac_2", (void *)&ssv_cfg.maddr[1][0], 0, __string2mac },
    { "def_chan", (void *)&ssv_cfg.def_chan, 0, __string2u32 },
    { "hw_cap_ht", (void *)&ssv_cfg.hw_caps, 0, __string2flag32 },
    { "hw_cap_gf", (void *)&ssv_cfg.hw_caps, 1, __string2flag32 },
    { "hw_cap_2ghz", (void *)&ssv_cfg.hw_caps, 2, __string2flag32 },
    { "hw_cap_5ghz", (void *)&ssv_cfg.hw_caps, 3, __string2flag32 },
    { "hw_cap_security", (void *)&ssv_cfg.hw_caps, 4, __string2flag32 },
    { "hw_cap_sgi_20", (void *)&ssv_cfg.hw_caps, 5, __string2flag32 },
    { "hw_cap_sgi_40", (void *)&ssv_cfg.hw_caps, 6, __string2flag32 },
    { "hw_cap_ap", (void *)&ssv_cfg.hw_caps, 7, __string2flag32 },
    { "hw_cap_p2p", (void *)&ssv_cfg.hw_caps, 8, __string2flag32 },
    { "hw_cap_ampdu_rx", (void *)&ssv_cfg.hw_caps, 9, __string2flag32 },
    { "hw_cap_ampdu_tx", (void *)&ssv_cfg.hw_caps, 10, __string2flag32 },
    { "hw_cap_tdls", (void *)&ssv_cfg.hw_caps, 11, __string2flag32 },
    { "use_wpa2_only", (void *)&ssv_cfg.use_wpa2_only, 0, __string2u32 },
    { "wifi_tx_gain_level_gn",(void *)&ssv_cfg.wifi_tx_gain_level_gn, 0, __string2u32 },
    { "wifi_tx_gain_level_b", (void *)&ssv_cfg.wifi_tx_gain_level_b, 0, __string2u32 },
    { "rssi_ctl", (void *)&ssv_cfg.rssi_ctl, 0, __string2u32 },
    { "xtal_clock", (void *)&ssv_cfg.crystal_type, 0, __string2u32 },
    { "volt_regulator", (void *)&ssv_cfg.volt_regulator, 0, __string2u32 },
    { "force_chip_identity", (void *)&ssv_cfg.force_chip_identity, 0, __string2u32 },
    { "firmware_path", (void *)&ssv_cfg.firmware_path[0], 0, __string2str },
    { "flash_bin_path",(void *)&ssv_cfg.flash_bin_path[0],0,__string2str },
    { "mac_address_path", (void *)&ssv_cfg.mac_address_path[0], 0, __string2str },
    { "mac_output_path", (void *)&ssv_cfg.mac_output_path[0], 0, __string2str },
    { "ignore_efuse_mac", (void *)&ssv_cfg.ignore_efuse_mac, 0, __string2u32 },
    { "mac_address_mode", (void *)&ssv_cfg.mac_address_mode, 0, __string2u32 },
    { "sr_bhvr", (void *)&ssv_cfg.sr_bhvr, 0, __string2u32 },
    { "register", NULL, 0, __string2configuration },
    { NULL, NULL, 0, NULL },
};
EXPORT_SYMBOL(cfg_cmds);
static int ssv_cmd_cfg(int argc, char *argv[])
{
    char temp_buf[64];
    int s;
    if (argc==2 && strcmp(argv[1], "reset")==0) {
        memset(&ssv_cfg, 0, sizeof(ssv_cfg));
        return 0;
    }
    else if (argc==2 && strcmp(argv[1], "show")==0) {
        strcpy(ssv6xxx_result_buf, ">> ssv6xxx config:\n");
        sprintf(temp_buf, "    hw_caps = 0x%08x\n", ssv_cfg.hw_caps);
        strcat(ssv6xxx_result_buf, temp_buf);
        sprintf(temp_buf, "    def_chan = %d\n", ssv_cfg.def_chan);
        strcat(ssv6xxx_result_buf, temp_buf);
        sprintf(temp_buf, "    wifi_tx_gain_level_gn = %d\n", ssv_cfg.wifi_tx_gain_level_gn);
        strcat(ssv6xxx_result_buf, temp_buf);
        sprintf(temp_buf, "    wifi_tx_gain_level_b = %d\n", ssv_cfg.wifi_tx_gain_level_b);
        strcat(ssv6xxx_result_buf, temp_buf);
        sprintf(temp_buf, "    rssi_ctl = %d\n", ssv_cfg.rssi_ctl);
        strcat(ssv6xxx_result_buf, temp_buf);
        sprintf(temp_buf, "    sr_bhvr = %d\n", ssv_cfg.sr_bhvr);
        strcat(ssv6xxx_result_buf, temp_buf);
        sprintf(temp_buf, "    sta-mac = %02x:%02x:%02x:%02x:%02x:%02x",
            ssv_cfg.maddr[0][0], ssv_cfg.maddr[0][1], ssv_cfg.maddr[0][2],
            ssv_cfg.maddr[0][3], ssv_cfg.maddr[0][4], ssv_cfg.maddr[0][5]);
        strcat(ssv6xxx_result_buf, temp_buf);
        strcat(ssv6xxx_result_buf, "\n");
        return 0;
    }
    if (argc != 4)
        return -1;
    for(s=0; cfg_cmds[s].cfg_cmd!=NULL; s++) {
        if (strcmp(cfg_cmds[s].cfg_cmd, argv[1])==0) {
            cfg_cmds[s].translate_func(argv[3],
                cfg_cmds[s].var, cfg_cmds[s].arg);
            strcpy(ssv6xxx_result_buf, "");
            return 0;
        }
    }
    return -1;
}
void *ssv_dbg_phy_table = NULL;
EXPORT_SYMBOL(ssv_dbg_phy_table);
u32 ssv_dbg_phy_len = 0;
EXPORT_SYMBOL(ssv_dbg_phy_len);
void *ssv_dbg_rf_table = NULL;
EXPORT_SYMBOL(ssv_dbg_rf_table);
u32 ssv_dbg_rf_len = 0;
EXPORT_SYMBOL(ssv_dbg_rf_len);
struct ssv_softc *ssv_dbg_sc = NULL;
EXPORT_SYMBOL(ssv_dbg_sc);
struct ssv6xxx_hci_ctrl *ssv_dbg_ctrl_hci = NULL;
EXPORT_SYMBOL(ssv_dbg_ctrl_hci);
struct Dump_Sta_Info {
    char *dump_buf;
    int sta_idx;
};
static void _dump_sta_info (struct ssv_softc *sc,
                            struct ssv_vif_info *vif_info,
                            struct ssv_sta_info *sta_info,
                            void *param)
{
    char tmpbf[128];
    struct Dump_Sta_Info *dump_sta_info = (struct Dump_Sta_Info *)param;
    struct ssv_sta_priv_data *priv_sta = (struct ssv_sta_priv_data *)sta_info->sta->drv_priv;
    if ((sta_info->s_flags & STA_FLAG_VALID) == 0)
        sprintf(tmpbf,
                "        Station %d: %d is not valid\n",
                dump_sta_info->sta_idx, priv_sta->sta_idx);
    else
        sprintf(tmpbf,
                "        Station %d: %d\n"
                "             Address: %02X:%02X:%02X:%02X:%02X:%02X\n"
                "             WISD: %d\n"
                "             AID: %d\n"
                "             Sleep: %d\n",
                dump_sta_info->sta_idx, priv_sta->sta_idx,
                sta_info->sta->addr[0], sta_info->sta->addr[1], sta_info->sta->addr[2],
                sta_info->sta->addr[3], sta_info->sta->addr[4], sta_info->sta->addr[5],
                sta_info->hw_wsid, sta_info->aid, sta_info->sleeping);
    dump_sta_info->sta_idx++;
    strcat(dump_sta_info->dump_buf, tmpbf);
}
void ssv6xxx_dump_sta_info (struct ssv_softc *sc, char *target_buf)
{
    int j;
    char tmpbf[128];
    struct Dump_Sta_Info dump_sta_info = {target_buf, 0};
    sprintf(tmpbf, "  >>>> bcast queue len[%d]\n", sc->bcast_txq.cur_qsize);
    strcat(target_buf, tmpbf);
    for (j=0; j<SSV6200_MAX_VIF; j++) {
        struct ieee80211_vif *vif = sc->vif_info[j].vif;
        struct ssv_vif_priv_data *priv_vif;
        struct ssv_sta_priv_data *sta_priv_iter;
        if (vif == NULL)
        {
            sprintf(tmpbf, "    VIF: %d is not used.\n", j);
            strcat(target_buf, tmpbf);
            continue;
        }
        sprintf(tmpbf,
                "    VIF: %d - [%02X:%02X:%02X:%02X:%02X:%02X] type[%d] p2p[%d]\n", j,
                vif->addr[0], vif->addr[1], vif->addr[2],
                vif->addr[3], vif->addr[4], vif->addr[5], vif->type, vif->p2p);
        strcat(target_buf, tmpbf);
        priv_vif = (struct ssv_vif_priv_data *)(vif->drv_priv);
        list_for_each_entry(sta_priv_iter, &priv_vif->sta_list, list)
        {
            if ((sta_priv_iter->sta_info->s_flags & STA_FLAG_VALID) == 0)
            {
                sprintf(tmpbf, "    VIF: %d  is not valid.\n", j);
                strcat(target_buf, tmpbf);
                continue;
            }
            _dump_sta_info(sc, &sc->vif_info[priv_vif->vif_idx],
                           sta_priv_iter->sta_info, &dump_sta_info);
        }
    }
#if 0
    sta set channel 7
    if (argc >= 2 && strcmp(argv[1], "set")==0) {
        if (argc==4 && strcmp(argv[2], "channel")==0) {
            char *endp;
            int ch=simple_strtoul(argv[3], &endp, 10);
            if (ch>=0 && ch<=13) {
            }
            return -1;
        }
    }
#endif
}
static int ssv_cmd_sta(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "show")==0)
        ssv6xxx_dump_sta_info(ssv_dbg_sc, ssv6xxx_result_buf);
    else
        strcat(ssv6xxx_result_buf, "sta show\n\n");
    return 0;
}
static int ssv_cmd_dump(int argc, char *argv[])
{
    u32 addr, regval;
    char tmpbf[64];
    int s;
 if(!ssv6xxx_result_buf)
 {
  printk("ssv6xxx_result_buf = NULL!!\n");
  return -1;
 }
    if (argc != 2)
    {
        sprintf(tmpbf, "dump [wsid|decision|phy-info|phy-reg|rf-reg]\n");
        strcat(ssv6xxx_result_buf, tmpbf);
        return 0;
    }
    if (strcmp(argv[1], "wsid") == 0) {
        const u32 reg_wsid[]={ ADR_WSID0, ADR_WSID1 };
     const u32 reg_wsid_tid0[]={ ADR_WSID0_TID0_RX_SEQ, ADR_WSID1_TID0_RX_SEQ };
     const u32 reg_wsid_tid7[]={ ADR_WSID0_TID7_RX_SEQ, ADR_WSID1_TID7_RX_SEQ };
        const u8 *op_mode_str[]={"STA", "AP", "AD-HOC", "WDS"};
        const u8 *ht_mode_str[]={"Non-HT", "HT-MF", "HT-GF", "RSVD"};
        for(s=0; s<SSV_NUM_HW_STA; s++) {
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, reg_wsid[s], &regval));
            sprintf(tmpbf, "==>WSID[%d]\n\tvalid[%d] qos[%d] op_mode[%s] ht_mode[%s]\n",
                s, regval&0x1, (regval>>1)&0x1, op_mode_str[((regval>>2)&3)], ht_mode_str[((regval>>4)&3)]);
            strcat(ssv6xxx_result_buf, tmpbf);
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, reg_wsid[s]+4, &regval));
            sprintf(tmpbf, "\tMAC[%02x:%02x:%02x:%02x:",
                   (regval&0xff), ((regval>>8)&0xff), ((regval>>16)&0xff), ((regval>>24)&0xff));
            strcat(ssv6xxx_result_buf, tmpbf);
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, reg_wsid[s]+8, &regval));
            sprintf(tmpbf, "%02x:%02x]\n",
                   (regval&0xff), ((regval>>8)&0xff));
            strcat(ssv6xxx_result_buf, tmpbf);
            for(addr=reg_wsid_tid0[s]; addr<=reg_wsid_tid7[s]; addr+=4){
                if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &regval));
                sprintf(tmpbf, "\trx_seq%d[%d]\n", ((addr-reg_wsid_tid0[s])>>2), ((regval)&0xffff));
                strcat(ssv6xxx_result_buf, tmpbf);
            }
        }
        return 0;
    }
    if (strcmp(argv[1], "decision") ==0 ) {
        strcpy(ssv6xxx_result_buf, ">> Decision Table:\n");
        for(s=0,addr=ADR_MRX_FLT_TB0; s<16; s++, addr+=4) {
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &regval));
            sprintf(tmpbf, "   [%d]: ADDR[0x%08x] = 0x%08x\n",
                s, addr, regval);
            strcat(ssv6xxx_result_buf, tmpbf);
        }
        strcat(ssv6xxx_result_buf, "\n\n>> Decision Mask:\n");
        for(s=0,addr=ADR_MRX_FLT_EN0; s<9; s++, addr+=4) {
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &regval));
            sprintf(tmpbf, "   [%d]: ADDR[0x%08x] = 0x%08x\n",
                s, addr, regval);
            strcat(ssv6xxx_result_buf, tmpbf);
        }
        strcat(ssv6xxx_result_buf, "\n\n");
        return 0;
    }
    if (strcmp(argv[1], "phy-info") == 0) {
        return 0;
    }
    if (strcmp(argv[1], "phy-reg") == 0) {
        struct ssv6xxx_dev_table *raw;
        raw = (struct ssv6xxx_dev_table *)ssv_dbg_phy_table;
        strcpy(ssv6xxx_result_buf, ">> PHY Register Table:\n");
        for(s=0; s<ssv_dbg_phy_len; s++, raw++) {
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, raw->address, &regval));
            sprintf(tmpbf, "   ADDR[0x%08x] = 0x%08x\n",
                raw->address, regval);
            strcat(ssv6xxx_result_buf, tmpbf);
        }
        strcat(ssv6xxx_result_buf, "\n\n");
        return 0;
    }
    if (strcmp(argv[1], "rf-reg") == 0) {
        struct ssv6xxx_dev_table *raw;
        raw = (struct ssv6xxx_dev_table *)ssv_dbg_rf_table;
        strcpy(ssv6xxx_result_buf, ">> RF Register Table:\n");
        for(s=0; s<ssv_dbg_rf_len; s++, raw++) {
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, raw->address, &regval));
            sprintf(tmpbf, "   ADDR[0x%08x] = 0x%08x\n",
                raw->address, regval);
            strcat(ssv6xxx_result_buf, tmpbf);
        }
        strcat(ssv6xxx_result_buf, "\n\n");
        return 0;
    }
    return -1;
}
static int ssv_cmd_irq(int argc, char *argv[])
{
    char *endp;
    u32 irq_sts;
    if (argc>=3 && strcmp(argv[1], "set")==0) {
        if (strcmp(argv[2], "mask")==0 && argc==4) {
            irq_sts = simple_strtoul(argv[3], &endp, 16);
       if(!ssv6xxx_debug_ifops->ifops->irq_setmask) {
             sprintf(ssv6xxx_result_buf, "The interface doesn't provide irq_setmask operation.\n");
             return 0;
       }
            ssv6xxx_debug_ifops->ifops->irq_setmask(
                ssv6xxx_debug_ifops->dev, irq_sts);
            sprintf(ssv6xxx_result_buf, "set sdio irq mask to 0x%08x\n", irq_sts);
            return 0;
        }
        if (strcmp(argv[2], "enable")==0) {
       if(!ssv6xxx_debug_ifops->ifops->irq_enable) {
             sprintf(ssv6xxx_result_buf, "The interface doesn't provide irq_enable operation.\n");
             return 0;
       }
            ssv6xxx_debug_ifops->ifops->irq_enable(
                ssv6xxx_debug_ifops->dev);
            strcpy(ssv6xxx_result_buf, "enable sdio irq.\n");
            return 0;
        }
        if (strcmp(argv[2], "disable")==0) {
       if(!ssv6xxx_debug_ifops->ifops->irq_disable) {
             sprintf(ssv6xxx_result_buf, "The interface doesn't provide irq_disable operation.\n");
             return 0;
       }
            ssv6xxx_debug_ifops->ifops->irq_disable(
                ssv6xxx_debug_ifops->dev, false);
            strcpy(ssv6xxx_result_buf, "disable sdio irq.\n");
            return 0;
        }
        return -1;
    }
    else if (argc==3 && strcmp(argv[1], "get")==0) {
        if (strcmp(argv[2], "mask") == 0) {
       if(!ssv6xxx_debug_ifops->ifops->irq_getmask) {
             sprintf(ssv6xxx_result_buf, "The interface doesn't provide irq_getmask operation.\n");
             return 0;
       }
            ssv6xxx_debug_ifops->ifops->irq_getmask(
                ssv6xxx_debug_ifops->dev, &irq_sts);
            sprintf(ssv6xxx_result_buf, "sdio irq mask: 0x%08x, int_mask=0x%08x\n", irq_sts,
                ssv_dbg_ctrl_hci->int_mask);
            return 0;
        }
        if (strcmp(argv[2], "status") == 0) {
       if(!ssv6xxx_debug_ifops->ifops->irq_getstatus) {
             sprintf(ssv6xxx_result_buf, "The interface doesn't provide irq_getstatus operation.\n");
             return 0;
       }
            ssv6xxx_debug_ifops->ifops->irq_getstatus(
                ssv6xxx_debug_ifops->dev, &irq_sts);
            sprintf(ssv6xxx_result_buf, "sdio irq status: 0x%08x\n", irq_sts);
            return 0;
        }
        return -1;
    }
    else
    {
        sprintf(ssv6xxx_result_buf, "irq [set|get] [mask|enable|disable|status]\n");
    }
    return 0;
}
static int ssv_cmd_mac(int argc, char *argv[])
{
    char temp_str[128], *endp;
    u32 s;
    int i;
    if (argc==3 && !strcmp(argv[1], "wsid") && !strcmp(argv[2], "show")) {
        for(s=0; s<SSV_NUM_HW_STA; s++) {
        }
        return 0;
    }
    else if (argc==3 && !strcmp(argv[1], "rx")){
        if(!strcmp(argv[2], "enable")){
            ssv_dbg_sc->dbg_rx_frame = 1;
        }
        else{
            ssv_dbg_sc->dbg_rx_frame = 0;
        }
        sprintf(temp_str, "  dbg_rx_frame %d\n", ssv_dbg_sc->dbg_rx_frame);
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    else if (argc==3 && !strcmp(argv[1], "tx")){
        if(!strcmp(argv[2], "enable")){
            ssv_dbg_sc->dbg_tx_frame = 1;
        }
        else{
            ssv_dbg_sc->dbg_tx_frame = 0;
        }
        sprintf(temp_str, "  dbg_tx_frame %d\n", ssv_dbg_sc->dbg_tx_frame);
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    else if (argc==3 && !strcmp(argv[1], "rxq") && !strcmp(argv[2], "show")) {
        sprintf(temp_str, ">> MAC RXQ: (%s)\n    cur_qsize=%d\n",
            ((ssv_dbg_sc->sc_flags&SC_OP_OFFCHAN)? "off channel": "on channel"),
            ssv_dbg_sc->rx.rxq_count);
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
#if 0
    if (argc==3 && !strcmp(argv[1], "tx") && !strcmp(argv[2], "status")) {
        sprintf(temp_str, ">> MAC TX Status:\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str, "    txq flow control: 0x%x\n", ssv_dbg_sc->tx.flow_ctrl_status);
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str, "    rxq cur_qsize: %d\n", ssv_dbg_sc->rx.rxq_count);
        strcat(ssv6xxx_result_buf, temp_str);
    }
#endif
    else if (argc==4 && !strcmp(argv[1], "set") && !strcmp(argv[2], "rate")) {
        if (strcmp(argv[3], "auto")==0) {
            ssv_dbg_sc->sc_flags &= ~SC_OP_FIXED_RATE;
            return 0;
        }
        i = simple_strtoul(argv[3], &endp, 10);
        if (i<0 || i>38) {
            strcpy(ssv6xxx_result_buf, " Invalid rat index !!\n");
            return -1;
        }
        ssv_dbg_sc->max_rate_idx = i;
        ssv_dbg_sc->sc_flags |= SC_OP_FIXED_RATE;
        sprintf(temp_str, " Set rate to index %d\n", i);
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    else if (argc==3 && !strcmp(argv[1], "get") && !strcmp(argv[2], "rate")) {
        if (ssv_dbg_sc->sc_flags & SC_OP_FIXED_RATE)
            sprintf(temp_str, " Current Rate Index: %d\n", ssv_dbg_sc->max_rate_idx);
        else sprintf(temp_str, "  Current Rate Index: auto\n");
        strcpy(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    else
    {
        sprintf(temp_str, "mac [security|wsid|rxq]  [show]\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str, "mac [set|get] [rate] [auto|idx]\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str, "mac [rx|tx] [eable|disable]\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    return 0;
}
#ifdef CONFIG_IRQ_DEBUG_COUNT
void print_irq_count(void)
{
 char temp_str[512];
 sprintf(temp_str, "irq debug (%s)\n",ssv_dbg_ctrl_hci->irq_enable?"enable":"disable");
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "total irq (%d)\n",ssv_dbg_ctrl_hci->irq_count);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "invalid irq (%d)\n",ssv_dbg_ctrl_hci->invalid_irq_count);
    strcat(ssv6xxx_result_buf, temp_str);
 sprintf(temp_str, "rx irq (%d)\n",ssv_dbg_ctrl_hci->rx_irq_count);
    strcat(ssv6xxx_result_buf, temp_str);
 sprintf(temp_str, "tx irq (%d)\n",ssv_dbg_ctrl_hci->tx_irq_count);
    strcat(ssv6xxx_result_buf, temp_str);
 sprintf(temp_str, "real tx count irq (%d)\n",ssv_dbg_ctrl_hci->real_tx_irq_count);
    strcat(ssv6xxx_result_buf, temp_str);
 sprintf(temp_str, "tx  packet count (%d)\n",ssv_dbg_ctrl_hci->irq_tx_pkt_count);
    strcat(ssv6xxx_result_buf, temp_str);
 sprintf(temp_str, "rx packet (%d)\n",ssv_dbg_ctrl_hci->irq_rx_pkt_count);
    strcat(ssv6xxx_result_buf, temp_str);
}
#endif
void print_isr_info(void)
{
    char temp_str[512];
    sprintf(temp_str, ">>>> HCI Calculate ISR TIME(%s) unit:us\n",
        ((ssv_dbg_ctrl_hci->isr_summary_eable)? "enable": "disable"));
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "isr_routine_time(%d)\n",
        jiffies_to_usecs(ssv_dbg_ctrl_hci->isr_routine_time));
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "isr_tx_time(%d)\n",
        jiffies_to_usecs(ssv_dbg_ctrl_hci->isr_tx_time));
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "isr_rx_time(%d)\n",
        jiffies_to_usecs(ssv_dbg_ctrl_hci->isr_rx_time));
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "isr_idle_time(%d)\n",
        jiffies_to_usecs(ssv_dbg_ctrl_hci->isr_idle_time));
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "isr_rx_idle_time(%d)\n",
        jiffies_to_usecs(ssv_dbg_ctrl_hci->isr_rx_idle_time));
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "isr_miss_cnt(%d)\n",
        ssv_dbg_ctrl_hci->isr_miss_cnt);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "prev_isr_jiffes(%lu)\n",
        ssv_dbg_ctrl_hci->prev_isr_jiffes);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str, "prev_rx_isr_jiffes(%lu)\n",
        ssv_dbg_ctrl_hci->prev_rx_isr_jiffes);
    strcat(ssv6xxx_result_buf, temp_str);
}
static int ssv_cmd_hci(int argc, char *argv[])
{
    struct ssv_hw_txq *txq;
    char temp_str[512];
    int s,ac = 0;
    if (argc==3 && !strcmp(argv[1], "txq") && !strcmp(argv[2], "show") )
    {
        for(s=0; s<WMM_NUM_AC; s++) {
            if (ssv_dbg_sc != NULL)
                ac = ssv_dbg_sc->tx.ac_txqid[s];
            txq = &ssv_dbg_ctrl_hci->hw_txq[s];
            sprintf(temp_str, ">> txq[%d]", txq->txq_no);
            if (ssv_dbg_sc != NULL)
                sprintf(temp_str, "(%s): ",((ssv_dbg_sc->sc_flags&SC_OP_OFFCHAN)? "off channel": "on channel"));
            sprintf(temp_str, "cur_qsize=%d\n", skb_queue_len(&txq->qhead));
            strcat(ssv6xxx_result_buf, temp_str);
            sprintf(temp_str, "            max_qsize=%d, pause=%d, resume_thres=%d",
                txq->max_qsize, txq->paused, txq->resum_thres);\
            if (ssv_dbg_sc != NULL)
                sprintf(temp_str, " flow_control[%d]\n", !!(ssv_dbg_sc->tx.flow_ctrl_status&(1<<ac)));
            strcat(ssv6xxx_result_buf, temp_str);
            sprintf(temp_str, "            Total %d frame sent\n", txq->tx_pkt);
            strcat(ssv6xxx_result_buf, temp_str);
        }
        sprintf(temp_str, ">> HCI Debug Counters:\n    read_rs0_info_fail=%d, read_rs1_info_fail=%d\n",
                    ssv_dbg_ctrl_hci->read_rs0_info_fail, ssv_dbg_ctrl_hci->read_rs1_info_fail);
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str, "    rx_work_running=%d, isr_running=%d, xmit_running=%d\n",
                ssv_dbg_ctrl_hci->rx_work_running, ssv_dbg_ctrl_hci->isr_running,
                ssv_dbg_ctrl_hci->xmit_running);
        strcat(ssv6xxx_result_buf, temp_str);
        if (ssv_dbg_sc != NULL)
            sprintf(temp_str, "    flow_ctrl_status=%08x\n", ssv_dbg_sc->tx.flow_ctrl_status);
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    else if (argc==3 && !strcmp(argv[1], "rxq") && !strcmp(argv[2], "show") ) {
        sprintf(temp_str, ">> HCI RX Queue (%s): cur_qsize=%d\n",
            ((ssv_dbg_sc->sc_flags&SC_OP_OFFCHAN)? "off channel": "on channel"),
            ssv_dbg_ctrl_hci->rx_pkt);
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    else if (argc==3 && !strcmp(argv[1], "isr_time") && !strcmp(argv[2], "start") ) {
        ssv_dbg_ctrl_hci->isr_summary_eable = 1;
        ssv_dbg_ctrl_hci->isr_routine_time = 0;
        ssv_dbg_ctrl_hci->isr_tx_time = 0;
        ssv_dbg_ctrl_hci->isr_rx_time = 0;
        ssv_dbg_ctrl_hci->isr_idle_time = 0;
        ssv_dbg_ctrl_hci->isr_rx_idle_time = 0;
        ssv_dbg_ctrl_hci->isr_miss_cnt = 0;
        ssv_dbg_ctrl_hci->prev_isr_jiffes = 0;
        ssv_dbg_ctrl_hci->prev_rx_isr_jiffes = 0;
        print_isr_info();
        return 0;
    }
    else if (argc==3 && !strcmp(argv[1], "isr_time") && !strcmp(argv[2], "stop") ) {
        ssv_dbg_ctrl_hci->isr_summary_eable = 0;
        print_isr_info();
        return 0;
    }
    else if (argc==3 && !strcmp(argv[1], "isr_time") && !strcmp(argv[2], "show") ) {
        print_isr_info();
        return 0;
    }
#ifdef CONFIG_IRQ_DEBUG_COUNT
 else if (argc==3 && !strcmp(argv[1], "isr_debug") && !strcmp(argv[2], "reset") ) {
  ssv_dbg_ctrl_hci->irq_enable= 0;
        ssv_dbg_ctrl_hci->irq_count = 0;
        ssv_dbg_ctrl_hci->invalid_irq_count = 0;
        ssv_dbg_ctrl_hci->tx_irq_count = 0;
        ssv_dbg_ctrl_hci->real_tx_irq_count = 0;
        ssv_dbg_ctrl_hci->rx_irq_count = 0;
        ssv_dbg_ctrl_hci->isr_rx_idle_time = 0;
        ssv_dbg_ctrl_hci->irq_rx_pkt_count = 0;
        ssv_dbg_ctrl_hci->irq_tx_pkt_count = 0;
        strcat(ssv6xxx_result_buf, "irq debug reset count\n");
        return 0;
    }
    else if (argc==3 && !strcmp(argv[1], "isr_debug") && !strcmp(argv[2], "show") ) {
  print_irq_count();
        return 0;
    }
 else if (argc==3 && !strcmp(argv[1], "isr_debug") && !strcmp(argv[2], "stop") ) {
  ssv_dbg_ctrl_hci->irq_enable= 0;
  strcat(ssv6xxx_result_buf, "irq debug stop\n");
        return 0;
    }
 else if (argc==3 && !strcmp(argv[1], "isr_debug") && !strcmp(argv[2], "start") ) {
  ssv_dbg_ctrl_hci->irq_enable= 1;
  strcat(ssv6xxx_result_buf, "irq debug start\n");
        return 0;
    }
#endif
    else
    {
        strcat(ssv6xxx_result_buf, "hci [txq|rxq] [show]\nhci [isr_time] [start|stop|show]\n\n");
        return 0;
    }
    return -1;
}
static int ssv_cmd_hwq(int argc, char *argv[])
{
#undef GET_FFO0_CNT
#undef GET_FFO1_CNT
#undef GET_FFO2_CNT
#undef GET_FFO3_CNT
#undef GET_FFO4_CNT
#undef GET_FFO5_CNT
#undef GET_FFO6_CNT
#undef GET_FFO7_CNT
#undef GET_FFO8_CNT
#undef GET_FFO9_CNT
#undef GET_FFO10_CNT
#undef GET_FFO11_CNT
#undef GET_FFO12_CNT
#undef GET_FFO13_CNT
#undef GET_FFO14_CNT
#undef GET_FFO15_CNT
#undef GET_FF0_CNT
#undef GET_FF1_CNT
#undef GET_FF3_CNT
#undef GET_FF5_CNT
#undef GET_FF6_CNT
#undef GET_FF7_CNT
#undef GET_FF8_CNT
#undef GET_FF9_CNT
#undef GET_FF10_CNT
#undef GET_FF11_CNT
#undef GET_FF12_CNT
#undef GET_FF13_CNT
#undef GET_FF14_CNT
#undef GET_FF15_CNT
#undef GET_FF4_CNT
#undef GET_FF2_CNT
#undef GET_TX_ID_ALC_LEN
#undef GET_RX_ID_ALC_LEN
#undef GET_AVA_TAG
#define GET_FFO0_CNT ((value & 0x0000001f ) >> 0)
#define GET_FFO1_CNT ((value & 0x000003e0 ) >> 5)
#define GET_FFO2_CNT ((value & 0x00000c00 ) >> 10)
#define GET_FFO3_CNT ((value & 0x000f8000 ) >> 15)
#define GET_FFO4_CNT ((value & 0x00300000 ) >> 20)
#define GET_FFO5_CNT ((value & 0x0e000000 ) >> 25)
#define GET_FFO6_CNT ((value1 & 0x0000000f ) >> 0)
#define GET_FFO7_CNT ((value1 & 0x000003e0 ) >> 5)
#define GET_FFO8_CNT ((value1 & 0x00007c00 ) >> 10)
#define GET_FFO9_CNT ((value1 & 0x000f8000 ) >> 15)
#define GET_FFO10_CNT ((value1 & 0x00f00000 ) >> 20)
#define GET_FFO11_CNT ((value1 & 0x3e000000 ) >> 25)
#define GET_FFO12_CNT ((value2 & 0x00000007 ) >> 0)
#define GET_FFO13_CNT ((value2 & 0x00000060 ) >> 5)
#define GET_FFO14_CNT ((value2 & 0x00000c00 ) >> 10)
#define GET_FFO15_CNT ((value2 & 0x001f8000 ) >> 15)
#define GET_FF0_CNT ((value & 0x0000001f ) >> 0)
#define GET_FF1_CNT ((value & 0x000001e0 ) >> 5)
#define GET_FF3_CNT ((value & 0x00003800 ) >> 11)
#define GET_FF5_CNT ((value & 0x000e0000 ) >> 17)
#define GET_FF6_CNT ((value & 0x00700000 ) >> 20)
#define GET_FF7_CNT ((value & 0x03800000 ) >> 23)
#define GET_FF8_CNT ((value & 0x1c000000 ) >> 26)
#define GET_FF9_CNT ((value & 0xe0000000 ) >> 29)
#define GET_FF10_CNT ((value1 & 0x00000007 ) >> 0)
#define GET_FF11_CNT ((value1 & 0x00000038 ) >> 3)
#define GET_FF12_CNT ((value1 & 0x000001c0 ) >> 6)
#define GET_FF13_CNT ((value1 & 0x00000600 ) >> 9)
#define GET_FF14_CNT ((value1 & 0x00001800 ) >> 11)
#define GET_FF15_CNT ((value1 & 0x00006000 ) >> 13)
#define GET_FF4_CNT ((value1 & 0x000f8000 ) >> 15)
#define GET_FF2_CNT ((value1 & 0x00700000 ) >> 20)
#define GET_TX_ID_ALC_LEN ((value & 0x0003fe00 ) >> 9)
#define GET_RX_ID_ALC_LEN ((value & 0x07fc0000 ) >> 18)
#define GET_AVA_TAG ((value1 & 0x01ff0000 ) >> 16)
    u32 addr, value, value1, value2;
    char temp_str[512];
    addr = ADR_RD_FFOUT_CNT1;
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value));
    addr = ADR_RD_FFOUT_CNT2;
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value1));
    addr = ADR_RD_FFOUT_CNT3;
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value2));
    sprintf(temp_str, "\n[TAG]  MCU - HCI - SEC -  RX - MIC - TX0 - TX1 - TX2 - TX3 - TX4 - SEC - MIC - TSH\n");
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str,"OUTPUT %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d\n",
   GET_FFO0_CNT, GET_FFO1_CNT, GET_FFO3_CNT, GET_FFO4_CNT, GET_FFO5_CNT, GET_FFO6_CNT,
   GET_FFO7_CNT, GET_FFO8_CNT, GET_FFO9_CNT, GET_FFO10_CNT, GET_FFO11_CNT, GET_FFO12_CNT, GET_FFO15_CNT);
    strcat(ssv6xxx_result_buf, temp_str);
    addr = ADR_RD_IN_FFCNT1;
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value));
    addr = ADR_RD_IN_FFCNT2;
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value1));
    sprintf(temp_str, "INPUT  %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d - %3d\n",
   GET_FF0_CNT, GET_FF1_CNT, GET_FF3_CNT, GET_FF4_CNT, GET_FF5_CNT, GET_FF6_CNT,
   GET_FF7_CNT, GET_FF8_CNT, GET_FF9_CNT, GET_FF10_CNT, GET_FF11_CNT, GET_FF12_CNT, GET_FF15_CNT);
    strcat(ssv6xxx_result_buf, temp_str);
    addr = ADR_ID_LEN_THREADSHOLD2;
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value));
    addr = ADR_TAG_STATUS;
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value1));
    sprintf(temp_str, "TX[%d]RX[%d]AVA[%d]\n",GET_TX_ID_ALC_LEN, GET_RX_ID_ALC_LEN, GET_AVA_TAG);
    strcat(ssv6xxx_result_buf, temp_str);
    return 0;
}
#ifdef CONFIG_P2P_NOA
#if 0
struct ssv6xxx_p2p_noa_param {
    u32 duration;
    u32 interval;
    u32 start_time;
    u32 enable:8;
    u32 count:8;
    u8 addr[6];
};
#endif
static struct ssv6xxx_p2p_noa_param cmd_noa_param = {
    50,
    100,
    0x12345678,
    1,
    255,
    {0x4c, 0xe6, 0x76, 0xa2, 0x4e, 0x7c}
};
void noa_dump(char* temp_str)
{
    sprintf(temp_str, "NOA Parameter:\nEnable=%d\nInterval=%d\nDuration=%d\nStart_time=0x%08x\nCount=%d\nAddr=[%02x:%02x:%02x:%02x:%02x:%02x]\n",
                                cmd_noa_param.enable,
                                cmd_noa_param.interval,
                                cmd_noa_param.duration,
                                cmd_noa_param.start_time,
                                cmd_noa_param.count,
                                cmd_noa_param.addr[0],
                                cmd_noa_param.addr[1],
                                cmd_noa_param.addr[2],
                                cmd_noa_param.addr[3],
                                cmd_noa_param.addr[4],
                                cmd_noa_param.addr[5]);
     strcat(ssv6xxx_result_buf, temp_str);
}
void ssv6xxx_send_noa_cmd(struct ssv_softc *sc, struct ssv6xxx_p2p_noa_param *p2p_noa_param)
{
    struct sk_buff *skb;
    struct cfg_host_cmd *host_cmd;
    int retry_cnt = 5;
    skb = ssvdevice_skb_alloc(HOST_CMD_HDR_LEN + sizeof(struct ssv6xxx_p2p_noa_param));
    skb->data_len = HOST_CMD_HDR_LEN + sizeof(struct ssv6xxx_p2p_noa_param);
    skb->len = skb->data_len;
    host_cmd = (struct cfg_host_cmd *)skb->data;
    host_cmd->c_type = HOST_CMD;
    host_cmd->h_cmd = (u8)SSV6XXX_HOST_CMD_SET_NOA;
    host_cmd->len = skb->data_len;
    memcpy(host_cmd->dat32, p2p_noa_param, sizeof(struct ssv6xxx_p2p_noa_param));
    while((HCI_SEND_CMD(sc->sh, skb)!=0)&&(retry_cnt)){
        printk(KERN_INFO "NOA cmd retry=%d!!\n",retry_cnt);
        retry_cnt--;
    }
    ssvdevice_skb_free(skb);
}
static int ssv_cmd_noa(int argc, char *argv[])
{
    char temp_str[512];
    char *endp;
    if (argc==2 && !strcmp(argv[1], "show") ) {
     ;
    }else if (argc==3 && !strcmp(argv[1], "duration") ){
        cmd_noa_param.duration= simple_strtoul(argv[2], &endp, 0);
    }else if (argc==3 && !strcmp(argv[1], "interval") ) {
        cmd_noa_param.interval= simple_strtoul(argv[2], &endp, 0);
    }else if (argc==3 && !strcmp(argv[1], "start") ) {
        cmd_noa_param.start_time= simple_strtoul(argv[2], &endp, 0);
    }else if (argc==3 && !strcmp(argv[1], "enable") ) {
        cmd_noa_param.enable= simple_strtoul(argv[2], &endp, 0);
    }else if (argc==3 && !strcmp(argv[1], "count") ) {
         cmd_noa_param.count= simple_strtoul(argv[2], &endp, 0);
    }else if (argc==8 && !strcmp(argv[1], "addr") ) {
         cmd_noa_param.addr[0]= simple_strtoul(argv[2], &endp, 16);
         cmd_noa_param.addr[1]= simple_strtoul(argv[3], &endp, 16);
         cmd_noa_param.addr[2]= simple_strtoul(argv[4], &endp, 16);
         cmd_noa_param.addr[3]= simple_strtoul(argv[5], &endp, 16);
         cmd_noa_param.addr[4]= simple_strtoul(argv[6], &endp, 16);
         cmd_noa_param.addr[5]= simple_strtoul(argv[7], &endp, 16);
    }else if (argc==2 && !strcmp(argv[1], "send") ) {
        ssv6xxx_send_noa_cmd(ssv_dbg_sc, &cmd_noa_param);
    }else{
        sprintf(temp_str,"## wrong command\n");
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    noa_dump(temp_str);
    return 0;
}
#endif
static int ssv_cmd_mib(int argc, char *argv[])
{
    u32 addr, value;
    char temp_str[512];
    int i;
    if (argc==2 && !strcmp(argv[1], "reset") ) {
        addr = MIB_REG_BASE;
        value = 0x0;
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, MIB_REG_BASE, value));
        value = 0xffffffff;
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, MIB_REG_BASE, value));
        value = 0x0;
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, 0xCE0023F8, value));
        value = 0x100000;
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, 0xCE0023F8, value));
        value = 0x0;
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, 0xCE0043F8, value));
        value = 0x100000;
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, 0xCE0043F8, value));
        value = 0x0;
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, 0xCE000088, value));
        value = 0x80000000;
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, 0xCE000088, value));
        sprintf(temp_str, " => MIB reseted\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }else if (argc==2 && !strcmp(argv[1], "list") ) {
        addr = MIB_REG_BASE;
        for(i=0; i<120; i++, addr+=4) {
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value));
            sprintf(temp_str, "%08x ", value);
            strcat(ssv6xxx_result_buf, temp_str);
            if (((i+1)&0x07) == 0)
                strcat(ssv6xxx_result_buf, "\n");
        }
        strcat(ssv6xxx_result_buf, "\n");
    }
    else if (argc == 2 && strcmp(argv[1], "rx")==0) {
         sprintf(temp_str, "%-10s\t\t%-10s\t\t%-10s\t\t%-10s\n","MRX_FCS_SUCC", "MRX_FCS_ERR", "MRX_ALC_FAIL", "MRX_MISS");
         strcat(ssv6xxx_result_buf, temp_str);
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_MRX_FCS_SUCC, &value)) {
            sprintf(temp_str, "[%08x]\t\t", value);
            strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_MRX_FCS_ERR, &value)) {
            sprintf(temp_str, "[%08x]\t\t", value);
            strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_MRX_ALC_FAIL, &value)) {
            sprintf(temp_str, "[%08x]\t\t", value);
            strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_MRX_MISS, &value)) {
            sprintf(temp_str, "[%08x]\n", value);
            strcat(ssv6xxx_result_buf, temp_str);
         sprintf(temp_str, "%-10s\t\t%-10s\t\t%-10s\t%-10s\n", "MRX_MB_MISS", "MRX_NIDLE_MISS", "DBG_LEN_ALC_FAIL", "DBG_LEN_CRC_FAIL");
         strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_MRX_MB_MISS, &value)) {
            sprintf(temp_str, "[%08x]\t\t", value);
            strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_MRX_NIDLE_MISS, &value)) {
            sprintf(temp_str, "[%08x]\t\t", value);
            strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_DBG_LEN_ALC_FAIL, &value)) {
            sprintf(temp_str, "[%08x]\t\t", value);
            strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_DBG_LEN_CRC_FAIL, &value)) {
            sprintf(temp_str, "[%08x]\n\n", value);
            strcat(ssv6xxx_result_buf, temp_str);
          strcat(ssv6xxx_result_buf, temp_str);
          }
          if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_DBG_AMPDU_PASS, &value)) {
             sprintf(temp_str, "[%08x]\t\t", value);
             strcat(ssv6xxx_result_buf, temp_str);
          }
          if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_DBG_AMPDU_FAIL, &value)) {
             sprintf(temp_str, "[%08x]\t\t", value);
             strcat(ssv6xxx_result_buf, temp_str);
          }
          if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_ID_ALC_FAIL1, &value)) {
             sprintf(temp_str, "[%08x]\t\t", value);
             strcat(ssv6xxx_result_buf, temp_str);
          }
          if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_ID_ALC_FAIL2, &value)) {
             sprintf(temp_str, "[%08x]\n\n", value);
             strcat(ssv6xxx_result_buf, temp_str);
         sprintf(temp_str, "PHY B mode:\n");
         strcat(ssv6xxx_result_buf, temp_str);
         sprintf(temp_str, "%-10s\t\t%-10s\t\t%-10s\n", "CRC error","CCA","counter");
         strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, 0xCE0023E8, &value)) {
            sprintf(temp_str, "[%08x]\t\t", value&0xffff);
            strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, 0xCE0023EC, &value)) {
            sprintf(temp_str, "[%08x]\t\t", (value>>16)&0xffff);
            strcat(ssv6xxx_result_buf, temp_str);
            sprintf(temp_str, "[%08x]\t\t\n\n", value&0xffff);
            strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str, "PHY G/N mode:\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str, "%-10s\t\t%-10s\t\t%-10s\n", "CRC error","CCA","counter");
        strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, 0xCE0043E8, &value)) {
            sprintf(temp_str, "[%08x]\t\t", value&0xffff);
            strcat(ssv6xxx_result_buf, temp_str);
         }
         if(SSV_REG_READ1(ssv6xxx_debug_ifops, 0xCE0043EC, &value)) {
            sprintf(temp_str, "[%08x]\t\t", (value>>16)&0xffff);
            strcat(ssv6xxx_result_buf, temp_str);
            sprintf(temp_str, "[%08x]\t\t\n\n", value&0xffff);
            strcat(ssv6xxx_result_buf, temp_str);
         }
    }
    else
    {
        sprintf(temp_str, "mib [reset|list|rx]\n\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    return 0;
}
static int ssv_cmd_sdio(int argc, char *argv[])
{
    u32 addr, value;
    char temp_str[512], *endp;
    int ret=0;
    if (argc==4 && !strcmp(argv[1], "reg") && !strcmp(argv[2], "r") ) {
        addr = simple_strtoul(argv[3], &endp, 16);
   if(!ssv6xxx_debug_ifops->ifops->cmd52_read){
      sprintf(temp_str,"The interface doesn't provide cmd52 read\n");
      strcat(ssv6xxx_result_buf, temp_str);
   return 0;
  }
  ret = ssv6xxx_debug_ifops->ifops->cmd52_read(
            ssv6xxx_debug_ifops->dev,
            addr,
            &value
        );
        if (ret >= 0) {
            sprintf(temp_str,"  ==> %x\n", value);
            strcat(ssv6xxx_result_buf, temp_str);
            return 0;
        }
    }
    else if (argc==5 && !strcmp(argv[1], "reg") && !strcmp(argv[2], "w") ) {
        addr = simple_strtoul(argv[3], &endp, 16);
        value = simple_strtoul(argv[4], &endp, 16);
        if(!ssv6xxx_debug_ifops->ifops->cmd52_write){
      sprintf(temp_str,"The interface doesn't provide cmd52 write\n");
      strcat(ssv6xxx_result_buf, temp_str);
   return 0;
  }
        ret = ssv6xxx_debug_ifops->ifops->cmd52_write(
            ssv6xxx_debug_ifops->dev,
            addr,
            value
        );
        if (ret >= 0) {
            sprintf(temp_str,"  ==> write odne.\n");
            strcat(ssv6xxx_result_buf, temp_str);
            return 0;
        }
    }
    sprintf(temp_str,"sdio cmd52 fail: %d\n", ret);
    strcat(ssv6xxx_result_buf, temp_str);
    return 0;
}
#ifdef CONFIG_SSV_CABRIO_E
static struct ssv6xxx_iqk_cfg cmd_iqk_cfg = {
    SSV6XXX_IQK_CFG_XTAL_26M,
    SSV6XXX_IQK_CFG_PA_DEF,
    0,
    0,
    26,
    3,
    0x75,
    0x75,
    0x80,
    0x80,
    SSV6XXX_IQK_CMD_INIT_CALI,
    { SSV6XXX_IQK_TEMPERATURE
    + SSV6XXX_IQK_RXDC
    + SSV6XXX_IQK_RXRC
    + SSV6XXX_IQK_TXDC
    + SSV6XXX_IQK_TXIQ
    + SSV6XXX_IQK_RXIQ
    },
};
static int ssv_cmd_iqk (int argc, char *argv[]) {
    char temp_str[512], *endp;
    struct sk_buff *skb;
    struct cfg_host_cmd *host_cmd;
    u32 rxcnt_total, rxcnt_error;
    sprintf(temp_str,"# got iqk command\n");
    strcat(ssv6xxx_result_buf, temp_str);
    if ((argc == 3) && (strcmp(argv[1], "cfg-pa") == 0)) {
        cmd_iqk_cfg.cfg_pa = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## set cfg_pa as %d\n", cmd_iqk_cfg.cfg_pa);
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    else if ((argc == 3) && (strcmp(argv[1], "cfg-tssi-trgt") == 0)) {
        cmd_iqk_cfg.cfg_tssi_trgt = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## set cfg_tssi_trgt as %d\n", cmd_iqk_cfg.cfg_tssi_trgt);
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    else if ((argc == 3) && (strcmp(argv[1], "init-cali") == 0)) {
        cmd_iqk_cfg.cmd_sel = SSV6XXX_IQK_CMD_INIT_CALI;
        cmd_iqk_cfg.fx_sel = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## do init-cali\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else if ((argc == 3) && (strcmp(argv[1], "rtbl-load") == 0)) {
        cmd_iqk_cfg.cmd_sel = SSV6XXX_IQK_CMD_RTBL_LOAD;
        cmd_iqk_cfg.fx_sel = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## do rtbl-load\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else if ((argc == 3) && (strcmp(argv[1], "rtbl-load-def") == 0)) {
        cmd_iqk_cfg.cmd_sel = SSV6XXX_IQK_CMD_RTBL_LOAD_DEF;
        cmd_iqk_cfg.fx_sel = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## do rtbl-load\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else if ((argc == 3) && (strcmp(argv[1], "rtbl-reset") == 0)) {
        cmd_iqk_cfg.cmd_sel = SSV6XXX_IQK_CMD_RTBL_RESET;
        cmd_iqk_cfg.fx_sel = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## do rtbl-reset\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else if ((argc == 3) && (strcmp(argv[1], "rtbl-set") == 0)) {
        cmd_iqk_cfg.cmd_sel = SSV6XXX_IQK_CMD_RTBL_SET;
        cmd_iqk_cfg.fx_sel = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## do rtbl-set\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else if ((argc == 3) && (strcmp(argv[1], "rtbl-export") == 0)) {
        cmd_iqk_cfg.cmd_sel = SSV6XXX_IQK_CMD_RTBL_EXPORT;
        cmd_iqk_cfg.fx_sel = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## do rtbl-export\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else if ((argc == 3) && (strcmp(argv[1], "tk-evm") == 0)) {
        cmd_iqk_cfg.cmd_sel = SSV6XXX_IQK_CMD_TK_EVM;
        cmd_iqk_cfg.argv = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## do tk-evm\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else if ((argc == 3) && (strcmp(argv[1], "tk-tone") == 0)) {
        cmd_iqk_cfg.cmd_sel = SSV6XXX_IQK_CMD_TK_TONE;
        cmd_iqk_cfg.argv = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## do tk-tone\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else if ((argc == 3) && (strcmp(argv[1], "channel") == 0)) {
        cmd_iqk_cfg.cmd_sel = SSV6XXX_IQK_CMD_TK_CHCH;
        cmd_iqk_cfg.argv = simple_strtoul(argv[2], &endp, 0);
        sprintf(temp_str,"## do change channel\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else if ((argc == 2) && (strcmp(argv[1], "tk-rxcnt-report") == 0)) {
        if(SSV_REG_READ1(ssv6xxx_debug_ifops, 0xCE0043E8, &rxcnt_error));
        if(SSV_REG_READ1(ssv6xxx_debug_ifops, 0xCE0043EC, &rxcnt_total));
        sprintf(temp_str,"## GN Rx error rate = (%06d/%06d)\n", rxcnt_error, rxcnt_total);
        strcat(ssv6xxx_result_buf, temp_str);
        if(SSV_REG_READ1(ssv6xxx_debug_ifops, 0xCE0023E8, &rxcnt_error));
        if(SSV_REG_READ1(ssv6xxx_debug_ifops, 0xCE0023EC, &rxcnt_total));
        sprintf(temp_str,"## B Rx error rate = (%06d/%06d)\n", rxcnt_error, rxcnt_total);
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    else {
        sprintf(temp_str,"## invalid iqk command\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str,"## cmd: cfg-pa/cfg-tssi-trgt\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str,"## cmd: init-cali/rtbl-load/rtbl-load-def/rtbl-reset/rtbl-set/rtbl-export/tk-evm/tk-tone/tk-channel\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str,"## fx_sel: 0x0008: RXDC\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str,"           0x0010: RXRC\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str,"           0x0020: TXDC\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str,"           0x0040: TXIQ\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str,"           0x0080: RXIQ\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str,"           0x0100: TSSI\n");
        strcat(ssv6xxx_result_buf, temp_str);
        sprintf(temp_str,"           0x0200: PAPD\n");
        strcat(ssv6xxx_result_buf, temp_str);
        return 0;
    }
    skb = ssvdevice_skb_alloc(HOST_CMD_HDR_LEN + IQK_CFG_LEN + PHY_SETTING_SIZE + RF_SETTING_SIZE);
    if(skb == NULL)
    {
        printk("ssv command ssvdevice_skb_alloc fail!!!\n");
        return 0;
    }
    if((PHY_SETTING_SIZE > MAX_PHY_SETTING_TABLE_SIZE) ||
        (RF_SETTING_SIZE > MAX_RF_SETTING_TABLE_SIZE))
    {
        printk("Please check RF or PHY table size!!!\n");
        BUG_ON(1);
        return 0;
    }
    skb->data_len = HOST_CMD_HDR_LEN + IQK_CFG_LEN + PHY_SETTING_SIZE + RF_SETTING_SIZE;
    skb->len = skb->data_len;
    host_cmd = (struct cfg_host_cmd *)skb->data;
    host_cmd->c_type = HOST_CMD;
    host_cmd->h_cmd = (u8)SSV6XXX_HOST_CMD_INIT_CALI;
    host_cmd->len = skb->data_len;
    cmd_iqk_cfg.phy_tbl_size = PHY_SETTING_SIZE;
    cmd_iqk_cfg.rf_tbl_size = RF_SETTING_SIZE;
    memcpy(host_cmd->dat32, &cmd_iqk_cfg, IQK_CFG_LEN);
    memcpy(host_cmd->dat8+IQK_CFG_LEN, phy_setting, PHY_SETTING_SIZE);
    memcpy(host_cmd->dat8+IQK_CFG_LEN+PHY_SETTING_SIZE, asic_rf_setting, RF_SETTING_SIZE);
    if(ssv_dbg_ctrl_hci->shi->hci_ops->hci_send_cmd(skb) == 0) {
        sprintf(temp_str,"## hci send cmd success\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    else {
        sprintf(temp_str,"## hci send cmd fail\n");
        strcat(ssv6xxx_result_buf, temp_str);
    }
    ssvdevice_skb_free(skb);
    return 0;
}
#endif
#define LBYTESWAP(a) ((((a) & 0x00ff00ff) << 8) | \
    (((a) & 0xff00ff00) >> 8))
#define LONGSWAP(a) ((LBYTESWAP(a) << 16) | (LBYTESWAP(a) >> 16))
static int ssv_cmd_version (int argc, char *argv[]) {
    char temp_str[256];
    u32 regval;
    u64 chip_tag=0;
    char chip_id[24]="";
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_IC_TIME_TAG_1, &regval));
    chip_tag = ((u64)regval<<32);
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_IC_TIME_TAG_0, &regval));
    chip_tag |= (regval);
    sprintf(temp_str,"CHIP TAG: %llx \n",chip_tag);
    strcat(ssv6xxx_result_buf, temp_str);
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_CHIP_ID_3, &regval));
    *((u32 *)&chip_id[0]) = (u32)LONGSWAP(regval);
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_CHIP_ID_2, &regval));
    *((u32 *)&chip_id[4]) = (u32)LONGSWAP(regval);
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_CHIP_ID_1, &regval));
    *((u32 *)&chip_id[8]) = (u32)LONGSWAP(regval);
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_CHIP_ID_0, &regval));
    *((u32 *)&chip_id[12]) = (u32)LONGSWAP(regval);
    sprintf(temp_str,"CHIP ID: %s \n",chip_id);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str,"# current Software mac version: %d\n", ssv_root_version);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str,"SVN ROOT URL %s \n", SSV_ROOT_URl);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str,"COMPILER HOST %s \n", COMPILERHOST);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str,"COMPILER DATE %s \n", COMPILERDATE);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str,"COMPILER OS %s \n", COMPILEROS);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str,"COMPILER OS ARCH %s \n", COMPILEROSARCH);
    strcat(ssv6xxx_result_buf, temp_str);
    if(SSV_REG_READ1(ssv6xxx_debug_ifops, FW_VERSION_REG, &regval));
    sprintf(temp_str,"Firmware image version: %d\n", regval);
    strcat(ssv6xxx_result_buf, temp_str);
    sprintf(temp_str,"\n[Compiler Option!!]\n");
    strcat(ssv6xxx_result_buf, temp_str);
    regval = sizeof(conf_parser)/ sizeof(*conf_parser);
    while(regval)
    {
        sprintf(temp_str,"Define %s \n", conf_parser[--regval]);
        strcat(ssv6xxx_result_buf, temp_str);
    };
    return 0;
}
static int ssv_cmd_tool(int argc, char *argv[])
{
    u32 addr, value, count;
    char tmpbf[12], *endp;
    int s;
    if (argc == 4 && strcmp(argv[1], "w")==0) {
        addr = simple_strtoul(argv[2], &endp, 16);
        value = simple_strtoul(argv[3], &endp, 16);
        if(SSV_REG_WRITE1(ssv6xxx_debug_ifops, addr, value));
        sprintf(ssv6xxx_result_buf, "ok");
        return 0;
    }
    if ((argc==4||argc==3) && strcmp(argv[1], "r")==0) {
        count = (argc==3)? 1: simple_strtoul(argv[3], &endp, 10);
        addr = simple_strtoul(argv[2], &endp, 16);
        for(s=0; s<count; s++, addr+=4) {
            if(SSV_REG_READ1(ssv6xxx_debug_ifops, addr, &value));
            sprintf(tmpbf, "%08x\n", value);
            strcat(ssv6xxx_result_buf, tmpbf);
        }
        return 0;
    }
    return -1;
}
struct _ssv6xxx_txtput{
    struct task_struct *txtput_tsk;
    struct sk_buff *skb;
    u32 size_per_frame;
    u32 loop_times;
    u32 occupied_tx_pages;
};
struct _ssv6xxx_txtput *ssv6xxx_txtput;
struct _ssv6xxx_txtput ssv_txtput = { NULL, NULL, 0, 0, 0};
static int txtput_thread_m2(void *data)
{
#define Q_DELAY_MS 20
 struct sk_buff *skb = NULL;
 struct ssv6200_tx_desc *tx_desc;
 int qlen = 0, max_qlen, q_delay_urange[2];
 max_qlen = (200 * 1000 / 8 * Q_DELAY_MS) / ssv6xxx_txtput->size_per_frame;
 q_delay_urange[0] = Q_DELAY_MS * 1000;
 q_delay_urange[1] = q_delay_urange[0] + 1000;
 printk("max_qlen: %d\n", max_qlen);
 while (!kthread_should_stop() && ssv6xxx_txtput->loop_times > 0) {
  ssv6xxx_txtput->loop_times--;
  skb = ssvdevice_skb_alloc(ssv6xxx_txtput->size_per_frame);
  if (skb == NULL) {
   printk("ssv command txtput_generate_m2 "
   "ssvdevice_skb_alloc fail!!!\n");
   goto end;
  }
  skb->data_len = ssv6xxx_txtput->size_per_frame;
  skb->len = ssv6xxx_txtput->size_per_frame;
  tx_desc = (struct ssv6200_tx_desc *)skb->data;
  memset((void *)tx_desc, 0xff, SSV6XXX_TX_DESC_LEN);
  tx_desc->len = skb->len;
  tx_desc->c_type = M2_TXREQ;
  tx_desc->fCmd = (M_ENG_CPU << 4) | M_ENG_HWHCI;
  tx_desc->reason = ID_TRAP_SW_TXTPUT;
  qlen = ssv_dbg_ctrl_hci->shi->hci_ops->hci_tx(skb, 0, 0);
  if (qlen >= max_qlen) {
   usleep_range(q_delay_urange[0], q_delay_urange[1]);
  }
 }
end:
 ssv6xxx_txtput->txtput_tsk = NULL;
 return 0;
}
static int txtput_thread(void *data)
{
    struct sk_buff *skb = ssv6xxx_txtput->skb;
    struct ssv6xxx_hci_txq_info2 txq_info2;
    u32 ret = 0, free_tx_page;
    int send_cnt;
    unsigned long start_time, end_time, throughput, time_elapse;
    throughput = ssv6xxx_txtput->loop_times * ssv6xxx_txtput->size_per_frame * 8;
    start_time = jiffies;
    while (!kthread_should_stop() && ssv6xxx_txtput->loop_times > 0) {
        ret = SSV_REG_READ1(ssv6xxx_debug_ifops, ADR_TX_ID_ALL_INFO2, (u32 *)&txq_info2);
        if (ret < 0) {
            printk("%s, read ADR_TX_ID_ALL_INFO2 failed\n", __func__);
            goto end;
        }
        free_tx_page = SSV6200_PAGE_TX_THRESHOLD - txq_info2.tx_use_page;
        send_cnt = free_tx_page / ssv6xxx_txtput->occupied_tx_pages;
        while (send_cnt > 0 && ssv6xxx_txtput->loop_times > 0) {
            send_cnt--;
            ssv6xxx_txtput->loop_times--;
            ssv_dbg_ctrl_hci->shi->hci_ops->hci_send_cmd(skb);
        }
    }
    end_time = jiffies;
    ssvdevice_skb_free(skb);
    time_elapse = ((end_time - start_time) * 1000) / HZ;
    if (time_elapse > 0) {
        throughput = throughput / time_elapse;
        printk("duration %ldms, avg. throughput %d Kbps\n", time_elapse, (int)throughput);
    }
end:
    ssv6xxx_txtput->txtput_tsk = NULL;
    return 0;
}
int txtput_generate_m2(u32 size_per_frame, u32 loop_times)
{
 ssv6xxx_txtput->size_per_frame = size_per_frame;
 ssv6xxx_txtput->loop_times = loop_times;
 ssv6xxx_txtput->txtput_tsk = kthread_run(txtput_thread_m2, NULL, "txtput_thread_m2");
 return 0;
}
int txtput_generate_host_cmd(u32 size_per_frame, u32 loop_times)
{
#define PAGESIZE 256
 struct cfg_host_cmd *host_cmd;
 struct sk_buff *skb;
 skb = ssvdevice_skb_alloc(size_per_frame);
 if(skb == NULL) {
  printk("ssv command txtput_generate_host_cmd ssvdevice_skb_alloc fail!!!\n");
  return 0;
 }
 skb->data_len = size_per_frame;
 skb->len = skb->data_len;
 host_cmd = (struct cfg_host_cmd *)skb->data;
 host_cmd->c_type = TEST_CMD;
 host_cmd->h_cmd = (u8)SSV6XXX_HOST_CMD_TX_TPUT;
 host_cmd->len = skb->data_len;
 memcpy(host_cmd->dat32, skb->data, size_per_frame);
 ssv6xxx_txtput->occupied_tx_pages = (size_per_frame/PAGESIZE)+((size_per_frame%PAGESIZE)!=0);
 ssv6xxx_txtput->size_per_frame = size_per_frame;
 ssv6xxx_txtput->loop_times = loop_times;
 ssv6xxx_txtput->skb = skb;
 ssv6xxx_txtput->txtput_tsk = kthread_run(txtput_thread, NULL, "txtput_thread");
 return 0 ;
}
int txtput_tsk_cleanup(void)
{
 int ret = 0;
 if (ssv6xxx_txtput->txtput_tsk) {
  ret = kthread_stop(ssv6xxx_txtput->txtput_tsk);
  ssv6xxx_txtput->txtput_tsk = NULL;
 }
 return ret;
}
int watchdog_controller(struct ssv_hw *sh ,u8 flag)
{
    struct sk_buff *skb;
    struct cfg_host_cmd *host_cmd;
    int ret = 0;
    printk("watchdog_controller %d\n",flag);
    skb = ssvdevice_skb_alloc(HOST_CMD_HDR_LEN);
    if(skb == NULL)
    {
        printk("init watchdog_controller fail!!!\n");
        return (-1);
    }
    skb->data_len = HOST_CMD_HDR_LEN;
    skb->len = skb->data_len;
    host_cmd = (struct cfg_host_cmd *)skb->data;
    host_cmd->c_type = HOST_CMD;
    host_cmd->h_cmd = (u8)flag;
    host_cmd->len = skb->data_len;
    sh->hci.hci_ops->hci_send_cmd(skb);
    ssvdevice_skb_free(skb);
    return ret;
}
static int ssv_cmd_txtput(int argc, char *argv[])
{
 char tmpbf[64], *endp;
 u32 size_per_frame, loop_times, pkt_type;
 ssv6xxx_txtput = &ssv_txtput;
 if (argc == 2 && !strcmp(argv[1], "stop")) {
  txtput_tsk_cleanup();
  return 0;
 }
 if (argc != 4) {
  sprintf(tmpbf, "* txtput stop\n");
  strcat(ssv6xxx_result_buf, tmpbf);
  sprintf(tmpbf, "* txtput [type] [size] [frames]\n");
  strcat(ssv6xxx_result_buf, tmpbf);
  sprintf(tmpbf, "    type(packet type):\n");
  strcat(ssv6xxx_result_buf, tmpbf);
  sprintf(tmpbf, "         0 = host_cmd\n");
  strcat(ssv6xxx_result_buf, tmpbf);
  sprintf(tmpbf, "         1 = m2_type \n");
  strcat(ssv6xxx_result_buf, tmpbf);
  sprintf(tmpbf, " EX: txtput 1 14000 9999 \n");
  strcat(ssv6xxx_result_buf, tmpbf);
  return 0;
 }
 pkt_type = simple_strtoul(argv[1], &endp, 10);
 size_per_frame = simple_strtoul(argv[2], &endp, 10);
 loop_times = simple_strtoul(argv[3], &endp, 10);
 sprintf(tmpbf, "type&size&frames:%d&%d&%d\n", pkt_type, size_per_frame, loop_times);
 strncat(ssv6xxx_result_buf, tmpbf, sizeof(tmpbf));
 if (ssv6xxx_txtput->txtput_tsk) {
  sprintf(tmpbf, "txtput already in progress\n");
  strcat(ssv6xxx_result_buf, tmpbf);
  return 0;
 }
    watchdog_controller(((struct ssv_softc *)ssv_dbg_sc)->sh ,(u8)SSV6XXX_HOST_CMD_WATCHDOG_STOP);
    ((struct ssv_softc *)ssv_dbg_sc)->watchdog_flag = WD_SLEEP;
 if (pkt_type)
  txtput_generate_m2(size_per_frame + SSV6XXX_TX_DESC_LEN, loop_times);
 else
  txtput_generate_host_cmd(size_per_frame + HOST_CMD_HDR_LEN, loop_times);
 return 0;
}
static int ssv_cmd_rxtput(int argc, char *argv[])
{
    struct sk_buff *skb;
    struct cfg_host_cmd *host_cmd;
 struct sdio_rxtput_cfg cmd_rxtput_cfg;
    char tmpbf[32], *endp;
    if (argc != 3) {
        sprintf(ssv6xxx_result_buf, "rxtput [size] [frames]\n");
  return 0;
    }
    skb = ssvdevice_skb_alloc(HOST_CMD_HDR_LEN + sizeof(struct sdio_rxtput_cfg));
    if(skb == NULL)
    {
        printk("ssv command ssvdevice_skb_alloc fail!!!\n");
        return 0;
    }
    watchdog_controller(((struct ssv_softc *)ssv_dbg_sc)->sh ,(u8)SSV6XXX_HOST_CMD_WATCHDOG_STOP);
    ((struct ssv_softc *)ssv_dbg_sc)->watchdog_flag = WD_SLEEP;
 cmd_rxtput_cfg.size_per_frame = simple_strtoul(argv[1], &endp, 10);
 cmd_rxtput_cfg.total_frames = simple_strtoul(argv[2], &endp, 10);
    sprintf(tmpbf, "size&frames:%d&%d\n", cmd_rxtput_cfg.size_per_frame, cmd_rxtput_cfg.total_frames);
    strcat(ssv6xxx_result_buf, tmpbf);
    skb->data_len = HOST_CMD_HDR_LEN + sizeof(struct sdio_rxtput_cfg);
    skb->len = skb->data_len;
    host_cmd = (struct cfg_host_cmd *)skb->data;
    host_cmd->c_type = HOST_CMD;
    host_cmd->h_cmd = (u8)SSV6XXX_HOST_CMD_RX_TPUT;
    host_cmd->len = skb->data_len;
    memcpy(host_cmd->dat32, &cmd_rxtput_cfg, sizeof(struct sdio_rxtput_cfg));
    if(ssv_dbg_ctrl_hci->shi->hci_ops->hci_send_cmd(skb) == 0) {
        strcat(ssv6xxx_result_buf, "## hci cmd was sent successfully\n");
    }
    else {
        strcat(ssv6xxx_result_buf, "## hci cmd was sent failed\n");
    }
    ssvdevice_skb_free(skb);
 return 0;
}
static int ssv_cmd_check(int argc, char *argv[])
{
    u32 size,i,j,x,y,id,value,address,id_value;
    char *endp;
    u32 id_base_address[4];
    id_base_address[0] = 0xcd010008;
    id_base_address[1] = 0xcd01000c;
    id_base_address[2] = 0xcd010054;
    id_base_address[3] = 0xcd010058;
    if (argc != 2) {
        sprintf(ssv6xxx_result_buf, "check [packet size]\n");
        return 0;
    }
    size = simple_strtoul(argv[1], &endp, 10);
    size = size >> 2;
    for(x=0;x<4;x++)
    {
        if(SSV_REG_READ1(ssv6xxx_debug_ifops, id_base_address[x], &id_value));
        for(y=0;y<32 && id_value;y++,id_value>>=1)
        {
            if(id_value&0x1)
            {
                id = 32*x + y;
                address = 0x80000000 + (id<<16);
                {
                    printk("        ");
                    for (i= 0;i<size;i+=8)
                    {
                        if(SSV_REG_READ1(ssv6xxx_debug_ifops, address, &value));
                        printk("\n%08X:%08X", address,value);
                        address += 4;
                        for (j = 1; j < 8; j++)
                        {
                            if(SSV_REG_READ1(ssv6xxx_debug_ifops, address, &value));
                            printk(" %08X", value);
                            address += 4;
                        }
                    }
                    printk("\n");
                }
            }
        }
    }
    return 0;
}
struct ssv_cmd_table cmd_table[] = {
    { "help", ssv_cmd_help, "ssv6200 command usage." },
    { "-h", ssv_cmd_help, "ssv6200 command usage." },
    { "--help", ssv_cmd_help, "ssv6200 command usage." },
    { "reg", ssv_cmd_reg, "ssv6200 register read/write." },
    { "cfg", ssv_cmd_cfg, "ssv6200 configuration." },
    { "sta", ssv_cmd_sta, "svv6200 station info." },
    { "dump", ssv_cmd_dump, "dump ssv6200 tables." },
    { "hwq", ssv_cmd_hwq, "hardware queue staus" },
#ifdef CONFIG_P2P_NOA
 { "noa", ssv_cmd_noa, "config noa param" },
#endif
 { "irq", ssv_cmd_irq, "get sdio irq status." },
    { "mac", ssv_cmd_mac, "ieee80211 swmac." },
    { "hci", ssv_cmd_hci, "HCI command." },
    { "sdio", ssv_cmd_sdio, "SDIO command." },
#ifdef CONFIG_SSV_CABRIO_E
    { "iqk", ssv_cmd_iqk, "iqk command" },
#endif
    { "version",ssv_cmd_version,"version information" },
    { "mib", ssv_cmd_mib, "mib counter related" },
    { "tool", ssv_cmd_tool, "ssv6200 tool register read/write." },
    { "rxtput", ssv_cmd_rxtput, "test rx sdio throughput" },
    { "txtput", ssv_cmd_txtput, "test tx sdio throughput" },
    { "check", ssv_cmd_check, "dump all allocate packet buffer" },
    { NULL, NULL, NULL },
};
int ssv_cmd_submit(char *cmd)
{
    struct ssv_cmd_table *sc_tbl;
    char *pch, ch;
    int ret;
    ssv6xxx_debug_ifops = (void *)ssv6xxx_ifdebug_info;
    strcpy(sg_cmd_buffer, cmd);
    for( sg_argc=0,ch=0, pch=sg_cmd_buffer;
        (*pch!=0x00)&&(sg_argc<CLI_ARG_SIZE); pch++ )
    {
        if ( (ch==0) && (*pch!=' ') )
        {
            ch = 1;
            sg_argv[sg_argc] = pch;
        }
        if ( (ch==1) && (*pch==' ') )
        {
            *pch = 0x00;
            ch = 0;
            sg_argc ++;
        }
    }
    if ( ch == 1)
    {
        sg_argc ++;
    }
    else if ( sg_argc > 0 )
    {
        *(pch-1) = ' ';
    }
    if ( sg_argc > 0 )
    {
        for( sc_tbl=cmd_table; sc_tbl->cmd; sc_tbl ++ )
        {
            if ( !strcmp(sg_argv[0], sc_tbl->cmd) )
            {
    if( (sc_tbl->cmd_func_ptr != ssv_cmd_cfg) &&
     (!ssv6xxx_debug_ifops->dev||
     !ssv6xxx_debug_ifops->ifops||
     !ssv6xxx_debug_ifops->pdev))
    {
     strcpy(ssv6xxx_result_buf, "Member of ssv6xxx_ifdebug_info is NULL !\n");
     return -1;
    }
                ssv6xxx_result_buf[0] = 0x00;
                ret = sc_tbl->cmd_func_ptr(sg_argc, sg_argv);
                if (ret < 0) {
                    strcpy(ssv6xxx_result_buf, "Invalid command !\n");
                }
                return 0;
            }
        }
     strcpy(ssv6xxx_result_buf, "Command not found !\n");
    }
    else
    {
        strcpy(ssv6xxx_result_buf, "./cli -h\n");
    }
    return 0;
}
