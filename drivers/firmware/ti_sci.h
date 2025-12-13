/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Texas Instruments System Control Interface (TISCI) Protocol
 *
 * Communication protocol with TI SCI hardware
 * The system works in a message response protocol
 * See: https://software-dl.ti.com/tisci/esd/latest/index.html for details
 *
 * Copyright (C)  2015-2024 Texas Instruments Incorporated - https://www.ti.com/
 */

#ifndef __TI_SCI_H
#define __TI_SCI_H

/* Generic Messages */
#define TI_SCI_MSG_ENABLE_WDT	0x0000
#define TI_SCI_MSG_WAKE_RESET	0x0001
#define TI_SCI_MSG_VERSION	0x0002
#define TI_SCI_MSG_WAKE_REASON	0x0003
#define TI_SCI_MSG_GOODBYE	0x0004
#define TI_SCI_MSG_SYS_RESET	0x0005
#define TI_SCI_MSG_QUERY_FW_CAPS	0x0022

/* Device requests */
#define TI_SCI_MSG_SET_DEVICE_STATE	0x0200
#define TI_SCI_MSG_GET_DEVICE_STATE	0x0201
#define TI_SCI_MSG_SET_DEVICE_RESETS	0x0202

/* Clock requests */
#define TI_SCI_MSG_SET_CLOCK_STATE	0x0100
#define TI_SCI_MSG_GET_CLOCK_STATE	0x0101
#define TI_SCI_MSG_SET_CLOCK_PARENT	0x0102
#define TI_SCI_MSG_GET_CLOCK_PARENT	0x0103
#define TI_SCI_MSG_GET_NUM_CLOCK_PARENTS 0x0104
#define TI_SCI_MSG_SET_CLOCK_FREQ	0x010c
#define TI_SCI_MSG_QUERY_CLOCK_FREQ	0x010d
#define TI_SCI_MSG_GET_CLOCK_FREQ	0x010e

/* Low Power Mode Requests */
#define TI_SCI_MSG_PREPARE_SLEEP	0x0300
#define TI_SCI_MSG_LPM_WAKE_REASON	0x0306
#define TI_SCI_MSG_SET_IO_ISOLATION	0x0307
#define TI_SCI_MSG_LPM_SET_DEVICE_CONSTRAINT	0x0309
#define TI_SCI_MSG_LPM_SET_LATENCY_CONSTRAINT	0x030A
#define TI_SCI_MSG_LPM_ABORT	0x0311

/* Resource Management Requests */
#define TI_SCI_MSG_GET_RESOURCE_RANGE	0x1500

/* IRQ requests */
#define TI_SCI_MSG_SET_IRQ		0x1000
#define TI_SCI_MSG_FREE_IRQ		0x1001

/* NAVSS resource management */
/* Ringacc requests */
#define TI_SCI_MSG_RM_RING_ALLOCATE		0x1100
#define TI_SCI_MSG_RM_RING_FREE			0x1101
#define TI_SCI_MSG_RM_RING_RECONFIG		0x1102
#define TI_SCI_MSG_RM_RING_RESET		0x1103
#define TI_SCI_MSG_RM_RING_CFG			0x1110

/* PSI-L requests */
#define TI_SCI_MSG_RM_PSIL_PAIR			0x1280
#define TI_SCI_MSG_RM_PSIL_UNPAIR		0x1281

#define TI_SCI_MSG_RM_UDMAP_TX_ALLOC		0x1200
#define TI_SCI_MSG_RM_UDMAP_TX_FREE		0x1201
#define TI_SCI_MSG_RM_UDMAP_RX_ALLOC		0x1210
#define TI_SCI_MSG_RM_UDMAP_RX_FREE		0x1211
#define TI_SCI_MSG_RM_UDMAP_FLOW_CFG		0x1220
#define TI_SCI_MSG_RM_UDMAP_OPT_FLOW_CFG	0x1221

#define TISCI_MSG_RM_UDMAP_TX_CH_CFG		0x1205
#define TISCI_MSG_RM_UDMAP_TX_CH_GET_CFG	0x1206
#define TISCI_MSG_RM_UDMAP_RX_CH_CFG		0x1215
#define TISCI_MSG_RM_UDMAP_RX_CH_GET_CFG	0x1216
#define TISCI_MSG_RM_UDMAP_FLOW_CFG		0x1230
#define TISCI_MSG_RM_UDMAP_FLOW_SIZE_THRESH_CFG	0x1231
#define TISCI_MSG_RM_UDMAP_FLOW_GET_CFG		0x1232
#define TISCI_MSG_RM_UDMAP_FLOW_SIZE_THRESH_GET_CFG	0x1233

/* Processor Control requests */
#define TI_SCI_MSG_PROC_REQUEST		0xc000
#define TI_SCI_MSG_PROC_RELEASE		0xc001
#define TI_SCI_MSG_PROC_HANDOVER	0xc005
#define TI_SCI_MSG_SET_CONFIG		0xc100
#define TI_SCI_MSG_SET_CTRL		0xc101
#define TI_SCI_MSG_GET_STATUS		0xc400

/**
 * struct ti_sci_msg_hdr - Generic Message Header for All messages and responses
 * @type:	Type of messages: One of TI_SCI_MSG* values
 * @host:	Host of the message
 * @seq:	Message identifier indicating a transfer sequence
 * @flags:	Flag for the message
 */
struct ti_sci_msg_hdr {
	u16 type;
	u8 host;
	u8 seq;
#define TI_SCI_MSG_FLAG(val)			(1 << (val))
#define TI_SCI_FLAG_REQ_GENERIC_NORESPONSE	0x0
#define TI_SCI_FLAG_REQ_ACK_ON_RECEIVED		TI_SCI_MSG_FLAG(0)
#define TI_SCI_FLAG_REQ_ACK_ON_PROCESSED	TI_SCI_MSG_FLAG(1)
#define TI_SCI_FLAG_RESP_GENERIC_NACK		0x0
#define TI_SCI_FLAG_RESP_GENERIC_ACK		TI_SCI_MSG_FLAG(1)
	/* Additional Flags */
	u32 flags;
} __packed;

/**
 * struct ti_sci_msg_resp_version - Response for a message
 * @hdr:		Generic header
 * @firmware_description: String describing the firmware
 * @firmware_revision:	Firmware revision
 * @abi_major:		Major version of the ABI that firmware supports
 * @abi_minor:		Minor version of the ABI that firmware supports
 *
 * In general, ABI version changes follow the rule that minor version increments
 * are backward compatible. Major revision changes in ABI may not be
 * backward compatible.
 *
 * Response to a generic message with message type TI_SCI_MSG_VERSION
 */
struct ti_sci_msg_resp_version {
	struct ti_sci_msg_hdr hdr;
	char firmware_description[32];
	u16 firmware_revision;
	u8 abi_major;
	u8 abi_minor;
} __packed;

/**
 * struct ti_sci_msg_req_reboot - Reboot the SoC
 * @hdr:	Generic Header
 *
 * Request type is TI_SCI_MSG_SYS_RESET, responded with a generic
 * ACK/NACK message.
 */
struct ti_sci_msg_req_reboot {
	struct ti_sci_msg_hdr hdr;
} __packed;

/**
 * struct ti_sci_msg_resp_query_fw_caps - Response for query firmware caps
 * @hdr:	Generic header
 * @fw_caps:	Each bit in fw_caps indicating one FW/SOC capability
 *		MSG_FLAG_CAPS_GENERIC: Generic capability (LPM not supported)
 *		MSG_FLAG_CAPS_LPM_PARTIAL_IO: Partial IO in LPM
 *		MSG_FLAG_CAPS_LPM_DM_MANAGED: LPM can be managed by DM
 *		MSG_FLAG_CAPS_LPM_ABORT: Abort entry to LPM
 *
 * Response to a generic message with message type TI_SCI_MSG_QUERY_FW_CAPS
 * providing currently available SOC/firmware capabilities. SoC that don't
 * support low power modes return only MSG_FLAG_CAPS_GENERIC capability.
 */
