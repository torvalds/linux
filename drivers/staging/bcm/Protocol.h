/************************************
* 	Protocol.h
*************************************/
#ifndef	__PROTOCOL_H__
#define	__PROTOCOL_H__


#define IPV4				4
#define IPV6                6


struct ArpHeader {
    struct arphdr       arp;
    unsigned char       ar_sha[ETH_ALEN];   /* sender hardware address  */
    unsigned char       ar_sip[4];      /* sender IP address        */
    unsigned char       ar_tha[ETH_ALEN];   /* target hardware address  */
    unsigned char       ar_tip[4];      /* target IP address        */
}/*__attribute__((packed))*/;


struct TransportHeaderT
{
	union
	{
		struct udphdr uhdr;
		struct tcphdr thdr;
	};
} __attribute__((packed));
typedef struct TransportHeaderT xporthdr;


typedef enum _E_NWPKT_IPFRAME_TYPE
{
	eNonIPPacket,
	eIPv4Packet,
	eIPv6Packet
}E_NWPKT_IPFRAME_TYPE;

typedef enum _E_NWPKT_ETHFRAME_TYPE
{
	eEthUnsupportedFrame,
	eEth802LLCFrame,
	eEth802LLCSNAPFrame,
	eEth802QVLANFrame,
	eEthOtherFrame
} E_NWPKT_ETHFRAME_TYPE;

typedef struct _S_ETHCS_PKT_INFO
{
	E_NWPKT_IPFRAME_TYPE eNwpktIPFrameType;
	E_NWPKT_ETHFRAME_TYPE eNwpktEthFrameType;
	USHORT	usEtherType;
	UCHAR	ucDSAP;
}S_ETHCS_PKT_INFO,*PS_ETHCS_PKT_INFO;

typedef struct _ETH_CS_802_Q_FRAME
{
	struct bcm_eth_header EThHdr;
	USHORT UserPriority:3;
	USHORT CFI:1;
	USHORT VLANID:12;
	USHORT EthType;
} __attribute__((packed)) ETH_CS_802_Q_FRAME;

typedef struct _ETH_CS_802_LLC_FRAME
{
	struct bcm_eth_header EThHdr;
	unsigned char DSAP;
	unsigned char SSAP;
	unsigned char Control;
}__attribute__((packed)) ETH_CS_802_LLC_FRAME;

typedef struct _ETH_CS_802_LLC_SNAP_FRAME
{
	struct bcm_eth_header EThHdr;
	unsigned char DSAP;
	unsigned char SSAP;
	unsigned char Control;
	unsigned char OUI[3];
	unsigned short usEtherType;
} __attribute__((packed)) ETH_CS_802_LLC_SNAP_FRAME;

typedef struct _ETH_CS_ETH2_FRAME
{
	struct bcm_eth_header EThHdr;
} __attribute__((packed)) ETH_CS_ETH2_FRAME;

#define ETHERNET_FRAMETYPE_IPV4		ntohs(0x0800)
#define ETHERNET_FRAMETYPE_IPV6 	ntohs(0x86dd)
#define ETHERNET_FRAMETYPE_802QVLAN 	ntohs(0x8100)

//Per SF CS Specification Encodings
typedef enum _E_SERVICEFLOW_CS_SPEC_
{
	eCSSpecUnspecified =0,
	eCSPacketIPV4,
	eCSPacketIPV6,
	eCS802_3PacketEthernet,
	eCS802_1QPacketVLAN,
	eCSPacketIPV4Over802_3Ethernet,
	eCSPacketIPV6Over802_3Ethernet,
	eCSPacketIPV4Over802_1QVLAN,
	eCSPacketIPV6Over802_1QVLAN,
	eCSPacketUnsupported
}E_SERVICEFLOW_CS_SPEC;


#define	IP6_HEADER_LEN	40

#define IP_VERSION(byte)        (((byte&0xF0)>>4))



#define MAC_ADDRESS_SIZE	6
#define	ETH_AND_IP_HEADER_LEN	14 + 20
#define L4_SRC_PORT_LEN 2
#define L4_DEST_PORT_LEN 2



#define	CTRL_PKT_LEN		8 + ETH_AND_IP_HEADER_LEN

#define	ETH_ARP_FRAME			0x806
#define	ETH_IPV4_FRAME			0x800
#define	ETH_IPV6_FRAME			0x86DD
#define UDP 					0x11
#define TCP         			0x06

#define	ARP_OP_REQUEST			0x01
#define	ARP_OP_REPLY			0x02
#define	ARP_PKT_SIZE			60

// This is the format for the TCP packet header
typedef struct _TCP_HEADER
{
	USHORT usSrcPort;
	USHORT usDestPort;
	ULONG  ulSeqNumber;
	ULONG  ulAckNumber;
	UCHAR  HeaderLength;
    UCHAR  ucFlags;
	USHORT usWindowsSize;
	USHORT usChkSum;
	USHORT usUrgetPtr;
} TCP_HEADER,*PTCP_HEADER;
#define TCP_HEADER_LEN  	sizeof(TCP_HEADER)
#define TCP_ACK             0x10  //Bit 4 in tcpflags field.
#define GET_TCP_HEADER_LEN(byte) ((byte&0xF0)>>4)


#endif //__PROTOCOL_H__
