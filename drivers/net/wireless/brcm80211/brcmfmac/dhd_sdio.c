/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/printk.h>
#include <linux/pci_ids.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/semaphore.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/bcma/bcma.h>
#include <asm/unaligned.h>
#include <defs.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include <brcm_hw_ids.h>
#include <soc.h>
#include "sdio_host.h"
#include "sdio_chip.h"

#define DCMD_RESP_TIMEOUT  2000	/* In milli second */

#ifdef DEBUG

#define BRCMF_TRAP_INFO_SIZE	80

#define CBUF_LEN	(128)

struct rte_log_le {
	__le32 buf;		/* Can't be pointer on (64-bit) hosts */
	__le32 buf_size;
	__le32 idx;
	char *_buf_compat;	/* Redundant pointer for backward compat. */
};

struct rte_console {
	/* Virtual UART
	 * When there is no UART (e.g. Quickturn),
	 * the host should write a complete
	 * input line directly into cbuf and then write
	 * the length into vcons_in.
	 * This may also be used when there is a real UART
	 * (at risk of conflicting with
	 * the real UART).  vcons_out is currently unused.
	 */
	uint vcons_in;
	uint vcons_out;

	/* Output (logging) buffer
	 * Console output is written to a ring buffer log_buf at index log_idx.
	 * The host may read the output when it sees log_idx advance.
	 * Output will be lost if the output wraps around faster than the host
	 * polls.
	 */
	struct rte_log_le log_le;

	/* Console input line buffer
	 * Characters are read one at a time into cbuf
	 * until <CR> is received, then
	 * the buffer is processed as a command line.
	 * Also used for virtual UART.
	 */
	uint cbuf_idx;
	char cbuf[CBUF_LEN];
};

#endif				/* DEBUG */
#include <chipcommon.h>

#include "dhd_bus.h"
#include "dhd_dbg.h"

#define TXQLEN		2048	/* bulk tx queue length */
#define TXHI		(TXQLEN - 256)	/* turn on flow control above TXHI */
#define TXLOW		(TXHI - 256)	/* turn off flow control below TXLOW */
#define PRIOMASK	7

#define TXRETRIES	2	/* # of retries for tx frames */

#define BRCMF_RXBOUND	50	/* Default for max rx frames in
				 one scheduling */

#define BRCMF_TXBOUND	20	/* Default for max tx frames in
				 one scheduling */

#define BRCMF_TXMINMAX	1	/* Max tx frames if rx still pending */

#define MEMBLOCK	2048	/* Block size used for downloading
				 of dongle image */
#define MAX_DATA_BUF	(32 * 1024)	/* Must be large enough to hold
				 biggest possible glom */

#define BRCMF_FIRSTREAD	(1 << 6)


/* SBSDIO_DEVICE_CTL */

/* 1: device will assert busy signal when receiving CMD53 */
#define SBSDIO_DEVCTL_SETBUSY		0x01
/* 1: assertion of sdio interrupt is synchronous to the sdio clock */
#define SBSDIO_DEVCTL_SPI_INTR_SYNC	0x02
/* 1: mask all interrupts to host except the chipActive (rev 8) */
#define SBSDIO_DEVCTL_CA_INT_ONLY	0x04
/* 1: isolate internal sdio signals, put external pads in tri-state; requires
 * sdio bus power cycle to clear (rev 9) */
#define SBSDIO_DEVCTL_PADS_ISO		0x08
/* Force SD->SB reset mapping (rev 11) */
#define SBSDIO_DEVCTL_SB_RST_CTL	0x30
/*   Determined by CoreControl bit */
#define SBSDIO_DEVCTL_RST_CORECTL	0x00
/*   Force backplane reset */
#define SBSDIO_DEVCTL_RST_BPRESET	0x10
/*   Force no backplane reset */
#define SBSDIO_DEVCTL_RST_NOBPRESET	0x20

/* direct(mapped) cis space */

/* MAPPED common CIS address */
#define SBSDIO_CIS_BASE_COMMON		0x1000
/* maximum bytes in one CIS */
#define SBSDIO_CIS_SIZE_LIMIT		0x200
/* cis offset addr is < 17 bits */
#define SBSDIO_CIS_OFT_ADDR_MASK	0x1FFFF

/* manfid tuple length, include tuple, link bytes */
#define SBSDIO_CIS_MANFID_TUPLE_LEN	6

/* intstatus */
#define I_SMB_SW0	(1 << 0)	/* To SB Mail S/W interrupt 0 */
#define I_SMB_SW1	(1 << 1)	/* To SB Mail S/W interrupt 1 */
#define I_SMB_SW2	(1 << 2)	/* To SB Mail S/W interrupt 2 */
#define I_SMB_SW3	(1 << 3)	/* To SB Mail S/W interrupt 3 */
#define I_SMB_SW_MASK	0x0000000f	/* To SB Mail S/W interrupts mask */
#define I_SMB_SW_SHIFT	0	/* To SB Mail S/W interrupts shift */
#define I_HMB_SW0	(1 << 4)	/* To Host Mail S/W interrupt 0 */
#define I_HMB_SW1	(1 << 5)	/* To Host Mail S/W interrupt 1 */
#define I_HMB_SW2	(1 << 6)	/* To Host Mail S/W interrupt 2 */
#define I_HMB_SW3	(1 << 7)	/* To Host Mail S/W interrupt 3 */
#define I_HMB_SW_MASK	0x000000f0	/* To Host Mail S/W interrupts mask */
#define I_HMB_SW_SHIFT	4	/* To Host Mail S/W interrupts shift */
#define I_WR_OOSYNC	(1 << 8)	/* Write Frame Out Of Sync */
#define I_RD_OOSYNC	(1 << 9)	/* Read Frame Out Of Sync */
#define	I_PC		(1 << 10)	/* descriptor error */
#define	I_PD		(1 << 11)	/* data error */
#define	I_DE		(1 << 12)	/* Descriptor protocol Error */
#define	I_RU		(1 << 13)	/* Receive descriptor Underflow */
#define	I_RO		(1 << 14)	/* Receive fifo Overflow */
#define	I_XU		(1 << 15)	/* Transmit fifo Underflow */
#define	I_RI		(1 << 16)	/* Receive Interrupt */
#define I_BUSPWR	(1 << 17)	/* SDIO Bus Power Change (rev 9) */
#define I_XMTDATA_AVAIL (1 << 23)	/* bits in fifo */
#define	I_XI		(1 << 24)	/* Transmit Interrupt */
#define I_RF_TERM	(1 << 25)	/* Read Frame Terminate */
#define I_WF_TERM	(1 << 26)	/* Write Frame Terminate */
#define I_PCMCIA_XU	(1 << 27)	/* PCMCIA Transmit FIFO Underflow */
#define I_SBINT		(1 << 28)	/* sbintstatus Interrupt */
#define I_CHIPACTIVE	(1 << 29)	/* chip from doze to active state */
#define I_SRESET	(1 << 30)	/* CCCR RES interrupt */
#define I_IOE2		(1U << 31)	/* CCCR IOE2 Bit Changed */
#define	I_ERRORS	(I_PC | I_PD | I_DE | I_RU | I_RO | I_XU)
#define I_DMA		(I_RI | I_XI | I_ERRORS)

/* corecontrol */
#define CC_CISRDY		(1 << 0)	/* CIS Ready */
#define CC_BPRESEN		(1 << 1)	/* CCCR RES signal */
#define CC_F2RDY		(1 << 2)	/* set CCCR IOR2 bit */
#define CC_CLRPADSISO		(1 << 3)	/* clear SDIO pads isolation */
#define CC_XMTDATAAVAIL_MODE	(1 << 4)
#define CC_XMTDATAAVAIL_CTRL	(1 << 5)

/* SDA_FRAMECTRL */
#define SFC_RF_TERM	(1 << 0)	/* Read Frame Terminate */
#define SFC_WF_TERM	(1 << 1)	/* Write Frame Terminate */
#define SFC_CRC4WOOS	(1 << 2)	/* CRC error for write out of sync */
#define SFC_ABORTALL	(1 << 3)	/* Abort all in-progress frames */

/* HW frame tag */
#define SDPCM_FRAMETAG_LEN	4	/* 2 bytes len, 2 bytes check val */

/* Total length of frame header for dongle protocol */
#define SDPCM_HDRLEN	(SDPCM_FRAMETAG_LEN + SDPCM_SWHEADER_LEN)
#define SDPCM_RESERVE	(SDPCM_HDRLEN + BRCMF_SDALIGN)

/*
 * Software allocation of To SB Mailbox resources
 */

/* tosbmailbox bits corresponding to intstatus bits */
#define SMB_NAK		(1 << 0)	/* Frame NAK */
#define SMB_INT_ACK	(1 << 1)	/* Host Interrupt ACK */
#define SMB_USE_OOB	(1 << 2)	/* Use OOB Wakeup */
#define SMB_DEV_INT	(1 << 3)	/* Miscellaneous Interrupt */

/* tosbmailboxdata */
#define SMB_DATA_VERSION_SHIFT	16	/* host protocol version */

/*
 * Software allocation of To Host Mailbox resources
 */

/* intstatus bits */
#define I_HMB_FC_STATE	I_HMB_SW0	/* Flow Control State */
#define I_HMB_FC_CHANGE	I_HMB_SW1	/* Flow Control State Changed */
#define I_HMB_FRAME_IND	I_HMB_SW2	/* Frame Indication */
#define I_HMB_HOST_INT	I_HMB_SW3	/* Miscellaneous Interrupt */

/* tohostmailboxdata */
#define HMB_DATA_NAKHANDLED	1	/* retransmit NAK'd frame */
#define HMB_DATA_DEVREADY	2	/* talk to host after enable */
#define HMB_DATA_FC		4	/* per prio flowcontrol update flag */
#define HMB_DATA_FWREADY	8	/* fw ready for protocol activity */

#define HMB_DATA_FCDATA_MASK	0xff000000
#define HMB_DATA_FCDATA_SHIFT	24

#define HMB_DATA_VERSION_MASK	0x00ff0000
#define HMB_DATA_VERSION_SHIFT	16

/*
 * Software-defined protocol header
 */

/* Current protocol version */
#define SDPCM_PROT_VERSION	4

/* SW frame header */
#define SDPCM_PACKET_SEQUENCE(p)	(((u8 *)p)[0] & 0xff)

#define SDPCM_CHANNEL_MASK		0x00000f00
#define SDPCM_CHANNEL_SHIFT		8
#define SDPCM_PACKET_CHANNEL(p)		(((u8 *)p)[1] & 0x0f)

#define SDPCM_NEXTLEN_OFFSET		2

/* Data Offset from SOF (HW Tag, SW Tag, Pad) */
#define SDPCM_DOFFSET_OFFSET		3	/* Data Offset */
#define SDPCM_DOFFSET_VALUE(p)		(((u8 *)p)[SDPCM_DOFFSET_OFFSET] & 0xff)
#define SDPCM_DOFFSET_MASK		0xff000000
#define SDPCM_DOFFSET_SHIFT		24
#define SDPCM_FCMASK_OFFSET		4	/* Flow control */
#define SDPCM_FCMASK_VALUE(p)		(((u8 *)p)[SDPCM_FCMASK_OFFSET] & 0xff)
#define SDPCM_WINDOW_OFFSET		5	/* Credit based fc */
#define SDPCM_WINDOW_VALUE(p)		(((u8 *)p)[SDPCM_WINDOW_OFFSET] & 0xff)

#define SDPCM_SWHEADER_LEN	8	/* SW header is 64 bits */

/* logical channel numbers */
#define SDPCM_CONTROL_CHANNEL	0	/* Control channel Id */
#define SDPCM_EVENT_CHANNEL	1	/* Asyc Event Indication Channel Id */
#define SDPCM_DATA_CHANNEL	2	/* Data Xmit/Recv Channel Id */
#define SDPCM_GLOM_CHANNEL	3	/* For coalesced packets */
#define SDPCM_TEST_CHANNEL	15	/* Reserved for test/debug packets */

#define SDPCM_SEQUENCE_WRAP	256	/* wrap-around val for 8bit frame seq */

#define SDPCM_GLOMDESC(p)	(((u8 *)p)[1] & 0x80)

/*
 * Shared structure between dongle and the host.
 * The structure contains pointers to trap or assert information.
 */
#define SDPCM_SHARED_VERSION       0x0002
#define SDPCM_SHARED_VERSION_MASK  0x00FF
#define SDPCM_SHARED_ASSERT_BUILT  0x0100
#define SDPCM_SHARED_ASSERT        0x0200
#define SDPCM_SHARED_TRAP          0x0400

/* Space for header read, limit for data packets */
#define MAX_HDR_READ	(1 << 6)
#define MAX_RX_DATASZ	2048

/* Maximum milliseconds to wait for F2 to come up */
#define BRCMF_WAIT_F2RDY	3000

/* Bump up limit on waiting for HT to account for first startup;
 * if the image is doing a CRC calculation before programming the PMU
 * for HT availability, it could take a couple hundred ms more, so
 * max out at a 1 second (1000000us).
 */
#undef PMU_MAX_TRANSITION_DLY
#define PMU_MAX_TRANSITION_DLY 1000000

/* Value for ChipClockCSR during initial setup */
#define BRCMF_INIT_CLKCTL1	(SBSDIO_FORCE_HW_CLKREQ_OFF |	\
					SBSDIO_ALP_AVAIL_REQ)

/* Flags for SDH calls */
#define F2SYNC	(SDIO_REQ_4BYTE | SDIO_REQ_FIXED)

#define BRCMF_SDIO_FW_NAME	"brcm/brcmfmac-sdio.bin"
#define BRCMF_SDIO_NV_NAME	"brcm/brcmfmac-sdio.txt"
MODULE_FIRMWARE(BRCMF_SDIO_FW_NAME);
MODULE_FIRMWARE(BRCMF_SDIO_NV_NAME);

#define BRCMF_IDLE_IMMEDIATE	(-1)	/* Enter idle immediately */
#define BRCMF_IDLE_ACTIVE	0	/* Do not request any SD clock change
					 * when idle
					 */
#define BRCMF_IDLE_INTERVAL	1

/*
 * Conversion of 802.1D priority to precedence level
 */
static uint prio2prec(u32 prio)
{
	return (prio == PRIO_8021D_NONE || prio == PRIO_8021D_BE) ?
	       (prio^2) : prio;
}

/* core registers */
struct sdpcmd_regs {
	u32 corecontrol;		/* 0x00, rev8 */
	u32 corestatus;			/* rev8 */
	u32 PAD[1];
	u32 biststatus;			/* rev8 */

	/* PCMCIA access */
	u16 pcmciamesportaladdr;	/* 0x010, rev8 */
	u16 PAD[1];
	u16 pcmciamesportalmask;	/* rev8 */
	u16 PAD[1];
	u16 pcmciawrframebc;		/* rev8 */
	u16 PAD[1];
	u16 pcmciaunderflowtimer;	/* rev8 */
	u16 PAD[1];

	/* interrupt */
	u32 intstatus;			/* 0x020, rev8 */
	u32 hostintmask;		/* rev8 */
	u32 intmask;			/* rev8 */
	u32 sbintstatus;		/* rev8 */
	u32 sbintmask;			/* rev8 */
	u32 funcintmask;		/* rev4 */
	u32 PAD[2];
	u32 tosbmailbox;		/* 0x040, rev8 */
	u32 tohostmailbox;		/* rev8 */
	u32 tosbmailboxdata;		/* rev8 */
	u32 tohostmailboxdata;		/* rev8 */

	/* synchronized access to registers in SDIO clock domain */
	u32 sdioaccess;			/* 0x050, rev8 */
	u32 PAD[3];

	/* PCMCIA frame control */
	u8 pcmciaframectrl;		/* 0x060, rev8 */
	u8 PAD[3];
	u8 pcmciawatermark;		/* rev8 */
	u8 PAD[155];

	/* interrupt batching control */
	u32 intrcvlazy;			/* 0x100, rev8 */
	u32 PAD[3];

	/* counters */
	u32 cmd52rd;			/* 0x110, rev8 */
	u32 cmd52wr;			/* rev8 */
	u32 cmd53rd;			/* rev8 */
	u32 cmd53wr;			/* rev8 */
	u32 abort;			/* rev8 */
	u32 datacrcerror;		/* rev8 */
	u32 rdoutofsync;		/* rev8 */
	u32 wroutofsync;		/* rev8 */
	u32 writebusy;			/* rev8 */
	u32 readwait;			/* rev8 */
	u32 readterm;			/* rev8 */
	u32 writeterm;			/* rev8 */
	u32 PAD[40];
	u32 clockctlstatus;		/* rev8 */
	u32 PAD[7];

	u32 PAD[128];			/* DMA engines */

	/* SDIO/PCMCIA CIS region */
	char cis[512];			/* 0x400-0x5ff, rev6 */

	/* PCMCIA function control registers */
	char pcmciafcr[256];		/* 0x600-6ff, rev6 */
	u16 PAD[55];

	/* PCMCIA backplane access */
	u16 backplanecsr;		/* 0x76E, rev6 */
	u16 backplaneaddr0;		/* rev6 */
	u16 backplaneaddr1;		/* rev6 */
	u16 backplaneaddr2;		/* rev6 */
	u16 backplaneaddr3;		/* rev6 */
	u16 backplanedata0;		/* rev6 */
	u16 backplanedata1;		/* rev6 */
	u16 backplanedata2;		/* rev6 */
	u16 backplanedata3;		/* rev6 */
	u16 PAD[31];

	/* sprom "size" & "blank" info */
	u16 spromstatus;		/* 0x7BE, rev2 */
	u32 PAD[464];

	u16 PAD[0x80];
};