struct ti_sci_msg_resp_query_fw_caps {
	struct ti_sci_msg_hdr hdr;
#define MSG_FLAG_CAPS_GENERIC		TI_SCI_MSG_FLAG(0)
#define MSG_FLAG_CAPS_LPM_PARTIAL_IO	TI_SCI_MSG_FLAG(4)
#define MSG_FLAG_CAPS_LPM_DM_MANAGED	TI_SCI_MSG_FLAG(5)
#define MSG_FLAG_CAPS_LPM_ABORT		TI_SCI_MSG_FLAG(9)
#define MSG_MASK_CAPS_LPM		GENMASK_ULL(4, 1)
	u64 fw_caps;
} __packed;

/**
 * struct ti_sci_msg_req_set_device_state - Set the desired state of the device
 * @hdr:		Generic header
 * @id:	Indicates which device to modify
 * @reserved: Reserved space in message, must be 0 for backward compatibility
 * @state: The desired state of the device.
 *
 * Certain flags can also be set to alter the device state:
 * + MSG_FLAG_DEVICE_WAKE_ENABLED - Configure the device to be a wake source.
 * The meaning of this flag will vary slightly from device to device and from
 * SoC to SoC but it generally allows the device to wake the SoC out of deep
 * suspend states.
 * + MSG_FLAG_DEVICE_RESET_ISO - Enable reset isolation for this device.
 * + MSG_FLAG_DEVICE_EXCLUSIVE - Claim this device exclusively. When passed
 * with STATE_RETENTION or STATE_ON, it will claim the device exclusively.
 * If another host already has this device set to STATE_RETENTION or STATE_ON,
 * the message will fail. Once successful, other hosts attempting to set
 * STATE_RETENTION or STATE_ON will fail.
 *
 * Request type is TI_SCI_MSG_SET_DEVICE_STATE, responded with a generic
 * ACK/NACK message.
 */
struct ti_sci_msg_req_set_device_state {
	/* Additional hdr->flags options */
#define MSG_FLAG_DEVICE_WAKE_ENABLED	TI_SCI_MSG_FLAG(8)
#define MSG_FLAG_DEVICE_RESET_ISO	TI_SCI_MSG_FLAG(9)
#define MSG_FLAG_DEVICE_EXCLUSIVE	TI_SCI_MSG_FLAG(10)
	struct ti_sci_msg_hdr hdr;
	u32 id;
	u32 reserved;

#define MSG_DEVICE_SW_STATE_AUTO_OFF	0
#define MSG_DEVICE_SW_STATE_RETENTION	1
#define MSG_DEVICE_SW_STATE_ON		2
	u8 state;
} __packed;

/**
 * struct ti_sci_msg_req_get_device_state - Request to get device.
 * @hdr:		Generic header
 * @id:		Device Identifier
 *
 * Request type is TI_SCI_MSG_GET_DEVICE_STATE, responded device state
 * information
 */
struct ti_sci_msg_req_get_device_state {
	struct ti_sci_msg_hdr hdr;
	u32 id;
} __packed;

/**
 * struct ti_sci_msg_resp_get_device_state - Response to get device request.
 * @hdr:		Generic header
 * @context_loss_count: Indicates how many times the device has lost context. A
 *	driver can use this monotonic counter to determine if the device has
 *	lost context since the last time this message was exchanged.
 * @resets: Programmed state of the reset lines.
 * @programmed_state:	The state as programmed by set_device.
 *			- Uses the MSG_DEVICE_SW_* macros
 * @current_state:	The actual state of the hardware.
 *
 * Response to request TI_SCI_MSG_GET_DEVICE_STATE.
 */
struct ti_sci_msg_resp_get_device_state {
	struct ti_sci_msg_hdr hdr;
	u32 context_loss_count;
	u32 resets;
	u8 programmed_state;
#define MSG_DEVICE_HW_STATE_OFF		0
#define MSG_DEVICE_HW_STATE_ON		1
#define MSG_DEVICE_HW_STATE_TRANS	2
	u8 current_state;
} __packed;

/**
 * struct ti_sci_msg_req_set_device_resets - Set the desired resets
 *				configuration of the device
 * @hdr:		Generic header
 * @id:	Indicates which device to modify
 * @resets: A bit field of resets for the device. The meaning, behavior,
 *	and usage of the reset flags are device specific. 0 for a bit
 *	indicates releasing the reset represented by that bit while 1
 *	indicates keeping it held.
 *
 * Request type is TI_SCI_MSG_SET_DEVICE_RESETS, responded with a generic
 * ACK/NACK message.
 */
struct ti_sci_msg_req_set_device_resets {
	struct ti_sci_msg_hdr hdr;
	u32 id;
	u32 resets;
} __packed;

/**
 * struct ti_sci_msg_req_set_clock_state - Request to setup a Clock state
 * @hdr:	Generic Header, Certain flags can be set specific to the clocks:
 *		MSG_FLAG_CLOCK_ALLOW_SSC: Allow this clock to be modified
 *		via spread spectrum clocking.
 *		MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE: Allow this clock's
 *		frequency to be changed while it is running so long as it
 *		is within the min/max limits.
 *		MSG_FLAG_CLOCK_INPUT_TERM: Enable input termination, this
 *		is only applicable to clock inputs on the SoC pseudo-device.
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify. Set to 255 if clock ID is
 *		greater than or equal to 255.
 * @request_state: Request the state for the clock to be set to.
 *		MSG_CLOCK_SW_STATE_UNREQ: The IP does not require this clock,
 *		it can be disabled, regardless of the state of the device
 *		MSG_CLOCK_SW_STATE_AUTO: Allow the System Controller to
 *		automatically manage the state of this clock. If the device
 *		is enabled, then the clock is enabled. If the device is set
 *		to off or retention, then the clock is internally set as not
 *		being required by the device.(default)
 *		MSG_CLOCK_SW_STATE_REQ:  Configure the clock to be enabled,
 *		regardless of the state of the device.
 * @clk_id_32:	Clock identifier for the device for this request.
 *		Only to be used if the clock ID is greater than or equal to
 *		255.
 *
 * Normally, all required clocks are managed by TISCI entity, this is used
 * only for specific control *IF* required. Auto managed state is
 * MSG_CLOCK_SW_STATE_AUTO, in other states, TISCI entity assume remote
 * will explicitly control.
 *
 * Request type is TI_SCI_MSG_SET_CLOCK_STATE, response is a generic
 * ACK or NACK message.
 */
struct ti_sci_msg_req_set_clock_state {
	/* Additional hdr->flags options */
#define MSG_FLAG_CLOCK_ALLOW_SSC		TI_SCI_MSG_FLAG(8)
#define MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE	TI_SCI_MSG_FLAG(9)
#define MSG_FLAG_CLOCK_INPUT_TERM		TI_SCI_MSG_FLAG(10)
	struct ti_sci_msg_hdr hdr;
	u32 dev_id;
	u8 clk_id;
#define MSG_CLOCK_SW_STATE_UNREQ	0
#define MSG_CLOCK_SW_STATE_AUTO		1
#define MSG_CLOCK_SW_STATE_REQ		2
	u8 request_state;
	u32 clk_id_32;
} __packed;

/**
 * struct ti_sci_msg_req_get_clock_state - Request for clock state
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to get state of. Set to 255 if the clock
 *		ID is greater than or equal to 255.
 * @clk_id_32:	Clock identifier for the device for the request.
 *		Only to be used if the clock ID is greater than or equal to
 *		255.
 *
 * Request type is TI_SCI_MSG_GET_CLOCK_STATE, response is state
 * of the clock
 */
struct ti_sci_msg_req_get_clock_state {
	struct ti_sci_msg_hdr hdr;
	u32 dev_id;
	u8 clk_id;
	u32 clk_id_32;
} __packed;

/**
 * struct ti_sci_msg_resp_get_clock_state - Response to get clock state
 * @hdr:	Generic Header
 * @programmed_state: Any programmed state of the clock. This is one of
 *		MSG_CLOCK_SW_STATE* values.
 * @current_state: Current state of the clock. This is one of:
 *		MSG_CLOCK_HW_STATE_NOT_READY: Clock is not ready
 *		MSG_CLOCK_HW_STATE_READY: Clock is ready
 *
 * Response to TI_SCI_MSG_GET_CLOCK_STATE.
 */
struct ti_sci_msg_resp_get_clock_state {
	struct ti_sci_msg_hdr hdr;
	u8 programmed_state;
#define MSG_CLOCK_HW_STATE_NOT_READY	0
#define MSG_CLOCK_HW_STATE_READY	1
	u8 current_state;
} __packed;

