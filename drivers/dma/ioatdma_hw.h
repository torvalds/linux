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
#ifndef _IOAT_HW_H_
#define _IOAT_HW_H_

/* PCI Configuration Space Values */
#define IOAT_PCI_VID			0x8086
#define IOAT_PCI_DID			0x1A38
#define IOAT_PCI_RID			0x00
#define IOAT_PCI_SVID			0x8086
#define IOAT_PCI_SID			0x8086
#define IOAT_VER_1_2			0x12	/* Version 1.2 */

struct ioat_dma_descriptor {
	uint32_t	size;
	uint32_t	ctl;
	uint64_t	src_addr;
	uint64_t	dst_addr;
	uint64_t	next;
	uint64_t	rsv1;
	uint64_t	rsv2;
	uint64_t	user1;
	uint64_t	user2;
};

#define IOAT_DMA_DESCRIPTOR_CTL_INT_GN	0x00000001
#define IOAT_DMA_DESCRIPTOR_CTL_SRC_SN	0x00000002
#define IOAT_DMA_DESCRIPTOR_CTL_DST_SN	0x00000004
#define IOAT_DMA_DESCRIPTOR_CTL_CP_STS	0x00000008
#define IOAT_DMA_DESCRIPTOR_CTL_FRAME	0x00000010
#define IOAT_DMA_DESCRIPTOR_NUL		0x00000020
#define IOAT_DMA_DESCRIPTOR_OPCODE	0xFF000000

#endif
