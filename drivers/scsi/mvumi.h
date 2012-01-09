/*
  * Marvell UMI head file
  *
  * Copyright 2011 Marvell. <jyli@marvell.com>
  *
  * This file is licensed under GPLv2.
  *
  * This program is free software; you can redistribute it and/or
  * modify it under the terms of the GNU General Public License as
  * published by the Free Software Foundation; version 2 of the
  * License.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  * General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program; if not, write to the Free Software
  * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  * USA
 */

#ifndef MVUMI_H
#define MVUMI_H

#define MAX_BASE_ADDRESS	6

#define VER_MAJOR		1
#define VER_MINOR		1
#define VER_OEM			0
#define VER_BUILD		1500

#define MV_DRIVER_NAME			"mvumi"
#define PCI_VENDOR_ID_MARVELL_2		0x1b4b
#define PCI_DEVICE_ID_MARVELL_MV9143	0x9143

#define MVUMI_INTERNAL_CMD_WAIT_TIME	45

#define IS_DMA64			(sizeof(dma_addr_t) == 8)

enum mvumi_qc_result {
	MV_QUEUE_COMMAND_RESULT_SENT	= 0,
	MV_QUEUE_COMMAND_RESULT_NO_RESOURCE,
};

enum {
	/*******************************************/

	/* ARM Mbus Registers Map	*/

	/*******************************************/
	CPU_MAIN_INT_CAUSE_REG	= 0x20200,
	CPU_MAIN_IRQ_MASK_REG	= 0x20204,
	CPU_MAIN_FIQ_MASK_REG	= 0x20208,
	CPU_ENPOINTA_MASK_REG	= 0x2020C,
	CPU_ENPOINTB_MASK_REG	= 0x20210,

	INT_MAP_COMAERR		= 1 << 6,
	INT_MAP_COMAIN		= 1 << 7,
	INT_MAP_COMAOUT		= 1 << 8,
	INT_MAP_COMBERR		= 1 << 9,
	INT_MAP_COMBIN		= 1 << 10,
	INT_MAP_COMBOUT		= 1 << 11,

	INT_MAP_COMAINT	= (INT_MAP_COMAOUT | INT_MAP_COMAERR),
	INT_MAP_COMBINT	= (INT_MAP_COMBOUT | INT_MAP_COMBIN | INT_MAP_COMBERR),

	INT_MAP_DL_PCIEA2CPU	= 1 << 0,
	INT_MAP_DL_CPU2PCIEA	= 1 << 1,

	/***************************************/

	/* ARM Doorbell Registers Map		*/

	/***************************************/
	CPU_PCIEA_TO_ARM_DRBL_REG	= 0x20400,
	CPU_PCIEA_TO_ARM_MASK_REG	= 0x20404,
	CPU_ARM_TO_PCIEA_DRBL_REG	= 0x20408,
	CPU_ARM_TO_PCIEA_MASK_REG	= 0x2040C,

	DRBL_HANDSHAKE			= 1 << 0,
	DRBL_SOFT_RESET			= 1 << 1,
	DRBL_BUS_CHANGE			= 1 << 2,
	DRBL_EVENT_NOTIFY		= 1 << 3,
	DRBL_MU_RESET			= 1 << 4,
	DRBL_HANDSHAKE_ISR		= DRBL_HANDSHAKE,

	CPU_PCIEA_TO_ARM_MSG0		= 0x20430,
	CPU_PCIEA_TO_ARM_MSG1		= 0x20434,
	CPU_ARM_TO_PCIEA_MSG0		= 0x20438,
	CPU_ARM_TO_PCIEA_MSG1		= 0x2043C,

	/*******************************************/

	/* ARM Communication List Registers Map    */

	/*******************************************/
	CLA_INB_LIST_BASEL		= 0x500,
	CLA_INB_LIST_BASEH		= 0x504,
	CLA_INB_AVAL_COUNT_BASEL	= 0x508,
	CLA_INB_AVAL_COUNT_BASEH	= 0x50C,
	CLA_INB_DESTI_LIST_BASEL	= 0x510,
	CLA_INB_DESTI_LIST_BASEH	= 0x514,
	CLA_INB_WRITE_POINTER		= 0x518,
	CLA_INB_READ_POINTER		= 0x51C,

