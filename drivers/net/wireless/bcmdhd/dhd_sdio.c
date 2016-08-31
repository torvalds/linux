/*
 * DHD Bus Module for SDIO
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_sdio.c 634247 2016-04-27 05:53:55Z $
 */

#include <typedefs.h>
#include <osl.h>
#include <bcmsdh.h>

#ifdef BCMEMBEDIMAGE
#include BCMEMBEDIMAGE
#endif /* BCMEMBEDIMAGE */

#include <bcmdefs.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmdevs.h>

#include <siutils.h>
#include <hndpmu.h>
#include <hndsoc.h>
#include <bcmsdpcm.h>
#if defined(DHD_DEBUG)
#include <hndrte_armtrap.h>
#include <hndrte_cons.h>
#endif /* defined(DHD_DEBUG) */
#include <sbchipc.h>
#include <sbhnddma.h>

#include <sdio.h>
#include <sbsdio.h>
#include <sbsdpcmdev.h>
#include <bcmsdpcm.h>
#include <bcmsdbus.h>

#include <proto/ethernet.h>
#include <proto/802.1d.h>
#include <proto/802.11.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhdioctl.h>
#include <sdiovar.h>

#ifdef PROP_TXSTATUS
#include <dhd_wlfc.h>
#endif

#ifdef DHDTCPACK_SUPPRESS
#include <dhd_ip.h>
#endif

bool dhd_mp_halting(dhd_pub_t *dhdp);
extern void bcmsdh_waitfor_iodrain(void *sdh);
extern void bcmsdh_reject_ioreqs(void *sdh, bool reject);
extern bool  bcmsdh_fatal_error(void *sdh);

#ifndef DHDSDIO_MEM_DUMP_FNAME
#define DHDSDIO_MEM_DUMP_FNAME         "mem_dump"
#endif

#define QLEN		(1024) /* bulk rx and tx queue lengths */
#define FCHI		(QLEN - 10)
#define FCLOW		(FCHI / 2)
#define PRIOMASK	7

#define TXRETRIES	2	/* # of retries for tx frames */
#define READ_FRM_CNT_RETRIES	3
#ifndef DHD_RXBOUND
#define DHD_RXBOUND	50	/* Default for max rx frames in one scheduling */
#endif

#ifndef DHD_TXBOUND
#define DHD_TXBOUND	20	/* Default for max tx frames in one scheduling */
#endif

#define DHD_TXMINMAX	1	/* Max tx frames if rx still pending */

#define MEMBLOCK	2048		/* Block size used for downloading of dongle image */
#define MAX_NVRAMBUF_SIZE	4096	/* max nvram buf size */
#define MAX_DATA_BUF	(64 * 1024)	/* Must be large enough to hold biggest possible glom */

#ifndef DHD_FIRSTREAD
#define DHD_FIRSTREAD   32
#endif
#if !ISPOWEROF2(DHD_FIRSTREAD)
#error DHD_FIRSTREAD is not a power of 2!
#endif

/* Total length of frame header for dongle protocol */
#define SDPCM_HDRLEN	(SDPCM_FRAMETAG_LEN + SDPCM_SWHEADER_LEN)
#define SDPCM_HDRLEN_TXGLOM	(SDPCM_HDRLEN + SDPCM_HWEXT_LEN)
#define MAX_TX_PKTCHAIN_CNT	SDPCM_MAXGLOM_SIZE

#ifdef SDTEST
#define SDPCM_RESERVE	(SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + DHD_SDALIGN)
#else
#define SDPCM_RESERVE	(SDPCM_HDRLEN + DHD_SDALIGN)
#endif

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
#define PMU_TRANSITION_DLY_EXTRA 500000
#endif

/* hooks for limiting threshold custom tx num in rx processing */
#define DEFAULT_TXINRX_THRES	0
#ifndef	CUSTOM_TXINRX_THRES
#define CUSTOM_TXINRX_THRES		DEFAULT_TXINRX_THRES
#endif

/* Value for ChipClockCSR during initial setup */
#define DHD_INIT_CLKCTL1	(SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ)
#define DHD_INIT_CLKCTL2	(SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP)

/* Flags for SDH calls */
#define F2SYNC	(SDIO_REQ_4BYTE | SDIO_REQ_FIXED)

/* Packet free applicable unconditionally for sdio and sdspi.  Conditional if
 * bufpool was present for gspi bus.
 */
#define PKTFREE2()		if ((bus->bus != SPI_BUS) || bus->usebufpool) \
					PKTFREE(bus->dhd->osh, pkt, FALSE);
DHD_SPINWAIT_SLEEP_INIT(sdioh_spinwait_sleep);


#ifdef DHD_DEBUG
/* Device console log buffer state */
#define CONSOLE_LINE_MAX	192
#define CONSOLE_BUFFER_MAX	2024
typedef struct dhd_console {
	uint		count;			/* Poll interval msec counter */
	uint		log_addr;		/* Log struct address (fixed) */
	hndrte_log_t	log;			/* Log struct (host copy) */
	uint		bufsize;		/* Size of log buffer */
	uint8		*buf;			/* Log buffer (host copy) */
	uint		last;			/* Last buffer read index */
} dhd_console_t;
#endif /* DHD_DEBUG */

#define	REMAP_ENAB(bus)			((bus)->remap)
#define	REMAP_ISADDR(bus, a)		(((a) >= ((bus)->orig_ramsize)) && ((a) < ((bus)->ramsize)))
#define	KSO_ENAB(bus)			((bus)->kso)
#define	SR_ENAB(bus)			((bus)->_srenab)
#define	SLPAUTO_ENAB(bus)		((SR_ENAB(bus)) && ((bus)->_slpauto))
#define	MIN_RSRC_ADDR			(SI_ENUM_BASE + 0x618)
#define	MIN_RSRC_SR			0x3
#define	CORE_CAPEXT_ADDR		(SI_ENUM_BASE + 0x64c)
#define	CORE_CAPEXT_SR_SUPPORTED_MASK	(1 << 1)
#define RCTL_MACPHY_DISABLE_MASK	(1 << 26)
#define RCTL_LOGIC_DISABLE_MASK		(1 << 27)

#define	OOB_WAKEUP_ENAB(bus)		((bus)->_oobwakeup)
#define	GPIO_DEV_SRSTATE		16	/* Host gpio17 mapped to device gpio0 SR state */
#define	GPIO_DEV_SRSTATE_TIMEOUT	320000	/* 320ms */
#define	GPIO_DEV_WAKEUP			17	/* Host gpio17 mapped to device gpio1 wakeup */
#define	CC_CHIPCTRL2_GPIO1_WAKEUP	(1  << 0)
#define	CC_CHIPCTRL3_SR_ENG_ENABLE	(1  << 2)
#define OVERFLOW_BLKSZ512_WM		96
#define OVERFLOW_BLKSZ512_MES		80

#define CC_PMUCC3	(0x3)
/* Private data for SDIO bus interaction */
typedef struct dhd_bus {
	dhd_pub_t	*dhd;

	bcmsdh_info_t	*sdh;			/* Handle for BCMSDH calls */
	si_t		*sih;			/* Handle for SI calls */
	char		*vars;			/* Variables (from CIS and/or other) */
	uint		varsz;			/* Size of variables buffer */
	uint32		sbaddr;			/* Current SB window pointer (-1, invalid) */

	sdpcmd_regs_t	*regs;			/* Registers for SDIO core */
	uint		sdpcmrev;		/* SDIO core revision */
	uint		armrev;			/* CPU core revision */
	uint		ramrev;			/* SOCRAM core revision */
	uint32		ramsize;		/* Size of RAM in SOCRAM (bytes) */
	uint32		orig_ramsize;		/* Size of RAM in SOCRAM (bytes) */
	uint32		srmemsize;		/* Size of SRMEM */

	uint32		bus;			/* gSPI or SDIO bus */
	uint32		bus_num;		/* bus number */
	uint32		slot_num;		/* slot ID */
	uint32		hostintmask;	/* Copy of Host Interrupt Mask */
	uint32		intstatus;		/* Intstatus bits (events) pending */
	bool		dpc_sched;		/* Indicates DPC schedule (intrpt rcvd) */
	bool		fcstate;		/* State of dongle flow-control */

	uint16		cl_devid;		/* cached devid for dhdsdio_probe_attach() */
	char		*fw_path;		/* module_param: path to firmware image */
	char		*nv_path;		/* module_param: path to nvram vars file */
	const char      *nvram_params;		/* user specified nvram params. */

	uint		blocksize;		/* Block size of SDIO transfers */
	uint		roundup;		/* Max roundup limit */

	struct pktq	txq;			/* Queue length used for flow-control */
	uint8		flowcontrol;		/* per prio flow control bitmask */
	uint8		tx_seq;			/* Transmit sequence number (next) */
	uint8		tx_max;			/* Maximum transmit sequence allowed */

	uint8		hdrbuf[MAX_HDR_READ + DHD_SDALIGN];
	uint8		*rxhdr;			/* Header of current rx frame (in hdrbuf) */
	uint16		nextlen;		/* Next Read Len from last header */
	uint8		rx_seq;			/* Receive sequence number (expected) */
	bool		rxskip;			/* Skip receive (awaiting NAK ACK) */

	void		*glomd;			/* Packet containing glomming descriptor */
	void		*glom;			/* Packet chain for glommed superframe */
	uint		glomerr;		/* Glom packet read errors */

	uint8		*rxbuf;			/* Buffer for receiving control packets */
	uint		rxblen;			/* Allocated length of rxbuf */
	uint8		*rxctl;			/* Aligned pointer into rxbuf */
	uint8		*databuf;		/* Buffer for receiving big glom packet */
	uint8		*dataptr;		/* Aligned pointer into databuf */
	uint		rxlen;			/* Length of valid data in buffer */

	uint8		sdpcm_ver;		/* Bus protocol reported by dongle */

	bool		intr;			/* Use interrupts */
	bool		poll;			/* Use polling */
	bool		ipend;			/* Device interrupt is pending */
	bool		intdis;			/* Interrupts disabled by isr */
	uint 		intrcount;		/* Count of device interrupt callbacks */
	uint		lastintrs;		/* Count as of last watchdog timer */
	uint		spurious;		/* Count of spurious interrupts */
	uint		pollrate;		/* Ticks between device polls */
	uint		polltick;		/* Tick counter */
	uint		pollcnt;		/* Count of active polls */

#ifdef DHD_DEBUG
	dhd_console_t	console;		/* Console output polling support */
	uint		console_addr;		/* Console address from shared struct */
#endif /* DHD_DEBUG */

	uint		regfails;		/* Count of R_REG/W_REG failures */

	uint		clkstate;		/* State of sd and backplane clock(s) */
	bool		activity;		/* Activity flag for clock down */
	int32		idletime;		/* Control for activity timeout */
	int32		idlecount;		/* Activity timeout counter */
	int32		idleclock;		/* How to set bus driver when idle */
	int32		sd_divisor;		/* Speed control to bus driver */
	int32		sd_mode;		/* Mode control to bus driver */
	int32		sd_rxchain;		/* If bcmsdh api accepts PKT chains */
	bool		use_rxchain;		/* If dhd should use PKT chains */
	bool		sleeping;		/* Is SDIO bus sleeping? */
#if defined(SUPPORT_P2P_GO_PS)
	wait_queue_head_t bus_sleep;
#endif /* LINUX && SUPPORT_P2P_GO_PS */
	uint		rxflow_mode;		/* Rx flow control mode */
	bool		rxflow;			/* Is rx flow control on */
	uint		prev_rxlim_hit;		/* Is prev rx limit exceeded (per dpc schedule) */
	bool		alp_only;		/* Don't use HT clock (ALP only) */
	/* Field to decide if rx of control frames happen in rxbuf or lb-pool */
	bool		usebufpool;
	int32		txinrx_thres;	/* num of in-queued pkts */
	int32		dotxinrx;	/* tx first in dhdsdio_readframes */
#ifdef SDTEST
	/* external loopback */
	bool		ext_loop;
	uint8		loopid;

	/* pktgen configuration */
	uint		pktgen_freq;		/* Ticks between bursts */
	uint		pktgen_count;		/* Packets to send each burst */
	uint		pktgen_print;		/* Bursts between count displays */
	uint		pktgen_total;		/* Stop after this many */
	uint		pktgen_minlen;		/* Minimum packet data len */
	uint		pktgen_maxlen;		/* Maximum packet data len */
	uint		pktgen_mode;		/* Configured mode: tx, rx, or echo */
	uint		pktgen_stop;		/* Number of tx failures causing stop */

	/* active pktgen fields */
	uint		pktgen_tick;		/* Tick counter for bursts */
	uint		pktgen_ptick;		/* Burst counter for printing */
	uint		pktgen_sent;		/* Number of test packets generated */
	uint		pktgen_rcvd;		/* Number of test packets received */
	uint		pktgen_prev_time;	/* Time at which previous stats where printed */
	uint		pktgen_prev_sent;	/* Number of test packets generated when
						 * previous stats were printed
						 */
	uint		pktgen_prev_rcvd;	/* Number of test packets received when
						 * previous stats were printed
						 */
	uint		pktgen_fail;		/* Number of failed send attempts */
	uint16		pktgen_len;		/* Length of next packet to send */
#define PKTGEN_RCV_IDLE     (0)
#define PKTGEN_RCV_ONGOING  (1)
	uint16		pktgen_rcv_state;		/* receive state */
	uint		pktgen_rcvd_rcvsession;	/* test pkts rcvd per rcv session. */
#endif /* SDTEST */

	/* Some additional counters */
	uint		tx_sderrs;		/* Count of tx attempts with sd errors */
	uint		fcqueued;		/* Tx packets that got queued */
	uint		rxrtx;			/* Count of rtx requests (NAK to dongle) */
	uint		rx_toolong;		/* Receive frames too long to receive */
	uint		rxc_errors;		/* SDIO errors when reading control frames */
	uint		rx_hdrfail;		/* SDIO errors on header reads */
	uint		rx_badhdr;		/* Bad received headers (roosync?) */
	uint		rx_badseq;		/* Mismatched rx sequence number */
	uint		fc_rcvd;		/* Number of flow-control events received */
	uint		fc_xoff;		/* Number which turned on flow-control */
	uint		fc_xon;			/* Number which turned off flow-control */
	uint		rxglomfail;		/* Failed deglom attempts */
	uint		rxglomframes;		/* Number of glom frames (superframes) */
	uint		rxglompkts;		/* Number of packets from glom frames */
	uint		f2rxhdrs;		/* Number of header reads */
	uint		f2rxdata;		/* Number of frame data reads */
	uint		f2txdata;		/* Number of f2 frame writes */
	uint		f1regdata;		/* Number of f1 register accesses */
#ifdef DHDENABLE_TAILPAD
	uint		tx_tailpad_chain;	/* Number of tail padding by chaining pad_pkt */
	uint		tx_tailpad_pktget;	/* Number of tail padding by new PKTGET */
#endif
	uint8		*ctrl_frame_buf;
	uint32		ctrl_frame_len;
	bool		ctrl_frame_stat;
	uint32		rxint_mode;	/* rx interrupt mode */
	bool		remap;		/* Contiguous 1MB RAM: 512K socram + 512K devram
					 * Available with socram rev 16
					 * Remap region not DMA-able
					 */
	bool		kso;
	bool		_slpauto;
	bool		_oobwakeup;
	bool		_srenab;
	bool        readframes;
	bool        reqbussleep;
	uint32		resetinstr;
	uint32		dongle_ram_base;

	void		*glom_pkt_arr[SDPCM_MAXGLOM_SIZE];	/* Array of pkts for glomming */
	uint32		txglom_cnt;	/* Number of pkts in the glom array */
	uint32		txglom_total_len;	/* Total length of pkts in glom array */
	bool		txglom_enable;	/* Flag to indicate whether tx glom is enabled/disabled */
	uint32		txglomsize;	/* Glom size limitation */
	void		*pad_pkt;
} dhd_bus_t;

/* clkstate */
#define CLK_NONE	0
#define CLK_SDONLY	1
#define CLK_PENDING	2	/* Not used yet */
#define CLK_AVAIL	3

#define DHD_NOPMU(dhd)	(FALSE)

#ifdef DHD_DEBUG
static int qcount[NUMPRIO];
static int tx_packets[NUMPRIO];
#endif /* DHD_DEBUG */

/* Deferred transmit */
const uint dhd_deferred_tx = 1;

extern uint dhd_watchdog_ms;

extern void dhd_os_wd_timer(void *bus, uint wdtick);

/* Tx/Rx bounds */
uint dhd_txbound;
uint dhd_rxbound;
uint dhd_txminmax = DHD_TXMINMAX;

/* override the RAM size if possible */
#define DONGLE_MIN_RAMSIZE (128 *1024)
int dhd_dongle_ramsize;

uint dhd_doflow = TRUE;
uint dhd_dpcpoll = FALSE;

module_param(dhd_doflow, uint, 0644);
module_param(dhd_dpcpoll, uint, 0644);

#if defined(OOB_PARAM)
extern uint dhd_oob_disable;
#endif /* OOB_PARAM */

static bool dhd_alignctl;

static bool sd1idle;

static bool retrydata;
#define RETRYCHAN(chan) (((chan) == SDPCM_EVENT_CHANNEL) || retrydata)

static uint watermark = 8;
static uint mesbusyctrl = 0;
static const uint firstread = DHD_FIRSTREAD;

/* Retry count for register access failures */
static const uint retry_limit = 2;

/* Force even SD lengths (some host controllers mess up on odd bytes) */
static bool forcealign;

#define ALIGNMENT  4

#if defined(OOB_INTR_ONLY) && defined(HW_OOB)
extern void bcmsdh_enable_hw_oob_intr(void *sdh, bool enable);
#endif

#if defined(OOB_INTR_ONLY) && defined(SDIO_ISR_THREAD)
#error OOB_INTR_ONLY is NOT working with SDIO_ISR_THREAD
#endif /* defined(OOB_INTR_ONLY) && defined(SDIO_ISR_THREAD) */
#define PKTALIGN(osh, p, len, align)					\
	do {								\
		uintptr datalign;						\
		datalign = (uintptr)PKTDATA((osh), (p));		\
		datalign = ROUNDUP(datalign, (align)) - datalign;	\
		ASSERT(datalign < (align));				\
		ASSERT(PKTLEN((osh), (p)) >= ((len) + datalign));	\
		if (datalign)						\
			PKTPULL((osh), (p), (uint)datalign);			\
		PKTSETLEN((osh), (p), (len));				\
	} while (0)

/* Limit on rounding up frames */
static const uint max_roundup = 512;

/* Try doing readahead */
static bool dhd_readahead;

/* To check if there's window offered */
#define DATAOK(bus) \
	(((uint8)(bus->tx_max - bus->tx_seq) > 1) && \
	(((uint8)(bus->tx_max - bus->tx_seq) & 0x80) == 0))

/* To check if there's window offered for ctrl frame */
#define TXCTLOK(bus) \
	(((uint8)(bus->tx_max - bus->tx_seq) != 0) && \
	(((uint8)(bus->tx_max - bus->tx_seq) & 0x80) == 0))

/* Number of pkts available in dongle for data RX */
#define DATABUFCNT(bus) \
	((uint8)(bus->tx_max - bus->tx_seq) - 1)

/* Macros to get register read/write status */
/* NOTE: these assume a local dhdsdio_bus_t *bus! */
#define R_SDREG(regvar, regaddr, retryvar) \
do { \
	retryvar = 0; \
	do { \
		regvar = R_REG(bus->dhd->osh, regaddr); \
	} while (bcmsdh_regfail(bus->sdh) && (++retryvar <= retry_limit)); \
	if (retryvar) { \
		bus->regfails += (retryvar-1); \
		if (retryvar > retry_limit) { \
			DHD_ERROR(("%s: FAILED" #regvar "READ, LINE %d\n", \
			           __FUNCTION__, __LINE__)); \
			regvar = 0; \
		} \
	} \
} while (0)

#define W_SDREG(regval, regaddr, retryvar) \
do { \
	retryvar = 0; \
	do { \
		W_REG(bus->dhd->osh, regaddr, regval); \
	} while (bcmsdh_regfail(bus->sdh) && (++retryvar <= retry_limit)); \
	if (retryvar) { \
		bus->regfails += (retryvar-1); \
		if (retryvar > retry_limit) \
			DHD_ERROR(("%s: FAILED REGISTER WRITE, LINE %d\n", \
			           __FUNCTION__, __LINE__)); \
	} \
} while (0)

#define BUS_WAKE(bus) \
	do { \
		bus->idlecount = 0; \
		if ((bus)->sleeping) \
			dhdsdio_bussleep((bus), FALSE); \
	} while (0);

/*
 * pktavail interrupts from dongle to host can be managed in 3 different ways
 * whenever there is a packet available in dongle to transmit to host.
 *
 * Mode 0:	Dongle writes the software host mailbox and host is interrupted.
 * Mode 1:	(sdiod core rev >= 4)
 *		Device sets a new bit in the intstatus whenever there is a packet
 *		available in fifo.  Host can't clear this specific status bit until all the
 *		packets are read from the FIFO.  No need to ack dongle intstatus.
 * Mode 2:	(sdiod core rev >= 4)
 *		Device sets a bit in the intstatus, and host acks this by writing
 *		one to this bit.  Dongle won't generate anymore packet interrupts
 *		until host reads all the packets from the dongle and reads a zero to
 *		figure that there are no more packets.  No need to disable host ints.
 *		Need to ack the intstatus.
 */

#define SDIO_DEVICE_HMB_RXINT		0	/* default old way */
#define SDIO_DEVICE_RXDATAINT_MODE_0	1	/* from sdiod rev 4 */
#define SDIO_DEVICE_RXDATAINT_MODE_1	2	/* from sdiod rev 4 */


#define FRAME_AVAIL_MASK(bus) 	\
	((bus->rxint_mode == SDIO_DEVICE_HMB_RXINT) ? I_HMB_FRAME_IND : I_XMTDATA_AVAIL)

#define DHD_BUS			SDIO_BUS

#define PKT_AVAILABLE(bus, intstatus)	((intstatus) & (FRAME_AVAIL_MASK(bus)))

#define HOSTINTMASK		(I_HMB_SW_MASK | I_CHIPACTIVE)

#define GSPI_PR55150_BAILOUT

#ifdef SDTEST
static void dhdsdio_testrcv(dhd_bus_t *bus, void *pkt, uint seq);
static void dhdsdio_sdtest_set(dhd_bus_t *bus, uint count);
#endif

#ifdef DHD_DEBUG
static int dhdsdio_checkdied(dhd_bus_t *bus, char *data, uint size);
static int dhd_serialconsole(dhd_bus_t *bus, bool get, bool enable, int *bcmerror);
#endif /* DHD_DEBUG */

static int dhdsdio_devcap_set(dhd_bus_t *bus, uint8 cap);
static int dhdsdio_download_state(dhd_bus_t *bus, bool enter);

static void dhdsdio_release(dhd_bus_t *bus, osl_t *osh);
static void dhdsdio_release_malloc(dhd_bus_t *bus, osl_t *osh);
static void dhdsdio_disconnect(void *ptr);
static bool dhdsdio_chipmatch(uint16 chipid);
static bool dhdsdio_probe_attach(dhd_bus_t *bus, osl_t *osh, void *sdh,
                                 void * regsva, uint16  devid);
static bool dhdsdio_probe_malloc(dhd_bus_t *bus, osl_t *osh, void *sdh);
static bool dhdsdio_probe_init(dhd_bus_t *bus, osl_t *osh, void *sdh);
static void dhdsdio_release_dongle(dhd_bus_t *bus, osl_t *osh, bool dongle_isolation,
	bool reset_flag);

static void dhd_dongle_setramsize(struct dhd_bus *bus, int mem_size);
static int dhd_bcmsdh_recv_buf(dhd_bus_t *bus, uint32 addr, uint fn, uint flags,
	uint8 *buf, uint nbytes,
	void *pkt, bcmsdh_cmplt_fn_t complete, void *handle);
static int dhd_bcmsdh_send_buf(dhd_bus_t *bus, uint32 addr, uint fn, uint flags,
	uint8 *buf, uint nbytes,
	void *pkt, bcmsdh_cmplt_fn_t complete, void *handle, int max_retry);
static int dhdsdio_txpkt(dhd_bus_t *bus, uint chan, void** pkts, int num_pkt, bool free_pkt);
static int dhdsdio_txpkt_preprocess(dhd_bus_t *bus, void *pkt, int chan, int txseq,
	int prev_chain_total_len, bool last_chained_pkt,
	int *pad_pkt_len, void **new_pkt);
static int dhdsdio_txpkt_postprocess(dhd_bus_t *bus, void *pkt);

static int dhdsdio_download_firmware(dhd_bus_t *bus, osl_t *osh, void *sdh);
static int _dhdsdio_download_firmware(dhd_bus_t *bus);

static int dhdsdio_download_code_file(dhd_bus_t *bus, char *image_path);
static int dhdsdio_download_nvram(dhd_bus_t *bus);
#ifdef BCMEMBEDIMAGE
static int dhdsdio_download_code_array(dhd_bus_t *bus);
#endif
static int dhdsdio_bussleep(dhd_bus_t *bus, bool sleep);
static int dhdsdio_clkctl(dhd_bus_t *bus, uint target, bool pendok);
static uint8 dhdsdio_sleepcsr_get(dhd_bus_t *bus);

#ifdef WLMEDIA_HTSF
#include <htsf.h>
extern uint32 dhd_get_htsf(void *dhd, int ifidx);
#endif /* WLMEDIA_HTSF */

static void
dhdsdio_tune_fifoparam(struct dhd_bus *bus)
{
	int err;
	uint8 devctl, wm, mes;

	if (bus->sih->buscorerev >= 15) {
		/* See .ppt in PR for these recommended values */
		if (bus->blocksize == 512) {
			wm = OVERFLOW_BLKSZ512_WM;
			mes = OVERFLOW_BLKSZ512_MES;
		} else {
			mes = bus->blocksize/4;
			wm = bus->blocksize/4;
		}

		watermark = wm;
		mesbusyctrl = mes;
	} else {
		DHD_INFO(("skip fifotune: SdioRev(%d) is lower than minimal requested ver\n",
			bus->sih->buscorerev));
		return;
	}

	/* Update watermark */
	if (wm > 0) {
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_WATERMARK, wm, &err);

		devctl = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
		devctl |= SBSDIO_DEVCTL_F2WM_ENAB;
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
	}

	/* Update MES */
	if (mes > 0) {
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_MESBUSYCTRL,
			(mes | SBSDIO_MESBUSYCTRL_ENAB), &err);
	}

	DHD_INFO(("Apply overflow WAR: 0x%02x 0x%02x 0x%02x\n",
		bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err),
		bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_WATERMARK, &err),
		bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_MESBUSYCTRL, &err)));
}

static void
dhd_dongle_setramsize(struct dhd_bus *bus, int mem_size)
{
	int32 min_size =  DONGLE_MIN_RAMSIZE;
	/* Restrict the ramsize to user specified limit */
	DHD_ERROR(("user: Restrict the dongle ram size to %d, min accepted %d\n",
		dhd_dongle_ramsize, min_size));
	if ((dhd_dongle_ramsize > min_size) &&
		(dhd_dongle_ramsize < (int32)bus->orig_ramsize))
		bus->ramsize = dhd_dongle_ramsize;
}

static int
dhdsdio_set_siaddr_window(dhd_bus_t *bus, uint32 address)
{
	int err = 0;
	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRLOW,
	                 (address >> 8) & SBSDIO_SBADDRLOW_MASK, &err);
	if (!err)
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRMID,
		                 (address >> 16) & SBSDIO_SBADDRMID_MASK, &err);
	if (!err)
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SBADDRHIGH,
		                 (address >> 24) & SBSDIO_SBADDRHIGH_MASK, &err);
	return err;
}


#ifdef USE_OOB_GPIO1
static int
dhdsdio_oobwakeup_init(dhd_bus_t *bus)
{
	uint32 val, addr, data;

	bcmsdh_gpioouten(bus->sdh, GPIO_DEV_WAKEUP);

	addr = SI_ENUM_BASE + OFFSETOF(chipcregs_t, chipcontrol_addr);
	data = SI_ENUM_BASE + OFFSETOF(chipcregs_t, chipcontrol_data);

	/* Set device for gpio1 wakeup */
	bcmsdh_reg_write(bus->sdh, addr, 4, 2);
	val = bcmsdh_reg_read(bus->sdh, data, 4);
	val |= CC_CHIPCTRL2_GPIO1_WAKEUP;
	bcmsdh_reg_write(bus->sdh, data, 4, val);

	bus->_oobwakeup = TRUE;

	return 0;
}
#endif /* USE_OOB_GPIO1 */

/*
 * Query if FW is in SR mode
 */
static bool
dhdsdio_sr_cap(dhd_bus_t *bus)
{
	bool cap = FALSE;
	uint32  core_capext, addr, data;


	if (bus->sih->chip == BCM43430_CHIP_ID) {
		/* check if fw initialized sr engine */
		addr = SI_ENUM_BASE + OFFSETOF(chipcregs_t, sr_control1);
		if (bcmsdh_reg_read(bus->sdh, addr, 4) != 0)
			cap = TRUE;

		return cap;
	}

	if (bus->sih->chip == BCM4324_CHIP_ID) {
			addr = SI_ENUM_BASE + OFFSETOF(chipcregs_t, chipcontrol_addr);
			data = SI_ENUM_BASE + OFFSETOF(chipcregs_t, chipcontrol_data);
			bcmsdh_reg_write(bus->sdh, addr, 4, 3);
			core_capext = bcmsdh_reg_read(bus->sdh, data, 4);
	} else if (bus->sih->chip == BCM4330_CHIP_ID) {
			core_capext = FALSE;
	} else if ((bus->sih->chip == BCM4335_CHIP_ID) ||
		(bus->sih->chip == BCM4339_CHIP_ID) ||
		(bus->sih->chip == BCM43349_CHIP_ID) ||
		(bus->sih->chip == BCM4345_CHIP_ID) ||
		(bus->sih->chip == BCM4354_CHIP_ID) ||
		(bus->sih->chip == BCM4356_CHIP_ID) ||
		(bus->sih->chip == BCM4350_CHIP_ID)) {
		core_capext = TRUE;
	} else {
			core_capext = bcmsdh_reg_read(bus->sdh, CORE_CAPEXT_ADDR, 4);
			core_capext = (core_capext & CORE_CAPEXT_SR_SUPPORTED_MASK);
	}
	if (!(core_capext))
		return FALSE;

	if (bus->sih->chip == BCM4324_CHIP_ID) {
		/* FIX: Should change to query SR control register instead */
		cap = TRUE;
	} else if ((bus->sih->chip == BCM4335_CHIP_ID) ||
		(bus->sih->chip == BCM4339_CHIP_ID) ||
		(bus->sih->chip == BCM43349_CHIP_ID) ||
		(bus->sih->chip == BCM4345_CHIP_ID) ||
		(bus->sih->chip == BCM4354_CHIP_ID) ||
		(bus->sih->chip == BCM4356_CHIP_ID) ||
		(bus->sih->chip == BCM4350_CHIP_ID)) {
		uint32 enabval = 0;
		addr = SI_ENUM_BASE + OFFSETOF(chipcregs_t, chipcontrol_addr);
		data = SI_ENUM_BASE + OFFSETOF(chipcregs_t, chipcontrol_data);
		bcmsdh_reg_write(bus->sdh, addr, 4, CC_PMUCC3);
		enabval = bcmsdh_reg_read(bus->sdh, data, 4);

		if ((bus->sih->chip == BCM4350_CHIP_ID) ||
			(bus->sih->chip == BCM4345_CHIP_ID) ||
			(bus->sih->chip == BCM4356_CHIP_ID) ||
			(bus->sih->chip == BCM4354_CHIP_ID))
			enabval &= CC_CHIPCTRL3_SR_ENG_ENABLE;

		if (enabval)
			cap = TRUE;
	} else {
		data = bcmsdh_reg_read(bus->sdh,
			SI_ENUM_BASE + OFFSETOF(chipcregs_t, retention_ctl), 4);
		if ((data & (RCTL_MACPHY_DISABLE_MASK | RCTL_LOGIC_DISABLE_MASK)) == 0)
			cap = TRUE;
	}

	return cap;
}

static int
dhdsdio_srwar_init(dhd_bus_t *bus)
{
	bcmsdh_gpio_init(bus->sdh);

#ifdef USE_OOB_GPIO1
	dhdsdio_oobwakeup_init(bus);
#endif


	return 0;
}

static int
dhdsdio_sr_init(dhd_bus_t *bus)
{
	uint8 val;
	int err = 0;

	if ((bus->sih->chip == BCM4334_CHIP_ID) && (bus->sih->chiprev == 2))
		dhdsdio_srwar_init(bus);

	val = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_WAKEUPCTRL, NULL);
	val |= 1 << SBSDIO_FUNC1_WCTRL_HTWAIT_SHIFT;
	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_WAKEUPCTRL,
		1 << SBSDIO_FUNC1_WCTRL_HTWAIT_SHIFT, &err);
	val = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_WAKEUPCTRL, NULL);

#ifdef USE_CMD14
	/* Add CMD14 Support */
	dhdsdio_devcap_set(bus,
		(SDIOD_CCCR_BRCM_CARDCAP_CMD14_SUPPORT | SDIOD_CCCR_BRCM_CARDCAP_CMD14_EXT));
#endif /* USE_CMD14 */

	dhdsdio_devcap_set(bus, SDIOD_CCCR_BRCM_CARDCAP_CMD_NODEC);

	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1,
		SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_FORCE_HT, &err);

	bus->_slpauto = dhd_slpauto ? TRUE : FALSE;

	bus->_srenab = TRUE;

	return 0;
}

/*
 * FIX: Be sure KSO bit is enabled
 * Currently, it's defaulting to 0 which should be 1.
 */
static int
dhdsdio_clk_kso_init(dhd_bus_t *bus)
{
	uint8 val;
	int err = 0;

	/* set flag */
	bus->kso = TRUE;

	/*
	 * Enable KeepSdioOn (KSO) bit for normal operation
	 * Default is 0 (4334A0) so set it. Fixed in B0.
	 */
	val = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SLEEPCSR, NULL);
	if (!(val & SBSDIO_FUNC1_SLEEPCSR_KSO_MASK)) {
		val |= (SBSDIO_FUNC1_SLEEPCSR_KSO_EN << SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT);
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SLEEPCSR, val, &err);
		if (err)
			DHD_ERROR(("%s: SBSDIO_FUNC1_SLEEPCSR err: 0x%x\n", __FUNCTION__, err));
	}

	return 0;
}

#define KSO_DBG(x)
#define KSO_WAIT_US 50
#define KSO_WAIT_MS 1
#define KSO_SLEEP_RETRY_COUNT 20
#define ERROR_BCME_NODEVICE_MAX 1

#define MAX_KSO_ATTEMPTS (PMU_MAX_TRANSITION_DLY/KSO_WAIT_US)
static int
dhdsdio_clk_kso_enab(dhd_bus_t *bus, bool on)
{
	uint8 wr_val = 0, rd_val, cmp_val, bmask;
	int err = 0;
	int try_cnt = 0;

	KSO_DBG(("%s> op:%s\n", __FUNCTION__, (on ? "KSO_SET" : "KSO_CLR")));

	wr_val |= (on << SBSDIO_FUNC1_SLEEPCSR_KSO_SHIFT);

	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SLEEPCSR, wr_val, &err);

	if (on) {
		cmp_val = SBSDIO_FUNC1_SLEEPCSR_KSO_MASK |  SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK;
		bmask = cmp_val;

		OSL_SLEEP(3);
	} else {
		/* Put device to sleep, turn off  KSO  */
		cmp_val = 0;
		bmask = SBSDIO_FUNC1_SLEEPCSR_KSO_MASK;
	}

	do {
		rd_val = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SLEEPCSR, &err);
		if (((rd_val & bmask) == cmp_val) && !err)
			break;

		KSO_DBG(("%s> KSO wr/rd retry:%d, ERR:%x \n", __FUNCTION__, try_cnt, err));

		if (((try_cnt + 1) % KSO_SLEEP_RETRY_COUNT) == 0) {
			OSL_SLEEP(KSO_WAIT_MS);
		} else
			OSL_DELAY(KSO_WAIT_US);

		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SLEEPCSR, wr_val, &err);
	} while (try_cnt++ < MAX_KSO_ATTEMPTS);


	if (try_cnt > 2)
		KSO_DBG(("%s> op:%s, try_cnt:%d, rd_val:%x, ERR:%x \n",
			__FUNCTION__, (on ? "KSO_SET" : "KSO_CLR"), try_cnt, rd_val, err));

	if (try_cnt > MAX_KSO_ATTEMPTS)  {
		DHD_ERROR(("%s> op:%s, ERROR: try_cnt:%d, rd_val:%x, ERR:%x \n",
			__FUNCTION__, (on ? "KSO_SET" : "KSO_CLR"), try_cnt, rd_val, err));
	}
	return err;
}

static int
dhdsdio_clk_kso_iovar(dhd_bus_t *bus, bool on)
{
	int err = 0;

	if (on == FALSE) {

		BUS_WAKE(bus);
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

		DHD_ERROR(("%s: KSO disable clk: 0x%x\n", __FUNCTION__,
			bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, &err)));
		dhdsdio_clk_kso_enab(bus, FALSE);
	} else {
		DHD_ERROR(("%s: KSO enable\n", __FUNCTION__));

		/* Make sure we have SD bus access */
		if (bus->clkstate == CLK_NONE) {
			DHD_ERROR(("%s: Request SD clk\n", __FUNCTION__));
			dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);
		}

		dhdsdio_clk_kso_enab(bus, TRUE);

		DHD_ERROR(("%s: sleepcsr: 0x%x\n", __FUNCTION__,
			dhdsdio_sleepcsr_get(bus)));
	}

	bus->kso = on;
	BCM_REFERENCE(err);

	return 0;
}

