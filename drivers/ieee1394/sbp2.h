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
 * SBP2 specific structures and defines
 */

#define ORB_DIRECTION_WRITE_TO_MEDIA    0x0
#define ORB_DIRECTION_READ_FROM_MEDIA   0x1
#define ORB_DIRECTION_NO_DATA_TRANSFER  0x2

#define ORB_SET_NULL_PTR(value)			((value & 0x1) << 31)
#define ORB_SET_NOTIFY(value)			((value & 0x1) << 31)
#define ORB_SET_RQ_FMT(value)			((value & 0x3) << 29)	/* unused ? */
#define ORB_SET_NODE_ID(value)			((value & 0xffff) << 16)
#define ORB_SET_STATUS_FIFO_HI(value, id)	(value >> 32 | ORB_SET_NODE_ID(id))
#define ORB_SET_STATUS_FIFO_LO(value)		(value & 0xffffffff)
#define ORB_SET_DATA_SIZE(value)		(value & 0xffff)
#define ORB_SET_PAGE_SIZE(value)		((value & 0x7) << 16)
#define ORB_SET_PAGE_TABLE_PRESENT(value)	((value & 0x1) << 19)
#define ORB_SET_MAX_PAYLOAD(value)		((value & 0xf) << 20)
#define ORB_SET_SPEED(value)			((value & 0x7) << 24)
#define ORB_SET_DIRECTION(value)		((value & 0x1) << 27)

struct sbp2_command_orb {
	volatile u32 next_ORB_hi;
	volatile u32 next_ORB_lo;
	u32 data_descriptor_hi;
	u32 data_descriptor_lo;
	u32 misc;
	u8 cdb[12];
};

#define SBP2_LOGIN_REQUEST		0x0
#define SBP2_QUERY_LOGINS_REQUEST	0x1
#define SBP2_RECONNECT_REQUEST		0x3
#define SBP2_SET_PASSWORD_REQUEST	0x4
#define SBP2_LOGOUT_REQUEST		0x7
#define SBP2_ABORT_TASK_REQUEST		0xb
#define SBP2_ABORT_TASK_SET		0xc
#define SBP2_LOGICAL_UNIT_RESET		0xe
#define SBP2_TARGET_RESET_REQUEST	0xf

#define ORB_SET_LUN(value)                      (value & 0xffff)
#define ORB_SET_FUNCTION(value)                 ((value & 0xf) << 16)
#define ORB_SET_RECONNECT(value)                ((value & 0xf) << 20)
#define ORB_SET_EXCLUSIVE(value)                ((value & 0x1) << 28)
#define ORB_SET_LOGIN_RESP_LENGTH(value)        (value & 0xffff)
#define ORB_SET_PASSWD_LENGTH(value)            ((value & 0xffff) << 16)

struct sbp2_login_orb {
	u32 password_hi;
	u32 password_lo;
	u32 login_response_hi;
	u32 login_response_lo;
	u32 lun_misc;
	u32 passwd_resp_lengths;
	u32 status_fifo_hi;
	u32 status_fifo_lo;
};

#define RESPONSE_GET_LOGIN_ID(value)            (value & 0xffff)
#define RESPONSE_GET_LENGTH(value)              ((value >> 16) & 0xffff)
#define RESPONSE_GET_RECONNECT_HOLD(value)      (value & 0xffff)

struct sbp2_login_response {
	u32 length_login_ID;
	u32 command_block_agent_hi;
	u32 command_block_agent_lo;
	u32 reconnect_hold;
};

#define ORB_SET_LOGIN_ID(value)                 (value & 0xffff)

#define ORB_SET_QUERY_LOGINS_RESP_LENGTH(value) (value & 0xffff)

struct sbp2_query_logins_orb {
	u32 reserved1;
	u32 reserved2;
	u32 query_response_hi;
	u32 query_response_lo;
	u32 lun_misc;
	u32 reserved_resp_length;
	u32 status_fifo_hi;
	u32 status_fifo_lo;
};

#define RESPONSE_GET_MAX_LOGINS(value)          (value & 0xffff)
#define RESPONSE_GET_ACTIVE_LOGINS(value)       ((RESPONSE_GET_LENGTH(value) - 4) / 12)

struct sbp2_query_logins_response {
	u32 length_max_logins;
	u32 misc_IDs;
	u32 initiator_misc_hi;
	u32 initiator_misc_lo;
};

struct sbp2_reconnect_orb {
	u32 reserved1;
	u32 reserved2;
	u32 reserved3;
	u32 reserved4;
	u32 login_ID_misc;
	u32 reserved5;
	u32 status_fifo_hi;
	u32 status_fifo_lo;
};

