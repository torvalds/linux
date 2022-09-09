/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared CARL9170 Header
 *
 * Firmware descriptor format
 *
 * Copyright 2009-2011 Christian Lamparter <chunkeey@googlemail.com>
 */

#ifndef __CARL9170_SHARED_FWDESC_H
#define __CARL9170_SHARED_FWDESC_H

/* NOTE: Don't mess with the order of the flags! */
enum carl9170fw_feature_list {
	/* Always set */
	CARL9170FW_DUMMY_FEATURE,

	/*
	 * Indicates that this image has special boot block which prevents
	 * legacy drivers to drive the firmware.
	 */
	CARL9170FW_MINIBOOT,

	/* usb registers are initialized by the firmware */
	CARL9170FW_USB_INIT_FIRMWARE,

	/* command traps & notifications are send through EP2 */
	CARL9170FW_USB_RESP_EP2,

	/* usb download (app -> fw) stream */
	CARL9170FW_USB_DOWN_STREAM,

	/* usb upload (fw -> app) stream */
	CARL9170FW_USB_UP_STREAM,

	/* unusable - reserved to flag non-functional debug firmwares */
	CARL9170FW_UNUSABLE,

	/* AR9170_CMD_RF_INIT, AR9170_CMD_FREQ_START, AR9170_CMD_FREQUENCY */
	CARL9170FW_COMMAND_PHY,

	/* AR9170_CMD_EKEY, AR9170_CMD_DKEY */
	CARL9170FW_COMMAND_CAM,

	/* Firmware has a software Content After Beacon Queueing mechanism */
	CARL9170FW_WLANTX_CAB,

	/* The firmware is capable of responding to incoming BAR frames */
	CARL9170FW_HANDLE_BACK_REQ,

	/* GPIO Interrupt | CARL9170_RSP_GPIO */
	CARL9170FW_GPIO_INTERRUPT,

	/* Firmware PSM support | CARL9170_CMD_PSM */
	CARL9170FW_PSM,

	/* Firmware RX filter | CARL9170_CMD_RX_FILTER */
	CARL9170FW_RX_FILTER,

	/* Wake up on WLAN */
	CARL9170FW_WOL,

	/* Firmware supports PSM in the 5GHZ Band */
	CARL9170FW_FIXED_5GHZ_PSM,

	/* HW (ANI, CCA, MIB) tally counters */
	CARL9170FW_HW_COUNTERS,

	/* Firmware will pass BA when BARs are queued */
	CARL9170FW_RX_BA_FILTER,

	/* Firmware has support to write a byte at a time */
	CARL9170FW_HAS_WREGB_CMD,

	/* Pattern generator */
	CARL9170FW_PATTERN_GENERATOR,

	/* KEEP LAST */
	__CARL9170FW_FEATURE_NUM
};

#define OTUS_MAGIC	"OTAR"
#define MOTD_MAGIC	"MOTD"
#define FIX_MAGIC	"FIX\0"
#define DBG_MAGIC	"DBG\0"
#define CHK_MAGIC	"CHK\0"
#define TXSQ_MAGIC	"TXSQ"
#define WOL_MAGIC	"WOL\0"
#define LAST_MAGIC	"LAST"

#define CARL9170FW_SET_DAY(d) (((d) - 1) % 31)
#define CARL9170FW_SET_MONTH(m) ((((m) - 1) % 12) * 31)
#define CARL9170FW_SET_YEAR(y) (((y) - 10) * 372)

#define CARL9170FW_GET_DAY(d) (((d) % 31) + 1)
#define CARL9170FW_GET_MONTH(m) ((((m) / 31) % 12) + 1)
#define CARL9170FW_GET_YEAR(y) ((y) / 372 + 10)

#define CARL9170FW_MAGIC_SIZE			4

struct carl9170fw_desc_head {
	u8	magic[CARL9170FW_MAGIC_SIZE];
	__le16 length;
	u8 min_ver;
	u8 cur_ver;
} __packed;
#define CARL9170FW_DESC_HEAD_SIZE			\
	(sizeof(struct carl9170fw_desc_head))

#define CARL9170FW_OTUS_DESC_MIN_VER		6
#define CARL9170FW_OTUS_DESC_CUR_VER		7
struct carl9170fw_otus_desc {
	struct carl9170fw_desc_head head;
	__le32 feature_set;
	__le32 fw_address;
	__le32 bcn_addr;
	__le16 bcn_len;
	__le16 miniboot_size;
	__le16 tx_frag_len;
	__le16 rx_max_frame_len;
	u8 tx_descs;
	u8 cmd_bufs;
	u8 api_ver;
	u8 vif_num;
} __packed;
#define CARL9170FW_OTUS_DESC_SIZE			\
	(sizeof(struct carl9170fw_otus_desc))

#define CARL9170FW_MOTD_STRING_LEN			24
#define CARL9170FW_MOTD_RELEASE_LEN			20
#define CARL9170FW_MOTD_DESC_MIN_VER			1
#define CARL9170FW_MOTD_DESC_CUR_VER			2
struct carl9170fw_motd_desc {
	struct carl9170fw_desc_head head;
	__le32 fw_year_month_day;
	char desc[CARL9170FW_MOTD_STRING_LEN];
	char release[CARL9170FW_MOTD_RELEASE_LEN];
} __packed;
#define CARL9170FW_MOTD_DESC_SIZE			\
	(sizeof(struct carl9170fw_motd_desc))

