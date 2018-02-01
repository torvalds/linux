/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom BM2835 V4L2 driver
 *
 * Copyright © 2013 Raspberry Pi (Trading) Ltd.
 *
 * Authors: Vincent Sanders <vincent.sanders@collabora.co.uk>
 *          Dave Stevenson <dsteve@broadcom.com>
 *          Simon Mellor <simellor@broadcom.com>
 *          Luke Diamand <luked@broadcom.com>
 */

/* all the data structures which serialise the MMAL protocol. note
 * these are directly mapped onto the recived message data.
 *
 * BEWARE: They seem to *assume* pointers are u32 and that there is no
 * structure padding!
 *
 * NOTE: this implementation uses kernel types to ensure sizes. Rather
 * than assigning values to enums to force their size the
 * implementation uses fixed size types and not the enums (though the
 * comments have the actual enum type
 */

#define VC_MMAL_VER 15
#define VC_MMAL_MIN_VER 10
#define VC_MMAL_SERVER_NAME  MAKE_FOURCC("mmal")

/* max total message size is 512 bytes */
#define MMAL_MSG_MAX_SIZE 512
/* with six 32bit header elements max payload is therefore 488 bytes */
#define MMAL_MSG_MAX_PAYLOAD 488

#include "mmal-msg-common.h"
#include "mmal-msg-format.h"
#include "mmal-msg-port.h"

enum mmal_msg_type {
	MMAL_MSG_TYPE_QUIT = 1,
	MMAL_MSG_TYPE_SERVICE_CLOSED,
	MMAL_MSG_TYPE_GET_VERSION,
	MMAL_MSG_TYPE_COMPONENT_CREATE,
	MMAL_MSG_TYPE_COMPONENT_DESTROY, /* 5 */
	MMAL_MSG_TYPE_COMPONENT_ENABLE,
	MMAL_MSG_TYPE_COMPONENT_DISABLE,
	MMAL_MSG_TYPE_PORT_INFO_GET,
	MMAL_MSG_TYPE_PORT_INFO_SET,
	MMAL_MSG_TYPE_PORT_ACTION, /* 10 */
	MMAL_MSG_TYPE_BUFFER_FROM_HOST,
	MMAL_MSG_TYPE_BUFFER_TO_HOST,
	MMAL_MSG_TYPE_GET_STATS,
	MMAL_MSG_TYPE_PORT_PARAMETER_SET,
	MMAL_MSG_TYPE_PORT_PARAMETER_GET, /* 15 */
	MMAL_MSG_TYPE_EVENT_TO_HOST,
	MMAL_MSG_TYPE_GET_CORE_STATS_FOR_PORT,
	MMAL_MSG_TYPE_OPAQUE_ALLOCATOR,
	MMAL_MSG_TYPE_CONSUME_MEM,
	MMAL_MSG_TYPE_LMK, /* 20 */
	MMAL_MSG_TYPE_OPAQUE_ALLOCATOR_DESC,
	MMAL_MSG_TYPE_DRM_GET_LHS32,
	MMAL_MSG_TYPE_DRM_GET_TIME,
	MMAL_MSG_TYPE_BUFFER_FROM_HOST_ZEROLEN,
	MMAL_MSG_TYPE_PORT_FLUSH, /* 25 */
	MMAL_MSG_TYPE_HOST_LOG,
	MMAL_MSG_TYPE_MSG_LAST
};

/* port action request messages differ depending on the action type */
enum mmal_msg_port_action_type {
	MMAL_MSG_PORT_ACTION_TYPE_UNKNOWN = 0,      /* Unknown action */
	MMAL_MSG_PORT_ACTION_TYPE_ENABLE,           /* Enable a port */
	MMAL_MSG_PORT_ACTION_TYPE_DISABLE,          /* Disable a port */
	MMAL_MSG_PORT_ACTION_TYPE_FLUSH,            /* Flush a port */
	MMAL_MSG_PORT_ACTION_TYPE_CONNECT,          /* Connect ports */
	MMAL_MSG_PORT_ACTION_TYPE_DISCONNECT,       /* Disconnect ports */
	MMAL_MSG_PORT_ACTION_TYPE_SET_REQUIREMENTS, /* Set buffer requirements*/
};

struct mmal_msg_header {
	u32 magic;
	u32 type; /** enum mmal_msg_type */

	/* Opaque handle to the control service */
	u32 control_service;

	u32 context; /** a u32 per message context */
	u32 status; /** The status of the vchiq operation */
	u32 padding;
};

/* Send from VC to host to report version */
struct mmal_msg_version {
	u32 flags;
	u32 major;
	u32 minor;
	u32 minimum;
};

/* request to VC to create component */
struct mmal_msg_component_create {
	u32 client_component; /* component context */
	char name[128];
	u32 pid;                /* For debug */
};

