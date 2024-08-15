/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_PROTOCOL_OPS_H
#define IOSM_IPC_PROTOCOL_OPS_H

#define SIZE_MASK 0x00FFFFFF
#define COMPLETION_STATUS 24
#define RESET_BIT 7

/**
 * enum ipc_mem_td_cs - Completion status of a TD
 * @IPC_MEM_TD_CS_INVALID:	      Initial status - td not yet used.
 * @IPC_MEM_TD_CS_PARTIAL_TRANSFER:   More data pending -> next TD used for this
 * @IPC_MEM_TD_CS_END_TRANSFER:	      IO transfer is complete.
 * @IPC_MEM_TD_CS_OVERFLOW:	      IO transfer to small for the buff to write
 * @IPC_MEM_TD_CS_ABORT:	      TD marked as abort and shall be discarded
 *				      by AP.
 * @IPC_MEM_TD_CS_ERROR:	      General error.
 */
enum ipc_mem_td_cs {
	IPC_MEM_TD_CS_INVALID,
	IPC_MEM_TD_CS_PARTIAL_TRANSFER,
	IPC_MEM_TD_CS_END_TRANSFER,
	IPC_MEM_TD_CS_OVERFLOW,
	IPC_MEM_TD_CS_ABORT,
	IPC_MEM_TD_CS_ERROR,
};

/**
 * enum ipc_mem_msg_cs - Completion status of IPC Message
 * @IPC_MEM_MSG_CS_INVALID:	Initial status.
 * @IPC_MEM_MSG_CS_SUCCESS:	IPC Message completion success.
 * @IPC_MEM_MSG_CS_ERROR:	Message send error.
 */
enum ipc_mem_msg_cs {
	IPC_MEM_MSG_CS_INVALID,
	IPC_MEM_MSG_CS_SUCCESS,
	IPC_MEM_MSG_CS_ERROR,
};

/**
 * struct ipc_msg_prep_args_pipe - struct for pipe args for message preparation
 * @pipe:	Pipe to open/close
 */
struct ipc_msg_prep_args_pipe {
	struct ipc_pipe *pipe;
};

/**
 * struct ipc_msg_prep_args_sleep - struct for sleep args for message
 *				    preparation
 * @target:	0=host, 1=device
 * @state:	0=enter sleep, 1=exit sleep
 */
struct ipc_msg_prep_args_sleep {
	unsigned int target;
	unsigned int state;
};

/**
 * struct ipc_msg_prep_feature_set - struct for feature set argument for
 *				     message preparation
 * @reset_enable:	0=out-of-band, 1=in-band-crash notification
 */
struct ipc_msg_prep_feature_set {
	u8 reset_enable;
};

/**
 * struct ipc_msg_prep_map - struct for map argument for message preparation
 * @region_id:	Region to map
 * @addr:	Pcie addr of region to map
 * @size:	Size of the region to map
 */
struct ipc_msg_prep_map {
	unsigned int region_id;
	unsigned long addr;
	size_t size;
};

/**
 * struct ipc_msg_prep_unmap - struct for unmap argument for message preparation
 * @region_id:	Region to unmap
 */
struct ipc_msg_prep_unmap {
	unsigned int region_id;
};

/**
 * struct ipc_msg_prep_args - Union to handle different message types
 * @pipe_open:		Pipe open message preparation struct
 * @pipe_close:		Pipe close message preparation struct
 * @sleep:		Sleep message preparation struct
 * @feature_set:	Feature set message preparation struct
 * @map:		Memory map message preparation struct
 * @unmap:		Memory unmap message preparation struct
 */
union ipc_msg_prep_args {
	struct ipc_msg_prep_args_pipe pipe_open;
	struct ipc_msg_prep_args_pipe pipe_close;
	struct ipc_msg_prep_args_sleep sleep;
	struct ipc_msg_prep_feature_set feature_set;
	struct ipc_msg_prep_map map;
	struct ipc_msg_prep_unmap unmap;
};

