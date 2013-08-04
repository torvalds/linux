/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#ifndef __QLCNIC_83XX_HW_H
#define __QLCNIC_83XX_HW_H

#include <linux/types.h>
#include <linux/etherdevice.h>
#include "qlcnic_hw.h"

#define QLCNIC_83XX_BAR0_LENGTH 0x4000

/* Directly mapped registers */
#define QLC_83XX_CRB_WIN_BASE		0x3800
#define QLC_83XX_CRB_WIN_FUNC(f)	(QLC_83XX_CRB_WIN_BASE+((f)*4))
#define QLC_83XX_SEM_LOCK_BASE		0x3840
#define QLC_83XX_SEM_UNLOCK_BASE	0x3844
#define QLC_83XX_SEM_LOCK_FUNC(f)	(QLC_83XX_SEM_LOCK_BASE+((f)*8))
#define QLC_83XX_SEM_UNLOCK_FUNC(f)	(QLC_83XX_SEM_UNLOCK_BASE+((f)*8))
#define QLC_83XX_LINK_STATE(f)		(0x3698+((f) > 7 ? 4 : 0))
#define QLC_83XX_LINK_SPEED(f)		(0x36E0+(((f) >> 2) * 4))
#define QLC_83XX_LINK_SPEED_FACTOR	10
#define QLC_83xx_FUNC_VAL(v, f)	((v) & (1 << (f * 4)))
#define QLC_83XX_INTX_PTR		0x38C0
#define QLC_83XX_INTX_TRGR		0x38C4
#define QLC_83XX_INTX_MASK		0x38C8

#define QLC_83XX_DRV_LOCK_WAIT_COUNTER			100
#define QLC_83XX_DRV_LOCK_WAIT_DELAY			20
#define QLC_83XX_NEED_DRV_LOCK_RECOVERY		1
#define QLC_83XX_DRV_LOCK_RECOVERY_IN_PROGRESS		2
#define QLC_83XX_MAX_DRV_LOCK_RECOVERY_ATTEMPT		3
#define QLC_83XX_DRV_LOCK_RECOVERY_DELAY		200
#define QLC_83XX_DRV_LOCK_RECOVERY_STATUS_MASK		0x3
#define QLC_83XX_LB_WAIT_COUNT				250
#define QLC_83XX_LB_MSLEEP_COUNT			20
#define QLC_83XX_NO_NIC_RESOURCE	0x5
#define QLC_83XX_MAC_PRESENT		0xC
#define QLC_83XX_MAC_ABSENT		0xD


#define QLC_83XX_FLASH_SECTOR_SIZE		(64 * 1024)

/* PEG status definitions */
#define QLC_83XX_CMDPEG_COMPLETE		0xff01
#define QLC_83XX_VALID_INTX_BIT30(val)		((val) & BIT_30)
#define QLC_83XX_VALID_INTX_BIT31(val)		((val) & BIT_31)
#define QLC_83XX_INTX_FUNC(val)		((val) & 0xFF)
#define QLC_83XX_LEGACY_INTX_MAX_RETRY		100
#define QLC_83XX_LEGACY_INTX_DELAY		4
#define QLC_83XX_REG_DESC			1
#define QLC_83XX_LRO_DESC			2
#define QLC_83XX_CTRL_DESC			3
#define QLC_83XX_FW_CAPABILITY_TSO		BIT_6
#define QLC_83XX_FW_CAP_LRO_MSS		BIT_17
#define QLC_83XX_HOST_RDS_MODE_UNIQUE		0
#define QLC_83XX_HOST_SDS_MBX_IDX		8

#define QLCNIC_HOST_RDS_MBX_IDX			88
#define QLCNIC_MAX_RING_SETS			8

/* Pause control registers */
#define QLC_83XX_SRE_SHIM_REG		0x0D200284
#define QLC_83XX_PORT0_THRESHOLD	0x0B2003A4
#define QLC_83XX_PORT1_THRESHOLD	0x0B2013A4
#define QLC_83XX_PORT0_TC_MC_REG	0x0B200388
#define QLC_83XX_PORT1_TC_MC_REG	0x0B201388
#define QLC_83XX_PORT0_TC_STATS		0x0B20039C
#define QLC_83XX_PORT1_TC_STATS		0x0B20139C
#define QLC_83XX_PORT2_IFB_THRESHOLD	0x0B200704
#define QLC_83XX_PORT3_IFB_THRESHOLD	0x0B201704

