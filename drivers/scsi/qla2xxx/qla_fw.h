/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#ifndef __QLA_FW_H
#define __QLA_FW_H

#define MBS_CHECKSUM_ERROR	0x4010
#define MBS_INVALID_PRODUCT_KEY	0x4020

/*
 * Firmware Options.
 */
#define FO1_ENABLE_PUREX	BIT_10
#define FO1_DISABLE_LED_CTRL	BIT_6
#define FO1_ENABLE_8016		BIT_0
#define FO2_ENABLE_SEL_CLASS2	BIT_5
#define FO3_NO_ABTS_ON_LINKDOWN	BIT_14
#define FO3_HOLD_STS_IOCB	BIT_12

/*
 * Port Database structure definition for ISP 24xx.
 */
#define PDO_FORCE_ADISC		BIT_1
#define PDO_FORCE_PLOGI		BIT_0


#define	PORT_DATABASE_24XX_SIZE		64
struct port_database_24xx {
	uint16_t flags;
#define PDF_TASK_RETRY_ID	BIT_14
#define PDF_FC_TAPE		BIT_7
#define PDF_ACK0_CAPABLE	BIT_6
#define PDF_FCP2_CONF		BIT_5
#define PDF_CLASS_2		BIT_4
#define PDF_HARD_ADDR		BIT_1

	uint8_t current_login_state;
	uint8_t last_login_state;
#define PDS_PLOGI_PENDING	0x03
#define PDS_PLOGI_COMPLETE	0x04
#define PDS_PRLI_PENDING	0x05
#define PDS_PRLI_COMPLETE	0x06
#define PDS_PORT_UNAVAILABLE	0x07
#define PDS_PRLO_PENDING	0x09
#define PDS_LOGO_PENDING	0x11
#define PDS_PRLI2_PENDING	0x12

	uint8_t hard_address[3];
	uint8_t reserved_1;

	uint8_t port_id[3];
	uint8_t sequence_id;

	uint16_t port_timer;

	uint16_t nport_handle;			/* N_PORT handle. */

	uint16_t receive_data_size;
	uint16_t reserved_2;

	uint8_t prli_svc_param_word_0[2];	/* Big endian */
						/* Bits 15-0 of word 0 */
	uint8_t prli_svc_param_word_3[2];	/* Big endian */
						/* Bits 15-0 of word 3 */

	uint8_t port_name[WWN_SIZE];
	uint8_t node_name[WWN_SIZE];

	uint8_t reserved_3[24];
};

struct vp_database_24xx {
	uint16_t vp_status;
	uint8_t  options;
	uint8_t  id;
	uint8_t  port_name[WWN_SIZE];
	uint8_t  node_name[WWN_SIZE];
	uint16_t port_id_low;
	uint16_t port_id_high;
};

struct nvram_24xx {
	/* NVRAM header. */
	uint8_t id[4];
	uint16_t nvram_version;
	uint16_t reserved_0;

	/* Firmware Initialization Control Block. */
	uint16_t version;
	uint16_t reserved_1;
	uint16_t frame_payload_size;
	uint16_t execution_throttle;
	uint16_t exchange_count;
	uint16_t hard_address;

	uint8_t port_name[WWN_SIZE];
	uint8_t node_name[WWN_SIZE];

	uint16_t login_retry_count;
	uint16_t link_down_on_nos;
	uint16_t interrupt_delay_timer;
	uint16_t login_timeout;

	uint32_t firmware_options_1;
	uint32_t firmware_options_2;
	uint32_t firmware_options_3;

	/* Offset 56. */

	/*
	 * BIT 0     = Control Enable
	 * BIT 1-15  =
	 *
	 * BIT 0-7   = Reserved
	 * BIT 8-10  = Output Swing 1G
	 * BIT 11-13 = Output Emphasis 1G
	 * BIT 14-15 = Reserved
	 *
	 * BIT 0-7   = Reserved
	 * BIT 8-10  = Output Swing 2G
	 * BIT 11-13 = Output Emphasis 2G
	 * BIT 14-15 = Reserved
	 *
	 * BIT 0-7   = Reserved
	 * BIT 8-10  = Output Swing 4G
	 * BIT 11-13 = Output Emphasis 4G
	 * BIT 14-15 = Reserved
	 */
	uint16_t seriallink_options[4];

	uint16_t reserved_2[16];

	/* Offset 96. */
	uint16_t reserved_3[16];

	/* PCIe table entries. */
	uint16_t reserved_4[16];

	/* Offset 160. */
	uint16_t reserved_5[16];

	/* Offset 192. */
	uint16_t reserved_6[16];

	/* Offset 224. */
	uint16_t reserved_7[16];

	/*
	 * BIT 0  = Enable spinup delay
	 * BIT 1  = Disable BIOS
	 * BIT 2  = Enable Memory Map BIOS
	 * BIT 3  = Enable Selectable Boot
	 * BIT 4  = Disable RISC code load
	 * BIT 5  = Disable Serdes
	 * BIT 6  =
	 * BIT 7  =
	 *
	 * BIT 8  =
	 * BIT 9  =
	 * BIT 10 = Enable lip full login
	 * BIT 11 = Enable target reset
	 * BIT 12 =
	 * BIT 13 =
	 * BIT 14 =
	 * BIT 15 = Enable alternate WWN
	 *
	 * BIT 16-31 =
	 */
	uint32_t host_p;

	uint8_t alternate_port_name[WWN_SIZE];
	uint8_t alternate_node_name[WWN_SIZE];

	uint8_t boot_port_name[WWN_SIZE];
	uint16_t boot_lun_number;
	uint16_t reserved_8;

	uint8_t alt1_boot_port_name[WWN_SIZE];
	uint16_t alt1_boot_lun_number;
	uint16_t reserved_9;

	uint8_t alt2_boot_port_name[WWN_SIZE];
	uint16_t alt2_boot_lun_number;
	uint16_t reserved_10;

	uint8_t alt3_boot_port_name[WWN_SIZE];
	uint16_t alt3_boot_lun_number;
	uint16_t reserved_11;

	/*
	 * BIT 0 = Selective Login
	 * BIT 1 = Alt-Boot Enable
	 * BIT 2 = Reserved
	 * BIT 3 = Boot Order List
	 * BIT 4 = Reserved
	 * BIT 5 = Selective LUN
	 * BIT 6 = Reserved
	 * BIT 7-31 =
	 */
	uint32_t efi_parameters;

	uint8_t reset_delay;
	uint8_t reserved_12;
	uint16_t reserved_13;

	uint16_t boot_id_number;
	uint16_t reserved_14;

	uint16_t max_luns_per_target;
	uint16_t reserved_15;

	uint16_t port_down_retry_count;
	uint16_t link_down_timeout;

	/* FCode parameters. */
	uint16_t fcode_parameter;

	uint16_t reserved_16[3];

	/* Offset 352. */
	uint8_t prev_drv_ver_major;
	uint8_t prev_drv_ver_submajob;
	uint8_t prev_drv_ver_minor;
	uint8_t prev_drv_ver_subminor;

	uint16_t prev_bios_ver_major;
	uint16_t prev_bios_ver_minor;

	uint16_t prev_efi_ver_major;
	uint16_t prev_efi_ver_minor;

	uint16_t prev_fw_ver_major;
	uint8_t prev_fw_ver_minor;
	uint8_t prev_fw_ver_subminor;

	uint16_t reserved_17[8];

	/* Offset 384. */
	uint16_t reserved_18[16];

	/* Offset 416. */
	uint16_t reserved_19[16];

	/* Offset 448. */
	uint16_t reserved_20[16];

	/* Offset 480. */
	uint8_t model_name[16];

	uint16_t reserved_21[2];

	/* Offset 500. */
	/* HW Parameter Block. */
	uint16_t pcie_table_sig;
	uint16_t pcie_table_offset;

	uint16_t subsystem_vendor_id;
	uint16_t subsystem_device_id;

	uint32_t checksum;
};

/*
 * ISP Initialization Control Block.
 * Little endian except where noted.
 */
#define	ICB_VERSION 1
struct init_cb_24xx {
	uint16_t version;
	uint16_t reserved_1;

	uint16_t frame_payload_size;
	uint16_t execution_throttle;
	uint16_t exchange_count;

	uint16_t hard_address;

	uint8_t port_name[WWN_SIZE];		/* Big endian. */
	uint8_t node_name[WWN_SIZE];		/* Big endian. */

	uint16_t response_q_inpointer;
	uint16_t request_q_outpointer;