/**
 * struct ti_sci_msg_req_set_clock_parent - Set the clock parent
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to modify. Set to 255 if clock ID is
 *		greater than or equal to 255.
 * @parent_id:	The new clock parent is selectable by an index via this
 *		parameter. Set to 255 if clock ID is greater than or
 *		equal to 255.
 * @clk_id_32:	Clock identifier if @clk_id field is 255.
 * @parent_id_32:	Parent identifier if @parent_id is 255.
 *
 * Request type is TI_SCI_MSG_SET_CLOCK_PARENT, response is generic
 * ACK / NACK message.
 */
struct ti_sci_msg_req_set_clock_parent {
	struct ti_sci_msg_hdr hdr;
	u32 dev_id;
	u8 clk_id;
	u8 parent_id;
	u32 clk_id_32;
	u32 parent_id_32;
} __packed;

/**
 * struct ti_sci_msg_req_get_clock_parent - Get the clock parent
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request.
 *		Each device has it's own set of clock inputs. This indexes
 *		which clock input to get the parent for. If this field
 *		contains 255, the actual clock identifier is stored in
 *		@clk_id_32.
 * @clk_id_32:	Clock identifier if the @clk_id field contains 255.
 *
 * Request type is TI_SCI_MSG_GET_CLOCK_PARENT, response is parent information
 */
struct ti_sci_msg_req_get_clock_parent {
	struct ti_sci_msg_hdr hdr;
	u32 dev_id;
	u8 clk_id;
	u32 clk_id_32;
} __packed;

/**
 * struct ti_sci_msg_resp_get_clock_parent - Response with clock parent
 * @hdr:	Generic Header
 * @parent_id:	The current clock parent. If set to 255, the current parent
 *		ID can be found from the @parent_id_32 field.
 * @parent_id_32:	Current clock parent if @parent_id field is set to
 *			255.
 *
 * Response to TI_SCI_MSG_GET_CLOCK_PARENT.
 */
struct ti_sci_msg_resp_get_clock_parent {
	struct ti_sci_msg_hdr hdr;
	u8 parent_id;
	u32 parent_id_32;
} __packed;

/**
 * struct ti_sci_msg_req_get_clock_num_parents - Request to get clock parents
 * @hdr:	Generic header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request. Set to
 *		255 if clock ID is greater than or equal to 255.
 * @clk_id_32:	Clock identifier if the @clk_id field contains 255.
 *
 * This request provides information about how many clock parent options
 * are available for a given clock to a device. This is typically used
 * for input clocks.
 *
 * Request type is TI_SCI_MSG_GET_NUM_CLOCK_PARENTS, response is appropriate
 * message, or NACK in case of inability to satisfy request.
 */
struct ti_sci_msg_req_get_clock_num_parents {
	struct ti_sci_msg_hdr hdr;
	u32 dev_id;
	u8 clk_id;
	u32 clk_id_32;
} __packed;

/**
 * struct ti_sci_msg_resp_get_clock_num_parents - Response for get clk parents
 * @hdr:		Generic header
 * @num_parents:	Number of clock parents. If set to 255, the actual
 *			number of parents is stored into @num_parents_32
 *			field instead.
 * @num_parents_32:	Number of clock parents if @num_parents field is
 *			set to 255.
 *
 * Response to TI_SCI_MSG_GET_NUM_CLOCK_PARENTS
 */
struct ti_sci_msg_resp_get_clock_num_parents {
	struct ti_sci_msg_hdr hdr;
	u8 num_parents;
	u32 num_parents_32;
} __packed;

/**
 * struct ti_sci_msg_req_query_clock_freq - Request to query a frequency
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @min_freq_hz: The minimum allowable frequency in Hz. This is the minimum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @target_freq_hz: The target clock frequency. A frequency will be found
 *		as close to this target frequency as possible.
 * @max_freq_hz: The maximum allowable frequency in Hz. This is the maximum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @clk_id:	Clock identifier for the device for this request. Set to
 *		255 if clock identifier is greater than or equal to 255.
 * @clk_id_32:	Clock identifier if @clk_id is set to 255.
 *
 * NOTE: Normally clock frequency management is automatically done by TISCI
 * entity. In case of specific requests, TISCI evaluates capability to achieve
 * requested frequency within provided range and responds with
 * result message.
 *
 * Request type is TI_SCI_MSG_QUERY_CLOCK_FREQ, response is appropriate message,
 * or NACK in case of inability to satisfy request.
 */
struct ti_sci_msg_req_query_clock_freq {
	struct ti_sci_msg_hdr hdr;
	u32 dev_id;
	u64 min_freq_hz;
	u64 target_freq_hz;
	u64 max_freq_hz;
	u8 clk_id;
	u32 clk_id_32;
} __packed;

/**
 * struct ti_sci_msg_resp_query_clock_freq - Response to a clock frequency query
 * @hdr:	Generic Header
 * @freq_hz:	Frequency that is the best match in Hz.
 *
 * Response to request type TI_SCI_MSG_QUERY_CLOCK_FREQ. NOTE: if the request
 * cannot be satisfied, the message will be of type NACK.
 */
struct ti_sci_msg_resp_query_clock_freq {
	struct ti_sci_msg_hdr hdr;
	u64 freq_hz;
} __packed;

/**
 * struct ti_sci_msg_req_set_clock_freq - Request to setup a clock frequency
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @min_freq_hz: The minimum allowable frequency in Hz. This is the minimum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @target_freq_hz: The target clock frequency. The clock will be programmed
 *		at a rate as close to this target frequency as possible.
 * @max_freq_hz: The maximum allowable frequency in Hz. This is the maximum
 *		allowable programmed frequency and does not account for clock
 *		tolerances and jitter.
 * @clk_id:	Clock identifier for the device for this request. Set to
 *		255 if clock ID is greater than or equal to 255.
 * @clk_id_32:	Clock identifier if @clk_id field is set to 255.
 *
 * NOTE: Normally clock frequency management is automatically done by TISCI
 * entity. In case of specific requests, TISCI evaluates capability to achieve
 * requested range and responds with success/failure message.
 *
 * This sets the desired frequency for a clock within an allowable
 * range. This message will fail on an enabled clock unless
 * MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE is set for the clock. Additionally,
 * if other clocks have their frequency modified due to this message,
 * they also must have the MSG_FLAG_CLOCK_ALLOW_FREQ_CHANGE or be disabled.
 *
 * Calling set frequency on a clock input to the SoC pseudo-device will
 * inform the PMMC of that clock's frequency. Setting a frequency of
 * zero will indicate the clock is disabled.
 *
 * Calling set frequency on clock outputs from the SoC pseudo-device will
 * function similarly to setting the clock frequency on a device.
 *
 * Request type is TI_SCI_MSG_SET_CLOCK_FREQ, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_set_clock_freq {
	struct ti_sci_msg_hdr hdr;
	u32 dev_id;
	u64 min_freq_hz;
	u64 target_freq_hz;
	u64 max_freq_hz;
	u8 clk_id;
	u32 clk_id_32;
} __packed;

/**
 * struct ti_sci_msg_req_get_clock_freq - Request to get the clock frequency
 * @hdr:	Generic Header
 * @dev_id:	Device identifier this request is for
 * @clk_id:	Clock identifier for the device for this request. Set to
 *		255 if clock ID is greater than or equal to 255.
 * @clk_id_32:	Clock identifier if @clk_id field is set to 255.
 *
 * NOTE: Normally clock frequency management is automatically done by TISCI
 * entity. In some cases, clock frequencies are configured by host.
 *
 * Request type is TI_SCI_MSG_GET_CLOCK_FREQ, responded with clock frequency
 * that the clock is currently at.
 */
struct ti_sci_msg_req_get_clock_freq {
	struct ti_sci_msg_hdr hdr;
	u32 dev_id;
	u8 clk_id;
	u32 clk_id_32;
} __packed;

/**
 * struct ti_sci_msg_resp_get_clock_freq - Response of clock frequency request
 * @hdr:	Generic Header
 * @freq_hz:	Frequency that the clock is currently on, in Hz.
 *
 * Response to request type TI_SCI_MSG_GET_CLOCK_FREQ.
 */
struct ti_sci_msg_resp_get_clock_freq {
	struct ti_sci_msg_hdr hdr;
	u64 freq_hz;
} __packed;