/* Peg PC status registers */
#define QLC_83XX_CRB_PEG_NET_0		0x3400003c
#define QLC_83XX_CRB_PEG_NET_1		0x3410003c
#define QLC_83XX_CRB_PEG_NET_2		0x3420003c
#define QLC_83XX_CRB_PEG_NET_3		0x3430003c
#define QLC_83XX_CRB_PEG_NET_4		0x34b0003c

/* Firmware image definitions */
#define QLC_83XX_BOOTLOADER_FLASH_ADDR	0x10000
#define QLC_83XX_FW_FILE_NAME		"83xx_fw.bin"
#define QLC_83XX_BOOT_FROM_FLASH	0
#define QLC_83XX_BOOT_FROM_FILE		0x12345678

#define QLC_83XX_MAX_RESET_SEQ_ENTRIES	16

#define QLC_83XX_MBX_POST_BC_OP		0x1
#define QLC_83XX_MBX_COMPLETION		0x0
#define QLC_83XX_MBX_REQUEST		0x1

#define QLC_83XX_MBX_TIMEOUT		(5 * HZ)
#define QLC_83XX_MBX_CMD_LOOP		5000000

/* status descriptor mailbox data
 * @phy_addr_{low|high}: physical address of buffer
 * @sds_ring_size: buffer size
 * @intrpt_id: interrupt id
 * @intrpt_val: source of interrupt
 */
struct qlcnic_sds_mbx {
	u32	phy_addr_low;
	u32	phy_addr_high;
	u32	rsvd1[4];
#if defined(__LITTLE_ENDIAN)
	u16	sds_ring_size;
	u16	rsvd2;
	u16	rsvd3[2];
	u16	intrpt_id;
	u8	intrpt_val;
	u8	rsvd4;
#elif defined(__BIG_ENDIAN)
	u16	rsvd2;
	u16	sds_ring_size;
	u16	rsvd3[2];
	u8	rsvd4;
	u8	intrpt_val;
	u16	intrpt_id;
#endif
	u32	rsvd5;
} __packed;

/* receive descriptor buffer data
 * phy_addr_reg_{low|high}: physical address of regular buffer
 * phy_addr_jmb_{low|high}: physical address of jumbo buffer
 * reg_ring_sz: size of regular buffer
 * reg_ring_len: no. of entries in regular buffer
 * jmb_ring_len: no. of entries in jumbo buffer
 * jmb_ring_sz: size of jumbo buffer
 */
struct qlcnic_rds_mbx {
	u32	phy_addr_reg_low;
	u32	phy_addr_reg_high;
	u32	phy_addr_jmb_low;
	u32	phy_addr_jmb_high;
#if defined(__LITTLE_ENDIAN)
	u16	reg_ring_sz;
	u16	reg_ring_len;
	u16	jmb_ring_sz;
	u16	jmb_ring_len;
#elif defined(__BIG_ENDIAN)
	u16	reg_ring_len;
	u16	reg_ring_sz;
	u16	jmb_ring_len;
	u16	jmb_ring_sz;
#endif
} __packed;

/* host producers for regular and jumbo rings */
struct __host_producer_mbx {
	u32	reg_buf;
	u32	jmb_buf;
} __packed;

/* Receive context mailbox data outbox registers
 * @state: state of the context
 * @vport_id: virtual port id
 * @context_id: receive context id
 * @num_pci_func: number of pci functions of the port
 * @phy_port: physical port id
 */
struct qlcnic_rcv_mbx_out {
#if defined(__LITTLE_ENDIAN)
	u8	rcv_num;
	u8	sts_num;
	u16	ctx_id;
	u8	state;
	u8	num_pci_func;
	u8	phy_port;
	u8	vport_id;
#elif defined(__BIG_ENDIAN)
	u16	ctx_id;
	u8	sts_num;
	u8	rcv_num;
	u8	vport_id;
	u8	phy_port;
	u8	num_pci_func;
	u8	state;
#endif
	u32	host_csmr[QLCNIC_MAX_RING_SETS];
	struct __host_producer_mbx host_prod[QLCNIC_MAX_RING_SETS];
} __packed;

struct qlcnic_add_rings_mbx_out {
#if defined(__LITTLE_ENDIAN)
	u8      rcv_num;
	u8      sts_num;
	u16	ctx_id;
#elif defined(__BIG_ENDIAN)
	u16	ctx_id;
	u8	sts_num;
	u8	rcv_num;
#endif
	u32  host_csmr[QLCNIC_MAX_RING_SETS];
	struct __host_producer_mbx host_prod[QLCNIC_MAX_RING_SETS];
} __packed;

