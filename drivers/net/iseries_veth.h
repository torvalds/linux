/* File veth.h created by Kyle A. Lucke on Mon Aug  7 2000. */

#ifndef _ISERIES_VETH_H
#define _ISERIES_VETH_H

#define VethEventTypeCap	(0)
#define VethEventTypeFrames	(1)
#define VethEventTypeMonitor	(2)
#define VethEventTypeFramesAck	(3)

#define VETH_MAX_ACKS_PER_MSG	(20)
#define VETH_MAX_FRAMES_PER_MSG	(6)

struct VethFramesData {
	u32 addr[VETH_MAX_FRAMES_PER_MSG];
	u16 len[VETH_MAX_FRAMES_PER_MSG];
	u32 eofmask;
};
#define VETH_EOF_SHIFT		(32-VETH_MAX_FRAMES_PER_MSG)

struct VethFramesAckData {
	u16 token[VETH_MAX_ACKS_PER_MSG];
};

struct VethCapData {
	u8 caps_version;
	u8 rsvd1;
	u16 num_buffers;
	u16 ack_threshold;
	u16 rsvd2;
	u32 ack_timeout;
	u32 rsvd3;
	u64 rsvd4[3];
};

struct VethLpEvent {
	struct HvLpEvent base_event;
	union {
		struct VethCapData caps_data;
		struct VethFramesData frames_data;
		struct VethFramesAckData frames_ack_data;
	} u;

};

#endif	/* _ISERIES_VETH_H */
