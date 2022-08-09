/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright 2018-2021 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef _EFA_ADMIN_H_
#define _EFA_ADMIN_H_

enum efa_admin_aq_completion_status {
	EFA_ADMIN_SUCCESS                           = 0,
	EFA_ADMIN_RESOURCE_ALLOCATION_FAILURE       = 1,
	EFA_ADMIN_BAD_OPCODE                        = 2,
	EFA_ADMIN_UNSUPPORTED_OPCODE                = 3,
	EFA_ADMIN_MALFORMED_REQUEST                 = 4,
	/* Additional status is provided in ACQ entry extended_status */
	EFA_ADMIN_ILLEGAL_PARAMETER                 = 5,
	EFA_ADMIN_UNKNOWN_ERROR                     = 6,
	EFA_ADMIN_RESOURCE_BUSY                     = 7,
};

struct efa_admin_aq_common_desc {
	/*
	 * 11:0 : command_id
	 * 15:12 : reserved12
	 */
	u16 command_id;

	/* as appears in efa_admin_aq_opcode */
	u8 opcode;

	/*
	 * 0 : phase
	 * 1 : ctrl_data - control buffer address valid
	 * 2 : ctrl_data_indirect - control buffer address
	 *    points to list of pages with addresses of control
	 *    buffers
	 * 7:3 : reserved3
	 */
	u8 flags;
};

/*
 * used in efa_admin_aq_entry. Can point directly to control data, or to a
 * page list chunk. Used also at the end of indirect mode page list chunks,
 * for chaining.
 */
struct efa_admin_ctrl_buff_info {
	u32 length;

	struct efa_common_mem_addr address;
};

struct efa_admin_aq_entry {
	struct efa_admin_aq_common_desc aq_common_descriptor;

	union {
		u32 inline_data_w1[3];

		struct efa_admin_ctrl_buff_info control_buffer;
	} u;

	u32 inline_data_w4[12];
};

struct efa_admin_acq_common_desc {
	/*
	 * command identifier to associate it with the aq descriptor
	 * 11:0 : command_id
	 * 15:12 : reserved12
	 */
	u16 command;

	u8 status;

	/*
	 * 0 : phase
	 * 7:1 : reserved1
	 */
	u8 flags;

	u16 extended_status;

	/*
	 * indicates to the driver which AQ entry has been consumed by the
	 * device and could be reused
	 */
	u16 sq_head_indx;
};

struct efa_admin_acq_entry {
	struct efa_admin_acq_common_desc acq_common_descriptor;

	u32 response_specific_data[14];
};

struct efa_admin_aenq_common_desc {
	u16 group;

	u16 syndrom;

	/*
	 * 0 : phase
	 * 7:1 : reserved - MBZ
	 */
	u8 flags;

	u8 reserved1[3];

	u32 timestamp_low;

	u32 timestamp_high;
};

struct efa_admin_aenq_entry {
	struct efa_admin_aenq_common_desc aenq_common_desc;

	/* command specific inline data */
	u32 inline_data_w4[12];
};

enum efa_admin_eqe_event_type {
	EFA_ADMIN_EQE_EVENT_TYPE_COMPLETION         = 0,
};

/* Completion event */
struct efa_admin_comp_event {
	/* CQ number */
	u16 cqn;

	/* MBZ */
	u16 reserved;

	/* MBZ */
	u32 reserved2;
};

/* Event Queue Element */
struct efa_admin_eqe {
	/*
	 * 0 : phase
	 * 8:1 : event_type - Event type
	 * 31:9 : reserved - MBZ
	 */
	u32 common;

	/* MBZ */
	u32 reserved;

	union {
		/* Event data */
		u32 event_data[2];

		/* Completion Event */
		struct efa_admin_comp_event comp_event;
	} u;
};

/* aq_common_desc */
#define EFA_ADMIN_AQ_COMMON_DESC_COMMAND_ID_MASK            GENMASK(11, 0)
#define EFA_ADMIN_AQ_COMMON_DESC_PHASE_MASK                 BIT(0)
#define EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_MASK             BIT(1)
#define EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK    BIT(2)

/* acq_common_desc */
#define EFA_ADMIN_ACQ_COMMON_DESC_COMMAND_ID_MASK           GENMASK(11, 0)
#define EFA_ADMIN_ACQ_COMMON_DESC_PHASE_MASK                BIT(0)

/* aenq_common_desc */
#define EFA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK               BIT(0)

/* eqe */
#define EFA_ADMIN_EQE_PHASE_MASK                            BIT(0)
#define EFA_ADMIN_EQE_EVENT_TYPE_MASK                       GENMASK(8, 1)

#endif /* _EFA_ADMIN_H_ */