/**
 * struct tisci_msg_req_prepare_sleep - Request for TISCI_MSG_PREPARE_SLEEP.
 *
 * @hdr				TISCI header to provide ACK/NAK flags to the host.
 * @mode			Low power mode to enter.
 * @ctx_lo			Low 32-bits of physical pointer to address to use for context save.
 * @ctx_hi			High 32-bits of physical pointer to address to use for context save.
 * @debug_flags			Flags that can be set to halt the sequence during suspend or
 *				resume to allow JTAG connection and debug.
 *
 * This message is used as the first step of entering a low power mode. It
 * allows configurable information, including which state to enter to be
 * easily shared from the application, as this is a non-secure message and
 * therefore can be sent by anyone.
 */
struct ti_sci_msg_req_prepare_sleep {
	struct ti_sci_msg_hdr	hdr;

#define TISCI_MSG_VALUE_SLEEP_MODE_DM_MANAGED				0xfd
	u8			mode;
	u32			ctx_lo;
	u32			ctx_hi;
	u32			debug_flags;
} __packed;

/**
 * struct tisci_msg_set_io_isolation_req - Request for TI_SCI_MSG_SET_IO_ISOLATION.
 *
 * @hdr:	Generic header
 * @state:	The deseared state of the IO isolation.
 *
 * This message is used to enable/disable IO isolation for low power modes.
 * Response is generic ACK / NACK message.
 */
struct ti_sci_msg_req_set_io_isolation {
	struct ti_sci_msg_hdr hdr;
	u8 state;
} __packed;

/**
 * struct ti_sci_msg_resp_lpm_wake_reason - Response for TI_SCI_MSG_LPM_WAKE_REASON.
 *
 * @hdr:		Generic header.
 * @wake_source:	The wake up source that woke soc from LPM.
 * @wake_timestamp:	Timestamp at which soc woke.
 * @wake_pin: The pin that has triggered wake up.
 * @mode: The last entered low power mode.
 * @rsvd:	Reserved for future use.
 *
 * Response to a generic message with message type TI_SCI_MSG_LPM_WAKE_REASON,
 * used to query the wake up source, pin and entered low power mode.
 */
struct ti_sci_msg_resp_lpm_wake_reason {
	struct ti_sci_msg_hdr hdr;
	u32 wake_source;
	u64 wake_timestamp;
	u8 wake_pin;
	u8 mode;
	u32 rsvd[2];
} __packed;

/**
 * struct ti_sci_msg_req_lpm_set_device_constraint - Request for
 * TISCI_MSG_LPM_SET_DEVICE_CONSTRAINT.
 *
 * @hdr:	TISCI header to provide ACK/NAK flags to the host.
 * @id:	Device ID of device whose constraint has to be modified.
 * @state:	The desired state of device constraint: set or clear.
 * @rsvd:	Reserved for future use.
 *
 * This message is used by host to set constraint on the device. This can be
 * sent anytime after boot before prepare sleep message. Any device can set a
 * constraint on the low power mode that the SoC can enter. It allows
 * configurable information to be easily shared from the application, as this
 * is a non-secure message and therefore can be sent by anyone. By setting a
 * constraint, the device ensures that it will not be powered off or reset in
 * the selected mode. Note: Access Restriction: Exclusivity flag of Device will
 * be honored. If some other host already has constraint on this device ID,
 * NACK will be returned.
 */
struct ti_sci_msg_req_lpm_set_device_constraint {
	struct ti_sci_msg_hdr hdr;
	u32 id;
	u8 state;
	u32 rsvd[2];
} __packed;

/**
 * struct ti_sci_msg_req_lpm_set_latency_constraint - Request for
 * TISCI_MSG_LPM_SET_LATENCY_CONSTRAINT.
 *
 * @hdr:	TISCI header to provide ACK/NAK flags to the host.
 * @wkup_latency:	The maximum acceptable latency to wake up from low power mode
 *			in milliseconds. The deeper the state, the higher the latency.
 * @state:	The desired state of wakeup latency constraint: set or clear.
 * @rsvd:	Reserved for future use.
 *
 * This message is used by host to set wakeup latency from low power mode. This can
 * be sent anytime after boot before prepare sleep message, and can be sent after
 * current low power mode is exited. Any device can set a constraint on the low power
 * mode that the SoC can enter. It allows configurable information to be easily shared
 * from the application, as this is a non-secure message and therefore can be sent by
 * anyone. By setting a wakeup latency constraint, the host ensures that the resume time
 * from selected low power mode will be less than the constraint value.
 */
struct ti_sci_msg_req_lpm_set_latency_constraint {
	struct ti_sci_msg_hdr hdr;
	u16 latency;
	u8 state;
	u32 rsvd;
} __packed;

#define TI_SCI_IRQ_SECONDARY_HOST_INVALID	0xff

/**
 * struct ti_sci_msg_req_get_resource_range - Request to get a host's assigned
 *					      range of resources.
 * @hdr:		Generic Header
 * @type:		Unique resource assignment type
 * @subtype:		Resource assignment subtype within the resource type.
 * @secondary_host:	Host processing entity to which the resources are
 *			allocated. This is required only when the destination
 *			host id id different from ti sci interface host id,
 *			else TI_SCI_IRQ_SECONDARY_HOST_INVALID can be passed.
 *
 * Request type is TI_SCI_MSG_GET_RESOURCE_RANGE. Responded with requested
 * resource range which is of type TI_SCI_MSG_GET_RESOURCE_RANGE.
 */
struct ti_sci_msg_req_get_resource_range {
	struct ti_sci_msg_hdr hdr;
#define MSG_RM_RESOURCE_TYPE_MASK	GENMASK(9, 0)
#define MSG_RM_RESOURCE_SUBTYPE_MASK	GENMASK(5, 0)
	u16 type;
	u8 subtype;
	u8 secondary_host;
} __packed;

/**
 * struct ti_sci_msg_resp_get_resource_range - Response to resource get range.
 * @hdr:		Generic Header
 * @range_start:	Start index of the first resource range.
 * @range_num:		Number of resources in the first range.
 * @range_start_sec:	Start index of the second resource range.
 * @range_num_sec:	Number of resources in the second range.
 *
 * Response to request TI_SCI_MSG_GET_RESOURCE_RANGE.
 */
struct ti_sci_msg_resp_get_resource_range {
	struct ti_sci_msg_hdr hdr;
	u16 range_start;
	u16 range_num;
	u16 range_start_sec;
	u16 range_num_sec;
} __packed;

/**
 * struct ti_sci_msg_req_manage_irq - Request to configure/release the route
 *					between the dev and the host.
 * @hdr:		Generic Header
 * @valid_params:	Bit fields defining the validity of interrupt source
 *			parameters. If a bit is not set, then corresponding
 *			field is not valid and will not be used for route set.
 *			Bit field definitions:
 *			0 - Valid bit for @dst_id
 *			1 - Valid bit for @dst_host_irq
 *			2 - Valid bit for @ia_id
 *			3 - Valid bit for @vint
 *			4 - Valid bit for @global_event
 *			5 - Valid bit for @vint_status_bit_index
 *			31 - Valid bit for @secondary_host
 * @src_id:		IRQ source peripheral ID.
 * @src_index:		IRQ source index within the peripheral
 * @dst_id:		IRQ Destination ID. Based on the architecture it can be
 *			IRQ controller or host processor ID.
 * @dst_host_irq:	IRQ number of the destination host IRQ controller
 * @ia_id:		Device ID of the interrupt aggregator in which the
 *			vint resides.
 * @vint:		Virtual interrupt number if the interrupt route
 *			is through an interrupt aggregator.
 * @global_event:	Global event that is to be mapped to interrupt
 *			aggregator virtual interrupt status bit.
 * @vint_status_bit:	Virtual interrupt status bit if the interrupt route
 *			utilizes an interrupt aggregator status bit.
 * @secondary_host:	Host ID of the IRQ destination computing entity. This is
 *			required only when destination host id is different
 *			from ti sci interface host id.
 *
 * Request type is TI_SCI_MSG_SET/RELEASE_IRQ.
 * Response is generic ACK / NACK message.
 */
