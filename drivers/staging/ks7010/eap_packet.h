#ifndef EAP_PACKET_H
#define EAP_PACKET_H

#include <linux/compiler.h>

#define WBIT(n) (1 << (n))

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

#define ETHER_HDR_SIZE 20

struct ether_hdr {
	unsigned char h_dest[ETH_ALEN];	/* destination eth addr */
	unsigned char h_source[ETH_ALEN];	/* source ether addr    */
	unsigned char h_dest_snap;
	unsigned char h_source_snap;
	unsigned char h_command;
	unsigned char h_vendor_id[3];
	unsigned short h_proto;	/* packet type ID field */
#define ETHER_PROTOCOL_TYPE_EAP		0x888e
#define ETHER_PROTOCOL_TYPE_IP		0x0800
#define ETHER_PROTOCOL_TYPE_ARP		0x0806
	/* followed by length octets of data */
} __packed;

struct ieee802_1x_hdr {
	unsigned char version;
	unsigned char type;
	unsigned short length;
	/* followed by length octets of data */
} __packed;

#define EAPOL_VERSION 2

enum { IEEE802_1X_TYPE_EAP_PACKET = 0,
	IEEE802_1X_TYPE_EAPOL_START = 1,
	IEEE802_1X_TYPE_EAPOL_LOGOFF = 2,
	IEEE802_1X_TYPE_EAPOL_KEY = 3,
	IEEE802_1X_TYPE_EAPOL_ENCAPSULATED_ASF_ALERT = 4
};

enum { EAPOL_KEY_TYPE_RC4 = 1, EAPOL_KEY_TYPE_RSN = 2,
	EAPOL_KEY_TYPE_WPA = 254
};

#define IEEE8021X_REPLAY_COUNTER_LEN 8
#define IEEE8021X_KEY_SIGN_LEN 16
#define IEEE8021X_KEY_IV_LEN 16

#define IEEE8021X_KEY_INDEX_FLAG 0x80
#define IEEE8021X_KEY_INDEX_MASK 0x03

struct ieee802_1x_eapol_key {
	unsigned char type;
	unsigned short key_length;
	/*
	 * does not repeat within the life of the keying material used to
	 * encrypt the Key field; 64-bit NTP timestamp MAY be used here
	 */
	unsigned char replay_counter[IEEE8021X_REPLAY_COUNTER_LEN];
	unsigned char key_iv[IEEE8021X_KEY_IV_LEN]; /* cryptographically random
						     * number
						     */
	unsigned char key_index;	/*
					 * key flag in the most significant bit:
					 * 0 = broadcast (default key),
					 * 1 = unicast (key mapping key);
					 * key index is in the 7 least
					 * significant bits
					 */
	/*
	 * HMAC-MD5 message integrity check computed with MS-MPPE-Send-Key as
	 * the key
	 */
	unsigned char key_signature[IEEE8021X_KEY_SIGN_LEN];

	/*
	 * followed by key: if packet body length = 44 + key length, then the
	 * key field (of key_length bytes) contains the key in encrypted form;
	 * if packet body length = 44, key field is absent and key_length
	 * represents the number of least significant octets from
	 * MS-MPPE-Send-Key attribute to be used as the keying material;
	 * RC4 key used in encryption = Key-IV + MS-MPPE-Recv-Key
	 */
} __packed;

#define WPA_NONCE_LEN 32
#define WPA_REPLAY_COUNTER_LEN 8

struct wpa_eapol_key {
	unsigned char type;
	unsigned short key_info;
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

#define WPA_KEY_INFO_TYPE_MASK (WBIT(0) | WBIT(1) | WBIT(2))
#define WPA_KEY_INFO_TYPE_HMAC_MD5_RC4 WBIT(0)
#define WPA_KEY_INFO_TYPE_HMAC_SHA1_AES WBIT(1)
#define WPA_KEY_INFO_KEY_TYPE WBIT(3)	/* 1 = Pairwise, 0 = Group key */
/* bit4..5 is used in WPA, but is reserved in IEEE 802.11i/RSN */
#define WPA_KEY_INFO_KEY_INDEX_MASK (WBIT(4) | WBIT(5))
#define WPA_KEY_INFO_KEY_INDEX_SHIFT 4
#define WPA_KEY_INFO_INSTALL WBIT(6)	/* pairwise */
#define WPA_KEY_INFO_TXRX WBIT(6)	/* group */
#define WPA_KEY_INFO_ACK WBIT(7)
#define WPA_KEY_INFO_MIC WBIT(8)
#define WPA_KEY_INFO_SECURE WBIT(9)
#define WPA_KEY_INFO_ERROR WBIT(10)
#define WPA_KEY_INFO_REQUEST WBIT(11)
#define WPA_KEY_INFO_ENCR_KEY_DATA WBIT(12)	/* IEEE 802.11i/RSN only */

#define WPA_CAPABILITY_PREAUTH WBIT(0)

#define GENERIC_INFO_ELEM 0xdd
#define RSN_INFO_ELEM 0x30

enum {
	REASON_UNSPECIFIED = 1,
	REASON_DEAUTH_LEAVING = 3,
	REASON_INVALID_IE = 13,
	REASON_MICHAEL_MIC_FAILURE = 14,
	REASON_4WAY_HANDSHAKE_TIMEOUT = 15,
	REASON_GROUP_KEY_UPDATE_TIMEOUT = 16,
	REASON_IE_IN_4WAY_DIFFERS = 17,
	REASON_GROUP_CIPHER_NOT_VALID = 18,
	REASON_PAIRWISE_CIPHER_NOT_VALID = 19,
	REASON_AKMP_NOT_VALID = 20,
	REASON_UNSUPPORTED_RSN_IE_VERSION = 21,
	REASON_INVALID_RSN_IE_CAPAB = 22,
	REASON_IEEE_802_1X_AUTH_FAILED = 23,
	REASON_CIPHER_SUITE_REJECTED = 24
};

#endif /* EAP_PACKET_H */
