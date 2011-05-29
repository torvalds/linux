
//
union ip2breadcrumb 
{
	struct { 
		unsigned char port, cat, codes, label;
	} __attribute__ ((packed)) hdr;
	unsigned long value;
};

#define ITRC_NO_PORT 	0xFF
#define CHANN	(pCh->port_index)

#define	ITRC_ERROR	'!'
#define	ITRC_INIT 	'A'
#define	ITRC_OPEN	'B'
#define	ITRC_CLOSE	'C'
#define	ITRC_DRAIN	'D'
#define	ITRC_IOCTL	'E'
#define	ITRC_FLUSH	'F'
#define	ITRC_STATUS	'G'
#define	ITRC_HANGUP	'H'
#define	ITRC_INTR 	'I'
#define	ITRC_SFLOW	'J'
#define	ITRC_SBCMD	'K'
#define	ITRC_SICMD	'L'
#define	ITRC_MODEM	'M'
#define	ITRC_INPUT	'N'
#define	ITRC_OUTPUT	'O'
#define	ITRC_PUTC	'P'
#define	ITRC_QUEUE	'Q'
#define	ITRC_STFLW	'R'
#define	ITRC_SFIFO	'S'
#define	ITRC_VERIFY	'V'
#define	ITRC_WRITE	'W'

#define	ITRC_ENTER	0x00
#define	ITRC_RETURN	0xFF

#define	ITRC_QUEUE_ROOM	2
#define	ITRC_QUEUE_CMD	6