struct ti_sci_msg_req_manage_irq {
	struct ti_sci_msg_hdr hdr;
#define MSG_FLAG_DST_ID_VALID			TI_SCI_MSG_FLAG(0)
#define MSG_FLAG_DST_HOST_IRQ_VALID		TI_SCI_MSG_FLAG(1)
#define MSG_FLAG_IA_ID_VALID			TI_SCI_MSG_FLAG(2)
#define MSG_FLAG_VINT_VALID			TI_SCI_MSG_FLAG(3)
#define MSG_FLAG_GLB_EVNT_VALID			TI_SCI_MSG_FLAG(4)
#define MSG_FLAG_VINT_STS_BIT_VALID		TI_SCI_MSG_FLAG(5)
#define MSG_FLAG_SHOST_VALID			TI_SCI_MSG_FLAG(31)
	u32 valid_params;
	u16 src_id;
	u16 src_index;
	u16 dst_id;
	u16 dst_host_irq;
	u16 ia_id;
	u16 vint;
	u16 global_event;
	u8 vint_status_bit;
	u8 secondary_host;
} __packed;

/**
 * struct ti_sci_msg_rm_ring_cfg_req - Configure a Navigator Subsystem ring
 *
 * Configures the non-real-time registers of a Navigator Subsystem ring.
 * @hdr:	Generic Header
 * @valid_params: Bitfield defining validity of ring configuration parameters.
 *	The ring configuration fields are not valid, and will not be used for
 *	ring configuration, if their corresponding valid bit is zero.
 *	Valid bit usage:
 *	0 - Valid bit for @tisci_msg_rm_ring_cfg_req addr_lo
 *	1 - Valid bit for @tisci_msg_rm_ring_cfg_req addr_hi
 *	2 - Valid bit for @tisci_msg_rm_ring_cfg_req count
 *	3 - Valid bit for @tisci_msg_rm_ring_cfg_req mode
 *	4 - Valid bit for @tisci_msg_rm_ring_cfg_req size
 *	5 - Valid bit for @tisci_msg_rm_ring_cfg_req order_id
 *	6 - Valid bit for @tisci_msg_rm_ring_cfg_req virtid
 *	7 - Valid bit for @tisci_msg_rm_ring_cfg_req ASEL
 * @nav_id: Device ID of Navigator Subsystem from which the ring is allocated
 * @index: ring index to be configured.
 * @addr_lo: 32 LSBs of ring base address to be programmed into the ring's
 *	RING_BA_LO register
 * @addr_hi: 16 MSBs of ring base address to be programmed into the ring's
 *	RING_BA_HI register.
 * @count: Number of ring elements. Must be even if mode is CREDENTIALS or QM
 *	modes.
 * @mode: Specifies the mode the ring is to be configured.
 * @size: Specifies encoded ring element size. To calculate the encoded size use
 *	the formula (log2(size_bytes) - 2), where size_bytes cannot be
 *	greater than 256.
 * @order_id: Specifies the ring's bus order ID.
 * @virtid: Ring virt ID value
 * @asel: Ring ASEL (address select) value to be set into the ASEL field of the
 *	ring's RING_BA_HI register.
 */
struct ti_sci_msg_rm_ring_cfg_req {
	struct ti_sci_msg_hdr hdr;
	u32 valid_params;
	u16 nav_id;
	u16 index;
	u32 addr_lo;
	u32 addr_hi;
	u32 count;
	u8 mode;
	u8 size;
	u8 order_id;
	u16 virtid;
	u8 asel;
} __packed;

/**
 * struct ti_sci_msg_psil_pair - Pairs a PSI-L source thread to a destination
 *				 thread
 * @hdr:	Generic Header
 * @nav_id:	SoC Navigator Subsystem device ID whose PSI-L config proxy is
 *		used to pair the source and destination threads.
 * @src_thread:	PSI-L source thread ID within the PSI-L System thread map.
 *
 * UDMAP transmit channels mapped to source threads will have their
 * TCHAN_THRD_ID register programmed with the destination thread if the pairing
 * is successful.

 * @dst_thread: PSI-L destination thread ID within the PSI-L System thread map.
 * PSI-L destination threads start at index 0x8000.  The request is NACK'd if
 * the destination thread is not greater than or equal to 0x8000.
 *
 * UDMAP receive channels mapped to destination threads will have their
 * RCHAN_THRD_ID register programmed with the source thread if the pairing
 * is successful.
 *
 * Request type is TI_SCI_MSG_RM_PSIL_PAIR, response is a generic ACK or NACK
 * message.
 */
struct ti_sci_msg_psil_pair {
	struct ti_sci_msg_hdr hdr;
	u32 nav_id;
	u32 src_thread;
	u32 dst_thread;
} __packed;

/**
 * struct ti_sci_msg_psil_unpair - Unpairs a PSI-L source thread from a
 *				   destination thread
 * @hdr:	Generic Header
 * @nav_id:	SoC Navigator Subsystem device ID whose PSI-L config proxy is
 *		used to unpair the source and destination threads.
 * @src_thread:	PSI-L source thread ID within the PSI-L System thread map.
 *
 * UDMAP transmit channels mapped to source threads will have their
 * TCHAN_THRD_ID register cleared if the unpairing is successful.
 *
 * @dst_thread: PSI-L destination thread ID within the PSI-L System thread map.
 * PSI-L destination threads start at index 0x8000.  The request is NACK'd if
 * the destination thread is not greater than or equal to 0x8000.
 *
 * UDMAP receive channels mapped to destination threads will have their
 * RCHAN_THRD_ID register cleared if the unpairing is successful.
 *
 * Request type is TI_SCI_MSG_RM_PSIL_UNPAIR, response is a generic ACK or NACK
 * message.
 */
struct ti_sci_msg_psil_unpair {
	struct ti_sci_msg_hdr hdr;
	u32 nav_id;
	u32 src_thread;
	u32 dst_thread;
} __packed;

/**
 * struct ti_sci_msg_udmap_rx_flow_cfg -  UDMAP receive flow configuration
 *					  message
 * @hdr: Generic Header
 * @nav_id: SoC Navigator Subsystem device ID from which the receive flow is
 *	allocated
 * @flow_index: UDMAP receive flow index for non-optional configuration.
 * @rx_ch_index: Specifies the index of the receive channel using the flow_index
 * @rx_einfo_present: UDMAP receive flow extended packet info present.
 * @rx_psinfo_present: UDMAP receive flow PS words present.
 * @rx_error_handling: UDMAP receive flow error handling configuration. Valid
 *	values are TI_SCI_RM_UDMAP_RX_FLOW_ERR_DROP/RETRY.
 * @rx_desc_type: UDMAP receive flow descriptor type. It can be one of
 *	TI_SCI_RM_UDMAP_RX_FLOW_DESC_HOST/MONO.
 * @rx_sop_offset: UDMAP receive flow start of packet offset.
 * @rx_dest_qnum: UDMAP receive flow destination queue number.
 * @rx_ps_location: UDMAP receive flow PS words location.
 *	0 - end of packet descriptor
 *	1 - Beginning of the data buffer
 * @rx_src_tag_hi: UDMAP receive flow source tag high byte constant
 * @rx_src_tag_lo: UDMAP receive flow source tag low byte constant
 * @rx_dest_tag_hi: UDMAP receive flow destination tag high byte constant
 * @rx_dest_tag_lo: UDMAP receive flow destination tag low byte constant
 * @rx_src_tag_hi_sel: UDMAP receive flow source tag high byte selector
 * @rx_src_tag_lo_sel: UDMAP receive flow source tag low byte selector
 * @rx_dest_tag_hi_sel: UDMAP receive flow destination tag high byte selector
 * @rx_dest_tag_lo_sel: UDMAP receive flow destination tag low byte selector
 * @rx_size_thresh_en: UDMAP receive flow packet size based free buffer queue
 *	enable. If enabled, the ti_sci_rm_udmap_rx_flow_opt_cfg also need to be
 *	configured and sent.
 * @rx_fdq0_sz0_qnum: UDMAP receive flow free descriptor queue 0.
 * @rx_fdq1_qnum: UDMAP receive flow free descriptor queue 1.
 * @rx_fdq2_qnum: UDMAP receive flow free descriptor queue 2.
 * @rx_fdq3_qnum: UDMAP receive flow free descriptor queue 3.
 *
 * For detailed information on the settings, see the UDMAP section of the TRM.
 */