static uint8
dhdsdio_sleepcsr_get(dhd_bus_t *bus)
{
	int err = 0;
	uint8 val = 0;

	val = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_SLEEPCSR, &err);
	if (err)
		DHD_TRACE(("Failed to read SLEEPCSR: %d\n", err));

	return val;
}

uint8
dhdsdio_devcap_get(dhd_bus_t *bus)
{
	return bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_BRCM_CARDCAP, NULL);
}

static int
dhdsdio_devcap_set(dhd_bus_t *bus, uint8 cap)
{
	int err = 0;

	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_BRCM_CARDCAP, cap, &err);
	if (err)
		DHD_ERROR(("%s: devcap set err: 0x%x\n", __FUNCTION__, err));

	return 0;
}

static int
dhdsdio_clk_devsleep_iovar(dhd_bus_t *bus, bool on)
{
	int err = 0, retry;
	uint8 val;

	retry = 0;
	if (on == TRUE) {
		/* Enter Sleep */

		/* Be sure we request clk before going to sleep
		 * so we can wake-up with clk request already set
		 * else device can go back to sleep immediately
		 */
		if (!SLPAUTO_ENAB(bus))
			dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
		else {
			val = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
			if ((val & SBSDIO_CSR_MASK) == 0) {
				DHD_ERROR(("%s: No clock before enter sleep:0x%x\n",
					__FUNCTION__, val));

				/* Reset clock request */
				bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
					SBSDIO_ALP_AVAIL_REQ, &err);
				DHD_ERROR(("%s: clock before sleep:0x%x\n", __FUNCTION__,
					bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1,
					SBSDIO_FUNC1_CHIPCLKCSR, &err)));
			}
		}

		DHD_TRACE(("%s: clk before sleep: 0x%x\n", __FUNCTION__,
			bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, &err)));
#ifdef USE_CMD14
		err = bcmsdh_sleep(bus->sdh, TRUE);
#else
		err = dhdsdio_clk_kso_enab(bus, FALSE);
		if (OOB_WAKEUP_ENAB(bus))
		{
			err = bcmsdh_gpioout(bus->sdh, GPIO_DEV_WAKEUP, FALSE);  /* GPIO_1 is off */
		}
#endif /* USE_CMD14 */
	} else {
		/* Exit Sleep */
		/* Make sure we have SD bus access */
		if (bus->clkstate == CLK_NONE) {
			DHD_TRACE(("%s: Request SD clk\n", __FUNCTION__));
			dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);
		}

		if ((bus->sih->chip == BCM4334_CHIP_ID) && (bus->sih->chiprev == 2)) {
			SPINWAIT_SLEEP(sdioh_spinwait_sleep,
				(bcmsdh_gpioin(bus->sdh, GPIO_DEV_SRSTATE) != TRUE),
				GPIO_DEV_SRSTATE_TIMEOUT);

			if (bcmsdh_gpioin(bus->sdh, GPIO_DEV_SRSTATE) == FALSE) {
				DHD_ERROR(("ERROR: GPIO_DEV_SRSTATE still low!\n"));
			}
		}
#ifdef USE_CMD14
		err = bcmsdh_sleep(bus->sdh, FALSE);
		if (SLPAUTO_ENAB(bus) && (err != 0)) {
			OSL_DELAY(10000);
			DHD_TRACE(("%s: Resync device sleep\n", __FUNCTION__));

			/* Toggle sleep to resync with host and device */
			err = bcmsdh_sleep(bus->sdh, TRUE);
			OSL_DELAY(10000);
			err = bcmsdh_sleep(bus->sdh, FALSE);

			if (err) {
				OSL_DELAY(10000);
				DHD_ERROR(("%s: CMD14 exit failed again!\n", __FUNCTION__));

				/* Toggle sleep to resync with host and device */
				err = bcmsdh_sleep(bus->sdh, TRUE);
				OSL_DELAY(10000);
				err = bcmsdh_sleep(bus->sdh, FALSE);
				if (err) {
					DHD_ERROR(("%s: CMD14 exit failed twice!\n", __FUNCTION__));
					DHD_ERROR(("%s: FATAL: Device non-response!\n",
						__FUNCTION__));
					err = 0;
				}
			}
		}
#else
		if (OOB_WAKEUP_ENAB(bus))
		{
			err = bcmsdh_gpioout(bus->sdh, GPIO_DEV_WAKEUP, TRUE);  /* GPIO_1 is on */
		}
		do {
			err = dhdsdio_clk_kso_enab(bus, TRUE);
			if (err)
				OSL_SLEEP(10);
		} while ((err != 0) && (++retry < 3));

		if (err != 0) {
			DHD_ERROR(("ERROR: kso set failed retry: %d\n", retry));
			err = 0; /* continue anyway */
		}
#endif /* !USE_CMD14 */

		if (err == 0) {
			uint8 csr;

			/* Wait for device ready during transition to wake-up */
			SPINWAIT_SLEEP(sdioh_spinwait_sleep,
				(((csr = dhdsdio_sleepcsr_get(bus)) &
				SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK) !=
				(SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK)), (20000));

			DHD_TRACE(("%s: ExitSleep sleepcsr: 0x%x\n", __FUNCTION__, csr));

			if (!(csr & SBSDIO_FUNC1_SLEEPCSR_DEVON_MASK)) {
				DHD_ERROR(("%s:ERROR: ExitSleep device NOT Ready! 0x%x\n",
					__FUNCTION__, csr));
				err = BCME_NODEVICE;
			}

			SPINWAIT_SLEEP(sdioh_spinwait_sleep,
				(((csr = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1,
				SBSDIO_FUNC1_CHIPCLKCSR, &err)) & SBSDIO_HT_AVAIL) !=
				(SBSDIO_HT_AVAIL)), (10000));

			DHD_TRACE(("%s: SBSDIO_FUNC1_CHIPCLKCSR : 0x%x\n", __FUNCTION__, csr));
			if (!err && ((csr & SBSDIO_HT_AVAIL) != SBSDIO_HT_AVAIL)) {
				DHD_ERROR(("%s:ERROR: device NOT Ready! 0x%x\n",
					__FUNCTION__, csr));
				err = BCME_NODEVICE;
			}
		}
	}

	/* Update if successful */
	if (err == 0)
		bus->kso = on ? FALSE : TRUE;
	else {
		DHD_ERROR(("%s: Sleep request failed: kso:%d on:%d err:%d\n",
			__FUNCTION__, bus->kso, on, err));
		if (!on && retry > 2)
			bus->kso = FALSE;
	}

	return err;
}

/* Turn backplane clock on or off */
static int
dhdsdio_htclk(dhd_bus_t *bus, bool on, bool pendok)
{
#define HT_AVAIL_ERROR_MAX 10
	static int ht_avail_error = 0;
	int err;
	uint8 clkctl, clkreq, devctl;
	bcmsdh_info_t *sdh;
	uint32 delay = PMU_MAX_TRANSITION_DLY;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	clkctl = 0;
	sdh = bus->sdh;


	if (!KSO_ENAB(bus))
		return BCME_OK;

	if (SLPAUTO_ENAB(bus)) {
		bus->clkstate = (on ? CLK_AVAIL : CLK_SDONLY);
		return BCME_OK;
	}

	if (on) {
		/* Request HT Avail */
		clkreq = bus->alp_only ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;



		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
		if (err) {
			ht_avail_error++;
			if (ht_avail_error < HT_AVAIL_ERROR_MAX) {
				DHD_ERROR(("%s: HT Avail request error: %d\n", __FUNCTION__, err));
			}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
			else if (ht_avail_error == HT_AVAIL_ERROR_MAX) {
				dhd_os_send_hang_message(bus->dhd);
			}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) */
			return BCME_ERROR;
		} else {
			ht_avail_error = 0;
		}


		/* Check current status */
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			DHD_ERROR(("%s: HT Avail read error: %d\n", __FUNCTION__, err));
			return BCME_ERROR;
		}

#if !defined(OOB_INTR_ONLY) || defined(OOB_PARAM)
		/* Go to pending and await interrupt if appropriate */
		if (1 &&
#if defined(OOB_PARAM)
			dhd_oob_disable &&
#endif /* OOB_PARAM */
			!SBSDIO_CLKAV(clkctl, bus->alp_only) && pendok) {
			/* Allow only clock-available interrupt */
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			if (err) {
				DHD_ERROR(("%s: Devctl access error setting CA: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}

			devctl |= SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
			DHD_INFO(("CLKCTL: set PENDING\n"));
			bus->clkstate = CLK_PENDING;
			return BCME_OK;
		} else
#endif /* !defined(OOB_INTR_ONLY) || defined(OOB_PARAM) */
		{
			if (bus->clkstate == CLK_PENDING) {
				/* Cancel CA-only interrupt filter */
				devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
				devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
				bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
			}
		}

		/* Give the 43362 chip a little bit extra time for HT Avail */
		if (bus->sih->chip == BCM43362_CHIP_ID) {
			delay += PMU_TRANSITION_DLY_EXTRA;
		}

		/* Otherwise, wait here (polling) for HT Avail */
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			SPINWAIT_SLEEP(sdioh_spinwait_sleep,
				((clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
				SBSDIO_FUNC1_CHIPCLKCSR, &err)),
				!SBSDIO_CLKAV(clkctl, bus->alp_only)),
				delay);
		}
		if (err) {
			DHD_ERROR(("%s: HT Avail request error: %d\n", __FUNCTION__, err));
			return BCME_ERROR;
		}
		if (!SBSDIO_CLKAV(clkctl, bus->alp_only)) {
			DHD_ERROR(("%s: HT Avail timeout (%d): clkctl 0x%02x\n",
			           __FUNCTION__, PMU_MAX_TRANSITION_DLY, clkctl));
			return BCME_ERROR;
		}

		/* Mark clock available */
		bus->clkstate = CLK_AVAIL;
		DHD_INFO(("CLKCTL: turned ON\n"));

#if defined(DHD_DEBUG)
		if (bus->alp_only == TRUE) {
#if !defined(BCMLXSDMMC)
			if (!SBSDIO_ALPONLY(clkctl)) {
				DHD_ERROR(("%s: HT Clock, when ALP Only\n", __FUNCTION__));
			}
#endif /* !defined(BCMLXSDMMC) */
		} else {
			if (SBSDIO_ALPONLY(clkctl)) {
				DHD_ERROR(("%s: HT Clock should be on.\n", __FUNCTION__));
			}
		}
#endif /* defined (DHD_DEBUG) */

		bus->activity = TRUE;
#ifdef DHD_USE_IDLECOUNT
		bus->idlecount = 0;
#endif /* DHD_USE_IDLECOUNT */
	} else {
		clkreq = 0;

		if (bus->clkstate == CLK_PENDING) {
			/* Cancel CA-only interrupt filter */
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
		}

		bus->clkstate = CLK_SDONLY;
		if (!SR_ENAB(bus)) {
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
			DHD_INFO(("CLKCTL: turned OFF\n"));
			if (err) {
				DHD_ERROR(("%s: Failed access turning clock off: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		}
	}
	return BCME_OK;
}

/* Change idle/active SD state */
static int
dhdsdio_sdclk(dhd_bus_t *bus, bool on)
{
	int err;
	int32 iovalue;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (on) {
		if (bus->idleclock == DHD_IDLE_STOP) {
			/* Turn on clock and restore mode */
			iovalue = 1;
			err = bcmsdh_iovar_op(bus->sdh, "sd_clock", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error enabling sd_clock: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}

			iovalue = bus->sd_mode;
			err = bcmsdh_iovar_op(bus->sdh, "sd_mode", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error changing sd_mode: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		} else if (bus->idleclock != DHD_IDLE_ACTIVE) {
			/* Restore clock speed */
			iovalue = bus->sd_divisor;
			err = bcmsdh_iovar_op(bus->sdh, "sd_divisor", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error restoring sd_divisor: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		}
		bus->clkstate = CLK_SDONLY;
	} else {
		/* Stop or slow the SD clock itself */
		if ((bus->sd_divisor == -1) || (bus->sd_mode == -1)) {
			DHD_TRACE(("%s: can't idle clock, divisor %d mode %d\n",
			           __FUNCTION__, bus->sd_divisor, bus->sd_mode));
			return BCME_ERROR;
		}
		if (bus->idleclock == DHD_IDLE_STOP) {
			if (sd1idle) {
				/* Change to SD1 mode and turn off clock */
				iovalue = 1;
				err = bcmsdh_iovar_op(bus->sdh, "sd_mode", NULL, 0,
				                      &iovalue, sizeof(iovalue), TRUE);
				if (err) {
					DHD_ERROR(("%s: error changing sd_clock: %d\n",
					           __FUNCTION__, err));
					return BCME_ERROR;
				}
			}

			iovalue = 0;
			err = bcmsdh_iovar_op(bus->sdh, "sd_clock", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error disabling sd_clock: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		} else if (bus->idleclock != DHD_IDLE_ACTIVE) {
			/* Set divisor to idle value */
			iovalue = bus->idleclock;
			err = bcmsdh_iovar_op(bus->sdh, "sd_divisor", NULL, 0,
			                      &iovalue, sizeof(iovalue), TRUE);
			if (err) {
				DHD_ERROR(("%s: error changing sd_divisor: %d\n",
				           __FUNCTION__, err));
				return BCME_ERROR;
			}
		}
		bus->clkstate = CLK_NONE;
	}

	return BCME_OK;
}

/* Transition SD and backplane clock readiness */
static int
dhdsdio_clkctl(dhd_bus_t *bus, uint target, bool pendok)
{
	int ret = BCME_OK;
#ifdef DHD_DEBUG
	uint oldstate = bus->clkstate;
#endif /* DHD_DEBUG */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* Early exit if we're already there */
	if (bus->clkstate == target) {
		if (target == CLK_AVAIL) {
			dhd_os_wd_timer(bus->dhd, dhd_watchdog_ms);
			bus->activity = TRUE;
#ifdef DHD_USE_IDLECOUNT
			bus->idlecount = 0;
#endif /* DHD_USE_IDLECOUNT */
		}
		return ret;
	}

	switch (target) {
	case CLK_AVAIL:
		/* Make sure SD clock is available */
		if (bus->clkstate == CLK_NONE)
			dhdsdio_sdclk(bus, TRUE);
		/* Now request HT Avail on the backplane */
		ret = dhdsdio_htclk(bus, TRUE, pendok);
		if (ret == BCME_OK) {
			dhd_os_wd_timer(bus->dhd, dhd_watchdog_ms);
		bus->activity = TRUE;
#ifdef DHD_USE_IDLECOUNT
			bus->idlecount = 0;
#endif /* DHD_USE_IDLECOUNT */
		}
		break;

	case CLK_SDONLY:
		/* Remove HT request, or bring up SD clock */
		if (bus->clkstate == CLK_NONE)
			ret = dhdsdio_sdclk(bus, TRUE);
		else if (bus->clkstate == CLK_AVAIL)
			ret = dhdsdio_htclk(bus, FALSE, FALSE);
		else
			DHD_ERROR(("dhdsdio_clkctl: request for %d -> %d\n",
			           bus->clkstate, target));
		if (ret == BCME_OK) {
			dhd_os_wd_timer(bus->dhd, dhd_watchdog_ms);
		}
		break;

	case CLK_NONE:
		/* Make sure to remove HT request */
		if (bus->clkstate == CLK_AVAIL)
			ret = dhdsdio_htclk(bus, FALSE, FALSE);
		/* Now remove the SD clock */
		ret = dhdsdio_sdclk(bus, FALSE);
#ifdef DHD_DEBUG
		if (dhd_console_ms == 0)
#endif /* DHD_DEBUG */
		if (bus->poll == 0)
			dhd_os_wd_timer(bus->dhd, 0);
		break;
	}
#ifdef DHD_DEBUG
	DHD_INFO(("dhdsdio_clkctl: %d -> %d\n", oldstate, bus->clkstate));
#endif /* DHD_DEBUG */

	return ret;
}

static int
dhdsdio_bussleep(dhd_bus_t *bus, bool sleep)
{
	int err = 0;
	bcmsdh_info_t *sdh = bus->sdh;
	sdpcmd_regs_t *regs = bus->regs;
	uint retries = 0;

	DHD_INFO(("dhdsdio_bussleep: request %s (currently %s)\n",
	          (sleep ? "SLEEP" : "WAKE"),
	          (bus->sleeping ? "SLEEP" : "WAKE")));

	if (bus->dhd->hang_was_sent)
		return BCME_ERROR;

	/* Done if we're already in the requested state */
	if (sleep == bus->sleeping)
		return BCME_OK;

	/* Going to sleep: set the alarm and turn off the lights... */
	if (sleep) {
		/* Don't sleep if something is pending */
		if (bus->dpc_sched || bus->rxskip || pktq_len(&bus->txq))
			return BCME_BUSY;


		if (!SLPAUTO_ENAB(bus)) {
			/* Disable SDIO interrupts (no longer interested) */
			bcmsdh_intr_disable(bus->sdh);

			/* Make sure the controller has the bus up */
			dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

			/* Tell device to start using OOB wakeup */
			W_SDREG(SMB_USE_OOB, &regs->tosbmailbox, retries);
			if (retries > retry_limit)
				DHD_ERROR(("CANNOT SIGNAL CHIP, WILL NOT WAKE UP!!\n"));

			/* Turn off our contribution to the HT clock request */
			dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);

			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
				SBSDIO_FORCE_HW_CLKREQ_OFF, NULL);

			/* Isolate the bus */
			if (bus->sih->chip != BCM4329_CHIP_ID &&
				bus->sih->chip != BCM4319_CHIP_ID) {
				bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL,
					SBSDIO_DEVCTL_PADS_ISO, NULL);
			}
		} else {
			/* Leave interrupts enabled since device can exit sleep and
			 * interrupt host
			 */
			err = dhdsdio_clk_devsleep_iovar(bus, TRUE /* sleep */);
		}

		/* Change state */
		bus->sleeping = TRUE;
#if defined(SUPPORT_P2P_GO_PS)
		wake_up(&bus->bus_sleep);
#endif /* LINUX && SUPPORT_P2P_GO_PS */
	} else {
		/* Waking up: bus power up is ok, set local state */

		if (!SLPAUTO_ENAB(bus)) {
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, 0, &err);

			/* Force pad isolation off if possible (in case power never toggled) */
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, 0, NULL);


			/* Make sure the controller has the bus up */
			dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

			/* Send misc interrupt to indicate OOB not needed */
			W_SDREG(0, &regs->tosbmailboxdata, retries);
			if (retries <= retry_limit)
				W_SDREG(SMB_DEV_INT, &regs->tosbmailbox, retries);

			if (retries > retry_limit)
				DHD_ERROR(("CANNOT SIGNAL CHIP TO CLEAR OOB!!\n"));

			/* Make sure we have SD bus access */
			dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);

			/* Enable interrupts again */
			if (bus->intr && (bus->dhd->busstate == DHD_BUS_DATA)) {
				bus->intdis = FALSE;
				bcmsdh_intr_enable(bus->sdh);
			}
		} else {
			err = dhdsdio_clk_devsleep_iovar(bus, FALSE /* wake */);
		}

		if (err == 0) {
			/* Change state */
			bus->sleeping = FALSE;
		}
	}

	return err;
}


#if defined(OOB_INTR_ONLY)
void
dhd_enable_oob_intr(struct dhd_bus *bus, bool enable)
{
#if defined(HW_OOB)
	bcmsdh_enable_hw_oob_intr(bus->sdh, enable);
#else
	sdpcmd_regs_t *regs = bus->regs;
	uint retries = 0;

	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
	if (enable == TRUE) {

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
	dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);
#endif /* !defined(HW_OOB) */
}
#endif 

int
dhd_bus_txdata(struct dhd_bus *bus, void *pkt)
{
	int ret = BCME_ERROR;
	osl_t *osh;
	uint datalen, prec;
#if defined(DHD_TX_DUMP) || defined(DHD_8021X_DUMP)
	uint8 *dump_data;
	uint16 protocol;
#endif /* DHD_TX_DUMP || DHD_8021X_DUMP */

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	osh = bus->dhd->osh;
	datalen = PKTLEN(osh, pkt);

#ifdef SDTEST
	/* Push the test header if doing loopback */
	if (bus->ext_loop) {
		uint8* data;
		PKTPUSH(osh, pkt, SDPCM_TEST_HDRLEN);
		data = PKTDATA(osh, pkt);
		*data++ = SDPCM_TEST_ECHOREQ;
		*data++ = (uint8)bus->loopid++;
		*data++ = (datalen >> 0);
		*data++ = (datalen >> 8);
		datalen += SDPCM_TEST_HDRLEN;
	}
#else /* SDTEST */
	BCM_REFERENCE(datalen);
#endif /* SDTEST */

#if defined(DHD_TX_DUMP) || defined(DHD_8021X_DUMP)
	dump_data = PKTDATA(osh, pkt);
	dump_data += 4; /* skip 4 bytes header */
	protocol = (dump_data[12] << 8) | dump_data[13];

	if (protocol == ETHER_TYPE_802_1X) {
		DHD_ERROR(("ETHER_TYPE_802_1X [TX]: ver %d, type %d, replay %d\n",
			dump_data[14], dump_data[15], dump_data[30]));
	}
#endif /* DHD_TX_DUMP || DHD_8021X_DUMP */

#if defined(DHD_TX_DUMP) && defined(DHD_TX_FULL_DUMP)
	{
		int i;
		DHD_ERROR(("TX DUMP\n"));

		for (i = 0; i < (datalen - 4); i++) {
			DHD_ERROR(("%02X ", dump_data[i]));
			if ((i & 15) == 15)
				printk("\n");
		}
		DHD_ERROR(("\n"));
	}
#endif /* DHD_TX_DUMP && DHD_TX_FULL_DUMP */

	prec = PRIO2PREC((PKTPRIO(pkt) & PRIOMASK));

	/* Check for existing queue, current flow-control, pending event, or pending clock */
	if (dhd_deferred_tx || bus->fcstate || pktq_len(&bus->txq) || bus->dpc_sched ||
	    (!DATAOK(bus)) || (bus->flowcontrol & NBITVAL(prec)) ||
	    (bus->clkstate != CLK_AVAIL)) {
		bool deq_ret;
		int pkq_len;

		DHD_TRACE(("%s: deferring pktq len %d\n", __FUNCTION__, pktq_len(&bus->txq)));
		bus->fcqueued++;

		/* Priority based enq */
		dhd_os_sdlock_txq(bus->dhd);
		deq_ret = dhd_prec_enq(bus->dhd, &bus->txq, pkt, prec);
		dhd_os_sdunlock_txq(bus->dhd);

		if (!deq_ret) {
#ifdef PROP_TXSTATUS
			if (DHD_PKTTAG_WLFCPKT(PKTTAG(pkt)) == 0)
#endif /* PROP_TXSTATUS */
			{
#ifdef DHDTCPACK_SUPPRESS
				if (dhd_tcpack_check_xmit(bus->dhd, pkt) == BCME_ERROR) {
					DHD_ERROR(("%s %d: tcpack_suppress ERROR!!! Stop using\n",
						__FUNCTION__, __LINE__));
					dhd_tcpack_suppress_set(bus->dhd, TCPACK_SUP_OFF);
				}
#endif /* DHDTCPACK_SUPPRESS */
				dhd_txcomplete(bus->dhd, pkt, FALSE);
				PKTFREE(osh, pkt, TRUE);
			}
			ret = BCME_NORESOURCE;
		} else
			ret = BCME_OK;

		dhd_os_sdlock_txq(bus->dhd);
		pkq_len = pktq_len(&bus->txq);
		dhd_os_sdunlock_txq(bus->dhd);
		if (pkq_len >= FCHI) {
			bool wlfc_enabled = FALSE;
#ifdef PROP_TXSTATUS
			wlfc_enabled = (dhd_wlfc_flowcontrol(bus->dhd, ON, FALSE) !=
				WLFC_UNSUPPORTED);
#endif
			if (!wlfc_enabled && dhd_doflow) {
				dhd_txflowcontrol(bus->dhd, ALL_INTERFACES, ON);
			}
		}

#ifdef DHD_DEBUG
		dhd_os_sdlock_txq(bus->dhd);
		if (pktq_plen(&bus->txq, prec) > qcount[prec])
			qcount[prec] = pktq_plen(&bus->txq, prec);
		dhd_os_sdunlock_txq(bus->dhd);
#endif

		/* Schedule DPC if needed to send queued packet(s) */
		if (dhd_deferred_tx && !bus->dpc_sched) {
			bus->dpc_sched = TRUE;
			dhd_sched_dpc(bus->dhd);
		}
	} else {
		int chan = SDPCM_DATA_CHANNEL;

#ifdef SDTEST
		chan = (bus->ext_loop ? SDPCM_TEST_CHANNEL : SDPCM_DATA_CHANNEL);
#endif
		/* Lock: we're about to use shared data/code (and SDIO) */
		dhd_os_sdlock(bus->dhd);

		/* Otherwise, send it now */
		BUS_WAKE(bus);
		/* Make sure back plane ht clk is on, no pending allowed */
		dhdsdio_clkctl(bus, CLK_AVAIL, TRUE);

		ret = dhdsdio_txpkt(bus, chan, &pkt, 1, TRUE);

		if (ret != BCME_OK)
			bus->dhd->tx_errors++;
		else
			bus->dhd->dstats.tx_bytes += datalen;

		if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
			bus->activity = FALSE;
			dhdsdio_clkctl(bus, CLK_NONE, TRUE);
		}

		dhd_os_sdunlock(bus->dhd);
	}

	return ret;
}

/* align packet data pointer and packet length to n-byte boundary, process packet headers,
 * a new packet may be allocated if there is not enough head and/or tail from for padding.
 * the caller is responsible for updating the glom size in the head packet (when glom is
 * used)
 *
 * pad_pkt_len: returns the length of extra padding needed from the padding packet, this parameter
 * is taken in tx glom mode only
 *
 * new_pkt: out, pointer of the new packet allocated due to insufficient head room for alignment
 * padding, NULL if not needed, the caller is responsible for freeing the new packet
 *
 * return: positive value - length of the packet, including head and tail padding
 *		   negative value - errors
 */
static int dhdsdio_txpkt_preprocess(dhd_bus_t *bus, void *pkt, int chan, int txseq,
	int prev_chain_total_len, bool last_chained_pkt,
	int *pad_pkt_len, void **new_pkt)
{
	osl_t *osh;
	uint8 *frame;
	int pkt_len;
	int modulo;
	int head_padding;
	int tail_padding = 0;
	uint32 swheader;
	uint32 swhdr_offset;
	bool alloc_new_pkt = FALSE;
	uint8 sdpcm_hdrlen = bus->txglom_enable ? SDPCM_HDRLEN_TXGLOM : SDPCM_HDRLEN;

	*new_pkt = NULL;
	osh = bus->dhd->osh;

#ifdef DHDTCPACK_SUPPRESS
	if (dhd_tcpack_check_xmit(bus->dhd, pkt) == BCME_ERROR) {
		DHD_ERROR(("%s %d: tcpack_suppress ERROR!!! Stop using it\n",
			__FUNCTION__, __LINE__));
		dhd_tcpack_suppress_set(bus->dhd, TCPACK_SUP_OFF);
	}
#endif /* DHDTCPACK_SUPPRESS */

	/* Add space for the SDPCM hardware/software headers */
	PKTPUSH(osh, pkt, sdpcm_hdrlen);
	ASSERT(ISALIGNED((uintptr)PKTDATA(osh, pkt), 2));

	frame = (uint8*)PKTDATA(osh, pkt);
	pkt_len = (uint16)PKTLEN(osh, pkt);

#ifdef WLMEDIA_HTSF
	frame = (uint8*)PKTDATA(osh, pkt);
	if (PKTLEN(osh, pkt) >= 100) {
		htsf_ts = (htsfts_t*) (frame + HTSF_HOSTOFFSET + 12);
		if (htsf_ts->magic == HTSFMAGIC) {
			htsf_ts->c20 = get_cycles();
			htsf_ts->t20 = dhd_get_htsf(bus->dhd->info, 0);
		}
	}
#endif /* WLMEDIA_HTSF */
#ifdef DHD_DEBUG
	if (PKTPRIO(pkt) < ARRAYSIZE(tx_packets))
		tx_packets[PKTPRIO(pkt)]++;
#endif /* DHD_DEBUG */

	/* align the data pointer, allocate a new packet if there is not enough space (new
	 * packet data pointer will be aligned thus no padding will be needed)
	 */
	head_padding = (ulong)frame % DHD_SDALIGN;
	if (PKTHEADROOM(osh, pkt) < head_padding) {
		head_padding = 0;
		alloc_new_pkt = TRUE;
	} else {
		uint cur_chain_total_len;
		int chain_tail_padding = 0;

		/* All packets need to be aligned by DHD_SDALIGN */
		modulo = (pkt_len + head_padding) % DHD_SDALIGN;
		tail_padding = modulo > 0 ? (DHD_SDALIGN - modulo) : 0;

		/* Total pkt chain length needs to be aligned by block size,
		 * unless it is a single pkt chain with total length less than one block size,
		 * which we prefer sending by byte mode.
		 *
		 * Do the chain alignment here if
		 * 1. This is the last pkt of the chain of multiple pkts or a single pkt.
		 * 2-1. This chain is of multiple pkts, or
		 * 2-2. This is a single pkt whose size is longer than one block size.
		 */
		cur_chain_total_len = prev_chain_total_len +
			(head_padding + pkt_len + tail_padding);
		if (last_chained_pkt && bus->blocksize != 0 &&
			(cur_chain_total_len > (int)bus->blocksize || prev_chain_total_len > 0)) {
			modulo = cur_chain_total_len % bus->blocksize;
			chain_tail_padding = modulo > 0 ? (bus->blocksize - modulo) : 0;
		}

#ifdef DHDENABLE_TAILPAD
		if (PKTTAILROOM(osh, pkt) < tail_padding) {
			/* We don't have tail room to align by DHD_SDALIGN */
			alloc_new_pkt = TRUE;
			bus->tx_tailpad_pktget++;
		} else if (PKTTAILROOM(osh, pkt) < tail_padding + chain_tail_padding) {
			/* We have tail room for tail_padding of this pkt itself, but not for
			 * total pkt chain alignment by block size.
			 * Use the padding packet to avoid memory copy if applicable,
			 * otherwise, just allocate a new pkt.
			 */
			if (bus->pad_pkt) {
				*pad_pkt_len = chain_tail_padding;
				bus->tx_tailpad_chain++;
			} else {
				alloc_new_pkt = TRUE;
				bus->tx_tailpad_pktget++;
			}
		} else
		/* This last pkt's tailroom is sufficient to hold both tail_padding
		 * of the pkt itself and chain_tail_padding of total pkt chain
		 */
#endif /* DHDENABLE_TAILPAD */
		tail_padding += chain_tail_padding;
	}

	DHD_INFO(("%s sdhdr len + orig_pkt_len %d h_pad %d t_pad %d pad_pkt_len %d\n",
		__FUNCTION__, pkt_len, head_padding, tail_padding, *pad_pkt_len));

	if (alloc_new_pkt) {
		void *tmp_pkt;
		int newpkt_size;
		int cur_total_len;

		ASSERT(*pad_pkt_len == 0);

		DHD_INFO(("%s allocating new packet for padding\n", __FUNCTION__));

		/* head pointer is aligned now, no padding needed */
		head_padding = 0;

		/* update the tail padding as it depends on the head padding, since a new packet is
		 * allocated, the head padding is non longer needed and packet length is chagned
		 */

		cur_total_len = prev_chain_total_len + pkt_len;
		if (last_chained_pkt && bus->blocksize != 0 &&
			(cur_total_len > (int)bus->blocksize || prev_chain_total_len > 0)) {
			modulo = cur_total_len % bus->blocksize;
			tail_padding = modulo > 0 ? (bus->blocksize - modulo) : 0;
		}
		else {
			modulo = pkt_len % DHD_SDALIGN;
			tail_padding = modulo > 0 ? (DHD_SDALIGN - modulo) : 0;
		}

		newpkt_size = PKTLEN(osh, pkt) + bus->blocksize + DHD_SDALIGN;
		bus->dhd->tx_realloc++;
		tmp_pkt = PKTGET(osh, newpkt_size, TRUE);
		if (tmp_pkt == NULL) {
			DHD_ERROR(("failed to alloc new %d byte packet\n", newpkt_size));
			return BCME_NOMEM;
		}
		PKTALIGN(osh, tmp_pkt, PKTLEN(osh, pkt), DHD_SDALIGN);
		bcopy(PKTDATA(osh, pkt), PKTDATA(osh, tmp_pkt), PKTLEN(osh, pkt));
		*new_pkt = tmp_pkt;
		pkt = tmp_pkt;
	}

	if (head_padding)
		PKTPUSH(osh, pkt, head_padding);

	frame = (uint8*)PKTDATA(osh, pkt);
	bzero(frame, head_padding + sdpcm_hdrlen);
	pkt_len = (uint16)PKTLEN(osh, pkt);

	/* the header has the followming format
	 * 4-byte HW frame tag: length, ~length (for glom this is the total length)
	 *
	 * 8-byte HW extesion flags (glom mode only) as the following:
	 *			2-byte packet length, excluding HW tag and padding
	 *			2-byte frame channel and frame flags (e.g. next frame following)
	 *			2-byte header length
	 *			2-byte tail padding size
	 *
	 * 8-byte SW frame tags as the following
	 *			4-byte flags: host tx seq, channel, data offset
	 *			4-byte flags: TBD
	 */

	swhdr_offset = SDPCM_FRAMETAG_LEN;

	/* hardware frame tag:
	 *
	 * in tx-glom mode, dongle only checks the hardware frame tag in the first
	 * packet and sees it as the total lenght of the glom (including tail padding),
	 * for each packet in the glom, the packet length needs to be updated, (see
	 * below PKTSETLEN)
	 *
	 * in non tx-glom mode, PKTLEN still need to include tail padding as to be
	 * referred to in sdioh_request_buffer(). The tail length will be excluded in
	 * dhdsdio_txpkt_postprocess().
	 */
	*(uint16*)frame = (uint16)htol16(pkt_len);
	*(((uint16*)frame) + 1) = (uint16)htol16(~pkt_len);
	pkt_len += tail_padding;

	/* hardware extesion flags */
	if (bus->txglom_enable) {
		uint32 hwheader1;
		uint32 hwheader2;

		swhdr_offset += SDPCM_HWEXT_LEN;
		hwheader1 = (pkt_len - SDPCM_FRAMETAG_LEN - tail_padding) |
			(last_chained_pkt << 24);
		hwheader2 = (tail_padding) << 16;
		htol32_ua_store(hwheader1, frame + SDPCM_FRAMETAG_LEN);
		htol32_ua_store(hwheader2, frame + SDPCM_FRAMETAG_LEN + 4);
	}
	PKTSETLEN((osh), (pkt), (pkt_len));

	/* software frame tags */
	swheader = ((chan << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK)
		| (txseq % SDPCM_SEQUENCE_WRAP) |
		(((head_padding + sdpcm_hdrlen) << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK);
	htol32_ua_store(swheader, frame + swhdr_offset);
	htol32_ua_store(0, frame + swhdr_offset + sizeof(swheader));

	return pkt_len;
}

static int dhdsdio_txpkt_postprocess(dhd_bus_t *bus, void *pkt)
{
	osl_t *osh;
	uint8 *frame;
	int data_offset;
	int tail_padding;
	int swhdr_offset = SDPCM_FRAMETAG_LEN + (bus->txglom_enable ? SDPCM_HWEXT_LEN : 0);

	(void)osh;
	osh = bus->dhd->osh;

	/* restore pkt buffer pointer, but keeps the header pushed by dhd_prot_hdrpush */
	frame = (uint8*)PKTDATA(osh, pkt);

	DHD_INFO(("%s PKTLEN before postprocess %d",
		__FUNCTION__, PKTLEN(osh, pkt)));

	/* PKTLEN still includes tail_padding, so exclude it.
	 * We shall have head_padding + original pkt_len for PKTLEN afterwards.
	 */
	if (bus->txglom_enable) {
		/* txglom pkts have tail_padding length in HW ext header */
		tail_padding = ltoh32_ua(frame + SDPCM_FRAMETAG_LEN + 4) >> 16;
		PKTSETLEN(osh, pkt, PKTLEN(osh, pkt) - tail_padding);
		DHD_INFO((" txglom pkt: tail_padding %d PKTLEN %d\n",
			tail_padding, PKTLEN(osh, pkt)));
	} else {
		/* non-txglom pkts have head_padding + original pkt length in HW frame tag.
		 * We cannot refer to this field for txglom pkts as the first pkt of the chain will
		 * have the field for the total length of the chain.
		 */
		PKTSETLEN(osh, pkt, *(uint16*)frame);
		DHD_INFO((" non-txglom pkt: HW frame tag len %d after PKTLEN %d\n",
			*(uint16*)frame, PKTLEN(osh, pkt)));
	}

	data_offset = ltoh32_ua(frame + swhdr_offset);
	data_offset = (data_offset & SDPCM_DOFFSET_MASK) >> SDPCM_DOFFSET_SHIFT;
	/* Get rid of sdpcm header + head_padding */
	PKTPULL(osh, pkt, data_offset);

	DHD_INFO(("%s data_offset %d, PKTLEN %d\n",
		__FUNCTION__, data_offset, PKTLEN(osh, pkt)));

	return BCME_OK;
}

static int dhdsdio_txpkt(dhd_bus_t *bus, uint chan, void** pkts, int num_pkt, bool free_pkt)
{
	int i;
	int ret = 0;
	osl_t *osh;
	bcmsdh_info_t *sdh;
	void *pkt = NULL;
	void *pkt_chain;
	int total_len = 0;
	void *head_pkt = NULL;
	void *prev_pkt = NULL;
	int pad_pkt_len = 0;
	int new_pkt_num = 0;
	void *new_pkts[MAX_TX_PKTCHAIN_CNT];
	bool wlfc_enabled = FALSE;

	if (bus->dhd->dongle_reset)
		return BCME_NOTREADY;

	sdh = bus->sdh;
	osh = bus->dhd->osh;
	/* init new_pkts[0] to make some compiler happy, not necessary as we check new_pkt_num */
	new_pkts[0] = NULL;

	for (i = 0; i < num_pkt; i++) {
		int pkt_len;
		bool last_pkt;
		void *new_pkt = NULL;

		pkt = pkts[i];
		ASSERT(pkt);
		last_pkt = (i == num_pkt - 1);
		pkt_len = dhdsdio_txpkt_preprocess(bus, pkt, chan, bus->tx_seq + i,
			total_len, last_pkt, &pad_pkt_len, &new_pkt);
		if (pkt_len <= 0)
			goto done;
		if (new_pkt) {
			pkt = new_pkt;
			new_pkts[new_pkt_num++] = new_pkt;
		}
		total_len += pkt_len;

		PKTSETNEXT(osh, pkt, NULL);
		/* insert the packet into the list */
		head_pkt ? PKTSETNEXT(osh, prev_pkt, pkt) : (head_pkt = pkt);
		prev_pkt = pkt;

	}

	/* Update the HW frame tag (total length) in the first pkt of the glom */
	if (bus->txglom_enable) {
		uint8 *frame;

		total_len += pad_pkt_len;
		frame = (uint8*)PKTDATA(osh, head_pkt);
		*(uint16*)frame = (uint16)htol16(total_len);
		*(((uint16*)frame) + 1) = (uint16)htol16(~total_len);

	}

	/* if a padding packet if needed, insert it to the end of the link list */
	if (pad_pkt_len) {
		PKTSETLEN(osh, bus->pad_pkt, pad_pkt_len);
		PKTSETNEXT(osh, pkt, bus->pad_pkt);
	}

	/* dhd_bcmsdh_send_buf ignores the buffer pointer if he packet
	 * parameter is not NULL, for non packet chian we pass NULL pkt pointer
	 * so it will take the aligned length and buffer pointer.
	 */
	pkt_chain = PKTNEXT(osh, head_pkt) ? head_pkt : NULL;
	ret = dhd_bcmsdh_send_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
		PKTDATA(osh, head_pkt), total_len, pkt_chain, NULL, NULL, TXRETRIES);
	if (ret == BCME_OK)
		bus->tx_seq = (bus->tx_seq + num_pkt) % SDPCM_SEQUENCE_WRAP;

	/* if a padding packet was needed, remove it from the link list as it not a data pkt */
	if (pad_pkt_len && pkt)
		PKTSETNEXT(osh, pkt, NULL);

done:
	pkt = head_pkt;
	while (pkt) {
		void *pkt_next = PKTNEXT(osh, pkt);
		PKTSETNEXT(osh, pkt, NULL);
		dhdsdio_txpkt_postprocess(bus, pkt);
		pkt = pkt_next;
	}

	/* new packets might be allocated due to insufficient room for padding, but we
	 * still have to indicate the original packets to upper layer
	 */
	for (i = 0; i < num_pkt; i++) {
		pkt = pkts[i];
		wlfc_enabled = FALSE;
#ifdef PROP_TXSTATUS
		if (DHD_PKTTAG_WLFCPKT(PKTTAG(pkt))) {
			wlfc_enabled = (dhd_wlfc_txcomplete(bus->dhd, pkt, ret == 0) !=
				WLFC_UNSUPPORTED);
		}
#endif /* PROP_TXSTATUS */
		if (!wlfc_enabled) {
			PKTSETNEXT(osh, pkt, NULL);
			dhd_txcomplete(bus->dhd, pkt, ret != 0);
			if (free_pkt)
				PKTFREE(osh, pkt, TRUE);
		}
	}
	for (i = 0; i < new_pkt_num; i++)
		PKTFREE(osh, new_pkts[i], TRUE);

	return ret;
}

static uint
dhdsdio_sendfromq(dhd_bus_t *bus, uint maxframes)
{
	uint cnt = 0;
	uint8 tx_prec_map;
	uint16 txpktqlen = 0;
	uint32 intstatus = 0;
	uint retries = 0;
	osl_t *osh;
	uint datalen = 0;
	dhd_pub_t *dhd = bus->dhd;
	sdpcmd_regs_t *regs = bus->regs;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!KSO_ENAB(bus)) {
		DHD_ERROR(("%s: Device asleep\n", __FUNCTION__));
		return BCME_NODEVICE;
	}

	osh = dhd->osh;
	tx_prec_map = ~bus->flowcontrol;
	for (cnt = 0; (cnt < maxframes) && DATAOK(bus);) {
		int i;
		int num_pkt = 1;
		void *pkts[MAX_TX_PKTCHAIN_CNT];
		int prec_out;

		dhd_os_sdlock_txq(bus->dhd);
		if (bus->txglom_enable) {
			num_pkt = MIN((uint32)DATABUFCNT(bus), (uint32)bus->txglomsize);
			num_pkt = MIN(num_pkt, ARRAYSIZE(pkts));
		}
		num_pkt = MIN(num_pkt, pktq_mlen(&bus->txq, tx_prec_map));
		for (i = 0; i < num_pkt; i++) {
			pkts[i] = pktq_mdeq(&bus->txq, ~bus->flowcontrol, &prec_out);
			datalen += PKTLEN(osh, pkts[i]);
		}
		dhd_os_sdunlock_txq(bus->dhd);

		if (i == 0)
			break;
		if (dhdsdio_txpkt(bus, SDPCM_DATA_CHANNEL, pkts, i, TRUE) != BCME_OK)
			dhd->tx_errors++;
		else
			dhd->dstats.tx_bytes += datalen;
		cnt += i;

		/* In poll mode, need to check for other events */
		if (!bus->intr && cnt)
		{
			/* Check device status, signal pending interrupt */
			R_SDREG(intstatus, &regs->intstatus, retries);
			bus->f2txdata++;
			if (bcmsdh_regfail(bus->sdh))
				break;
			if (intstatus & bus->hostintmask)
				bus->ipend = TRUE;
		}

	}

	dhd_os_sdlock_txq(bus->dhd);
	txpktqlen = pktq_len(&bus->txq);
	dhd_os_sdunlock_txq(bus->dhd);

	/* Do flow-control if needed */
	if (dhd->up && (dhd->busstate == DHD_BUS_DATA) && (txpktqlen < FCLOW)) {
		bool wlfc_enabled = FALSE;
#ifdef PROP_TXSTATUS
		wlfc_enabled = (dhd_wlfc_flowcontrol(dhd, OFF, TRUE) != WLFC_UNSUPPORTED);
#endif
		if (!wlfc_enabled && dhd_doflow && dhd->txoff) {
			dhd_txflowcontrol(dhd, ALL_INTERFACES, OFF);
		}
	}

	return cnt;
}

