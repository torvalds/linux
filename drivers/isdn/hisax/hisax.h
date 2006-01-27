/* $Id: hisax.h,v 2.64.2.4 2004/02/11 13:21:33 keil Exp $
 *
 * Basic declarations, defines and prototypes
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/isdnif.h>
#include <linux/tty.h>
#include <linux/serial_reg.h>
#include <linux/netdevice.h>

#define ERROR_STATISTIC

#define REQUEST		0
#define CONFIRM		1
#define INDICATION	2
#define RESPONSE	3

#define HW_ENABLE	0x0000
#define HW_RESET	0x0004
#define HW_POWERUP	0x0008
#define HW_ACTIVATE	0x0010
#define HW_DEACTIVATE	0x0018

#define HW_INFO1	0x0010
#define HW_INFO2	0x0020
#define HW_INFO3	0x0030
#define HW_INFO4	0x0040
#define HW_INFO4_P8	0x0040
#define HW_INFO4_P10	0x0048
#define HW_RSYNC	0x0060
#define HW_TESTLOOP	0x0070
#define CARD_RESET	0x00F0
#define CARD_INIT	0x00F2
#define CARD_RELEASE	0x00F3
#define CARD_TEST	0x00F4
#define CARD_AUX_IND	0x00F5

#define PH_ACTIVATE	0x0100
#define PH_DEACTIVATE	0x0110
#define PH_DATA		0x0120
#define PH_PULL		0x0130
#define PH_TESTLOOP	0x0140
#define PH_PAUSE	0x0150
#define MPH_ACTIVATE	0x0180
#define MPH_DEACTIVATE	0x0190
#define MPH_INFORMATION	0x01A0

#define DL_ESTABLISH	0x0200
#define DL_RELEASE	0x0210
#define DL_DATA		0x0220
#define DL_FLUSH	0x0224
#define DL_UNIT_DATA	0x0230

#define MDL_BC_RELEASE  0x0278  // Formula-n enter:now
#define MDL_BC_ASSIGN   0x027C  // Formula-n enter:now
#define MDL_ASSIGN	0x0280
#define MDL_REMOVE	0x0284
#define MDL_ERROR	0x0288
#define MDL_INFO_SETUP	0x02E0
#define MDL_INFO_CONN	0x02E4
#define MDL_INFO_REL	0x02E8

#define CC_SETUP	0x0300
#define CC_RESUME	0x0304
#define CC_MORE_INFO	0x0310
#define CC_IGNORE	0x0320
#define CC_REJECT	0x0324
#define CC_SETUP_COMPL	0x0330
#define CC_PROCEEDING	0x0340
#define CC_ALERTING	0x0344
#define CC_PROGRESS	0x0348
#define CC_CONNECT	0x0350
#define CC_CHARGE	0x0354
#define CC_NOTIFY	0x0358
#define CC_DISCONNECT	0x0360
#define CC_RELEASE	0x0368
#define CC_SUSPEND	0x0370
#define CC_PROCEED_SEND 0x0374
#define CC_REDIR        0x0378
#define CC_T302		0x0382
#define CC_T303		0x0383
#define CC_T304		0x0384
#define CC_T305		0x0385
#define CC_T308_1	0x0388
#define CC_T308_2	0x038A
#define CC_T309         0x0309
#define CC_T310		0x0390
#define CC_T313		0x0393
#define CC_T318		0x0398
#define CC_T319		0x0399
#define CC_TSPID	0x03A0
#define CC_NOSETUP_RSP	0x03E0
#define CC_SETUP_ERR	0x03E1
#define CC_SUSPEND_ERR	0x03E2
#define CC_RESUME_ERR	0x03E3
#define CC_CONNECT_ERR	0x03E4
#define CC_RELEASE_ERR	0x03E5
#define CC_RESTART	0x03F4
#define CC_TDSS1_IO     0x13F4    /* DSS1 IO user timer */
#define CC_TNI1_IO      0x13F5    /* NI1 IO user timer */

/* define maximum number of possible waiting incoming calls */
#define MAX_WAITING_CALLS 2


#ifdef __KERNEL__

/* include l3dss1 & ni1 specific process structures, but no other defines */
#ifdef CONFIG_HISAX_EURO
  #define l3dss1_process
  #include "l3dss1.h" 
  #undef  l3dss1_process
#endif /* CONFIG_HISAX_EURO */

#ifdef CONFIG_HISAX_NI1
  #define l3ni1_process
  #include "l3ni1.h" 
  #undef  l3ni1_process
#endif /* CONFIG_HISAX_NI1 */

