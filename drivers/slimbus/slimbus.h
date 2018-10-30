// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2017, The Linux Foundation
 */

#ifndef _DRIVERS_SLIMBUS_H
#define _DRIVERS_SLIMBUS_H
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/slimbus.h>

/* Standard values per SLIMbus spec needed by controllers and devices */
#define SLIM_CL_PER_SUPERFRAME		6144
#define SLIM_CL_PER_SUPERFRAME_DIV8	(SLIM_CL_PER_SUPERFRAME >> 3)

/* SLIMbus message types. Related to interpretation of message code. */
#define SLIM_MSG_MT_CORE			0x0
#define SLIM_MSG_MT_DEST_REFERRED_USER		0x2
#define SLIM_MSG_MT_SRC_REFERRED_USER		0x6

/*
 * SLIM Broadcast header format
 * BYTE 0: MT[7:5] RL[4:0]
 * BYTE 1: RSVD[7] MC[6:0]
 * BYTE 2: RSVD[7:6] DT[5:4] PI[3:0]
 */
#define SLIM_MSG_MT_MASK	GENMASK(2, 0)
#define SLIM_MSG_MT_SHIFT	5
#define SLIM_MSG_RL_MASK	GENMASK(4, 0)
#define SLIM_MSG_RL_SHIFT	0
#define SLIM_MSG_MC_MASK	GENMASK(6, 0)
#define SLIM_MSG_MC_SHIFT	0
#define SLIM_MSG_DT_MASK	GENMASK(1, 0)
#define SLIM_MSG_DT_SHIFT	4

#define SLIM_HEADER_GET_MT(b)	((b >> SLIM_MSG_MT_SHIFT) & SLIM_MSG_MT_MASK)
#define SLIM_HEADER_GET_RL(b)	((b >> SLIM_MSG_RL_SHIFT) & SLIM_MSG_RL_MASK)
#define SLIM_HEADER_GET_MC(b)	((b >> SLIM_MSG_MC_SHIFT) & SLIM_MSG_MC_MASK)
#define SLIM_HEADER_GET_DT(b)	((b >> SLIM_MSG_DT_SHIFT) & SLIM_MSG_DT_MASK)

/* Device management messages used by this framework */
#define SLIM_MSG_MC_REPORT_PRESENT               0x1
#define SLIM_MSG_MC_ASSIGN_LOGICAL_ADDRESS       0x2
#define SLIM_MSG_MC_REPORT_ABSENT                0xF

/* Data channel management messages */
#define SLIM_MSG_MC_CONNECT_SOURCE		0x10
#define SLIM_MSG_MC_CONNECT_SINK		0x11
#define SLIM_MSG_MC_DISCONNECT_PORT		0x14
#define SLIM_MSG_MC_CHANGE_CONTENT		0x18

/* Clock pause Reconfiguration messages */
#define SLIM_MSG_MC_BEGIN_RECONFIGURATION        0x40
#define SLIM_MSG_MC_NEXT_PAUSE_CLOCK             0x4A
#define SLIM_MSG_MC_NEXT_DEFINE_CHANNEL          0x50
#define SLIM_MSG_MC_NEXT_DEFINE_CONTENT          0x51
#define SLIM_MSG_MC_NEXT_ACTIVATE_CHANNEL        0x54
#define SLIM_MSG_MC_NEXT_DEACTIVATE_CHANNEL      0x55
#define SLIM_MSG_MC_NEXT_REMOVE_CHANNEL          0x58
#define SLIM_MSG_MC_RECONFIGURE_NOW              0x5F

/*
 * Clock pause flag to indicate that the reconfig message
 * corresponds to clock pause sequence
 */
#define SLIM_MSG_CLK_PAUSE_SEQ_FLG		(1U << 8)

/* Clock pause values per SLIMbus spec */
#define SLIM_CLK_FAST				0
#define SLIM_CLK_CONST_PHASE			1
#define SLIM_CLK_UNSPECIFIED			2

/* Destination type Values */
#define SLIM_MSG_DEST_LOGICALADDR	0
#define SLIM_MSG_DEST_ENUMADDR		1
#define	SLIM_MSG_DEST_BROADCAST		3

/* Standard values per SLIMbus spec needed by controllers and devices */
#define SLIM_MAX_CLK_GEAR		10
#define SLIM_MIN_CLK_GEAR		1
#define SLIM_SLOT_LEN_BITS		4

