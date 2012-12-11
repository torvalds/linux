#ifndef _IPV6_PROTOCOL_DEFINES_
#define _IPV6_PROTOCOL_DEFINES_

#define IPV6HDR_TYPE_HOPBYHOP 0x0
#define IPV6HDR_TYPE_ROUTING 0x2B
#define IPV6HDR_TYPE_FRAGMENTATION 0x2C
#define IPV6HDR_TYPE_DESTOPTS 0x3c
#define IPV6HDR_TYPE_AUTHENTICATION 0x33
#define IPV6HDR_TYPE_ENCRYPTEDSECURITYPAYLOAD 0x34
#define MASK_IPV6_CS_SPEC 0x2

#define TCP_HEADER_TYPE	0x6
#define UDP_HEADER_TYPE	0x11
#define IPV6_ICMP_HDR_TYPE 0x2
#define IPV6_FLOWLABEL_BITOFFSET 9

#define IPV6_MAX_CHAINEDHDR_BUFFBYTES 0x64
/*
 * Size of Dest Options field of Destinations Options Header
 * in bytes.
 */
#define IPV6_DESTOPTS_HDR_OPTIONSIZE 0x8

typedef struct IPV6HeaderFormatTag {
	unsigned char  ucVersionPrio;
	unsigned char  aucFlowLabel[3];
	unsigned short usPayloadLength;
	unsigned char  ucNextHeader;
	unsigned char  ucHopLimit;
	unsigned long  ulSrcIpAddress[4];
	unsigned long  ulDestIpAddress[4];
} IPV6Header;

typedef struct IPV6RoutingHeaderFormatTag {
	unsigned char ucNextHeader;
	unsigned char ucRoutingType;
	unsigned char ucNumAddresses;
	unsigned char ucNextAddress;
	unsigned long ulReserved;
} IPV6RoutingHeader;

typedef struct IPV6FragmentHeaderFormatTag {
	unsigned char  ucNextHeader;
	unsigned char  ucReserved;
	unsigned short usFragmentOffset;
	unsigned long  ulIdentification;
} IPV6FragmentHeader;

typedef struct IPV6DestOptionsHeaderFormatTag {
	unsigned char ucNextHeader;
	unsigned char ucHdrExtLen;
	unsigned char ucDestOptions[6];
} IPV6DestOptionsHeader;

typedef struct IPV6HopByHopOptionsHeaderFormatTag {
	unsigned char ucNextHeader;
	unsigned char ucMisc[3];
	unsigned long ulJumboPayloadLen;
} IPV6HopByHopOptionsHeader;

typedef struct IPV6AuthenticationHeaderFormatTag {
	unsigned char  ucNextHeader;
	unsigned char  ucLength;
	unsigned short usReserved;
	unsigned long  ulSecurityParametersIndex;
} IPV6AuthenticationHeader;

enum bcm_ipaddr_context {
	eSrcIpAddress,
	eDestIpAddress
};

/* Function Prototypes */

unsigned short IpVersion6(struct bcm_mini_adapter *Adapter, /* < Pointer to the driver control structure */
					void *pcIpHeader, /* <Pointer to the IP Hdr of the packet */
					struct bcm_classifier_rule *pstClassifierRule);

void DumpIpv6Address(unsigned long *puIpv6Address);

extern bool MatchSrcPort(struct bcm_classifier_rule *pstClassifierRule, unsigned short ushSrcPort);
extern bool MatchDestPort(struct bcm_classifier_rule *pstClassifierRule, unsigned short ushSrcPort);
extern bool MatchProtocol(struct bcm_classifier_rule *pstClassifierRule, unsigned char ucProtocol);

#endif