#define MAX_DFRAME_LEN	260
#define MAX_DFRAME_LEN_L1	300
#define HSCX_BUFMAX	4096
#define MAX_DATA_SIZE	(HSCX_BUFMAX - 4)
#define MAX_DATA_MEM	(HSCX_BUFMAX + 64)
#define RAW_BUFMAX	(((HSCX_BUFMAX*6)/5) + 5)
#define MAX_HEADER_LEN	4
#define MAX_WINDOW	8
#define MAX_MON_FRAME	32
#define MAX_DLOG_SPACE	2048
#define MAX_BLOG_SPACE	256

/* #define I4L_IRQ_FLAG SA_INTERRUPT */
#define I4L_IRQ_FLAG    0

/*
 * Statemachine
 */

struct FsmInst;

typedef void (* FSMFNPTR)(struct FsmInst *, int, void *);

struct Fsm {
	FSMFNPTR *jumpmatrix;
	int state_count, event_count;
	char **strEvent, **strState;
};

struct FsmInst {
	struct Fsm *fsm;
	int state;
	int debug;
	void *userdata;
	int userint;
	void (*printdebug) (struct FsmInst *, char *, ...);
};

struct FsmNode {
	int state, event;
	void (*routine) (struct FsmInst *, int, void *);
};

struct FsmTimer {
	struct FsmInst *fi;
	struct timer_list tl;
	int event;
	void *arg;
};

struct L3Timer {
	struct l3_process *pc;
	struct timer_list tl;
	int event;
};

#define FLG_L1_ACTIVATING	1
#define FLG_L1_ACTIVATED	2
#define FLG_L1_DEACTTIMER	3
#define FLG_L1_ACTTIMER		4
#define FLG_L1_T3RUN		5
#define FLG_L1_PULL_REQ		6
#define FLG_L1_UINT		7

struct Layer1 {
	void *hardware;
	struct BCState *bcs;
	struct PStack **stlistp;
	long Flags;
	struct FsmInst l1m;
	struct FsmTimer	timer;
	void (*l1l2) (struct PStack *, int, void *);
	void (*l1hw) (struct PStack *, int, void *);
	void (*l1tei) (struct PStack *, int, void *);
	int mode, bc;
	int delay;
};

#define GROUP_TEI	127
#define TEI_SAPI	63
#define CTRL_SAPI	0
#define PACKET_NOACK	250

/* Layer2 Flags */

#define FLG_LAPB	0
#define FLG_LAPD	1
#define FLG_ORIG	2
#define FLG_MOD128	3
#define FLG_PEND_REL	4
#define FLG_L3_INIT	5
#define FLG_T200_RUN	6
#define FLG_ACK_PEND	7
#define FLG_REJEXC	8
#define FLG_OWN_BUSY	9
#define FLG_PEER_BUSY	10
#define FLG_DCHAN_BUSY	11
#define FLG_L1_ACTIV	12
#define FLG_ESTAB_PEND	13
#define FLG_PTP		14
#define FLG_FIXED_TEI	15
#define FLG_L2BLOCK	16

struct Layer2 {
	int tei;
	int sap;
	int maxlen;
	u_long flag;
	spinlock_t lock;
	u_int vs, va, vr;
	int rc;
	unsigned int window;
	unsigned int sow;
	struct sk_buff *windowar[MAX_WINDOW];
	struct sk_buff_head i_queue;
	struct sk_buff_head ui_queue;
	void (*l2l1) (struct PStack *, int, void *);
	void (*l2l3) (struct PStack *, int, void *);
	void (*l2tei) (struct PStack *, int, void *);
	struct FsmInst l2m;
	struct FsmTimer t200, t203;
	int T200, N200, T203;
	int debug;
	char debug_id[16];
};

struct Layer3 {
	void (*l3l4) (struct PStack *, int, void *);
        void (*l3ml3) (struct PStack *, int, void *);
	void (*l3l2) (struct PStack *, int, void *);
	struct FsmInst l3m;
        struct FsmTimer l3m_timer;
	struct sk_buff_head squeue;
	struct l3_process *proc;
	struct l3_process *global;
	int N303;
	int debug;
	char debug_id[8];
};

struct LLInterface {
	void (*l4l3) (struct PStack *, int, void *);
        int  (*l4l3_proto) (struct PStack *, isdn_ctrl *);
	void *userdata;
	u_long flag;
};

#define	FLG_LLI_L1WAKEUP	1
#define	FLG_LLI_L2WAKEUP	2

struct Management {
	int	ri;
	struct FsmInst tei_m;
	struct FsmTimer t202;
	int T202, N202, debug;
	void (*layer) (struct PStack *, int, void *);
};

#define NO_CAUSE 254

struct Param {
	u_char cause;
	u_char loc;
	u_char diag[6];
	int bchannel;
	int chargeinfo;
	int spv;		/* SPV Flag */
	setup_parm setup;	/* from isdnif.h numbers and Serviceindicator */
	u_char moderate;	/* transfer mode and rate (bearer octet 4) */
};


