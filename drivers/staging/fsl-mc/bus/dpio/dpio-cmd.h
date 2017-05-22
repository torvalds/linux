/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
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
#ifndef _FSL_DPIO_CMD_H
#define _FSL_DPIO_CMD_H

/* DPIO Version */
#define DPIO_VER_MAJOR			4
#define DPIO_VER_MINOR			2

/* Command Versioning */

#define DPIO_CMD_ID_OFFSET		4
#define DPIO_CMD_BASE_VERSION		1

#define DPIO_CMD(id)	(((id) << DPIO_CMD_ID_OFFSET) | DPIO_CMD_BASE_VERSION)

/* Command IDs */
#define DPIO_CMDID_CLOSE				DPIO_CMD(0x800)
#define DPIO_CMDID_OPEN					DPIO_CMD(0x803)
#define DPIO_CMDID_GET_API_VERSION			DPIO_CMD(0xa03)
#define DPIO_CMDID_ENABLE				DPIO_CMD(0x002)
#define DPIO_CMDID_DISABLE				DPIO_CMD(0x003)
#define DPIO_CMDID_GET_ATTR				DPIO_CMD(0x004)

struct dpio_cmd_open {
	__le32 dpio_id;
};

#define DPIO_CHANNEL_MODE_MASK		0x3

struct dpio_rsp_get_attr {
	/* cmd word 0 */
	__le32 id;
	__le16 qbman_portal_id;
	u8 num_priorities;
	u8 channel_mode;
	/* cmd word 1 */
	__le64 qbman_portal_ce_addr;
	/* cmd word 2 */
	__le64 qbman_portal_ci_addr;
	/* cmd word 3 */
	__le32 qbman_version;
};

#endif /* _FSL_DPIO_CMD_H */