/* reply from VC to component creation request */
struct mmal_msg_component_create_reply {
	u32 status;	/* enum mmal_msg_status - how does this differ to
			 * the one in the header?
			 */
	u32 component_handle; /* VideoCore handle for component */
	u32 input_num;        /* Number of input ports */
	u32 output_num;       /* Number of output ports */
	u32 clock_num;        /* Number of clock ports */
};

/* request to VC to destroy a component */
struct mmal_msg_component_destroy {
	u32 component_handle;
};

struct mmal_msg_component_destroy_reply {
	u32 status; /** The component destruction status */
};

/* request and reply to VC to enable a component */
struct mmal_msg_component_enable {
	u32 component_handle;
};

struct mmal_msg_component_enable_reply {
	u32 status; /** The component enable status */
};

/* request and reply to VC to disable a component */
struct mmal_msg_component_disable {
	u32 component_handle;
};

struct mmal_msg_component_disable_reply {
	u32 status; /** The component disable status */
};

/* request to VC to get port information */
struct mmal_msg_port_info_get {
	u32 component_handle;  /* component handle port is associated with */
	u32 port_type;         /* enum mmal_msg_port_type */
	u32 index;             /* port index to query */
};

/* reply from VC to get port info request */
struct mmal_msg_port_info_get_reply {
	u32 status; /** enum mmal_msg_status */
	u32 component_handle;  /* component handle port is associated with */
	u32 port_type;         /* enum mmal_msg_port_type */
	u32 port_index;        /* port indexed in query */
	s32 found;             /* unused */
	u32 port_handle;               /**< Handle to use for this port */
	struct mmal_port port;
	struct mmal_es_format format; /* elementary stream format */
	union mmal_es_specific_format es; /* es type specific data */
	u8 extradata[MMAL_FORMAT_EXTRADATA_MAX_SIZE]; /* es extra data */
};

/* request to VC to set port information */
struct mmal_msg_port_info_set {
	u32 component_handle;
	u32 port_type;         /* enum mmal_msg_port_type */
	u32 port_index;           /* port indexed in query */
	struct mmal_port port;
	struct mmal_es_format format;
	union mmal_es_specific_format es;
	u8 extradata[MMAL_FORMAT_EXTRADATA_MAX_SIZE];
};

/* reply from VC to port info set request */
struct mmal_msg_port_info_set_reply {
	u32 status;
	u32 component_handle;  /* component handle port is associated with */
	u32 port_type;         /* enum mmal_msg_port_type */
	u32 index;             /* port indexed in query */
	s32 found;             /* unused */
	u32 port_handle;               /**< Handle to use for this port */
	struct mmal_port port;
	struct mmal_es_format format;
	union mmal_es_specific_format es;
	u8 extradata[MMAL_FORMAT_EXTRADATA_MAX_SIZE];
};

/* port action requests that take a mmal_port as a parameter */
struct mmal_msg_port_action_port {
	u32 component_handle;
	u32 port_handle;
	u32 action; /* enum mmal_msg_port_action_type */
	struct mmal_port port;
};

/* port action requests that take handles as a parameter */
struct mmal_msg_port_action_handle {
	u32 component_handle;
	u32 port_handle;
	u32 action; /* enum mmal_msg_port_action_type */
	u32 connect_component_handle;
	u32 connect_port_handle;
};

struct mmal_msg_port_action_reply {
	u32 status; /** The port action operation status */
};

/* MMAL buffer transfer */

/** Size of space reserved in a buffer message for short messages. */
#define MMAL_VC_SHORT_DATA 128

/** Signals that the current payload is the end of the stream of data */
#define MMAL_BUFFER_HEADER_FLAG_EOS                    BIT(0)
/** Signals that the start of the current payload starts a frame */
#define MMAL_BUFFER_HEADER_FLAG_FRAME_START            BIT(1)
/** Signals that the end of the current payload ends a frame */
#define MMAL_BUFFER_HEADER_FLAG_FRAME_END              BIT(2)
/** Signals that the current payload contains only complete frames (>1) */
#define MMAL_BUFFER_HEADER_FLAG_FRAME                  \
	(MMAL_BUFFER_HEADER_FLAG_FRAME_START|MMAL_BUFFER_HEADER_FLAG_FRAME_END)
/** Signals that the current payload is a keyframe (i.e. self decodable) */
#define MMAL_BUFFER_HEADER_FLAG_KEYFRAME               BIT(3)
/** Signals a discontinuity in the stream of data (e.g. after a seek).
 * Can be used for instance by a decoder to reset its state
 */
#define MMAL_BUFFER_HEADER_FLAG_DISCONTINUITY          BIT(4)
/** Signals a buffer containing some kind of config data for the component
 * (e.g. codec config data)
 */
#define MMAL_BUFFER_HEADER_FLAG_CONFIG                 BIT(5)
/** Signals an encrypted payload */
#define MMAL_BUFFER_HEADER_FLAG_ENCRYPTED              BIT(6)
/** Signals a buffer containing side information */
#define MMAL_BUFFER_HEADER_FLAG_CODECSIDEINFO          BIT(7)
/** Signals a buffer which is the snapshot/postview image from a stills
 * capture
 */
