/*
 * Copyright 2013-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * * Neither the name of the above-listed copyright holders nor the
 * names of any contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __FSL_MC_CMD_H
#define __FSL_MC_CMD_H

#define MC_CMD_NUM_OF_PARAMS	7

struct mc_cmd_header {
	u8 src_id;
	u8 flags_hw;
	u8 status;
	u8 flags_sw;
	__le16 token;
	__le16 cmd_id;
};

struct mc_command {
	u64 header;
	u64 params[MC_CMD_NUM_OF_PARAMS];
};

enum mc_cmd_status {
	MC_CMD_STATUS_OK = 0x0, /* Completed successfully */
	MC_CMD_STATUS_READY = 0x1, /* Ready to be processed */
	MC_CMD_STATUS_AUTH_ERR = 0x3, /* Authentication error */
	MC_CMD_STATUS_NO_PRIVILEGE = 0x4, /* No privilege */
	MC_CMD_STATUS_DMA_ERR = 0x5, /* DMA or I/O error */
	MC_CMD_STATUS_CONFIG_ERR = 0x6, /* Configuration error */
	MC_CMD_STATUS_TIMEOUT = 0x7, /* Operation timed out */
	MC_CMD_STATUS_NO_RESOURCE = 0x8, /* No resources */
	MC_CMD_STATUS_NO_MEMORY = 0x9, /* No memory available */
	MC_CMD_STATUS_BUSY = 0xA, /* Device is busy */
	MC_CMD_STATUS_UNSUPPORTED_OP = 0xB, /* Unsupported operation */
	MC_CMD_STATUS_INVALID_STATE = 0xC /* Invalid state */
};

/*
 * MC command flags
 */

/* High priority flag */
#define MC_CMD_FLAG_PRI		0x80
/* Command completion flag */
#define MC_CMD_FLAG_INTR_DIS	0x01

#define MC_CMD_HDR_CMDID_MASK		0xFFF0
#define MC_CMD_HDR_CMDID_SHIFT		4
#define MC_CMD_HDR_TOKEN_MASK		0xFFC0
#define MC_CMD_HDR_TOKEN_SHIFT		6

static inline u64 mc_encode_cmd_header(u16 cmd_id,
				       u32 cmd_flags,
				       u16 token)
{
	u64 header = 0;
	struct mc_cmd_header *hdr = (struct mc_cmd_header *)&header;

	hdr->cmd_id = cpu_to_le16((cmd_id << MC_CMD_HDR_CMDID_SHIFT) &
				  MC_CMD_HDR_CMDID_MASK);
	hdr->token = cpu_to_le16((token << MC_CMD_HDR_TOKEN_SHIFT) &
				 MC_CMD_HDR_TOKEN_MASK);
	hdr->status = MC_CMD_STATUS_READY;
	if (cmd_flags & MC_CMD_FLAG_PRI)
		hdr->flags_hw = MC_CMD_FLAG_PRI;
	if (cmd_flags & MC_CMD_FLAG_INTR_DIS)
		hdr->flags_sw = MC_CMD_FLAG_INTR_DIS;

	return header;
}

static inline u16 mc_cmd_hdr_read_token(struct mc_command *cmd)
{
	struct mc_cmd_header *hdr = (struct mc_cmd_header *)&cmd->header;
	u16 token = le16_to_cpu(hdr->token);

	return (token & MC_CMD_HDR_TOKEN_MASK) >> MC_CMD_HDR_TOKEN_SHIFT;
}

#endif /* __FSL_MC_CMD_H */