/* Transmit context mailbox inbox registers
 * @phys_addr_{low|high}: DMA address of the transmit buffer
 * @cnsmr_index_{low|high}: host consumer index
 * @size: legth of transmit buffer ring
 * @intr_id: interrput id
 * @src: src of interrupt
 */
struct qlcnic_tx_mbx {
	u32	phys_addr_low;
	u32	phys_addr_high;
	u32	cnsmr_index_low;
	u32	cnsmr_index_high;
#if defined(__LITTLE_ENDIAN)
	u16	size;
	u16	intr_id;
	u8	src;
	u8	rsvd[3];
#elif defined(__BIG_ENDIAN)
	u16	intr_id;
	u16	size;
	u8	rsvd[3];
	u8	src;
#endif
} __packed;

/* Transmit context mailbox outbox registers
 * @host_prod: host producer index
 * @ctx_id: transmit context id
 * @state: state of the transmit context
 */

struct qlcnic_tx_mbx_out {
	u32	host_prod;
#if defined(__LITTLE_ENDIAN)
	u16	ctx_id;
	u8	state;
	u8	rsvd;
#elif defined(__BIG_ENDIAN)
	u8	rsvd;
	u8	state;
	u16	ctx_id;
#endif
} __packed;

struct qlcnic_intrpt_config {
	u8	type;
	u8	enabled;
	u16	id;
	u32	src;
};

struct qlcnic_macvlan_mbx {
#if defined(__LITTLE_ENDIAN)
	u8	mac_addr0;
	u8	mac_addr1;
	u8	mac_addr2;
	u8	mac_addr3;
	u8	mac_addr4;
	u8	mac_addr5;
	u16	vlan;
#elif defined(__BIG_ENDIAN)
	u8	mac_addr3;
	u8	mac_addr2;
	u8	mac_addr1;
	u8	mac_addr0;
	u16	vlan;
	u8	mac_addr5;
	u8	mac_addr4;
#endif
};

struct qlc_83xx_fw_info {
	const struct firmware	*fw;
	u16	major_fw_version;
	u8	minor_fw_version;
	u8	sub_fw_version;
	u8	fw_build_num;
	u8	load_from_file;
};

struct qlc_83xx_reset {
	struct qlc_83xx_reset_hdr *hdr;
	int	seq_index;
	int	seq_error;
	int	array_index;
	u32	array[QLC_83XX_MAX_RESET_SEQ_ENTRIES];
	u8	*buff;
	u8	*stop_offset;
	u8	*start_offset;
	u8	*init_offset;
	u8	seq_end;
	u8	template_end;
};

#define QLC_83XX_IDC_DISABLE_FW_RESET_RECOVERY		0x1
#define QLC_83XX_IDC_GRACEFULL_RESET			0x2
#define QLC_83XX_IDC_TIMESTAMP				0
#define QLC_83XX_IDC_DURATION				1
#define QLC_83XX_IDC_INIT_TIMEOUT_SECS			30
#define QLC_83XX_IDC_RESET_ACK_TIMEOUT_SECS		10
#define QLC_83XX_IDC_RESET_TIMEOUT_SECS		10
#define QLC_83XX_IDC_QUIESCE_ACK_TIMEOUT_SECS		20
#define QLC_83XX_IDC_FW_POLL_DELAY			(1 * HZ)
#define QLC_83XX_IDC_FW_FAIL_THRESH			2
#define QLC_83XX_IDC_MAX_FUNC_PER_PARTITION_INFO	8
#define QLC_83XX_IDC_MAX_CNA_FUNCTIONS			16
#define QLC_83XX_IDC_MAJOR_VERSION			1
#define QLC_83XX_IDC_MINOR_VERSION			0
#define QLC_83XX_IDC_FLASH_PARAM_ADDR			0x3e8020

struct qlcnic_adapter;
struct qlc_83xx_idc {
	int (*state_entry) (struct qlcnic_adapter *);
	u64		sec_counter;
	u64		delay;
	unsigned long	status;
	int		err_code;
	int		collect_dump;
	u8		curr_state;
	u8		prev_state;
	u8		vnic_state;
	u8		vnic_wait_limit;
	u8		quiesce_req;
	u8		delay_reset;
	char		**name;
};

/* Device States */
enum qlcnic_83xx_states {
	QLC_83XX_IDC_DEV_UNKNOWN,
	QLC_83XX_IDC_DEV_COLD,
	QLC_83XX_IDC_DEV_INIT,
	QLC_83XX_IDC_DEV_READY,
	QLC_83XX_IDC_DEV_NEED_RESET,
	QLC_83XX_IDC_DEV_NEED_QUISCENT,
	QLC_83XX_IDC_DEV_FAILED,
	QLC_83XX_IDC_DEV_QUISCENT
};

