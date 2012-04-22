/*
 *
 *  sep_driver_hw_defs.h - Security Processor Driver hardware definitions
 *
 *  Copyright(c) 2009-2011 Intel Corporation. All rights reserved.
 *  Contributions(c) 2009-2011 Discretix. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  CONTACTS:
 *
 *  Mark Allyn		mark.a.allyn@intel.com
 *  Jayant Mangalampalli jayant.mangalampalli@intel.com
 *
 *  CHANGES:
 *
 *  2010.09.20	Upgrade to Medfield
 *  2011.02.22  Enable kernel crypto
 *
 */

#ifndef SEP_DRIVER_HW_DEFS__H
#define SEP_DRIVER_HW_DEFS__H

/*----------------------- */
/* HW Registers Defines.  */
/*                        */
/*---------------------- -*/


/* cf registers */
#define		HW_HOST_IRR_REG_ADDR			0x0A00UL
#define		HW_HOST_IMR_REG_ADDR			0x0A04UL
#define		HW_HOST_ICR_REG_ADDR			0x0A08UL
#define		HW_HOST_SEP_HOST_GPR0_REG_ADDR		0x0B00UL
#define		HW_HOST_SEP_HOST_GPR1_REG_ADDR		0x0B04UL
#define		HW_HOST_SEP_HOST_GPR2_REG_ADDR		0x0B08UL
#define		HW_HOST_SEP_HOST_GPR3_REG_ADDR		0x0B0CUL
#define		HW_HOST_HOST_SEP_GPR0_REG_ADDR		0x0B80UL
#define		HW_HOST_HOST_SEP_GPR1_REG_ADDR		0x0B84UL
#define		HW_HOST_HOST_SEP_GPR2_REG_ADDR		0x0B88UL
#define		HW_HOST_HOST_SEP_GPR3_REG_ADDR		0x0B8CUL
#define		HW_SRAM_DATA_READY_REG_ADDR		0x0F08UL

#endif		/* ifndef HW_DEFS */
