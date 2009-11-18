/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*  Module Name : usbdrv.c                                              */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains network interface up/down related functions.*/
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

/* src/usbdrv.c */

#define ZM_PIBSS_MODE   0
#define ZM_AP_MODE      0
#define ZM_CHANNEL      11
#define ZM_WEP_MOME     0
#define ZM_SHARE_AUTH   0
#define ZM_DISABLE_XMIT 0

#include "usbdrv.h"
#include "oal_dt.h"
#include "80211core/pub_zfi.h"

#include "linux/netlink.h"
#include "linux/rtnetlink.h"

#include <net/iw_handler.h>

#ifdef ZM_HOSTAPD_SUPPORT
#include "athr_common.h"
#endif

extern void zfDumpDescriptor(zdev_t* dev, u16_t type);
//extern void zfiWlanQueryMacAddress(zdev_t* dev, u8_t* addr);

// ISR handler
irqreturn_t usbdrv_intr(int, void *, struct pt_regs *);

// Network Device interface related function
int usbdrv_open(struct net_device *);
int usbdrv_close(struct net_device *);
int usbdrv_change_mtu(struct net_device *, int);
int usbdrv_set_mac(struct net_device *, void *);
int usbdrv_xmit_frame(struct sk_buff *, struct net_device *);
void usbdrv_set_multi(struct net_device *);
struct net_device_stats *usbdrv_get_stats(struct net_device *);

//wireless extension helper functions
int usbdrv_ioctl_setessid(struct net_device *dev, struct iw_point *erq);
int usbdrv_ioctl_getessid(struct net_device *dev, struct iw_point *erq);
int usbdrv_ioctl_setrts(struct net_device *dev, struct iw_param *rrq);
/* Wireless Extension Handler functions */
int usbdrvwext_giwmode(struct net_device *dev, struct iw_request_info* info,
        __u32 *mode, char *extra);
int zfLnxPrivateIoctl(struct usbdrv_private *macp, struct zdap_ioctl *zdreq);

void zfLnx10msTimer(struct net_device* dev);
int zfUnregisterWdsDev(struct net_device* parentDev, u16_t wdsId);
int zfRegisterWdsDev(struct net_device* parentDev, u16_t wdsId);
int zfWdsOpen(struct net_device *dev);
int zfWdsClose(struct net_device *dev);
int zfLnxVapOpen(struct net_device *dev);
int zfLnxVapClose(struct net_device *dev);
int zfLnxVapXmitFrame(struct sk_buff *skb, struct net_device *dev);
int zfLnxRegisterVapDev(struct net_device* parentDev, u16_t vapId);
int usbdrv_wpa_ioctl(struct net_device *dev, struct athr_wlan_param *zdparm);
extern u16_t zfLnxGetVapId(zdev_t* dev);
extern u16_t zfLnxCheckTxBufferCnt(zdev_t *dev);
extern UsbTxQ_t *zfLnxGetUsbTxBuffer(zdev_t *dev);

extern u16_t zfLnxAuthNotify(zdev_t* dev, u16_t* macAddr);
extern u16_t zfLnxAsocNotify(zdev_t* dev, u16_t* macAddr, u8_t* body, u16_t bodySize, u16_t port);
extern u16_t zfLnxDisAsocNotify(zdev_t* dev, u8_t* macAddr, u16_t port);
extern u16_t zfLnxApConnectNotify(zdev_t* dev, u8_t* macAddr, u16_t port);
extern void zfLnxConnectNotify(zdev_t* dev, u16_t status, u16_t* bssid);
extern void zfLnxScanNotify(zdev_t* dev, struct zsScanResult* result);
extern void zfLnxStatisticsNotify(zdev_t* dev, struct zsStastics* result);
extern void zfLnxMicFailureNotify(zdev_t* dev, u16_t* addr, u16_t status);
extern void zfLnxApMicFailureNotify(zdev_t* dev, u8_t* addr, zbuf_t* buf);
extern void zfLnxIbssPartnerNotify(zdev_t* dev, u16_t status, struct zsPartnerNotifyEvent *event);
extern void zfLnxMacAddressNotify(zdev_t* dev, u8_t* addr);
extern void zfLnxSendCompleteIndication(zdev_t* dev, zbuf_t* buf);
extern void zfLnxRecvEth(zdev_t* dev, zbuf_t* buf, u16_t port);
extern void zfLnxRestoreBufData(zdev_t* dev, zbuf_t* buf);
#ifdef ZM_ENABLE_CENC
extern u16_t zfLnxCencAsocNotify(zdev_t* dev, u16_t* macAddr, u8_t* body, u16_t bodySize, u16_t port);
#endif //ZM_ENABLE_CENC
extern void zfLnxWatchDogNotify(zdev_t* dev);
extern void zfLnxRecv80211(zdev_t* dev, zbuf_t* buf, struct zsAdditionInfo* addInfo);
extern u8_t zfLnxCreateThread(zdev_t *dev);

/******************************************************************************
*                        P U B L I C   D A T A
*******************************************************************************
*/

/* Definition of Wireless Extension */

//wireless extension helper functions
extern int usbdrv_ioctl_setessid(struct net_device *dev, struct iw_point *erq);
extern int usbdrv_ioctl_setrts(struct net_device *dev, struct iw_param *rrq);
/* Wireless Extension Handler functions */
extern int usbdrvwext_giwname(struct net_device *dev, struct iw_request_info *info,
        union iwreq_data *wrq, char *extra);
extern int usbdrvwext_siwfreq(struct net_device *dev, struct iw_request_info *info,
        struct iw_freq *freq, char *extra);
extern int usbdrvwext_giwfreq(struct net_device *dev, struct iw_request_info *info,
        struct iw_freq *freq, char *extra);