/* Indicate that the frequency of the flow and the bus frequency are locked */
#define SLIM_CHANNEL_CONTENT_FL		BIT(7)

/* Standard values per SLIMbus spec needed by controllers and devices */
#define SLIM_CL_PER_SUPERFRAME		6144
#define SLIM_SLOTS_PER_SUPERFRAME	(SLIM_CL_PER_SUPERFRAME >> 2)
#define SLIM_SL_PER_SUPERFRAME		(SLIM_CL_PER_SUPERFRAME >> 2)
/* Manager's logical address is set to 0xFF per spec */
#define SLIM_LA_MANAGER 0xFF

#define SLIM_MAX_TIDS			256
/**
 * struct slim_framer - Represents SLIMbus framer.
 * Every controller may have multiple framers. There is 1 active framer device
 * responsible for clocking the bus.
 * Manager is responsible for framer hand-over.
 * @dev: Driver model representation of the device.
 * @e_addr: Enumeration address of the framer.
 * @rootfreq: Root Frequency at which the framer can run. This is maximum
 *	frequency ('clock gear 10') at which the bus can operate.
 * @superfreq: Superframes per root frequency. Every frame is 6144 bits.
 */
struct slim_framer {
	struct device		dev;
	struct slim_eaddr	e_addr;
	int			rootfreq;
	int			superfreq;
};

#define to_slim_framer(d) container_of(d, struct slim_framer, dev)

/**
 * struct slim_msg_txn - Message to be sent by the controller.
 *			This structure has packet header,
 *			payload and buffer to be filled (if any)
 * @rl: Header field. remaining length.
 * @mt: Header field. Message type.
 * @mc: Header field. LSB is message code for type mt.
 * @dt: Header field. Destination type.
 * @ec: Element code. Used for elemental access APIs.
 * @tid: Transaction ID. Used for messages expecting response.
 *	(relevant for message-codes involving read operation)
 * @la: Logical address of the device this message is going to.
 *	(Not used when destination type is broadcast.)
 * @msg: Elemental access message to be read/written
 * @comp: completion if read/write is synchronous, used internally
 *	for tid based transactions.
 */
struct slim_msg_txn {
	u8			rl;
	u8			mt;
	u8			mc;
	u8			dt;
	u16			ec;
	u8			tid;
	u8			la;
	struct slim_val_inf	*msg;
	struct	completion	*comp;
};

/* Frequently used message transaction structures */
#define DEFINE_SLIM_LDEST_TXN(name, mc, rl, la, msg) \
	struct slim_msg_txn name = { rl, 0, mc, SLIM_MSG_DEST_LOGICALADDR, 0,\
					0, la, msg, }

#define DEFINE_SLIM_BCAST_TXN(name, mc, rl, la, msg) \
	struct slim_msg_txn name = { rl, 0, mc, SLIM_MSG_DEST_BROADCAST, 0,\
					0, la, msg, }

#define DEFINE_SLIM_EDEST_TXN(name, mc, rl, la, msg) \
	struct slim_msg_txn name = { rl, 0, mc, SLIM_MSG_DEST_ENUMADDR, 0,\
					0, la, msg, }
/**
 * enum slim_clk_state: SLIMbus controller's clock state used internally for
 *	maintaining current clock state.
 * @SLIM_CLK_ACTIVE: SLIMbus clock is active
 * @SLIM_CLK_ENTERING_PAUSE: SLIMbus clock pause sequence is being sent on the
 *	bus. If this succeeds, state changes to SLIM_CLK_PAUSED. If the
 *	transition fails, state changes back to SLIM_CLK_ACTIVE
 * @SLIM_CLK_PAUSED: SLIMbus controller clock has paused.
 */
enum slim_clk_state {
	SLIM_CLK_ACTIVE,
	SLIM_CLK_ENTERING_PAUSE,
	SLIM_CLK_PAUSED,
};

/**
 * struct slim_sched: Framework uses this structure internally for scheduling.
 * @clk_state: Controller's clock state from enum slim_clk_state
 * @pause_comp: Signals completion of clock pause sequence. This is useful when
 *	client tries to call SLIMbus transaction when controller is entering
 *	clock pause.
 * @m_reconf: This mutex is held until current reconfiguration (data channel
 *	scheduling, message bandwidth reservation) is done. Message APIs can
 *	use the bus concurrently when this mutex is held since elemental access
 *	messages can be sent on the bus when reconfiguration is in progress.
 */