#ifdef DEBUG
/* Device console log buffer state */
struct brcmf_console {
	uint count;		/* Poll interval msec counter */
	uint log_addr;		/* Log struct address (fixed) */
	struct rte_log_le log_le;	/* Log struct (host copy) */
	uint bufsize;		/* Size of log buffer */
	u8 *buf;		/* Log buffer (host copy) */
	uint last;		/* Last buffer read index */
};
#endif				/* DEBUG */

struct sdpcm_shared {
	u32 flags;
	u32 trap_addr;
	u32 assert_exp_addr;
	u32 assert_file_addr;
	u32 assert_line;
	u32 console_addr;	/* Address of struct rte_console */
	u32 msgtrace_addr;
	u8 tag[32];
};

struct sdpcm_shared_le {
	__le32 flags;
	__le32 trap_addr;
	__le32 assert_exp_addr;
	__le32 assert_file_addr;
	__le32 assert_line;
	__le32 console_addr;	/* Address of struct rte_console */
	__le32 msgtrace_addr;
	u8 tag[32];
};


/* misc chip info needed by some of the routines */
/* Private data for SDIO bus interaction */
struct brcmf_sdio {
	struct brcmf_sdio_dev *sdiodev;	/* sdio device handler */
	struct chip_info *ci;	/* Chip info struct */
	char *vars;		/* Variables (from CIS and/or other) */
	uint varsz;		/* Size of variables buffer */

	u32 ramsize;		/* Size of RAM in SOCRAM (bytes) */

	u32 hostintmask;	/* Copy of Host Interrupt Mask */
	u32 intstatus;	/* Intstatus bits (events) pending */
	bool dpc_sched;		/* Indicates DPC schedule (intrpt rcvd) */
	bool fcstate;		/* State of dongle flow-control */

	uint blocksize;		/* Block size of SDIO transfers */
	uint roundup;		/* Max roundup limit */

	struct pktq txq;	/* Queue length used for flow-control */
	u8 flowcontrol;	/* per prio flow control bitmask */
	u8 tx_seq;		/* Transmit sequence number (next) */
	u8 tx_max;		/* Maximum transmit sequence allowed */

	u8 hdrbuf[MAX_HDR_READ + BRCMF_SDALIGN];
	u8 *rxhdr;		/* Header of current rx frame (in hdrbuf) */
	u16 nextlen;		/* Next Read Len from last header */
	u8 rx_seq;		/* Receive sequence number (expected) */
	bool rxskip;		/* Skip receive (awaiting NAK ACK) */

	uint rxbound;		/* Rx frames to read before resched */
	uint txbound;		/* Tx frames to send before resched */
	uint txminmax;

	struct sk_buff *glomd;	/* Packet containing glomming descriptor */
	struct sk_buff_head glom; /* Packet list for glommed superframe */
	uint glomerr;		/* Glom packet read errors */

	u8 *rxbuf;		/* Buffer for receiving control packets */
	uint rxblen;		/* Allocated length of rxbuf */
	u8 *rxctl;		/* Aligned pointer into rxbuf */
	u8 *databuf;		/* Buffer for receiving big glom packet */
	u8 *dataptr;		/* Aligned pointer into databuf */
	uint rxlen;		/* Length of valid data in buffer */

	u8 sdpcm_ver;	/* Bus protocol reported by dongle */

	bool intr;		/* Use interrupts */
	bool poll;		/* Use polling */
	bool ipend;		/* Device interrupt is pending */
	uint intrcount;		/* Count of device interrupt callbacks */
	uint lastintrs;		/* Count as of last watchdog timer */
	uint spurious;		/* Count of spurious interrupts */
	uint pollrate;		/* Ticks between device polls */
	uint polltick;		/* Tick counter */
	uint pollcnt;		/* Count of active polls */

#ifdef DEBUG
	uint console_interval;
	struct brcmf_console console;	/* Console output polling support */
	uint console_addr;	/* Console address from shared struct */
#endif				/* DEBUG */

	uint regfails;		/* Count of R_REG failures */

	uint clkstate;		/* State of sd and backplane clock(s) */
	bool activity;		/* Activity flag for clock down */
	s32 idletime;		/* Control for activity timeout */
	s32 idlecount;	/* Activity timeout counter */
	s32 idleclock;	/* How to set bus driver when idle */
	s32 sd_rxchain;
	bool use_rxchain;	/* If brcmf should use PKT chains */
	bool sleeping;		/* Is SDIO bus sleeping? */
	bool rxflow_mode;	/* Rx flow control mode */
	bool rxflow;		/* Is rx flow control on */
	bool alp_only;		/* Don't use HT clock (ALP only) */
/* Field to decide if rx of control frames happen in rxbuf or lb-pool */
	bool usebufpool;

	/* Some additional counters */
	uint tx_sderrs;		/* Count of tx attempts with sd errors */
	uint fcqueued;		/* Tx packets that got queued */
	uint rxrtx;		/* Count of rtx requests (NAK to dongle) */
	uint rx_toolong;	/* Receive frames too long to receive */
	uint rxc_errors;	/* SDIO errors when reading control frames */
	uint rx_hdrfail;	/* SDIO errors on header reads */
	uint rx_badhdr;		/* Bad received headers (roosync?) */
	uint rx_badseq;		/* Mismatched rx sequence number */
	uint fc_rcvd;		/* Number of flow-control events received */
	uint fc_xoff;		/* Number which turned on flow-control */
	uint fc_xon;		/* Number which turned off flow-control */
	uint rxglomfail;	/* Failed deglom attempts */
	uint rxglomframes;	/* Number of glom frames (superframes) */
	uint rxglompkts;	/* Number of packets from glom frames */
	uint f2rxhdrs;		/* Number of header reads */
	uint f2rxdata;		/* Number of frame data reads */
	uint f2txdata;		/* Number of f2 frame writes */
	uint f1regdata;		/* Number of f1 register accesses */
	uint tickcnt;		/* Number of watchdog been schedule */
	unsigned long tx_ctlerrs;	/* Err of sending ctrl frames */
	unsigned long tx_ctlpkts;	/* Ctrl frames sent to dongle */
	unsigned long rx_ctlerrs;	/* Err of processing rx ctrl frames */
	unsigned long rx_ctlpkts;	/* Ctrl frames processed from dongle */
	unsigned long rx_readahead_cnt;	/* Number of packets where header
					 * read-ahead was used. */

	u8 *ctrl_frame_buf;
	u32 ctrl_frame_len;
	bool ctrl_frame_stat;

	spinlock_t txqlock;
	wait_queue_head_t ctrl_wait;
	wait_queue_head_t dcmd_resp_wait;

	struct timer_list timer;
	struct completion watchdog_wait;
	struct task_struct *watchdog_tsk;
	bool wd_timer_valid;
	uint save_ms;

	struct task_struct *dpc_tsk;
	struct completion dpc_wait;
	struct list_head dpc_tsklst;
	spinlock_t dpc_tl_lock;

	struct semaphore sdsem;

	const struct firmware *firmware;
	u32 fw_ptr;

	bool txoff;		/* Transmit flow-controlled */
};

/* clkstate */
#define CLK_NONE	0
#define CLK_SDONLY	1
#define CLK_PENDING	2	/* Not used yet */
#define CLK_AVAIL	3

#ifdef DEBUG
static int qcount[NUMPRIO];
static int tx_packets[NUMPRIO];
#endif				/* DEBUG */

#define SDIO_DRIVE_STRENGTH	6	/* in milliamps */

#define RETRYCHAN(chan) ((chan) == SDPCM_EVENT_CHANNEL)

/* Retry count for register access failures */
static const uint retry_limit = 2;

/* Limit on rounding up frames */
static const uint max_roundup = 512;

#define ALIGNMENT  4

static void pkt_align(struct sk_buff *p, int len, int align)
{
	uint datalign;
	datalign = (unsigned long)(p->data);
	datalign = roundup(datalign, (align)) - datalign;
	if (datalign)
		skb_pull(p, datalign);
	__skb_trim(p, len);
}

/* To check if there's window offered */
static bool data_ok(struct brcmf_sdio *bus)
{
	return (u8)(bus->tx_max - bus->tx_seq) != 0 &&
	       ((u8)(bus->tx_max - bus->tx_seq) & 0x80) == 0;
}

/*
 * Reads a register in the SDIO hardware block. This block occupies a series of
 * adresses on the 32 bit backplane bus.
 */
static void
r_sdreg32(struct brcmf_sdio *bus, u32 *regvar, u32 reg_offset, u32 *retryvar)
{
	u8 idx = brcmf_sdio_chip_getinfidx(bus->ci, BCMA_CORE_SDIO_DEV);
	*retryvar = 0;
	do {
		*regvar = brcmf_sdcard_reg_read(bus->sdiodev,
				bus->ci->c_inf[idx].base + reg_offset);
	} while (brcmf_sdcard_regfail(bus->sdiodev) &&
		 (++(*retryvar) <= retry_limit));
	if (*retryvar) {
		bus->regfails += (*retryvar-1);
		if (*retryvar > retry_limit) {
			brcmf_dbg(ERROR, "FAILED READ %Xh\n", reg_offset);
			*regvar = 0;
		}
	}
}

static void
w_sdreg32(struct brcmf_sdio *bus, u32 regval, u32 reg_offset, u32 *retryvar)
{
	u8 idx = brcmf_sdio_chip_getinfidx(bus->ci, BCMA_CORE_SDIO_DEV);
	*retryvar = 0;
	do {
		brcmf_sdcard_reg_write(bus->sdiodev,
				       bus->ci->c_inf[idx].base + reg_offset,
				       regval);
	} while (brcmf_sdcard_regfail(bus->sdiodev) &&
		 (++(*retryvar) <= retry_limit));
	if (*retryvar) {
		bus->regfails += (*retryvar-1);
		if (*retryvar > retry_limit)
			brcmf_dbg(ERROR, "FAILED REGISTER WRITE %Xh\n",
				  reg_offset);
	}
}

#define PKT_AVAILABLE()		(intstatus & I_HMB_FRAME_IND)

#define HOSTINTMASK		(I_HMB_SW_MASK | I_CHIPACTIVE)

/* Packet free applicable unconditionally for sdio and sdspi.
 * Conditional if bufpool was present for gspi bus.
 */
static void brcmf_sdbrcm_pktfree2(struct brcmf_sdio *bus, struct sk_buff *pkt)
{
	if (bus->usebufpool)
		brcmu_pkt_buf_free_skb(pkt);
}

/* Turn backplane clock on or off */
static int brcmf_sdbrcm_htclk(struct brcmf_sdio *bus, bool on, bool pendok)
{
	int err;
	u8 clkctl, clkreq, devctl;
	unsigned long timeout;

	brcmf_dbg(TRACE, "Enter\n");

	clkctl = 0;

	if (on) {
		/* Request HT Avail */
		clkreq =
		    bus->alp_only ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;

		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
				       SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
		if (err) {
			brcmf_dbg(ERROR, "HT Avail request error: %d\n", err);
			return -EBADE;
		}

		/* Check current status */
		clkctl = brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
					       SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			brcmf_dbg(ERROR, "HT Avail read error: %d\n", err);
			return -EBADE;
		}

		/* Go to pending and await interrupt if appropriate */
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only) && pendok) {
			/* Allow only clock-available interrupt */
			devctl = brcmf_sdcard_cfg_read(bus->sdiodev,
					SDIO_FUNC_1,
					SBSDIO_DEVICE_CTL, &err);
			if (err) {
				brcmf_dbg(ERROR, "Devctl error setting CA: %d\n",
					  err);
				return -EBADE;
			}

			devctl |= SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
					       SBSDIO_DEVICE_CTL, devctl, &err);
			brcmf_dbg(INFO, "CLKCTL: set PENDING\n");
			bus->clkstate = CLK_PENDING;

			return 0;
		} else if (bus->clkstate == CLK_PENDING) {
			/* Cancel CA-only interrupt filter */
			devctl =
			    brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
						  SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
				SBSDIO_DEVICE_CTL, devctl, &err);
		}

		/* Otherwise, wait here (polling) for HT Avail */
		timeout = jiffies +
			  msecs_to_jiffies(PMU_MAX_TRANSITION_DLY/1000);
		while (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			clkctl = brcmf_sdcard_cfg_read(bus->sdiodev,
						       SDIO_FUNC_1,
						       SBSDIO_FUNC1_CHIPCLKCSR,
						       &err);
			if (time_after(jiffies, timeout))
				break;
			else
				usleep_range(5000, 10000);
		}
		if (err) {
			brcmf_dbg(ERROR, "HT Avail request error: %d\n", err);
			return -EBADE;
		}
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			brcmf_dbg(ERROR, "HT Avail timeout (%d): clkctl 0x%02x\n",
				  PMU_MAX_TRANSITION_DLY, clkctl);
			return -EBADE;
		}

		/* Mark clock available */
		bus->clkstate = CLK_AVAIL;
		brcmf_dbg(INFO, "CLKCTL: turned ON\n");

#if defined(DEBUG)
		if (!bus->alp_only) {
			if (SBSDIO_ALPONLY(clkctl))
				brcmf_dbg(ERROR, "HT Clock should be on\n");
		}
#endif				/* defined (DEBUG) */

		bus->activity = true;
	} else {
		clkreq = 0;

		if (bus->clkstate == CLK_PENDING) {
			/* Cancel CA-only interrupt filter */
			devctl = brcmf_sdcard_cfg_read(bus->sdiodev,
					SDIO_FUNC_1,
					SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
				SBSDIO_DEVICE_CTL, devctl, &err);
		}

		bus->clkstate = CLK_SDONLY;
		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
		brcmf_dbg(INFO, "CLKCTL: turned OFF\n");
		if (err) {
			brcmf_dbg(ERROR, "Failed access turning clock off: %d\n",
				  err);
			return -EBADE;
		}
	}
	return 0;
}

/* Change idle/active SD state */
static int brcmf_sdbrcm_sdclk(struct brcmf_sdio *bus, bool on)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (on)
		bus->clkstate = CLK_SDONLY;
	else
		bus->clkstate = CLK_NONE;

	return 0;
}

/* Transition SD and backplane clock readiness */
static int brcmf_sdbrcm_clkctl(struct brcmf_sdio *bus, uint target, bool pendok)
{
#ifdef DEBUG
	uint oldstate = bus->clkstate;
#endif				/* DEBUG */

	brcmf_dbg(TRACE, "Enter\n");

	/* Early exit if we're already there */
	if (bus->clkstate == target) {
		if (target == CLK_AVAIL) {
			brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);
			bus->activity = true;
		}
		return 0;
	}

	switch (target) {
	case CLK_AVAIL:
		/* Make sure SD clock is available */
		if (bus->clkstate == CLK_NONE)
			brcmf_sdbrcm_sdclk(bus, true);
		/* Now request HT Avail on the backplane */
		brcmf_sdbrcm_htclk(bus, true, pendok);
		brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);
		bus->activity = true;
		break;

	case CLK_SDONLY:
		/* Remove HT request, or bring up SD clock */
		if (bus->clkstate == CLK_NONE)
			brcmf_sdbrcm_sdclk(bus, true);
		else if (bus->clkstate == CLK_AVAIL)
			brcmf_sdbrcm_htclk(bus, false, false);
		else
			brcmf_dbg(ERROR, "request for %d -> %d\n",
				  bus->clkstate, target);
		brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);
		break;

	case CLK_NONE:
		/* Make sure to remove HT request */
		if (bus->clkstate == CLK_AVAIL)
			brcmf_sdbrcm_htclk(bus, false, false);
		/* Now remove the SD clock */
		brcmf_sdbrcm_sdclk(bus, false);
		brcmf_sdbrcm_wd_timer(bus, 0);
		break;
	}
#ifdef DEBUG
	brcmf_dbg(INFO, "%d -> %d\n", oldstate, bus->clkstate);
#endif				/* DEBUG */

	return 0;
}

static int brcmf_sdbrcm_bussleep(struct brcmf_sdio *bus, bool sleep)
{
	uint retries = 0;

	brcmf_dbg(INFO, "request %s (currently %s)\n",
		  sleep ? "SLEEP" : "WAKE",
		  bus->sleeping ? "SLEEP" : "WAKE");

	/* Done if we're already in the requested state */
	if (sleep == bus->sleeping)
		return 0;

	/* Going to sleep: set the alarm and turn off the lights... */
	if (sleep) {
		/* Don't sleep if something is pending */
		if (bus->dpc_sched || bus->rxskip || pktq_len(&bus->txq))
			return -EBUSY;

		/* Make sure the controller has the bus up */
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

		/* Tell device to start using OOB wakeup */
		w_sdreg32(bus, SMB_USE_OOB,
			  offsetof(struct sdpcmd_regs, tosbmailbox), &retries);
		if (retries > retry_limit)
			brcmf_dbg(ERROR, "CANNOT SIGNAL CHIP, WILL NOT WAKE UP!!\n");

		/* Turn off our contribution to the HT clock request */
		brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);

		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR,
			SBSDIO_FORCE_HW_CLKREQ_OFF, NULL);

		/* Isolate the bus */
		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
			SBSDIO_DEVICE_CTL,
			SBSDIO_DEVCTL_PADS_ISO, NULL);

		/* Change state */
		bus->sleeping = true;

	} else {
		/* Waking up: bus power up is ok, set local state */

		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);

		/* Make sure the controller has the bus up */
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

		/* Send misc interrupt to indicate OOB not needed */
		w_sdreg32(bus, 0, offsetof(struct sdpcmd_regs, tosbmailboxdata),
			  &retries);
		if (retries <= retry_limit)
			w_sdreg32(bus, SMB_DEV_INT,
				  offsetof(struct sdpcmd_regs, tosbmailbox),
				  &retries);

		if (retries > retry_limit)
			brcmf_dbg(ERROR, "CANNOT SIGNAL CHIP TO CLEAR OOB!!\n");

		/* Make sure we have SD bus access */
		brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);

		/* Change state */
		bus->sleeping = false;
	}

	return 0;
}