extern int usbdrvwext_siwmode(struct net_device *dev, struct iw_request_info *info,
        union iwreq_data *wrq, char *extra);
extern int usbdrvwext_giwmode(struct net_device *dev, struct iw_request_info *info,
        __u32 *mode, char *extra);
extern int usbdrvwext_siwsens(struct net_device *dev, struct iw_request_info *info,
		struct iw_param *sens, char *extra);
extern int usbdrvwext_giwsens(struct net_device *dev, struct iw_request_info *info,
		struct iw_param *sens, char *extra);
extern int usbdrvwext_giwrange(struct net_device *dev, struct iw_request_info *info,
        struct iw_point *data, char *extra);
extern int usbdrvwext_siwap(struct net_device *dev, struct iw_request_info *info,
        struct sockaddr *MacAddr, char *extra);
extern int usbdrvwext_giwap(struct net_device *dev, struct iw_request_info *info,
        struct sockaddr *MacAddr, char *extra);
extern int usbdrvwext_iwaplist(struct net_device *dev, struct iw_request_info *info,
		struct iw_point *data, char *extra);
extern int usbdrvwext_siwscan(struct net_device *dev, struct iw_request_info *info,
        struct iw_point *data, char *extra);
extern int usbdrvwext_giwscan(struct net_device *dev, struct iw_request_info *info,
        struct iw_point *data, char *extra);
extern int usbdrvwext_siwessid(struct net_device *dev, struct iw_request_info *info,
        struct iw_point *essid, char *extra);
extern int usbdrvwext_giwessid(struct net_device *dev, struct iw_request_info *info,
        struct iw_point *essid, char *extra);
extern int usbdrvwext_siwnickn(struct net_device *dev, struct iw_request_info *info,
	    struct iw_point *data, char *nickname);
extern int usbdrvwext_giwnickn(struct net_device *dev, struct iw_request_info *info,
	    struct iw_point *data, char *nickname);
extern int usbdrvwext_siwrate(struct net_device *dev, struct iw_request_info *info,
        struct iw_param *frq, char *extra);
extern int usbdrvwext_giwrate(struct net_device *dev, struct iw_request_info *info,
        struct iw_param *frq, char *extra);
extern int usbdrvwext_siwrts(struct net_device *dev, struct iw_request_info *info,
        struct iw_param *rts, char *extra);
extern int usbdrvwext_giwrts(struct net_device *dev, struct iw_request_info *info,
        struct iw_param *rts, char *extra);
extern int usbdrvwext_siwfrag(struct net_device *dev, struct iw_request_info *info,
        struct iw_param *frag, char *extra);
extern int usbdrvwext_giwfrag(struct net_device *dev, struct iw_request_info *info,
        struct iw_param *frag, char *extra);
extern int usbdrvwext_siwtxpow(struct net_device *dev, struct iw_request_info *info,
		struct iw_param *rrq, char *extra);
extern int usbdrvwext_giwtxpow(struct net_device *dev, struct iw_request_info *info,
		struct iw_param *rrq, char *extra);
extern int usbdrvwext_siwretry(struct net_device *dev, struct iw_request_info *info,
	    struct iw_param *rrq, char *extra);
extern int usbdrvwext_giwretry(struct net_device *dev, struct iw_request_info *info,
	    struct iw_param *rrq, char *extra);
extern int usbdrvwext_siwencode(struct net_device *dev, struct iw_request_info *info,
        struct iw_point *erq, char *key);
extern int usbdrvwext_giwencode(struct net_device *dev, struct iw_request_info *info,
        struct iw_point *erq, char *key);
extern int usbdrvwext_siwpower(struct net_device *dev, struct iw_request_info *info,
        struct iw_param *frq, char *extra);
extern int usbdrvwext_giwpower(struct net_device *dev, struct iw_request_info *info,
        struct iw_param *frq, char *extra);
extern int usbdrv_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
/*
 * Structures to export the Wireless Handlers
 */

struct iw_priv_args usbdrv_private_args[] = {
//    { SIOCIWFIRSTPRIV + 0x0, 0, 0, "list_bss" },
//    { SIOCIWFIRSTPRIV + 0x1, 0, 0, "card_reset" },
    { SIOCIWFIRSTPRIV + 0x2, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "set_auth" },  /* 0 - open, 1 - shared key */
    { SIOCIWFIRSTPRIV + 0x3, 0, IW_PRIV_TYPE_CHAR | 12, "get_auth" },
//    { SIOCIWFIRSTPRIV + 0x4, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "set_preamble" },  /* 0 - long, 1 - short */
//    { SIOCIWFIRSTPRIV + 0x5, 0, IW_PRIV_TYPE_CHAR | 6, "get_preamble" },
//    { SIOCIWFIRSTPRIV + 0x6, 0, 0, "cnt" },
//    { SIOCIWFIRSTPRIV + 0x7, 0, 0, "regs" },
//    { SIOCIWFIRSTPRIV + 0x8, 0, 0, "probe" },
//    { SIOCIWFIRSTPRIV + 0x9, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "dbg_flag" },
//    { SIOCIWFIRSTPRIV + 0xA, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "connect" },
//    { SIOCIWFIRSTPRIV + 0xB, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "set_mac_mode" },
//    { SIOCIWFIRSTPRIV + 0xC, 0, IW_PRIV_TYPE_CHAR | 12, "get_mac_mode" },
};

