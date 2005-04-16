/* am7990 (lance) definitions
 * 
 * This is an extension to the Linux operating system, and is covered by
 * same GNU General Public License that covers that work.
 * 
 * Michael Hipp
 * email: mhipp@student.uni-tuebingen.de
 *
 * sources: (mail me or ask archie if you need them) 
 *    crynwr-packet-driver
 */

/*
 * 	Control and Status Register 0 (CSR0) bit definitions
 * (R=Readable) (W=Writeable) (S=Set on write) (C-Clear on write)
 *
 */

#define CSR0_ERR	0x8000	/* Error summary (R) */
#define CSR0_BABL	0x4000	/* Babble transmitter timeout error (RC) */
#define CSR0_CERR	0x2000	/* Collision Error (RC) */
#define CSR0_MISS	0x1000	/* Missed packet (RC) */
#define CSR0_MERR	0x0800	/* Memory Error (RC) */
#define CSR0_RINT	0x0400	/* Receiver Interrupt (RC) */
#define CSR0_TINT       0x0200	/* Transmit Interrupt (RC) */
#define CSR0_IDON	0x0100	/* Initialization Done (RC) */
#define CSR0_INTR	0x0080	/* Interrupt Flag (R) */
#define CSR0_INEA	0x0040	/* Interrupt Enable (RW) */
#define CSR0_RXON	0x0020	/* Receiver on (R) */
#define CSR0_TXON	0x0010	/* Transmitter on (R) */
#define CSR0_TDMD	0x0008	/* Transmit Demand (RS) */
#define CSR0_STOP	0x0004	/* Stop (RS) */
#define CSR0_STRT	0x0002	/* Start (RS) */
#define CSR0_INIT	0x0001	/* Initialize (RS) */

#define CSR0_CLRALL    0x7f00	/* mask for all clearable bits */
/*
 *	Initialization Block  Mode operation Bit Definitions.
 */

#define M_PROM		0x8000	/* Promiscuous Mode */
#define M_INTL		0x0040	/* Internal Loopback */
#define M_DRTY		0x0020	/* Disable Retry */
#define M_COLL		0x0010	/* Force Collision */
#define M_DTCR		0x0008	/* Disable Transmit CRC) */
#define M_LOOP		0x0004	/* Loopback */
#define M_DTX		0x0002	/* Disable the Transmitter */
#define M_DRX		0x0001	/* Disable the Receiver */


/*
 * 	Receive message descriptor bit definitions.
 */

#define RCV_OWN		0x80	/* owner bit 0 = host, 1 = lance */
#define RCV_ERR		0x40	/* Error Summary */
#define RCV_FRAM	0x20	/* Framing Error */
#define RCV_OFLO	0x10	/* Overflow Error */
#define RCV_CRC		0x08	/* CRC Error */
#define RCV_BUF_ERR	0x04	/* Buffer Error */
#define RCV_START	0x02	/* Start of Packet */
#define RCV_END		0x01	/* End of Packet */


/*
 *	Transmit  message descriptor bit definitions.
 */

#define XMIT_OWN	0x80	/* owner bit 0 = host, 1 = lance */
#define XMIT_ERR	0x40	/* Error Summary */
#define XMIT_RETRY	0x10	/* more the 1 retry needed to Xmit */
#define XMIT_1_RETRY	0x08	/* one retry needed to Xmit */
#define XMIT_DEF	0x04	/* Deferred */
#define XMIT_START	0x02	/* Start of Packet */
#define XMIT_END	0x01	/* End of Packet */

/*
 * transmit status (2) (valid if XMIT_ERR == 1)
 */

#define XMIT_TDRMASK    0x03ff	/* time-domain-reflectometer-value */
#define XMIT_RTRY 	0x0400	/* Failed after 16 retransmissions  */
#define XMIT_LCAR 	0x0800	/* Loss of Carrier */
#define XMIT_LCOL 	0x1000	/* Late collision */
#define XMIT_RESERV 	0x2000	/* Reserved */
#define XMIT_UFLO 	0x4000	/* Underflow (late memory) */
#define XMIT_BUFF 	0x8000	/* Buffering error (no ENP) */

struct init_block {
	unsigned short mode;
	unsigned char eaddr[6];
	unsigned char filter[8];
	/* bit 29-31: number of rmd's (power of 2) */
	u32 rrp;		/* receive ring pointer (align 8) */
	/* bit 29-31: number of tmd's (power of 2) */
	u32 trp;		/* transmit ring pointer (align 8) */
};

struct rmd {			/* Receive Message Descriptor */
	union {
		volatile u32 buffer;
		struct {
			volatile unsigned char dummy[3];
			volatile unsigned char status;
		} s;
	} u;
	volatile short blen;
	volatile unsigned short mlen;
};

struct tmd {
	union {
		volatile u32 buffer;
		struct {
			volatile unsigned char dummy[3];
			volatile unsigned char status;
		} s;
	} u;
	volatile unsigned short blen;
	volatile unsigned short status2;
};