static void bus_wake(struct brcmf_sdio *bus)
{
	if (bus->sleeping)
		brcmf_sdbrcm_bussleep(bus, false);
}

static u32 brcmf_sdbrcm_hostmail(struct brcmf_sdio *bus)
{
	u32 intstatus = 0;
	u32 hmb_data;
	u8 fcbits;
	uint retries = 0;

	brcmf_dbg(TRACE, "Enter\n");

	/* Read mailbox data and ack that we did so */
	r_sdreg32(bus, &hmb_data,
		  offsetof(struct sdpcmd_regs, tohostmailboxdata), &retries);

	if (retries <= retry_limit)
		w_sdreg32(bus, SMB_INT_ACK,
			  offsetof(struct sdpcmd_regs, tosbmailbox), &retries);
	bus->f1regdata += 2;

	/* Dongle recomposed rx frames, accept them again */
	if (hmb_data & HMB_DATA_NAKHANDLED) {
		brcmf_dbg(INFO, "Dongle reports NAK handled, expect rtx of %d\n",
			  bus->rx_seq);
		if (!bus->rxskip)
			brcmf_dbg(ERROR, "unexpected NAKHANDLED!\n");

		bus->rxskip = false;
		intstatus |= I_HMB_FRAME_IND;
	}

	/*
	 * DEVREADY does not occur with gSPI.
	 */
	if (hmb_data & (HMB_DATA_DEVREADY | HMB_DATA_FWREADY)) {
		bus->sdpcm_ver =
		    (hmb_data & HMB_DATA_VERSION_MASK) >>
		    HMB_DATA_VERSION_SHIFT;
		if (bus->sdpcm_ver != SDPCM_PROT_VERSION)
			brcmf_dbg(ERROR, "Version mismatch, dongle reports %d, "
				  "expecting %d\n",
				  bus->sdpcm_ver, SDPCM_PROT_VERSION);
		else
			brcmf_dbg(INFO, "Dongle ready, protocol version %d\n",
				  bus->sdpcm_ver);
	}

	/*
	 * Flow Control has been moved into the RX headers and this out of band
	 * method isn't used any more.
	 * remaining backward compatible with older dongles.
	 */
	if (hmb_data & HMB_DATA_FC) {
		fcbits = (hmb_data & HMB_DATA_FCDATA_MASK) >>
							HMB_DATA_FCDATA_SHIFT;

		if (fcbits & ~bus->flowcontrol)
			bus->fc_xoff++;

		if (bus->flowcontrol & ~fcbits)
			bus->fc_xon++;

		bus->fc_rcvd++;
		bus->flowcontrol = fcbits;
	}

	/* Shouldn't be any others */
	if (hmb_data & ~(HMB_DATA_DEVREADY |
			 HMB_DATA_NAKHANDLED |
			 HMB_DATA_FC |
			 HMB_DATA_FWREADY |
			 HMB_DATA_FCDATA_MASK | HMB_DATA_VERSION_MASK))
		brcmf_dbg(ERROR, "Unknown mailbox data content: 0x%02x\n",
			  hmb_data);

	return intstatus;
}

static void brcmf_sdbrcm_rxfail(struct brcmf_sdio *bus, bool abort, bool rtx)
{
	uint retries = 0;
	u16 lastrbc;
	u8 hi, lo;
	int err;

	brcmf_dbg(ERROR, "%sterminate frame%s\n",
		  abort ? "abort command, " : "",
		  rtx ? ", send NAK" : "");

	if (abort)
		brcmf_sdcard_abort(bus->sdiodev, SDIO_FUNC_2);

	brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
			       SBSDIO_FUNC1_FRAMECTRL,
			       SFC_RF_TERM, &err);
	bus->f1regdata++;

	/* Wait until the packet has been flushed (device/FIFO stable) */
	for (lastrbc = retries = 0xffff; retries > 0; retries--) {
		hi = brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
					   SBSDIO_FUNC1_RFRAMEBCHI, NULL);
		lo = brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
					   SBSDIO_FUNC1_RFRAMEBCLO, NULL);
		bus->f1regdata += 2;

		if ((hi == 0) && (lo == 0))
			break;

		if ((hi > (lastrbc >> 8)) && (lo > (lastrbc & 0x00ff))) {
			brcmf_dbg(ERROR, "count growing: last 0x%04x now 0x%04x\n",
				  lastrbc, (hi << 8) + lo);
		}
		lastrbc = (hi << 8) + lo;
	}

	if (!retries)
		brcmf_dbg(ERROR, "count never zeroed: last 0x%04x\n", lastrbc);
	else
		brcmf_dbg(INFO, "flush took %d iterations\n", 0xffff - retries);

	if (rtx) {
		bus->rxrtx++;
		w_sdreg32(bus, SMB_NAK,
			  offsetof(struct sdpcmd_regs, tosbmailbox), &retries);

		bus->f1regdata++;
		if (retries <= retry_limit)
			bus->rxskip = true;
	}

	/* Clear partial in any case */
	bus->nextlen = 0;

	/* If we can't reach the device, signal failure */
	if (err || brcmf_sdcard_regfail(bus->sdiodev))
		bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
}

/* copy a buffer into a pkt buffer chain */
static uint brcmf_sdbrcm_glom_from_buf(struct brcmf_sdio *bus, uint len)
{
	uint n, ret = 0;
	struct sk_buff *p;
	u8 *buf;

	buf = bus->dataptr;

	/* copy the data */
	skb_queue_walk(&bus->glom, p) {
		n = min_t(uint, p->len, len);
		memcpy(p->data, buf, n);
		buf += n;
		len -= n;
		ret += n;
		if (!len)
			break;
	}

	return ret;
}

/* return total length of buffer chain */
static uint brcmf_sdbrcm_glom_len(struct brcmf_sdio *bus)
{
	struct sk_buff *p;
	uint total;

	total = 0;
	skb_queue_walk(&bus->glom, p)
		total += p->len;
	return total;
}

static void brcmf_sdbrcm_free_glom(struct brcmf_sdio *bus)
{
	struct sk_buff *cur, *next;

	skb_queue_walk_safe(&bus->glom, cur, next) {
		skb_unlink(cur, &bus->glom);
		brcmu_pkt_buf_free_skb(cur);
	}
}

static u8 brcmf_sdbrcm_rxglom(struct brcmf_sdio *bus, u8 rxseq)
{
	u16 dlen, totlen;
	u8 *dptr, num = 0;

	u16 sublen, check;
	struct sk_buff *pfirst, *pnext;

	int errcode;
	u8 chan, seq, doff, sfdoff;
	u8 txmax;

	int ifidx = 0;
	bool usechain = bus->use_rxchain;

	/* If packets, issue read(s) and send up packet chain */
	/* Return sequence numbers consumed? */

	brcmf_dbg(TRACE, "start: glomd %p glom %p\n",
		  bus->glomd, skb_peek(&bus->glom));

	/* If there's a descriptor, generate the packet chain */
	if (bus->glomd) {
		pfirst = pnext = NULL;
		dlen = (u16) (bus->glomd->len);
		dptr = bus->glomd->data;
		if (!dlen || (dlen & 1)) {
			brcmf_dbg(ERROR, "bad glomd len(%d), ignore descriptor\n",
				  dlen);
			dlen = 0;
		}

		for (totlen = num = 0; dlen; num++) {
			/* Get (and move past) next length */
			sublen = get_unaligned_le16(dptr);
			dlen -= sizeof(u16);
			dptr += sizeof(u16);
			if ((sublen < SDPCM_HDRLEN) ||
			    ((num == 0) && (sublen < (2 * SDPCM_HDRLEN)))) {
				brcmf_dbg(ERROR, "descriptor len %d bad: %d\n",
					  num, sublen);
				pnext = NULL;
				break;
			}
			if (sublen % BRCMF_SDALIGN) {
				brcmf_dbg(ERROR, "sublen %d not multiple of %d\n",
					  sublen, BRCMF_SDALIGN);
				usechain = false;
			}
			totlen += sublen;

			/* For last frame, adjust read len so total
				 is a block multiple */
			if (!dlen) {
				sublen +=
				    (roundup(totlen, bus->blocksize) - totlen);
				totlen = roundup(totlen, bus->blocksize);
			}

			/* Allocate/chain packet for next subframe */
			pnext = brcmu_pkt_buf_get_skb(sublen + BRCMF_SDALIGN);
			if (pnext == NULL) {
				brcmf_dbg(ERROR, "bcm_pkt_buf_get_skb failed, num %d len %d\n",
					  num, sublen);
				break;
			}
			skb_queue_tail(&bus->glom, pnext);

			/* Adhere to start alignment requirements */
			pkt_align(pnext, sublen, BRCMF_SDALIGN);
		}

		/* If all allocations succeeded, save packet chain
			 in bus structure */
		if (pnext) {
			brcmf_dbg(GLOM, "allocated %d-byte packet chain for %d subframes\n",
				  totlen, num);
			if (BRCMF_GLOM_ON() && bus->nextlen &&
			    totlen != bus->nextlen) {
				brcmf_dbg(GLOM, "glomdesc mismatch: nextlen %d glomdesc %d rxseq %d\n",
					  bus->nextlen, totlen, rxseq);
			}
			pfirst = pnext = NULL;
		} else {
			brcmf_sdbrcm_free_glom(bus);
			num = 0;
		}

		/* Done with descriptor packet */
		brcmu_pkt_buf_free_skb(bus->glomd);
		bus->glomd = NULL;
		bus->nextlen = 0;
	}

	/* Ok -- either we just generated a packet chain,
		 or had one from before */
	if (!skb_queue_empty(&bus->glom)) {
		if (BRCMF_GLOM_ON()) {
			brcmf_dbg(GLOM, "try superframe read, packet chain:\n");
			skb_queue_walk(&bus->glom, pnext) {
				brcmf_dbg(GLOM, "    %p: %p len 0x%04x (%d)\n",
					  pnext, (u8 *) (pnext->data),
					  pnext->len, pnext->len);
			}
		}

		pfirst = skb_peek(&bus->glom);
		dlen = (u16) brcmf_sdbrcm_glom_len(bus);

		/* Do an SDIO read for the superframe.  Configurable iovar to
		 * read directly into the chained packet, or allocate a large
		 * packet and and copy into the chain.
		 */
		if (usechain) {
			errcode = brcmf_sdcard_recv_chain(bus->sdiodev,
					bus->sdiodev->sbwad,
					SDIO_FUNC_2, F2SYNC, &bus->glom);
		} else if (bus->dataptr) {
			errcode = brcmf_sdcard_recv_buf(bus->sdiodev,
					bus->sdiodev->sbwad,
					SDIO_FUNC_2, F2SYNC,
					bus->dataptr, dlen);
			sublen = (u16) brcmf_sdbrcm_glom_from_buf(bus, dlen);
			if (sublen != dlen) {
				brcmf_dbg(ERROR, "FAILED TO COPY, dlen %d sublen %d\n",
					  dlen, sublen);
				errcode = -1;
			}
			pnext = NULL;
		} else {
			brcmf_dbg(ERROR, "COULDN'T ALLOC %d-BYTE GLOM, FORCE FAILURE\n",
				  dlen);
			errcode = -1;
		}
		bus->f2rxdata++;

		/* On failure, kill the superframe, allow a couple retries */
		if (errcode < 0) {
			brcmf_dbg(ERROR, "glom read of %d bytes failed: %d\n",
				  dlen, errcode);
			bus->sdiodev->bus_if->dstats.rx_errors++;

			if (bus->glomerr++ < 3) {
				brcmf_sdbrcm_rxfail(bus, true, true);
			} else {
				bus->glomerr = 0;
				brcmf_sdbrcm_rxfail(bus, true, false);
				bus->rxglomfail++;
				brcmf_sdbrcm_free_glom(bus);
			}
			return 0;
		}

		brcmf_dbg_hex_dump(BRCMF_GLOM_ON(),
				   pfirst->data, min_t(int, pfirst->len, 48),
				   "SUPERFRAME:\n");

		/* Validate the superframe header */
		dptr = (u8 *) (pfirst->data);
		sublen = get_unaligned_le16(dptr);
		check = get_unaligned_le16(dptr + sizeof(u16));

		chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
		bus->nextlen = dptr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
			brcmf_dbg(INFO, "nextlen too large (%d) seq %d\n",
				  bus->nextlen, seq);
			bus->nextlen = 0;
		}
		doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

		errcode = 0;
		if ((u16)~(sublen ^ check)) {
			brcmf_dbg(ERROR, "(superframe): HW hdr error: len/check 0x%04x/0x%04x\n",
				  sublen, check);
			errcode = -1;
		} else if (roundup(sublen, bus->blocksize) != dlen) {
			brcmf_dbg(ERROR, "(superframe): len 0x%04x, rounded 0x%04x, expect 0x%04x\n",
				  sublen, roundup(sublen, bus->blocksize),
				  dlen);
			errcode = -1;
		} else if (SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]) !=
			   SDPCM_GLOM_CHANNEL) {
			brcmf_dbg(ERROR, "(superframe): bad channel %d\n",
				  SDPCM_PACKET_CHANNEL(
					  &dptr[SDPCM_FRAMETAG_LEN]));
			errcode = -1;
		} else if (SDPCM_GLOMDESC(&dptr[SDPCM_FRAMETAG_LEN])) {
			brcmf_dbg(ERROR, "(superframe): got 2nd descriptor?\n");
			errcode = -1;
		} else if ((doff < SDPCM_HDRLEN) ||
			   (doff > (pfirst->len - SDPCM_HDRLEN))) {
			brcmf_dbg(ERROR, "(superframe): Bad data offset %d: HW %d pkt %d min %d\n",
				  doff, sublen, pfirst->len, SDPCM_HDRLEN);
			errcode = -1;
		}

		/* Check sequence number of superframe SW header */
		if (rxseq != seq) {
			brcmf_dbg(INFO, "(superframe) rx_seq %d, expected %d\n",
				  seq, rxseq);
			bus->rx_badseq++;
			rxseq = seq;
		}

		/* Check window for sanity */
		if ((u8) (txmax - bus->tx_seq) > 0x40) {
			brcmf_dbg(ERROR, "unlikely tx max %d with tx_seq %d\n",
				  txmax, bus->tx_seq);
			txmax = bus->tx_seq + 2;
		}
		bus->tx_max = txmax;

		/* Remove superframe header, remember offset */
		skb_pull(pfirst, doff);
		sfdoff = doff;
		num = 0;

		/* Validate all the subframe headers */
		skb_queue_walk(&bus->glom, pnext) {
			/* leave when invalid subframe is found */
			if (errcode)
				break;

			dptr = (u8 *) (pnext->data);
			dlen = (u16) (pnext->len);
			sublen = get_unaligned_le16(dptr);
			check = get_unaligned_le16(dptr + sizeof(u16));
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);
			brcmf_dbg_hex_dump(BRCMF_GLOM_ON(),
					   dptr, 32, "subframe:\n");

			if ((u16)~(sublen ^ check)) {
				brcmf_dbg(ERROR, "(subframe %d): HW hdr error: len/check 0x%04x/0x%04x\n",
					  num, sublen, check);
				errcode = -1;
			} else if ((sublen > dlen) || (sublen < SDPCM_HDRLEN)) {
				brcmf_dbg(ERROR, "(subframe %d): length mismatch: len 0x%04x, expect 0x%04x\n",
					  num, sublen, dlen);
				errcode = -1;
			} else if ((chan != SDPCM_DATA_CHANNEL) &&
				   (chan != SDPCM_EVENT_CHANNEL)) {
				brcmf_dbg(ERROR, "(subframe %d): bad channel %d\n",
					  num, chan);
				errcode = -1;
			} else if ((doff < SDPCM_HDRLEN) || (doff > sublen)) {
				brcmf_dbg(ERROR, "(subframe %d): Bad data offset %d: HW %d min %d\n",
					  num, doff, sublen, SDPCM_HDRLEN);
				errcode = -1;
			}
			/* increase the subframe count */
			num++;
		}

		if (errcode) {
			/* Terminate frame on error, request
				 a couple retries */
			if (bus->glomerr++ < 3) {
				/* Restore superframe header space */
				skb_push(pfirst, sfdoff);
				brcmf_sdbrcm_rxfail(bus, true, true);
			} else {
				bus->glomerr = 0;
				brcmf_sdbrcm_rxfail(bus, true, false);
				bus->rxglomfail++;
				brcmf_sdbrcm_free_glom(bus);
			}
			bus->nextlen = 0;
			return 0;
		}

		/* Basic SD framing looks ok - process each packet (header) */

		skb_queue_walk_safe(&bus->glom, pfirst, pnext) {
			dptr = (u8 *) (pfirst->data);
			sublen = get_unaligned_le16(dptr);
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

			brcmf_dbg(GLOM, "Get subframe %d, %p(%p/%d), sublen %d chan %d seq %d\n",
				  num, pfirst, pfirst->data,
				  pfirst->len, sublen, chan, seq);

			/* precondition: chan == SDPCM_DATA_CHANNEL ||
					 chan == SDPCM_EVENT_CHANNEL */

			if (rxseq != seq) {
				brcmf_dbg(GLOM, "rx_seq %d, expected %d\n",
					  seq, rxseq);
				bus->rx_badseq++;
				rxseq = seq;
			}
			rxseq++;

			brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_DATA_ON(),
					   dptr, dlen, "Rx Subframe Data:\n");

			__skb_trim(pfirst, sublen);
			skb_pull(pfirst, doff);

			if (pfirst->len == 0) {
				skb_unlink(pfirst, &bus->glom);
				brcmu_pkt_buf_free_skb(pfirst);
				continue;
			} else if (brcmf_proto_hdrpull(bus->sdiodev->dev,
						       &ifidx, pfirst) != 0) {
				brcmf_dbg(ERROR, "rx protocol error\n");
				bus->sdiodev->bus_if->dstats.rx_errors++;
				skb_unlink(pfirst, &bus->glom);
				brcmu_pkt_buf_free_skb(pfirst);
				continue;
			}

			brcmf_dbg_hex_dump(BRCMF_GLOM_ON(),
					   pfirst->data,
					   min_t(int, pfirst->len, 32),
					   "subframe %d to stack, %p (%p/%d) nxt/lnk %p/%p\n",
					   bus->glom.qlen, pfirst, pfirst->data,
					   pfirst->len, pfirst->next,
					   pfirst->prev);
		}
		/* sent any remaining packets up */
		if (bus->glom.qlen) {
			up(&bus->sdsem);
			brcmf_rx_frame(bus->sdiodev->dev, ifidx, &bus->glom);
			down(&bus->sdsem);
		}

		bus->rxglomframes++;
		bus->rxglompkts += bus->glom.qlen;
	}
	return num;
}