	CLA_OUTB_LIST_BASEL		= 0x530,
	CLA_OUTB_LIST_BASEH		= 0x534,
	CLA_OUTB_SOURCE_LIST_BASEL	= 0x538,
	CLA_OUTB_SOURCE_LIST_BASEH	= 0x53C,
	CLA_OUTB_COPY_POINTER		= 0x544,
	CLA_OUTB_READ_POINTER		= 0x548,

	CLA_ISR_CAUSE			= 0x560,
	CLA_ISR_MASK			= 0x564,

	INT_MAP_MU		= (INT_MAP_DL_CPU2PCIEA | INT_MAP_COMAINT),

	CL_POINTER_TOGGLE		= 1 << 12,

	CLIC_IN_IRQ			= 1 << 0,
	CLIC_OUT_IRQ			= 1 << 1,
	CLIC_IN_ERR_IRQ			= 1 << 8,
	CLIC_OUT_ERR_IRQ		= 1 << 12,

	CL_SLOT_NUM_MASK		= 0xFFF,

	/*
	* Command flag is the flag for the CDB command itself
	*/
	/* 1-non data; 0-data command */
	CMD_FLAG_NON_DATA		= 1 << 0,
	CMD_FLAG_DMA			= 1 << 1,
	CMD_FLAG_PIO			= 1 << 2,
	/* 1-host read data */
	CMD_FLAG_DATA_IN		= 1 << 3,
	/* 1-host write data */
	CMD_FLAG_DATA_OUT		= 1 << 4,

	SCSI_CMD_MARVELL_SPECIFIC	= 0xE1,
	CDB_CORE_SHUTDOWN		= 0xB,
};

#define APICDB0_EVENT			0xF4
#define APICDB1_EVENT_GETEVENT		0
#define MAX_EVENTS_RETURNED		6

struct mvumi_driver_event {
	u32	time_stamp;
	u32	sequence_no;
	u32	event_id;
	u8	severity;
	u8	param_count;
	u16	device_id;
	u32	params[4];
	u8	sense_data_length;
	u8	Reserved1;
	u8	sense_data[30];
};

struct mvumi_event_req {
	unsigned char	count;
	unsigned char	reserved[3];
	struct mvumi_driver_event  events[MAX_EVENTS_RETURNED];
};

struct mvumi_events_wq {
	struct work_struct work_q;
	struct mvumi_hba *mhba;
	unsigned int event;
	void *param;
};

#define MVUMI_MAX_SG_ENTRY	32
#define SGD_EOT			(1L << 27)

struct mvumi_sgl {
	u32	baseaddr_l;
	u32	baseaddr_h;
	u32	flags;
	u32	size;
};

struct mvumi_res {
	struct list_head entry;
	dma_addr_t bus_addr;
	void *virt_addr;
	unsigned int size;
	unsigned short type;	/* enum Resource_Type */
};

/* Resource type */
enum resource_type {
	RESOURCE_CACHED_MEMORY = 0,
	RESOURCE_UNCACHED_MEMORY
};

struct mvumi_sense_data {
	u8 error_eode:7;
	u8 valid:1;
	u8 segment_number;
	u8 sense_key:4;
	u8 reserved:1;
	u8 incorrect_length:1;
	u8 end_of_media:1;
	u8 file_mark:1;
	u8 information[4];
	u8 additional_sense_length;
	u8 command_specific_information[4];
	u8 additional_sense_code;
	u8 additional_sense_code_qualifier;
	u8 field_replaceable_unit_code;
	u8 sense_key_specific[3];
};

/* Request initiator must set the status to REQ_STATUS_PENDING. */
#define REQ_STATUS_PENDING		0x80

struct mvumi_cmd {
	struct list_head queue_pointer;
	struct mvumi_msg_frame *frame;
	struct scsi_cmnd *scmd;
	atomic_t sync_cmd;
	void *data_buf;
	unsigned short request_id;
	unsigned char cmd_status;
};

