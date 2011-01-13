/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

#ifndef _IWM_HAL_H_
#define _IWM_HAL_H_

#include "umac.h"

#define GET_VAL8(s, name)	((s >> name##_POS) & name##_SEED)
#define GET_VAL16(s, name)	((le16_to_cpu(s) >> name##_POS) & name##_SEED)
#define GET_VAL32(s, name)	((le32_to_cpu(s) >> name##_POS) & name##_SEED)

#define SET_VAL8(s, name, val)						  \
do {									  \
	s = (s & ~(name##_SEED << name##_POS)) |			  \
	    ((val & name##_SEED) << name##_POS);			  \
} while (0)

#define SET_VAL16(s, name, val)						  \
do {									  \
	s = cpu_to_le16((le16_to_cpu(s) & ~(name##_SEED << name##_POS)) | \
			((val & name##_SEED) << name##_POS));		  \
} while (0)

#define SET_VAL32(s, name, val)						  \
do {									  \
	s = cpu_to_le32((le32_to_cpu(s) & ~(name##_SEED << name##_POS)) | \
			((val & name##_SEED) << name##_POS));		  \
} while (0)


#define UDMA_UMAC_INIT	{	.eop = 1,				  \
				.credit_group = 0x4,			  \
				.ra_tid = UMAC_HDI_ACT_TBL_IDX_HOST_CMD,  \
				.lmac_offset = 0 }
#define UDMA_LMAC_INIT	{	.eop = 1,				  \
				.credit_group = 0x4,			  \
				.ra_tid = UMAC_HDI_ACT_TBL_IDX_HOST_CMD,  \
				.lmac_offset = 4 }


/* UDMA IN OP CODE -- cmd bits [3:0] */
#define UDMA_HDI_IN_NW_CMD_OPCODE_POS		0
#define UDMA_HDI_IN_NW_CMD_OPCODE_SEED		0xF

#define UDMA_IN_OPCODE_GENERAL_RESP		0x0
#define UDMA_IN_OPCODE_READ_RESP		0x1
#define UDMA_IN_OPCODE_WRITE_RESP		0x2
#define UDMA_IN_OPCODE_PERS_WRITE_RESP		0x5
#define UDMA_IN_OPCODE_PERS_READ_RESP		0x6
#define UDMA_IN_OPCODE_RD_MDFY_WR_RESP		0x7
#define UDMA_IN_OPCODE_EP_MNGMT_MSG		0x8
#define UDMA_IN_OPCODE_CRDT_CHNG_MSG		0x9
#define UDMA_IN_OPCODE_CNTRL_DATABASE_MSG	0xA
#define UDMA_IN_OPCODE_SW_MSG			0xB
#define UDMA_IN_OPCODE_WIFI			0xF
#define UDMA_IN_OPCODE_WIFI_LMAC		0x1F
#define UDMA_IN_OPCODE_WIFI_UMAC		0x2F

/* HW API: udma_hdi_nonwifi API (OUT and IN) */

/* iwm_udma_nonwifi_cmd request response -- bits [9:9] */
#define UDMA_HDI_OUT_NW_CMD_RESP_POS		9
#define UDMA_HDI_OUT_NW_CMD_RESP_SEED		0x1

/* iwm_udma_nonwifi_cmd handle by HW -- bits [11:11] */
#define UDMA_HDI_OUT_NW_CMD_HANDLE_BY_HW_POS	11
#define UDMA_HDI_OUT_NW_CMD_HANDLE_BY_HW_SEED	0x1

/* iwm_udma_nonwifi_cmd sequence-number -- bits [12:15] */
#define UDMA_HDI_OUT_NW_CMD_SEQ_NUM_POS		12
#define UDMA_HDI_OUT_NW_CMD_SEQ_NUM_SEED	0xF

/* UDMA IN Non-WIFI HW sequence number -- bits [12:15] */
#define UDMA_IN_NW_HW_SEQ_NUM_POS		12
#define UDMA_IN_NW_HW_SEQ_NUM_SEED		0xF

/* UDMA IN Non-WIFI HW signature -- bits [16:31] */
#define UDMA_IN_NW_HW_SIG_POS			16
#define UDMA_IN_NW_HW_SIG_SEED			0xFFFF

/* fixed signature */
#define UDMA_IN_NW_HW_SIG			0xCBBC

/* UDMA IN Non-WIFI HW block length -- bits [32:35] */
#define UDMA_IN_NW_HW_LENGTH_SEED		0xF
#define UDMA_IN_NW_HW_LENGTH_POS		32

/* End of HW API: udma_hdi_nonwifi API (OUT and IN) */

#define IWM_SDIO_FW_MAX_CHUNK_SIZE	2032
#define IWM_MAX_WIFI_HEADERS_SIZE	32
#define IWM_MAX_NONWIFI_HEADERS_SIZE	16
#define IWM_MAX_NONWIFI_CMD_BUFF_SIZE	(IWM_SDIO_FW_MAX_CHUNK_SIZE - \
					 IWM_MAX_NONWIFI_HEADERS_SIZE)
#define IWM_MAX_WIFI_CMD_BUFF_SIZE	(IWM_SDIO_FW_MAX_CHUNK_SIZE - \
					 IWM_MAX_WIFI_HEADERS_SIZE)

#define IWM_HAL_CONCATENATE_BUF_SIZE	(32 * 1024)

struct iwm_wifi_cmd_buff {
	u16 len;
	u8 *start;
	u8 hdr[IWM_MAX_WIFI_HEADERS_SIZE];
	u8 payload[IWM_MAX_WIFI_CMD_BUFF_SIZE];
};

struct iwm_nonwifi_cmd_buff {
	u16 len;
	u8 *start;
	u8 hdr[IWM_MAX_NONWIFI_HEADERS_SIZE];
	u8 payload[IWM_MAX_NONWIFI_CMD_BUFF_SIZE];
};

struct iwm_udma_nonwifi_cmd {
	u8 opcode;
	u8 eop;
	u8 resp;
	u8 handle_by_hw;
	__le32 addr;
	__le32 op1_sz;
	__le32 op2;
	__le16 seq_num;
};

struct iwm_udma_wifi_cmd {
	__le16 count;
	u8 eop;
	u8 credit_group;
	u8 ra_tid;
	u8 lmac_offset;
};

struct iwm_umac_cmd {
	u8 id;
	__le16 count;
	u8 resp;
	__le16 seq_num;
	u8 color;
};

struct iwm_lmac_cmd {
	u8 id;
	__le16 count;
	u8 resp;
	__le16 seq_num;
};

struct iwm_nonwifi_cmd {
	u16 seq_num;
	bool resp_received;
	struct list_head pending;
	struct iwm_udma_nonwifi_cmd udma_cmd;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_lmac_cmd lmac_cmd;
	struct iwm_nonwifi_cmd_buff buf;
	u32 flags;
};

struct iwm_wifi_cmd {
	u16 seq_num;
	struct list_head pending;
	struct iwm_udma_wifi_cmd udma_cmd;
	struct iwm_umac_cmd umac_cmd;
	struct iwm_lmac_cmd lmac_cmd;
	struct iwm_wifi_cmd_buff buf;
	u32 flags;
};

void iwm_cmd_flush(struct iwm_priv *iwm);

struct iwm_wifi_cmd *iwm_get_pending_wifi_cmd(struct iwm_priv *iwm,
					      u16 seq_num);
struct iwm_nonwifi_cmd *iwm_get_pending_nonwifi_cmd(struct iwm_priv *iwm,
						    u8 seq_num, u8 cmd_opcode);


int iwm_hal_send_target_cmd(struct iwm_priv *iwm,
			    struct iwm_udma_nonwifi_cmd *ucmd,
			    const void *payload);

int iwm_hal_send_host_cmd(struct iwm_priv *iwm,
			  struct iwm_udma_wifi_cmd *udma_cmd,
			  struct iwm_umac_cmd *umac_cmd,
			  struct iwm_lmac_cmd *lmac_cmd,
			  const void *payload, u16 payload_size);

int iwm_hal_send_umac_cmd(struct iwm_priv *iwm,
			  struct iwm_udma_wifi_cmd *udma_cmd,
			  struct iwm_umac_cmd *umac_cmd,
			  const void *payload, u16 payload_size);

u16 iwm_alloc_wifi_cmd_seq(struct iwm_priv *iwm);

void iwm_udma_wifi_hdr_set_eop(struct iwm_priv *iwm, u8 *buf, u8 eop);
void iwm_build_udma_wifi_hdr(struct iwm_priv *iwm,
			     struct iwm_udma_out_wifi_hdr *hdr,
			     struct iwm_udma_wifi_cmd *cmd);
void iwm_build_umac_hdr(struct iwm_priv *iwm,
			struct iwm_umac_fw_cmd_hdr *hdr,
			struct iwm_umac_cmd *cmd);
#endif /* _IWM_HAL_H_ */
