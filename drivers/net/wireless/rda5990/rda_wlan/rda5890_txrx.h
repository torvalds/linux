#ifndef _RDA5890_TXRX_H_
#define _RDA5890_TXRX_H_

int rda5890_host_to_card(struct rda5890_private *priv, 
		char *packet, unsigned short packet_len, unsigned char packet_type);

void rda5890_data_rx(struct rda5890_private *priv, 
		char *data, unsigned short data_len);
int rda5890_data_tx(struct rda5890_private *priv, 
		struct sk_buff *skb, struct net_device *dev);

#endif