struct ti_sci_msg_udmap_rx_flow_cfg {
	struct ti_sci_msg_hdr hdr;
	u32 nav_id;
	u32 flow_index;
	u32 rx_ch_index;
	u8 rx_einfo_present;
	u8 rx_psinfo_present;
	u8 rx_error_handling;
	u8 rx_desc_type;
	u16 rx_sop_offset;
	u16 rx_dest_qnum;
	u8 rx_ps_location;
	u8 rx_src_tag_hi;
	u8 rx_src_tag_lo;
	u8 rx_dest_tag_hi;
	u8 rx_dest_tag_lo;
	u8 rx_src_tag_hi_sel;
	u8 rx_src_tag_lo_sel;
	u8 rx_dest_tag_hi_sel;
	u8 rx_dest_tag_lo_sel;
	u8 rx_size_thresh_en;
	u16 rx_fdq0_sz0_qnum;
	u16 rx_fdq1_qnum;
	u16 rx_fdq2_qnum;
	u16 rx_fdq3_qnum;
} __packed;

/**
 * struct rm_ti_sci_msg_udmap_rx_flow_opt_cfg - parameters for UDMAP receive
 *						flow optional configuration
 * @hdr: Generic Header
 * @nav_id: SoC Navigator Subsystem device ID from which the receive flow is
 *	allocated
 * @flow_index: UDMAP receive flow index for optional configuration.
 * @rx_ch_index: Specifies the index of the receive channel using the flow_index
 * @rx_size_thresh0: UDMAP receive flow packet size threshold 0.
 * @rx_size_thresh1: UDMAP receive flow packet size threshold 1.
 * @rx_size_thresh2: UDMAP receive flow packet size threshold 2.
 * @rx_fdq0_sz1_qnum: UDMAP receive flow free descriptor queue for size
 *	threshold 1.
 * @rx_fdq0_sz2_qnum: UDMAP receive flow free descriptor queue for size
 *	threshold 2.
 * @rx_fdq0_sz3_qnum: UDMAP receive flow free descriptor queue for size
 *	threshold 3.
 *
 * For detailed information on the settings, see the UDMAP section of the TRM.
 */
struct rm_ti_sci_msg_udmap_rx_flow_opt_cfg {
	struct ti_sci_msg_hdr hdr;
	u32 nav_id;
	u32 flow_index;
	u32 rx_ch_index;
	u16 rx_size_thresh0;
	u16 rx_size_thresh1;
	u16 rx_size_thresh2;
	u16 rx_fdq0_sz1_qnum;
	u16 rx_fdq0_sz2_qnum;
	u16 rx_fdq0_sz3_qnum;
} __packed;

/**
 * Configures a Navigator Subsystem UDMAP transmit channel
 *
 * Configures the non-real-time registers of a Navigator Subsystem UDMAP
 * transmit channel.  The channel index must be assigned to the host defined
 * in the TISCI header via the RM board configuration resource assignment
 * range list.
 *
 * @hdr: Generic Header
 *
 * @valid_params: Bitfield defining validity of tx channel configuration
 * parameters. The tx channel configuration fields are not valid, and will not
 * be used for ch configuration, if their corresponding valid bit is zero.
 * Valid bit usage:
 *    0 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_pause_on_err
 *    1 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_atype
 *    2 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_chan_type
 *    3 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_fetch_size
 *    4 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::txcq_qnum
 *    5 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_priority
 *    6 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_qos
 *    7 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_orderid
 *    8 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_sched_priority
 *    9 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_filt_einfo
 *   10 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_filt_pswords
 *   11 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_supr_tdpkt
 *   12 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_credit_count
 *   13 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::fdepth
 *   14 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_burst_size
 *   15 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::tx_tdtype
 *   16 - Valid bit for @ref ti_sci_msg_rm_udmap_tx_ch_cfg::extended_ch_type
 *
 * @nav_id: SoC device ID of Navigator Subsystem where tx channel is located
 *
 * @index: UDMAP transmit channel index.
 *
 * @tx_pause_on_err: UDMAP transmit channel pause on error configuration to
 * be programmed into the tx_pause_on_err field of the channel's TCHAN_TCFG
 * register.
 *
 * @tx_filt_einfo: UDMAP transmit channel extended packet information passing
 * configuration to be programmed into the tx_filt_einfo field of the
 * channel's TCHAN_TCFG register.
 *
 * @tx_filt_pswords: UDMAP transmit channel protocol specific word passing
 * configuration to be programmed into the tx_filt_pswords field of the
 * channel's TCHAN_TCFG register.
 *
 * @tx_atype: UDMAP transmit channel non Ring Accelerator access pointer
 * interpretation configuration to be programmed into the tx_atype field of
 * the channel's TCHAN_TCFG register.
 *
 * @tx_chan_type: UDMAP transmit channel functional channel type and work
 * passing mechanism configuration to be programmed into the tx_chan_type
 * field of the channel's TCHAN_TCFG register.
 *
 * @tx_supr_tdpkt: UDMAP transmit channel teardown packet generation suppression
 * configuration to be programmed into the tx_supr_tdpkt field of the channel's
 * TCHAN_TCFG register.
 *
 * @tx_fetch_size: UDMAP transmit channel number of 32-bit descriptor words to
 * fetch configuration to be programmed into the tx_fetch_size field of the
 * channel's TCHAN_TCFG register.  The user must make sure to set the maximum
 * word count that can pass through the channel for any allowed descriptor type.
 *
 * @tx_credit_count: UDMAP transmit channel transfer request credit count
 * configuration to be programmed into the count field of the TCHAN_TCREDIT
 * register.  Specifies how many credits for complete TRs are available.
 *
 * @txcq_qnum: UDMAP transmit channel completion queue configuration to be
 * programmed into the txcq_qnum field of the TCHAN_TCQ register. The specified
 * completion queue must be assigned to the host, or a subordinate of the host,
 * requesting configuration of the transmit channel.
 *
 * @tx_priority: UDMAP transmit channel transmit priority value to be programmed
 * into the priority field of the channel's TCHAN_TPRI_CTRL register.
 *
 * @tx_qos: UDMAP transmit channel transmit qos value to be programmed into the
 * qos field of the channel's TCHAN_TPRI_CTRL register.
 *
 * @tx_orderid: UDMAP transmit channel bus order id value to be programmed into
 * the orderid field of the channel's TCHAN_TPRI_CTRL register.
 *
 * @fdepth: UDMAP transmit channel FIFO depth configuration to be programmed
 * into the fdepth field of the TCHAN_TFIFO_DEPTH register. Sets the number of
 * Tx FIFO bytes which are allowed to be stored for the channel. Check the UDMAP
 * section of the TRM for restrictions regarding this parameter.
 *
 * @tx_sched_priority: UDMAP transmit channel tx scheduling priority
 * configuration to be programmed into the priority field of the channel's
 * TCHAN_TST_SCHED register.
 *
 * @tx_burst_size: UDMAP transmit channel burst size configuration to be
 * programmed into the tx_burst_size field of the TCHAN_TCFG register.
 *
 * @tx_tdtype: UDMAP transmit channel teardown type configuration to be
 * programmed into the tdtype field of the TCHAN_TCFG register:
 * 0 - Return immediately
 * 1 - Wait for completion message from remote peer
 *
 * @extended_ch_type: Valid for BCDMA.
 * 0 - the channel is split tx channel (tchan)
 * 1 - the channel is block copy channel (bchan)
 */
struct ti_sci_msg_rm_udmap_tx_ch_cfg_req {
	struct ti_sci_msg_hdr hdr;
	u32 valid_params;
	u16 nav_id;
	u16 index;
	u8 tx_pause_on_err;
	u8 tx_filt_einfo;
	u8 tx_filt_pswords;
	u8 tx_atype;
	u8 tx_chan_type;
	u8 tx_supr_tdpkt;
	u16 tx_fetch_size;
	u8 tx_credit_count;
	u16 txcq_qnum;
	u8 tx_priority;
	u8 tx_qos;
	u8 tx_orderid;
	u16 fdepth;
	u8 tx_sched_priority;
	u8 tx_burst_size;
	u8 tx_tdtype;
	u8 extended_ch_type;
} __packed;

