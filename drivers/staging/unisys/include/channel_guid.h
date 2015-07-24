/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 * CHANNEL Guids
 */

/* {414815ed-c58c-11da-95a9-00e08161165f} */
#define SPAR_VHBA_CHANNEL_PROTOCOL_UUID \
		UUID_LE(0x414815ed, 0xc58c, 0x11da, \
				0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)
static const uuid_le spar_vhba_channel_protocol_uuid =
	SPAR_VHBA_CHANNEL_PROTOCOL_UUID;
#define SPAR_VHBA_CHANNEL_PROTOCOL_UUID_STR \
	"414815ed-c58c-11da-95a9-00e08161165f"

/* {8cd5994d-c58e-11da-95a9-00e08161165f} */
#define SPAR_VNIC_CHANNEL_PROTOCOL_UUID \
		UUID_LE(0x8cd5994d, 0xc58e, 0x11da, \
				0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)
static const uuid_le spar_vnic_channel_protocol_uuid =
	SPAR_VNIC_CHANNEL_PROTOCOL_UUID;
#define SPAR_VNIC_CHANNEL_PROTOCOL_UUID_STR \
	"8cd5994d-c58e-11da-95a9-00e08161165f"

/* {72120008-4AAB-11DC-8530-444553544200} */
#define SPAR_SIOVM_UUID \
		UUID_LE(0x72120008, 0x4AAB, 0x11DC, \
				0x85, 0x30, 0x44, 0x45, 0x53, 0x54, 0x42, 0x00)
static const uuid_le spar_siovm_uuid = SPAR_SIOVM_UUID;

/* {5b52c5ac-e5f5-4d42-8dff-429eaecd221f} */
#define SPAR_CONTROLDIRECTOR_CHANNEL_PROTOCOL_UUID  \
		UUID_LE(0x5b52c5ac, 0xe5f5, 0x4d42, \
				0x8d, 0xff, 0x42, 0x9e, 0xae, 0xcd, 0x22, 0x1f)

static const uuid_le spar_controldirector_channel_protocol_uuid =
	SPAR_CONTROLDIRECTOR_CHANNEL_PROTOCOL_UUID;

/* {b4e79625-aede-4eAA-9e11-D3eddcd4504c} */
#define SPAR_DIAG_POOL_CHANNEL_PROTOCOL_UUID				\
		UUID_LE(0xb4e79625, 0xaede, 0x4eaa, \
				0x9e, 0x11, 0xd3, 0xed, 0xdc, 0xd4, 0x50, 0x4c)