static void
dhdsdio_sendpendctl(dhd_bus_t *bus)
{
	bcmsdh_info_t *sdh = bus->sdh;
	int ret;
	uint8* frame_seq = bus->ctrl_frame_buf + SDPCM_FRAMETAG_LEN;

	if (bus->txglom_enable)
		frame_seq += SDPCM_HWEXT_LEN;

	if (*frame_seq != bus->tx_seq) {
		DHD_INFO(("%s IOCTL frame seq lag detected!"
			" frm_seq:%d != bus->tx_seq:%d, corrected\n",
			__FUNCTION__, *frame_seq, bus->tx_seq));
		*frame_seq = bus->tx_seq;
	}

	ret = dhd_bcmsdh_send_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
		(uint8 *)bus->ctrl_frame_buf, (uint32)bus->ctrl_frame_len,
		NULL, NULL, NULL, 1);
	if (ret == BCME_OK)
		bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;

	bus->ctrl_frame_stat = FALSE;
	dhd_wait_event_wakeup(bus->dhd);
}

int
dhd_bus_txctl(struct dhd_bus *bus, uchar *msg, uint msglen)
{
	static int err_nodevice = 0;
	uint8 *frame;
	uint16 len;
	uint32 swheader;
	bcmsdh_info_t *sdh = bus->sdh;
	uint8 doff = 0;
	int ret = -1;
	uint8 sdpcm_hdrlen = bus->txglom_enable ? SDPCM_HDRLEN_TXGLOM : SDPCM_HDRLEN;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd->dongle_reset)
		return -EIO;

	/* Back the pointer to make a room for bus header */
	frame = msg - sdpcm_hdrlen;
	len = (msglen += sdpcm_hdrlen);

	/* Add alignment padding (optional for ctl frames) */
	if (dhd_alignctl) {
		if ((doff = ((uintptr)frame % DHD_SDALIGN))) {
			frame -= doff;
			len += doff;
			msglen += doff;
			bzero(frame, doff + sdpcm_hdrlen);
		}
		ASSERT(doff < DHD_SDALIGN);
	}
	doff += sdpcm_hdrlen;

	/* Round send length to next SDIO block */
	if (bus->roundup && bus->blocksize && (len > bus->blocksize)) {
		uint16 pad = bus->blocksize - (len % bus->blocksize);
		if ((pad <= bus->roundup) && (pad < bus->blocksize))
			len += pad;
	} else if (len % DHD_SDALIGN) {
		len += DHD_SDALIGN - (len % DHD_SDALIGN);
	}

	/* Satisfy length-alignment requirements */
	if (forcealign && (len & (ALIGNMENT - 1)))
		len = ROUNDUP(len, ALIGNMENT);

	ASSERT(ISALIGNED((uintptr)frame, 2));


	/* Need to lock here to protect txseq and SDIO tx calls */
	dhd_os_sdlock(bus->dhd);

	BUS_WAKE(bus);

	/* Make sure backplane clock is on */
	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

	/* Hardware tag: 2 byte len followed by 2 byte ~len check (all LE) */
	*(uint16*)frame = htol16((uint16)msglen);
	*(((uint16*)frame) + 1) = htol16(~msglen);

	if (bus->txglom_enable) {
		uint32 hwheader1, hwheader2;
		/* Software tag: channel, sequence number, data offset */
		swheader = ((SDPCM_CONTROL_CHANNEL << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK)
				| bus->tx_seq
				| ((doff << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK);
		htol32_ua_store(swheader, frame + SDPCM_FRAMETAG_LEN + SDPCM_HWEXT_LEN);
		htol32_ua_store(0, frame + SDPCM_FRAMETAG_LEN
			+ SDPCM_HWEXT_LEN + sizeof(swheader));

		hwheader1 = (msglen - SDPCM_FRAMETAG_LEN) | (1 << 24);
		hwheader2 = (len - (msglen)) << 16;
		htol32_ua_store(hwheader1, frame + SDPCM_FRAMETAG_LEN);
		htol32_ua_store(hwheader2, frame + SDPCM_FRAMETAG_LEN + 4);

		*(uint16*)frame = htol16(len);
		*(((uint16*)frame) + 1) = htol16(~(len));
	} else {
		/* Software tag: channel, sequence number, data offset */
		swheader = ((SDPCM_CONTROL_CHANNEL << SDPCM_CHANNEL_SHIFT) & SDPCM_CHANNEL_MASK)
		        | bus->tx_seq | ((doff << SDPCM_DOFFSET_SHIFT) & SDPCM_DOFFSET_MASK);
		htol32_ua_store(swheader, frame + SDPCM_FRAMETAG_LEN);
		htol32_ua_store(0, frame + SDPCM_FRAMETAG_LEN + sizeof(swheader));
	}
	if (!TXCTLOK(bus)) {
		DHD_INFO(("%s: No bus credit bus->tx_max %d, bus->tx_seq %d\n",
			__FUNCTION__, bus->tx_max, bus->tx_seq));
		bus->ctrl_frame_stat = TRUE;
		/* Send from dpc */
		bus->ctrl_frame_buf = frame;
		bus->ctrl_frame_len = len;

		if (!bus->dpc_sched) {
			bus->dpc_sched = TRUE;
			dhd_sched_dpc(bus->dhd);
		}
		if (bus->ctrl_frame_stat) {
			dhd_wait_for_event(bus->dhd, &bus->ctrl_frame_stat);
		}

		if (bus->ctrl_frame_stat == FALSE) {
			DHD_INFO(("%s: ctrl_frame_stat == FALSE\n", __FUNCTION__));
			ret = 0;
		} else {
			bus->dhd->txcnt_timeout++;
			if (!bus->dhd->hang_was_sent) {
				DHD_ERROR(("%s: ctrl_frame_stat == TRUE txcnt_timeout=%d\n",
					__FUNCTION__, bus->dhd->txcnt_timeout));
			}
			ret = -1;
			bus->ctrl_frame_stat = FALSE;
			goto done;
		}
	}

	bus->dhd->txcnt_timeout = 0;
	bus->ctrl_frame_stat = TRUE;

	if (ret == -1) {
#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() && DHD_CTL_ON()) {
			prhex("Tx Frame", frame, len);
		} else if (DHD_HDRS_ON()) {
			prhex("TxHdr", frame, MIN(len, 16));
		}
#endif
		ret = dhd_bcmsdh_send_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
		                          frame, len, NULL, NULL, NULL, TXRETRIES);
		if (ret == BCME_OK)
			bus->tx_seq = (bus->tx_seq + 1) % SDPCM_SEQUENCE_WRAP;
	}
	bus->ctrl_frame_stat = FALSE;

done:
	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
		bus->activity = FALSE;
		dhdsdio_clkctl(bus, CLK_NONE, TRUE);
	}

	dhd_os_sdunlock(bus->dhd);

	if (ret)
		bus->dhd->tx_ctlerrs++;
	else
		bus->dhd->tx_ctlpkts++;

	if (bus->dhd->txcnt_timeout >= MAX_CNTL_TX_TIMEOUT)
		return -ETIMEDOUT;

	if (ret == BCME_NODEVICE)
		err_nodevice++;
	else
		err_nodevice = 0;

	return ret ? err_nodevice >= ERROR_BCME_NODEVICE_MAX ? -ETIMEDOUT : -EIO : 0;
}

int
dhd_bus_rxctl(struct dhd_bus *bus, uchar *msg, uint msglen)
{
	int timeleft;
	uint rxlen = 0;
	bool pending;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd->dongle_reset)
		return -EIO;

	/* Wait until control frame is available */
	timeleft = dhd_os_ioctl_resp_wait(bus->dhd, &bus->rxlen, &pending);

	dhd_os_sdlock(bus->dhd);
	rxlen = bus->rxlen;
	bcopy(bus->rxctl, msg, MIN(msglen, rxlen));
	bus->rxlen = 0;
	dhd_os_sdunlock(bus->dhd);

	if (rxlen) {
		DHD_CTL(("%s: resumed on rxctl frame, got %d expected %d\n",
			__FUNCTION__, rxlen, msglen));
	} else if (timeleft == 0) {
#ifdef DHD_DEBUG
		uint32 status, retry = 0;
		R_SDREG(status, &bus->regs->intstatus, retry);
		DHD_ERROR(("%s: resumed on timeout, INT status=0x%08X\n",
			__FUNCTION__, status));
#else
		DHD_ERROR(("%s: resumed on timeout\n", __FUNCTION__));
#endif /* DHD_DEBUG */
#ifdef DHD_DEBUG
			dhd_os_sdlock(bus->dhd);
			dhdsdio_checkdied(bus, NULL, 0);
			dhd_os_sdunlock(bus->dhd);
#endif /* DHD_DEBUG */
	} else if (pending == TRUE) {
		/* signal pending */
		DHD_ERROR(("%s: signal pending\n", __FUNCTION__));
		return -EINTR;

	} else {
		DHD_CTL(("%s: resumed for unknown reason?\n", __FUNCTION__));
#ifdef DHD_DEBUG
		dhd_os_sdlock(bus->dhd);
		dhdsdio_checkdied(bus, NULL, 0);
		dhd_os_sdunlock(bus->dhd);
#endif /* DHD_DEBUG */
	}
	if (timeleft == 0) {
		if (rxlen == 0)
			bus->dhd->rxcnt_timeout++;
		DHD_ERROR(("%s: rxcnt_timeout=%d, rxlen=%d\n", __FUNCTION__,
			bus->dhd->rxcnt_timeout, rxlen));
	}
	else
		bus->dhd->rxcnt_timeout = 0;

	if (rxlen)
		bus->dhd->rx_ctlpkts++;
	else
		bus->dhd->rx_ctlerrs++;

	if (bus->dhd->rxcnt_timeout >= MAX_CNTL_RX_TIMEOUT)
		return -ETIMEDOUT;

	if (bus->dhd->dongle_trap_occured)
		return -EREMOTEIO;

	return rxlen ? (int)rxlen : -EIO;
}

/* IOVar table */
enum {
	IOV_INTR = 1,
	IOV_POLLRATE,
	IOV_SDREG,
	IOV_SBREG,
	IOV_SDCIS,
	IOV_MEMBYTES,
	IOV_RAMSIZE,
	IOV_RAMSTART,
#ifdef DHD_DEBUG
	IOV_CHECKDIED,
	IOV_SERIALCONS,
#endif /* DHD_DEBUG */
	IOV_SET_DOWNLOAD_STATE,
	IOV_SOCRAM_STATE,
	IOV_FORCEEVEN,
	IOV_SDIOD_DRIVE,
	IOV_READAHEAD,
	IOV_SDRXCHAIN,
	IOV_ALIGNCTL,
	IOV_SDALIGN,
	IOV_DEVRESET,
	IOV_CPU,
#if defined(USE_SDIOFIFO_IOVAR)
	IOV_WATERMARK,
	IOV_MESBUSYCTRL,
#endif /* USE_SDIOFIFO_IOVAR */
#ifdef SDTEST
	IOV_PKTGEN,
	IOV_EXTLOOP,
#endif /* SDTEST */
	IOV_SPROM,
	IOV_TXBOUND,
	IOV_RXBOUND,
	IOV_TXMINMAX,
	IOV_IDLETIME,
	IOV_IDLECLOCK,
	IOV_SD1IDLE,
	IOV_SLEEP,
	IOV_DONGLEISOLATION,
	IOV_KSO,
	IOV_DEVSLEEP,
	IOV_DEVCAP,
	IOV_VARS,
#ifdef SOFTAP
	IOV_FWPATH,
#endif
	IOV_TXGLOMSIZE,
	IOV_TXGLOMMODE,
	IOV_HANGREPORT,
	IOV_TXINRX_THRES
};

const bcm_iovar_t dhdsdio_iovars[] = {
	{"intr",	IOV_INTR,	0,	IOVT_BOOL,	0 },
	{"sleep",	IOV_SLEEP,	0,	IOVT_BOOL,	0 },
	{"pollrate",	IOV_POLLRATE,	0,	IOVT_UINT32,	0 },
	{"idletime",	IOV_IDLETIME,	0,	IOVT_INT32,	0 },
	{"idleclock",	IOV_IDLECLOCK,	0,	IOVT_INT32,	0 },
	{"sd1idle",	IOV_SD1IDLE,	0,	IOVT_BOOL,	0 },
	{"membytes",	IOV_MEMBYTES,	0,	IOVT_BUFFER,	2 * sizeof(int) },
	{"ramsize",	IOV_RAMSIZE,	0,	IOVT_UINT32,	0 },
	{"ramstart",	IOV_RAMSTART,	0,	IOVT_UINT32,	0 },
	{"dwnldstate",	IOV_SET_DOWNLOAD_STATE,	0,	IOVT_BOOL,	0 },
	{"socram_state",	IOV_SOCRAM_STATE,	0,	IOVT_BOOL,	0 },
	{"vars",	IOV_VARS,	0,	IOVT_BUFFER,	0 },
	{"sdiod_drive",	IOV_SDIOD_DRIVE, 0,	IOVT_UINT32,	0 },
	{"readahead",	IOV_READAHEAD,	0,	IOVT_BOOL,	0 },
	{"sdrxchain",	IOV_SDRXCHAIN,	0,	IOVT_BOOL,	0 },
	{"alignctl",	IOV_ALIGNCTL,	0,	IOVT_BOOL,	0 },
	{"sdalign",	IOV_SDALIGN,	0,	IOVT_BOOL,	0 },
	{"devreset",	IOV_DEVRESET,	0,	IOVT_BOOL,	0 },
#ifdef DHD_DEBUG
	{"sdreg",	IOV_SDREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t) },
	{"sbreg",	IOV_SBREG,	0,	IOVT_BUFFER,	sizeof(sdreg_t) },
	{"sd_cis",	IOV_SDCIS,	0,	IOVT_BUFFER,	DHD_IOCTL_MAXLEN },
	{"forcealign",	IOV_FORCEEVEN,	0,	IOVT_BOOL,	0 },
	{"txbound",	IOV_TXBOUND,	0,	IOVT_UINT32,	0 },
	{"rxbound",	IOV_RXBOUND,	0,	IOVT_UINT32,	0 },
	{"txminmax",	IOV_TXMINMAX,	0,	IOVT_UINT32,	0 },
	{"cpu",		IOV_CPU,	0,	IOVT_BOOL,	0 },
#ifdef DHD_DEBUG
	{"checkdied",	IOV_CHECKDIED,	0,	IOVT_BUFFER,	0 },
	{"serial",	IOV_SERIALCONS,	0,	IOVT_UINT32,	0 },
#endif /* DHD_DEBUG  */
#endif /* DHD_DEBUG */
#ifdef SDTEST
	{"extloop",	IOV_EXTLOOP,	0,	IOVT_BOOL,	0 },
	{"pktgen",	IOV_PKTGEN,	0,	IOVT_BUFFER,	sizeof(dhd_pktgen_t) },
#endif /* SDTEST */
#if defined(USE_SDIOFIFO_IOVAR)
	{"watermark",	IOV_WATERMARK,	0,	IOVT_UINT32,	0 },
	{"mesbusyctrl",	IOV_MESBUSYCTRL,	0,	IOVT_UINT32,	0 },
#endif /* USE_SDIOFIFO_IOVAR */
	{"devcap", IOV_DEVCAP,	0,	IOVT_UINT32,	0 },
	{"dngl_isolation", IOV_DONGLEISOLATION,	0,	IOVT_UINT32,	0 },
	{"kso",	IOV_KSO,	0,	IOVT_UINT32,	0 },
	{"devsleep", IOV_DEVSLEEP,	0,	IOVT_UINT32,	0 },
#ifdef SOFTAP
	{"fwpath", IOV_FWPATH, 0, IOVT_BUFFER, 0 },
#endif
	{"txglomsize", IOV_TXGLOMSIZE, 0, IOVT_UINT32, 0 },
	{"fw_hang_report", IOV_HANGREPORT, 0, IOVT_BOOL, 0 },
	{"txinrx_thres", IOV_TXINRX_THRES, 0, IOVT_INT32, 0 },
	{NULL, 0, 0, 0, 0 }
};

static void
dhd_dump_pct(struct bcmstrbuf *strbuf, char *desc, uint num, uint div)
{
	uint q1, q2;

	if (!div) {
		bcm_bprintf(strbuf, "%s N/A", desc);
	} else {
		q1 = num / div;
		q2 = (100 * (num - (q1 * div))) / div;
		bcm_bprintf(strbuf, "%s %d.%02d", desc, q1, q2);
	}
}

void
dhd_bus_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	dhd_bus_t *bus = dhdp->bus;

	bcm_bprintf(strbuf, "Bus SDIO structure:\n");
	bcm_bprintf(strbuf, "hostintmask 0x%08x intstatus 0x%08x sdpcm_ver %d\n",
	            bus->hostintmask, bus->intstatus, bus->sdpcm_ver);
	bcm_bprintf(strbuf, "fcstate %d qlen %u tx_seq %d, max %d, rxskip %d rxlen %u rx_seq %d\n",
	            bus->fcstate, pktq_len(&bus->txq), bus->tx_seq, bus->tx_max, bus->rxskip,
	            bus->rxlen, bus->rx_seq);
	bcm_bprintf(strbuf, "intr %d intrcount %u lastintrs %u spurious %u\n",
	            bus->intr, bus->intrcount, bus->lastintrs, bus->spurious);
	bcm_bprintf(strbuf, "pollrate %u pollcnt %u regfails %u\n",
	            bus->pollrate, bus->pollcnt, bus->regfails);

	bcm_bprintf(strbuf, "\nAdditional counters:\n");
#ifdef DHDENABLE_TAILPAD
	bcm_bprintf(strbuf, "tx_tailpad_chain %u tx_tailpad_pktget %u\n",
	            bus->tx_tailpad_chain, bus->tx_tailpad_pktget);
#endif
	bcm_bprintf(strbuf, "tx_sderrs %u fcqueued %u rxrtx %u rx_toolong %u rxc_errors %u\n",
	            bus->tx_sderrs, bus->fcqueued, bus->rxrtx, bus->rx_toolong,
	            bus->rxc_errors);
	bcm_bprintf(strbuf, "rx_hdrfail %u badhdr %u badseq %u\n",
	            bus->rx_hdrfail, bus->rx_badhdr, bus->rx_badseq);
	bcm_bprintf(strbuf, "fc_rcvd %u, fc_xoff %u, fc_xon %u\n",
	            bus->fc_rcvd, bus->fc_xoff, bus->fc_xon);
	bcm_bprintf(strbuf, "rxglomfail %u, rxglomframes %u, rxglompkts %u\n",
	            bus->rxglomfail, bus->rxglomframes, bus->rxglompkts);
	bcm_bprintf(strbuf, "f2rx (hdrs/data) %u (%u/%u), f2tx %u f1regs %u\n",
	            (bus->f2rxhdrs + bus->f2rxdata), bus->f2rxhdrs, bus->f2rxdata,
	            bus->f2txdata, bus->f1regdata);
	{
		dhd_dump_pct(strbuf, "\nRx: pkts/f2rd", bus->dhd->rx_packets,
		             (bus->f2rxhdrs + bus->f2rxdata));
		dhd_dump_pct(strbuf, ", pkts/f1sd", bus->dhd->rx_packets, bus->f1regdata);
		dhd_dump_pct(strbuf, ", pkts/sd", bus->dhd->rx_packets,
		             (bus->f2rxhdrs + bus->f2rxdata + bus->f1regdata));
		dhd_dump_pct(strbuf, ", pkts/int", bus->dhd->rx_packets, bus->intrcount);
		bcm_bprintf(strbuf, "\n");

		dhd_dump_pct(strbuf, "Rx: glom pct", (100 * bus->rxglompkts),
		             bus->dhd->rx_packets);
		dhd_dump_pct(strbuf, ", pkts/glom", bus->rxglompkts, bus->rxglomframes);
		bcm_bprintf(strbuf, "\n");

		dhd_dump_pct(strbuf, "Tx: pkts/f2wr", bus->dhd->tx_packets, bus->f2txdata);
		dhd_dump_pct(strbuf, ", pkts/f1sd", bus->dhd->tx_packets, bus->f1regdata);
		dhd_dump_pct(strbuf, ", pkts/sd", bus->dhd->tx_packets,
		             (bus->f2txdata + bus->f1regdata));
		dhd_dump_pct(strbuf, ", pkts/int", bus->dhd->tx_packets, bus->intrcount);
		bcm_bprintf(strbuf, "\n");

		dhd_dump_pct(strbuf, "Total: pkts/f2rw",
		             (bus->dhd->tx_packets + bus->dhd->rx_packets),
		             (bus->f2txdata + bus->f2rxhdrs + bus->f2rxdata));
		dhd_dump_pct(strbuf, ", pkts/f1sd",
		             (bus->dhd->tx_packets + bus->dhd->rx_packets), bus->f1regdata);
		dhd_dump_pct(strbuf, ", pkts/sd",
		             (bus->dhd->tx_packets + bus->dhd->rx_packets),
		             (bus->f2txdata + bus->f2rxhdrs + bus->f2rxdata + bus->f1regdata));
		dhd_dump_pct(strbuf, ", pkts/int",
		             (bus->dhd->tx_packets + bus->dhd->rx_packets), bus->intrcount);
		bcm_bprintf(strbuf, "\n\n");
	}

#ifdef SDTEST
	if (bus->pktgen_count) {
		bcm_bprintf(strbuf, "pktgen config and count:\n");
		bcm_bprintf(strbuf, "freq %u count %u print %u total %u min %u len %u\n",
		            bus->pktgen_freq, bus->pktgen_count, bus->pktgen_print,
		            bus->pktgen_total, bus->pktgen_minlen, bus->pktgen_maxlen);
		bcm_bprintf(strbuf, "send attempts %u rcvd %u fail %u\n",
		            bus->pktgen_sent, bus->pktgen_rcvd, bus->pktgen_fail);
	}
#endif /* SDTEST */
#ifdef DHD_DEBUG
	bcm_bprintf(strbuf, "dpc_sched %d host interrupt%spending\n",
	            bus->dpc_sched, (bcmsdh_intr_pending(bus->sdh) ? " " : " not "));
	bcm_bprintf(strbuf, "blocksize %u roundup %u\n", bus->blocksize, bus->roundup);
#endif /* DHD_DEBUG */
	bcm_bprintf(strbuf, "clkstate %d activity %d idletime %d idlecount %d sleeping %d\n",
	            bus->clkstate, bus->activity, bus->idletime, bus->idlecount, bus->sleeping);
}

void
dhd_bus_clearcounts(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = (dhd_bus_t *)dhdp->bus;

	bus->intrcount = bus->lastintrs = bus->spurious = bus->regfails = 0;
	bus->rxrtx = bus->rx_toolong = bus->rxc_errors = 0;
	bus->rx_hdrfail = bus->rx_badhdr = bus->rx_badseq = 0;
#ifdef DHDENABLE_TAILPAD
	bus->tx_tailpad_chain = bus->tx_tailpad_pktget = 0;
#endif
	bus->tx_sderrs = bus->fc_rcvd = bus->fc_xoff = bus->fc_xon = 0;
	bus->rxglomfail = bus->rxglomframes = bus->rxglompkts = 0;
	bus->f2rxhdrs = bus->f2rxdata = bus->f2txdata = bus->f1regdata = 0;
}

#ifdef SDTEST
static int
dhdsdio_pktgen_get(dhd_bus_t *bus, uint8 *arg)
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

	bcopy(&pktgen, arg, sizeof(pktgen));

	return 0;
}

static int
dhdsdio_pktgen_set(dhd_bus_t *bus, uint8 *arg)
{
	dhd_pktgen_t pktgen;
	uint oldcnt, oldmode;

	bcopy(arg, &pktgen, sizeof(pktgen));
	if (pktgen.version != DHD_PKTGEN_VERSION)
		return BCME_BADARG;

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
	bus->pktgen_prev_time = jiffies;
	bus->pktgen_len = MAX(bus->pktgen_len, bus->pktgen_minlen);
	bus->pktgen_len = MIN(bus->pktgen_len, bus->pktgen_maxlen);

	/* Clear counts for a new pktgen (mode change, or was stopped) */
	if (bus->pktgen_count && (!oldcnt || oldmode != bus->pktgen_mode)) {
		bus->pktgen_sent = bus->pktgen_prev_sent = bus->pktgen_rcvd = 0;
		bus->pktgen_prev_rcvd = bus->pktgen_fail = 0;
	}

	return 0;
}
#endif /* SDTEST */

static void
dhdsdio_devram_remap(dhd_bus_t *bus, bool val)
{
	uint8 enable, protect, remap;

	si_socdevram(bus->sih, FALSE, &enable, &protect, &remap);
	remap = val ? TRUE : FALSE;
	si_socdevram(bus->sih, TRUE, &enable, &protect, &remap);
}

static int
dhdsdio_membytes(dhd_bus_t *bus, bool write, uint32 address, uint8 *data, uint size)
{
	int bcmerror = 0;
	uint32 sdaddr;
	uint dsize;

	/* In remap mode, adjust address beyond socram and redirect
	 * to devram at SOCDEVRAM_BP_ADDR since remap address > orig_ramsize
	 * is not backplane accessible
	 */
	if (REMAP_ENAB(bus) && REMAP_ISADDR(bus, address)) {
		address -= bus->orig_ramsize;
		address += SOCDEVRAM_BP_ADDR;
	}

	/* Determine initial transfer parameters */
	sdaddr = address & SBSDIO_SB_OFT_ADDR_MASK;
	if ((sdaddr + size) & SBSDIO_SBWINDOW_MASK)
		dsize = (SBSDIO_SB_OFT_ADDR_LIMIT - sdaddr);
	else
		dsize = size;

	/* Set the backplane window to include the start address */
	if ((bcmerror = dhdsdio_set_siaddr_window(bus, address))) {
		DHD_ERROR(("%s: window change failed\n", __FUNCTION__));
		goto xfer_done;
	}

	/* Do the transfer(s) */
	while (size) {
		DHD_INFO(("%s: %s %d bytes at offset 0x%08x in window 0x%08x\n",
		          __FUNCTION__, (write ? "write" : "read"), dsize, sdaddr,
		          (address & SBSDIO_SBWINDOW_MASK)));
		if ((bcmerror = bcmsdh_rwdata(bus->sdh, write, sdaddr, data, dsize))) {
			DHD_ERROR(("%s: membytes transfer failed\n", __FUNCTION__));
			break;
		}

		/* Adjust for next transfer (if any) */
		if ((size -= dsize)) {
			data += dsize;
			address += dsize;
			if ((bcmerror = dhdsdio_set_siaddr_window(bus, address))) {
				DHD_ERROR(("%s: window change failed\n", __FUNCTION__));
				break;
			}
			sdaddr = 0;
			dsize = MIN(SBSDIO_SB_OFT_ADDR_LIMIT, size);
		}

	}

xfer_done:
	/* Return the window to backplane enumeration space for core access */
	if (dhdsdio_set_siaddr_window(bus, bcmsdh_cur_sbwad(bus->sdh))) {
		DHD_ERROR(("%s: FAILED to set window back to 0x%x\n", __FUNCTION__,
			bcmsdh_cur_sbwad(bus->sdh)));
	}

	return bcmerror;
}

#ifdef DHD_DEBUG
static int
dhdsdio_readshared(dhd_bus_t *bus, sdpcm_shared_t *sh)
{
	uint32 addr;
	int rv, i;
	uint32 shaddr = 0;

	if (CHIPID(bus->sih->chip) == BCM43430_CHIP_ID && !dhdsdio_sr_cap(bus))
		bus->srmemsize = 0;

	shaddr = bus->dongle_ram_base + bus->ramsize - 4;
	i = 0;
	do {
		/* Read last word in memory to determine address of sdpcm_shared structure */
		if ((rv = dhdsdio_membytes(bus, FALSE, shaddr, (uint8 *)&addr, 4)) < 0)
			return rv;

		addr = ltoh32(addr);

		DHD_INFO(("sdpcm_shared address 0x%08X\n", addr));

		/*
		 * Check if addr is valid.
		 * NVRAM length at the end of memory should have been overwritten.
		 */
		if (addr == 0 || ((~addr >> 16) & 0xffff) == (addr & 0xffff)) {
			if ((bus->srmemsize > 0) && (i++ == 0)) {
				shaddr -= bus->srmemsize;
			} else {
				DHD_ERROR(("%s: address (0x%08x) of sdpcm_shared invalid\n",
					__FUNCTION__, addr));
				return BCME_ERROR;
			}
		} else
			break;
	} while (i < 2);

	/* Read hndrte_shared structure */
	if ((rv = dhdsdio_membytes(bus, FALSE, addr, (uint8 *)sh, sizeof(sdpcm_shared_t))) < 0)
		return rv;

	/* Endianness */
	sh->flags = ltoh32(sh->flags);
	sh->trap_addr = ltoh32(sh->trap_addr);
	sh->assert_exp_addr = ltoh32(sh->assert_exp_addr);
	sh->assert_file_addr = ltoh32(sh->assert_file_addr);
	sh->assert_line = ltoh32(sh->assert_line);
	sh->console_addr = ltoh32(sh->console_addr);
	sh->msgtrace_addr = ltoh32(sh->msgtrace_addr);

	if ((sh->flags & SDPCM_SHARED_VERSION_MASK) == 3 && SDPCM_SHARED_VERSION == 1)
		return BCME_OK;

	if ((sh->flags & SDPCM_SHARED_VERSION_MASK) != SDPCM_SHARED_VERSION) {
		DHD_ERROR(("%s: sdpcm_shared version %d in dhd "
		           "is different than sdpcm_shared version %d in dongle\n",
		           __FUNCTION__, SDPCM_SHARED_VERSION,
		           sh->flags & SDPCM_SHARED_VERSION_MASK));
		return BCME_ERROR;
	}

	return BCME_OK;
}

#define CONSOLE_LINE_MAX	192

