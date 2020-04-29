/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018 Quantenna Communications */

#ifndef _QTN_FMAC_PCIE_IPC_H_
#define _QTN_FMAC_PCIE_IPC_H_

#include <linux/types.h>

#include "shm_ipc_defs.h"

/* EP/RC status and flags */
#define QTN_BDA_PCIE_INIT		0x01
#define QTN_BDA_PCIE_RDY		0x02
#define QTN_BDA_FW_LOAD_RDY		0x03
#define QTN_BDA_FW_LOAD_DONE		0x04
#define QTN_BDA_FW_START		0x05
#define QTN_BDA_FW_RUN			0x06
#define QTN_BDA_FW_HOST_RDY		0x07
#define QTN_BDA_FW_TARGET_RDY		0x11
#define QTN_BDA_FW_TARGET_BOOT		0x12
#define QTN_BDA_FW_FLASH_BOOT		0x13
#define QTN_BDA_FW_QLINK_DONE		0x14
#define QTN_BDA_FW_HOST_LOAD		0x08
#define QTN_BDA_FW_BLOCK_DONE		0x09
#define QTN_BDA_FW_BLOCK_RDY		0x0A
#define QTN_BDA_FW_EP_RDY		0x0B
#define QTN_BDA_FW_BLOCK_END		0x0C
#define QTN_BDA_FW_CONFIG		0x0D
#define QTN_BDA_FW_RUNNING		0x0E
#define QTN_BDA_PCIE_FAIL		0x82
#define QTN_BDA_FW_LOAD_FAIL		0x85

#define QTN_BDA_RCMODE			BIT(1)
#define QTN_BDA_MSI			BIT(2)
#define QTN_BDA_HOST_CALCMD		BIT(3)
#define QTN_BDA_FLASH_PRESENT		BIT(4)
#define QTN_BDA_FLASH_BOOT		BIT(5)
#define QTN_BDA_XMIT_UBOOT		BIT(6)
#define QTN_BDA_HOST_QLINK_DRV		BIT(7)
#define QTN_BDA_TARGET_FBOOT_ERR	BIT(8)
#define QTN_BDA_TARGET_FWLOAD_ERR	BIT(9)
#define QTN_BDA_HOST_NOFW_ERR		BIT(12)
#define QTN_BDA_HOST_MEMALLOC_ERR	BIT(13)
#define QTN_BDA_HOST_MEMMAP_ERR		BIT(14)
#define QTN_BDA_VER(x)			(((x) >> 4) & 0xFF)
#define QTN_BDA_ERROR_MASK		0xFF00

/* registers and shmem address macros */
#if BITS_PER_LONG == 64
#define QTN_HOST_HI32(a)	((u32)(((u64)a) >> 32))
#define QTN_HOST_LO32(a)	((u32)(((u64)a) & 0xffffffffUL))
#define QTN_HOST_ADDR(h, l)	((((u64)h) << 32) | ((u64)l))
#elif BITS_PER_LONG == 32
#define QTN_HOST_HI32(a)	0
#define QTN_HOST_LO32(a)	((u32)(((u32)a) & 0xffffffffUL))
#define QTN_HOST_ADDR(h, l)	((u32)l)
#else
#error Unexpected BITS_PER_LONG value
#endif

#define QTN_PCIE_BDA_VERSION		0x1001

#define PCIE_BDA_NAMELEN		32

#define QTN_PCIE_RC_TX_QUEUE_LEN	256
#define QTN_PCIE_TX_VALID_PKT		0x80000000
#define QTN_PCIE_PKT_LEN_MASK		0xffff

#define QTN_BD_EMPTY		((uint32_t)0x00000001)
#define QTN_BD_WRAP		((uint32_t)0x00000002)
#define QTN_BD_MASK_LEN		((uint32_t)0xFFFF0000)
#define QTN_BD_MASK_OFFSET	((uint32_t)0x0000FF00)

#define QTN_GET_LEN(x)		(((x) >> 16) & 0xFFFF)
#define QTN_GET_OFFSET(x)	(((x) >> 8) & 0xFF)
#define QTN_SET_LEN(len)	(((len) & 0xFFFF) << 16)
#define QTN_SET_OFFSET(of)	(((of) & 0xFF) << 8)

#define RX_DONE_INTR_MSK	((0x1 << 6) - 1)

#define PCIE_DMA_OFFSET_ERROR		0xFFFF
#define PCIE_DMA_OFFSET_ERROR_MASK	0xFFFF

#define QTN_PCI_ENDIAN_DETECT_DATA	0x12345678
#define QTN_PCI_ENDIAN_REVERSE_DATA	0x78563412
#define QTN_PCI_ENDIAN_VALID_STATUS	0x3c3c3c3c
#define QTN_PCI_ENDIAN_INVALID_STATUS	0
#define QTN_PCI_LITTLE_ENDIAN		0
#define QTN_PCI_BIG_ENDIAN		0xffffffff

#define NBLOCKS(size, blksize)		\
	((size) / (blksize) + (((size) % (blksize) > 0) ? 1 : 0))

#endif /* _QTN_FMAC_PCIE_IPC_H_ */
