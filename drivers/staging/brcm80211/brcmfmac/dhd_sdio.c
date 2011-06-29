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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/pci_ids.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/mmc/sdio.h>
#include <asm/unaligned.h>
#include <defs.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include <brcm_hw_ids.h>
#include <soc.h>
#include "sdio_host.h"

/* register access macros */
#ifndef __BIG_ENDIAN
#ifndef __mips__
#define R_REG(r) \
	brcmf_sdcard_reg_read(NULL, (unsigned long)(r), sizeof(*(r)))
#else				/* __mips__ */
#define R_REG(r) \
	({ \
		__typeof(*(r)) __osl_v; \
		__asm__ __volatile__("sync"); \
		__osl_v = brcmf_sdcard_reg_read(NULL, (unsigned long)(r),\
					  sizeof(*(r))); \
		__asm__ __volatile__("sync"); \
		__osl_v; \
	})
#endif				/* __mips__ */

#define W_REG(r, v) do { \
		brcmf_sdcard_reg_write(NULL, (unsigned long)(r), sizeof(*(r)), \
				       (v)); \
	} while (0)
#else				/* __BIG_ENDIAN */
#define R_REG(r) \
	brcmf_sdcard_reg_read(NULL, (unsigned long)(r), sizeof(*(r)))
#define W_REG(r, v) do { \
		brcmf_sdcard_reg_write(NULL, (unsigned long)(r), sizeof(*(r)), \
				       (v)); \
	} while (0)
#endif				/* __BIG_ENDIAN */

#define AND_REG(r, v)	W_REG((r), R_REG(r) & (v))
#define OR_REG(r, v)	W_REG((r), R_REG(r) | (v))

#define SET_REG(r, mask, val) \
		W_REG((r), ((R_REG(r) & ~(mask)) | (val)))

#ifdef DHD_DEBUG

/* ARM trap handling */

/* Trap types defined by ARM (see arminc.h) */

/* Trap locations in lo memory */
#define	TRAP_STRIDE	4
#define FIRST_TRAP	TR_RST
#define LAST_TRAP	(TR_FIQ * TRAP_STRIDE)

#if defined(__ARM_ARCH_4T__)
#define	MAX_TRAP_TYPE	(TR_FIQ + 1)
#elif defined(__ARM_ARCH_7M__)
#define	MAX_TRAP_TYPE	(TR_ISR + ARMCM3_NUMINTS)
#endif				/* __ARM_ARCH_7M__ */

/* The trap structure is defined here as offsets for assembly */
#define	TR_TYPE		0x00
#define	TR_EPC		0x04
#define	TR_CPSR		0x08
#define	TR_SPSR		0x0c
#define	TR_REGS		0x10
#define	TR_REG(n)	(TR_REGS + (n) * 4)
#define	TR_SP		TR_REG(13)
#define	TR_LR		TR_REG(14)
#define	TR_PC		TR_REG(15)

#define	TRAP_T_SIZE	80

typedef struct _trap_struct {
	u32 type;
	u32 epc;
	u32 cpsr;
	u32 spsr;
	u32 r0;
	u32 r1;
	u32 r2;
	u32 r3;
	u32 r4;
	u32 r5;
	u32 r6;
	u32 r7;
	u32 r8;
	u32 r9;
	u32 r10;
	u32 r11;
	u32 r12;
	u32 r13;
	u32 r14;
	u32 pc;
} trap_t;

#define CBUF_LEN	(128)

#define LOG_BUF_LEN	1024

typedef struct {
	u32 buf;		/* Can't be pointer on (64-bit) hosts */
	uint buf_size;
	uint idx;
	char *_buf_compat;	/* Redundant pointer for backward compat. */
} rte_log_t;

typedef struct {
	/* Virtual UART
	 * When there is no UART (e.g. Quickturn),
	 * the host should write a complete
	 * input line directly into cbuf and then write
	 * the length into vcons_in.
	 * This may also be used when there is a real UART
	 * (at risk of conflicting with
	 * the real UART).  vcons_out is currently unused.
	 */
	volatile uint vcons_in;
	volatile uint vcons_out;

	/* Output (logging) buffer
	 * Console output is written to a ring buffer log_buf at index log_idx.
	 * The host may read the output when it sees log_idx advance.
	 * Output will be lost if the output wraps around faster than the host
	 * polls.
	 */
	rte_log_t log;

	/* Console input line buffer
	 * Characters are read one at a time into cbuf
	 * until <CR> is received, then
	 * the buffer is processed as a command line.
	 * Also used for virtual UART.
	 */
	uint cbuf_idx;
	char cbuf[CBUF_LEN];
} rte_cons_t;

#endif				/* DHD_DEBUG */
#include <chipcommon.h>

#include "sbsdio.h"

#include "dngl_stats.h"
#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_proto.h"
#include "dhd_dbg.h"
#include <sdiovar.h>
#include <bcmchip.h>

#ifndef DHDSDIO_MEM_DUMP_FNAME
#define DHDSDIO_MEM_DUMP_FNAME         "mem_dump"
#endif

#define TXQLEN		2048	/* bulk tx queue length */
#define TXHI		(TXQLEN - 256)	/* turn on flow control above TXHI */
#define TXLOW		(TXHI - 256)	/* turn off flow control below TXLOW */
#define PRIOMASK	7

#define TXRETRIES	2	/* # of retries for tx frames */

#if defined(CONFIG_MACH_SANDGATE2G)
#define DHD_RXBOUND	250	/* Default for max rx frames in
				 one scheduling */
#else
#define DHD_RXBOUND	50	/* Default for max rx frames in
				 one scheduling */
#endif				/* defined(CONFIG_MACH_SANDGATE2G) */

#define DHD_TXBOUND	20	/* Default for max tx frames in
				 one scheduling */

#define DHD_TXMINMAX	1	/* Max tx frames if rx still pending */

#define MEMBLOCK	2048	/* Block size used for downloading
				 of dongle image */
#define MAX_DATA_BUF	(32 * 1024)	/* Must be large enough to hold
				 biggest possible glom */

/* Packet alignment for most efficient SDIO (can change based on platform) */
#ifndef DHD_SDALIGN
#define DHD_SDALIGN	32
#endif
#if !ISPOWEROF2(DHD_SDALIGN)
#error DHD_SDALIGN is not a power of 2!
#endif

#ifndef DHD_FIRSTREAD
#define DHD_FIRSTREAD	32
#endif
#if !ISPOWEROF2(DHD_FIRSTREAD)
#error DHD_FIRSTREAD is not a power of 2!
#endif

/* Total length of frame header for dongle protocol */
#define SDPCM_HDRLEN	(SDPCM_FRAMETAG_LEN + SDPCM_SWHEADER_LEN)
#ifdef SDTEST
#define SDPCM_RESERVE	(SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + DHD_SDALIGN)
#else
#define SDPCM_RESERVE	(SDPCM_HDRLEN + DHD_SDALIGN)
#endif

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

/* For TEST_CHANNEL packets, define another 4-byte header */
#define SDPCM_TEST_HDRLEN	4	/*
					 * Generally: Cmd(1), Ext(1), Len(2);
					 * Semantics of Ext byte depend on
					 * command. Len is current or requested
					 * frame length, not including test
					 * header; sent little-endian.
					 */
#define SDPCM_TEST_DISCARD	0x01	/* Receiver discards. Ext:pattern id. */
#define SDPCM_TEST_ECHOREQ	0x02	/* Echo request. Ext:pattern id. */
#define SDPCM_TEST_ECHORSP	0x03	/* Echo response. Ext:pattern id. */
#define SDPCM_TEST_BURST	0x04	/*
					 * Receiver to send a burst.
					 * Ext is a frame count
					 */
#define SDPCM_TEST_SEND		0x05	/*
					 * Receiver sets send mode.
					 * Ext is boolean on/off
					 */

/* Handy macro for filling in datagen packets with a pattern */
#define SDPCM_TEST_FILL(byteno, id)	((u8)(id + byteno))

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
#ifndef MAX_HDR_READ
#define MAX_HDR_READ	32
#endif
#if !ISPOWEROF2(MAX_HDR_READ)
#error MAX_HDR_READ is not a power of 2!
#endif

#define MAX_RX_DATASZ	2048

/* Maximum milliseconds to wait for F2 to come up */
#define DHD_WAIT_F2RDY	3000

/* Bump up limit on waiting for HT to account for first startup;
 * if the image is doing a CRC calculation before programming the PMU
 * for HT availability, it could take a couple hundred ms more, so
 * max out at a 1 second (1000000us).
 */
#if (PMU_MAX_TRANSITION_DLY <= 1000000)
#undef PMU_MAX_TRANSITION_DLY
#define PMU_MAX_TRANSITION_DLY 1000000
#endif

/* Value for ChipClockCSR during initial setup */
#define DHD_INIT_CLKCTL1	(SBSDIO_FORCE_HW_CLKREQ_OFF |	\
					SBSDIO_ALP_AVAIL_REQ)
#define DHD_INIT_CLKCTL2	(SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP)

/* Flags for SDH calls */
#define F2SYNC	(SDIO_REQ_4BYTE | SDIO_REQ_FIXED)

/* sbimstate */
#define	SBIM_IBE		0x20000	/* inbanderror */
#define	SBIM_TO			0x40000	/* timeout */
#define	SBIM_BY			0x01800000	/* busy (sonics >= 2.3) */
#define	SBIM_RJ			0x02000000	/* reject (sonics >= 2.3) */

/* sbtmstatelow */
#define	SBTML_RESET		0x0001	/* reset */
#define	SBTML_REJ_MASK		0x0006	/* reject field */
#define	SBTML_REJ		0x0002	/* reject */
#define	SBTML_TMPREJ		0x0004	/* temporary reject, for error recovery */

#define	SBTML_SICF_SHIFT	16	/* Shift to locate the SI control flags in sbtml */

/* sbtmstatehigh */
#define	SBTMH_SERR		0x0001	/* serror */
#define	SBTMH_INT		0x0002	/* interrupt */
#define	SBTMH_BUSY		0x0004	/* busy */
#define	SBTMH_TO		0x0020	/* timeout (sonics >= 2.3) */

#define	SBTMH_SISF_SHIFT	16	/* Shift to locate the SI status flags in sbtmh */

/* sbidlow */
#define	SBIDL_INIT		0x80	/* initiator */

/* sbidhigh */
#define	SBIDH_RC_MASK		0x000f	/* revision code */
#define	SBIDH_RCE_MASK		0x7000	/* revision code extension field */
#define	SBIDH_RCE_SHIFT		8
#define	SBCOREREV(sbidh) \
	((((sbidh) & SBIDH_RCE_MASK) >> SBIDH_RCE_SHIFT) | ((sbidh) & SBIDH_RC_MASK))
#define	SBIDH_CC_MASK		0x8ff0	/* core code */
#define	SBIDH_CC_SHIFT		4
#define	SBIDH_VC_MASK		0xffff0000	/* vendor code */
#define	SBIDH_VC_SHIFT		16

/*
 * Conversion of 802.1D priority to precedence level
 */
#define PRIO2PREC(prio) \
	(((prio) == PRIO_8021D_NONE || (prio) == PRIO_8021D_BE) ? \
	((prio^2)) : (prio))

DHD_SPINWAIT_SLEEP_INIT(sdioh_spinwait_sleep);
extern int dhdcdc_set_ioctl(dhd_pub_t *dhd, int ifidx, uint cmd, void *buf,
			    uint len);

/* Core reg address translation */
#define CORE_CC_REG(base, field)	(base + offsetof(chipcregs_t, field))
#define CORE_BUS_REG(base, field) \
		(base + offsetof(struct sdpcmd_regs, field))
#define CORE_SB(base, field) \
		(base + SBCONFIGOFF + offsetof(sbconfig_t, field))

#ifdef DHD_DEBUG
/* Device console log buffer state */
typedef struct dhd_console {
	uint count;		/* Poll interval msec counter */
	uint log_addr;		/* Log struct address (fixed) */
	rte_log_t log;	/* Log struct (host copy) */
	uint bufsize;		/* Size of log buffer */
	u8 *buf;		/* Log buffer (host copy) */
	uint last;		/* Last buffer read index */
} dhd_console_t;
#endif				/* DHD_DEBUG */

struct sdpcm_shared {
	u32 flags;
	u32 trap_addr;
	u32 assert_exp_addr;
	u32 assert_file_addr;
	u32 assert_line;
	u32 console_addr;	/* Address of rte_cons_t */
	u32 msgtrace_addr;
	u8 tag[32];
};


/* misc chip info needed by some of the routines */
struct chip_info {
	u32 chip;
	u32 chiprev;
	u32 cccorebase;
	u32 ccrev;
	u32 cccaps;
	u32 buscorebase;
	u32 buscorerev;
	u32 buscoretype;
	u32 ramcorebase;
	u32 armcorebase;
	u32 pmurev;
	u32 ramsize;
};

/* Private data for SDIO bus interaction */
typedef struct dhd_bus {
	dhd_pub_t *dhd;

	bcmsdh_info_t *sdh;	/* Handle for BCMSDH calls */
	struct chip_info *ci;	/* Chip info struct */
	char *vars;		/* Variables (from CIS and/or other) */
	uint varsz;		/* Size of variables buffer */
	u32 sbaddr;		/* Current SB window pointer (-1, invalid) */

	struct sdpcmd_regs *regs;	/* SDIO core */
	uint sdpcmrev;		/* SDIO core revision */
	uint armrev;		/* CPU core revision */
	uint ramrev;		/* SOCRAM core revision */
	u32 ramsize;		/* Size of RAM in SOCRAM (bytes) */
	u32 orig_ramsize;	/* Size of RAM in SOCRAM (bytes) */

	u32 bus;		/* gSPI or SDIO bus */
	u32 hostintmask;	/* Copy of Host Interrupt Mask */
	u32 intstatus;	/* Intstatus bits (events) pending */
	bool dpc_sched;		/* Indicates DPC schedule (intrpt rcvd) */
	bool fcstate;		/* State of dongle flow-control */

	u16 cl_devid;	/* cached devid for brcmf_sdio_probe_attach() */
	char *fw_path;		/* module_param: path to firmware image */
	char *nv_path;		/* module_param: path to nvram vars file */
	const char *nvram_params;	/* user specified nvram params. */

	uint blocksize;		/* Block size of SDIO transfers */
	uint roundup;		/* Max roundup limit */

	struct pktq txq;	/* Queue length used for flow-control */
	u8 flowcontrol;	/* per prio flow control bitmask */
	u8 tx_seq;		/* Transmit sequence number (next) */
	u8 tx_max;		/* Maximum transmit sequence allowed */

	u8 hdrbuf[MAX_HDR_READ + DHD_SDALIGN];
	u8 *rxhdr;		/* Header of current rx frame (in hdrbuf) */
	u16 nextlen;		/* Next Read Len from last header */
	u8 rx_seq;		/* Receive sequence number (expected) */
	bool rxskip;		/* Skip receive (awaiting NAK ACK) */

	struct sk_buff *glomd;	/* Packet containing glomming descriptor */
	struct sk_buff *glom;	/* Packet chain for glommed superframe */
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
	bool intdis;		/* Interrupts disabled by isr */
	uint intrcount;		/* Count of device interrupt callbacks */
	uint lastintrs;		/* Count as of last watchdog timer */
	uint spurious;		/* Count of spurious interrupts */
	uint pollrate;		/* Ticks between device polls */
	uint polltick;		/* Tick counter */
	uint pollcnt;		/* Count of active polls */

#ifdef DHD_DEBUG
	dhd_console_t console;	/* Console output polling support */
	uint console_addr;	/* Console address from shared struct */
#endif				/* DHD_DEBUG */

	uint regfails;		/* Count of R_REG/W_REG failures */

	uint clkstate;		/* State of sd and backplane clock(s) */
	bool activity;		/* Activity flag for clock down */
	s32 idletime;		/* Control for activity timeout */
	s32 idlecount;	/* Activity timeout counter */
	s32 idleclock;	/* How to set bus driver when idle */
	s32 sd_rxchain;	/* If bcmsdh api accepts PKT chains */
	bool use_rxchain;	/* If dhd should use PKT chains */
	bool sleeping;		/* Is SDIO bus sleeping? */
	bool rxflow_mode;	/* Rx flow control mode */
	bool rxflow;		/* Is rx flow control on */
	uint prev_rxlim_hit;	/* Is prev rx limit exceeded
					 (per dpc schedule) */
	bool alp_only;		/* Don't use HT clock (ALP only) */
/* Field to decide if rx of control frames happen in rxbuf or lb-pool */
	bool usebufpool;

#ifdef SDTEST
	/* external loopback */
	bool ext_loop;
	u8 loopid;

	/* pktgen configuration */
	uint pktgen_freq;	/* Ticks between bursts */
	uint pktgen_count;	/* Packets to send each burst */
	uint pktgen_print;	/* Bursts between count displays */
	uint pktgen_total;	/* Stop after this many */
	uint pktgen_minlen;	/* Minimum packet data len */
	uint pktgen_maxlen;	/* Maximum packet data len */
	uint pktgen_mode;	/* Configured mode: tx, rx, or echo */
	uint pktgen_stop;	/* Number of tx failures causing stop */

	/* active pktgen fields */
	uint pktgen_tick;	/* Tick counter for bursts */
	uint pktgen_ptick;	/* Burst counter for printing */
	uint pktgen_sent;	/* Number of test packets generated */
	uint pktgen_rcvd;	/* Number of test packets received */
	uint pktgen_fail;	/* Number of failed send attempts */
	u16 pktgen_len;	/* Length of next packet to send */
#endif				/* SDTEST */

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

	u8 *ctrl_frame_buf;
	u32 ctrl_frame_len;
	bool ctrl_frame_stat;

	spinlock_t txqlock;
} dhd_bus_t;

typedef volatile struct _sbconfig {
	u32 PAD[2];
	u32 sbipsflag;	/* initiator port ocp slave flag */
	u32 PAD[3];
	u32 sbtpsflag;	/* target port ocp slave flag */
	u32 PAD[11];
	u32 sbtmerrloga;	/* (sonics >= 2.3) */
	u32 PAD;
	u32 sbtmerrlog;	/* (sonics >= 2.3) */
	u32 PAD[3];
	u32 sbadmatch3;	/* address match3 */
	u32 PAD;
	u32 sbadmatch2;	/* address match2 */
	u32 PAD;
	u32 sbadmatch1;	/* address match1 */
	u32 PAD[7];
	u32 sbimstate;	/* initiator agent state */
	u32 sbintvec;	/* interrupt mask */
	u32 sbtmstatelow;	/* target state */
	u32 sbtmstatehigh;	/* target state */
	u32 sbbwa0;		/* bandwidth allocation table0 */
	u32 PAD;
	u32 sbimconfiglow;	/* initiator configuration */
	u32 sbimconfighigh;	/* initiator configuration */
	u32 sbadmatch0;	/* address match0 */
	u32 PAD;
	u32 sbtmconfiglow;	/* target configuration */
	u32 sbtmconfighigh;	/* target configuration */
	u32 sbbconfig;	/* broadcast configuration */
	u32 PAD;
	u32 sbbstate;	/* broadcast state */
	u32 PAD[3];
	u32 sbactcnfg;	/* activate configuration */
	u32 PAD[3];
	u32 sbflagst;	/* current sbflags */
	u32 PAD[3];
	u32 sbidlow;		/* identification */
	u32 sbidhigh;	/* identification */
} sbconfig_t;

/* clkstate */
#define CLK_NONE	0
#define CLK_SDONLY	1
#define CLK_PENDING	2	/* Not used yet */
#define CLK_AVAIL	3

#define DHD_NOPMU(dhd)	(false)

#ifdef DHD_DEBUG
static int qcount[NUMPRIO];
static int tx_packets[NUMPRIO];
#endif				/* DHD_DEBUG */

/* Deferred transmit */
const uint dhd_deferred_tx = 1;

/* Tx/Rx bounds */
uint dhd_txbound;
uint dhd_rxbound;
uint dhd_txminmax;

/* override the RAM size if possible */
#define DONGLE_MIN_MEMSIZE (128 * 1024)
int dhd_dongle_memsize;

static bool dhd_alignctl;

static bool sd1idle;

static bool retrydata;
#define RETRYCHAN(chan) (((chan) == SDPCM_EVENT_CHANNEL) || retrydata)

static const uint watermark = 8;
static const uint firstread = DHD_FIRSTREAD;

#define HDATLEN (firstread - (SDPCM_HDRLEN))

/* Retry count for register access failures */
static const uint retry_limit = 2;

/* Force even SD lengths (some host controllers mess up on odd bytes) */
static bool forcealign;

#define ALIGNMENT  4

#if defined(OOB_INTR_ONLY) && defined(HW_OOB)
extern void brcmf_sdcard_enable_hw_oob_intr(void *sdh, bool enable);
#endif

#if defined(OOB_INTR_ONLY) && defined(SDIO_ISR_THREAD)
#error OOB_INTR_ONLY is NOT working with SDIO_ISR_THREAD
#endif	/* defined(OOB_INTR_ONLY) && defined(SDIO_ISR_THREAD) */
#define PKTALIGN(_p, _len, _align)				\
	do {								\
		uint datalign;						\
		datalign = (unsigned long)((_p)->data);			\
		datalign = roundup(datalign, (_align)) - datalign;	\
		ASSERT(datalign < (_align));				\
		ASSERT((_p)->len >= ((_len) + datalign));		\
		if (datalign)						\
			skb_pull((_p), datalign);			\
		__skb_trim((_p), (_len));				\
	} while (0)

/* Limit on rounding up frames */
static const uint max_roundup = 512;

/* Try doing readahead */
static bool dhd_readahead;

/* To check if there's window offered */
#define DATAOK(bus) \
	(((u8)(bus->tx_max - bus->tx_seq) != 0) && \
	(((u8)(bus->tx_max - bus->tx_seq) & 0x80) == 0))

/* Macros to get register read/write status */
/* NOTE: these assume a local dhdsdio_bus_t *bus! */
#define R_SDREG(regvar, regaddr, retryvar) \
do { \
	retryvar = 0; \
	do { \
		regvar = R_REG(regaddr); \
	} while (brcmf_sdcard_regfail(bus->sdh) && \
		 (++retryvar <= retry_limit)); \
	if (retryvar) { \
		bus->regfails += (retryvar-1); \
		if (retryvar > retry_limit) { \
			DHD_ERROR(("%s: FAILED" #regvar "READ, LINE %d\n", \
			__func__, __LINE__)); \
			regvar = 0; \
		} \
	} \
} while (0)

#define W_SDREG(regval, regaddr, retryvar) \
do { \
	retryvar = 0; \
	do { \
		W_REG(regaddr, regval); \
	} while (brcmf_sdcard_regfail(bus->sdh) && \
		 (++retryvar <= retry_limit)); \
	if (retryvar) { \
		bus->regfails += (retryvar-1); \
		if (retryvar > retry_limit) \
			DHD_ERROR(("%s: FAILED REGISTER WRITE, LINE %d\n", \
			__func__, __LINE__)); \
	} \
} while (0)

#define DHD_BUS			SDIO_BUS

#define PKT_AVAILABLE()		(intstatus & I_HMB_FRAME_IND)

#define HOSTINTMASK		(I_HMB_SW_MASK | I_CHIPACTIVE)

#ifdef SDTEST
static void brcmf_sdbrcm_checkdied(dhd_bus_t *bus, void *pkt, uint seq);
static void brcmf_sdbrcm_sdtest_set(dhd_bus_t *bus, bool start);
#endif

#ifdef DHD_DEBUG
static int brcmf_sdbrcm_checkdied(dhd_bus_t *bus, u8 *data, uint size);
static int brcmf_sdbrcm_mem_dump(dhd_bus_t *bus);
#endif				/* DHD_DEBUG  */
static int brcmf_sdbrcm_download_state(dhd_bus_t *bus, bool enter);

static void brcmf_sdbrcm_release(dhd_bus_t *bus);
static void brcmf_sdbrcm_release_malloc(dhd_bus_t *bus);
static void brcmf_sdbrcm_disconnect(void *ptr);
static bool brcmf_sdbrcm_chipmatch(u16 chipid);
static bool brcmf_sdbrcm_probe_attach(dhd_bus_t *bus, void *sdh,
				 void *regsva, u16 devid);
static bool brcmf_sdbrcm_probe_malloc(dhd_bus_t *bus, void *sdh);
static bool brcmf_sdbrcm_probe_init(dhd_bus_t *bus, void *sdh);
static void brcmf_sdbrcm_release_dongle(dhd_bus_t *bus);

static uint brcmf_process_nvram_vars(char *varbuf, uint len);

static void brcmf_sdbrcm_setmemsize(struct dhd_bus *bus, int mem_size);
static int brcmf_sdbrcm_send_buf(dhd_bus_t *bus, u32 addr, uint fn,
			       uint flags, u8 *buf, uint nbytes,
			       struct sk_buff *pkt, bcmsdh_cmplt_fn_t complete,
			       void *handle);

static bool brcmf_sdbrcm_download_firmware(struct dhd_bus *bus, void *sdh);
static int  _brcmf_sdbrcm_download_firmware(struct dhd_bus *bus);

static int
brcmf_sdbrcm_download_code_file(struct dhd_bus *bus, char *image_path);
static int brcmf_sdbrcm_download_nvram(struct dhd_bus *bus);
static void brcmf_sdbrcm_chip_disablecore(bcmsdh_info_t *sdh, u32 corebase);
static int brcmf_sdbrcm_chip_attach(struct dhd_bus *bus, void *regs);
static void brcmf_sdbrcm_chip_resetcore(bcmsdh_info_t *sdh, u32 corebase);
static void brcmf_sdbrcm_sdiod_drive_strength_init(struct dhd_bus *bus,
					u32 drivestrength);
static void brcmf_sdbrcm_chip_detach(struct dhd_bus *bus);

/* Packet free applicable unconditionally for sdio and sdspi.
 * Conditional if bufpool was present for gspi bus.
 */
static void brcmf_sdbrcm_pktfree2(dhd_bus_t *bus, struct sk_buff *pkt)
{
	if ((bus->bus != SPI_BUS) || bus->usebufpool)
		brcmu_pkt_buf_free_skb(pkt);
}

