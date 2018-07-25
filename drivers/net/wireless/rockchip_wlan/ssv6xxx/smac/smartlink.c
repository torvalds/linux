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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <net/sock.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/version.h>
#include <ssv6200.h>
#include "lib.h"
#include "dev.h"
#define NETLINK_SMARTLINK (31)
#define MAX_PAYLOAD (2048)
static struct sock *nl_sk = NULL;
struct ssv_softc *ssv_smartlink_sc = NULL;
EXPORT_SYMBOL(ssv_smartlink_sc);
u32 ssv_smartlink_status=0;
static int _ksmartlink_start_smartlink(u8 *pInBuf, u32 inBufLen, u8 *pOutBuf, u32 *pOutBufLen)
{
#ifdef KSMARTLINK_DEBUG
    printk(KERN_INFO "%s\n", __FUNCTION__);
#endif
    ssv_smartlink_status = 1;
    *pOutBufLen = 0;
    return 0;
}
int ksmartlink_smartlink_started(void)
{
    return ssv_smartlink_status;
}
EXPORT_SYMBOL(ksmartlink_smartlink_started);
static int _ksmartlink_stop_smartlink(u8 *pInBuf, u32 inBufLen, u8 *pOutBuf, u32 *pOutBufLen)
{
#ifdef KSMARTLINK_DEBUG
    printk(KERN_INFO "%s\n", __FUNCTION__);
#endif
    ssv_smartlink_status = 0;
    *pOutBufLen = 0;
    return 0;
}
static int _ksmartlink_set_channel(u8 *pInBuf, u32 inBufLen, u8 *pOutBuf, u32 *pOutBufLen)
{
    int ret=-10;
    int ch=(int)(*pInBuf);
    struct ssv_softc *sc=ssv_smartlink_sc;
#ifdef KSMARTLINK_DEBUG
    printk(KERN_INFO "%s %d\n", __FUNCTION__, ch);
#endif
    if (!sc)
    {
        goto out;
    }
    mutex_lock(&sc->mutex);
    ret = ssv6xxx_set_channel(sc, ch);
    mutex_unlock(&sc->mutex);
    *pOutBufLen = 0;
out:
    return ret;
}
static int _ksmartlink_get_channel(u8 *pInBuf, u32 inBufLen, u8 *pOutBuf, u32 *pOutBufLen)
{
    int ret=-10;
    int ch=0;
    struct ssv_softc *sc=ssv_smartlink_sc;
#ifdef KSMARTLINK_DEBUG
    printk(KERN_INFO "%s\n", __FUNCTION__);
#endif
    if (!sc)
    {
        goto out;
    }
    mutex_lock(&sc->mutex);
    ret = ssv6xxx_get_channel(sc, &ch);
    mutex_unlock(&sc->mutex);
    *pOutBuf = ch;
    *pOutBufLen = 1;
#ifdef KSMARTLINK_DEBUG
    printk(KERN_INFO "%s %d\n", __FUNCTION__, ch);
#endif
out:
    return ret;
}
static int _ksmartlink_set_promisc(u8 *pInBuf, u32 inBufLen, u8 *pOutBuf, u32 *pOutBufLen)
{
    int ret=-10;
    int accept=(int)(*pInBuf);
    struct ssv_softc *sc=ssv_smartlink_sc;
#ifdef KSMARTLINK_DEBUG
    printk(KERN_INFO "%s %d\n", __FUNCTION__, accept);
#endif
    if (!sc)
    {
        goto out;
    }
    mutex_lock(&sc->mutex);
    ret = ssv6xxx_set_promisc(sc, accept);
    mutex_unlock(&sc->mutex);
    *pOutBufLen = 0;
out:
    return ret;
}
static int _ksmartlink_get_promisc(u8 *pInBuf, u32 inBufLen, u8 *pOutBuf, u32 *pOutBufLen)
{
    int ret=-10;
    int accept=(int)(*pInBuf);
    struct ssv_softc *sc=ssv_smartlink_sc;
#ifdef KSMARTLINK_DEBUG
    printk(KERN_INFO "%s\n", __FUNCTION__);
#endif
    if (!sc)
    {
        goto out;
    }
    mutex_lock(&sc->mutex);
    ret = ssv6xxx_get_promisc(sc, &accept);
    mutex_unlock(&sc->mutex);
    *pOutBuf = accept;
    *pOutBufLen = 1;
#ifdef KSMARTLINK_DEBUG
    printk(KERN_INFO "%s %d\n", __FUNCTION__, accept);
#endif
out:
    return ret;
}
#define SMARTLINK_CMD_FIXED_LEN (10)
#define SMARTLINK_CMD_FIXED_TOT_LEN (SMARTLINK_CMD_FIXED_LEN+1)
#define SMARTLINK_RES_FIXED_LEN (SMARTLINK_CMD_FIXED_LEN)
#define SMARTLINK_RES_FIXED_TOT_LEN (SMARTLINK_RES_FIXED_LEN+2)
struct ksmartlink_cmd
{
    char *cmd;
    int (*process_func)(u8 *, u32, u8 *, u32 *);
};
static struct ksmartlink_cmd _ksmartlink_cmd_table[] =
{
    {"startairki", _ksmartlink_start_smartlink},
    {"stopairkis", _ksmartlink_stop_smartlink},
    {"setchannel", _ksmartlink_set_channel},
    {"getchannel", _ksmartlink_get_channel},
    {"setpromisc", _ksmartlink_set_promisc},
    {"getpromisc", _ksmartlink_get_promisc},
};
static u32 _ksmartlink_cmd_table_size=sizeof(_ksmartlink_cmd_table)/sizeof(struct ksmartlink_cmd);
#ifdef KSMARTLINK_DEBUG
static void _ksmartlink_hex_dump(u8 *pInBuf, u32 inBufLen)
{
    u32 i=0;
    printk(KERN_INFO "\nKernel Hex Dump(len=%d):\n", inBufLen);
    printk(KERN_INFO ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    for (i=0; i<inBufLen; i++)
    {
        if ((i) && ((i & 0xf) == 0))
        {
            printk("\n");
        }
        printk("%02x ", pInBuf[i]);
    }
    printk(KERN_INFO "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
}
#endif
static int _ksmartlink_process_msg(u8 *pInBuf, u32 inBufLen, u8 *pOutBuf, u32 *pOutBufLen)
{
    int ret=0;
    u32 i=0;
    struct ksmartlink_cmd *pCmd;
    if (!pInBuf || !pOutBuf || !pOutBufLen)
    {
        printk(KERN_ERR "NULL pointer\n");
        return -1;
    }
    for (i=0; i<_ksmartlink_cmd_table_size; i++)
    {
        if (!strncmp(_ksmartlink_cmd_table[i].cmd, pInBuf, SMARTLINK_CMD_FIXED_LEN))
        {
            break;
        }
    }
    if (i < _ksmartlink_cmd_table_size)
    {
        pCmd = &_ksmartlink_cmd_table[i];
        if (!pCmd->process_func)
        {
            printk(KERN_ERR "CMD %s has NULL process_func\n", pCmd->cmd);
            return -3;
        }
        ret = pCmd->process_func(pInBuf+SMARTLINK_CMD_FIXED_LEN, inBufLen, pOutBuf, pOutBufLen);
    #ifdef CONFIG_SSV_NETLINK_RESPONSE
        if (ret < 0)
        {
            *pOutBufLen = SMARTLINK_RES_FIXED_TOT_LEN;
        }
        else
        {
            if (*pOutBufLen > 0)
            {
                pOutBuf[SMARTLINK_RES_FIXED_LEN] = (u8)ret;
                pOutBuf[SMARTLINK_RES_FIXED_LEN+1]= *pOutBuf;
            }
            else
            {
                pOutBuf[SMARTLINK_RES_FIXED_LEN] = (u8)ret;
                pOutBuf[SMARTLINK_RES_FIXED_LEN+1]= 0;
            }
            *pOutBufLen = SMARTLINK_RES_FIXED_TOT_LEN;
        }
        memcpy(pOutBuf, pCmd->cmd, SMARTLINK_RES_FIXED_LEN);
    #else
        (void)pOutBuf;
        (void)pOutBufLen;
    #endif
        return 0;
    }
    else
    {
        printk(KERN_INFO "Unknow CMD or Packet?\n");
    }
    return 0;
}
static u8 gkBuf[MAX_PAYLOAD]={0};
static int ssv_usr_pid=0;
void smartlink_nl_recv_msg(struct sk_buff *skb)
{
    struct nlmsghdr *nlh;
#ifdef CONFIG_SSV_NETLINK_RESPONSE
    struct sk_buff *skb_out;
#endif
    int ret=0;
    u8 *pInBuf=NULL;
    u32 inBufLen=0;
    u32 outBufLen=0;
    nlh = (struct nlmsghdr *)skb->data;
    ssv_usr_pid = nlh->nlmsg_pid;
    pInBuf = (u8 *)nlmsg_data(nlh);
    inBufLen = nlmsg_len(nlh);
    #ifdef KSMARTLINK_DEBUG
    _ksmartlink_hex_dump(pInBuf, inBufLen);
    #endif
    outBufLen = 0;
    memset(gkBuf, 0, MAX_PAYLOAD);
    ret = _ksmartlink_process_msg(pInBuf, inBufLen, gkBuf, &outBufLen);
#ifdef CONFIG_SSV_NETLINK_RESPONSE
    if (outBufLen == 0)
    {
        memcpy(gkBuf, "Nothing", 8);
        outBufLen = strlen(gkBuf);
    }
    skb_out = nlmsg_new(outBufLen, 0);
    if (!skb_out)
    {
        printk(KERN_ERR "Failed to allocate new skb\n");
        return;
    }
    nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, outBufLen, 0);
    NETLINK_CB(skb_out).dst_group = 0;
    memcpy(nlmsg_data(nlh), gkBuf, outBufLen);
    ret = nlmsg_unicast(nl_sk, skb_out, ssv_usr_pid);
    if (ret < 0)
    {
        printk(KERN_ERR "Error while sending bak to user\n");
    }
#endif
    return;
}
void smartlink_nl_send_msg(struct sk_buff *skb)
{
    struct nlmsghdr *nlh;
    struct sk_buff *skb_out;
    int ret=0;
    u8 *pOutBuf=skb->data;
    u32 outBufLen=skb->len;
    #ifdef KSMARTLINK_DEBUG
    #endif
    skb_out = nlmsg_new(outBufLen, 0);
    if (!skb_out)
    {
        printk(KERN_ERR "Allocate new skb failed!\n");
        return;
    }
    nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, outBufLen, 0);
    NETLINK_CB(skb_out).dst_group = 0;
    memcpy(nlmsg_data(nlh), pOutBuf, outBufLen);
    ret = nlmsg_unicast(nl_sk, skb_out, ssv_usr_pid);
    if (ret < 0)
    {
        printk(KERN_ERR "nlmsg_unicast failed!\n");
    }
    kfree_skb(skb);
    return;
}
EXPORT_SYMBOL(smartlink_nl_send_msg);
int ksmartlink_init(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
    nl_sk = netlink_kernel_create(&init_net,
                                  NETLINK_SMARTLINK,
                                  0,
                                  smartlink_nl_recv_msg,
                                  NULL,
                                  THIS_MODULE);
#else
    struct netlink_kernel_cfg cfg =
            {
                .groups = 0,
                .input = smartlink_nl_recv_msg,
            };
    nl_sk = netlink_kernel_create(&init_net,
                                  NETLINK_SMARTLINK,
                                  &cfg);
#endif
    printk(KERN_INFO "***************SmartLink Init-S**************\n");
    if(!nl_sk)
    {
        printk(KERN_ERR "Error creating socket.\n");
        return -10;
    }
    printk(KERN_INFO "***************SmartLink Init-E**************\n");
    return 0;
}
void ksmartlink_exit(void)
{
    printk(KERN_INFO "%s\n", __FUNCTION__);
    if (nl_sk)
    {
        netlink_kernel_release(nl_sk);
        nl_sk = NULL;
    }
}
#if (defined(CONFIG_SSV_SUPPORT_ANDROID)||defined(CONFIG_SSV_BUILD_AS_ONE_KO))
EXPORT_SYMBOL(ksmartlink_init);
EXPORT_SYMBOL(ksmartlink_exit);
#else
module_init(ksmartlink_init);
module_exit(ksmartlink_exit);
#endif
