/*
 * sbp2.h - Defines and prototypes for sbp2.c
 *
 * Copyright (C) 2000 James Goodwin, Filanet Corporation (www.filanet.com)
 * jamesg@filanet.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef SBP2_H
#define SBP2_H

#define SBP2_DEVICE_NAME		"sbp2"

/*
 * SBP-2 specific definitions
 */

#define ORB_DIRECTION_WRITE_TO_MEDIA	0x0
#define ORB_DIRECTION_READ_FROM_MEDIA	0x1
#define ORB_DIRECTION_NO_DATA_TRANSFER	0x2

#define ORB_SET_NULL_PTR(v)		(((v) & 0x1) << 31)
#define ORB_SET_NOTIFY(v)		(((v) & 0x1) << 31)
#define ORB_SET_RQ_FMT(v)		(((v) & 0x3) << 29)
#define ORB_SET_NODE_ID(v)		(((v) & 0xffff) << 16)
#define ORB_SET_STATUS_FIFO_HI(v, id)	((v) >> 32 | ORB_SET_NODE_ID(id))
#define ORB_SET_STATUS_FIFO_LO(v)	((v) & 0xffffffff)
#define ORB_SET_DATA_SIZE(v)		((v) & 0xffff)
#define ORB_SET_PAGE_SIZE(v)		(((v) & 0x7) << 16)
#define ORB_SET_PAGE_TABLE_PRESENT(v)	(((v) & 0x1) << 19)
#define ORB_SET_MAX_PAYLOAD(v)		(((v) & 0xf) << 20)
#define ORB_SET_SPEED(v)		(((v) & 0x7) << 24)
#define ORB_SET_DIRECTION(v)		(((v) & 0x1) << 27)

struct sbp2_command_orb {
	u32 next_ORB_hi;
	u32 next_ORB_lo;
	u32 data_descriptor_hi;
	u32 data_descriptor_lo;
	u32 misc;
	u8 cdb[12];
} __attribute__((packed));

#define SBP2_LOGIN_REQUEST		0x0
#define SBP2_QUERY_LOGINS_REQUEST	0x1
#define SBP2_RECONNECT_REQUEST		0x3
#define SBP2_SET_PASSWORD_REQUEST	0x4
#define SBP2_LOGOUT_REQUEST		0x7
#define SBP2_ABORT_TASK_REQUEST		0xb
#define SBP2_ABORT_TASK_SET		0xc
#define SBP2_LOGICAL_UNIT_RESET		0xe
#define SBP2_TARGET_RESET_REQUEST	0xf

#define ORB_SET_LUN(v)			((v) & 0xffff)
#define ORB_SET_FUNCTION(v)		(((v) & 0xf) << 16)
#define ORB_SET_RECONNECT(v)		(((v) & 0xf) << 20)
#define ORB_SET_EXCLUSIVE(v)		(((v) & 0x1) << 28)
#define ORB_SET_LOGIN_RESP_LENGTH(v)	((v) & 0xffff)
#define ORB_SET_PASSWD_LENGTH(v)	(((v) & 0xffff) << 16)

struct sbp2_login_orb {
	u32 password_hi;
	u32 password_lo;
	u32 login_response_hi;
	u32 login_response_lo;
	u32 lun_misc;
	u32 passwd_resp_lengths;
	u32 status_fifo_hi;
	u32 status_fifo_lo;
} __attribute__((packed));

#define RESPONSE_GET_LOGIN_ID(v)	((v) & 0xffff)
#define RESPONSE_GET_LENGTH(v)		(((v) >> 16) & 0xffff)
#define RESPONSE_GET_RECONNECT_HOLD(v)	((v) & 0xffff)

struct sbp2_login_response {
	u32 length_login_ID;
	u32 command_block_agent_hi;
	u32 command_block_agent_lo;
	u32 reconnect_hold;
} __attribute__((packed));

#define ORB_SET_LOGIN_ID(v)                 ((v) & 0xffff)
#define ORB_SET_QUERY_LOGINS_RESP_LENGTH(v) ((v) & 0xffff)

struct sbp2_query_logins_orb {
	u32 reserved1;
	u32 reserved2;
	u32 query_response_hi;
	u32 query_response_lo;
	u32 lun_misc;
	u32 reserved_resp_length;
	u32 status_fifo_hi;
	u32 status_fifo_lo;
} __attribute__((packed));

#define RESPONSE_GET_MAX_LOGINS(v)	((v) & 0xffff)
#define RESPONSE_GET_ACTIVE_LOGINS(v)	((RESPONSE_GET_LENGTH((v)) - 4) / 12)

struct sbp2_query_logins_response {
	u32 length_max_logins;
	u32 misc_IDs;
	u32 initiator_misc_hi;
	u32 initiator_misc_lo;
} __attribute__((packed));