static void brcmf_sdbrcm_setmemsize(struct dhd_bus *bus, int mem_size)
{
	s32 min_size = DONGLE_MIN_MEMSIZE;
	/* Restrict the memsize to user specified limit */
	DHD_ERROR(("user: Restrict the dongle ram size to %d, min %d\n",
		dhd_dongle_memsize, min_size));
	if ((dhd_dongle_memsize > min_size) &&
	    (dhd_dongle_memsize < (s32) bus->orig_ramsize))
		bus->ramsize = dhd_dongle_memsize;
}

static int brcmf_sdbrcm_set_siaddr_window(dhd_bus_t *bus, u32 address)
{
	int err = 0;
	brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW,
			 (address >> 8) & SBSDIO_SBADDRLOW_MASK, &err);
	if (!err)
		brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1,
				 SBSDIO_FUNC1_SBADDRMID,
				 (address >> 16) & SBSDIO_SBADDRMID_MASK, &err);
	if (!err)
		brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1,
				       SBSDIO_FUNC1_SBADDRHIGH,
				       (address >> 24) & SBSDIO_SBADDRHIGH_MASK,
				       &err);
	return err;
}

/* Turn backplane clock on or off */
static int brcmf_sdbrcm_htclk(dhd_bus_t *bus, bool on, bool pendok)
{
	int err;
	u8 clkctl, clkreq, devctl;
	bcmsdh_info_t *sdh;

	DHD_TRACE(("%s: Enter\n", __func__));

#if defined(OOB_INTR_ONLY)
	pendok = false;
#endif
	clkctl = 0;
	sdh = bus->sdh;

	if (on) {
		/* Request HT Avail */
		clkreq =
		    bus->alp_only ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;

		if ((bus->ci->chip == BCM4329_CHIP_ID)
		    && (bus->ci->chiprev == 0))
			clkreq |= SBSDIO_FORCE_ALP;

		brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
				       SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
		if (err) {
			DHD_ERROR(("%s: HT Avail request error: %d\n",
				   __func__, err));
			return -EBADE;
		}

		if (pendok && ((bus->ci->buscoretype == PCMCIA_CORE_ID)
			       && (bus->ci->buscorerev == 9))) {
			u32 dummy, retries;
			R_SDREG(dummy, &bus->regs->clockctlstatus, retries);
		}

		/* Check current status */
		clkctl = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
					       SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			DHD_ERROR(("%s: HT Avail read error: %d\n",
				   __func__, err));
			return -EBADE;
		}

		/* Go to pending and await interrupt if appropriate */
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only) && pendok) {
			/* Allow only clock-available interrupt */
			devctl = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
					SBSDIO_DEVICE_CTL, &err);
			if (err) {
				DHD_ERROR(("%s: Devctl error setting CA: %d\n",
					__func__, err));
				return -EBADE;
			}

			devctl |= SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
					       SBSDIO_DEVICE_CTL, devctl, &err);
			DHD_INFO(("CLKCTL: set PENDING\n"));
			bus->clkstate = CLK_PENDING;

			return 0;
		} else if (bus->clkstate == CLK_PENDING) {
			/* Cancel CA-only interrupt filter */
			devctl =
			    brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
						  SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
				SBSDIO_DEVICE_CTL, devctl, &err);
		}

		/* Otherwise, wait here (polling) for HT Avail */
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			SPINWAIT_SLEEP(sdioh_spinwait_sleep,
				       ((clkctl =
					 brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
						 SBSDIO_FUNC1_CHIPCLKCSR,
							 &err)),
					!SBSDIO_CLKAV(clkctl, bus->alp_only)),
				       PMU_MAX_TRANSITION_DLY);
		}
		if (err) {
			DHD_ERROR(("%s: HT Avail request error: %d\n",
				   __func__, err));
			return -EBADE;
		}
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			DHD_ERROR(("%s: HT Avail timeout (%d): clkctl 0x%02x\n",
				   __func__, PMU_MAX_TRANSITION_DLY, clkctl));
			return -EBADE;
		}

		/* Mark clock available */
		bus->clkstate = CLK_AVAIL;
		DHD_INFO(("CLKCTL: turned ON\n"));

#if defined(DHD_DEBUG)
		if (bus->alp_only == true) {
#if !defined(BCMLXSDMMC)
			if (!SBSDIO_ALPONLY(clkctl)) {
				DHD_ERROR(("%s: HT Clock, when ALP Only\n",
					   __func__));
			}
#endif				/* !defined(BCMLXSDMMC) */
		} else {
			if (SBSDIO_ALPONLY(clkctl)) {
				DHD_ERROR(("%s: HT Clock should be on.\n",
					   __func__));
			}
		}
#endif				/* defined (DHD_DEBUG) */

		bus->activity = true;
	} else {
		clkreq = 0;

		if (bus->clkstate == CLK_PENDING) {
			/* Cancel CA-only interrupt filter */
			devctl = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
					SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
				SBSDIO_DEVICE_CTL, devctl, &err);
		}

		bus->clkstate = CLK_SDONLY;
		brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
		DHD_INFO(("CLKCTL: turned OFF\n"));
		if (err) {
			DHD_ERROR(("%s: Failed access turning clock off: %d\n",
				   __func__, err));
			return -EBADE;
		}
	}
	return 0;
}

/* Change idle/active SD state */
static int brcmf_sdbrcm_sdclk(dhd_bus_t *bus, bool on)
{
	DHD_TRACE(("%s: Enter\n", __func__));

	if (on)
		bus->clkstate = CLK_SDONLY;
	else
		bus->clkstate = CLK_NONE;

	return 0;
}

/* Transition SD and backplane clock readiness */
static int brcmf_sdbrcm_clkctl(dhd_bus_t *bus, uint target, bool pendok)
{
#ifdef DHD_DEBUG
	uint oldstate = bus->clkstate;
#endif				/* DHD_DEBUG */

	DHD_TRACE(("%s: Enter\n", __func__));

	/* Early exit if we're already there */
	if (bus->clkstate == target) {
		if (target == CLK_AVAIL) {
			dhd_os_wd_timer(bus->dhd, dhd_watchdog_ms);
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
		dhd_os_wd_timer(bus->dhd, dhd_watchdog_ms);
		bus->activity = true;
		break;

	case CLK_SDONLY:
		/* Remove HT request, or bring up SD clock */
		if (bus->clkstate == CLK_NONE)
			brcmf_sdbrcm_sdclk(bus, true);
		else if (bus->clkstate == CLK_AVAIL)
			brcmf_sdbrcm_htclk(bus, false, false);
		else
			DHD_ERROR(("brcmf_sdbrcm_clkctl: request for %d -> %d"
				   "\n", bus->clkstate, target));
		dhd_os_wd_timer(bus->dhd, dhd_watchdog_ms);
		break;

	case CLK_NONE:
		/* Make sure to remove HT request */
		if (bus->clkstate == CLK_AVAIL)
			brcmf_sdbrcm_htclk(bus, false, false);
		/* Now remove the SD clock */
		brcmf_sdbrcm_sdclk(bus, false);
		dhd_os_wd_timer(bus->dhd, 0);
		break;
	}
#ifdef DHD_DEBUG
	DHD_INFO(("brcmf_sdbrcm_clkctl: %d -> %d\n", oldstate, bus->clkstate));
#endif				/* DHD_DEBUG */

	return 0;
}

int brcmf_sdbrcm_bussleep(dhd_bus_t *bus, bool sleep)
{
	bcmsdh_info_t *sdh = bus->sdh;
	struct sdpcmd_regs *regs = bus->regs;
	uint retries = 0;

	DHD_INFO(("brcmf_sdbrcm_bussleep: request %s (currently %s)\n",
		  (sleep ? "SLEEP" : "WAKE"),
		  (bus->sleeping ? "SLEEP" : "WAKE")));

	/* Done if we're already in the requested state */
	if (sleep == bus->sleeping)
		return 0;

	/* Going to sleep: set the alarm and turn off the lights... */
	if (sleep) {
		/* Don't sleep if something is pending */
		if (bus->dpc_sched || bus->rxskip || pktq_len(&bus->txq))
			return -EBUSY;

		/* Disable SDIO interrupts (no longer interested) */
		brcmf_sdcard_intr_disable(bus->sdh);

		/* Make sure the controller has the bus up */
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

		/* Tell device to start using OOB wakeup */
		W_SDREG(SMB_USE_OOB, &regs->tosbmailbox, retries);
		if (retries > retry_limit)
			DHD_ERROR(("CANNOT SIGNAL CHIP, WILL NOT WAKE UP!!\n"));

		/* Turn off our contribution to the HT clock request */
		brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);

		brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR,
			SBSDIO_FORCE_HW_CLKREQ_OFF, NULL);

		/* Isolate the bus */
		if (bus->ci->chip != BCM4329_CHIP_ID
		    && bus->ci->chip != BCM4319_CHIP_ID) {
			brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
				SBSDIO_DEVICE_CTL,
				SBSDIO_DEVCTL_PADS_ISO, NULL);
		}

		/* Change state */
		bus->sleeping = true;

	} else {
		/* Waking up: bus power up is ok, set local state */

		brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);

		/* Force pad isolation off if possible
			 (in case power never toggled) */
		if ((bus->ci->buscoretype == PCMCIA_CORE_ID)
		    && (bus->ci->buscorerev >= 10))
			brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
				SBSDIO_DEVICE_CTL, 0, NULL);

		/* Make sure the controller has the bus up */
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

		/* Send misc interrupt to indicate OOB not needed */
		W_SDREG(0, &regs->tosbmailboxdata, retries);
		if (retries <= retry_limit)
			W_SDREG(SMB_DEV_INT, &regs->tosbmailbox, retries);

		if (retries > retry_limit)
			DHD_ERROR(("CANNOT SIGNAL CHIP TO CLEAR OOB!!\n"));

		/* Make sure we have SD bus access */
		brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);

		/* Change state */
		bus->sleeping = false;

		/* Enable interrupts again */
		if (bus->intr && (bus->dhd->busstate == DHD_BUS_DATA)) {
			bus->intdis = false;
			brcmf_sdcard_intr_enable(bus->sdh);
		}
	}

	return 0;
}

#if defined(OOB_INTR_ONLY)
void brcmf_sdbrcm_enable_oob_intr(struct dhd_bus *bus, bool enable)
{
#if defined(HW_OOB)
	brcmf_sdcard_enable_hw_oob_intr(bus->sdh, enable);
#else
	sdpcmd_regs_t *regs = bus->regs;
	uint retries = 0;

	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
	if (enable == true) {

		/* Tell device to start using OOB wakeup */
		W_SDREG(SMB_USE_OOB, &regs->tosbmailbox, retries);
		if (retries > retry_limit)
			DHD_ERROR(("CANNOT SIGNAL CHIP, WILL NOT WAKE UP!!\n"));

	} else {
		/* Send misc interrupt to indicate OOB not needed */
		W_SDREG(0, &regs->tosbmailboxdata, retries);
		if (retries <= retry_limit)
			W_SDREG(SMB_DEV_INT, &regs->tosbmailbox, retries);
	}

	/* Turn off our contribution to the HT clock request */
	brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);
#endif				/* !defined(HW_OOB) */
}
#endif				/* defined(OOB_INTR_ONLY) */

#define BUS_WAKE(bus) \
	do { \
		if ((bus)->sleeping) \
			brcmf_sdbrcm_bussleep((bus), false); \
	} while (0);

/* Writes a HW/SW header into the packet and sends it. */
/* Assumes: (a) header space already there, (b) caller holds lock */
static int brcmf_sdbrcm_txpkt(dhd_bus_t *bus, struct sk_buff *pkt, uint chan,
			 bool free_pkt)
{
	int ret;
	u8 *frame;
	u16 len, pad = 0;
	u32 swheader;
	uint retries = 0;
	bcmsdh_info_t *sdh;
	struct sk_buff *new;
	int i;

	DHD_TRACE(("%s: Enter\n", __func__));

	sdh = bus->sdh;

	if (bus->dhd->dongle_reset) {
		ret = -EPERM;
		goto done;
	}

	frame = (u8 *) (pkt->data);

	/* Add alignment padding, allocate new packet if needed */
	pad = ((unsigned long)frame % DHD_SDALIGN);
	if (pad) {
		if (skb_headroom(pkt) < pad) {
			DHD_INFO(("%s: insufficient headroom %d for %d pad\n",
				  __func__, skb_headroom(pkt), pad));
			bus->dhd->tx_realloc++;
			new = brcmu_pkt_buf_get_skb(pkt->len + DHD_SDALIGN);
			if (!new) {
				DHD_ERROR(("%s: couldn't allocate new %d-byte "
					"packet\n",
					__func__, pkt->len + DHD_SDALIGN));
				ret = -ENOMEM;
				goto done;
			}

			PKTALIGN(new, pkt->len, DHD_SDALIGN);
			memcpy(new->data, pkt->data, pkt->len);
			if (free_pkt)
				brcmu_pkt_buf_free_skb(pkt);
			/* free the pkt if canned one is not used */
			free_pkt = true;
			pkt = new;
			frame = (u8 *) (pkt->data);
			ASSERT(((unsigned long)frame % DHD_SDALIGN) == 0);
			pad = 0;
		} else {
			skb_push(pkt, pad);
			frame = (u8 *) (pkt->data);

			ASSERT((pad + SDPCM_HDRLEN) <= (int)(pkt->len));
			memset(frame, 0, pad + SDPCM_HDRLEN);
		}
	}
	ASSERT(pad < DHD_SDALIGN);

	/* Hardware tag: 2 byte len followed by 2 byte ~len check (all LE) */
	len = (u16) (pkt->len);
	*(u16 *) frame = cpu_to_le16(len);
	*(((u16 *) frame) + 1) = cpu_to_le16(~len);

	/* Software tag: channel, sequence number, data offset */
	swheader =
	    ((chan << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK) | bus->tx_seq |
	    (((pad +
	       SDPCM_HDRLEN) << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK);

	put_unaligned_le32(swheader, frame + SDPCM_FRAMETAG_LEN);
	put_unaligned_le32(0, frame + SDPCM_FRAMETAG_LEN + sizeof(swheader));

#ifdef DHD_DEBUG
	tx_packets[pkt->priority]++;
	if (DHD_BYTES_ON() &&
	    (((DHD_CTL_ON() && (chan == SDPCM_CONTROL_CHANNEL)) ||
	      (DHD_DATA_ON() && (chan != SDPCM_CONTROL_CHANNEL))))) {
		printk(KERN_DEBUG "Tx Frame:\n");
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, frame, len);
	} else if (DHD_HDRS_ON()) {
		printk(KERN_DEBUG "TxHdr:\n");
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
				     frame, min_t(u16, len, 16));
	}
#endif

	/* Raise len to next SDIO block to eliminate tail command */
	if (bus->roundup && bus->blocksize && (len > bus->blocksize)) {
		u16 pad = bus->blocksize - (len % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize))
#ifdef NOTUSED
			if (pad <= skb_tailroom(pkt))
#endif				/* NOTUSED */
				len += pad;
	} else if (len % DHD_SDALIGN) {
		len += DHD_SDALIGN - (len % DHD_SDALIGN);
	}

	/* Some controllers have trouble with odd bytes -- round to even */
	if (forcealign && (len & (ALIGNMENT - 1))) {
#ifdef NOTUSED
		if (skb_tailroom(pkt))
#endif
			len = roundup(len, ALIGNMENT);
#ifdef NOTUSED
		else
			DHD_ERROR(("%s: sending unrounded %d-byte packet\n",
				   __func__, len));
#endif
	}

	do {
		ret = brcmf_sdbrcm_send_buf(bus, brcmf_sdcard_cur_sbwad(sdh),
			SDIO_FUNC_2, F2SYNC, frame, len, pkt, NULL, NULL);
		bus->f2txdata++;
		ASSERT(ret != -BCME_PENDING);

		if (ret < 0) {
			/* On failure, abort the command
			 and terminate the frame */
			DHD_INFO(("%s: sdio error %d, abort command and "
				"terminate frame.\n", __func__, ret));
			bus->tx_sderrs++;

			brcmf_sdcard_abort(sdh, SDIO_FUNC_2);
			brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
					 SBSDIO_FUNC1_FRAMECTRL, SFC_WF_TERM,
					 NULL);
			bus->f1regdata++;

			for (i = 0; i < 3; i++) {
				u8 hi, lo;
				hi = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
						     SBSDIO_FUNC1_WFRAMEBCHI,
						     NULL);
				lo = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
						     SBSDIO_FUNC1_WFRAMEBCLO,
						     NULL);
				bus->f1regdata += 2;
				if ((hi == 0) && (lo == 0))
					break;
			}

		}
		if (ret == 0)
			bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

	} while ((ret < 0) && retrydata && retries++ < TXRETRIES);

done:
	/* restore pkt buffer pointer before calling tx complete routine */
	skb_pull(pkt, SDPCM_HDRLEN + pad);
	dhd_os_sdunlock(bus->dhd);
	dhd_txcomplete(bus->dhd, pkt, ret != 0);
	dhd_os_sdlock(bus->dhd);

	if (free_pkt)
		brcmu_pkt_buf_free_skb(pkt);

	return ret;
}

int brcmf_sdbrcm_bus_txdata(struct dhd_bus *bus, struct sk_buff *pkt)
{
	int ret = -EBADE;
	uint datalen, prec;

	DHD_TRACE(("%s: Enter\n", __func__));

	datalen = pkt->len;

#ifdef SDTEST
	/* Push the test header if doing loopback */
	if (bus->ext_loop) {
		u8 *data;
		skb_push(pkt, SDPCM_TEST_HDRLEN);
		data = pkt->data;
		*data++ = SDPCM_TEST_ECHOREQ;
		*data++ = (u8) bus->loopid++;
		*data++ = (datalen >> 0);
		*data++ = (datalen >> 8);
		datalen += SDPCM_TEST_HDRLEN;
	}
#endif				/* SDTEST */

	/* Add space for the header */
	skb_push(pkt, SDPCM_HDRLEN);
	ASSERT(IS_ALIGNED((unsigned long)(pkt->data), 2));

	prec = PRIO2PREC((pkt->priority & PRIOMASK));

	/* Check for existing queue, current flow-control,
			 pending event, or pending clock */
	if (dhd_deferred_tx || bus->fcstate || pktq_len(&bus->txq)
	    || bus->dpc_sched || (!DATAOK(bus))
	    || (bus->flowcontrol & NBITVAL(prec))
	    || (bus->clkstate != CLK_AVAIL)) {
		DHD_TRACE(("%s: deferring pktq len %d\n", __func__,
			   pktq_len(&bus->txq)));
		bus->fcqueued++;

		/* Priority based enq */
		spin_lock_bh(&bus->txqlock);
		if (dhd_prec_enq(bus->dhd, &bus->txq, pkt, prec) == false) {
			skb_pull(pkt, SDPCM_HDRLEN);
			dhd_txcomplete(bus->dhd, pkt, false);
			brcmu_pkt_buf_free_skb(pkt);
			DHD_ERROR(("%s: out of bus->txq !!!\n", __func__));
			ret = -ENOSR;
		} else {
			ret = 0;
		}
		spin_unlock_bh(&bus->txqlock);

		if (pktq_len(&bus->txq) >= TXHI)
			dhd_txflowcontrol(bus->dhd, 0, ON);

#ifdef DHD_DEBUG
		if (pktq_plen(&bus->txq, prec) > qcount[prec])
			qcount[prec] = pktq_plen(&bus->txq, prec);
#endif
		/* Schedule DPC if needed to send queued packet(s) */
		if (dhd_deferred_tx && !bus->dpc_sched) {
			bus->dpc_sched = true;
			dhd_sched_dpc(bus->dhd);
		}
	} else {
		/* Lock: we're about to use shared data/code (and SDIO) */
		dhd_os_sdlock(bus->dhd);

		/* Otherwise, send it now */
		BUS_WAKE(bus);
		/* Make sure back plane ht clk is on, no pending allowed */
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, true);

#ifndef SDTEST
		DHD_TRACE(("%s: calling txpkt\n", __func__));
		ret = brcmf_sdbrcm_txpkt(bus, pkt, SDPCM_DATA_CHANNEL, true);
#else
		ret = brcmf_sdbrcm_txpkt(bus, pkt,
				    (bus->ext_loop ? SDPCM_TEST_CHANNEL :
				     SDPCM_DATA_CHANNEL), true);
#endif
		if (ret)
			bus->dhd->tx_errors++;
		else
			bus->dhd->dstats.tx_bytes += datalen;

		if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
			bus->activity = false;
			brcmf_sdbrcm_clkctl(bus, CLK_NONE, true);
		}

		dhd_os_sdunlock(bus->dhd);
	}

	return ret;
}

static uint brcmf_sdbrcm_sendfromq(dhd_bus_t *bus, uint maxframes)
{
	struct sk_buff *pkt;
	u32 intstatus = 0;
	uint retries = 0;
	int ret = 0, prec_out;
	uint cnt = 0;
	uint datalen;
	u8 tx_prec_map;

	dhd_pub_t *dhd = bus->dhd;
	struct sdpcmd_regs *regs = bus->regs;

	DHD_TRACE(("%s: Enter\n", __func__));

	tx_prec_map = ~bus->flowcontrol;

	/* Send frames until the limit or some other event */
	for (cnt = 0; (cnt < maxframes) && DATAOK(bus); cnt++) {
		spin_lock_bh(&bus->txqlock);
		pkt = brcmu_pktq_mdeq(&bus->txq, tx_prec_map, &prec_out);
		if (pkt == NULL) {
			spin_unlock_bh(&bus->txqlock);
			break;
		}
		spin_unlock_bh(&bus->txqlock);
		datalen = pkt->len - SDPCM_HDRLEN;

#ifndef SDTEST
		ret = brcmf_sdbrcm_txpkt(bus, pkt, SDPCM_DATA_CHANNEL, true);
#else
		ret = brcmf_sdbrcm_txpkt(bus, pkt,
				    (bus->ext_loop ? SDPCM_TEST_CHANNEL :
				     SDPCM_DATA_CHANNEL), true);
#endif
		if (ret)
			bus->dhd->tx_errors++;
		else
			bus->dhd->dstats.tx_bytes += datalen;

		/* In poll mode, need to check for other events */
		if (!bus->intr && cnt) {
			/* Check device status, signal pending interrupt */
			R_SDREG(intstatus, &regs->intstatus, retries);
			bus->f2txdata++;
			if (brcmf_sdcard_regfail(bus->sdh))
				break;
			if (intstatus & bus->hostintmask)
				bus->ipend = true;
		}
	}

	/* Deflow-control stack if needed */
	if (dhd->up && (dhd->busstate == DHD_BUS_DATA) &&
	    dhd->txoff && (pktq_len(&bus->txq) < TXLOW))
		dhd_txflowcontrol(dhd, 0, OFF);

	return cnt;
}

int
brcmf_sdbrcm_bus_txctl(struct dhd_bus *bus, unsigned char *msg, uint msglen)
{
	u8 *frame;
	u16 len;
	u32 swheader;
	uint retries = 0;
	bcmsdh_info_t *sdh = bus->sdh;
	u8 doff = 0;
	int ret = -1;
	int i;

	DHD_TRACE(("%s: Enter\n", __func__));

	if (bus->dhd->dongle_reset)
		return -EIO;

	/* Back the pointer to make a room for bus header */
	frame = msg - SDPCM_HDRLEN;
	len = (msglen += SDPCM_HDRLEN);

	/* Add alignment padding (optional for ctl frames) */
	if (dhd_alignctl) {
		doff = ((unsigned long)frame % DHD_SDALIGN);
		if (doff) {
			frame -= doff;
			len += doff;
			msglen += doff;
			memset(frame, 0, doff + SDPCM_HDRLEN);
		}
		ASSERT(doff < DHD_SDALIGN);
	}
	doff += SDPCM_HDRLEN;

	/* Round send length to next SDIO block */
	if (bus->roundup && bus->blocksize && (len > bus->blocksize)) {
		u16 pad = bus->blocksize - (len % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize))
			len += pad;
	} else if (len % DHD_SDALIGN) {
		len += DHD_SDALIGN - (len % DHD_SDALIGN);
	}

	/* Satisfy length-alignment requirements */
	if (forcealign && (len & (ALIGNMENT - 1)))
		len = roundup(len, ALIGNMENT);

	ASSERT(IS_ALIGNED((unsigned long)frame, 2));

	/* Need to lock here to protect txseq and SDIO tx calls */
	dhd_os_sdlock(bus->dhd);

	BUS_WAKE(bus);

	/* Make sure backplane clock is on */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

	/* Hardware tag: 2 byte len followed by 2 byte ~len check (all LE) */
	*(u16 *) frame = cpu_to_le16((u16) msglen);
	*(((u16 *) frame) + 1) = cpu_to_le16(~msglen);

	/* Software tag: channel, sequence number, data offset */
	swheader =
	    ((SDPCM_CONTROL_CHANNEL << SDPCM_CHANNEL_SHIFT) &
	     SDPCM_CHANNEL_MASK)
	    | bus->tx_seq | ((doff << SDPCM_DOFFSET_SHIFT) &
			     SDPCM_DOFFSET_MASK);
	put_unaligned_le32(swheader, frame + SDPCM_FRAMETAG_LEN);
	put_unaligned_le32(0, frame + SDPCM_FRAMETAG_LEN + sizeof(swheader));

	if (!DATAOK(bus)) {
		DHD_INFO(("%s: No bus credit bus->tx_max %d, bus->tx_seq %d\n",
			  __func__, bus->tx_max, bus->tx_seq));
		bus->ctrl_frame_stat = true;
		/* Send from dpc */
		bus->ctrl_frame_buf = frame;
		bus->ctrl_frame_len = len;

		dhd_wait_for_event(bus->dhd, &bus->ctrl_frame_stat);

		if (bus->ctrl_frame_stat == false) {
			DHD_INFO(("%s: ctrl_frame_stat == false\n", __func__));
			ret = 0;
		} else {
			DHD_INFO(("%s: ctrl_frame_stat == true\n", __func__));
			ret = -1;
		}
	}

	if (ret == -1) {
#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() && DHD_CTL_ON()) {
			printk(KERN_DEBUG "Tx Frame:\n");
			print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
					     frame, len);
		} else if (DHD_HDRS_ON()) {
			printk(KERN_DEBUG "TxHdr:\n");
			print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
					     frame, min_t(u16, len, 16));
		}