static iw_handler usbdrvwext_handler[] = {
    (iw_handler) NULL,                              /* SIOCSIWCOMMIT */
    (iw_handler) usbdrvwext_giwname,                /* SIOCGIWNAME */
    (iw_handler) NULL,                              /* SIOCSIWNWID */
    (iw_handler) NULL,                              /* SIOCGIWNWID */
    (iw_handler) usbdrvwext_siwfreq,                /* SIOCSIWFREQ */
    (iw_handler) usbdrvwext_giwfreq,                /* SIOCGIWFREQ */
    (iw_handler) usbdrvwext_siwmode,                /* SIOCSIWMODE */
    (iw_handler) usbdrvwext_giwmode,                /* SIOCGIWMODE */
    (iw_handler) usbdrvwext_siwsens,                /* SIOCSIWSENS */
    (iw_handler) usbdrvwext_giwsens,                /* SIOCGIWSENS */
    (iw_handler) NULL, /* not used */               /* SIOCSIWRANGE */
    (iw_handler) usbdrvwext_giwrange,               /* SIOCGIWRANGE */
    (iw_handler) NULL, /* not used */               /* SIOCSIWPRIV */
    (iw_handler) NULL, /* kernel code */            /* SIOCGIWPRIV */
    (iw_handler) NULL, /* not used */               /* SIOCSIWSTATS */
    (iw_handler) NULL, /* kernel code */            /* SIOCGIWSTATS */
    (iw_handler) NULL,                              /* SIOCSIWSPY */
    (iw_handler) NULL,                              /* SIOCGIWSPY */
    (iw_handler) NULL,                              /* -- hole -- */
    (iw_handler) NULL,                              /* -- hole -- */
    (iw_handler) usbdrvwext_siwap,                  /* SIOCSIWAP */
    (iw_handler) usbdrvwext_giwap,                  /* SIOCGIWAP */
    (iw_handler) NULL,              /* -- hole -- */
    (iw_handler) usbdrvwext_iwaplist,               /* SIOCGIWAPLIST */
    (iw_handler) usbdrvwext_siwscan,                /* SIOCSIWSCAN */
    (iw_handler) usbdrvwext_giwscan,                /* SIOCGIWSCAN */
    (iw_handler) usbdrvwext_siwessid,               /* SIOCSIWESSID */
    (iw_handler) usbdrvwext_giwessid,               /* SIOCGIWESSID */

    (iw_handler) usbdrvwext_siwnickn,               /* SIOCSIWNICKN */
    (iw_handler) usbdrvwext_giwnickn,               /* SIOCGIWNICKN */
    (iw_handler) NULL,                              /* -- hole -- */
    (iw_handler) NULL,                              /* -- hole -- */
    (iw_handler) usbdrvwext_siwrate,                /* SIOCSIWRATE */
    (iw_handler) usbdrvwext_giwrate,                /* SIOCGIWRATE */
    (iw_handler) usbdrvwext_siwrts,                 /* SIOCSIWRTS */
    (iw_handler) usbdrvwext_giwrts,                 /* SIOCGIWRTS */
    (iw_handler) usbdrvwext_siwfrag,                /* SIOCSIWFRAG */
    (iw_handler) usbdrvwext_giwfrag,                /* SIOCGIWFRAG */
    (iw_handler) usbdrvwext_siwtxpow,               /* SIOCSIWTXPOW */
    (iw_handler) usbdrvwext_giwtxpow,               /* SIOCGIWTXPOW */
    (iw_handler) usbdrvwext_siwretry,               /* SIOCSIWRETRY */
    (iw_handler) usbdrvwext_giwretry,               /* SIOCGIWRETRY */
    (iw_handler) usbdrvwext_siwencode,              /* SIOCSIWENCODE */
    (iw_handler) usbdrvwext_giwencode,              /* SIOCGIWENCODE */
    (iw_handler) usbdrvwext_siwpower,               /* SIOCSIWPOWER */
    (iw_handler) usbdrvwext_giwpower,               /* SIOCGIWPOWER */
};

static const iw_handler usbdrv_private_handler[] =
{
	//(iw_handler) usbdrvwext_setparam,		/* SIOCWFIRSTPRIV+0 */
	//(iw_handler) usbdrvwext_getparam,		/* SIOCWFIRSTPRIV+1 */
	//(iw_handler) usbdrvwext_setkey,		    /* SIOCWFIRSTPRIV+2 */
	//(iw_handler) usbdrvwext_setwmmparams,	/* SIOCWFIRSTPRIV+3 */
	//(iw_handler) usbdrvwext_delkey,		    /* SIOCWFIRSTPRIV+4 */
	//(iw_handler) usbdrvwext_getwmmparams,	/* SIOCWFIRSTPRIV+5 */
	//(iw_handler) usbdrvwext_setmlme,		/* SIOCWFIRSTPRIV+6 */
	//(iw_handler) usbdrvwext_getchaninfo,	/* SIOCWFIRSTPRIV+7 */
	//(iw_handler) usbdrvwext_setoptie,		/* SIOCWFIRSTPRIV+8 */
	//(iw_handler) usbdrvwext_getoptie,		/* SIOCWFIRSTPRIV+9 */
	//(iw_handler) usbdrvwext_addmac,		    /* SIOCWFIRSTPRIV+10 */
	//(iw_handler) usbdrvwext_getscanresults,	/* SIOCWFIRSTPRIV+11 */
	//(iw_handler) usbdrvwext_delmac,		    /* SIOCWFIRSTPRIV+12 */
	//(iw_handler) usbdrvwext_getchanlist,	/* SIOCWFIRSTPRIV+13 */
	//(iw_handler) usbdrvwext_setchanlist,	/* SIOCWFIRSTPRIV+14 */
	//(iw_handler) NULL,				        /* SIOCWFIRSTPRIV+15 */
	//(iw_handler) usbdrvwext_chanswitch,	    /* SIOCWFIRSTPRIV+16 */
	//(iw_handler) usbdrvwext_setmode,		/* SIOCWFIRSTPRIV+17 */
	//(iw_handler) usbdrvwext_getmode,		/* SIOCWFIRSTPRIV+18 */
    NULL,               /* SIOCIWFIRSTPRIV */
};