	uint16_t login_retry_count;

	uint16_t prio_request_q_outpointer;

	uint16_t response_q_length;
	uint16_t request_q_length;

	uint16_t link_down_on_nos;		/* Milliseconds. */

	uint16_t prio_request_q_length;

	uint32_t request_q_address[2];
	uint32_t response_q_address[2];
	uint32_t prio_request_q_address[2];

	uint16_t msix;
	uint16_t msix_atio;
	uint8_t reserved_2[4];

	uint16_t atio_q_inpointer;
	uint16_t atio_q_length;
	uint32_t atio_q_address[2];

	uint16_t interrupt_delay_timer;		/* 100us increments. */
	uint16_t login_timeout;

	/*
	 * BIT 0  = Enable Hard Loop Id
	 * BIT 1  = Enable Fairness
	 * BIT 2  = Enable Full-Duplex
	 * BIT 3  = Reserved
	 * BIT 4  = Enable Target Mode
	 * BIT 5  = Disable Initiator Mode
	 * BIT 6  = Reserved
	 * BIT 7  = Reserved
	 *
	 * BIT 8  = Reserved
	 * BIT 9  = Non Participating LIP
	 * BIT 10 = Descending Loop ID Search
	 * BIT 11 = Acquire Loop ID in LIPA
	 * BIT 12 = Reserved
	 * BIT 13 = Full Login after LIP
	 * BIT 14 = Node Name Option
	 * BIT 15-31 = Reserved
	 */
	uint32_t firmware_options_1;

	/*
	 * BIT 0  = Operation Mode bit 0
	 * BIT 1  = Operation Mode bit 1
	 * BIT 2  = Operation Mode bit 2
	 * BIT 3  = Operation Mode bit 3
	 * BIT 4  = Connection Options bit 0
	 * BIT 5  = Connection Options bit 1
	 * BIT 6  = Connection Options bit 2
	 * BIT 7  = Enable Non part on LIHA failure
	 *
	 * BIT 8  = Enable Class 2
	 * BIT 9  = Enable ACK0
	 * BIT 10 = Reserved
	 * BIT 11 = Enable FC-SP Security
	 * BIT 12 = FC Tape Enable
	 * BIT 13 = Reserved
	 * BIT 14 = Enable Target PRLI Control
	 * BIT 15-31 = Reserved
	 */
	uint32_t firmware_options_2;

	/*
	 * BIT 0  = Reserved
	 * BIT 1  = Soft ID only
	 * BIT 2  = Reserved
	 * BIT 3  = Reserved
	 * BIT 4  = FCP RSP Payload bit 0
	 * BIT 5  = FCP RSP Payload bit 1
	 * BIT 6  = Enable Receive Out-of-Order data frame handling
	 * BIT 7  = Disable Automatic PLOGI on Local Loop
	 *
	 * BIT 8  = Reserved
	 * BIT 9  = Enable Out-of-Order FCP_XFER_RDY relative offset handling
	 * BIT 10 = Reserved
	 * BIT 11 = Reserved
	 * BIT 12 = Reserved
	 * BIT 13 = Data Rate bit 0
	 * BIT 14 = Data Rate bit 1
	 * BIT 15 = Data Rate bit 2
	 * BIT 16 = Enable 75 ohm Termination Select
	 * BIT 17-28 = Reserved
	 * BIT 29 = Enable response queue 0 in index shadowing
	 * BIT 30 = Enable request queue 0 out index shadowing
	 * BIT 31 = Reserved
	 */
	uint32_t firmware_options_3;
	uint16_t qos;
	uint16_t rid;
	uint8_t  reserved_3[20];
};

/*
 * ISP queue - command entry structure definition.
 */
#define COMMAND_BIDIRECTIONAL 0x75
struct cmd_bidir {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined */
	uint8_t entry_status;		/* Entry status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT hanlde. */

	uint16_t timeout;		/* Commnad timeout. */

	uint16_t wr_dseg_count;		/* Write Data segment count. */
	uint16_t rd_dseg_count;		/* Read Data segment count. */

	struct scsi_lun lun;		/* FCP LUN (BE). */

	uint16_t control_flags;		/* Control flags. */
#define BD_WRAP_BACK			BIT_3
#define BD_READ_DATA			BIT_1
#define BD_WRITE_DATA			BIT_0

	uint16_t fcp_cmnd_dseg_len;		/* Data segment length. */
	uint32_t fcp_cmnd_dseg_address[2];	/* Data segment address. */

	uint16_t reserved[2];			/* Reserved */

	uint32_t rd_byte_count;			/* Total Byte count Read. */
	uint32_t wr_byte_count;			/* Total Byte count write. */

	uint8_t port_id[3];			/* PortID of destination port.*/
	uint8_t vp_index;

	uint32_t fcp_data_dseg_address[2];	/* Data segment address. */
	uint16_t fcp_data_dseg_len;		/* Data segment length. */
};

#define COMMAND_TYPE_6	0x48		/* Command Type 6 entry */
struct cmd_type_6 {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */
	uint16_t timeout;		/* Command timeout. */

	uint16_t dseg_count;		/* Data segment count. */

	uint16_t fcp_rsp_dsd_len;	/* FCP_RSP DSD length. */

	struct scsi_lun lun;		/* FCP LUN (BE). */

	uint16_t control_flags;		/* Control flags. */
#define CF_DIF_SEG_DESCR_ENABLE		BIT_3
#define CF_DATA_SEG_DESCR_ENABLE	BIT_2
#define CF_READ_DATA			BIT_1
#define CF_WRITE_DATA			BIT_0

	uint16_t fcp_cmnd_dseg_len;		/* Data segment length. */
	uint32_t fcp_cmnd_dseg_address[2];	/* Data segment address. */

	uint32_t fcp_rsp_dseg_address[2];	/* Data segment address. */

	uint32_t byte_count;		/* Total byte count. */

	uint8_t port_id[3];		/* PortID of destination port. */
	uint8_t vp_index;

	uint32_t fcp_data_dseg_address[2];	/* Data segment address. */
	uint32_t fcp_data_dseg_len;		/* Data segment length. */
};

#define COMMAND_TYPE_7	0x18		/* Command Type 7 entry */
struct cmd_type_7 {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */
	uint16_t timeout;		/* Command timeout. */
#define FW_MAX_TIMEOUT		0x1999

	uint16_t dseg_count;		/* Data segment count. */
	uint16_t reserved_1;

	struct scsi_lun lun;		/* FCP LUN (BE). */

	uint16_t task_mgmt_flags;	/* Task management flags. */
#define TMF_CLEAR_ACA		BIT_14
#define TMF_TARGET_RESET	BIT_13
#define TMF_LUN_RESET		BIT_12
#define TMF_CLEAR_TASK_SET	BIT_10
#define TMF_ABORT_TASK_SET	BIT_9
#define TMF_DSD_LIST_ENABLE	BIT_2
#define TMF_READ_DATA		BIT_1
#define TMF_WRITE_DATA		BIT_0

	uint8_t task;
#define TSK_SIMPLE		0
#define TSK_HEAD_OF_QUEUE	1
#define TSK_ORDERED		2
#define TSK_ACA			4
#define TSK_UNTAGGED		5

	uint8_t crn;

	uint8_t fcp_cdb[MAX_CMDSZ]; 	/* SCSI command words. */
	uint32_t byte_count;		/* Total byte count. */

	uint8_t port_id[3];		/* PortID of destination port. */
	uint8_t vp_index;

	uint32_t dseg_0_address[2];	/* Data segment 0 address. */
	uint32_t dseg_0_len;		/* Data segment 0 length. */
};

#define COMMAND_TYPE_CRC_2	0x6A	/* Command Type CRC_2 (Type 6)
					 * (T10-DIF) */
struct cmd_type_crc_2 {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */
	uint16_t timeout;		/* Command timeout. */

	uint16_t dseg_count;		/* Data segment count. */

	uint16_t fcp_rsp_dseg_len;	/* FCP_RSP DSD length. */

	struct scsi_lun lun;		/* FCP LUN (BE). */

	uint16_t control_flags;		/* Control flags. */

	uint16_t fcp_cmnd_dseg_len;		/* Data segment length. */
	uint32_t fcp_cmnd_dseg_address[2];	/* Data segment address. */

	uint32_t fcp_rsp_dseg_address[2];	/* Data segment address. */

	uint32_t byte_count;		/* Total byte count. */

	uint8_t port_id[3];		/* PortID of destination port. */
	uint8_t vp_index;