#define QLCNIC_MBX_RSP(reg)		LSW(reg)
#define QLCNIC_MBX_NUM_REGS(reg)	(MSW(reg) & 0x1FF)
#define QLCNIC_MBX_STATUS(reg)		(((reg) >> 25) & 0x7F)
#define QLCNIC_MBX_HOST(ahw, i)	((ahw)->pci_base0 + ((i) * 4))
#define QLCNIC_MBX_FW(ahw, i)		((ahw)->pci_base0 + 0x800 + ((i) * 4))

/* Mailbox process AEN count */
#define QLC_83XX_IDC_COMP_AEN			3
#define QLC_83XX_MBX_AEN_CNT			5
#define QLC_83XX_MODULE_LOADED			1
#define QLC_83XX_MBX_READY			2
#define QLC_83XX_MBX_AEN_ACK			3
#define QLC_83XX_SFP_PRESENT(data)		((data) & 3)
#define QLC_83XX_SFP_ERR(data)			(((data) >> 2) & 3)
#define QLC_83XX_SFP_MODULE_TYPE(data)		(((data) >> 4) & 0x1F)
#define QLC_83XX_SFP_CU_LENGTH(data)		(LSB((data) >> 16))
#define QLC_83XX_SFP_TX_FAULT(data)		((data) & BIT_10)
#define QLC_83XX_SFP_10G_CAPABLE(data)		((data) & BIT_11)
#define QLC_83XX_LINK_STATS(data)		((data) & BIT_0)
#define QLC_83XX_CURRENT_LINK_SPEED(data)	(((data) >> 3) & 7)
#define QLC_83XX_LINK_PAUSE(data)		(((data) >> 6) & 3)
#define QLC_83XX_LINK_LB(data)			(((data) >> 8) & 7)
#define QLC_83XX_LINK_FEC(data)		((data) & BIT_12)
#define QLC_83XX_LINK_EEE(data)		((data) & BIT_13)
#define QLC_83XX_DCBX(data)			(((data) >> 28) & 7)
#define QLC_83XX_AUTONEG(data)			((data) & BIT_15)
#define QLC_83XX_CFG_STD_PAUSE			(1 << 5)
#define QLC_83XX_CFG_STD_TX_PAUSE		(1 << 20)
#define QLC_83XX_CFG_STD_RX_PAUSE		(2 << 20)
#define QLC_83XX_CFG_STD_TX_RX_PAUSE		(3 << 20)
#define QLC_83XX_ENABLE_AUTONEG		(1 << 15)
#define QLC_83XX_CFG_LOOPBACK_HSS		(2 << 1)
#define QLC_83XX_CFG_LOOPBACK_PHY		(3 << 1)
#define QLC_83XX_CFG_LOOPBACK_EXT		(4 << 1)

/* LED configuration settings */
#define QLC_83XX_ENABLE_BEACON		0xe
#define QLC_83XX_LED_RATE		0xff
#define QLC_83XX_LED_ACT		(1 << 10)
#define QLC_83XX_LED_MOD		(0 << 13)
#define QLC_83XX_LED_CONFIG	(QLC_83XX_LED_RATE | QLC_83XX_LED_ACT |	\
				 QLC_83XX_LED_MOD)

#define QLC_83XX_10M_LINK	1
#define QLC_83XX_100M_LINK	2
#define QLC_83XX_1G_LINK	3
#define QLC_83XX_10G_LINK	4
#define QLC_83XX_STAT_TX	3
#define QLC_83XX_STAT_RX	2
#define QLC_83XX_STAT_MAC	1
#define QLC_83XX_TX_STAT_REGS	14
#define QLC_83XX_RX_STAT_REGS	40
#define QLC_83XX_MAC_STAT_REGS	94

#define QLC_83XX_GET_FUNC_PRIVILEGE(VAL, FN)	(0x3 & ((VAL) >> (FN * 2)))
#define QLC_83XX_SET_FUNC_OPMODE(VAL, FN)	((VAL) << (FN * 2))
#define QLC_83XX_DEFAULT_OPMODE			0x55555555
#define QLC_83XX_PRIVLEGED_FUNC			0x1
#define QLC_83XX_VIRTUAL_FUNC				0x2

#define QLC_83XX_LB_MAX_FILTERS			2048
#define QLC_83XX_LB_BUCKET_SIZE			256
#define QLC_83XX_MINIMUM_VECTOR			3
#define QLC_83XX_MAX_MC_COUNT			38
#define QLC_83XX_MAX_UC_COUNT			4096

