// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/


#define _OSDEP_SERVICE_C_

#include <drv_types.h>
#include <rtw_debug.h>

/*
* Translate the OS dependent @param error_code to OS independent RTW_STATUS_CODE
* @return: one of RTW_STATUS_CODE
*/
inline int RTW_STATUS_CODE(int error_code)
{
	if (error_code >= 0)
		return _SUCCESS;
	return _FAIL;
}

void *_rtw_malloc(u32 sz)
{
	return kmalloc(sz, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
}

void *_rtw_zmalloc(u32 sz)
{
	void *pbuf = _rtw_malloc(sz);

	if (pbuf)
		memset(pbuf, 0, sz);

	return pbuf;
}

inline struct sk_buff *_rtw_skb_alloc(u32 sz)
{
	return __dev_alloc_skb(sz, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
}

inline struct sk_buff *_rtw_skb_copy(const struct sk_buff *skb)
{
	return skb_copy(skb, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
}

inline int _rtw_netif_rx(_nic_hdl ndev, struct sk_buff *skb)
{
	skb->dev = ndev;
	return netif_rx(skb);
}

void _rtw_init_queue(struct __queue *pqueue)
{
	INIT_LIST_HEAD(&(pqueue->queue));

	spin_lock_init(&(pqueue->lock));
}

struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv, void *old_priv)
{
	struct net_device *pnetdev;
	struct rtw_netdev_priv_indicator *pnpi;

	pnetdev = alloc_etherdev_mq(sizeof(struct rtw_netdev_priv_indicator), 4);
	if (!pnetdev)
		goto RETURN;

	pnpi = netdev_priv(pnetdev);
	pnpi->priv = old_priv;
	pnpi->sizeof_priv = sizeof_priv;

RETURN:
	return pnetdev;
}

struct net_device *rtw_alloc_etherdev(int sizeof_priv)
{
	struct net_device *pnetdev;
	struct rtw_netdev_priv_indicator *pnpi;

	pnetdev = alloc_etherdev_mq(sizeof(struct rtw_netdev_priv_indicator), 4);
	if (!pnetdev)
		goto RETURN;

	pnpi = netdev_priv(pnetdev);

	pnpi->priv = vzalloc(sizeof_priv);
	if (!pnpi->priv) {
		free_netdev(pnetdev);
		pnetdev = NULL;
		goto RETURN;
	}

	pnpi->sizeof_priv = sizeof_priv;
RETURN:
	return pnetdev;
}

void rtw_free_netdev(struct net_device *netdev)
{
	struct rtw_netdev_priv_indicator *pnpi;

	if (!netdev)
		goto RETURN;

	pnpi = netdev_priv(netdev);

	if (!pnpi->priv)
		goto RETURN;

	vfree(pnpi->priv);
	free_netdev(netdev);

RETURN:
	return;
}

int rtw_change_ifname(struct adapter *padapter, const char *ifname)
{
	struct net_device *pnetdev;
	struct net_device *cur_pnetdev;
	struct rereg_nd_name_data *rereg_priv;
	int ret;

	if (!padapter)
		goto error;

	cur_pnetdev = padapter->pnetdev;
	rereg_priv = &padapter->rereg_nd_name_priv;

	/* free the old_pnetdev */
	if (rereg_priv->old_pnetdev) {
		free_netdev(rereg_priv->old_pnetdev);
		rereg_priv->old_pnetdev = NULL;
	}

	if (!rtnl_is_locked())
		unregister_netdev(cur_pnetdev);
	else
		unregister_netdevice(cur_pnetdev);

	rereg_priv->old_pnetdev = cur_pnetdev;

	pnetdev = rtw_init_netdev(padapter);
	if (!pnetdev)
		goto error;

	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(adapter_to_dvobj(padapter)));

	rtw_init_netdev_name(pnetdev, ifname);

	memcpy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);

	if (!rtnl_is_locked())
		ret = register_netdev(pnetdev);
	else
		ret = register_netdevice(pnetdev);

	if (ret != 0) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("register_netdev() failed\n"));
		goto error;
	}

	return 0;

error:
	return -1;
}

void rtw_buf_free(u8 **buf, u32 *buf_len)
{
	if (!buf || !buf_len)
		return;

	if (*buf) {
		*buf_len = 0;
		kfree(*buf);
		*buf = NULL;
	}
}

void rtw_buf_update(u8 **buf, u32 *buf_len, u8 *src, u32 src_len)
{
	u32 ori_len = 0, dup_len = 0;
	u8 *ori = NULL;
	u8 *dup = NULL;

	if (!buf || !buf_len)
		return;

	if (!src || !src_len)
		goto keep_ori;

	/* duplicate src */
	dup = rtw_malloc(src_len);
	if (dup) {
		dup_len = src_len;
		memcpy(dup, src, dup_len);
	}

keep_ori:
	ori = *buf;
	ori_len = *buf_len;

	/* replace buf with dup */
	*buf_len = 0;
	*buf = dup;
	*buf_len = dup_len;

	/* free ori */
	if (ori && ori_len > 0)
		kfree(ori);
}


/**
 * rtw_cbuf_full - test if cbuf is full
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: true if cbuf is full
 */
inline bool rtw_cbuf_full(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read - 1) ? true : false;
}

/**
 * rtw_cbuf_empty - test if cbuf is empty
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Returns: true if cbuf is empty
 */
inline bool rtw_cbuf_empty(struct rtw_cbuf *cbuf)
{
	return (cbuf->write == cbuf->read) ? true : false;
}

/**
 * rtw_cbuf_push - push a pointer into cbuf
 * @cbuf: pointer of struct rtw_cbuf
 * @buf: pointer to push in
 *
 * Lock free operation, be careful of the use scheme
 * Returns: true push success
 */
bool rtw_cbuf_push(struct rtw_cbuf *cbuf, void *buf)
{
	if (rtw_cbuf_full(cbuf))
		return _FAIL;

	DBG_871X("%s on %u\n", __func__, cbuf->write);
	cbuf->bufs[cbuf->write] = buf;
	cbuf->write = (cbuf->write + 1) % cbuf->size;

	return _SUCCESS;
}

/**
 * rtw_cbuf_pop - pop a pointer from cbuf
 * @cbuf: pointer of struct rtw_cbuf
 *
 * Lock free operation, be careful of the use scheme
 * Returns: pointer popped out
 */
void *rtw_cbuf_pop(struct rtw_cbuf *cbuf)
{
	void *buf;
	if (rtw_cbuf_empty(cbuf))
		return NULL;

	DBG_871X("%s on %u\n", __func__, cbuf->read);
	buf = cbuf->bufs[cbuf->read];
	cbuf->read = (cbuf->read + 1) % cbuf->size;

	return buf;
}

/**
 * rtw_cbuf_alloc - allocate a rtw_cbuf with given size and do initialization
 * @size: size of pointer
 *
 * Returns: pointer of srtuct rtw_cbuf, NULL for allocation failure
 */
struct rtw_cbuf *rtw_cbuf_alloc(u32 size)
{
	struct rtw_cbuf *cbuf;

	cbuf = rtw_malloc(sizeof(*cbuf) + sizeof(void *) * size);

	if (cbuf) {
		cbuf->write = cbuf->read = 0;
		cbuf->size = size;
	}

	return cbuf;
}
