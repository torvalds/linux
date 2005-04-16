#ifndef _IEEE802_11_H
#define _IEEE802_11_H

#define IEEE802_11_DATA_LEN		2304
/* Maximum size for the MA-UNITDATA primitive, 802.11 standard section
   6.2.1.1.2.

   The figure in section 7.1.2 suggests a body size of up to 2312
   bytes is allowed, which is a bit confusing, I suspect this
   represents the 2304 bytes of real data, plus a possible 8 bytes of
   WEP IV and ICV. (this interpretation suggested by Ramiro Barreiro) */


#define IEEE802_11_HLEN			30
#define IEEE802_11_FRAME_LEN		(IEEE802_11_DATA_LEN + IEEE802_11_HLEN)

struct ieee802_11_hdr {
	u16 frame_ctl;
	u16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u16 seq_ctl;
	u8 addr4[ETH_ALEN];
} __attribute__ ((packed));

/* Frame control field constants */
#define IEEE802_11_FCTL_VERS		0x0002
#define IEEE802_11_FCTL_FTYPE		0x000c
#define IEEE802_11_FCTL_STYPE		0x00f0
#define IEEE802_11_FCTL_TODS		0x0100
#define IEEE802_11_FCTL_FROMDS		0x0200
#define IEEE802_11_FCTL_MOREFRAGS	0x0400
#define IEEE802_11_FCTL_RETRY		0x0800
#define IEEE802_11_FCTL_PM		0x1000
#define IEEE802_11_FCTL_MOREDATA	0x2000
#define IEEE802_11_FCTL_WEP		0x4000
#define IEEE802_11_FCTL_ORDER		0x8000

#define IEEE802_11_FTYPE_MGMT		0x0000
#define IEEE802_11_FTYPE_CTL		0x0004
#define IEEE802_11_FTYPE_DATA		0x0008

/* management */
#define IEEE802_11_STYPE_ASSOC_REQ	0x0000
#define IEEE802_11_STYPE_ASSOC_RESP 	0x0010
#define IEEE802_11_STYPE_REASSOC_REQ	0x0020
#define IEEE802_11_STYPE_REASSOC_RESP	0x0030
#define IEEE802_11_STYPE_PROBE_REQ	0x0040
#define IEEE802_11_STYPE_PROBE_RESP	0x0050
#define IEEE802_11_STYPE_BEACON		0x0080
#define IEEE802_11_STYPE_ATIM		0x0090
#define IEEE802_11_STYPE_DISASSOC	0x00A0
#define IEEE802_11_STYPE_AUTH		0x00B0
#define IEEE802_11_STYPE_DEAUTH		0x00C0

/* control */
#define IEEE802_11_STYPE_PSPOLL		0x00A0
#define IEEE802_11_STYPE_RTS		0x00B0
#define IEEE802_11_STYPE_CTS		0x00C0
#define IEEE802_11_STYPE_ACK		0x00D0
#define IEEE802_11_STYPE_CFEND		0x00E0
#define IEEE802_11_STYPE_CFENDACK	0x00F0

/* data */
#define IEEE802_11_STYPE_DATA		0x0000
#define IEEE802_11_STYPE_DATA_CFACK	0x0010
#define IEEE802_11_STYPE_DATA_CFPOLL	0x0020
#define IEEE802_11_STYPE_DATA_CFACKPOLL	0x0030
#define IEEE802_11_STYPE_NULLFUNC	0x0040
#define IEEE802_11_STYPE_CFACK		0x0050
#define IEEE802_11_STYPE_CFPOLL		0x0060
#define IEEE802_11_STYPE_CFACKPOLL	0x0070

#define IEEE802_11_SCTL_FRAG		0x000F
#define IEEE802_11_SCTL_SEQ		0xFFF0

#endif /* _IEEE802_11_H */