static int
dhdsdio_readconsole(dhd_bus_t *bus)
{
	dhd_console_t *c = &bus->console;
	uint8 line[CONSOLE_LINE_MAX], ch;
	uint32 n, idx, addr;
	int rv;

	/* Don't do anything until FWREADY updates console address */
	if (bus->console_addr == 0)
		return 0;

	if (!KSO_ENAB(bus))
		return 0;

	/* Read console log struct */
	addr = bus->console_addr + OFFSETOF(hndrte_cons_t, log);
	if ((rv = dhdsdio_membytes(bus, FALSE, addr, (uint8 *)&c->log, sizeof(c->log))) < 0)
		return rv;

	/* Allocate console buffer (one time only) */
	if (c->buf == NULL) {
		c->bufsize = ltoh32(c->log.buf_size);
		if ((c->buf = MALLOC(bus->dhd->osh, c->bufsize)) == NULL)
			return BCME_NOMEM;
	}

	idx = ltoh32(c->log.idx);

	/* Protect against corrupt value */
	if (idx > c->bufsize)
		return BCME_ERROR;

	/* Skip reading the console buffer if the index pointer has not moved */
	if (idx == c->last)
		return BCME_OK;

	/* Read the console buffer */
	addr = ltoh32(c->log.buf);
	if ((rv = dhdsdio_membytes(bus, FALSE, addr, c->buf, c->bufsize)) < 0)
		return rv;

	while (c->last != idx) {
		for (n = 0; n < CONSOLE_LINE_MAX - 2; n++) {
			if (c->last == idx) {
				/* This would output a partial line.  Instead, back up
				 * the buffer pointer and output this line next time around.
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
			printf("CONSOLE: %s\n", line);
#ifdef LOG_INTO_TCPDUMP
			dhd_sendup_log(bus->dhd, line, n);
#endif /* LOG_INTO_TCPDUMP */
		}
	}
break2:

	return BCME_OK;
}

static int
dhdsdio_checkdied(dhd_bus_t *bus, char *data, uint size)
{
	int bcmerror = 0;
	uint msize = 512;
	char *mbuffer = NULL;
	char *console_buffer = NULL;
	uint maxstrlen = 256;
	char *str = NULL;
	trap_t tr;
	sdpcm_shared_t sdpcm_shared;
	struct bcmstrbuf strbuf;
	uint32 console_ptr, console_size, console_index;
	uint8 line[CONSOLE_LINE_MAX], ch;
	uint32 n, i, addr;
	int rv;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (DHD_NOCHECKDIED_ON())
		return 0;

	if (data == NULL) {
		/*
		 * Called after a rx ctrl timeout. "data" is NULL.
		 * allocate memory to trace the trap or assert.
		 */
		size = msize;
		mbuffer = data = MALLOC(bus->dhd->osh, msize);
		if (mbuffer == NULL) {
			DHD_ERROR(("%s: MALLOC(%d) failed \n", __FUNCTION__, msize));
			bcmerror = BCME_NOMEM;
			goto done;
		}
	}

	if ((str = MALLOC(bus->dhd->osh, maxstrlen)) == NULL) {
		DHD_ERROR(("%s: MALLOC(%d) failed \n", __FUNCTION__, maxstrlen));
		bcmerror = BCME_NOMEM;
		goto done;
	}

	if ((bcmerror = dhdsdio_readshared(bus, &sdpcm_shared)) < 0)
		goto done;

	bcm_binit(&strbuf, data, size);

	bcm_bprintf(&strbuf, "msgtrace address : 0x%08X\nconsole address  : 0x%08X\n",
	            sdpcm_shared.msgtrace_addr, sdpcm_shared.console_addr);

	if ((sdpcm_shared.flags & SDPCM_SHARED_ASSERT_BUILT) == 0) {
		/* NOTE: Misspelled assert is intentional - DO NOT FIX.
		 * (Avoids conflict with real asserts for programmatic parsing of output.)
		 */
		bcm_bprintf(&strbuf, "Assrt not built in dongle\n");
	}

	if ((sdpcm_shared.flags & (SDPCM_SHARED_ASSERT|SDPCM_SHARED_TRAP)) == 0) {
		/* NOTE: Misspelled assert is intentional - DO NOT FIX.
		 * (Avoids conflict with real asserts for programmatic parsing of output.)
		 */
		bcm_bprintf(&strbuf, "No trap%s in dongle",
		          (sdpcm_shared.flags & SDPCM_SHARED_ASSERT_BUILT)
		          ?"/assrt" :"");
	} else {
		if (sdpcm_shared.flags & SDPCM_SHARED_ASSERT) {
			/* Download assert */
			bcm_bprintf(&strbuf, "Dongle assert");
			if (sdpcm_shared.assert_exp_addr != 0) {
				str[0] = '\0';
				if ((bcmerror = dhdsdio_membytes(bus, FALSE,
				                                 sdpcm_shared.assert_exp_addr,
				                                 (uint8 *)str, maxstrlen)) < 0)
					goto done;

				str[maxstrlen - 1] = '\0';
				bcm_bprintf(&strbuf, " expr \"%s\"", str);
			}

			if (sdpcm_shared.assert_file_addr != 0) {
				str[0] = '\0';
				if ((bcmerror = dhdsdio_membytes(bus, FALSE,
				                                 sdpcm_shared.assert_file_addr,
				                                 (uint8 *)str, maxstrlen)) < 0)
					goto done;

				str[maxstrlen - 1] = '\0';
				bcm_bprintf(&strbuf, " file \"%s\"", str);
			}

			bcm_bprintf(&strbuf, " line %d ", sdpcm_shared.assert_line);
		}

		if (sdpcm_shared.flags & SDPCM_SHARED_TRAP) {
			bus->dhd->dongle_trap_occured = TRUE;
			if ((bcmerror = dhdsdio_membytes(bus, FALSE,
			                                 sdpcm_shared.trap_addr,
			                                 (uint8*)&tr, sizeof(trap_t))) < 0)
				goto done;

			bcm_bprintf(&strbuf,
			"Dongle trap type 0x%x @ epc 0x%x, cpsr 0x%x, spsr 0x%x, sp 0x%x,"
			            "lp 0x%x, rpc 0x%x Trap offset 0x%x, "
			"r0 0x%x, r1 0x%x, r2 0x%x, r3 0x%x, "
			"r4 0x%x, r5 0x%x, r6 0x%x, r7 0x%x\n\n",
			ltoh32(tr.type), ltoh32(tr.epc), ltoh32(tr.cpsr), ltoh32(tr.spsr),
			ltoh32(tr.r13), ltoh32(tr.r14), ltoh32(tr.pc),
			ltoh32(sdpcm_shared.trap_addr),
			ltoh32(tr.r0), ltoh32(tr.r1), ltoh32(tr.r2), ltoh32(tr.r3),
			ltoh32(tr.r4), ltoh32(tr.r5), ltoh32(tr.r6), ltoh32(tr.r7));

			addr = sdpcm_shared.console_addr + OFFSETOF(hndrte_cons_t, log);
			if ((rv = dhdsdio_membytes(bus, FALSE, addr,
				(uint8 *)&console_ptr, sizeof(console_ptr))) < 0)
				goto printbuf;

			addr = sdpcm_shared.console_addr + OFFSETOF(hndrte_cons_t, log.buf_size);
			if ((rv = dhdsdio_membytes(bus, FALSE, addr,
				(uint8 *)&console_size, sizeof(console_size))) < 0)
				goto printbuf;

			addr = sdpcm_shared.console_addr + OFFSETOF(hndrte_cons_t, log.idx);
			if ((rv = dhdsdio_membytes(bus, FALSE, addr,
				(uint8 *)&console_index, sizeof(console_index))) < 0)
				goto printbuf;

			console_ptr = ltoh32(console_ptr);
			console_size = ltoh32(console_size);
			console_index = ltoh32(console_index);

			if (console_size > CONSOLE_BUFFER_MAX ||
				!(console_buffer = MALLOC(bus->dhd->osh, console_size)))
				goto printbuf;

			if ((rv = dhdsdio_membytes(bus, FALSE, console_ptr,
				(uint8 *)console_buffer, console_size)) < 0)
				goto printbuf;

			for (i = 0, n = 0; i < console_size; i += n + 1) {
				for (n = 0; n < CONSOLE_LINE_MAX - 2; n++) {
					ch = console_buffer[(console_index + i + n) % console_size];
					if (ch == '\n')
						break;
					line[n] = ch;
				}


				if (n > 0) {
					if (line[n - 1] == '\r')
						n--;
					line[n] = 0;
					/* Don't use DHD_ERROR macro since we print
					 * a lot of information quickly. The macro
					 * will truncate a lot of the printfs
					 */

					if (dhd_msg_level & DHD_ERROR_VAL)
						printf("CONSOLE: %s\n", line);
				}
			}
		}
	}

printbuf:
	if (sdpcm_shared.flags & (SDPCM_SHARED_ASSERT | SDPCM_SHARED_TRAP)) {
		DHD_ERROR(("%s: %s\n", __FUNCTION__, strbuf.origbuf));
	}


done:
	if (mbuffer)
		MFREE(bus->dhd->osh, mbuffer, msize);
	if (str)
		MFREE(bus->dhd->osh, str, maxstrlen);
	if (console_buffer)
		MFREE(bus->dhd->osh, console_buffer, console_size);

	return bcmerror;
}
#endif /* #ifdef DHD_DEBUG */


int
dhdsdio_downloadvars(dhd_bus_t *bus, void *arg, int len)
{
	int bcmerror = BCME_OK;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* Basic sanity checks */
	if (bus->dhd->up) {
		bcmerror = BCME_NOTDOWN;
		goto err;
	}
	if (!len) {
		bcmerror = BCME_BUFTOOSHORT;
		goto err;
	}

	/* Free the old ones and replace with passed variables */
	if (bus->vars)
		MFREE(bus->dhd->osh, bus->vars, bus->varsz);

	bus->vars = MALLOC(bus->dhd->osh, len);
	bus->varsz = bus->vars ? len : 0;
	if (bus->vars == NULL) {
		bcmerror = BCME_NOMEM;
		goto err;
	}

	/* Copy the passed variables, which should include the terminating double-null */
	bcopy(arg, bus->vars, bus->varsz);
err:
	return bcmerror;
}

#ifdef DHD_DEBUG

#define CC_PLL_CHIPCTRL_SERIAL_ENAB		(1  << 24)
#define CC_CHIPCTRL_JTAG_SEL			(1  << 3)
#define CC_CHIPCTRL_GPIO_SEL				(0x3)
#define CC_PLL_CHIPCTRL_SERIAL_ENAB_4334	(1  << 28)

static int
dhd_serialconsole(dhd_bus_t *bus, bool set, bool enable, int *bcmerror)
{
	int int_val;
	uint32 addr, data, uart_enab = 0;
	uint32 jtag_sel = CC_CHIPCTRL_JTAG_SEL;
	uint32 gpio_sel = CC_CHIPCTRL_GPIO_SEL;

	addr = SI_ENUM_BASE + OFFSETOF(chipcregs_t, chipcontrol_addr);
	data = SI_ENUM_BASE + OFFSETOF(chipcregs_t, chipcontrol_data);
	*bcmerror = 0;

	bcmsdh_reg_write(bus->sdh, addr, 4, 1);
	if (bcmsdh_regfail(bus->sdh)) {
		*bcmerror = BCME_SDIO_ERROR;
		return -1;
	}
	int_val = bcmsdh_reg_read(bus->sdh, data, 4);
	if (bcmsdh_regfail(bus->sdh)) {
		*bcmerror = BCME_SDIO_ERROR;
		return -1;
	}
	if (bus->sih->chip == BCM4330_CHIP_ID) {
		uart_enab = CC_PLL_CHIPCTRL_SERIAL_ENAB;
	}
	else if (bus->sih->chip == BCM4334_CHIP_ID ||
		bus->sih->chip == BCM43340_CHIP_ID ||
		bus->sih->chip == BCM43341_CHIP_ID ||
		bus->sih->chip == BCM43342_CHIP_ID ||
		0) {
		if (enable) {
			/* Moved to PMU chipcontrol 1 from 4330 */
			int_val &= ~gpio_sel;
			int_val |= jtag_sel;
		} else {
			int_val |= gpio_sel;
			int_val &= ~jtag_sel;
		}
		uart_enab = CC_PLL_CHIPCTRL_SERIAL_ENAB_4334;
	}

	if (!set)
		return (int_val & uart_enab);
	if (enable)
		int_val |= uart_enab;
	else
		int_val &= ~uart_enab;
	bcmsdh_reg_write(bus->sdh, data, 4, int_val);
	if (bcmsdh_regfail(bus->sdh)) {
		*bcmerror = BCME_SDIO_ERROR;
		return -1;
	}
	if (bus->sih->chip == BCM4330_CHIP_ID) {
		uint32 chipcontrol;
		addr = SI_ENUM_BASE + OFFSETOF(chipcregs_t, chipcontrol);
		chipcontrol = bcmsdh_reg_read(bus->sdh, addr, 4);
		chipcontrol &= ~jtag_sel;
		if (enable) {
			chipcontrol |=  jtag_sel;
			chipcontrol &= ~gpio_sel;
		}
		bcmsdh_reg_write(bus->sdh, addr, 4, chipcontrol);
	}

	return (int_val & uart_enab);
}
#endif 

static int
dhdsdio_doiovar(dhd_bus_t *bus, const bcm_iovar_t *vi, uint32 actionid, const char *name,
                void *params, int plen, void *arg, int len, int val_size)
{
	int bcmerror = 0;
	int32 int_val = 0;
	bool bool_val = 0;

	DHD_TRACE(("%s: Enter, action %d name %s params %p plen %d arg %p len %d val_size %d\n",
	           __FUNCTION__, actionid, name, params, plen, arg, len, val_size));

	if ((bcmerror = bcm_iovar_lencheck(vi, arg, len, IOV_ISSET(actionid))) != 0)
		goto exit;

	if (plen >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	bool_val = (int_val != 0) ? TRUE : FALSE;


	/* Some ioctls use the bus */
	dhd_os_sdlock(bus->dhd);

	/* Check if dongle is in reset. If so, only allow DEVRESET iovars */
	if (bus->dhd->dongle_reset && !(actionid == IOV_SVAL(IOV_DEVRESET) ||
	                                actionid == IOV_GVAL(IOV_DEVRESET))) {
		bcmerror = BCME_NOTREADY;
		goto exit;
	}

	/*
	 * Special handling for keepSdioOn: New SDIO Wake-up Mechanism
	 */
	if ((vi->varid == IOV_KSO) && (IOV_ISSET(actionid))) {
		dhdsdio_clk_kso_iovar(bus, bool_val);
		goto exit;
	} else if ((vi->varid == IOV_DEVSLEEP) && (IOV_ISSET(actionid))) {
		{
			dhdsdio_clk_devsleep_iovar(bus, bool_val);
			if (!SLPAUTO_ENAB(bus) && (bool_val == FALSE) && (bus->ipend)) {
				DHD_ERROR(("INT pending in devsleep 1, dpc_sched: %d\n",
					bus->dpc_sched));
				if (!bus->dpc_sched) {
					bus->dpc_sched = TRUE;
					dhd_sched_dpc(bus->dhd);
				}
			}
		}
		goto exit;
	}

	/* Handle sleep stuff before any clock mucking */
	if (vi->varid == IOV_SLEEP) {
		if (IOV_ISSET(actionid)) {
			bcmerror = dhdsdio_bussleep(bus, bool_val);
		} else {
			int_val = (int32)bus->sleeping;
			bcopy(&int_val, arg, val_size);
		}
		goto exit;
	}

	/* Request clock to allow SDIO accesses */
	if (!bus->dhd->dongle_reset) {
		BUS_WAKE(bus);
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
	}

	switch (actionid) {
	case IOV_GVAL(IOV_INTR):
		int_val = (int32)bus->intr;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_INTR):
		bus->intr = bool_val;
		bus->intdis = FALSE;
		if (bus->dhd->up) {
			if (bus->intr) {
				DHD_INTR(("%s: enable SDIO device interrupts\n", __FUNCTION__));
				bcmsdh_intr_enable(bus->sdh);
			} else {
				DHD_INTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
				bcmsdh_intr_disable(bus->sdh);
			}
		}
		break;

	case IOV_GVAL(IOV_POLLRATE):
		int_val = (int32)bus->pollrate;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_POLLRATE):
		bus->pollrate = (uint)int_val;
		bus->poll = (bus->pollrate != 0);
		break;

	case IOV_GVAL(IOV_IDLETIME):
		int_val = bus->idletime;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_IDLETIME):
		if ((int_val < 0) && (int_val != DHD_IDLE_IMMEDIATE)) {
			bcmerror = BCME_BADARG;
		} else {
			bus->idletime = int_val;
		}
		break;

	case IOV_GVAL(IOV_IDLECLOCK):
		int_val = (int32)bus->idleclock;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_IDLECLOCK):
		bus->idleclock = int_val;
		break;

	case IOV_GVAL(IOV_SD1IDLE):
		int_val = (int32)sd1idle;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SD1IDLE):
		sd1idle = bool_val;
		break;


	case IOV_SVAL(IOV_MEMBYTES):
	case IOV_GVAL(IOV_MEMBYTES):
	{
		uint32 address;
		uint size, dsize;
		uint8 *data;

		bool set = (actionid == IOV_SVAL(IOV_MEMBYTES));

		ASSERT(plen >= 2*sizeof(int));

		address = (uint32)int_val;
		bcopy((char *)params + sizeof(int_val), &int_val, sizeof(int_val));
		size = (uint)int_val;

		/* Do some validation */
		dsize = set ? plen - (2 * sizeof(int)) : len;
		if (dsize < size) {
			DHD_ERROR(("%s: error on %s membytes, addr 0x%08x size %d dsize %d\n",
			           __FUNCTION__, (set ? "set" : "get"), address, size, dsize));
			bcmerror = BCME_BADARG;
			break;
		}

		DHD_INFO(("%s: Request to %s %d bytes at address 0x%08x\n", __FUNCTION__,
		          (set ? "write" : "read"), size, address));

		/* check if CR4 */
		if (si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
			/*
			 * If address is start of RAM (i.e. a downloaded image),
			 * store the reset instruction to be written in 0
			 */
			if (set && address == bus->dongle_ram_base) {
				bus->resetinstr = *(((uint32*)params) + 2);
			}
		} else {
		/* If we know about SOCRAM, check for a fit */
		if ((bus->orig_ramsize) &&
		    ((address > bus->orig_ramsize) || (address + size > bus->orig_ramsize)))
		{
			uint8 enable, protect, remap;
			si_socdevram(bus->sih, FALSE, &enable, &protect, &remap);
			if (!enable || protect) {
				DHD_ERROR(("%s: ramsize 0x%08x doesn't have %d bytes at 0x%08x\n",
					__FUNCTION__, bus->orig_ramsize, size, address));
				DHD_ERROR(("%s: socram enable %d, protect %d\n",
					__FUNCTION__, enable, protect));
				bcmerror = BCME_BADARG;
				break;
			}

			if (!REMAP_ENAB(bus) && (address >= SOCDEVRAM_ARM_ADDR)) {
				uint32 devramsize = si_socdevram_size(bus->sih);
				if ((address < SOCDEVRAM_ARM_ADDR) ||
					(address + size > (SOCDEVRAM_ARM_ADDR + devramsize))) {
					DHD_ERROR(("%s: bad address 0x%08x, size 0x%08x\n",
						__FUNCTION__, address, size));
					DHD_ERROR(("%s: socram range 0x%08x,size 0x%08x\n",
						__FUNCTION__, SOCDEVRAM_ARM_ADDR, devramsize));
					bcmerror = BCME_BADARG;
					break;
				}
				/* move it such that address is real now */
				address -= SOCDEVRAM_ARM_ADDR;
				address += SOCDEVRAM_BP_ADDR;
				DHD_INFO(("%s: Request to %s %d bytes @ Mapped address 0x%08x\n",
					__FUNCTION__, (set ? "write" : "read"), size, address));
			} else if (REMAP_ENAB(bus) && REMAP_ISADDR(bus, address) && remap) {
				/* Can not access remap region while devram remap bit is set
				 * ROM content would be returned in this case
				 */
				DHD_ERROR(("%s: Need to disable remap for address 0x%08x\n",
					__FUNCTION__, address));
				bcmerror = BCME_ERROR;
				break;
			}
		}
		}

		/* Generate the actual data pointer */
		data = set ? (uint8*)params + 2 * sizeof(int): (uint8*)arg;

		/* Call to do the transfer */
		bcmerror = dhdsdio_membytes(bus, set, address, data, size);

		break;
	}

	case IOV_GVAL(IOV_RAMSIZE):
		int_val = (int32)bus->ramsize;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_RAMSTART):
		int_val = (int32)bus->dongle_ram_base;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_SDIOD_DRIVE):
		int_val = (int32)dhd_sdiod_drive_strength;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SDIOD_DRIVE):
		dhd_sdiod_drive_strength = int_val;
		si_sdiod_drive_strength_init(bus->sih, bus->dhd->osh, dhd_sdiod_drive_strength);
		break;

	case IOV_SVAL(IOV_SET_DOWNLOAD_STATE):
		bcmerror = dhdsdio_download_state(bus, bool_val);
		break;

	case IOV_SVAL(IOV_SOCRAM_STATE):
		bcmerror = dhdsdio_download_state(bus, bool_val);
		break;

	case IOV_SVAL(IOV_VARS):
		bcmerror = dhdsdio_downloadvars(bus, arg, len);
		break;

	case IOV_GVAL(IOV_READAHEAD):
		int_val = (int32)dhd_readahead;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_READAHEAD):
		if (bool_val && !dhd_readahead)
			bus->nextlen = 0;
		dhd_readahead = bool_val;
		break;

	case IOV_GVAL(IOV_SDRXCHAIN):
		int_val = (int32)bus->use_rxchain;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SDRXCHAIN):
		if (bool_val && !bus->sd_rxchain)
			bcmerror = BCME_UNSUPPORTED;
		else
			bus->use_rxchain = bool_val;
		break;
	case IOV_GVAL(IOV_ALIGNCTL):
		int_val = (int32)dhd_alignctl;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_ALIGNCTL):
		dhd_alignctl = bool_val;
		break;

	case IOV_GVAL(IOV_SDALIGN):
		int_val = DHD_SDALIGN;
		bcopy(&int_val, arg, val_size);
		break;

#ifdef DHD_DEBUG
	case IOV_GVAL(IOV_VARS):
		if (bus->varsz < (uint)len)
			bcopy(bus->vars, arg, bus->varsz);
		else
			bcmerror = BCME_BUFTOOSHORT;
		break;
#endif /* DHD_DEBUG */

#ifdef DHD_DEBUG
	case IOV_GVAL(IOV_SDREG):
	{
		sdreg_t *sd_ptr;
		uint32 addr, size;

		sd_ptr = (sdreg_t *)params;

		addr = (ulong)bus->regs + sd_ptr->offset;
		size = sd_ptr->func;
		int_val = (int32)bcmsdh_reg_read(bus->sdh, addr, size);
		if (bcmsdh_regfail(bus->sdh))
			bcmerror = BCME_SDIO_ERROR;
		bcopy(&int_val, arg, sizeof(int32));
		break;
	}

	case IOV_SVAL(IOV_SDREG):
	{
		sdreg_t *sd_ptr;
		uint32 addr, size;

		sd_ptr = (sdreg_t *)params;

		addr = (ulong)bus->regs + sd_ptr->offset;
		size = sd_ptr->func;
		bcmsdh_reg_write(bus->sdh, addr, size, sd_ptr->value);
		if (bcmsdh_regfail(bus->sdh))
			bcmerror = BCME_SDIO_ERROR;
		break;
	}

	/* Same as above, but offset is not backplane (not SDIO core) */
	case IOV_GVAL(IOV_SBREG):
	{
		sdreg_t sdreg;
		uint32 addr, size;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = SI_ENUM_BASE + sdreg.offset;
		size = sdreg.func;
		int_val = (int32)bcmsdh_reg_read(bus->sdh, addr, size);
		if (bcmsdh_regfail(bus->sdh))
			bcmerror = BCME_SDIO_ERROR;
		bcopy(&int_val, arg, sizeof(int32));
		break;
	}

	case IOV_SVAL(IOV_SBREG):
	{
		sdreg_t sdreg;
		uint32 addr, size;

		bcopy(params, &sdreg, sizeof(sdreg));

		addr = SI_ENUM_BASE + sdreg.offset;
		size = sdreg.func;
		bcmsdh_reg_write(bus->sdh, addr, size, sdreg.value);
		if (bcmsdh_regfail(bus->sdh))
			bcmerror = BCME_SDIO_ERROR;
		break;
	}

	case IOV_GVAL(IOV_SDCIS):
	{
		*(char *)arg = 0;

		bcmstrcat(arg, "\nFunc 0\n");
		bcmsdh_cis_read(bus->sdh, 0x10, (uint8 *)arg + strlen(arg), SBSDIO_CIS_SIZE_LIMIT);
		bcmstrcat(arg, "\nFunc 1\n");
		bcmsdh_cis_read(bus->sdh, 0x11, (uint8 *)arg + strlen(arg), SBSDIO_CIS_SIZE_LIMIT);
		bcmstrcat(arg, "\nFunc 2\n");
		bcmsdh_cis_read(bus->sdh, 0x12, (uint8 *)arg + strlen(arg), SBSDIO_CIS_SIZE_LIMIT);
		break;
	}

	case IOV_GVAL(IOV_FORCEEVEN):
		int_val = (int32)forcealign;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_FORCEEVEN):
		forcealign = bool_val;
		break;

	case IOV_GVAL(IOV_TXBOUND):
		int_val = (int32)dhd_txbound;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_TXBOUND):
		dhd_txbound = (uint)int_val;
		break;

	case IOV_GVAL(IOV_RXBOUND):
		int_val = (int32)dhd_rxbound;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_RXBOUND):
		dhd_rxbound = (uint)int_val;
		break;

	case IOV_GVAL(IOV_TXMINMAX):
		int_val = (int32)dhd_txminmax;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_TXMINMAX):
		dhd_txminmax = (uint)int_val;
		break;

	case IOV_GVAL(IOV_SERIALCONS):
		int_val = dhd_serialconsole(bus, FALSE, 0, &bcmerror);
		if (bcmerror != 0)
			break;

		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_SERIALCONS):
		dhd_serialconsole(bus, TRUE, bool_val, &bcmerror);
		break;


#endif /* DHD_DEBUG */


#ifdef SDTEST
	case IOV_GVAL(IOV_EXTLOOP):
		int_val = (int32)bus->ext_loop;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_EXTLOOP):
		bus->ext_loop = bool_val;
		break;

	case IOV_GVAL(IOV_PKTGEN):
		bcmerror = dhdsdio_pktgen_get(bus, arg);
		break;

	case IOV_SVAL(IOV_PKTGEN):
		bcmerror = dhdsdio_pktgen_set(bus, arg);
		break;
#endif /* SDTEST */

#if defined(USE_SDIOFIFO_IOVAR)
	case IOV_GVAL(IOV_WATERMARK):
		int_val = (int32)watermark;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_WATERMARK):
		watermark = (uint)int_val;
		watermark = (watermark > SBSDIO_WATERMARK_MASK) ? SBSDIO_WATERMARK_MASK : watermark;
		DHD_ERROR(("Setting watermark as 0x%x.\n", watermark));
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_WATERMARK, (uint8)watermark, NULL);
		break;

	case IOV_GVAL(IOV_MESBUSYCTRL):
		int_val = (int32)mesbusyctrl;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_MESBUSYCTRL):
		mesbusyctrl = (uint)int_val;
		mesbusyctrl = (mesbusyctrl > SBSDIO_MESBUSYCTRL_MASK)
			? SBSDIO_MESBUSYCTRL_MASK : mesbusyctrl;
		DHD_ERROR(("Setting mesbusyctrl as 0x%x.\n", mesbusyctrl));
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_MESBUSYCTRL,
			((uint8)mesbusyctrl | 0x80), NULL);
		break;
#endif 


	case IOV_GVAL(IOV_DONGLEISOLATION):
		int_val = bus->dhd->dongle_isolation;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DONGLEISOLATION):
		bus->dhd->dongle_isolation = bool_val;
		break;

	case IOV_SVAL(IOV_DEVRESET):
		DHD_TRACE(("%s: Called set IOV_DEVRESET=%d dongle_reset=%d busstate=%d\n",
		           __FUNCTION__, bool_val, bus->dhd->dongle_reset,
		           bus->dhd->busstate));

		ASSERT(bus->dhd->osh);
		/* ASSERT(bus->cl_devid); */

		dhd_bus_devreset(bus->dhd, (uint8)bool_val);

		break;
	/*
	 * softap firmware is updated through module parameter or android private command
	 */

	case IOV_GVAL(IOV_DEVRESET):
		DHD_TRACE(("%s: Called get IOV_DEVRESET\n", __FUNCTION__));

		/* Get its status */
		int_val = (bool) bus->dhd->dongle_reset;
		bcopy(&int_val, arg, val_size);

		break;

	case IOV_GVAL(IOV_KSO):
		int_val = dhdsdio_sleepcsr_get(bus);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_DEVCAP):
		int_val = dhdsdio_devcap_get(bus);
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_DEVCAP):
		dhdsdio_devcap_set(bus, (uint8) int_val);
		break;
	case IOV_GVAL(IOV_TXGLOMSIZE):
		int_val = (int32)bus->txglomsize;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_SVAL(IOV_TXGLOMSIZE):
		if (int_val > SDPCM_MAXGLOM_SIZE) {
			bcmerror = BCME_ERROR;
		} else {
			bus->txglomsize = (uint)int_val;
		}
		break;
	case IOV_SVAL(IOV_HANGREPORT):
		bus->dhd->hang_report = bool_val;
		DHD_ERROR(("%s: Set hang_report as %d\n", __FUNCTION__, bus->dhd->hang_report));
		break;

	case IOV_GVAL(IOV_HANGREPORT):
		int_val = (int32)bus->dhd->hang_report;
		bcopy(&int_val, arg, val_size);
		break;

	case IOV_GVAL(IOV_TXINRX_THRES):
		int_val = bus->txinrx_thres;
		bcopy(&int_val, arg, val_size);
		break;
	case IOV_SVAL(IOV_TXINRX_THRES):
		if (int_val < 0) {
			bcmerror = BCME_BADARG;
		} else {
			bus->txinrx_thres = int_val;
		}
		break;

	default:
		bcmerror = BCME_UNSUPPORTED;
		break;
	}

exit:
	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
		bus->activity = FALSE;
		dhdsdio_clkctl(bus, CLK_NONE, TRUE);
	}

	dhd_os_sdunlock(bus->dhd);

	return bcmerror;
}

static int
dhdsdio_write_vars(dhd_bus_t *bus)
{
	int bcmerror = 0;
	uint32 varsize, phys_size;
	uint32 varaddr;
	uint8 *vbuffer;
	uint32 varsizew;
#ifdef DHD_DEBUG
	uint8 *nvram_ularray;
#endif /* DHD_DEBUG */

	/* Even if there are no vars are to be written, we still need to set the ramsize. */
	varsize = bus->varsz ? ROUNDUP(bus->varsz, 4) : 0;
	varaddr = (bus->ramsize - 4) - varsize;

	varaddr += bus->dongle_ram_base;

	if (bus->vars) {
		if ((bus->sih->buscoretype == SDIOD_CORE_ID) && (bus->sdpcmrev == 7)) {
			if (((varaddr & 0x3C) == 0x3C) && (varsize > 4)) {
				DHD_ERROR(("PR85623WAR in place\n"));
				varsize += 4;
				varaddr -= 4;
			}
		}

		vbuffer = (uint8 *)MALLOC(bus->dhd->osh, varsize);
		if (!vbuffer)
			return BCME_NOMEM;

		bzero(vbuffer, varsize);
		bcopy(bus->vars, vbuffer, bus->varsz);

		/* Write the vars list */
		bcmerror = dhdsdio_membytes(bus, TRUE, varaddr, vbuffer, varsize);
#ifdef DHD_DEBUG
		/* Verify NVRAM bytes */
		DHD_INFO(("Compare NVRAM dl & ul; varsize=%d\n", varsize));
		nvram_ularray = (uint8*)MALLOC(bus->dhd->osh, varsize);
		if (!nvram_ularray) {
			MFREE(bus->dhd->osh, vbuffer, varsize);
			return BCME_NOMEM;
		}

		/* Upload image to verify downloaded contents. */
		memset(nvram_ularray, 0xaa, varsize);

		/* Read the vars list to temp buffer for comparison */
		bcmerror = dhdsdio_membytes(bus, FALSE, varaddr, nvram_ularray, varsize);
		if (bcmerror) {
				DHD_ERROR(("%s: error %d on reading %d nvram bytes at 0x%08x\n",
					__FUNCTION__, bcmerror, varsize, varaddr));
		}
		/* Compare the org NVRAM with the one read from RAM */
		if (memcmp(vbuffer, nvram_ularray, varsize)) {
			DHD_ERROR(("%s: Downloaded NVRAM image is corrupted.\n", __FUNCTION__));
		} else
			DHD_ERROR(("%s: Download, Upload and compare of NVRAM succeeded.\n",
			__FUNCTION__));

		MFREE(bus->dhd->osh, nvram_ularray, varsize);
#endif /* DHD_DEBUG */

		MFREE(bus->dhd->osh, vbuffer, varsize);
	}

	phys_size = REMAP_ENAB(bus) ? bus->ramsize : bus->orig_ramsize;

	phys_size += bus->dongle_ram_base;

	/* adjust to the user specified RAM */
	DHD_INFO(("Physical memory size: %d, usable memory size: %d\n",
		phys_size, bus->ramsize));
	DHD_INFO(("Vars are at %d, orig varsize is %d\n",
		varaddr, varsize));
	varsize = ((phys_size - 4) - varaddr);

	/*
	 * Determine the length token:
	 * Varsize, converted to words, in lower 16-bits, checksum in upper 16-bits.
	 */
	if (bcmerror) {
		varsizew = 0;
	} else {
		varsizew = varsize / 4;
		varsizew = (~varsizew << 16) | (varsizew & 0x0000FFFF);
		varsizew = htol32(varsizew);
	}

	DHD_INFO(("New varsize is %d, length token=0x%08x\n", varsize, varsizew));

	/* Write the length token to the last word */
	bcmerror = dhdsdio_membytes(bus, TRUE, (phys_size - 4),
		(uint8*)&varsizew, 4);

	return bcmerror;
}

