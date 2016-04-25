/* Copyright 2013-2015 Freescale Semiconductor Inc.
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

#define MAKE_UMASK64(_width) \
	((u64)((_width) < 64 ? ((u64)1 << (_width)) - 1 : -1))

static inline u64 mc_enc(int lsoffset, int width, u64 val)
{
	return (u64)(((u64)val & MAKE_UMASK64(width)) << lsoffset);
}

static inline u64 mc_dec(u64 val, int lsoffset, int width)
{
	return (u64)((val >> lsoffset) & MAKE_UMASK64(width));
}

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
#define MC_CMD_FLAG_PRI		0x00008000
/* Command completion flag */
#define MC_CMD_FLAG_INTR_DIS	0x01000000

/*
 * TODO Remove following two defines after completion of flib 8.0.0
 * integration
 */
#define MC_CMD_PRI_LOW		0 /*!< Low Priority command indication */
#define MC_CMD_PRI_HIGH		1 /*!< High Priority command indication */

#define MC_CMD_HDR_CMDID_O	52	/* Command ID field offset */
#define MC_CMD_HDR_CMDID_S	12	/* Command ID field size */
#define MC_CMD_HDR_TOKEN_O	38	/* Token field offset */
#define MC_CMD_HDR_TOKEN_S	10	/* Token field size */
#define MC_CMD_HDR_STATUS_O	16	/* Status field offset */
#define MC_CMD_HDR_STATUS_S	8	/* Status field size*/
#define MC_CMD_HDR_FLAGS_O	0	/* Flags field offset */
#define MC_CMD_HDR_FLAGS_S	32	/* Flags field size*/
#define MC_CMD_HDR_FLAGS_MASK	0xFF00FF00 /* Command flags mask */

#define MC_CMD_HDR_READ_STATUS(_hdr) \
	((enum mc_cmd_status)mc_dec((_hdr), \
		MC_CMD_HDR_STATUS_O, MC_CMD_HDR_STATUS_S))

#define MC_CMD_HDR_READ_TOKEN(_hdr) \
	((u16)mc_dec((_hdr), MC_CMD_HDR_TOKEN_O, MC_CMD_HDR_TOKEN_S))

#define MC_CMD_HDR_READ_FLAGS(_hdr) \
	((u32)mc_dec((_hdr), MC_CMD_HDR_FLAGS_O, MC_CMD_HDR_FLAGS_S))

#define MC_EXT_OP(_ext, _param, _offset, _width, _type, _arg) \
	((_ext)[_param] |= mc_enc((_offset), (_width), _arg))

#define MC_CMD_OP(_cmd, _param, _offset, _width, _type, _arg) \
	((_cmd).params[_param] |= mc_enc((_offset), (_width), _arg))

#define MC_RSP_OP(_cmd, _param, _offset, _width, _type, _arg) \
	(_arg = (_type)mc_dec(_cmd.params[_param], (_offset), (_width)))

static inline u64 mc_encode_cmd_header(u16 cmd_id,
				       u32 cmd_flags,
				       u16 token)
{
	u64 hdr;

	hdr = mc_enc(MC_CMD_HDR_CMDID_O, MC_CMD_HDR_CMDID_S, cmd_id);
	hdr |= mc_enc(MC_CMD_HDR_FLAGS_O, MC_CMD_HDR_FLAGS_S,
		       (cmd_flags & MC_CMD_HDR_FLAGS_MASK));
	hdr |= mc_enc(MC_CMD_HDR_TOKEN_O, MC_CMD_HDR_TOKEN_S, token);
	hdr |= mc_enc(MC_CMD_HDR_STATUS_O, MC_CMD_HDR_STATUS_S,
		       MC_CMD_STATUS_READY);

	return hdr;
}

#endif /* __FSL_MC_CMD_H */