	uint32_t crc_context_address[2];	/* Data segment address. */
	uint16_t crc_context_len;		/* Data segment length. */
	uint16_t reserved_1;			/* MUST be set to 0. */
};


/*
 * ISP queue - status entry structure definition.
 */
#define	STATUS_TYPE	0x03		/* Status entry. */
struct sts_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t comp_status;		/* Completion status. */
	uint16_t ox_id;			/* OX_ID used by the firmware. */

	uint32_t residual_len;		/* FW calc residual transfer length. */

	uint16_t reserved_1;
	uint16_t state_flags;		/* State flags. */
#define SF_TRANSFERRED_DATA	BIT_11
#define SF_FCP_RSP_DMA		BIT_0

	uint16_t reserved_2;
	uint16_t scsi_status;		/* SCSI status. */
#define SS_CONFIRMATION_REQ		BIT_12

	uint32_t rsp_residual_count;	/* FCP RSP residual count. */

	uint32_t sense_len;		/* FCP SENSE length. */
	uint32_t rsp_data_len;		/* FCP response data length. */
	uint8_t data[28];		/* FCP response/sense information. */
	/*
	 * If DIF Error is set in comp_status, these additional fields are
	 * defined:
	 *
	 * !!! NOTE: Firmware sends expected/actual DIF data in big endian
	 * format; but all of the "data" field gets swab32-d in the beginning
	 * of qla2x00_status_entry().
	 *
	 * &data[10] : uint8_t report_runt_bg[2];	- computed guard
	 * &data[12] : uint8_t actual_dif[8];		- DIF Data received
	 * &data[20] : uint8_t expected_dif[8];		- DIF Data computed
	*/
};


/*
 * Status entry completion status
 */
#define CS_DATA_REASSEMBLY_ERROR 0x11	/* Data Reassembly Error.. */
#define CS_ABTS_BY_TARGET	0x13	/* Target send ABTS to abort IOCB. */
#define CS_FW_RESOURCE		0x2C	/* Firmware Resource Unavailable. */
#define CS_TASK_MGMT_OVERRUN	0x30	/* Task management overrun (8+). */
#define CS_ABORT_BY_TARGET	0x47	/* Abort By Target. */

/*
 * ISP queue - marker entry structure definition.
 */
#define MARKER_TYPE	0x04		/* Marker entry. */
struct mrk_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */

	uint8_t modifier;		/* Modifier (7-0). */
#define MK_SYNC_ID_LUN	0		/* Synchronize ID/LUN */
#define MK_SYNC_ID	1		/* Synchronize ID */
#define MK_SYNC_ALL	2		/* Synchronize all ID/LUN */
	uint8_t reserved_1;

	uint8_t reserved_2;
	uint8_t vp_index;

	uint16_t reserved_3;

	uint8_t lun[8];			/* FCP LUN (BE). */
	uint8_t reserved_4[40];
};

/*
 * ISP queue - CT Pass-Through entry structure definition.
 */
#define CT_IOCB_TYPE		0x29	/* CT Pass-Through IOCB entry */
struct ct_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System Defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t comp_status;		/* Completion status. */

	uint16_t nport_handle;		/* N_PORT handle. */

	uint16_t cmd_dsd_count;

	uint8_t vp_index;
	uint8_t reserved_1;

	uint16_t timeout;		/* Command timeout. */
	uint16_t reserved_2;

	uint16_t rsp_dsd_count;

	uint8_t reserved_3[10];

	uint32_t rsp_byte_count;
	uint32_t cmd_byte_count;

	uint32_t dseg_0_address[2];	/* Data segment 0 address. */
	uint32_t dseg_0_len;		/* Data segment 0 length. */
	uint32_t dseg_1_address[2];	/* Data segment 1 address. */
	uint32_t dseg_1_len;		/* Data segment 1 length. */
};

/*
 * ISP queue - ELS Pass-Through entry structure definition.
 */
#define ELS_IOCB_TYPE		0x53	/* ELS Pass-Through IOCB entry */
struct els_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System Defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t reserved_1;

	uint16_t nport_handle;		/* N_PORT handle. */

	uint16_t tx_dsd_count;

	uint8_t vp_index;
	uint8_t sof_type;
#define EST_SOFI3		(1 << 4)
#define EST_SOFI2		(3 << 4)

	uint32_t rx_xchg_address;	/* Receive exchange address. */
	uint16_t rx_dsd_count;

	uint8_t opcode;
	uint8_t reserved_2;

	uint8_t port_id[3];
	uint8_t reserved_3;

	uint16_t reserved_4;

	uint16_t control_flags;		/* Control flags. */
#define ECF_PAYLOAD_DESCR_MASK	(BIT_15|BIT_14|BIT_13)
#define EPD_ELS_COMMAND		(0 << 13)
#define EPD_ELS_ACC		(1 << 13)
#define EPD_ELS_RJT		(2 << 13)
#define EPD_RX_XCHG		(3 << 13)
#define ECF_CLR_PASSTHRU_PEND	BIT_12
#define ECF_INCL_FRAME_HDR	BIT_11

	uint32_t rx_byte_count;
	uint32_t tx_byte_count;

	uint32_t tx_address[2];		/* Data segment 0 address. */
	uint32_t tx_len;		/* Data segment 0 length. */
	uint32_t rx_address[2];		/* Data segment 1 address. */
	uint32_t rx_len;		/* Data segment 1 length. */
};

struct els_sts_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System Defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t comp_status;

	uint16_t nport_handle;		/* N_PORT handle. */

	uint16_t reserved_1;

	uint8_t vp_index;
	uint8_t sof_type;

	uint32_t rx_xchg_address;	/* Receive exchange address. */
	uint16_t reserved_2;

	uint8_t opcode;
	uint8_t reserved_3;

	uint8_t port_id[3];
	uint8_t reserved_4;

	uint16_t reserved_5;

	uint16_t control_flags;		/* Control flags. */
	uint32_t total_byte_count;
	uint32_t error_subcode_1;
	uint32_t error_subcode_2;
};
/*
 * ISP queue - Mailbox Command entry structure definition.
 */
#define MBX_IOCB_TYPE	0x39
struct mbx_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t mbx[28];
};


#define LOGINOUT_PORT_IOCB_TYPE	0x52	/* Login/Logout Port entry. */
struct logio_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t comp_status;		/* Completion status. */
#define CS_LOGIO_ERROR		0x31	/* Login/Logout IOCB error. */

	uint16_t nport_handle;		/* N_PORT handle. */

	uint16_t control_flags;		/* Control flags. */
					/* Modifiers. */
#define LCF_INCLUDE_SNS		BIT_10	/* Include SNS (FFFFFC) during LOGO. */
#define LCF_FCP2_OVERRIDE	BIT_9	/* Set/Reset word 3 of PRLI. */
#define LCF_CLASS_2		BIT_8	/* Enable class 2 during PLOGI. */
#define LCF_FREE_NPORT		BIT_7	/* Release NPORT handle after LOGO. */
#define LCF_EXPL_LOGO		BIT_6	/* Perform an explicit LOGO. */
#define LCF_SKIP_PRLI		BIT_5	/* Skip PRLI after PLOGI. */
#define LCF_IMPL_LOGO_ALL	BIT_5	/* Implicit LOGO to all ports. */
#define LCF_COND_PLOGI		BIT_4	/* PLOGI only if not logged-in. */
#define LCF_IMPL_LOGO		BIT_4	/* Perform an implicit LOGO. */
#define LCF_IMPL_PRLO		BIT_4	/* Perform an implicit PRLO. */
					/* Commands. */
#define LCF_COMMAND_PLOGI	0x00	/* PLOGI. */
#define LCF_COMMAND_PRLI	0x01	/* PRLI. */
#define LCF_COMMAND_PDISC	0x02	/* PDISC. */
#define LCF_COMMAND_ADISC	0x03	/* ADISC. */
#define LCF_COMMAND_LOGO	0x08	/* LOGO. */
#define LCF_COMMAND_PRLO	0x09	/* PRLO. */
#define LCF_COMMAND_TPRLO	0x0A	/* TPRLO. */

	uint8_t vp_index;
	uint8_t reserved_1;

	uint8_t port_id[3];		/* PortID of destination port. */

	uint8_t rsp_size;		/* Response size in 32bit words. */

	uint32_t io_parameter[11];	/* General I/O parameters. */
