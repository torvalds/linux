/*
 * Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#ifndef _IOAT_REGISTERS_H_
#define _IOAT_REGISTERS_H_


/* MMIO Device Registers */
#define IOAT_CHANCNT_OFFSET			0x00	/*  8-bit */

#define IOAT_XFERCAP_OFFSET			0x01	/*  8-bit */
#define IOAT_XFERCAP_4KB			12
#define IOAT_XFERCAP_8KB			13
#define IOAT_XFERCAP_16KB			14
#define IOAT_XFERCAP_32KB			15
#define IOAT_XFERCAP_32GB			0

#define IOAT_GENCTRL_OFFSET			0x02	/*  8-bit */
#define IOAT_GENCTRL_DEBUG_EN			0x01

#define IOAT_INTRCTRL_OFFSET			0x03	/*  8-bit */
#define IOAT_INTRCTRL_MASTER_INT_EN		0x01	/* Master Interrupt Enable */
#define IOAT_INTRCTRL_INT_STATUS		0x02	/* ATTNSTATUS -or- Channel Int */
#define IOAT_INTRCTRL_INT			0x04	/* INT_STATUS -and- MASTER_INT_EN */

#define IOAT_ATTNSTATUS_OFFSET			0x04	/* Each bit is a channel */

#define IOAT_VER_OFFSET				0x08	/*  8-bit */
#define IOAT_VER_MAJOR_MASK			0xF0
#define IOAT_VER_MINOR_MASK			0x0F
#define GET_IOAT_VER_MAJOR(x)			((x) & IOAT_VER_MAJOR_MASK)
#define GET_IOAT_VER_MINOR(x)			((x) & IOAT_VER_MINOR_MASK)

#define IOAT_PERPORTOFFSET_OFFSET		0x0A	/* 16-bit */

#define IOAT_INTRDELAY_OFFSET			0x0C	/* 16-bit */
#define IOAT_INTRDELAY_INT_DELAY_MASK		0x3FFF	/* Interrupt Delay Time */
#define IOAT_INTRDELAY_COALESE_SUPPORT		0x8000	/* Interrupt Coalesing Supported */

#define IOAT_DEVICE_STATUS_OFFSET		0x0E	/* 16-bit */
#define IOAT_DEVICE_STATUS_DEGRADED_MODE	0x0001


#define IOAT_CHANNEL_MMIO_SIZE			0x80	/* Each Channel MMIO space is this size */

/* DMA Channel Registers */
#define IOAT_CHANCTRL_OFFSET			0x00	/* 16-bit Channel Control Register */
#define IOAT_CHANCTRL_CHANNEL_PRIORITY_MASK	0xF000
#define IOAT_CHANCTRL_CHANNEL_IN_USE		0x0100
#define IOAT_CHANCTRL_DESCRIPTOR_ADDR_SNOOP_CONTROL	0x0020
#define IOAT_CHANCTRL_ERR_INT_EN		0x0010
#define IOAT_CHANCTRL_ANY_ERR_ABORT_EN		0x0008
#define IOAT_CHANCTRL_ERR_COMPLETION_EN		0x0004
#define IOAT_CHANCTRL_INT_DISABLE		0x0001

#define IOAT_DMA_COMP_OFFSET			0x02	/* 16-bit DMA channel compatability */
#define IOAT_DMA_COMP_V1			0x0001	/* Compatability with DMA version 1 */

#define IOAT_CHANSTS_OFFSET			0x04	/* 64-bit Channel Status Register */
#define IOAT_CHANSTS_OFFSET_LOW			0x04
#define IOAT_CHANSTS_OFFSET_HIGH		0x08
#define IOAT_CHANSTS_COMPLETED_DESCRIPTOR_ADDR	0xFFFFFFFFFFFFFFC0UL
#define IOAT_CHANSTS_SOFT_ERR			0x0000000000000010
#define IOAT_CHANSTS_DMA_TRANSFER_STATUS	0x0000000000000007
#define IOAT_CHANSTS_DMA_TRANSFER_STATUS_ACTIVE	0x0
#define IOAT_CHANSTS_DMA_TRANSFER_STATUS_DONE	0x1
#define IOAT_CHANSTS_DMA_TRANSFER_STATUS_SUSPENDED	0x2
#define IOAT_CHANSTS_DMA_TRANSFER_STATUS_HALTED	0x3

#define IOAT_CHAINADDR_OFFSET			0x0C	/* 64-bit Descriptor Chain Address Register */
#define IOAT_CHAINADDR_OFFSET_LOW		0x0C
#define IOAT_CHAINADDR_OFFSET_HIGH		0x10

#define IOAT_CHANCMD_OFFSET			0x14	/*  8-bit DMA Channel Command Register */
#define IOAT_CHANCMD_RESET			0x20
#define IOAT_CHANCMD_RESUME			0x10
#define IOAT_CHANCMD_ABORT			0x08
#define IOAT_CHANCMD_SUSPEND			0x04
#define IOAT_CHANCMD_APPEND			0x02
#define IOAT_CHANCMD_START			0x01

#define IOAT_CHANCMP_OFFSET			0x18	/* 64-bit Channel Completion Address Register */
#define IOAT_CHANCMP_OFFSET_LOW			0x18
#define IOAT_CHANCMP_OFFSET_HIGH		0x1C

#define IOAT_CDAR_OFFSET			0x20	/* 64-bit Current Descriptor Address Register */
#define IOAT_CDAR_OFFSET_LOW			0x20
#define IOAT_CDAR_OFFSET_HIGH			0x24

#define IOAT_CHANERR_OFFSET			0x28	/* 32-bit Channel Error Register */
#define IOAT_CHANERR_DMA_TRANSFER_SRC_ADDR_ERR	0x0001
#define IOAT_CHANERR_DMA_TRANSFER_DEST_ADDR_ERR	0x0002
#define IOAT_CHANERR_NEXT_DESCRIPTOR_ADDR_ERR	0x0004
#define IOAT_CHANERR_NEXT_DESCRIPTOR_ALIGNMENT_ERR	0x0008
#define IOAT_CHANERR_CHAIN_ADDR_VALUE_ERR	0x0010
#define IOAT_CHANERR_CHANCMD_ERR		0x0020
#define IOAT_CHANERR_CHIPSET_UNCORRECTABLE_DATA_INTEGRITY_ERR	0x0040
#define IOAT_CHANERR_DMA_UNCORRECTABLE_DATA_INTEGRITY_ERR	0x0080
#define IOAT_CHANERR_READ_DATA_ERR		0x0100
#define IOAT_CHANERR_WRITE_DATA_ERR		0x0200
#define IOAT_CHANERR_DESCRIPTOR_CONTROL_ERR	0x0400
#define IOAT_CHANERR_DESCRIPTOR_LENGTH_ERR	0x0800
#define IOAT_CHANERR_COMPLETION_ADDR_ERR	0x1000
#define IOAT_CHANERR_INT_CONFIGURATION_ERR	0x2000
#define IOAT_CHANERR_SOFT_ERR			0x4000

#define IOAT_CHANERR_MASK_OFFSET		0x2C	/* 32-bit Channel Error Register */

#endif /* _IOAT_REGISTERS_H_ */