static int brcmf_sdbrcm_dcmd_resp_wait(struct brcmf_sdio *bus, uint *condition,
					bool *pending)
{
	DECLARE_WAITQUEUE(wait, current);
	int timeout = msecs_to_jiffies(DCMD_RESP_TIMEOUT);

	/* Wait until control frame is available */
	add_wait_queue(&bus->dcmd_resp_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while (!(*condition) && (!signal_pending(current) && timeout))
		timeout = schedule_timeout(timeout);

	if (signal_pending(current))
		*pending = true;

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&bus->dcmd_resp_wait, &wait);

	return timeout;
}

static int brcmf_sdbrcm_dcmd_resp_wake(struct brcmf_sdio *bus)
{
	if (waitqueue_active(&bus->dcmd_resp_wait))
		wake_up_interruptible(&bus->dcmd_resp_wait);

	return 0;
}
static void
brcmf_sdbrcm_read_control(struct brcmf_sdio *bus, u8 *hdr, uint len, uint doff)
{
	uint rdlen, pad;

	int sdret;

	brcmf_dbg(TRACE, "Enter\n");

	/* Set rxctl for frame (w/optional alignment) */
	bus->rxctl = bus->rxbuf;
	bus->rxctl += BRCMF_FIRSTREAD;
	pad = ((unsigned long)bus->rxctl % BRCMF_SDALIGN);
	if (pad)
		bus->rxctl += (BRCMF_SDALIGN - pad);
	bus->rxctl -= BRCMF_FIRSTREAD;

	/* Copy the already-read portion over */
	memcpy(bus->rxctl, hdr, BRCMF_FIRSTREAD);
	if (len <= BRCMF_FIRSTREAD)
		goto gotpkt;

	/* Raise rdlen to next SDIO block to avoid tail command */
	rdlen = len - BRCMF_FIRSTREAD;
	if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
		pad = bus->blocksize - (rdlen % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
		    ((len + pad) < bus->sdiodev->bus_if->maxctl))
			rdlen += pad;
	} else if (rdlen % BRCMF_SDALIGN) {
		rdlen += BRCMF_SDALIGN - (rdlen % BRCMF_SDALIGN);
	}

	/* Satisfy length-alignment requirements */
	if (rdlen & (ALIGNMENT - 1))
		rdlen = roundup(rdlen, ALIGNMENT);

	/* Drop if the read is too big or it exceeds our maximum */
	if ((rdlen + BRCMF_FIRSTREAD) > bus->sdiodev->bus_if->maxctl) {
		brcmf_dbg(ERROR, "%d-byte control read exceeds %d-byte buffer\n",
			  rdlen, bus->sdiodev->bus_if->maxctl);
		bus->sdiodev->bus_if->dstats.rx_errors++;
		brcmf_sdbrcm_rxfail(bus, false, false);
		goto done;
	}

	if ((len - doff) > bus->sdiodev->bus_if->maxctl) {
		brcmf_dbg(ERROR, "%d-byte ctl frame (%d-byte ctl data) exceeds %d-byte limit\n",
			  len, len - doff, bus->sdiodev->bus_if->maxctl);
		bus->sdiodev->bus_if->dstats.rx_errors++;
		bus->rx_toolong++;
		brcmf_sdbrcm_rxfail(bus, false, false);
		goto done;
	}

	/* Read remainder of frame body into the rxctl buffer */
	sdret = brcmf_sdcard_recv_buf(bus->sdiodev,
				bus->sdiodev->sbwad,
				SDIO_FUNC_2,
				F2SYNC, (bus->rxctl + BRCMF_FIRSTREAD), rdlen);
	bus->f2rxdata++;

	/* Control frame failures need retransmission */
	if (sdret < 0) {
		brcmf_dbg(ERROR, "read %d control bytes failed: %d\n",
			  rdlen, sdret);
		bus->rxc_errors++;
		brcmf_sdbrcm_rxfail(bus, true, true);
		goto done;
	}

gotpkt:

	brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_CTL_ON(),
			   bus->rxctl, len, "RxCtrl:\n");

	/* Point to valid data and indicate its length */
	bus->rxctl += doff;
	bus->rxlen = len - doff;

done:
	/* Awake any waiters */
	brcmf_sdbrcm_dcmd_resp_wake(bus);
}

/* Pad read to blocksize for efficiency */
static void brcmf_pad(struct brcmf_sdio *bus, u16 *pad, u16 *rdlen)
{
	if (bus->roundup && bus->blocksize && *rdlen > bus->blocksize) {
		*pad = bus->blocksize - (*rdlen % bus->blocksize);
		if (*pad <= bus->roundup && *pad < bus->blocksize &&
		    *rdlen + *pad + BRCMF_FIRSTREAD < MAX_RX_DATASZ)
			*rdlen += *pad;
	} else if (*rdlen % BRCMF_SDALIGN) {
		*rdlen += BRCMF_SDALIGN - (*rdlen % BRCMF_SDALIGN);
	}
}

static void
brcmf_alloc_pkt_and_read(struct brcmf_sdio *bus, u16 rdlen,
			 struct sk_buff **pkt, u8 **rxbuf)
{
	int sdret;		/* Return code from calls */

	*pkt = brcmu_pkt_buf_get_skb(rdlen + BRCMF_SDALIGN);
	if (*pkt == NULL)
		return;

	pkt_align(*pkt, rdlen, BRCMF_SDALIGN);
	*rxbuf = (u8 *) ((*pkt)->data);
	/* Read the entire frame */
	sdret = brcmf_sdcard_recv_pkt(bus->sdiodev, bus->sdiodev->sbwad,
				      SDIO_FUNC_2, F2SYNC, *pkt);
	bus->f2rxdata++;

	if (sdret < 0) {
		brcmf_dbg(ERROR, "(nextlen): read %d bytes failed: %d\n",
			  rdlen, sdret);
		brcmu_pkt_buf_free_skb(*pkt);
		bus->sdiodev->bus_if->dstats.rx_errors++;
		/* Force retry w/normal header read.
		 * Don't attempt NAK for
		 * gSPI
		 */
		brcmf_sdbrcm_rxfail(bus, true, true);
		*pkt = NULL;
	}
}

/* Checks the header */
static int
brcmf_check_rxbuf(struct brcmf_sdio *bus, struct sk_buff *pkt, u8 *rxbuf,
		  u8 rxseq, u16 nextlen, u16 *len)
{
	u16 check;
	bool len_consistent;	/* Result of comparing readahead len and
				   len from hw-hdr */

	memcpy(bus->rxhdr, rxbuf, SDPCM_HDRLEN);

	/* Extract hardware header fields */
	*len = get_unaligned_le16(bus->rxhdr);
	check = get_unaligned_le16(bus->rxhdr + sizeof(u16));

	/* All zeros means readahead info was bad */
	if (!(*len | check)) {
		brcmf_dbg(INFO, "(nextlen): read zeros in HW header???\n");
		goto fail;
	}

	/* Validate check bytes */
	if ((u16)~(*len ^ check)) {
		brcmf_dbg(ERROR, "(nextlen): HW hdr error: nextlen/len/check 0x%04x/0x%04x/0x%04x\n",
			  nextlen, *len, check);
		bus->rx_badhdr++;
		brcmf_sdbrcm_rxfail(bus, false, false);
		goto fail;
	}

	/* Validate frame length */
	if (*len < SDPCM_HDRLEN) {
		brcmf_dbg(ERROR, "(nextlen): HW hdr length invalid: %d\n",
			  *len);
		goto fail;
	}

	/* Check for consistency with readahead info */
	len_consistent = (nextlen != (roundup(*len, 16) >> 4));
	if (len_consistent) {
		/* Mismatch, force retry w/normal
			header (may be >4K) */
		brcmf_dbg(ERROR, "(nextlen): mismatch, nextlen %d len %d rnd %d; expected rxseq %d\n",
			  nextlen, *len, roundup(*len, 16),
			  rxseq);
		brcmf_sdbrcm_rxfail(bus, true, true);
		goto fail;
	}

	return 0;

fail:
	brcmf_sdbrcm_pktfree2(bus, pkt);
	return -EINVAL;
}