#define LSC_SCODE_NOLINK	0x01
#define LSC_SCODE_NOIOCB	0x02
#define LSC_SCODE_NOXCB		0x03
#define LSC_SCODE_CMD_FAILED	0x04
#define LSC_SCODE_NOFABRIC	0x05
#define LSC_SCODE_FW_NOT_READY	0x07
#define LSC_SCODE_NOT_LOGGED_IN	0x09
#define LSC_SCODE_NOPCB		0x0A

#define LSC_SCODE_ELS_REJECT	0x18
#define LSC_SCODE_CMD_PARAM_ERR	0x19
#define LSC_SCODE_PORTID_USED	0x1A
#define LSC_SCODE_NPORT_USED	0x1B
#define LSC_SCODE_NONPORT	0x1C
#define LSC_SCODE_LOGGED_IN	0x1D
#define LSC_SCODE_NOFLOGI_ACC	0x1F
};

#define TSK_MGMT_IOCB_TYPE	0x14
struct tsk_mgmt_entry {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */

	uint16_t reserved_1;

	uint16_t delay;			/* Activity delay in seconds. */

	uint16_t timeout;		/* Command timeout. */

	struct scsi_lun lun;		/* FCP LUN (BE). */

	uint32_t control_flags;		/* Control Flags. */
#define TCF_NOTMCMD_TO_TARGET	BIT_31
#define TCF_LUN_RESET		BIT_4
#define TCF_ABORT_TASK_SET	BIT_3
#define TCF_CLEAR_TASK_SET	BIT_2
#define TCF_TARGET_RESET	BIT_1
#define TCF_CLEAR_ACA		BIT_0

	uint8_t reserved_2[20];

	uint8_t port_id[3];		/* PortID of destination port. */
	uint8_t vp_index;

	uint8_t reserved_3[12];
};

#define ABORT_IOCB_TYPE	0x33
struct abort_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t nport_handle;		/* N_PORT handle. */
					/* or Completion status. */

	uint16_t options;		/* Options. */
#define AOF_NO_ABTS		BIT_0	/* Do not send any ABTS. */

	uint32_t handle_to_abort;	/* System handle to abort. */

	uint16_t req_que_no;
	uint8_t reserved_1[30];

	uint8_t port_id[3];		/* PortID of destination port. */
	uint8_t vp_index;

	uint8_t reserved_2[12];
};

/*
 * ISP I/O Register Set structure definitions.
 */
struct device_reg_24xx {
	uint32_t flash_addr;		/* Flash/NVRAM BIOS address. */
#define FARX_DATA_FLAG	BIT_31
#define FARX_ACCESS_FLASH_CONF	0x7FFD0000
#define FARX_ACCESS_FLASH_DATA	0x7FF00000
#define FARX_ACCESS_NVRAM_CONF	0x7FFF0000
#define FARX_ACCESS_NVRAM_DATA	0x7FFE0000

#define FA_NVRAM_FUNC0_ADDR	0x80
#define FA_NVRAM_FUNC1_ADDR	0x180

#define FA_NVRAM_VPD_SIZE	0x200
#define FA_NVRAM_VPD0_ADDR	0x00
#define FA_NVRAM_VPD1_ADDR	0x100

#define FA_BOOT_CODE_ADDR	0x00000
					/*
					 * RISC code begins at offset 512KB
					 * within flash. Consisting of two
					 * contiguous RISC code segments.
					 */
#define FA_RISC_CODE_ADDR	0x20000
#define FA_RISC_CODE_SEGMENTS	2

#define FA_FLASH_DESCR_ADDR_24	0x11000
#define FA_FLASH_LAYOUT_ADDR_24	0x11400
#define FA_NPIV_CONF0_ADDR_24	0x16000
#define FA_NPIV_CONF1_ADDR_24	0x17000

#define FA_FW_AREA_ADDR		0x40000
#define FA_VPD_NVRAM_ADDR	0x48000
#define FA_FEATURE_ADDR		0x4C000
#define FA_FLASH_DESCR_ADDR	0x50000
#define FA_FLASH_LAYOUT_ADDR	0x50400
#define FA_HW_EVENT0_ADDR	0x54000
#define FA_HW_EVENT1_ADDR	0x54400
#define FA_HW_EVENT_SIZE	0x200
#define FA_HW_EVENT_ENTRY_SIZE	4
#define FA_NPIV_CONF0_ADDR	0x5C000
#define FA_NPIV_CONF1_ADDR	0x5D000
#define FA_FCP_PRIO0_ADDR	0x10000
#define FA_FCP_PRIO1_ADDR	0x12000

/*
 * Flash Error Log Event Codes.
 */
#define HW_EVENT_RESET_ERR	0xF00B
#define HW_EVENT_ISP_ERR	0xF020
#define HW_EVENT_PARITY_ERR	0xF022
#define HW_EVENT_NVRAM_CHKSUM_ERR	0xF023
#define HW_EVENT_FLASH_FW_ERR	0xF024

	uint32_t flash_data;		/* Flash/NVRAM BIOS data. */

	uint32_t ctrl_status;		/* Control/Status. */
#define CSRX_FLASH_ACCESS_ERROR	BIT_18	/* Flash/NVRAM Access Error. */
#define CSRX_DMA_ACTIVE		BIT_17	/* DMA Active status. */
#define CSRX_DMA_SHUTDOWN	BIT_16	/* DMA Shutdown control status. */
#define CSRX_FUNCTION		BIT_15	/* Function number. */
					/* PCI-X Bus Mode. */
#define CSRX_PCIX_BUS_MODE_MASK	(BIT_11|BIT_10|BIT_9|BIT_8)
#define PBM_PCI_33MHZ		(0 << 8)
#define PBM_PCIX_M1_66MHZ	(1 << 8)
#define PBM_PCIX_M1_100MHZ	(2 << 8)
#define PBM_PCIX_M1_133MHZ	(3 << 8)
#define PBM_PCIX_M2_66MHZ	(5 << 8)
#define PBM_PCIX_M2_100MHZ	(6 << 8)
#define PBM_PCIX_M2_133MHZ	(7 << 8)
#define PBM_PCI_66MHZ		(8 << 8)
					/* Max Write Burst byte count. */
#define CSRX_MAX_WRT_BURST_MASK	(BIT_5|BIT_4)
#define MWB_512_BYTES		(0 << 4)
#define MWB_1024_BYTES		(1 << 4)
#define MWB_2048_BYTES		(2 << 4)
#define MWB_4096_BYTES		(3 << 4)

#define CSRX_64BIT_SLOT		BIT_2	/* PCI 64-Bit Bus Slot. */
#define CSRX_FLASH_ENABLE	BIT_1	/* Flash BIOS Read/Write enable. */
#define CSRX_ISP_SOFT_RESET	BIT_0	/* ISP soft reset. */

	uint32_t ictrl;			/* Interrupt control. */
#define ICRX_EN_RISC_INT	BIT_3	/* Enable RISC interrupts on PCI. */

	uint32_t istatus;		/* Interrupt status. */
#define ISRX_RISC_INT		BIT_3	/* RISC interrupt. */

	uint32_t unused_1[2];		/* Gap. */

					/* Request Queue. */
	uint32_t req_q_in;		/*  In-Pointer. */
	uint32_t req_q_out;		/*  Out-Pointer. */
					/* Response Queue. */
	uint32_t rsp_q_in;		/*  In-Pointer. */
	uint32_t rsp_q_out;		/*  Out-Pointer. */
					/* Priority Request Queue. */
	uint32_t preq_q_in;		/*  In-Pointer. */
	uint32_t preq_q_out;		/*  Out-Pointer. */

	uint32_t unused_2[2];		/* Gap. */

					/* ATIO Queue. */
	uint32_t atio_q_in;		/*  In-Pointer. */
	uint32_t atio_q_out;		/*  Out-Pointer. */

	uint32_t host_status;
#define HSRX_RISC_INT		BIT_15	/* RISC to Host interrupt. */
#define HSRX_RISC_PAUSED	BIT_8	/* RISC Paused. */

