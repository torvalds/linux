/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __FDMI_H__
#define __FDMI_H__

#include <protocol/types.h>
#include <protocol/fc.h>
#include <protocol/ct.h>

#pragma pack(1)

/*
 * FDMI Command Codes
 */
#define	FDMI_GRHL		0x0100
#define	FDMI_GHAT		0x0101
#define	FDMI_GRPL		0x0102
#define	FDMI_GPAT		0x0110
#define	FDMI_RHBA		0x0200
#define	FDMI_RHAT		0x0201
#define	FDMI_RPRT		0x0210
#define	FDMI_RPA		0x0211
#define	FDMI_DHBA		0x0300
#define	FDMI_DPRT		0x0310

/*
 * FDMI reason codes
 */
#define	FDMI_NO_ADDITIONAL_EXP		0x00
#define	FDMI_HBA_ALREADY_REG		0x10
#define	FDMI_HBA_ATTRIB_NOT_REG		0x11
#define	FDMI_HBA_ATTRIB_MULTIPLE	0x12
#define	FDMI_HBA_ATTRIB_LENGTH_INVALID	0x13
#define	FDMI_HBA_ATTRIB_NOT_PRESENT	0x14
#define	FDMI_PORT_ORIG_NOT_IN_LIST	0x15
#define	FDMI_PORT_HBA_NOT_IN_LIST	0x16
#define	FDMI_PORT_ATTRIB_NOT_REG	0x20
#define	FDMI_PORT_NOT_REG		0x21
#define	FDMI_PORT_ATTRIB_MULTIPLE	0x22
#define	FDMI_PORT_ATTRIB_LENGTH_INVALID	0x23
#define	FDMI_PORT_ALREADY_REGISTEREED	0x24

/*
 * FDMI Transmission Speed Mask values
 */
#define	FDMI_TRANS_SPEED_1G		0x00000001
#define	FDMI_TRANS_SPEED_2G		0x00000002
#define	FDMI_TRANS_SPEED_10G		0x00000004
#define	FDMI_TRANS_SPEED_4G		0x00000008
#define	FDMI_TRANS_SPEED_8G		0x00000010
#define	FDMI_TRANS_SPEED_16G		0x00000020
#define	FDMI_TRANS_SPEED_UNKNOWN	0x00008000

/*
 * FDMI HBA attribute types
 */
enum fdmi_hba_attribute_type {
	FDMI_HBA_ATTRIB_NODENAME = 1,	/* 0x0001 */
	FDMI_HBA_ATTRIB_MANUFACTURER,	/* 0x0002 */
	FDMI_HBA_ATTRIB_SERIALNUM,	/* 0x0003 */
	FDMI_HBA_ATTRIB_MODEL,		/* 0x0004 */
	FDMI_HBA_ATTRIB_MODEL_DESC,	/* 0x0005 */
	FDMI_HBA_ATTRIB_HW_VERSION,	/* 0x0006 */
	FDMI_HBA_ATTRIB_DRIVER_VERSION,	/* 0x0007 */
	FDMI_HBA_ATTRIB_ROM_VERSION,	/* 0x0008 */
	FDMI_HBA_ATTRIB_FW_VERSION,	/* 0x0009 */
	FDMI_HBA_ATTRIB_OS_NAME,	/* 0x000A */
	FDMI_HBA_ATTRIB_MAX_CT,		/* 0x000B */

	FDMI_HBA_ATTRIB_MAX_TYPE
};

/*
 * FDMI Port attribute types
 */
enum fdmi_port_attribute_type {
	FDMI_PORT_ATTRIB_FC4_TYPES = 1,	/* 0x0001 */
	FDMI_PORT_ATTRIB_SUPP_SPEED,	/* 0x0002 */
	FDMI_PORT_ATTRIB_PORT_SPEED,	/* 0x0003 */
	FDMI_PORT_ATTRIB_FRAME_SIZE,	/* 0x0004 */
	FDMI_PORT_ATTRIB_DEV_NAME,	/* 0x0005 */
	FDMI_PORT_ATTRIB_HOST_NAME,	/* 0x0006 */

	FDMI_PORT_ATTR_MAX_TYPE
};

/*
 * FDMI attribute
 */
struct fdmi_attr_s {
	u16        type;
	u16        len;
	u8         value[1];
};

/*
 * HBA Attribute Block
 */
struct fdmi_hba_attr_s {
	u32        attr_count;	/* # of attributes */
	struct fdmi_attr_s     hba_attr;	/* n attributes */
};

/*
 * Registered Port List
 */
struct fdmi_port_list_s {
	u32        num_ports;	/* number Of Port Entries */
	wwn_t           port_entry;	/* one or more */
};

/*
 * Port Attribute Block
 */
struct fdmi_port_attr_s {
	u32        attr_count;	/* # of attributes */
	struct fdmi_attr_s     port_attr;	/* n attributes */
};

/*
 * FDMI Register HBA Attributes
 */
struct fdmi_rhba_s {
	wwn_t           hba_id;		/* HBA Identifier */
	struct fdmi_port_list_s port_list;	/* Registered Port List */
	struct fdmi_hba_attr_s hba_attr_blk;	/* HBA attribute block */
};

/*
 * FDMI Register Port
 */
struct fdmi_rprt_s {
	wwn_t           hba_id;		/* HBA Identifier */
	wwn_t           port_name;	/* Port wwn */
	struct fdmi_port_attr_s port_attr_blk;	/* Port Attr Block */
};

/*
 * FDMI Register Port Attributes
 */
struct fdmi_rpa_s {
	wwn_t           port_name;	/* port wwn */
	struct fdmi_port_attr_s port_attr_blk;	/* Port Attr Block */
};

#pragma pack()

#endif
