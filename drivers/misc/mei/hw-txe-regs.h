/******************************************************************************
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Intel MEI Interface Header
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING
 *
 * Contact Information:
 *	Intel Corporation.
 *	linux-mei@linux.intel.com
 *	http://www.intel.com
 *
 * BSD LICENSE
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#ifndef _MEI_HW_TXE_REGS_H_
#define _MEI_HW_TXE_REGS_H_

#include "hw.h"

#define SEC_ALIVENESS_TIMER_TIMEOUT        (5 * MSEC_PER_SEC)
#define SEC_ALIVENESS_WAIT_TIMEOUT         (1 * MSEC_PER_SEC)
#define SEC_RESET_WAIT_TIMEOUT             (1 * MSEC_PER_SEC)
#define SEC_READY_WAIT_TIMEOUT             (5 * MSEC_PER_SEC)
#define START_MESSAGE_RESPONSE_WAIT_TIMEOUT (5 * MSEC_PER_SEC)
#define RESET_CANCEL_WAIT_TIMEOUT          (1 * MSEC_PER_SEC)

enum {
	SEC_BAR,
	BRIDGE_BAR,

	NUM_OF_MEM_BARS
};

/* SeC FW Status Register
 *
 * FW uses this register in order to report its status to host.
 * This register resides in PCI-E config space.
 */
#define PCI_CFG_TXE_FW_STS0   0x40
#  define PCI_CFG_TXE_FW_STS0_WRK_ST_MSK    0x0000000F
#  define PCI_CFG_TXE_FW_STS0_OP_ST_MSK     0x000001C0
#  define PCI_CFG_TXE_FW_STS0_FW_INIT_CMPLT 0x00000200
#  define PCI_CFG_TXE_FW_STS0_ERR_CODE_MSK  0x0000F000
#  define PCI_CFG_TXE_FW_STS0_OP_MODE_MSK   0x000F0000
#  define PCI_CFG_TXE_FW_STS0_RST_CNT_MSK   0x00F00000
#define PCI_CFG_TXE_FW_STS1   0x48

#define IPC_BASE_ADDR	0x80400 /* SeC IPC Base Address */

/* IPC Input Doorbell Register */
#define SEC_IPC_INPUT_DOORBELL_REG       (0x0000 + IPC_BASE_ADDR)

/* IPC Input Status Register
 * This register indicates whether or not processing of
 * the most recent command has been completed by the SEC
 * New commands and payloads should not be written by the Host
 * until this indicates that the previous command has been processed.
 */
#define SEC_IPC_INPUT_STATUS_REG         (0x0008 + IPC_BASE_ADDR)
#  define SEC_IPC_INPUT_STATUS_RDY    BIT(0)

/* IPC Host Interrupt Status Register */
#define SEC_IPC_HOST_INT_STATUS_REG      (0x0010 + IPC_BASE_ADDR)
#define   SEC_IPC_HOST_INT_STATUS_OUT_DB             BIT(0)
#define   SEC_IPC_HOST_INT_STATUS_IN_RDY             BIT(1)
#define   SEC_IPC_HOST_INT_STATUS_HDCP_M0_RCVD       BIT(5)
#define   SEC_IPC_HOST_INT_STATUS_ILL_MEM_ACCESS     BIT(17)
#define   SEC_IPC_HOST_INT_STATUS_AES_HKEY_ERR       BIT(18)
#define   SEC_IPC_HOST_INT_STATUS_DES_HKEY_ERR       BIT(19)
#define   SEC_IPC_HOST_INT_STATUS_TMRMTB_OVERFLOW    BIT(21)

/* Convenient mask for pending interrupts */
#define   SEC_IPC_HOST_INT_STATUS_PENDING \
		(SEC_IPC_HOST_INT_STATUS_OUT_DB| \
		SEC_IPC_HOST_INT_STATUS_IN_RDY)