#endif

		do {
			bus->ctrl_frame_stat = false;
			ret = brcmf_sdbrcm_send_buf(bus,
				brcmf_sdcard_cur_sbwad(sdh), SDIO_FUNC_2,
				F2SYNC, frame, len, NULL, NULL, NULL);

			ASSERT(ret != -BCME_PENDING);

			if (ret < 0) {
				/* On failure, abort the command and
				 terminate the frame */
				DHD_INFO(("%s: sdio error %d, abort command and terminate frame.\n",
					__func__, ret));
				bus->tx_sderrs++;

				brcmf_sdcard_abort(sdh, SDIO_FUNC_2);

				brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
						 SBSDIO_FUNC1_FRAMECTRL,
						 SFC_WF_TERM, NULL);
				bus->f1regdata++;

				for (i = 0; i < 3; i++) {
					u8 hi, lo;
					hi = brcmf_sdcard_cfg_read(sdh,
					     SDIO_FUNC_1,
					     SBSDIO_FUNC1_WFRAMEBCHI,
					     NULL);
					lo = brcmf_sdcard_cfg_read(sdh,
					     SDIO_FUNC_1,
					     SBSDIO_FUNC1_WFRAMEBCLO,
					     NULL);
					bus->f1regdata += 2;
					if ((hi == 0) && (lo == 0))
						break;
				}

			}
			if (ret == 0) {
				bus->tx_seq =
				    (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;
			}
		} while ((ret < 0) && retries++ < TXRETRIES);
	}

	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
		bus->activity = false;
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, true);
	}

	dhd_os_sdunlock(bus->dhd);

	if (ret)
		bus->dhd->tx_ctlerrs++;
	else
		bus->dhd->tx_ctlpkts++;

	return ret ? -EIO : 0;
}

int brcmf_sdbrcm_bus_rxctl(struct dhd_bus *bus, unsigned char *msg, uint msglen)
{
	int timeleft;
	uint rxlen = 0;
	bool pending;

	DHD_TRACE(("%s: Enter\n", __func__));

	if (bus->dhd->dongle_reset)
		return -EIO;

	/* Wait until control frame is available */
	timeleft = dhd_os_ioctl_resp_wait(bus->dhd, &bus->rxlen, &pending);

	dhd_os_sdlock(bus->dhd);
	rxlen = bus->rxlen;
	memcpy(msg, bus->rxctl, min(msglen, rxlen));
	bus->rxlen = 0;
	dhd_os_sdunlock(bus->dhd);

	if (rxlen) {
		DHD_CTL(("%s: resumed on rxctl frame, got %d expected %d\n",
			 __func__, rxlen, msglen));
	} else if (timeleft == 0) {
		DHD_ERROR(("%s: resumed on timeout\n", __func__));
#ifdef DHD_DEBUG
		dhd_os_sdlock(bus->dhd);
		brcmf_sdbrcm_checkdied(bus, NULL, 0);
		dhd_os_sdunlock(bus->dhd);
#endif				/* DHD_DEBUG */
	} else if (pending == true) {
		DHD_CTL(("%s: cancelled\n", __func__));
		return -ERESTARTSYS;
	} else {
		DHD_CTL(("%s: resumed for unknown reason?\n", __func__));
#ifdef DHD_DEBUG
		dhd_os_sdlock(bus->dhd);
		brcmf_sdbrcm_checkdied(bus, NULL, 0);
		dhd_os_sdunlock(bus->dhd);
#endif				/* DHD_DEBUG */
	}

	if (rxlen)
		bus->dhd->rx_ctlpkts++;
	else
		bus->dhd->rx_ctlerrs++;

	return rxlen ? (int)rxlen : -ETIMEDOUT;
}

/* IOVar table */
enum {
	IOV_INTR = 1,
	IOV_POLLRATE,
	IOV_SDREG,
	IOV_SBREG,
	IOV_SDCIS,
	IOV_MEMBYTES,
	IOV_MEMSIZE,
#ifdef DHD_DEBUG
	IOV_CHECKDIED,
#endif
	IOV_DOWNLOAD,
	IOV_FORCEEVEN,
	IOV_SDIOD_DRIVE,
	IOV_READAHEAD,
	IOV_SDRXCHAIN,
	IOV_ALIGNCTL,
	IOV_SDALIGN,
	IOV_DEVRESET,
	IOV_CPU,
#ifdef SDTEST
	IOV_PKTGEN,
	IOV_EXTLOOP,
#endif				/* SDTEST */
	IOV_SPROM,
	IOV_TXBOUND,
	IOV_RXBOUND,
	IOV_TXMINMAX,
	IOV_IDLETIME,
	IOV_IDLECLOCK,
	IOV_SD1IDLE,
	IOV_SLEEP,
	IOV_VARS
};

const struct brcmu_iovar dhdsdio_iovars[] = {
	{"intr", IOV_INTR, 0, IOVT_BOOL, 0},
	{"sleep", IOV_SLEEP, 0, IOVT_BOOL, 0},
	{"pollrate", IOV_POLLRATE, 0, IOVT_UINT32, 0},
	{"idletime", IOV_IDLETIME, 0, IOVT_INT32, 0},
	{"idleclock", IOV_IDLECLOCK, 0, IOVT_INT32, 0},
	{"sd1idle", IOV_SD1IDLE, 0, IOVT_BOOL, 0},
	{"membytes", IOV_MEMBYTES, 0, IOVT_BUFFER, 2 * sizeof(int)},
	{"memsize", IOV_MEMSIZE, 0, IOVT_UINT32, 0},
	{"download", IOV_DOWNLOAD, 0, IOVT_BOOL, 0},
	{"vars", IOV_VARS, 0, IOVT_BUFFER, 0},
	{"sdiod_drive", IOV_SDIOD_DRIVE, 0, IOVT_UINT32, 0},
	{"readahead", IOV_READAHEAD, 0, IOVT_BOOL, 0},
	{"sdrxchain", IOV_SDRXCHAIN, 0, IOVT_BOOL, 0},
	{"alignctl", IOV_ALIGNCTL, 0, IOVT_BOOL, 0},
	{"sdalign", IOV_SDALIGN, 0, IOVT_BOOL, 0},
	{"devreset", IOV_DEVRESET, 0, IOVT_BOOL, 0},
#ifdef DHD_DEBUG
	{"sdreg", IOV_SDREG, 0, IOVT_BUFFER, sizeof(sdreg_t)}
	,
	{"sbreg", IOV_SBREG, 0, IOVT_BUFFER, sizeof(sdreg_t)}
	,
	{"sd_cis", IOV_SDCIS, 0, IOVT_BUFFER, DHD_IOCTL_MAXLEN}
	,
	{"forcealign", IOV_FORCEEVEN, 0, IOVT_BOOL, 0}
	,
	{"txbound", IOV_TXBOUND, 0, IOVT_UINT32, 0}
	,
	{"rxbound", IOV_RXBOUND, 0, IOVT_UINT32, 0}
	,
	{"txminmax", IOV_TXMINMAX, 0, IOVT_UINT32, 0}
	,
	{"cpu", IOV_CPU, 0, IOVT_BOOL, 0}
	,
#ifdef DHD_DEBUG
	{"checkdied", IOV_CHECKDIED, 0, IOVT_BUFFER, 0}
	,
#endif				/* DHD_DEBUG  */
#endif				/* DHD_DEBUG */
#ifdef SDTEST
	{"extloop", IOV_EXTLOOP, 0, IOVT_BOOL, 0}
	,
	{"pktgen", IOV_PKTGEN, 0, IOVT_BUFFER, sizeof(dhd_pktgen_t)}
	,
#endif				/* SDTEST */

	{NULL, 0, 0, 0, 0}
};

static void
dhd_dump_pct(struct brcmu_strbuf *strbuf, char *desc, uint num, uint div)
{
	uint q1, q2;

	if (!div) {
		brcmu_bprintf(strbuf, "%s N/A", desc);
	} else {
		q1 = num / div;
		q2 = (100 * (num - (q1 * div))) / div;
		brcmu_bprintf(strbuf, "%s %d.%02d", desc, q1, q2);
	}
}

void brcmf_sdbrcm_bus_dump(dhd_pub_t *dhdp, struct brcmu_strbuf *strbuf)
{
	dhd_bus_t *bus = dhdp->bus;

	brcmu_bprintf(strbuf, "Bus SDIO structure:\n");
	brcmu_bprintf(strbuf,
		    "hostintmask 0x%08x intstatus 0x%08x sdpcm_ver %d\n",
		    bus->hostintmask, bus->intstatus, bus->sdpcm_ver);
	brcmu_bprintf(strbuf,
		    "fcstate %d qlen %d tx_seq %d, max %d, rxskip %d rxlen %d rx_seq %d\n",
		    bus->fcstate, pktq_len(&bus->txq), bus->tx_seq, bus->tx_max,
		    bus->rxskip, bus->rxlen, bus->rx_seq);
	brcmu_bprintf(strbuf, "intr %d intrcount %d lastintrs %d spurious %d\n",
		    bus->intr, bus->intrcount, bus->lastintrs, bus->spurious);
	brcmu_bprintf(strbuf, "pollrate %d pollcnt %d regfails %d\n",
		    bus->pollrate, bus->pollcnt, bus->regfails);

	brcmu_bprintf(strbuf, "\nAdditional counters:\n");
	brcmu_bprintf(strbuf,
		    "tx_sderrs %d fcqueued %d rxrtx %d rx_toolong %d rxc_errors %d\n",
		    bus->tx_sderrs, bus->fcqueued, bus->rxrtx, bus->rx_toolong,
		    bus->rxc_errors);
	brcmu_bprintf(strbuf, "rx_hdrfail %d badhdr %d badseq %d\n",
		    bus->rx_hdrfail, bus->rx_badhdr, bus->rx_badseq);
	brcmu_bprintf(strbuf, "fc_rcvd %d, fc_xoff %d, fc_xon %d\n",
		      bus->fc_rcvd, bus->fc_xoff, bus->fc_xon);
	brcmu_bprintf(strbuf, "rxglomfail %d, rxglomframes %d, rxglompkts %d\n",
		    bus->rxglomfail, bus->rxglomframes, bus->rxglompkts);
	brcmu_bprintf(strbuf, "f2rx (hdrs/data) %d (%d/%d), f2tx %d f1regs"
		      " %d\n",
		      (bus->f2rxhdrs + bus->f2rxdata), bus->f2rxhdrs,
		      bus->f2rxdata, bus->f2txdata, bus->f1regdata);
	{
		dhd_dump_pct(strbuf, "\nRx: pkts/f2rd", bus->dhd->rx_packets,
			     (bus->f2rxhdrs + bus->f2rxdata));
		dhd_dump_pct(strbuf, ", pkts/f1sd", bus->dhd->rx_packets,
			     bus->f1regdata);
		dhd_dump_pct(strbuf, ", pkts/sd", bus->dhd->rx_packets,
			     (bus->f2rxhdrs + bus->f2rxdata + bus->f1regdata));
		dhd_dump_pct(strbuf, ", pkts/int", bus->dhd->rx_packets,
			     bus->intrcount);
		brcmu_bprintf(strbuf, "\n");

		dhd_dump_pct(strbuf, "Rx: glom pct", (100 * bus->rxglompkts),
			     bus->dhd->rx_packets);
		dhd_dump_pct(strbuf, ", pkts/glom", bus->rxglompkts,
			     bus->rxglomframes);
		brcmu_bprintf(strbuf, "\n");

		dhd_dump_pct(strbuf, "Tx: pkts/f2wr", bus->dhd->tx_packets,
			     bus->f2txdata);
		dhd_dump_pct(strbuf, ", pkts/f1sd", bus->dhd->tx_packets,
			     bus->f1regdata);
		dhd_dump_pct(strbuf, ", pkts/sd", bus->dhd->tx_packets,
			     (bus->f2txdata + bus->f1regdata));
		dhd_dump_pct(strbuf, ", pkts/int", bus->dhd->tx_packets,
			     bus->intrcount);
		brcmu_bprintf(strbuf, "\n");

		dhd_dump_pct(strbuf, "Total: pkts/f2rw",
			     (bus->dhd->tx_packets + bus->dhd->rx_packets),
			     (bus->f2txdata + bus->f2rxhdrs + bus->f2rxdata));
		dhd_dump_pct(strbuf, ", pkts/f1sd",
			     (bus->dhd->tx_packets + bus->dhd->rx_packets),
			     bus->f1regdata);
		dhd_dump_pct(strbuf, ", pkts/sd",
			     (bus->dhd->tx_packets + bus->dhd->rx_packets),
			     (bus->f2txdata + bus->f2rxhdrs + bus->f2rxdata +
			      bus->f1regdata));
		dhd_dump_pct(strbuf, ", pkts/int",
			     (bus->dhd->tx_packets + bus->dhd->rx_packets),
			     bus->intrcount);
		brcmu_bprintf(strbuf, "\n\n");
	}

#ifdef SDTEST
	if (bus->pktgen_count) {
		brcmu_bprintf(strbuf, "pktgen config and count:\n");
		brcmu_bprintf(strbuf,
			    "freq %d count %d print %d total %d min %d len %d\n",
			    bus->pktgen_freq, bus->pktgen_count,
			    bus->pktgen_print, bus->pktgen_total,
			    bus->pktgen_minlen, bus->pktgen_maxlen);
		brcmu_bprintf(strbuf, "send attempts %d rcvd %d fail %d\n",
			    bus->pktgen_sent, bus->pktgen_rcvd,
			    bus->pktgen_fail);
	}
#endif				/* SDTEST */
#ifdef DHD_DEBUG
	brcmu_bprintf(strbuf, "dpc_sched %d host interrupt%spending\n",
		    bus->dpc_sched,
		    (brcmf_sdcard_intr_pending(bus->sdh) ? " " : " not "));
	brcmu_bprintf(strbuf, "blocksize %d roundup %d\n", bus->blocksize,
		    bus->roundup);
#endif				/* DHD_DEBUG */
	brcmu_bprintf(strbuf,
		    "clkstate %d activity %d idletime %d idlecount %d sleeping %d\n",
		    bus->clkstate, bus->activity, bus->idletime, bus->idlecount,
		    bus->sleeping);
}

void dhd_bus_clearcounts(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = (dhd_bus_t *) dhdp->bus;

	bus->intrcount = bus->lastintrs = bus->spurious = bus->regfails = 0;
	bus->rxrtx = bus->rx_toolong = bus->rxc_errors = 0;
	bus->rx_hdrfail = bus->rx_badhdr = bus->rx_badseq = 0;
	bus->tx_sderrs = bus->fc_rcvd = bus->fc_xoff = bus->fc_xon = 0;
	bus->rxglomfail = bus->rxglomframes = bus->rxglompkts = 0;
	bus->f2rxhdrs = bus->f2rxdata = bus->f2txdata = bus->f1regdata = 0;
}

#ifdef SDTEST
static int brcmf_sdbrcm_pktgen_get(dhd_bus_t *bus, u8 *arg)
{
	dhd_pktgen_t pktgen;

	pktgen.version = DHD_PKTGEN_VERSION;
	pktgen.freq = bus->pktgen_freq;
	pktgen.count = bus->pktgen_count;
	pktgen.print = bus->pktgen_print;
	pktgen.total = bus->pktgen_total;
	pktgen.minlen = bus->pktgen_minlen;
	pktgen.maxlen = bus->pktgen_maxlen;
	pktgen.numsent = bus->pktgen_sent;
	pktgen.numrcvd = bus->pktgen_rcvd;
	pktgen.numfail = bus->pktgen_fail;
	pktgen.mode = bus->pktgen_mode;
	pktgen.stop = bus->pktgen_stop;

	memcpy(arg, &pktgen, sizeof(pktgen));

	return 0;
}

static int brcmf_sdbrcm_pktgen_set(dhd_bus_t *bus, u8 *arg)
{
	dhd_pktgen_t pktgen;
	uint oldcnt, oldmode;

	memcpy(&pktgen, arg, sizeof(pktgen));
	if (pktgen.version != DHD_PKTGEN_VERSION)
		return -EINVAL;

	oldcnt = bus->pktgen_count;
	oldmode = bus->pktgen_mode;

	bus->pktgen_freq = pktgen.freq;
	bus->pktgen_count = pktgen.count;
	bus->pktgen_print = pktgen.print;
	bus->pktgen_total = pktgen.total;
	bus->pktgen_minlen = pktgen.minlen;
	bus->pktgen_maxlen = pktgen.maxlen;
	bus->pktgen_mode = pktgen.mode;
	bus->pktgen_stop = pktgen.stop;

	bus->pktgen_tick = bus->pktgen_ptick = 0;
	bus->pktgen_len = max(bus->pktgen_len, bus->pktgen_minlen);
	bus->pktgen_len = min(bus->pktgen_len, bus->pktgen_maxlen);

	/* Clear counts for a new pktgen (mode change, or was stopped) */
	if (bus->pktgen_count && (!oldcnt || oldmode != bus->pktgen_mode))
		bus->pktgen_sent = bus->pktgen_rcvd = bus->pktgen_fail = 0;

	return 0;
}
#endif				/* SDTEST */

static int
brcmf_sdbrcm_membytes(dhd_bus_t *bus, bool write, u32 address, u8 *data,
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
	bcmerror = brcmf_sdbrcm_set_siaddr_window(bus, address);
	if (bcmerror) {
		DHD_ERROR(("%s: window change failed\n", __func__));
		goto xfer_done;
	}

	/* Do the transfer(s) */
	while (size) {
		DHD_INFO(("%s: %s %d bytes at offset 0x%08x in window 0x%08x\n",
			  __func__, (write ? "write" : "read"), dsize,
			  sdaddr, (address & SBSDIO_SBWINDOW_MASK)));
		bcmerror =
		     brcmf_sdcard_rwdata(bus->sdh, write, sdaddr, data, dsize);
		if (bcmerror) {
			DHD_ERROR(("%s: membytes transfer failed\n", __func__));
			break;
		}

		/* Adjust for next transfer (if any) */
		size -= dsize;
		if (size) {
			data += dsize;
			address += dsize;
			bcmerror = brcmf_sdbrcm_set_siaddr_window(bus, address);
			if (bcmerror) {
				DHD_ERROR(("%s: window change failed\n",
					   __func__));
				break;
			}
			sdaddr = 0;
			dsize = min_t(uint, SBSDIO_SB_OFT_ADDR_LIMIT, size);
		}
	}

xfer_done:
	/* Return the window to backplane enumeration space for core access */
	if (brcmf_sdbrcm_set_siaddr_window(bus,
					   brcmf_sdcard_cur_sbwad(bus->sdh))) {
		DHD_ERROR(("%s: FAILED to set window back to 0x%x\n",
			   __func__, brcmf_sdcard_cur_sbwad(bus->sdh)));
	}

	return bcmerror;
}

#ifdef DHD_DEBUG
static int brcmf_sdbrcm_readshared(dhd_bus_t *bus, struct sdpcm_shared *sh)
{
	u32 addr;
	int rv;

	/* Read last word in memory to determine address of
			 sdpcm_shared structure */
	rv = brcmf_sdbrcm_membytes(bus, false, bus->ramsize - 4, (u8 *)&addr,
				   4);
	if (rv < 0)
		return rv;

	addr = le32_to_cpu(addr);

	DHD_INFO(("sdpcm_shared address 0x%08X\n", addr));

	/*
	 * Check if addr is valid.
	 * NVRAM length at the end of memory should have been overwritten.
	 */
	if (addr == 0 || ((~addr >> 16) & 0xffff) == (addr & 0xffff)) {
		DHD_ERROR(("%s: address (0x%08x) of sdpcm_shared invalid\n",
			   __func__, addr));
		return -EBADE;
	}

	/* Read rte_shared structure */
	rv = brcmf_sdbrcm_membytes(bus, false, addr, (u8 *) sh,
			      sizeof(struct sdpcm_shared));
	if (rv < 0)
		return rv;

	/* Endianness */
	sh->flags = le32_to_cpu(sh->flags);
	sh->trap_addr = le32_to_cpu(sh->trap_addr);
	sh->assert_exp_addr = le32_to_cpu(sh->assert_exp_addr);
	sh->assert_file_addr = le32_to_cpu(sh->assert_file_addr);
	sh->assert_line = le32_to_cpu(sh->assert_line);
	sh->console_addr = le32_to_cpu(sh->console_addr);
	sh->msgtrace_addr = le32_to_cpu(sh->msgtrace_addr);

	if ((sh->flags & SDPCM_SHARED_VERSION_MASK) != SDPCM_SHARED_VERSION) {
		DHD_ERROR(("%s: sdpcm_shared version %d in dhd "
			   "is different than sdpcm_shared version %d in dongle\n",
			   __func__, SDPCM_SHARED_VERSION,
			   sh->flags & SDPCM_SHARED_VERSION_MASK));
		return -EBADE;
	}

	return 0;
}

static int brcmf_sdbrcm_checkdied(dhd_bus_t *bus, u8 *data, uint size)
{
	int bcmerror = 0;
	uint msize = 512;
	char *mbuffer = NULL;
	uint maxstrlen = 256;
	char *str = NULL;
	trap_t tr;
	struct sdpcm_shared sdpcm_shared;
	struct brcmu_strbuf strbuf;

	DHD_TRACE(("%s: Enter\n", __func__));

	if (data == NULL) {
		/*
		 * Called after a rx ctrl timeout. "data" is NULL.
		 * allocate memory to trace the trap or assert.
		 */
		size = msize;
		mbuffer = data = kmalloc(msize, GFP_ATOMIC);
		if (mbuffer == NULL) {
			DHD_ERROR(("%s: kmalloc(%d) failed\n", __func__,
				   msize));
			bcmerror = -ENOMEM;
			goto done;
		}
	}

	str = kmalloc(maxstrlen, GFP_ATOMIC);
	if (str == NULL) {
		DHD_ERROR(("%s: kmalloc(%d) failed\n", __func__, maxstrlen));
		bcmerror = -ENOMEM;
		goto done;
	}

	bcmerror = brcmf_sdbrcm_readshared(bus, &sdpcm_shared);
	if (bcmerror < 0)
		goto done;

	brcmu_binit(&strbuf, data, size);

	brcmu_bprintf(&strbuf,
		    "msgtrace address : 0x%08X\nconsole address  : 0x%08X\n",
		    sdpcm_shared.msgtrace_addr, sdpcm_shared.console_addr);

	if ((sdpcm_shared.flags & SDPCM_SHARED_ASSERT_BUILT) == 0) {
		/* NOTE: Misspelled assert is intentional - DO NOT FIX.
		 * (Avoids conflict with real asserts for programmatic
		 * parsing of output.)
		 */
		brcmu_bprintf(&strbuf, "Assrt not built in dongle\n");
	}

	if ((sdpcm_shared.flags & (SDPCM_SHARED_ASSERT | SDPCM_SHARED_TRAP)) ==
	    0) {
		/* NOTE: Misspelled assert is intentional - DO NOT FIX.
		 * (Avoids conflict with real asserts for programmatic
		 * parsing of output.)
		 */
		brcmu_bprintf(&strbuf, "No trap%s in dongle",
			    (sdpcm_shared.flags & SDPCM_SHARED_ASSERT_BUILT)
			    ? "/assrt" : "");
	} else {
		if (sdpcm_shared.flags & SDPCM_SHARED_ASSERT) {
			/* Download assert */
			brcmu_bprintf(&strbuf, "Dongle assert");
			if (sdpcm_shared.assert_exp_addr != 0) {
				str[0] = '\0';
				bcmerror = brcmf_sdbrcm_membytes(bus, false,
						sdpcm_shared.assert_exp_addr,
						(u8 *) str, maxstrlen);
				if (bcmerror < 0)
					goto done;

				str[maxstrlen - 1] = '\0';
				brcmu_bprintf(&strbuf, " expr \"%s\"", str);
			}

			if (sdpcm_shared.assert_file_addr != 0) {
				str[0] = '\0';
				bcmerror = brcmf_sdbrcm_membytes(bus, false,
						sdpcm_shared.assert_file_addr,
						(u8 *) str, maxstrlen);
				if (bcmerror < 0)
					goto done;

				str[maxstrlen - 1] = '\0';
				brcmu_bprintf(&strbuf, " file \"%s\"", str);
			}

			brcmu_bprintf(&strbuf, " line %d ",
				    sdpcm_shared.assert_line);
		}

		if (sdpcm_shared.flags & SDPCM_SHARED_TRAP) {
			bcmerror = brcmf_sdbrcm_membytes(bus, false,
					sdpcm_shared.trap_addr, (u8 *)&tr,
					sizeof(trap_t));
			if (bcmerror < 0)
				goto done;

			brcmu_bprintf(&strbuf,
				    "Dongle trap type 0x%x @ epc 0x%x, cpsr 0x%x, spsr 0x%x, sp 0x%x,"
				    "lp 0x%x, rpc 0x%x Trap offset 0x%x, "
				    "r0 0x%x, r1 0x%x, r2 0x%x, r3 0x%x, r4 0x%x, r5 0x%x, r6 0x%x, r7 0x%x\n",
				    tr.type, tr.epc, tr.cpsr, tr.spsr, tr.r13,
				    tr.r14, tr.pc, sdpcm_shared.trap_addr,
				    tr.r0, tr.r1, tr.r2, tr.r3, tr.r4, tr.r5,
				    tr.r6, tr.r7);
		}
	}

	if (sdpcm_shared.flags & (SDPCM_SHARED_ASSERT | SDPCM_SHARED_TRAP))
		DHD_ERROR(("%s: %s\n", __func__, strbuf.origbuf));

#ifdef DHD_DEBUG
	if (sdpcm_shared.flags & SDPCM_SHARED_TRAP) {
		/* Mem dump to a file on device */
		brcmf_sdbrcm_mem_dump(bus);
	}
#endif				/* DHD_DEBUG */

done:
	kfree(mbuffer);
	kfree(str);

	return bcmerror;
}

