/*
 * 802.1x EAPOL definitions
 *
 * See
 * IEEE Std 802.1X-2001
 * IEEE 802.1X RADIUS Usage Guidelines
 *
 * Copyright Open Broadcom Corporation
 *
 * $Id: eapol.h 452678 2014-01-31 19:16:29Z $
 */

#ifndef _eapol_h_
#define _eapol_h_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

#include <bcmcrypto/aeskeywrap.h>

/* EAPOL for 802.3/Ethernet */
typedef BWL_PRE_PACKED_STRUCT struct {
	struct ether_header eth;	/* 802.3/Ethernet header */
	unsigned char version;		/* EAPOL protocol version */
	unsigned char type;		/* EAPOL type */
	unsigned short length;		/* Length of body */
	unsigned char body[1];		/* Body (optional) */
} BWL_POST_PACKED_STRUCT eapol_header_t;

#define EAPOL_HEADER_LEN 18

typedef struct {
	unsigned char version;		/* EAPOL protocol version */
	unsigned char type;		/* EAPOL type */
	unsigned short length;		/* Length of body */
} eapol_hdr_t;

#define EAPOL_HDR_LEN 4

/* EAPOL version */
#define WPA2_EAPOL_VERSION	2
#define WPA_EAPOL_VERSION	1
#define LEAP_EAPOL_VERSION	1
#define SES_EAPOL_VERSION	1

/* EAPOL types */
#define EAP_PACKET		0
#define EAPOL_START		1
#define EAPOL_LOGOFF		2
#define EAPOL_KEY		3
#define EAPOL_ASF		4

/* EAPOL-Key types */
#define EAPOL_RC4_KEY		1
#define EAPOL_WPA2_KEY		2	/* 802.11i/WPA2 */
#define EAPOL_WPA_KEY		254	/* WPA */

/* RC4 EAPOL-Key header field sizes */
#define EAPOL_KEY_REPLAY_LEN	8
#define EAPOL_KEY_IV_LEN	16
#define EAPOL_KEY_SIG_LEN	16

/* RC4 EAPOL-Key */
typedef BWL_PRE_PACKED_STRUCT struct {
	unsigned char type;			/* Key Descriptor Type */
	unsigned short length;			/* Key Length (unaligned) */
	unsigned char replay[EAPOL_KEY_REPLAY_LEN];	/* Replay Counter */
	unsigned char iv[EAPOL_KEY_IV_LEN];		/* Key IV */
	unsigned char index;				/* Key Flags & Index */
	unsigned char signature[EAPOL_KEY_SIG_LEN];	/* Key Signature */
	unsigned char key[1];				/* Key (optional) */
} BWL_POST_PACKED_STRUCT eapol_key_header_t;

#define EAPOL_KEY_HEADER_LEN 	44

/* RC4 EAPOL-Key flags */
#define EAPOL_KEY_FLAGS_MASK	0x80
#define EAPOL_KEY_BROADCAST	0
#define EAPOL_KEY_UNICAST	0x80

/* RC4 EAPOL-Key index */
#define EAPOL_KEY_INDEX_MASK	0x7f

/* WPA/802.11i/WPA2 EAPOL-Key header field sizes */
#define EAPOL_WPA_KEY_REPLAY_LEN	8
#define EAPOL_WPA_KEY_NONCE_LEN		32
#define EAPOL_WPA_KEY_IV_LEN		16
#define EAPOL_WPA_KEY_RSC_LEN		8
#define EAPOL_WPA_KEY_ID_LEN		8
#define EAPOL_WPA_KEY_MIC_LEN		16
#define EAPOL_WPA_KEY_DATA_LEN		(EAPOL_WPA_MAX_KEY_SIZE + AKW_BLOCK_LEN)
#define EAPOL_WPA_MAX_KEY_SIZE		32

