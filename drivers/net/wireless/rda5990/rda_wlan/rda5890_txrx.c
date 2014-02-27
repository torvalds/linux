#include <linux/module.h>
#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <net/iw_handler.h>

#include "rda5890_defs.h"
#include "rda5890_dev.h"
#include "rda5890_wid.h"
#include "rda5890_wext.h"

void rda5890_data_rx(struct rda5890_private *priv, 
		char *data, unsigned short data_len)
{
	struct sk_buff *skb;
	char *pkt_data;

	RDA5890_DBGLAP(RDA5890_DA_TXRX, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	skb = dev_alloc_skb(data_len + NET_IP_ALIGN);
	if (!skb) {
		priv->stats.rx_dropped++;
		return;
	}
	skb_reserve(skb, NET_IP_ALIGN);
	pkt_data = skb_put(skb, data_len);
	memcpy(pkt_data, data, data_len);
	skb->dev = priv->dev;
	skb->protocol = eth_type_trans(skb, priv->dev);
	skb->ip_summed = CHECKSUM_NONE;

	RDA5890_DBGLAP(RDA5890_DA_TXRX, RDA5890_DL_TRACE,
		"netif rx, len %d\n", skb->len);
	RDA5890_DBGLAP(RDA5890_DA_TXRX, RDA5890_DL_TRACE,
		"%02x %02x %02x %02x ... ... %02x %02x %02x %02x\n",
		skb->data[0], skb->data[1], skb->data[2], skb->data[3], 
		skb->data[skb->len - 4], skb->data[skb->len - 3],
		skb->data[skb->len - 2], skb->data[skb->len - 1]);

	priv->stats.rx_packets++;
	priv->stats.rx_bytes += data_len;

	if (in_interrupt())
		netif_rx(skb);
	else
		netif_rx_ni(skb);

	RDA5890_DBGLAP(RDA5890_DA_TXRX, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
}


int rda5890_host_to_card(struct rda5890_private *priv, 
		char *packet, unsigned short packet_len, unsigned char packet_type)
{
	int ret = 0;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s <<< \n", __func__);

	ret = priv->hw_host_to_card(priv, packet, packet_len, packet_type);

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
		"%s >>> \n", __func__);

	return ret;
}


int rda5890_data_tx(struct rda5890_private *priv, 
		struct sk_buff *skb, struct net_device *dev)
{
	int ret;
	char *pkt_data;
	uint16_t pkt_len;
	char buf[ETH_FRAME_LEN + 2];
	uint16_t data_len;

	RDA5890_DBGLAP(RDA5890_DA_TXRX, RDA5890_DL_DEBUG,
		"%s <<< \n", __func__);

	ret = NETDEV_TX_OK;

	if (!skb->len || (skb->len > ETH_FRAME_LEN)) {
		priv->stats.tx_dropped++;
		priv->stats.tx_errors++;
		goto free;
	}

	pkt_data = skb->data;
	pkt_len = skb->len;

	RDA5890_DBGLAP(RDA5890_DA_TXRX, RDA5890_DL_TRACE,
		"netif tx len %d\n", pkt_len);
	RDA5890_DBGLAP(RDA5890_DA_TXRX, RDA5890_DL_TRACE,
		"%02x %02x %02x %02x ... ... %02x %02x %02x %02x\n",
		skb->data[0], skb->data[1], skb->data[2], skb->data[3], 
		skb->data[skb->len - 4], skb->data[skb->len - 3],
		skb->data[skb->len - 2], skb->data[skb->len - 1]);

	/* FIXME: we can save this memcpy by adding header inside the sdio driver */
	memcpy(buf + 2, pkt_data, pkt_len);
	data_len = pkt_len + 2;
	buf[0] = (char)(data_len&0xFF);
	buf[1] = (char)((data_len>>8)&0x0F);
	buf[1] |= 0x10;  // for DataOut 0x1

	//RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_NORM,
	//	"sdio tx len %d\n", data_len);
	//RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_NORM,
	//	"%02x %02x %02x %02x ... ... %02x %02x %02x %02x\n",
	//	buf[0], buf[1], buf[2], buf[3], 
	//	buf[data_len - 4], buf[data_len - 3],
	//	buf[data_len - 2], buf[data_len - 1]);

	ret = rda5890_host_to_card(priv, buf, data_len, DATA_REQUEST_PACKET);
	if (ret) {
		RDA5890_ERRP("host_to_card send failed, ret = %d\n", ret);
		goto free;
	}

	priv->stats.tx_packets++;
	priv->stats.tx_bytes += skb->len;

	dev->trans_start = jiffies;

 free:
	/* free right away, since we do copy */
	dev_kfree_skb_any(skb);

	RDA5890_DBGLAP(RDA5890_DA_TXRX, RDA5890_DL_DEBUG,
		"%s >>> \n", __func__);
	return ret;
}