static struct iw_handler_def p80211wext_handler_def = {
    .num_standard = sizeof(usbdrvwext_handler) / sizeof(iw_handler),
    .num_private = sizeof(usbdrv_private_handler)/sizeof(iw_handler),
    .num_private_args = sizeof(usbdrv_private_args)/sizeof(struct iw_priv_args),
    .standard = usbdrvwext_handler,
    .private = (iw_handler *) usbdrv_private_handler,
    .private_args = (struct iw_priv_args *) usbdrv_private_args
};

/* WDS */
//struct zsWdsStruct wds[ZM_WDS_PORT_NUMBER];
//void zfInitWdsStruct(void);

/* VAP */
struct zsVapStruct vap[ZM_VAP_PORT_NUMBER];
void zfLnxInitVapStruct(void);


/**
 * usbdrv_intr - interrupt handler
 * @irq: the IRQ number
 * @dev_inst: the net_device struct
 * @regs: registers (unused)
 *
 * This routine is the ISR for the usbdrv board. It services
 * the RX & TX queues & starts the RU if it has stopped due
 * to no resources.
 */
irqreturn_t usbdrv_intr(int irq, void *dev_inst, struct pt_regs *regs)
{
    struct net_device *dev;
    struct usbdrv_private *macp;

    dev = dev_inst;
    macp = dev->ml_priv;


    /* Read register error, card may be unpluged */
    if (0)//(intr_status == -1)
        return IRQ_NONE;

    /* the device is closed, don't continue or else bad things may happen. */
    if (!netif_running(dev)) {
        return IRQ_NONE;
    }

    if (macp->driver_isolated) {
        return IRQ_NONE;
    }

#if (WLAN_HOSTIF == WLAN_PCI)
    //zfiIsrPci(dev);
#endif

    return IRQ_HANDLED;
}

int usbdrv_open(struct net_device *dev)
{
    struct usbdrv_private *macp = dev->ml_priv;
    int rc = 0;
    u16_t size;
    void* mem;
    //unsigned char addr[6];
    struct zsCbFuncTbl cbFuncTbl;

    printk("Enter open()\n");

//#ifndef CONFIG_SMP
//    read_lock(&(macp->isolate_lock));
//#endif
    if (macp->driver_isolated) {
        rc = -EBUSY;
        goto exit;
    }

    size = zfiGlobalDataSize(dev);
    if ((mem = kmalloc(size, GFP_KERNEL)) == NULL)
    {
        rc = -EBUSY;
        goto exit;
    }
    macp->wd = mem;

    memset(&cbFuncTbl, 0, sizeof(struct zsCbFuncTbl));
    cbFuncTbl.zfcbAuthNotify = zfLnxAuthNotify;
    cbFuncTbl.zfcbAuthNotify = zfLnxAuthNotify;
    cbFuncTbl.zfcbAsocNotify = zfLnxAsocNotify;
    cbFuncTbl.zfcbDisAsocNotify = zfLnxDisAsocNotify;
    cbFuncTbl.zfcbApConnectNotify = zfLnxApConnectNotify;
    cbFuncTbl.zfcbConnectNotify = zfLnxConnectNotify;
    cbFuncTbl.zfcbScanNotify = zfLnxScanNotify;
    cbFuncTbl.zfcbMicFailureNotify = zfLnxMicFailureNotify;
    cbFuncTbl.zfcbApMicFailureNotify = zfLnxApMicFailureNotify;
    cbFuncTbl.zfcbIbssPartnerNotify = zfLnxIbssPartnerNotify;
    cbFuncTbl.zfcbMacAddressNotify = zfLnxMacAddressNotify;
    cbFuncTbl.zfcbSendCompleteIndication = zfLnxSendCompleteIndication;
    cbFuncTbl.zfcbRecvEth = zfLnxRecvEth;
    cbFuncTbl.zfcbRecv80211 = zfLnxRecv80211;
    cbFuncTbl.zfcbRestoreBufData = zfLnxRestoreBufData;
#ifdef ZM_ENABLE_CENC
    cbFuncTbl.zfcbCencAsocNotify = zfLnxCencAsocNotify;
#endif //ZM_ENABLE_CENC
    cbFuncTbl.zfcbHwWatchDogNotify = zfLnxWatchDogNotify;
    zfiWlanOpen(dev, &cbFuncTbl);

#if 0
    {
        //u16_t mac[3] = {0x1300, 0xb6d4, 0x5aaf};
        u16_t mac[3] = {0x8000, 0x00ab, 0x0000};
        //zfiWlanSetMacAddress(dev, mac);
    }
    /* MAC address */
    zfiWlanQueryMacAddress(dev, addr);
    dev->dev_addr[0] = addr[0];
    dev->dev_addr[1] = addr[1];
    dev->dev_addr[2] = addr[2];
    dev->dev_addr[3] = addr[3];
    dev->dev_addr[4] = addr[4];
    dev->dev_addr[5] = addr[5];
#endif
    //zfwMacAddressNotify() will be called to setup dev->dev_addr[]

    zfLnxCreateThread(dev);

    mod_timer(&(macp->hbTimer10ms), jiffies + (1*HZ)/100);   //10 ms

    netif_carrier_on(dev);

    netif_start_queue(dev);

#if ZM_AP_MODE == 1
    zfiWlanSetWlanMode(dev, ZM_MODE_AP);
    zfiWlanSetBasicRate(dev, 0xf, 0, 0);
    zfiWlanSetSSID(dev, "OTUS_CWY", 8);
    zfiWlanSetDtimCount(dev, 3);

  #if ZM_WEP_MOME == 1
    {
        u8_t key[16] = {0x12, 0x34, 0x56, 0x78, 0x90};
        struct zsKeyInfo keyInfo;

        keyInfo.keyLength = 5;
        keyInfo.keyIndex = 0;
        keyInfo.flag = 0;
        keyInfo.key = key;
        zfiWlanSetKey(dev, keyInfo);

        zfiWlanSetEncryMode(dev, ZM_WEP64);
    }

    #if ZM_SHARE_AUTH == 1
    zfiWlanSetAuthenticationMode(dev, 1);
    #endif //#if ZM_SHARE_AUTH == 1
  #endif //#if ZM_WEP_MOME == 1

#elif ZM_PIBSS_MODE == 1
    zfiWlanSetWlanMode(dev, ZM_MODE_PSEUDO);
#else
    zfiWlanSetWlanMode(dev, ZM_MODE_INFRASTRUCTURE);
#endif
    //zfiWlanSetChannel(dev, ZM_CHANNEL, FALSE);
    zfiWlanSetFrequency(dev, 2462000, FALSE);
    zfiWlanSetRtsThreshold(dev, 32767);
    zfiWlanSetFragThreshold(dev, 0);

    zfiWlanEnable(dev);

#ifdef ZM_ENABLE_CENC
    macp->netlink_sk = netlink_kernel_create(NETLINK_USERSOCK, 1, NULL, THIS_MODULE);

    if (macp->netlink_sk == NULL)
    {
        printk(KERN_ERR "Can't create NETLINK socket\n");
    }
#endif

    macp->DeviceOpened = 1;
exit:
//#ifndef CONFIG_SMP
//    read_unlock(&(macp->isolate_lock));
//#endif
    //zfRegisterWdsDev(dev, 0);
    //zfLnxRegisterVapDev(dev, 0);

    return rc;
}