/**
 * enum ipc_msg_prep_type - Enum for message prepare actions
 * @IPC_MSG_PREP_SLEEP:		Sleep message preparation type
 * @IPC_MSG_PREP_PIPE_OPEN:	Pipe open message preparation type
 * @IPC_MSG_PREP_PIPE_CLOSE:	Pipe close message preparation type
 * @IPC_MSG_PREP_FEATURE_SET:	Feature set message preparation type
 * @IPC_MSG_PREP_MAP:		Memory map message preparation type
 * @IPC_MSG_PREP_UNMAP:		Memory unmap message preparation type
 */
enum ipc_msg_prep_type {
	IPC_MSG_PREP_SLEEP,
	IPC_MSG_PREP_PIPE_OPEN,
	IPC_MSG_PREP_PIPE_CLOSE,
	IPC_MSG_PREP_FEATURE_SET,
	IPC_MSG_PREP_MAP,
	IPC_MSG_PREP_UNMAP,
};

/**
 * struct ipc_rsp - Response to sent message
 * @completion:	For waking up requestor
 * @status:	Completion status
 */
struct ipc_rsp {
	struct completion completion;
	enum ipc_mem_msg_cs status;
};

/**
 * enum ipc_mem_msg - Type-definition of the messages.
 * @IPC_MEM_MSG_OPEN_PIPE:	AP ->CP: Open a pipe
 * @IPC_MEM_MSG_CLOSE_PIPE:	AP ->CP: Close a pipe
 * @IPC_MEM_MSG_ABORT_PIPE:	AP ->CP: wait for completion of the
 *				running transfer and abort all pending
 *				IO-transfers for the pipe
 * @IPC_MEM_MSG_SLEEP:		AP ->CP: host enter or exit sleep
 * @IPC_MEM_MSG_FEATURE_SET:	AP ->CP: Intel feature configuration
 */
enum ipc_mem_msg {
	IPC_MEM_MSG_OPEN_PIPE = 0x01,
	IPC_MEM_MSG_CLOSE_PIPE = 0x02,
	IPC_MEM_MSG_ABORT_PIPE = 0x03,
	IPC_MEM_MSG_SLEEP = 0x04,
	IPC_MEM_MSG_FEATURE_SET = 0xF0,
};

/**
 * struct ipc_mem_msg_open_pipe - Message structure for open pipe
 * @tdr_addr:			Tdr address
 * @tdr_entries:		Tdr entries
 * @pipe_nr:			Pipe number
 * @type_of_message:		Message type
 * @irq_vector:			MSI vector number
 * @accumulation_backoff:	Time in usec for data accumalation
 * @completion_status:		Message Completion Status
 */
struct ipc_mem_msg_open_pipe {
	__le64 tdr_addr;
	__le16 tdr_entries;
	u8 pipe_nr;
	u8 type_of_message;
	__le32 irq_vector;
	__le32 accumulation_backoff;
	__le32 completion_status;
};

/**
 * struct ipc_mem_msg_close_pipe - Message structure for close pipe
 * @reserved1:			Reserved
 * @reserved2:			Reserved
 * @pipe_nr:			Pipe number
 * @type_of_message:		Message type
 * @reserved3:			Reserved
 * @reserved4:			Reserved
 * @completion_status:		Message Completion Status
 */
struct ipc_mem_msg_close_pipe {
	__le32 reserved1[2];
	__le16 reserved2;
	u8 pipe_nr;
	u8 type_of_message;
	__le32  reserved3;
	__le32 reserved4;
	__le32 completion_status;
};

/**
 * struct ipc_mem_msg_abort_pipe - Message structure for abort pipe
 * @reserved1:			Reserved
 * @reserved2:			Reserved
 * @pipe_nr:			Pipe number
 * @type_of_message:		Message type
 * @reserved3:			Reserved
 * @reserved4:			Reserved
 * @completion_status:		Message Completion Status
 */
struct ipc_mem_msg_abort_pipe {
	__le32  reserved1[2];
	__le16 reserved2;
	u8 pipe_nr;
	u8 type_of_message;
	__le32 reserved3;
	__le32 reserved4;
	__le32 completion_status;
};