struct PStack {
	struct PStack *next;
	struct Layer1 l1;
	struct Layer2 l2;
	struct Layer3 l3;
	struct LLInterface lli;
	struct Management ma;
	int protocol;		/* EDSS1, 1TR6 or NI1 */

        /* protocol specific data fields */
        union
	 { u_char uuuu; /* only as dummy */
#ifdef CONFIG_HISAX_EURO
           dss1_stk_priv dss1; /* private dss1 data */
#endif /* CONFIG_HISAX_EURO */              
#ifdef CONFIG_HISAX_NI1
           ni1_stk_priv ni1; /* private ni1 data */
#endif /* CONFIG_HISAX_NI1 */             
	 } prot;
};

struct l3_process {
	int callref;
	int state;
	struct L3Timer timer;
	int N303;
	int debug;
	struct Param para;
	struct Channel *chan;
	struct PStack *st;
	struct l3_process *next;
        ulong redir_result;

        /* protocol specific data fields */
        union 
	 { u_char uuuu; /* only when euro not defined, avoiding empty union */
#ifdef CONFIG_HISAX_EURO 
           dss1_proc_priv dss1; /* private dss1 data */
#endif /* CONFIG_HISAX_EURO */            
#ifdef CONFIG_HISAX_NI1
           ni1_proc_priv ni1; /* private ni1 data */
#endif /* CONFIG_HISAX_NI1 */             
	 } prot;
};

struct hscx_hw {
	int hscx;
	int rcvidx;
	int count;              /* Current skb sent count */
	u_char *rcvbuf;         /* B-Channel receive Buffer */
	u_char tsaxr0;
	u_char tsaxr1;
};

struct w6692B_hw {
	int bchan;
	int rcvidx;
	int count;              /* Current skb sent count */
	u_char *rcvbuf;         /* B-Channel receive Buffer */
};

struct isar_reg {
	unsigned long Flags;
	volatile u_char bstat;
	volatile u_char iis;
	volatile u_char cmsb;
	volatile u_char clsb;
	volatile u_char par[8];
};

struct isar_hw {
	int dpath;
	int rcvidx;
	int txcnt;
	int mml;
	u_char state;
	u_char cmd;
	u_char mod;
	u_char newcmd;
	u_char newmod;
	char try_mod;
	struct timer_list ftimer;
	u_char *rcvbuf;         /* B-Channel receive Buffer */
	u_char conmsg[16];
	struct isar_reg *reg;
};

struct hdlc_stat_reg {
#ifdef __BIG_ENDIAN
	u_char fill;
	u_char mode;
	u_char xml;
	u_char cmd;
#else
	u_char cmd;
	u_char xml;
	u_char mode;
	u_char fill;
#endif
} __attribute__((packed));

struct hdlc_hw {
	union {
		u_int ctrl;
		struct hdlc_stat_reg sr;
	} ctrl;
	u_int stat;
	int rcvidx;
	int count;              /* Current skb sent count */
	u_char *rcvbuf;         /* B-Channel receive Buffer */
};

struct hfcB_hw {
	unsigned int *send;
	int f1;
	int f2;
};

struct tiger_hw {
	u_int *send;
	u_int *s_irq;
	u_int *s_end;
	u_int *sendp;
	u_int *rec;
	int free;
	u_char *rcvbuf;
	u_char *sendbuf;
	u_char *sp;
	int sendcnt;
	u_int s_tot;
	u_int r_bitcnt;
	u_int r_tot;
	u_int r_err;
	u_int r_fcs;
	u_char r_state;
	u_char r_one;
	u_char r_val;
	u_char s_state;
};

struct amd7930_hw {
	u_char *tx_buff;
	u_char *rv_buff;
	int rv_buff_in;
	int rv_buff_out;
	struct sk_buff *rv_skb;
	struct hdlc_state *hdlc_state;
	struct work_struct tq_rcv;
	struct work_struct tq_xmt;
};

#define BC_FLG_INIT	1
#define BC_FLG_ACTIV	2
#define BC_FLG_BUSY	3
#define BC_FLG_NOFRAME	4
#define BC_FLG_HALF	5
#define BC_FLG_EMPTY	6
#define BC_FLG_ORIG	7
#define BC_FLG_DLEETX	8
#define BC_FLG_LASTDLE	9
#define BC_FLG_FIRST	10
#define BC_FLG_LASTDATA	11
#define BC_FLG_NMD_DATA	12
#define BC_FLG_FTI_RUN	13
#define BC_FLG_LL_OK	14
#define BC_FLG_LL_CONN	15
#define BC_FLG_FTI_FTS	16
#define BC_FLG_FRH_WAIT	17

