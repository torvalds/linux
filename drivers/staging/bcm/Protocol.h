/************************************
*	Protocol.h
*************************************/
#ifndef	__PROTOCOL_H__
#define	__PROTOCOL_H__

#define IPV4 4
#define IPV6 6

struct ArpHeader {
	struct arphdr arp;
	unsigned char ar_sha[ETH_ALEN];	/* sender hardware address  */
	unsigned char ar_sip[4];	/* sender IP address        */
	unsigned char ar_tha[ETH_ALEN];	/* target hardware address  */
	unsigned char ar_tip[4];	/* target IP address        */
};

struct bcm_transport_header {
	union {
		struct udphdr uhdr;
		struct tcphdr thdr;
	};
} __packed;

enum bcm_ip_frame_type {
	eNonIPPacket,
	eIPv4Packet,
	eIPv6Packet
};

enum bcm_eth_frame_type {
	eEthUnsupportedFrame,
	eEth802LLCFrame,
	eEth802LLCSNAPFrame,
	eEth802QVLANFrame,
	eEthOtherFrame
};

struct bcm_eth_packet_info {
	enum bcm_ip_frame_type  eNwpktIPFrameType;
	enum bcm_eth_frame_type eNwpktEthFrameType;
	unsigned short	usEtherType;
	unsigned char	ucDSAP;
};

struct bcm_eth_q_frame {
	struct bcm_eth_header EThHdr;
	unsigned short UserPriority:3;
	unsigned short CFI:1;
	unsigned short VLANID:12;
	unsigned short EthType;
} __packed;

struct bcm_eth_llc_frame {
	struct bcm_eth_header EThHdr;
	unsigned char DSAP;
	unsigned char SSAP;
	unsigned char Control;
} __packed;

struct bcm_eth_llc_snap_frame {
	struct bcm_eth_header EThHdr;
	unsigned char DSAP;
	unsigned char SSAP;
	unsigned char Control;
	unsigned char OUI[3];
	unsigned short usEtherType;
} __packed;

struct bcm_ethernet2_frame {
	struct bcm_eth_header EThHdr;
} __packed;

#define ETHERNET_FRAMETYPE_IPV4		ntohs(0x0800)
#define ETHERNET_FRAMETYPE_IPV6		ntohs(0x86dd)
#define ETHERNET_FRAMETYPE_802QVLAN	ntohs(0x8100)

/* Per SF CS Specification Encodings */
enum bcm_spec_encoding {
	eCSSpecUnspecified = 0,
	eCSPacketIPV4,
	eCSPacketIPV6,
	eCS802_3PacketEthernet,
	eCS802_1QPacketVLAN,
	eCSPacketIPV4Over802_3Ethernet,
	eCSPacketIPV6Over802_3Ethernet,
	eCSPacketIPV4Over802_1QVLAN,
	eCSPacketIPV6Over802_1QVLAN,
	eCSPacketUnsupported
};

#define	IP6_HEADER_LEN		40
#define IP_VERSION(byte)	(((byte&0xF0)>>4))

#define MAC_ADDRESS_SIZE	6
#define	ETH_AND_IP_HEADER_LEN	(14 + 20)
#define L4_SRC_PORT_LEN		2
#define L4_DEST_PORT_LEN	2
#define	CTRL_PKT_LEN		(8 + ETH_AND_IP_HEADER_LEN)

#define	ETH_ARP_FRAME		0x806
#define	ETH_IPV4_FRAME		0x800
#define	ETH_IPV6_FRAME		0x86DD
#define UDP			0x11
#define TCP			0x06

#define	ARP_OP_REQUEST		0x01
#define	ARP_OP_REPLY		0x02
#define	ARP_PKT_SIZE		60

/* This is the format for the TCP packet header */
struct bcm_tcp_header {
	unsigned short usSrcPort;
	unsigned short usDestPort;
	unsigned long  ulSeqNumber;
	unsigned long  ulAckNumber;
	unsigned char  HeaderLength;
	unsigned char  ucFlags;
	unsigned short usWindowsSize;
	unsigned short usChkSum;
	unsigned short usUrgetPtr;
};

#define TCP_HEADER_LEN		sizeof(struct bcm_tcp_header)
#define TCP_ACK			0x10  /* Bit 4 in tcpflags field. */
#define GET_TCP_HEADER_LEN(byte) ((byte&0xF0)>>4)

#endif /* __PROTOCOL_H__ */
