
/*
 * File Name: Arp.c
 * Abstract: This file contains the routines for handling ARP PACKETS
 */
#include "headers.h"
#define	ARP_PKT_SIZE	60

/* =========================================================================
 * Function    - reply_to_arp_request()
 *
 * Description - When this host tries to broadcast ARP request packet through
 *		 		 the virtual interface (veth0), reply directly to upper layer.
 *		 		 This function allocates a new skb for ARP reply packet,
 *		 		 fills in the fields of the packet and then sends it to
 *		 		 upper layer.
 *
 * Parameters  - skb:	Pointer to sk_buff structure of the ARP request pkt.
 *
 * Returns     - None
 * =========================================================================*/

VOID
reply_to_arp_request(struct sk_buff *skb)
{
	PMINI_ADAPTER		Adapter;
	struct ArpHeader 	*pArpHdr = NULL;
	struct ethhdr		*pethhdr = NULL;
	UCHAR 				uiIPHdr[4];
	/* Check for valid skb */
	if(skb == NULL)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Invalid skb: Cannot reply to ARP request\n");
		return;
	}


	Adapter = GET_BCM_ADAPTER(skb->dev);
	/* Print the ARP Request Packet */
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, ARP_RESP, DBG_LVL_ALL, "ARP Packet Dump :");
	BCM_DEBUG_PRINT_BUFFER(Adapter,DBG_TYPE_TX, ARP_RESP, DBG_LVL_ALL, (PUCHAR)(skb->data), skb->len);

	/*
	 * Extract the Ethernet Header and Arp Payload including Header
     */
	pethhdr = (struct ethhdr *)skb->data;
	pArpHdr  = (struct ArpHeader *)(skb->data+ETH_HLEN);

	if(Adapter->bETHCSEnabled)
	{
		if(memcmp(pethhdr->h_source, Adapter->dev->dev_addr, ETH_ALEN))
		{
			bcm_kfree_skb(skb);
			return;
		}
	}

	// Set the Ethernet Header First.
	memcpy(pethhdr->h_dest, pethhdr->h_source, ETH_ALEN);
	if(!memcmp(pethhdr->h_source, Adapter->dev->dev_addr, ETH_ALEN))
	{
		pethhdr->h_source[5]++;
	}

	/* Set the reply to ARP Reply */
	pArpHdr->arp.ar_op = ntohs(ARPOP_REPLY);

	/* Set the HW Address properly */
	memcpy(pArpHdr->ar_sha, pethhdr->h_source, ETH_ALEN);
	memcpy(pArpHdr->ar_tha, pethhdr->h_dest, ETH_ALEN);

	// Swapping the IP Adddress
	memcpy(uiIPHdr,pArpHdr->ar_sip,4);
	memcpy(pArpHdr->ar_sip,pArpHdr->ar_tip,4);
	memcpy(pArpHdr->ar_tip,uiIPHdr,4);

	/* Print the ARP Reply Packet */

	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, ARP_RESP, DBG_LVL_ALL, "ARP REPLY PACKET: ");

	/* Send the Packet to upper layer */
	BCM_DEBUG_PRINT_BUFFER(Adapter,DBG_TYPE_TX, ARP_RESP, DBG_LVL_ALL, (PUCHAR)(skb->data), skb->len);

	skb->protocol = eth_type_trans(skb,skb->dev);
	skb->pkt_type = PACKET_HOST;

//	skb->mac.raw=skb->data+LEADER_SIZE;
	skb_set_mac_header (skb, LEADER_SIZE);
	netif_rx(skb);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, ARP_RESP, DBG_LVL_ALL, "<=============\n");
	return;
}