/**
 * usbdrv_get_stats - get driver statistics
 * @dev: adapter's net_device struct
 *
 * This routine is called when the OS wants the adapter's stats returned.
 * It returns the address of the net_device_stats stucture for the device.
 * If the statistics are currently being updated, then they might be incorrect
 * for a short while. However, since this cannot actually cause damage, no
 * locking is used.
 */

struct net_device_stats * usbdrv_get_stats(struct net_device *dev)
{
    struct usbdrv_private *macp = dev->ml_priv;

    macp->drv_stats.net_stats.tx_errors =
        macp->drv_stats.net_stats.tx_carrier_errors +
        macp->drv_stats.net_stats.tx_aborted_errors;

    macp->drv_stats.net_stats.rx_errors =
        macp->drv_stats.net_stats.rx_crc_errors +
        macp->drv_stats.net_stats.rx_frame_errors +
        macp->drv_stats.net_stats.rx_length_errors;


    return &(macp->drv_stats.net_stats);
}


/**
 * usbdrv_set_mac - set the MAC address
 * @dev: adapter's net_device struct
 * @addr: the new address
 *
 * This routine sets the ethernet address of the board
 * Returns:
 * 0  - if successful
 * -1 - otherwise
 */

int usbdrv_set_mac(struct net_device *dev, void *addr)
{
    struct usbdrv_private *macp;
    int rc = -1;

    macp = dev->ml_priv;
    read_lock(&(macp->isolate_lock));

    if (macp->driver_isolated) {
        goto exit;
    }

    rc = 0;


exit:
    read_unlock(&(macp->isolate_lock));
    return rc;
}



void
usbdrv_isolate_driver(struct usbdrv_private *macp)
{
#ifndef CONFIG_SMP
    write_lock_irq(&(macp->isolate_lock));
#endif
    macp->driver_isolated = TRUE;
#ifndef CONFIG_SMP
    write_unlock_irq(&(macp->isolate_lock));
#endif

    if (netif_running(macp->device))
    {
        netif_carrier_off(macp->device);
        netif_stop_queue(macp->device);
    }
}

#define VLAN_SIZE   	4
int usbdrv_change_mtu(struct net_device *dev, int new_mtu)
{
    if ((new_mtu < 68) || (new_mtu > (ETH_DATA_LEN + VLAN_SIZE)))
        return -EINVAL;

    dev->mtu = new_mtu;
    return 0;
}

void zfLnxUnlinkAllUrbs(struct usbdrv_private *macp);

int usbdrv_close(struct net_device *dev)
{
extern void zfHpLedCtrl(struct net_device *dev, u16_t ledId, u8_t mode);

    struct usbdrv_private *macp = dev->ml_priv;

    printk(KERN_DEBUG "usbdrv_close\n");

    netif_carrier_off(macp->device);

    del_timer_sync(&macp->hbTimer10ms);

    printk(KERN_DEBUG "usbdrv_netif_carrier_off\n");

    usbdrv_isolate_driver(macp);

    printk(KERN_DEBUG "usbdrv_isolate_driver\n");

    netif_carrier_off(macp->device);
#ifdef ZM_ENABLE_CENC
    /* CENC */
    if (macp->netlink_sk != NULL)
    {
    //    sock_release(macp->netlink_sk);
        printk(KERN_ERR "usbdrv close netlink socket\n");
    }
#endif //ZM_ENABLE_CENC
#if (WLAN_HOSTIF == WLAN_PCI)
    //free_irq(dev->irq, dev);
#endif

    /* Turn off LED */
    zfHpLedCtrl(dev, 0, 0);
    zfHpLedCtrl(dev, 1, 0);

    /* Delay for a while */
    mdelay(10);

    /* clear WPA/RSN IE */
    macp->supIe[1] = 0;

    /* set the isolate flag to false, so usbdrv_open can be called */
    macp->driver_isolated = FALSE;

    zfiWlanClose(dev);
    kfree(macp->wd);

    zfLnxUnlinkAllUrbs(macp);

    return 0;
}




