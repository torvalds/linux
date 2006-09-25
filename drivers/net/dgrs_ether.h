/*
 *	A filtering function.  There are two filters/port.  Filter "0"
 *	is the input filter, and filter "1" is the output filter.
 */
typedef int (FILTER_FUNC)(uchar *pktp, int pktlen, ulong *scratch, int port);
#define	NFILTERS	2

/*
 *	The per port structure
 */
typedef struct
{
	int		chan;		/* Channel number (0-3) */
	ulong		portaddr;	/* address of 596 port register */
	volatile ulong	*ca;		/* address of 596 chan attention */
	ulong		intmask;	/* Interrupt mask for this port */
	ulong		intack;		/* Ack bit for this port */

	uchar		ethaddr[6];	/* Ethernet address of this port */
	int		is_promisc;	/* Port is promiscuous */

	int		debug;		/* Debugging turned on */

	I596_ISCP	*iscpp;		/* Uncached ISCP pointer */
	I596_SCP	*scpp;		/* Uncached SCP pointer */
	I596_SCB	*scbp;		/* Uncached SCB pointer */

	I596_ISCP	iscp;
	I596_SCB	scb;

	/* Command Queue */
	I596_CB		*cb0;
	I596_CB		*cbN;
	I596_CB		*cb_head;
	I596_CB		*cb_tail;

	/* Receive Queue */
	I596_RFD	*rfd0;
	I596_RFD	*rfdN;
	I596_RFD	*rfd_head;
	I596_RFD	*rfd_tail;

	/* Receive Buffers */
	I596_RBD	*rbd0;
	I596_RBD	*rbdN;
	I596_RBD	*rbd_head;
	I596_RBD	*rbd_tail;
	int		buf_size;	/* Size of an RBD buffer */
	int		buf_cnt;	/* Total RBD's allocated */

	/* Rx Statistics */
	ulong		cnt_rx_cnt;	/* Total packets rcvd, good and bad */
	ulong		cnt_rx_good;	/* Total good packets rcvd */
	ulong		cnt_rx_bad;	/* Total of all bad packets rcvd */
					/* Subtotals can be gotten from SCB */
	ulong		cnt_rx_nores;	/* No resources */
	ulong		cnt_rx_bytes;	/* Total bytes rcvd */

	/* Tx Statistics */
	ulong		cnt_tx_queued;
	ulong		cnt_tx_done;
	ulong		cnt_tx_freed;
	ulong		cnt_tx_nores;	/* No resources */

	ulong		cnt_tx_bad;
	ulong		cnt_tx_err_late;
	ulong		cnt_tx_err_nocrs;
	ulong		cnt_tx_err_nocts;
	ulong		cnt_tx_err_under;
	ulong		cnt_tx_err_maxcol;
	ulong		cnt_tx_collisions;

	/* Special stuff for host */
#	define		rfd_freed	cnt_rx_cnt
	ulong		rbd_freed;
	int		host_timer;

	/* Added after first beta */
	ulong		cnt_tx_races;	/* Counts race conditions */
	int		spanstate;
	ulong		cnt_st_tx;	/* send span tree pkts */
	ulong		cnt_st_fail_tx;	/* Failures to send span tree pkts */
	ulong		cnt_st_fail_rbd;/* Failures to send span tree pkts */
	ulong		cnt_st_rx;	/* rcv span tree pkts */
	ulong		cnt_st_rx_bad;	/* bogus st packets rcvd */
	ulong		cnt_rx_fwd;	/* Rcvd packets that were forwarded */

	ulong		cnt_rx_mcast;	/* Multicast pkts received */
	ulong		cnt_tx_mcast;	/* Multicast pkts transmitted */
	ulong		cnt_tx_bytes;	/* Bytes transmitted */

	/*
	 *	Packet filtering
	 *	Filter 0: input filter
	 *	Filter 1: output filter
	 */

	ulong		*filter_space[NFILTERS];
	FILTER_FUNC	*filter_func[NFILTERS];
	ulong		filter_cnt[NFILTERS];
	ulong		filter_len[NFILTERS];

	ulong		pad[ (512-300) / 4];
} PORT;

/*
 *	Port[0]			is host interface
 *	Port[1..SE_NPORTS]	are external 10 Base T ports.  Fewer may be in
 *				use, depending on whether this is an SE-4 or
 *				an SE-6.
 *	Port[SE_NPORTS]		Pseudo-port for Spanning tree and SNMP
 */
extern PORT	Port[1+SE_NPORTS+1];

extern int	Nports;		/* Number of genuine ethernet controllers */
extern int	Nchan;		/* ... plus one for host interface */

extern int	FirstChan;	/* 0 or 1, depedning on whether host is used */
extern int	NumChan;	/* 4 or 5 */

/*
 *	A few globals
 */
extern int	IsPromisc;
extern int	MultiNicMode;

/*
 *	Functions
 */
extern void	eth_xmit_spew_on(PORT *p, int cnt);
extern void	eth_xmit_spew_off(PORT *p);

extern I596_RBD	*alloc_rbds(PORT *p, int num);

extern I596_CB * eth_cb_alloc(PORT *p);