static int
dhdsdio_download_state(dhd_bus_t *bus, bool enter)
{
	uint retries;
	int bcmerror = 0;
	int foundcr4 = 0;

	if (!bus->sih)
		return BCME_ERROR;
	/* To enter download state, disable ARM and reset SOCRAM.
	 * To exit download state, simply reset ARM (default is RAM boot).
	 */
	if (enter) {
		bus->alp_only = TRUE;

		if (!(si_setcore(bus->sih, ARM7S_CORE_ID, 0)) &&
		    !(si_setcore(bus->sih, ARMCM3_CORE_ID, 0))) {
			if (si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
				foundcr4 = 1;
			} else {
				DHD_ERROR(("%s: Failed to find ARM core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}
		}

		if (!foundcr4) {
			si_core_disable(bus->sih, 0);
			if (bcmsdh_regfail(bus->sdh)) {
				bcmerror = BCME_SDIO_ERROR;
				goto fail;
			}

			if (!(si_setcore(bus->sih, SOCRAM_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find SOCRAM core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}

			si_core_reset(bus->sih, 0, 0);
			if (bcmsdh_regfail(bus->sdh)) {
				DHD_ERROR(("%s: Failure trying reset SOCRAM core?\n",
				           __FUNCTION__));
				bcmerror = BCME_SDIO_ERROR;
				goto fail;
			}

			/* Disable remap for download */
			if (REMAP_ENAB(bus) && si_socdevram_remap_isenb(bus->sih))
				dhdsdio_devram_remap(bus, FALSE);

			if (CHIPID(bus->sih->chip) == BCM43430_CHIP_ID) {
				/* Disabling Remap for SRAM_3 */
				si_socram_set_bankpda(bus->sih, 0x3, 0x0);
			}

			/* Clear the top bit of memory */
			if (bus->ramsize) {
				uint32 zeros = 0;
				if (dhdsdio_membytes(bus, TRUE, bus->ramsize - 4,
				                     (uint8*)&zeros, 4) < 0) {
					bcmerror = BCME_SDIO_ERROR;
					goto fail;
				}
			}
		} else {
			/* For CR4,
			 * Halt ARM
			 * Remove ARM reset
			 * Read RAM base address [0x18_0000]
			 * [next] Download firmware
			 * [done at else] Populate the reset vector
			 * [done at else] Remove ARM halt
			*/
			/* Halt ARM & remove reset */
			si_core_reset(bus->sih, SICF_CPUHALT, SICF_CPUHALT);
		}
	} else {
		if (!si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
			if (!(si_setcore(bus->sih, SOCRAM_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find SOCRAM core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}

			if (!si_iscoreup(bus->sih)) {
				DHD_ERROR(("%s: SOCRAM core is down after reset?\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}

			if ((bcmerror = dhdsdio_write_vars(bus))) {
				DHD_ERROR(("%s: could not write vars to RAM\n", __FUNCTION__));
				goto fail;
			}

			/* Enable remap before ARM reset but after vars.
			 * No backplane access in remap mode
			 */
			if (REMAP_ENAB(bus) && !si_socdevram_remap_isenb(bus->sih))
				dhdsdio_devram_remap(bus, TRUE);

			if (!si_setcore(bus->sih, PCMCIA_CORE_ID, 0) &&
			    !si_setcore(bus->sih, SDIOD_CORE_ID, 0)) {
				DHD_ERROR(("%s: Can't change back to SDIO core?\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}
			W_SDREG(0xFFFFFFFF, &bus->regs->intstatus, retries);


			if (!(si_setcore(bus->sih, ARM7S_CORE_ID, 0)) &&
			    !(si_setcore(bus->sih, ARMCM3_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find ARM core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}
		} else {
			/* cr4 has no socram, but tcm's */
			/* write vars */
			if ((bcmerror = dhdsdio_write_vars(bus))) {
				DHD_ERROR(("%s: could not write vars to RAM\n", __FUNCTION__));
				goto fail;
			}

			if (!si_setcore(bus->sih, PCMCIA_CORE_ID, 0) &&
			    !si_setcore(bus->sih, SDIOD_CORE_ID, 0)) {
				DHD_ERROR(("%s: Can't change back to SDIO core?\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}
			W_SDREG(0xFFFFFFFF, &bus->regs->intstatus, retries);

			/* switch back to arm core again */
			if (!(si_setcore(bus->sih, ARMCR4_CORE_ID, 0))) {
				DHD_ERROR(("%s: Failed to find ARM CR4 core!\n", __FUNCTION__));
				bcmerror = BCME_ERROR;
				goto fail;
			}
			/* write address 0 with reset instruction */
			bcmerror = dhdsdio_membytes(bus, TRUE, 0,
				(uint8 *)&bus->resetinstr, sizeof(bus->resetinstr));

			/* now remove reset and halt and continue to run CR4 */
		}

		si_core_reset(bus->sih, 0, 0);
		if (bcmsdh_regfail(bus->sdh)) {
			DHD_ERROR(("%s: Failure trying to reset ARM core?\n", __FUNCTION__));
			bcmerror = BCME_SDIO_ERROR;
			goto fail;
		}

		/* Allow HT Clock now that the ARM is running. */
		bus->alp_only = FALSE;

		bus->dhd->busstate = DHD_BUS_LOAD;
	}

fail:
	/* Always return to SDIOD core */
	if (!si_setcore(bus->sih, PCMCIA_CORE_ID, 0))
		si_setcore(bus->sih, SDIOD_CORE_ID, 0);

	return bcmerror;
}

int
dhd_bus_iovar_op(dhd_pub_t *dhdp, const char *name,
                 void *params, int plen, void *arg, int len, bool set)
{
	dhd_bus_t *bus = dhdp->bus;
	const bcm_iovar_t *vi = NULL;
	int bcmerror = 0;
	int val_size;
	uint32 actionid;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(name);
	ASSERT(len >= 0);

	/* Get MUST have return space */
	ASSERT(set || (arg && len));

	/* Set does NOT take qualifiers */
	ASSERT(!set || (!params && !plen));

	/* Look up var locally; if not found pass to host driver */
	if ((vi = bcm_iovar_lookup(dhdsdio_iovars, name)) == NULL) {
		dhd_os_sdlock(bus->dhd);

		BUS_WAKE(bus);

		/* Turn on clock in case SD command needs backplane */
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

		bcmerror = bcmsdh_iovar_op(bus->sdh, name, params, plen, arg, len, set);

		/* Check for bus configuration changes of interest */

		/* If it was divisor change, read the new one */
		if (set && strcmp(name, "sd_divisor") == 0) {
			if (bcmsdh_iovar_op(bus->sdh, "sd_divisor", NULL, 0,
			                    &bus->sd_divisor, sizeof(int32), FALSE) != BCME_OK) {
				bus->sd_divisor = -1;
				DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, name));
			} else {
				DHD_INFO(("%s: noted %s update, value now %d\n",
				          __FUNCTION__, name, bus->sd_divisor));
			}
		}
		/* If it was a mode change, read the new one */
		if (set && strcmp(name, "sd_mode") == 0) {
			if (bcmsdh_iovar_op(bus->sdh, "sd_mode", NULL, 0,
			                    &bus->sd_mode, sizeof(int32), FALSE) != BCME_OK) {
				bus->sd_mode = -1;
				DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, name));
			} else {
				DHD_INFO(("%s: noted %s update, value now %d\n",
				          __FUNCTION__, name, bus->sd_mode));
			}
		}
		/* Similar check for blocksize change */
		if (set && strcmp(name, "sd_blocksize") == 0) {
			int32 fnum = 2;
			if (bcmsdh_iovar_op(bus->sdh, "sd_blocksize", &fnum, sizeof(int32),
			                    &bus->blocksize, sizeof(int32), FALSE) != BCME_OK) {
				bus->blocksize = 0;
				DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, "sd_blocksize"));
			} else {
				DHD_INFO(("%s: noted %s update, value now %d\n",
				          __FUNCTION__, "sd_blocksize", bus->blocksize));

				dhdsdio_tune_fifoparam(bus);
			}
		}
		bus->roundup = MIN(max_roundup, bus->blocksize);

		if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
			bus->activity = FALSE;
			dhdsdio_clkctl(bus, CLK_NONE, TRUE);
		}

		dhd_os_sdunlock(bus->dhd);
		goto exit;
	}

	DHD_CTL(("%s: %s %s, len %d plen %d\n", __FUNCTION__,
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
	bcmerror = dhdsdio_doiovar(bus, vi, actionid, name, params, plen, arg, len, val_size);

exit:
	return bcmerror;
}

void
dhd_bus_stop(struct dhd_bus *bus, bool enforce_mutex)
{
	osl_t *osh;
	uint32 local_hostintmask;
	uint8 saveclk;
	uint retries;
	int err;
	bool wlfc_enabled = FALSE;

	if (!bus->dhd)
		return;

	osh = bus->dhd->osh;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	bcmsdh_waitlockfree(bus->sdh);

	if (enforce_mutex)
		dhd_os_sdlock(bus->dhd);

	if ((bus->dhd->busstate == DHD_BUS_DOWN) || bus->dhd->hang_was_sent) {
		/* if Firmware already hangs disbale any interrupt */
		bus->dhd->busstate = DHD_BUS_DOWN;
		bus->hostintmask = 0;
		bcmsdh_intr_disable(bus->sdh);
	} else {

		BUS_WAKE(bus);

		/* Change our idea of bus state */
		bus->dhd->busstate = DHD_BUS_DOWN;

		if (KSO_ENAB(bus)) {

		/* Enable clock for device interrupts */
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

		/* Disable and clear interrupts at the chip level also */
		W_SDREG(0, &bus->regs->hostintmask, retries);
		local_hostintmask = bus->hostintmask;
		bus->hostintmask = 0;

		/* Force clocks on backplane to be sure F2 interrupt propagates */
		saveclk = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (!err) {
			bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
			                 (saveclk | SBSDIO_FORCE_HT), &err);
		}
		if (err) {
			DHD_ERROR(("%s: Failed to force clock for F2: err %d\n",
			            __FUNCTION__, err));
		}

		/* Turn off the bus (F2), free any pending packets */
		DHD_INTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
		bcmsdh_intr_disable(bus->sdh);
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, SDIO_FUNC_ENABLE_1, NULL);

		/* Clear any pending interrupts now that F2 is disabled */
		W_SDREG(local_hostintmask, &bus->regs->intstatus, retries);
		}

		/* Turn off the backplane clock (only) */
		dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);
	}

#ifdef PROP_TXSTATUS
	wlfc_enabled = (dhd_wlfc_cleanup_txq(bus->dhd, NULL, 0) != WLFC_UNSUPPORTED);
#endif
	if (!wlfc_enabled) {
#ifdef DHDTCPACK_SUPPRESS
		/* Clean tcp_ack_info_tbl in order to prevent access to flushed pkt,
		 * when there is a newly coming packet from network stack.
		 */
		dhd_tcpack_info_tbl_clean(bus->dhd);
#endif /* DHDTCPACK_SUPPRESS */
		/* Clear the data packet queues */
		pktq_flush(osh, &bus->txq, TRUE, NULL, 0);
	}

	/* Clear any held glomming stuff */
	if (bus->glomd)
		PKTFREE(osh, bus->glomd, FALSE);

	if (bus->glom)
		PKTFREE(osh, bus->glom, FALSE);

	bus->glom = bus->glomd = NULL;

	/* Clear rx control and wake any waiters */
	bus->rxlen = 0;
	dhd_os_ioctl_resp_wake(bus->dhd);

	/* Reset some F2 state stuff */
	bus->rxskip = FALSE;
	bus->tx_seq = bus->rx_seq = 0;

	bus->tx_max = 4;

	if (enforce_mutex)
		dhd_os_sdunlock(bus->dhd);
}

#if defined(BCMSDIOH_TXGLOM) && defined(BCMSDIOH_STD)
extern uint sd_txglom;
#endif
void
dhd_txglom_enable(dhd_pub_t *dhdp, bool enable)
{
	/* can't enable host txglom by default, some platforms have no
	 * (or crappy) ADMA support and txglom will cause kernel assertions (e.g.
	 * panda board)
	 */
	dhd_bus_t *bus = dhdp->bus;
#ifdef BCMSDIOH_TXGLOM
	char buf[256];
	uint32 rxglom;
	int32 ret;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

#ifdef BCMSDIOH_STD
	if (enable)
		enable = sd_txglom;
#endif /* BCMSDIOH_STD */

	if (enable) {
		rxglom = 1;
		memset(buf, 0, sizeof(buf));
		bcm_mkiovar("bus:rxglom", (void *)&rxglom, 4, buf, sizeof(buf));
		ret = dhd_wl_ioctl_cmd(dhdp, WLC_SET_VAR, buf, sizeof(buf), TRUE, 0);
		if (ret >= 0)
			bus->txglom_enable = TRUE;
		else {
#ifdef BCMSDIOH_STD
			sd_txglom = 0;
#endif /* BCMSDIOH_STD */
			bus->txglom_enable = FALSE;
		}
	} else
#endif /* BCMSDIOH_TXGLOM */
		bus->txglom_enable = FALSE;
}

int
dhd_bus_init(dhd_pub_t *dhdp, bool enforce_mutex)
{
	dhd_bus_t *bus = dhdp->bus;
	dhd_timeout_t tmo;
	uint retries = 0;
	uint8 ready, enable;
	int err, ret = 0;
	uint8 saveclk;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	ASSERT(bus->dhd);
	if (!bus->dhd)
		return 0;

	if (enforce_mutex)
		dhd_os_sdlock(bus->dhd);

	/* Make sure backplane clock is on, needed to generate F2 interrupt */
	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
	if (bus->clkstate != CLK_AVAIL) {
		DHD_ERROR(("%s: clock state is wrong. state = %d\n", __FUNCTION__, bus->clkstate));
		ret = -1;
		goto exit;
	}


	/* Force clocks on backplane to be sure F2 interrupt propagates */
	saveclk = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
	if (!err) {
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		                 (saveclk | SBSDIO_FORCE_HT), &err);
	}
	if (err) {
		DHD_ERROR(("%s: Failed to force clock for F2: err %d\n", __FUNCTION__, err));
		ret = -1;
		goto exit;
	}

	/* Enable function 2 (frame transfers) */
	W_SDREG((SDPCM_PROT_VERSION << SMB_DATA_VERSION_SHIFT),
	        &bus->regs->tosbmailboxdata, retries);
	enable = (SDIO_FUNC_ENABLE_1 | SDIO_FUNC_ENABLE_2);

	bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, enable, NULL);

	/* Give the dongle some time to do its thing and set IOR2 */
	dhd_timeout_start(&tmo, DHD_WAIT_F2RDY * 1000);

	ready = 0;
	while (ready != enable && !dhd_timeout_expired(&tmo))
	        ready = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_IORDY, NULL);

	DHD_ERROR(("%s: enable 0x%02x, ready 0x%02x (waited %uus)\n",
	          __FUNCTION__, enable, ready, tmo.elapsed));


	/* If F2 successfully enabled, set core and enable interrupts */
	if (ready == enable) {
		/* Make sure we're talking to the core. */
		if (!(bus->regs = si_setcore(bus->sih, PCMCIA_CORE_ID, 0)))
			bus->regs = si_setcore(bus->sih, SDIOD_CORE_ID, 0);
		ASSERT(bus->regs != NULL);

		/* Set up the interrupt mask and enable interrupts */
		bus->hostintmask = HOSTINTMASK;
		/* corerev 4 could use the newer interrupt logic to detect the frames */
		if ((bus->sih->buscoretype == SDIOD_CORE_ID) && (bus->sdpcmrev == 4) &&
			(bus->rxint_mode != SDIO_DEVICE_HMB_RXINT)) {
			bus->hostintmask &= ~I_HMB_FRAME_IND;
			bus->hostintmask |= I_XMTDATA_AVAIL;
		}
		W_SDREG(bus->hostintmask, &bus->regs->hostintmask, retries);

		if (bus->sih->buscorerev < 15) {
			bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1, SBSDIO_WATERMARK,
				(uint8)watermark, &err);
		}

		/* Set bus state according to enable result */
		dhdp->busstate = DHD_BUS_DATA;

		/* bcmsdh_intr_unmask(bus->sdh); */

		bus->intdis = FALSE;
		if (bus->intr) {
			DHD_INTR(("%s: enable SDIO device interrupts\n", __FUNCTION__));
			bcmsdh_intr_enable(bus->sdh);
		} else {
			DHD_INTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
			bcmsdh_intr_disable(bus->sdh);
		}

	}


	else {
		/* Disable F2 again */
		enable = SDIO_FUNC_ENABLE_1;
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, enable, NULL);
	}

	if (dhdsdio_sr_cap(bus)) {
		dhdsdio_sr_init(bus);
		/* Masking the chip active interrupt  permanantly */
		bus->hostintmask &= ~I_CHIPACTIVE;
		W_SDREG(bus->hostintmask, &bus->regs->hostintmask, retries);
		DHD_INFO(("%s: disable I_CHIPACTIVE in hostintmask[0x%08x]\n",
		__FUNCTION__, bus->hostintmask));
	}
	else
		bcmsdh_cfg_write(bus->sdh, SDIO_FUNC_1,
			SBSDIO_FUNC1_CHIPCLKCSR, saveclk, &err);

	/* If we didn't come up, turn off backplane clock */
	if (dhdp->busstate != DHD_BUS_DATA)
		dhdsdio_clkctl(bus, CLK_NONE, FALSE);


exit:
	if (enforce_mutex)
		dhd_os_sdunlock(bus->dhd);

	return ret;
}

static void
dhdsdio_rxfail(dhd_bus_t *bus, bool abort, bool rtx)
{
	bcmsdh_info_t *sdh = bus->sdh;
	sdpcmd_regs_t *regs = bus->regs;
	uint retries = 0;
	uint16 lastrbc;
	uint8 hi, lo;
	int err;

	DHD_ERROR(("%s: %sterminate frame%s\n", __FUNCTION__,
	           (abort ? "abort command, " : ""), (rtx ? ", send NAK" : "")));

	if (!KSO_ENAB(bus)) {
		DHD_ERROR(("%s: Device asleep\n", __FUNCTION__));
		return;
	}

	if (abort) {
		bcmsdh_abort(sdh, SDIO_FUNC_2);
	}

	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_FRAMECTRL, SFC_RF_TERM, &err);
	if (err) {
		DHD_ERROR(("%s: SBSDIO_FUNC1_FRAMECTRL cmd err\n", __FUNCTION__));
		goto fail;
	}
	bus->f1regdata++;

	/* Wait until the packet has been flushed (device/FIFO stable) */
	for (lastrbc = retries = 0xffff; retries > 0; retries--) {
		hi = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_RFRAMEBCHI, NULL);
		lo = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_RFRAMEBCLO, &err);
		if (err) {
			DHD_ERROR(("%s: SBSDIO_FUNC1_RFAMEBCLO cmd err\n", __FUNCTION__));
			goto fail;
		}

		bus->f1regdata += 2;

		if ((hi == 0) && (lo == 0))
			break;

		if ((hi > (lastrbc >> 8)) && (lo > (lastrbc & 0x00ff))) {
			DHD_ERROR(("%s: count growing: last 0x%04x now 0x%04x\n",
			           __FUNCTION__, lastrbc, ((hi << 8) + lo)));
		}
		lastrbc = (hi << 8) + lo;
	}

	if (!retries) {
		DHD_ERROR(("%s: count never zeroed: last 0x%04x\n", __FUNCTION__, lastrbc));
	} else {
		DHD_INFO(("%s: flush took %d iterations\n", __FUNCTION__, (0xffff - retries)));
	}

	if (rtx) {
		bus->rxrtx++;
		W_SDREG(SMB_NAK, &regs->tosbmailbox, retries);
		bus->f1regdata++;
		if (retries <= retry_limit) {
			bus->rxskip = TRUE;
		}
	}

	/* Clear partial in any case */
	bus->nextlen = 0;

fail:
	/* If we can't reach the device, signal failure */
	if (err || bcmsdh_regfail(sdh))
		bus->dhd->busstate = DHD_BUS_DOWN;
}

static void
dhdsdio_read_control(dhd_bus_t *bus, uint8 *hdr, uint len, uint doff)
{
	bcmsdh_info_t *sdh = bus->sdh;
	uint rdlen, pad;

	int sdret;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* Control data already received in aligned rxctl */
	if ((bus->bus == SPI_BUS) && (!bus->usebufpool))
		goto gotpkt;

	ASSERT(bus->rxbuf);
	/* Set rxctl for frame (w/optional alignment) */
	bus->rxctl = bus->rxbuf;
	if (dhd_alignctl) {
		bus->rxctl += firstread;
		if ((pad = ((uintptr)bus->rxctl % DHD_SDALIGN)))
			bus->rxctl += (DHD_SDALIGN - pad);
		bus->rxctl -= firstread;
	}
	ASSERT(bus->rxctl >= bus->rxbuf);

	/* Copy the already-read portion over */
	bcopy(hdr, bus->rxctl, firstread);
	if (len <= firstread)
		goto gotpkt;

	/* Copy the full data pkt in gSPI case and process ioctl. */
	if (bus->bus == SPI_BUS) {
		bcopy(hdr, bus->rxctl, len);
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
		rdlen = ROUNDUP(rdlen, ALIGNMENT);

	/* Drop if the read is too big or it exceeds our maximum */
	if ((rdlen + firstread) > bus->dhd->maxctl) {
		DHD_ERROR(("%s: %d-byte control read exceeds %d-byte buffer\n",
		           __FUNCTION__, rdlen, bus->dhd->maxctl));
		bus->dhd->rx_errors++;
		dhdsdio_rxfail(bus, FALSE, FALSE);
		goto done;
	}

	if ((len - doff) > bus->dhd->maxctl) {
		DHD_ERROR(("%s: %d-byte ctl frame (%d-byte ctl data) exceeds %d-byte limit\n",
		           __FUNCTION__, len, (len - doff), bus->dhd->maxctl));
		bus->dhd->rx_errors++; bus->rx_toolong++;
		dhdsdio_rxfail(bus, FALSE, FALSE);
		goto done;
	}


	/* Read remainder of frame body into the rxctl buffer */
	sdret = dhd_bcmsdh_recv_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
	                            (bus->rxctl + firstread), rdlen, NULL, NULL, NULL);
	bus->f2rxdata++;
	ASSERT(sdret != BCME_PENDING);

	/* Control frame failures need retransmission */
	if (sdret < 0) {
		DHD_ERROR(("%s: read %d control bytes failed: %d\n", __FUNCTION__, rdlen, sdret));
		bus->rxc_errors++; /* dhd.rx_ctlerrs is higher level */
		dhdsdio_rxfail(bus, TRUE, TRUE);
		goto done;
	}

gotpkt:

#ifdef DHD_DEBUG
	if (DHD_BYTES_ON() && DHD_CTL_ON()) {
		prhex("RxCtrl", bus->rxctl, len);
	}
#endif

	/* Point to valid data and indicate its length */
	bus->rxctl += doff;
	bus->rxlen = len - doff;

done:
	/* Awake any waiters */
	dhd_os_ioctl_resp_wake(bus->dhd);
}
int
dhd_process_pkt_reorder_info(dhd_pub_t *dhd, uchar *reorder_info_buf, uint reorder_info_len,
	void **pkt, uint32 *pkt_count);

static uint8
dhdsdio_rxglom(dhd_bus_t *bus, uint8 rxseq)
{
	uint16 dlen, totlen;
	uint8 *dptr, num = 0;

	uint16 sublen, check;
	void *pfirst, *plast, *pnext;
	void * list_tail[DHD_MAX_IFS] = { NULL };
	void * list_head[DHD_MAX_IFS] = { NULL };
	uint8 idx;
	osl_t *osh = bus->dhd->osh;

	int errcode;
	uint8 chan, seq, doff, sfdoff;
	uint8 txmax;
	uchar reorder_info_buf[WLHOST_REORDERDATA_TOTLEN];
	uint reorder_info_len;

	int ifidx = 0;
	bool usechain = bus->use_rxchain;

	/* If packets, issue read(s) and send up packet chain */
	/* Return sequence numbers consumed? */

	DHD_TRACE(("dhdsdio_rxglom: start: glomd %p glom %p\n", bus->glomd, bus->glom));

	/* If there's a descriptor, generate the packet chain */
	if (bus->glomd) {
		dhd_os_sdlock_rxq(bus->dhd);

		pfirst = plast = pnext = NULL;
		dlen = (uint16)PKTLEN(osh, bus->glomd);
		dptr = PKTDATA(osh, bus->glomd);
		if (!dlen || (dlen & 1)) {
			DHD_ERROR(("%s: bad glomd len (%d), ignore descriptor\n",
			           __FUNCTION__, dlen));
			dlen = 0;
		}

		for (totlen = num = 0; dlen; num++) {
			/* Get (and move past) next length */
			sublen = ltoh16_ua(dptr);
			dlen -= sizeof(uint16);
			dptr += sizeof(uint16);
			if ((sublen < SDPCM_HDRLEN) ||
			    ((num == 0) && (sublen < (2 * SDPCM_HDRLEN)))) {
				DHD_ERROR(("%s: descriptor len %d bad: %d\n",
				           __FUNCTION__, num, sublen));
				pnext = NULL;
				break;
			}
			if (sublen % DHD_SDALIGN) {
				DHD_ERROR(("%s: sublen %d not a multiple of %d\n",
				           __FUNCTION__, sublen, DHD_SDALIGN));
				usechain = FALSE;
			}
			totlen += sublen;

			/* For last frame, adjust read len so total is a block multiple */
			if (!dlen) {
				sublen += (ROUNDUP(totlen, bus->blocksize) - totlen);
				totlen = ROUNDUP(totlen, bus->blocksize);
			}

			/* Allocate/chain packet for next subframe */
			if ((pnext = PKTGET(osh, sublen + DHD_SDALIGN, FALSE)) == NULL) {
				DHD_ERROR(("%s: PKTGET failed, num %d len %d\n",
				           __FUNCTION__, num, sublen));
				break;
			}
			ASSERT(!PKTLINK(pnext));
			if (!pfirst) {
				ASSERT(!plast);
				pfirst = plast = pnext;
			} else {
				ASSERT(plast);
				PKTSETNEXT(osh, plast, pnext);
				plast = pnext;
			}

			/* Adhere to start alignment requirements */
			PKTALIGN(osh, pnext, sublen, DHD_SDALIGN);
		}

		/* If all allocations succeeded, save packet chain in bus structure */
		if (pnext) {
			DHD_GLOM(("%s: allocated %d-byte packet chain for %d subframes\n",
			          __FUNCTION__, totlen, num));
			if (DHD_GLOM_ON() && bus->nextlen) {
				if (totlen != bus->nextlen) {
					DHD_GLOM(("%s: glomdesc mismatch: nextlen %d glomdesc %d "
					          "rxseq %d\n", __FUNCTION__, bus->nextlen,
					          totlen, rxseq));
				}
			}
			bus->glom = pfirst;
			pfirst = pnext = NULL;
		} else {
			if (pfirst)
				PKTFREE(osh, pfirst, FALSE);
			bus->glom = NULL;
			num = 0;
		}

		/* Done with descriptor packet */
		PKTFREE(osh, bus->glomd, FALSE);
		bus->glomd = NULL;
		bus->nextlen = 0;

		dhd_os_sdunlock_rxq(bus->dhd);
	}

	/* Ok -- either we just generated a packet chain, or had one from before */
	if (bus->glom) {
		if (DHD_GLOM_ON()) {
			DHD_GLOM(("%s: attempt superframe read, packet chain:\n", __FUNCTION__));
			for (pnext = bus->glom; pnext; pnext = PKTNEXT(osh, pnext)) {
				DHD_GLOM(("    %p: %p len 0x%04x (%d)\n",
				          pnext, (uint8*)PKTDATA(osh, pnext),
				          PKTLEN(osh, pnext), PKTLEN(osh, pnext)));
			}
		}

		pfirst = bus->glom;
		dlen = (uint16)pkttotlen(osh, pfirst);

		/* Do an SDIO read for the superframe.  Configurable iovar to
		 * read directly into the chained packet, or allocate a large
		 * packet and and copy into the chain.
		 */
		if (usechain) {
			errcode = dhd_bcmsdh_recv_buf(bus,
			                              bcmsdh_cur_sbwad(bus->sdh), SDIO_FUNC_2,
			                              F2SYNC, (uint8*)PKTDATA(osh, pfirst),
			                              dlen, pfirst, NULL, NULL);
		} else if (bus->dataptr) {
			errcode = dhd_bcmsdh_recv_buf(bus,
			                              bcmsdh_cur_sbwad(bus->sdh), SDIO_FUNC_2,
			                              F2SYNC, bus->dataptr,
			                              dlen, NULL, NULL, NULL);
			sublen = (uint16)pktfrombuf(osh, pfirst, 0, dlen, bus->dataptr);
			if (sublen != dlen) {
				DHD_ERROR(("%s: FAILED TO COPY, dlen %d sublen %d\n",
				           __FUNCTION__, dlen, sublen));
				errcode = -1;
			}
			pnext = NULL;
		} else {
			DHD_ERROR(("COULDN'T ALLOC %d-BYTE GLOM, FORCE FAILURE\n", dlen));
			errcode = -1;
		}
		bus->f2rxdata++;
		ASSERT(errcode != BCME_PENDING);

		/* On failure, kill the superframe, allow a couple retries */
		if (errcode < 0) {
			DHD_ERROR(("%s: glom read of %d bytes failed: %d\n",
			           __FUNCTION__, dlen, errcode));
			bus->dhd->rx_errors++;

			if (bus->glomerr++ < 3) {
				dhdsdio_rxfail(bus, TRUE, TRUE);
			} else {
				bus->glomerr = 0;
				dhdsdio_rxfail(bus, TRUE, FALSE);
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE(osh, bus->glom, FALSE);
				dhd_os_sdunlock_rxq(bus->dhd);
				bus->rxglomfail++;
				bus->glom = NULL;
			}
			return 0;
		}

#ifdef DHD_DEBUG
		if (DHD_GLOM_ON()) {
			prhex("SUPERFRAME", PKTDATA(osh, pfirst),
			      MIN(PKTLEN(osh, pfirst), 48));
		}
#endif


		/* Validate the superframe header */
		dptr = (uint8 *)PKTDATA(osh, pfirst);
		sublen = ltoh16_ua(dptr);
		check = ltoh16_ua(dptr + sizeof(uint16));

		chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
		bus->nextlen = dptr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
			DHD_INFO(("%s: got frame w/nextlen too large (%d) seq %d\n",
			          __FUNCTION__, bus->nextlen, seq));
			bus->nextlen = 0;
		}
		doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

		errcode = 0;
		if ((uint16)~(sublen^check)) {
			DHD_ERROR(("%s (superframe): HW hdr error: len/check 0x%04x/0x%04x\n",
			           __FUNCTION__, sublen, check));
			errcode = -1;
		} else if (ROUNDUP(sublen, bus->blocksize) != dlen) {
			DHD_ERROR(("%s (superframe): len 0x%04x, rounded 0x%04x, expect 0x%04x\n",
			           __FUNCTION__, sublen, ROUNDUP(sublen, bus->blocksize), dlen));
			errcode = -1;
		} else if (SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]) != SDPCM_GLOM_CHANNEL) {
			DHD_ERROR(("%s (superframe): bad channel %d\n", __FUNCTION__,
			           SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN])));
			errcode = -1;
		} else if (SDPCM_GLOMDESC(&dptr[SDPCM_FRAMETAG_LEN])) {
			DHD_ERROR(("%s (superframe): got second descriptor?\n", __FUNCTION__));
			errcode = -1;
		} else if ((doff < SDPCM_HDRLEN) ||
		           (doff > (PKTLEN(osh, pfirst) - SDPCM_HDRLEN))) {
			DHD_ERROR(("%s (superframe): Bad data offset %d: HW %d pkt %d min %d\n",
				__FUNCTION__, doff, sublen, PKTLEN(osh, pfirst),
				SDPCM_HDRLEN));
			errcode = -1;
		}

		/* Check sequence number of superframe SW header */
		if (rxseq != seq) {
			DHD_INFO(("%s: (superframe) rx_seq %d, expected %d\n",
			          __FUNCTION__, seq, rxseq));
			bus->rx_badseq++;
			rxseq = seq;
		}

		/* Check window for sanity */
		if ((uint8)(txmax - bus->tx_seq) > 0x70) {
			DHD_ERROR(("%s: got unlikely tx max %d with tx_seq %d\n",
			           __FUNCTION__, txmax, bus->tx_seq));
			txmax = bus->tx_max;
		}
		bus->tx_max = txmax;

		/* Remove superframe header, remember offset */
		PKTPULL(osh, pfirst, doff);
		sfdoff = doff;

		/* Validate all the subframe headers */
		for (num = 0, pnext = pfirst; pnext && !errcode;
		     num++, pnext = PKTNEXT(osh, pnext)) {
			dptr = (uint8 *)PKTDATA(osh, pnext);
			dlen = (uint16)PKTLEN(osh, pnext);
			sublen = ltoh16_ua(dptr);
			check = ltoh16_ua(dptr + sizeof(uint16));
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);
#ifdef DHD_DEBUG
			if (DHD_GLOM_ON()) {
				prhex("subframe", dptr, 32);
			}
#endif

			if ((uint16)~(sublen^check)) {
				DHD_ERROR(("%s (subframe %d): HW hdr error: "
				           "len/check 0x%04x/0x%04x\n",
				           __FUNCTION__, num, sublen, check));
				errcode = -1;
			} else if ((sublen > dlen) || (sublen < SDPCM_HDRLEN)) {
				DHD_ERROR(("%s (subframe %d): length mismatch: "
				           "len 0x%04x, expect 0x%04x\n",
				           __FUNCTION__, num, sublen, dlen));
				errcode = -1;
			} else if ((chan != SDPCM_DATA_CHANNEL) &&
			           (chan != SDPCM_EVENT_CHANNEL)) {
				DHD_ERROR(("%s (subframe %d): bad channel %d\n",
				           __FUNCTION__, num, chan));
				errcode = -1;
			} else if ((doff < SDPCM_HDRLEN) || (doff > sublen)) {
				DHD_ERROR(("%s (subframe %d): Bad data offset %d: HW %d min %d\n",
				           __FUNCTION__, num, doff, sublen, SDPCM_HDRLEN));
				errcode = -1;
			}
		}

		if (errcode) {
			/* Terminate frame on error, request a couple retries */
			if (bus->glomerr++ < 3) {
				/* Restore superframe header space */
				PKTPUSH(osh, pfirst, sfdoff);
				dhdsdio_rxfail(bus, TRUE, TRUE);
			} else {
				bus->glomerr = 0;
				dhdsdio_rxfail(bus, TRUE, FALSE);
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE(osh, bus->glom, FALSE);
				dhd_os_sdunlock_rxq(bus->dhd);
				bus->rxglomfail++;
				bus->glom = NULL;
			}
			bus->nextlen = 0;
			return 0;
		}

		/* Basic SD framing looks ok - process each packet (header) */
		bus->glom = NULL;
		plast = NULL;

		dhd_os_sdlock_rxq(bus->dhd);
		for (num = 0; pfirst; rxseq++, pfirst = pnext) {
			pnext = PKTNEXT(osh, pfirst);
			PKTSETNEXT(osh, pfirst, NULL);

			dptr = (uint8 *)PKTDATA(osh, pfirst);
			sublen = ltoh16_ua(dptr);
			chan = SDPCM_PACKET_CHANNEL(&dptr[SDPCM_FRAMETAG_LEN]);
			seq = SDPCM_PACKET_SEQUENCE(&dptr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&dptr[SDPCM_FRAMETAG_LEN]);

			DHD_GLOM(("%s: Get subframe %d, %p(%p/%d), sublen %d chan %d seq %d\n",
			          __FUNCTION__, num, pfirst, PKTDATA(osh, pfirst),
			          PKTLEN(osh, pfirst), sublen, chan, seq));

			ASSERT((chan == SDPCM_DATA_CHANNEL) || (chan == SDPCM_EVENT_CHANNEL));

			if (rxseq != seq) {
				DHD_GLOM(("%s: rx_seq %d, expected %d\n",
				          __FUNCTION__, seq, rxseq));
				bus->rx_badseq++;
				rxseq = seq;
			}

#ifdef DHD_DEBUG
			if (DHD_BYTES_ON() && DHD_DATA_ON()) {
				prhex("Rx Subframe Data", dptr, dlen);
			}
#endif

			PKTSETLEN(osh, pfirst, sublen);
			PKTPULL(osh, pfirst, doff);

			reorder_info_len = sizeof(reorder_info_buf);

			if (PKTLEN(osh, pfirst) == 0) {
				PKTFREE(bus->dhd->osh, pfirst, FALSE);
				continue;
			} else if (dhd_prot_hdrpull(bus->dhd, &ifidx, pfirst, reorder_info_buf,
				&reorder_info_len) != 0) {
				DHD_ERROR(("%s: rx protocol error\n", __FUNCTION__));
				bus->dhd->rx_errors++;
				PKTFREE(osh, pfirst, FALSE);
				continue;
			}
			if (reorder_info_len) {
				uint32 free_buf_count;
				void *ppfirst;

				ppfirst = pfirst;
				/* Reordering info from the firmware */
				dhd_process_pkt_reorder_info(bus->dhd, reorder_info_buf,
					reorder_info_len, &ppfirst, &free_buf_count);

				if (free_buf_count == 0) {
					continue;
				}
				else {
					void *temp;

					/*  go to the end of the chain and attach the pnext there */
					temp = ppfirst;
					while (PKTNEXT(osh, temp) != NULL) {
						temp = PKTNEXT(osh, temp);
					}
					pfirst = temp;
					if (list_tail[ifidx] == NULL)
						list_head[ifidx] = ppfirst;
					else
						PKTSETNEXT(osh, list_tail[ifidx], ppfirst);
					list_tail[ifidx] = pfirst;
				}

				num += (uint8)free_buf_count;
			}
			else {
				/* this packet will go up, link back into chain and count it */

				if (list_tail[ifidx] == NULL) {
					list_head[ifidx] = list_tail[ifidx] = pfirst;
				}
				else {
					PKTSETNEXT(osh, list_tail[ifidx], pfirst);
					list_tail[ifidx] = pfirst;
				}
				num++;
			}
#ifdef DHD_DEBUG
			if (DHD_GLOM_ON()) {
				DHD_GLOM(("%s subframe %d to stack, %p(%p/%d) nxt/lnk %p/%p\n",
				          __FUNCTION__, num, pfirst,
				          PKTDATA(osh, pfirst), PKTLEN(osh, pfirst),
				          PKTNEXT(osh, pfirst), PKTLINK(pfirst)));
				prhex("", (uint8 *)PKTDATA(osh, pfirst),
				      MIN(PKTLEN(osh, pfirst), 32));
			}
#endif /* DHD_DEBUG */
		}
		dhd_os_sdunlock_rxq(bus->dhd);

		for (idx = 0; idx < DHD_MAX_IFS; idx++) {
			if (list_head[idx]) {
				void *temp;
				uint8 cnt = 0;
				temp = list_head[idx];
				do {
					temp = PKTNEXT(osh, temp);
					cnt++;
				} while (temp);
				if (cnt) {
					dhd_os_sdunlock(bus->dhd);
					dhd_rx_frame(bus->dhd, idx, list_head[idx], cnt, 0);
					dhd_os_sdlock(bus->dhd);
				}
			}
		}
		bus->rxglomframes++;
		bus->rxglompkts += num;
	}
	return num;
}


