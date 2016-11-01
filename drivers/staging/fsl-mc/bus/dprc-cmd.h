/* Copyright 2013-2016 Freescale Semiconductor Inc.
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

/*************************************************************************//*
 dprc-cmd.h

 defines dprc portal commands

 *//**************************************************************************/

#ifndef _FSL_DPRC_CMD_H
#define _FSL_DPRC_CMD_H

/* Minimal supported DPRC Version */
#define DPRC_MIN_VER_MAJOR			5
#define DPRC_MIN_VER_MINOR			0

/* Command IDs */
#define DPRC_CMDID_CLOSE			0x800
#define DPRC_CMDID_OPEN				0x805
#define DPRC_CMDID_CREATE			0x905

#define DPRC_CMDID_GET_ATTR			0x004
#define DPRC_CMDID_RESET_CONT			0x005

#define DPRC_CMDID_SET_IRQ			0x010
#define DPRC_CMDID_GET_IRQ			0x011
#define DPRC_CMDID_SET_IRQ_ENABLE		0x012
#define DPRC_CMDID_GET_IRQ_ENABLE		0x013
#define DPRC_CMDID_SET_IRQ_MASK			0x014
#define DPRC_CMDID_GET_IRQ_MASK			0x015
#define DPRC_CMDID_GET_IRQ_STATUS		0x016
#define DPRC_CMDID_CLEAR_IRQ_STATUS		0x017

#define DPRC_CMDID_CREATE_CONT			0x151
#define DPRC_CMDID_DESTROY_CONT			0x152
#define DPRC_CMDID_SET_RES_QUOTA		0x155
#define DPRC_CMDID_GET_RES_QUOTA		0x156
#define DPRC_CMDID_ASSIGN			0x157
#define DPRC_CMDID_UNASSIGN			0x158
#define DPRC_CMDID_GET_OBJ_COUNT		0x159
#define DPRC_CMDID_GET_OBJ			0x15A
#define DPRC_CMDID_GET_RES_COUNT		0x15B
#define DPRC_CMDID_GET_RES_IDS			0x15C
#define DPRC_CMDID_GET_OBJ_REG			0x15E
#define DPRC_CMDID_SET_OBJ_IRQ			0x15F
#define DPRC_CMDID_GET_OBJ_IRQ			0x160
#define DPRC_CMDID_SET_OBJ_LABEL		0x161
#define DPRC_CMDID_GET_OBJ_DESC			0x162

#define DPRC_CMDID_CONNECT			0x167
#define DPRC_CMDID_DISCONNECT			0x168
#define DPRC_CMDID_GET_POOL			0x169
#define DPRC_CMDID_GET_POOL_COUNT		0x16A

#define DPRC_CMDID_GET_CONNECTION		0x16C

struct dprc_cmd_open {
	__le32 container_id;
};

struct dprc_cmd_create_container {
	/* cmd word 0 */
	__le32 options;
	__le16 icid;
	__le16 pad0;
	/* cmd word 1 */
	__le32 pad1;
	__le32 portal_id;
	/* cmd words 2-3 */
	u8 label[16];
};

struct dprc_rsp_create_container {
	/* response word 0 */
	__le64 pad0;
	/* response word 1 */
	__le32 child_container_id;
	__le32 pad1;
	/* response word 2 */
	__le64 child_portal_addr;
};

struct dprc_cmd_destroy_container {
	__le32 child_container_id;
};

