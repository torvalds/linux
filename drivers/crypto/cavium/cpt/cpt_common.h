/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Cavium, Inc.
 */

#ifndef __CPT_COMMON_H
#define __CPT_COMMON_H

#include <asm/byteorder.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "cpt_hw_types.h"

/* Device ID */
#define CPT_81XX_PCI_PF_DEVICE_ID 0xa040
#define CPT_81XX_PCI_VF_DEVICE_ID 0xa041

/* flags to indicate the features supported */
#define CPT_FLAG_SRIOV_ENABLED BIT(1)
#define CPT_FLAG_VF_DRIVER BIT(2)
#define CPT_FLAG_DEVICE_READY BIT(3)

#define cpt_sriov_enabled(cpt) ((cpt)->flags & CPT_FLAG_SRIOV_ENABLED)
#define cpt_vf_driver(cpt) ((cpt)->flags & CPT_FLAG_VF_DRIVER)
#define cpt_device_ready(cpt) ((cpt)->flags & CPT_FLAG_DEVICE_READY)

#define CPT_MBOX_MSG_TYPE_ACK 1
#define CPT_MBOX_MSG_TYPE_NACK 2
#define CPT_MBOX_MSG_TIMEOUT 2000
#define VF_STATE_DOWN 0
#define VF_STATE_UP 1

/*
 * CPT Registers map for 81xx
 */

/* PF registers */
#define CPTX_PF_CONSTANTS(a) (0x0ll + ((u64)(a) << 36))
#define CPTX_PF_RESET(a) (0x100ll + ((u64)(a) << 36))
#define CPTX_PF_DIAG(a) (0x120ll + ((u64)(a) << 36))
#define CPTX_PF_BIST_STATUS(a) (0x160ll + ((u64)(a) << 36))
#define CPTX_PF_ECC0_CTL(a) (0x200ll + ((u64)(a) << 36))
#define CPTX_PF_ECC0_FLIP(a) (0x210ll + ((u64)(a) << 36))
#define CPTX_PF_ECC0_INT(a) (0x220ll + ((u64)(a) << 36))
#define CPTX_PF_ECC0_INT_W1S(a) (0x230ll + ((u64)(a) << 36))
#define CPTX_PF_ECC0_ENA_W1S(a)	(0x240ll + ((u64)(a) << 36))
#define CPTX_PF_ECC0_ENA_W1C(a)	(0x250ll + ((u64)(a) << 36))
#define CPTX_PF_MBOX_INTX(a, b)	\
	(0x400ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_MBOX_INT_W1SX(a, b) \
	(0x420ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_MBOX_ENA_W1CX(a, b) \
	(0x440ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_MBOX_ENA_W1SX(a, b) \
	(0x460ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_EXEC_INT(a) (0x500ll + 0x1000000000ll * ((a) & 0x1))
#define CPTX_PF_EXEC_INT_W1S(a)	(0x520ll + ((u64)(a) << 36))
#define CPTX_PF_EXEC_ENA_W1C(a)	(0x540ll + ((u64)(a) << 36))
#define CPTX_PF_EXEC_ENA_W1S(a)	(0x560ll + ((u64)(a) << 36))
#define CPTX_PF_GX_EN(a, b) \
	(0x600ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_EXEC_INFO(a) (0x700ll + ((u64)(a) << 36))
#define CPTX_PF_EXEC_BUSY(a) (0x800ll + ((u64)(a) << 36))
#define CPTX_PF_EXEC_INFO0(a) (0x900ll + ((u64)(a) << 36))
#define CPTX_PF_EXEC_INFO1(a) (0x910ll + ((u64)(a) << 36))
#define CPTX_PF_INST_REQ_PC(a) (0x10000ll + ((u64)(a) << 36))
#define CPTX_PF_INST_LATENCY_PC(a) \
	(0x10020ll + ((u64)(a) << 36))
#define CPTX_PF_RD_REQ_PC(a) (0x10040ll + ((u64)(a) << 36))
#define CPTX_PF_RD_LATENCY_PC(a) (0x10060ll + ((u64)(a) << 36))
#define CPTX_PF_RD_UC_PC(a) (0x10080ll + ((u64)(a) << 36))
#define CPTX_PF_ACTIVE_CYCLES_PC(a) (0x10100ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_CTL(a) (0x4000000ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_STATUS(a) (0x4000008ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_CLK(a) (0x4000010ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_DBG_CTL(a) (0x4000018ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_DBG_DATA(a)	(0x4000020ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_BIST_STATUS(a) (0x4000028ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_REQ_TIMER(a) (0x4000030ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_MEM_CTL(a) (0x4000038ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_PERF_CTL(a)	(0x4001000ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_DBG_CNTX(a, b) \
	(0x4001100ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_EXE_PERF_EVENT_CNT(a) (0x4001180ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_EPCI_INBX_CNT(a, b) \
	(0x4001200ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_EXE_EPCI_OUTBX_CNT(a, b) \
	(0x4001240ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_ENGX_UCODE_BASE(a, b) \
	(0x4002000ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_QX_CTL(a, b) \
	(0x8000000ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_PF_QX_GMCTL(a, b) \
	(0x8000020ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_PF_QX_CTL2(a, b) \
	(0x8000100ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_PF_VFX_MBOXX(a, b, c) \
	(0x8001000ll + ((u64)(a) << 36) + ((b) << 20) + ((c) << 8))

/* VF registers */
#define CPTX_VQX_CTL(a, b) (0x100ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_SADDR(a, b) (0x200ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_DONE_WAIT(a, b) (0x400ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_INPROG(a, b) (0x410ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_DONE(a, b) (0x420ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_DONE_ACK(a, b) (0x440ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_DONE_INT_W1S(a, b) (0x460ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_DONE_INT_W1C(a, b) (0x468ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_DONE_ENA_W1S(a, b) (0x470ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_DONE_ENA_W1C(a, b) (0x478ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_MISC_INT(a, b)	(0x500ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_MISC_INT_W1S(a, b) (0x508ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_MISC_ENA_W1S(a, b) (0x510ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_MISC_ENA_W1C(a, b) (0x518ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VQX_DOORBELL(a, b) (0x600ll + ((u64)(a) << 36) + ((b) << 20))
#define CPTX_VFX_PF_MBOXX(a, b, c) \
	(0x1000ll + ((u64)(a) << 36) + ((b) << 20) + ((c) << 3))

enum vftype {
	AE_TYPES = 1,
	SE_TYPES = 2,
	BAD_CPT_TYPES,
};

/* Max CPT devices supported */
enum cpt_mbox_opcode {
	CPT_MSG_VF_UP = 1,
	CPT_MSG_VF_DOWN,
	CPT_MSG_READY,
	CPT_MSG_QLEN,
	CPT_MSG_QBIND_GRP,
	CPT_MSG_VQ_PRIORITY,
};

/* CPT mailbox structure */
struct cpt_mbox {
	u64 msg; /* Message type MBOX[0] */
	u64 data;/* Data         MBOX[1] */
};

/* Register read/write APIs */
static inline void cpt_write_csr64(u8 __iomem *hw_addr, u64 offset,
				   u64 val)
{
	writeq(val, hw_addr + offset);
}

static inline u64 cpt_read_csr64(u8 __iomem *hw_addr, u64 offset)
{
	return readq(hw_addr + offset);
}
#endif /* __CPT_COMMON_H */