/*
 * the function type of the in bound frame
 */
#define CL_FUN_SCSI_CMD			0x1

struct mvumi_msg_frame {
	u16 device_id;
	u16 tag;
	u8 cmd_flag;
	u8 req_function;
	u8 cdb_length;
	u8 sg_counts;
	u32 data_transfer_length;
	u16 request_id;
	u16 reserved1;
	u8 cdb[MAX_COMMAND_SIZE];
	u32 payload[1];
};

/*
 * the respond flag for data_payload of the out bound frame
 */
#define CL_RSP_FLAG_NODATA		0x0
#define CL_RSP_FLAG_SENSEDATA		0x1

struct mvumi_rsp_frame {
	u16 device_id;
	u16 tag;
	u8 req_status;
	u8 rsp_flag;	/* Indicates the type of Data_Payload.*/
	u16 request_id;
	u32 payload[1];
};

struct mvumi_ob_data {
	struct list_head list;
	unsigned char data[0];
};

struct version_info {
	u32 ver_major;
	u32 ver_minor;
	u32 ver_oem;
	u32 ver_build;
};

#define FW_MAX_DELAY			30
#define MVUMI_FW_BUSY			(1U << 0)
#define MVUMI_FW_ATTACH			(1U << 1)
#define MVUMI_FW_ALLOC			(1U << 2)

/*
 * State is the state of the MU
 */
#define FW_STATE_IDLE			0
#define FW_STATE_STARTING		1
#define FW_STATE_HANDSHAKING		2
#define FW_STATE_STARTED		3
#define FW_STATE_ABORT			4

#define HANDSHAKE_SIGNATURE		0x5A5A5A5AL
#define HANDSHAKE_READYSTATE		0x55AA5AA5L
#define HANDSHAKE_DONESTATE		0x55AAA55AL

/* HandShake Status definition */
#define HS_STATUS_OK			1
#define HS_STATUS_ERR			2
#define HS_STATUS_INVALID		3

/* HandShake State/Cmd definition */
#define HS_S_START			1
#define HS_S_RESET			2
#define HS_S_PAGE_ADDR			3
#define HS_S_QUERY_PAGE			4
#define HS_S_SEND_PAGE			5
#define HS_S_END			6
#define HS_S_ABORT			7
#define HS_PAGE_VERIFY_SIZE		128

#define HS_GET_STATE(a)			(a & 0xFFFF)
#define HS_GET_STATUS(a)		((a & 0xFFFF0000) >> 16)
#define HS_SET_STATE(a, b)		(a |= (b & 0xFFFF))
#define HS_SET_STATUS(a, b)		(a |= ((b & 0xFFFF) << 16))

/* handshake frame */
struct mvumi_hs_frame {
	u16 size;
	/* host information */
	u8 host_type;
	u8 reserved_1[1];
	struct version_info host_ver; /* bios or driver version */

	/* controller information */
	u32 system_io_bus;
	u32 slot_number;
	u32 intr_level;
	u32 intr_vector;

	/* communication list configuration */
	u32 ib_baseaddr_l;
	u32 ib_baseaddr_h;
	u32 ob_baseaddr_l;
	u32 ob_baseaddr_h;

	u8 ib_entry_size;
	u8 ob_entry_size;
	u8 ob_depth;
	u8 ib_depth;

	/* system time */
	u64 seconds_since1970;
};

struct mvumi_hs_header {
	u8	page_code;
	u8	checksum;
	u16	frame_length;
	u32	frame_content[1];
};

/*
 * the page code type of the handshake header
 */
#define HS_PAGE_FIRM_CAP	0x1
#define HS_PAGE_HOST_INFO	0x2
#define HS_PAGE_FIRM_CTL	0x3
#define HS_PAGE_CL_INFO		0x4
#define HS_PAGE_TOTAL		0x5

#define HSP_SIZE(i)	sizeof(struct mvumi_hs_page##i)