struct dprc_cmd_reset_container {
	__le32 child_container_id;
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

struct dprc_cmd_get_irq {
	__le32 pad;
	u8 irq_index;
};

struct dprc_rsp_get_irq {
	/* response word 0 */
	__le32 irq_val;
	__le32 pad;
	/* response word 1 */
	__le64 irq_addr;
	/* response word 2 */
	__le32 irq_num;
	__le32 type;
};

#define DPRC_ENABLE		0x1

struct dprc_cmd_set_irq_enable {
	u8 enable;
	u8 pad[3];
	u8 irq_index;
};

struct dprc_cmd_get_irq_enable {
	__le32 pad;
	u8 irq_index;
};

struct dprc_rsp_get_irq_enable {
	u8 enabled;
};

struct dprc_cmd_set_irq_mask {
	__le32 mask;
	u8 irq_index;
};

struct dprc_cmd_get_irq_mask {
	__le32 pad;
	u8 irq_index;
};

struct dprc_rsp_get_irq_mask {
	__le32 mask;
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
	/* response word 2 */
	__le16 version_major;
	__le16 version_minor;
};

struct dprc_cmd_set_res_quota {
	/* cmd word 0 */
	__le32 child_container_id;
	__le16 quota;
	__le16 pad;
	/* cmd words 1-2 */
	u8 type[16];
};

struct dprc_cmd_get_res_quota {
	/* cmd word 0 */
	__le32 child_container_id;
	__le32 pad;
	/* cmd word 1-2 */
	u8 type[16];
};

struct dprc_rsp_get_res_quota {
	__le32 pad;
	__le16 quota;
};

struct dprc_cmd_assign {
	/* cmd word 0 */
	__le32 container_id;
	__le32 options;
	/* cmd word 1 */
	__le32 num;
	__le32 id_base_align;
	/* cmd word 2-3 */
	u8 type[16];
};

struct dprc_cmd_unassign {
	/* cmd word 0 */
	__le32 child_container_id;
	__le32 options;
	/* cmd word 1 */
	__le32 num;
	__le32 id_base_align;
	/* cmd word 2-3 */
	u8 type[16];
};

struct dprc_rsp_get_pool_count {
	__le32 pool_count;
};

struct dprc_cmd_get_pool {
	__le32 pool_index;
};

struct dprc_rsp_get_pool {
	/* response word 0 */
	__le64 pad;
	/* response word 1-2 */
	u8 type[16];
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

struct dprc_cmd_get_obj_desc {
	/* cmd word 0 */
	__le32 obj_id;
	__le32 pad;
	/* cmd word 1-2 */
	u8 type[16];
};

struct dprc_rsp_get_obj_desc {
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

struct dprc_cmd_get_res_count {
	/* cmd word 0 */
	__le64 pad;
	/* cmd word 1-2 */
	u8 type[16];
};

struct dprc_rsp_get_res_count {
	__le32 res_count;
};

struct dprc_cmd_get_res_ids {
	/* cmd word 0 */
	u8 pad0[5];
	u8 iter_status;
	__le16 pad1;
	/* cmd word 1 */
	__le32 base_id;
	__le32 last_id;
	/* cmd word 2-3 */
	u8 type[16];
};

struct dprc_rsp_get_res_ids {
	/* response word 0 */
	u8 pad0[5];
	u8 iter_status;
	__le16 pad1;
	/* response word 1 */
	__le32 base_id;
	__le32 last_id;
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

struct dprc_cmd_set_obj_label {
	/* cmd word 0 */
	__le32 obj_id;
	__le32 pad;
	/* cmd word 1-2 */
	u8 label[16];
	/* cmd word 3-4 */
	u8 obj_type[16];
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

struct dprc_cmd_get_obj_irq {
	/* cmd word 0 */
	__le32 obj_id;
	u8 irq_index;
	u8 pad[3];
	/* cmd word 1-2 */
	u8 obj_type[16];
};

struct dprc_rsp_get_obj_irq {
	/* response word 0 */
	__le32 irq_val;
	__le32 pad;
	/* response word 1 */
	__le64 irq_addr;
	/* response word 2 */
	__le32 irq_num;
	__le32 type;
};

struct dprc_cmd_connect {
	/* cmd word 0 */
	__le32 ep1_id;
	__le32 ep1_interface_id;
	/* cmd word 1 */
	__le32 ep2_id;
	__le32 ep2_interface_id;
	/* cmd word 2-3 */
	u8 ep1_type[16];
	/* cmd word 4 */
	__le32 max_rate;
	__le32 committed_rate;
	/* cmd word 5-6 */
	u8 ep2_type[16];
};

struct dprc_cmd_disconnect {
	/* cmd word 0 */
	__le32 id;
	__le32 interface_id;
	/* cmd word 1-2 */
	u8 type[16];
};

struct dprc_cmd_get_connection {
	/* cmd word 0 */
	__le32 ep1_id;
	__le32 ep1_interface_id;
	/* cmd word 1-2 */
	u8 ep1_type[16];
};

struct dprc_rsp_get_connection {
	/* response word 0-2 */
	__le64 pad[3];
	/* response word 3 */
	__le32 ep2_id;
	__le32 ep2_interface_id;
	/* response word 4-5 */
	u8 ep2_type[16];
	/* response word 6 */
	__le32 state;
};

#endif /* _FSL_DPRC_CMD_H */