#ifdef SDHOST3
static bool
dhdsdio_pr94636_WAR(dhd_bus_t *bus)
{
	uint cd = 0;
	uint ld = 0;
	int bcmerror = 0;
	uint32 l_data[5];
	uint32 l_addr = (0x18002200 & SBSDIO_SB_OFT_ADDR_MASK);

	/* Read 20 bytes from 0x18002200
	 * the sdiod Tx DMA registers address on AI Backplane.
	*/
	if ((bcmerror = bcmsdh_rwdata(bus->sdh, FALSE, l_addr, (uint8 *)&l_data[0], 20))) {
		DHD_ERROR(("%s: bcmsdh_rwdata failed\n", __FUNCTION__));
		return FALSE;
	}
	ld = l_data[1];
	ld = ld & 0x00001fff;
	cd = l_data[4];
	cd = cd & 0x00001fff;
	if (cd == ld)
		return TRUE;
	else
		return FALSE;
}
#endif /* SDHOST3 */
/* Return TRUE if there may be more frames to read */
static uint
dhdsdio_readframes(dhd_bus_t *bus, uint maxframes, bool *finished)
{
	osl_t *osh = bus->dhd->osh;
	bcmsdh_info_t *sdh = bus->sdh;

	uint16 len, check;	/* Extracted hardware header fields */
	uint8 chan, seq, doff;	/* Extracted software header fields */
	uint8 fcbits;		/* Extracted fcbits from software header */
	uint8 delta;

	void *pkt;	/* Packet for event or data frames */
	uint16 pad;	/* Number of pad bytes to read */
	uint16 rdlen;	/* Total number of bytes to read */
	uint8 rxseq;	/* Next sequence number to expect */
	uint rxleft = 0;	/* Remaining number of frames allowed */
	int sdret;	/* Return code from bcmsdh calls */
	uint8 txmax;	/* Maximum tx sequence offered */
	bool len_consistent; /* Result of comparing readahead len and len from hw-hdr */
	uint8 *rxbuf;
	int ifidx = 0;
	uint rxcount = 0; /* Total frames read */
	uchar reorder_info_buf[WLHOST_REORDERDATA_TOTLEN];
	uint reorder_info_len;
	uint pkt_count;

#if defined(DHD_DEBUG) || defined(SDTEST)
	bool sdtest = FALSE;	/* To limit message spew from test mode */
#endif

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	bus->readframes = TRUE;

	if (!KSO_ENAB(bus)) {
		DHD_ERROR(("%s: KSO off\n", __FUNCTION__));
		bus->readframes = FALSE;
		return 0;
	}

	ASSERT(maxframes);

#ifdef SDTEST
	/* Allow pktgen to override maxframes */
	if (bus->pktgen_count && (bus->pktgen_mode == DHD_PKTGEN_RECV)) {
		maxframes = bus->pktgen_count;
		sdtest = TRUE;
	}
#endif

	/* Not finished unless we encounter no more frames indication */
	*finished = FALSE;


	for (rxseq = bus->rx_seq, rxleft = maxframes;
	     !bus->rxskip && rxleft && bus->dhd->busstate != DHD_BUS_DOWN;
	     rxseq++, rxleft--) {
#ifdef DHDTCPACK_SUP_DBG
		if (bus->dhd->tcpack_sup_mode != TCPACK_SUP_DELAYTX) {
			if (bus->dotxinrx == FALSE)
				DHD_ERROR(("%s %d: dotxinrx FALSE with tcpack_sub_mode %d\n",
					__FUNCTION__, __LINE__, bus->dhd->tcpack_sup_mode));
		}
#ifdef DEBUG_COUNTER
		else if (pktq_mlen(&bus->txq, ~bus->flowcontrol) > 0) {
			tack_tbl.cnt[bus->dotxinrx ? 6 : 7]++;
		}
#endif /* DEBUG_COUNTER */
#endif /* DHDTCPACK_SUP_DBG */
		/* tx more to improve rx performance */
		if (TXCTLOK(bus) && bus->ctrl_frame_stat && (bus->clkstate == CLK_AVAIL)) {
			dhdsdio_sendpendctl(bus);
		} else if (bus->dotxinrx && (bus->clkstate == CLK_AVAIL) &&
			!bus->fcstate && DATAOK(bus) &&
			(pktq_mlen(&bus->txq, ~bus->flowcontrol) > bus->txinrx_thres)) {
			dhdsdio_sendfromq(bus, dhd_txbound);
#ifdef DHDTCPACK_SUPPRESS
			/* In TCPACK_SUP_DELAYTX mode, do txinrx only if
			 * 1. Any DATA packet to TX
			 * 2. TCPACK to TCPDATA PSH packets.
			 * in bus txq.
			 */
			bus->dotxinrx = (bus->dhd->tcpack_sup_mode == TCPACK_SUP_DELAYTX) ?
				FALSE : TRUE;
#endif
		}

		/* Handle glomming separately */
		if (bus->glom || bus->glomd) {
			uint8 cnt;
			DHD_GLOM(("%s: calling rxglom: glomd %p, glom %p\n",
			          __FUNCTION__, bus->glomd, bus->glom));
			cnt = dhdsdio_rxglom(bus, rxseq);
			DHD_GLOM(("%s: rxglom returned %d\n", __FUNCTION__, cnt));
			rxseq += cnt - 1;
			rxleft = (rxleft > cnt) ? (rxleft - cnt) : 1;
			continue;
		}

		/* Try doing single read if we can */
		if (dhd_readahead && bus->nextlen) {
			uint16 nextlen = bus->nextlen;
			bus->nextlen = 0;

			if (bus->bus == SPI_BUS) {
				rdlen = len = nextlen;
			}
			else {
				rdlen = len = nextlen << 4;

				/* Pad read to blocksize for efficiency */
				if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
					pad = bus->blocksize - (rdlen % bus->blocksize);
					if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
						((rdlen + pad + firstread) < MAX_RX_DATASZ))
						rdlen += pad;
				} else if (rdlen % DHD_SDALIGN) {
					rdlen += DHD_SDALIGN - (rdlen % DHD_SDALIGN);
				}
			}

			/* We use bus->rxctl buffer in WinXP for initial control pkt receives.
			 * Later we use buffer-poll for data as well as control packets.
			 * This is required because dhd receives full frame in gSPI unlike SDIO.
			 * After the frame is received we have to distinguish whether it is data
			 * or non-data frame.
			 */
			/* Allocate a packet buffer */
			dhd_os_sdlock_rxq(bus->dhd);
			if (!(pkt = PKTGET(osh, rdlen + DHD_SDALIGN, FALSE))) {
				if (bus->bus == SPI_BUS) {
					bus->usebufpool = FALSE;
					bus->rxctl = bus->rxbuf;
					if (dhd_alignctl) {
						bus->rxctl += firstread;
						if ((pad = ((uintptr)bus->rxctl % DHD_SDALIGN)))
							bus->rxctl += (DHD_SDALIGN - pad);
						bus->rxctl -= firstread;
					}
					ASSERT(bus->rxctl >= bus->rxbuf);
					rxbuf = bus->rxctl;
					/* Read the entire frame */
					sdret = dhd_bcmsdh_recv_buf(bus,
					                            bcmsdh_cur_sbwad(sdh),
					                            SDIO_FUNC_2,
					                            F2SYNC, rxbuf, rdlen,
					                            NULL, NULL, NULL);
					bus->f2rxdata++;
					ASSERT(sdret != BCME_PENDING);


					/* Control frame failures need retransmission */
					if (sdret < 0) {
						DHD_ERROR(("%s: read %d control bytes failed: %d\n",
						   __FUNCTION__, rdlen, sdret));
						/* dhd.rx_ctlerrs is higher level */
						bus->rxc_errors++;
						dhd_os_sdunlock_rxq(bus->dhd);
						dhdsdio_rxfail(bus, TRUE,
						    (bus->bus == SPI_BUS) ? FALSE : TRUE);
						continue;
					}
				} else {
					/* Give up on data, request rtx of events */
					DHD_ERROR(("%s (nextlen): PKTGET failed: len %d rdlen %d "
					           "expected rxseq %d\n",
					           __FUNCTION__, len, rdlen, rxseq));
					/* Just go try again w/normal header read */
					dhd_os_sdunlock_rxq(bus->dhd);
					continue;
				}
			} else {
				if (bus->bus == SPI_BUS)
					bus->usebufpool = TRUE;

				ASSERT(!PKTLINK(pkt));
				PKTALIGN(osh, pkt, rdlen, DHD_SDALIGN);
				rxbuf = (uint8 *)PKTDATA(osh, pkt);
				/* Read the entire frame */
				sdret = dhd_bcmsdh_recv_buf(bus, bcmsdh_cur_sbwad(sdh),
				                            SDIO_FUNC_2,
				                            F2SYNC, rxbuf, rdlen,
				                            pkt, NULL, NULL);
				bus->f2rxdata++;
				ASSERT(sdret != BCME_PENDING);

				if (sdret < 0) {
					DHD_ERROR(("%s (nextlen): read %d bytes failed: %d\n",
					   __FUNCTION__, rdlen, sdret));
					PKTFREE(bus->dhd->osh, pkt, FALSE);
					bus->dhd->rx_errors++;
					dhd_os_sdunlock_rxq(bus->dhd);
					/* Force retry w/normal header read.  Don't attempt NAK for
					 * gSPI
					 */
					dhdsdio_rxfail(bus, TRUE,
					      (bus->bus == SPI_BUS) ? FALSE : TRUE);
					continue;
				}
			}
			dhd_os_sdunlock_rxq(bus->dhd);

			/* Now check the header */
			bcopy(rxbuf, bus->rxhdr, SDPCM_HDRLEN);

			/* Extract hardware header fields */
			len = ltoh16_ua(bus->rxhdr);
			check = ltoh16_ua(bus->rxhdr + sizeof(uint16));

			/* All zeros means readahead info was bad */
			if (!(len|check)) {
				DHD_INFO(("%s (nextlen): read zeros in HW header???\n",
				           __FUNCTION__));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				GSPI_PR55150_BAILOUT;
				continue;
			}

			/* Validate check bytes */
			if ((uint16)~(len^check)) {
				DHD_ERROR(("%s (nextlen): HW hdr error: nextlen/len/check"
				           " 0x%04x/0x%04x/0x%04x\n", __FUNCTION__, nextlen,
				           len, check));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				bus->rx_badhdr++;
				dhdsdio_rxfail(bus, FALSE, FALSE);
				GSPI_PR55150_BAILOUT;
				continue;
			}

			/* Validate frame length */
			if (len < SDPCM_HDRLEN) {
				DHD_ERROR(("%s (nextlen): HW hdr length invalid: %d\n",
				           __FUNCTION__, len));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				GSPI_PR55150_BAILOUT;
				continue;
			}

			/* Check for consistency with readahead info */
				len_consistent = (nextlen != (ROUNDUP(len, 16) >> 4));
			if (len_consistent) {
				/* Mismatch, force retry w/normal header (may be >4K) */
				DHD_ERROR(("%s (nextlen): mismatch, nextlen %d len %d rnd %d; "
				           "expected rxseq %d\n",
				           __FUNCTION__, nextlen, len, ROUNDUP(len, 16), rxseq));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				dhdsdio_rxfail(bus, TRUE, (bus->bus == SPI_BUS) ? FALSE : TRUE);
				GSPI_PR55150_BAILOUT;
				continue;
			}


			/* Extract software header fields */
			chan = SDPCM_PACKET_CHANNEL(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
			seq = SDPCM_PACKET_SEQUENCE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
			doff = SDPCM_DOFFSET_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
			txmax = SDPCM_WINDOW_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

				bus->nextlen =
				         bus->rxhdr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
				if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
					DHD_INFO(("%s (nextlen): got frame w/nextlen too large"
					          " (%d), seq %d\n", __FUNCTION__, bus->nextlen,
					          seq));
					bus->nextlen = 0;
				}

				bus->dhd->rx_readahead_cnt ++;
			/* Handle Flow Control */
			fcbits = SDPCM_FCMASK_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

			delta = 0;
			if (~bus->flowcontrol & fcbits) {
				bus->fc_xoff++;
				delta = 1;
			}
			if (bus->flowcontrol & ~fcbits) {
				bus->fc_xon++;
				delta = 1;
			}

			if (delta) {
				bus->fc_rcvd++;
				bus->flowcontrol = fcbits;
			}

			/* Check and update sequence number */
			if (rxseq != seq) {
				DHD_INFO(("%s (nextlen): rx_seq %d, expected %d\n",
				          __FUNCTION__, seq, rxseq));
				bus->rx_badseq++;
				rxseq = seq;
			}

			/* Check window for sanity */
			if ((uint8)(txmax - bus->tx_seq) > 0x70) {
					DHD_ERROR(("%s: got unlikely tx max %d with tx_seq %d\n",
						__FUNCTION__, txmax, bus->tx_seq));
					txmax = bus->tx_max;
			}
			bus->tx_max = txmax;

#ifdef DHD_DEBUG
			if (DHD_BYTES_ON() && DHD_DATA_ON()) {
				prhex("Rx Data", rxbuf, len);
			} else if (DHD_HDRS_ON()) {
				prhex("RxHdr", bus->rxhdr, SDPCM_HDRLEN);
			}
#endif

			if (chan == SDPCM_CONTROL_CHANNEL) {
				if (bus->bus == SPI_BUS) {
					dhdsdio_read_control(bus, rxbuf, len, doff);
					if (bus->usebufpool) {
						dhd_os_sdlock_rxq(bus->dhd);
						PKTFREE(bus->dhd->osh, pkt, FALSE);
						dhd_os_sdunlock_rxq(bus->dhd);
					}
					continue;
				} else {
					DHD_ERROR(("%s (nextlen): readahead on control"
					           " packet %d?\n", __FUNCTION__, seq));
					/* Force retry w/normal header read */
					bus->nextlen = 0;
					dhdsdio_rxfail(bus, FALSE, TRUE);
					dhd_os_sdlock_rxq(bus->dhd);
					PKTFREE2();
					dhd_os_sdunlock_rxq(bus->dhd);
					continue;
				}
			}

			if ((bus->bus == SPI_BUS) && !bus->usebufpool) {
				DHD_ERROR(("Received %d bytes on %d channel. Running out of "
				           "rx pktbuf's or not yet malloced.\n", len, chan));
				continue;
			}

			/* Validate data offset */
			if ((doff < SDPCM_HDRLEN) || (doff > len)) {
				DHD_ERROR(("%s (nextlen): bad data offset %d: HW len %d min %d\n",
				           __FUNCTION__, doff, len, SDPCM_HDRLEN));
				dhd_os_sdlock_rxq(bus->dhd);
				PKTFREE2();
				dhd_os_sdunlock_rxq(bus->dhd);
				ASSERT(0);
				dhdsdio_rxfail(bus, FALSE, FALSE);
				continue;
			}

			/* All done with this one -- now deliver the packet */
			goto deliver;
		}
		/* gSPI frames should not be handled in fractions */
		if (bus->bus == SPI_BUS) {
			break;
		}
#ifdef SDHOST3
		if (((((uint16)bus->sih->chip) == BCM4324_CHIP_ID) && (bus->sih->chiprev <= 1)) ||
			(((uint16)bus->sih->chip) == BCM43340_CHIP_ID) ||
			(((uint16)bus->sih->chip) == BCM43341_CHIP_ID) ||
			(((uint16)bus->sih->chip) == BCM4334_CHIP_ID)) {
			if (dhdsdio_pr94636_WAR(bus) == TRUE) {
				*finished = TRUE;
				break;
			}
		}
#endif /* SDHOST3 */

		/* Read frame header (hardware and software) */
		sdret = dhd_bcmsdh_recv_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
		                            bus->rxhdr, firstread, NULL, NULL, NULL);
		bus->f2rxhdrs++;
		ASSERT(sdret != BCME_PENDING);

		if (sdret < 0) {
			DHD_ERROR(("%s: RXHEADER FAILED: %d\n", __FUNCTION__, sdret));
			bus->rx_hdrfail++;
			dhdsdio_rxfail(bus, TRUE, TRUE);
			continue;
		}

#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() || DHD_HDRS_ON()) {
			prhex("RxHdr", bus->rxhdr, SDPCM_HDRLEN);
		}
#endif

		/* Extract hardware header fields */
		len = ltoh16_ua(bus->rxhdr);
		check = ltoh16_ua(bus->rxhdr + sizeof(uint16));

		/* All zeros means no more frames */
		if (!(len|check)) {
			*finished = TRUE;
			break;
		}

		/* Validate check bytes */
		if ((uint16)~(len^check)) {
			DHD_ERROR(("%s: HW hdr error: len/check 0x%04x/0x%04x\n",
			           __FUNCTION__, len, check));
			bus->rx_badhdr++;
			dhdsdio_rxfail(bus, FALSE, FALSE);
			continue;
		}

		/* Validate frame length */
		if (len < SDPCM_HDRLEN) {
			DHD_ERROR(("%s: HW hdr length invalid: %d\n", __FUNCTION__, len));
			continue;
		}

		/* Extract software header fields */
		chan = SDPCM_PACKET_CHANNEL(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		seq = SDPCM_PACKET_SEQUENCE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		doff = SDPCM_DOFFSET_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);
		txmax = SDPCM_WINDOW_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

		/* Validate data offset */
		if ((doff < SDPCM_HDRLEN) || (doff > len)) {
			DHD_ERROR(("%s: Bad data offset %d: HW len %d, min %d seq %d\n",
			           __FUNCTION__, doff, len, SDPCM_HDRLEN, seq));
			bus->rx_badhdr++;
			ASSERT(0);
			dhdsdio_rxfail(bus, FALSE, FALSE);
			continue;
		}

		/* Save the readahead length if there is one */
		bus->nextlen = bus->rxhdr[SDPCM_FRAMETAG_LEN + SDPCM_NEXTLEN_OFFSET];
		if ((bus->nextlen << 4) > MAX_RX_DATASZ) {
			DHD_INFO(("%s (nextlen): got frame w/nextlen too large (%d), seq %d\n",
			          __FUNCTION__, bus->nextlen, seq));
			bus->nextlen = 0;
		}

		/* Handle Flow Control */
		fcbits = SDPCM_FCMASK_VALUE(&bus->rxhdr[SDPCM_FRAMETAG_LEN]);

		delta = 0;
		if (~bus->flowcontrol & fcbits) {
			bus->fc_xoff++;
			delta = 1;
		}
		if (bus->flowcontrol & ~fcbits) {
			bus->fc_xon++;
			delta = 1;
		}

		if (delta) {
			bus->fc_rcvd++;
			bus->flowcontrol = fcbits;
		}

		/* Check and update sequence number */
		if (rxseq != seq) {
			DHD_INFO(("%s: rx_seq %d, expected %d\n", __FUNCTION__, seq, rxseq));
			bus->rx_badseq++;
			rxseq = seq;
		}

		/* Check window for sanity */
		if ((uint8)(txmax - bus->tx_seq) > 0x70) {
			DHD_ERROR(("%s: got unlikely tx max %d with tx_seq %d\n",
			           __FUNCTION__, txmax, bus->tx_seq));
			txmax = bus->tx_max;
		}
		bus->tx_max = txmax;

		/* Call a separate function for control frames */
		if (chan == SDPCM_CONTROL_CHANNEL) {
			dhdsdio_read_control(bus, bus->rxhdr, len, doff);
			continue;
		}

		ASSERT((chan == SDPCM_DATA_CHANNEL) || (chan == SDPCM_EVENT_CHANNEL) ||
		       (chan == SDPCM_TEST_CHANNEL) || (chan == SDPCM_GLOM_CHANNEL));

		/* Length to read */
		rdlen = (len > firstread) ? (len - firstread) : 0;

		/* May pad read to blocksize for efficiency */
		if (bus->roundup && bus->blocksize && (rdlen > bus->blocksize)) {
			pad = bus->blocksize - (rdlen % bus->blocksize);
			if ((pad <= bus->roundup) && (pad < bus->blocksize) &&
			    ((rdlen + pad + firstread) < MAX_RX_DATASZ))
				rdlen += pad;
		} else if (rdlen % DHD_SDALIGN) {
			rdlen += DHD_SDALIGN - (rdlen % DHD_SDALIGN);
		}

		/* Satisfy length-alignment requirements */
		if (forcealign && (rdlen & (ALIGNMENT - 1)))
			rdlen = ROUNDUP(rdlen, ALIGNMENT);

		if ((rdlen + firstread) > MAX_RX_DATASZ) {
			/* Too long -- skip this frame */
			DHD_ERROR(("%s: too long: len %d rdlen %d\n", __FUNCTION__, len, rdlen));
			bus->dhd->rx_errors++; bus->rx_toolong++;
			dhdsdio_rxfail(bus, FALSE, FALSE);
			continue;
		}

		dhd_os_sdlock_rxq(bus->dhd);
		if (!(pkt = PKTGET(osh, (rdlen + firstread + DHD_SDALIGN), FALSE))) {
			/* Give up on data, request rtx of events */
			DHD_ERROR(("%s: PKTGET failed: rdlen %d chan %d\n",
			           __FUNCTION__, rdlen, chan));
			bus->dhd->rx_dropped++;
			dhd_os_sdunlock_rxq(bus->dhd);
			dhdsdio_rxfail(bus, FALSE, RETRYCHAN(chan));
			continue;
		}
		dhd_os_sdunlock_rxq(bus->dhd);

		ASSERT(!PKTLINK(pkt));

		/* Leave room for what we already read, and align remainder */
		ASSERT(firstread < (PKTLEN(osh, pkt)));
		PKTPULL(osh, pkt, firstread);
		PKTALIGN(osh, pkt, rdlen, DHD_SDALIGN);

		/* Read the remaining frame data */
		sdret = dhd_bcmsdh_recv_buf(bus, bcmsdh_cur_sbwad(sdh), SDIO_FUNC_2, F2SYNC,
		                            ((uint8 *)PKTDATA(osh, pkt)), rdlen, pkt, NULL, NULL);
		bus->f2rxdata++;
		ASSERT(sdret != BCME_PENDING);

		if (sdret < 0) {
			DHD_ERROR(("%s: read %d %s bytes failed: %d\n", __FUNCTION__, rdlen,
			           ((chan == SDPCM_EVENT_CHANNEL) ? "event" :
			            ((chan == SDPCM_DATA_CHANNEL) ? "data" : "test")), sdret));
			dhd_os_sdlock_rxq(bus->dhd);
			PKTFREE(bus->dhd->osh, pkt, FALSE);
			dhd_os_sdunlock_rxq(bus->dhd);
			bus->dhd->rx_errors++;
			dhdsdio_rxfail(bus, TRUE, RETRYCHAN(chan));
			continue;
		}

		/* Copy the already-read portion */
		PKTPUSH(osh, pkt, firstread);
		bcopy(bus->rxhdr, PKTDATA(osh, pkt), firstread);

#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() && DHD_DATA_ON()) {
			prhex("Rx Data", PKTDATA(osh, pkt), len);
		}
#endif

deliver:
		/* Save superframe descriptor and allocate packet frame */
		if (chan == SDPCM_GLOM_CHANNEL) {
			if (SDPCM_GLOMDESC(&bus->rxhdr[SDPCM_FRAMETAG_LEN])) {
				DHD_GLOM(("%s: got glom descriptor, %d bytes:\n",
				          __FUNCTION__, len));
#ifdef DHD_DEBUG
				if (DHD_GLOM_ON()) {
					prhex("Glom Data", PKTDATA(osh, pkt), len);
				}
#endif
				PKTSETLEN(osh, pkt, len);
				ASSERT(doff == SDPCM_HDRLEN);
				PKTPULL(osh, pkt, SDPCM_HDRLEN);
				bus->glomd = pkt;
			} else {
				DHD_ERROR(("%s: glom superframe w/o descriptor!\n", __FUNCTION__));
				dhdsdio_rxfail(bus, FALSE, FALSE);
			}
			continue;
		}

		/* Fill in packet len and prio, deliver upward */
		PKTSETLEN(osh, pkt, len);
		PKTPULL(osh, pkt, doff);

#ifdef SDTEST
		/* Test channel packets are processed separately */
		if (chan == SDPCM_TEST_CHANNEL) {
			dhdsdio_testrcv(bus, pkt, seq);
			continue;
		}
#endif /* SDTEST */

		if (PKTLEN(osh, pkt) == 0) {
			dhd_os_sdlock_rxq(bus->dhd);
			PKTFREE(bus->dhd->osh, pkt, FALSE);
			dhd_os_sdunlock_rxq(bus->dhd);
			continue;
		} else if (dhd_prot_hdrpull(bus->dhd, &ifidx, pkt, reorder_info_buf,
			&reorder_info_len) != 0) {
			DHD_ERROR(("%s: rx protocol error\n", __FUNCTION__));
			dhd_os_sdlock_rxq(bus->dhd);
			PKTFREE(bus->dhd->osh, pkt, FALSE);
			dhd_os_sdunlock_rxq(bus->dhd);
			bus->dhd->rx_errors++;
			continue;
		}
		if (reorder_info_len) {
			/* Reordering info from the firmware */
			dhd_process_pkt_reorder_info(bus->dhd, reorder_info_buf, reorder_info_len,
				&pkt, &pkt_count);
			if (pkt_count == 0)
				continue;
		}
		else
			pkt_count = 1;

		/* Unlock during rx call */
		dhd_os_sdunlock(bus->dhd);
		dhd_rx_frame(bus->dhd, ifidx, pkt, pkt_count, chan);
		dhd_os_sdlock(bus->dhd);
	}
	rxcount = maxframes - rxleft;
#ifdef DHD_DEBUG
	/* Message if we hit the limit */
	if (!rxleft && !sdtest)
		DHD_DATA(("%s: hit rx limit of %d frames\n", __FUNCTION__, maxframes));
	else
#endif /* DHD_DEBUG */
	DHD_DATA(("%s: processed %d frames\n", __FUNCTION__, rxcount));
	/* Back off rxseq if awaiting rtx, update rx_seq */
	if (bus->rxskip)
		rxseq--;
	bus->rx_seq = rxseq;

	if (bus->reqbussleep)
	{
	    dhdsdio_bussleep(bus, TRUE);
		bus->reqbussleep = FALSE;
	}
	bus->readframes = FALSE;

	return rxcount;
}

static uint32
dhdsdio_hostmail(dhd_bus_t *bus)
{
	sdpcmd_regs_t *regs = bus->regs;
	uint32 intstatus = 0;
	uint32 hmb_data;
	uint8 fcbits;
	uint retries = 0;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* Read mailbox data and ack that we did so */
	R_SDREG(hmb_data, &regs->tohostmailboxdata, retries);
	if (retries <= retry_limit)
		W_SDREG(SMB_INT_ACK, &regs->tosbmailbox, retries);
	bus->f1regdata += 2;

	/* Dongle recomposed rx frames, accept them again */
	if (hmb_data & HMB_DATA_NAKHANDLED) {
		DHD_INFO(("Dongle reports NAK handled, expect rtx of %d\n", bus->rx_seq));
		if (!bus->rxskip) {
			DHD_ERROR(("%s: unexpected NAKHANDLED!\n", __FUNCTION__));
		}
		bus->rxskip = FALSE;
		intstatus |= FRAME_AVAIL_MASK(bus);
	}

	/*
	 * DEVREADY does not occur with gSPI.
	 */
	if (hmb_data & (HMB_DATA_DEVREADY | HMB_DATA_FWREADY)) {
		bus->sdpcm_ver = (hmb_data & HMB_DATA_VERSION_MASK) >> HMB_DATA_VERSION_SHIFT;
		if (bus->sdpcm_ver != SDPCM_PROT_VERSION)
			DHD_ERROR(("Version mismatch, dongle reports %d, expecting %d\n",
			           bus->sdpcm_ver, SDPCM_PROT_VERSION));
		else
			DHD_INFO(("Dongle ready, protocol version %d\n", bus->sdpcm_ver));
		/* make sure for the SDIO_DEVICE_RXDATAINT_MODE_1 corecontrol is proper */
		if ((bus->sih->buscoretype == SDIOD_CORE_ID) && (bus->sdpcmrev >= 4) &&
		    (bus->rxint_mode  == SDIO_DEVICE_RXDATAINT_MODE_1)) {
			uint32 val;

			val = R_REG(bus->dhd->osh, &bus->regs->corecontrol);
			val &= ~CC_XMTDATAAVAIL_MODE;
			val |= CC_XMTDATAAVAIL_CTRL;
			W_REG(bus->dhd->osh, &bus->regs->corecontrol, val);

			val = R_REG(bus->dhd->osh, &bus->regs->corecontrol);
		}

#ifdef DHD_DEBUG
		/* Retrieve console state address now that firmware should have updated it */
		{
			sdpcm_shared_t shared;
			if (dhdsdio_readshared(bus, &shared) == 0)
				bus->console_addr = shared.console_addr;
		}
#endif /* DHD_DEBUG */
	}

	/*
	 * Flow Control has been moved into the RX headers and this out of band
	 * method isn't used any more.  Leave this here for possibly remaining backward
	 * compatible with older dongles
	 */
	if (hmb_data & HMB_DATA_FC) {
		fcbits = (hmb_data & HMB_DATA_FCDATA_MASK) >> HMB_DATA_FCDATA_SHIFT;

		if (fcbits & ~bus->flowcontrol)
			bus->fc_xoff++;
		if (bus->flowcontrol & ~fcbits)
			bus->fc_xon++;

		bus->fc_rcvd++;
		bus->flowcontrol = fcbits;
	}

#ifdef DHD_DEBUG
	/* At least print a message if FW halted */
	if (hmb_data & HMB_DATA_FWHALT) {
		DHD_ERROR(("INTERNAL ERROR: FIRMWARE HALTED : set BUS DOWN\n"));
		dhdsdio_checkdied(bus, NULL, 0);
		bus->dhd->busstate = DHD_BUS_DOWN;
	}
#endif /* DHD_DEBUG */

	/* Shouldn't be any others */
	if (hmb_data & ~(HMB_DATA_DEVREADY |
	                 HMB_DATA_FWHALT |
	                 HMB_DATA_NAKHANDLED |
	                 HMB_DATA_FC |
	                 HMB_DATA_FWREADY |
	                 HMB_DATA_FCDATA_MASK |
	                 HMB_DATA_VERSION_MASK)) {
		DHD_ERROR(("Unknown mailbox data content: 0x%02x\n", hmb_data));
	}

	return intstatus;
}

static bool
dhdsdio_dpc(dhd_bus_t *bus)
{
	bcmsdh_info_t *sdh = bus->sdh;
	sdpcmd_regs_t *regs = bus->regs;
	uint32 intstatus, newstatus = 0;
	uint retries = 0;
	uint rxlimit = dhd_rxbound; /* Rx frames to read before resched */
	uint txlimit = dhd_txbound; /* Tx frames to send before resched */
	uint framecnt = 0;		  /* Temporary counter of tx/rx frames */
	bool rxdone = TRUE;		  /* Flag for no more read data */
	bool resched = FALSE;	  /* Flag indicating resched wanted */
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	dhd_os_sdlock(bus->dhd);

	if (bus->dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s: Bus down, ret\n", __FUNCTION__));
		bus->intstatus = 0;
		dhd_os_sdunlock(bus->dhd);
		return 0;
	}

	/* Start with leftover status bits */
	intstatus = bus->intstatus;

	if (!SLPAUTO_ENAB(bus) && !KSO_ENAB(bus)) {
		DHD_ERROR(("%s: Device asleep\n", __FUNCTION__));
		goto exit;
	}

	/* If waiting for HTAVAIL, check status */
	if (!SLPAUTO_ENAB(bus) && (bus->clkstate == CLK_PENDING)) {
		int err;
		uint8 clkctl, devctl = 0;

#ifdef DHD_DEBUG
		/* Check for inconsistent device control */
		devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
		if (err) {
			DHD_ERROR(("%s: error reading DEVCTL: %d\n", __FUNCTION__, err));
			bus->dhd->busstate = DHD_BUS_DOWN;
		} else {
			ASSERT(devctl & SBSDIO_DEVCTL_CA_INT_ONLY);
		}
#endif /* DHD_DEBUG */

		/* Read CSR, if clock on switch to AVAIL, else ignore */
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);
		if (err) {
			DHD_ERROR(("%s: error reading CSR: %d\n", __FUNCTION__, err));
			bus->dhd->busstate = DHD_BUS_DOWN;
		}

		DHD_INFO(("DPC: PENDING, devctl 0x%02x clkctl 0x%02x\n", devctl, clkctl));

		if (SBSDIO_HTAV(clkctl)) {
			devctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, &err);
			if (err) {
				DHD_ERROR(("%s: error reading DEVCTL: %d\n",
				           __FUNCTION__, err));
				bus->dhd->busstate = DHD_BUS_DOWN;
			}
			devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_DEVICE_CTL, devctl, &err);
			if (err) {
				DHD_ERROR(("%s: error writing DEVCTL: %d\n",
				           __FUNCTION__, err));
				bus->dhd->busstate = DHD_BUS_DOWN;
			}
			bus->clkstate = CLK_AVAIL;
		} else {
			goto clkwait;
		}
	}

	BUS_WAKE(bus);

	/* Make sure backplane clock is on */
	dhdsdio_clkctl(bus, CLK_AVAIL, TRUE);
	if (bus->clkstate != CLK_AVAIL)
		goto clkwait;

	/* Pending interrupt indicates new device status */
	if (bus->ipend) {
		bus->ipend = FALSE;
		R_SDREG(newstatus, &regs->intstatus, retries);
		bus->f1regdata++;
		if (bcmsdh_regfail(bus->sdh))
			newstatus = 0;
		newstatus &= bus->hostintmask;
		bus->fcstate = !!(newstatus & I_HMB_FC_STATE);
		if (newstatus) {
			bus->f1regdata++;
			if ((bus->rxint_mode == SDIO_DEVICE_RXDATAINT_MODE_0) &&
				(newstatus == I_XMTDATA_AVAIL)) {
			}
			else
				W_SDREG(newstatus, &regs->intstatus, retries);
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
		bus->fcstate = !!(newstatus & (I_HMB_FC_STATE | I_HMB_FC_CHANGE));
		intstatus |= (newstatus & bus->hostintmask);
	}

	/* Just being here means nothing more to do for chipactive */
	if (intstatus & I_CHIPACTIVE) {
		/* ASSERT(bus->clkstate == CLK_AVAIL); */
		intstatus &= ~I_CHIPACTIVE;
	}

	/* Handle host mailbox indication */
	if (intstatus & I_HMB_HOST_INT) {
		intstatus &= ~I_HMB_HOST_INT;
		intstatus |= dhdsdio_hostmail(bus);
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

	if (intstatus & I_HMB_FC_STATE) {
		DHD_INFO(("Dongle reports HMB_FC_STATE\n"));
		intstatus &= ~I_HMB_FC_STATE;
	}

	/* Ignore frame indications if rxskip is set */
	if (bus->rxskip) {
		intstatus &= ~FRAME_AVAIL_MASK(bus);
	}

	/* On frame indication, read available frames */
	if (PKT_AVAILABLE(bus, intstatus)) {
		framecnt = dhdsdio_readframes(bus, rxlimit, &rxdone);
		if (rxdone || bus->rxskip)
			intstatus  &= ~FRAME_AVAIL_MASK(bus);
		rxlimit -= MIN(framecnt, rxlimit);
	}

	/* Keep still-pending events for next scheduling */
	bus->intstatus = intstatus;

clkwait:
	/* Re-enable interrupts to detect new device events (mailbox, rx frame)
	 * or clock availability.  (Allows tx loop to check ipend if desired.)
	 * (Unless register access seems hosed, as we may not be able to ACK...)
	 */
	if (bus->intr && bus->intdis && !bcmsdh_regfail(sdh)) {
		DHD_INTR(("%s: enable SDIO interrupts, rxdone %d framecnt %d\n",
		          __FUNCTION__, rxdone, framecnt));
		bus->intdis = FALSE;
#if defined(OOB_INTR_ONLY)
		OOB_PARAM_IF(!dhd_oob_disable) {
			bcmsdh_oob_intr_set(bus->sdh, TRUE);
		}
#endif /* defined(OOB_INTR_ONLY) */
		bcmsdh_intr_enable(sdh);
	}

#if defined(OOB_INTR_ONLY) && !defined(HW_OOB)
	OOB_PARAM_IF(!dhd_oob_disable) {
		/* In case of SW-OOB(using edge trigger),
		 * Check interrupt status in the dongle again after enable irq on the host.
		 * and rechedule dpc if interrupt is pended in the dongle.
		 * There is a chance to miss OOB interrupt while irq is disabled on the host.
		 * No need to do this with HW-OOB(level trigger)
		 */
		R_SDREG(newstatus, &regs->intstatus, retries);
		if (bcmsdh_regfail(bus->sdh))
			newstatus = 0;
		if (newstatus & bus->hostintmask) {
			bus->ipend = TRUE;
			resched = TRUE;
		}
	}
#endif /* defined(OOB_INTR_ONLY) && !defined(HW_OOB) */

#ifdef PROP_TXSTATUS
	dhd_wlfc_commit_packets(bus->dhd, (f_commitpkt_t)dhd_bus_txdata, (void *)bus, NULL, FALSE);
#endif

	if (TXCTLOK(bus) && bus->ctrl_frame_stat && (bus->clkstate == CLK_AVAIL))
		dhdsdio_sendpendctl(bus);

	/* Send queued frames (limit 1 if rx may still be pending) */
	else if ((bus->clkstate == CLK_AVAIL) && !bus->fcstate &&
	    pktq_mlen(&bus->txq, ~bus->flowcontrol) && txlimit && DATAOK(bus)) {
		framecnt = rxdone ? txlimit : MIN(txlimit, dhd_txminmax);
		framecnt = dhdsdio_sendfromq(bus, framecnt);
		txlimit -= framecnt;
	}
	/* Resched the DPC if ctrl cmd is pending on bus credit */
	if (bus->ctrl_frame_stat)
		resched = TRUE;

	/* Resched if events or tx frames are pending, else await next interrupt */
	/* On failed register access, all bets are off: no resched or interrupts */
	if ((bus->dhd->busstate == DHD_BUS_DOWN) || bcmsdh_regfail(sdh)) {
		if ((bus->sih && bus->sih->buscorerev >= 12) && !(dhdsdio_sleepcsr_get(bus) &
			SBSDIO_FUNC1_SLEEPCSR_KSO_MASK)) {
			/* Bus failed because of KSO */
			DHD_ERROR(("%s: Bus failed due to KSO\n", __FUNCTION__));
			bus->kso = FALSE;
		} else {
			DHD_ERROR(("%s: failed backplane access over SDIO, halting operation\n",
				__FUNCTION__));
			bus->dhd->busstate = DHD_BUS_DOWN;
			bus->intstatus = 0;
		}
	} else if (bus->clkstate == CLK_PENDING) {
		/* Awaiting I_CHIPACTIVE; don't resched */
	} else if (bus->intstatus || bus->ipend ||
	           (!bus->fcstate && pktq_mlen(&bus->txq, ~bus->flowcontrol) && DATAOK(bus)) ||
			PKT_AVAILABLE(bus, bus->intstatus)) {  /* Read multiple frames */
		resched = TRUE;
	}

	bus->dpc_sched = resched;

	/* If we're done for now, turn off clock request. */
	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && (bus->clkstate != CLK_PENDING)) {
		bus->activity = FALSE;
		dhdsdio_clkctl(bus, CLK_NONE, FALSE);
	}

exit:

	if (!resched && dhd_dpcpoll) {
		if (dhdsdio_readframes(bus, dhd_rxbound, &rxdone) != 0) {
			resched = TRUE;
		}
	}

	dhd_os_sdunlock(bus->dhd);
	return resched;
}