static int brcmf_sdbrcm_mem_dump(dhd_bus_t *bus)
{
	int ret = 0;
	int size;		/* Full mem size */
	int start = 0;		/* Start address */
	int read_size = 0;	/* Read size of each iteration */
	u8 *buf = NULL, *databuf = NULL;

	/* Get full mem size */
	size = bus->ramsize;
	buf = kmalloc(size, GFP_ATOMIC);
	if (!buf) {
		DHD_ERROR(("%s: Out of memory (%d bytes)\n", __func__, size));
		return -1;
	}

	/* Read mem content */
	printk(KERN_DEBUG "Dump dongle memory");
	databuf = buf;
	while (size) {
		read_size = min(MEMBLOCK, size);
		ret = brcmf_sdbrcm_membytes(bus, false, start, databuf,
					  read_size);
		if (ret) {
			DHD_ERROR(("%s: Error membytes %d\n", __func__, ret));
			kfree(buf);
			return -1;
		}
		printk(".");

		/* Decrement size and increment start address */
		size -= read_size;
		start += read_size;
		databuf += read_size;
	}
	printk(KERN_DEBUG "Done\n");

	/* free buf before return !!! */
	if (write_to_file(bus->dhd, buf, bus->ramsize)) {
		DHD_ERROR(("%s: Error writing to files\n", __func__));
		return -1;
	}

	/* buf free handled in write_to_file, not here */
	return 0;
}

#define CONSOLE_LINE_MAX	192

static int brcmf_sdbrcm_readconsole(dhd_bus_t *bus)
{
	dhd_console_t *c = &bus->console;
	u8 line[CONSOLE_LINE_MAX], ch;
	u32 n, idx, addr;
	int rv;

	/* Don't do anything until FWREADY updates console address */
	if (bus->console_addr == 0)
		return 0;

	/* Read console log struct */
	addr = bus->console_addr + offsetof(rte_cons_t, log);
	rv = brcmf_sdbrcm_membytes(bus, false, addr, (u8 *)&c->log,
				sizeof(c->log));
	if (rv < 0)
		return rv;

	/* Allocate console buffer (one time only) */
	if (c->buf == NULL) {
		c->bufsize = le32_to_cpu(c->log.buf_size);
		c->buf = kmalloc(c->bufsize, GFP_ATOMIC);
		if (c->buf == NULL)
			return -ENOMEM;
	}

	idx = le32_to_cpu(c->log.idx);

	/* Protect against corrupt value */
	if (idx > c->bufsize)
		return -EBADE;

	/* Skip reading the console buffer if the index pointer
	 has not moved */
	if (idx == c->last)
		return 0;

	/* Read the console buffer */
	addr = le32_to_cpu(c->log.buf);
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
			printk(KERN_DEBUG "CONSOLE: %s\n", line);
		}
	}
break2:

	return 0;
}
#endif				/* DHD_DEBUG */

int brcmf_sdbrcm_downloadvars(dhd_bus_t *bus, void *arg, int len)
{
	int bcmerror = 0;

	DHD_TRACE(("%s: Enter\n", __func__));

	/* Basic sanity checks */
	if (bus->dhd->up) {
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

static int
brcmf_sdbrcm_doiovar(dhd_bus_t *bus, const struct brcmu_iovar *vi, u32 actionid,
		const char *name, void *params, int plen, void *arg, int len,
		int val_size)
{
	int bcmerror = 0;
	s32 int_val = 0;
	bool bool_val = 0;

	DHD_TRACE(("%s: Enter, action %d name %s params %p plen %d arg %p "
		"len %d val_size %d\n",
		__func__, actionid, name, params, plen, arg, len, val_size));

	bcmerror = brcmu_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid));
	if (bcmerror != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		memcpy(&int_val, params, sizeof(int_val));

	bool_val = (int_val != 0) ? true : false;

	/* Some ioctls use the bus */
	dhd_os_sdlock(bus->dhd);

	/* Check if dongle is in reset. If so, only allow DEVRESET iovars */
	if (bus->dhd->dongle_reset && !(actionid == IOV_SVAL(IOV_DEVRESET) ||
					actionid == IOV_GVAL(IOV_DEVRESET))) {
		bcmerror = -EPERM;
		goto exit;
	}

	/* Handle sleep stuff before any clock mucking */
	if (vi->varid == IOV_SLEEP) {
		if (IOV_ISSET(actionid)) {
			bcmerror = brcmf_sdbrcm_bussleep(bus, bool_val);
		} else {
			int_val = (s32) bus->sleeping;
			memcpy(arg, &int_val, val_size);
		}
		goto exit;
	}

	/* Request clock to allow SDIO accesses */
	if (!bus->dhd->dongle_reset) {
		BUS_WAKE(bus);
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
	}

	switch (actionid) {
	case IOV_GVAL(IOV_INTR):
		int_val = (s32) bus->intr;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_INTR):
		bus->intr = bool_val;
		bus->intdis = false;
		if (bus->dhd->up) {
			if (bus->intr) {
				DHD_INTR(("%s: enable SDIO device interrupts\n",
					  __func__));
				brcmf_sdcard_intr_enable(bus->sdh);
			} else {
				DHD_INTR(("%s: disable SDIO interrupts\n",
					  __func__));
				brcmf_sdcard_intr_disable(bus->sdh);
			}
		}
		break;

	case IOV_GVAL(IOV_POLLRATE):
		int_val = (s32) bus->pollrate;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_POLLRATE):
		bus->pollrate = (uint) int_val;
		bus->poll = (bus->pollrate != 0);
		break;

	case IOV_GVAL(IOV_IDLETIME):
		int_val = bus->idletime;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_IDLETIME):
		if ((int_val < 0) && (int_val != DHD_IDLE_IMMEDIATE))
			bcmerror = -EINVAL;
		else
			bus->idletime = int_val;
		break;

	case IOV_GVAL(IOV_IDLECLOCK):
		int_val = (s32) bus->idleclock;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_IDLECLOCK):
		bus->idleclock = int_val;
		break;

	case IOV_GVAL(IOV_SD1IDLE):
		int_val = (s32) sd1idle;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_SD1IDLE):
		sd1idle = bool_val;
		break;

	case IOV_SVAL(IOV_MEMBYTES):
	case IOV_GVAL(IOV_MEMBYTES):
		{
			u32 address;
			uint size, dsize;
			u8 *data;

			bool set = (actionid == IOV_SVAL(IOV_MEMBYTES));

			ASSERT(plen >= 2 * sizeof(int));

			address = (u32) int_val;
			memcpy(&int_val, (char *)params + sizeof(int_val),
			       sizeof(int_val));
			size = (uint) int_val;

			/* Do some validation */
			dsize = set ? plen - (2 * sizeof(int)) : len;
			if (dsize < size) {
				DHD_ERROR(("%s: error on %s membytes, addr "
				"0x%08x size %d dsize %d\n",
				__func__, (set ? "set" : "get"),
				address, size, dsize));
				bcmerror = -EINVAL;
				break;
			}

			DHD_INFO(("%s: Request to %s %d bytes at address "
			"0x%08x\n",
			__func__, (set ? "write" : "read"), size, address));

			/* If we know about SOCRAM, check for a fit */
			if ((bus->orig_ramsize) &&
			    ((address > bus->orig_ramsize)
			     || (address + size > bus->orig_ramsize))) {
				DHD_ERROR(("%s: ramsize 0x%08x doesn't have %d "
				"bytes at 0x%08x\n",
				__func__, bus->orig_ramsize, size, address));
				bcmerror = -EINVAL;
				break;
			}

			/* Generate the actual data pointer */
			data =
			    set ? (u8 *) params +
			    2 * sizeof(int) : (u8 *) arg;

			/* Call to do the transfer */
			bcmerror = brcmf_sdbrcm_membytes(bus, set, address,
							 data, size);

			break;
		}

	case IOV_GVAL(IOV_MEMSIZE):
		int_val = (s32) bus->ramsize;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_GVAL(IOV_SDIOD_DRIVE):
		int_val = (s32) dhd_sdiod_drive_strength;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_SDIOD_DRIVE):
		dhd_sdiod_drive_strength = int_val;
		brcmf_sdbrcm_sdiod_drive_strength_init(bus,
					     dhd_sdiod_drive_strength);
		break;

	case IOV_SVAL(IOV_DOWNLOAD):
		bcmerror = brcmf_sdbrcm_download_state(bus, bool_val);
		break;

	case IOV_SVAL(IOV_VARS):
		bcmerror = brcmf_sdbrcm_downloadvars(bus, arg, len);
		break;

	case IOV_GVAL(IOV_READAHEAD):
		int_val = (s32) dhd_readahead;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_READAHEAD):
		if (bool_val && !dhd_readahead)
			bus->nextlen = 0;
		dhd_readahead = bool_val;
		break;

	case IOV_GVAL(IOV_SDRXCHAIN):
		int_val = (s32) bus->use_rxchain;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_SDRXCHAIN):
		if (bool_val && !bus->sd_rxchain)
			bcmerror = -ENOTSUPP;
		else
			bus->use_rxchain = bool_val;
		break;
	case IOV_GVAL(IOV_ALIGNCTL):
		int_val = (s32) dhd_alignctl;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_ALIGNCTL):
		dhd_alignctl = bool_val;
		break;

	case IOV_GVAL(IOV_SDALIGN):
		int_val = DHD_SDALIGN;
		memcpy(arg, &int_val, val_size);
		break;

#ifdef DHD_DEBUG
	case IOV_GVAL(IOV_VARS):
		if (bus->varsz < (uint) len)
			memcpy(arg, bus->vars, bus->varsz);
		else
			bcmerror = -EOVERFLOW;
		break;
#endif				/* DHD_DEBUG */

#ifdef DHD_DEBUG
	case IOV_GVAL(IOV_SDREG):
		{
			sdreg_t *sd_ptr;
			u32 addr, size;

			sd_ptr = (sdreg_t *) params;

			addr = (unsigned long)bus->regs + sd_ptr->offset;
			size = sd_ptr->func;
			int_val = (s32) brcmf_sdcard_reg_read(bus->sdh, addr,
							      size);
			if (brcmf_sdcard_regfail(bus->sdh))
				bcmerror = -EIO;
			memcpy(arg, &int_val, sizeof(s32));
			break;
		}

	case IOV_SVAL(IOV_SDREG):
		{
			sdreg_t *sd_ptr;
			u32 addr, size;

			sd_ptr = (sdreg_t *) params;

			addr = (unsigned long)bus->regs + sd_ptr->offset;
			size = sd_ptr->func;
			brcmf_sdcard_reg_write(bus->sdh, addr, size,
					       sd_ptr->value);
			if (brcmf_sdcard_regfail(bus->sdh))
				bcmerror = -EIO;
			break;
		}

		/* Same as above, but offset is not backplane
		 (not SDIO core) */
	case IOV_GVAL(IOV_SBREG):
		{
			sdreg_t sdreg;
			u32 addr, size;

			memcpy(&sdreg, params, sizeof(sdreg));

			addr = SI_ENUM_BASE + sdreg.offset;
			size = sdreg.func;
			int_val = (s32) brcmf_sdcard_reg_read(bus->sdh, addr,
							      size);
			if (brcmf_sdcard_regfail(bus->sdh))
				bcmerror = -EIO;
			memcpy(arg, &int_val, sizeof(s32));
			break;
		}

	case IOV_SVAL(IOV_SBREG):
		{
			sdreg_t sdreg;
			u32 addr, size;

			memcpy(&sdreg, params, sizeof(sdreg));

			addr = SI_ENUM_BASE + sdreg.offset;
			size = sdreg.func;
			brcmf_sdcard_reg_write(bus->sdh, addr, size,
					       sdreg.value);
			if (brcmf_sdcard_regfail(bus->sdh))
				bcmerror = -EIO;
			break;
		}

	case IOV_GVAL(IOV_SDCIS):
		{
			*(char *)arg = 0;

			strcat(arg, "\nFunc 0\n");
			brcmf_sdcard_cis_read(bus->sdh, 0x10,
					(u8 *) arg + strlen(arg),
					SBSDIO_CIS_SIZE_LIMIT);
			strcat(arg, "\nFunc 1\n");
			brcmf_sdcard_cis_read(bus->sdh, 0x11,
					(u8 *) arg + strlen(arg),
					SBSDIO_CIS_SIZE_LIMIT);
			strcat(arg, "\nFunc 2\n");
			brcmf_sdcard_cis_read(bus->sdh, 0x12,
					(u8 *) arg + strlen(arg),
					SBSDIO_CIS_SIZE_LIMIT);
			break;
		}

	case IOV_GVAL(IOV_FORCEEVEN):
		int_val = (s32) forcealign;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_FORCEEVEN):
		forcealign = bool_val;
		break;

	case IOV_GVAL(IOV_TXBOUND):
		int_val = (s32) dhd_txbound;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_TXBOUND):
		dhd_txbound = (uint) int_val;
		break;

	case IOV_GVAL(IOV_RXBOUND):
		int_val = (s32) dhd_rxbound;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_RXBOUND):
		dhd_rxbound = (uint) int_val;
		break;

	case IOV_GVAL(IOV_TXMINMAX):
		int_val = (s32) dhd_txminmax;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_TXMINMAX):
		dhd_txminmax = (uint) int_val;
		break;
#endif				/* DHD_DEBUG */

#ifdef SDTEST
	case IOV_GVAL(IOV_EXTLOOP):
		int_val = (s32) bus->ext_loop;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_EXTLOOP):
		bus->ext_loop = bool_val;
		break;

	case IOV_GVAL(IOV_PKTGEN):
		bcmerror = brcmf_sdbrcm_pktgen_get(bus, arg);
		break;

	case IOV_SVAL(IOV_PKTGEN):
		bcmerror = brcmf_sdbrcm_pktgen_set(bus, arg);
		break;
#endif				/* SDTEST */

	case IOV_SVAL(IOV_DEVRESET):
		DHD_TRACE(("%s: Called set IOV_DEVRESET=%d dongle_reset=%d "
			"busstate=%d\n",
			__func__, bool_val, bus->dhd->dongle_reset,
			bus->dhd->busstate));

		dhd_bus_devreset(bus->dhd, (u8) bool_val);

		break;

	case IOV_GVAL(IOV_DEVRESET):
		DHD_TRACE(("%s: Called get IOV_DEVRESET\n", __func__));

		/* Get its status */
		int_val = (bool) bus->dhd->dongle_reset;
		memcpy(arg, &int_val, val_size);

		break;

	default:
		bcmerror = -ENOTSUPP;
		break;
	}

exit:
	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
		bus->activity = false;
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, true);
	}

	dhd_os_sdunlock(bus->dhd);

	if (actionid == IOV_SVAL(IOV_DEVRESET) && bool_val == false)
		dhd_preinit_ioctls((dhd_pub_t *) bus->dhd);

	return bcmerror;
}

static int brcmf_sdbrcm_write_vars(dhd_bus_t *bus)
{
	int bcmerror = 0;
	u32 varsize;
	u32 varaddr;
	u8 *vbuffer;
	u32 varsizew;
#ifdef DHD_DEBUG
	char *nvram_ularray;
#endif				/* DHD_DEBUG */

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
#ifdef DHD_DEBUG
		/* Verify NVRAM bytes */
		DHD_INFO(("Compare NVRAM dl & ul; varsize=%d\n", varsize));
		nvram_ularray = kmalloc(varsize, GFP_ATOMIC);
		if (!nvram_ularray)
			return -ENOMEM;

		/* Upload image to verify downloaded contents. */
		memset(nvram_ularray, 0xaa, varsize);

		/* Read the vars list to temp buffer for comparison */
		bcmerror =
		    brcmf_sdbrcm_membytes(bus, false, varaddr, nvram_ularray,
				     varsize);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on reading %d nvram bytes at "
			"0x%08x\n", __func__, bcmerror, varsize, varaddr));
		}
		/* Compare the org NVRAM with the one read from RAM */
		if (memcmp(vbuffer, nvram_ularray, varsize)) {
			DHD_ERROR(("%s: Downloaded NVRAM image is corrupted.\n",
				   __func__));
		} else
			DHD_ERROR(("%s: Download/Upload/Compare of NVRAM ok.\n",
				__func__));

		kfree(nvram_ularray);
#endif				/* DHD_DEBUG */

		kfree(vbuffer);
	}

	/* adjust to the user specified RAM */
	DHD_INFO(("Physical memory size: %d, usable memory size: %d\n",
		  bus->orig_ramsize, bus->ramsize));
	DHD_INFO(("Vars are at %d, orig varsize is %d\n", varaddr, varsize));
	varsize = ((bus->orig_ramsize - 4) - varaddr);

	/*
	 * Determine the length token:
	 * Varsize, converted to words, in lower 16-bits, checksum
	 * in upper 16-bits.
	 */
	if (bcmerror) {
		varsizew = 0;
	} else {
		varsizew = varsize / 4;
		varsizew = (~varsizew << 16) | (varsizew & 0x0000FFFF);
		varsizew = cpu_to_le32(varsizew);
	}

	DHD_INFO(("New varsize is %d, length token=0x%08x\n", varsize,
		  varsizew));

	/* Write the length token to the last word */
	bcmerror = brcmf_sdbrcm_membytes(bus, true, (bus->orig_ramsize - 4),
				    (u8 *)&varsizew, 4);

	return bcmerror;
}

static int brcmf_sdbrcm_download_state(dhd_bus_t *bus, bool enter)
{
	uint retries;
	u32 regdata;
	int bcmerror = 0;

	/* To enter download state, disable ARM and reset SOCRAM.
	 * To exit download state, simply reset ARM (default is RAM boot).
	 */
	if (enter) {
		bus->alp_only = true;

		brcmf_sdbrcm_chip_disablecore(bus->sdh, bus->ci->armcorebase);

		brcmf_sdbrcm_chip_resetcore(bus->sdh, bus->ci->ramcorebase);

		/* Clear the top bit of memory */
		if (bus->ramsize) {
			u32 zeros = 0;
			brcmf_sdbrcm_membytes(bus, true, bus->ramsize - 4,
					 (u8 *)&zeros, 4);
		}
	} else {
		regdata = brcmf_sdcard_reg_read(bus->sdh,
			CORE_SB(bus->ci->ramcorebase, sbtmstatelow), 4);
		regdata &= (SBTML_RESET | SBTML_REJ_MASK |
			(SICF_CLOCK_EN << SBTML_SICF_SHIFT));
		if ((SICF_CLOCK_EN << SBTML_SICF_SHIFT) != regdata) {
			DHD_ERROR(("%s: SOCRAM core is down after reset?\n",
				   __func__));
			bcmerror = -EBADE;
			goto fail;
		}

		bcmerror = brcmf_sdbrcm_write_vars(bus);
		if (bcmerror) {
			DHD_ERROR(("%s: no vars written to RAM\n", __func__));
			bcmerror = 0;
		}

		W_SDREG(0xFFFFFFFF, &bus->regs->intstatus, retries);

		brcmf_sdbrcm_chip_resetcore(bus->sdh, bus->ci->armcorebase);

		/* Allow HT Clock now that the ARM is running. */
		bus->alp_only = false;

		bus->dhd->busstate = DHD_BUS_LOAD;
	}
fail:
	return bcmerror;
}

int
brcmf_sdbrcm_bus_iovar_op(dhd_pub_t *dhdp, const char *name,
			  void *params, int plen, void *arg, int len, bool set)
{
	dhd_bus_t *bus = dhdp->bus;
	const struct brcmu_iovar *vi = NULL;
	int bcmerror = 0;
	int val_size;
	u32 actionid;

	DHD_TRACE(("%s: Enter\n", __func__));

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get MUST have return space */
	ASSERT(set || (arg && len));

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !plen));

	/* Look up var locally; if not found pass to host driver */
	vi = brcmu_iovar_lookup(dhdsdio_iovars, name);
	if (vi == NULL) {
		dhd_os_sdlock(bus->dhd);

		BUS_WAKE(bus);

		/* Turn on clock in case SD command needs backplane */
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

		bcmerror = brcmf_sdcard_iovar_op(bus->sdh, name, params, plen,
						 arg, len, set);

		/* Similar check for blocksize change */
		if (set && strcmp(name, "sd_blocksize") == 0) {
			s32 fnum = 2;
			if (brcmf_sdcard_iovar_op
			    (bus->sdh, "sd_blocksize", &fnum, sizeof(s32),
			     &bus->blocksize, sizeof(s32),
			     false) != 0) {
				bus->blocksize = 0;
				DHD_ERROR(("%s: fail on %s get\n", __func__,
					   "sd_blocksize"));
			} else {
				DHD_INFO(("%s: noted %s update, value now %d\n",
					  __func__, "sd_blocksize",
					  bus->blocksize));
			}
		}
		bus->roundup = min(max_roundup, bus->blocksize);

		if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
			bus->activity = false;
			brcmf_sdbrcm_clkctl(bus, CLK_NONE, true);
		}

		dhd_os_sdunlock(bus->dhd);
		goto exit;
	}

	DHD_CTL(("%s: %s %s, len %d plen %d\n", __func__,
		 name, (set ? "set" : "get"), len, plen));

	/* set up 'params' pointer in case this is a set command so that
	 * the convenience int and bool code can be common to set and get
	 */
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		/* all other types are integer sized */
		val_size = sizeof(int);

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);
	bcmerror = brcmf_sdbrcm_doiovar(bus, vi, actionid, name, params, plen,
					arg, len, val_size);

exit:
	return bcmerror;
}

void brcmf_sdbrcm_bus_stop(struct dhd_bus *bus, bool enforce_mutex)
{
	u32 local_hostintmask;
	u8 saveclk;
	uint retries;
	int err;

	DHD_TRACE(("%s: Enter\n", __func__));

	if (enforce_mutex)
		dhd_os_sdlock(bus->dhd);

	BUS_WAKE(bus);

	/* Enable clock for device interrupts */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

	/* Disable and clear interrupts at the chip level also */
	W_SDREG(0, &bus->regs->hostintmask, retries);
	local_hostintmask = bus->hostintmask;
	bus->hostintmask = 0;

	/* Change our idea of bus state */
	bus->dhd->busstate = DHD_BUS_DOWN;

	/* Force clocks on backplane to be sure F2 interrupt propagates */
	saveclk = brcmf_sdcard_cfg_read(bus->sdh, SDIO_FUNC_1,
					SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1,
				       SBSDIO_FUNC1_CHIPCLKCSR,
				       (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err) {
		DHD_ERROR(("%s: Failed to force clock for F2: err %d\n",
			   __func__, err));
	}

	/* Turn off the bus (F2), free any pending packets */
	DHD_INTR(("%s: disable SDIO interrupts\n", __func__));
	brcmf_sdcard_intr_disable(bus->sdh);
	brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_0, SDIO_CCCR_IOEx,
			 SDIO_FUNC_ENABLE_1, NULL);

	/* Clear any pending interrupts now that F2 is disabled */
	W_SDREG(local_hostintmask, &bus->regs->intstatus, retries);

	/* Turn off the backplane clock (only) */
	brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);

	/* Clear the data packet queues */
	brcmu_pktq_flush(&bus->txq, true, NULL, NULL);

	/* Clear any held glomming stuff */
	if (bus->glomd)
		brcmu_pkt_buf_free_skb(bus->glomd);

	if (bus->glom)
		brcmu_pkt_buf_free_skb(bus->glom);

	bus->glom = bus->glomd = NULL;

	/* Clear rx control and wake any waiters */
	bus->rxlen = 0;
	dhd_os_ioctl_resp_wake(bus->dhd);

	/* Reset some F2 state stuff */
	bus->rxskip = false;
	bus->tx_seq = bus->rx_seq = 0;

	if (enforce_mutex)
		dhd_os_sdunlock(bus->dhd);
}

int brcmf_sdbrcm_bus_init(dhd_pub_t *dhdp, bool enforce_mutex)
{
	dhd_bus_t *bus = dhdp->bus;
	dhd_timeout_t tmo;
	uint retries = 0;
	u8 ready, enable;
	int err, ret = 0;
	u8 saveclk;

	DHD_TRACE(("%s: Enter\n", __func__));

	ASSERT(bus->dhd);
	if (!bus->dhd)
		return 0;

	if (enforce_mutex)
		dhd_os_sdlock(bus->dhd);

	/* Make sure backplane clock is on, needed to generate F2 interrupt */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
	if (bus->clkstate != CLK_AVAIL)
		goto exit;

	/* Force clocks on backplane to be sure F2 interrupt propagates */
	saveclk =
	    brcmf_sdcard_cfg_read(bus->sdh, SDIO_FUNC_1,
				  SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1,
				       SBSDIO_FUNC1_CHIPCLKCSR,
				       (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err) {
		DHD_ERROR(("%s: Failed to force clock for F2: err %d\n",
			   __func__, err));
		goto exit;
	}

	/* Enable function 2 (frame transfers) */
	W_SDREG((SDPCM_PROT_VERSION << SMB_DATA_VERSION_SHIFT),
		&bus->regs->tosbmailboxdata, retries);
	enable = (SDIO_FUNC_ENABLE_1 | SDIO_FUNC_ENABLE_2);

	brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_0, SDIO_CCCR_IOEx, enable,
			       NULL);

	/* Give the dongle some time to do its thing and set IOR2 */
	dhd_timeout_start(&tmo, DHD_WAIT_F2RDY * 1000);

	ready = 0;
	while (ready != enable && !dhd_timeout_expired(&tmo))
		ready =
		    brcmf_sdcard_cfg_read(bus->sdh, SDIO_FUNC_0, SDIO_CCCR_IORx,
				    NULL);

	DHD_INFO(("%s: enable 0x%02x, ready 0x%02x (waited %uus)\n",
		  __func__, enable, ready, tmo.elapsed));

	/* If F2 successfully enabled, set core and enable interrupts */
	if (ready == enable) {
		/* Set up the interrupt mask and enable interrupts */
		bus->hostintmask = HOSTINTMASK;
		W_SDREG(bus->hostintmask,
			(unsigned int *)CORE_BUS_REG(bus->ci->buscorebase,
			hostintmask), retries);

		brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_WATERMARK,
				 (u8) watermark, &err);

		/* Set bus state according to enable result */
		dhdp->busstate = DHD_BUS_DATA;

		/* bcmsdh_intr_unmask(bus->sdh); */

		bus->intdis = false;
		if (bus->intr) {
			DHD_INTR(("%s: enable SDIO device interrupts\n",
				  __func__));
			brcmf_sdcard_intr_enable(bus->sdh);
		} else {
			DHD_INTR(("%s: disable SDIO interrupts\n", __func__));
			brcmf_sdcard_intr_disable(bus->sdh);
		}

	}

	else {
		/* Disable F2 again */
		enable = SDIO_FUNC_ENABLE_1;
		brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_0, SDIO_CCCR_IOEx,
				       enable, NULL);
	}

	/* Restore previous clock setting */
	brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
			 saveclk, &err);

	/* If we didn't come up, turn off backplane clock */
	if (dhdp->busstate != DHD_BUS_DATA)
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);