/**
 * struct ipc_mem_msg_host_sleep - Message structure for sleep message.
 * @reserved1:		Reserved
 * @target:		0=host, 1=device, host or EP devie
 *			is the message target
 * @state:		0=enter sleep, 1=exit sleep,
 *			2=enter sleep no protocol
 * @reserved2:		Reserved
 * @type_of_message:	Message type
 * @reserved3:		Reserved
 * @reserved4:		Reserved
 * @completion_status:	Message Completion Status
 */
struct ipc_mem_msg_host_sleep {
	__le32 reserved1[2];
	u8 target;
	u8 state;
	u8 reserved2;
	u8 type_of_message;
	__le32 reserved3;
	__le32 reserved4;
	__le32 completion_status;
};

/**
 * struct ipc_mem_msg_feature_set - Message structure for feature_set message
 * @reserved1:			Reserved
 * @reserved2:			Reserved
 * @reset_enable:		0=out-of-band, 1=in-band-crash notification
 * @type_of_message:		Message type
 * @reserved3:			Reserved
 * @reserved4:			Reserved
 * @completion_status:		Message Completion Status
 */
struct ipc_mem_msg_feature_set {
	__le32 reserved1[2];
	__le16 reserved2;
	u8 reset_enable;
	u8 type_of_message;
	__le32 reserved3;
	__le32 reserved4;
	__le32 completion_status;
};

/**
 * struct ipc_mem_msg_common - Message structure for completion status update.
 * @reserved1:			Reserved
 * @reserved2:			Reserved
 * @type_of_message:		Message type
 * @reserved3:			Reserved
 * @reserved4:			Reserved
 * @completion_status:		Message Completion Status
 */
struct ipc_mem_msg_common {
	__le32 reserved1[2];
	u8 reserved2[3];
	u8 type_of_message;
	__le32 reserved3;
	__le32 reserved4;
	__le32 completion_status;
};

/**
 * union ipc_mem_msg_entry - Union with all possible messages.
 * @open_pipe:		Open pipe message struct
 * @close_pipe:		Close pipe message struct
 * @abort_pipe:		Abort pipe message struct
 * @host_sleep:		Host sleep message struct
 * @feature_set:	Featuer set message struct
 * @common:		Used to access msg_type and to set the completion status
 */
union ipc_mem_msg_entry {
	struct ipc_mem_msg_open_pipe open_pipe;
	struct ipc_mem_msg_close_pipe close_pipe;
	struct ipc_mem_msg_abort_pipe abort_pipe;
	struct ipc_mem_msg_host_sleep host_sleep;
	struct ipc_mem_msg_feature_set feature_set;
	struct ipc_mem_msg_common common;
};

/* Transfer descriptor definition. */
struct ipc_protocol_td {
	union {
		/*   0 :  63 - 64-bit address of a buffer in host memory. */
		dma_addr_t address;
		struct {
			/*   0 :  31 - 32 bit address */
			__le32 address;
			/*  32 :  63 - corresponding descriptor */
			__le32 desc;
		} __packed shm;
	} buffer;

	/*	0 - 2nd byte - Size of the buffer.
	 *	The host provides the size of the buffer queued.
	 *	The EP device reads this value and shall update
	 *	it for downlink transfers to indicate the
	 *	amount of data written in buffer.
	 *	3rd byte - This field provides the completion status
	 *	of the TD. When queuing the TD, the host sets
	 *	the status to 0. The EP device updates this
	 *	field when completing the TD.
	 */
	__le32 scs;

	/*	0th - nr of following descriptors
	 *	1 - 3rd byte - reserved
	 */
	__le32 next;
} __packed;

/**
 * ipc_protocol_msg_prep - Prepare message based upon message type
 * @ipc_imem:	iosm_protocol instance
 * @msg_type:	message prepare type
 * @args:	message arguments
 *
 * Return: 0 on success and failure value on error
 */
int ipc_protocol_msg_prep(struct iosm_imem *ipc_imem,
			  enum ipc_msg_prep_type msg_type,
			  union ipc_msg_prep_args *args);