int usbdrv_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
    int notify_stop = FALSE;
    struct usbdrv_private *macp = dev->ml_priv;

#if 0
    /* Test code */
    {
        struct sk_buff* s;

        s = skb_copy_expand(skb, 8, 0, GFP_ATOMIC);
        skb_push(s, 8);
        s->data[0] = 'z';
        s->data[1] = 'y';
        s->data[2] = 'd';
        s->data[3] = 'a';
        s->data[4] = 's';
        printk("len1=%d, len2=%d", skb->len, s->len);
        netlink_broadcast(rtnl, s, 0, RTMGRP_LINK, GFP_ATOMIC);
    }
#endif

#if ZM_DISABLE_XMIT
    dev_kfree_skb_irq(skb);
#else
    zfiTxSendEth(dev, skb, 0);
#endif
    macp->drv_stats.net_stats.tx_bytes += skb->len;
    macp->drv_stats.net_stats.tx_packets++;

    //dev_kfree_skb_irq(skb);

    if (notify_stop) {
        netif_carrier_off(dev);
        netif_stop_queue(dev);
    }

    return NETDEV_TX_OK;
}




void usbdrv_set_multi(struct net_device *dev)
{


    if (!(dev->flags & IFF_UP))
        return;

        return;

}



/**
 * usbdrv_clear_structs - free resources

 * @dev: adapter's net_device struct
 *
 * Free all device specific structs, unmap i/o address, etc.
 */
void usbdrv_clear_structs(struct net_device *dev)
{
    struct usbdrv_private *macp = dev->ml_priv;


#if (WLAN_HOSTIF == WLAN_PCI)
    iounmap(macp->regp);

    pci_release_regions(macp->pdev);
    pci_disable_device(macp->pdev);
    pci_set_drvdata(macp->pdev, NULL);
#endif

    kfree(macp);

    kfree(dev);

}

void usbdrv_remove1(struct pci_dev *pcid)
{
    struct net_device *dev;
    struct usbdrv_private *macp;

    if (!(dev = (struct net_device *) pci_get_drvdata(pcid)))
        return;

    macp = dev->ml_priv;
    unregister_netdev(dev);

    usbdrv_clear_structs(dev);
}


void zfLnx10msTimer(struct net_device* dev)
{
    struct usbdrv_private *macp = dev->ml_priv;

    mod_timer(&(macp->hbTimer10ms), jiffies + (1*HZ)/100);   //10 ms
    zfiHeartBeat(dev);
    return;
}

void zfLnxInitVapStruct(void)
{
    u16_t i;

    for (i=0; i<ZM_VAP_PORT_NUMBER; i++)
    {
        vap[i].dev = NULL;
        vap[i].openFlag = 0;
    }
}

int zfLnxVapOpen(struct net_device *dev)
{
    u16_t vapId;

    vapId = zfLnxGetVapId(dev);

    if (vap[vapId].openFlag == 0)
    {
        vap[vapId].openFlag = 1;
    	printk("zfLnxVapOpen : device name=%s, vap ID=%d\n", dev->name, vapId);
    	zfiWlanSetSSID(dev, "vap1", 4);
    	zfiWlanEnable(dev);
    	netif_start_queue(dev);
    }
    else
    {
        printk("VAP opened error : vap ID=%d\n", vapId);
    }
	return 0;
}

int zfLnxVapClose(struct net_device *dev)
{
    u16_t vapId;

    vapId = zfLnxGetVapId(dev);

    if (vapId != 0xffff)
    {
        if (vap[vapId].openFlag == 1)
        {
            printk("zfLnxVapClose: device name=%s, vap ID=%d\n", dev->name, vapId);

            netif_stop_queue(dev);
            vap[vapId].openFlag = 0;
        }
        else
        {
            printk("VAP port was not opened : vap ID=%d\n", vapId);
        }
    }
	return 0;
}

int zfLnxVapXmitFrame(struct sk_buff *skb, struct net_device *dev)
{
    int notify_stop = FALSE;
    struct usbdrv_private *macp = dev->ml_priv;
    u16_t vapId;

    vapId = zfLnxGetVapId(dev);
    //printk("zfLnxVapXmitFrame: vap ID=%d\n", vapId);
    //printk("zfLnxVapXmitFrame(), skb=%lxh\n", (u32_t)skb);

    if (vapId >= ZM_VAP_PORT_NUMBER)
    {
        dev_kfree_skb_irq(skb);
        return NETDEV_TX_OK;
    }
#if 1
    if (vap[vapId].openFlag == 0)
    {
        dev_kfree_skb_irq(skb);
        return NETDEV_TX_OK;
    }
#endif


    zfiTxSendEth(dev, skb, 0x1);

    macp->drv_stats.net_stats.tx_bytes += skb->len;
    macp->drv_stats.net_stats.tx_packets++;

    //dev_kfree_skb_irq(skb);

    if (notify_stop) {
        netif_carrier_off(dev);
        netif_stop_queue(dev);
    }

    return NETDEV_TX_OK;
}

static const struct net_device_ops vap_netdev_ops = {
	.ndo_open		= zfLnxVapOpen,
	.ndo_stop		= zfLnxVapClose,
	.ndo_start_xmit		= zfLnxVapXmitFrame,
	.ndo_get_stats		= usbdrv_get_stats,
	.ndo_change_mtu		= usbdrv_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef ZM_HOSTAPD_SUPPORT
	.ndo_do_ioctl		= usbdrv_ioctl,
#else
	.ndo_do_ioctl		= NULL,
#endif
};

