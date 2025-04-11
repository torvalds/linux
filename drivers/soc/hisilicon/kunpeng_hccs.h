/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2023 Hisilicon Limited. */

#ifndef __KUNPENG_HCCS_H__
#define __KUNPENG_HCCS_H__

/*
 * |---------------  Chip0  ---------------|----------------  ChipN  -------------|
 * |--------Die0-------|--------DieN-------|--------Die0-------|-------DieN-------|
 * | P0 | P1 | P2 | P3 | P0 | P1 | P2 | P3 | P0 | P1 | P2 | P3 |P0 | P1 | P2 | P3 |
 */

enum hccs_port_type {
	HCCS_V1 = 1,
	HCCS_V2,
};

#define HCCS_IP_PREFIX	"HCCS-v"
#define HCCS_IP_MAX		255
#define HCCS_NAME_MAX_LEN	9
struct hccs_type_name_map {
	u8 type;
	char name[HCCS_NAME_MAX_LEN + 1];
};

/*
 * This value cannot be 255, otherwise the loop of the multi-BD communication
 * case cannot end.
 */
#define HCCS_DIE_MAX_PORT_ID	254

struct hccs_port_info {
	u8 port_id;
	u8 port_type;
	u8 max_lane_num;
	bool enable; /* if the port is enabled */
	struct kobject kobj;
	bool dir_created;
	struct hccs_die_info *die; /* point to the die the port is located */
};

struct hccs_die_info {
	u8 die_id;
	u8 port_num;
	u8 min_port_id;
	u8 max_port_id;
	struct hccs_port_info *ports;
	struct kobject kobj;
	bool dir_created;
	struct hccs_chip_info *chip; /* point to the chip the die is located */
};

struct hccs_chip_info {
	u8 chip_id;
	u8 die_num;
	struct hccs_die_info *dies;
	struct kobject kobj;
	struct hccs_dev *hdev;
};

struct hccs_mbox_client_info {
	struct mbox_client client;
	struct pcc_mbox_chan *pcc_chan;
	u64 deadline_us;
	struct completion done;
};

struct hccs_desc;

struct hccs_verspecific_data {
	void (*rx_callback)(struct mbox_client *cl, void *mssg);
	int (*wait_cmd_complete)(struct hccs_dev *hdev);
	void (*fill_pcc_shared_mem)(struct hccs_dev *hdev,
				    u8 cmd, struct hccs_desc *desc,
				    void __iomem *comm_space,
				    u16 space_size);
	u16 shared_mem_size;
	bool has_txdone_irq;
};

#define HCCS_CAPS_HCCS_V2_PM	BIT_ULL(0)

struct hccs_dev {
	struct device *dev;
	struct acpi_device *acpi_dev;
	const struct hccs_verspecific_data *verspec_data;
	/* device capabilities from firmware, like HCCS_CAPS_xxx. */
	u64 caps;
	u8 chip_num;
	struct hccs_chip_info *chips;
	u16 used_type_num;
	struct hccs_type_name_map *type_name_maps;
	u8 chan_id;
	struct mutex lock;
	struct hccs_mbox_client_info cl_info;
};

#define HCCS_SERDES_MODULE_CODE 0x32
enum hccs_subcmd_type {
	HCCS_GET_CHIP_NUM = 0x1,
	HCCS_GET_DIE_NUM,
	HCCS_GET_DIE_INFO,
	HCCS_GET_DIE_PORT_INFO,
	HCCS_GET_DEV_CAP,
	HCCS_GET_PORT_LINK_STATUS,
	HCCS_GET_PORT_CRC_ERR_CNT,
	HCCS_GET_DIE_PORTS_LANE_STA,
	HCCS_GET_DIE_PORTS_LINK_STA,
	HCCS_GET_DIE_PORTS_CRC_ERR_CNT,
	HCCS_GET_PORT_IDLE_STATUS,
	HCCS_PM_DEC_LANE,
	HCCS_PM_INC_LANE,
	HCCS_SUB_CMD_MAX = 255,
};

