/*
 * IEEE 802.1X-2010 Key Agree Protocol of PAE state machine
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IEEE802_1X_KAY_I_H
#define IEEE802_1X_KAY_I_H

#include "utils/list.h"
#include "common/defs.h"
#include "common/ieee802_1x_defs.h"

#define MKA_VERSION_ID              1

/* IEEE Std 802.1X-2010, 11.11.1, Table 11-7 */
enum mka_packet_type {
	MKA_BASIC_PARAMETER_SET = MKA_VERSION_ID,
	MKA_LIVE_PEER_LIST = 1,
	MKA_POTENTIAL_PEER_LIST = 2,
	MKA_SAK_USE = 3,
	MKA_DISTRIBUTED_SAK = 4,
	MKA_DISTRIBUTED_CAK = 5,
	MKA_KMD = 6,
	MKA_ANNOUNCEMENT = 7,
	MKA_ICV_INDICATOR = 255
};

#define ICV_LEN                         16  /* 16 bytes */
#define SAK_WRAPPED_LEN                 24
/* KN + Wrapper SAK */
#define DEFAULT_DIS_SAK_BODY_LENGTH     (SAK_WRAPPED_LEN + 4)
#define MAX_RETRY_CNT                   5

struct ieee802_1x_kay;

struct ieee802_1x_mka_peer_id {
	u8 mi[MI_LEN];
	be32 mn;
};

struct ieee802_1x_kay_peer {
	struct ieee802_1x_mka_sci sci;
	u8 mi[MI_LEN];
	u32 mn;
	time_t expire;
	Boolean is_key_server;
	u8 key_server_priority;
	Boolean macsec_desired;
	enum macsec_cap macsec_capability;
	Boolean sak_used;
	struct dl_list list;
};

struct macsec_ciphersuite {
	u64 id;
	char name[32];
	enum macsec_cap capable;
	int sak_len; /* unit: byte */

	u32 index;
};

struct mka_alg {
	u8 parameter[4];
	size_t cak_len;
	size_t kek_len;
	size_t ick_len;
	size_t icv_len;

	int (*cak_trfm)(const u8 *msk, const u8 *mac1, const u8 *mac2, u8 *cak);
	int (*ckn_trfm)(const u8 *msk, const u8 *mac1, const u8 *mac2,
			const u8 *sid, size_t sid_len, u8 *ckn);
	int (*kek_trfm)(const u8 *cak, const u8 *ckn, size_t ckn_len, u8 *kek);
	int (*ick_trfm)(const u8 *cak, const u8 *ckn, size_t ckn_len, u8 *ick);
	int (*icv_hash)(const u8 *ick, const u8 *msg, size_t msg_len, u8 *icv);

	int index; /* index for configuring */
};

#define DEFAULT_MKA_ALG_INDEX 0

/* See IEEE Std 802.1X-2010, 9.16 MKA management */
struct ieee802_1x_mka_participant {
	/* used for active and potential participant */
	struct mka_key_name ckn;
	struct mka_key cak;
	Boolean cached;

	/* used by management to monitor and control activation */
	Boolean active;
	Boolean participant;
	Boolean retain;
	enum mka_created_mode mode;

	enum { DEFAULT, DISABLED, ON_OPER_UP, ALWAYS } activate;

	/* used for active participant */
	Boolean principal;
	struct dl_list live_peers;
	struct dl_list potential_peers;

	/* not defined in IEEE 802.1X */
	struct dl_list list;

	struct mka_key kek;
	struct mka_key ick;

	struct ieee802_1x_mka_ki lki;
	u8 lan;
	Boolean ltx;
	Boolean lrx;

	struct ieee802_1x_mka_ki oki;
	u8 oan;
	Boolean otx;
	Boolean orx;

	Boolean is_key_server;
	Boolean is_obliged_key_server;
	Boolean can_be_key_server;
	Boolean is_elected;

	struct dl_list sak_list;
	struct dl_list rxsc_list;

	struct transmit_sc *txsc;

	u8 mi[MI_LEN];
	u32 mn;

	struct ieee802_1x_mka_peer_id current_peer_id;
	struct ieee802_1x_mka_sci current_peer_sci;
	time_t cak_life;
	time_t mka_life;
	Boolean to_dist_sak;
	Boolean to_use_sak;
	Boolean new_sak;

