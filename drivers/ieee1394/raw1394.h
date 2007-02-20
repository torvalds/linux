#ifndef IEEE1394_RAW1394_H
#define IEEE1394_RAW1394_H

/* header for the raw1394 API that is exported to user-space */

#define RAW1394_KERNELAPI_VERSION 4

/* state: opened */
#define RAW1394_REQ_INITIALIZE    1

/* state: initialized */
#define RAW1394_REQ_LIST_CARDS    2
#define RAW1394_REQ_SET_CARD      3

/* state: connected */
#define RAW1394_REQ_ASYNC_READ      100
#define RAW1394_REQ_ASYNC_WRITE     101
#define RAW1394_REQ_LOCK            102
#define RAW1394_REQ_LOCK64          103
#define RAW1394_REQ_ISO_SEND        104
#define RAW1394_REQ_ASYNC_SEND      105
#define RAW1394_REQ_ASYNC_STREAM    106

#define RAW1394_REQ_ISO_LISTEN      200
#define RAW1394_REQ_FCP_LISTEN      201
#define RAW1394_REQ_RESET_BUS       202
#define RAW1394_REQ_GET_ROM         203
#define RAW1394_REQ_UPDATE_ROM      204
#define RAW1394_REQ_ECHO            205
#define RAW1394_REQ_MODIFY_ROM      206

#define RAW1394_REQ_ARM_REGISTER    300
#define RAW1394_REQ_ARM_UNREGISTER  301
#define RAW1394_REQ_ARM_SET_BUF     302
#define RAW1394_REQ_ARM_GET_BUF     303

#define RAW1394_REQ_RESET_NOTIFY    400

#define RAW1394_REQ_PHYPACKET       500

/* kernel to user */
#define RAW1394_REQ_BUS_RESET        10000
#define RAW1394_REQ_ISO_RECEIVE      10001
#define RAW1394_REQ_FCP_REQUEST      10002
#define RAW1394_REQ_ARM              10003
#define RAW1394_REQ_RAWISO_ACTIVITY  10004

/* error codes */
#define RAW1394_ERROR_NONE        0
#define RAW1394_ERROR_COMPAT      (-1001)
#define RAW1394_ERROR_STATE_ORDER (-1002)
#define RAW1394_ERROR_GENERATION  (-1003)
#define RAW1394_ERROR_INVALID_ARG (-1004)
#define RAW1394_ERROR_MEMFAULT    (-1005)
#define RAW1394_ERROR_ALREADY     (-1006)

#define RAW1394_ERROR_EXCESSIVE   (-1020)
#define RAW1394_ERROR_UNTIDY_LEN  (-1021)

#define RAW1394_ERROR_SEND_ERROR  (-1100)
#define RAW1394_ERROR_ABORTED     (-1101)
#define RAW1394_ERROR_TIMEOUT     (-1102)

/* arm_codes */
#define ARM_READ   1
#define ARM_WRITE  2
#define ARM_LOCK   4

#define RAW1394_LONG_RESET  0
#define RAW1394_SHORT_RESET 1

/* busresetnotify ... */
#define RAW1394_NOTIFY_OFF 0
#define RAW1394_NOTIFY_ON  1

#include <asm/types.h>

struct raw1394_request {
        __u32 type;
        __s32 error;
        __u32 misc;

        __u32 generation;
        __u32 length;

        __u64 address;

        __u64 tag;

        __u64 sendb;
        __u64 recvb;
};

struct raw1394_khost_list {
        __u32 nodes;
        __u8 name[32];
};

typedef struct arm_request {
        __u16           destination_nodeid;
        __u16           source_nodeid;
        __u64           destination_offset;
        __u8            tlabel;
        __u8            tcode;
        __u8            extended_transaction_code;
        __u32           generation;
        __u16           buffer_length;
        __u8            __user *buffer;
} *arm_request_t;

typedef struct arm_response {
        __s32           response_code;
        __u16           buffer_length;
        __u8            __user *buffer;
} *arm_response_t;

typedef struct arm_request_response {
        struct arm_request  __user *request;
        struct arm_response __user *response;
} *arm_request_response_t;

/* rawiso API */
#include "ieee1394-ioctl.h"

/* per-packet metadata embedded in the ringbuffer */
/* must be identical to hpsb_iso_packet_info in iso.h! */
struct raw1394_iso_packet_info {
	__u32 offset;
	__u16 len;
	__u16 cycle;   /* recv only */
	__u8  channel; /* recv only */
	__u8  tag;
	__u8  sy;
};

/* argument for RAW1394_ISO_RECV/XMIT_PACKETS ioctls */
struct raw1394_iso_packets {
	__u32 n_packets;
	struct raw1394_iso_packet_info __user *infos;
};

struct raw1394_iso_config {
	/* size of packet data buffer, in bytes (will be rounded up to PAGE_SIZE) */
	__u32 data_buf_size;

	/* # of packets to buffer */
	__u32 buf_packets;

	/* iso channel (set to -1 for multi-channel recv) */
	__s32 channel;

	/* xmit only - iso transmission speed */
	__u8 speed;

	/* The mode of the dma when receiving iso data. Must be supported by chip */
	__u8 dma_mode;

	/* max. latency of buffer, in packets (-1 if you don't care) */
	__s32 irq_interval;
};

/* argument to RAW1394_ISO_XMIT/RECV_INIT and RAW1394_ISO_GET_STATUS */
struct raw1394_iso_status {
	/* current settings */
	struct raw1394_iso_config config;

	/* number of packets waiting to be filled with data (ISO transmission)
	   or containing data received (ISO reception) */
	__u32 n_packets;

	/* approximate number of packets dropped due to overflow or
	   underflow of the packet buffer (a value of zero guarantees
	   that no packets have been dropped) */
	__u32 overflows;

	/* cycle number at which next packet will be transmitted;
	   -1 if not known */
	__s16 xmit_cycle;
};

/* argument to RAW1394_IOC_GET_CYCLE_TIMER ioctl */
struct raw1394_cycle_timer {
	/* contents of Isochronous Cycle Timer register,
	   as in OHCI 1.1 clause 5.13 (also with non-OHCI hosts) */
	__u32 cycle_timer;

	/* local time in microseconds since Epoch,
	   simultaneously read with cycle timer */
	__u64 local_time;
};
#endif /* IEEE1394_RAW1394_H */