/* Return true if there may be more frames to read */
static uint
brcmf_sdbrcm_readframes(struct brcmf_sdio *bus, uint maxframes, bool *finished)
{
	u16 len, check;	/* Extracted hardware header fields */
	u8 chan, seq, doff;	/* Extracted software header fields */
	u8 fcbits;		/* Extracted fcbits from software header */

	struct sk_buff *pkt;		/* Packet for event or data frames */
	u16 pad;		/* Number of pad bytes to read */
	u16 rdlen;		/* Total number of bytes to read */
	u8 rxseq;		/* Next sequence number to expect */
	uint rxleft = 0;	/* Remaining number of frames allowed */
	int sdret;		/* Return code from calls */
	u8 txmax;		/* Maximum tx sequence offered */
	u8 *rxbuf;
	int ifidx = 0;
	uint rxcount = 0;	/* Total frames read */

	brcmf_dbg(TRACE, "Enter\n");

	/* Not finished unless we encounter no more frames indication */
	*finished = false;

	for (rxseq = bus->rx_seq, rxleft = maxframes;
	     !bus->rxskip && rxleft &&
	     bus->sdiodev->bus_if->state != BRCMF_BUS_DOWN;
	     rxseq++, rxleft--) {

		/* Handle glomming separately */
		if (bus->glomd || !skb_queue_empty(&bus->glom)) {
			u8 cnt;
			brcmf_dbg(GLOM, "calling rxglom: glomd %p, glom %p\n",
				  bus->glomd, skb_peek(&bus->glom));
			cnt = brcmf_sdbrcm_rxglom(bus, rxseq);
			brcmf_dbg(GLOM, "rxglom returned %d\n", cnt);
			rxseq += cnt - 1;
			rxleft = (rxleft > cnt) ? (rxleft - cnt) : 1;
			continue;
		}

		/* Try doing single read if we can */
		if (bus->nextlen) {
			u16 nextlen = bus->nextlen;
			bus->nextlen = 0;

			rdlen = len = nextlen << 4;
			brcmf_pad(bus, &pad, &rdlen);

			/*
			 * After the frame is received we have to
			 * distinguish whether it is data
			 * or non-data frame.
			 */
			brcmf_alloc_pkt_and_read(bus, rdlen, &pkt, &rxbuf);
			if (pkt == NULL) {
				/* Give up on data, request rtx of events */
				brcmf_dbg(ERROR, "(nextlen): brcmf_alloc_pkt_and_read failed: len %d rdlen %d expected rxseq %d\n",
					  len, rdlen, rxseq);
				continue;
			}

			if (brcmf_check_rxbuf(bus, pkt, rxbuf, rxseq, nextlen,
					      &len) < 0)
				continue;

			/* Extract software header fields */
			chan = SDPCM_PACKET_CHANNEL(
					&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
			seq = SDPCM_PACKET_SEQUENCE(
					&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(
					&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
			txmax = SDPCM_WINDOW_VALUE(
					&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

			bus->nextlen =
			    bus->rxhdr[SDPCM_FRAMETAG_LEN +
				       SDPCM_NEXTLEN_OFFSET];
			if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
				brcmf_dbg(INFO, "(nextlen): got frame w/nextlen too large (%d), seq %d\n",
					  bus->nextlen, seq);
				bus->nextlen = 0;
			}

			bus->rx_readahead_cnt++;

			/* Handle Flow Control */
			fcbits = SDPCM_FCMASK_VALUE(
					&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

			if (bus->flowcontrol != fcbits) {
				if (~bus->flowcontrol & fcbits)
					bus->fc_xoff++;

				if (bus->flowcontrol & ~fcbits)
					bus->fc_xon++;

				bus->fc_rcvd++;
				bus->flowcontrol = fcbits;
			}

			/* Check and update sequence number */
			if (rxseq != seq) {
				brcmf_dbg(INFO, "(nextlen): rx_seq %d, expected %d\n",
					  seq, rxseq);
				bus->rx_badseq++;
				rxseq = seq;
			}

			/* Check window for sanity */
			if ((u8) (txmax - bus->tx_seq) > 0x40) {
				brcmf_dbg(ERROR, "got unlikely tx max %d with tx_seq %d\n",
					  txmax, bus->tx_seq);
				txmax = bus->tx_seq + 2;
			}
			bus->tx_max = txmax;

			brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_DATA_ON(),
					   rxbuf, len, "Rx Data:\n");
			brcmf_dbg_hex_dump(!(BRCMF_BYTES_ON() &&
					     BRCMF_DATA_ON()) &&
					   BRCMF_HDRS_ON(),
					   bus->rxhdr, SDPCM_HDRLEN,
					   "RxHdr:\n");

			if (chan == SDPCM_CONTROL_CHANNEL) {
				brcmf_dbg(ERROR, "(nextlen): readahead on control packet %d?\n",
					  seq);
				/* Force retry w/normal header read */
				bus->nextlen = 0;
				brcmf_sdbrcm_rxfail(bus, false, true);
				brcmf_sdbrcm_pktfree2(bus, pkt);
				continue;
			}

			/* Validate data offset */
			if ((doff < SDPCM_HDRLEN) || (doff > len)) {
				brcmf_dbg(ERROR, "(nextlen): bad data offset %d: HW len %d min %d\n",
					  doff, len, SDPCM_HDRLEN);
				brcmf_sdbrcm_rxfail(bus, false, false);
				brcmf_sdbrcm_pktfree2(bus, pkt);
				continue;
			}

			/* All done with this one -- now deliver the packet */
			goto deliver;
		}

		/* Read frame header (hardware and software) */
		sdret = brcmf_sdcard_recv_buf(bus->sdiodev, bus->sdiodev->sbwad,
					      SDIO_FUNC_2, F2SYNC, bus->rxhdr,
					      BRCMF_FIRSTREAD);
		bus->f2rxhdrs++;

		if (sdret < 0) {
			brcmf_dbg(ERROR, "RXHEADER FAILED: %d\n", sdret);
			bus->rx_hdrfail++;
			brcmf_sdbrcm_rxfail(bus, true, true);
			continue;
		}
		brcmf_dbg_hex_dump(BRCMF_BYTES_ON() || BRCMF_HDRS_ON(),
				   bus->rxhdr, SDPCM_HDRLEN, "RxHdr:\n");


		/* Extract hardware header fields */
		len = get_unaligned_le16(bus->rxhdr);
		check = get_unaligned_le16(bus->rxhdr + sizeof(u16));

		/* All zeros means no more frames */
		if (!(len | check)) {
			*finished = true;
			break;
		}

		/* Validate check bytes */
		if ((u16) ~(len ^ check)) {
			brcmf_dbg(ERROR, "HW hdr err: len/check 0x%04x/0x%04x\n",
				  len, check);
			bus->rx_badhdr++;
			brcmf_sdbrcm_rxfail(bus, false, false);
			continue;
		}

		/* Validate frame length */
		if (len < SDPCM_HDRLEN) {
			brcmf_dbg(ERROR, "HW hdr length invalid: %d\n", len);
			continue;
		}

		/* Extract software header fields */
		chan = SDPCM_PACKET_CHANNEL(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		doff = SDPCM_DOFFSET_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

		/* Validate data offset */
		if ((doff < SDPCM_HDRLEN) || (doff > len)) {
			brcmf_dbg(ERROR, "Bad data offset %d: HW len %d, min %d seq %d\n",
				  doff, len, SDPCM_HDRLEN, seq);
			bus->rx_badhdr++;
			brcmf_sdbrcm_rxfail(bus, false, false);
			continue;
		}

		/* Save the readahead length if there is one */
		bus->nextlen =
		    bus->rxhdr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
			brcmf_dbg(INFO, "(nextlen): got frame w/nextlen too large (%d), seq %d\n",
				  bus->nextlen, seq);
			bus->nextlen = 0;
		}

		/* Handle Flow Control */
		fcbits = SDPCM_FCMASK_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

		if (bus->flowcontrol != fcbits) {
			if (~bus->flowcontrol & fcbits)
				bus->fc_xoff++;

			if (bus->flowcontrol & ~fcbits)
				bus->fc_xon++;

			bus->fc_rcvd++;
			bus->flowcontrol = fcbits;
		}

		/* Check and update sequence number */
		if (rxseq != seq) {
			brcmf_dbg(INFO, "rx_seq %d, expected %d\n", seq, rxseq);
			bus->rx_badseq++;
			rxseq = seq;
		}

		/* Check window for sanity */
		if ((u8) (txmax - bus->tx_seq) > 0x40) {
			brcmf_dbg(ERROR, "unlikely tx max %d with tx_seq %d\n",
				  txmax, bus->tx_seq);
			txmax = bus->tx_seq + 2;
		}
		bus->tx_max = txmax;

		/* Call a separate function for control frames */
		if (chan == SDPCM_CONTROL_CHANNEL) {
			brcmf_sdbrcm_read_control(bus, bus->rxhdr, len, doff);
			continue;
		}

		/* precondition: chan is either SDPCM_DATA_CHANNEL,
		   SDPCM_EVENT_CHANNEL, SDPCM_TEST_CHANNEL or
		   SDPCM_GLOM_CHANNEL */

		/* Length to read */
		rdlen = (len > BRCMF_FIRSTREAD) ? (len - BRCMF_FIRSTREAD) : 0;

		/* May pad read to blocksize for efficiency */
		if (bus->roundup && bus->blocksize &&
			(rdlen > bus->blocksize)) {
			pad = bus->blocksize - (rdlen % bus->blocksize);
			if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
			    ((rdlen + pad + BRCMF_FIRSTREAD) < MAX_RX_DATASZ))
				rdlen += pad;
		} else if (rdlen % BRCMF_SDALIGN) {
			rdlen += BRCMF_SDALIGN - (rdlen % BRCMF_SDALIGN);
		}

		/* Satisfy length-alignment requirements */
		if (rdlen & (ALIGNMENT - 1))
			rdlen = roundup(rdlen, ALIGNMENT);

		if ((rdlen + BRCMF_FIRSTREAD) > MAX_RX_DATASZ) {
			/* Too long -- skip this frame */
			brcmf_dbg(ERROR, "too long: len %d rdlen %d\n",
				  len, rdlen);
			bus->sdiodev->bus_if->dstats.rx_errors++;
			bus->rx_toolong++;
			brcmf_sdbrcm_rxfail(bus, false, false);
			continue;
		}

		pkt = brcmu_pkt_buf_get_skb(rdlen +
					    BRCMF_FIRSTREAD + BRCMF_SDALIGN);
		if (!pkt) {
			/* Give up on data, request rtx of events */
			brcmf_dbg(ERROR, "brcmu_pkt_buf_get_skb failed: rdlen %d chan %d\n",
				  rdlen, chan);
			bus->sdiodev->bus_if->dstats.rx_dropped++;
			brcmf_sdbrcm_rxfail(bus, false, RETRYCHAN(chan));
			continue;
		}

		/* Leave room for what we already read, and align remainder */
		skb_pull(pkt, BRCMF_FIRSTREAD);
		pkt_align(pkt, rdlen, BRCMF_SDALIGN);

		/* Read the remaining frame data */
		sdret = brcmf_sdcard_recv_pkt(bus->sdiodev, bus->sdiodev->sbwad,
					      SDIO_FUNC_2, F2SYNC, pkt);
		bus->f2rxdata++;

		if (sdret < 0) {
			brcmf_dbg(ERROR, "read %d %s bytes failed: %d\n", rdlen,
				  ((chan == SDPCM_EVENT_CHANNEL) ? "event"
				   : ((chan == SDPCM_DATA_CHANNEL) ? "data"
				      : "test")), sdret);
			brcmu_pkt_buf_free_skb(pkt);
			bus->sdiodev->bus_if->dstats.rx_errors++;
			brcmf_sdbrcm_rxfail(bus, true, RETRYCHAN(chan));
			continue;
		}

		/* Copy the already-read portion */
		skb_push(pkt, BRCMF_FIRSTREAD);
		memcpy(pkt->data, bus->rxhdr, BRCMF_FIRSTREAD);

		brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_DATA_ON(),
				   pkt->data, len, "Rx Data:\n");

deliver:
		/* Save superframe descriptor and allocate packet frame */
		if (chan == SDPCM_GLOM_CHANNEL) {
			if (SDPCM_GLOMDESC(&bus->rxhdr[SDPCM_FRAMETAG_LEN])) {
				brcmf_dbg(GLOM, "glom descriptor, %d bytes:\n",
					  len);
				brcmf_dbg_hex_dump(BRCMF_GLOM_ON(),
						   pkt->data, len,
						   "Glom Data:\n");
				__skb_trim(pkt, len);
				skb_pull(pkt, SDPCM_HDRLEN);
				bus->glomd = pkt;
			} else {
				brcmf_dbg(ERROR, "%s: glom superframe w/o "
					  "descriptor!\n", __func__);
				brcmf_sdbrcm_rxfail(bus, false, false);
			}
			continue;
		}

		/* Fill in packet len and prio, deliver upward */
		__skb_trim(pkt, len);
		skb_pull(pkt, doff);

		if (pkt->len == 0) {
			brcmu_pkt_buf_free_skb(pkt);
			continue;
		} else if (brcmf_proto_hdrpull(bus->sdiodev->dev, &ifidx,
			   pkt) != 0) {
			brcmf_dbg(ERROR, "rx protocol error\n");
			brcmu_pkt_buf_free_skb(pkt);
			bus->sdiodev->bus_if->dstats.rx_errors++;
			continue;
		}

		/* Unlock during rx call */
		up(&bus->sdsem);
		brcmf_rx_packet(bus->sdiodev->dev, ifidx, pkt);
		down(&bus->sdsem);
	}
	rxcount = maxframes - rxleft;
	/* Message if we hit the limit */
	if (!rxleft)
		brcmf_dbg(DATA, "hit rx limit of %d frames\n",
			  maxframes);
	else
		brcmf_dbg(DATA, "processed %d frames\n", rxcount);
	/* Back off rxseq if awaiting rtx, update rx_seq */
	if (bus->rxskip)
		rxseq--;
	bus->rx_seq = rxseq;

	return rxcount;
}

static void
brcmf_sdbrcm_wait_for_event(struct brcmf_sdio *bus, bool *lockvar)
{
	up(&bus->sdsem);
	wait_event_interruptible_timeout(bus->ctrl_wait, !*lockvar, HZ * 2);
	down(&bus->sdsem);
	return;
}

static void
brcmf_sdbrcm_wait_event_wakeup(struct brcmf_sdio *bus)
{
	if (waitqueue_active(&bus->ctrl_wait))
		wake_up_interruptible(&bus->ctrl_wait);
	return;
}

/* Writes a HW/SW header into the packet and sends it. */
/* Assumes: (a) header space already there, (b) caller holds lock */
static int brcmf_sdbrcm_txpkt(struct brcmf_sdio *bus, struct sk_buff *pkt,
			      uint chan, bool free_pkt)
{
	int ret;
	u8 *frame;
	u16 len, pad = 0;
	u32 swheader;
	struct sk_buff *new;
	int i;

	brcmf_dbg(TRACE, "Enter\n");

	frame = (u8 *) (pkt->data);

	/* Add alignment padding, allocate new packet if needed */
	pad = ((unsigned long)frame % BRCMF_SDALIGN);
	if (pad) {
		if (skb_headroom(pkt) < pad) {
			brcmf_dbg(INFO, "insufficient headroom %d for %d pad\n",
				  skb_headroom(pkt), pad);
			bus->sdiodev->bus_if->tx_realloc++;
			new = brcmu_pkt_buf_get_skb(pkt->len + BRCMF_SDALIGN);
			if (!new) {
				brcmf_dbg(ERROR, "couldn't allocate new %d-byte packet\n",
					  pkt->len + BRCMF_SDALIGN);
				ret = -ENOMEM;
				goto done;
			}

			pkt_align(new, pkt->len, BRCMF_SDALIGN);
			memcpy(new->data, pkt->data, pkt->len);
			if (free_pkt)
				brcmu_pkt_buf_free_skb(pkt);
			/* free the pkt if canned one is not used */
			free_pkt = true;
			pkt = new;
			frame = (u8 *) (pkt->data);
			/* precondition: (frame % BRCMF_SDALIGN) == 0) */
			pad = 0;
		} else {
			skb_push(pkt, pad);
			frame = (u8 *) (pkt->data);
			/* precondition: pad + SDPCM_HDRLEN <= pkt->len */
			memset(frame, 0, pad + SDPCM_HDRLEN);
		}
	}
	/* precondition: pad < BRCMF_SDALIGN */

	/* Hardware tag: 2 byte len followed by 2 byte ~len check (all LE) */
	len = (u16) (pkt->len);
	*(__le16 *) frame = cpu_to_le16(len);
	*(((__le16 *) frame) + 1) = cpu_to_le16(~len);

	/* Software tag: channel, sequence number, data offset */
	swheader =
	    ((chan << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK) | bus->tx_seq |
	    (((pad +
	       SDPCM_HDRLEN) << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK);

	put_unaligned_le32(swheader, frame + SDPCM_FRAMETAG_LEN);
	put_unaligned_le32(0, frame + SDPCM_FRAMETAG_LEN + sizeof(swheader));

#ifdef DEBUG
	tx_packets[pkt->priority]++;
#endif

	brcmf_dbg_hex_dump(BRCMF_BYTES_ON() &&
			   ((BRCMF_CTL_ON() && chan == SDPCM_CONTROL_CHANNEL) ||
			    (BRCMF_DATA_ON() && chan != SDPCM_CONTROL_CHANNEL)),
			   frame, len, "Tx Frame:\n");
	brcmf_dbg_hex_dump(!(BRCMF_BYTES_ON() &&
			     ((BRCMF_CTL_ON() &&
			       chan == SDPCM_CONTROL_CHANNEL) ||
			      (BRCMF_DATA_ON() &&
			       chan != SDPCM_CONTROL_CHANNEL))) &&
			   BRCMF_HDRS_ON(),
			   frame, min_t(u16, len, 16), "TxHdr:\n");

	/* Raise len to next SDIO block to eliminate tail command */
	if (bus->roundup && bus->blocksize && (len > bus->blocksize)) {
		u16 pad = bus->blocksize - (len % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize))
				len += pad;
	} else if (len % BRCMF_SDALIGN) {
		len += BRCMF_SDALIGN - (len % BRCMF_SDALIGN);
	}

	/* Some controllers have trouble with odd bytes -- round to even */
	if (len & (ALIGNMENT - 1))
			len = roundup(len, ALIGNMENT);

	ret = brcmf_sdcard_send_pkt(bus->sdiodev, bus->sdiodev->sbwad,
				    SDIO_FUNC_2, F2SYNC, pkt);
	bus->f2txdata++;

	if (ret < 0) {
		/* On failure, abort the command and terminate the frame */
		brcmf_dbg(INFO, "sdio error %d, abort command and terminate frame\n",
			  ret);
		bus->tx_sderrs++;

		brcmf_sdcard_abort(bus->sdiodev, SDIO_FUNC_2);
		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
				 SBSDIO_FUNC1_FRAMECTRL, SFC_WF_TERM,
				 NULL);
		bus->f1regdata++;

		for (i = 0; i < 3; i++) {
			u8 hi, lo;
			hi = brcmf_sdcard_cfg_read(bus->sdiodev,
					     SDIO_FUNC_1,
					     SBSDIO_FUNC1_WFRAMEBCHI,
					     NULL);
			lo = brcmf_sdcard_cfg_read(bus->sdiodev,
					     SDIO_FUNC_1,
					     SBSDIO_FUNC1_WFRAMEBCLO,
					     NULL);
			bus->f1regdata += 2;
			if ((hi == 0) && (lo == 0))
				break;
		}

	}
	if (ret == 0)
		bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

done:
	/* restore pkt buffer pointer before calling tx complete routine */
	skb_pull(pkt, SDPCM_HDRLEN + pad);
	up(&bus->sdsem);
	brcmf_txcomplete(bus->sdiodev->dev, pkt, ret != 0);
	down(&bus->sdsem);

	if (free_pkt)
		brcmu_pkt_buf_free_skb(pkt);

	return ret;
}

static uint brcmf_sdbrcm_sendfromq(struct brcmf_sdio *bus, uint maxframes)
{
	struct sk_buff *pkt;
	u32 intstatus = 0;
	uint retries = 0;
	int ret = 0, prec_out;
	uint cnt = 0;
	uint datalen;
	u8 tx_prec_map;

	brcmf_dbg(TRACE, "Enter\n");

	tx_prec_map = ~bus->flowcontrol;

	/* Send frames until the limit or some other event */
	for (cnt = 0; (cnt < maxframes) && data_ok(bus); cnt++) {
		spin_lock_bh(&bus->txqlock);
		pkt = brcmu_pktq_mdeq(&bus->txq, tx_prec_map, &prec_out);
		if (pkt == NULL) {
			spin_unlock_bh(&bus->txqlock);
			break;
		}
		spin_unlock_bh(&bus->txqlock);
		datalen = pkt->len - SDPCM_HDRLEN;

		ret = brcmf_sdbrcm_txpkt(bus, pkt, SDPCM_DATA_CHANNEL, true);
		if (ret)
			bus->sdiodev->bus_if->dstats.tx_errors++;
		else
			bus->sdiodev->bus_if->dstats.tx_bytes += datalen;

		/* In poll mode, need to check for other events */
		if (!bus->intr && cnt) {
			/* Check device status, signal pending interrupt */
			r_sdreg32(bus, &intstatus,
				  offsetof(struct sdpcmd_regs, intstatus),
				  &retries);
			bus->f2txdata++;
			if (brcmf_sdcard_regfail(bus->sdiodev))
				break;
			if (intstatus & bus->hostintmask)
				bus->ipend = true;
		}
	}

	/* Deflow-control stack if needed */
	if (bus->sdiodev->bus_if->drvr_up &&
	    (bus->sdiodev->bus_if->state == BRCMF_BUS_DATA) &&
	    bus->txoff && (pktq_len(&bus->txq) < TXLOW)) {
		bus->txoff = OFF;
		brcmf_txflowcontrol(bus->sdiodev->dev, 0, OFF);
	}

	return cnt;
}

static void brcmf_sdbrcm_bus_stop(struct device *dev)
{
	u32 local_hostintmask;
	u8 saveclk;
	uint retries;
	int err;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;

	brcmf_dbg(TRACE, "Enter\n");

	if (bus->watchdog_tsk) {
		send_sig(SIGTERM, bus->watchdog_tsk, 1);
		kthread_stop(bus->watchdog_tsk);
		bus->watchdog_tsk = NULL;
	}

	if (bus->dpc_tsk && bus->dpc_tsk != current) {
		send_sig(SIGTERM, bus->dpc_tsk, 1);
		kthread_stop(bus->dpc_tsk);
		bus->dpc_tsk = NULL;
	}

	down(&bus->sdsem);

	bus_wake(bus);

	/* Enable clock for device interrupts */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

	/* Disable and clear interrupts at the chip level also */
	w_sdreg32(bus, 0, offsetof(struct sdpcmd_regs, hostintmask), &retries);
	local_hostintmask = bus->hostintmask;
	bus->hostintmask = 0;

	/* Change our idea of bus state */
	bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;

	/* Force clocks on backplane to be sure F2 interrupt propagates */
	saveclk = brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
					SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
				       SBSDIO_FUNC1_CHIPCLKCSR,
				       (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err)
		brcmf_dbg(ERROR, "Failed to force clock for F2: err %d\n", err);

	/* Turn off the bus (F2), free any pending packets */
	brcmf_dbg(INTR, "disable SDIO interrupts\n");
	brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_0, SDIO_CCCR_IOEx,
			 SDIO_FUNC_ENABLE_1, NULL);

	/* Clear any pending interrupts now that F2 is disabled */
	w_sdreg32(bus, local_hostintmask,
		  offsetof(struct sdpcmd_regs, intstatus), &retries);

	/* Turn off the backplane clock (only) */
	brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);

	/* Clear the data packet queues */
	brcmu_pktq_flush(&bus->txq, true, NULL, NULL);

	/* Clear any held glomming stuff */
	if (bus->glomd)
		brcmu_pkt_buf_free_skb(bus->glomd);
	brcmf_sdbrcm_free_glom(bus);

	/* Clear rx control and wake any waiters */
	bus->rxlen = 0;
	brcmf_sdbrcm_dcmd_resp_wake(bus);

	/* Reset some F2 state stuff */
	bus->rxskip = false;
	bus->tx_seq = bus->rx_seq = 0;

	up(&bus->sdsem);
}

#ifdef CONFIG_BRCMFMAC_SDIO_OOB
static inline void brcmf_sdbrcm_clrintr(struct brcmf_sdio *bus)
{
	unsigned long flags;

	spin_lock_irqsave(&bus->sdiodev->irq_en_lock, flags);
	if (!bus->sdiodev->irq_en && !bus->ipend) {
		enable_irq(bus->sdiodev->irq);
		bus->sdiodev->irq_en = true;
	}
	spin_unlock_irqrestore(&bus->sdiodev->irq_en_lock, flags);
}
#else
static inline void brcmf_sdbrcm_clrintr(struct brcmf_sdio *bus)
{
}
#endif		/* CONFIG_BRCMFMAC_SDIO_OOB */