exit:
	if (enforce_mutex)
		dhd_os_sdunlock(bus->dhd);

	return ret;
}

static void brcmf_sdbrcm_rxfail(dhd_bus_t *bus, bool abort, bool rtx)
{
	bcmsdh_info_t *sdh = bus->sdh;
	struct sdpcmd_regs *regs = bus->regs;
	uint retries = 0;
	u16 lastrbc;
	u8 hi, lo;
	int err;

	DHD_ERROR(("%s: %sterminate frame%s\n", __func__,
		   (abort ? "abort command, " : ""),
		   (rtx ? ", send NAK" : "")));

	if (abort)
		brcmf_sdcard_abort(sdh, SDIO_FUNC_2);

	brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_FRAMECTRL,
			       SFC_RF_TERM, &err);
	bus->f1regdata++;

	/* Wait until the packet has been flushed (device/FIFO stable) */
	for (lastrbc = retries = 0xffff; retries > 0; retries--) {
		hi = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
					   SBSDIO_FUNC1_RFRAMEBCHI, NULL);
		lo = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
					   SBSDIO_FUNC1_RFRAMEBCLO, NULL);
		bus->f1regdata += 2;

		if ((hi == 0) && (lo == 0))
			break;

		if ((hi > (lastrbc >> 8)) && (lo > (lastrbc & 0x00ff))) {
			DHD_ERROR(("%s: count growing: last 0x%04x now "
				"0x%04x\n",
				__func__, lastrbc, ((hi << 8) + lo)));
		}
		lastrbc = (hi << 8) + lo;
	}

	if (!retries) {
		DHD_ERROR(("%s: count never zeroed: last 0x%04x\n",
			   __func__, lastrbc));
	} else {
		DHD_INFO(("%s: flush took %d iterations\n", __func__,
			  (0xffff - retries)));
	}

	if (rtx) {
		bus->rxrtx++;
		W_SDREG(SMB_NAK, &regs->tosbmailbox, retries);
		bus->f1regdata++;
		if (retries <= retry_limit)
			bus->rxskip = true;
	}

	/* Clear partial in any case */
	bus->nextlen = 0;

	/* If we can't reach the device, signal failure */
	if (err || brcmf_sdcard_regfail(sdh))
		bus->dhd->busstate = DHD_BUS_DOWN;
}

static void
brcmf_sdbrcm_read_control(dhd_bus_t *bus, u8 *hdr, uint len, uint doff)
{
	bcmsdh_info_t *sdh = bus->sdh;
	uint rdlen, pad;

	int sdret;

	DHD_TRACE(("%s: Enter\n", __func__));

	/* Control data already received in aligned rxctl */
	if ((bus->bus == SPI_BUS) && (!bus->usebufpool))
		goto gotpkt;

	ASSERT(bus->rxbuf);
	/* Set rxctl for frame (w/optional alignment) */
	bus->rxctl = bus->rxbuf;
	if (dhd_alignctl) {
		bus->rxctl += firstread;
		pad = ((unsigned long)bus->rxctl % DHD_SDALIGN);
		if (pad)
			bus->rxctl += (DHD_SDALIGN - pad);
		bus->rxctl -= firstread;
	}
	ASSERT(bus->rxctl >= bus->rxbuf);

	/* Copy the already-read portion over */
	memcpy(bus->rxctl, hdr, firstread);
	if (len <= firstread)
		goto gotpkt;

	/* Copy the full data pkt in gSPI case and process ioctl. */
	if (bus->bus == SPI_BUS) {
		memcpy(bus->rxctl, hdr, len);
		goto gotpkt;
	}

	/* Raise rdlen to next SDIO block to avoid tail command */
	rdlen = len - firstread;
	if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
		pad = bus->blocksize - (rdlen % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
		    ((len + pad) < bus->dhd->maxctl))
			rdlen += pad;
	} else if (rdlen % DHD_SDALIGN) {
		rdlen += DHD_SDALIGN - (rdlen % DHD_SDALIGN);
	}

	/* Satisfy length-alignment requirements */
	if (forcealign && (rdlen & (ALIGNMENT - 1)))
		rdlen = roundup(rdlen, ALIGNMENT);

	/* Drop if the read is too big or it exceeds our maximum */
	if ((rdlen + firstread) > bus->dhd->maxctl) {
		DHD_ERROR(("%s: %d-byte control read exceeds %d-byte buffer\n",
			   __func__, rdlen, bus->dhd->maxctl));
		bus->dhd->rx_errors++;
		brcmf_sdbrcm_rxfail(bus, false, false);
		goto done;
	}

	if ((len - doff) > bus->dhd->maxctl) {
		DHD_ERROR(("%s: %d-byte ctl frame (%d-byte ctl data) exceeds "
			"%d-byte limit\n",
			__func__, len, (len - doff), bus->dhd->maxctl));
		bus->dhd->rx_errors++;
		bus->rx_toolong++;
		brcmf_sdbrcm_rxfail(bus, false, false);
		goto done;
	}

	/* Read remainder of frame body into the rxctl buffer */
	sdret = brcmf_sdcard_recv_buf(sdh, brcmf_sdcard_cur_sbwad(sdh),
				SDIO_FUNC_2,
				F2SYNC, (bus->rxctl + firstread), rdlen,
				NULL, NULL, NULL);
	bus->f2rxdata++;
	ASSERT(sdret != -BCME_PENDING);

	/* Control frame failures need retransmission */
	if (sdret < 0) {
		DHD_ERROR(("%s: read %d control bytes failed: %d\n",
			   __func__, rdlen, sdret));
		bus->rxc_errors++;	/* dhd.rx_ctlerrs is higher level */
		brcmf_sdbrcm_rxfail(bus, true, true);
		goto done;
	}

gotpkt:

#ifdef DHD_DEBUG
	if (DHD_BYTES_ON() && DHD_CTL_ON()) {
		printk(KERN_DEBUG "RxCtrl:\n");
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, bus->rxctl, len);
	}
#endif

	/* Point to valid data and indicate its length */
	bus->rxctl += doff;
	bus->rxlen = len - doff;

done:
	/* Awake any waiters */
	dhd_os_ioctl_resp_wake(bus->dhd);
}

static u8 brcmf_sdbrcm_rxglom(dhd_bus_t *bus, u8 rxseq)
{
	u16 dlen, totlen;
	u8 *dptr, num = 0;

	u16 sublen, check;
	struct sk_buff *pfirst, *plast, *pnext, *save_pfirst;

	int errcode;
	u8 chan, seq, doff, sfdoff;
	u8 txmax;

	int ifidx = 0;
	bool usechain = bus->use_rxchain;

	/* If packets, issue read(s) and send up packet chain */
	/* Return sequence numbers consumed? */

	DHD_TRACE(("brcmf_sdbrcm_rxglom: start: glomd %p glom %p\n", bus->glomd,
		   bus->glom));

	/* If there's a descriptor, generate the packet chain */
	if (bus->glomd) {
		pfirst = plast = pnext = NULL;
		dlen = (u16) (bus->glomd->len);
		dptr = bus->glomd->data;
		if (!dlen || (dlen & 1)) {
			DHD_ERROR(("%s: bad glomd len(%d), ignore descriptor\n",
			__func__, dlen));
			dlen = 0;
		}

		for (totlen = num = 0; dlen; num++) {
			/* Get (and move past) next length */
			sublen = get_unaligned_le16(dptr);
			dlen -= sizeof(u16);
			dptr += sizeof(u16);
			if ((sublen < SDPCM_HDRLEN) ||
			    ((num == 0) && (sublen < (2 * SDPCM_HDRLEN)))) {
				DHD_ERROR(("%s: descriptor len %d bad: %d\n",
					   __func__, num, sublen));
				pnext = NULL;
				break;
			}
			if (sublen % DHD_SDALIGN) {
				DHD_ERROR(("%s: sublen %d not multiple of %d\n",
				__func__, sublen, DHD_SDALIGN));
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
			pnext = brcmu_pkt_buf_get_skb(sublen + DHD_SDALIGN);
			if (pnext == NULL) {
				DHD_ERROR(("%s: bcm_pkt_buf_get_skb failed, "
					"num %d len %d\n", __func__,
					num, sublen));
				break;
			}
			ASSERT(!(pnext->prev));
			if (!pfirst) {
				ASSERT(!plast);
				pfirst = plast = pnext;
			} else {
				ASSERT(plast);
				plast->next = pnext;
				plast = pnext;
			}

			/* Adhere to start alignment requirements */
			PKTALIGN(pnext, sublen, DHD_SDALIGN);
		}

		/* If all allocations succeeded, save packet chain
			 in bus structure */
		if (pnext) {
			DHD_GLOM(("%s: allocated %d-byte packet chain for %d "
				"subframes\n", __func__, totlen, num));
			if (DHD_GLOM_ON() && bus->nextlen) {
				if (totlen != bus->nextlen) {
					DHD_GLOM(("%s: glomdesc mismatch: nextlen %d glomdesc %d " "rxseq %d\n",
						__func__, bus->nextlen,
						totlen, rxseq));
				}
			}
			bus->glom = pfirst;
			pfirst = pnext = NULL;
		} else {
			if (pfirst)
				brcmu_pkt_buf_free_skb(pfirst);
			bus->glom = NULL;
			num = 0;
		}

		/* Done with descriptor packet */
		brcmu_pkt_buf_free_skb(bus->glomd);
		bus->glomd = NULL;
		bus->nextlen = 0;
	}

	/* Ok -- either we just generated a packet chain,
		 or had one from before */
	if (bus->glom) {
		if (DHD_GLOM_ON()) {
			DHD_GLOM(("%s: try superframe read, packet chain:\n",
				__func__));
			for (pnext = bus->glom; pnext; pnext = pnext->next) {
				DHD_GLOM(("    %p: %p len 0x%04x (%d)\n",
					  pnext, (u8 *) (pnext->data),
					  pnext->len, pnext->len));
			}
		}

		pfirst = bus->glom;
		dlen = (u16) brcmu_pkttotlen(pfirst);

		/* Do an SDIO read for the superframe.  Configurable iovar to
		 * read directly into the chained packet, or allocate a large
		 * packet and and copy into the chain.
		 */
		if (usechain) {
			errcode = brcmf_sdcard_recv_buf(bus->sdh,
					brcmf_sdcard_cur_sbwad(bus->sdh),
					SDIO_FUNC_2,
					F2SYNC, (u8 *) pfirst->data, dlen,
					pfirst, NULL, NULL);
		} else if (bus->dataptr) {
			errcode = brcmf_sdcard_recv_buf(bus->sdh,
					brcmf_sdcard_cur_sbwad(bus->sdh),
					SDIO_FUNC_2,
					F2SYNC, bus->dataptr, dlen,
					NULL, NULL, NULL);
			sublen = (u16) brcmu_pktfrombuf(pfirst, 0, dlen,
						bus->dataptr);
			if (sublen != dlen) {
				DHD_ERROR(("%s: FAILED TO COPY, dlen %d sublen %d\n",
					__func__, dlen, sublen));
				errcode = -1;
			}
			pnext = NULL;
		} else {
			DHD_ERROR(("COULDN'T ALLOC %d-BYTE GLOM, FORCE FAILURE\n",
				dlen));
			errcode = -1;
		}
		bus->f2rxdata++;
		ASSERT(errcode != -BCME_PENDING);

		/* On failure, kill the superframe, allow a couple retries */
		if (errcode < 0) {
			DHD_ERROR(("%s: glom read of %d bytes failed: %d\n",
				   __func__, dlen, errcode));
			bus->dhd->rx_errors++;

			if (bus->glomerr++ < 3) {
				brcmf_sdbrcm_rxfail(bus, true, true);
			} else {
				bus->glomerr = 0;
				brcmf_sdbrcm_rxfail(bus, true, false);
				brcmu_pkt_buf_free_skb(bus->glom);
				bus->rxglomfail++;
				bus->glom = NULL;
			}
			return 0;
		}
#ifdef DHD_DEBUG
		if (DHD_GLOM_ON()) {
			printk(KERN_DEBUG "SUPERFRAME:\n");
			print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
				pfirst->data, min_t(int, pfirst->len, 48));
		}
#endif

		/* Validate the superframe header */
		dptr = (u8 *) (pfirst->data);
		sublen = get_unaligned_le16(dptr);
		check = get_unaligned_le16(dptr + sizeof(u16));

		chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
		bus->nextlen = dptr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
			DHD_INFO(("%s: nextlen too large (%d) seq %d\n",
				__func__, bus->nextlen, seq));
			bus->nextlen = 0;
		}
		doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

		errcode = 0;
		if ((u16)~(sublen ^ check)) {
			DHD_ERROR(("%s (superframe): HW hdr error: len/check "
				"0x%04x/0x%04x\n", __func__, sublen, check));
			errcode = -1;
		} else if (roundup(sublen, bus->blocksize) != dlen) {
			DHD_ERROR(("%s (superframe): len 0x%04x, rounded "
				"0x%04x, expect 0x%04x\n",
				__func__, sublen,
				roundup(sublen, bus->blocksize), dlen));
			errcode = -1;
		} else if (SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]) !=
			   SDPCM_GLOM_CHANNEL) {
			DHD_ERROR(("%s (superframe): bad channel %d\n",
				   __func__,
				   SDPCM_PACKET_CHANNEL(&dptr
							[SDPCM_FRAMETAG_LEN])));
			errcode = -1;
		} else if (SDPCM_GLOMDESC(&dptr[SDPCM_FRAMETAG_LEN])) {
			DHD_ERROR(("%s (superframe): got second descriptor?\n",
				   __func__));
			errcode = -1;
		} else if ((doff < SDPCM_HDRLEN) ||
			   (doff > (pfirst->len - SDPCM_HDRLEN))) {
			DHD_ERROR(("%s (superframe): Bad data offset %d: HW %d "
				"pkt %d min %d\n",
				__func__, doff, sublen,
				pfirst->len, SDPCM_HDRLEN));
			errcode = -1;
		}

		/* Check sequence number of superframe SW header */
		if (rxseq != seq) {
			DHD_INFO(("%s: (superframe) rx_seq %d, expected %d\n",
				  __func__, seq, rxseq));
			bus->rx_badseq++;
			rxseq = seq;
		}

		/* Check window for sanity */
		if ((u8) (txmax - bus->tx_seq) > 0x40) {
			DHD_ERROR(("%s: unlikely tx max %d with tx_seq %d\n",
				__func__, txmax, bus->tx_seq));
			txmax = bus->tx_seq + 2;
		}
		bus->tx_max = txmax;

		/* Remove superframe header, remember offset */
		skb_pull(pfirst, doff);
		sfdoff = doff;

		/* Validate all the subframe headers */
		for (num = 0, pnext = pfirst; pnext && !errcode;
		     num++, pnext = pnext->next) {
			dptr = (u8 *) (pnext->data);
			dlen = (u16) (pnext->len);
			sublen = get_unaligned_le16(dptr);
			check = get_unaligned_le16(dptr + sizeof(u16));
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);
#ifdef DHD_DEBUG
			if (DHD_GLOM_ON()) {
				printk(KERN_DEBUG "subframe:\n");
				print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
						     dptr, 32);
			}
#endif

			if ((u16)~(sublen ^ check)) {
				DHD_ERROR(("%s (subframe %d): HW hdr error: "
					   "len/check 0x%04x/0x%04x\n",
					   __func__, num, sublen, check));
				errcode = -1;
			} else if ((sublen > dlen) || (sublen < SDPCM_HDRLEN)) {
				DHD_ERROR(("%s (subframe %d): length mismatch: "
					   "len 0x%04x, expect 0x%04x\n",
					   __func__, num, sublen, dlen));
				errcode = -1;
			} else if ((chan != SDPCM_DATA_CHANNEL) &&
				   (chan != SDPCM_EVENT_CHANNEL)) {
				DHD_ERROR(("%s (subframe %d): bad channel %d\n",
					   __func__, num, chan));
				errcode = -1;
			} else if ((doff < SDPCM_HDRLEN) || (doff > sublen)) {
				DHD_ERROR(("%s (subframe %d): Bad data offset %d: HW %d min %d\n",
					__func__, num, doff, sublen,
					SDPCM_HDRLEN));
				errcode = -1;
			}
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
				brcmu_pkt_buf_free_skb(bus->glom);
				bus->rxglomfail++;
				bus->glom = NULL;
			}
			bus->nextlen = 0;
			return 0;
		}

		/* Basic SD framing looks ok - process each packet (header) */
		save_pfirst = pfirst;
		bus->glom = NULL;
		plast = NULL;

		for (num = 0; pfirst; rxseq++, pfirst = pnext) {
			pnext = pfirst->next;
			pfirst->next = NULL;

			dptr = (u8 *) (pfirst->data);
			sublen = get_unaligned_le16(dptr);
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

			DHD_GLOM(("%s: Get subframe %d, %p(%p/%d), sublen %d "
				"chan %d seq %d\n",
				__func__, num, pfirst, pfirst->data,
				pfirst->len, sublen, chan, seq));

			ASSERT((chan == SDPCM_DATA_CHANNEL)
			       || (chan == SDPCM_EVENT_CHANNEL));

			if (rxseq != seq) {
				DHD_GLOM(("%s: rx_seq %d, expected %d\n",
					  __func__, seq, rxseq));
				bus->rx_badseq++;
				rxseq = seq;
			}
#ifdef DHD_DEBUG
			if (DHD_BYTES_ON() && DHD_DATA_ON()) {
				printk(KERN_DEBUG "Rx Subframe Data:\n");
				print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
						     dptr, dlen);
			}
#endif

			__skb_trim(pfirst, sublen);
			skb_pull(pfirst, doff);

			if (pfirst->len == 0) {
				brcmu_pkt_buf_free_skb(pfirst);
				if (plast) {
					plast->next = pnext;
				} else {
					ASSERT(save_pfirst == pfirst);
					save_pfirst = pnext;
				}
				continue;
			} else if (dhd_prot_hdrpull(bus->dhd, &ifidx, pfirst) !=
				   0) {
				DHD_ERROR(("%s: rx protocol error\n",
					   __func__));
				bus->dhd->rx_errors++;
				brcmu_pkt_buf_free_skb(pfirst);
				if (plast) {
					plast->next = pnext;
				} else {
					ASSERT(save_pfirst == pfirst);
					save_pfirst = pnext;
				}
				continue;
			}

			/* this packet will go up, link back into
				 chain and count it */
			pfirst->next = pnext;
			plast = pfirst;
			num++;

#ifdef DHD_DEBUG
			if (DHD_GLOM_ON()) {
				DHD_GLOM(("%s subframe %d to stack, %p(%p/%d) "
				"nxt/lnk %p/%p\n",
				__func__, num, pfirst, pfirst->data,
				pfirst->len, pfirst->next,
				pfirst->prev));
				print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
						pfirst->data,
						min_t(int, pfirst->len, 32));
			}
#endif				/* DHD_DEBUG */
		}
		if (num) {
			dhd_os_sdunlock(bus->dhd);
			dhd_rx_frame(bus->dhd, ifidx, save_pfirst, num);
			dhd_os_sdlock(bus->dhd);
		}

		bus->rxglomframes++;
		bus->rxglompkts += num;
	}
	return num;
}

/* Return true if there may be more frames to read */
static uint
brcmf_sdbrcm_readframes(dhd_bus_t *bus, uint maxframes, bool *finished)
{
	bcmsdh_info_t *sdh = bus->sdh;

	u16 len, check;	/* Extracted hardware header fields */
	u8 chan, seq, doff;	/* Extracted software header fields */
	u8 fcbits;		/* Extracted fcbits from software header */

	struct sk_buff *pkt;		/* Packet for event or data frames */
	u16 pad;		/* Number of pad bytes to read */
	u16 rdlen;		/* Total number of bytes to read */
	u8 rxseq;		/* Next sequence number to expect */
	uint rxleft = 0;	/* Remaining number of frames allowed */
	int sdret;		/* Return code from bcmsdh calls */
	u8 txmax;		/* Maximum tx sequence offered */
	bool len_consistent;	/* Result of comparing readahead len and
					 len from hw-hdr */
	u8 *rxbuf;
	int ifidx = 0;
	uint rxcount = 0;	/* Total frames read */

#if defined(DHD_DEBUG) || defined(SDTEST)
	bool sdtest = false;	/* To limit message spew from test mode */
#endif

	DHD_TRACE(("%s: Enter\n", __func__));

	ASSERT(maxframes);

#ifdef SDTEST
	/* Allow pktgen to override maxframes */
	if (bus->pktgen_count && (bus->pktgen_mode == DHD_PKTGEN_RECV)) {
		maxframes = bus->pktgen_count;
		sdtest = true;
	}
#endif

	/* Not finished unless we encounter no more frames indication */
	*finished = false;

	for (rxseq = bus->rx_seq, rxleft = maxframes;
	     !bus->rxskip && rxleft && bus->dhd->busstate != DHD_BUS_DOWN;
	     rxseq++, rxleft--) {

		/* Handle glomming separately */
		if (bus->glom || bus->glomd) {
			u8 cnt;
			DHD_GLOM(("%s: calling rxglom: glomd %p, glom %p\n",
				  __func__, bus->glomd, bus->glom));
			cnt = brcmf_sdbrcm_rxglom(bus, rxseq);
			DHD_GLOM(("%s: rxglom returned %d\n", __func__, cnt));
			rxseq += cnt - 1;
			rxleft = (rxleft > cnt) ? (rxleft - cnt) : 1;
			continue;
		}

		/* Try doing single read if we can */
		if (dhd_readahead && bus->nextlen) {
			u16 nextlen = bus->nextlen;
			bus->nextlen = 0;

			if (bus->bus == SPI_BUS) {
				rdlen = len = nextlen;
			} else {
				rdlen = len = nextlen << 4;

				/* Pad read to blocksize for efficiency */
				if (bus->roundup && bus->blocksize
				    && (rdlen > bus->blocksize)) {
					pad =
					    bus->blocksize -
					    (rdlen % bus->blocksize);
					if ((pad <= bus->roundup)
					    && (pad < bus->blocksize)
					    && ((rdlen + pad + firstread) <
						MAX_RX_DATASZ))
						rdlen += pad;
				} else if (rdlen % DHD_SDALIGN) {
					rdlen +=
					    DHD_SDALIGN - (rdlen % DHD_SDALIGN);
				}
			}

			/* We use bus->rxctl buffer in WinXP for initial
			 * control pkt receives.
			 * Later we use buffer-poll for data as well
			 * as control packets.
			 * This is required because dhd receives full
			 * frame in gSPI unlike SDIO.
			 * After the frame is received we have to
			 * distinguish whether it is data
			 * or non-data frame.
			 */
			/* Allocate a packet buffer */
			pkt = brcmu_pkt_buf_get_skb(rdlen + DHD_SDALIGN);
			if (!pkt) {
				if (bus->bus == SPI_BUS) {
					bus->usebufpool = false;
					bus->rxctl = bus->rxbuf;
					if (dhd_alignctl) {
						bus->rxctl += firstread;
						pad = ((unsigned long)bus->rxctl %
						      DHD_SDALIGN);
						if (pad)
							bus->rxctl +=
							    (DHD_SDALIGN - pad);
						bus->rxctl -= firstread;
					}
					ASSERT(bus->rxctl >= bus->rxbuf);
					rxbuf = bus->rxctl;
					/* Read the entire frame */
					sdret = brcmf_sdcard_recv_buf(sdh,
						    brcmf_sdcard_cur_sbwad(sdh),
						    SDIO_FUNC_2, F2SYNC,
						    rxbuf, rdlen,
						    NULL, NULL, NULL);
					bus->f2rxdata++;
					ASSERT(sdret != -BCME_PENDING);

					/* Control frame failures need
					 retransmission */
					if (sdret < 0) {
						DHD_ERROR(("%s: read %d control bytes failed: %d\n",
							__func__,
							rdlen, sdret));
						/* dhd.rx_ctlerrs is higher */
						bus->rxc_errors++;
						brcmf_sdbrcm_rxfail(bus, true,
						       (bus->bus ==
							SPI_BUS) ? false
						       : true);
						continue;
					}
				} else {
					/* Give up on data,
					request rtx of events */
					DHD_ERROR(("%s (nextlen): "
						   "brcmu_pkt_buf_get_skb "
						   "failed:"
						   " len %d rdlen %d expected"
						   " rxseq %d\n", __func__,
						   len, rdlen, rxseq));
					continue;
				}
			} else {
				if (bus->bus == SPI_BUS)
					bus->usebufpool = true;

				ASSERT(!(pkt->prev));
				PKTALIGN(pkt, rdlen, DHD_SDALIGN);
				rxbuf = (u8 *) (pkt->data);
				/* Read the entire frame */
				sdret = brcmf_sdcard_recv_buf(sdh,
						brcmf_sdcard_cur_sbwad(sdh),
						SDIO_FUNC_2, F2SYNC,
						rxbuf, rdlen,
						pkt, NULL, NULL);
				bus->f2rxdata++;
				ASSERT(sdret != -BCME_PENDING);

				if (sdret < 0) {
					DHD_ERROR(("%s (nextlen): read %d bytes failed: %d\n",
						__func__, rdlen, sdret));
					brcmu_pkt_buf_free_skb(pkt);
					bus->dhd->rx_errors++;
					/* Force retry w/normal header read.
					 * Don't attempt NAK for
					 * gSPI
					 */
					brcmf_sdbrcm_rxfail(bus, true,
						       (bus->bus ==
							SPI_BUS) ? false :
						       true);
					continue;
				}
			}

			/* Now check the header */
			memcpy(bus->rxhdr, rxbuf, SDPCM_HDRLEN);

			/* Extract hardware header fields */
			len = get_unaligned_le16(bus->rxhdr);
			check = get_unaligned_le16(bus->rxhdr + sizeof(u16));

			/* All zeros means readahead info was bad */
			if (!(len | check)) {
				DHD_INFO(("%s (nextlen): read zeros in HW "
					"header???\n", __func__));
				brcmf_sdbrcm_pktfree2(bus, pkt);
				continue;
			}

			/* Validate check bytes */
			if ((u16)~(len ^ check)) {
				DHD_ERROR(("%s (nextlen): HW hdr error:"
					" nextlen/len/check"
					" 0x%04x/0x%04x/0x%04x\n",
					__func__, nextlen, len, check));
				bus->rx_badhdr++;
				brcmf_sdbrcm_rxfail(bus, false, false);
				brcmf_sdbrcm_pktfree2(bus, pkt);
				continue;
			}

			/* Validate frame length */
			if (len < SDPCM_HDRLEN) {
				DHD_ERROR(("%s (nextlen): HW hdr length "
					"invalid: %d\n", __func__, len));
				brcmf_sdbrcm_pktfree2(bus, pkt);
				continue;
			}

			/* Check for consistency withreadahead info */
			len_consistent = (nextlen != (roundup(len, 16) >> 4));
			if (len_consistent) {
				/* Mismatch, force retry w/normal
					header (may be >4K) */
				DHD_ERROR(("%s (nextlen): mismatch, "
					"nextlen %d len %d rnd %d; "
					"expected rxseq %d\n",
					__func__, nextlen,
					len, roundup(len, 16), rxseq));
				brcmf_sdbrcm_rxfail(bus, true,
						  bus->bus != SPI_BUS);
				brcmf_sdbrcm_pktfree2(bus, pkt);
				continue;
			}

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
				DHD_INFO(("%s (nextlen): got frame w/nextlen too large" " (%d), seq %d\n",
					__func__, bus->nextlen, seq));
				bus->nextlen = 0;
			}

			bus->dhd->rx_readahead_cnt++;

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
				DHD_INFO(("%s (nextlen): rx_seq %d, expected "
					"%d\n", __func__, seq, rxseq));
				bus->rx_badseq++;
				rxseq = seq;
			}

			/* Check window for sanity */
			if ((u8) (txmax - bus->tx_seq) > 0x40) {
				DHD_ERROR(("%s: got unlikely tx max %d with "
					"tx_seq %d\n",
					__func__, txmax, bus->tx_seq));
				txmax = bus->tx_seq + 2;
			}
			bus->tx_max = txmax;

#ifdef DHD_DEBUG
			if (DHD_BYTES_ON() && DHD_DATA_ON()) {
				printk(KERN_DEBUG "Rx Data:\n");
				print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
						     rxbuf, len);
			} else if (DHD_HDRS_ON()) {
				printk(KERN_DEBUG "RxHdr:\n");
				print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
						     bus->rxhdr, SDPCM_HDRLEN);
			}