/**
 * Configures a Navigator Subsystem UDMAP receive channel
 *
 * Configures the non-real-time registers of a Navigator Subsystem UDMAP
 * receive channel.  The channel index must be assigned to the host defined
 * in the TISCI header via the RM board configuration resource assignment
 * range list.
 *
 * @hdr: Generic Header
 *
 * @valid_params: Bitfield defining validity of rx channel configuration
 * parameters.
 * The rx channel configuration fields are not valid, and will not be used for
 * ch configuration, if their corresponding valid bit is zero.
 * Valid bit usage:
 *    0 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_pause_on_err
 *    1 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_atype
 *    2 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_chan_type
 *    3 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_fetch_size
 *    4 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rxcq_qnum
 *    5 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_priority
 *    6 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_qos
 *    7 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_orderid
 *    8 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_sched_priority
 *    9 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::flowid_start
 *   10 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::flowid_cnt
 *   11 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_ignore_short
 *   12 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_ignore_long
 *   14 - Valid bit for @ti_sci_msg_rm_udmap_rx_ch_cfg_req::rx_burst_size
 *
 * @nav_id: SoC device ID of Navigator Subsystem where rx channel is located
 *
 * @index: UDMAP receive channel index.
 *
 * @rx_fetch_size: UDMAP receive channel number of 32-bit descriptor words to
 * fetch configuration to be programmed into the rx_fetch_size field of the
 * channel's RCHAN_RCFG register.
 *
 * @rxcq_qnum: UDMAP receive channel completion queue configuration to be
 * programmed into the rxcq_qnum field of the RCHAN_RCQ register.
 * The specified completion queue must be assigned to the host, or a subordinate
 * of the host, requesting configuration of the receive channel.
 *
 * @rx_priority: UDMAP receive channel receive priority value to be programmed
 * into the priority field of the channel's RCHAN_RPRI_CTRL register.
 *
 * @rx_qos: UDMAP receive channel receive qos value to be programmed into the
 * qos field of the channel's RCHAN_RPRI_CTRL register.
 *
 * @rx_orderid: UDMAP receive channel bus order id value to be programmed into
 * the orderid field of the channel's RCHAN_RPRI_CTRL register.
 *
 * @rx_sched_priority: UDMAP receive channel rx scheduling priority
 * configuration to be programmed into the priority field of the channel's
 * RCHAN_RST_SCHED register.
 *
 * @flowid_start: UDMAP receive channel additional flows starting index
 * configuration to program into the flow_start field of the RCHAN_RFLOW_RNG
 * register. Specifies the starting index for flow IDs the receive channel is to
 * make use of beyond the default flow. flowid_start and @ref flowid_cnt must be
 * set as valid and configured together. The starting flow ID set by
 * @ref flowid_cnt must be a flow index within the Navigator Subsystem's subset
 * of flows beyond the default flows statically mapped to receive channels.
 * The additional flows must be assigned to the host, or a subordinate of the
 * host, requesting configuration of the receive channel.
 *
 * @flowid_cnt: UDMAP receive channel additional flows count configuration to
 * program into the flowid_cnt field of the RCHAN_RFLOW_RNG register.
 * This field specifies how many flow IDs are in the additional contiguous range
 * of legal flow IDs for the channel.  @ref flowid_start and flowid_cnt must be
 * set as valid and configured together. Disabling the valid_params field bit
 * for flowid_cnt indicates no flow IDs other than the default are to be
 * allocated and used by the receive channel. @ref flowid_start plus flowid_cnt
 * cannot be greater than the number of receive flows in the receive channel's
 * Navigator Subsystem.  The additional flows must be assigned to the host, or a
 * subordinate of the host, requesting configuration of the receive channel.
 *
 * @rx_pause_on_err: UDMAP receive channel pause on error configuration to be
 * programmed into the rx_pause_on_err field of the channel's RCHAN_RCFG
 * register.
 *
 * @rx_atype: UDMAP receive channel non Ring Accelerator access pointer
 * interpretation configuration to be programmed into the rx_atype field of the
 * channel's RCHAN_RCFG register.
 *
 * @rx_chan_type: UDMAP receive channel functional channel type and work passing
 * mechanism configuration to be programmed into the rx_chan_type field of the
 * channel's RCHAN_RCFG register.
 *
 * @rx_ignore_short: UDMAP receive channel short packet treatment configuration
 * to be programmed into the rx_ignore_short field of the RCHAN_RCFG register.
 *
 * @rx_ignore_long: UDMAP receive channel long packet treatment configuration to
 * be programmed into the rx_ignore_long field of the RCHAN_RCFG register.
 *
 * @rx_burst_size: UDMAP receive channel burst size configuration to be
 * programmed into the rx_burst_size field of the RCHAN_RCFG register.
 */
struct ti_sci_msg_rm_udmap_rx_ch_cfg_req {
	struct ti_sci_msg_hdr hdr;
	u32 valid_params;
	u16 nav_id;
	u16 index;
	u16 rx_fetch_size;
	u16 rxcq_qnum;
	u8 rx_priority;
	u8 rx_qos;
	u8 rx_orderid;
	u8 rx_sched_priority;
	u16 flowid_start;
	u16 flowid_cnt;
	u8 rx_pause_on_err;
	u8 rx_atype;
	u8 rx_chan_type;
	u8 rx_ignore_short;
	u8 rx_ignore_long;
	u8 rx_burst_size;
} __packed;

/**
 * Configures a Navigator Subsystem UDMAP receive flow
 *
 * Configures a Navigator Subsystem UDMAP receive flow's registers.
 * Configuration does not include the flow registers which handle size-based
 * free descriptor queue routing.
 *
 * The flow index must be assigned to the host defined in the TISCI header via
 * the RM board configuration resource assignment range list.
 *
 * @hdr: Standard TISCI header
 *
 * @valid_params
 * Bitfield defining validity of rx flow configuration parameters.  The
 * rx flow configuration fields are not valid, and will not be used for flow
 * configuration, if their corresponding valid bit is zero.  Valid bit usage:
 *     0 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_einfo_present
 *     1 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_psinfo_present
 *     2 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_error_handling
 *     3 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_desc_type
 *     4 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_sop_offset
 *     5 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_qnum
 *     6 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_src_tag_hi
 *     7 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_src_tag_lo
 *     8 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_tag_hi
 *     9 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_tag_lo
 *    10 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_src_tag_hi_sel
 *    11 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_src_tag_lo_sel
 *    12 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_tag_hi_sel
 *    13 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_dest_tag_lo_sel
 *    14 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_fdq0_sz0_qnum
 *    15 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_fdq1_sz0_qnum
 *    16 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_fdq2_sz0_qnum
 *    17 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_fdq3_sz0_qnum
 *    18 - Valid bit for @tisci_msg_rm_udmap_flow_cfg_req::rx_ps_location
 *
 * @nav_id: SoC device ID of Navigator Subsystem from which the receive flow is
 * allocated
 *
 * @flow_index: UDMAP receive flow index for non-optional configuration.
 *
 * @rx_einfo_present:
 * UDMAP receive flow extended packet info present configuration to be
 * programmed into the rx_einfo_present field of the flow's RFLOW_RFA register.
 *
 * @rx_psinfo_present:
 * UDMAP receive flow PS words present configuration to be programmed into the
 * rx_psinfo_present field of the flow's RFLOW_RFA register.
 *
 * @rx_error_handling:
 * UDMAP receive flow error handling configuration to be programmed into the
 * rx_error_handling field of the flow's RFLOW_RFA register.
 *
 * @rx_desc_type:
 * UDMAP receive flow descriptor type configuration to be programmed into the
 * rx_desc_type field field of the flow's RFLOW_RFA register.
 *
 * @rx_sop_offset:
 * UDMAP receive flow start of packet offset configuration to be programmed
 * into the rx_sop_offset field of the RFLOW_RFA register.  See the UDMAP
 * section of the TRM for more information on this setting.  Valid values for
 * this field are 0-255 bytes.
 *
 * @rx_dest_qnum:
 * UDMAP receive flow destination queue configuration to be programmed into the
 * rx_dest_qnum field of the flow's RFLOW_RFA register.  The specified
 * destination queue must be valid within the Navigator Subsystem and must be
 * owned by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_src_tag_hi:
 * UDMAP receive flow source tag high byte constant configuration to be
 * programmed into the rx_src_tag_hi field of the flow's RFLOW_RFB register.
 * See the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_src_tag_lo:
 * UDMAP receive flow source tag low byte constant configuration to be
 * programmed into the rx_src_tag_lo field of the flow's RFLOW_RFB register.
 * See the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_dest_tag_hi:
 * UDMAP receive flow destination tag high byte constant configuration to be
 * programmed into the rx_dest_tag_hi field of the flow's RFLOW_RFB register.
 * See the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_dest_tag_lo:
 * UDMAP receive flow destination tag low byte constant configuration to be
 * programmed into the rx_dest_tag_lo field of the flow's RFLOW_RFB register.
 * See the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_src_tag_hi_sel:
 * UDMAP receive flow source tag high byte selector configuration to be
 * programmed into the rx_src_tag_hi_sel field of the RFLOW_RFC register.  See
 * the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_src_tag_lo_sel:
 * UDMAP receive flow source tag low byte selector configuration to be
 * programmed into the rx_src_tag_lo_sel field of the RFLOW_RFC register.  See
 * the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_dest_tag_hi_sel:
 * UDMAP receive flow destination tag high byte selector configuration to be
 * programmed into the rx_dest_tag_hi_sel field of the RFLOW_RFC register.  See
 * the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_dest_tag_lo_sel:
 * UDMAP receive flow destination tag low byte selector configuration to be
 * programmed into the rx_dest_tag_lo_sel field of the RFLOW_RFC register.  See
 * the UDMAP section of the TRM for more information on this setting.
 *
 * @rx_fdq0_sz0_qnum:
 * UDMAP receive flow free descriptor queue 0 configuration to be programmed
 * into the rx_fdq0_sz0_qnum field of the flow's RFLOW_RFD register.  See the
 * UDMAP section of the TRM for more information on this setting. The specified
 * free queue must be valid within the Navigator Subsystem and must be owned
 * by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_fdq1_qnum:
 * UDMAP receive flow free descriptor queue 1 configuration to be programmed
 * into the rx_fdq1_qnum field of the flow's RFLOW_RFD register.  See the
 * UDMAP section of the TRM for more information on this setting.  The specified
 * free queue must be valid within the Navigator Subsystem and must be owned
 * by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_fdq2_qnum:
 * UDMAP receive flow free descriptor queue 2 configuration to be programmed
 * into the rx_fdq2_qnum field of the flow's RFLOW_RFE register.  See the
 * UDMAP section of the TRM for more information on this setting.  The specified
 * free queue must be valid within the Navigator Subsystem and must be owned
 * by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_fdq3_qnum:
 * UDMAP receive flow free descriptor queue 3 configuration to be programmed
 * into the rx_fdq3_qnum field of the flow's RFLOW_RFE register.  See the
 * UDMAP section of the TRM for more information on this setting.  The specified
 * free queue must be valid within the Navigator Subsystem and must be owned
 * by the host, or a subordinate of the host, requesting allocation and
 * configuration of the receive flow.
 *
 * @rx_ps_location:
 * UDMAP receive flow PS words location configuration to be programmed into the
 * rx_ps_location field of the flow's RFLOW_RFA register.
 */