static bool brcmf_sdbrcm_dpc(struct brcmf_sdio *bus)
{
	u32 intstatus, newstatus = 0;
	uint retries = 0;
	uint rxlimit = bus->rxbound;	/* Rx frames to read before resched */
	uint txlimit = bus->txbound;	/* Tx frames to send before resched */
	uint framecnt = 0;	/* Temporary counter of tx/rx frames */
	bool rxdone = true;	/* Flag for no more read data */
	bool resched = false;	/* Flag indicating resched wanted */

	brcmf_dbg(TRACE, "Enter\n");

	/* Start with leftover status bits */
	intstatus = bus->intstatus;

	down(&bus->sdsem);

	/* If waiting for HTAVAIL, check status */
	if (bus->clkstate == CLK_PENDING) {
		int err;
		u8 clkctl, devctl = 0;

#ifdef DEBUG
		/* Check for inconsistent device control */
		devctl = brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
					       SBSDIO_DEVICE_CTL, &err);
		if (err) {
			brcmf_dbg(ERROR, "error reading DEVCTL: %d\n", err);
			bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
		}
#endif				/* DEBUG */

		/* Read CSR, if clock on switch to AVAIL, else ignore */
		clkctl = brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
					       SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			brcmf_dbg(ERROR, "error reading CSR: %d\n",
				  err);
			bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
		}

		brcmf_dbg(INFO, "DPC: PENDING, devctl 0x%02x clkctl 0x%02x\n",
			  devctl, clkctl);

		if (SBSDIO_HTAV(clkctl)) {
			devctl = brcmf_sdcard_cfg_read(bus->sdiodev,
						       SDIO_FUNC_1,
						       SBSDIO_DEVICE_CTL, &err);
			if (err) {
				brcmf_dbg(ERROR, "error reading DEVCTL: %d\n",
					  err);
				bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
			}
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
				SBSDIO_DEVICE_CTL, devctl, &err);
			if (err) {
				brcmf_dbg(ERROR, "error writing DEVCTL: %d\n",
					  err);
				bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
			}
			bus->clkstate = CLK_AVAIL;
		} else {
			goto clkwait;
		}
	}

	bus_wake(bus);

	/* Make sure backplane clock is on */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, true);
	if (bus->clkstate == CLK_PENDING)
		goto clkwait;

	/* Pending interrupt indicates new device status */
	if (bus->ipend) {
		bus->ipend = false;
		r_sdreg32(bus, &newstatus,
			  offsetof(struct sdpcmd_regs, intstatus), &retries);
		bus->f1regdata++;
		if (brcmf_sdcard_regfail(bus->sdiodev))
			newstatus = 0;
		newstatus &= bus->hostintmask;
		bus->fcstate = !!(newstatus & I_HMB_FC_STATE);
		if (newstatus) {
			w_sdreg32(bus, newstatus,
				  offsetof(struct sdpcmd_regs, intstatus),
				  &retries);
			bus->f1regdata++;
		}
	}

	/* Merge new bits with previous */
	intstatus |= newstatus;
	bus->intstatus = 0;

	/* Handle flow-control change: read new state in case our ack
	 * crossed another change interrupt.  If change still set, assume
	 * FC ON for safety, let next loop through do the debounce.
	 */
	if (intstatus & I_HMB_FC_CHANGE) {
		intstatus &= ~I_HMB_FC_CHANGE;
		w_sdreg32(bus, I_HMB_FC_CHANGE,
			  offsetof(struct sdpcmd_regs, intstatus), &retries);

		r_sdreg32(bus, &newstatus,
			  offsetof(struct sdpcmd_regs, intstatus), &retries);
		bus->f1regdata += 2;
		bus->fcstate =
		    !!(newstatus & (I_HMB_FC_STATE | I_HMB_FC_CHANGE));
		intstatus |= (newstatus & bus->hostintmask);
	}

	/* Handle host mailbox indication */
	if (intstatus & I_HMB_HOST_INT) {
		intstatus &= ~I_HMB_HOST_INT;
		intstatus |= brcmf_sdbrcm_hostmail(bus);
	}

	/* Generally don't ask for these, can get CRC errors... */
	if (intstatus & I_WR_OOSYNC) {
		brcmf_dbg(ERROR, "Dongle reports WR_OOSYNC\n");
		intstatus &= ~I_WR_OOSYNC;
	}

	if (intstatus & I_RD_OOSYNC) {
		brcmf_dbg(ERROR, "Dongle reports RD_OOSYNC\n");
		intstatus &= ~I_RD_OOSYNC;
	}

	if (intstatus & I_SBINT) {
		brcmf_dbg(ERROR, "Dongle reports SBINT\n");
		intstatus &= ~I_SBINT;
	}

	/* Would be active due to wake-wlan in gSPI */
	if (intstatus & I_CHIPACTIVE) {
		brcmf_dbg(INFO, "Dongle reports CHIPACTIVE\n");
		intstatus &= ~I_CHIPACTIVE;
	}

	/* Ignore frame indications if rxskip is set */
	if (bus->rxskip)
		intstatus &= ~I_HMB_FRAME_IND;

	/* On frame indication, read available frames */
	if (PKT_AVAILABLE()) {
		framecnt = brcmf_sdbrcm_readframes(bus, rxlimit, &rxdone);
		if (rxdone || bus->rxskip)
			intstatus &= ~I_HMB_FRAME_IND;
		rxlimit -= min(framecnt, rxlimit);
	}

	/* Keep still-pending events for next scheduling */
	bus->intstatus = intstatus;

clkwait:
	brcmf_sdbrcm_clrintr(bus);

	if (data_ok(bus) && bus->ctrl_frame_stat &&
		(bus->clkstate == CLK_AVAIL)) {
		int ret, i;

		ret = brcmf_sdcard_send_buf(bus->sdiodev, bus->sdiodev->sbwad,
			SDIO_FUNC_2, F2SYNC, (u8 *) bus->ctrl_frame_buf,
			(u32) bus->ctrl_frame_len);

		if (ret < 0) {
			/* On failure, abort the command and
				terminate the frame */
			brcmf_dbg(INFO, "sdio error %d, abort command and terminate frame\n",
				  ret);
			bus->tx_sderrs++;

			brcmf_sdcard_abort(bus->sdiodev, SDIO_FUNC_2);

			brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
					 SBSDIO_FUNC1_FRAMECTRL, SFC_WF_TERM,
					 NULL);
			bus->f1regdata++;

			for (i = 0; i < 3; i++) {
				u8 hi, lo;
				hi = brcmf_sdcard_cfg_read(bus->sdiodev,
						     SDIO_FUNC_1,
						     SBSDIO_FUNC1_WFRAMEBCHI,
						     NULL);
				lo = brcmf_sdcard_cfg_read(bus->sdiodev,
						     SDIO_FUNC_1,
						     SBSDIO_FUNC1_WFRAMEBCLO,
						     NULL);
				bus->f1regdata += 2;
				if ((hi == 0) && (lo == 0))
					break;
			}

		}
		if (ret == 0)
			bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

		brcmf_dbg(INFO, "Return_dpc value is : %d\n", ret);
		bus->ctrl_frame_stat = false;
		brcmf_sdbrcm_wait_event_wakeup(bus);
	}
	/* Send queued frames (limit 1 if rx may still be pending) */
	else if ((bus->clkstate == CLK_AVAIL) && !bus->fcstate &&
		 brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol) && txlimit
		 && data_ok(bus)) {
		framecnt = rxdone ? txlimit : min(txlimit, bus->txminmax);
		framecnt = brcmf_sdbrcm_sendfromq(bus, framecnt);
		txlimit -= framecnt;
	}

	/* Resched if events or tx frames are pending,
		 else await next interrupt */
	/* On failed register access, all bets are off:
		 no resched or interrupts */
	if ((bus->sdiodev->bus_if->state == BRCMF_BUS_DOWN) ||
	    brcmf_sdcard_regfail(bus->sdiodev)) {
		brcmf_dbg(ERROR, "failed backplane access over SDIO, halting operation %d\n",
			  brcmf_sdcard_regfail(bus->sdiodev));
		bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
		bus->intstatus = 0;
	} else if (bus->clkstate == CLK_PENDING) {
		brcmf_dbg(INFO, "rescheduled due to CLK_PENDING awaiting I_CHIPACTIVE interrupt\n");
		resched = true;
	} else if (bus->intstatus || bus->ipend ||
		(!bus->fcstate && brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol)
		 && data_ok(bus)) || PKT_AVAILABLE()) {
		resched = true;
	}

	bus->dpc_sched = resched;

	/* If we're done for now, turn off clock request. */
	if ((bus->clkstate != CLK_PENDING)
	    && bus->idletime == BRCMF_IDLE_IMMEDIATE) {
		bus->activity = false;
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);
	}

	up(&bus->sdsem);

	return resched;
}

static inline void brcmf_sdbrcm_adddpctsk(struct brcmf_sdio *bus)
{
	struct list_head *new_hd;
	unsigned long flags;

	if (in_interrupt())
		new_hd = kzalloc(sizeof(struct list_head), GFP_ATOMIC);
	else
		new_hd = kzalloc(sizeof(struct list_head), GFP_KERNEL);
	if (new_hd == NULL)
		return;

	spin_lock_irqsave(&bus->dpc_tl_lock, flags);
	list_add_tail(new_hd, &bus->dpc_tsklst);
	spin_unlock_irqrestore(&bus->dpc_tl_lock, flags);
}

static int brcmf_sdbrcm_dpc_thread(void *data)
{
	struct brcmf_sdio *bus = (struct brcmf_sdio *) data;
	struct list_head *cur_hd, *tmp_hd;
	unsigned long flags;

	allow_signal(SIGTERM);
	/* Run until signal received */
	while (1) {
		if (kthread_should_stop())
			break;

		if (list_empty(&bus->dpc_tsklst))
			if (wait_for_completion_interruptible(&bus->dpc_wait))
				break;

		spin_lock_irqsave(&bus->dpc_tl_lock, flags);
		list_for_each_safe(cur_hd, tmp_hd, &bus->dpc_tsklst) {
			spin_unlock_irqrestore(&bus->dpc_tl_lock, flags);

			if (bus->sdiodev->bus_if->state == BRCMF_BUS_DOWN) {
				/* after stopping the bus, exit thread */
				brcmf_sdbrcm_bus_stop(bus->sdiodev->dev);
				bus->dpc_tsk = NULL;
				spin_lock_irqsave(&bus->dpc_tl_lock, flags);
				break;
			}

			if (brcmf_sdbrcm_dpc(bus))
				brcmf_sdbrcm_adddpctsk(bus);

			spin_lock_irqsave(&bus->dpc_tl_lock, flags);
			list_del(cur_hd);
			kfree(cur_hd);
		}
		spin_unlock_irqrestore(&bus->dpc_tl_lock, flags);
	}
	return 0;
}

static int brcmf_sdbrcm_bus_txdata(struct device *dev, struct sk_buff *pkt)
{
	int ret = -EBADE;
	uint datalen, prec;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;

	brcmf_dbg(TRACE, "Enter\n");

	datalen = pkt->len;

	/* Add space for the header */
	skb_push(pkt, SDPCM_HDRLEN);
	/* precondition: IS_ALIGNED((unsigned long)(pkt->data), 2) */

	prec = prio2prec((pkt->priority & PRIOMASK));

	/* Check for existing queue, current flow-control,
			 pending event, or pending clock */
	brcmf_dbg(TRACE, "deferring pktq len %d\n", pktq_len(&bus->txq));
	bus->fcqueued++;

	/* Priority based enq */
	spin_lock_bh(&bus->txqlock);
	if (!brcmf_c_prec_enq(bus->sdiodev->dev, &bus->txq, pkt, prec)) {
		skb_pull(pkt, SDPCM_HDRLEN);
		brcmf_txcomplete(bus->sdiodev->dev, pkt, false);
		brcmu_pkt_buf_free_skb(pkt);
		brcmf_dbg(ERROR, "out of bus->txq !!!\n");
		ret = -ENOSR;
	} else {
		ret = 0;
	}
	spin_unlock_bh(&bus->txqlock);

	if (pktq_len(&bus->txq) >= TXHI) {
		bus->txoff = ON;
		brcmf_txflowcontrol(bus->sdiodev->dev, 0, ON);
	}

#ifdef DEBUG
	if (pktq_plen(&bus->txq, prec) > qcount[prec])
		qcount[prec] = pktq_plen(&bus->txq, prec);
#endif
	/* Schedule DPC if needed to send queued packet(s) */
	if (!bus->dpc_sched) {
		bus->dpc_sched = true;
		if (bus->dpc_tsk) {
			brcmf_sdbrcm_adddpctsk(bus);
			complete(&bus->dpc_wait);
		}
	}

	return ret;
}

static int
brcmf_sdbrcm_membytes(struct brcmf_sdio *bus, bool write, u32 address, u8 *data,
		 uint size)
{
	int bcmerror = 0;
	u32 sdaddr;
	uint dsize;

	/* Determine initial transfer parameters */
	sdaddr = address & SBSDIO_SB_OFT_ADDR_MASK;
	if ((sdaddr + size) & SBSDIO_SBWINDOW_MASK)
		dsize = (SBSDIO_SB_OFT_ADDR_LIMIT - sdaddr);
	else
		dsize = size;

	/* Set the backplane window to include the start address */
	bcmerror = brcmf_sdcard_set_sbaddr_window(bus->sdiodev, address);
	if (bcmerror) {
		brcmf_dbg(ERROR, "window change failed\n");
		goto xfer_done;
	}

	/* Do the transfer(s) */
	while (size) {
		brcmf_dbg(INFO, "%s %d bytes at offset 0x%08x in window 0x%08x\n",
			  write ? "write" : "read", dsize,
			  sdaddr, address & SBSDIO_SBWINDOW_MASK);
		bcmerror = brcmf_sdcard_rwdata(bus->sdiodev, write,
					       sdaddr, data, dsize);
		if (bcmerror) {
			brcmf_dbg(ERROR, "membytes transfer failed\n");
			break;
		}

		/* Adjust for next transfer (if any) */
		size -= dsize;
		if (size) {
			data += dsize;
			address += dsize;
			bcmerror = brcmf_sdcard_set_sbaddr_window(bus->sdiodev,
								  address);
			if (bcmerror) {
				brcmf_dbg(ERROR, "window change failed\n");
				break;
			}
			sdaddr = 0;
			dsize = min_t(uint, SBSDIO_SB_OFT_ADDR_LIMIT, size);
		}
	}

xfer_done:
	/* Return the window to backplane enumeration space for core access */
	if (brcmf_sdcard_set_sbaddr_window(bus->sdiodev, bus->sdiodev->sbwad))
		brcmf_dbg(ERROR, "FAILED to set window back to 0x%x\n",
			  bus->sdiodev->sbwad);

	return bcmerror;
}

#ifdef DEBUG
#define CONSOLE_LINE_MAX	192

static int brcmf_sdbrcm_readconsole(struct brcmf_sdio *bus)
{
	struct brcmf_console *c = &bus->console;
	u8 line[CONSOLE_LINE_MAX], ch;
	u32 n, idx, addr;
	int rv;

	/* Don't do anything until FWREADY updates console address */
	if (bus->console_addr == 0)
		return 0;

	/* Read console log struct */
	addr = bus->console_addr + offsetof(struct rte_console, log_le);
	rv = brcmf_sdbrcm_membytes(bus, false, addr, (u8 *)&c->log_le,
				   sizeof(c->log_le));
	if (rv < 0)
		return rv;

	/* Allocate console buffer (one time only) */
	if (c->buf == NULL) {
		c->bufsize = le32_to_cpu(c->log_le.buf_size);
		c->buf = kmalloc(c->bufsize, GFP_ATOMIC);
		if (c->buf == NULL)
			return -ENOMEM;
	}

	idx = le32_to_cpu(c->log_le.idx);

	/* Protect against corrupt value */
	if (idx > c->bufsize)
		return -EBADE;

	/* Skip reading the console buffer if the index pointer
	 has not moved */
	if (idx == c->last)
		return 0;

	/* Read the console buffer */
	addr = le32_to_cpu(c->log_le.buf);
	rv = brcmf_sdbrcm_membytes(bus, false, addr, c->buf, c->bufsize);
	if (rv < 0)
		return rv;

	while (c->last != idx) {
		for (n = 0; n < CONSOLE_LINE_MAX - 2; n++) {
			if (c->last == idx) {
				/* This would output a partial line.
				 * Instead, back up
				 * the buffer pointer and output this
				 * line next time around.
				 */
				if (c->last >= n)
					c->last -= n;
				else
					c->last = c->bufsize - n;
				goto break2;
			}
			ch = c->buf[c->last];
			c->last = (c->last + 1) % c->bufsize;
			if (ch == '\n')
				break;
			line[n] = ch;
		}

		if (n > 0) {
			if (line[n - 1] == '\r')
				n--;
			line[n] = 0;
			pr_debug("CONSOLE: %s\n", line);
		}
	}
break2:

	return 0;
}
#endif				/* DEBUG */

static int brcmf_tx_frame(struct brcmf_sdio *bus, u8 *frame, u16 len)
{
	int i;
	int ret;

	bus->ctrl_frame_stat = false;
	ret = brcmf_sdcard_send_buf(bus->sdiodev, bus->sdiodev->sbwad,
				    SDIO_FUNC_2, F2SYNC, frame, len);

	if (ret < 0) {
		/* On failure, abort the command and terminate the frame */
		brcmf_dbg(INFO, "sdio error %d, abort command and terminate frame\n",
			  ret);
		bus->tx_sderrs++;

		brcmf_sdcard_abort(bus->sdiodev, SDIO_FUNC_2);

		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
				       SBSDIO_FUNC1_FRAMECTRL,
				       SFC_WF_TERM, NULL);
		bus->f1regdata++;

		for (i = 0; i < 3; i++) {
			u8 hi, lo;
			hi = brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
						   SBSDIO_FUNC1_WFRAMEBCHI,
						   NULL);
			lo = brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
						   SBSDIO_FUNC1_WFRAMEBCLO,
						   NULL);
			bus->f1regdata += 2;
			if (hi == 0 && lo == 0)
				break;
		}
		return ret;
	}

	bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

	return ret;
}

