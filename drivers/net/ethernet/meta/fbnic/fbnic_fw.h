/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_FW_H_
#define _FBNIC_FW_H_

#include <linux/if_ether.h>
#include <linux/types.h>

struct fbnic_dev;
struct fbnic_tlv_msg;

struct fbnic_fw_mbx {
	u8 ready, head, tail;
	struct {
		struct fbnic_tlv_msg	*msg;
		dma_addr_t		addr;
	} buf_info[FBNIC_IPC_MBX_DESC_LEN];
};

// FW_VER_MAX_SIZE must match ETHTOOL_FWVERS_LEN
#define FBNIC_FW_VER_MAX_SIZE	                32
// Formatted version is in the format XX.YY.ZZ_RRR_COMMIT
#define FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE	(FBNIC_FW_VER_MAX_SIZE - 13)
#define FBNIC_FW_LOG_MAX_SIZE	                256

struct fbnic_fw_ver {
	u32 version;
	char commit[FBNIC_FW_CAP_RESP_COMMIT_MAX_SIZE];
};

struct fbnic_fw_cap {
	struct {
		struct fbnic_fw_ver mgmt, bootloader;
	} running;
	struct {
		struct fbnic_fw_ver mgmt, bootloader, undi;
	} stored;
	u8	active_slot;
	u8	bmc_mac_addr[4][ETH_ALEN];
	u8	bmc_present	: 1;
	u8	all_multi	: 1;
	u8	link_speed;
	u8	link_fec;
};

struct fbnic_fw_completion {
	u32 msg_type;
	struct completion done;
	struct kref ref_count;
	int result;
	union {
		struct {
			s32 millivolts;
			s32 millidegrees;
		} tsene;
	} u;
};

void fbnic_mbx_init(struct fbnic_dev *fbd);
void fbnic_mbx_clean(struct fbnic_dev *fbd);
void fbnic_mbx_poll(struct fbnic_dev *fbd);
int fbnic_mbx_poll_tx_ready(struct fbnic_dev *fbd);
void fbnic_mbx_flush_tx(struct fbnic_dev *fbd);
int fbnic_fw_xmit_ownership_msg(struct fbnic_dev *fbd, bool take_ownership);
int fbnic_fw_init_heartbeat(struct fbnic_dev *fbd, bool poll);
void fbnic_fw_check_heartbeat(struct fbnic_dev *fbd);
int fbnic_fw_xmit_tsene_read_msg(struct fbnic_dev *fbd,
				 struct fbnic_fw_completion *cmpl_data);
void fbnic_fw_init_cmpl(struct fbnic_fw_completion *cmpl_data,
			u32 msg_type);
void fbnic_fw_clear_compl(struct fbnic_dev *fbd);
void fbnic_fw_put_cmpl(struct fbnic_fw_completion *cmpl_data);

#define fbnic_mk_full_fw_ver_str(_rev_id, _delim, _commit, _str, _str_sz) \
do {									\
	const u32 __rev_id = _rev_id;					\
	snprintf(_str, _str_sz, "%02lu.%02lu.%02lu-%03lu%s%s",	\
		 FIELD_GET(FBNIC_FW_CAP_RESP_VERSION_MAJOR, __rev_id),	\
		 FIELD_GET(FBNIC_FW_CAP_RESP_VERSION_MINOR, __rev_id),	\
		 FIELD_GET(FBNIC_FW_CAP_RESP_VERSION_PATCH, __rev_id),	\
		 FIELD_GET(FBNIC_FW_CAP_RESP_VERSION_BUILD, __rev_id),	\
		 _delim, _commit);					\
} while (0)

#define fbnic_mk_fw_ver_str(_rev_id, _str) \
	fbnic_mk_full_fw_ver_str(_rev_id, "", "", _str, sizeof(_str))

#define FW_HEARTBEAT_PERIOD		(10 * HZ)