	Boolean advised_desired;
	enum macsec_cap advised_capability;

	struct data_key *new_key;
	u32 retry_count;

	struct ieee802_1x_kay *kay;
};

struct ieee802_1x_mka_hdr {
	/* octet 1 */
	u8 type;
	/* octet 2 */
	u8 reserve;
	/* octet 3 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u8 length:4;
	u8 reserve1:4;
#elif __BYTE_ORDER == __BIG_ENDIAN
	u8 reserve1:4;
	u8 length:4;
#else
#error "Please fix <bits/endian.h>"
#endif
	/* octet 4 */
	u8 length1;
};

#define MKA_HDR_LEN sizeof(struct ieee802_1x_mka_hdr)

/**
 * struct ieee802_1x_mka_basic_body - Basic Parameter Set (Figure 11-8)
 * @version: MKA Version Identifier
 * @priority: Key Server Priority
 * @length: Parameter set body length
 * @macsec_capability: MACsec capability, as defined in ieee802_1x_defs.h
 * @macsec_desired: the participant wants MACsec to be used to protect frames
 *	(9.6.1)
 * @key_server: the participant has not decided that another participant is or
 *	will be the key server (9.5.1)
 * @length1: Parameter set body length (cont)
 * @actor_mi: Actor's Member Identifier
 * @actor_mn: Actor's Message Number
 * @algo_agility: Algorithm Agility parameter
 * @ckn: CAK Name
 */
struct ieee802_1x_mka_basic_body {
	/* octet 1 */
	u8 version;
	/* octet 2 */
	u8 priority;
	/* octet 3 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u8 length:4;
	u8 macsec_capability:2;
	u8 macsec_desired:1;
	u8 key_server:1;
#elif __BYTE_ORDER == __BIG_ENDIAN
	u8 key_server:1;
	u8 macsec_desired:1;
	u8 macsec_capability:2;
	u8 length:4;
#endif
	/* octet 4 */
	u8 length1;

	struct ieee802_1x_mka_sci actor_sci;
	u8 actor_mi[MI_LEN];
	be32 actor_mn;
	u8 algo_agility[4];

	/* followed by CAK Name*/
	u8 ckn[0];
};

/**
 * struct ieee802_1x_mka_peer_body - Live Peer List and Potential Peer List
 *	parameter sets (Figure 11-9)
 * @type: Parameter set type (1 or 2)
 * @length: Parameter set body length
 * @length1: Parameter set body length (cont)
 * @peer: array of (MI, MN) pairs
 */
struct ieee802_1x_mka_peer_body {
	/* octet 1 */
	u8 type;
	/* octet 2 */
	u8 reserve;
	/* octet 3 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u8 length:4;
	u8 reserve1:4;
#elif __BYTE_ORDER == __BIG_ENDIAN
	u8 reserve1:4;
	u8 length:4;
#endif
	/* octet 4 */
	u8 length1;

	u8 peer[0];
	/* followed by Peers */
};

/**
 * struct ieee802_1x_mka_sak_use_body - MACsec SAK Use parameter set (Figure
 *	11-10)
 * @type: MKA message type
 * @lan: latest key AN
 * @ltx: latest key TX
 * @lrx: latest key RX
 * @oan: old key AN
 * @otx: old key TX
 * @orx: old key RX
 * @ptx: plain TX, ie protectFrames is False
 * @prx: plain RX, ie validateFrames is not Strict
 * @delay_protect: True if LPNs are being reported sufficiently frequently to
 *	allow the recipient to provide data delay protection. If False, the LPN
 *	can be reported as zero.
 * @lsrv_mi: latest key server MI
 * @lkn: latest key number (together with MI, form the KI)
 * @llpn: latest lowest acceptable PN (LPN)
 * @osrv_mi: old key server MI
 * @okn: old key number (together with MI, form the KI)
 * @olpn: old lowest acceptable PN (LPN)
 */
