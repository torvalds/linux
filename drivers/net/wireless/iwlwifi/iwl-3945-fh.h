/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2009 Intel Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2009 Intel Corporation. All rights reserved.
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
#ifndef __iwl_3945_fh_h__
#define __iwl_3945_fh_h__

/************************************/
/* iwl3945 Flow Handler Definitions */
/************************************/

/**
 * This I/O area is directly read/writable by driver (e.g. Linux uses writel())
 * Addresses are offsets from device's PCI hardware base address.
 */
#define FH39_MEM_LOWER_BOUND                   (0x0800)
#define FH39_MEM_UPPER_BOUND                   (0x1000)

#define FH39_CBCC_TABLE		(FH39_MEM_LOWER_BOUND + 0x140)
#define FH39_TFDB_TABLE		(FH39_MEM_LOWER_BOUND + 0x180)
#define FH39_RCSR_TABLE		(FH39_MEM_LOWER_BOUND + 0x400)
#define FH39_RSSR_TABLE		(FH39_MEM_LOWER_BOUND + 0x4c0)
#define FH39_TCSR_TABLE		(FH39_MEM_LOWER_BOUND + 0x500)
#define FH39_TSSR_TABLE		(FH39_MEM_LOWER_BOUND + 0x680)

/* TFDB (Transmit Frame Buffer Descriptor) */
#define FH39_TFDB(_ch, buf)			(FH39_TFDB_TABLE + \
						 ((_ch) * 2 + (buf)) * 0x28)
#define FH39_TFDB_CHNL_BUF_CTRL_REG(_ch)	(FH39_TFDB_TABLE + 0x50 * (_ch))

/* CBCC channel is [0,2] */
#define FH39_CBCC(_ch)		(FH39_CBCC_TABLE + (_ch) * 0x8)
#define FH39_CBCC_CTRL(_ch)	(FH39_CBCC(_ch) + 0x00)
#define FH39_CBCC_BASE(_ch)	(FH39_CBCC(_ch) + 0x04)

/* RCSR channel is [0,2] */
#define FH39_RCSR(_ch)			(FH39_RCSR_TABLE + (_ch) * 0x40)
#define FH39_RCSR_CONFIG(_ch)		(FH39_RCSR(_ch) + 0x00)
#define FH39_RCSR_RBD_BASE(_ch)		(FH39_RCSR(_ch) + 0x04)
#define FH39_RCSR_WPTR(_ch)		(FH39_RCSR(_ch) + 0x20)
#define FH39_RCSR_RPTR_ADDR(_ch)	(FH39_RCSR(_ch) + 0x24)

#define FH39_RSCSR_CHNL0_WPTR		(FH39_RCSR_WPTR(0))

/* RSSR */
#define FH39_RSSR_CTRL			(FH39_RSSR_TABLE + 0x000)
#define FH39_RSSR_STATUS		(FH39_RSSR_TABLE + 0x004)

/* TCSR */
#define FH39_TCSR(_ch)			(FH39_TCSR_TABLE + (_ch) * 0x20)
#define FH39_TCSR_CONFIG(_ch)		(FH39_TCSR(_ch) + 0x00)
#define FH39_TCSR_CREDIT(_ch)		(FH39_TCSR(_ch) + 0x04)
#define FH39_TCSR_BUFF_STTS(_ch)	(FH39_TCSR(_ch) + 0x08)

/* TSSR */
#define FH39_TSSR_CBB_BASE        (FH39_TSSR_TABLE + 0x000)
#define FH39_TSSR_MSG_CONFIG      (FH39_TSSR_TABLE + 0x008)
#define FH39_TSSR_TX_STATUS       (FH39_TSSR_TABLE + 0x010)


/* DBM */

#define FH39_SRVC_CHNL                            (6)

#define FH39_RCSR_RX_CONFIG_REG_POS_RBDC_SIZE     (20)
#define FH39_RCSR_RX_CONFIG_REG_POS_IRQ_RBTH      (4)

#define FH39_RCSR_RX_CONFIG_REG_BIT_WR_STTS_EN    (0x08000000)

#define FH39_RCSR_RX_CONFIG_REG_VAL_DMA_CHNL_EN_ENABLE        (0x80000000)

#define FH39_RCSR_RX_CONFIG_REG_VAL_RDRBD_EN_ENABLE           (0x20000000)

#define FH39_RCSR_RX_CONFIG_REG_VAL_MAX_FRAG_SIZE_128		(0x01000000)

#define FH39_RCSR_RX_CONFIG_REG_VAL_IRQ_DEST_INT_HOST		(0x00001000)

#define FH39_RCSR_RX_CONFIG_REG_VAL_MSG_MODE_FH			(0x00000000)

#define FH39_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_TXF		(0x00000000)
#define FH39_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_DRIVER		(0x00000001)

#define FH39_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE_VAL	(0x00000000)
#define FH39_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL	(0x00000008)

#define FH39_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_IFTFD		(0x00200000)

#define FH39_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_NOINT		(0x00000000)

#define FH39_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE		(0x00000000)
#define FH39_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE		(0x80000000)

#define FH39_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID		(0x00004000)

#define FH39_TCSR_CHNL_TX_BUF_STS_REG_BIT_TFDB_WPTR		(0x00000001)

#define FH39_TSSR_TX_MSG_CONFIG_REG_VAL_SNOOP_RD_TXPD_ON	(0xFF000000)
#define FH39_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RD_TXPD_ON	(0x00FF0000)

#define FH39_TSSR_TX_MSG_CONFIG_REG_VAL_MAX_FRAG_SIZE_128B	(0x00000400)

#define FH39_TSSR_TX_MSG_CONFIG_REG_VAL_SNOOP_RD_TFD_ON		(0x00000100)
#define FH39_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RD_CBB_ON		(0x00000080)

#define FH39_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RSP_WAIT_TH	(0x00000020)
#define FH39_TSSR_TX_MSG_CONFIG_REG_VAL_RSP_WAIT_TH		(0x00000005)

#define FH39_TSSR_TX_STATUS_REG_BIT_BUFS_EMPTY(_ch)	(BIT(_ch) << 24)
#define FH39_TSSR_TX_STATUS_REG_BIT_NO_PEND_REQ(_ch)	(BIT(_ch) << 16)

#define FH39_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(_ch) \
	(FH39_TSSR_TX_STATUS_REG_BIT_BUFS_EMPTY(_ch) | \
	 FH39_TSSR_TX_STATUS_REG_BIT_NO_PEND_REQ(_ch))

#define FH39_RSSR_CHNL0_RX_STATUS_CHNL_IDLE			(0x01000000)

struct iwl3945_tfd_tb {
	__le32 addr;
	__le32 len;
} __attribute__ ((packed));

struct iwl3945_tfd {
	__le32 control_flags;
	struct iwl3945_tfd_tb tbs[4];
	u8 __pad[28];
} __attribute__ ((packed));


#endif /* __iwl_3945_fh_h__ */