#define QLC_83XX_GET_FUNC_MODE_FROM_NPAR_INFO(val)	(val & 0x80000000)
#define QLC_83XX_GET_LRO_CAPABILITY(val)		(val & 0x20)
#define QLC_83XX_GET_LSO_CAPABILITY(val)		(val & 0x40)
#define QLC_83XX_GET_LSO_CAPABILITY(val)		(val & 0x40)
#define QLC_83XX_GET_HW_LRO_CAPABILITY(val)		(val & 0x400)
#define QLC_83XX_GET_VLAN_ALIGN_CAPABILITY(val)	(val & 0x4000)
#define QLC_83XX_GET_FW_LRO_MSS_CAPABILITY(val)	(val & 0x20000)
#define QLC_83XX_VIRTUAL_NIC_MODE			0xFF
#define QLC_83XX_DEFAULT_MODE				0x0
#define QLC_83XX_SRIOV_MODE				0x1
#define QLCNIC_BRDTYPE_83XX_10G			0x0083

#define QLC_83XX_FLASH_SPI_STATUS		0x2808E010
#define QLC_83XX_FLASH_SPI_CONTROL		0x2808E014
#define QLC_83XX_FLASH_STATUS			0x42100004
#define QLC_83XX_FLASH_CONTROL			0x42110004
#define QLC_83XX_FLASH_ADDR			0x42110008
#define QLC_83XX_FLASH_WRDATA			0x4211000C
#define QLC_83XX_FLASH_RDDATA			0x42110018
#define QLC_83XX_FLASH_DIRECT_WINDOW		0x42110030
#define QLC_83XX_FLASH_DIRECT_DATA(DATA)	(0x42150000 | (0x0000FFFF&DATA))
#define QLC_83XX_FLASH_SECTOR_ERASE_CMD	0xdeadbeef
#define QLC_83XX_FLASH_WRITE_CMD		0xdacdacda
#define QLC_83XX_FLASH_BULK_WRITE_CMD		0xcadcadca
#define QLC_83XX_FLASH_READ_RETRY_COUNT	5000
#define QLC_83XX_FLASH_STATUS_READY		0x6
#define QLC_83XX_FLASH_WRITE_MIN		2
#define QLC_83XX_FLASH_WRITE_MAX		64
#define QLC_83XX_FLASH_STATUS_REG_POLL_DELAY	1
#define QLC_83XX_ERASE_MODE			1
#define QLC_83XX_WRITE_MODE			2
#define QLC_83XX_BULK_WRITE_MODE		3
#define QLC_83XX_FLASH_FDT_WRITE_DEF_SIG	0xFD0100
#define QLC_83XX_FLASH_FDT_ERASE_DEF_SIG	0xFD0300
#define QLC_83XX_FLASH_FDT_READ_MFG_ID_VAL	0xFD009F
#define QLC_83XX_FLASH_OEM_ERASE_SIG		0xFD03D8
#define QLC_83XX_FLASH_OEM_WRITE_SIG		0xFD0101
#define QLC_83XX_FLASH_OEM_READ_SIG		0xFD0005
#define QLC_83XX_FLASH_ADDR_TEMP_VAL		0x00800000
#define QLC_83XX_FLASH_ADDR_SECOND_TEMP_VAL	0x00800001
#define QLC_83XX_FLASH_WRDATA_DEF		0x0
#define QLC_83XX_FLASH_READ_CTRL		0x3F
#define QLC_83XX_FLASH_SPI_CTRL		0x4
#define QLC_83XX_FLASH_FIRST_ERASE_MS_VAL	0x2
#define QLC_83XX_FLASH_SECOND_ERASE_MS_VAL	0x5
#define QLC_83XX_FLASH_LAST_ERASE_MS_VAL	0x3D
#define QLC_83XX_FLASH_FIRST_MS_PATTERN	0x43
#define QLC_83XX_FLASH_SECOND_MS_PATTERN	0x7F
#define QLC_83XX_FLASH_LAST_MS_PATTERN		0x7D
#define QLC_83xx_FLASH_MAX_WAIT_USEC		100
#define QLC_83XX_FLASH_LOCK_TIMEOUT		10000

enum qlc_83xx_mbx_cmd_type {
	QLC_83XX_MBX_CMD_WAIT = 0,
	QLC_83XX_MBX_CMD_NO_WAIT,
	QLC_83XX_MBX_CMD_BUSY_WAIT,
};

enum qlc_83xx_mbx_response_states {
	QLC_83XX_MBX_RESPONSE_WAIT = 0,
	QLC_83XX_MBX_RESPONSE_ARRIVED,
};