#define CARL9170FW_FIX_DESC_MIN_VER			1
#define CARL9170FW_FIX_DESC_CUR_VER			2
struct carl9170fw_fix_entry {
	__le32 address;
	__le32 mask;
	__le32 value;
} __packed;

struct carl9170fw_fix_desc {
	struct carl9170fw_desc_head head;
	struct carl9170fw_fix_entry data[];
} __packed;
#define CARL9170FW_FIX_DESC_SIZE			\
	(sizeof(struct carl9170fw_fix_desc))

#define CARL9170FW_DBG_DESC_MIN_VER			1
#define CARL9170FW_DBG_DESC_CUR_VER			3
struct carl9170fw_dbg_desc {
	struct carl9170fw_desc_head head;

	__le32 bogoclock_addr;
	__le32 counter_addr;
	__le32 rx_total_addr;
	__le32 rx_overrun_addr;
	__le32 rx_filter;

	/* Put your debugging definitions here */
} __packed;
#define CARL9170FW_DBG_DESC_SIZE			\
	(sizeof(struct carl9170fw_dbg_desc))

#define CARL9170FW_CHK_DESC_MIN_VER			1
#define CARL9170FW_CHK_DESC_CUR_VER			2
struct carl9170fw_chk_desc {
	struct carl9170fw_desc_head head;
	__le32 fw_crc32;
	__le32 hdr_crc32;
} __packed;
#define CARL9170FW_CHK_DESC_SIZE			\
	(sizeof(struct carl9170fw_chk_desc))

#define CARL9170FW_TXSQ_DESC_MIN_VER			1
#define CARL9170FW_TXSQ_DESC_CUR_VER			1
struct carl9170fw_txsq_desc {
	struct carl9170fw_desc_head head;

	__le32 seq_table_addr;
} __packed;
#define CARL9170FW_TXSQ_DESC_SIZE			\
	(sizeof(struct carl9170fw_txsq_desc))

#define CARL9170FW_WOL_DESC_MIN_VER			1
#define CARL9170FW_WOL_DESC_CUR_VER			1
struct carl9170fw_wol_desc {
	struct carl9170fw_desc_head head;

	__le32 supported_triggers;	/* CARL9170_WOL_ */
} __packed;
#define CARL9170FW_WOL_DESC_SIZE			\
	(sizeof(struct carl9170fw_wol_desc))

#define CARL9170FW_LAST_DESC_MIN_VER			1
#define CARL9170FW_LAST_DESC_CUR_VER			2
struct carl9170fw_last_desc {
	struct carl9170fw_desc_head head;
} __packed;
#define CARL9170FW_LAST_DESC_SIZE			\
	(sizeof(struct carl9170fw_fix_desc))

#define CARL9170FW_DESC_MAX_LENGTH			8192

#define CARL9170FW_FILL_DESC(_magic, _length, _min_ver, _cur_ver)	\
	.head = {							\
		.magic = _magic,					\
		.length = cpu_to_le16(_length),				\
		.min_ver = _min_ver,					\
		.cur_ver = _cur_ver,					\
	}

static inline void carl9170fw_fill_desc(struct carl9170fw_desc_head *head,
					 u8 magic[CARL9170FW_MAGIC_SIZE],
					 __le16 length, u8 min_ver, u8 cur_ver)
{
	head->magic[0] = magic[0];
	head->magic[1] = magic[1];
	head->magic[2] = magic[2];
	head->magic[3] = magic[3];

	head->length = length;
	head->min_ver = min_ver;
	head->cur_ver = cur_ver;
}

#define carl9170fw_for_each_hdr(desc, fw_desc)				\
	for (desc = fw_desc;						\
	     memcmp(desc->magic, LAST_MAGIC, CARL9170FW_MAGIC_SIZE) &&	\
	     le16_to_cpu(desc->length) >= CARL9170FW_DESC_HEAD_SIZE &&	\
	     le16_to_cpu(desc->length) < CARL9170FW_DESC_MAX_LENGTH;	\
	     desc = (void *)((unsigned long)desc + le16_to_cpu(desc->length)))

#define CHECK_HDR_VERSION(head, _min_ver)				\
	(((head)->cur_ver < _min_ver) || ((head)->min_ver > _min_ver))	\

static inline bool carl9170fw_supports(__le32 list, u8 feature)
{
	return le32_to_cpu(list) & BIT(feature);
}

static inline bool carl9170fw_desc_cmp(const struct carl9170fw_desc_head *head,
				       const u8 descid[CARL9170FW_MAGIC_SIZE],
				       u16 min_len, u8 compatible_revision)
{
	if (descid[0] == head->magic[0] && descid[1] == head->magic[1] &&
	    descid[2] == head->magic[2] && descid[3] == head->magic[3] &&
	    !CHECK_HDR_VERSION(head, compatible_revision) &&
	    (le16_to_cpu(head->length) >= min_len))
		return true;

	return false;
}

#define CARL9170FW_MIN_SIZE	32
#define CARL9170FW_MAX_SIZE	16384

static inline bool carl9170fw_size_check(unsigned int len)
{
	return (len <= CARL9170FW_MAX_SIZE && len >= CARL9170FW_MIN_SIZE);
}

#endif /* __CARL9170_SHARED_FWDESC_H */
