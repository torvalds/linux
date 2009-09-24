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
#ifndef __BFA_DEFS_MFG_H__
#define __BFA_DEFS_MFG_H__

#include <bfa_os_inc.h>

/**
 * Manufacturing block version
 */
#define BFA_MFG_VERSION				1

/**
 * Manufacturing block format
 */
#define BFA_MFG_SERIALNUM_SIZE			11
#define BFA_MFG_PARTNUM_SIZE			14
#define BFA_MFG_SUPPLIER_ID_SIZE		10
#define BFA_MFG_SUPPLIER_PARTNUM_SIZE	20
#define BFA_MFG_SUPPLIER_SERIALNUM_SIZE	20
#define BFA_MFG_SUPPLIER_REVISION_SIZE	4
#define STRSZ(_n)	(((_n) + 4) & ~3)

/**
 * VPD data length
 */
#define BFA_MFG_VPD_LEN     256

/**
 * All numerical fields are in big-endian format.
 */
struct bfa_mfg_vpd_s {
    u8     version;    /*  vpd data version */
    u8     vpd_sig[3]; /*  characters 'V', 'P', 'D' */
    u8     chksum;     /*  u8 checksum */
    u8     vendor;     /*  vendor */
    u8     len;        /*  vpd data length excluding header */
    u8     rsv;
    u8     data[BFA_MFG_VPD_LEN];  /*  vpd data */
};

#pragma pack(1)

#endif /* __BFA_DEFS_MFG_H__ */