#define QLC_83XX_MBX_RESPONSE_FAILED	0x2
#define QLC_83XX_MBX_RESPONSE_UNKNOWN	0x3

/* Additional registers in 83xx */
enum qlc_83xx_ext_regs {
	QLCNIC_GLOBAL_RESET = 0,
	QLCNIC_WILDCARD,
	QLCNIC_INFORMANT,
	QLCNIC_HOST_MBX_CTRL,
	QLCNIC_FW_MBX_CTRL,
	QLCNIC_BOOTLOADER_ADDR,
	QLCNIC_BOOTLOADER_SIZE,
	QLCNIC_FW_IMAGE_ADDR,
	QLCNIC_MBX_INTR_ENBL,
	QLCNIC_DEF_INT_MASK,
	QLCNIC_DEF_INT_ID,
	QLC_83XX_IDC_MAJ_VERSION,
	QLC_83XX_IDC_DEV_STATE,
	QLC_83XX_IDC_DRV_PRESENCE,
	QLC_83XX_IDC_DRV_ACK,
	QLC_83XX_IDC_CTRL,
	QLC_83XX_IDC_DRV_AUDIT,
	QLC_83XX_IDC_MIN_VERSION,
	QLC_83XX_RECOVER_DRV_LOCK,
	QLC_83XX_IDC_PF_0,
	QLC_83XX_IDC_PF_1,
	QLC_83XX_IDC_PF_2,
	QLC_83XX_IDC_PF_3,
	QLC_83XX_IDC_PF_4,
	QLC_83XX_IDC_PF_5,
	QLC_83XX_IDC_PF_6,
	QLC_83XX_IDC_PF_7,
	QLC_83XX_IDC_PF_8,
	QLC_83XX_IDC_PF_9,
	QLC_83XX_IDC_PF_10,
	QLC_83XX_IDC_PF_11,
	QLC_83XX_IDC_PF_12,
	QLC_83XX_IDC_PF_13,
	QLC_83XX_IDC_PF_14,
	QLC_83XX_IDC_PF_15,
	QLC_83XX_IDC_DEV_PARTITION_INFO_1,
	QLC_83XX_IDC_DEV_PARTITION_INFO_2,
	QLC_83XX_DRV_OP_MODE,
	QLC_83XX_VNIC_STATE,
	QLC_83XX_DRV_LOCK,
	QLC_83XX_DRV_UNLOCK,
	QLC_83XX_DRV_LOCK_ID,
	QLC_83XX_ASIC_TEMP,
};

/* 83xx funcitons */
int qlcnic_83xx_get_fw_version(struct qlcnic_adapter *);
int qlcnic_83xx_issue_cmd(struct qlcnic_adapter *, struct qlcnic_cmd_args *);
int qlcnic_83xx_setup_intr(struct qlcnic_adapter *, u8);
void qlcnic_83xx_get_func_no(struct qlcnic_adapter *);
int qlcnic_83xx_cam_lock(struct qlcnic_adapter *);
void qlcnic_83xx_cam_unlock(struct qlcnic_adapter *);
int qlcnic_send_ctrl_op(struct qlcnic_adapter *, struct qlcnic_cmd_args *, u32);
void qlcnic_83xx_add_sysfs(struct qlcnic_adapter *);
void qlcnic_83xx_remove_sysfs(struct qlcnic_adapter *);
void qlcnic_83xx_write_crb(struct qlcnic_adapter *, char *, loff_t, size_t);
void qlcnic_83xx_read_crb(struct qlcnic_adapter *, char *, loff_t, size_t);
int qlcnic_83xx_rd_reg_indirect(struct qlcnic_adapter *, ulong, int *);
int qlcnic_83xx_wrt_reg_indirect(struct qlcnic_adapter *, ulong, u32);
void qlcnic_83xx_process_rcv_diag(struct qlcnic_adapter *, int, u64 []);
int qlcnic_83xx_nic_set_promisc(struct qlcnic_adapter *, u32);
int qlcnic_83xx_set_lb_mode(struct qlcnic_adapter *, u8);
int qlcnic_83xx_clear_lb_mode(struct qlcnic_adapter *, u8);
int qlcnic_83xx_config_hw_lro(struct qlcnic_adapter *, int);
int qlcnic_83xx_config_rss(struct qlcnic_adapter *, int);
int qlcnic_83xx_config_intr_coalesce(struct qlcnic_adapter *);
void qlcnic_83xx_change_l2_filter(struct qlcnic_adapter *, u64 *, u16);
int qlcnic_83xx_get_pci_info(struct qlcnic_adapter *, struct qlcnic_pci_info *);
int qlcnic_83xx_set_nic_info(struct qlcnic_adapter *, struct qlcnic_info *);
void qlcnic_83xx_register_nic_idc_func(struct qlcnic_adapter *, int);