#define MMAL_BUFFER_HEADER_FLAGS_SNAPSHOT              BIT(8)
/** Signals a buffer which contains data known to be corrupted */
#define MMAL_BUFFER_HEADER_FLAG_CORRUPTED              BIT(9)
/** Signals that a buffer failed to be transmitted */
#define MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED    BIT(10)

struct mmal_driver_buffer {
	u32 magic;
	u32 component_handle;
	u32 port_handle;
	u32 client_context;
};

/* buffer header */
struct mmal_buffer_header {
	u32 next; /* next header */
	u32 priv; /* framework private data */
	u32 cmd;
	u32 data;
	u32 alloc_size;
	u32 length;
	u32 offset;
	u32 flags;
	s64 pts;
	s64 dts;
	u32 type;
	u32 user_data;
};

struct mmal_buffer_header_type_specific {
	union {
		struct {
		u32 planes;
		u32 offset[4];
		u32 pitch[4];
		u32 flags;
		} video;
	} u;
};

struct mmal_msg_buffer_from_host {
	/* The front 32 bytes of the buffer header are copied
	 * back to us in the reply to allow for context. This
	 * area is used to store two mmal_driver_buffer structures to
	 * allow for multiple concurrent service users.
	 */
	/* control data */
	struct mmal_driver_buffer drvbuf;

	/* referenced control data for passthrough buffer management */
	struct mmal_driver_buffer drvbuf_ref;
	struct mmal_buffer_header buffer_header; /* buffer header itself */
	struct mmal_buffer_header_type_specific buffer_header_type_specific;
	s32 is_zero_copy;
	s32 has_reference;

	/** allows short data to be xfered in control message */
	u32 payload_in_message;
	u8 short_data[MMAL_VC_SHORT_DATA];
};

/* port parameter setting */

#define MMAL_WORKER_PORT_PARAMETER_SPACE      96

struct mmal_msg_port_parameter_set {
	u32 component_handle; /* component */
	u32 port_handle;      /* port */
	u32 id;     /* Parameter ID  */
	u32 size;      /* Parameter size */
	uint32_t value[MMAL_WORKER_PORT_PARAMETER_SPACE];
};

struct mmal_msg_port_parameter_set_reply {
	u32 status;	/* enum mmal_msg_status todo: how does this
			 * differ to the one in the header?
			 */
};

/* port parameter getting */

struct mmal_msg_port_parameter_get {
	u32 component_handle; /* component */
	u32 port_handle;      /* port */
	u32 id;     /* Parameter ID  */
	u32 size;      /* Parameter size */
};

struct mmal_msg_port_parameter_get_reply {
	u32 status;           /* Status of mmal_port_parameter_get call */
	u32 id;     /* Parameter ID  */
	u32 size;      /* Parameter size */
	uint32_t value[MMAL_WORKER_PORT_PARAMETER_SPACE];
};

/* event messages */
#define MMAL_WORKER_EVENT_SPACE 256

struct mmal_msg_event_to_host {
	u32 client_component; /* component context */

	u32 port_type;
	u32 port_num;

	u32 cmd;
	u32 length;
	u8 data[MMAL_WORKER_EVENT_SPACE];
	u32 delayed_buffer;
};

/* all mmal messages are serialised through this structure */
struct mmal_msg {
	/* header */
	struct mmal_msg_header h;
	/* payload */
	union {
		struct mmal_msg_version version;

		struct mmal_msg_component_create component_create;
		struct mmal_msg_component_create_reply component_create_reply;

		struct mmal_msg_component_destroy component_destroy;
		struct mmal_msg_component_destroy_reply component_destroy_reply;

		struct mmal_msg_component_enable component_enable;
		struct mmal_msg_component_enable_reply component_enable_reply;

		struct mmal_msg_component_disable component_disable;
		struct mmal_msg_component_disable_reply component_disable_reply;

		struct mmal_msg_port_info_get port_info_get;
		struct mmal_msg_port_info_get_reply port_info_get_reply;

		struct mmal_msg_port_info_set port_info_set;
		struct mmal_msg_port_info_set_reply port_info_set_reply;

		struct mmal_msg_port_action_port port_action_port;
		struct mmal_msg_port_action_handle port_action_handle;
		struct mmal_msg_port_action_reply port_action_reply;

		struct mmal_msg_buffer_from_host buffer_from_host;

		struct mmal_msg_port_parameter_set port_parameter_set;
		struct mmal_msg_port_parameter_set_reply
			port_parameter_set_reply;
		struct mmal_msg_port_parameter_get
			port_parameter_get;
		struct mmal_msg_port_parameter_get_reply
			port_parameter_get_reply;

		struct mmal_msg_event_to_host event_to_host;

		u8 payload[MMAL_MSG_MAX_PAYLOAD];
	} u;
};
