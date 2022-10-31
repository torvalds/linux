/**
 ******************************************************************************
 *
 * @file ecrnx_amt.c
 *
 * @brief Entry point of the ECRNX driver
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */
 
#include "ecrnx_main.h"
#include "eswin_utils.h"
#include "ecrnx_calibration_data.h"

#ifdef CONFIG_ECRNX_WIFO_CAIL
#define AMT_GAIN_DELTA_MSG_FLAG ("gaindelta=")
#define AMT_GAIN_DELTA_MSG_BUFF_LEN 60 //strlen(AMT_GAIN_DELTA_MSG_FLAG) + sizeof(gain_delta)
static char gain_delta_msg_buf[AMT_GAIN_DELTA_MSG_BUFF_LEN];
extern bool set_gain;

struct ecrnx_iwpriv_amt_vif amt_vif;
#if defined(CONFIG_WIRELESS_EXT) && defined(CONFIG_WEXT_PRIV)
extern const struct iw_handler_def  ecrnx_wext_private_handler;
#endif

static int eswin_netdev_amt_open(struct net_device *ndev)
{
	ECRNX_DBG("amt netdev open\n");

	return 0;
}

static int eswin_netdev_amt_stop(struct net_device *ndev)
{
	ECRNX_DBG("amt netdev stop\n");

	return 0;
}

static netdev_tx_t eswin_netdev_amt_start_xmit(struct sk_buff *skb,
					    struct net_device *ndev)
{
	if (skb)
		dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops eswin_netdev_ops_amt = {
	.ndo_open = eswin_netdev_amt_open,
	.ndo_stop = eswin_netdev_amt_stop,
	.ndo_start_xmit = eswin_netdev_amt_start_xmit
};

static int ecrnx_amt_gain_delta_msg_send(void)
{
    if (set_gain != true)
        return -1;

    memcpy(gain_delta_msg_buf, AMT_GAIN_DELTA_MSG_FLAG, strlen(AMT_GAIN_DELTA_MSG_FLAG));
    memcpy((gain_delta_msg_buf + strlen(AMT_GAIN_DELTA_MSG_FLAG)), gain_delta, GAIN_DELTA_CFG_BUF_SIZE);

    host_send(gain_delta_msg_buf, sizeof(gain_delta_msg_buf), TX_FLAG_AMT_IWPRIV_IE);

    return 0;
}

int ecrnx_amt_init(void)
{
	struct net_device *ndev;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
    ndev = alloc_netdev(0, "amt0", ether_setup);
#else
	ndev = alloc_netdev(0, "amt0", NET_NAME_UNKNOWN, ether_setup);
#endif
	if (!ndev) {
		ECRNX_DBG("alloc netdev fail !!");
		return -1;
	}

	amt_vif.ndev = ndev;
	amt_vif.rxlen = 0;
	init_waitqueue_head(&amt_vif.rxdataq);

	ndev->netdev_ops = &eswin_netdev_ops_amt;
#if defined(CONFIG_WIRELESS_EXT) && defined(CONFIG_WEXT_PRIV)
	ndev->wireless_handlers = &ecrnx_wext_private_handler;
#endif
	/* clear the mac address */
	memset(ndev->dev_addr, 0, ETH_ALEN);

	if (register_netdev(ndev) != 0)
        goto err_dev;

    ecrnx_amt_gain_delta_msg_send();

	ECRNX_PRINT(" %s:register the amt net device success\n", __func__);
	return 0;

err_dev:
	ECRNX_ERR(" %s couldn't register the amt net device!!",  __func__);
	free_netdev(ndev);
	amt_vif.ndev = NULL;
    return -1;
}

void ecrnx_amt_deinit(void)
{
    ECRNX_DBG(ECRNX_FN_ENTRY_STR);

	if (amt_vif.ndev)
		unregister_netdev(amt_vif.ndev);

	amt_vif.ndev = NULL;
}
#endif

 


