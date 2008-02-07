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
#define SERIAL_TYPE_INFO    3
#define EPCA_EVENT_HANGUP   1
#define EPCA_MAGIC          0x5c6df104L

struct channel 
{
	long   magic;
	unsigned char boardnum;
	unsigned char channelnum;
	unsigned char omodem;         /* FEP output modem status     */
	unsigned char imodem;         /* FEP input modem status      */
	unsigned char modemfake;      /* Modem values to be forced   */
	unsigned char modem;          /* Force values                */
	unsigned char hflow;
	unsigned char dsr;
	unsigned char dcd;
	unsigned char m_rts ; 		/* The bits used in whatever FEP */
	unsigned char m_dcd ;		/* is indiginous to this board to */
	unsigned char m_dsr ;		/* represent each of the physical */
	unsigned char m_cts ;		/* handshake lines */
	unsigned char m_ri ;
	unsigned char m_dtr ;
	unsigned char stopc;
	unsigned char startc;
	unsigned char stopca;
	unsigned char startca;
	unsigned char fepstopc;
	unsigned char fepstartc;
	unsigned char fepstopca;
	unsigned char fepstartca;
	unsigned char txwin;
	unsigned char rxwin;
	unsigned short fepiflag;
	unsigned short fepcflag;
	unsigned short fepoflag;
	unsigned short txbufhead;
	unsigned short txbufsize;
	unsigned short rxbufhead;
	unsigned short rxbufsize;
	int    close_delay;
	int    count;
	int    blocked_open;
	unsigned long  event;
	int    asyncflags;
	uint   dev;
	unsigned long  statusflags;
	unsigned long  c_iflag;
	unsigned long  c_cflag;
	unsigned long  c_lflag;
	unsigned long  c_oflag;
	unsigned char __iomem *txptr;
	unsigned char __iomem *rxptr;
	struct board_info           *board;
	struct board_chan	    __iomem *brdchan;
	struct digi_struct          digiext;
	struct tty_struct           *tty;
	wait_queue_head_t           open_wait;
	wait_queue_head_t           close_wait;
	struct work_struct          tqueue;
	struct global_data 	    __iomem *mailbox;
};

struct board_info	
{
	unsigned char status;
	unsigned char type;
	unsigned char altpin;
	unsigned short numports;
	unsigned long port;
	unsigned long membase;
	void __iomem *re_map_port;
	void __iomem *re_map_membase;
	unsigned long  memory_seg;
	void ( * memwinon )	(struct board_info *, unsigned int) ;
	void ( * memwinoff ) 	(struct board_info *, unsigned int) ;
	void ( * globalwinon )	(struct channel *) ;
	void ( * txwinon ) 	(struct channel *) ;
	void ( * rxwinon )	(struct channel *) ;
	void ( * memoff )	(struct channel *) ;
	void ( * assertgwinon )	(struct channel *) ;
	void ( * assertmemoff )	(struct channel *) ;
	unsigned char poller_inhibited ;
};

