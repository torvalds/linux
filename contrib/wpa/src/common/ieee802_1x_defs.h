/*
 * IEEE Std 802.1X-2010 definitions
 * Copyright (c) 2013-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IEEE802_1X_DEFS_H
#define IEEE802_1X_DEFS_H

#define CS_ID_LEN		8
#define CS_ID_GCM_AES_128	0x0080020001000001ULL
#define CS_NAME_GCM_AES_128	"GCM-AES-128"
#define CS_ID_GCM_AES_256	0x0080c20001000002ULL
#define CS_NAME_GCM_AES_256	"GCM-AES-256"

enum macsec_policy {
	/**
	 * Should secure sessions.
	 * This accepts key server's advice to determine whether to secure the
	 * session or not.
	 */
	SHOULD_SECURE,

	/**
	 * Disabled MACsec - do not secure sessions.
	 */
	DO_NOT_SECURE,

	/**
	 * Should secure sessions, and try to use encryption.
	 * Like @SHOULD_SECURE, this follows the key server's decision.
	 */
	SHOULD_ENCRYPT,
};


/* IEEE Std 802.1X-2010 - Table 11-6 - MACsec Capability */
enum macsec_cap {
	/**
	 * MACsec is not implemented
	 */
	MACSEC_CAP_NOT_IMPLEMENTED,

	/**
	 * 'Integrity without confidentiality'
	 */
	MACSEC_CAP_INTEGRITY,

	/**
	 * 'Integrity without confidentiality' and
	 * 'Integrity and confidentiality' with a confidentiality offset of 0
	 */
	MACSEC_CAP_INTEG_AND_CONF,

	/**
	 * 'Integrity without confidentiality' and
	 * 'Integrity and confidentiality' with a confidentiality offset of 0,
	 * 30, 50
	 */
	MACSEC_CAP_INTEG_AND_CONF_0_30_50,
};

enum validate_frames {
	Disabled,
	Checked,
	Strict,
};

/* IEEE Std 802.1X-2010 - Table 11-6 - Confidentiality Offset */
enum confidentiality_offset {
	CONFIDENTIALITY_NONE      = 0,
	CONFIDENTIALITY_OFFSET_0  = 1,
	CONFIDENTIALITY_OFFSET_30 = 2,
	CONFIDENTIALITY_OFFSET_50 = 3,
};

/* IEEE Std 802.1X-2010 - Table 9-2 */
#define DEFAULT_PRIO_INFRA_PORT        0x10
#define DEFAULT_PRIO_PRIMRAY_AP        0x30
#define DEFAULT_PRIO_SECONDARY_AP      0x50
#define DEFAULT_PRIO_GROUP_CA_MEMBER   0x70
#define DEFAULT_PRIO_NOT_KEY_SERVER    0xFF

#endif /* IEEE802_1X_DEFS_H */