struct ti_sci_msg_rm_udmap_flow_cfg_req {
	struct ti_sci_msg_hdr hdr;
	u32 valid_params;
	u16 nav_id;
	u16 flow_index;
	u8 rx_einfo_present;
	u8 rx_psinfo_present;
	u8 rx_error_handling;
	u8 rx_desc_type;
	u16 rx_sop_offset;
	u16 rx_dest_qnum;
	u8 rx_src_tag_hi;
	u8 rx_src_tag_lo;
	u8 rx_dest_tag_hi;
	u8 rx_dest_tag_lo;
	u8 rx_src_tag_hi_sel;
	u8 rx_src_tag_lo_sel;
	u8 rx_dest_tag_hi_sel;
	u8 rx_dest_tag_lo_sel;
	u16 rx_fdq0_sz0_qnum;
	u16 rx_fdq1_qnum;
	u16 rx_fdq2_qnum;
	u16 rx_fdq3_qnum;
	u8 rx_ps_location;
} __packed;

/**
 * struct ti_sci_msg_req_proc_request - Request a processor
 * @hdr:		Generic Header
 * @processor_id:	ID of processor being requested
 *
 * Request type is TI_SCI_MSG_PROC_REQUEST, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_proc_request {
	struct ti_sci_msg_hdr hdr;
	u8 processor_id;
} __packed;

/**
 * struct ti_sci_msg_req_proc_release - Release a processor
 * @hdr:		Generic Header
 * @processor_id:	ID of processor being released
 *
 * Request type is TI_SCI_MSG_PROC_RELEASE, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_proc_release {
	struct ti_sci_msg_hdr hdr;
	u8 processor_id;
} __packed;

/**
 * struct ti_sci_msg_req_proc_handover - Handover a processor to a host
 * @hdr:		Generic Header
 * @processor_id:	ID of processor being handed over
 * @host_id:		Host ID the control needs to be transferred to
 *
 * Request type is TI_SCI_MSG_PROC_HANDOVER, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_proc_handover {
	struct ti_sci_msg_hdr hdr;
	u8 processor_id;
	u8 host_id;
} __packed;

/* Boot Vector masks */
#define TI_SCI_ADDR_LOW_MASK			GENMASK_ULL(31, 0)
#define TI_SCI_ADDR_HIGH_MASK			GENMASK_ULL(63, 32)
#define TI_SCI_ADDR_HIGH_SHIFT			32

/**
 * struct ti_sci_msg_req_set_config - Set Processor boot configuration
 * @hdr:		Generic Header
 * @processor_id:	ID of processor being configured
 * @bootvector_low:	Lower 32 bit address (Little Endian) of boot vector
 * @bootvector_high:	Higher 32 bit address (Little Endian) of boot vector
 * @config_flags_set:	Optional Processor specific Config Flags to set.
 *			Setting a bit here implies the corresponding mode
 *			will be set
 * @config_flags_clear:	Optional Processor specific Config Flags to clear.
 *			Setting a bit here implies the corresponding mode
 *			will be cleared
 *
 * Request type is TI_SCI_MSG_PROC_HANDOVER, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_set_config {
	struct ti_sci_msg_hdr hdr;
	u8 processor_id;
	u32 bootvector_low;
	u32 bootvector_high;
	u32 config_flags_set;
	u32 config_flags_clear;
} __packed;

/**
 * struct ti_sci_msg_req_set_ctrl - Set Processor boot control flags
 * @hdr:		Generic Header
 * @processor_id:	ID of processor being configured
 * @control_flags_set:	Optional Processor specific Control Flags to set.
 *			Setting a bit here implies the corresponding mode
 *			will be set
 * @control_flags_clear:Optional Processor specific Control Flags to clear.
 *			Setting a bit here implies the corresponding mode
 *			will be cleared
 *
 * Request type is TI_SCI_MSG_SET_CTRL, response is a generic ACK/NACK
 * message.
 */
struct ti_sci_msg_req_set_ctrl {
	struct ti_sci_msg_hdr hdr;
	u8 processor_id;
	u32 control_flags_set;
	u32 control_flags_clear;
} __packed;

/**
 * struct ti_sci_msg_req_get_status - Processor boot status request
 * @hdr:		Generic Header
 * @processor_id:	ID of processor whose status is being requested
 *
 * Request type is TI_SCI_MSG_GET_STATUS, response is an appropriate
 * message, or NACK in case of inability to satisfy request.
 */
struct ti_sci_msg_req_get_status {
	struct ti_sci_msg_hdr hdr;
	u8 processor_id;
} __packed;

/**
 * struct ti_sci_msg_resp_get_status - Processor boot status response
 * @hdr:		Generic Header
 * @processor_id:	ID of processor whose status is returned
 * @bootvector_low:	Lower 32 bit address (Little Endian) of boot vector
 * @bootvector_high:	Higher 32 bit address (Little Endian) of boot vector
 * @config_flags:	Optional Processor specific Config Flags set currently
 * @control_flags:	Optional Processor specific Control Flags set currently
 * @status_flags:	Optional Processor specific Status Flags set currently
 *
 * Response structure to a TI_SCI_MSG_GET_STATUS request.
 */
struct ti_sci_msg_resp_get_status {
	struct ti_sci_msg_hdr hdr;
	u8 processor_id;
	u32 bootvector_low;
	u32 bootvector_high;
	u32 config_flags;
	u32 control_flags;
	u32 status_flags;
} __packed;

#endif /* __TI_SCI_H */