#endif

			if (chan == SDPCM_CONTROL_CHANNEL) {
				if (bus->bus == SPI_BUS) {
					brcmf_sdbrcm_read_control(bus, rxbuf,
								  len, doff);
				} else {
					DHD_ERROR(("%s (nextlen): readahead on control" " packet %d?\n",
						__func__, seq));
					/* Force retry w/normal header read */
					bus->nextlen = 0;
					brcmf_sdbrcm_rxfail(bus, false, true);
				}
				brcmf_sdbrcm_pktfree2(bus, pkt);
				continue;
			}

			if ((bus->bus == SPI_BUS) && !bus->usebufpool) {
				DHD_ERROR(("Received %d bytes on %d channel. Running out of " "rx pktbuf's or not yet malloced.\n",
					len, chan));
				continue;
			}

			/* Validate data offset */
			if ((doff < SDPCM_HDRLEN) || (doff > len)) {
				DHD_ERROR(("%s (nextlen): bad data offset %d: HW len %d min %d\n",
					__func__, doff, len, SDPCM_HDRLEN));
				brcmf_sdbrcm_rxfail(bus, false, false);
				brcmf_sdbrcm_pktfree2(bus, pkt);
				continue;
			}

			/* All done with this one -- now deliver the packet */
			goto deliver;
		}
		/* gSPI frames should not be handled in fractions */
		if (bus->bus == SPI_BUS)
			break;

		/* Read frame header (hardware and software) */
		sdret = brcmf_sdcard_recv_buf(sdh, brcmf_sdcard_cur_sbwad(sdh),
				SDIO_FUNC_2, F2SYNC, bus->rxhdr, firstread,
				NULL, NULL, NULL);
		bus->f2rxhdrs++;
		ASSERT(sdret != -BCME_PENDING);

		if (sdret < 0) {
			DHD_ERROR(("%s: RXHEADER FAILED: %d\n", __func__,
				   sdret));
			bus->rx_hdrfail++;
			brcmf_sdbrcm_rxfail(bus, true, true);
			continue;
		}
#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() || DHD_HDRS_ON()) {
			printk(KERN_DEBUG "RxHdr:\n");
			print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
					     bus->rxhdr, SDPCM_HDRLEN);
		}
#endif

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
			DHD_ERROR(("%s: HW hdr err: len/check 0x%04x/0x%04x\n",
				__func__, len, check));
			bus->rx_badhdr++;
			brcmf_sdbrcm_rxfail(bus, false, false);
			continue;
		}

		/* Validate frame length */
		if (len < SDPCM_HDRLEN) {
			DHD_ERROR(("%s: HW hdr length invalid: %d\n",
				   __func__, len));
			continue;
		}

		/* Extract software header fields */
		chan = SDPCM_PACKET_CHANNEL(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		doff = SDPCM_DOFFSET_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

		/* Validate data offset */
		if ((doff < SDPCM_HDRLEN) || (doff > len)) {
			DHD_ERROR(("%s: Bad data offset %d: HW len %d, min %d "
				"seq %d\n",
				__func__, doff, len, SDPCM_HDRLEN, seq));
			bus->rx_badhdr++;
			ASSERT(0);
			brcmf_sdbrcm_rxfail(bus, false, false);
			continue;
		}

		/* Save the readahead length if there is one */
		bus->nextlen =
		    bus->rxhdr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
			DHD_INFO(("%s (nextlen): got frame w/nextlen too large "
				"(%d), seq %d\n",
				__func__, bus->nextlen, seq));
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
			DHD_INFO(("%s: rx_seq %d, expected %d\n", __func__,
				  seq, rxseq));
			bus->rx_badseq++;
			rxseq = seq;
		}

		/* Check window for sanity */
		if ((u8) (txmax - bus->tx_seq) > 0x40) {
			DHD_ERROR(("%s: unlikely tx max %d with tx_seq %d\n",
				__func__, txmax, bus->tx_seq));
			txmax = bus->tx_seq + 2;
		}
		bus->tx_max = txmax;

		/* Call a separate function for control frames */
		if (chan == SDPCM_CONTROL_CHANNEL) {
			brcmf_sdbrcm_read_control(bus, bus->rxhdr, len, doff);
			continue;
		}

		ASSERT((chan == SDPCM_DATA_CHANNEL)
		       || (chan == SDPCM_EVENT_CHANNEL)
		       || (chan == SDPCM_TEST_CHANNEL)
		       || (chan == SDPCM_GLOM_CHANNEL));

		/* Length to read */
		rdlen = (len > firstread) ? (len - firstread) : 0;

		/* May pad read to blocksize for efficiency */
		if (bus->roundup && bus->blocksize &&
			(rdlen > bus->blocksize)) {
			pad = bus->blocksize - (rdlen % bus->blocksize);
			if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
			    ((rdlen + pad + firstread) < MAX_RX_DATASZ))
				rdlen += pad;
		} else if (rdlen % DHD_SDALIGN) {
			rdlen += DHD_SDALIGN - (rdlen % DHD_SDALIGN);
		}

		/* Satisfy length-alignment requirements */
		if (forcealign && (rdlen & (ALIGNMENT - 1)))
			rdlen = roundup(rdlen, ALIGNMENT);

		if ((rdlen + firstread) > MAX_RX_DATASZ) {
			/* Too long -- skip this frame */
			DHD_ERROR(("%s: too long: len %d rdlen %d\n",
				   __func__, len, rdlen));
			bus->dhd->rx_errors++;
			bus->rx_toolong++;
			brcmf_sdbrcm_rxfail(bus, false, false);
			continue;
		}

		pkt = brcmu_pkt_buf_get_skb(rdlen + firstread + DHD_SDALIGN);
		if (!pkt) {
			/* Give up on data, request rtx of events */
			DHD_ERROR(("%s: brcmu_pkt_buf_get_skb failed: rdlen %d"
				   " chan %d\n", __func__, rdlen, chan));
			bus->dhd->rx_dropped++;
			brcmf_sdbrcm_rxfail(bus, false, RETRYCHAN(chan));
			continue;
		}

		ASSERT(!(pkt->prev));

		/* Leave room for what we already read, and align remainder */
		ASSERT(firstread < pkt->len);
		skb_pull(pkt, firstread);
		PKTALIGN(pkt, rdlen, DHD_SDALIGN);

		/* Read the remaining frame data */
		sdret = brcmf_sdcard_recv_buf(sdh, brcmf_sdcard_cur_sbwad(sdh),
				SDIO_FUNC_2, F2SYNC, ((u8 *) (pkt->data)),
				rdlen, pkt, NULL, NULL);
		bus->f2rxdata++;
		ASSERT(sdret != -BCME_PENDING);

		if (sdret < 0) {
			DHD_ERROR(("%s: read %d %s bytes failed: %d\n",
				   __func__, rdlen,
				   ((chan ==
				     SDPCM_EVENT_CHANNEL) ? "event" : ((chan ==
					SDPCM_DATA_CHANNEL)
				       ? "data" : "test")),
				   sdret));
			brcmu_pkt_buf_free_skb(pkt);
			bus->dhd->rx_errors++;
			brcmf_sdbrcm_rxfail(bus, true, RETRYCHAN(chan));
			continue;
		}

		/* Copy the already-read portion */
		skb_push(pkt, firstread);
		memcpy(pkt->data, bus->rxhdr, firstread);

#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() && DHD_DATA_ON()) {
			printk(KERN_DEBUG "Rx Data:\n");
			print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
					     pkt->data, len);
		}
#endif

deliver:
		/* Save superframe descriptor and allocate packet frame */
		if (chan == SDPCM_GLOM_CHANNEL) {
			if (SDPCM_GLOMDESC(&bus->rxhdr[SDPCM_FRAMETAG_LEN])) {
				DHD_GLOM(("%s: glom descriptor, %d bytes:\n",
					__func__, len));
#ifdef DHD_DEBUG
				if (DHD_GLOM_ON()) {
					printk(KERN_DEBUG "Glom Data:\n");
					print_hex_dump_bytes("",
							     DUMP_PREFIX_OFFSET,
							     pkt->data, len);
				}
#endif
				__skb_trim(pkt, len);
				ASSERT(doff == SDPCM_HDRLEN);
				skb_pull(pkt, SDPCM_HDRLEN);
				bus->glomd = pkt;
			} else {
				DHD_ERROR(("%s: glom superframe w/o "
					"descriptor!\n", __func__));
				brcmf_sdbrcm_rxfail(bus, false, false);
			}
			continue;
		}

		/* Fill in packet len and prio, deliver upward */
		__skb_trim(pkt, len);
		skb_pull(pkt, doff);

#ifdef SDTEST
		/* Test channel packets are processed separately */
		if (chan == SDPCM_TEST_CHANNEL) {
			brcmf_sdbrcm_checkdied(bus, pkt, seq);
			continue;
		}
#endif				/* SDTEST */

		if (pkt->len == 0) {
			brcmu_pkt_buf_free_skb(pkt);
			continue;
		} else if (dhd_prot_hdrpull(bus->dhd, &ifidx, pkt) != 0) {
			DHD_ERROR(("%s: rx protocol error\n", __func__));
			brcmu_pkt_buf_free_skb(pkt);
			bus->dhd->rx_errors++;
			continue;
		}

		/* Unlock during rx call */
		dhd_os_sdunlock(bus->dhd);
		dhd_rx_frame(bus->dhd, ifidx, pkt, 1);
		dhd_os_sdlock(bus->dhd);
	}
	rxcount = maxframes - rxleft;
#ifdef DHD_DEBUG
	/* Message if we hit the limit */
	if (!rxleft && !sdtest)
		DHD_DATA(("%s: hit rx limit of %d frames\n", __func__,
			  maxframes));
	else
#endif				/* DHD_DEBUG */
		DHD_DATA(("%s: processed %d frames\n", __func__, rxcount));
	/* Back off rxseq if awaiting rtx, update rx_seq */
	if (bus->rxskip)
		rxseq--;
	bus->rx_seq = rxseq;

	return rxcount;
}

static u32 brcmf_sdbrcm_hostmail(dhd_bus_t *bus)
{
	struct sdpcmd_regs *regs = bus->regs;
	u32 intstatus = 0;
	u32 hmb_data;
	u8 fcbits;
	uint retries = 0;

	DHD_TRACE(("%s: Enter\n", __func__));

	/* Read mailbox data and ack that we did so */
	R_SDREG(hmb_data, &regs->tohostmailboxdata, retries);
	if (retries <= retry_limit)
		W_SDREG(SMB_INT_ACK, &regs->tosbmailbox, retries);
	bus->f1regdata += 2;

	/* Dongle recomposed rx frames, accept them again */
	if (hmb_data & HMB_DATA_NAKHANDLED) {
		DHD_INFO(("Dongle reports NAK handled, expect rtx of %d\n",
			  bus->rx_seq));
		if (!bus->rxskip)
			DHD_ERROR(("%s: unexpected NAKHANDLED!\n", __func__));

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
			DHD_ERROR(("Version mismatch, dongle reports %d, "
				"expecting %d\n",
				bus->sdpcm_ver, SDPCM_PROT_VERSION));
		else
			DHD_INFO(("Dongle ready, protocol version %d\n",
				  bus->sdpcm_ver));
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
			 HMB_DATA_FCDATA_MASK | HMB_DATA_VERSION_MASK)) {
		DHD_ERROR(("Unknown mailbox data content: 0x%02x\n", hmb_data));
	}

	return intstatus;
}

bool brcmf_sdbrcm_dpc(dhd_bus_t *bus)
{
	bcmsdh_info_t *sdh = bus->sdh;
	struct sdpcmd_regs *regs = bus->regs;
	u32 intstatus, newstatus = 0;
	uint retries = 0;
	uint rxlimit = dhd_rxbound;	/* Rx frames to read before resched */
	uint txlimit = dhd_txbound;	/* Tx frames to send before resched */
	uint framecnt = 0;	/* Temporary counter of tx/rx frames */
	bool rxdone = true;	/* Flag for no more read data */
	bool resched = false;	/* Flag indicating resched wanted */

	DHD_TRACE(("%s: Enter\n", __func__));

	/* Start with leftover status bits */
	intstatus = bus->intstatus;

	dhd_os_sdlock(bus->dhd);

	/* If waiting for HTAVAIL, check status */
	if (bus->clkstate == CLK_PENDING) {
		int err;
		u8 clkctl, devctl = 0;

#ifdef DHD_DEBUG
		/* Check for inconsistent device control */
		devctl = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
					       SBSDIO_DEVICE_CTL, &err);
		if (err) {
			DHD_ERROR(("%s: error reading DEVCTL: %d\n",
				   __func__, err));
			bus->dhd->busstate = DHD_BUS_DOWN;
		} else {
			ASSERT(devctl & SBSDIO_DEVCTL_CA_INT_ONLY);
		}
#endif				/* DHD_DEBUG */

		/* Read CSR, if clock on switch to AVAIL, else ignore */
		clkctl = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
					       SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			DHD_ERROR(("%s: error reading CSR: %d\n", __func__,
				   err));
			bus->dhd->busstate = DHD_BUS_DOWN;
		}

		DHD_INFO(("DPC: PENDING, devctl 0x%02x clkctl 0x%02x\n", devctl,
			  clkctl));

		if (SBSDIO_HTAV(clkctl)) {
			devctl = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
						       SBSDIO_DEVICE_CTL, &err);
			if (err) {
				DHD_ERROR(("%s: error reading DEVCTL: %d\n",
					   __func__, err));
				bus->dhd->busstate = DHD_BUS_DOWN;
			}
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
				SBSDIO_DEVICE_CTL, devctl, &err);
			if (err) {
				DHD_ERROR(("%s: error writing DEVCTL: %d\n",
					   __func__, err));
				bus->dhd->busstate = DHD_BUS_DOWN;
			}
			bus->clkstate = CLK_AVAIL;
		} else {
			goto clkwait;
		}
	}

	BUS_WAKE(bus);

	/* Make sure backplane clock is on */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, true);
	if (bus->clkstate == CLK_PENDING)
		goto clkwait;

	/* Pending interrupt indicates new device status */
	if (bus->ipend) {
		bus->ipend = false;
		R_SDREG(newstatus, &regs->intstatus, retries);
		bus->f1regdata++;
		if (brcmf_sdcard_regfail(bus->sdh))
			newstatus = 0;
		newstatus &= bus->hostintmask;
		bus->fcstate = !!(newstatus & I_HMB_FC_STATE);
		if (newstatus) {
			W_SDREG(newstatus, &regs->intstatus, retries);
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
		W_SDREG(I_HMB_FC_CHANGE, &regs->intstatus, retries);
		R_SDREG(newstatus, &regs->intstatus, retries);
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
		DHD_ERROR(("Dongle reports WR_OOSYNC\n"));
		intstatus &= ~I_WR_OOSYNC;
	}

	if (intstatus & I_RD_OOSYNC) {
		DHD_ERROR(("Dongle reports RD_OOSYNC\n"));
		intstatus &= ~I_RD_OOSYNC;
	}

	if (intstatus & I_SBINT) {
		DHD_ERROR(("Dongle reports SBINT\n"));
		intstatus &= ~I_SBINT;
	}

	/* Would be active due to wake-wlan in gSPI */
	if (intstatus & I_CHIPACTIVE) {
		DHD_INFO(("Dongle reports CHIPACTIVE\n"));
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
#if defined(OOB_INTR_ONLY)
	brcmf_sdio_oob_intr_set(1);
#endif				/* (OOB_INTR_ONLY) */
	/* Re-enable interrupts to detect new device events (mailbox, rx frame)
	 * or clock availability.  (Allows tx loop to check ipend if desired.)
	 * (Unless register access seems hosed, as we may not be able to ACK...)
	 */
	if (bus->intr && bus->intdis && !brcmf_sdcard_regfail(sdh)) {
		DHD_INTR(("%s: enable SDIO interrupts, rxdone %d framecnt %d\n",
			  __func__, rxdone, framecnt));
		bus->intdis = false;
		brcmf_sdcard_intr_enable(sdh);
	}

	if (DATAOK(bus) && bus->ctrl_frame_stat &&
		(bus->clkstate == CLK_AVAIL)) {
		int ret, i;

		ret = brcmf_sdbrcm_send_buf(bus, brcmf_sdcard_cur_sbwad(sdh),
			SDIO_FUNC_2, F2SYNC, (u8 *) bus->ctrl_frame_buf,
			(u32) bus->ctrl_frame_len, NULL, NULL, NULL);
		ASSERT(ret != -BCME_PENDING);

		if (ret < 0) {
			/* On failure, abort the command and
				terminate the frame */
			DHD_INFO(("%s: sdio error %d, abort command and "
				"terminate frame.\n", __func__, ret));
			bus->tx_sderrs++;

			brcmf_sdcard_abort(sdh, SDIO_FUNC_2);

			brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1,
					 SBSDIO_FUNC1_FRAMECTRL, SFC_WF_TERM,
					 NULL);
			bus->f1regdata++;

			for (i = 0; i < 3; i++) {
				u8 hi, lo;
				hi = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
						     SBSDIO_FUNC1_WFRAMEBCHI,
						     NULL);
				lo = brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
						     SBSDIO_FUNC1_WFRAMEBCLO,
						     NULL);
				bus->f1regdata += 2;
				if ((hi == 0) && (lo == 0))
					break;
			}

		}
		if (ret == 0)
			bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

		DHD_INFO(("Return_dpc value is : %d\n", ret));
		bus->ctrl_frame_stat = false;
		dhd_wait_event_wakeup(bus->dhd);
	}
	/* Send queued frames (limit 1 if rx may still be pending) */
	else if ((bus->clkstate == CLK_AVAIL) && !bus->fcstate &&
		 brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol) && txlimit
		 && DATAOK(bus)) {
		framecnt = rxdone ? txlimit : min(txlimit, dhd_txminmax);
		framecnt = brcmf_sdbrcm_sendfromq(bus, framecnt);
		txlimit -= framecnt;
	}

	/* Resched if events or tx frames are pending,
		 else await next interrupt */
	/* On failed register access, all bets are off:
		 no resched or interrupts */
	if ((bus->dhd->busstate == DHD_BUS_DOWN) || brcmf_sdcard_regfail(sdh)) {
		DHD_ERROR(("%s: failed backplane access over SDIO, halting "
			"operation %d\n", __func__, brcmf_sdcard_regfail(sdh)));
		bus->dhd->busstate = DHD_BUS_DOWN;
		bus->intstatus = 0;
	} else if (bus->clkstate == CLK_PENDING) {
		DHD_INFO(("%s: rescheduled due to CLK_PENDING awaiting "
			"I_CHIPACTIVE interrupt\n", __func__));
		resched = true;
	} else if (bus->intstatus || bus->ipend ||
		(!bus->fcstate && brcmu_pktq_mlen(&bus->txq, ~bus->flowcontrol)
		 && DATAOK(bus)) || PKT_AVAILABLE()) {
		resched = true;
	}

	bus->dpc_sched = resched;

	/* If we're done for now, turn off clock request. */
	if ((bus->clkstate != CLK_PENDING)
	    && bus->idletime == DHD_IDLE_IMMEDIATE) {
		bus->activity = false;
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);
	}

	dhd_os_sdunlock(bus->dhd);

	return resched;
}

bool dhd_bus_dpc(struct dhd_bus *bus)
{
	bool resched;

	/* Call the DPC directly. */
	DHD_TRACE(("Calling brcmf_sdbrcm_dpc() from %s\n", __func__));
	resched = brcmf_sdbrcm_dpc(bus);

	return resched;
}

void brcmf_sdbrcm_isr(void *arg)
{
	dhd_bus_t *bus = (dhd_bus_t *) arg;
	bcmsdh_info_t *sdh;

	DHD_TRACE(("%s: Enter\n", __func__));

	if (!bus) {
		DHD_ERROR(("%s : bus is null pointer , exit\n", __func__));
		return;
	}
	sdh = bus->sdh;

	if (bus->dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n",
			   __func__));
		return;
	}
	/* Count the interrupt call */
	bus->intrcount++;
	bus->ipend = true;

	/* Shouldn't get this interrupt if we're sleeping? */
	if (bus->sleeping) {
		DHD_ERROR(("INTERRUPT WHILE SLEEPING??\n"));
		return;
	}

	/* Disable additional interrupts (is this needed now)? */
	if (bus->intr)
		DHD_INTR(("%s: disable SDIO interrupts\n", __func__));
	else
		DHD_ERROR(("brcmf_sdbrcm_isr() w/o interrupt configured!\n"));

	brcmf_sdcard_intr_disable(sdh);
	bus->intdis = true;

#if defined(SDIO_ISR_THREAD)
	DHD_TRACE(("Calling brcmf_sdbrcm_dpc() from %s\n", __func__));
	while (brcmf_sdbrcm_dpc(bus))
		;
#else
	bus->dpc_sched = true;
	dhd_sched_dpc(bus->dhd);
#endif

}

#ifdef SDTEST
static void brcmf_sdbrcm_pktgen_init(dhd_bus_t *bus)
{
	/* Default to specified length, or full range */
	if (dhd_pktgen_len) {
		bus->pktgen_maxlen = min(dhd_pktgen_len, MAX_PKTGEN_LEN);
		bus->pktgen_minlen = bus->pktgen_maxlen;
	} else {
		bus->pktgen_maxlen = MAX_PKTGEN_LEN;
		bus->pktgen_minlen = 0;
	}
	bus->pktgen_len = (u16) bus->pktgen_minlen;

	/* Default to per-watchdog burst with 10s print time */
	bus->pktgen_freq = 1;
	bus->pktgen_print = 10000 / dhd_watchdog_ms;
	bus->pktgen_count = (dhd_pktgen * dhd_watchdog_ms + 999) / 1000;

	/* Default to echo mode */
	bus->pktgen_mode = DHD_PKTGEN_ECHO;
	bus->pktgen_stop = 1;
}