	uint32_t hccr;			/* Host command & control register. */
					/* HCCR statuses. */
#define HCCRX_HOST_INT		BIT_6	/* Host to RISC interrupt bit. */
#define HCCRX_RISC_RESET	BIT_5	/* RISC Reset mode bit. */
					/* HCCR commands. */
					/* NOOP. */
#define HCCRX_NOOP		0x00000000
					/* Set RISC Reset. */
#define HCCRX_SET_RISC_RESET	0x10000000
					/* Clear RISC Reset. */
#define HCCRX_CLR_RISC_RESET	0x20000000
					/* Set RISC Pause. */
#define HCCRX_SET_RISC_PAUSE	0x30000000
					/* Releases RISC Pause. */
#define HCCRX_REL_RISC_PAUSE	0x40000000
					/* Set HOST to RISC interrupt. */
#define HCCRX_SET_HOST_INT	0x50000000
					/* Clear HOST to RISC interrupt. */
#define HCCRX_CLR_HOST_INT	0x60000000
					/* Clear RISC to PCI interrupt. */
#define HCCRX_CLR_RISC_INT	0xA0000000

	uint32_t gpiod;			/* GPIO Data register. */

					/* LED update mask. */
#define GPDX_LED_UPDATE_MASK	(BIT_20|BIT_19|BIT_18)
					/* Data update mask. */
#define GPDX_DATA_UPDATE_MASK	(BIT_17|BIT_16)
					/* Data update mask. */
#define GPDX_DATA_UPDATE_2_MASK	(BIT_28|BIT_27|BIT_26|BIT_17|BIT_16)
					/* LED control mask. */
#define GPDX_LED_COLOR_MASK	(BIT_4|BIT_3|BIT_2)
					/* LED bit values. Color names as
					 * referenced in fw spec.
					 */
#define GPDX_LED_YELLOW_ON	BIT_2
#define GPDX_LED_GREEN_ON	BIT_3
#define GPDX_LED_AMBER_ON	BIT_4
					/* Data in/out. */
#define GPDX_DATA_INOUT		(BIT_1|BIT_0)

	uint32_t gpioe;			/* GPIO Enable register. */
					/* Enable update mask. */
#define GPEX_ENABLE_UPDATE_MASK	(BIT_17|BIT_16)
					/* Enable update mask. */
#define GPEX_ENABLE_UPDATE_2_MASK (BIT_28|BIT_27|BIT_26|BIT_17|BIT_16)
					/* Enable. */
#define GPEX_ENABLE		(BIT_1|BIT_0)

	uint32_t iobase_addr;		/* I/O Bus Base Address register. */

	uint32_t unused_3[10];		/* Gap. */

	uint16_t mailbox0;
	uint16_t mailbox1;
	uint16_t mailbox2;
	uint16_t mailbox3;
	uint16_t mailbox4;
	uint16_t mailbox5;
	uint16_t mailbox6;
	uint16_t mailbox7;
	uint16_t mailbox8;
	uint16_t mailbox9;
	uint16_t mailbox10;
	uint16_t mailbox11;
	uint16_t mailbox12;
	uint16_t mailbox13;
	uint16_t mailbox14;
	uint16_t mailbox15;
	uint16_t mailbox16;
	uint16_t mailbox17;
	uint16_t mailbox18;
	uint16_t mailbox19;
	uint16_t mailbox20;
	uint16_t mailbox21;
	uint16_t mailbox22;
	uint16_t mailbox23;
	uint16_t mailbox24;
	uint16_t mailbox25;
	uint16_t mailbox26;
	uint16_t mailbox27;
	uint16_t mailbox28;
	uint16_t mailbox29;
	uint16_t mailbox30;
	uint16_t mailbox31;

	uint32_t iobase_window;
	uint32_t iobase_c4;
	uint32_t iobase_c8;
	uint32_t unused_4_1[6];		/* Gap. */
	uint32_t iobase_q;
	uint32_t unused_5[2];		/* Gap. */
	uint32_t iobase_select;
	uint32_t unused_6[2];		/* Gap. */
	uint32_t iobase_sdata;
};
/* RISC-RISC semaphore register PCI offet */
#define RISC_REGISTER_BASE_OFFSET	0x7010
#define RISC_REGISTER_WINDOW_OFFET	0x6

/* RISC-RISC semaphore/flag register (risc address 0x7016) */

#define RISC_SEMAPHORE		0x1UL
#define RISC_SEMAPHORE_WE	(RISC_SEMAPHORE << 16)
#define RISC_SEMAPHORE_CLR	(RISC_SEMAPHORE_WE | 0x0UL)
#define RISC_SEMAPHORE_SET	(RISC_SEMAPHORE_WE | RISC_SEMAPHORE)

#define RISC_SEMAPHORE_FORCE		0x8000UL
#define RISC_SEMAPHORE_FORCE_WE		(RISC_SEMAPHORE_FORCE << 16)
#define RISC_SEMAPHORE_FORCE_CLR	(RISC_SEMAPHORE_FORCE_WE | 0x0UL)
#define RISC_SEMAPHORE_FORCE_SET	\
		(RISC_SEMAPHORE_FORCE_WE | RISC_SEMAPHORE_FORCE)

/* RISC semaphore timeouts (ms) */
#define TIMEOUT_SEMAPHORE		2500
#define TIMEOUT_SEMAPHORE_FORCE		2000
#define TIMEOUT_TOTAL_ELAPSED		4500

/* Trace Control *************************************************************/

#define TC_AEN_DISABLE		0

#define TC_EFT_ENABLE		4
#define TC_EFT_DISABLE		5

#define TC_FCE_ENABLE		8
#define TC_FCE_OPTIONS		0
#define TC_FCE_DEFAULT_RX_SIZE	2112
#define TC_FCE_DEFAULT_TX_SIZE	2112
#define TC_FCE_DISABLE		9
#define TC_FCE_DISABLE_TRACE	BIT_0

/* MID Support ***************************************************************/

#define MIN_MULTI_ID_FABRIC	64	/* Must be power-of-2. */
#define MAX_MULTI_ID_FABRIC	256	/* ... */

struct mid_conf_entry_24xx {
	uint16_t reserved_1;

	/*
	 * BIT 0  = Enable Hard Loop Id
	 * BIT 1  = Acquire Loop ID in LIPA
	 * BIT 2  = ID not Acquired
	 * BIT 3  = Enable VP
	 * BIT 4  = Enable Initiator Mode
	 * BIT 5  = Disable Target Mode
	 * BIT 6-7 = Reserved
	 */
	uint8_t options;

	uint8_t hard_address;

	uint8_t port_name[WWN_SIZE];
	uint8_t node_name[WWN_SIZE];
};

struct mid_init_cb_24xx {
	struct init_cb_24xx init_cb;

	uint16_t count;
	uint16_t options;

	struct mid_conf_entry_24xx entries[MAX_MULTI_ID_FABRIC];
};


struct mid_db_entry_24xx {
	uint16_t status;
#define MDBS_NON_PARTIC		BIT_3
#define MDBS_ID_ACQUIRED	BIT_1
#define MDBS_ENABLED		BIT_0

	uint8_t options;
	uint8_t hard_address;

	uint8_t port_name[WWN_SIZE];
	uint8_t node_name[WWN_SIZE];

	uint8_t port_id[3];
	uint8_t reserved_1;
};

/*
 * Virtual Port Control IOCB
 */
#define VP_CTRL_IOCB_TYPE	0x30	/* Virtual Port Control entry. */
struct vp_ctrl_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t vp_idx_failed;

	uint16_t comp_status;		/* Completion status. */
#define CS_VCE_IOCB_ERROR       0x01    /* Error processing IOCB */
#define CS_VCE_ACQ_ID_ERROR	0x02	/* Error while acquireing ID. */
#define CS_VCE_BUSY		0x05	/* Firmware not ready to accept cmd. */

	uint16_t command;
#define VCE_COMMAND_ENABLE_VPS	0x00	/* Enable VPs. */
#define VCE_COMMAND_DISABLE_VPS	0x08	/* Disable VPs. */
#define VCE_COMMAND_DISABLE_VPS_REINIT	0x09 /* Disable VPs and reinit link. */
#define VCE_COMMAND_DISABLE_VPS_LOGO	0x0a /* Disable VPs and LOGO ports. */
#define VCE_COMMAND_DISABLE_VPS_LOGO_ALL        0x0b /* Disable VPs and LOGO ports. */

	uint16_t vp_count;

	uint8_t vp_idx_map[16];
	uint16_t flags;
	uint16_t id;
	uint16_t reserved_4;
	uint16_t hopct;
	uint8_t reserved_5[24];
};

/*
 * Modify Virtual Port Configuration IOCB
 */
#define VP_CONFIG_IOCB_TYPE	0x31	/* Virtual Port Config entry. */
struct vp_config_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t flags;
#define CS_VF_BIND_VPORTS_TO_VF         BIT_0
#define CS_VF_SET_QOS_OF_VPORTS         BIT_1
#define CS_VF_SET_HOPS_OF_VPORTS        BIT_2

	uint16_t comp_status;		/* Completion status. */