#define L1_MODE_NULL	0
#define L1_MODE_TRANS	1
#define L1_MODE_HDLC	2
#define L1_MODE_EXTRN	3
#define L1_MODE_HDLC_56K 4
#define L1_MODE_MODEM	7
#define L1_MODE_V32	8
#define L1_MODE_FAX	9

struct BCState {
	int channel;
	int mode;
	u_long Flag;
	struct IsdnCardState *cs;
	int tx_cnt;		/* B-Channel transmit counter */
	struct sk_buff *tx_skb; /* B-Channel transmit Buffer */
	struct sk_buff_head rqueue;	/* B-Channel receive Queue */
	struct sk_buff_head squeue;	/* B-Channel send Queue */
	int ackcnt;
	spinlock_t aclock;
	struct PStack *st;
	u_char *blog;
	u_char *conmsg;
	struct timer_list transbusy;
	struct work_struct tqueue;
	u_long event;
	int  (*BC_SetStack) (struct PStack *, struct BCState *);
	void (*BC_Close) (struct BCState *);
#ifdef ERROR_STATISTIC
	int err_crc;
	int err_tx;
	int err_rdo;
	int err_inv;
#endif
	union {
		struct hscx_hw hscx;
		struct hdlc_hw hdlc;
		struct isar_hw isar;
		struct hfcB_hw hfc;
		struct tiger_hw tiger;
		struct amd7930_hw  amd7930;
		struct w6692B_hw w6692;
		struct hisax_b_if *b_if;
	} hw;
};

struct Channel {
	struct PStack *b_st, *d_st;
	struct IsdnCardState *cs;
	struct BCState *bcs;
	int chan;
	int incoming;
	struct FsmInst fi;
	struct FsmTimer drel_timer, dial_timer;
	int debug;
	int l2_protocol, l2_active_protocol;
	int l3_protocol;
	int data_open;
	struct l3_process *proc;
	setup_parm setup;	/* from isdnif.h numbers and Serviceindicator */
	u_long Flags;		/* for remembering action done in l4 */
	int leased;
};

struct elsa_hw {
	struct pci_dev *dev;
	unsigned long base;
	unsigned int cfg;
	unsigned int ctrl;
	unsigned int ale;
	unsigned int isac;
	unsigned int itac;
	unsigned int hscx;
	unsigned int trig;
	unsigned int timer;
	unsigned int counter;
	unsigned int status;
	struct timer_list tl;
	unsigned int MFlag;
	struct BCState *bcs;
	u_char *transbuf;
	u_char *rcvbuf;
	unsigned int transp;
	unsigned int rcvp;
	unsigned int transcnt;
	unsigned int rcvcnt;
	u_char IER;
	u_char FCR;
	u_char LCR;
	u_char MCR;
	u_char ctrl_reg;
};

struct teles3_hw {
	unsigned int cfg_reg;
	signed   int isac;
	signed   int hscx[2];
	signed   int isacfifo;
	signed   int hscxfifo[2];
};

struct teles0_hw {
	unsigned int cfg_reg;
	void __iomem *membase;
	unsigned long phymem;
};

struct avm_hw {
	unsigned int cfg_reg;
	unsigned int isac;
	unsigned int hscx[2];
	unsigned int isacfifo;
	unsigned int hscxfifo[2];
	unsigned int counter;
	struct pci_dev *dev;
};

struct ix1_hw {
	unsigned int cfg_reg;
	unsigned int isac_ale;
	unsigned int isac;
	unsigned int hscx_ale;
	unsigned int hscx;
};

struct diva_hw {
	unsigned long cfg_reg;
	unsigned long pci_cfg;
	unsigned int ctrl;
	unsigned long isac_adr;
	unsigned int isac;
	unsigned long hscx_adr;
	unsigned int hscx;
	unsigned int status;
	struct timer_list tl;
	u_char ctrl_reg;
	struct pci_dev *dev;
};

struct asus_hw {
	unsigned int cfg_reg;
	unsigned int adr;
	unsigned int isac;
	unsigned int hscx;
	unsigned int u7;
	unsigned int pots;
};


struct hfc_hw {
	unsigned int addr;
	unsigned int fifosize;
	unsigned char cirm;
	unsigned char ctmt;
	unsigned char cip;
	u_char isac_spcr;
	struct timer_list timer;
};	

struct sedl_hw {
	unsigned int cfg_reg;
	unsigned int adr;
	unsigned int isac;
	unsigned int hscx;
	unsigned int reset_on;
	unsigned int reset_off;
	struct isar_reg isar;
	unsigned int chip;
	unsigned int bus;
	struct pci_dev *dev;
};

