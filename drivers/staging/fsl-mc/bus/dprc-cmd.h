/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the above-listed copyright holders nor the
 *       names of any contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
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

/*
 * dprc-cmd.h
 *
 * defines dprc portal commands
 *
 */

#ifndef _FSL_DPRC_CMD_H
#define _FSL_DPRC_CMD_H

/* Minimal supported DPRC Version */
#define DPRC_MIN_VER_MAJOR			6
#define DPRC_MIN_VER_MINOR			0

/* Command versioning */
#define DPRC_CMD_BASE_VERSION			1
#define DPRC_CMD_ID_OFFSET			4

#define DPRC_CMD(id)	(((id) << DPRC_CMD_ID_OFFSET) | DPRC_CMD_BASE_VERSION)

/* Command IDs */
#define DPRC_CMDID_CLOSE                        DPRC_CMD(0x800)
#define DPRC_CMDID_OPEN                         DPRC_CMD(0x805)
#define DPRC_CMDID_GET_API_VERSION              DPRC_CMD(0xa05)

#define DPRC_CMDID_GET_ATTR                     DPRC_CMD(0x004)

#define DPRC_CMDID_SET_IRQ                      DPRC_CMD(0x010)
#define DPRC_CMDID_SET_IRQ_ENABLE               DPRC_CMD(0x012)
#define DPRC_CMDID_SET_IRQ_MASK                 DPRC_CMD(0x014)
#define DPRC_CMDID_GET_IRQ_STATUS               DPRC_CMD(0x016)
#define DPRC_CMDID_CLEAR_IRQ_STATUS             DPRC_CMD(0x017)

#define DPRC_CMDID_GET_CONT_ID                  DPRC_CMD(0x830)
#define DPRC_CMDID_GET_OBJ_COUNT                DPRC_CMD(0x159)
#define DPRC_CMDID_GET_OBJ                      DPRC_CMD(0x15A)
#define DPRC_CMDID_GET_OBJ_REG                  DPRC_CMD(0x15E)
#define DPRC_CMDID_SET_OBJ_IRQ                  DPRC_CMD(0x15F)

struct dprc_cmd_open {
	__le32 container_id;
};

struct dprc_cmd_set_irq {
	/* cmd word 0 */
	__le32 irq_val;
	u8 irq_index;
	u8 pad[3];
	/* cmd word 1 */
	__le64 irq_addr;
	/* cmd word 2 */
	__le32 irq_num;
};

#define DPRC_ENABLE		0x1

struct dprc_cmd_set_irq_enable {
	u8 enable;
	u8 pad[3];
	u8 irq_index;
};

struct dprc_cmd_set_irq_mask {
	__le32 mask;
	u8 irq_index;
};

struct dprc_cmd_get_irq_status {
	__le32 status;
	u8 irq_index;
};

struct dprc_rsp_get_irq_status {
	__le32 status;
};

struct dprc_cmd_clear_irq_status {
	__le32 status;
	u8 irq_index;
};

struct dprc_rsp_get_attributes {
	/* response word 0 */
	__le32 container_id;
	__le16 icid;
	__le16 pad;
	/* response word 1 */
	__le32 options;
	__le32 portal_id;
};

struct dprc_rsp_get_obj_count {
	__le32 pad;
	__le32 obj_count;
};

struct dprc_cmd_get_obj {
	__le32 obj_index;
};

struct dprc_rsp_get_obj {
	/* response word 0 */
	__le32 pad0;
	__le32 id;
	/* response word 1 */
	__le16 vendor;
	u8 irq_count;
	u8 region_count;
	__le32 state;
	/* response word 2 */
	__le16 version_major;
	__le16 version_minor;
	__le16 flags;
	__le16 pad1;
	/* response word 3-4 */
	u8 type[16];
	/* response word 5-6 */
	u8 label[16];
};

struct dprc_cmd_get_obj_region {
	/* cmd word 0 */
	__le32 obj_id;
	__le16 pad0;
	u8 region_index;
	u8 pad1;
	/* cmd word 1-2 */
	__le64 pad2[2];
	/* cmd word 3-4 */
	u8 obj_type[16];
};

struct dprc_rsp_get_obj_region {
	/* response word 0 */
	__le64 pad;
	/* response word 1 */
	__le64 base_addr;
	/* response word 2 */
	__le32 size;
};

struct dprc_cmd_set_obj_irq {
	/* cmd word 0 */
	__le32 irq_val;
	u8 irq_index;
	u8 pad[3];
	/* cmd word 1 */
	__le64 irq_addr;
	/* cmd word 2 */
	__le32 irq_num;
	__le32 obj_id;
	/* cmd word 3-4 */
	u8 obj_type[16];
};

#endif /* _FSL_DPRC_CMD_H */
