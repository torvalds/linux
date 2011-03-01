/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-version.h: Driver for Exar Corp's X3100 Series 10GbE PCIe I/O
 *                 Virtualized Server Adapter.
 * Copyright(c) 2002-2010 Exar Corp.
 ******************************************************************************/
#ifndef VXGE_VERSION_H
#define VXGE_VERSION_H

#define VXGE_VERSION_MAJOR	"2"
#define VXGE_VERSION_MINOR	"5"
#define VXGE_VERSION_FIX	"1"
#define VXGE_VERSION_BUILD	"22082"
#define VXGE_VERSION_FOR	"k"

#define VXGE_FW_VER(maj, min, bld) (((maj) << 16) + ((min) << 8) + (bld))

#define VXGE_DEAD_FW_VER_MAJOR	1
#define VXGE_DEAD_FW_VER_MINOR	4
#define VXGE_DEAD_FW_VER_BUILD	4

#define VXGE_FW_DEAD_VER VXGE_FW_VER(VXGE_DEAD_FW_VER_MAJOR, \
				     VXGE_DEAD_FW_VER_MINOR, \
				     VXGE_DEAD_FW_VER_BUILD)

#define VXGE_EPROM_FW_VER_MAJOR	1
#define VXGE_EPROM_FW_VER_MINOR	6
#define VXGE_EPROM_FW_VER_BUILD	1

#define VXGE_EPROM_FW_VER VXGE_FW_VER(VXGE_EPROM_FW_VER_MAJOR, \
				      VXGE_EPROM_FW_VER_MINOR, \
				      VXGE_EPROM_FW_VER_BUILD)

#define VXGE_CERT_FW_VER_MAJOR	1
#define VXGE_CERT_FW_VER_MINOR	8
#define VXGE_CERT_FW_VER_BUILD	1

#define VXGE_CERT_FW_VER VXGE_FW_VER(VXGE_CERT_FW_VER_MAJOR, \
				     VXGE_CERT_FW_VER_MINOR, \
				     VXGE_CERT_FW_VER_BUILD)

#endif
