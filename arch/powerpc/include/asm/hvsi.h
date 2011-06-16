#ifndef _HVSI_H
#define _HVSI_H

#define VS_DATA_PACKET_HEADER           0xff
#define VS_CONTROL_PACKET_HEADER        0xfe
#define VS_QUERY_PACKET_HEADER          0xfd
#define VS_QUERY_RESPONSE_PACKET_HEADER 0xfc

/* control verbs */
#define VSV_SET_MODEM_CTL    1 /* to service processor only */
#define VSV_MODEM_CTL_UPDATE 2 /* from service processor only */
#define VSV_CLOSE_PROTOCOL   3

/* query verbs */
#define VSV_SEND_VERSION_NUMBER 1
#define VSV_SEND_MODEM_CTL_STATUS 2

/* yes, these masks are not consecutive. */
#define HVSI_TSDTR 0x01
#define HVSI_TSCD  0x20

#define HVSI_MAX_OUTGOING_DATA 12
#define HVSI_VERSION 1

struct hvsi_header {
	uint8_t  type;
	uint8_t  len;
	uint16_t seqno;
} __attribute__((packed));

struct hvsi_data {
	struct hvsi_header hdr;
	uint8_t  data[HVSI_MAX_OUTGOING_DATA];
} __attribute__((packed));

struct hvsi_control {
	struct hvsi_header hdr;
	uint16_t verb;
	/* optional depending on verb: */
	uint32_t word;
	uint32_t mask;
} __attribute__((packed));

struct hvsi_query {
	struct hvsi_header hdr;
	uint16_t verb;
} __attribute__((packed));

struct hvsi_query_response {
	struct hvsi_header hdr;
	uint16_t verb;
	uint16_t query_seqno;
	union {
		uint8_t  version;
		uint32_t mctrl_word;
	} u;
} __attribute__((packed));


#endif /* _HVSI_H */