struct spt_hw {
	unsigned int cfg_reg;
	unsigned int isac;
	unsigned int hscx[2];
	unsigned char res_irq;
};

struct mic_hw {
	unsigned int cfg_reg;
	unsigned int adr;
	unsigned int isac;
	unsigned int hscx;
};

struct njet_hw {
	unsigned long base;
	unsigned int isac;
	unsigned int auxa;
	unsigned char auxd;
	unsigned char dmactrl;
	unsigned char ctrl_reg;
	unsigned char irqmask0;
	unsigned char irqstat0;
	unsigned char last_is0;
	struct pci_dev *dev;
};

struct hfcPCI_hw {
	unsigned char cirm;
	unsigned char ctmt;
	unsigned char conn;
	unsigned char mst_m;
	unsigned char int_m1;
	unsigned char int_m2;
	unsigned char int_s1;
	unsigned char sctrl;
        unsigned char sctrl_r;
        unsigned char sctrl_e;
        unsigned char trm;
	unsigned char stat;
	unsigned char fifo;
        unsigned char fifo_en;
        unsigned char bswapped;
        unsigned char nt_mode;
        int nt_timer;
        struct pci_dev *dev;
        unsigned char *pci_io; /* start of PCI IO memory */
        void *share_start; /* shared memory for Fifos start */
        void *fifos; /* FIFO memory */ 
        int last_bfifo_cnt[2]; /* marker saving last b-fifo frame count */
	struct timer_list timer;
};

struct hfcSX_hw {
        unsigned long base;
	unsigned char cirm;
	unsigned char ctmt;
	unsigned char conn;
	unsigned char mst_m;
	unsigned char int_m1;
	unsigned char int_m2;
	unsigned char int_s1;
	unsigned char sctrl;
        unsigned char sctrl_r;
        unsigned char sctrl_e;
        unsigned char trm;
	unsigned char stat;
	unsigned char fifo;
        unsigned char bswapped;
        unsigned char nt_mode;
        unsigned char chip;
        int b_fifo_size;
        unsigned char last_fifo;
        void *extra;
        int nt_timer;
	struct timer_list timer;
};

struct hfcD_hw {
	unsigned int addr;
	unsigned int bfifosize;
	unsigned int dfifosize;
	unsigned char cirm;
	unsigned char ctmt;
	unsigned char cip;
	unsigned char conn;
	unsigned char mst_m;
	unsigned char int_m1;
	unsigned char int_m2;
	unsigned char int_s1;
	unsigned char sctrl;
	unsigned char stat;
	unsigned char fifo;
	unsigned char f1;
	unsigned char f2;
	unsigned int *send;
	struct timer_list timer;
};

struct isurf_hw {
	unsigned int reset;
	unsigned long phymem;
	void __iomem *isac;
	void __iomem *isar;
	struct isar_reg isar_r;
};

struct saphir_hw {
	struct pci_dev *dev;
	unsigned int cfg_reg;
	unsigned int ale;
	unsigned int isac;
	unsigned int hscx;
	struct timer_list timer;
};

struct bkm_hw {
	struct pci_dev *dev;
	unsigned long base;
	/* A4T stuff */
	unsigned long isac_adr;
	unsigned int isac_ale;
	unsigned long jade_adr;
	unsigned int jade_ale;
	/* Scitel Quadro stuff */
	unsigned long plx_adr;
	unsigned long data_adr;
};	

struct gazel_hw {
	struct pci_dev *dev;
	unsigned int cfg_reg;
	unsigned int pciaddr[2];
        signed   int ipac;
	signed   int isac;
	signed   int hscx[2];
	signed   int isacfifo;
	signed   int hscxfifo[2];
	unsigned char timeslot;
	unsigned char iom2;
};

struct w6692_hw {
	struct pci_dev *dev;
	unsigned int iobase;
	struct timer_list timer;
};

#ifdef  CONFIG_HISAX_TESTEMU
struct te_hw {
	unsigned char *sfifo;
	unsigned char *sfifo_w;
	unsigned char *sfifo_r;
	unsigned char *sfifo_e;
	int sfifo_cnt;
	unsigned int stat;
	wait_queue_head_t rwaitq;
	wait_queue_head_t swaitq;
};
#endif

struct arcofi_msg {
	struct arcofi_msg *next;
	u_char receive;
	u_char len;
	u_char msg[10];
};

struct isac_chip {
	int ph_state;
	u_char *mon_tx;
	u_char *mon_rx;
	int mon_txp;
	int mon_txc;
	int mon_rxp;
	struct arcofi_msg *arcofi_list;
	struct timer_list arcofitimer;
	wait_queue_head_t arcofi_wait;
	u_char arcofi_bc;
	u_char arcofi_state;
	u_char mocr;
	u_char adf2;
};

