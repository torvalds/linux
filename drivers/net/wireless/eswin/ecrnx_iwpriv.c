/**
 ******************************************************************************
 *
 * @file ecrnx_iwpriv.c
 *
 * @brief iwpriv function definitions
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

/**
 * INCLUDE FILES
 ******************************************************************************
 */
#include <net/cfg80211.h>
#include <net/iw_handler.h>
#include "ecrnx_defs.h"
#include "eswin_utils.h"

#ifdef CONFIG_ECRNX_WIFO_CAIL
#include "ecrnx_amt.h"
#include "core.h"
#endif

#ifdef CONFIG_WIRELESS_EXT
/**
 * FUNCTION DEFINITIONS
 ******************************************************************************
 */
#define IN
#define OUT

#ifdef CONFIG_WEXT_PRIV
 /* This may be wrong. When using the new SIOCIWFIRSTPRIV range, we probably
  * should use only "GET" ioctls (last bit set to 1). "SET" ioctls are root
  * only and don't return the modified struct ifreq to the application which
  * is usually a problem. - Jean II */
#ifdef CONFIG_ECRNX_WIFO_CAIL
#define IOCTL_IWPRIV_AMT                (SIOCIWFIRSTPRIV + 1)
#endif
#define IOCTL_IWPRIV_WD                 (SIOCIWFIRSTPRIV + 3)