struct sbp2_reconnect_orb {
	u32 reserved1;
	u32 reserved2;
	u32 reserved3;
	u32 reserved4;
	u32 login_ID_misc;
	u32 reserved5;
	u32 status_fifo_hi;
	u32 status_fifo_lo;
} __attribute__((packed));

struct sbp2_logout_orb {
	u32 reserved1;
	u32 reserved2;
	u32 reserved3;
	u32 reserved4;
	u32 login_ID_misc;
	u32 reserved5;
	u32 status_fifo_hi;
	u32 status_fifo_lo;
} __attribute__((packed));

#define PAGE_TABLE_SET_SEGMENT_BASE_HI(v)	((v) & 0xffff)
#define PAGE_TABLE_SET_SEGMENT_LENGTH(v)	(((v) & 0xffff) << 16)

struct sbp2_unrestricted_page_table {
	u32 length_segment_base_hi;
	u32 segment_base_lo;
} __attribute__((packed));

#define RESP_STATUS_REQUEST_COMPLETE		0x0
#define RESP_STATUS_TRANSPORT_FAILURE		0x1
#define RESP_STATUS_ILLEGAL_REQUEST		0x2
#define RESP_STATUS_VENDOR_DEPENDENT		0x3

#define SBP2_STATUS_NO_ADDITIONAL_INFO		0x0
#define SBP2_STATUS_REQ_TYPE_NOT_SUPPORTED	0x1
#define SBP2_STATUS_SPEED_NOT_SUPPORTED		0x2
#define SBP2_STATUS_PAGE_SIZE_NOT_SUPPORTED	0x3
#define SBP2_STATUS_ACCESS_DENIED		0x4
#define SBP2_STATUS_LU_NOT_SUPPORTED		0x5
#define SBP2_STATUS_MAX_PAYLOAD_TOO_SMALL	0x6
#define SBP2_STATUS_RESOURCES_UNAVAILABLE	0x8
#define SBP2_STATUS_FUNCTION_REJECTED		0x9
#define SBP2_STATUS_LOGIN_ID_NOT_RECOGNIZED	0xa
#define SBP2_STATUS_DUMMY_ORB_COMPLETED		0xb
#define SBP2_STATUS_REQUEST_ABORTED		0xc
#define SBP2_STATUS_UNSPECIFIED_ERROR		0xff

#define SFMT_CURRENT_ERROR			0x0
#define SFMT_DEFERRED_ERROR			0x1
#define SFMT_VENDOR_DEPENDENT_STATUS		0x3

#define STATUS_GET_SRC(v)			(((v) >> 30) & 0x3)
#define STATUS_GET_RESP(v)			(((v) >> 28) & 0x3)
#define STATUS_GET_LEN(v)			(((v) >> 24) & 0x7)
#define STATUS_GET_SBP_STATUS(v)		(((v) >> 16) & 0xff)
#define STATUS_GET_ORB_OFFSET_HI(v)		((v) & 0x0000ffff)
#define STATUS_TEST_DEAD(v)			((v) & 0x08000000)
/* test 'resp' | 'dead' | 'sbp2_status' */
#define STATUS_TEST_RDS(v)			((v) & 0x38ff0000)

struct sbp2_status_block {
	u32 ORB_offset_hi_misc;
	u32 ORB_offset_lo;
	u8 command_set_dependent[24];
} __attribute__((packed));


/*
 * SBP2 related configuration ROM definitions
 */

#define SBP2_UNIT_DIRECTORY_OFFSET_KEY		0xd1
#define SBP2_CSR_OFFSET_KEY			0x54
#define SBP2_UNIT_SPEC_ID_KEY			0x12
#define SBP2_UNIT_SW_VERSION_KEY		0x13
#define SBP2_COMMAND_SET_SPEC_ID_KEY		0x38
#define SBP2_COMMAND_SET_KEY			0x39
#define SBP2_UNIT_CHARACTERISTICS_KEY		0x3a
#define SBP2_DEVICE_TYPE_AND_LUN_KEY		0x14
#define SBP2_FIRMWARE_REVISION_KEY		0x3c

#define SBP2_AGENT_STATE_OFFSET			0x00ULL
#define SBP2_AGENT_RESET_OFFSET			0x04ULL
#define SBP2_ORB_POINTER_OFFSET			0x08ULL
#define SBP2_DOORBELL_OFFSET			0x10ULL
#define SBP2_UNSOLICITED_STATUS_ENABLE_OFFSET	0x14ULL
#define SBP2_UNSOLICITED_STATUS_VALUE		0xf

#define SBP2_BUSY_TIMEOUT_ADDRESS		0xfffff0000210ULL
/* biggest possible value for Single Phase Retry count is 0xf */
#define SBP2_BUSY_TIMEOUT_VALUE			0xf

#define SBP2_AGENT_RESET_DATA			0xf

#define SBP2_UNIT_SPEC_ID_ENTRY			0x0000609e
#define SBP2_SW_VERSION_ENTRY			0x00010483