struct hfcd_chip {
	int ph_state;
};

struct hfcpci_chip {
	int ph_state;
};

struct hfcsx_chip {
	int ph_state;
};

struct w6692_chip {
	int ph_state;
};

struct amd7930_chip {
	u_char lmr1;
	u_char ph_state;
	u_char old_state;
	u_char flg_t3;
	unsigned int tx_xmtlen;
	struct timer_list timer3;
	void (*ph_command) (struct IsdnCardState *, u_char, char *);
	void (*setIrqMask) (struct IsdnCardState *, u_char);
};

struct icc_chip {
	int ph_state;
	u_char *mon_tx;
	u_char *mon_rx;
	int mon_txp;
	int mon_txc;
	int mon_rxp;
	struct arcofi_msg *arcofi_list;
	struct timer_list arcofitimer;
	wait_queue_head_t arcofi_wait;
	u_char arcofi_bc;
	u_char arcofi_state;
	u_char mocr;
	u_char adf2;
};

#define HW_IOM1			0
#define HW_IPAC			1
#define HW_ISAR			2
#define HW_ARCOFI		3
#define FLG_TWO_DCHAN		4
#define FLG_L1_DBUSY		5
#define FLG_DBUSY_TIMER 	6
#define FLG_LOCK_ATOMIC 	7
#define FLG_ARCOFI_TIMER	8
#define FLG_ARCOFI_ERROR	9
#define FLG_HW_L1_UINT		10

struct IsdnCardState {
	spinlock_t	lock;
	u_char		typ;
	u_char		subtyp;
	int		protocol;
	u_int		irq;
	u_long		irq_flags;
	u_long		HW_Flags;
	int		*busy_flag;
        int		chanlimit; /* limited number of B-chans to use */
        int		logecho; /* log echo if supported by card */
	union {
		struct elsa_hw elsa;
		struct teles0_hw teles0;
		struct teles3_hw teles3;
		struct avm_hw avm;
		struct ix1_hw ix1;
		struct diva_hw diva;
		struct asus_hw asus;
		struct hfc_hw hfc;
		struct sedl_hw sedl;
		struct spt_hw spt;
		struct mic_hw mic;
		struct njet_hw njet;
		struct hfcD_hw hfcD;
		struct hfcPCI_hw hfcpci;
		struct hfcSX_hw hfcsx;
		struct ix1_hw niccy;
		struct isurf_hw isurf;
		struct saphir_hw saphir;
#ifdef CONFIG_HISAX_TESTEMU
		struct te_hw te;
#endif
		struct bkm_hw ax;
		struct gazel_hw gazel;
		struct w6692_hw w6692;
		struct hisax_d_if *hisax_d_if;
	} hw;
	int		myid;
	isdn_if		iif;
	spinlock_t	statlock;
	u_char		*status_buf;
	u_char		*status_read;
	u_char		*status_write;
	u_char		*status_end;
	u_char		(*readisac) (struct IsdnCardState *, u_char);
	void		(*writeisac) (struct IsdnCardState *, u_char, u_char);
	void		(*readisacfifo) (struct IsdnCardState *, u_char *, int);
	void		(*writeisacfifo) (struct IsdnCardState *, u_char *, int);
	u_char		(*BC_Read_Reg) (struct IsdnCardState *, int, u_char);
	void		(*BC_Write_Reg) (struct IsdnCardState *, int, u_char, u_char);
	void		(*BC_Send_Data) (struct BCState *);
	int		(*cardmsg) (struct IsdnCardState *, int, void *);
	void		(*setstack_d) (struct PStack *, struct IsdnCardState *);
	void		(*DC_Close) (struct IsdnCardState *);
	int		(*irq_func) (int, void *, struct pt_regs *);
	int		(*auxcmd) (struct IsdnCardState *, isdn_ctrl *);
	struct Channel	channel[2+MAX_WAITING_CALLS];
	struct BCState	bcs[2+MAX_WAITING_CALLS];
	struct PStack	*stlist;
	struct sk_buff_head rq, sq; /* D-channel queues */
	int		cardnr;
	char		*dlog;
	int		debug;
	union {
		struct isac_chip isac;
		struct hfcd_chip hfcd;
		struct hfcpci_chip hfcpci;
		struct hfcsx_chip hfcsx;
		struct w6692_chip w6692;
		struct amd7930_chip amd7930;
		struct icc_chip icc;
	} dc;
	u_char		*rcvbuf;
	int		rcvidx;
	struct sk_buff	*tx_skb;
	int		tx_cnt;
	u_long		event;
	struct work_struct tqueue;
	struct timer_list dbusytimer;
#ifdef ERROR_STATISTIC
	int		err_crc;
	int		err_tx;
	int		err_rx;
#endif
};


