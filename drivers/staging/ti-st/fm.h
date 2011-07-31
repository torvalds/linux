struct fm_event_hdr {
	unsigned char plen;
} __attribute__ ((packed));

#define FM_MAX_FRAME_SIZE 0xFF	/* TODO: */
#define FM_EVENT_HDR_SIZE 1	/* size of fm_event_hdr */
#define ST_FM_CH8_PKT 0x8

/* gps stuff */
struct gps_event_hdr {
unsigned char opcode;
unsigned short plen;
} __attribute__ ((packed));