struct slim_sched {
	enum slim_clk_state	clk_state;
	struct completion	pause_comp;
	struct mutex		m_reconf;
};

/**
 * enum slim_port_direction: SLIMbus port direction
 *
 * @SLIM_PORT_SINK: SLIMbus port is a sink
 * @SLIM_PORT_SOURCE: SLIMbus port is a source
 */
enum slim_port_direction {
	SLIM_PORT_SINK = 0,
	SLIM_PORT_SOURCE,
};
/**
 * enum slim_port_state: SLIMbus Port/Endpoint state machine
 *	according to SLIMbus Spec 2.0
 * @SLIM_PORT_DISCONNECTED: SLIMbus port is disconnected
 *	entered from Unconfigure/configured state after
 *	DISCONNECT_PORT or REMOVE_CHANNEL core command
 * @SLIM_PORT_UNCONFIGURED: SLIMbus port is in unconfigured state.
 *	entered from disconnect state after CONNECT_SOURCE/SINK core command
 * @SLIM_PORT_CONFIGURED: SLIMbus port is in configured state.
 *	entered from unconfigured state after DEFINE_CHANNEL, DEFINE_CONTENT
 *	and ACTIVATE_CHANNEL core commands. Ready for data transmission.
 */
enum slim_port_state {
	SLIM_PORT_DISCONNECTED = 0,
	SLIM_PORT_UNCONFIGURED,
	SLIM_PORT_CONFIGURED,
};

/**
 * enum slim_channel_state: SLIMbus channel state machine used by core.
 * @SLIM_CH_STATE_DISCONNECTED: SLIMbus channel is disconnected
 * @SLIM_CH_STATE_ALLOCATED: SLIMbus channel is allocated
 * @SLIM_CH_STATE_ASSOCIATED: SLIMbus channel is associated with port
 * @SLIM_CH_STATE_DEFINED: SLIMbus channel parameters are defined
 * @SLIM_CH_STATE_CONTENT_DEFINED: SLIMbus channel content is defined
 * @SLIM_CH_STATE_ACTIVE: SLIMbus channel is active and ready for data
 * @SLIM_CH_STATE_REMOVED: SLIMbus channel is inactive and removed
 */
enum slim_channel_state {
	SLIM_CH_STATE_DISCONNECTED = 0,
	SLIM_CH_STATE_ALLOCATED,
	SLIM_CH_STATE_ASSOCIATED,
	SLIM_CH_STATE_DEFINED,
	SLIM_CH_STATE_CONTENT_DEFINED,
	SLIM_CH_STATE_ACTIVE,
	SLIM_CH_STATE_REMOVED,
};

/**
 * enum slim_ch_data_fmt: SLIMbus channel data Type identifiers according to
 *	Table 60 of SLIMbus Spec 1.01.01
 * @SLIM_CH_DATA_FMT_NOT_DEFINED: Undefined
 * @SLIM_CH_DATA_FMT_LPCM_AUDIO: LPCM audio
 * @SLIM_CH_DATA_FMT_IEC61937_COMP_AUDIO: IEC61937 Compressed audio
 * @SLIM_CH_DATA_FMT_PACKED_PDM_AUDIO: Packed PDM audio
 */
enum slim_ch_data_fmt {
	SLIM_CH_DATA_FMT_NOT_DEFINED = 0,
	SLIM_CH_DATA_FMT_LPCM_AUDIO = 1,
	SLIM_CH_DATA_FMT_IEC61937_COMP_AUDIO = 2,
	SLIM_CH_DATA_FMT_PACKED_PDM_AUDIO = 3,
};

/**
 * enum slim_ch_aux_fmt: SLIMbus channel Aux Field format IDs according to
 *	Table 63 of SLIMbus Spec 2.0
 * @SLIM_CH_AUX_FMT_NOT_APPLICABLE: Undefined
 * @SLIM_CH_AUX_FMT_ZCUV_TUNNEL_IEC60958: ZCUV for tunneling IEC60958
 * @SLIM_CH_AUX_FMT_USER_DEFINED: User defined
 */
enum slim_ch_aux_bit_fmt {
	SLIM_CH_AUX_FMT_NOT_APPLICABLE = 0,
	SLIM_CH_AUX_FMT_ZCUV_TUNNEL_IEC60958 = 1,
	SLIM_CH_AUX_FMT_USER_DEFINED = 0xF,
};

