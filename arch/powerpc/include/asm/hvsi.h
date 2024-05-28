/* SPDX-License-Identifier: GPL-2.0 */
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
	__be16 seqno;
} __attribute__((packed));

struct hvsi_data {
	struct hvsi_header hdr;
	uint8_t  data[HVSI_MAX_OUTGOING_DATA];
} __attribute__((packed));

struct hvsi_control {
	struct hvsi_header hdr;
	__be16 verb;
	/* optional depending on verb: */
	__be32 word;
	__be32 mask;
} __attribute__((packed));

struct hvsi_query {
	struct hvsi_header hdr;
	__be16 verb;
} __attribute__((packed));

struct hvsi_query_response {
	struct hvsi_header hdr;
	__be16 verb;
	__be16 query_seqno;
	union {
		uint8_t  version;
		__be32 mctrl_word;
	} u;
} __attribute__((packed));

/* hvsi lib struct definitions */
#define HVSI_INBUF_SIZE		255
struct tty_struct;
struct hvsi_priv {
	unsigned int	inbuf_len;	/* data in input buffer */
	unsigned char	inbuf[HVSI_INBUF_SIZE];
	unsigned int	inbuf_cur;	/* Cursor in input buffer */
	size_t		inbuf_pktlen;	/* packet length from cursor */
	atomic_t	seqno;		/* packet sequence number */
	unsigned int	opened:1;	/* driver opened */
	unsigned int	established:1;	/* protocol established */
	unsigned int 	is_console:1;	/* used as a kernel console device */
	unsigned int	mctrl_update:1;	/* modem control updated */
	unsigned short	mctrl;		/* modem control */
	struct tty_struct *tty;		/* tty structure */
	ssize_t (*get_chars)(uint32_t termno, u8 *buf, size_t count);
	ssize_t (*put_chars)(uint32_t termno, const u8 *buf, size_t count);
	uint32_t	termno;
};

/* hvsi lib functions */
struct hvc_struct;
extern void hvsilib_init(struct hvsi_priv *pv,
			 ssize_t (*get_chars)(uint32_t termno, u8 *buf,
					      size_t count),
			 ssize_t (*put_chars)(uint32_t termno, const u8 *buf,
					      size_t count),
			 int termno, int is_console);
extern int hvsilib_open(struct hvsi_priv *pv, struct hvc_struct *hp);
extern void hvsilib_close(struct hvsi_priv *pv, struct hvc_struct *hp);
extern int hvsilib_read_mctrl(struct hvsi_priv *pv);
extern int hvsilib_write_mctrl(struct hvsi_priv *pv, int dtr);
extern void hvsilib_establish(struct hvsi_priv *pv);
extern ssize_t hvsilib_get_chars(struct hvsi_priv *pv, u8 *buf, size_t count);
extern ssize_t hvsilib_put_chars(struct hvsi_priv *pv, const u8 *buf,
				 size_t count);

#endif /* _HVSI_H */