#define HSP_MAX_SIZE ({					\
	int size, m1, m2;				\
	m1 = max(HSP_SIZE(1), HSP_SIZE(3));		\
	m2 = max(HSP_SIZE(2), HSP_SIZE(4));		\
	size = max(m1, m2);				\
	size;						\
})

/* The format of the page code for Firmware capability */
struct mvumi_hs_page1 {
	u8 pagecode;
	u8 checksum;
	u16 frame_length;

	u16 number_of_ports;
	u16 max_devices_support;
	u16 max_io_support;
	u16 umi_ver;
	u32 max_transfer_size;
	struct version_info fw_ver;
	u8 cl_in_max_entry_size;
	u8 cl_out_max_entry_size;
	u8 cl_inout_list_depth;
	u8 total_pages;
	u16 capability;
	u16 reserved1;
};

/* The format of the page code for Host information */
struct mvumi_hs_page2 {
	u8 pagecode;
	u8 checksum;
	u16 frame_length;

	u8 host_type;
	u8 reserved[3];
	struct version_info host_ver;
	u32 system_io_bus;
	u32 slot_number;
	u32 intr_level;
	u32 intr_vector;
	u64 seconds_since1970;
};

/* The format of the page code for firmware control  */
struct mvumi_hs_page3 {
	u8	pagecode;
	u8	checksum;
	u16	frame_length;
	u16	control;
	u8	reserved[2];
	u32	host_bufferaddr_l;
	u32	host_bufferaddr_h;
	u32	host_eventaddr_l;
	u32	host_eventaddr_h;
};

struct mvumi_hs_page4 {
	u8	pagecode;
	u8	checksum;
	u16	frame_length;
	u32	ib_baseaddr_l;
	u32	ib_baseaddr_h;
	u32	ob_baseaddr_l;
	u32	ob_baseaddr_h;
	u8	ib_entry_size;
	u8	ob_entry_size;
	u8	ob_depth;
	u8	ib_depth;
};

struct mvumi_tag {
	unsigned short *stack;
	unsigned short top;
	unsigned short size;
};

struct mvumi_hba {
	void *base_addr[MAX_BASE_ADDRESS];
	void *mmio;
	struct list_head cmd_pool;
	struct Scsi_Host *shost;
	wait_queue_head_t int_cmd_wait_q;
	struct pci_dev *pdev;
	unsigned int unique_id;
	atomic_t fw_outstanding;
	struct mvumi_instance_template *instancet;

	void *ib_list;
	dma_addr_t ib_list_phys;

	void *ob_list;
	dma_addr_t ob_list_phys;

	void *ib_shadow;
	dma_addr_t ib_shadow_phys;

	void *ob_shadow;
	dma_addr_t ob_shadow_phys;

	void *handshake_page;
	dma_addr_t handshake_page_phys;

	unsigned int global_isr;
	unsigned int isr_status;

	unsigned short max_sge;
	unsigned short max_target_id;
	unsigned char *target_map;
	unsigned int max_io;
	unsigned int list_num_io;
	unsigned int ib_max_size;
	unsigned int ob_max_size;
	unsigned int ib_max_size_setting;
	unsigned int ob_max_size_setting;
	unsigned int max_transfer_size;
	unsigned char hba_total_pages;
	unsigned char fw_flag;
	unsigned char request_id_enabled;
	unsigned short hba_capability;
	unsigned short io_seq;

	unsigned int ib_cur_slot;
	unsigned int ob_cur_slot;
	unsigned int fw_state;

	struct list_head ob_data_list;
	struct list_head free_ob_list;
	struct list_head res_list;
	struct list_head waiting_req_list;

	struct mvumi_tag tag_pool;
	struct mvumi_cmd **tag_cmd;
};

struct mvumi_instance_template {
	void (*fire_cmd)(struct mvumi_hba *, struct mvumi_cmd *);
	void (*enable_intr)(void *) ;
	void (*disable_intr)(void *);
	int (*clear_intr)(void *);
	unsigned int (*read_fw_status_reg)(void *);
};

extern struct timezone sys_tz;
#endif
