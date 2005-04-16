#define XEMPORTS    0xC02
#define XEPORTS     0xC22

#define MAX_ALLOC   0x100

#define MAXBOARDS   12
#define FEPCODESEG  0x0200L
#define FEPCODE     0x2000L
#define BIOSCODE    0xf800L

#define MISCGLOBAL  0x0C00L
#define NPORT       0x0C22L
#define MBOX        0x0C40L
#define PORTBASE    0x0C90L

/* Begin code defines used for epca_setup */

#define INVALID_BOARD_TYPE   0x1
#define INVALID_NUM_PORTS    0x2
#define INVALID_MEM_BASE     0x4
#define INVALID_PORT_BASE    0x8
#define INVALID_BOARD_STATUS 0x10
#define INVALID_ALTPIN       0x20

/* End code defines used for epca_setup */


#define FEPCLR      0x00
#define FEPMEM      0x02
#define FEPRST      0x04
#define FEPINT      0x08
#define	FEPMASK     0x0e
#define	FEPWIN      0x80

#define PCXE    0
#define PCXEVE  1
#define PCXEM   2   
#define EISAXEM 3
#define PC64XE  4
#define PCXI    5
#define PCIXEM  7
#define PCICX   8
#define PCIXR   9
#define PCIXRJ  10
#define EPCA_NUM_TYPES 6


static char *board_desc[] = 
{
	"PC/Xe",
	"PC/Xeve",
	"PC/Xem",
	"EISA/Xem",
	"PC/64Xe",
	"PC/Xi",
	"unknown",
	"PCI/Xem",
	"PCI/CX",
	"PCI/Xr",
	"PCI/Xrj",
};

#define STARTC      021
#define STOPC       023
#define IAIXON      0x2000


#define TXSTOPPED  0x1
#define LOWWAIT    0x2
#define EMPTYWAIT  0x4
#define RXSTOPPED  0x8
#define TXBUSY     0x10

#define DISABLED   0
#define ENABLED    1
#define OFF        0
#define ON         1

#define FEPTIMEOUT 200000  
#define SERIAL_TYPE_NORMAL  1
#define SERIAL_TYPE_INFO    3
#define EPCA_EVENT_HANGUP   1
#define EPCA_MAGIC          0x5c6df104L

struct channel 
{
	long   magic;
	unchar boardnum;
	unchar channelnum;
	unchar omodem;         /* FEP output modem status     */
	unchar imodem;         /* FEP input modem status      */
	unchar modemfake;      /* Modem values to be forced   */
	unchar modem;          /* Force values                */
	unchar hflow;
	unchar dsr;
	unchar dcd;
	unchar m_rts ; 		/* The bits used in whatever FEP */
	unchar m_dcd ;		/* is indiginous to this board to */
	unchar m_dsr ;		/* represent each of the physical */
	unchar m_cts ;		/* handshake lines */
	unchar m_ri ;
	unchar m_dtr ;
	unchar stopc;
	unchar startc;
	unchar stopca;
	unchar startca;
	unchar fepstopc;
	unchar fepstartc;
	unchar fepstopca;
	unchar fepstartca;
	unchar txwin;
	unchar rxwin;
	ushort fepiflag;
	ushort fepcflag;
	ushort fepoflag;
	ushort txbufhead;
	ushort txbufsize;
	ushort rxbufhead;
	ushort rxbufsize;
	int    close_delay;
	int    count;
	int    blocked_open;
	ulong  event;
	int    asyncflags;
	uint   dev;
	ulong  statusflags;
	ulong  c_iflag;
	ulong  c_cflag;
	ulong  c_lflag;
	ulong  c_oflag;
	unchar *txptr;
	unchar *rxptr;
	unchar *tmp_buf;
	struct board_info           *board;
	volatile struct board_chan  *brdchan;
	struct digi_struct          digiext;
	struct tty_struct           *tty;
	wait_queue_head_t           open_wait;
	wait_queue_head_t           close_wait;
	struct work_struct            tqueue;
	volatile struct global_data *mailbox;
};

struct board_info	
{
	unchar status;
	unchar type;
	unchar altpin;
	ushort numports;
	unchar *port;
	unchar *membase;
	unchar __iomem *re_map_port;
	unchar *re_map_membase;
	ulong  memory_seg;
	void ( * memwinon )	(struct board_info *, unsigned int) ;
	void ( * memwinoff ) 	(struct board_info *, unsigned int) ;
	void ( * globalwinon )	(struct channel *) ;
	void ( * txwinon ) 	(struct channel *) ;
	void ( * rxwinon )	(struct channel *) ;
	void ( * memoff )	(struct channel *) ;
	void ( * assertgwinon )	(struct channel *) ;
	void ( * assertmemoff )	(struct channel *) ;
	unchar poller_inhibited ;
};