struct sbp2_logout_orb {
	u32 reserved1;
	u32 reserved2;
	u32 reserved3;
	u32 reserved4;
	u32 login_ID_misc;
	u32 reserved5;
	u32 status_fifo_hi;
	u32 status_fifo_lo;
};

#define PAGE_TABLE_SET_SEGMENT_BASE_HI(value)   (value & 0xffff)
#define PAGE_TABLE_SET_SEGMENT_LENGTH(value)    ((value & 0xffff) << 16)

struct sbp2_unrestricted_page_table {
	u32 length_segment_base_hi;
	u32 segment_base_lo;
};

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

#define SBP2_SCSI_STATUS_GOOD			0x0
#define SBP2_SCSI_STATUS_CHECK_CONDITION	0x2
#define SBP2_SCSI_STATUS_CONDITION_MET		0x4
#define SBP2_SCSI_STATUS_BUSY			0x8
#define SBP2_SCSI_STATUS_RESERVATION_CONFLICT	0x18
#define SBP2_SCSI_STATUS_COMMAND_TERMINATED	0x22

#define SBP2_SCSI_STATUS_SELECTION_TIMEOUT	0xff

#define STATUS_GET_ORB_OFFSET_HI(value)         (value & 0xffff)
#define STATUS_GET_SBP_STATUS(value)            ((value >> 16) & 0xff)
#define STATUS_GET_LENGTH(value)                ((value >> 24) & 0x7)
#define STATUS_GET_DEAD_BIT(value)              ((value >> 27) & 0x1)
#define STATUS_GET_RESP(value)                  ((value >> 28) & 0x3)
#define STATUS_GET_SRC(value)                   ((value >> 30) & 0x3)

struct sbp2_status_block {
	u32 ORB_offset_hi_misc;
	u32 ORB_offset_lo;
	u8 command_set_dependent[24];
};

/*
 * Miscellaneous SBP2 related config rom defines
 */

#define SBP2_UNIT_DIRECTORY_OFFSET_KEY				0xd1
#define SBP2_CSR_OFFSET_KEY					0x54
#define SBP2_UNIT_SPEC_ID_KEY					0x12
#define SBP2_UNIT_SW_VERSION_KEY				0x13
#define SBP2_COMMAND_SET_SPEC_ID_KEY				0x38
#define SBP2_COMMAND_SET_KEY					0x39
#define SBP2_UNIT_CHARACTERISTICS_KEY				0x3a
#define SBP2_DEVICE_TYPE_AND_LUN_KEY				0x14
#define SBP2_FIRMWARE_REVISION_KEY				0x3c

#define SBP2_AGENT_STATE_OFFSET					0x00ULL
#define SBP2_AGENT_RESET_OFFSET					0x04ULL
#define SBP2_ORB_POINTER_OFFSET					0x08ULL
#define SBP2_DOORBELL_OFFSET					0x10ULL
#define SBP2_UNSOLICITED_STATUS_ENABLE_OFFSET			0x14ULL
#define SBP2_UNSOLICITED_STATUS_VALUE				0xf

#define SBP2_BUSY_TIMEOUT_ADDRESS				0xfffff0000210ULL
#define SBP2_BUSY_TIMEOUT_VALUE					0xf

#define SBP2_AGENT_RESET_DATA					0xf

/*
 * Unit spec id and sw version entry for SBP-2 devices
 */

#define SBP2_UNIT_SPEC_ID_ENTRY					0x0000609e
#define SBP2_SW_VERSION_ENTRY					0x00010483

/*
 * SCSI specific stuff
 */

#define SBP2_MAX_SG_ELEMENT_LENGTH	0xf000
#define SBP2_MAX_SECTORS		255	/* Max sectors supported */
#define SBP2_MAX_CMDS			8	/* This should be safe */

/* Flags for detected oddities and brokeness */
#define SBP2_WORKAROUND_128K_MAX_TRANS	0x1
#define SBP2_WORKAROUND_INQUIRY_36	0x2
#define SBP2_WORKAROUND_MODE_SENSE_8	0x4
#define SBP2_WORKAROUND_FIX_CAPACITY	0x8
#define SBP2_WORKAROUND_OVERRIDE	0x100

/* This is the two dma types we use for cmd_dma below */
enum cmd_dma_types {
	CMD_DMA_NONE,
	CMD_DMA_PAGE,
	CMD_DMA_SINGLE
};

/*
 * Encapsulates all the info necessary for an outstanding command.
 */
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
	enum cmd_dma_types dma_type;
	unsigned long dma_size;
	int dma_dir;

};

struct sbp2scsi_host_info;

/*
 * Information needed on a per scsi id basis (one for each sbp2 device)
 */
