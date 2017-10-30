/*
 * Copyright © 2016 Intel Corporation
 *
 * Authors:
 *    Rafael Antognolli <rafael.antognolli@intel.com>
 *    Scott  Bauer      <scott.bauer@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/types.h>

#ifndef _OPAL_PROTO_H
#define _OPAL_PROTO_H

/*
 * These constant values come from:
 * SPC-4 section
 * 6.30 SECURITY PROTOCOL IN command / table 265.
 */
enum {
	TCG_SECP_00 = 0,
	TCG_SECP_01,
};

/*
 * Token defs derived from:
 * TCG_Storage_Architecture_Core_Spec_v2.01_r1.00
 * 3.2.2 Data Stream Encoding
 */
enum opal_response_token {
	OPAL_DTA_TOKENID_BYTESTRING = 0xe0,
	OPAL_DTA_TOKENID_SINT = 0xe1,
	OPAL_DTA_TOKENID_UINT = 0xe2,
	OPAL_DTA_TOKENID_TOKEN = 0xe3, /* actual token is returned */
	OPAL_DTA_TOKENID_INVALID = 0X0
};

#define DTAERROR_NO_METHOD_STATUS 0x89
#define GENERIC_HOST_SESSION_NUM 0x41

#define TPER_SYNC_SUPPORTED 0x01
#define MBR_ENABLED_MASK 0x10

#define TINY_ATOM_DATA_MASK 0x3F
#define TINY_ATOM_SIGNED 0x40

#define SHORT_ATOM_ID 0x80
#define SHORT_ATOM_BYTESTRING 0x20
#define SHORT_ATOM_SIGNED 0x10
#define SHORT_ATOM_LEN_MASK 0xF

#define MEDIUM_ATOM_ID 0xC0
#define MEDIUM_ATOM_BYTESTRING 0x10
#define MEDIUM_ATOM_SIGNED 0x8
#define MEDIUM_ATOM_LEN_MASK 0x7

#define LONG_ATOM_ID 0xe0
#define LONG_ATOM_BYTESTRING 0x2
#define LONG_ATOM_SIGNED 0x1

/* Derived from TCG Core spec 2.01 Section:
 * 3.2.2.1
 * Data Type
 */
#define TINY_ATOM_BYTE   0x7F
#define SHORT_ATOM_BYTE  0xBF
#define MEDIUM_ATOM_BYTE 0xDF
#define LONG_ATOM_BYTE   0xE3

#define OPAL_INVAL_PARAM 12
#define OPAL_MANUFACTURED_INACTIVE 0x08
#define OPAL_DISCOVERY_COMID 0x0001

#define LOCKING_RANGE_NON_GLOBAL 0x03
/*
 * User IDs used in the TCG storage SSCs
 * Derived from: TCG_Storage_Architecture_Core_Spec_v2.01_r1.00
 * Section: 6.3 Assigned UIDs
 */
#define OPAL_UID_LENGTH 8
#define OPAL_METHOD_LENGTH 8
#define OPAL_MSID_KEYLEN 15
#define OPAL_UID_LENGTH_HALF 4

/* Enum to index OPALUID array */
enum opal_uid {
	/* users */
	OPAL_SMUID_UID,
	OPAL_THISSP_UID,
	OPAL_ADMINSP_UID,
	OPAL_LOCKINGSP_UID,
	OPAL_ENTERPRISE_LOCKINGSP_UID,
	OPAL_ANYBODY_UID,
	OPAL_SID_UID,
	OPAL_ADMIN1_UID,
	OPAL_USER1_UID,
	OPAL_USER2_UID,
	OPAL_PSID_UID,
	OPAL_ENTERPRISE_BANDMASTER0_UID,
	OPAL_ENTERPRISE_ERASEMASTER_UID,
	/* tables */
	OPAL_LOCKINGRANGE_GLOBAL,
	OPAL_LOCKINGRANGE_ACE_RDLOCKED,
	OPAL_LOCKINGRANGE_ACE_WRLOCKED,
	OPAL_MBRCONTROL,
	OPAL_MBR,
	OPAL_AUTHORITY_TABLE,
	OPAL_C_PIN_TABLE,
	OPAL_LOCKING_INFO_TABLE,
	OPAL_ENTERPRISE_LOCKING_INFO_TABLE,
	/* C_PIN_TABLE object ID's */
	OPAL_C_PIN_MSID,
	OPAL_C_PIN_SID,
	OPAL_C_PIN_ADMIN1,
	/* half UID's (only first 4 bytes used) */
	OPAL_HALF_UID_AUTHORITY_OBJ_REF,
	OPAL_HALF_UID_BOOLEAN_ACE,
	/* omitted optional parameter */
	OPAL_UID_HEXFF,
};