/**
 * ipc_protocol_msg_hp_update - Function for head pointer update
 *				of message ring
 * @ipc_imem:	iosm_protocol instance
 */
void ipc_protocol_msg_hp_update(struct iosm_imem *ipc_imem);

/**
 * ipc_protocol_msg_process - Function for processing responses
 *			      to IPC messages
 * @ipc_imem:	iosm_protocol instance
 * @irq:	IRQ vector
 *
 * Return:	True on success, false if error
 */
bool ipc_protocol_msg_process(struct iosm_imem *ipc_imem, int irq);

/**
 * ipc_protocol_ul_td_send - Function for sending the data to CP
 * @ipc_protocol:	iosm_protocol instance
 * @pipe:		Pipe instance
 * @p_ul_list:		uplink sk_buff list
 *
 * Return: true in success, false in case of error
 */
bool ipc_protocol_ul_td_send(struct iosm_protocol *ipc_protocol,
			     struct ipc_pipe *pipe,
			     struct sk_buff_head *p_ul_list);

/**
 * ipc_protocol_ul_td_process - Function for processing the sent data
 * @ipc_protocol:	iosm_protocol instance
 * @pipe:		Pipe instance
 *
 * Return: sk_buff instance
 */
struct sk_buff *ipc_protocol_ul_td_process(struct iosm_protocol *ipc_protocol,
					   struct ipc_pipe *pipe);

/**
 * ipc_protocol_dl_td_prepare - Function for providing DL TDs to CP
 * @ipc_protocol:	iosm_protocol instance
 * @pipe:		Pipe instance
 *
 * Return: true in success, false in case of error
 */
bool ipc_protocol_dl_td_prepare(struct iosm_protocol *ipc_protocol,
				struct ipc_pipe *pipe);

/**
 * ipc_protocol_dl_td_process - Function for processing the DL data
 * @ipc_protocol:	iosm_protocol instance
 * @pipe:		Pipe instance
 *
 * Return: sk_buff instance
 */
struct sk_buff *ipc_protocol_dl_td_process(struct iosm_protocol *ipc_protocol,
					   struct ipc_pipe *pipe);

/**
 * ipc_protocol_get_head_tail_index - Function for getting Head and Tail
 *				      pointer index of given pipe
 * @ipc_protocol:	iosm_protocol instance
 * @pipe:		Pipe Instance
 * @head:		head pointer index of the given pipe
 * @tail:		tail pointer index of the given pipe
 */
void ipc_protocol_get_head_tail_index(struct iosm_protocol *ipc_protocol,
				      struct ipc_pipe *pipe, u32 *head,
				      u32 *tail);
/**
 * ipc_protocol_get_ipc_status - Function for getting the IPC Status
 * @ipc_protocol:	iosm_protocol instance
 *
 * Return: Returns IPC State
 */
enum ipc_mem_device_ipc_state ipc_protocol_get_ipc_status(struct iosm_protocol
							  *ipc_protocol);

/**
 * ipc_protocol_pipe_cleanup - Function to cleanup pipe resources
 * @ipc_protocol:	iosm_protocol instance
 * @pipe:		Pipe instance
 */
void ipc_protocol_pipe_cleanup(struct iosm_protocol *ipc_protocol,
			       struct ipc_pipe *pipe);

/**
 * ipc_protocol_get_ap_exec_stage - Function for getting AP Exec Stage
 * @ipc_protocol:	pointer to struct iosm protocol
 *
 * Return: returns BOOT Stages
 */
enum ipc_mem_exec_stage
ipc_protocol_get_ap_exec_stage(struct iosm_protocol *ipc_protocol);

/**
 * ipc_protocol_pm_dev_get_sleep_notification - Function for getting Dev Sleep
 *						notification
 * @ipc_protocol:	iosm_protocol instance
 *
 * Return: Returns dev PM State
 */
u32 ipc_protocol_pm_dev_get_sleep_notification(struct iosm_protocol
					       *ipc_protocol);
#endif