/**
 * struct slim_channel  - SLIMbus channel, used for state machine
 *
 * @id: ID of channel
 * @prrate: Presense rate of channel from Table 66 of SLIMbus 2.0 Specs
 * @seg_dist: segment distribution code from Table 20 of SLIMbus 2.0 Specs
 * @data_fmt: Data format of channel.
 * @aux_fmt: Aux format for this channel.
 * @state: channel state machine
 */
struct slim_channel {
	int id;
	int prrate;
	int seg_dist;
	enum slim_ch_data_fmt data_fmt;
	enum slim_ch_aux_bit_fmt aux_fmt;
	enum slim_channel_state state;
};

/**
 * struct slim_port  - SLIMbus port
 *
 * @id: Port id
 * @direction: Port direction, Source or Sink.
 * @state: state machine of port.
 * @ch: channel associated with this port.
 */
struct slim_port {
	int id;
	enum slim_port_direction direction;
	enum slim_port_state state;
	struct slim_channel ch;
};

/**
 * enum slim_transport_protocol: SLIMbus Transport protocol list from
 *	Table 47 of SLIMbus 2.0 specs.
 * @SLIM_PROTO_ISO: Isochronous Protocol, no flow control as data rate match
 *		channel rate flow control embedded in the data.
 * @SLIM_PROTO_PUSH: Pushed Protocol, includes flow control, Used to carry
 *		data whose rate	is equal to, or lower than the channel rate.
 * @SLIM_PROTO_PULL: Pulled Protocol, similar usage as pushed protocol
 *		but pull is a unicast.
 * @SLIM_PROTO_LOCKED: Locked Protocol
 * @SLIM_PROTO_ASYNC_SMPLX: Asynchronous Protocol-Simplex
 * @SLIM_PROTO_ASYNC_HALF_DUP: Asynchronous Protocol-Half-duplex
 * @SLIM_PROTO_EXT_SMPLX: Extended Asynchronous Protocol-Simplex
 * @SLIM_PROTO_EXT_HALF_DUP: Extended Asynchronous Protocol-Half-duplex
 */
enum slim_transport_protocol {
	SLIM_PROTO_ISO = 0,
	SLIM_PROTO_PUSH,
	SLIM_PROTO_PULL,
	SLIM_PROTO_LOCKED,
	SLIM_PROTO_ASYNC_SMPLX,
	SLIM_PROTO_ASYNC_HALF_DUP,
	SLIM_PROTO_EXT_SMPLX,
	SLIM_PROTO_EXT_HALF_DUP,
};

/**
 * struct slim_stream_runtime  - SLIMbus stream runtime instance
 *
 * @name: Name of the stream
 * @dev: SLIM Device instance associated with this stream
 * @direction: direction of stream
 * @prot: Transport protocol used in this stream
 * @rate: Data rate of samples *
 * @bps: bits per sample
 * @ratem: rate multipler which is super frame rate/data rate
 * @num_ports: number of ports
 * @ports: pointer to instance of ports
 * @node: list head for stream associated with slim device.
 */
struct slim_stream_runtime {
	const char *name;
	struct slim_device *dev;
	int direction;
	enum slim_transport_protocol prot;
	unsigned int rate;
	unsigned int bps;
	unsigned int ratem;
	int num_ports;
	struct slim_port *ports;
	struct list_head node;
};