#define CS_VCT_STS_ERROR	0x01	/* Specified VPs were not disabled. */
#define CS_VCT_CNT_ERROR	0x02	/* Invalid VP count. */
#define CS_VCT_ERROR		0x03	/* Unknown error. */
#define CS_VCT_IDX_ERROR	0x02	/* Invalid VP index. */
#define CS_VCT_BUSY		0x05	/* Firmware not ready to accept cmd. */

	uint8_t command;
#define VCT_COMMAND_MOD_VPS     0x00    /* Modify VP configurations. */
#define VCT_COMMAND_MOD_ENABLE_VPS 0x01 /* Modify configuration & enable VPs. */

	uint8_t vp_count;

	uint8_t vp_index1;
	uint8_t vp_index2;

	uint8_t options_idx1;
	uint8_t hard_address_idx1;
	uint16_t reserved_vp1;
	uint8_t port_name_idx1[WWN_SIZE];
	uint8_t node_name_idx1[WWN_SIZE];

	uint8_t options_idx2;
	uint8_t hard_address_idx2;
	uint16_t reserved_vp2;
	uint8_t port_name_idx2[WWN_SIZE];
	uint8_t node_name_idx2[WWN_SIZE];
	uint16_t id;
	uint16_t reserved_4;
	uint16_t hopct;
	uint8_t reserved_5[2];
};

#define VP_RPT_ID_IOCB_TYPE	0x32	/* Report ID Acquisition entry. */
struct vp_rpt_id_entry_24xx {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	uint16_t vp_count;		/* Format 0 -- | VP setup | VP acq |. */
					/* Format 1 -- | VP count |. */
	uint16_t vp_idx;		/* Format 0 -- Reserved. */
					/* Format 1 -- VP status and index. */

	uint8_t port_id[3];
	uint8_t format;

	uint8_t vp_idx_map[16];

	uint8_t reserved_4[32];
};

#define VF_EVFP_IOCB_TYPE       0x26    /* Exchange Virtual Fabric Parameters entry. */
struct vf_evfp_entry_24xx {
        uint8_t entry_type;             /* Entry type. */
        uint8_t entry_count;            /* Entry count. */
        uint8_t sys_define;             /* System defined. */
        uint8_t entry_status;           /* Entry Status. */

        uint32_t handle;                /* System handle. */
        uint16_t comp_status;           /* Completion status. */
        uint16_t timeout;               /* timeout */
        uint16_t adim_tagging_mode;

        uint16_t vfport_id;
        uint32_t exch_addr;

        uint16_t nport_handle;          /* N_PORT handle. */
        uint16_t control_flags;
        uint32_t io_parameter_0;
        uint32_t io_parameter_1;
        uint32_t tx_address[2];         /* Data segment 0 address. */
        uint32_t tx_len;                /* Data segment 0 length. */
        uint32_t rx_address[2];         /* Data segment 1 address. */
        uint32_t rx_len;                /* Data segment 1 length. */
};

/* END MID Support ***********************************************************/

/* Flash Description Table ***************************************************/

struct qla_fdt_layout {
	uint8_t sig[4];
	uint16_t version;
	uint16_t len;
	uint16_t checksum;
	uint8_t unused1[2];
	uint8_t model[16];
	uint16_t man_id;
	uint16_t id;
	uint8_t flags;
	uint8_t erase_cmd;
	uint8_t alt_erase_cmd;
	uint8_t wrt_enable_cmd;
	uint8_t wrt_enable_bits;
	uint8_t wrt_sts_reg_cmd;
	uint8_t unprotect_sec_cmd;
	uint8_t read_man_id_cmd;
	uint32_t block_size;
	uint32_t alt_block_size;
	uint32_t flash_size;
	uint32_t wrt_enable_data;
	uint8_t read_id_addr_len;
	uint8_t wrt_disable_bits;
	uint8_t read_dev_id_len;
	uint8_t chip_erase_cmd;
	uint16_t read_timeout;
	uint8_t protect_sec_cmd;
	uint8_t unused2[65];
};

/* Flash Layout Table ********************************************************/

struct qla_flt_location {
	uint8_t sig[4];
	uint16_t start_lo;
	uint16_t start_hi;
	uint8_t version;
	uint8_t unused[5];
	uint16_t checksum;
};

struct qla_flt_header {
	uint16_t version;
	uint16_t length;
	uint16_t checksum;
	uint16_t unused;
};

#define FLT_REG_FW		0x01
#define FLT_REG_BOOT_CODE	0x07
#define FLT_REG_VPD_0		0x14
#define FLT_REG_NVRAM_0		0x15
#define FLT_REG_VPD_1		0x16
#define FLT_REG_NVRAM_1		0x17
#define FLT_REG_VPD_2		0xD4
#define FLT_REG_NVRAM_2		0xD5
#define FLT_REG_VPD_3		0xD6
#define FLT_REG_NVRAM_3		0xD7
#define FLT_REG_FDT		0x1a
#define FLT_REG_FLT		0x1c
#define FLT_REG_HW_EVENT_0	0x1d
#define FLT_REG_HW_EVENT_1	0x1f
#define FLT_REG_NPIV_CONF_0	0x29
#define FLT_REG_NPIV_CONF_1	0x2a
#define FLT_REG_GOLD_FW		0x2f
#define FLT_REG_FCP_PRIO_0	0x87
#define FLT_REG_FCP_PRIO_1	0x88
#define FLT_REG_CNA_FW		0x97
#define FLT_REG_BOOT_CODE_8044	0xA2
#define FLT_REG_FCOE_FW		0xA4
#define FLT_REG_FCOE_NVRAM_0	0xAA
#define FLT_REG_FCOE_NVRAM_1	0xAC

struct qla_flt_region {
	uint32_t code;
	uint32_t size;
	uint32_t start;
	uint32_t end;
};

/* Flash NPIV Configuration Table ********************************************/

struct qla_npiv_header {
	uint8_t sig[2];
	uint16_t version;
	uint16_t entries;
	uint16_t unused[4];
	uint16_t checksum;
};

struct qla_npiv_entry {
	uint16_t flags;
	uint16_t vf_id;
	uint8_t q_qos;
	uint8_t f_qos;
	uint16_t unused1;
	uint8_t port_name[WWN_SIZE];
	uint8_t node_name[WWN_SIZE];
};

/* 84XX Support **************************************************************/

#define MBA_ISP84XX_ALERT	0x800f  /* Alert Notification. */
#define A84_PANIC_RECOVERY	0x1
#define A84_OP_LOGIN_COMPLETE	0x2
#define A84_DIAG_LOGIN_COMPLETE	0x3
#define A84_GOLD_LOGIN_COMPLETE	0x4

#define MBC_ISP84XX_RESET	0x3a    /* Reset. */

#define FSTATE_REMOTE_FC_DOWN	BIT_0
#define FSTATE_NSL_LINK_DOWN	BIT_1
#define FSTATE_IS_DIAG_FW	BIT_2
#define FSTATE_LOGGED_IN	BIT_3
#define FSTATE_WAITING_FOR_VERIFY	BIT_4

#define VERIFY_CHIP_IOCB_TYPE	0x1B
struct verify_chip_entry_84xx {
	uint8_t entry_type;
	uint8_t entry_count;
	uint8_t sys_defined;
	uint8_t entry_status;

	uint32_t handle;

	uint16_t options;
#define VCO_DONT_UPDATE_FW	BIT_0
#define VCO_FORCE_UPDATE	BIT_1
#define VCO_DONT_RESET_UPDATE	BIT_2
#define VCO_DIAG_FW		BIT_3
#define VCO_END_OF_DATA		BIT_14
#define VCO_ENABLE_DSD		BIT_15

	uint16_t reserved_1;

	uint16_t data_seg_cnt;
	uint16_t reserved_2[3];

	uint32_t fw_ver;
	uint32_t exchange_address;

	uint32_t reserved_3[3];
	uint32_t fw_size;
	uint32_t fw_seq_size;
	uint32_t relative_offset;

	uint32_t dseg_address[2];
	uint32_t dseg_length;
};

struct verify_chip_rsp_84xx {
	uint8_t entry_type;
	uint8_t entry_count;
	uint8_t sys_defined;
	uint8_t entry_status;

	uint32_t handle;