struct scsi_id_instance_data {
	/*
	 * Various sbp2 specific structures
	 */
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

	/*
	 * Stuff we need to know about the sbp2 device itself
	 */
	u64 sbp2_management_agent_addr;
	u64 sbp2_command_block_agent_addr;
	u32 speed_code;
	u32 max_payload_size;

	/*
	 * Values pulled from the device's unit directory
	 */
	u32 sbp2_command_set_spec_id;
	u32 sbp2_command_set;
	u32 sbp2_unit_characteristics;
	u32 sbp2_lun;
	u32 sbp2_firmware_revision;

	/*
	 * Address for the device to write status blocks to
	 */
	u64 status_fifo_addr;

	/*
	 * Variable used for logins, reconnects, logouts, query logins
	 */
	atomic_t sbp2_login_complete;

	/*
	 * Pool of command orbs, so we can have more than overlapped command per id
	 */
	spinlock_t sbp2_command_orb_lock;
	struct list_head sbp2_command_orb_inuse;
	struct list_head sbp2_command_orb_completed;

	struct list_head scsi_list;

	/* Node entry, as retrieved from NodeMgr entries */
	struct node_entry *ne;
	struct unit_directory *ud;

	/* A backlink to our host_info */
	struct sbp2scsi_host_info *hi;

	/* SCSI related pointers */
	struct scsi_device *sdev;
	struct Scsi_Host *scsi_host;

	/* Device specific workarounds/brokeness */
	unsigned workarounds;
};

/* Sbp2 host data structure (one per IEEE1394 host) */
struct sbp2scsi_host_info {
	struct hpsb_host *host;		/* IEEE1394 host */
	struct list_head scsi_ids;	/* List of scsi ids on this host */
};

/*
 * Function prototypes
 */

/*
 * Various utility prototypes
 */
static int sbp2util_create_command_orb_pool(struct scsi_id_instance_data *scsi_id);
static void sbp2util_remove_command_orb_pool(struct scsi_id_instance_data *scsi_id);
static struct sbp2_command_info *sbp2util_find_command_for_orb(struct scsi_id_instance_data *scsi_id, dma_addr_t orb);
static struct sbp2_command_info *sbp2util_find_command_for_SCpnt(struct scsi_id_instance_data *scsi_id, void *SCpnt);
static struct sbp2_command_info *sbp2util_allocate_command_orb(struct scsi_id_instance_data *scsi_id,
							  struct scsi_cmnd *Current_SCpnt,
							  void (*Current_done)(struct scsi_cmnd *));
static void sbp2util_mark_command_completed(struct scsi_id_instance_data *scsi_id,
		struct sbp2_command_info *command);


static int sbp2_start_device(struct scsi_id_instance_data *scsi_id);
static void sbp2_remove_device(struct scsi_id_instance_data *scsi_id);

#ifdef CONFIG_IEEE1394_SBP2_PHYS_DMA
static int sbp2_handle_physdma_write(struct hpsb_host *host, int nodeid, int destid, quadlet_t *data,
                                     u64 addr, size_t length, u16 flags);
static int sbp2_handle_physdma_read(struct hpsb_host *host, int nodeid, quadlet_t *data,
                                    u64 addr, size_t length, u16 flags);
#endif

/*
 * SBP-2 protocol related prototypes
 */
static int sbp2_query_logins(struct scsi_id_instance_data *scsi_id);
static int sbp2_login_device(struct scsi_id_instance_data *scsi_id);
static int sbp2_reconnect_device(struct scsi_id_instance_data *scsi_id);
static int sbp2_logout_device(struct scsi_id_instance_data *scsi_id);
static int sbp2_handle_status_write(struct hpsb_host *host, int nodeid, int destid,
				    quadlet_t *data, u64 addr, size_t length, u16 flags);
static int sbp2_agent_reset(struct scsi_id_instance_data *scsi_id, int wait);
static int sbp2_link_orb_command(struct scsi_id_instance_data *scsi_id,
				 struct sbp2_command_info *command);
static int sbp2_send_command(struct scsi_id_instance_data *scsi_id,
			     struct scsi_cmnd *SCpnt,
			     void (*done)(struct scsi_cmnd *));
static unsigned int sbp2_status_to_sense_data(unchar *sbp2_status,
					      unchar *sense_data);
static void sbp2_parse_unit_directory(struct scsi_id_instance_data *scsi_id,
				      struct unit_directory *ud);
static int sbp2_set_busy_timeout(struct scsi_id_instance_data *scsi_id);
static int sbp2_max_speed_and_size(struct scsi_id_instance_data *scsi_id);

#endif /* SBP2_H */