/**
 * struct slim_controller  - Controls every instance of SLIMbus
 *				(similar to 'master' on SPI)
 * @dev: Device interface to this driver
 * @id: Board-specific number identifier for this controller/bus
 * @name: Name for this controller
 * @min_cg: Minimum clock gear supported by this controller (default value: 1)
 * @max_cg: Maximum clock gear supported by this controller (default value: 10)
 * @clkgear: Current clock gear in which this bus is running
 * @laddr_ida: logical address id allocator
 * @a_framer: Active framer which is clocking the bus managed by this controller
 * @lock: Mutex protecting controller data structures
 * @devices: Slim device list
 * @tid_idr: tid id allocator
 * @txn_lock: Lock to protect table of transactions
 * @sched: scheduler structure used by the controller
 * @xfer_msg: Transfer a message on this controller (this can be a broadcast
 *	control/status message like data channel setup, or a unicast message
 *	like value element read/write.
 * @set_laddr: Setup logical address at laddr for the slave with elemental
 *	address e_addr. Drivers implementing controller will be expected to
 *	send unicast message to this device with its logical address.
 * @get_laddr: It is possible that controller needs to set fixed logical
 *	address table and get_laddr can be used in that case so that controller
 *	can do this assignment. Use case is when the master is on the remote
 *	processor side, who is resposible for allocating laddr.
 * @wakeup: This function pointer implements controller-specific procedure
 *	to wake it up from clock-pause. Framework will call this to bring
 *	the controller out of clock pause.
 * @enable_stream: This function pointer implements controller-specific procedure
 *	to enable a stream.
 * @disable_stream: This function pointer implements controller-specific procedure
 *	to disable stream.
 *
 *	'Manager device' is responsible for  device management, bandwidth
 *	allocation, channel setup, and port associations per channel.
 *	Device management means Logical address assignment/removal based on
 *	enumeration (report-present, report-absent) of a device.
 *	Bandwidth allocation is done dynamically by the manager based on active
 *	channels on the bus, message-bandwidth requests made by SLIMbus devices.
 *	Based on current bandwidth usage, manager chooses a frequency to run
 *	the bus at (in steps of 'clock-gear', 1 through 10, each clock gear
 *	representing twice the frequency than the previous gear).
 *	Manager is also responsible for entering (and exiting) low-power-mode
 *	(known as 'clock pause').
 *	Manager can do handover of framer if there are multiple framers on the
 *	bus and a certain usecase warrants using certain framer to avoid keeping
 *	previous framer being powered-on.
 *
 *	Controller here performs duties of the manager device, and 'interface
 *	device'. Interface device is responsible for monitoring the bus and
 *	reporting information such as loss-of-synchronization, data
 *	slot-collision.
 */
struct slim_controller {
	struct device		*dev;
	unsigned int		id;
	char			name[SLIMBUS_NAME_SIZE];
	int			min_cg;
	int			max_cg;
	int			clkgear;
	struct ida		laddr_ida;
	struct slim_framer	*a_framer;
	struct mutex		lock;
	struct list_head	devices;
	struct idr		tid_idr;
	spinlock_t		txn_lock;
	struct slim_sched	sched;
	int			(*xfer_msg)(struct slim_controller *ctrl,
					    struct slim_msg_txn *tx);
	int			(*set_laddr)(struct slim_controller *ctrl,
					     struct slim_eaddr *ea, u8 laddr);
	int			(*get_laddr)(struct slim_controller *ctrl,
					     struct slim_eaddr *ea, u8 *laddr);
	int		(*enable_stream)(struct slim_stream_runtime *rt);
	int		(*disable_stream)(struct slim_stream_runtime *rt);
	int			(*wakeup)(struct slim_controller *ctrl);
};

int slim_device_report_present(struct slim_controller *ctrl,
			       struct slim_eaddr *e_addr, u8 *laddr);
void slim_report_absent(struct slim_device *sbdev);
int slim_register_controller(struct slim_controller *ctrl);
int slim_unregister_controller(struct slim_controller *ctrl);
void slim_msg_response(struct slim_controller *ctrl, u8 *reply, u8 tid, u8 l);
int slim_do_transfer(struct slim_controller *ctrl, struct slim_msg_txn *txn);
int slim_ctrl_clk_pause(struct slim_controller *ctrl, bool wakeup, u8 restart);
int slim_alloc_txn_tid(struct slim_controller *ctrl, struct slim_msg_txn *txn);
void slim_free_txn_tid(struct slim_controller *ctrl, struct slim_msg_txn *txn);

static inline bool slim_tid_txn(u8 mt, u8 mc)
{
	return (mt == SLIM_MSG_MT_CORE &&
		(mc == SLIM_MSG_MC_REQUEST_INFORMATION ||
		 mc == SLIM_MSG_MC_REQUEST_CLEAR_INFORMATION ||
		 mc == SLIM_MSG_MC_REQUEST_VALUE ||
		 mc == SLIM_MSG_MC_REQUEST_CLEAR_INFORMATION));
}

static inline bool slim_ec_txn(u8 mt, u8 mc)
{
	return (mt == SLIM_MSG_MT_CORE &&
		((mc >= SLIM_MSG_MC_REQUEST_INFORMATION &&
		  mc <= SLIM_MSG_MC_REPORT_INFORMATION) ||
		 (mc >= SLIM_MSG_MC_REQUEST_VALUE &&
		  mc <= SLIM_MSG_MC_CHANGE_VALUE)));
}
#endif /* _LINUX_SLIMBUS_H */