struct ieee802_1x_mka_sak_use_body {
	/* octet 1 */
	u8 type;
	/* octet 2 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u8 orx:1;
	u8 otx:1;
	u8 oan:2;
	u8 lrx:1;
	u8 ltx:1;
	u8 lan:2;
#elif __BYTE_ORDER == __BIG_ENDIAN
	u8 lan:2;
	u8 ltx:1;
	u8 lrx:1;
	u8 oan:2;
	u8 otx:1;
	u8 orx:1;
#endif

	/* octet 3 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u8 length:4;
	u8 delay_protect:1;
	u8 reserve:1;
	u8 prx:1;
	u8 ptx:1;
#elif __BYTE_ORDER == __BIG_ENDIAN
	u8 ptx:1;
	u8 prx:1;
	u8 reserve:1;
	u8 delay_protect:1;
	u8 length:4;
#endif

	/* octet 4 */
	u8 length1;

	/* octet 5 - 16 */
	u8 lsrv_mi[MI_LEN];
	/* octet 17 - 20 */
	be32 lkn;
	/* octet 21 - 24 */
	be32 llpn;

	/* octet 25 - 36 */
	u8 osrv_mi[MI_LEN];
	/* octet 37 - 40 */
	be32 okn;
	/* octet 41 - 44 */
	be32 olpn;
};

/**
 * struct ieee802_1x_mka_dist_sak_body - Distributed SAK parameter set
 *	(GCM-AES-128, Figure 11-11)
 * @type: Parameter set type (4)
 * @length: Parameter set body length
 * @length1: Parameter set body length (cont)
 *           Total parameter body length values:
 *            -  0 for plain text
 *            - 28 for GCM-AES-128
 *            - 36 or more for other cipher suites
 * @confid_offset: confidentiality offset, as defined in ieee802_1x_defs.h
 * @dan: distributed AN (0 for plain text)
 * @kn: Key Number
 * @sak: AES Key Wrap of SAK (see 9.8)
 */
struct ieee802_1x_mka_dist_sak_body {
	/* octet 1 */
	u8 type;
	/* octet 2 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u8 reserve:4;
	u8 confid_offset:2;
	u8 dan:2;
#elif __BYTE_ORDER == __BIG_ENDIAN
	u8 dan:2;
	u8 confid_offset:2;
	u8 reserve:4;
#endif
	/* octet 3 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u8 length:4;
	u8 reserve1:4;
#elif __BYTE_ORDER == __BIG_ENDIAN
	u8 reserve1:4;
	u8 length:4;
#endif
	/* octet 4 */
	u8 length1;
	/* octet 5 - 8 */
	be32 kn;

	/* for GCM-AES-128: octet 9-32: SAK
	 * for other cipher suite: octet 9-16: cipher suite id, octet 17-: SAK
	 */
	u8 sak[0];
};

/**
 * struct ieee802_1x_mka_dist_cak_body - Distributed CAK parameter set (Figure
 *	11-13)
 * @type: Parameter set type (5)
 * @length: Parameter set body length
 * @length1: Parameter set body length (cont)
 *           Total parameter body length values:
 *            -  0 for plain text
 *            - 28 for GCM-AES-128
 *            - 36 or more for other cipher suites
 * @cak: AES Key Wrap of CAK (see 9.8)
 * @ckn: CAK Name
 */
struct ieee802_1x_mka_dist_cak_body {
	/* octet 1 */
	u8 type;
	/* octet 2 */
	u8 reserve;
	/* octet 3 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u8 length:4;
	u8 reserve1:4;
#elif __BYTE_ORDER == __BIG_ENDIAN
	u8 reserve1:4;
	u8 length:4;
#endif
	/* octet 4 */
	u8 length1;

	/* octet 5 - 28 */
	u8 cak[24];

	/* followed by CAK Name, 29- */
	u8 ckn[0];
};

struct ieee802_1x_mka_icv_body {
	/* octet 1 */
	u8 type;
	/* octet 2 */
	u8 reserve;
	/* octet 3 */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u8 length:4;
	u8 reserve1:4;
#elif __BYTE_ORDER == __BIG_ENDIAN
	u8 reserve1:4;
	u8 length:4;
#endif
	/* octet 4 */
	u8 length1;

	/* octet 5 - */
	u8 icv[0];
};

#endif /* IEEE802_1X_KAY_I_H */