struct hccs_die_num_req_param {
	u8 chip_id;
};

struct hccs_die_info_req_param {
	u8 chip_id;
	u8 die_idx;
};

struct hccs_die_info_rsp_data {
	u8 die_id;
	u8 port_num;
	u8 min_port_id;
	u8 max_port_id;
};

struct hccs_port_attr {
	u8 port_id;
	u8 port_type;
	u8 max_lane_num;
	u8 enable : 1; /* if the port is enabled */
	u16 rsv[2];
};

/*
 * The common command request for getting the information of all HCCS port on
 * specified DIE.
 */
struct hccs_die_comm_req_param {
	u8 chip_id;
	u8 die_id; /* id in hardware */
};

/* The common command request for getting the information of a specific port */
struct hccs_port_comm_req_param {
	u8 chip_id;
	u8 die_id;
	u8 port_id;
};

#define HCCS_PREPARE_INC_LANE	1
#define HCCS_GET_ADAPT_RES	2
#define HCCS_START_RETRAINING	3
struct hccs_inc_lane_req_param {
	u8 port_type;
	u8 opt_type;
};

#define HCCS_PORT_RESET         1
#define HCCS_PORT_SETUP         2
#define HCCS_PORT_CONFIG        3
#define HCCS_PORT_READY         4
struct hccs_link_status {
	u8 lane_mask; /* indicate which lanes are used. */
	u8 link_fsm : 3; /* link fsm, 1: reset 2: setup 3: config 4: link-up */
	u8 lane_num : 5; /* current lane number */
};

struct hccs_req_head {
	u8 module_code; /* set to 0x32 for serdes */
	u8 start_id;
	u8 rsv[2];
};

struct hccs_rsp_head {
	u8 data_len;
	u8 next_id;
	u8 rsv[2];
};

struct hccs_fw_inner_head {
	u8 retStatus; /* 0: success, other: failure */
	u8 rsv[7];
};

#define HCCS_PCC_SHARE_MEM_BYTES	64
#define HCCS_FW_INNER_HEAD_BYTES	8
#define HCCS_RSP_HEAD_BYTES		4

#define HCCS_MAX_RSP_DATA_BYTES		(HCCS_PCC_SHARE_MEM_BYTES - \
					 HCCS_FW_INNER_HEAD_BYTES - \
					 HCCS_RSP_HEAD_BYTES)
#define HCCS_MAX_RSP_DATA_SIZE_MAX	(HCCS_MAX_RSP_DATA_BYTES / 4)

/*
 * Note: Actual available size of data field also depands on the PCC header
 * bytes of the specific type. Driver needs to copy the response data in the
 * communication space based on the real length.
 */
struct hccs_rsp_desc {
	struct hccs_fw_inner_head fw_inner_head; /* 8 Bytes */
	struct hccs_rsp_head rsp_head; /* 4 Bytes */
	u32 data[HCCS_MAX_RSP_DATA_SIZE_MAX];
};

#define HCCS_REQ_HEAD_BYTES		4
#define HCCS_MAX_REQ_DATA_BYTES		(HCCS_PCC_SHARE_MEM_BYTES - \
					 HCCS_REQ_HEAD_BYTES)
#define HCCS_MAX_REQ_DATA_SIZE_MAX	(HCCS_MAX_REQ_DATA_BYTES / 4)

/*
 * Note: Actual available size of data field also depands on the PCC header
 * bytes of the specific type. Driver needs to copy the request data to the
 * communication space based on the real length.
 */
struct hccs_req_desc {
	struct hccs_req_head req_head; /* 4 Bytes */
	u32 data[HCCS_MAX_REQ_DATA_SIZE_MAX];
};

struct hccs_desc {
	union {
		struct hccs_req_desc req;
		struct hccs_rsp_desc rsp;
	};
};

#endif /* __KUNPENG_HCCS_H__ */