int qlcnic_83xx_napi_add(struct qlcnic_adapter *, struct net_device *);
void qlcnic_83xx_napi_del(struct qlcnic_adapter *);
void qlcnic_83xx_napi_enable(struct qlcnic_adapter *);
void qlcnic_83xx_napi_disable(struct qlcnic_adapter *);
int qlcnic_83xx_config_led(struct qlcnic_adapter *, u32, u32);
void qlcnic_ind_wr(struct qlcnic_adapter *, u32, u32);
int qlcnic_ind_rd(struct qlcnic_adapter *, u32);
int qlcnic_83xx_create_rx_ctx(struct qlcnic_adapter *);
int qlcnic_83xx_create_tx_ctx(struct qlcnic_adapter *,
			      struct qlcnic_host_tx_ring *, int);
void qlcnic_83xx_del_rx_ctx(struct qlcnic_adapter *);
void qlcnic_83xx_del_tx_ctx(struct qlcnic_adapter *,
			    struct qlcnic_host_tx_ring *);
int qlcnic_83xx_get_nic_info(struct qlcnic_adapter *, struct qlcnic_info *, u8);
int qlcnic_83xx_setup_link_event(struct qlcnic_adapter *, int);
void qlcnic_83xx_process_rcv_ring_diag(struct qlcnic_host_sds_ring *);
int qlcnic_83xx_config_intrpt(struct qlcnic_adapter *, bool);
int qlcnic_83xx_sre_macaddr_change(struct qlcnic_adapter *, u8 *, u16, u8);
int qlcnic_83xx_get_mac_address(struct qlcnic_adapter *, u8 *);
void qlcnic_83xx_configure_mac(struct qlcnic_adapter *, u8 *, u8,
			       struct qlcnic_cmd_args *);
int qlcnic_83xx_alloc_mbx_args(struct qlcnic_cmd_args *,
			       struct qlcnic_adapter *, u32);
void qlcnic_free_mbx_args(struct qlcnic_cmd_args *);
void qlcnic_set_npar_data(struct qlcnic_adapter *, const struct qlcnic_info *,
			  struct qlcnic_info *);
void qlcnic_83xx_config_intr_coal(struct qlcnic_adapter *);
irqreturn_t qlcnic_83xx_handle_aen(int, void *);
int qlcnic_83xx_get_port_info(struct qlcnic_adapter *);
void qlcnic_83xx_enable_mbx_interrupt(struct qlcnic_adapter *);
void qlcnic_83xx_disable_mbx_intr(struct qlcnic_adapter *);
irqreturn_t qlcnic_83xx_clear_legacy_intr(struct qlcnic_adapter *);
irqreturn_t qlcnic_83xx_intr(int, void *);
irqreturn_t qlcnic_83xx_tmp_intr(int, void *);
void qlcnic_83xx_enable_intr(struct qlcnic_adapter *,
			     struct qlcnic_host_sds_ring *);
void qlcnic_83xx_disable_intr(struct qlcnic_adapter *,
			     struct qlcnic_host_sds_ring *);
void qlcnic_83xx_check_vf(struct qlcnic_adapter *,
			  const struct pci_device_id *);
void __qlcnic_83xx_process_aen(struct qlcnic_adapter *);
int qlcnic_83xx_get_port_config(struct qlcnic_adapter *);
int qlcnic_83xx_set_port_config(struct qlcnic_adapter *);
int qlcnic_enable_eswitch(struct qlcnic_adapter *, u8, u8);
int qlcnic_83xx_get_nic_configuration(struct qlcnic_adapter *);
int qlcnic_83xx_config_default_opmode(struct qlcnic_adapter *);
int qlcnic_83xx_setup_mbx_intr(struct qlcnic_adapter *);
void qlcnic_83xx_free_mbx_intr(struct qlcnic_adapter *);
void qlcnic_83xx_register_map(struct qlcnic_hardware_context *);
void qlcnic_83xx_idc_aen_work(struct work_struct *);
void qlcnic_83xx_config_ipaddr(struct qlcnic_adapter *, __be32, int);