#define OPAL_METHOD_LENGTH 8

/* Enum for indexing the OPALMETHOD array */
enum opal_method {
	OPAL_PROPERTIES,
	OPAL_STARTSESSION,
	OPAL_REVERT,
	OPAL_ACTIVATE,
	OPAL_EGET,
	OPAL_ESET,
	OPAL_NEXT,
	OPAL_EAUTHENTICATE,
	OPAL_GETACL,
	OPAL_GENKEY,
	OPAL_REVERTSP,
	OPAL_GET,
	OPAL_SET,
	OPAL_AUTHENTICATE,
	OPAL_RANDOM,
	OPAL_ERASE,
};

enum opal_token {
	/* Boolean */
	OPAL_TRUE = 0x01,
	OPAL_FALSE = 0x00,
	OPAL_BOOLEAN_EXPR = 0x03,
	/* cellblocks */
	OPAL_TABLE = 0x00,
	OPAL_STARTROW = 0x01,
	OPAL_ENDROW = 0x02,
	OPAL_STARTCOLUMN = 0x03,
	OPAL_ENDCOLUMN = 0x04,
	OPAL_VALUES = 0x01,
	/* authority table */
	OPAL_PIN = 0x03,
	/* locking tokens */
	OPAL_RANGESTART = 0x03,
	OPAL_RANGELENGTH = 0x04,
	OPAL_READLOCKENABLED = 0x05,
	OPAL_WRITELOCKENABLED = 0x06,
	OPAL_READLOCKED = 0x07,
	OPAL_WRITELOCKED = 0x08,
	OPAL_ACTIVEKEY = 0x0A,
	/* locking info table */
	OPAL_MAXRANGES = 0x04,
	 /* mbr control */
	OPAL_MBRENABLE = 0x01,
	OPAL_MBRDONE = 0x02,
	/* properties */
	OPAL_HOSTPROPERTIES = 0x00,
	/* atoms */
	OPAL_STARTLIST = 0xf0,
	OPAL_ENDLIST = 0xf1,
	OPAL_STARTNAME = 0xf2,
	OPAL_ENDNAME = 0xf3,
	OPAL_CALL = 0xf8,
	OPAL_ENDOFDATA = 0xf9,
	OPAL_ENDOFSESSION = 0xfa,
	OPAL_STARTTRANSACTON = 0xfb,
	OPAL_ENDTRANSACTON = 0xfC,
	OPAL_EMPTYATOM = 0xff,
	OPAL_WHERE = 0x00,
};

/* Locking state for a locking range */
enum opal_lockingstate {
	OPAL_LOCKING_READWRITE = 0x01,
	OPAL_LOCKING_READONLY = 0x02,
	OPAL_LOCKING_LOCKED = 0x03,
};

/* Packets derived from:
 * TCG_Storage_Architecture_Core_Spec_v2.01_r1.00
 * Secion: 3.2.3 ComPackets, Packets & Subpackets
 */

/* Comm Packet (header) for transmissions. */
struct opal_compacket {
	__be32 reserved0;
	u8 extendedComID[4];
	__be32 outstandingData;
	__be32 minTransfer;
	__be32 length;
};

/* Packet structure. */
struct opal_packet {
	__be32 tsn;
	__be32 hsn;
	__be32 seq_number;
	__be16 reserved0;
	__be16 ack_type;
	__be32 acknowledgment;
	__be32 length;
};

/* Data sub packet header */
struct opal_data_subpacket {
	u8 reserved0[6];
	__be16 kind;
	__be32 length;
};

/* header of a response */
struct opal_header {
	struct opal_compacket cp;
	struct opal_packet pkt;
	struct opal_data_subpacket subpkt;
};

#define FC_TPER       0x0001
#define FC_LOCKING    0x0002
#define FC_GEOMETRY   0x0003
#define FC_ENTERPRISE 0x0100
#define FC_DATASTORE  0x0202
#define FC_SINGLEUSER 0x0201
#define FC_OPALV100   0x0200
#define FC_OPALV200   0x0203

/*
 * The Discovery 0 Header. As defined in
 * Opal SSC Documentation
 * Section: 3.3.5 Capability Discovery
 */
struct d0_header {
	__be32 length; /* the length of the header 48 in 2.00.100 */
	__be32 revision; /**< revision of the header 1 in 2.00.100 */
	__be32 reserved01;
	__be32 reserved02;
	/*
	 * the remainder of the structure is vendor specific and will not be
	 * addressed now
	 */
	u8 ignored[32];
};