#define  schedule_event(s, ev)	do {test_and_set_bit(ev, &s->event);schedule_work(&s->tqueue); } while(0)

#define  MON0_RX	1
#define  MON1_RX	2
#define  MON0_TX	4
#define  MON1_TX	8


#ifdef ISDN_CHIP_ISAC
#undef ISDN_CHIP_ISAC
#endif

#ifdef	CONFIG_HISAX_16_0
#define  CARD_TELES0 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define  CARD_TELES0  0
#endif

#ifdef	CONFIG_HISAX_16_3
#define  CARD_TELES3 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define  CARD_TELES3  0
#endif

#ifdef	CONFIG_HISAX_TELESPCI
#define  CARD_TELESPCI 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define  CARD_TELESPCI  0
#endif

#ifdef	CONFIG_HISAX_AVM_A1
#define  CARD_AVM_A1 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define  CARD_AVM_A1  0
#endif

#ifdef	CONFIG_HISAX_AVM_A1_PCMCIA
#define  CARD_AVM_A1_PCMCIA 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define  CARD_AVM_A1_PCMCIA  0
#endif

#ifdef	CONFIG_HISAX_FRITZPCI
#define  CARD_FRITZPCI 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define  CARD_FRITZPCI  0
#endif

#ifdef	CONFIG_HISAX_ELSA
#define  CARD_ELSA 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define  CARD_ELSA  0
#endif

#ifdef	CONFIG_HISAX_IX1MICROR2
#define	CARD_IX1MICROR2 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_IX1MICROR2 0
#endif

#ifdef  CONFIG_HISAX_DIEHLDIVA
#define CARD_DIEHLDIVA 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_DIEHLDIVA 0
#endif

#ifdef  CONFIG_HISAX_ASUSCOM
#define CARD_ASUSCOM 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_ASUSCOM 0
#endif

#ifdef  CONFIG_HISAX_TELEINT
#define CARD_TELEINT 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_TELEINT 0
#endif

#ifdef  CONFIG_HISAX_SEDLBAUER
#define CARD_SEDLBAUER 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_SEDLBAUER 0
#endif

#ifdef  CONFIG_HISAX_SPORTSTER
#define CARD_SPORTSTER 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_SPORTSTER 0
#endif

#ifdef  CONFIG_HISAX_MIC
#define CARD_MIC 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_MIC 0
#endif

#ifdef  CONFIG_HISAX_NETJET
#define CARD_NETJET_S 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_NETJET_S 0
#endif

#ifdef	CONFIG_HISAX_HFCS
#define  CARD_HFCS 1
#else
#define  CARD_HFCS 0
#endif

#ifdef	CONFIG_HISAX_HFC_PCI
#define  CARD_HFC_PCI 1
#else
#define  CARD_HFC_PCI 0
#endif

#ifdef	CONFIG_HISAX_HFC_SX
#define  CARD_HFC_SX 1
#else
#define  CARD_HFC_SX 0
#endif

#ifdef  CONFIG_HISAX_AMD7930
#define CARD_AMD7930 1
#else
#define CARD_AMD7930 0
#endif

#ifdef	CONFIG_HISAX_NICCY
#define	CARD_NICCY 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_NICCY 0
#endif

#ifdef	CONFIG_HISAX_ISURF
#define	CARD_ISURF 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_ISURF 0
#endif

#ifdef	CONFIG_HISAX_S0BOX
#define	CARD_S0BOX 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_S0BOX 0
#endif

#ifdef	CONFIG_HISAX_HSTSAPHIR
#define	CARD_HSTSAPHIR 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_HSTSAPHIR 0
#endif

#ifdef	CONFIG_HISAX_TESTEMU
#define	CARD_TESTEMU 1
#define ISDN_CTYPE_TESTEMU 99
#undef ISDN_CTYPE_COUNT
#define  ISDN_CTYPE_COUNT ISDN_CTYPE_TESTEMU
#else
#define CARD_TESTEMU 0
#endif

#ifdef	CONFIG_HISAX_BKM_A4T
#define	CARD_BKM_A4T 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_BKM_A4T 0
#endif

#ifdef	CONFIG_HISAX_SCT_QUADRO
#define	CARD_SCT_QUADRO 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define CARD_SCT_QUADRO 0
#endif

#ifdef	CONFIG_HISAX_GAZEL
#define  CARD_GAZEL 1
#ifndef ISDN_CHIP_ISAC
#define ISDN_CHIP_ISAC 1
#endif
#else
#define  CARD_GAZEL  0
#endif

#ifdef	CONFIG_HISAX_W6692
#define	CARD_W6692	1
#ifndef	ISDN_CHIP_W6692
#define	ISDN_CHIP_W6692	1
#endif
#else
#define	CARD_W6692	0
#endif

