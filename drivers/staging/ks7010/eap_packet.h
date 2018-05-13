/* SPDX-License-Identifier: GPL-2.0 */
#ifndef EAP_PACKET_H
#define EAP_PACKET_H

#include <linux/compiler.h>
#include <linux/bitops.h>
#include <uapi/linux/if_ether.h>

struct ether_hdr {
	unsigned char h_dest[ETH_ALEN];	/* destination eth addr */
	unsigned char h_source[ETH_ALEN];	/* source ether addr    */
	unsigned char h_dest_snap;
	unsigned char h_source_snap;
	unsigned char h_command;
	unsigned char h_vendor_id[3];
	__be16 h_proto;	/* packet type ID field */
	/* followed by length octets of data */
} __packed;

#define ETHER_HDR_SIZE sizeof(struct ether_hdr)

struct ieee802_1x_hdr {
	unsigned char version;
	unsigned char type;
	unsigned short length;
	/* followed by length octets of data */
} __packed;

enum {
	IEEE802_1X_TYPE_EAP_PACKET = 0,
	IEEE802_1X_TYPE_EAPOL_START = 1,
	IEEE802_1X_TYPE_EAPOL_LOGOFF = 2,
	IEEE802_1X_TYPE_EAPOL_KEY = 3,
	IEEE802_1X_TYPE_EAPOL_ENCAPSULATED_ASF_ALERT = 4
};

#define WPA_NONCE_LEN 32
#define WPA_REPLAY_COUNTER_LEN 8

struct wpa_eapol_key {
	unsigned char type;
	__be16 key_info;
	unsigned short key_length;
	unsigned char replay_counter[WPA_REPLAY_COUNTER_LEN];
	unsigned char key_nonce[WPA_NONCE_LEN];
	unsigned char key_iv[16];
	unsigned char key_rsc[8];
	unsigned char key_id[8];	/* Reserved in IEEE 802.11i/RSN */
	unsigned char key_mic[16];
	unsigned short key_data_length;
	/* followed by key_data_length bytes of key_data */
} __packed;

#define WPA_KEY_INFO_TYPE_MASK GENMASK(2, 0)
#define WPA_KEY_INFO_TYPE_HMAC_MD5_RC4 BIT(0)
#define WPA_KEY_INFO_TYPE_HMAC_SHA1_AES BIT(1)
#define WPA_KEY_INFO_KEY_TYPE BIT(3)	/* 1 = Pairwise, 0 = Group key */
/* bit4..5 is used in WPA, but is reserved in IEEE 802.11i/RSN */
#define WPA_KEY_INFO_KEY_INDEX_MASK GENMASK(5, 4)
#define WPA_KEY_INFO_KEY_INDEX_SHIFT 4
#define WPA_KEY_INFO_INSTALL BIT(6)	/* pairwise */
#define WPA_KEY_INFO_TXRX BIT(6)	/* group */
#define WPA_KEY_INFO_ACK BIT(7)
#define WPA_KEY_INFO_MIC BIT(8)
#define WPA_KEY_INFO_SECURE BIT(9)
#define WPA_KEY_INFO_ERROR BIT(10)
#define WPA_KEY_INFO_REQUEST BIT(11)
#define WPA_KEY_INFO_ENCR_KEY_DATA BIT(12)	/* IEEE 802.11i/RSN only */

#endif /* EAP_PACKET_H */