/*
 * TPer Feature Descriptor. Contains flags indicating support for the
 * TPer features described in the OPAL specification. The names match the
 * OPAL terminology
 *
 * code == 0x001 in 2.00.100
 */
struct d0_tper_features {
	/*
	 * supported_features bits:
	 * bit 7: reserved
	 * bit 6: com ID management
	 * bit 5: reserved
	 * bit 4: streaming support
	 * bit 3: buffer management
	 * bit 2: ACK/NACK
	 * bit 1: async
	 * bit 0: sync
	 */
	u8 supported_features;
	/*
	 * bytes 5 through 15 are reserved, but we represent the first 3 as
	 * u8 to keep the other two 32bits integers aligned.
	 */
	u8 reserved01[3];
	__be32 reserved02;
	__be32 reserved03;
};

/*
 * Locking Feature Descriptor. Contains flags indicating support for the
 * locking features described in the OPAL specification. The names match the
 * OPAL terminology
 *
 * code == 0x0002 in 2.00.100
 */
struct d0_locking_features {
	/*
	 * supported_features bits:
	 * bits 6-7: reserved
	 * bit 5: MBR done
	 * bit 4: MBR enabled
	 * bit 3: media encryption
	 * bit 2: locked
	 * bit 1: locking enabled
	 * bit 0: locking supported
	 */
	u8 supported_features;
	/*
	 * bytes 5 through 15 are reserved, but we represent the first 3 as
	 * u8 to keep the other two 32bits integers aligned.
	 */
	u8 reserved01[3];
	__be32 reserved02;
	__be32 reserved03;
};

/*
 * Geometry Feature Descriptor. Contains flags indicating support for the
 * geometry features described in the OPAL specification. The names match the
 * OPAL terminology
 *
 * code == 0x0003 in 2.00.100
 */
struct d0_geometry_features {
	/*
	 * skip 32 bits from header, needed to align the struct to 64 bits.
	 */
	u8 header[4];
	/*
	 * reserved01:
	 * bits 1-6: reserved
	 * bit 0: align
	 */
	u8 reserved01;
	u8 reserved02[7];
	__be32 logical_block_size;
	__be64 alignment_granularity;
	__be64 lowest_aligned_lba;
};

/*
 * Enterprise SSC Feature
 *
 * code == 0x0100
 */
struct d0_enterprise_ssc {
	__be16 baseComID;
	__be16 numComIDs;
	/* range_crossing:
	 * bits 1-6: reserved
	 * bit 0: range crossing
	 */
	u8 range_crossing;
	u8 reserved01;
	__be16 reserved02;
	__be32 reserved03;
	__be32 reserved04;
};

/*
 * Opal V1 feature
 *
 * code == 0x0200
 */
struct d0_opal_v100 {
	__be16 baseComID;
	__be16 numComIDs;
};

/*
 * Single User Mode feature
 *
 * code == 0x0201
 */
struct d0_single_user_mode {
	__be32 num_locking_objects;
	/* reserved01:
	 * bit 0: any
	 * bit 1: all
	 * bit 2: policy
	 * bits 3-7: reserved
	 */
	u8 reserved01;
	u8 reserved02;
	__be16 reserved03;
	__be32 reserved04;
};

/*
 * Additonal Datastores feature
 *
 * code == 0x0202
 */
struct d0_datastore_table {
	__be16 reserved01;
	__be16 max_tables;
	__be32 max_size_tables;
	__be32 table_size_alignment;
};

/*
 * OPAL 2.0 feature
 *
 * code == 0x0203
 */
struct d0_opal_v200 {
	__be16 baseComID;
	__be16 numComIDs;
	/* range_crossing:
	 * bits 1-6: reserved
	 * bit 0: range crossing
	 */
	u8 range_crossing;
	/* num_locking_admin_auth:
	 * not aligned to 16 bits, so use two u8.
	 * stored in big endian:
	 * 0: MSB
	 * 1: LSB
	 */
	u8 num_locking_admin_auth[2];
	/* num_locking_user_auth:
	 * not aligned to 16 bits, so use two u8.
	 * stored in big endian:
	 * 0: MSB
	 * 1: LSB
	 */
	u8 num_locking_user_auth[2];
	u8 initialPIN;
	u8 revertedPIN;
	u8 reserved01;
	__be32 reserved02;
};

/* Union of features used to parse the discovery 0 response */
struct d0_features {
	__be16 code;
	/*
	 * r_version bits:
	 * bits 4-7: version
	 * bits 0-3: reserved
	 */
	u8 r_version;
	u8 length;
	u8 features[];
};

#endif /* _OPAL_PROTO_H */