	uint16_t comp_status;
#define CS_VCS_CHIP_FAILURE	0x3
#define CS_VCS_BAD_EXCHANGE	0x8
#define CS_VCS_SEQ_COMPLETEi	0x40

	uint16_t failure_code;
#define VFC_CHECKSUM_ERROR	0x1
#define VFC_INVALID_LEN		0x2
#define VFC_ALREADY_IN_PROGRESS	0x8

	uint16_t reserved_1[4];

	uint32_t fw_ver;
	uint32_t exchange_address;

	uint32_t reserved_2[6];
};

#define ACCESS_CHIP_IOCB_TYPE	0x2B
struct access_chip_84xx {
	uint8_t entry_type;
	uint8_t entry_count;
	uint8_t sys_defined;
	uint8_t entry_status;

	uint32_t handle;

	uint16_t options;
#define ACO_DUMP_MEMORY		0x0
#define ACO_LOAD_MEMORY		0x1
#define ACO_CHANGE_CONFIG_PARAM	0x2
#define ACO_REQUEST_INFO	0x3

	uint16_t reserved1;

	uint16_t dseg_count;
	uint16_t reserved2[3];

	uint32_t parameter1;
	uint32_t parameter2;
	uint32_t parameter3;

	uint32_t reserved3[3];
	uint32_t total_byte_cnt;
	uint32_t reserved4;

	uint32_t dseg_address[2];
	uint32_t dseg_length;
};

struct access_chip_rsp_84xx {
	uint8_t entry_type;
	uint8_t entry_count;
	uint8_t sys_defined;
	uint8_t entry_status;

	uint32_t handle;

	uint16_t comp_status;
	uint16_t failure_code;
	uint32_t residual_count;

	uint32_t reserved[12];
};

/* 81XX Support **************************************************************/

#define MBA_DCBX_START		0x8016
#define MBA_DCBX_COMPLETE	0x8030
#define MBA_FCF_CONF_ERR	0x8031
#define MBA_DCBX_PARAM_UPDATE	0x8032
#define MBA_IDC_COMPLETE	0x8100
#define MBA_IDC_NOTIFY		0x8101
#define MBA_IDC_TIME_EXT	0x8102

#define MBC_IDC_ACK		0x101
#define MBC_RESTART_MPI_FW	0x3d
#define MBC_FLASH_ACCESS_CTRL	0x3e	/* Control flash access. */
#define MBC_GET_XGMAC_STATS	0x7a
#define MBC_GET_DCBX_PARAMS	0x51

/*
 * ISP83xx mailbox commands
 */
#define MBC_WRITE_REMOTE_REG		0x0001 /* Write remote register */
#define MBC_READ_REMOTE_REG		0x0009 /* Read remote register */
#define MBC_RESTART_NIC_FIRMWARE	0x003d /* Restart NIC firmware */
#define MBC_SET_ACCESS_CONTROL		0x003e /* Access control command */

/* Flash access control option field bit definitions */
#define FAC_OPT_FORCE_SEMAPHORE		BIT_15
#define FAC_OPT_REQUESTOR_ID		BIT_14
#define FAC_OPT_CMD_SUBCODE		0xff

/* Flash access control command subcodes */
#define FAC_OPT_CMD_WRITE_PROTECT	0x00
#define FAC_OPT_CMD_WRITE_ENABLE	0x01
#define FAC_OPT_CMD_ERASE_SECTOR	0x02
#define FAC_OPT_CMD_LOCK_SEMAPHORE	0x03
#define FAC_OPT_CMD_UNLOCK_SEMAPHORE	0x04
#define FAC_OPT_CMD_GET_SECTOR_SIZE	0x05

struct nvram_81xx {
	/* NVRAM header. */
	uint8_t id[4];
	uint16_t nvram_version;
	uint16_t reserved_0;

	/* Firmware Initialization Control Block. */
	uint16_t version;
	uint16_t reserved_1;
	uint16_t frame_payload_size;
	uint16_t execution_throttle;
	uint16_t exchange_count;
	uint16_t reserved_2;

	uint8_t port_name[WWN_SIZE];
	uint8_t node_name[WWN_SIZE];

	uint16_t login_retry_count;
	uint16_t reserved_3;
	uint16_t interrupt_delay_timer;
	uint16_t login_timeout;

	uint32_t firmware_options_1;
	uint32_t firmware_options_2;
	uint32_t firmware_options_3;

	uint16_t reserved_4[4];

	/* Offset 64. */
	uint8_t enode_mac[6];
	uint16_t reserved_5[5];

	/* Offset 80. */
	uint16_t reserved_6[24];

	/* Offset 128. */
	uint16_t ex_version;
	uint8_t prio_fcf_matching_flags;
	uint8_t reserved_6_1[3];
	uint16_t pri_fcf_vlan_id;
	uint8_t pri_fcf_fabric_name[8];
	uint16_t reserved_6_2[7];
	uint8_t spma_mac_addr[6];
	uint16_t reserved_6_3[14];

	/* Offset 192. */
	uint16_t reserved_7[32];

	/*
	 * BIT 0  = Enable spinup delay
	 * BIT 1  = Disable BIOS
	 * BIT 2  = Enable Memory Map BIOS
	 * BIT 3  = Enable Selectable Boot
	 * BIT 4  = Disable RISC code load
	 * BIT 5  = Disable Serdes
	 * BIT 6  = Opt boot mode
	 * BIT 7  = Interrupt enable
	 *
	 * BIT 8  = EV Control enable
	 * BIT 9  = Enable lip reset
	 * BIT 10 = Enable lip full login
	 * BIT 11 = Enable target reset
	 * BIT 12 = Stop firmware
	 * BIT 13 = Enable nodename option
	 * BIT 14 = Default WWPN valid
	 * BIT 15 = Enable alternate WWN
	 *
	 * BIT 16 = CLP LUN string
	 * BIT 17 = CLP Target string
	 * BIT 18 = CLP BIOS enable string
	 * BIT 19 = CLP Serdes string
	 * BIT 20 = CLP WWPN string
	 * BIT 21 = CLP WWNN string
	 * BIT 22 =
	 * BIT 23 =
	 * BIT 24 = Keep WWPN
	 * BIT 25 = Temp WWPN
	 * BIT 26-31 =
	 */
	uint32_t host_p;

	uint8_t alternate_port_name[WWN_SIZE];
	uint8_t alternate_node_name[WWN_SIZE];

	uint8_t boot_port_name[WWN_SIZE];
	uint16_t boot_lun_number;
	uint16_t reserved_8;

	uint8_t alt1_boot_port_name[WWN_SIZE];
	uint16_t alt1_boot_lun_number;
	uint16_t reserved_9;

	uint8_t alt2_boot_port_name[WWN_SIZE];
	uint16_t alt2_boot_lun_number;
	uint16_t reserved_10;

	uint8_t alt3_boot_port_name[WWN_SIZE];
	uint16_t alt3_boot_lun_number;
	uint16_t reserved_11;

	/*
	 * BIT 0 = Selective Login
	 * BIT 1 = Alt-Boot Enable
	 * BIT 2 = Reserved
	 * BIT 3 = Boot Order List
	 * BIT 4 = Reserved
	 * BIT 5 = Selective LUN
	 * BIT 6 = Reserved
	 * BIT 7-31 =
	 */
	uint32_t efi_parameters;

	uint8_t reset_delay;
	uint8_t reserved_12;
	uint16_t reserved_13;

	uint16_t boot_id_number;
	uint16_t reserved_14;

	uint16_t max_luns_per_target;
	uint16_t reserved_15;

	uint16_t port_down_retry_count;
	uint16_t link_down_timeout;

	/* FCode parameters. */
	uint16_t fcode_parameter;

	uint16_t reserved_16[3];

	/* Offset 352. */
	uint8_t reserved_17[4];
	uint16_t reserved_18[5];
	uint8_t reserved_19[2];
	uint16_t reserved_20[8];

	/* Offset 384. */
	uint8_t reserved_21[16];
	uint16_t reserved_22[3];

	/*
	 * BIT 0 = Extended BB credits for LR
	 * BIT 1 = Virtual Fabric Enable
	 * BIT 2 = Enhanced Features Unused
	 * BIT 3-7 = Enhanced Features Reserved
	 */
	/* Enhanced Features */
	uint8_t enhanced_features;

	uint8_t reserved_23;
	uint16_t reserved_24[4];

	/* Offset 416. */
	uint16_t reserved_25[32];

	/* Offset 480. */
	uint8_t model_name[16];