int zfLnxRegisterVapDev(struct net_device* parentDev, u16_t vapId)
{
    /* Allocate net device structure */
    vap[vapId].dev = alloc_etherdev(0);
    printk("Register vap dev=%x\n", (u32_t)vap[vapId].dev);

    if(vap[vapId].dev == NULL) {
        printk("alloc_etherdev fail\n");
        return -ENOMEM;
    }

    /* Setup the default settings */
    ether_setup(vap[vapId].dev);

    /* MAC address */
    memcpy(vap[vapId].dev->dev_addr, parentDev->dev_addr, ETH_ALEN);

    vap[vapId].dev->irq = parentDev->irq;
    vap[vapId].dev->base_addr = parentDev->base_addr;
    vap[vapId].dev->mem_start = parentDev->mem_start;
    vap[vapId].dev->mem_end = parentDev->mem_end;
    vap[vapId].dev->ml_priv = parentDev->ml_priv;

    //dev->hard_start_xmit = &zd1212_wds_xmit_frame;
    vap[vapId].dev->netdev_ops = &vap_netdev_ops;
    vap[vapId].dev->destructor = free_netdev;

    vap[vapId].dev->tx_queue_len = 0;

    vap[vapId].dev->dev_addr[0] = parentDev->dev_addr[0];
    vap[vapId].dev->dev_addr[1] = parentDev->dev_addr[1];
    vap[vapId].dev->dev_addr[2] = parentDev->dev_addr[2];
    vap[vapId].dev->dev_addr[3] = parentDev->dev_addr[3];
    vap[vapId].dev->dev_addr[4] = parentDev->dev_addr[4];
    vap[vapId].dev->dev_addr[5] = parentDev->dev_addr[5] + (vapId+1);

    /* Stop the network queue first */
    netif_stop_queue(vap[vapId].dev);

    sprintf(vap[vapId].dev->name, "vap%d", vapId);
    printk("Register VAP dev success : %s\n", vap[vapId].dev->name);

    if(register_netdevice(vap[vapId].dev) != 0) {
        printk("register VAP device fail\n");
        vap[vapId].dev = NULL;
        return -EINVAL;
    }

    return 0;
}

int zfLnxUnregisterVapDev(struct net_device* parentDev, u16_t vapId)
{
    int ret = 0;

    printk("Unregister VAP dev : %s\n", vap[vapId].dev->name);

    if(vap[vapId].dev != NULL) {
        printk("Unregister vap dev=%x\n", (u32_t)vap[vapId].dev);
        //
        //unregister_netdevice(wds[wdsId].dev);
        unregister_netdev(vap[vapId].dev);

        printk("VAP unregister_netdevice\n");
        vap[vapId].dev = NULL;
    }
    else {
        printk("unregister VAP device: %d fail\n", vapId);
        ret = -EINVAL;
    }

    return ret;
}



#  define SUBMIT_URB(u,f)       usb_submit_urb(u,f)
#  define USB_ALLOC_URB(u,f)    usb_alloc_urb(u,f)

//extern void zfiWlanQueryMacAddress(zdev_t* dev, u8_t* addr);

extern int usbdrv_open(struct net_device *dev);
extern int usbdrv_close(struct net_device *dev);
extern int usbdrv_xmit_frame(struct sk_buff *skb, struct net_device *dev);
extern int usbdrv_xmit_frame(struct sk_buff *skb, struct net_device *dev);
extern int usbdrv_change_mtu(struct net_device *dev, int new_mtu);
extern void usbdrv_set_multi(struct net_device *dev);
extern int usbdrv_set_mac(struct net_device *dev, void *addr);
extern struct net_device_stats * usbdrv_get_stats(struct net_device *dev);
extern int usbdrv_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
extern UsbTxQ_t *zfLnxGetUsbTxBuffer(struct net_device *dev);

int zfLnxAllocAllUrbs(struct usbdrv_private *macp)
{
    struct usb_interface *interface = macp->interface;
    struct usb_host_interface *iface_desc = &interface->altsetting[0];

    struct usb_endpoint_descriptor *endpoint;
    int i;

    /* descriptor matches, let's find the endpoints needed */
    /* check out the endpoints */
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i)
    {
        endpoint = &iface_desc->endpoint[i].desc;
	 if (usb_endpoint_is_bulk_in(endpoint))
        {
            /* we found a bulk in endpoint */
            printk(KERN_ERR "bulk in: wMaxPacketSize = %x\n", le16_to_cpu(endpoint->wMaxPacketSize));
        }

	 if (usb_endpoint_is_bulk_out(endpoint))
        {
            /* we found a bulk out endpoint */
            printk(KERN_ERR "bulk out: wMaxPacketSize = %x\n", le16_to_cpu(endpoint->wMaxPacketSize));
        }

	 if (usb_endpoint_is_int_in(endpoint))
        {
            /* we found a interrupt in endpoint */
            printk(KERN_ERR "interrupt in: wMaxPacketSize = %x\n", le16_to_cpu(endpoint->wMaxPacketSize));
            printk(KERN_ERR "interrupt in: int_interval = %d\n", endpoint->bInterval);
        }

	 if (usb_endpoint_is_int_out(endpoint))
        {
            /* we found a interrupt out endpoint */
            printk(KERN_ERR "interrupt out: wMaxPacketSize = %x\n", le16_to_cpu(endpoint->wMaxPacketSize));
            printk(KERN_ERR "interrupt out: int_interval = %d\n", endpoint->bInterval);
        }
    }

    /* Allocate all Tx URBs */
    for (i = 0; i < ZM_MAX_TX_URB_NUM; i++)
    {
        macp->WlanTxDataUrb[i] = USB_ALLOC_URB(0, GFP_KERNEL);

        if (macp->WlanTxDataUrb[i] == 0)
        {
            int j;

            /* Free all urbs */
            for (j = 0; j < i; j++)
            {
                usb_free_urb(macp->WlanTxDataUrb[j]);
            }

            return 0;
        }
    }

    /* Allocate all Rx URBs */
    for (i = 0; i < ZM_MAX_RX_URB_NUM; i++)
    {
        macp->WlanRxDataUrb[i] = USB_ALLOC_URB(0, GFP_KERNEL);

        if (macp->WlanRxDataUrb[i] == 0)
        {
            int j;

            /* Free all urbs */
            for (j = 0; j < i; j++)
            {
                usb_free_urb(macp->WlanRxDataUrb[j]);
            }

            for (j = 0; j < ZM_MAX_TX_URB_NUM; j++)
            {
                usb_free_urb(macp->WlanTxDataUrb[j]);
            }

            return 0;
        }
    }

    /* Allocate Register Read/Write USB */
    macp->RegOutUrb = USB_ALLOC_URB(0, GFP_KERNEL);
    macp->RegInUrb = USB_ALLOC_URB(0, GFP_KERNEL);

    return 1;
}

