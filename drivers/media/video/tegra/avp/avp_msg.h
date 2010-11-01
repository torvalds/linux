/* drivers/media/video/tegra/avp/avp_msg.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Dima Zavin <dima@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MEDIA_VIDEO_TEGRA_AVP_MSG_H
#define __MEDIA_VIDEO_TEGRA_AVP_MSG_H

#include <linux/tegra_avp.h>
#include <linux/types.h>

/* Note: the port name string is not NUL terminated, so make sure to
 * allocate appropriate space locally when operating on the string */
#define XPC_PORT_NAME_LEN		16

#define SVC_ARGS_MAX_LEN		220
#define SVC_MAX_STRING_LEN		200

#define AVP_ERR_ENOTSUP			0x2
#define AVP_ERR_EINVAL			0x4
#define AVP_ERR_ENOMEM			0x6
#define AVP_ERR_EACCES			0x00030010

enum {
	SVC_NVMAP_CREATE		= 0,
	SVC_NVMAP_CREATE_RESPONSE	= 1,
	SVC_NVMAP_FREE			= 3,
	SVC_NVMAP_ALLOC			= 4,
	SVC_NVMAP_ALLOC_RESPONSE	= 5,
	SVC_NVMAP_PIN			= 6,
	SVC_NVMAP_PIN_RESPONSE		= 7,
	SVC_NVMAP_UNPIN			= 8,
	SVC_NVMAP_UNPIN_RESPONSE	= 9,
	SVC_NVMAP_GET_ADDRESS		= 10,
	SVC_NVMAP_GET_ADDRESS_RESPONSE	= 11,
	SVC_NVMAP_FROM_ID		= 12,
	SVC_NVMAP_FROM_ID_RESPONSE	= 13,
	SVC_MODULE_CLOCK		= 14,
	SVC_MODULE_CLOCK_RESPONSE	= 15,
	SVC_MODULE_RESET		= 16,
	SVC_MODULE_RESET_RESPONSE	= 17,
	SVC_POWER_REGISTER		= 18,
	SVC_POWER_UNREGISTER		= 19,
	SVC_POWER_STARVATION		= 20,
	SVC_POWER_BUSY_HINT		= 21,
	SVC_POWER_BUSY_HINT_MULTI	= 22,
	SVC_DFS_GETSTATE		= 23,
	SVC_DFS_GETSTATE_RESPONSE	= 24,
	SVC_POWER_RESPONSE		= 25,
	SVC_POWER_MAXFREQ		= 26,
	SVC_ENTER_LP0			= 27,
	SVC_ENTER_LP0_RESPONSE		= 28,
	SVC_PRINTF			= 29,
	SVC_LIBRARY_ATTACH		= 30,
	SVC_LIBRARY_ATTACH_RESPONSE	= 31,
	SVC_LIBRARY_DETACH		= 32,
	SVC_LIBRARY_DETACH_RESPONSE	= 33,
	SVC_AVP_WDT_RESET		= 34,
	SVC_DFS_GET_CLK_UTIL		= 35,
	SVC_DFS_GET_CLK_UTIL_RESPONSE	= 36,
};

struct svc_msg {
	u32	svc_id;
	u8	data[0];
};

struct svc_common_resp {
	u32	svc_id;
	u32	err;
};

struct svc_printf {
	u32		svc_id;
	const char	str[SVC_MAX_STRING_LEN];
};

struct svc_enter_lp0 {
	u32	svc_id;
	u32	src_addr;
	u32	buf_addr;
	u32	buf_size;
};

/* nvmap messages */
struct svc_nvmap_create {
	u32	svc_id;
	u32	size;
};

struct svc_nvmap_create_resp {
	u32	svc_id;
	u32	handle_id;
	u32	err;
};

enum {
	AVP_NVMAP_HEAP_EXTERNAL			= 1,
	AVP_NVMAP_HEAP_GART			= 2,
	AVP_NVMAP_HEAP_EXTERNAL_CARVEOUT	= 3,
	AVP_NVMAP_HEAP_IRAM			= 4,
};

struct svc_nvmap_alloc {
	u32	svc_id;
	u32	handle_id;
	u32	heaps[4];
	u32	num_heaps;
	u32	align;
	u32	mapping_type;
};

struct svc_nvmap_free {
	u32	svc_id;
	u32	handle_id;
};

struct svc_nvmap_pin {
	u32	svc_id;
	u32	handle_id;
};

struct svc_nvmap_pin_resp {
	u32	svc_id;
	u32	addr;
};

struct svc_nvmap_unpin {
	u32	svc_id;
	u32	handle_id;
};