bool
dhd_bus_dpc(struct dhd_bus *bus)
{
	bool resched;

	/* Call the DPC directly. */
	DHD_TRACE(("Calling dhdsdio_dpc() from %s\n", __FUNCTION__));
	resched = dhdsdio_dpc(bus);

	return resched;
}

void
dhdsdio_isr(void *arg)
{
	dhd_bus_t *bus = (dhd_bus_t*)arg;
	bcmsdh_info_t *sdh;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (!bus) {
		DHD_ERROR(("%s : bus is null pointer , exit \n", __FUNCTION__));
		return;
	}
	sdh = bus->sdh;

	if (bus->dhd->busstate == DHD_BUS_DOWN) {
		DHD_ERROR(("%s : bus is down. we have nothing to do\n", __FUNCTION__));
		return;
	}

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	/* Count the interrupt call */
	bus->intrcount++;
	bus->ipend = TRUE;

	/* Shouldn't get this interrupt if we're sleeping? */
	if (!SLPAUTO_ENAB(bus)) {
		if (bus->sleeping) {
			DHD_ERROR(("INTERRUPT WHILE SLEEPING??\n"));
			return;
		} else if (!KSO_ENAB(bus)) {
			DHD_ERROR(("ISR in devsleep 1\n"));
		}
	}

	/* Disable additional interrupts (is this needed now)? */
	if (bus->intr) {
		DHD_INTR(("%s: disable SDIO interrupts\n", __FUNCTION__));
	} else {
		DHD_ERROR(("dhdsdio_isr() w/o interrupt configured!\n"));
	}

	bcmsdh_intr_disable(sdh);
	bus->intdis = TRUE;

#if defined(SDIO_ISR_THREAD) || defined(OOB_PARAM)
	OOB_PARAM_IF(dhd_oob_disable) {
		DHD_TRACE(("Calling dhdsdio_dpc() from %s\n", __FUNCTION__));
		DHD_OS_WAKE_LOCK(bus->dhd);
		dhdsdio_dpc(bus);
		DHD_OS_WAKE_UNLOCK(bus->dhd);
	} OOB_PARAM_ELSE()
#endif /* defined(SDIO_ISR_THREAD) || defined(OOB_PARAM) */
#if !defined(SDIO_ISR_THREAD) || defined(OOB_PARAM)
	{
		bus->dpc_sched = TRUE;
		dhd_sched_dpc(bus->dhd);
	}
#endif /* !defined(SDIO_ISR_THREAD) || defined(OOB_PARAM) */


}

#ifdef SDTEST
static void
dhdsdio_pktgen_init(dhd_bus_t *bus)
{
	/* Default to specified length, or full range */
	if (dhd_pktgen_len) {
		bus->pktgen_maxlen = MIN(dhd_pktgen_len, MAX_PKTGEN_LEN);
		bus->pktgen_minlen = bus->pktgen_maxlen;
	} else {
		bus->pktgen_maxlen = MAX_PKTGEN_LEN;
		bus->pktgen_minlen = 0;
	}
	bus->pktgen_len = (uint16)bus->pktgen_minlen;

	/* Default to per-watchdog burst with 10s print time */
	bus->pktgen_freq = 1;
	bus->pktgen_print = dhd_watchdog_ms ? (10000 / dhd_watchdog_ms) : 0;
	bus->pktgen_count = (dhd_pktgen * dhd_watchdog_ms + 999) / 1000;

	/* Default to echo mode */
	bus->pktgen_mode = DHD_PKTGEN_ECHO;
	bus->pktgen_stop = 1;
}

static void
dhdsdio_pktgen(dhd_bus_t *bus)
{
	void *pkt;
	uint8 *data;
	uint pktcount;
	uint fillbyte;
	osl_t *osh = bus->dhd->osh;
	uint16 len;
	ulong time_lapse;
	uint sent_pkts;
	uint rcvd_pkts;

	/* Display current count if appropriate */
	if (bus->pktgen_print && (++bus->pktgen_ptick >= bus->pktgen_print)) {
		bus->pktgen_ptick = 0;
		printf("%s: send attempts %d, rcvd %d, errors %d\n",
		       __FUNCTION__, bus->pktgen_sent, bus->pktgen_rcvd, bus->pktgen_fail);

		/* Print throughput stats only for constant length packet runs */
		if (bus->pktgen_minlen == bus->pktgen_maxlen) {
			time_lapse = jiffies - bus->pktgen_prev_time;
			bus->pktgen_prev_time = jiffies;
			sent_pkts = bus->pktgen_sent - bus->pktgen_prev_sent;
			bus->pktgen_prev_sent = bus->pktgen_sent;
			rcvd_pkts = bus->pktgen_rcvd - bus->pktgen_prev_rcvd;
			bus->pktgen_prev_rcvd = bus->pktgen_rcvd;

			printf("%s: Tx Throughput %d kbps, Rx Throughput %d kbps\n",
			  __FUNCTION__,
			  (sent_pkts * bus->pktgen_len / jiffies_to_msecs(time_lapse)) * 8,
			  (rcvd_pkts * bus->pktgen_len  / jiffies_to_msecs(time_lapse)) * 8);
		}
	}

	/* For recv mode, just make sure dongle has started sending */
	if (bus->pktgen_mode == DHD_PKTGEN_RECV) {
		if (bus->pktgen_rcv_state == PKTGEN_RCV_IDLE) {
			bus->pktgen_rcv_state = PKTGEN_RCV_ONGOING;
			dhdsdio_sdtest_set(bus, bus->pktgen_total);
		}
		return;
	}

	/* Otherwise, generate or request the specified number of packets */
	for (pktcount = 0; pktcount < bus->pktgen_count; pktcount++) {
		/* Stop if total has been reached */
		if (bus->pktgen_total && (bus->pktgen_sent >= bus->pktgen_total)) {
			bus->pktgen_count = 0;
			break;
		}

		/* Allocate an appropriate-sized packet */
		if (bus->pktgen_mode == DHD_PKTGEN_RXBURST) {
			len = SDPCM_TEST_PKT_CNT_FLD_LEN;
		} else {
			len = bus->pktgen_len;
		}
		if (!(pkt = PKTGET(osh, (len + SDPCM_HDRLEN + SDPCM_TEST_HDRLEN + DHD_SDALIGN),
		                   TRUE))) {;
			DHD_ERROR(("%s: PKTGET failed!\n", __FUNCTION__));
			break;
		}
		PKTALIGN(osh, pkt, (len + SDPCM_HDRLEN + SDPCM_TEST_HDRLEN), DHD_SDALIGN);
		data = (uint8*)PKTDATA(osh, pkt) + SDPCM_HDRLEN;

		/* Write test header cmd and extra based on mode */
		switch (bus->pktgen_mode) {
		case DHD_PKTGEN_ECHO:
			*data++ = SDPCM_TEST_ECHOREQ;
			*data++ = (uint8)bus->pktgen_sent;
			break;

		case DHD_PKTGEN_SEND:
			*data++ = SDPCM_TEST_DISCARD;
			*data++ = (uint8)bus->pktgen_sent;
			break;

		case DHD_PKTGEN_RXBURST:
			*data++ = SDPCM_TEST_BURST;
			*data++ = (uint8)bus->pktgen_count; /* Just for backward compatability */
			break;

		default:
			DHD_ERROR(("Unrecognized pktgen mode %d\n", bus->pktgen_mode));
			PKTFREE(osh, pkt, TRUE);
			bus->pktgen_count = 0;
			return;
		}

		/* Write test header length field */
		*data++ = (bus->pktgen_len >> 0);
		*data++ = (bus->pktgen_len >> 8);

		/* Write frame count in a 4 byte field adjucent to SDPCM test header for
		 * burst mode
		 */
		if (bus->pktgen_mode == DHD_PKTGEN_RXBURST) {
			*data++ = (uint8)(bus->pktgen_count >> 0);
			*data++ = (uint8)(bus->pktgen_count >> 8);
			*data++ = (uint8)(bus->pktgen_count >> 16);
			*data++ = (uint8)(bus->pktgen_count >> 24);
		} else {

			/* Then fill in the remainder -- N/A for burst */
			for (fillbyte = 0; fillbyte < len; fillbyte++)
				*data++ = SDPCM_TEST_FILL(fillbyte, (uint8)bus->pktgen_sent);
		}

#ifdef DHD_DEBUG
		if (DHD_BYTES_ON() && DHD_DATA_ON()) {
			data = (uint8*)PKTDATA(osh, pkt) + SDPCM_HDRLEN;
			prhex("dhdsdio_pktgen: Tx Data", data, PKTLEN(osh, pkt) - SDPCM_HDRLEN);
		}
#endif

		/* Send it */
		if (dhdsdio_txpkt(bus, SDPCM_TEST_CHANNEL, &pkt, 1, TRUE) != BCME_OK) {
			bus->pktgen_fail++;
			if (bus->pktgen_stop && bus->pktgen_stop == bus->pktgen_fail)
				bus->pktgen_count = 0;
		}
		bus->pktgen_sent++;

		/* Bump length if not fixed, wrap at max */
		if (++bus->pktgen_len > bus->pktgen_maxlen)
			bus->pktgen_len = (uint16)bus->pktgen_minlen;

		/* Special case for burst mode: just send one request! */
		if (bus->pktgen_mode == DHD_PKTGEN_RXBURST)
			break;
	}
}

static void
dhdsdio_sdtest_set(dhd_bus_t *bus, uint count)
{
	void *pkt;
	uint8 *data;
	osl_t *osh = bus->dhd->osh;

	/* Allocate the packet */
	if (!(pkt = PKTGET(osh, SDPCM_HDRLEN + SDPCM_TEST_HDRLEN +
		SDPCM_TEST_PKT_CNT_FLD_LEN + DHD_SDALIGN, TRUE))) {
		DHD_ERROR(("%s: PKTGET failed!\n", __FUNCTION__));
		return;
	}
	PKTALIGN(osh, pkt, (SDPCM_HDRLEN + SDPCM_TEST_HDRLEN +
		SDPCM_TEST_PKT_CNT_FLD_LEN), DHD_SDALIGN);
	data = (uint8*)PKTDATA(osh, pkt) + SDPCM_HDRLEN;

	/* Fill in the test header */
	*data++ = SDPCM_TEST_SEND;
	*data++ = (count > 0)?TRUE:FALSE;
	*data++ = (bus->pktgen_maxlen >> 0);
	*data++ = (bus->pktgen_maxlen >> 8);
	*data++ = (uint8)(count >> 0);
	*data++ = (uint8)(count >> 8);
	*data++ = (uint8)(count >> 16);
	*data++ = (uint8)(count >> 24);

	/* Send it */
	if (dhdsdio_txpkt(bus, SDPCM_TEST_CHANNEL, &pkt, 1, TRUE) != BCME_OK)
		bus->pktgen_fail++;
}


static void
dhdsdio_testrcv(dhd_bus_t *bus, void *pkt, uint seq)
{
	osl_t *osh = bus->dhd->osh;
	uint8 *data;
	uint pktlen;

	uint8 cmd;
	uint8 extra;
	uint16 len;
	uint16 offset;

	/* Check for min length */
	if ((pktlen = PKTLEN(osh, pkt)) < SDPCM_TEST_HDRLEN) {
		DHD_ERROR(("dhdsdio_restrcv: toss runt frame, pktlen %d\n", pktlen));
		PKTFREE(osh, pkt, FALSE);
		return;
	}

	/* Extract header fields */
	data = PKTDATA(osh, pkt);
	cmd = *data++;
	extra = *data++;
	len = *data++; len += *data++ << 8;
	DHD_TRACE(("%s:cmd:%d, xtra:%d,len:%d\n", __FUNCTION__, cmd, extra, len));
	/* Check length for relevant commands */
	if (cmd == SDPCM_TEST_DISCARD || cmd == SDPCM_TEST_ECHOREQ || cmd == SDPCM_TEST_ECHORSP) {
		if (pktlen != len + SDPCM_TEST_HDRLEN) {
			DHD_ERROR(("dhdsdio_testrcv: frame length mismatch, pktlen %d seq %d"
			           " cmd %d extra %d len %d\n", pktlen, seq, cmd, extra, len));
			PKTFREE(osh, pkt, FALSE);
			return;
		}
	}

	/* Process as per command */
	switch (cmd) {
	case SDPCM_TEST_ECHOREQ:
		/* Rx->Tx turnaround ok (even on NDIS w/current implementation) */
		*(uint8 *)(PKTDATA(osh, pkt)) = SDPCM_TEST_ECHORSP;
		if (dhdsdio_txpkt(bus, SDPCM_TEST_CHANNEL, &pkt, 1, TRUE) == BCME_OK) {
			bus->pktgen_sent++;
		} else {
			bus->pktgen_fail++;
			PKTFREE(osh, pkt, FALSE);
		}
		bus->pktgen_rcvd++;
		break;

	case SDPCM_TEST_ECHORSP:
		if (bus->ext_loop) {
			PKTFREE(osh, pkt, FALSE);
			bus->pktgen_rcvd++;
			break;
		}

		for (offset = 0; offset < len; offset++, data++) {
			if (*data != SDPCM_TEST_FILL(offset, extra)) {
				DHD_ERROR(("dhdsdio_testrcv: echo data mismatch: "
				           "offset %d (len %d) expect 0x%02x rcvd 0x%02x\n",
				           offset, len, SDPCM_TEST_FILL(offset, extra), *data));
				break;
			}
		}
		PKTFREE(osh, pkt, FALSE);
		bus->pktgen_rcvd++;
		break;

	case SDPCM_TEST_DISCARD:
		{
			int i = 0;
			uint8 *prn = data;
			uint8 testval = extra;
			for (i = 0; i < len; i++) {
				if (*prn != testval) {
					DHD_ERROR(("DIErr@Pkt#:%d,Ix:%d, expected:0x%x, got:0x%x\n",
						i, bus->pktgen_rcvd_rcvsession, testval, *prn));
					prn++; testval++;
				}
			}
		}
		PKTFREE(osh, pkt, FALSE);
		bus->pktgen_rcvd++;
		break;

	case SDPCM_TEST_BURST:
	case SDPCM_TEST_SEND:
	default:
		DHD_INFO(("dhdsdio_testrcv: unsupported or unknown command, pktlen %d seq %d"
		          " cmd %d extra %d len %d\n", pktlen, seq, cmd, extra, len));
		PKTFREE(osh, pkt, FALSE);
		break;
	}

	/* For recv mode, stop at limit (and tell dongle to stop sending) */
	if (bus->pktgen_mode == DHD_PKTGEN_RECV) {
		if (bus->pktgen_rcv_state != PKTGEN_RCV_IDLE) {
			bus->pktgen_rcvd_rcvsession++;

			if (bus->pktgen_total &&
				(bus->pktgen_rcvd_rcvsession >= bus->pktgen_total)) {
			bus->pktgen_count = 0;
			DHD_ERROR(("Pktgen:rcv test complete!\n"));
			bus->pktgen_rcv_state = PKTGEN_RCV_IDLE;
			dhdsdio_sdtest_set(bus, FALSE);
				bus->pktgen_rcvd_rcvsession = 0;
			}
		}
	}
}
#endif /* SDTEST */

int dhd_bus_oob_intr_register(dhd_pub_t *dhdp)
{
	int err = 0;

#if defined(OOB_INTR_ONLY)
	OOB_PARAM_IF(!dhd_oob_disable) {
		err = bcmsdh_oob_intr_register(dhdp->bus->sdh, dhdsdio_isr, dhdp->bus);
	}
#endif
	return err;
}

void dhd_bus_oob_intr_unregister(dhd_pub_t *dhdp)
{
#if defined(OOB_INTR_ONLY)
	OOB_PARAM_IF(!dhd_oob_disable) {
		bcmsdh_oob_intr_unregister(dhdp->bus->sdh);
	}
#endif
}

void dhd_bus_oob_intr_set(dhd_pub_t *dhdp, bool enable)
{
#if defined(OOB_INTR_ONLY)
	OOB_PARAM_IF(!dhd_oob_disable) {
		bcmsdh_oob_intr_set(dhdp->bus->sdh, enable);
	}
#endif
}

void dhd_bus_dev_pm_stay_awake(dhd_pub_t *dhdpub)
{
	bcmsdh_dev_pm_stay_awake(dhdpub->bus->sdh);
}

void dhd_bus_dev_pm_relax(dhd_pub_t *dhdpub)
{
	bcmsdh_dev_relax(dhdpub->bus->sdh);
}

bool dhd_bus_dev_pm_enabled(dhd_pub_t *dhdpub)
{
	bool enabled = FALSE;

	enabled = bcmsdh_dev_pm_enabled(dhdpub->bus->sdh);
	return enabled;
}

extern bool
dhd_bus_watchdog(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus;

	DHD_TIMER(("%s: Enter\n", __FUNCTION__));

	bus = dhdp->bus;

	if (bus->dhd->dongle_reset)
		return FALSE;

	if (bus->dhd->hang_was_sent) {
		dhd_os_wd_timer(bus->dhd, 0);
		return FALSE;
	}

	/* Ignore the timer if simulating bus down */
	if (!SLPAUTO_ENAB(bus) && bus->sleeping)
		return FALSE;

	if (dhdp->busstate == DHD_BUS_DOWN)
		return FALSE;

	/* Poll period: check device if appropriate. */
	if (!SLPAUTO_ENAB(bus) && (bus->poll && (++bus->polltick >= bus->pollrate))) {
		uint32 intstatus = 0;

		/* Reset poll tick */
		bus->polltick = 0;

		/* Check device if no interrupts */
		if (!bus->intr || (bus->intrcount == bus->lastintrs)) {

			if (!bus->dpc_sched) {
				uint8 devpend;
				devpend = bcmsdh_cfg_read(bus->sdh, SDIO_FUNC_0,
				                          SDIOD_CCCR_INTPEND, NULL);
				intstatus = devpend & (INTR_STATUS_FUNC1 | INTR_STATUS_FUNC2);
			}

			/* If there is something, make like the ISR and schedule the DPC */
			if (intstatus) {
				bus->pollcnt++;
				bus->ipend = TRUE;
				if (bus->intr) {
					bcmsdh_intr_disable(bus->sdh);
				}
				bus->dpc_sched = TRUE;
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
			if (SLPAUTO_ENAB(bus))
				dhdsdio_bussleep(bus, FALSE);
			else
			dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
			if (dhdsdio_readconsole(bus) < 0)
				dhd_console_ms = 0;	/* On error, stop trying */
		}
	}
#endif /* DHD_DEBUG */

#ifdef SDTEST
	/* Generate packets if configured */
	if (bus->pktgen_count && (++bus->pktgen_tick >= bus->pktgen_freq)) {
		/* Make sure backplane clock is on */
		if (SLPAUTO_ENAB(bus))
			dhdsdio_bussleep(bus, FALSE);
		else
			dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
		bus->pktgen_tick = 0;
		dhdsdio_pktgen(bus);
	}
#endif

	/* On idle timeout clear activity flag and/or turn off clock */
#ifdef DHD_USE_IDLECOUNT
	if (bus->activity)
		bus->activity = FALSE;
	else {
		bus->idlecount++;

		if ((bus->idletime > 0) && (bus->idlecount >= bus->idletime)) {
			DHD_TIMER(("%s: DHD Idle state!!\n", __FUNCTION__));
			if (SLPAUTO_ENAB(bus)) {
				if (dhdsdio_bussleep(bus, TRUE) != BCME_BUSY)
					dhd_os_wd_timer(bus->dhd, 0);
			} else
				dhdsdio_clkctl(bus, CLK_NONE, FALSE);

			bus->idlecount = 0;
		}
	}
#else
	if ((bus->idletime > 0) && (bus->clkstate == CLK_AVAIL)) {
		if (++bus->idlecount >= bus->idletime) {
			bus->idlecount = 0;
			if (bus->activity) {
				bus->activity = FALSE;
				if (SLPAUTO_ENAB(bus)) {
					if (!bus->readframes)
						dhdsdio_bussleep(bus, TRUE);
					else
						bus->reqbussleep = TRUE;
				}
				else
					dhdsdio_clkctl(bus, CLK_NONE, FALSE);
			}
		}
	}
#endif /* DHD_USE_IDLECOUNT */

	return bus->ipend;
}

#ifdef DHD_DEBUG
extern int
dhd_bus_console_in(dhd_pub_t *dhdp, uchar *msg, uint msglen)
{
	dhd_bus_t *bus = dhdp->bus;
	uint32 addr, val;
	int rv;
	void *pkt;

	/* Address could be zero if CONSOLE := 0 in dongle Makefile */
	if (bus->console_addr == 0)
		return BCME_UNSUPPORTED;

	/* Exclusive bus access */
	dhd_os_sdlock(bus->dhd);

	/* Don't allow input if dongle is in reset */
	if (bus->dhd->dongle_reset) {
		dhd_os_sdunlock(bus->dhd);
		return BCME_NOTREADY;
	}

	/* Request clock to allow SDIO accesses */
	BUS_WAKE(bus);
	/* No pend allowed since txpkt is called later, ht clk has to be on */
	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

	/* Zero cbuf_index */
	addr = bus->console_addr + OFFSETOF(hndrte_cons_t, cbuf_idx);
	val = htol32(0);
	if ((rv = dhdsdio_membytes(bus, TRUE, addr, (uint8 *)&val, sizeof(val))) < 0)
		goto done;

	/* Write message into cbuf */
	addr = bus->console_addr + OFFSETOF(hndrte_cons_t, cbuf);
	if ((rv = dhdsdio_membytes(bus, TRUE, addr, (uint8 *)msg, msglen)) < 0)
		goto done;

	/* Write length into vcons_in */
	addr = bus->console_addr + OFFSETOF(hndrte_cons_t, vcons_in);
	val = htol32(msglen);
	if ((rv = dhdsdio_membytes(bus, TRUE, addr, (uint8 *)&val, sizeof(val))) < 0)
		goto done;

	/* Bump dongle by sending an empty packet on the event channel.
	 * sdpcm_sendup (RX) checks for virtual console input.
	 */
	if ((pkt = PKTGET(bus->dhd->osh, 4 + SDPCM_RESERVE, TRUE)) != NULL)
		rv = dhdsdio_txpkt(bus, SDPCM_EVENT_CHANNEL, &pkt, 1, TRUE);

done:
	if ((bus->idletime == DHD_IDLE_IMMEDIATE) && !bus->dpc_sched) {
		bus->activity = FALSE;
		dhdsdio_clkctl(bus, CLK_NONE, TRUE);
	}

	dhd_os_sdunlock(bus->dhd);

	return rv;
}
#endif /* DHD_DEBUG */

#ifdef DHD_DEBUG
static void
dhd_dump_cis(uint fn, uint8 *cis)
{
	uint byte, tag, tdata;
	DHD_INFO(("Function %d CIS:\n", fn));

	for (tdata = byte = 0; byte < SBSDIO_CIS_SIZE_LIMIT; byte++) {
		if ((byte % 16) == 0)
			DHD_INFO(("    "));
		DHD_INFO(("%02x ", cis[byte]));
		if ((byte % 16) == 15)
			DHD_INFO(("\n"));
		if (!tdata--) {
			tag = cis[byte];
			if (tag == 0xff)
				break;
			else if (!tag)
				tdata = 0;
			else if ((byte + 1) < SBSDIO_CIS_SIZE_LIMIT)
				tdata = cis[byte + 1] + 1;
			else
				DHD_INFO(("]"));
		}
	}
	if ((byte % 16) != 15)
		DHD_INFO(("\n"));
}
#endif /* DHD_DEBUG */

static bool
dhdsdio_chipmatch(uint16 chipid)
{
	if (chipid == BCM4325_CHIP_ID)
		return TRUE;
	if (chipid == BCM4329_CHIP_ID)
		return TRUE;
	if (chipid == BCM4315_CHIP_ID)
		return TRUE;
	if (chipid == BCM4319_CHIP_ID)
		return TRUE;
	if (chipid == BCM4336_CHIP_ID)
		return TRUE;
	if (chipid == BCM4330_CHIP_ID)
		return TRUE;
	if (chipid == BCM43237_CHIP_ID)
		return TRUE;
	if (chipid == BCM43362_CHIP_ID)
		return TRUE;
	if (chipid == BCM4314_CHIP_ID)
		return TRUE;
	if (chipid == BCM43242_CHIP_ID)
		return TRUE;
	if (chipid == BCM43340_CHIP_ID)
		return TRUE;
	if (chipid == BCM43341_CHIP_ID)
		return TRUE;
	if (chipid == BCM43143_CHIP_ID)
		return TRUE;
	if (chipid == BCM43342_CHIP_ID)
		return TRUE;
	if (chipid == BCM4334_CHIP_ID)
		return TRUE;
	if (chipid == BCM43239_CHIP_ID)
		return TRUE;
	if (chipid == BCM4324_CHIP_ID)
		return TRUE;
	if (chipid == BCM4335_CHIP_ID)
		return TRUE;
	if (chipid == BCM4339_CHIP_ID)
		return TRUE;
	if (chipid == BCM43349_CHIP_ID)
		return TRUE;
	if (chipid == BCM4345_CHIP_ID)
		return TRUE;
	if (chipid == BCM4350_CHIP_ID)
		return TRUE;
	if (chipid == BCM4354_CHIP_ID)
		return TRUE;
	if (chipid == BCM4356_CHIP_ID)
		return TRUE;
	if (chipid == BCM43430_CHIP_ID)
		return TRUE;
	return FALSE;
}

static void *
dhdsdio_probe(uint16 venid, uint16 devid, uint16 bus_no, uint16 slot,
	uint16 func, uint bustype, void *regsva, osl_t * osh, void *sdh)
{
	int ret;
	dhd_bus_t *bus;


	/* Init global variables at run-time, not as part of the declaration.
	 * This is required to support init/de-init of the driver. Initialization
	 * of globals as part of the declaration results in non-deterministic
	 * behavior since the value of the globals may be different on the
	 * first time that the driver is initialized vs subsequent initializations.
	 */
	dhd_txbound = DHD_TXBOUND;
	dhd_rxbound = DHD_RXBOUND;
	dhd_alignctl = TRUE;
	sd1idle = TRUE;
	dhd_readahead = TRUE;
	retrydata = FALSE;

	dhd_doflow = FALSE;
	dhd_dongle_ramsize = 0;
	dhd_txminmax = DHD_TXMINMAX;

	forcealign = TRUE;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	DHD_INFO(("%s: venid 0x%04x devid 0x%04x\n", __FUNCTION__, venid, devid));

	/* We make assumptions about address window mappings */
	ASSERT((uintptr)regsva == SI_ENUM_BASE);

	/* BCMSDH passes venid and devid based on CIS parsing -- but low-power start
	 * means early parse could fail, so here we should get either an ID
	 * we recognize OR (-1) indicating we must request power first.
	 */
	/* Check the Vendor ID */
	switch (venid) {
		case 0x0000:
		case VENDOR_BROADCOM:
			break;
		default:
			DHD_ERROR(("%s: unknown vendor: 0x%04x\n",
			           __FUNCTION__, venid));
			goto forcereturn;
	}

	/* Check the Device ID and make sure it's one that we support */
	switch (devid) {
		case BCM4325_D11DUAL_ID:		/* 4325 802.11a/g id */
		case BCM4325_D11G_ID:			/* 4325 802.11g 2.4Ghz band id */
		case BCM4325_D11A_ID:			/* 4325 802.11a 5Ghz band id */
			DHD_INFO(("%s: found 4325 Dongle\n", __FUNCTION__));
			break;
		case BCM4329_D11N_ID:		/* 4329 802.11n dualband device */
		case BCM4329_D11N2G_ID:		/* 4329 802.11n 2.4G device */
		case BCM4329_D11N5G_ID:		/* 4329 802.11n 5G device */
		case 0x4329:
			DHD_INFO(("%s: found 4329 Dongle\n", __FUNCTION__));
			break;
		case BCM4315_D11DUAL_ID:		/* 4315 802.11a/g id */
		case BCM4315_D11G_ID:			/* 4315 802.11g id */
		case BCM4315_D11A_ID:			/* 4315 802.11a id */
			DHD_INFO(("%s: found 4315 Dongle\n", __FUNCTION__));
			break;
		case BCM4319_D11N_ID:			/* 4319 802.11n id */
		case BCM4319_D11N2G_ID:			/* 4319 802.11n2g id */
		case BCM4319_D11N5G_ID:			/* 4319 802.11n5g id */
			DHD_INFO(("%s: found 4319 Dongle\n", __FUNCTION__));
			break;
		case 0:
			DHD_INFO(("%s: allow device id 0, will check chip internals\n",
			          __FUNCTION__));
			break;

		default:
			DHD_ERROR(("%s: skipping 0x%04x/0x%04x, not a dongle\n",
			           __FUNCTION__, venid, devid));
			goto forcereturn;
	}

	if (osh == NULL) {
		DHD_ERROR(("%s: osh is NULL!\n", __FUNCTION__));
		goto forcereturn;
	}

	/* Allocate private bus interface state */
	if (!(bus = MALLOC(osh, sizeof(dhd_bus_t)))) {
		DHD_ERROR(("%s: MALLOC of dhd_bus_t failed\n", __FUNCTION__));
		goto fail;
	}
	bzero(bus, sizeof(dhd_bus_t));
	bus->sdh = sdh;
	bus->cl_devid = (uint16)devid;
	bus->bus = DHD_BUS;
	bus->bus_num = bus_no;
	bus->slot_num = slot;
	bus->tx_seq = SDPCM_SEQUENCE_WRAP - 1;
	bus->usebufpool = FALSE; /* Use bufpool if allocated, else use locally malloced rxbuf */

#if defined(SUPPORT_P2P_GO_PS)
	init_waitqueue_head(&bus->bus_sleep);
#endif /* LINUX && SUPPORT_P2P_GO_PS */

	/* attempt to attach to the dongle */
	if (!(dhdsdio_probe_attach(bus, osh, sdh, regsva, devid))) {
		DHD_ERROR(("%s: dhdsdio_probe_attach failed\n", __FUNCTION__));
		goto fail;
	}

	/* Attach to the dhd/OS/network interface */
	if (!(bus->dhd = dhd_attach(osh, bus, SDPCM_RESERVE))) {
		DHD_ERROR(("%s: dhd_attach failed\n", __FUNCTION__));
		goto fail;
	}

	/* Allocate buffers */
	if (!(dhdsdio_probe_malloc(bus, osh, sdh))) {
		DHD_ERROR(("%s: dhdsdio_probe_malloc failed\n", __FUNCTION__));
		goto fail;
	}

	if (!(dhdsdio_probe_init(bus, osh, sdh))) {
		DHD_ERROR(("%s: dhdsdio_probe_init failed\n", __FUNCTION__));
		goto fail;
	}

	if (bus->intr) {
		/* Register interrupt callback, but mask it (not operational yet). */
		DHD_INTR(("%s: disable SDIO interrupts (not interested yet)\n", __FUNCTION__));
		bcmsdh_intr_disable(sdh);
		if ((ret = bcmsdh_intr_reg(sdh, dhdsdio_isr, bus)) != 0) {
			DHD_ERROR(("%s: FAILED: bcmsdh_intr_reg returned %d\n",
			           __FUNCTION__, ret));
			goto fail;
		}
		DHD_INTR(("%s: registered SDIO interrupt function ok\n", __FUNCTION__));
	} else {
		DHD_INFO(("%s: SDIO interrupt function is NOT registered due to polling mode\n",
		           __FUNCTION__));
	}

	DHD_INFO(("%s: completed!!\n", __FUNCTION__));

	/* if firmware path present try to download and bring up bus */
	bus->dhd->hang_report  = TRUE;
	if (dhd_download_fw_on_driverload) {
		if ((ret = dhd_bus_start(bus->dhd)) != 0) {
			DHD_ERROR(("%s: dhd_bus_start failed\n", __FUNCTION__));
				goto fail;
		}
	}
	/* Ok, have the per-port tell the stack we're open for business */
	if (dhd_register_if(bus->dhd, 0, TRUE) != 0) {
		DHD_ERROR(("%s: Net attach failed!!\n", __FUNCTION__));
		goto fail;
	}


	return bus;

fail:
	dhdsdio_release(bus, osh);

forcereturn:

	return NULL;
}

static bool
dhdsdio_probe_attach(struct dhd_bus *bus, osl_t *osh, void *sdh, void *regsva,
                     uint16 devid)
{
	int err = 0;
	uint8 clkctl = 0;

	bus->alp_only = TRUE;
	bus->sih = NULL;

	/* Return the window to backplane enumeration space for core access */
	if (dhdsdio_set_siaddr_window(bus, SI_ENUM_BASE)) {
		DHD_ERROR(("%s: FAILED to return to SI_ENUM_BASE\n", __FUNCTION__));
	}


	/* Force PLL off until si_attach() programs PLL control regs */



	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, DHD_INIT_CLKCTL1, &err);
	if (!err)
		clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, &err);

	if (err || ((clkctl & ~SBSDIO_AVBITS) != DHD_INIT_CLKCTL1)) {
		DHD_ERROR(("dhdsdio_probe: ChipClkCSR access: err %d wrote 0x%02x read 0x%02x\n",
		           err, DHD_INIT_CLKCTL1, clkctl));
		goto fail;
	}

#ifdef DHD_DEBUG
	if (DHD_INFO_ON()) {
		uint fn, numfn;
		uint8 *cis[SDIOD_MAX_IOFUNCS];
		int err = 0;

		numfn = bcmsdh_query_iofnum(sdh);
		ASSERT(numfn <= SDIOD_MAX_IOFUNCS);

		/* Make sure ALP is available before trying to read CIS */
		SPINWAIT(((clkctl = bcmsdh_cfg_read(sdh, SDIO_FUNC_1,
		                                    SBSDIO_FUNC1_CHIPCLKCSR, NULL)),
		          !SBSDIO_ALPAV(clkctl)), PMU_MAX_TRANSITION_DLY);

		/* Now request ALP be put on the bus */
		bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR,
		                 DHD_INIT_CLKCTL2, &err);
		OSL_DELAY(65);

		for (fn = 0; fn <= numfn; fn++) {
			if (!(cis[fn] = MALLOC(osh, SBSDIO_CIS_SIZE_LIMIT))) {
				DHD_INFO(("dhdsdio_probe: fn %d cis malloc failed\n", fn));
				break;
			}
			bzero(cis[fn], SBSDIO_CIS_SIZE_LIMIT);

			if ((err = bcmsdh_cis_read(sdh, fn, cis[fn], SBSDIO_CIS_SIZE_LIMIT))) {
				DHD_INFO(("dhdsdio_probe: fn %d cis read err %d\n", fn, err));
				MFREE(osh, cis[fn], SBSDIO_CIS_SIZE_LIMIT);
				break;
			}
			dhd_dump_cis(fn, cis[fn]);
		}

		while (fn-- > 0) {
			ASSERT(cis[fn]);
			MFREE(osh, cis[fn], SBSDIO_CIS_SIZE_LIMIT);
		}

		if (err) {
			DHD_ERROR(("dhdsdio_probe: failure reading or parsing CIS\n"));
			goto fail;
		}
	}
#endif /* DHD_DEBUG */

	/* si_attach() will provide an SI handle and scan the backplane */
	if (!(bus->sih = si_attach((uint)devid, osh, regsva, DHD_BUS, sdh,
	                           &bus->vars, &bus->varsz))) {
		DHD_ERROR(("%s: si_attach failed!\n", __FUNCTION__));
		goto fail;
	}

#ifdef DHD_DEBUG
	DHD_ERROR(("F1 signature OK, socitype:0x%x chip:0x%4x rev:0x%x pkg:0x%x\n",
		bus->sih->socitype, bus->sih->chip, bus->sih->chiprev, bus->sih->chippkg));