void zfLnxFreeAllUrbs(struct usbdrv_private *macp)
{
    int i;

    /* Free all Tx URBs */
    for (i = 0; i < ZM_MAX_TX_URB_NUM; i++)
    {
        if (macp->WlanTxDataUrb[i] != NULL)
        {
            usb_free_urb(macp->WlanTxDataUrb[i]);
        }
    }

    /* Free all Rx URBs */
    for (i = 0; i < ZM_MAX_RX_URB_NUM; i++)
    {
        if (macp->WlanRxDataUrb[i] != NULL)
        {
            usb_free_urb(macp->WlanRxDataUrb[i]);
        }
    }

    /* Free USB Register Read/Write URB */
    usb_free_urb(macp->RegOutUrb);
    usb_free_urb(macp->RegInUrb);
}

void zfLnxUnlinkAllUrbs(struct usbdrv_private *macp)
{
    int i;

    /* Unlink all Tx URBs */
    for (i = 0; i < ZM_MAX_TX_URB_NUM; i++)
    {
        if (macp->WlanTxDataUrb[i] != NULL)
        {
            usb_unlink_urb(macp->WlanTxDataUrb[i]);
        }
    }

    /* Unlink all Rx URBs */
    for (i = 0; i < ZM_MAX_RX_URB_NUM; i++)
    {
        if (macp->WlanRxDataUrb[i] != NULL)
        {
            usb_unlink_urb(macp->WlanRxDataUrb[i]);
        }
    }

    /* Unlink USB Register Read/Write URB */
    usb_unlink_urb(macp->RegOutUrb);

    usb_unlink_urb(macp->RegInUrb);
}

static const struct net_device_ops otus_netdev_ops = {
	.ndo_open		= usbdrv_open,
	.ndo_stop		= usbdrv_close,
	.ndo_start_xmit		= usbdrv_xmit_frame,
	.ndo_change_mtu		= usbdrv_change_mtu,
	.ndo_get_stats		= usbdrv_get_stats,
	.ndo_set_multicast_list	= usbdrv_set_multi,
	.ndo_set_mac_address	= usbdrv_set_mac,
	.ndo_do_ioctl		= usbdrv_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
};

u8_t zfLnxInitSetup(struct net_device *dev, struct usbdrv_private *macp)
{
    //unsigned char addr[6];

    //init_MUTEX(&macp->ps_sem);
    //init_MUTEX(&macp->reg_sem);
    //init_MUTEX(&macp->bcn_sem);
    //init_MUTEX(&macp->config_sem);

    spin_lock_init(&(macp->cs_lock));
#if 0
    /* MAC address */
    zfiWlanQueryMacAddress(dev, addr);
    dev->dev_addr[0] = addr[0];
    dev->dev_addr[1] = addr[1];
    dev->dev_addr[2] = addr[2];
    dev->dev_addr[3] = addr[3];
    dev->dev_addr[4] = addr[4];
    dev->dev_addr[5] = addr[5];
#endif
    dev->wireless_handlers = (struct iw_handler_def *)&p80211wext_handler_def;

    dev->netdev_ops = &otus_netdev_ops;

    dev->flags |= IFF_MULTICAST;

    dev->dev_addr[0] = 0x00;
    dev->dev_addr[1] = 0x03;
    dev->dev_addr[2] = 0x7f;
    dev->dev_addr[3] = 0x11;
    dev->dev_addr[4] = 0x22;
    dev->dev_addr[5] = 0x33;

    /* Initialize Heart Beat timer */
    init_timer(&macp->hbTimer10ms);
    macp->hbTimer10ms.data = (unsigned long)dev;
    macp->hbTimer10ms.function = (void *)&zfLnx10msTimer;

    /* Initialize WDS and VAP data structure */
    //zfInitWdsStruct();
    zfLnxInitVapStruct();

    return 1;
}

u8_t zfLnxClearStructs(struct net_device *dev)
{
    u16_t ii;
    u16_t TxQCnt;

    TxQCnt = zfLnxCheckTxBufferCnt(dev);

    printk(KERN_ERR "TxQCnt: %d\n", TxQCnt);

    for(ii = 0; ii < TxQCnt; ii++)
    {
        UsbTxQ_t *TxQ = zfLnxGetUsbTxBuffer(dev);

        printk(KERN_ERR "dev_kfree_skb_any\n");
        /* Free buffer */
        dev_kfree_skb_any(TxQ->buf);
    }

    return 0;
}