int qlcnic_83xx_erase_flash_sector(struct qlcnic_adapter *, u32);
int qlcnic_83xx_flash_bulk_write(struct qlcnic_adapter *, u32, u32 *, int);
int qlcnic_83xx_flash_write32(struct qlcnic_adapter *, u32, u32 *);
int qlcnic_83xx_lock_flash(struct qlcnic_adapter *);
void qlcnic_83xx_unlock_flash(struct qlcnic_adapter *);
int qlcnic_83xx_save_flash_status(struct qlcnic_adapter *);
int qlcnic_83xx_restore_flash_status(struct qlcnic_adapter *, int);
int qlcnic_83xx_read_flash_mfg_id(struct qlcnic_adapter *);
int qlcnic_83xx_read_flash_descriptor_table(struct qlcnic_adapter *);
int qlcnic_83xx_flash_read32(struct qlcnic_adapter *, u32, u8 *, int);
int qlcnic_83xx_lockless_flash_read32(struct qlcnic_adapter *,
				      u32, u8 *, int);
int qlcnic_83xx_init(struct qlcnic_adapter *, int);
int qlcnic_83xx_idc_ready_state_entry(struct qlcnic_adapter *);
int qlcnic_83xx_check_hw_status(struct qlcnic_adapter *p_dev);
void qlcnic_83xx_idc_poll_dev_state(struct work_struct *);
int qlcnic_83xx_get_reset_instruction_template(struct qlcnic_adapter *);
void qlcnic_83xx_idc_exit(struct qlcnic_adapter *);
void qlcnic_83xx_idc_request_reset(struct qlcnic_adapter *, u32);
int qlcnic_83xx_lock_driver(struct qlcnic_adapter *);
void qlcnic_83xx_unlock_driver(struct qlcnic_adapter *);
int qlcnic_83xx_set_default_offload_settings(struct qlcnic_adapter *);
int qlcnic_83xx_ms_mem_write128(struct qlcnic_adapter *, u64, u32 *, u32);
int qlcnic_83xx_idc_vnic_pf_entry(struct qlcnic_adapter *);
int qlcnic_83xx_enable_vnic_mode(struct qlcnic_adapter *, int);
int qlcnic_83xx_disable_vnic_mode(struct qlcnic_adapter *, int);
int qlcnic_83xx_config_vnic_opmode(struct qlcnic_adapter *);
int qlcnic_83xx_get_vnic_vport_info(struct qlcnic_adapter *,
				    struct qlcnic_info *, u8);
int qlcnic_83xx_get_vnic_pf_info(struct qlcnic_adapter *, struct qlcnic_info *);

void qlcnic_83xx_get_minidump_template(struct qlcnic_adapter *);
void qlcnic_83xx_get_stats(struct qlcnic_adapter *adapter, u64 *data);
int qlcnic_83xx_get_settings(struct qlcnic_adapter *, struct ethtool_cmd *);
int qlcnic_83xx_set_settings(struct qlcnic_adapter *, struct ethtool_cmd *);
void qlcnic_83xx_get_pauseparam(struct qlcnic_adapter *,
				struct ethtool_pauseparam *);
int qlcnic_83xx_set_pauseparam(struct qlcnic_adapter *,
			       struct ethtool_pauseparam *);
int qlcnic_83xx_test_link(struct qlcnic_adapter *);
int qlcnic_83xx_reg_test(struct qlcnic_adapter *);
int qlcnic_83xx_get_regs_len(struct qlcnic_adapter *);
int qlcnic_83xx_get_registers(struct qlcnic_adapter *, u32 *);
int qlcnic_83xx_loopback_test(struct net_device *, u8);
int qlcnic_83xx_interrupt_test(struct net_device *);
int qlcnic_83xx_set_led(struct net_device *, enum ethtool_phys_id_state);
int qlcnic_83xx_flash_test(struct qlcnic_adapter *);
int qlcnic_83xx_enable_flash_write(struct qlcnic_adapter *);
int qlcnic_83xx_disable_flash_write(struct qlcnic_adapter *);
void qlcnic_83xx_enable_mbx_poll(struct qlcnic_adapter *);
void qlcnic_83xx_disable_mbx_poll(struct qlcnic_adapter *);
void qlcnic_83xx_set_mac_filter_count(struct qlcnic_adapter *);
int qlcnic_83xx_shutdown(struct pci_dev *);
int qlcnic_83xx_resume(struct qlcnic_adapter *);
int qlcnic_83xx_idc_init(struct qlcnic_adapter *);
int qlcnic_83xx_idc_reattach_driver(struct qlcnic_adapter *);
int qlcnic_83xx_set_vnic_opmode(struct qlcnic_adapter *);
int qlcnic_83xx_check_vnic_state(struct qlcnic_adapter *);
#endif