#endif /* DHD_DEBUG */


	bcmsdh_chipinfo(sdh, bus->sih->chip, bus->sih->chiprev);

	if (!dhdsdio_chipmatch((uint16)bus->sih->chip)) {
		DHD_ERROR(("%s: unsupported chip: 0x%04x\n",
		           __FUNCTION__, bus->sih->chip));
		goto fail;
	}

	if (bus->sih->buscorerev >= 12)
		dhdsdio_clk_kso_init(bus);
	else
		bus->kso = TRUE;

	if (CST4330_CHIPMODE_SDIOD(bus->sih->chipst)) {
	}

	si_sdiod_drive_strength_init(bus->sih, osh, dhd_sdiod_drive_strength);


	/* Get info on the ARM and SOCRAM cores... */
	if (!DHD_NOPMU(bus)) {
		if ((si_setcore(bus->sih, ARM7S_CORE_ID, 0)) ||
		    (si_setcore(bus->sih, ARMCM3_CORE_ID, 0)) ||
		    (si_setcore(bus->sih, ARMCR4_CORE_ID, 0))) {
			bus->armrev = si_corerev(bus->sih);
		} else {
			DHD_ERROR(("%s: failed to find ARM core!\n", __FUNCTION__));
			goto fail;
		}

		if (!si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
			if (!(bus->orig_ramsize = si_socram_size(bus->sih))) {
				DHD_ERROR(("%s: failed to find SOCRAM memory!\n", __FUNCTION__));
				goto fail;
			}
		} else {
			/* cr4 has a different way to find the RAM size from TCM's */
			if (!(bus->orig_ramsize = si_tcm_size(bus->sih))) {
				DHD_ERROR(("%s: failed to find CR4-TCM memory!\n", __FUNCTION__));
				goto fail;
			}
			/* also populate base address */
			switch ((uint16)bus->sih->chip) {
			case BCM4335_CHIP_ID:
			case BCM4339_CHIP_ID:
			case BCM43349_CHIP_ID:
				bus->dongle_ram_base = CR4_4335_RAM_BASE;
				break;
			case BCM4350_CHIP_ID:
			case BCM4354_CHIP_ID:
			case BCM4356_CHIP_ID:
				bus->dongle_ram_base = CR4_4350_RAM_BASE;
				break;
			case BCM4360_CHIP_ID:
				bus->dongle_ram_base = CR4_4360_RAM_BASE;
				break;
			case BCM4345_CHIP_ID:
				bus->dongle_ram_base = CR4_4345_RAM_BASE;
				break;
			default:
				bus->dongle_ram_base = 0;
				DHD_ERROR(("%s: WARNING: Using default ram base at 0x%x\n",
				           __FUNCTION__, bus->dongle_ram_base));
			}
		}
		bus->ramsize = bus->orig_ramsize;
		if (dhd_dongle_ramsize)
			dhd_dongle_setramsize(bus, dhd_dongle_ramsize);

		DHD_ERROR(("DHD: dongle ram size is set to %d(orig %d) at 0x%x\n",
		           bus->ramsize, bus->orig_ramsize, bus->dongle_ram_base));

		bus->srmemsize = si_socram_srmem_size(bus->sih);
	}

	/* ...but normally deal with the SDPCMDEV core */
	if (!(bus->regs = si_setcore(bus->sih, PCMCIA_CORE_ID, 0)) &&
	    !(bus->regs = si_setcore(bus->sih, SDIOD_CORE_ID, 0))) {
		DHD_ERROR(("%s: failed to find SDIODEV core!\n", __FUNCTION__));
		goto fail;
	}
	bus->sdpcmrev = si_corerev(bus->sih);

	/* Set core control so an SDIO reset does a backplane reset */
	OR_REG(osh, &bus->regs->corecontrol, CC_BPRESEN);
	bus->rxint_mode = SDIO_DEVICE_HMB_RXINT;

	if ((bus->sih->buscoretype == SDIOD_CORE_ID) && (bus->sdpcmrev >= 4) &&
		(bus->rxint_mode  == SDIO_DEVICE_RXDATAINT_MODE_1))
	{
		uint32 val;

		val = R_REG(osh, &bus->regs->corecontrol);
		val &= ~CC_XMTDATAAVAIL_MODE;
		val |= CC_XMTDATAAVAIL_CTRL;
		W_REG(osh, &bus->regs->corecontrol, val);
	}


	pktq_init(&bus->txq, (PRIOMASK + 1), QLEN);

	/* Locate an appropriately-aligned portion of hdrbuf */
	bus->rxhdr = (uint8 *)ROUNDUP((uintptr)&bus->hdrbuf[0], DHD_SDALIGN);

	/* Set the poll and/or interrupt flags */
	bus->intr = (bool)dhd_intr;
	if ((bus->poll = (bool)dhd_poll))
		bus->pollrate = 1;

	/* Setting default Glom size */
	bus->txglomsize = SDPCM_DEFGLOM_SIZE;

	return TRUE;

fail:
	if (bus->sih != NULL) {
		si_detach(bus->sih);
		bus->sih = NULL;
	}
	return FALSE;
}

static bool
dhdsdio_probe_malloc(dhd_bus_t *bus, osl_t *osh, void *sdh)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd->maxctl) {
		bus->rxblen = ROUNDUP((bus->dhd->maxctl+SDPCM_HDRLEN), ALIGNMENT) + DHD_SDALIGN;
		if (!(bus->rxbuf = DHD_OS_PREALLOC(bus->dhd, DHD_PREALLOC_RXBUF, bus->rxblen))) {
			DHD_ERROR(("%s: MALLOC of %d-byte rxbuf failed\n",
			           __FUNCTION__, bus->rxblen));
			goto fail;
		}
	}
	/* Allocate buffer to receive glomed packet */
	if (!(bus->databuf = DHD_OS_PREALLOC(bus->dhd, DHD_PREALLOC_DATABUF, MAX_DATA_BUF))) {
		DHD_ERROR(("%s: MALLOC of %d-byte databuf failed\n",
			__FUNCTION__, MAX_DATA_BUF));
		/* release rxbuf which was already located as above */
		if (bus->rxbuf) {
			DHD_OS_PREFREE(bus->dhd, DHD_PREALLOC_RXBUF, bus->rxbuf, bus->rxblen);
		}
		goto fail;
	}

	/* Align the buffer */
	if ((uintptr)bus->databuf % DHD_SDALIGN)
		bus->dataptr = bus->databuf + (DHD_SDALIGN - ((uintptr)bus->databuf % DHD_SDALIGN));
	else
		bus->dataptr = bus->databuf;

	return TRUE;

fail:
	return FALSE;
}

static bool
dhdsdio_probe_init(dhd_bus_t *bus, osl_t *osh, void *sdh)
{
	int32 fnum;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	bus->_srenab = FALSE;

#ifdef SDTEST
	dhdsdio_pktgen_init(bus);
#endif /* SDTEST */

	/* Disable F2 to clear any intermediate frame state on the dongle */
	bcmsdh_cfg_write(sdh, SDIO_FUNC_0, SDIOD_CCCR_IOEN, SDIO_FUNC_ENABLE_1, NULL);

	bus->dhd->busstate = DHD_BUS_DOWN;
	bus->sleeping = FALSE;
	bus->rxflow = FALSE;
	bus->prev_rxlim_hit = 0;

	/* Done with backplane-dependent accesses, can drop clock... */
	bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);

	/* ...and initialize clock/power states */
	bus->clkstate = CLK_SDONLY;
	bus->idletime = (int32)dhd_idletime;
	bus->idleclock = DHD_IDLE_ACTIVE;

	/* Query the SD clock speed */
	if (bcmsdh_iovar_op(sdh, "sd_divisor", NULL, 0,
	                    &bus->sd_divisor, sizeof(int32), FALSE) != BCME_OK) {
		DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, "sd_divisor"));
		bus->sd_divisor = -1;
	} else {
		DHD_INFO(("%s: Initial value for %s is %d\n",
		          __FUNCTION__, "sd_divisor", bus->sd_divisor));
	}

	/* Query the SD bus mode */
	if (bcmsdh_iovar_op(sdh, "sd_mode", NULL, 0,
	                    &bus->sd_mode, sizeof(int32), FALSE) != BCME_OK) {
		DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, "sd_mode"));
		bus->sd_mode = -1;
	} else {
		DHD_INFO(("%s: Initial value for %s is %d\n",
		          __FUNCTION__, "sd_mode", bus->sd_mode));
	}

	/* Query the F2 block size, set roundup accordingly */
	fnum = 2;
	if (bcmsdh_iovar_op(sdh, "sd_blocksize", &fnum, sizeof(int32),
	                    &bus->blocksize, sizeof(int32), FALSE) != BCME_OK) {
		bus->blocksize = 0;
		DHD_ERROR(("%s: fail on %s get\n", __FUNCTION__, "sd_blocksize"));
	} else {
		DHD_INFO(("%s: Initial value for %s is %d\n",
		          __FUNCTION__, "sd_blocksize", bus->blocksize));

		dhdsdio_tune_fifoparam(bus);
	}
	bus->roundup = MIN(max_roundup, bus->blocksize);

	if (bus->pad_pkt)
		PKTFREE(osh, bus->pad_pkt, FALSE);
	bus->pad_pkt = PKTGET(osh, SDIO_MAX_BLOCK_SIZE, FALSE);
	if (bus->pad_pkt == NULL)
		DHD_ERROR(("failed to allocate padding packet\n"));
	else {
		int alignment_offset = 0;
		uintptr pktprt = (uintptr)PKTDATA(osh, bus->pad_pkt);
		if (!(pktprt&1) && (pktprt = (pktprt % DHD_SDALIGN)))
			PKTPUSH(osh, bus->pad_pkt, alignment_offset);
		PKTSETNEXT(osh, bus->pad_pkt, NULL);
	}


	/* Query if bus module supports packet chaining, default to use if supported */
	if (bcmsdh_iovar_op(sdh, "sd_rxchain", NULL, 0,
	                    &bus->sd_rxchain, sizeof(int32), FALSE) != BCME_OK) {
		bus->sd_rxchain = FALSE;
	} else {
		DHD_INFO(("%s: bus module (through bcmsdh API) %s chaining\n",
		          __FUNCTION__, (bus->sd_rxchain ? "supports" : "does not support")));
	}
	bus->use_rxchain = (bool)bus->sd_rxchain;
	bus->txinrx_thres = CUSTOM_TXINRX_THRES;
	/* TX first in dhdsdio_readframes() */
	bus->dotxinrx = TRUE;

	return TRUE;
}

int
dhd_bus_download_firmware(struct dhd_bus *bus, osl_t *osh,
                          char *pfw_path, char *pnv_path)
{
	int ret;

	bus->fw_path = pfw_path;
	bus->nv_path = pnv_path;

	ret = dhdsdio_download_firmware(bus, osh, bus->sdh);


	return ret;
}

static int
dhdsdio_download_firmware(struct dhd_bus *bus, osl_t *osh, void *sdh)
{
	int ret;


	DHD_TRACE_HW4(("%s: firmware path=%s, nvram path=%s\n",
		__FUNCTION__, bus->fw_path, bus->nv_path));
	DHD_OS_WAKE_LOCK(bus->dhd);

	/* Download the firmware */
	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

	ret = _dhdsdio_download_firmware(bus);

	dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);

	DHD_OS_WAKE_UNLOCK(bus->dhd);
	return ret;
}

/* Detach and free everything */
static void
dhdsdio_release(dhd_bus_t *bus, osl_t *osh)
{
	bool dongle_isolation = FALSE;
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus) {
		ASSERT(osh);

		if (bus->dhd) {
			dongle_isolation = bus->dhd->dongle_isolation;
			dhd_detach(bus->dhd);
		}

		/* De-register interrupt handler */
		bcmsdh_intr_disable(bus->sdh);
		bcmsdh_intr_dereg(bus->sdh);

		if (bus->dhd) {
			dhdsdio_release_dongle(bus, osh, dongle_isolation, TRUE);
			dhd_free(bus->dhd);
			bus->dhd = NULL;
		}

		dhdsdio_release_malloc(bus, osh);

#ifdef DHD_DEBUG
		if (bus->console.buf != NULL)
			MFREE(osh, bus->console.buf, bus->console.bufsize);
#endif

		if (bus->pad_pkt)
			PKTFREE(osh, bus->pad_pkt, FALSE);


		MFREE(osh, bus, sizeof(dhd_bus_t));
	}

	DHD_TRACE(("%s: Disconnected\n", __FUNCTION__));
}

static void
dhdsdio_release_malloc(dhd_bus_t *bus, osl_t *osh)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	if (bus->dhd && bus->dhd->dongle_reset)
		return;

	if (bus->rxbuf) {
#ifndef CONFIG_DHD_USE_STATIC_BUF
		MFREE(osh, bus->rxbuf, bus->rxblen);
#endif
		bus->rxctl = bus->rxbuf = NULL;
		bus->rxlen = 0;
	}

	if (bus->databuf) {
#ifndef CONFIG_DHD_USE_STATIC_BUF
		MFREE(osh, bus->databuf, MAX_DATA_BUF);
#endif
		bus->databuf = NULL;
	}

	if (bus->vars && bus->varsz) {
		MFREE(osh, bus->vars, bus->varsz);
		bus->vars = NULL;
	}

}


static void
dhdsdio_release_dongle(dhd_bus_t *bus, osl_t *osh, bool dongle_isolation, bool reset_flag)
{
	DHD_TRACE(("%s: Enter bus->dhd %p bus->dhd->dongle_reset %d \n", __FUNCTION__,
		bus->dhd, bus->dhd->dongle_reset));

	if ((bus->dhd && bus->dhd->dongle_reset) && reset_flag)
		return;

	if (bus->sih) {
#if !defined(BCMLXSDMMC)
		if (bus->dhd) {
			dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
		}
		if (KSO_ENAB(bus) && (dongle_isolation == FALSE))
			si_watchdog(bus->sih, 4);
#endif /* !defined(BCMLXSDMMC) */
		if (bus->dhd) {
			dhdsdio_clkctl(bus, CLK_NONE, FALSE);
		}
		si_detach(bus->sih);
		bus->sih = NULL;
		if (bus->vars && bus->varsz)
			MFREE(osh, bus->vars, bus->varsz);
		bus->vars = NULL;
	}

	DHD_TRACE(("%s: Disconnected\n", __FUNCTION__));
}

static void
dhdsdio_disconnect(void *ptr)
{
	dhd_bus_t *bus = (dhd_bus_t *)ptr;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));



	if (bus) {
		ASSERT(bus->dhd);
		dhdsdio_release(bus, bus->dhd->osh);
	}



	DHD_TRACE(("%s: Disconnected\n", __FUNCTION__));
}

static int
dhdsdio_suspend(void *context)
{
	int ret = 0;

	dhd_bus_t *bus = (dhd_bus_t*)context;
#ifdef SUPPORT_P2P_GO_PS
	int wait_time = 0;

	if (bus->idletime > 0) {
		wait_time = msecs_to_jiffies(bus->idletime * dhd_watchdog_ms);
	}
#endif /* SUPPORT_P2P_GO_PS */
	ret = dhd_os_check_wakelock(bus->dhd);
#ifdef SUPPORT_P2P_GO_PS
	if ((!ret) && (bus->dhd->up) && (bus->dhd->op_mode != DHD_FLAG_HOSTAP_MODE)) {
		if (wait_event_timeout(bus->bus_sleep, bus->sleeping, wait_time) == 0) {
			if (!bus->sleeping) {
				return 1;
			}
		}
	}
#endif /* SUPPORT_P2P_GO_PS */
	return ret;
}

static int
dhdsdio_resume(void *context)
{
#if defined(OOB_INTR_ONLY)
	dhd_bus_t *bus = (dhd_bus_t*)context;

	OOB_PARAM_IF(!dhd_oob_disable) {
		if (dhd_os_check_if_up(bus->dhd))
			bcmsdh_oob_intr_set(bus->sdh, TRUE);
	}
#endif 
	return 0;
}


/* Register/Unregister functions are called by the main DHD entry
 * point (e.g. module insertion) to link with the bus driver, in
 * order to look for or await the device.
 */

static bcmsdh_driver_t dhd_sdio = {
	dhdsdio_probe,
	dhdsdio_disconnect,
	dhdsdio_suspend,
	dhdsdio_resume
};

int
dhd_bus_register(void)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	return bcmsdh_register(&dhd_sdio);
}

void
dhd_bus_unregister(void)
{
	DHD_TRACE(("%s: Enter\n", __FUNCTION__));

	bcmsdh_unregister();
}

#if defined(BCMLXSDMMC)
/* Register a dummy SDIO client driver in order to be notified of new SDIO device */
int dhd_bus_reg_sdio_notify(void* semaphore)
{
	return bcmsdh_reg_sdio_notify(semaphore);
}

void dhd_bus_unreg_sdio_notify(void)
{
	bcmsdh_unreg_sdio_notify();
}
#endif /* defined(BCMLXSDMMC) */

#ifdef BCMEMBEDIMAGE
static int
dhdsdio_download_code_array(struct dhd_bus *bus)
{
	int bcmerror = -1;
	int offset = 0;
	unsigned char *ularray = NULL;

	DHD_INFO(("%s: download embedded firmware...\n", __FUNCTION__));

	/* Download image */
	while ((offset + MEMBLOCK) < sizeof(dlarray)) {
		/* check if CR4 */
		if (si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
			/* if address is 0, store the reset instruction to be written in 0 */

			if (offset == 0) {
				bus->resetinstr = *(((uint32*)dlarray));
				/* Add start of RAM address to the address given by user */
				offset += bus->dongle_ram_base;
			}
		}

		bcmerror = dhdsdio_membytes(bus, TRUE, offset,
			(uint8 *) (dlarray + offset), MEMBLOCK);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at 0x%08x\n",
			        __FUNCTION__, bcmerror, MEMBLOCK, offset));
			goto err;
		}

		offset += MEMBLOCK;
	}

	if (offset < sizeof(dlarray)) {
		bcmerror = dhdsdio_membytes(bus, TRUE, offset,
			(uint8 *) (dlarray + offset), sizeof(dlarray) - offset);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at 0x%08x\n",
			        __FUNCTION__, bcmerror, sizeof(dlarray) - offset, offset));
			goto err;
		}
	}

#ifdef DHD_DEBUG
	/* Upload and compare the downloaded code */
	{
		ularray = MALLOC(bus->dhd->osh, bus->ramsize);
		/* Upload image to verify downloaded contents. */
		offset = 0;
		memset(ularray, 0xaa, bus->ramsize);
		while ((offset + MEMBLOCK) < sizeof(dlarray)) {
			bcmerror = dhdsdio_membytes(bus, FALSE, offset, ularray + offset, MEMBLOCK);
			if (bcmerror) {
				DHD_ERROR(("%s: error %d on reading %d membytes at 0x%08x\n",
					__FUNCTION__, bcmerror, MEMBLOCK, offset));
				goto err;
			}

			offset += MEMBLOCK;
		}

		if (offset < sizeof(dlarray)) {
			bcmerror = dhdsdio_membytes(bus, FALSE, offset,
				ularray + offset, sizeof(dlarray) - offset);
			if (bcmerror) {
				DHD_ERROR(("%s: error %d on reading %d membytes at 0x%08x\n",
					__FUNCTION__, bcmerror, sizeof(dlarray) - offset, offset));
				goto err;
			}
		}

		if (memcmp(dlarray, ularray, sizeof(dlarray))) {
			DHD_ERROR(("%s: Downloaded image is corrupted (%s, %s, %s).\n",
			           __FUNCTION__, dlimagename, dlimagever, dlimagedate));
			goto err;
		} else
			DHD_ERROR(("%s: Download, Upload and compare succeeded (%s, %s, %s).\n",
			           __FUNCTION__, dlimagename, dlimagever, dlimagedate));

	}
#endif /* DHD_DEBUG */

err:
	if (ularray)
		MFREE(bus->dhd->osh, ularray, bus->ramsize);
	return bcmerror;
}
#endif /* BCMEMBEDIMAGE */

static int
dhdsdio_download_code_file(struct dhd_bus *bus, char *pfw_path)
{
	int bcmerror = -1;
	int offset = 0;
	int len;
	void *image = NULL;
	uint8 *memblock = NULL, *memptr;

	DHD_INFO(("%s: download firmware %s\n", __FUNCTION__, pfw_path));

	image = dhd_os_open_image(pfw_path);
	if (image == NULL)
		goto err;

	memptr = memblock = MALLOC(bus->dhd->osh, MEMBLOCK + DHD_SDALIGN);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n", __FUNCTION__, MEMBLOCK));
		goto err;
	}
	if ((uint32)(uintptr)memblock % DHD_SDALIGN)
		memptr += (DHD_SDALIGN - ((uint32)(uintptr)memblock % DHD_SDALIGN));

	/* Download image */
	while ((len = dhd_os_get_image_block((char*)memptr, MEMBLOCK, image))) {
		if (len < 0) {
			DHD_ERROR(("%s: dhd_os_get_image_block failed (%d)\n", __FUNCTION__, len));
			bcmerror = BCME_ERROR;
			goto err;
		}
		/* check if CR4 */
		if (si_setcore(bus->sih, ARMCR4_CORE_ID, 0)) {
			/* if address is 0, store the reset instruction to be written in 0 */

			if (offset == 0) {
				bus->resetinstr = *(((uint32*)memptr));
				/* Add start of RAM address to the address given by user */
				offset += bus->dongle_ram_base;
			}
		}

		bcmerror = dhdsdio_membytes(bus, TRUE, offset, memptr, len);
		if (bcmerror) {
			DHD_ERROR(("%s: error %d on writing %d membytes at 0x%08x\n",
			        __FUNCTION__, bcmerror, MEMBLOCK, offset));
			goto err;
		}

		offset += MEMBLOCK;
	}

err:
	if (memblock)
		MFREE(bus->dhd->osh, memblock, MEMBLOCK + DHD_SDALIGN);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
}

/*
	EXAMPLE: nvram_array
	nvram_arry format:
	name=value
	Use carriage return at the end of each assignment, and an empty string with
	carriage return at the end of array.

	For example:
	unsigned char  nvram_array[] = {"name1=value1\n", "name2=value2\n", "\n"};
	Hex values start with 0x, and mac addr format: xx:xx:xx:xx:xx:xx.

	Search "EXAMPLE: nvram_array" to see how the array is activated.
*/

void
dhd_bus_set_nvram_params(struct dhd_bus * bus, const char *nvram_params)
{
	bus->nvram_params = nvram_params;
}

static int
dhdsdio_download_nvram(struct dhd_bus *bus)
{
	int bcmerror = -1;
	uint len;
	void * image = NULL;
	char * memblock = NULL;
	char *bufp;
	char *pnv_path;
	bool nvram_file_exists;

	pnv_path = bus->nv_path;

	nvram_file_exists = ((pnv_path != NULL) && (pnv_path[0] != '\0'));
	if (!nvram_file_exists && (bus->nvram_params == NULL))
		return (0);

	if (nvram_file_exists) {
		image = dhd_os_open_image(pnv_path);
		if (image == NULL)
			goto err;
	}

	memblock = MALLOC(bus->dhd->osh, MAX_NVRAMBUF_SIZE);
	if (memblock == NULL) {
		DHD_ERROR(("%s: Failed to allocate memory %d bytes\n",
		           __FUNCTION__, MAX_NVRAMBUF_SIZE));
		goto err;
	}

	/* Download variables */
	if (nvram_file_exists) {
		len = dhd_os_get_image_block(memblock, MAX_NVRAMBUF_SIZE, image);
	}
	else {
		len = strlen(bus->nvram_params);
		ASSERT(len <= MAX_NVRAMBUF_SIZE);
		memcpy(memblock, bus->nvram_params, len);
	}
	if (len > 0 && len < MAX_NVRAMBUF_SIZE) {
		bufp = (char *)memblock;
		bufp[len] = 0;
		len = process_nvram_vars(bufp, len);
		if (len % 4) {
			len += 4 - (len % 4);
		}
		bufp += len;
		*bufp++ = 0;
		if (len)
			bcmerror = dhdsdio_downloadvars(bus, memblock, len + 1);
		if (bcmerror) {
			DHD_ERROR(("%s: error downloading vars: %d\n",
			           __FUNCTION__, bcmerror));
		}
	}
	else {
		DHD_ERROR(("%s: error reading nvram file: %d\n",
		           __FUNCTION__, len));
		bcmerror = BCME_SDIO_ERROR;
	}

err:
	if (memblock)
		MFREE(bus->dhd->osh, memblock, MAX_NVRAMBUF_SIZE);

	if (image)
		dhd_os_close_image(image);

	return bcmerror;
}

static int
_dhdsdio_download_firmware(struct dhd_bus *bus)
{
	int bcmerror = -1;

	bool embed = FALSE;	/* download embedded firmware */
	bool dlok = FALSE;	/* download firmware succeeded */

	/* Out immediately if no image to download */
	if ((bus->fw_path == NULL) || (bus->fw_path[0] == '\0')) {
#ifdef BCMEMBEDIMAGE
		embed = TRUE;
#else
		return 0;
#endif
	}

	/* Keep arm in reset */
	if (dhdsdio_download_state(bus, TRUE)) {
		DHD_ERROR(("%s: error placing ARM core in reset\n", __FUNCTION__));
		goto err;
	}

	/* External image takes precedence if specified */
	if ((bus->fw_path != NULL) && (bus->fw_path[0] != '\0')) {
		if (dhdsdio_download_code_file(bus, bus->fw_path)) {
			DHD_ERROR(("%s: dongle image file download failed\n", __FUNCTION__));
#ifdef BCMEMBEDIMAGE
			embed = TRUE;
#else
			goto err;
#endif
		}
		else {
			embed = FALSE;
			dlok = TRUE;
		}
	}

#ifdef BCMEMBEDIMAGE
	if (embed) {
		if (dhdsdio_download_code_array(bus)) {
			DHD_ERROR(("%s: dongle image array download failed\n", __FUNCTION__));
			goto err;
		}
		else {
			dlok = TRUE;
		}
	}
#else
	BCM_REFERENCE(embed);
#endif
	if (!dlok) {
		DHD_ERROR(("%s: dongle image download failed\n", __FUNCTION__));
		goto err;
	}

	/* EXAMPLE: nvram_array */
	/* If a valid nvram_arry is specified as above, it can be passed down to dongle */
	/* dhd_bus_set_nvram_params(bus, (char *)&nvram_array); */

	/* External nvram takes precedence if specified */
	if (dhdsdio_download_nvram(bus)) {
		DHD_ERROR(("%s: dongle nvram file download failed\n", __FUNCTION__));
		goto err;
	}

	/* Take arm out of reset */
	if (dhdsdio_download_state(bus, FALSE)) {
		DHD_ERROR(("%s: error getting out of ARM core reset\n", __FUNCTION__));
		goto err;
	}

	bcmerror = 0;

err:
	return bcmerror;
}

static int
dhd_bcmsdh_recv_buf(dhd_bus_t *bus, uint32 addr, uint fn, uint flags, uint8 *buf, uint nbytes,
	void *pkt, bcmsdh_cmplt_fn_t complete, void *handle)
{
	int status;

	if (!KSO_ENAB(bus)) {
		DHD_ERROR(("%s: Device asleep\n", __FUNCTION__));
		return BCME_NODEVICE;
	}

	status = bcmsdh_recv_buf(bus->sdh, addr, fn, flags, buf, nbytes, pkt, complete, handle);

	return status;
}

static int
dhd_bcmsdh_send_buf(dhd_bus_t *bus, uint32 addr, uint fn, uint flags, uint8 *buf, uint nbytes,
	void *pkt, bcmsdh_cmplt_fn_t complete, void *handle, int max_retry)
{
	int ret;
	int i = 0;
	int retries = 0;
	bcmsdh_info_t *sdh;

	if (!KSO_ENAB(bus)) {
		DHD_ERROR(("%s: Device asleep\n", __FUNCTION__));
		return BCME_NODEVICE;
	}

	sdh = bus->sdh;
	do {
		ret = bcmsdh_send_buf(bus->sdh, addr, fn, flags, buf, nbytes,
			pkt, complete, handle);

		bus->f2txdata++;
		ASSERT(ret != BCME_PENDING);

		if (ret == BCME_NODEVICE) {
			DHD_ERROR(("%s: Device asleep already\n", __FUNCTION__));
		} else if (ret < 0) {
			/* On failure, abort the command and terminate the frame */
			DHD_ERROR(("%s: sdio error %d, abort command and terminate frame.\n",
				__FUNCTION__, ret));
			bus->tx_sderrs++;
			bus->f1regdata++;
			bus->dhd->tx_errors++;
			bcmsdh_abort(sdh, SDIO_FUNC_2);
			bcmsdh_cfg_write(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_FRAMECTRL,
				SFC_WF_TERM, NULL);
			for (i = 0; i < READ_FRM_CNT_RETRIES; i++) {
				uint8 hi, lo;
				hi = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_WFRAMEBCHI,
					NULL);
				lo = bcmsdh_cfg_read(sdh, SDIO_FUNC_1, SBSDIO_FUNC1_WFRAMEBCLO,
					NULL);
				bus->f1regdata += 2;
				if ((hi == 0) && (lo == 0))
					break;
			}
		}
	} while ((ret < 0) && retrydata && ++retries < max_retry);

	return ret;
}

uint
dhd_bus_chip(struct dhd_bus *bus)
{
	ASSERT(bus->sih != NULL);
	return bus->sih->chip;
}

uint
dhd_bus_chiprev(struct dhd_bus *bus)
{
	ASSERT(bus);
	ASSERT(bus->sih != NULL);
	return bus->sih->chiprev;
}

void *
dhd_bus_pub(struct dhd_bus *bus)
{
	return bus->dhd;
}

void *
dhd_bus_sih(struct dhd_bus *bus)
{
	return (void *)bus->sih;
}

void *
dhd_bus_txq(struct dhd_bus *bus)
{
	return &bus->txq;
}

void
dhd_bus_set_dotxinrx(struct dhd_bus *bus, bool val)
{
	bus->dotxinrx = val;
}

int
dhd_bus_devreset(dhd_pub_t *dhdp, uint8 flag)
{
	int bcmerror = 0;
	dhd_bus_t *bus;

	bus = dhdp->bus;

	if (flag == TRUE) {
		if (!bus->dhd->dongle_reset) {
			dhd_os_sdlock(dhdp);
			dhd_os_wd_timer(dhdp, 0);
#if !defined(IGNORE_ETH0_DOWN)
			/* Force flow control as protection when stop come before ifconfig_down */
			dhd_txflowcontrol(bus->dhd, ALL_INTERFACES, ON);
#endif /* !defined(IGNORE_ETH0_DOWN) */
			/* Expect app to have torn down any connection before calling */
			/* Stop the bus, disable F2 */
			dhd_bus_stop(bus, FALSE);

#if defined(OOB_INTR_ONLY)
			OOB_PARAM_IF(!dhd_oob_disable) {
				/* Clean up any pending IRQ */
				dhd_enable_oob_intr(bus, FALSE);
				bcmsdh_oob_intr_set(bus->sdh, FALSE);
				bcmsdh_oob_intr_unregister(bus->sdh);
			}
#endif 

			/* Clean tx/rx buffer pointers, detach from the dongle */
			dhdsdio_release_dongle(bus, bus->dhd->osh, TRUE, TRUE);

			bus->dhd->dongle_reset = TRUE;
			bus->dhd->up = FALSE;
			dhd_txglom_enable(dhdp, FALSE);
			dhd_os_sdunlock(dhdp);

			DHD_TRACE(("%s:  WLAN OFF DONE\n", __FUNCTION__));
			/* App can now remove power from device */
		} else
			bcmerror = BCME_SDIO_ERROR;
	} else {
		/* App must have restored power to device before calling */

		DHD_TRACE(("\n\n%s: == WLAN ON ==\n", __FUNCTION__));

		if (bus->dhd->dongle_reset) {
			/* Turn on WLAN */
			dhd_os_sdlock(dhdp);
			/* Reset SD client */
			bcmsdh_reset(bus->sdh);

			/* Attempt to re-attach & download */
			if (dhdsdio_probe_attach(bus, bus->dhd->osh, bus->sdh,
				(uint32 *)SI_ENUM_BASE,
				bus->cl_devid)) {
				/* Attempt to download binary to the dongle */
				if (dhdsdio_probe_init(bus, bus->dhd->osh, bus->sdh) &&
				    dhdsdio_download_firmware(bus, bus->dhd->osh, bus->sdh) >= 0) {

					/* Re-init bus, enable F2 transfer */
					bcmerror = dhd_bus_init((dhd_pub_t *) bus->dhd, FALSE);
					if (bcmerror == BCME_OK) {
#if defined(OOB_INTR_ONLY)
						OOB_PARAM_IF(!dhd_oob_disable) {
							dhd_enable_oob_intr(bus, TRUE);
							bcmsdh_oob_intr_register(bus->sdh,
								dhdsdio_isr, bus);
							bcmsdh_oob_intr_set(bus->sdh, TRUE);
						}
#endif 

						bus->dhd->dongle_reset = FALSE;
						bus->dhd->up = TRUE;

#if !defined(IGNORE_ETH0_DOWN)
						/* Restore flow control  */
						dhd_txflowcontrol(bus->dhd, ALL_INTERFACES, OFF);
#endif 
						dhd_os_wd_timer(dhdp, dhd_watchdog_ms);

						DHD_TRACE(("%s: WLAN ON DONE\n", __FUNCTION__));
					} else {
						dhd_bus_stop(bus, FALSE);
						dhdsdio_release_dongle(bus, bus->dhd->osh,
							TRUE, FALSE);
					}
				} else
					bcmerror = BCME_SDIO_ERROR;
			} else
				bcmerror = BCME_SDIO_ERROR;

				dhd_os_sdunlock(dhdp);
		} else {
			bcmerror = BCME_SDIO_ERROR;
			DHD_INFO(("%s called when dongle is not in reset\n",
				__FUNCTION__));
			DHD_INFO(("Will call dhd_bus_start instead\n"));
			dhd_bus_resume(dhdp, 1);
			if ((bcmerror = dhd_bus_start(dhdp)) != 0)
				DHD_ERROR(("%s: dhd_bus_start fail with %d\n",
					__FUNCTION__, bcmerror));
		}
	}
	return bcmerror;
}

int dhd_bus_suspend(dhd_pub_t *dhdpub)
{
	return bcmsdh_stop(dhdpub->bus->sdh);
}

int dhd_bus_resume(dhd_pub_t *dhdpub, int stage)
{
	return bcmsdh_start(dhdpub->bus->sdh, stage);
}

/* Get Chip ID version */
uint dhd_bus_chip_id(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;

	return  bus->sih->chip;
}

/* Get Chip Rev ID version */
uint dhd_bus_chiprev_id(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;

	return bus->sih->chiprev;
}

/* Get Chip Pkg ID version */
uint dhd_bus_chippkg_id(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;

	return bus->sih->chippkg;
}

int dhd_bus_get_ids(struct dhd_bus *bus, uint32 *bus_type, uint32 *bus_num, uint32 *slot_num)
{
	*bus_type = bus->bus;
	*bus_num = bus->bus_num;
	*slot_num = bus->slot_num;
	return 0;
}

int
dhd_bus_membytes(dhd_pub_t *dhdp, bool set, uint32 address, uint8 *data, uint size)
{
	dhd_bus_t *bus;

	bus = dhdp->bus;
	return dhdsdio_membytes(bus, set, address, data, size);
}


void
dhd_bus_update_fw_nv_path(struct dhd_bus *bus, char *pfw_path, char *pnv_path)
{
	bus->fw_path = pfw_path;
	bus->nv_path = pnv_path;
}

int
dhd_enableOOB(dhd_pub_t *dhd, bool sleep)
{
	dhd_bus_t *bus = dhd->bus;
	sdpcmd_regs_t *regs = bus->regs;
	uint retries = 0;

	if (sleep) {
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);
		/* Tell device to start using OOB wakeup */
		W_SDREG(SMB_USE_OOB, &regs->tosbmailbox, retries);
		if (retries > retry_limit) {
			DHD_ERROR(("CANNOT SIGNAL CHIP, WILL NOT WAKE UP!!\n"));
			return BCME_BUSY;
		}
		/* Turn off our contribution to the HT clock request */
		dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);
	} else {
		/* Make sure the controller has the bus up */
		dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

		/* Send misc interrupt to indicate OOB not needed */
		W_SDREG(0, &regs->tosbmailboxdata, retries);
		if (retries <= retry_limit)
			W_SDREG(SMB_DEV_INT, &regs->tosbmailbox, retries);

		if (retries > retry_limit)
			DHD_ERROR(("CANNOT SIGNAL CHIP TO CLEAR OOB!!\n"));

		/* Make sure we have SD bus access */
		dhdsdio_clkctl(bus, CLK_SDONLY, FALSE);
	}
	return BCME_OK;
}

void
dhd_bus_pktq_flush(dhd_pub_t *dhdp)
{
	dhd_bus_t *bus = dhdp->bus;
	bool wlfc_enabled = FALSE;

#ifdef PROP_TXSTATUS
	wlfc_enabled = (dhd_wlfc_cleanup_txq(dhdp, NULL, 0) != WLFC_UNSUPPORTED);
#endif
	if (!wlfc_enabled) {
#ifdef DHDTCPACK_SUPPRESS
		/* Clean tcp_ack_info_tbl in order to prevent access to flushed pkt,
		 * when there is a newly coming packet from network stack.
		 */
		dhd_tcpack_info_tbl_clean(bus->dhd);
#endif /* DHDTCPACK_SUPPRESS */
		/* Clear the data packet queues */
		pktq_flush(dhdp->osh, &bus->txq, TRUE, NULL, 0);
	}
}

#ifdef BCMSDIO
int
dhd_sr_config(dhd_pub_t *dhd, bool on)
{
	dhd_bus_t *bus = dhd->bus;

	if (!bus->_srenab)
		return -1;

	return dhdsdio_clk_devsleep_iovar(bus, on);
}

uint16
dhd_get_chipid(dhd_pub_t *dhd)
{
	dhd_bus_t *bus = dhd->bus;

	if (bus && bus->sih)
		return (uint16)bus->sih->chip;
	else
		return 0;
}
#endif /* BCMSDIO */

#ifdef DEBUGGER
uint32 dhd_sdio_reg_read(void *h, uint32 addr)
{
	uint32 rval;
	struct dhd_bus *bus = (struct dhd_bus *) h;

	dhd_os_sdlock(bus->dhd);

	BUS_WAKE(bus);

	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

	rval = bcmsdh_reg_read(bus->sdh, addr, 4);

	dhd_os_sdunlock(bus->dhd);

	return rval;
}

void dhd_sdio_reg_write(void *h, uint32 addr, uint32 val)
{
	struct dhd_bus *bus = (struct dhd_bus *) h;

	dhd_os_sdlock(bus->dhd);

	BUS_WAKE(bus);

	dhdsdio_clkctl(bus, CLK_AVAIL, FALSE);

	bcmsdh_reg_write(bus->sdh, addr, 4, val);

	dhd_os_sdunlock(bus->dhd);
}
#endif /* DEBUGGER */