static void brcmf_sdbrcm_pktgen(dhd_bus_t *bus)
{
	struct sk_buff *pkt;
	u8 *data;
	uint pktcount;
	uint fillbyte;
	u16 len;

	/* Display current count if appropriate */
	if (bus->pktgen_print && (++bus->pktgen_ptick >= bus->pktgen_print)) {
		bus->pktgen_ptick = 0;
		printk(KERN_DEBUG "%s: send attempts %d rcvd %d\n",
		       __func__, bus->pktgen_sent, bus->pktgen_rcvd);
	}

	/* For recv mode, just make sure dongle has started sending */
	if (bus->pktgen_mode == DHD_PKTGEN_RECV) {
		if (!bus->pktgen_rcvd)
			brcmf_sdbrcm_sdtest_set(bus, true);
		return;
	}

	/* Otherwise, generate or request the specified number of packets */
	for (pktcount = 0; pktcount < bus->pktgen_count; pktcount++) {
		/* Stop if total has been reached */
		if (bus->pktgen_total
		    && (bus->pktgen_sent >= bus->pktgen_total)) {
			bus->pktgen_count = 0;
			break;
		}

		/* Allocate an appropriate-sized packet */
		len = bus->pktgen_len;
		pkt = brcmu_pkt_buf_get_skb(
			(len + SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + DHD_SDALIGN),
			true);
		if (!pkt) {
			DHD_ERROR(("%s: brcmu_pkt_buf_get_skb failed!\n",
				   __func__));
			break;
		}
		PKTALIGN(pkt, (len + SDPCM_HDRLEN + SDPCM_TEST_HDRLEN),
			 DHD_SDALIGN);
		data = (u8 *) (pkt->data) + SDPCM_HDRLEN;

		/* Write test header cmd and extra based on mode */
		switch (bus->pktgen_mode) {
		case DHD_PKTGEN_ECHO:
			*data++ = SDPCM_TEST_ECHOREQ;
			*data++ = (u8) bus->pktgen_sent;
			break;

		case DHD_PKTGEN_SEND:
			*data++ = SDPCM_TEST_DISCARD;
			*data++ = (u8) bus->pktgen_sent;
			break;

		case DHD_PKTGEN_RXBURST:
			*data++ = SDPCM_TEST_BURST;
			*data++ = (u8) bus->pktgen_count;
			break;

		default:
			DHD_ERROR(("Unrecognized pktgen mode %d\n",
				   bus->pktgen_mode));
			brcmu_pkt_buf_free_skb(pkt, true);
			bus->pktgen_count = 0;
			return;
		}

		/* Write test header length field */
		*data++ = (len >> 0);
		*data++ = (len >> 8);

		/* Then fill in the remainder -- N/A for burst,
			 but who cares... */
		for (fillbyte = 0; fillbyte < len; fillbyte++)
			*data++ =
			    SDPCM_TEST_FILL(fillbyte, (u8) bus->pktgen_sent);

#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() && DHD_DATA_ON()) {
			data = (u8 *) (pkt->data) + SDPCM_HDRLEN;
			printk(KERN_DEBUG "brcmf_sdbrcm_pktgen: Tx Data:\n");
			print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, data,
					     pkt->len - SDPCM_HDRLEN);
		}
#endif

		/* Send it */
		if (brcmf_sdbrcm_txpkt(bus, pkt, SDPCM_TEST_CHANNEL, true)) {
			bus->pktgen_fail++;
			if (bus->pktgen_stop
			    && bus->pktgen_stop == bus->pktgen_fail)
				bus->pktgen_count = 0;
		}
		bus->pktgen_sent++;

		/* Bump length if not fixed, wrap at max */
		if (++bus->pktgen_len > bus->pktgen_maxlen)
			bus->pktgen_len = (u16) bus->pktgen_minlen;

		/* Special case for burst mode: just send one request! */
		if (bus->pktgen_mode == DHD_PKTGEN_RXBURST)
			break;
	}
}

static void brcmf_sdbrcm_sdtest_set(dhd_bus_t *bus, bool start)
{
	struct sk_buff *pkt;
	u8 *data;

	/* Allocate the packet */
	pkt = brcmu_pkt_buf_get_skb(SDPCM_HDRLEN + SDPCM_TEST_HDRLEN +
		DHD_SDALIGN, true);
	if (!pkt) {
		DHD_ERROR(("%s: brcmu_pkt_buf_get_skb failed!\n", __func__));
		return;
	}
	PKTALIGN(pkt, (SDPCM_HDRLEN + SDPCM_TEST_HDRLEN), DHD_SDALIGN);
	data = (u8 *) (pkt->data) + SDPCM_HDRLEN;

	/* Fill in the test header */
	*data++ = SDPCM_TEST_SEND;
	*data++ = start;
	*data++ = (bus->pktgen_maxlen >> 0);
	*data++ = (bus->pktgen_maxlen >> 8);

	/* Send it */
	if (brcmf_sdbrcm_txpkt(bus, pkt, SDPCM_TEST_CHANNEL, true))
		bus->pktgen_fail++;
}

static void
brcmf_sdbrcm_checkdied(dhd_bus_t *bus, struct sk_buff *pkt, uint seq)
{
	u8 *data;
	uint pktlen;

	u8 cmd;
	u8 extra;
	u16 len;
	u16 offset;

	/* Check for min length */
	pktlen = pkt->len;
	if (pktlen < SDPCM_TEST_HDRLEN) {
		DHD_ERROR(("brcmf_sdbrcm_checkdied: toss runt frame, pktlen "
			   "%d\n", pktlen));
		brcmu_pkt_buf_free_skb(pkt, false);
		return;
	}

	/* Extract header fields */
	data = pkt->data;
	cmd = *data++;
	extra = *data++;
	len = *data++;
	len += *data++ << 8;

	/* Check length for relevant commands */
	if (cmd == SDPCM_TEST_DISCARD || cmd == SDPCM_TEST_ECHOREQ
	    || cmd == SDPCM_TEST_ECHORSP) {
		if (pktlen != len + SDPCM_TEST_HDRLEN) {
			DHD_ERROR(("brcmf_sdbrcm_checkdied: frame length "
				"mismatch, pktlen %d seq %d" " cmd %d extra %d "
				"len %d\n",
				pktlen, seq, cmd, extra, len));
			brcmu_pkt_buf_free_skb(pkt, false);
			return;
		}
	}

	/* Process as per command */
	switch (cmd) {
	case SDPCM_TEST_ECHOREQ:
		/* Rx->Tx turnaround ok (even on NDIS w/current
			 implementation) */
		*(u8 *) (pkt->data) = SDPCM_TEST_ECHORSP;
		if (brcmf_sdbrcm_txpkt(bus, pkt, SDPCM_TEST_CHANNEL, true) == 0)
			bus->pktgen_sent++;
		else {
			bus->pktgen_fail++;
			brcmu_pkt_buf_free_skb(pkt, false);
		}
		bus->pktgen_rcvd++;
		break;

	case SDPCM_TEST_ECHORSP:
		if (bus->ext_loop) {
			brcmu_pkt_buf_free_skb(pkt, false);
			bus->pktgen_rcvd++;
			break;
		}

		for (offset = 0; offset < len; offset++, data++) {
			if (*data != SDPCM_TEST_FILL(offset, extra)) {
				DHD_ERROR(("brcmf_sdbrcm_checkdied: echo data "
					   "mismatch: " "offset %d (len %d) "
					   "expect 0x%02x rcvd 0x%02x\n",
					   offset, len,
					   SDPCM_TEST_FILL(offset, extra),
					   *data));
				break;
			}
		}
		brcmu_pkt_buf_free_skb(pkt, false);
		bus->pktgen_rcvd++;
		break;

	case SDPCM_TEST_DISCARD:
		brcmu_pkt_buf_free_skb(pkt, false);
		bus->pktgen_rcvd++;
		break;

	case SDPCM_TEST_BURST:
	case SDPCM_TEST_SEND:
	default:
		DHD_INFO(("brcmf_sdbrcm_checkdied: unsupported or unknown "
			"command, pktlen %d seq %d" " cmd %d extra %d len %d\n",
			pktlen, seq, cmd, extra, len));
		brcmu_pkt_buf_free_skb(pkt, false);
		break;
	}

	/* For recv mode, stop at limie (and tell dongle to stop sending) */
	if (bus->pktgen_mode == DHD_PKTGEN_RECV) {
		if (bus->pktgen_total
		    && (bus->pktgen_rcvd >= bus->pktgen_total)) {
			bus->pktgen_count = 0;
			brcmf_sdbrcm_sdtest_set(bus, false);
		}
	}
}
#endif				/* SDTEST */

extern bool brcmf_sdbrcm_bus_watchdog(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus;

	DHD_TIMER(("%s: Enter\n", __func__));

	bus = dhdp->bus;

	if (bus->dhd->dongle_reset)
		return false;

	/* Ignore the timer if simulating bus down */
	if (bus->sleeping)
		return false;

	dhd_os_sdlock(bus->dhd);

	/* Poll period: check device if appropriate. */
	if (bus->poll && (++bus->polltick >= bus->pollrate)) {
		u32 intstatus = 0;

		/* Reset poll tick */
		bus->polltick = 0;

		/* Check device if no interrupts */
		if (!bus->intr || (bus->intrcount == bus->lastintrs)) {

			if (!bus->dpc_sched) {
				u8 devpend;
				devpend = brcmf_sdcard_cfg_read(bus->sdh,
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
				if (bus->intr)
					brcmf_sdcard_intr_disable(bus->sdh);

				bus->dpc_sched = true;
				dhd_sched_dpc(bus->dhd);

			}
		}

		/* Update interrupt tracking */
		bus->lastintrs = bus->intrcount;
	}
#ifdef DHD_DEBUG
	/* Poll for console output periodically */
	if (dhdp->busstate == DHD_BUS_DATA && dhd_console_ms != 0) {
		bus->console.count += dhd_watchdog_ms;
		if (bus->console.count >= dhd_console_ms) {
			bus->console.count -= dhd_console_ms;
			/* Make sure backplane clock is on */
			brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
			if (brcmf_sdbrcm_readconsole(bus) < 0)
				dhd_console_ms = 0;	/* On error,
							 stop trying */
		}
	}
#endif				/* DHD_DEBUG */

#ifdef SDTEST
	/* Generate packets if configured */
	if (bus->pktgen_count && (++bus->pktgen_tick >= bus->pktgen_freq)) {
		/* Make sure backplane clock is on */
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
		bus->pktgen_tick = 0;
		brcmf_sdbrcm_pktgen(bus);
	}
#endif

	/* On idle timeout clear activity flag and/or turn off clock */
	if ((bus->idletime > 0) && (bus->clkstate == CLK_AVAIL)) {
		if (++bus->idlecount >= bus->idletime) {
			bus->idlecount = 0;
			if (bus->activity) {
				bus->activity = false;
				dhd_os_wd_timer(bus->dhd, dhd_watchdog_ms);
			} else {
				brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);
			}
		}
	}

	dhd_os_sdunlock(bus->dhd);

	return bus->ipend;
}

#ifdef DHD_DEBUG
extern int brcmf_sdbrcm_bus_console_in(dhd_pub_t *dhdp, unsigned char *msg,
				       uint msglen)
{
	dhd_bus_t *bus = dhdp->bus;
	u32 addr, val;
	int rv;
	struct sk_buff *pkt;

	/* Address could be zero if CONSOLE := 0 in dongle Makefile */
	if (bus->console_addr == 0)
		return -ENOTSUPP;

	/* Exclusive bus access */
	dhd_os_sdlock(bus->dhd);

	/* Don't allow input if dongle is in reset */
	if (bus->dhd->dongle_reset) {
		dhd_os_sdunlock(bus->dhd);
		return -EPERM;
	}

	/* Request clock to allow SDIO accesses */
	BUS_WAKE(bus);
	/* No pend allowed since txpkt is called later, ht clk has to be on */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

	/* Zero cbuf_index */
	addr = bus->console_addr + offsetof(rte_cons_t, cbuf_idx);
	val = cpu_to_le32(0);
	rv = brcmf_sdbrcm_membytes(bus, true, addr, (u8 *)&val, sizeof(val));
	if (rv < 0)
		goto done;

	/* Write message into cbuf */
	addr = bus->console_addr + offsetof(rte_cons_t, cbuf);
	rv = brcmf_sdbrcm_membytes(bus, true, addr, (u8 *)msg, msglen);
	if (rv < 0)
		goto done;

	/* Write length into vcons_in */
	addr = bus->console_addr + offsetof(rte_cons_t, vcons_in);
	val = cpu_to_le32(msglen);
	rv = brcmf_sdbrcm_membytes(bus, true, addr, (u8 *)&val, sizeof(val));
	if (rv < 0)
		goto done;

	/* Bump dongle by sending an empty event pkt.
	 * sdpcm_sendup (RX) checks for virtual console input.
	 */
	pkt = brcmu_pkt_buf_get_skb(4 + SDPCM_RESERVE);
	if ((pkt != NULL) && bus->clkstate == CLK_AVAIL)
		brcmf_sdbrcm_txpkt(bus, pkt, SDPCM_EVENT_CHANNEL, true);

done:
	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
		bus->activity = false;
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, true);
	}

	dhd_os_sdunlock(bus->dhd);

	return rv;
}
#endif				/* DHD_DEBUG */

static bool brcmf_sdbrcm_chipmatch(u16 chipid)
{
	if (chipid == BCM4325_CHIP_ID)
		return true;
	if (chipid == BCM4329_CHIP_ID)
		return true;
	if (chipid == BCM4319_CHIP_ID)
		return true;
	return false;
}

static void *brcmf_sdbrcm_probe(u16 venid, u16 devid, u16 bus_no,
			   u16 slot, u16 func, uint bustype, void *regsva,
			   void *sdh)
{
	int ret;
	dhd_bus_t *bus;

	/* Init global variables at run-time, not as part of the declaration.
	 * This is required to support init/de-init of the driver.
	 * Initialization
	 * of globals as part of the declaration results in non-deterministic
	 * behavior since the value of the globals may be different on the
	 * first time that the driver is initialized vs subsequent
	 * initializations.
	 */
	dhd_txbound = DHD_TXBOUND;
	dhd_rxbound = DHD_RXBOUND;
	dhd_alignctl = true;
	sd1idle = true;
	dhd_readahead = true;
	retrydata = false;
	dhd_dongle_memsize = 0;
	dhd_txminmax = DHD_TXMINMAX;

	forcealign = true;

	dhd_common_init();

	DHD_TRACE(("%s: Enter\n", __func__));
	DHD_INFO(("%s: venid 0x%04x devid 0x%04x\n", __func__, venid, devid));

	/* We make assumptions about address window mappings */
	ASSERT((unsigned long)regsva == SI_ENUM_BASE);

	/* BCMSDH passes venid and devid based on CIS parsing -- but
	 * low-power start
	 * means early parse could fail, so here we should get either an ID
	 * we recognize OR (-1) indicating we must request power first.
	 */
	/* Check the Vendor ID */
	switch (venid) {
	case 0x0000:
	case PCI_VENDOR_ID_BROADCOM:
		break;
	default:
		DHD_ERROR(("%s: unknown vendor: 0x%04x\n", __func__, venid));
		return NULL;
	}

	/* Check the Device ID and make sure it's one that we support */
	switch (devid) {
	case BCM4325_D11DUAL_ID:	/* 4325 802.11a/g id */
	case BCM4325_D11G_ID:	/* 4325 802.11g 2.4Ghz band id */
	case BCM4325_D11A_ID:	/* 4325 802.11a 5Ghz band id */
		DHD_INFO(("%s: found 4325 Dongle\n", __func__));
		break;
	case BCM4329_D11NDUAL_ID:	/* 4329 802.11n dualband device */
	case BCM4329_D11N2G_ID:	/* 4329 802.11n 2.4G device */
	case BCM4329_D11N5G_ID:	/* 4329 802.11n 5G device */
	case 0x4329:
		DHD_INFO(("%s: found 4329 Dongle\n", __func__));
		break;
	case BCM4319_D11N_ID:	/* 4319 802.11n id */
	case BCM4319_D11N2G_ID:	/* 4319 802.11n2g id */
	case BCM4319_D11N5G_ID:	/* 4319 802.11n5g id */
		DHD_INFO(("%s: found 4319 Dongle\n", __func__));
		break;
	case 0:
		DHD_INFO(("%s: allow device id 0, will check chip internals\n",
			  __func__));
		break;

	default:
		DHD_ERROR(("%s: skipping 0x%04x/0x%04x, not a dongle\n",
			   __func__, venid, devid));
		return NULL;
	}

	/* Allocate private bus interface state */
	bus = kzalloc(sizeof(dhd_bus_t), GFP_ATOMIC);
	if (!bus) {
		DHD_ERROR(("%s: kmalloc of dhd_bus_t failed\n", __func__));
		goto fail;
	}
	bus->sdh = sdh;
	bus->cl_devid = (u16) devid;
	bus->bus = DHD_BUS;
	bus->tx_seq = SDPCM_SEQUENCE_WRAP - 1;
	bus->usebufpool = false;	/* Use bufpool if allocated,
					 else use locally malloced rxbuf */

	/* attempt to attach to the dongle */
	if (!(brcmf_sdbrcm_probe_attach(bus, sdh, regsva, devid))) {
		DHD_ERROR(("%s: brcmf_sdbrcm_probe_attach failed\n", __func__));
		goto fail;
	}

	spin_lock_init(&bus->txqlock);

	/* Attach to the dhd/OS/network interface */
	bus->dhd = dhd_attach(bus, SDPCM_RESERVE);
	if (!bus->dhd) {
		DHD_ERROR(("%s: dhd_attach failed\n", __func__));
		goto fail;
	}

	/* Allocate buffers */
	if (!(brcmf_sdbrcm_probe_malloc(bus, sdh))) {
		DHD_ERROR(("%s: brcmf_sdbrcm_probe_malloc failed\n", __func__));
		goto fail;
	}

	if (!(brcmf_sdbrcm_probe_init(bus, sdh))) {
		DHD_ERROR(("%s: brcmf_sdbrcm_probe_init failed\n", __func__));
		goto fail;
	}

	/* Register interrupt callback, but mask it (not operational yet). */
	DHD_INTR(("%s: disable SDIO interrupts (not interested yet)\n",
		  __func__));
	brcmf_sdcard_intr_disable(sdh);
	ret = brcmf_sdcard_intr_reg(sdh, brcmf_sdbrcm_isr, bus);
	if (ret != 0) {
		DHD_ERROR(("%s: FAILED: bcmsdh_intr_reg returned %d\n",
			   __func__, ret));
		goto fail;
	}
	DHD_INTR(("%s: registered SDIO interrupt function ok\n", __func__));

	DHD_INFO(("%s: completed!!\n", __func__));

	/* if firmware path present try to download and bring up bus */
	ret = dhd_bus_start(bus->dhd);
	if (ret != 0) {
		if (ret == -ENOLINK) {
			DHD_ERROR(("%s: dongle is not responding\n", __func__));
			goto fail;
		}
	}
	/* Ok, have the per-port tell the stack we're open for business */
	if (dhd_net_attach(bus->dhd, 0) != 0) {
		DHD_ERROR(("%s: Net attach failed!!\n", __func__));
		goto fail;
	}

	return bus;

fail:
	brcmf_sdbrcm_release(bus);
	return NULL;
}

static bool
brcmf_sdbrcm_probe_attach(struct dhd_bus *bus, void *sdh, void *regsva,
			  u16 devid)
{
	u8 clkctl = 0;
	int err = 0;

	bus->alp_only = true;

	/* Return the window to backplane enumeration space for core access */
	if (brcmf_sdbrcm_set_siaddr_window(bus, SI_ENUM_BASE))
		DHD_ERROR(("%s: FAILED to return to SI_ENUM_BASE\n", __func__));

#ifdef DHD_DEBUG
	printk(KERN_DEBUG "F1 signature read @0x18000000=0x%4x\n",
	       brcmf_sdcard_reg_read(bus->sdh, SI_ENUM_BASE, 4));

#endif				/* DHD_DEBUG */

	/*
	 * Force PLL off until brcmf_sdbrcm_chip_attach()
	 * programs PLL control regs
	 */

	brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
			 DHD_INIT_CLKCTL1, &err);
	if (!err)
		clkctl =
		    brcmf_sdcard_cfg_read(sdh, SDIO_FUNC_1,
					  SBSDIO_FUNC1_CHIPCLKCSR, &err);

	if (err || ((clkctl & ~SBSDIO_AVBITS) != DHD_INIT_CLKCTL1)) {
		DHD_ERROR(("brcmf_sdbrcm_probe: ChipClkCSR access: err %d wrote"
			" 0x%02x read 0x%02x\n",
			err, DHD_INIT_CLKCTL1, clkctl));
		goto fail;
	}

	if (brcmf_sdbrcm_chip_attach(bus, regsva)) {
		DHD_ERROR(("%s: brcmf_sdbrcm_chip_attach failed!\n", __func__));
		goto fail;
	}

	brcmf_sdcard_chipinfo(sdh, bus->ci->chip, bus->ci->chiprev);

	if (!brcmf_sdbrcm_chipmatch((u16) bus->ci->chip)) {
		DHD_ERROR(("%s: unsupported chip: 0x%04x\n",
			   __func__, bus->ci->chip));
		goto fail;
	}

	brcmf_sdbrcm_sdiod_drive_strength_init(bus, dhd_sdiod_drive_strength);

	/* Get info on the ARM and SOCRAM cores... */
	if (!DHD_NOPMU(bus)) {
		bus->armrev = SBCOREREV(brcmf_sdcard_reg_read(bus->sdh,
			CORE_SB(bus->ci->armcorebase, sbidhigh), 4));
		bus->orig_ramsize = bus->ci->ramsize;
		if (!(bus->orig_ramsize)) {
			DHD_ERROR(("%s: failed to find SOCRAM memory!\n",
				   __func__));
			goto fail;
		}
		bus->ramsize = bus->orig_ramsize;
		if (dhd_dongle_memsize)
			brcmf_sdbrcm_setmemsize(bus, dhd_dongle_memsize);

		DHD_ERROR(("DHD: dongle ram size is set to %d(orig %d)\n",
			   bus->ramsize, bus->orig_ramsize));
	}

	bus->regs = (void *)bus->ci->buscorebase;

	/* Set core control so an SDIO reset does a backplane reset */
	OR_REG(&bus->regs->corecontrol, CC_BPRESEN);

	brcmu_pktq_init(&bus->txq, (PRIOMASK + 1), TXQLEN);

	/* Locate an appropriately-aligned portion of hdrbuf */
	bus->rxhdr = (u8 *) roundup((unsigned long)&bus->hdrbuf[0], DHD_SDALIGN);

	/* Set the poll and/or interrupt flags */
	bus->intr = (bool) dhd_intr;
	bus->poll = (bool) dhd_poll;
	if (bus->poll)
		bus->pollrate = 1;

	return true;

fail:
	return false;
}

static bool brcmf_sdbrcm_probe_malloc(dhd_bus_t *bus, void *sdh)
{
	DHD_TRACE(("%s: Enter\n", __func__));

	if (bus->dhd->maxctl) {
		bus->rxblen =
		    roundup((bus->dhd->maxctl + SDPCM_HDRLEN),
			    ALIGNMENT) + DHD_SDALIGN;
		bus->rxbuf = kmalloc(bus->rxblen, GFP_ATOMIC);
		if (!(bus->rxbuf)) {
			DHD_ERROR(("%s: kmalloc of %d-byte rxbuf failed\n",
				   __func__, bus->rxblen));
			goto fail;
		}
	}

	/* Allocate buffer to receive glomed packet */
	bus->databuf = kmalloc(MAX_DATA_BUF, GFP_ATOMIC);
	if (!(bus->databuf)) {
		DHD_ERROR(("%s: kmalloc of %d-byte databuf failed\n",
			   __func__, MAX_DATA_BUF));
		/* release rxbuf which was already located as above */
		if (!bus->rxblen)
			kfree(bus->rxbuf);
		goto fail;
	}

	/* Align the buffer */
	if ((unsigned long)bus->databuf % DHD_SDALIGN)
		bus->dataptr =
		    bus->databuf + (DHD_SDALIGN -
				    ((unsigned long)bus->databuf % DHD_SDALIGN));
	else
		bus->dataptr = bus->databuf;

	return true;

fail:
	return false;
}

static bool brcmf_sdbrcm_probe_init(dhd_bus_t *bus, void *sdh)
{
	s32 fnum;

	DHD_TRACE(("%s: Enter\n", __func__));

#ifdef SDTEST
	brcmf_sdbrcm_pktgen_init(bus);
#endif				/* SDTEST */

	/* Disable F2 to clear any intermediate frame state on the dongle */
	brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_0, SDIO_CCCR_IOEx,
			       SDIO_FUNC_ENABLE_1, NULL);

	bus->dhd->busstate = DHD_BUS_DOWN;
	bus->sleeping = false;
	bus->rxflow = false;
	bus->prev_rxlim_hit = 0;

	/* Done with backplane-dependent accesses, can drop clock... */
	brcmf_sdcard_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, 0,
			       NULL);

	/* ...and initialize clock/power states */
	bus->clkstate = CLK_SDONLY;
	bus->idletime = (s32) dhd_idletime;
	bus->idleclock = DHD_IDLE_ACTIVE;

	/* Query the F2 block size, set roundup accordingly */
	fnum = 2;
	if (brcmf_sdcard_iovar_op(sdh, "sd_blocksize", &fnum, sizeof(s32),
			    &bus->blocksize, sizeof(s32), false) != 0) {
		bus->blocksize = 0;
		DHD_ERROR(("%s: fail on %s get\n", __func__, "sd_blocksize"));
	} else {
		DHD_INFO(("%s: Initial value for %s is %d\n",
			  __func__, "sd_blocksize", bus->blocksize));
	}
	bus->roundup = min(max_roundup, bus->blocksize);

	/* Query if bus module supports packet chaining,
		 default to use if supported */
	if (brcmf_sdcard_iovar_op(sdh, "sd_rxchain", NULL, 0,
			    &bus->sd_rxchain, sizeof(s32),
			    false) != 0) {
		bus->sd_rxchain = false;
	} else {
		DHD_INFO(("%s: bus module (through bcmsdh API) %s chaining\n",
			  __func__,
			  (bus->sd_rxchain ? "supports" : "does not support")));
	}
	bus->use_rxchain = (bool) bus->sd_rxchain;

	return true;
}

bool
dhd_bus_download_firmware(struct dhd_bus *bus, char *fw_path, char *nv_path)
{
	bool ret;
	bus->fw_path = fw_path;
	bus->nv_path = nv_path;

	ret = brcmf_sdbrcm_download_firmware(bus, bus->sdh);

	return ret;
}

static bool
brcmf_sdbrcm_download_firmware(struct dhd_bus *bus, void *sdh)
{
	bool ret;

	/* Download the firmware */
	brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);

	ret = _brcmf_sdbrcm_download_firmware(bus) == 0;

	brcmf_sdbrcm_clkctl(bus, CLK_SDONLY, false);

	return ret;
}

