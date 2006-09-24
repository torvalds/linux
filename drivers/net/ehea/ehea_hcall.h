/*
 *  linux/drivers/net/ehea/ehea_hcall.h
 *
 *  eHEA ethernet device driver for IBM eServer System p
 *
 *  (C) Copyright IBM Corp. 2006
 *
 *  Authors:
 *       Christoph Raisch <raisch@de.ibm.com>
 *       Jan-Bernd Themann <themann@de.ibm.com>
 *       Thomas Klein <tklein@de.ibm.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __EHEA_HCALL_H__
#define __EHEA_HCALL_H__

/**
 * This file contains HCALL defines that are to be included in the appropriate
 * kernel files later
 */

#define H_ALLOC_HEA_RESOURCE   0x278
#define H_MODIFY_HEA_QP        0x250
#define H_QUERY_HEA_QP         0x254
#define H_QUERY_HEA            0x258
#define H_QUERY_HEA_PORT       0x25C
#define H_MODIFY_HEA_PORT      0x260
#define H_REG_BCMC             0x264
#define H_DEREG_BCMC           0x268
#define H_REGISTER_HEA_RPAGES  0x26C
#define H_DISABLE_AND_GET_HEA  0x270
#define H_GET_HEA_INFO         0x274
#define H_ADD_CONN             0x284
#define H_DEL_CONN             0x288

#endif	/* __EHEA_HCALL_H__ */
