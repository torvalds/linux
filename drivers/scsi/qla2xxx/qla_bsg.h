/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2011 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#ifndef __QLA_BSG_H
#define __QLA_BSG_H

/* BSG Vendor specific commands */
#define QL_VND_LOOPBACK		0x01
#define QL_VND_A84_RESET	0x02
#define QL_VND_A84_UPDATE_FW	0x03
#define QL_VND_A84_MGMT_CMD	0x04
#define QL_VND_IIDMA		0x05
#define QL_VND_FCP_PRIO_CFG_CMD	0x06
#define QL_VND_READ_FLASH	0x07
#define QL_VND_UPDATE_FLASH	0x08

/* BSG definations for interpreting CommandSent field */
#define INT_DEF_LB_LOOPBACK_CMD         0
#define INT_DEF_LB_ECHO_CMD             1

/* Loopback related definations */
#define EXTERNAL_LOOPBACK		0xF2
#define ENABLE_INTERNAL_LOOPBACK	0x02
#define INTERNAL_LOOPBACK_MASK		0x000E
#define MAX_ELS_FRAME_PAYLOAD		252
#define ELS_OPCODE_BYTE			0x10

/* BSG Vendor specific definations */
#define A84_ISSUE_WRITE_TYPE_CMD        0
#define A84_ISSUE_READ_TYPE_CMD         1
#define A84_CLEANUP_CMD                 2
#define A84_ISSUE_RESET_OP_FW           3
#define A84_ISSUE_RESET_DIAG_FW         4
#define A84_ISSUE_UPDATE_OPFW_CMD       5
#define A84_ISSUE_UPDATE_DIAGFW_CMD     6

struct qla84_mgmt_param {
	union {
		struct {
			uint32_t start_addr;
		} mem; /* for QLA84_MGMT_READ/WRITE_MEM */
		struct {
			uint32_t id;
#define QLA84_MGMT_CONFIG_ID_UIF        1
#define QLA84_MGMT_CONFIG_ID_FCOE_COS   2
#define QLA84_MGMT_CONFIG_ID_PAUSE      3
#define QLA84_MGMT_CONFIG_ID_TIMEOUTS   4

		uint32_t param0;
		uint32_t param1;
	} config; /* for QLA84_MGMT_CHNG_CONFIG */

	struct {
		uint32_t type;
#define QLA84_MGMT_INFO_CONFIG_LOG_DATA         1 /* Get Config Log Data */
#define QLA84_MGMT_INFO_LOG_DATA                2 /* Get Log Data */
#define QLA84_MGMT_INFO_PORT_STAT               3 /* Get Port Statistics */
#define QLA84_MGMT_INFO_LIF_STAT                4 /* Get LIF Statistics  */
#define QLA84_MGMT_INFO_ASIC_STAT               5 /* Get ASIC Statistics */
#define QLA84_MGMT_INFO_CONFIG_PARAMS           6 /* Get Config Parameters */
#define QLA84_MGMT_INFO_PANIC_LOG               7 /* Get Panic Log */

		uint32_t context;
/*
* context definitions for QLA84_MGMT_INFO_CONFIG_LOG_DATA
*/
#define IC_LOG_DATA_LOG_ID_DEBUG_LOG                    0
#define IC_LOG_DATA_LOG_ID_LEARN_LOG                    1
#define IC_LOG_DATA_LOG_ID_FC_ACL_INGRESS_LOG           2
#define IC_LOG_DATA_LOG_ID_FC_ACL_EGRESS_LOG            3
#define IC_LOG_DATA_LOG_ID_ETHERNET_ACL_INGRESS_LOG     4
#define IC_LOG_DATA_LOG_ID_ETHERNET_ACL_EGRESS_LOG      5
#define IC_LOG_DATA_LOG_ID_MESSAGE_TRANSMIT_LOG         6
#define IC_LOG_DATA_LOG_ID_MESSAGE_RECEIVE_LOG          7
#define IC_LOG_DATA_LOG_ID_LINK_EVENT_LOG               8
#define IC_LOG_DATA_LOG_ID_DCX_LOG                      9

/*
* context definitions for QLA84_MGMT_INFO_PORT_STAT
*/
#define IC_PORT_STATISTICS_PORT_NUMBER_ETHERNET_PORT0   0
#define IC_PORT_STATISTICS_PORT_NUMBER_ETHERNET_PORT1   1
#define IC_PORT_STATISTICS_PORT_NUMBER_NSL_PORT0        2
#define IC_PORT_STATISTICS_PORT_NUMBER_NSL_PORT1        3
#define IC_PORT_STATISTICS_PORT_NUMBER_FC_PORT0         4
#define IC_PORT_STATISTICS_PORT_NUMBER_FC_PORT1         5


/*
* context definitions for QLA84_MGMT_INFO_LIF_STAT
*/
#define IC_LIF_STATISTICS_LIF_NUMBER_ETHERNET_PORT0     0
#define IC_LIF_STATISTICS_LIF_NUMBER_ETHERNET_PORT1     1
#define IC_LIF_STATISTICS_LIF_NUMBER_FC_PORT0           2
#define IC_LIF_STATISTICS_LIF_NUMBER_FC_PORT1           3
#define IC_LIF_STATISTICS_LIF_NUMBER_CPU                6

		} info; /* for QLA84_MGMT_GET_INFO */
	} u;
};

struct qla84_msg_mgmt {
	uint16_t cmd;
#define QLA84_MGMT_READ_MEM     0x00
#define QLA84_MGMT_WRITE_MEM    0x01
#define QLA84_MGMT_CHNG_CONFIG  0x02
#define QLA84_MGMT_GET_INFO     0x03
	uint16_t rsrvd;
	struct qla84_mgmt_param mgmtp;/* parameters for cmd */
	uint32_t len; /* bytes in payload following this struct */
	uint8_t payload[0]; /* payload for cmd */
};

struct qla_bsg_a84_mgmt {
	struct qla84_msg_mgmt mgmt;
} __attribute__ ((packed));

struct qla_scsi_addr {
	uint16_t bus;
	uint16_t target;
} __attribute__ ((packed));

struct qla_ext_dest_addr {
	union {
		uint8_t wwnn[8];
		uint8_t wwpn[8];
		uint8_t id[4];
		struct qla_scsi_addr scsi_addr;
	} dest_addr;
	uint16_t dest_type;
#define	EXT_DEF_TYPE_WWPN	2
	uint16_t lun;
	uint16_t padding[2];
} __attribute__ ((packed));

struct qla_port_param {
	struct qla_ext_dest_addr fc_scsi_addr;
	uint16_t mode;
	uint16_t speed;
} __attribute__ ((packed));
#endif