/* Detach and free everything */
static void brcmf_sdbrcm_release(dhd_bus_t *bus)
{
	DHD_TRACE(("%s: Enter\n", __func__));

	if (bus) {
		/* De-register interrupt handler */
		brcmf_sdcard_intr_disable(bus->sdh);
		brcmf_sdcard_intr_dereg(bus->sdh);

		if (bus->dhd) {
			dhd_detach(bus->dhd);
			brcmf_sdbrcm_release_dongle(bus);
			bus->dhd = NULL;
		}

		brcmf_sdbrcm_release_malloc(bus);

		kfree(bus);
	}

	DHD_TRACE(("%s: Disconnected\n", __func__));
}

static void brcmf_sdbrcm_release_malloc(dhd_bus_t *bus)
{
	DHD_TRACE(("%s: Enter\n", __func__));

	if (bus->dhd && bus->dhd->dongle_reset)
		return;

	kfree(bus->rxbuf);
	bus->rxctl = bus->rxbuf = NULL;
	bus->rxlen = 0;

	kfree(bus->databuf);
	bus->databuf = NULL;
}

static void brcmf_sdbrcm_release_dongle(dhd_bus_t *bus)
{
	DHD_TRACE(("%s: Enter\n", __func__));

	if (bus->dhd && bus->dhd->dongle_reset)
		return;

	if (bus->ci) {
		brcmf_sdbrcm_clkctl(bus, CLK_AVAIL, false);
		brcmf_sdbrcm_clkctl(bus, CLK_NONE, false);
		brcmf_sdbrcm_chip_detach(bus);
		if (bus->vars && bus->varsz)
			kfree(bus->vars);
		bus->vars = NULL;
	}

	DHD_TRACE(("%s: Disconnected\n", __func__));
}

static void brcmf_sdbrcm_disconnect(void *ptr)
{
	dhd_bus_t *bus = (dhd_bus_t *)ptr;

	DHD_TRACE(("%s: Enter\n", __func__));

	if (bus) {
		ASSERT(bus->dhd);
		brcmf_sdbrcm_release(bus);
	}

	DHD_TRACE(("%s: Disconnected\n", __func__));
}

/* Register/Unregister functions are called by the main DHD entry
 * point (e.g. module insertion) to link with the bus driver, in
 * order to look for or await the device.
 */

static bcmsdh_driver_t dhd_sdio = {
	brcmf_sdbrcm_probe,
	brcmf_sdbrcm_disconnect
};

int dhd_bus_register(void)
{
	DHD_TRACE(("%s: Enter\n", __func__));

	return brcmf_sdio_register(&dhd_sdio);
}

void dhd_bus_unregister(void)
{
	DHD_TRACE(("%s: Enter\n", __func__));

	brcmf_sdio_unregister();
}

static int brcmf_sdbrcm_download_code_file(struct dhd_bus *bus, char *fw_path)
{
	int bcmerror = -1;
	int offset = 0;
	uint len;
	void *image = NULL;
	u8 *memblock = NULL, *memptr;

	DHD_INFO(("%s: download firmware %s\n", __func__, fw_path));

	image = dhd_os_open_image(fw_path);
	if (image == NULL)
		goto err;

	memptr = memblock = kmalloc(MEMBLOCK + DHD_SDALIGN, GFP_ATOMIC);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n",
			   __func__, MEMBLOCK));
		goto err;
	}
	if ((u32)(unsigned long)memblock % DHD_SDALIGN)
		memptr +=
		    (DHD_SDALIGN - ((u32)(unsigned long)memblock % DHD_SDALIGN));

	/* Download image */
	while ((len =
		dhd_os_get_image_block((char *)memptr, MEMBLOCK, image))) {
		bcmerror = brcmf_sdbrcm_membytes(bus, true, offset, memptr,
						 len);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at "
			"0x%08x\n", __func__, bcmerror, MEMBLOCK, offset));
			goto err;
		}

		offset += MEMBLOCK;
	}

err:
	kfree(memblock);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
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

/*
	EXAMPLE: nvram_array
	nvram_arry format:
	name=value
	Use carriage return at the end of each assignment,
	 and an empty string with
	carriage return at the end of array.

	For example:
	unsigned char  nvram_array[] = {"name1=value1\n",
	"name2=value2\n", "\n"};
	Hex values start with 0x, and mac addr format: xx:xx:xx:xx:xx:xx.

	Search "EXAMPLE: nvram_array" to see how the array is activated.
*/

void dhd_bus_set_nvram_params(struct dhd_bus *bus, const char *nvram_params)
{
	bus->nvram_params = nvram_params;
}

static int brcmf_sdbrcm_download_nvram(struct dhd_bus *bus)
{
	int bcmerror = -1;
	uint len;
	void *image = NULL;
	char *memblock = NULL;
	char *bufp;
	char *nv_path;
	bool nvram_file_exists;

	nv_path = bus->nv_path;

	nvram_file_exists = ((nv_path != NULL) && (nv_path[0] != '\0'));
	if (!nvram_file_exists && (bus->nvram_params == NULL))
		return 0;

	if (nvram_file_exists) {
		image = dhd_os_open_image(nv_path);
		if (image == NULL)
			goto err;
	}

	memblock = kmalloc(MEMBLOCK, GFP_ATOMIC);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n",
			   __func__, MEMBLOCK));
		goto err;
	}

	/* Download variables */
	if (nvram_file_exists) {
		len = dhd_os_get_image_block(memblock, MEMBLOCK, image);
	} else {
		len = strlen(bus->nvram_params);
		ASSERT(len <= MEMBLOCK);
		if (len > MEMBLOCK)
			len = MEMBLOCK;
		memcpy(memblock, bus->nvram_params, len);
	}

	if (len > 0 && len < MEMBLOCK) {
		bufp = (char *)memblock;
		bufp[len] = 0;
		len = brcmf_process_nvram_vars(bufp, len);
		bufp += len;
		*bufp++ = 0;
		if (len)
			bcmerror = brcmf_sdbrcm_downloadvars(bus, memblock,
							   len + 1);
		if (bcmerror) {
			DHD_ERROR(("%s: error downloading vars: %d\n",
				   __func__, bcmerror));
		}
	} else {
		DHD_ERROR(("%s: error reading nvram file: %d\n",
			   __func__, len));
		bcmerror = -EIO;
	}

err:
	kfree(memblock);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
}

static int _brcmf_sdbrcm_download_firmware(struct dhd_bus *bus)
{
	int bcmerror = -1;

	bool embed = false;	/* download embedded firmware */
	bool dlok = false;	/* download firmware succeeded */

	/* Out immediately if no image to download */
	if ((bus->fw_path == NULL) || (bus->fw_path[0] == '\0'))
		return bcmerror;

	/* Keep arm in reset */
	if (brcmf_sdbrcm_download_state(bus, true)) {
		DHD_ERROR(("%s: error placing ARM core in reset\n", __func__));
		goto err;
	}

	/* External image takes precedence if specified */
	if ((bus->fw_path != NULL) && (bus->fw_path[0] != '\0')) {
		if (brcmf_sdbrcm_download_code_file(bus, bus->fw_path)) {
			DHD_ERROR(("%s: dongle image file download failed\n",
				   __func__));
			goto err;
		} else {
			embed = false;
			dlok = true;
		}
	}
	if (!dlok) {
		DHD_ERROR(("%s: dongle image download failed\n", __func__));
		goto err;
	}

	/* EXAMPLE: nvram_array */
	/* If a valid nvram_arry is specified as above, it can be passed
		 down to dongle */
	/* dhd_bus_set_nvram_params(bus, (char *)&nvram_array); */

	/* External nvram takes precedence if specified */
	if (brcmf_sdbrcm_download_nvram(bus)) {
		DHD_ERROR(("%s: dongle nvram file download failed\n",
			   __func__));
	}

	/* Take arm out of reset */
	if (brcmf_sdbrcm_download_state(bus, false)) {
		DHD_ERROR(("%s: error getting out of ARM core reset\n",
			   __func__));
		goto err;
	}

	bcmerror = 0;

err:
	return bcmerror;
}


static int
brcmf_sdbrcm_send_buf(dhd_bus_t *bus, u32 addr, uint fn, uint flags,
		    u8 *buf, uint nbytes, struct sk_buff *pkt,
		    bcmsdh_cmplt_fn_t complete, void *handle)
{
	return brcmf_sdcard_send_buf
		(bus->sdh, addr, fn, flags, buf, nbytes, pkt, complete,
		 handle);
}

uint dhd_bus_chip(struct dhd_bus *bus)
{
	ASSERT(bus->ci != NULL);
	return bus->ci->chip;
}

void *dhd_bus_pub(struct dhd_bus *bus)
{
	return bus->dhd;
}

void *dhd_bus_txq(struct dhd_bus *bus)
{
	return &bus->txq;
}

uint dhd_bus_hdrlen(struct dhd_bus *bus)
{
	return SDPCM_HDRLEN;
}

int dhd_bus_devreset(dhd_pub_t *dhdp, u8 flag)
{
	int bcmerror = 0;
	dhd_bus_t *bus;

	bus = dhdp->bus;

	if (flag == true) {
		if (!bus->dhd->dongle_reset) {
			/* Expect app to have torn down any
			 connection before calling */
			/* Stop the bus, disable F2 */
			brcmf_sdbrcm_bus_stop(bus, false);

			/* Clean tx/rx buffer pointers,
			 detach from the dongle */
			brcmf_sdbrcm_release_dongle(bus);

			bus->dhd->dongle_reset = true;
			bus->dhd->up = false;

			DHD_TRACE(("%s:  WLAN OFF DONE\n", __func__));
			/* App can now remove power from device */
		} else
			bcmerror = -EIO;
	} else {
		/* App must have restored power to device before calling */

		DHD_TRACE(("\n\n%s: == WLAN ON ==\n", __func__));

		if (bus->dhd->dongle_reset) {
			/* Turn on WLAN */
			/* Reset SD client */
			brcmf_sdcard_reset(bus->sdh);

			/* Attempt to re-attach & download */
			if (brcmf_sdbrcm_probe_attach(bus, bus->sdh,
						 (u32 *) SI_ENUM_BASE,
						 bus->cl_devid)) {
				/* Attempt to download binary to the dongle */
				if (brcmf_sdbrcm_probe_init
				    (bus, bus->sdh)
				    && brcmf_sdbrcm_download_firmware(bus,
								 bus->sdh)) {

					/* Re-init bus, enable F2 transfer */
					brcmf_sdbrcm_bus_init(
						(dhd_pub_t *) bus->dhd, false);

#if defined(OOB_INTR_ONLY)
					brcmf_sdbrcm_enable_oob_intr(bus, true);
#endif				/* defined(OOB_INTR_ONLY) */

					bus->dhd->dongle_reset = false;
					bus->dhd->up = true;

					DHD_TRACE(("%s: WLAN ON DONE\n",
						   __func__));
				} else
					bcmerror = -EIO;
			} else
				bcmerror = -EIO;
		} else {
			bcmerror = -EISCONN;
			DHD_ERROR(("%s: Set DEVRESET=false invoked when device "
				"is on\n", __func__));
			bcmerror = -EIO;
		}
	}
	return bcmerror;
}

static int
brcmf_sdbrcm_chip_recognition(bcmsdh_info_t *sdh, struct chip_info *ci,
			    void *regs)
{
	u32 regdata;

	/*
	 * Get CC core rev
	 * Chipid is assume to be at offset 0 from regs arg
	 * For different chiptypes or old sdio hosts w/o chipcommon,
	 * other ways of recognition should be added here.
	 */
	ci->cccorebase = (u32)regs;
	regdata = brcmf_sdcard_reg_read(sdh,
				CORE_CC_REG(ci->cccorebase, chipid), 4);
	ci->chip = regdata & CID_ID_MASK;
	ci->chiprev = (regdata & CID_REV_MASK) >> CID_REV_SHIFT;

	DHD_INFO(("%s: chipid=0x%x chiprev=%d\n",
		__func__, ci->chip, ci->chiprev));

	/* Address of cores for new chips should be added here */
	switch (ci->chip) {
	case BCM4329_CHIP_ID:
		ci->buscorebase = BCM4329_CORE_BUS_BASE;
		ci->ramcorebase = BCM4329_CORE_SOCRAM_BASE;
		ci->armcorebase	= BCM4329_CORE_ARM_BASE;
		ci->ramsize = BCM4329_RAMSIZE;
		break;
	default:
		DHD_ERROR(("%s: chipid 0x%x is not supported\n",
			__func__, ci->chip));
		return -ENODEV;
	}

	regdata = brcmf_sdcard_reg_read(sdh,
		CORE_SB(ci->cccorebase, sbidhigh), 4);
	ci->ccrev = SBCOREREV(regdata);

	regdata = brcmf_sdcard_reg_read(sdh,
		CORE_CC_REG(ci->cccorebase, pmucapabilities), 4);
	ci->pmurev = regdata & PCAP_REV_MASK;

	regdata = brcmf_sdcard_reg_read(sdh,
					CORE_SB(ci->buscorebase, sbidhigh), 4);
	ci->buscorerev = SBCOREREV(regdata);
	ci->buscoretype = (regdata & SBIDH_CC_MASK) >> SBIDH_CC_SHIFT;

	DHD_INFO(("%s: ccrev=%d, pmurev=%d, buscore rev/type=%d/0x%x\n",
		__func__, ci->ccrev, ci->pmurev,
		ci->buscorerev, ci->buscoretype));

	/* get chipcommon capabilites */
	ci->cccaps = brcmf_sdcard_reg_read(sdh,
		CORE_CC_REG(ci->cccorebase, capabilities), 4);

	return 0;
}

static void
brcmf_sdbrcm_chip_disablecore(bcmsdh_info_t *sdh, u32 corebase)
{
	u32 regdata;

	regdata = brcmf_sdcard_reg_read(sdh,
		CORE_SB(corebase, sbtmstatelow), 4);
	if (regdata & SBTML_RESET)
		return;

	regdata = brcmf_sdcard_reg_read(sdh,
		CORE_SB(corebase, sbtmstatelow), 4);
	if ((regdata & (SICF_CLOCK_EN << SBTML_SICF_SHIFT)) != 0) {
		/*
		 * set target reject and spin until busy is clear
		 * (preserve core-specific bits)
		 */
		regdata = brcmf_sdcard_reg_read(sdh,
			CORE_SB(corebase, sbtmstatelow), 4);
		brcmf_sdcard_reg_write(sdh, CORE_SB(corebase, sbtmstatelow), 4,
			regdata | SBTML_REJ);

		regdata = brcmf_sdcard_reg_read(sdh,
			CORE_SB(corebase, sbtmstatelow), 4);
		udelay(1);
		SPINWAIT((brcmf_sdcard_reg_read(sdh,
			CORE_SB(corebase, sbtmstatehigh), 4) &
			SBTMH_BUSY), 100000);

		regdata = brcmf_sdcard_reg_read(sdh,
			CORE_SB(corebase, sbtmstatehigh), 4);
		if (regdata & SBTMH_BUSY)
			DHD_ERROR(("%s: ARM core still busy\n", __func__));

		regdata = brcmf_sdcard_reg_read(sdh,
			CORE_SB(corebase, sbidlow), 4);
		if (regdata & SBIDL_INIT) {
			regdata = brcmf_sdcard_reg_read(sdh,
				CORE_SB(corebase, sbimstate), 4) |
				SBIM_RJ;
			brcmf_sdcard_reg_write(sdh,
				CORE_SB(corebase, sbimstate), 4,
				regdata);
			regdata = brcmf_sdcard_reg_read(sdh,
				CORE_SB(corebase, sbimstate), 4);
			udelay(1);
			SPINWAIT((brcmf_sdcard_reg_read(sdh,
				CORE_SB(corebase, sbimstate), 4) &
				SBIM_BY), 100000);
		}

		/* set reset and reject while enabling the clocks */
		brcmf_sdcard_reg_write(sdh,
			CORE_SB(corebase, sbtmstatelow), 4,
			(((SICF_FGC | SICF_CLOCK_EN) << SBTML_SICF_SHIFT) |
			SBTML_REJ | SBTML_RESET));
		regdata = brcmf_sdcard_reg_read(sdh,
			CORE_SB(corebase, sbtmstatelow), 4);
		udelay(10);

		/* clear the initiator reject bit */
		regdata = brcmf_sdcard_reg_read(sdh,
			CORE_SB(corebase, sbidlow), 4);
		if (regdata & SBIDL_INIT) {
			regdata = brcmf_sdcard_reg_read(sdh,
				CORE_SB(corebase, sbimstate), 4) &
				~SBIM_RJ;
			brcmf_sdcard_reg_write(sdh,
				CORE_SB(corebase, sbimstate), 4,
				regdata);
		}
	}

	/* leave reset and reject asserted */
	brcmf_sdcard_reg_write(sdh, CORE_SB(corebase, sbtmstatelow), 4,
		(SBTML_REJ | SBTML_RESET));
	udelay(1);
}

static int
brcmf_sdbrcm_chip_attach(struct dhd_bus *bus, void *regs)
{
	struct chip_info *ci;
	int err;
	u8 clkval, clkset;

	DHD_TRACE(("%s: Enter\n", __func__));

	/* alloc chip_info_t */
	ci = kmalloc(sizeof(struct chip_info), GFP_ATOMIC);
	if (NULL == ci) {
		DHD_ERROR(("%s: malloc failed!\n", __func__));
		return -ENOMEM;
	}

	memset((unsigned char *)ci, 0, sizeof(struct chip_info));

	/* bus/core/clk setup for register access */
	/* Try forcing SDIO core to do ALPAvail request only */
	clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ;
	brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
			clkset, &err);
	if (err) {
		DHD_ERROR(("%s: error writing for HT off\n", __func__));
		goto fail;
	}

	/* If register supported, wait for ALPAvail and then force ALP */
	/* This may take up to 15 milliseconds */
	clkval = brcmf_sdcard_cfg_read(bus->sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, NULL);
	if ((clkval & ~SBSDIO_AVBITS) == clkset) {
		SPINWAIT(((clkval =
				brcmf_sdcard_cfg_read(bus->sdh, SDIO_FUNC_1,
						SBSDIO_FUNC1_CHIPCLKCSR,
						NULL)),
				!SBSDIO_ALPAV(clkval)),
				PMU_MAX_TRANSITION_DLY);
		if (!SBSDIO_ALPAV(clkval)) {
			DHD_ERROR(("%s: timeout on ALPAV wait, clkval 0x%02x\n",
				__func__, clkval));
			err = -EBUSY;
			goto fail;
		}
		clkset = SBSDIO_FORCE_HW_CLKREQ_OFF |
				SBSDIO_FORCE_ALP;
		brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1,
				SBSDIO_FUNC1_CHIPCLKCSR,
				clkset, &err);
		udelay(65);
	} else {
		DHD_ERROR(("%s: ChipClkCSR access: wrote 0x%02x read 0x%02x\n",
			__func__, clkset, clkval));
		err = -EACCES;
		goto fail;
	}

	/* Also, disable the extra SDIO pull-ups */
	brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SDIOPULLUP,
			       0, NULL);

	err = brcmf_sdbrcm_chip_recognition(bus->sdh, ci, regs);
	if (err)
		goto fail;

	/*
	 * Make sure any on-chip ARM is off (in case strapping is wrong),
	 * or downloaded code was already running.
	 */
	brcmf_sdbrcm_chip_disablecore(bus->sdh, ci->armcorebase);

	brcmf_sdcard_reg_write(bus->sdh,
		CORE_CC_REG(ci->cccorebase, gpiopullup), 4, 0);
	brcmf_sdcard_reg_write(bus->sdh,
		CORE_CC_REG(ci->cccorebase, gpiopulldown), 4, 0);

	/* Disable F2 to clear any intermediate frame state on the dongle */
	brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_0, SDIO_CCCR_IOEx,
		SDIO_FUNC_ENABLE_1, NULL);

	/* WAR: cmd52 backplane read so core HW will drop ALPReq */
	clkval = brcmf_sdcard_cfg_read(bus->sdh, SDIO_FUNC_1,
			0, NULL);

	/* Done with backplane-dependent accesses, can drop clock... */
	brcmf_sdcard_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
			       0, NULL);

	bus->ci = ci;
	return 0;
fail:
	bus->ci = NULL;
	kfree(ci);
	return err;
}

static void
brcmf_sdbrcm_chip_resetcore(bcmsdh_info_t *sdh, u32 corebase)
{
	u32 regdata;

	/*
	 * Must do the disable sequence first to work for
	 * arbitrary current core state.
	 */
	brcmf_sdbrcm_chip_disablecore(sdh, corebase);

	/*
	 * Now do the initialization sequence.
	 * set reset while enabling the clock and
	 * forcing them on throughout the core
	 */
	brcmf_sdcard_reg_write(sdh, CORE_SB(corebase, sbtmstatelow), 4,
		((SICF_FGC | SICF_CLOCK_EN) << SBTML_SICF_SHIFT) |
		SBTML_RESET);
	udelay(1);

	regdata = brcmf_sdcard_reg_read(sdh, CORE_SB(corebase, sbtmstatehigh),
					4);
	if (regdata & SBTMH_SERR)
		brcmf_sdcard_reg_write(sdh, CORE_SB(corebase, sbtmstatehigh),
				       4, 0);

	regdata = brcmf_sdcard_reg_read(sdh, CORE_SB(corebase, sbimstate), 4);
	if (regdata & (SBIM_IBE | SBIM_TO))
		brcmf_sdcard_reg_write(sdh, CORE_SB(corebase, sbimstate), 4,
			regdata & ~(SBIM_IBE | SBIM_TO));

	/* clear reset and allow it to propagate throughout the core */
	brcmf_sdcard_reg_write(sdh, CORE_SB(corebase, sbtmstatelow), 4,
		(SICF_FGC << SBTML_SICF_SHIFT) |
		(SICF_CLOCK_EN << SBTML_SICF_SHIFT));
	udelay(1);

	/* leave clock enabled */
	brcmf_sdcard_reg_write(sdh, CORE_SB(corebase, sbtmstatelow), 4,
		(SICF_CLOCK_EN << SBTML_SICF_SHIFT));
	udelay(1);
}

/* SDIO Pad drive strength to select value mappings */
struct sdiod_drive_str {
	u8 strength;	/* Pad Drive Strength in mA */
	u8 sel;		/* Chip-specific select value */
};

/* SDIO Drive Strength to sel value table for PMU Rev 1 */
static const struct sdiod_drive_str sdiod_drive_strength_tab1[] = {
	{
	4, 0x2}, {
	2, 0x3}, {
	1, 0x0}, {
	0, 0x0}
	};

/* SDIO Drive Strength to sel value table for PMU Rev 2, 3 */
static const struct sdiod_drive_str sdiod_drive_strength_tab2[] = {
	{
	12, 0x7}, {
	10, 0x6}, {
	8, 0x5}, {
	6, 0x4}, {
	4, 0x2}, {
	2, 0x1}, {
	0, 0x0}
	};

/* SDIO Drive Strength to sel value table for PMU Rev 8 (1.8V) */
static const struct sdiod_drive_str sdiod_drive_strength_tab3[] = {
	{
	32, 0x7}, {
	26, 0x6}, {
	22, 0x5}, {
	16, 0x4}, {
	12, 0x3}, {
	8, 0x2}, {
	4, 0x1}, {
	0, 0x0}
	};

#define SDIOD_DRVSTR_KEY(chip, pmu)     (((chip) << 16) | (pmu))

static void
brcmf_sdbrcm_sdiod_drive_strength_init(struct dhd_bus *bus, u32 drivestrength) {
	struct sdiod_drive_str *str_tab = NULL;
	u32 str_mask = 0;
	u32 str_shift = 0;
	char chn[8];

	if (!(bus->ci->cccaps & CC_CAP_PMU))
		return;

	switch (SDIOD_DRVSTR_KEY(bus->ci->chip, bus->ci->pmurev)) {
	case SDIOD_DRVSTR_KEY(BCM4325_CHIP_ID, 1):
		str_tab = (struct sdiod_drive_str *)&sdiod_drive_strength_tab1;
		str_mask = 0x30000000;
		str_shift = 28;
		break;
	case SDIOD_DRVSTR_KEY(BCM4325_CHIP_ID, 2):
	case SDIOD_DRVSTR_KEY(BCM4325_CHIP_ID, 3):
		str_tab = (struct sdiod_drive_str *)&sdiod_drive_strength_tab2;
		str_mask = 0x00003800;
		str_shift = 11;
		break;
	case SDIOD_DRVSTR_KEY(BCM4336_CHIP_ID, 8):
		str_tab = (struct sdiod_drive_str *)&sdiod_drive_strength_tab3;
		str_mask = 0x00003800;
		str_shift = 11;
		break;
	default:
		DHD_ERROR(("No SDIO Drive strength init"
			"done for chip %s rev %d pmurev %d\n",
			brcmu_chipname(bus->ci->chip, chn, 8),
			bus->ci->chiprev, bus->ci->pmurev));
		break;
	}

	if (str_tab != NULL) {
		u32 drivestrength_sel = 0;
		u32 cc_data_temp;
		int i;

		for (i = 0; str_tab[i].strength != 0; i++) {
			if (drivestrength >= str_tab[i].strength) {
				drivestrength_sel = str_tab[i].sel;
				break;
			}
		}

		brcmf_sdcard_reg_write(bus->sdh,
			CORE_CC_REG(bus->ci->cccorebase, chipcontrol_addr),
			4, 1);
		cc_data_temp = brcmf_sdcard_reg_read(bus->sdh,
			CORE_CC_REG(bus->ci->cccorebase, chipcontrol_addr), 4);
		cc_data_temp &= ~str_mask;
		drivestrength_sel <<= str_shift;
		cc_data_temp |= drivestrength_sel;
		brcmf_sdcard_reg_write(bus->sdh,
			CORE_CC_REG(bus->ci->cccorebase, chipcontrol_addr),
			4, cc_data_temp);

		DHD_INFO(("SDIO: %dmA drive strength selected, set to 0x%08x\n",
			drivestrength, cc_data_temp));
	}
}

static void
brcmf_sdbrcm_chip_detach(struct dhd_bus *bus)
{
	DHD_TRACE(("%s: Enter\n", __func__));

	kfree(bus->ci);
	bus->ci = NULL;
}