/* IPC Host Interrupt Mask Register */
#define SEC_IPC_HOST_INT_MASK_REG        (0x0014 + IPC_BASE_ADDR)

#  define SEC_IPC_HOST_INT_MASK_OUT_DB	BIT(0) /* Output Doorbell Int Mask */
#  define SEC_IPC_HOST_INT_MASK_IN_RDY	BIT(1) /* Input Ready Int Mask */

/* IPC Input Payload RAM */
#define SEC_IPC_INPUT_PAYLOAD_REG        (0x0100 + IPC_BASE_ADDR)
/* IPC Shared Payload RAM */
#define IPC_SHARED_PAYLOAD_REG           (0x0200 + IPC_BASE_ADDR)

/* SeC Address Translation Table Entry 2 - Ctrl
 *
 * This register resides also in SeC's PCI-E Memory space.
 */
#define SATT2_CTRL_REG                   0x1040
#  define SATT2_CTRL_VALID_MSK            BIT(0)
#  define SATT2_CTRL_BR_BASE_ADDR_REG_SHIFT 8
#  define SATT2_CTRL_BRIDGE_HOST_EN_MSK   BIT(12)

/* SATT Table Entry 2 SAP Base Address Register */
#define SATT2_SAP_BA_REG                 0x1044
/* SATT Table Entry 2 SAP Size Register. */
#define SATT2_SAP_SIZE_REG               0x1048
 /* SATT Table Entry 2 SAP Bridge Address - LSB Register */
#define SATT2_BRG_BA_LSB_REG             0x104C

/* Host High-level Interrupt Status Register */
#define HHISR_REG                        0x2020
/* Host High-level Interrupt Enable Register
 *
 * Resides in PCI memory space. This is the top hierarchy for
 * interrupts from SeC to host, aggregating both interrupts that
 * arrive through HICR registers as well as interrupts
 * that arrive via IPC.
 */
#define HHIER_REG                        0x2024
#define   IPC_HHIER_SEC	BIT(0)
#define   IPC_HHIER_BRIDGE	BIT(1)
#define   IPC_HHIER_MSK	(IPC_HHIER_SEC | IPC_HHIER_BRIDGE)

/* Host High-level Interrupt Mask Register.
 *
 * Resides in PCI memory space.
 * This is the top hierarchy for masking interrupts from SeC to host.
 */
#define HHIMR_REG                        0x2028
#define   IPC_HHIMR_SEC       BIT(0)
#define   IPC_HHIMR_BRIDGE    BIT(1)

/* Host High-level IRQ Status Register */
#define HHIRQSR_REG                      0x202C

/* Host Interrupt Cause Register 0 - SeC IPC Readiness
 *
 * This register is both an ICR to Host from PCI Memory Space
 * and it is also exposed in the SeC memory space.
 * This register is used by SeC's IPC driver in order
 * to synchronize with host about IPC interface state.
 */
#define HICR_SEC_IPC_READINESS_REG       0x2040
#define   HICR_SEC_IPC_READINESS_HOST_RDY  BIT(0)
#define   HICR_SEC_IPC_READINESS_SEC_RDY   BIT(1)
#define   HICR_SEC_IPC_READINESS_SYS_RDY     \
	  (HICR_SEC_IPC_READINESS_HOST_RDY | \
	   HICR_SEC_IPC_READINESS_SEC_RDY)
#define   HICR_SEC_IPC_READINESS_RDY_CLR   BIT(2)

/* Host Interrupt Cause Register 1 - Aliveness Response */
/* This register is both an ICR to Host from PCI Memory Space
 * and it is also exposed in the SeC memory space.
 * The register may be used by SeC to ACK a host request for aliveness.
 */
#define HICR_HOST_ALIVENESS_RESP_REG     0x2044
#define   HICR_HOST_ALIVENESS_RESP_ACK    BIT(0)

/* Host Interrupt Cause Register 2 - SeC IPC Output Doorbell */
#define HICR_SEC_IPC_OUTPUT_DOORBELL_REG 0x2048

