/*
 * Copyright(c) 2004 - 2009 Intel Corporation. All rights reserved.
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
#define IOAT_PCI_VID            0x8086
#define IOAT_MMIO_BAR		0

/* CB device ID's */
#define IOAT_PCI_DID_5000       0x1A38
#define IOAT_PCI_DID_CNB        0x360B
#define IOAT_PCI_DID_SCNB       0x65FF
#define IOAT_PCI_DID_SNB        0x402F

#define IOAT_PCI_RID            0x00
#define IOAT_PCI_SVID           0x8086
#define IOAT_PCI_SID            0x8086
#define IOAT_VER_1_2            0x12    /* Version 1.2 */
#define IOAT_VER_2_0            0x20    /* Version 2.0 */
#define IOAT_VER_3_0            0x30    /* Version 3.0 */

struct ioat_dma_descriptor {
	uint32_t	size;
	union {
		uint32_t ctl;
		struct {
			unsigned int int_en:1;
			unsigned int src_snoop_dis:1;
			unsigned int dest_snoop_dis:1;
			unsigned int compl_write:1;
			unsigned int fence:1;
			unsigned int null:1;
			unsigned int src_brk:1;
			unsigned int dest_brk:1;
			unsigned int bundle:1;
			unsigned int dest_dca:1;
			unsigned int hint:1;
			unsigned int rsvd2:13;
			unsigned int op:8;
		} ctl_f;
	};
	uint64_t	src_addr;
	uint64_t	dst_addr;
	uint64_t	next;
	uint64_t	rsv1;
	uint64_t	rsv2;
	uint64_t	user1;
	uint64_t	user2;
};
#endif