struct svc_nvmap_from_id {
	u32	svc_id;
	u32	handle_id;
};

struct svc_nvmap_get_addr {
	u32	svc_id;
	u32	handle_id;
	u32	offs;
};

struct svc_nvmap_get_addr_resp {
	u32	svc_id;
	u32	addr;
};

/* library management messages */
enum {
	AVP_LIB_REASON_ATTACH		= 0,
	AVP_LIB_REASON_DETACH		= 1,
	AVP_LIB_REASON_ATTACH_GREEDY	= 2,
};

struct svc_lib_attach {
	u32	svc_id;
	u32	address;
	u32	args_len;
	u32	lib_size;
	u8	args[SVC_ARGS_MAX_LEN];
	u32	reason;
};

struct svc_lib_attach_resp {
	u32	svc_id;
	u32	err;
	u32	lib_id;
};

struct svc_lib_detach {
	u32	svc_id;
	u32	reason;
	u32	lib_id;
};

struct svc_lib_detach_resp {
	u32	svc_id;
	u32	err;
};

/* hw module management from the AVP side */
enum {
	AVP_MODULE_ID_AVP	= 2,
	AVP_MODULE_ID_VCP	= 3,
	AVP_MODULE_ID_BSEA	= 27,
	AVP_MODULE_ID_VDE	= 28,
	AVP_MODULE_ID_MPE	= 29,
};

struct svc_module_ctrl {
	u32	svc_id;
	u32	module_id;
	u32	client_id;
	u8	enable;
};

/* power messages */
struct svc_pwr_register {
	u32	svc_id;
	u32	client_id;
	u32	unused;
};

struct svc_pwr_register_resp {
	u32	svc_id;
	u32	err;
	u32	client_id;
};

struct svc_pwr_starve_hint {
	u32	svc_id;
	u32	dfs_clk_id;
	u32	client_id;
	u8	starving;
};

struct svc_pwr_busy_hint {
	u32	svc_id;
	u32	dfs_clk_id;
	u32	client_id;
	u32	boost_ms; /* duration */
	u32	boost_freq; /* in khz */
};

struct svc_pwr_max_freq {
	u32	svc_id;
	u32	module_id;
};

struct svc_pwr_max_freq_resp {
	u32	svc_id;
	u32	freq;
};

/* dfs related messages */
enum {
	AVP_DFS_STATE_INVALID		= 0,
	AVP_DFS_STATE_DISABLED		= 1,
	AVP_DFS_STATE_STOPPED		= 2,
	AVP_DFS_STATE_CLOSED_LOOP	= 3,
	AVP_DFS_STATE_PROFILED_LOOP	= 4,
};

struct svc_dfs_get_state_resp {
	u32	svc_id;
	u32	state;
};

enum {
	AVP_DFS_CLK_CPU		= 1,
	AVP_DFS_CLK_AVP		= 2,
	AVP_DFS_CLK_SYSTEM	= 3,
	AVP_DFS_CLK_AHB		= 4,
	AVP_DFS_CLK_APB		= 5,
	AVP_DFS_CLK_VDE		= 6,
	/* external memory controller */
	AVP_DFS_CLK_EMC		= 7,
};

struct avp_clk_usage {
	u32	min;
	u32	max;
	u32	curr_min;
	u32	curr_max;
	u32	curr;
	u32	avg; /* average activity.. whatever that means */
};

struct svc_dfs_get_clk_util {
	u32	svc_id;
	u32	dfs_clk_id;
};

/* all units are in kHz */
struct svc_dfs_get_clk_util_resp {
	u32			svc_id;
	u32			err;
	struct avp_clk_usage	usage;
};

/************************/

enum {
	CMD_ACK		= 0,
	CMD_CONNECT	= 2,
	CMD_DISCONNECT	= 3,
	CMD_MESSAGE	= 4,
	CMD_RESPONSE	= 5,
};

struct msg_data {
	u32		cmd;
	u8		data[0];
};

struct msg_ack {
	u32		cmd;
	u32		arg;
};

struct msg_connect {
	u32		cmd;
	u32		port_id;
	/* not NUL terminated, just 0 padded */
	char		name[XPC_PORT_NAME_LEN];
};

struct msg_connect_reply {
	u32		cmd;
	u32		port_id;
};

struct msg_disconnect {
	u32		cmd;
	u32		port_id;
};

struct msg_disconnect_reply {
	u32		cmd;
	u32		ack;
};

struct msg_port_data {
	u32		cmd;
	u32		port_id;
	u32		msg_len;
	u8		data[0];
};

#endif