	/* Offset 496. */
	uint16_t feature_mask_l;
	uint16_t feature_mask_h;
	uint16_t reserved_26[2];

	uint16_t subsystem_vendor_id;
	uint16_t subsystem_device_id;

	uint32_t checksum;
};

/*
 * ISP Initialization Control Block.
 * Little endian except where noted.
 */
#define	ICB_VERSION 1
struct init_cb_81xx {
	uint16_t version;
	uint16_t reserved_1;

	uint16_t frame_payload_size;
	uint16_t execution_throttle;
	uint16_t exchange_count;

	uint16_t reserved_2;

	uint8_t port_name[WWN_SIZE];		/* Big endian. */
	uint8_t node_name[WWN_SIZE];		/* Big endian. */

	uint16_t response_q_inpointer;
	uint16_t request_q_outpointer;

	uint16_t login_retry_count;

	uint16_t prio_request_q_outpointer;

	uint16_t response_q_length;
	uint16_t request_q_length;

	uint16_t reserved_3;

	uint16_t prio_request_q_length;

	uint32_t request_q_address[2];
	uint32_t response_q_address[2];
	uint32_t prio_request_q_address[2];

	uint8_t reserved_4[8];

	uint16_t atio_q_inpointer;
	uint16_t atio_q_length;
	uint32_t atio_q_address[2];

	uint16_t interrupt_delay_timer;		/* 100us increments. */
	uint16_t login_timeout;

	/*
	 * BIT 0-3 = Reserved
	 * BIT 4  = Enable Target Mode
	 * BIT 5  = Disable Initiator Mode
	 * BIT 6  = Reserved
	 * BIT 7  = Reserved
	 *
	 * BIT 8-13 = Reserved
	 * BIT 14 = Node Name Option
	 * BIT 15-31 = Reserved
	 */
	uint32_t firmware_options_1;

	/*
	 * BIT 0  = Operation Mode bit 0
	 * BIT 1  = Operation Mode bit 1
	 * BIT 2  = Operation Mode bit 2
	 * BIT 3  = Operation Mode bit 3
	 * BIT 4-7 = Reserved
	 *
	 * BIT 8  = Enable Class 2
	 * BIT 9  = Enable ACK0
	 * BIT 10 = Reserved
	 * BIT 11 = Enable FC-SP Security
	 * BIT 12 = FC Tape Enable
	 * BIT 13 = Reserved
	 * BIT 14 = Enable Target PRLI Control
	 * BIT 15-31 = Reserved
	 */
	uint32_t firmware_options_2;

	/*
	 * BIT 0-3 = Reserved
	 * BIT 4  = FCP RSP Payload bit 0
	 * BIT 5  = FCP RSP Payload bit 1
	 * BIT 6  = Enable Receive Out-of-Order data frame handling
	 * BIT 7  = Reserved
	 *
	 * BIT 8  = Reserved
	 * BIT 9  = Enable Out-of-Order FCP_XFER_RDY relative offset handling
	 * BIT 10-16 = Reserved
	 * BIT 17 = Enable multiple FCFs
	 * BIT 18-20 = MAC addressing mode
	 * BIT 21-25 = Ethernet data rate
	 * BIT 26 = Enable ethernet header rx IOCB for ATIO q
	 * BIT 27 = Enable ethernet header rx IOCB for response q
	 * BIT 28 = SPMA selection bit 0
	 * BIT 28 = SPMA selection bit 1
	 * BIT 30-31 = Reserved
	 */
	uint32_t firmware_options_3;

	uint8_t  reserved_5[8];

	uint8_t enode_mac[6];

	uint8_t reserved_6[10];
};

struct mid_init_cb_81xx {
	struct init_cb_81xx init_cb;

	uint16_t count;
	uint16_t options;

	struct mid_conf_entry_24xx entries[MAX_MULTI_ID_FABRIC];
};

struct ex_init_cb_81xx {
	uint16_t ex_version;
	uint8_t prio_fcf_matching_flags;
	uint8_t reserved_1[3];
	uint16_t pri_fcf_vlan_id;
	uint8_t pri_fcf_fabric_name[8];
	uint16_t reserved_2[7];
	uint8_t spma_mac_addr[6];
	uint16_t reserved_3[14];
};

#define FARX_ACCESS_FLASH_CONF_81XX	0x7FFD0000
#define FARX_ACCESS_FLASH_DATA_81XX	0x7F800000

/* FCP priority config defines *************************************/
/* operations */
#define QLFC_FCP_PRIO_DISABLE           0x0
#define QLFC_FCP_PRIO_ENABLE            0x1
#define QLFC_FCP_PRIO_GET_CONFIG        0x2
#define QLFC_FCP_PRIO_SET_CONFIG        0x3

struct qla_fcp_prio_entry {
	uint16_t flags;         /* Describes parameter(s) in FCP        */
	/* priority entry that are valid        */
#define FCP_PRIO_ENTRY_VALID            0x1
#define FCP_PRIO_ENTRY_TAG_VALID        0x2
#define FCP_PRIO_ENTRY_SPID_VALID       0x4
#define FCP_PRIO_ENTRY_DPID_VALID       0x8
#define FCP_PRIO_ENTRY_LUNB_VALID       0x10
#define FCP_PRIO_ENTRY_LUNE_VALID       0x20
#define FCP_PRIO_ENTRY_SWWN_VALID       0x40
#define FCP_PRIO_ENTRY_DWWN_VALID       0x80
	uint8_t  tag;           /* Priority value                   */
	uint8_t  reserved;      /* Reserved for future use          */
	uint32_t src_pid;       /* Src port id. high order byte     */
				/* unused; -1 (wild card)           */
	uint32_t dst_pid;       /* Src port id. high order byte     */
	/* unused; -1 (wild card)           */
	uint16_t lun_beg;       /* 1st lun num of lun range.        */
				/* -1 (wild card)                   */
	uint16_t lun_end;       /* 2nd lun num of lun range.        */
				/* -1 (wild card)                   */
	uint8_t  src_wwpn[8];   /* Source WWPN: -1 (wild card)      */
	uint8_t  dst_wwpn[8];   /* Destination WWPN: -1 (wild card) */
};

struct qla_fcp_prio_cfg {
	uint8_t  signature[4];  /* "HQOS" signature of config data  */
	uint16_t version;       /* 1: Initial version               */
	uint16_t length;        /* config data size in num bytes    */
	uint16_t checksum;      /* config data bytes checksum       */
	uint16_t num_entries;   /* Number of entries                */
	uint16_t size_of_entry; /* Size of each entry in num bytes  */
	uint8_t  attributes;    /* enable/disable, persistence      */
#define FCP_PRIO_ATTR_DISABLE   0x0
#define FCP_PRIO_ATTR_ENABLE    0x1
#define FCP_PRIO_ATTR_PERSIST   0x2
	uint8_t  reserved;      /* Reserved for future use          */
#define FCP_PRIO_CFG_HDR_SIZE   0x10
	struct qla_fcp_prio_entry entry[1];     /* fcp priority entries  */
#define FCP_PRIO_CFG_ENTRY_SIZE 0x20
};

#define FCP_PRIO_CFG_SIZE       (32*1024) /* fcp prio data per port*/

/* 25XX Support ****************************************************/
#define FA_FCP_PRIO0_ADDR_25	0x3C000
#define FA_FCP_PRIO1_ADDR_25	0x3E000

/* 81XX Flash locations -- occupies second 2MB region. */
#define FA_BOOT_CODE_ADDR_81	0x80000
#define FA_RISC_CODE_ADDR_81	0xA0000
#define FA_FW_AREA_ADDR_81	0xC0000
#define FA_VPD_NVRAM_ADDR_81	0xD0000
#define FA_VPD0_ADDR_81		0xD0000
#define FA_VPD1_ADDR_81		0xD0400
#define FA_NVRAM0_ADDR_81	0xD0080
#define FA_NVRAM1_ADDR_81	0xD0180
#define FA_FEATURE_ADDR_81	0xD4000
#define FA_FLASH_DESCR_ADDR_81	0xD8000
#define FA_FLASH_LAYOUT_ADDR_81	0xD8400
#define FA_HW_EVENT0_ADDR_81	0xDC000
#define FA_HW_EVENT1_ADDR_81	0xDC400
#define FA_NPIV_CONF0_ADDR_81	0xD1000
#define FA_NPIV_CONF1_ADDR_81	0xD2000

/* 83XX Flash locations -- occupies second 8MB region. */
#define FA_FLASH_LAYOUT_ADDR_83	0xFC400

#endif
