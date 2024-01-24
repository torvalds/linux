/*
 * TC956X ethernet driver.
 *
 * tc956x_vf_mbx.h
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  10 Jul 2020 : Initial Version
 *  VERSION     : 00-01
 *
 *  30 Nov 2021 : Base lined for SRIOV
 *  VERSION     : 01-02
 */

#ifndef __TC956X_VF_MBX_H__
#define __TC956X_VF_MBX_H__

#include "common.h"
#include <linux/netdevice.h>

#define VF_READ_RQST_OFST 0 /* VF to read request of PF */
#define VF_SEND_ACK_OFST 1  /* VF to send ACK to PF */

#define VF_SEND_RQST_OFST 1 /* VF to send request to PF */
#define VF_READ_ACK_OFST 0  /* VF to read ACK from PF */

#define PFS_MAX 4  /* 2 PF + 2 MCU->VF0x, MCU->VF1x */
#define VFNS_MAX 3 /* 3 VFs */

#define VF_MBX_SRAM_ADDR 0x47060 /*0x20007060*/
#endif /* __TC956X_VF_MBX_H__ */
