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

#include <ssv6200.h>
#include <linux/types.h>
#include <linux/nl80211.h>
#include <net/mac80211.h>
#include <linux/nl80211.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/kthread.h>
#include <net/mac80211.h>
#include <ssv6200.h>
#include "p2p.h"
#include "dev.h"
#include "lib.h"
#ifdef CONFIG_P2P_NOA
#define P2P_IE_VENDOR_TYPE 0x506f9a09
#define P2P_NOA_DETECT_INTERVAL (5 * HZ)
#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define COMPACT_MACSTR "%02x%02x%02x%02x%02x%02x"
#endif
void ssv6xxx_send_noa_cmd(struct ssv_softc *sc, struct ssv6xxx_p2p_noa_param *p2p_noa_param);
static inline u32 WPA_GET_BE32(const u8 *a)
{
 return (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
}
static inline u16 WPA_GET_LE16(const u8 *a)
{
 return (a[1] << 8) | a[0];
}
static inline u32 WPA_GET_LE32(const u8 *a)
{
 return (a[3] << 24) |(a[2] << 16) |(a[1] << 8) | a[0];
}
#define IEEE80211_HDRLEN 24
enum p2p_attr_id {
 P2P_ATTR_STATUS = 0,
 P2P_ATTR_MINOR_REASON_CODE = 1,
 P2P_ATTR_CAPABILITY = 2,
 P2P_ATTR_DEVICE_ID = 3,
 P2P_ATTR_GROUP_OWNER_INTENT = 4,
 P2P_ATTR_CONFIGURATION_TIMEOUT = 5,
 P2P_ATTR_LISTEN_CHANNEL = 6,
 P2P_ATTR_GROUP_BSSID = 7,
 P2P_ATTR_EXT_LISTEN_TIMING = 8,
 P2P_ATTR_INTENDED_INTERFACE_ADDR = 9,
 P2P_ATTR_MANAGEABILITY = 10,
 P2P_ATTR_CHANNEL_LIST = 11,
 P2P_ATTR_NOTICE_OF_ABSENCE = 12,
 P2P_ATTR_DEVICE_INFO = 13,
 P2P_ATTR_GROUP_INFO = 14,
 P2P_ATTR_GROUP_ID = 15,
 P2P_ATTR_INTERFACE = 16,
 P2P_ATTR_OPERATING_CHANNEL = 17,
 P2P_ATTR_INVITATION_FLAGS = 18,
 P2P_ATTR_OOB_GO_NEG_CHANNEL = 19,
 P2P_ATTR_VENDOR_SPECIFIC = 221
};
struct ssv6xxx_p2p_noa_attribute {
    u8 index;
    u16 ctwindows_oppps;
    struct ssv6xxx_p2p_noa_param noa_param;
};
extern void _ssv6xxx_hexdump(const char *title, const u8 *buf,
                             size_t len);
bool p2p_find_noa(const u8 *ies, struct ssv6xxx_p2p_noa_attribute *noa_attr)
{
    const u8 *end, *pos, *ie;
    u32 len;
 len = ie[1] - 4;
 pos = ie + 6;
 end = pos+len;
 while (pos < end) {
  u16 attr_len;
  if (pos + 2 >= end) {
   return false;
  }
  attr_len = WPA_GET_LE16(pos + 1);
  if (pos + 3 + attr_len > end) {
   return false;
  }
        if(pos[0] != P2P_ATTR_NOTICE_OF_ABSENCE){
            pos += 3 + attr_len;
            continue;
        }
        if(attr_len<15){
            printk("*********************NOA descriptor does not exist len[%d]\n", attr_len);
            break;
        }
        if(attr_len>15)
            printk("More than one NOA descriptor\n");
        noa_attr->index = pos[3];
        noa_attr->ctwindows_oppps = pos[4];
        noa_attr->noa_param.count = pos[5];
        noa_attr->noa_param.duration = WPA_GET_LE32(&pos[6]);
        noa_attr->noa_param.interval = WPA_GET_LE32(&pos[10]);
        noa_attr->noa_param.start_time = WPA_GET_LE32(&pos[14]);
        return true;
 }
 return false;
}
bool p2p_get_attribute_noa(const u8 *ies, u32 oui_type, struct ssv6xxx_p2p_noa_attribute *noa_attr)
{
 const u8 *end, *pos, *ie;
    u32 len;
 pos = ies;
 end = ies + ies_len;
 ie = NULL;
 while (pos + 1 < end) {
  if (pos + 2 + pos[1] > end)
   return false;
  if (pos[0] == WLAN_EID_VENDOR_SPECIFIC && pos[1] >= 4 &&
      WPA_GET_BE32(&pos[2]) == oui_type) {
   ie = pos;
            if(p2p_find_noa(ie, 0, noa_attr) == true)
                return true;
  }
  pos += 2 + pos[1];
 }
 return false;
}
void ssv6xxx_process_noa_event(struct ssv_softc *sc, struct sk_buff *skb)
{
    struct cfg_host_event *host_event;
    struct ssv62xx_noa_evt *noa_evt;
    host_event = (struct cfg_host_event *)skb->data;
    noa_evt= (struct ssv62xx_noa_evt *)&host_event->dat[0];
    switch(noa_evt->evt_id){
        case SSV6XXX_NOA_START:
            sc->p2p_noa.active_noa_vif |= (1<<noa_evt->vif);
            printk("SSV6XXX_NOA_START===>[%08x]\n", sc->p2p_noa.active_noa_vif);
            break;
        case SSV6XXX_NOA_STOP:
            sc->p2p_noa.active_noa_vif &= ~(1<<noa_evt->vif);
            printk("SSV6XXX_NOA_STOP===>[%08x]\n", sc->p2p_noa.active_noa_vif);
            break;
        default:
            printk("--------->NOA wrong command<---------\n");
            break;
    }
}
void ssv6xxx_noa_reset(struct ssv_softc *sc){
    unsigned long flags;
    printk("Reset NOA param...\n");
    spin_lock_irqsave(&sc->p2p_noa.p2p_config_lock, flags);
    memset(&sc->p2p_noa.noa_detect, 0, sizeof(struct ssv_p2p_noa_detect)*SSV_NUM_VIF);
    sc->p2p_noa.active_noa_vif = 0;
    sc->p2p_noa.monitor_noa_vif = 0;
    spin_unlock_irqrestore(&sc->p2p_noa.p2p_config_lock, flags);
}
void ssv6xxx_noa_host_stop_noa(struct ssv_softc *sc, u8 vif_id){
    struct ssv6xxx_p2p_noa_attribute noa_attr;
    if(sc->p2p_noa.noa_detect[vif_id].p2p_noa_index>=0){
        sc->p2p_noa.noa_detect[vif_id].p2p_noa_index = -1;
        sc->p2p_noa.active_noa_vif &= ~(1<<vif_id);
        memset(&sc->p2p_noa.noa_detect[vif_id].noa_param_cmd, 0, sizeof(struct ssv6xxx_p2p_noa_param));
        printk("->remove NOA operating vif[%d]\n", vif_id);
        noa_attr.noa_param.enable = 0;
        noa_attr.noa_param.vif_id = vif_id;
        ssv6xxx_send_noa_cmd(sc, &noa_attr.noa_param);
    }
}
void ssv6xxx_noa_detect(struct ssv_softc *sc, struct ieee80211_hdr * hdr, u32 len){
    int i;
    unsigned long flags;
    struct ieee80211_mgmt * mgmt =(struct ieee80211_mgmt *)hdr;
    struct ssv6xxx_p2p_noa_attribute noa_attr;
    spin_lock_irqsave(&sc->p2p_noa.p2p_config_lock, flags);
    if(sc->p2p_noa.monitor_noa_vif == 0)
        goto out;
    for(i=0;i<SSV_NUM_VIF;i++){
        if(sc->p2p_noa.noa_detect[i].noa_addr == NULL)
            continue;
        if(memcmp(mgmt->bssid, sc->p2p_noa.noa_detect[i].noa_addr, 6) != 0)
            continue;
        if(sc->p2p_noa.active_noa_vif &&
            ((sc->p2p_noa.active_noa_vif & 1<<i) == 0))
            continue;
  sc->p2p_noa.noa_detect[i].last_rx = jiffies;
        if(p2p_get_attribute_noa((const u8*)mgmt->u.beacon.variable,
                len - (IEEE80211_HDRLEN + sizeof(mgmt->u.beacon)),
                P2P_IE_VENDOR_TYPE,
                &noa_attr)== false){
            continue;
        }
        if(sc->p2p_noa.noa_detect[i].p2p_noa_index == noa_attr.index){
            goto out;
        }
        printk(MACSTR"->set NOA element\n", MAC2STR(mgmt->bssid));
        sc->p2p_noa.active_noa_vif |= (1<<i);
        sc->p2p_noa.noa_detect[i].p2p_noa_index = noa_attr.index;
        memcpy(&sc->p2p_noa.noa_detect[i].noa_param_cmd, &noa_attr.noa_param, sizeof(struct ssv6xxx_p2p_noa_param));
        noa_attr.noa_param.enable = 1;
        noa_attr.noa_param.vif_id = i;
        memcpy(noa_attr.noa_param.addr, hdr->addr2, 6);
        ssv6xxx_send_noa_cmd(sc, &noa_attr.noa_param);
    }
out:
    spin_unlock_irqrestore(&sc->p2p_noa.p2p_config_lock, flags);
}
void ssv6xxx_noa_hdl_bss_change(struct ssv_softc *sc, enum ssv6xxx_noa_conf conf, u8 vif_idx){
    unsigned long flags;
    if(sc->vif_info[vif_idx].vif->type != NL80211_IFTYPE_STATION ||
        sc->vif_info[vif_idx].vif->p2p != true)
        return;
    spin_lock_irqsave(&sc->p2p_noa.p2p_config_lock, flags);
    printk("====>[NOA]ssv6xxx_noa_hdl_bss_change conf[%d] vif_idx[%d]\n", conf, vif_idx);
    switch(conf)
    {
        case MONITOR_NOA_CONF_ADD:
            memset(&sc->p2p_noa.noa_detect[vif_idx], 0, sizeof(struct ssv_p2p_noa_detect));
            sc->p2p_noa.noa_detect[vif_idx].noa_addr = sc->vif_info[vif_idx].vif->bss_conf.bssid;
            sc->p2p_noa.noa_detect[vif_idx].p2p_noa_index = -1;
            sc->p2p_noa.noa_detect[vif_idx].last_rx = jiffies;
            sc->p2p_noa.monitor_noa_vif |= 1<< vif_idx;
            break;
        case MONITOR_NOA_CONF_REMOVE:
            sc->p2p_noa.monitor_noa_vif &= ~(1<< vif_idx);
            sc->p2p_noa.noa_detect[vif_idx].noa_addr = NULL;
            ssv6xxx_noa_host_stop_noa(sc, vif_idx);
            break;
        default:
            break;
    }
    spin_unlock_irqrestore(&sc->p2p_noa.p2p_config_lock, flags);
}
void ssv6xxx_send_noa_cmd(struct ssv_softc *sc, struct ssv6xxx_p2p_noa_param *p2p_noa_param)
{
    struct sk_buff *skb;
    struct cfg_host_cmd *host_cmd;
    int retry_cnt = 5;
    skb = ssv_skb_alloc(HOST_CMD_HDR_LEN + sizeof(struct ssv6xxx_p2p_noa_param));
    skb->data_len = HOST_CMD_HDR_LEN + sizeof(struct ssv6xxx_p2p_noa_param);
    skb->len = skb->data_len;
    host_cmd = (struct cfg_host_cmd *)skb->data;
    host_cmd->c_type = HOST_CMD;
    host_cmd->h_cmd = (u8)SSV6XXX_HOST_CMD_SET_NOA;
    host_cmd->len = skb->data_len;
    memcpy(host_cmd->dat32, p2p_noa_param, sizeof(struct ssv6xxx_p2p_noa_param));
    printk("Noa cmd NOA Parameter:\nEnable=%d\nInterval=%d\nDuration=%d\nStart_time=0x%08x\nCount=%d\nAddr=[%02x:%02x:%02x:%02x:%02x:%02x]vif[%d]\n\n",
                        p2p_noa_param->enable,
                        p2p_noa_param->interval,
                        p2p_noa_param->duration,
                        p2p_noa_param->start_time,
                        p2p_noa_param->count,
                        p2p_noa_param->addr[0],
                        p2p_noa_param->addr[1],
                        p2p_noa_param->addr[2],
                        p2p_noa_param->addr[3],
                        p2p_noa_param->addr[4],
                        p2p_noa_param->addr[5],
                        p2p_noa_param->vif_id);
    while((HCI_SEND_CMD(sc->sh, skb)!=0)&&(retry_cnt)){
        printk(KERN_INFO "NOA cmd retry=%d!!\n",retry_cnt);
        retry_cnt--;
    }
    ssv_skb_free(skb);
}
#if 0
void ssv6200_cmd_work(struct work_struct *work)
{
    struct ssv_softc *sc =
            container_of(work, struct ssv_softc, cmd_work);
    struct sk_buff *skb;
    bool cmd_mode;
    int retry_cnt = 5;
    do{
        if(sc->cmd_info.state == SSC_CMD_STATE_IDLE){
            struct cfg_host_cmd *host_cmd;
            skb = skb_peek(&sc->cmd_info.cmd_que);
            if(skb == NULL)
                break;
            host_cmd = (struct cfg_host_cmd *)skb->data;
            while((HCI_SEND_CMD(sc->sh, skb)!=0)&&(retry_cnt)){
                    printk(KERN_INFO "cmd retry=%d!!\n",retry_cnt);
                    retry_cnt--;
            }
            if(retry_cnt)
                sc->cmd_info.state = SSC_CMD_STATE_WAIT_RSP;
        }else{
            skb = skb_dequeue(&sc->cmd_info.evt_que);
            if(skb == NULL)
                break;
        }
    }while(1);
}
#endif
#endif