#if 0
static int priv_set_int(IN struct net_device *prNetDev,
          IN struct iw_request_info *prIwReqInfo,
          IN union iwreq_data *prIwReqData, IN OUT char *pcExtra)
{
    int *flag = (int*)pcExtra;
    ECRNX_PRINT("cmd=%x, flags=%x\n",
      prIwReqInfo->cmd, prIwReqInfo->flags);
    ECRNX_PRINT("mode=%x, flags=%x\n",
      prIwReqData->mode, prIwReqData->data.flags);
    *flag = 0x1234;
    prIwReqData->param.value = 0x1230;

    return 1;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Private ioctl get int handler.
*
* \param[in] pDev Net device requested.
* \param[out] pIwReq Pointer to iwreq structure.
* \param[in] prIwReqData The ioctl req structure, use the field of sub-command.
* \param[out] pcExtra The buffer with put the return value
*
* \retval 0 For success.
* \retval -EOPNOTSUPP If cmd is not supported.
* \retval -EFAULT For fail.
*
*/
/*----------------------------------------------------------------------------*/
static int priv_get_int(IN struct net_device *prNetDev,
      IN struct iw_request_info *prIwReqInfo,
      IN union iwreq_data *prIwReqData, IN OUT char *pcExtra)
{
    int status = 0;
    prIwReqData->mode = 0xabcd;
    return status;
}               /* __priv_get_int */

static int priv_set_struct(IN struct net_device *prNetDev,
       IN struct iw_request_info *prIwReqInfo,
       IN union iwreq_data *prIwReqData, IN OUT char *pcExtra)
{
  ECRNX_PRINT("cmd=%x, flags=%x\n",
       prIwReqInfo->cmd, prIwReqInfo->flags);
  ECRNX_PRINT("mode=%x, flags=%x\n",
       prIwReqData->mode, prIwReqData->data.flags);

  return 0;
  //return compat_priv(prNetDev, prIwReqInfo,
  //     prIwReqData, pcExtra, __priv_set_struct);
}

static int priv_get_struct(IN struct net_device *prNetDev,
       IN struct iw_request_info *prIwReqInfo,
       IN union iwreq_data *prIwReqData, IN OUT char *pcExtra)
{
    ECRNX_PRINT("cmd=%x, flags=%x\n",
       prIwReqInfo->cmd, prIwReqInfo->flags);
    ECRNX_PRINT("mode=%x, flags=%x\n",
       prIwReqData->mode, prIwReqData->data.flags);

    prIwReqData->data.length = 6;
    memcpy(pcExtra, "ecrnx", 6);
    return 0;

}

static int priv_get_mac(IN struct net_device *prNetDev,
     IN struct iw_request_info *prIwReqInfo,
     IN union iwreq_data *prIwReqData, IN OUT char *pcExtra)
{
    struct sockaddr *dst = (struct sockaddr *) pcExtra;
    struct ecrnx_vif *vif;
    dbg_req_t req;

    req.dbg_level = DBG_TYPE_D;
    req.direct = 0;

    vif = netdev_priv(prNetDev);
    //send cmd to slave
    ECRNX_PRINT("priv_get_mac: send cmd to slave \n");
    host_send(&req, sizeof(dbg_req_t), TX_FLAG_IWPRIV_IE);
    //wait for slave confirm
    vif->rxdatas = 0;
    wait_event_interruptible_timeout(vif->rxdataq, vif->rxdatas, 2*HZ);

    ECRNX_PRINT("priv_get_mac: rx_len:%d \n", vif->rxlen);
    if (!vif->rxlen)
        return -1;

    prIwReqData->data.length = vif->rxlen;
    memcpy(dst->sa_data, vif->rxdata, vif->rxlen);
    dst->sa_family = 1;
    prIwReqData->data.length = 1;

    return 0;
}

static int priv_get_vers(IN struct net_device *prNetDev,
      IN struct iw_request_info *prIwReqInfo,
      IN union iwreq_data *prIwReqData, IN OUT char *pcExtra)
{
    ECRNX_PRINT("get vers cmd=%x, flags=%x\n", prIwReqInfo->cmd, prIwReqInfo->flags);
    ECRNX_PRINT("mode=%x, flags=%x\n", prIwReqData->mode, prIwReqData->data.flags);

   memcpy(pcExtra, "1.0.1", 6);
   prIwReqData->data.length = 6;

    return 0;
}


static int priv_set_debug_level(IN struct net_device *prNetDev,
      IN struct iw_request_info *prIwReqInfo,
      IN union iwreq_data *prIwReqData, IN OUT char *pcExtra)
{
    ECRNX_PRINT("priv_set_debug_level cmd=%x, flags=%x\n",
    prIwReqInfo->cmd, prIwReqInfo->flags);
    ECRNX_PRINT("mode=%x, flags=%x\n",
    prIwReqData->mode, prIwReqData->data.flags);

    ECRNX_PRINT("param_value:%d \n", prIwReqData->param.value);

    ecrnx_dbg_level = prIwReqData->param.value;
    return 0;
}
#endif

#ifdef CONFIG_ECRNX_WIFO_CAIL
static int priv_amt(IN struct net_device *prNetDev,
     IN struct iw_request_info *prIwReqInfo,
     IN union iwreq_data *prIwReqData, IN OUT char *pcExtra)
{
    struct sockaddr *dst = (struct sockaddr *) pcExtra;

	if (amt_mode == false) {
		ECRNX_ERR(" The current mode does not support the AMT commands!!\n");
		return -1;
	}
//	printk("buff:%s, len:%d\n", prIwReqData->data.pointer, prIwReqData->data.length);
    //send cmd to slave
    char *reqdata = kzalloc(prIwReqData->data.length, GFP_KERNEL);
    if (!reqdata){
        return 0;
    }
    if (copy_from_user(reqdata, prIwReqData->data.pointer, prIwReqData->data.length)) {
        return 0;
    }
    host_send(reqdata, prIwReqData->data.length, TX_FLAG_AMT_IWPRIV_IE);
    kfree(reqdata);

    //wait for slave confirm
    amt_vif.rxdatas = 0;
	amt_vif.rxlen = 0;

    wait_event_interruptible_timeout(amt_vif.rxdataq, amt_vif.rxdatas, 2*HZ);

    ECRNX_PRINT("rxdatas: rx_len:%d, rxdata:[%s]\n", amt_vif.rxlen,amt_vif.rxdata);
    if (!amt_vif.rxdatas){
        return -1;
    }

	prIwReqData->data.length = amt_vif.rxlen;
	memcpy(dst->sa_data, amt_vif.rxdata, amt_vif.rxlen);
	dst->sa_family = 1;
	memcpy(pcExtra, amt_vif.rxdata, amt_vif.rxlen);

    return 0;
}
#endif

static struct ecrnx_vif *get_priv_vif(struct ecrnx_hw *ecrnx_hw)
{
     return ecrnx_hw->vif_table[0];
}

void priv_copy_data_wakeup(struct ecrnx_hw *ecrnx_hw, struct sk_buff *skb)
{
    struct ecrnx_vif* ecrnx_vif = get_priv_vif(ecrnx_hw);

    ECRNX_PRINT("iw_cfm vif_start:%d, vif_monitor:%d \n", ecrnx_hw->vif_started, ecrnx_hw->monitor_vif);
    //print_hex_dump(KERN_INFO, DBG_PREFIX_IW_CFM, DUMP_PREFIX_ADDRESS, 32, 1, skb->data, skb->len, false);
    if (ECRNX_RXSIZE > skb->len) {
        ecrnx_vif->rxlen = skb->len;
    } else {
        ecrnx_vif->rxlen = ECRNX_RXSIZE;
    }

    memcpy(ecrnx_vif->rxdata, skb->data, ecrnx_vif->rxlen);
    ecrnx_vif->rxdatas = 1;
    wake_up(&ecrnx_vif->rxdataq);
}

static int priv_wd(IN struct net_device *prNetDev,
     IN struct iw_request_info *prIwReqInfo,
     IN union iwreq_data *prIwReqData, IN OUT char *pcExtra)
{
    struct sockaddr *dst = (struct sockaddr *) pcExtra;
    struct ecrnx_vif *vif;

    //printk("priv_wd:%s, len:%d\n", prIwReqData->data.pointer, prIwReqData->data.length);
    //send cmd to slave
    char *reqdata = kzalloc(prIwReqData->data.length, GFP_KERNEL);
    if (!reqdata){
        return 0;
    }

    if (copy_from_user(reqdata, prIwReqData->data.pointer, prIwReqData->data.length)) {
        return 0;
    }
    host_send(reqdata, prIwReqData->data.length, TX_FLAG_IWPRIV_IE);
    kfree(reqdata);

    //wait for slave confirm
    vif = netdev_priv(prNetDev);
    vif = get_priv_vif(vif->ecrnx_hw);
    vif->rxdatas = 0;
    wait_event_interruptible_timeout(vif->rxdataq, vif->rxdatas, 2*HZ);

    if (!vif->rxdatas)
        return -1;
    
    ECRNX_PRINT("priv_wd: rx_len:%d rxdata:[%s]\n", vif->rxlen, vif->rxdata);
    prIwReqData->data.length = vif->rxlen;
    memcpy(dst->sa_data, vif->rxdata, vif->rxlen);
    dst->sa_family = 1;
    memcpy(pcExtra, vif->rxdata, vif->rxlen);

    return 0;
}

/*
 * Structures to export the Wireless Handlers
 */
static const struct iw_priv_args ecrnx_wext_private_args[] = {
#ifdef CONFIG_ECRNX_WIFO_CAIL
	{IOCTL_IWPRIV_AMT, IW_PRIV_TYPE_CHAR | 2000, IW_PRIV_TYPE_CHAR | 2000, "amt"},
#endif
    {IOCTL_IWPRIV_WD, IW_PRIV_TYPE_CHAR | 2000, IW_PRIV_TYPE_CHAR | 2000, "wd"},
};

const iw_handler ecrnx_wext_private_handler[] = {
#ifdef CONFIG_ECRNX_WIFO_CAIL
    [IOCTL_IWPRIV_AMT - SIOCIWFIRSTPRIV] = priv_amt,
#endif
    [IOCTL_IWPRIV_WD - SIOCIWFIRSTPRIV] = priv_wd,
};

#endif

/*------------------------------------------------------------------*/
/*
* Commit handler : called after a bunch of SET operations
*/
static int ecrnx_wext_config_commit(struct net_device *dev,
               struct iw_request_info *info, /* NULL */
               void *zwrq,           /* NULL */
               char *extra)          /* NULL */
{
    return 1;
}

static int ecrnx_wext_get_name(struct net_device *dev,
        struct iw_request_info *info,
        char *cwrq,
        char *extra)
{
    strcpy(cwrq, "IEEE 802.11");
    return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set frequency
 */
static int ecrnx_wext_set_freq(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_freq *fwrq,
             char *extra)
{
    int rc = -EINPROGRESS;      /* Call commit handler */
    ECRNX_PRINT("fwrq->e:%d, fwrq->m:%d \n", fwrq->e, fwrq->m);
    return rc;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get frequency
 */
static int ecrnx_wext_get_freq(struct net_device *dev,
             struct iw_request_info *info,
             struct iw_freq *fwrq,
             char *extra)
{
     fwrq->m = 100000 *
            2.412;
     fwrq->e = 1;
    return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Mode of Operation
 */
static int ecrnx_wext_set_mode(struct net_device *dev,
             struct iw_request_info *info,
             __u32 *uwrq,
             char *extra)
{
   ECRNX_PRINT("*uwrq:%d \n", *uwrq);
    return -EINPROGRESS;        /* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Mode of Operation
 */
static int ecrnx_wext_get_mode(struct net_device *dev,
             struct iw_request_info *info,
             __u32 *uwrq,
             char *extra)
{
   *uwrq = 0xFFEE;
    return 0;
}

static const iw_handler     ecrnx_wext_handler[] =
{
    (iw_handler) ecrnx_wext_config_commit,    /* SIOCSIWCOMMIT */
    (iw_handler) ecrnx_wext_get_name,     /* SIOCGIWNAME */
     (iw_handler) NULL,          /* SIOCSIWNWID */
    (iw_handler) NULL,          /* SIOCGIWNWID */
    (iw_handler) ecrnx_wext_set_freq,     /* SIOCSIWFREQ */
    (iw_handler) ecrnx_wext_get_freq,     /* SIOCGIWFREQ */
    (iw_handler) ecrnx_wext_set_mode,     /* SIOCSIWMODE */
    (iw_handler) ecrnx_wext_get_mode,     /* SIOCGIWMODE */
};

const struct iw_handler_def  ecrnx_wext_handler_def =
{
    .num_standard   = ARRAY_SIZE(ecrnx_wext_handler),
    .standard   = ecrnx_wext_handler,
#ifdef CONFIG_WEXT_PRIV
    .num_private    = ARRAY_SIZE(ecrnx_wext_private_handler),
    .num_private_args = ARRAY_SIZE(ecrnx_wext_private_args),
    .private    = ecrnx_wext_private_handler,
    .private_args   = ecrnx_wext_private_args,
#endif
};
#endif