/* Host Interrupt Status Register.
 *
 * Resides in PCI memory space.
 * This is the main register involved in generating interrupts
 * from SeC to host via HICRs.
 * The interrupt generation rules are as follows:
 * An interrupt will be generated whenever for any i,
 * there is a transition from a state where at least one of
 * the following conditions did not hold, to a state where
 * ALL the following conditions hold:
 * A) HISR.INT[i]_STS == 1.
 * B) HIER.INT[i]_EN == 1.
 */
#define HISR_REG                         0x2060
#define   HISR_INT_0_STS      BIT(0)
#define   HISR_INT_1_STS      BIT(1)
#define   HISR_INT_2_STS      BIT(2)
#define   HISR_INT_3_STS      BIT(3)
#define   HISR_INT_4_STS      BIT(4)
#define   HISR_INT_5_STS      BIT(5)
#define   HISR_INT_6_STS      BIT(6)
#define   HISR_INT_7_STS      BIT(7)
#define   HISR_INT_STS_MSK \
	(HISR_INT_0_STS | HISR_INT_1_STS | HISR_INT_2_STS)

/* Host Interrupt Enable Register. Resides in PCI memory space. */
#define HIER_REG                         0x2064
#define   HIER_INT_0_EN      BIT(0)
#define   HIER_INT_1_EN      BIT(1)
#define   HIER_INT_2_EN      BIT(2)
#define   HIER_INT_3_EN      BIT(3)
#define   HIER_INT_4_EN      BIT(4)
#define   HIER_INT_5_EN      BIT(5)
#define   HIER_INT_6_EN      BIT(6)
#define   HIER_INT_7_EN      BIT(7)

#define   HIER_INT_EN_MSK \
	 (HIER_INT_0_EN | HIER_INT_1_EN | HIER_INT_2_EN)


/* SEC Memory Space IPC output payload.
 *
 * This register is part of the output payload which SEC provides to host.
 */
#define BRIDGE_IPC_OUTPUT_PAYLOAD_REG    0x20C0

/* SeC Interrupt Cause Register - Host Aliveness Request
 * This register is both an ICR to SeC and it is also exposed
 * in the host-visible PCI memory space.
 * The register is used by host to request SeC aliveness.
 */
#define SICR_HOST_ALIVENESS_REQ_REG      0x214C
#define   SICR_HOST_ALIVENESS_REQ_REQUESTED    BIT(0)


/* SeC Interrupt Cause Register - Host IPC Readiness
 *
 * This register is both an ICR to SeC and it is also exposed
 * in the host-visible PCI memory space.
 * This register is used by the host's SeC driver uses in order
 * to synchronize with SeC about IPC interface state.
 */
#define SICR_HOST_IPC_READINESS_REQ_REG  0x2150


#define SICR_HOST_IPC_READINESS_HOST_RDY  BIT(0)
#define SICR_HOST_IPC_READINESS_SEC_RDY   BIT(1)
#define SICR_HOST_IPC_READINESS_SYS_RDY     \
	(SICR_HOST_IPC_READINESS_HOST_RDY | \
	 SICR_HOST_IPC_READINESS_SEC_RDY)
#define SICR_HOST_IPC_READINESS_RDY_CLR   BIT(2)

/* SeC Interrupt Cause Register - SeC IPC Output Status
 *
 * This register indicates whether or not processing of the most recent
 * command has been completed by the Host.
 * New commands and payloads should not be written by SeC until this
 * register indicates that the previous command has been processed.
 */
#define SICR_SEC_IPC_OUTPUT_STATUS_REG   0x2154
#  define SEC_IPC_OUTPUT_STATUS_RDY BIT(0)



/*  MEI IPC Message payload size 64 bytes */
#define PAYLOAD_SIZE        64

/* MAX size for SATT range 32MB */
#define SATT_RANGE_MAX     (32 << 20)


#endif /* _MEI_HW_TXE_REGS_H_ */