/*
 * SCSI specific definitions
 */

#define SBP2_MAX_SG_ELEMENT_LENGTH		0xf000
#define SBP2_MAX_SECTORS			255
/* There is no real limitation of the queue depth (i.e. length of the linked
 * list of command ORBs) at the target. The chosen depth is merely an
 * implementation detail of the sbp2 driver. */
#define SBP2_MAX_CMDS				8

#define SBP2_SCSI_STATUS_GOOD			0x0
#define SBP2_SCSI_STATUS_CHECK_CONDITION	0x2
#define SBP2_SCSI_STATUS_CONDITION_MET		0x4
#define SBP2_SCSI_STATUS_BUSY			0x8
#define SBP2_SCSI_STATUS_RESERVATION_CONFLICT	0x18
#define SBP2_SCSI_STATUS_COMMAND_TERMINATED	0x22
#define SBP2_SCSI_STATUS_SELECTION_TIMEOUT	0xff


/*
 * Representations of commands and devices
 */

enum sbp2_dma_types {
	CMD_DMA_NONE,
	CMD_DMA_PAGE,
	CMD_DMA_SINGLE
};

/* Per SCSI command */
struct sbp2_command_info {
	struct list_head list;
	struct sbp2_command_orb command_orb ____cacheline_aligned;
	dma_addr_t command_orb_dma ____cacheline_aligned;
	struct scsi_cmnd *Current_SCpnt;
	void (*Current_done)(struct scsi_cmnd *);

	/* Also need s/g structure for each sbp2 command */
	struct sbp2_unrestricted_page_table scatter_gather_element[SG_ALL] ____cacheline_aligned;
	dma_addr_t sge_dma ____cacheline_aligned;
	void *sge_buffer;
	dma_addr_t cmd_dma;
	enum sbp2_dma_types dma_type;
	unsigned long dma_size;
	int dma_dir;
};

/* Per FireWire host */
struct sbp2_fwhost_info {
	struct hpsb_host *host;
	struct list_head scsi_ids;
};

/* Per logical unit */
struct scsi_id_instance_data {
	/* Operation request blocks */
	struct sbp2_command_orb *last_orb;
	dma_addr_t last_orb_dma;
	struct sbp2_login_orb *login_orb;
	dma_addr_t login_orb_dma;
	struct sbp2_login_response *login_response;
	dma_addr_t login_response_dma;
	struct sbp2_query_logins_orb *query_logins_orb;
	dma_addr_t query_logins_orb_dma;
	struct sbp2_query_logins_response *query_logins_response;
	dma_addr_t query_logins_response_dma;
	struct sbp2_reconnect_orb *reconnect_orb;
	dma_addr_t reconnect_orb_dma;
	struct sbp2_logout_orb *logout_orb;
	dma_addr_t logout_orb_dma;
	struct sbp2_status_block status_block;

	/* How to talk to the unit */
	u64 management_agent_addr;
	u64 command_block_agent_addr;
	u32 speed_code;
	u32 max_payload_size;

	/* Pulled from the device's unit directory */
	u32 command_set_spec_id;
	u32 command_set;
	u32 unit_characteristics;
	u32 firmware_revision;
	u16 lun;

	/* Address for the unit to write status blocks to */
	u64 status_fifo_addr;

	/* Waitqueue flag for logins, reconnects, logouts, query logins */
	int access_complete:1;

	/* Pool of command ORBs for this logical unit */
	spinlock_t cmd_orb_lock;
	struct list_head cmd_orb_inuse;
	struct list_head cmd_orb_completed;

	/* Backlink to FireWire host; list of units attached to the host */
	struct sbp2_fwhost_info *hi;
	struct list_head scsi_list;

	/* IEEE 1394 core's device representations */
	struct node_entry *ne;
	struct unit_directory *ud;

	/* SCSI core's device representations */
	struct scsi_device *sdev;
	struct Scsi_Host *scsi_host;

	/* Device specific workarounds/brokeness */
	unsigned workarounds;

	/* Connection state */
	atomic_t state;

	/* For deferred requests to the fetch agent */
	struct work_struct protocol_work;
};

/* For use in scsi_id_instance_data.state */
enum sbp2lu_state_types {
	SBP2LU_STATE_RUNNING,		/* all normal */
	SBP2LU_STATE_IN_RESET,		/* between bus reset and reconnect */
	SBP2LU_STATE_IN_SHUTDOWN	/* when sbp2_remove was called */
};

/* For use in scsi_id_instance_data.workarounds and in the corresponding
 * module load parameter */
#define SBP2_WORKAROUND_128K_MAX_TRANS	0x1
#define SBP2_WORKAROUND_INQUIRY_36	0x2
#define SBP2_WORKAROUND_MODE_SENSE_8	0x4
#define SBP2_WORKAROUND_FIX_CAPACITY	0x8
#define SBP2_WORKAROUND_OVERRIDE	0x100

#endif /* SBP2_H */