static int
brcmf_sdbrcm_bus_txctl(struct device *dev, unsigned char *msg, uint msglen)
{
	u8 *frame;
	u16 len;
	u32 swheader;
	uint retries = 0;
	u8 doff = 0;
	int ret = -1;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;

	brcmf_dbg(TRACE, "Enter\n");

	/* Back the pointer to make a room for bus header */
	frame = msg - SDPCM_HDRLEN;
	len = (msglen += SDPCM_HDRLEN);

	/* Add alignment padding (optional for ctl frames) */
	doff = ((unsigned long)frame % BRCMF_SDALIGN);
	if (doff) {
		frame -= doff;
		len += doff;
		msglen += doff;
		memset(frame, 0, doff + SDPCM_HDRLEN);
	}
	/* precondition: doff < BRCMF_SDALIGN */
	doff += SDPCM_HDRLEN;

	/* Round send length to next SDIO block */
	if (bus->roundup && bus->blocksize && (len > bus->blocksize)) {
		u16 pad = bus->blocksize - (len % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize))
			len += pad;
	} else if (len % BRCMF_SDALIGN) {
		len += BRCMF_SDALIGN - (len % BRCMF_SDALIGN);
	}

	/* Satisfy length-alignment requirements */
	if (len & (ALIGNMENT - 1))
		len = roundup(len, ALIGNMENT);

	/* precondition: IS_ALIGNED((unsigned long)frame, 2) */

	/* Need to lock here to protect txseq and SDIO tx calls */
	down(&bus->sdsem);

	bus_wake(bus);

	/* Make sure backplane clock is on */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

	/* Hardware tag: 2 byte len followed by 2 byte ~len check (all LE) */
	*(__le16 *) frame = cpu_to_le16((u16) msglen);
	*(((__le16 *) frame) + 1) = cpu_to_le16(~msglen);

	/* Software tag: channel, sequence number, data offset */
	swheader =
	    ((SDPCM_CONTROL_CHANNEL << SDPCM_CHANNEL_SHIFT) &
	     SDPCM_CHANNEL_MASK)
	    | bus->tx_seq | ((doff << SDPCM_DOFFSET_SHIFT) &
			     SDPCM_DOFFSET_MASK);
	put_unaligned_le32(swheader, frame + SDPCM_FRAMETAG_LEN);
	put_unaligned_le32(0, frame + SDPCM_FRAMETAG_LEN + sizeof(swheader));

	if (!data_ok(bus)) {
		brcmf_dbg(INFO, "No bus credit bus->tx_max %d, bus->tx_seq %d\n",
			  bus->tx_max, bus->tx_seq);
		bus->ctrl_frame_stat = true;
		/* Send from dpc */
		bus->ctrl_frame_buf = frame;
		bus->ctrl_frame_len = len;

		brcmf_sdbrcm_wait_for_event(bus, &bus->ctrl_frame_stat);

		if (!bus->ctrl_frame_stat) {
			brcmf_dbg(INFO, "ctrl_frame_stat == false\n");
			ret = 0;
		} else {
			brcmf_dbg(INFO, "ctrl_frame_stat == true\n");
			ret = -1;
		}
	}

	if (ret == -1) {
		brcmf_dbg_hex_dump(BRCMF_BYTES_ON() && BRCMF_CTL_ON(),
				   frame, len, "Tx Frame:\n");
		brcmf_dbg_hex_dump(!(BRCMF_BYTES_ON() && BRCMF_CTL_ON()) &&
				   BRCMF_HDRS_ON(),
				   frame, min_t(u16, len, 16), "TxHdr:\n");

		do {
			ret = brcmf_tx_frame(bus, frame, len);
		} while (ret < 0 && retries++ < TXRETRIES);
	}

	if ((bus->idletime == BRCMF_IDLE_IMMEDIATE) && !bus->dpc_sched) {
		bus->activity = false;
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, true);
	}

	up(&bus->sdsem);

	if (ret)
		bus->tx_ctlerrs++;
	else
		bus->tx_ctlpkts++;

	return ret ? -EIO : 0;
}

static int
brcmf_sdbrcm_bus_rxctl(struct device *dev, unsigned char *msg, uint msglen)
{
	int timeleft;
	uint rxlen = 0;
	bool pending;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;

	brcmf_dbg(TRACE, "Enter\n");

	/* Wait until control frame is available */
	timeleft = brcmf_sdbrcm_dcmd_resp_wait(bus, &bus->rxlen, &pending);

	down(&bus->sdsem);
	rxlen = bus->rxlen;
	memcpy(msg, bus->rxctl, min(msglen, rxlen));
	bus->rxlen = 0;
	up(&bus->sdsem);

	if (rxlen) {
		brcmf_dbg(CTL, "resumed on rxctl frame, got %d expected %d\n",
			  rxlen, msglen);
	} else if (timeleft == 0) {
		brcmf_dbg(ERROR, "resumed on timeout\n");
	} else if (pending) {
		brcmf_dbg(CTL, "cancelled\n");
		return -ERESTARTSYS;
	} else {
		brcmf_dbg(CTL, "resumed for unknown reason?\n");
	}

	if (rxlen)
		bus->rx_ctlpkts++;
	else
		bus->rx_ctlerrs++;

	return rxlen ? (int)rxlen : -ETIMEDOUT;
}

static int brcmf_sdbrcm_downloadvars(struct brcmf_sdio *bus, void *arg, int len)
{
	int bcmerror = 0;

	brcmf_dbg(TRACE, "Enter\n");

	/* Basic sanity checks */
	if (bus->sdiodev->bus_if->drvr_up) {
		bcmerror = -EISCONN;
		goto err;
	}
	if (!len) {
		bcmerror = -EOVERFLOW;
		goto err;
	}

	/* Free the old ones and replace with passed variables */
	kfree(bus->vars);

	bus->vars = kmalloc(len, GFP_ATOMIC);
	bus->varsz = bus->vars ? len : 0;
	if (bus->vars == NULL) {
		bcmerror = -ENOMEM;
		goto err;
	}

	/* Copy the passed variables, which should include the
		 terminating double-null */
	memcpy(bus->vars, arg, bus->varsz);
err:
	return bcmerror;
}

static int brcmf_sdbrcm_write_vars(struct brcmf_sdio *bus)
{
	int bcmerror = 0;
	u32 varsize;
	u32 varaddr;
	u8 *vbuffer;
	u32 varsizew;
	__le32 varsizew_le;
#ifdef DEBUG
	char *nvram_ularray;
#endif				/* DEBUG */

	/* Even if there are no vars are to be written, we still
		 need to set the ramsize. */
	varsize = bus->varsz ? roundup(bus->varsz, 4) : 0;
	varaddr = (bus->ramsize - 4) - varsize;

	if (bus->vars) {
		vbuffer = kzalloc(varsize, GFP_ATOMIC);
		if (!vbuffer)
			return -ENOMEM;

		memcpy(vbuffer, bus->vars, bus->varsz);

		/* Write the vars list */
		bcmerror =
		    brcmf_sdbrcm_membytes(bus, true, varaddr, vbuffer, varsize);
#ifdef DEBUG
		/* Verify NVRAM bytes */
		brcmf_dbg(INFO, "Compare NVRAM dl & ul; varsize=%d\n", varsize);
		nvram_ularray = kmalloc(varsize, GFP_ATOMIC);
		if (!nvram_ularray) {
			kfree(vbuffer);
			return -ENOMEM;
		}

		/* Upload image to verify downloaded contents. */
		memset(nvram_ularray, 0xaa, varsize);

		/* Read the vars list to temp buffer for comparison */
		bcmerror =
		    brcmf_sdbrcm_membytes(bus, false, varaddr, nvram_ularray,
				     varsize);
		if (bcmerror) {
			brcmf_dbg(ERROR, "error %d on reading %d nvram bytes at 0x%08x\n",
				  bcmerror, varsize, varaddr);
		}
		/* Compare the org NVRAM with the one read from RAM */
		if (memcmp(vbuffer, nvram_ularray, varsize))
			brcmf_dbg(ERROR, "Downloaded NVRAM image is corrupted\n");
		else
			brcmf_dbg(ERROR, "Download/Upload/Compare of NVRAM ok\n");

		kfree(nvram_ularray);
#endif				/* DEBUG */

		kfree(vbuffer);
	}

	/* adjust to the user specified RAM */
	brcmf_dbg(INFO, "Physical memory size: %d\n", bus->ramsize);
	brcmf_dbg(INFO, "Vars are at %d, orig varsize is %d\n",
		  varaddr, varsize);
	varsize = ((bus->ramsize - 4) - varaddr);

	/*
	 * Determine the length token:
	 * Varsize, converted to words, in lower 16-bits, checksum
	 * in upper 16-bits.
	 */
	if (bcmerror) {
		varsizew = 0;
		varsizew_le = cpu_to_le32(0);
	} else {
		varsizew = varsize / 4;
		varsizew = (~varsizew << 16) | (varsizew & 0x0000FFFF);
		varsizew_le = cpu_to_le32(varsizew);
	}

	brcmf_dbg(INFO, "New varsize is %d, length token=0x%08x\n",
		  varsize, varsizew);

	/* Write the length token to the last word */
	bcmerror = brcmf_sdbrcm_membytes(bus, true, (bus->ramsize - 4),
					 (u8 *)&varsizew_le, 4);

	return bcmerror;
}

static int brcmf_sdbrcm_download_state(struct brcmf_sdio *bus, bool enter)
{
	uint retries;
	int bcmerror = 0;
	struct chip_info *ci = bus->ci;

	/* To enter download state, disable ARM and reset SOCRAM.
	 * To exit download state, simply reset ARM (default is RAM boot).
	 */
	if (enter) {
		bus->alp_only = true;

		ci->coredisable(bus->sdiodev, ci, BCMA_CORE_ARM_CM3);

		ci->resetcore(bus->sdiodev, ci, BCMA_CORE_INTERNAL_MEM);

		/* Clear the top bit of memory */
		if (bus->ramsize) {
			u32 zeros = 0;
			brcmf_sdbrcm_membytes(bus, true, bus->ramsize - 4,
					 (u8 *)&zeros, 4);
		}
	} else {
		if (!ci->iscoreup(bus->sdiodev, ci, BCMA_CORE_INTERNAL_MEM)) {
			brcmf_dbg(ERROR, "SOCRAM core is down after reset?\n");
			bcmerror = -EBADE;
			goto fail;
		}

		bcmerror = brcmf_sdbrcm_write_vars(bus);
		if (bcmerror) {
			brcmf_dbg(ERROR, "no vars written to RAM\n");
			bcmerror = 0;
		}

		w_sdreg32(bus, 0xFFFFFFFF,
			  offsetof(struct sdpcmd_regs, intstatus), &retries);

		ci->resetcore(bus->sdiodev, ci, BCMA_CORE_ARM_CM3);

		/* Allow HT Clock now that the ARM is running. */
		bus->alp_only = false;

		bus->sdiodev->bus_if->state = BRCMF_BUS_LOAD;
	}
fail:
	return bcmerror;
}

static int brcmf_sdbrcm_get_image(char *buf, int len, struct brcmf_sdio *bus)
{
	if (bus->firmware->size < bus->fw_ptr + len)
		len = bus->firmware->size - bus->fw_ptr;

	memcpy(buf, &bus->firmware->data[bus->fw_ptr], len);
	bus->fw_ptr += len;
	return len;
}

static int brcmf_sdbrcm_download_code_file(struct brcmf_sdio *bus)
{
	int offset = 0;
	uint len;
	u8 *memblock = NULL, *memptr;
	int ret;

	brcmf_dbg(INFO, "Enter\n");

	ret = request_firmware(&bus->firmware, BRCMF_SDIO_FW_NAME,
			       &bus->sdiodev->func[2]->dev);
	if (ret) {
		brcmf_dbg(ERROR, "Fail to request firmware %d\n", ret);
		return ret;
	}
	bus->fw_ptr = 0;

	memptr = memblock = kmalloc(MEMBLOCK + BRCMF_SDALIGN, GFP_ATOMIC);
	if (memblock == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	if ((u32)(unsigned long)memblock % BRCMF_SDALIGN)
		memptr += (BRCMF_SDALIGN -
			   ((u32)(unsigned long)memblock % BRCMF_SDALIGN));

	/* Download image */
	while ((len =
		brcmf_sdbrcm_get_image((char *)memptr, MEMBLOCK, bus))) {
		ret = brcmf_sdbrcm_membytes(bus, true, offset, memptr, len);
		if (ret) {
			brcmf_dbg(ERROR, "error %d on writing %d membytes at 0x%08x\n",
				  ret, MEMBLOCK, offset);
			goto err;
		}

		offset += MEMBLOCK;
	}

err:
	kfree(memblock);

	release_firmware(bus->firmware);
	bus->fw_ptr = 0;

	return ret;
}

/*
 * ProcessVars:Takes a buffer of "<var>=<value>\n" lines read from a file
 * and ending in a NUL.
 * Removes carriage returns, empty lines, comment lines, and converts
 * newlines to NULs.
 * Shortens buffer as needed and pads with NULs.  End of buffer is marked
 * by two NULs.
*/

static uint brcmf_process_nvram_vars(char *varbuf, uint len)
{
	char *dp;
	bool findNewline;
	int column;
	uint buf_len, n;

	dp = varbuf;

	findNewline = false;
	column = 0;

	for (n = 0; n < len; n++) {
		if (varbuf[n] == 0)
			break;
		if (varbuf[n] == '\r')
			continue;
		if (findNewline && varbuf[n] != '\n')
			continue;
		findNewline = false;
		if (varbuf[n] == '#') {
			findNewline = true;
			continue;
		}
		if (varbuf[n] == '\n') {
			if (column == 0)
				continue;
			*dp++ = 0;
			column = 0;
			continue;
		}
		*dp++ = varbuf[n];
		column++;
	}
	buf_len = dp - varbuf;

	while (dp < varbuf + n)
		*dp++ = 0;

	return buf_len;
}

static int brcmf_sdbrcm_download_nvram(struct brcmf_sdio *bus)
{
	uint len;
	char *memblock = NULL;
	char *bufp;
	int ret;

	ret = request_firmware(&bus->firmware, BRCMF_SDIO_NV_NAME,
			       &bus->sdiodev->func[2]->dev);
	if (ret) {
		brcmf_dbg(ERROR, "Fail to request nvram %d\n", ret);
		return ret;
	}
	bus->fw_ptr = 0;

	memblock = kmalloc(MEMBLOCK, GFP_ATOMIC);
	if (memblock == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	len = brcmf_sdbrcm_get_image(memblock, MEMBLOCK, bus);

	if (len > 0 && len < MEMBLOCK) {
		bufp = (char *)memblock;
		bufp[len] = 0;
		len = brcmf_process_nvram_vars(bufp, len);
		bufp += len;
		*bufp++ = 0;
		if (len)
			ret = brcmf_sdbrcm_downloadvars(bus, memblock, len + 1);
		if (ret)
			brcmf_dbg(ERROR, "error downloading vars: %d\n", ret);
	} else {
		brcmf_dbg(ERROR, "error reading nvram file: %d\n", len);
		ret = -EIO;
	}

err:
	kfree(memblock);

	release_firmware(bus->firmware);
	bus->fw_ptr = 0;

	return ret;
}

static int _brcmf_sdbrcm_download_firmware(struct brcmf_sdio *bus)
{
	int bcmerror = -1;

	/* Keep arm in reset */
	if (brcmf_sdbrcm_download_state(bus, true)) {
		brcmf_dbg(ERROR, "error placing ARM core in reset\n");
		goto err;
	}

	/* External image takes precedence if specified */
	if (brcmf_sdbrcm_download_code_file(bus)) {
		brcmf_dbg(ERROR, "dongle image file download failed\n");
		goto err;
	}

	/* External nvram takes precedence if specified */
	if (brcmf_sdbrcm_download_nvram(bus))
		brcmf_dbg(ERROR, "dongle nvram file download failed\n");

	/* Take arm out of reset */
	if (brcmf_sdbrcm_download_state(bus, false)) {
		brcmf_dbg(ERROR, "error getting out of ARM core reset\n");
		goto err;
	}

	bcmerror = 0;

err:
	return bcmerror;
}

static bool
brcmf_sdbrcm_download_firmware(struct brcmf_sdio *bus)
{
	bool ret;

	/* Download the firmware */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

	ret = _brcmf_sdbrcm_download_firmware(bus) == 0;

	brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);

	return ret;
}

static int brcmf_sdbrcm_bus_init(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_sdio_dev *sdiodev = bus_if->bus_priv.sdio;
	struct brcmf_sdio *bus = sdiodev->bus;
	unsigned long timeout;
	uint retries = 0;
	u8 ready, enable;
	int err, ret = 0;
	u8 saveclk;

	brcmf_dbg(TRACE, "Enter\n");

	/* try to download image and nvram to the dongle */
	if (bus_if->state == BRCMF_BUS_DOWN) {
		if (!(brcmf_sdbrcm_download_firmware(bus)))
			return -1;
	}

	if (!bus->sdiodev->bus_if->drvr)
		return 0;

	/* Start the watchdog timer */
	bus->tickcnt = 0;
	brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);

	down(&bus->sdsem);

	/* Make sure backplane clock is on, needed to generate F2 interrupt */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
	if (bus->clkstate != CLK_AVAIL)
		goto exit;

	/* Force clocks on backplane to be sure F2 interrupt propagates */
	saveclk =
	    brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
				  SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
				       SBSDIO_FUNC1_CHIPCLKCSR,
				       (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err) {
		brcmf_dbg(ERROR, "Failed to force clock for F2: err %d\n", err);
		goto exit;
	}

	/* Enable function 2 (frame transfers) */
	w_sdreg32(bus, SDPCM_PROT_VERSION << SMB_DATA_VERSION_SHIFT,
		  offsetof(struct sdpcmd_regs, tosbmailboxdata), &retries);
	enable = (SDIO_FUNC_ENABLE_1 | SDIO_FUNC_ENABLE_2);

	brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_0, SDIO_CCCR_IOEx,
			       enable, NULL);

	timeout = jiffies + msecs_to_jiffies(BRCMF_WAIT_F2RDY);
	ready = 0;
	while (enable != ready) {
		ready = brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_0,
					      SDIO_CCCR_IORx, NULL);
		if (time_after(jiffies, timeout))
			break;
		else if (time_after(jiffies, timeout - BRCMF_WAIT_F2RDY + 50))
			/* prevent busy waiting if it takes too long */
			msleep_interruptible(20);
	}

	brcmf_dbg(INFO, "enable 0x%02x, ready 0x%02x\n", enable, ready);

	/* If F2 successfully enabled, set core and enable interrupts */
	if (ready == enable) {
		/* Set up the interrupt mask and enable interrupts */
		bus->hostintmask = HOSTINTMASK;
		w_sdreg32(bus, bus->hostintmask,
			  offsetof(struct sdpcmd_regs, hostintmask), &retries);

		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
				       SBSDIO_WATERMARK, 8, &err);
	} else {
		/* Disable F2 again */
		enable = SDIO_FUNC_ENABLE_1;
		brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_0,
				       SDIO_CCCR_IOEx, enable, NULL);
		ret = -ENODEV;
	}

	/* Restore previous clock setting */
	brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
			       SBSDIO_FUNC1_CHIPCLKCSR, saveclk, &err);

	if (ret == 0) {
		ret = brcmf_sdio_intr_register(bus->sdiodev);
		if (ret != 0)
			brcmf_dbg(ERROR, "intr register failed:%d\n", ret);
	}

	/* If we didn't come up, turn off backplane clock */
	if (bus_if->state != BRCMF_BUS_DATA)
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);