enum {
	FBNIC_TLV_MSG_ID_HOST_CAP_REQ			= 0x10,
	FBNIC_TLV_MSG_ID_FW_CAP_RESP			= 0x11,
	FBNIC_TLV_MSG_ID_OWNERSHIP_REQ			= 0x12,
	FBNIC_TLV_MSG_ID_OWNERSHIP_RESP			= 0x13,
	FBNIC_TLV_MSG_ID_HEARTBEAT_REQ			= 0x14,
	FBNIC_TLV_MSG_ID_HEARTBEAT_RESP			= 0x15,
	FBNIC_TLV_MSG_ID_TSENE_READ_REQ			= 0x3C,
	FBNIC_TLV_MSG_ID_TSENE_READ_RESP		= 0x3D,
};

#define FBNIC_FW_CAP_RESP_VERSION_MAJOR		CSR_GENMASK(31, 24)
#define FBNIC_FW_CAP_RESP_VERSION_MINOR		CSR_GENMASK(23, 16)
#define FBNIC_FW_CAP_RESP_VERSION_PATCH		CSR_GENMASK(15, 8)
#define FBNIC_FW_CAP_RESP_VERSION_BUILD		CSR_GENMASK(7, 0)
enum {
	FBNIC_FW_CAP_RESP_VERSION			= 0x0,
	FBNIC_FW_CAP_RESP_BMC_PRESENT			= 0x1,
	FBNIC_FW_CAP_RESP_BMC_MAC_ADDR			= 0x2,
	FBNIC_FW_CAP_RESP_BMC_MAC_ARRAY			= 0x3,
	FBNIC_FW_CAP_RESP_STORED_VERSION		= 0x4,
	FBNIC_FW_CAP_RESP_ACTIVE_FW_SLOT		= 0x5,
	FBNIC_FW_CAP_RESP_VERSION_COMMIT_STR		= 0x6,
	FBNIC_FW_CAP_RESP_BMC_ALL_MULTI			= 0x8,
	FBNIC_FW_CAP_RESP_FW_STATE			= 0x9,
	FBNIC_FW_CAP_RESP_FW_LINK_SPEED			= 0xa,
	FBNIC_FW_CAP_RESP_FW_LINK_FEC			= 0xb,
	FBNIC_FW_CAP_RESP_STORED_COMMIT_STR		= 0xc,
	FBNIC_FW_CAP_RESP_CMRT_VERSION			= 0xd,
	FBNIC_FW_CAP_RESP_STORED_CMRT_VERSION		= 0xe,
	FBNIC_FW_CAP_RESP_CMRT_COMMIT_STR		= 0xf,
	FBNIC_FW_CAP_RESP_STORED_CMRT_COMMIT_STR	= 0x10,
	FBNIC_FW_CAP_RESP_UEFI_VERSION			= 0x11,
	FBNIC_FW_CAP_RESP_UEFI_COMMIT_STR		= 0x12,
	FBNIC_FW_CAP_RESP_MSG_MAX
};

enum {
	FBNIC_FW_LINK_SPEED_25R1		= 1,
	FBNIC_FW_LINK_SPEED_50R2		= 2,
	FBNIC_FW_LINK_SPEED_50R1		= 3,
	FBNIC_FW_LINK_SPEED_100R2		= 4,
};

enum {
	FBNIC_FW_LINK_FEC_NONE			= 1,
	FBNIC_FW_LINK_FEC_RS			= 2,
	FBNIC_FW_LINK_FEC_BASER			= 3,
};

enum {
	FBNIC_TSENE_THERM			= 0x0,
	FBNIC_TSENE_VOLT			= 0x1,
	FBNIC_TSENE_ERROR			= 0x2,
	FBNIC_TSENE_MSG_MAX
};

enum {
	FBNIC_FW_OWNERSHIP_FLAG			= 0x0,
	FBNIC_FW_OWNERSHIP_MSG_MAX
};
#endif /* _FBNIC_FW_H_ */