/* WPA EAPOL-Key */
typedef BWL_PRE_PACKED_STRUCT struct {
	unsigned char type;		/* Key Descriptor Type */
	unsigned short key_info;	/* Key Information (unaligned) */
	unsigned short key_len;		/* Key Length (unaligned) */
	unsigned char replay[EAPOL_WPA_KEY_REPLAY_LEN];	/* Replay Counter */
	unsigned char nonce[EAPOL_WPA_KEY_NONCE_LEN];	/* Nonce */
	unsigned char iv[EAPOL_WPA_KEY_IV_LEN];		/* Key IV */
	unsigned char rsc[EAPOL_WPA_KEY_RSC_LEN];	/* Key RSC */
	unsigned char id[EAPOL_WPA_KEY_ID_LEN];		/* WPA:Key ID, 802.11i/WPA2: Reserved */
	unsigned char mic[EAPOL_WPA_KEY_MIC_LEN];	/* Key MIC */
	unsigned short data_len;			/* Key Data Length */
	unsigned char data[EAPOL_WPA_KEY_DATA_LEN];	/* Key data */
} BWL_POST_PACKED_STRUCT eapol_wpa_key_header_t;

#define EAPOL_WPA_KEY_LEN 		95

/* WPA/802.11i/WPA2 KEY KEY_INFO bits */
#define WPA_KEY_DESC_V1		0x01
#define WPA_KEY_DESC_V2		0x02
#define WPA_KEY_DESC_V3		0x03
#define WPA_KEY_PAIRWISE	0x08
#define WPA_KEY_INSTALL		0x40
#define WPA_KEY_ACK		0x80
#define WPA_KEY_MIC		0x100
#define WPA_KEY_SECURE		0x200
#define WPA_KEY_ERROR		0x400
#define WPA_KEY_REQ		0x800

#define WPA_KEY_DESC_V2_OR_V3 WPA_KEY_DESC_V2

/* WPA-only KEY KEY_INFO bits */
#define WPA_KEY_INDEX_0		0x00
#define WPA_KEY_INDEX_1		0x10
#define WPA_KEY_INDEX_2		0x20
#define WPA_KEY_INDEX_3		0x30
#define WPA_KEY_INDEX_MASK	0x30
#define WPA_KEY_INDEX_SHIFT	0x04

/* 802.11i/WPA2-only KEY KEY_INFO bits */
#define WPA_KEY_ENCRYPTED_DATA	0x1000

/* Key Data encapsulation */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint8 type;
	uint8 length;
	uint8 oui[3];
	uint8 subtype;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT eapol_wpa2_encap_data_t;

#define EAPOL_WPA2_ENCAP_DATA_HDR_LEN 	6

#define WPA2_KEY_DATA_SUBTYPE_GTK	1
#define WPA2_KEY_DATA_SUBTYPE_STAKEY	2
#define WPA2_KEY_DATA_SUBTYPE_MAC	3
#define WPA2_KEY_DATA_SUBTYPE_PMKID	4
#define WPA2_KEY_DATA_SUBTYPE_IGTK	9

/* GTK encapsulation */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint8	flags;
	uint8	reserved;
	uint8	gtk[EAPOL_WPA_MAX_KEY_SIZE];
} BWL_POST_PACKED_STRUCT eapol_wpa2_key_gtk_encap_t;

#define EAPOL_WPA2_KEY_GTK_ENCAP_HDR_LEN 	2

#define WPA2_GTK_INDEX_MASK	0x03
#define WPA2_GTK_INDEX_SHIFT	0x00

#define WPA2_GTK_TRANSMIT	0x04

/* IGTK encapsulation */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint16	key_id;
	uint8	ipn[6];
	uint8	key[EAPOL_WPA_MAX_KEY_SIZE];
} BWL_POST_PACKED_STRUCT eapol_wpa2_key_igtk_encap_t;

#define EAPOL_WPA2_KEY_IGTK_ENCAP_HDR_LEN 	8

/* STAKey encapsulation */
typedef BWL_PRE_PACKED_STRUCT struct {
	uint8	reserved[2];
	uint8	mac[ETHER_ADDR_LEN];
	uint8	stakey[EAPOL_WPA_MAX_KEY_SIZE];
} BWL_POST_PACKED_STRUCT eapol_wpa2_key_stakey_encap_t;

#define WPA2_KEY_DATA_PAD	0xdd


/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _eapol_h_ */