#ifdef  CONFIG_HISAX_NETJET_U
#define CARD_NETJET_U 1
#ifndef ISDN_CHIP_ICC
#define ISDN_CHIP_ICC 1
#endif
#ifndef HISAX_UINTERFACE
#define HISAX_UINTERFACE 1
#endif
#else
#define CARD_NETJET_U 0
#endif

#ifdef CONFIG_HISAX_ENTERNOW_PCI
#define CARD_FN_ENTERNOW_PCI 1
#else
#define CARD_FN_ENTERNOW_PCI 0
#endif

#define TEI_PER_CARD 1

/* L1 Debug */
#define	L1_DEB_WARN		0x01
#define	L1_DEB_INTSTAT		0x02
#define	L1_DEB_ISAC		0x04
#define	L1_DEB_ISAC_FIFO	0x08
#define	L1_DEB_HSCX		0x10
#define	L1_DEB_HSCX_FIFO	0x20
#define	L1_DEB_LAPD	        0x40
#define	L1_DEB_IPAC	        0x80
#define	L1_DEB_RECEIVE_FRAME    0x100
#define L1_DEB_MONITOR		0x200
#define DEB_DLOG_HEX		0x400
#define DEB_DLOG_VERBOSE	0x800

#define L2FRAME_DEBUG

#ifdef L2FRAME_DEBUG
extern void Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir);
#endif

#include "hisax_cfg.h"

void init_bcstate(struct IsdnCardState *cs, int bc);

void setstack_HiSax(struct PStack *st, struct IsdnCardState *cs);
void HiSax_addlist(struct IsdnCardState *sp, struct PStack *st);
void HiSax_rmlist(struct IsdnCardState *sp, struct PStack *st);

void setstack_l1_B(struct PStack *st);

void setstack_tei(struct PStack *st);
void setstack_manager(struct PStack *st);

void setstack_isdnl2(struct PStack *st, char *debug_id);
void releasestack_isdnl2(struct PStack *st);
void setstack_transl2(struct PStack *st);
void releasestack_transl2(struct PStack *st);
void lli_writewakeup(struct PStack *st, int len);

void setstack_l3dc(struct PStack *st, struct Channel *chanp);
void setstack_l3bc(struct PStack *st, struct Channel *chanp);
void releasestack_isdnl3(struct PStack *st);

u_char *findie(u_char * p, int size, u_char ie, int wanted_set);
int getcallref(u_char * p);
int newcallref(void);

int FsmNew(struct Fsm *fsm, struct FsmNode *fnlist, int fncount);
void FsmFree(struct Fsm *fsm);
int FsmEvent(struct FsmInst *fi, int event, void *arg);
void FsmChangeState(struct FsmInst *fi, int newstate);
void FsmInitTimer(struct FsmInst *fi, struct FsmTimer *ft);
int FsmAddTimer(struct FsmTimer *ft, int millisec, int event,
	void *arg, int where);
void FsmRestartTimer(struct FsmTimer *ft, int millisec, int event,
	void *arg, int where);
void FsmDelTimer(struct FsmTimer *ft, int where);
int jiftime(char *s, long mark);

int HiSax_command(isdn_ctrl * ic);
int HiSax_writebuf_skb(int id, int chan, int ack, struct sk_buff *skb);
void HiSax_putstatus(struct IsdnCardState *cs, char *head, char *fmt, ...);
void VHiSax_putstatus(struct IsdnCardState *cs, char *head, char *fmt, va_list args);
void HiSax_reportcard(int cardnr, int sel);
int QuickHex(char *txt, u_char * p, int cnt);
void LogFrame(struct IsdnCardState *cs, u_char * p, int size);
void dlogframe(struct IsdnCardState *cs, struct sk_buff *skb, int dir);
void iecpy(u_char * dest, u_char * iestart, int ieoffset);
#endif	/* __KERNEL__ */

#define HZDELAY(jiffs) {int tout = jiffs; while (tout--) udelay(1000000/HZ);}

int ll_run(struct IsdnCardState *cs, int addfeatures);
int CallcNew(void);
void CallcFree(void);
int CallcNewChan(struct IsdnCardState *cs);
void CallcFreeChan(struct IsdnCardState *cs);
int Isdnl1New(void);
void Isdnl1Free(void);
int Isdnl2New(void);
void Isdnl2Free(void);
int Isdnl3New(void);
void Isdnl3Free(void);
void init_tei(struct IsdnCardState *cs, int protocol);
void release_tei(struct IsdnCardState *cs);
char *HiSax_getrev(const char *revision);
int TeiNew(void);
void TeiFree(void);