exit:
	up(&bus->sdsem);

	return ret;
}

void brcmf_sdbrcm_isr(void *arg)
{
	struct brcmf_sdio *bus = (struct brcmf_sdio *) arg;

	brcmf_dbg(TRACE, "Enter\n");

	if (!bus) {
		brcmf_dbg(ERROR, "bus is null pointer, exiting\n");
		return;
	}

	if (bus->sdiodev->bus_if->state == BRCMF_BUS_DOWN) {
		brcmf_dbg(ERROR, "bus is down. we have nothing to do\n");
		return;
	}
	/* Count the interrupt call */
	bus->intrcount++;
	bus->ipend = true;

	/* Shouldn't get this interrupt if we're sleeping? */
	if (bus->sleeping) {
		brcmf_dbg(ERROR, "INTERRUPT WHILE SLEEPING??\n");
		return;
	}

	/* Disable additional interrupts (is this needed now)? */
	if (!bus->intr)
		brcmf_dbg(ERROR, "isr w/o interrupt configured!\n");

	bus->dpc_sched = true;
	if (bus->dpc_tsk) {
		brcmf_sdbrcm_adddpctsk(bus);
		complete(&bus->dpc_wait);
	}
}

static bool brcmf_sdbrcm_bus_watchdog(struct brcmf_sdio *bus)
{
#ifdef DEBUG
	struct brcmf_bus *bus_if = dev_get_drvdata(bus->sdiodev->dev);
#endif	/* DEBUG */

	brcmf_dbg(TIMER, "Enter\n");

	/* Ignore the timer if simulating bus down */
	if (bus->sleeping)
		return false;

	down(&bus->sdsem);

	/* Poll period: check device if appropriate. */
	if (bus->poll && (++bus->polltick >= bus->pollrate)) {
		u32 intstatus = 0;

		/* Reset poll tick */
		bus->polltick = 0;

		/* Check device if no interrupts */
		if (!bus->intr || (bus->intrcount == bus->lastintrs)) {

			if (!bus->dpc_sched) {
				u8 devpend;
				devpend = brcmf_sdcard_cfg_read(bus->sdiodev,
						SDIO_FUNC_0, SDIO_CCCR_INTx,
						NULL);
				intstatus =
				    devpend & (INTR_STATUS_FUNC1 |
					       INTR_STATUS_FUNC2);
			}

			/* If there is something, make like the ISR and
				 schedule the DPC */
			if (intstatus) {
				bus->pollcnt++;
				bus->ipend = true;

				bus->dpc_sched = true;
				if (bus->dpc_tsk) {
					brcmf_sdbrcm_adddpctsk(bus);
					complete(&bus->dpc_wait);
				}
			}
		}

		/* Update interrupt tracking */
		bus->lastintrs = bus->intrcount;
	}
#ifdef DEBUG
	/* Poll for console output periodically */
	if (bus_if->state == BRCMF_BUS_DATA &&
	    bus->console_interval != 0) {
		bus->console.count += BRCMF_WD_POLL_MS;
		if (bus->console.count >= bus->console_interval) {
			bus->console.count -= bus->console_interval;
			/* Make sure backplane clock is on */
			brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
			if (brcmf_sdbrcm_readconsole(bus) < 0)
				/* stop on error */
				bus->console_interval = 0;
		}
	}
#endif				/* DEBUG */

	/* On idle timeout clear activity flag and/or turn off clock */
	if ((bus->idletime > 0) && (bus->clkstate == CLK_AVAIL)) {
		if (++bus->idlecount >= bus->idletime) {
			bus->idlecount = 0;
			if (bus->activity) {
				bus->activity = false;
				brcmf_sdbrcm_wd_timer(bus, BRCMF_WD_POLL_MS);
			} else {
				brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);
			}
		}
	}

	up(&bus->sdsem);

	return bus->ipend;
}

static bool brcmf_sdbrcm_chipmatch(u16 chipid)
{
	if (chipid == BCM4329_CHIP_ID)
		return true;
	if (chipid == BCM4330_CHIP_ID)
		return true;
	return false;
}

static void brcmf_sdbrcm_release_malloc(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	kfree(bus->rxbuf);
	bus->rxctl = bus->rxbuf = NULL;
	bus->rxlen = 0;

	kfree(bus->databuf);
	bus->databuf = NULL;
}

static bool brcmf_sdbrcm_probe_malloc(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (bus->sdiodev->bus_if->maxctl) {
		bus->rxblen =
		    roundup((bus->sdiodev->bus_if->maxctl + SDPCM_HDRLEN),
			    ALIGNMENT) + BRCMF_SDALIGN;
		bus->rxbuf = kmalloc(bus->rxblen, GFP_ATOMIC);
		if (!(bus->rxbuf))
			goto fail;
	}

	/* Allocate buffer to receive glomed packet */
	bus->databuf = kmalloc(MAX_DATA_BUF, GFP_ATOMIC);
	if (!(bus->databuf)) {
		/* release rxbuf which was already located as above */
		if (!bus->rxblen)
			kfree(bus->rxbuf);
		goto fail;
	}

	/* Align the buffer */
	if ((unsigned long)bus->databuf % BRCMF_SDALIGN)
		bus->dataptr = bus->databuf + (BRCMF_SDALIGN -
			       ((unsigned long)bus->databuf % BRCMF_SDALIGN));
	else
		bus->dataptr = bus->databuf;

	return true;

fail:
	return false;
}

static bool
brcmf_sdbrcm_probe_attach(struct brcmf_sdio *bus, u32 regsva)
{
	u8 clkctl = 0;
	int err = 0;
	int reg_addr;
	u32 reg_val;
	u8 idx;

	bus->alp_only = true;

	/* Return the window to backplane enumeration space for core access */
	if (brcmf_sdcard_set_sbaddr_window(bus->sdiodev, SI_ENUM_BASE))
		brcmf_dbg(ERROR, "FAILED to return to SI_ENUM_BASE\n");

	pr_debug("F1 signature read @0x18000000=0x%4x\n",
		 brcmf_sdcard_reg_read(bus->sdiodev, SI_ENUM_BASE));

	/*
	 * Force PLL off until brcmf_sdio_chip_attach()
	 * programs PLL control regs
	 */

	brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
			       SBSDIO_FUNC1_CHIPCLKCSR,
			       BRCMF_INIT_CLKCTL1, &err);
	if (!err)
		clkctl =
		    brcmf_sdcard_cfg_read(bus->sdiodev, SDIO_FUNC_1,
					  SBSDIO_FUNC1_CHIPCLKCSR, &err);

	if (err || ((clkctl & ~SBSDIO_AVBITS) != BRCMF_INIT_CLKCTL1)) {
		brcmf_dbg(ERROR, "ChipClkCSR access: err %d wrote 0x%02x read 0x%02x\n",
			  err, BRCMF_INIT_CLKCTL1, clkctl);
		goto fail;
	}

	if (brcmf_sdio_chip_attach(bus->sdiodev, &bus->ci, regsva)) {
		brcmf_dbg(ERROR, "brcmf_sdio_chip_attach failed!\n");
		goto fail;
	}

	if (!brcmf_sdbrcm_chipmatch((u16) bus->ci->chip)) {
		brcmf_dbg(ERROR, "unsupported chip: 0x%04x\n", bus->ci->chip);
		goto fail;
	}

	brcmf_sdio_chip_drivestrengthinit(bus->sdiodev, bus->ci,
					  SDIO_DRIVE_STRENGTH);

	/* Get info on the SOCRAM cores... */
	bus->ramsize = bus->ci->ramsize;
	if (!(bus->ramsize)) {
		brcmf_dbg(ERROR, "failed to find SOCRAM memory!\n");
		goto fail;
	}

	/* Set core control so an SDIO reset does a backplane reset */
	idx = brcmf_sdio_chip_getinfidx(bus->ci, BCMA_CORE_SDIO_DEV);
	reg_addr = bus->ci->c_inf[idx].base +
		   offsetof(struct sdpcmd_regs, corecontrol);
	reg_val = brcmf_sdcard_reg_read(bus->sdiodev, reg_addr);
	brcmf_sdcard_reg_write(bus->sdiodev, reg_addr, reg_val | CC_BPRESEN);

	brcmu_pktq_init(&bus->txq, (PRIOMASK + 1), TXQLEN);

	/* Locate an appropriately-aligned portion of hdrbuf */
	bus->rxhdr = (u8 *) roundup((unsigned long)&bus->hdrbuf[0],
				    BRCMF_SDALIGN);

	/* Set the poll and/or interrupt flags */
	bus->intr = true;
	bus->poll = false;
	if (bus->poll)
		bus->pollrate = 1;

	return true;

fail:
	return false;
}

static bool brcmf_sdbrcm_probe_init(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	/* Disable F2 to clear any intermediate frame state on the dongle */
	brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_0, SDIO_CCCR_IOEx,
			       SDIO_FUNC_ENABLE_1, NULL);

	bus->sdiodev->bus_if->state = BRCMF_BUS_DOWN;
	bus->sleeping = false;
	bus->rxflow = false;

	/* Done with backplane-dependent accesses, can drop clock... */
	brcmf_sdcard_cfg_write(bus->sdiodev, SDIO_FUNC_1,
			       SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);

	/* ...and initialize clock/power states */
	bus->clkstate = CLK_SDONLY;
	bus->idletime = BRCMF_IDLE_INTERVAL;
	bus->idleclock = BRCMF_IDLE_ACTIVE;

	/* Query the F2 block size, set roundup accordingly */
	bus->blocksize = bus->sdiodev->func[2]->cur_blksize;
	bus->roundup = min(max_roundup, bus->blocksize);

	/* bus module does not support packet chaining */
	bus->use_rxchain = false;
	bus->sd_rxchain = false;

	return true;
}

static int
brcmf_sdbrcm_watchdog_thread(void *data)
{
	struct brcmf_sdio *bus = (struct brcmf_sdio *)data;

	allow_signal(SIGTERM);
	/* Run until signal received */
	while (1) {
		if (kthread_should_stop())
			break;
		if (!wait_for_completion_interruptible(&bus->watchdog_wait)) {
			brcmf_sdbrcm_bus_watchdog(bus);
			/* Count the tick for reference */
			bus->tickcnt++;
		} else
			break;
	}
	return 0;
}

static void
brcmf_sdbrcm_watchdog(unsigned long data)
{
	struct brcmf_sdio *bus = (struct brcmf_sdio *)data;

	if (bus->watchdog_tsk) {
		complete(&bus->watchdog_wait);
		/* Reschedule the watchdog */
		if (bus->wd_timer_valid)
			mod_timer(&bus->timer,
				  jiffies + BRCMF_WD_POLL_MS * HZ / 1000);
	}
}

static void brcmf_sdbrcm_release_dongle(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (bus->ci) {
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);
		brcmf_sdio_chip_detach(&bus->ci);
		if (bus->vars && bus->varsz)
			kfree(bus->vars);
		bus->vars = NULL;
	}

	brcmf_dbg(TRACE, "Disconnected\n");
}

/* Detach and free everything */
static void brcmf_sdbrcm_release(struct brcmf_sdio *bus)
{
	brcmf_dbg(TRACE, "Enter\n");

	if (bus) {
		/* De-register interrupt handler */
		brcmf_sdio_intr_unregister(bus->sdiodev);

		if (bus->sdiodev->bus_if->drvr) {
			brcmf_detach(bus->sdiodev->dev);
			brcmf_sdbrcm_release_dongle(bus);
		}

		brcmf_sdbrcm_release_malloc(bus);

		kfree(bus);
	}

	brcmf_dbg(TRACE, "Disconnected\n");
}

void *brcmf_sdbrcm_probe(u32 regsva, struct brcmf_sdio_dev *sdiodev)
{
	int ret;
	struct brcmf_sdio *bus;

	brcmf_dbg(TRACE, "Enter\n");

	/* We make an assumption about address window mappings:
	 * regsva == SI_ENUM_BASE*/

	/* Allocate private bus interface state */
	bus = kzalloc(sizeof(struct brcmf_sdio), GFP_ATOMIC);
	if (!bus)
		goto fail;

	bus->sdiodev = sdiodev;
	sdiodev->bus = bus;
	skb_queue_head_init(&bus->glom);
	bus->txbound = BRCMF_TXBOUND;
	bus->rxbound = BRCMF_RXBOUND;
	bus->txminmax = BRCMF_TXMINMAX;
	bus->tx_seq = SDPCM_SEQUENCE_WRAP - 1;
	bus->usebufpool = false;	/* Use bufpool if allocated,
					 else use locally malloced rxbuf */

	/* attempt to attach to the dongle */
	if (!(brcmf_sdbrcm_probe_attach(bus, regsva))) {
		brcmf_dbg(ERROR, "brcmf_sdbrcm_probe_attach failed\n");
		goto fail;
	}

	spin_lock_init(&bus->txqlock);
	init_waitqueue_head(&bus->ctrl_wait);
	init_waitqueue_head(&bus->dcmd_resp_wait);

	/* Set up the watchdog timer */
	init_timer(&bus->timer);
	bus->timer.data = (unsigned long)bus;
	bus->timer.function = brcmf_sdbrcm_watchdog;

	/* Initialize thread based operation and lock */
	sema_init(&bus->sdsem, 1);

	/* Initialize watchdog thread */
	init_completion(&bus->watchdog_wait);
	bus->watchdog_tsk = kthread_run(brcmf_sdbrcm_watchdog_thread,
					bus, "brcmf_watchdog");
	if (IS_ERR(bus->watchdog_tsk)) {
		pr_warn("brcmf_watchdog thread failed to start\n");
		bus->watchdog_tsk = NULL;
	}
	/* Initialize DPC thread */
	init_completion(&bus->dpc_wait);
	INIT_LIST_HEAD(&bus->dpc_tsklst);
	spin_lock_init(&bus->dpc_tl_lock);
	bus->dpc_tsk = kthread_run(brcmf_sdbrcm_dpc_thread,
				   bus, "brcmf_dpc");
	if (IS_ERR(bus->dpc_tsk)) {
		pr_warn("brcmf_dpc thread failed to start\n");
		bus->dpc_tsk = NULL;
	}

	/* Assign bus interface call back */
	bus->sdiodev->bus_if->brcmf_bus_stop = brcmf_sdbrcm_bus_stop;
	bus->sdiodev->bus_if->brcmf_bus_init = brcmf_sdbrcm_bus_init;
	bus->sdiodev->bus_if->brcmf_bus_txdata = brcmf_sdbrcm_bus_txdata;
	bus->sdiodev->bus_if->brcmf_bus_txctl = brcmf_sdbrcm_bus_txctl;
	bus->sdiodev->bus_if->brcmf_bus_rxctl = brcmf_sdbrcm_bus_rxctl;
	/* Attach to the brcmf/OS/network interface */
	ret = brcmf_attach(SDPCM_RESERVE, bus->sdiodev->dev);
	if (ret != 0) {
		brcmf_dbg(ERROR, "brcmf_attach failed\n");
		goto fail;
	}

	/* Allocate buffers */
	if (!(brcmf_sdbrcm_probe_malloc(bus))) {
		brcmf_dbg(ERROR, "brcmf_sdbrcm_probe_malloc failed\n");
		goto fail;
	}

	if (!(brcmf_sdbrcm_probe_init(bus))) {
		brcmf_dbg(ERROR, "brcmf_sdbrcm_probe_init failed\n");
		goto fail;
	}

	brcmf_dbg(INFO, "completed!!\n");

	/* if firmware path present try to download and bring up bus */
	ret = brcmf_bus_start(bus->sdiodev->dev);
	if (ret != 0) {
		if (ret == -ENOLINK) {
			brcmf_dbg(ERROR, "dongle is not responding\n");
			goto fail;
		}
	}

	return bus;

fail:
	brcmf_sdbrcm_release(bus);
	return NULL;
}

void brcmf_sdbrcm_disconnect(void *ptr)
{
	struct brcmf_sdio *bus = (struct brcmf_sdio *)ptr;

	brcmf_dbg(TRACE, "Enter\n");

	if (bus)
		brcmf_sdbrcm_release(bus);

	brcmf_dbg(TRACE, "Disconnected\n");
}

void
brcmf_sdbrcm_wd_timer(struct brcmf_sdio *bus, uint wdtick)
{
	/* Totally stop the timer */
	if (!wdtick && bus->wd_timer_valid) {
		del_timer_sync(&bus->timer);
		bus->wd_timer_valid = false;
		bus->save_ms = wdtick;
		return;
	}

	/* don't start the wd until fw is loaded */
	if (bus->sdiodev->bus_if->state == BRCMF_BUS_DOWN)
		return;

	if (wdtick) {
		if (bus->save_ms != BRCMF_WD_POLL_MS) {
			if (bus->wd_timer_valid)
				/* Stop timer and restart at new value */
				del_timer_sync(&bus->timer);

			/* Create timer again when watchdog period is
			   dynamically changed or in the first instance
			 */
			bus->timer.expires =
				jiffies + BRCMF_WD_POLL_MS * HZ / 1000;
			add_timer(&bus->timer);

		} else {
			/* Re arm the timer, at last watchdog period */
			mod_timer(&bus->timer,
				jiffies + BRCMF_WD_POLL_MS * HZ / 1000);
		}

		bus->wd_timer_valid = true;
		bus->save_ms = wdtick;
	}
}
