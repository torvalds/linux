/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright (C) 2010 Samsung Electronics Co. Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl330.h>
#include <linux/scatterlist.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/err.h>

#include "dmaengine.h"
#define PL330_MAX_CHAN		8
#define PL330_MAX_IRQS		32
#define PL330_MAX_PERI		32

enum pl330_cachectrl {
	CCTRL0,		/* Noncacheable and nonbufferable */
	CCTRL1,		/* Bufferable only */
	CCTRL2,		/* Cacheable, but do not allocate */
	CCTRL3,		/* Cacheable and bufferable, but do not allocate */
	INVALID1,	/* AWCACHE = 0x1000 */
	INVALID2,
	CCTRL6,		/* Cacheable write-through, allocate on writes only */
	CCTRL7,		/* Cacheable write-back, allocate on writes only */
};

enum pl330_byteswap {
	SWAP_NO,
	SWAP_2,
	SWAP_4,
	SWAP_8,
	SWAP_16,
};

/* Register and Bit field Definitions */
#define DS			0x0
#define DS_ST_STOP		0x0
#define DS_ST_EXEC		0x1
#define DS_ST_CMISS		0x2
#define DS_ST_UPDTPC		0x3
#define DS_ST_WFE		0x4
#define DS_ST_ATBRR		0x5
#define DS_ST_QBUSY		0x6
#define DS_ST_WFP		0x7
#define DS_ST_KILL		0x8
#define DS_ST_CMPLT		0x9
#define DS_ST_FLTCMP		0xe
#define DS_ST_FAULT		0xf

#define DPC			0x4
#define INTEN			0x20
#define ES			0x24
#define INTSTATUS		0x28
#define INTCLR			0x2c
#define FSM			0x30
#define FSC			0x34
#define FTM			0x38

#define _FTC			0x40
#define FTC(n)			(_FTC + (n)*0x4)

#define _CS			0x100
#define CS(n)			(_CS + (n)*0x8)
#define CS_CNS			(1 << 21)

#define _CPC			0x104
#define CPC(n)			(_CPC + (n)*0x8)

#define _SA			0x400
#define SA(n)			(_SA + (n)*0x20)

#define _DA			0x404
#define DA(n)			(_DA + (n)*0x20)

#define _CC			0x408
#define CC(n)			(_CC + (n)*0x20)

#define CC_SRCINC		(1 << 0)
#define CC_DSTINC		(1 << 14)
#define CC_SRCPRI		(1 << 8)
#define CC_DSTPRI		(1 << 22)
#define CC_SRCNS		(1 << 9)
#define CC_DSTNS		(1 << 23)
#define CC_SRCIA		(1 << 10)
#define CC_DSTIA		(1 << 24)
#define CC_SRCBRSTLEN_SHFT	4
#define CC_DSTBRSTLEN_SHFT	18
#define CC_SRCBRSTSIZE_SHFT	1
#define CC_DSTBRSTSIZE_SHFT	15
#define CC_SRCCCTRL_SHFT	11
#define CC_SRCCCTRL_MASK	0x7
#define CC_DSTCCTRL_SHFT	25
#define CC_DRCCCTRL_MASK	0x7
#define CC_SWAP_SHFT		28

#define _LC0			0x40c
#define LC0(n)			(_LC0 + (n)*0x20)

#define _LC1			0x410
#define LC1(n)			(_LC1 + (n)*0x20)

#define DBGSTATUS		0xd00
#define DBG_BUSY		(1 << 0)

#define DBGCMD			0xd04
#define DBGINST0		0xd08
#define DBGINST1		0xd0c

#define CR0			0xe00
#define CR1			0xe04
#define CR2			0xe08
#define CR3			0xe0c
#define CR4			0xe10
#define CRD			0xe14

#define PERIPH_ID		0xfe0
#define PERIPH_REV_SHIFT	20
#define PERIPH_REV_MASK		0xf
#define PERIPH_REV_R0P0		0
#define PERIPH_REV_R1P0		1
#define PERIPH_REV_R1P1		2

#define CR0_PERIPH_REQ_SET	(1 << 0)
#define CR0_BOOT_EN_SET		(1 << 1)
#define CR0_BOOT_MAN_NS		(1 << 2)
#define CR0_NUM_CHANS_SHIFT	4
#define CR0_NUM_CHANS_MASK	0x7
#define CR0_NUM_PERIPH_SHIFT	12
#define CR0_NUM_PERIPH_MASK	0x1f
#define CR0_NUM_EVENTS_SHIFT	17
#define CR0_NUM_EVENTS_MASK	0x1f

#define CR1_ICACHE_LEN_SHIFT	0
#define CR1_ICACHE_LEN_MASK	0x7
#define CR1_NUM_ICACHELINES_SHIFT	4
#define CR1_NUM_ICACHELINES_MASK	0xf

#define CRD_DATA_WIDTH_SHIFT	0
#define CRD_DATA_WIDTH_MASK	0x7
#define CRD_WR_CAP_SHIFT	4
#define CRD_WR_CAP_MASK		0x7
#define CRD_WR_Q_DEP_SHIFT	8
#define CRD_WR_Q_DEP_MASK	0xf
#define CRD_RD_CAP_SHIFT	12
#define CRD_RD_CAP_MASK		0x7
#define CRD_RD_Q_DEP_SHIFT	16
#define CRD_RD_Q_DEP_MASK	0xf
#define CRD_DATA_BUFF_SHIFT	20
#define CRD_DATA_BUFF_MASK	0x3ff

#define PART			0x330
#define DESIGNER		0x41
#define REVISION		0x0
#define INTEG_CFG		0x0
#define PERIPH_ID_VAL		((PART << 0) | (DESIGNER << 12))

#define PL330_STATE_STOPPED		(1 << 0)
#define PL330_STATE_EXECUTING		(1 << 1)
#define PL330_STATE_WFE			(1 << 2)
#define PL330_STATE_FAULTING		(1 << 3)
#define PL330_STATE_COMPLETING		(1 << 4)
#define PL330_STATE_WFP			(1 << 5)
#define PL330_STATE_KILLING		(1 << 6)
#define PL330_STATE_FAULT_COMPLETING	(1 << 7)
#define PL330_STATE_CACHEMISS		(1 << 8)
#define PL330_STATE_UPDTPC		(1 << 9)
#define PL330_STATE_ATBARRIER		(1 << 10)
#define PL330_STATE_QUEUEBUSY		(1 << 11)
#define PL330_STATE_INVALID		(1 << 15)

#define PL330_STABLE_STATES (PL330_STATE_STOPPED | PL330_STATE_EXECUTING \
				| PL330_STATE_WFE | PL330_STATE_FAULTING)

#define CMD_DMAADDH		0x54
#define CMD_DMAEND		0x00
#define CMD_DMAFLUSHP		0x35
#define CMD_DMAGO		0xa0
#define CMD_DMALD		0x04
#define CMD_DMALDP		0x25
#define CMD_DMALP		0x20
#define CMD_DMALPEND		0x28
#define CMD_DMAKILL		0x01
#define CMD_DMAMOV		0xbc
#define CMD_DMANOP		0x18
#define CMD_DMARMB		0x12
#define CMD_DMASEV		0x34
#define CMD_DMAST		0x08
#define CMD_DMASTP		0x29
#define CMD_DMASTZ		0x0c
#define CMD_DMAWFE		0x36
#define CMD_DMAWFP		0x30
#define CMD_DMAWMB		0x13

#define SZ_DMAADDH		3
#define SZ_DMAEND		1
#define SZ_DMAFLUSHP		2
#define SZ_DMALD		1
#define SZ_DMALDP		2
#define SZ_DMALP		2
#define SZ_DMALPEND		2
#define SZ_DMAKILL		1
#define SZ_DMAMOV		6
#define SZ_DMANOP		1
#define SZ_DMARMB		1
#define SZ_DMASEV		2
#define SZ_DMAST		1
#define SZ_DMASTP		2
#define SZ_DMASTZ		1
#define SZ_DMAWFE		2
#define SZ_DMAWFP		2
#define SZ_DMAWMB		1
#define SZ_DMAGO		6

#define BRST_LEN(ccr)		((((ccr) >> CC_SRCBRSTLEN_SHFT) & 0xf) + 1)
#define BRST_SIZE(ccr)		(1 << (((ccr) >> CC_SRCBRSTSIZE_SHFT) & 0x7))

#define BYTE_TO_BURST(b, ccr)	((b) / BRST_SIZE(ccr) / BRST_LEN(ccr))
#define BURST_TO_BYTE(c, ccr)	((c) * BRST_SIZE(ccr) * BRST_LEN(ccr))

/*
 * With 256 bytes, we can do more than 2.5MB and 5MB xfers per req
 * at 1byte/burst for P<->M and M<->M respectively.
 * For typical scenario, at 1word/burst, 10MB and 20MB xfers per req
 * should be enough for P<->M and M<->M respectively.
 */
#define MCODE_BUFF_PER_REQ	256

/* Use this _only_ to wait on transient states */
#define UNTIL(t, s)	while (!(_state(t) & (s))) cpu_relax();

#ifdef PL330_DEBUG_MCGEN
static unsigned cmd_line;
#define PL330_DBGCMD_DUMP(off, x...)	do { \
						printk("%x:", cmd_line); \
						printk(x); \
						cmd_line += off; \
					} while (0)
#define PL330_DBGMC_START(addr)		(cmd_line = addr)
#else
#define PL330_DBGCMD_DUMP(off, x...)	do {} while (0)
#define PL330_DBGMC_START(addr)		do {} while (0)
#endif

/* The number of default descriptors */

#define NR_DEFAULT_DESC	16

/* Populated by the PL330 core driver for DMA API driver's info */
struct pl330_config {
	u32	periph_id;
#define DMAC_MODE_NS	(1 << 0)
	unsigned int	mode;
	unsigned int	data_bus_width:10; /* In number of bits */
	unsigned int	data_buf_dep:11;
	unsigned int	num_chan:4;
	unsigned int	num_peri:6;
	u32		peri_ns;
	unsigned int	num_events:6;
	u32		irq_ns;
};

/**
 * Request Configuration.
 * The PL330 core does not modify this and uses the last
 * working configuration if the request doesn't provide any.
 *
 * The Client may want to provide this info only for the
 * first request and a request with new settings.
 */
struct pl330_reqcfg {
	/* Address Incrementing */
	unsigned dst_inc:1;
	unsigned src_inc:1;

	/*
	 * For now, the SRC & DST protection levels
	 * and burst size/length are assumed same.
	 */
	bool nonsecure;
	bool privileged;
	bool insnaccess;
	unsigned brst_len:5;
	unsigned brst_size:3; /* in power of 2 */

	enum pl330_cachectrl dcctl;
	enum pl330_cachectrl scctl;
	enum pl330_byteswap swap;
	struct pl330_config *pcfg;
};

/*
 * One cycle of DMAC operation.
 * There may be more than one xfer in a request.
 */
struct pl330_xfer {
	u32 src_addr;
	u32 dst_addr;
	/* Size to xfer */
	u32 bytes;
};

/* The xfer callbacks are made with one of these arguments. */
enum pl330_op_err {
	/* The all xfers in the request were success. */
	PL330_ERR_NONE,
	/* If req aborted due to global error. */
	PL330_ERR_ABORT,
	/* If req failed due to problem with Channel. */
	PL330_ERR_FAIL,
};

enum dmamov_dst {
	SAR = 0,
	CCR,
	DAR,
};

enum pl330_dst {
	SRC = 0,
	DST,
};

enum pl330_cond {
	SINGLE,
	BURST,
	ALWAYS,
};

struct dma_pl330_desc;

struct _pl330_req {
	u32 mc_bus;
	void *mc_cpu;
	struct dma_pl330_desc *desc;
};

/* ToBeDone for tasklet */
struct _pl330_tbd {
	bool reset_dmac;
	bool reset_mngr;
	u8 reset_chan;
};

/* A DMAC Thread */
struct pl330_thread {
	u8 id;
	int ev;
	/* If the channel is not yet acquired by any client */
	bool free;
	/* Parent DMAC */
	struct pl330_dmac *dmac;
	/* Only two at a time */
	struct _pl330_req req[2];
	/* Index of the last enqueued request */
	unsigned lstenq;
	/* Index of the last submitted request or -1 if the DMA is stopped */
	int req_running;
};

enum pl330_dmac_state {
	UNINIT,
	INIT,
	DYING,
};

enum desc_status {
	/* In the DMAC pool */
	FREE,
	/*
	 * Allocated to some channel during prep_xxx
	 * Also may be sitting on the work_list.
	 */
	PREP,
	/*
	 * Sitting on the work_list and already submitted
	 * to the PL330 core. Not more than two descriptors
	 * of a channel can be BUSY at any time.
	 */
	BUSY,
	/*
	 * Sitting on the channel work_list but xfer done
	 * by PL330 core
	 */
	DONE,
};

struct dma_pl330_chan {
	/* Schedule desc completion */
	struct tasklet_struct task;

	/* DMA-Engine Channel */
	struct dma_chan chan;

	/* List of submitted descriptors */
	struct list_head submitted_list;
	/* List of issued descriptors */
	struct list_head work_list;
	/* List of completed descriptors */
	struct list_head completed_list;

	/* Pointer to the DMAC that manages this channel,
	 * NULL if the channel is available to be acquired.
	 * As the parent, this DMAC also provides descriptors
	 * to the channel.
	 */
	struct pl330_dmac *dmac;

	/* To protect channel manipulation */
	spinlock_t lock;

	/*
	 * Hardware channel thread of PL330 DMAC. NULL if the channel is
	 * available.
	 */
	struct pl330_thread *thread;

	/* For D-to-M and M-to-D channels */
	int burst_sz; /* the peripheral fifo width */
	int burst_len; /* the number of burst */
	dma_addr_t fifo_addr;

	/* for cyclic capability */
	bool cyclic;
};

struct pl330_dmac {
	/* DMA-Engine Device */
	struct dma_device ddma;

	/* Holds info about sg limitations */
	struct device_dma_parameters dma_parms;

	/* Pool of descriptors available for the DMAC's channels */
	struct list_head desc_pool;
	/* To protect desc_pool manipulation */
	spinlock_t pool_lock;

	/* Size of MicroCode buffers for each channel. */
	unsigned mcbufsz;
	/* ioremap'ed address of PL330 registers. */
	void __iomem	*base;
	/* Populated by the PL330 core driver during pl330_add */
	struct pl330_config	pcfg;

	spinlock_t		lock;
	/* Maximum possible events/irqs */
	int			events[32];
	/* BUS address of MicroCode buffer */
	dma_addr_t		mcode_bus;
	/* CPU address of MicroCode buffer */
	void			*mcode_cpu;
	/* List of all Channel threads */
	struct pl330_thread	*channels;
	/* Pointer to the MANAGER thread */
	struct pl330_thread	*manager;
	/* To handle bad news in interrupt */
	struct tasklet_struct	tasks;
	struct _pl330_tbd	dmac_tbd;
	/* State of DMAC operation */
	enum pl330_dmac_state	state;
	/* Holds list of reqs with due callbacks */
	struct list_head        req_done;

	/* Peripheral channels connected to this DMAC */
	unsigned int num_peripherals;
	struct dma_pl330_chan *peripherals; /* keep at end */
};

struct dma_pl330_desc {
	/* To attach to a queue as child */
	struct list_head node;

	/* Descriptor for the DMA Engine API */
	struct dma_async_tx_descriptor txd;

	/* Xfer for PL330 core */
	struct pl330_xfer px;

	struct pl330_reqcfg rqcfg;

	enum desc_status status;

	/* The channel which currently holds this desc */
	struct dma_pl330_chan *pchan;

	enum dma_transfer_direction rqtype;
	/* Index of peripheral for the xfer. */
	unsigned peri:5;
	/* Hook to attach to DMAC's list of reqs with due callback */
	struct list_head rqd;
};

struct _xfer_spec {
	u32 ccr;
	struct dma_pl330_desc *desc;
};

static inline bool _queue_empty(struct pl330_thread *thrd)
{
	return thrd->req[0].desc == NULL && thrd->req[1].desc == NULL;
}

static inline bool _queue_full(struct pl330_thread *thrd)
{
	return thrd->req[0].desc != NULL && thrd->req[1].desc != NULL;
}

static inline bool is_manager(struct pl330_thread *thrd)
{
	return thrd->dmac->manager == thrd;
}

/* If manager of the thread is in Non-Secure mode */
static inline bool _manager_ns(struct pl330_thread *thrd)
{
	return (thrd->dmac->pcfg.mode & DMAC_MODE_NS) ? true : false;
}

static inline u32 get_revision(u32 periph_id)
{
	return (periph_id >> PERIPH_REV_SHIFT) & PERIPH_REV_MASK;
}

static inline u32 _emit_ADDH(unsigned dry_run, u8 buf[],
		enum pl330_dst da, u16 val)
{
	if (dry_run)
		return SZ_DMAADDH;

	buf[0] = CMD_DMAADDH;
	buf[0] |= (da << 1);
	*((u16 *)&buf[1]) = val;

	PL330_DBGCMD_DUMP(SZ_DMAADDH, "\tDMAADDH %s %u\n",
		da == 1 ? "DA" : "SA", val);

	return SZ_DMAADDH;
}

static inline u32 _emit_END(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMAEND;

	buf[0] = CMD_DMAEND;

	PL330_DBGCMD_DUMP(SZ_DMAEND, "\tDMAEND\n");

	return SZ_DMAEND;
}

static inline u32 _emit_FLUSHP(unsigned dry_run, u8 buf[], u8 peri)
{
	if (dry_run)
		return SZ_DMAFLUSHP;

	buf[0] = CMD_DMAFLUSHP;

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMAFLUSHP, "\tDMAFLUSHP %u\n", peri >> 3);

	return SZ_DMAFLUSHP;
}

static inline u32 _emit_LD(unsigned dry_run, u8 buf[],	enum pl330_cond cond)
{
	if (dry_run)
		return SZ_DMALD;

	buf[0] = CMD_DMALD;

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (1 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (1 << 0);

	PL330_DBGCMD_DUMP(SZ_DMALD, "\tDMALD%c\n",
		cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'A'));

	return SZ_DMALD;
}

static inline u32 _emit_LDP(unsigned dry_run, u8 buf[],
		enum pl330_cond cond, u8 peri)
{
	if (dry_run)
		return SZ_DMALDP;

	buf[0] = CMD_DMALDP;

	if (cond == BURST)
		buf[0] |= (1 << 1);

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMALDP, "\tDMALDP%c %u\n",
		cond == SINGLE ? 'S' : 'B', peri >> 3);

	return SZ_DMALDP;
}

static inline u32 _emit_LP(unsigned dry_run, u8 buf[],
		unsigned loop, u8 cnt)
{
	if (dry_run)
		return SZ_DMALP;

	buf[0] = CMD_DMALP;

	if (loop)
		buf[0] |= (1 << 1);

	cnt--; /* DMAC increments by 1 internally */
	buf[1] = cnt;

	PL330_DBGCMD_DUMP(SZ_DMALP, "\tDMALP_%c %u\n", loop ? '1' : '0', cnt);

	return SZ_DMALP;
}

struct _arg_LPEND {
	enum pl330_cond cond;
	bool forever;
	unsigned loop;
	u8 bjump;
};

static inline u32 _emit_LPEND(unsigned dry_run, u8 buf[],
		const struct _arg_LPEND *arg)
{
	enum pl330_cond cond = arg->cond;
	bool forever = arg->forever;
	unsigned loop = arg->loop;
	u8 bjump = arg->bjump;

	if (dry_run)
		return SZ_DMALPEND;

	buf[0] = CMD_DMALPEND;

	if (loop)
		buf[0] |= (1 << 2);

	if (!forever)
		buf[0] |= (1 << 4);

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (1 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (1 << 0);

	buf[1] = bjump;

	PL330_DBGCMD_DUMP(SZ_DMALPEND, "\tDMALP%s%c_%c bjmpto_%x\n",
			forever ? "FE" : "END",
			cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'A'),
			loop ? '1' : '0',
			bjump);

	return SZ_DMALPEND;
}

static inline u32 _emit_KILL(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMAKILL;

	buf[0] = CMD_DMAKILL;

	return SZ_DMAKILL;
}

static inline u32 _emit_MOV(unsigned dry_run, u8 buf[],
		enum dmamov_dst dst, u32 val)
{
	if (dry_run)
		return SZ_DMAMOV;

	buf[0] = CMD_DMAMOV;
	buf[1] = dst;
	*((u32 *)&buf[2]) = val;

	PL330_DBGCMD_DUMP(SZ_DMAMOV, "\tDMAMOV %s 0x%x\n",
		dst == SAR ? "SAR" : (dst == DAR ? "DAR" : "CCR"), val);

	return SZ_DMAMOV;
}

static inline u32 _emit_NOP(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMANOP;

	buf[0] = CMD_DMANOP;

	PL330_DBGCMD_DUMP(SZ_DMANOP, "\tDMANOP\n");

	return SZ_DMANOP;
}

static inline u32 _emit_RMB(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMARMB;

	buf[0] = CMD_DMARMB;

	PL330_DBGCMD_DUMP(SZ_DMARMB, "\tDMARMB\n");

	return SZ_DMARMB;
}

static inline u32 _emit_SEV(unsigned dry_run, u8 buf[], u8 ev)
{
	if (dry_run)
		return SZ_DMASEV;

	buf[0] = CMD_DMASEV;

	ev &= 0x1f;
	ev <<= 3;
	buf[1] = ev;

	PL330_DBGCMD_DUMP(SZ_DMASEV, "\tDMASEV %u\n", ev >> 3);

	return SZ_DMASEV;
}

static inline u32 _emit_ST(unsigned dry_run, u8 buf[], enum pl330_cond cond)
{
	if (dry_run)
		return SZ_DMAST;

	buf[0] = CMD_DMAST;

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (1 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (1 << 0);

	PL330_DBGCMD_DUMP(SZ_DMAST, "\tDMAST%c\n",
		cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'A'));

	return SZ_DMAST;
}

static inline u32 _emit_STP(unsigned dry_run, u8 buf[],
		enum pl330_cond cond, u8 peri)
{
	if (dry_run)
		return SZ_DMASTP;

	buf[0] = CMD_DMASTP;

	if (cond == BURST)
		buf[0] |= (1 << 1);

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMASTP, "\tDMASTP%c %u\n",
		cond == SINGLE ? 'S' : 'B', peri >> 3);

	return SZ_DMASTP;
}

static inline u32 _emit_STZ(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMASTZ;

	buf[0] = CMD_DMASTZ;

	PL330_DBGCMD_DUMP(SZ_DMASTZ, "\tDMASTZ\n");

	return SZ_DMASTZ;
}

static inline u32 _emit_WFE(unsigned dry_run, u8 buf[], u8 ev,
		unsigned invalidate)
{
	if (dry_run)
		return SZ_DMAWFE;

	buf[0] = CMD_DMAWFE;

	ev &= 0x1f;
	ev <<= 3;
	buf[1] = ev;

	if (invalidate)
		buf[1] |= (1 << 1);

	PL330_DBGCMD_DUMP(SZ_DMAWFE, "\tDMAWFE %u%s\n",
		ev >> 3, invalidate ? ", I" : "");

	return SZ_DMAWFE;
}

static inline u32 _emit_WFP(unsigned dry_run, u8 buf[],
		enum pl330_cond cond, u8 peri)
{
	if (dry_run)
		return SZ_DMAWFP;

	buf[0] = CMD_DMAWFP;

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (0 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (0 << 0);
	else
		buf[0] |= (0 << 1) | (1 << 0);

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMAWFP, "\tDMAWFP%c %u\n",
		cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'P'), peri >> 3);

	return SZ_DMAWFP;
}

static inline u32 _emit_WMB(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMAWMB;

	buf[0] = CMD_DMAWMB;

	PL330_DBGCMD_DUMP(SZ_DMAWMB, "\tDMAWMB\n");

	return SZ_DMAWMB;
}

struct _arg_GO {
	u8 chan;
	u32 addr;
	unsigned ns;
};

static inline u32 _emit_GO(unsigned dry_run, u8 buf[],
		const struct _arg_GO *arg)
{
	u8 chan = arg->chan;
	u32 addr = arg->addr;
	unsigned ns = arg->ns;

	if (dry_run)
		return SZ_DMAGO;

	buf[0] = CMD_DMAGO;
	buf[0] |= (ns << 1);

	buf[1] = chan & 0x7;

	*((u32 *)&buf[2]) = addr;

	return SZ_DMAGO;
}

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)

/* Returns Time-Out */
static bool _until_dmac_idle(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->base;
	unsigned long loops = msecs_to_loops(5);

	do {
		/* Until Manager is Idle */
		if (!(readl(regs + DBGSTATUS) & DBG_BUSY))
			break;

		cpu_relax();
	} while (--loops);

	if (!loops)
		return true;

	return false;
}

static inline void _execute_DBGINSN(struct pl330_thread *thrd,
		u8 insn[], bool as_manager)
{
	void __iomem *regs = thrd->dmac->base;
	u32 val;

	val = (insn[0] << 16) | (insn[1] << 24);
	if (!as_manager) {
		val |= (1 << 0);
		val |= (thrd->id << 8); /* Channel Number */
	}
	writel(val, regs + DBGINST0);

	val = *((u32 *)&insn[2]);
	writel(val, regs + DBGINST1);

	/* If timed out due to halted state-machine */
	if (_until_dmac_idle(thrd)) {
		dev_err(thrd->dmac->ddma.dev, "DMAC halted!\n");
		return;
	}

	/* Get going */
	writel(0, regs + DBGCMD);
}

static inline u32 _state(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->base;
	u32 val;

	if (is_manager(thrd))
		val = readl(regs + DS) & 0xf;
	else
		val = readl(regs + CS(thrd->id)) & 0xf;

	switch (val) {
	case DS_ST_STOP:
		return PL330_STATE_STOPPED;
	case DS_ST_EXEC:
		return PL330_STATE_EXECUTING;
	case DS_ST_CMISS:
		return PL330_STATE_CACHEMISS;
	case DS_ST_UPDTPC:
		return PL330_STATE_UPDTPC;
	case DS_ST_WFE:
		return PL330_STATE_WFE;
	case DS_ST_FAULT:
		return PL330_STATE_FAULTING;
	case DS_ST_ATBRR:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_ATBARRIER;
	case DS_ST_QBUSY:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_QUEUEBUSY;
	case DS_ST_WFP:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_WFP;
	case DS_ST_KILL:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_KILLING;
	case DS_ST_CMPLT:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_COMPLETING;
	case DS_ST_FLTCMP:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_FAULT_COMPLETING;
	default:
		return PL330_STATE_INVALID;
	}
}

static void _stop(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->base;
	u8 insn[6] = {0, 0, 0, 0, 0, 0};

	if (_state(thrd) == PL330_STATE_FAULT_COMPLETING)
		UNTIL(thrd, PL330_STATE_FAULTING | PL330_STATE_KILLING);

	/* Return if nothing needs to be done */
	if (_state(thrd) == PL330_STATE_COMPLETING
		  || _state(thrd) == PL330_STATE_KILLING
		  || _state(thrd) == PL330_STATE_STOPPED)
		return;

	_emit_KILL(0, insn);

	/* Stop generating interrupts for SEV */
	writel(readl(regs + INTEN) & ~(1 << thrd->ev), regs + INTEN);

	_execute_DBGINSN(thrd, insn, is_manager(thrd));
}

/* Start doing req 'idx' of thread 'thrd' */
static bool _trigger(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->base;
	struct _pl330_req *req;
	struct dma_pl330_desc *desc;
	struct _arg_GO go;
	unsigned ns;
	u8 insn[6] = {0, 0, 0, 0, 0, 0};
	int idx;

	/* Return if already ACTIVE */
	if (_state(thrd) != PL330_STATE_STOPPED)
		return true;

	idx = 1 - thrd->lstenq;
	if (thrd->req[idx].desc != NULL) {
		req = &thrd->req[idx];
	} else {
		idx = thrd->lstenq;
		if (thrd->req[idx].desc != NULL)
			req = &thrd->req[idx];
		else
			req = NULL;
	}

	/* Return if no request */
	if (!req)
		return true;

	desc = req->desc;

	ns = desc->rqcfg.nonsecure ? 1 : 0;

	/* See 'Abort Sources' point-4 at Page 2-25 */
	if (_manager_ns(thrd) && !ns)
		dev_info(thrd->dmac->ddma.dev, "%s:%d Recipe for ABORT!\n",
			__func__, __LINE__);

	go.chan = thrd->id;
	go.addr = req->mc_bus;
	go.ns = ns;
	_emit_GO(0, insn, &go);

	/* Set to generate interrupts for SEV */
	writel(readl(regs + INTEN) | (1 << thrd->ev), regs + INTEN);

	/* Only manager can execute GO */
	_execute_DBGINSN(thrd, insn, true);

	thrd->req_running = idx;

	return true;
}

static bool _start(struct pl330_thread *thrd)
{
	switch (_state(thrd)) {
	case PL330_STATE_FAULT_COMPLETING:
		UNTIL(thrd, PL330_STATE_FAULTING | PL330_STATE_KILLING);

		if (_state(thrd) == PL330_STATE_KILLING)
			UNTIL(thrd, PL330_STATE_STOPPED)

	case PL330_STATE_FAULTING:
		_stop(thrd);

	case PL330_STATE_KILLING:
	case PL330_STATE_COMPLETING:
		UNTIL(thrd, PL330_STATE_STOPPED)

	case PL330_STATE_STOPPED:
		return _trigger(thrd);

	case PL330_STATE_WFP:
	case PL330_STATE_QUEUEBUSY:
	case PL330_STATE_ATBARRIER:
	case PL330_STATE_UPDTPC:
	case PL330_STATE_CACHEMISS:
	case PL330_STATE_EXECUTING:
		return true;

	case PL330_STATE_WFE: /* For RESUME, nothing yet */
	default:
		return false;
	}
}

static inline int _ldst_memtomem(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;
	struct pl330_config *pcfg = pxs->desc->rqcfg.pcfg;

	/* check lock-up free version */
	if (get_revision(pcfg->periph_id) >= PERIPH_REV_R1P0) {
		while (cyc--) {
			off += _emit_LD(dry_run, &buf[off], ALWAYS);
			off += _emit_ST(dry_run, &buf[off], ALWAYS);
		}
	} else {
		while (cyc--) {
			off += _emit_LD(dry_run, &buf[off], ALWAYS);
			off += _emit_RMB(dry_run, &buf[off]);
			off += _emit_ST(dry_run, &buf[off], ALWAYS);
			off += _emit_WMB(dry_run, &buf[off]);
		}
	}

	return off;
}

static inline int _ldst_devtomem(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;

	while (cyc--) {
		off += _emit_WFP(dry_run, &buf[off], SINGLE, pxs->desc->peri);
		off += _emit_LDP(dry_run, &buf[off], SINGLE, pxs->desc->peri);
		off += _emit_ST(dry_run, &buf[off], ALWAYS);
		off += _emit_FLUSHP(dry_run, &buf[off], pxs->desc->peri);
	}

	return off;
}

static inline int _ldst_memtodev(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;

	while (cyc--) {
		off += _emit_WFP(dry_run, &buf[off], SINGLE, pxs->desc->peri);
		off += _emit_LD(dry_run, &buf[off], ALWAYS);
		off += _emit_STP(dry_run, &buf[off], SINGLE, pxs->desc->peri);
		off += _emit_FLUSHP(dry_run, &buf[off], pxs->desc->peri);
	}

	return off;
}

static int _bursts(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;

	switch (pxs->desc->rqtype) {
	case DMA_MEM_TO_DEV:
		off += _ldst_memtodev(dry_run, &buf[off], pxs, cyc);
		break;
	case DMA_DEV_TO_MEM:
		off += _ldst_devtomem(dry_run, &buf[off], pxs, cyc);
		break;
	case DMA_MEM_TO_MEM:
		off += _ldst_memtomem(dry_run, &buf[off], pxs, cyc);
		break;
	default:
		off += 0x40000000; /* Scare off the Client */
		break;
	}

	return off;
}

/* Returns bytes consumed and updates bursts */
static inline int _loop(unsigned dry_run, u8 buf[],
		unsigned long *bursts, const struct _xfer_spec *pxs)
{
	int cyc, cycmax, szlp, szlpend, szbrst, off;
	unsigned lcnt0, lcnt1, ljmp0, ljmp1;
	struct _arg_LPEND lpend;

	/* Max iterations possible in DMALP is 256 */
	if (*bursts >= 256*256) {
		lcnt1 = 256;
		lcnt0 = 256;
		cyc = *bursts / lcnt1 / lcnt0;
	} else if (*bursts > 256) {
		lcnt1 = 256;
		lcnt0 = *bursts / lcnt1;
		cyc = 1;
	} else {
		lcnt1 = *bursts;
		lcnt0 = 0;
		cyc = 1;
	}

	szlp = _emit_LP(1, buf, 0, 0);
	szbrst = _bursts(1, buf, pxs, 1);

	lpend.cond = ALWAYS;
	lpend.forever = false;
	lpend.loop = 0;
	lpend.bjump = 0;
	szlpend = _emit_LPEND(1, buf, &lpend);

	if (lcnt0) {
		szlp *= 2;
		szlpend *= 2;
	}

	/*
	 * Max bursts that we can unroll due to limit on the
	 * size of backward jump that can be encoded in DMALPEND
	 * which is 8-bits and hence 255
	 */
	cycmax = (255 - (szlp + szlpend)) / szbrst;

	cyc = (cycmax < cyc) ? cycmax : cyc;

	off = 0;

	if (lcnt0) {
		off += _emit_LP(dry_run, &buf[off], 0, lcnt0);
		ljmp0 = off;
	}

	off += _emit_LP(dry_run, &buf[off], 1, lcnt1);
	ljmp1 = off;

	off += _bursts(dry_run, &buf[off], pxs, cyc);

	lpend.cond = ALWAYS;
	lpend.forever = false;
	lpend.loop = 1;
	lpend.bjump = off - ljmp1;
	off += _emit_LPEND(dry_run, &buf[off], &lpend);

	if (lcnt0) {
		lpend.cond = ALWAYS;
		lpend.forever = false;
		lpend.loop = 0;
		lpend.bjump = off - ljmp0;
		off += _emit_LPEND(dry_run, &buf[off], &lpend);
	}

	*bursts = lcnt1 * cyc;
	if (lcnt0)
		*bursts *= lcnt0;

	return off;
}

static inline int _setup_loops(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs)
{
	struct pl330_xfer *x = &pxs->desc->px;
	u32 ccr = pxs->ccr;
	unsigned long c, bursts = BYTE_TO_BURST(x->bytes, ccr);
	int off = 0;

	while (bursts) {
		c = bursts;
		off += _loop(dry_run, &buf[off], &c, pxs);
		bursts -= c;
	}

	return off;
}

static inline int _setup_xfer(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs)
{
	struct pl330_xfer *x = &pxs->desc->px;
	int off = 0;

	/* DMAMOV SAR, x->src_addr */
	off += _emit_MOV(dry_run, &buf[off], SAR, x->src_addr);
	/* DMAMOV DAR, x->dst_addr */
	off += _emit_MOV(dry_run, &buf[off], DAR, x->dst_addr);

	/* Setup Loop(s) */
	off += _setup_loops(dry_run, &buf[off], pxs);

	return off;
}

/*
 * A req is a sequence of one or more xfer units.
 * Returns the number of bytes taken to setup the MC for the req.
 */
static int _setup_req(unsigned dry_run, struct pl330_thread *thrd,
		unsigned index, struct _xfer_spec *pxs)
{
	struct _pl330_req *req = &thrd->req[index];
	struct pl330_xfer *x;
	u8 *buf = req->mc_cpu;
	int off = 0;

	PL330_DBGMC_START(req->mc_bus);

	/* DMAMOV CCR, ccr */
	off += _emit_MOV(dry_run, &buf[off], CCR, pxs->ccr);

	x = &pxs->desc->px;
	/* Error if xfer length is not aligned at burst size */
	if (x->bytes % (BRST_SIZE(pxs->ccr) * BRST_LEN(pxs->ccr)))
		return -EINVAL;

	off += _setup_xfer(dry_run, &buf[off], pxs);

	/* DMASEV peripheral/event */
	off += _emit_SEV(dry_run, &buf[off], thrd->ev);
	/* DMAEND */
	off += _emit_END(dry_run, &buf[off]);

	return off;
}

static inline u32 _prepare_ccr(const struct pl330_reqcfg *rqc)
{
	u32 ccr = 0;

	if (rqc->src_inc)
		ccr |= CC_SRCINC;

	if (rqc->dst_inc)
		ccr |= CC_DSTINC;

	/* We set same protection levels for Src and DST for now */
	if (rqc->privileged)
		ccr |= CC_SRCPRI | CC_DSTPRI;
	if (rqc->nonsecure)
		ccr |= CC_SRCNS | CC_DSTNS;
	if (rqc->insnaccess)
		ccr |= CC_SRCIA | CC_DSTIA;

	ccr |= (((rqc->brst_len - 1) & 0xf) << CC_SRCBRSTLEN_SHFT);
	ccr |= (((rqc->brst_len - 1) & 0xf) << CC_DSTBRSTLEN_SHFT);

	ccr |= (rqc->brst_size << CC_SRCBRSTSIZE_SHFT);
	ccr |= (rqc->brst_size << CC_DSTBRSTSIZE_SHFT);

	ccr |= (rqc->scctl << CC_SRCCCTRL_SHFT);
	ccr |= (rqc->dcctl << CC_DSTCCTRL_SHFT);

	ccr |= (rqc->swap << CC_SWAP_SHFT);

	return ccr;
}

/*
 * Submit a list of xfers after which the client wants notification.
 * Client is not notified after each xfer unit, just once after all
 * xfer units are done or some error occurs.
 */
static int pl330_submit_req(struct pl330_thread *thrd,
	struct dma_pl330_desc *desc)
{
	struct pl330_dmac *pl330 = thrd->dmac;
	struct _xfer_spec xs;
	unsigned long flags;
	unsigned idx;
	u32 ccr;
	int ret = 0;

	if (pl330->state == DYING
		|| pl330->dmac_tbd.reset_chan & (1 << thrd->id)) {
		dev_info(thrd->dmac->ddma.dev, "%s:%d\n",
			__func__, __LINE__);
		return -EAGAIN;
	}

	/* If request for non-existing peripheral */
	if (desc->rqtype != DMA_MEM_TO_MEM &&
	    desc->peri >= pl330->pcfg.num_peri) {
		dev_info(thrd->dmac->ddma.dev,
				"%s:%d Invalid peripheral(%u)!\n",
				__func__, __LINE__, desc->peri);
		return -EINVAL;
	}

	spin_lock_irqsave(&pl330->lock, flags);

	if (_queue_full(thrd)) {
		ret = -EAGAIN;
		goto xfer_exit;
	}

	/* Prefer Secure Channel */
	if (!_manager_ns(thrd))
		desc->rqcfg.nonsecure = 0;
	else
		desc->rqcfg.nonsecure = 1;

	ccr = _prepare_ccr(&desc->rqcfg);

	idx = thrd->req[0].desc == NULL ? 0 : 1;

	xs.ccr = ccr;
	xs.desc = desc;

	/* First dry run to check if req is acceptable */
	ret = _setup_req(1, thrd, idx, &xs);
	if (ret < 0)
		goto xfer_exit;

	if (ret > pl330->mcbufsz / 2) {
		dev_info(pl330->ddma.dev, "%s:%d Trying increasing mcbufsz\n",
				__func__, __LINE__);
		ret = -ENOMEM;
		goto xfer_exit;
	}

	/* Hook the request */
	thrd->lstenq = idx;
	thrd->req[idx].desc = desc;
	_setup_req(0, thrd, idx, &xs);

	ret = 0;

xfer_exit:
	spin_unlock_irqrestore(&pl330->lock, flags);

	return ret;
}

static void dma_pl330_rqcb(struct dma_pl330_desc *desc, enum pl330_op_err err)
{
	struct dma_pl330_chan *pch;
	unsigned long flags;

	if (!desc)
		return;

	pch = desc->pchan;

	/* If desc aborted */
	if (!pch)
		return;

	spin_lock_irqsave(&pch->lock, flags);

	desc->status = DONE;

	spin_unlock_irqrestore(&pch->lock, flags);

	tasklet_schedule(&pch->task);
}

static void pl330_dotask(unsigned long data)
{
	struct pl330_dmac *pl330 = (struct pl330_dmac *) data;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&pl330->lock, flags);

	/* The DMAC itself gone nuts */
	if (pl330->dmac_tbd.reset_dmac) {
		pl330->state = DYING;
		/* Reset the manager too */
		pl330->dmac_tbd.reset_mngr = true;
		/* Clear the reset flag */
		pl330->dmac_tbd.reset_dmac = false;
	}

	if (pl330->dmac_tbd.reset_mngr) {
		_stop(pl330->manager);
		/* Reset all channels */
		pl330->dmac_tbd.reset_chan = (1 << pl330->pcfg.num_chan) - 1;
		/* Clear the reset flag */
		pl330->dmac_tbd.reset_mngr = false;
	}

	for (i = 0; i < pl330->pcfg.num_chan; i++) {

		if (pl330->dmac_tbd.reset_chan & (1 << i)) {
			struct pl330_thread *thrd = &pl330->channels[i];
			void __iomem *regs = pl330->base;
			enum pl330_op_err err;

			_stop(thrd);

			if (readl(regs + FSC) & (1 << thrd->id))
				err = PL330_ERR_FAIL;
			else
				err = PL330_ERR_ABORT;

			spin_unlock_irqrestore(&pl330->lock, flags);
			dma_pl330_rqcb(thrd->req[1 - thrd->lstenq].desc, err);
			dma_pl330_rqcb(thrd->req[thrd->lstenq].desc, err);
			spin_lock_irqsave(&pl330->lock, flags);

			thrd->req[0].desc = NULL;
			thrd->req[1].desc = NULL;
			thrd->req_running = -1;

			/* Clear the reset flag */
			pl330->dmac_tbd.reset_chan &= ~(1 << i);
		}
	}

	spin_unlock_irqrestore(&pl330->lock, flags);

	return;
}

/* Returns 1 if state was updated, 0 otherwise */
static int pl330_update(struct pl330_dmac *pl330)
{
	struct dma_pl330_desc *descdone, *tmp;
	unsigned long flags;
	void __iomem *regs;
	u32 val;
	int id, ev, ret = 0;

	regs = pl330->base;

	spin_lock_irqsave(&pl330->lock, flags);

	val = readl(regs + FSM) & 0x1;
	if (val)
		pl330->dmac_tbd.reset_mngr = true;
	else
		pl330->dmac_tbd.reset_mngr = false;

	val = readl(regs + FSC) & ((1 << pl330->pcfg.num_chan) - 1);
	pl330->dmac_tbd.reset_chan |= val;
	if (val) {
		int i = 0;
		while (i < pl330->pcfg.num_chan) {
			if (val & (1 << i)) {
				dev_info(pl330->ddma.dev,
					"Reset Channel-%d\t CS-%x FTC-%x\n",
						i, readl(regs + CS(i)),
						readl(regs + FTC(i)));
				_stop(&pl330->channels[i]);
			}
			i++;
		}
	}

	/* Check which event happened i.e, thread notified */
	val = readl(regs + ES);
	if (pl330->pcfg.num_events < 32
			&& val & ~((1 << pl330->pcfg.num_events) - 1)) {
		pl330->dmac_tbd.reset_dmac = true;
		dev_err(pl330->ddma.dev, "%s:%d Unexpected!\n", __func__,
			__LINE__);
		ret = 1;
		goto updt_exit;
	}

	for (ev = 0; ev < pl330->pcfg.num_events; ev++) {
		if (val & (1 << ev)) { /* Event occurred */
			struct pl330_thread *thrd;
			u32 inten = readl(regs + INTEN);
			int active;

			/* Clear the event */
			if (inten & (1 << ev))
				writel(1 << ev, regs + INTCLR);

			ret = 1;

			id = pl330->events[ev];

			thrd = &pl330->channels[id];

			active = thrd->req_running;
			if (active == -1) /* Aborted */
				continue;

			/* Detach the req */
			descdone = thrd->req[active].desc;
			thrd->req[active].desc = NULL;

			/* Get going again ASAP */
			_start(thrd);

			/* For now, just make a list of callbacks to be done */
			list_add_tail(&descdone->rqd, &pl330->req_done);
		}
	}

	/* Now that we are in no hurry, do the callbacks */
	list_for_each_entry_safe(descdone, tmp, &pl330->req_done, rqd) {
		list_del(&descdone->rqd);
		spin_unlock_irqrestore(&pl330->lock, flags);
		dma_pl330_rqcb(descdone, PL330_ERR_NONE);
		spin_lock_irqsave(&pl330->lock, flags);
	}

updt_exit:
	spin_unlock_irqrestore(&pl330->lock, flags);

	if (pl330->dmac_tbd.reset_dmac
			|| pl330->dmac_tbd.reset_mngr
			|| pl330->dmac_tbd.reset_chan) {
		ret = 1;
		tasklet_schedule(&pl330->tasks);
	}

	return ret;
}

/* Reserve an event */
static inline int _alloc_event(struct pl330_thread *thrd)
{
	struct pl330_dmac *pl330 = thrd->dmac;
	int ev;

	for (ev = 0; ev < pl330->pcfg.num_events; ev++)
		if (pl330->events[ev] == -1) {
			pl330->events[ev] = thrd->id;
			return ev;
		}

	return -1;
}

static bool _chan_ns(const struct pl330_dmac *pl330, int i)
{
	return pl330->pcfg.irq_ns & (1 << i);
}

/* Upon success, returns IdentityToken for the
 * allocated channel, NULL otherwise.
 */
static struct pl330_thread *pl330_request_channel(struct pl330_dmac *pl330)
{
	struct pl330_thread *thrd = NULL;
	unsigned long flags;
	int chans, i;

	if (pl330->state == DYING)
		return NULL;

	chans = pl330->pcfg.num_chan;

	spin_lock_irqsave(&pl330->lock, flags);

	for (i = 0; i < chans; i++) {
		thrd = &pl330->channels[i];
		if ((thrd->free) && (!_manager_ns(thrd) ||
					_chan_ns(pl330, i))) {
			thrd->ev = _alloc_event(thrd);
			if (thrd->ev >= 0) {
				thrd->free = false;
				thrd->lstenq = 1;
				thrd->req[0].desc = NULL;
				thrd->req[1].desc = NULL;
				thrd->req_running = -1;
				break;
			}
		}
		thrd = NULL;
	}

	spin_unlock_irqrestore(&pl330->lock, flags);

	return thrd;
}

/* Release an event */
static inline void _free_event(struct pl330_thread *thrd, int ev)
{
	struct pl330_dmac *pl330 = thrd->dmac;

	/* If the event is valid and was held by the thread */
	if (ev >= 0 && ev < pl330->pcfg.num_events
			&& pl330->events[ev] == thrd->id)
		pl330->events[ev] = -1;
}

static void pl330_release_channel(struct pl330_thread *thrd)
{
	struct pl330_dmac *pl330;
	unsigned long flags;

	if (!thrd || thrd->free)
		return;

	_stop(thrd);

	dma_pl330_rqcb(thrd->req[1 - thrd->lstenq].desc, PL330_ERR_ABORT);
	dma_pl330_rqcb(thrd->req[thrd->lstenq].desc, PL330_ERR_ABORT);

	pl330 = thrd->dmac;

	spin_lock_irqsave(&pl330->lock, flags);
	_free_event(thrd, thrd->ev);
	thrd->free = true;
	spin_unlock_irqrestore(&pl330->lock, flags);
}

/* Initialize the structure for PL330 configuration, that can be used
 * by the client driver the make best use of the DMAC
 */
static void read_dmac_config(struct pl330_dmac *pl330)
{
	void __iomem *regs = pl330->base;
	u32 val;

	val = readl(regs + CRD) >> CRD_DATA_WIDTH_SHIFT;
	val &= CRD_DATA_WIDTH_MASK;
	pl330->pcfg.data_bus_width = 8 * (1 << val);

	val = readl(regs + CRD) >> CRD_DATA_BUFF_SHIFT;
	val &= CRD_DATA_BUFF_MASK;
	pl330->pcfg.data_buf_dep = val + 1;

	val = readl(regs + CR0) >> CR0_NUM_CHANS_SHIFT;
	val &= CR0_NUM_CHANS_MASK;
	val += 1;
	pl330->pcfg.num_chan = val;

	val = readl(regs + CR0);
	if (val & CR0_PERIPH_REQ_SET) {
		val = (val >> CR0_NUM_PERIPH_SHIFT) & CR0_NUM_PERIPH_MASK;
		val += 1;
		pl330->pcfg.num_peri = val;
		pl330->pcfg.peri_ns = readl(regs + CR4);
	} else {
		pl330->pcfg.num_peri = 0;
	}

	val = readl(regs + CR0);
	if (val & CR0_BOOT_MAN_NS)
		pl330->pcfg.mode |= DMAC_MODE_NS;
	else
		pl330->pcfg.mode &= ~DMAC_MODE_NS;

	val = readl(regs + CR0) >> CR0_NUM_EVENTS_SHIFT;
	val &= CR0_NUM_EVENTS_MASK;
	val += 1;
	pl330->pcfg.num_events = val;

	pl330->pcfg.irq_ns = readl(regs + CR3);
}

static inline void _reset_thread(struct pl330_thread *thrd)
{
	struct pl330_dmac *pl330 = thrd->dmac;

	thrd->req[0].mc_cpu = pl330->mcode_cpu
				+ (thrd->id * pl330->mcbufsz);
	thrd->req[0].mc_bus = pl330->mcode_bus
				+ (thrd->id * pl330->mcbufsz);
	thrd->req[0].desc = NULL;

	thrd->req[1].mc_cpu = thrd->req[0].mc_cpu
				+ pl330->mcbufsz / 2;
	thrd->req[1].mc_bus = thrd->req[0].mc_bus
				+ pl330->mcbufsz / 2;
	thrd->req[1].desc = NULL;

	thrd->req_running = -1;
}

static int dmac_alloc_threads(struct pl330_dmac *pl330)
{
	int chans = pl330->pcfg.num_chan;
	struct pl330_thread *thrd;
	int i;

	/* Allocate 1 Manager and 'chans' Channel threads */
	pl330->channels = kzalloc((1 + chans) * sizeof(*thrd),
					GFP_KERNEL);
	if (!pl330->channels)
		return -ENOMEM;

	/* Init Channel threads */
	for (i = 0; i < chans; i++) {
		thrd = &pl330->channels[i];
		thrd->id = i;
		thrd->dmac = pl330;
		_reset_thread(thrd);
		thrd->free = true;
	}

	/* MANAGER is indexed at the end */
	thrd = &pl330->channels[chans];
	thrd->id = chans;
	thrd->dmac = pl330;
	thrd->free = false;
	pl330->manager = thrd;

	return 0;
}

static int dmac_alloc_resources(struct pl330_dmac *pl330)
{
	int chans = pl330->pcfg.num_chan;
	int ret;

	/*
	 * Alloc MicroCode buffer for 'chans' Channel threads.
	 * A channel's buffer offset is (Channel_Id * MCODE_BUFF_PERCHAN)
	 */
	pl330->mcode_cpu = dma_alloc_coherent(pl330->ddma.dev,
				chans * pl330->mcbufsz,
				&pl330->mcode_bus, GFP_KERNEL);
	if (!pl330->mcode_cpu) {
		dev_err(pl330->ddma.dev, "%s:%d Can't allocate memory!\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	ret = dmac_alloc_threads(pl330);
	if (ret) {
		dev_err(pl330->ddma.dev, "%s:%d Can't to create channels for DMAC!\n",
			__func__, __LINE__);
		dma_free_coherent(pl330->ddma.dev,
				chans * pl330->mcbufsz,
				pl330->mcode_cpu, pl330->mcode_bus);
		return ret;
	}

	return 0;
}

static int pl330_add(struct pl330_dmac *pl330)
{
	void __iomem *regs;
	int i, ret;

	regs = pl330->base;

	/* Check if we can handle this DMAC */
	if ((pl330->pcfg.periph_id & 0xfffff) != PERIPH_ID_VAL) {
		dev_err(pl330->ddma.dev, "PERIPH_ID 0x%x !\n",
			pl330->pcfg.periph_id);
		return -EINVAL;
	}

	/* Read the configuration of the DMAC */
	read_dmac_config(pl330);

	if (pl330->pcfg.num_events == 0) {
		dev_err(pl330->ddma.dev, "%s:%d Can't work without events!\n",
			__func__, __LINE__);
		return -EINVAL;
	}

	spin_lock_init(&pl330->lock);

	INIT_LIST_HEAD(&pl330->req_done);

	/* Use default MC buffer size if not provided */
	if (!pl330->mcbufsz)
		pl330->mcbufsz = MCODE_BUFF_PER_REQ * 2;

	/* Mark all events as free */
	for (i = 0; i < pl330->pcfg.num_events; i++)
		pl330->events[i] = -1;

	/* Allocate resources needed by the DMAC */
	ret = dmac_alloc_resources(pl330);
	if (ret) {
		dev_err(pl330->ddma.dev, "Unable to create channels for DMAC\n");
		return ret;
	}

	tasklet_init(&pl330->tasks, pl330_dotask, (unsigned long) pl330);

	pl330->state = INIT;

	return 0;
}

static int dmac_free_threads(struct pl330_dmac *pl330)
{
	struct pl330_thread *thrd;
	int i;

	/* Release Channel threads */
	for (i = 0; i < pl330->pcfg.num_chan; i++) {
		thrd = &pl330->channels[i];
		pl330_release_channel(thrd);
	}

	/* Free memory */
	kfree(pl330->channels);

	return 0;
}

static void pl330_del(struct pl330_dmac *pl330)
{
	pl330->state = UNINIT;

	tasklet_kill(&pl330->tasks);

	/* Free DMAC resources */
	dmac_free_threads(pl330);

	dma_free_coherent(pl330->ddma.dev,
		pl330->pcfg.num_chan * pl330->mcbufsz, pl330->mcode_cpu,
		pl330->mcode_bus);
}

/* forward declaration */
static struct amba_driver pl330_driver;

static inline struct dma_pl330_chan *
to_pchan(struct dma_chan *ch)
{
	if (!ch)
		return NULL;

	return container_of(ch, struct dma_pl330_chan, chan);
}

static inline struct dma_pl330_desc *
to_desc(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct dma_pl330_desc, txd);
}

static inline void fill_queue(struct dma_pl330_chan *pch)
{
	struct dma_pl330_desc *desc;
	int ret;

	list_for_each_entry(desc, &pch->work_list, node) {

		/* If already submitted */
		if (desc->status == BUSY)
			continue;

		ret = pl330_submit_req(pch->thread, desc);
		if (!ret) {
			desc->status = BUSY;
		} else if (ret == -EAGAIN) {
			/* QFull or DMAC Dying */
			break;
		} else {
			/* Unacceptable request */
			desc->status = DONE;
			dev_err(pch->dmac->ddma.dev, "%s:%d Bad Desc(%d)\n",
					__func__, __LINE__, desc->txd.cookie);
			tasklet_schedule(&pch->task);
		}
	}
}

static void pl330_tasklet(unsigned long data)
{
	struct dma_pl330_chan *pch = (struct dma_pl330_chan *)data;
	struct dma_pl330_desc *desc, *_dt;
	unsigned long flags;

	spin_lock_irqsave(&pch->lock, flags);

	/* Pick up ripe tomatoes */
	list_for_each_entry_safe(desc, _dt, &pch->work_list, node)
		if (desc->status == DONE) {
			if (!pch->cyclic)
				dma_cookie_complete(&desc->txd);
			list_move_tail(&desc->node, &pch->completed_list);
		}

	/* Try to submit a req imm. next to the last completed cookie */
	fill_queue(pch);

	/* Make sure the PL330 Channel thread is active */
	spin_lock(&pch->thread->dmac->lock);
	_start(pch->thread);
	spin_unlock(&pch->thread->dmac->lock);

	while (!list_empty(&pch->completed_list)) {
		dma_async_tx_callback callback;
		void *callback_param;

		desc = list_first_entry(&pch->completed_list,
					struct dma_pl330_desc, node);

		callback = desc->txd.callback;
		callback_param = desc->txd.callback_param;

		if (pch->cyclic) {
			desc->status = PREP;
			list_move_tail(&desc->node, &pch->work_list);
		} else {
			desc->status = FREE;
			list_move_tail(&desc->node, &pch->dmac->desc_pool);
		}

		dma_descriptor_unmap(&desc->txd);

		if (callback) {
			spin_unlock_irqrestore(&pch->lock, flags);
			callback(callback_param);
			spin_lock_irqsave(&pch->lock, flags);
		}
	}
	spin_unlock_irqrestore(&pch->lock, flags);
}

bool pl330_filter(struct dma_chan *chan, void *param)
{
	u8 *peri_id;

	if (chan->device->dev->driver != &pl330_driver.drv)
		return false;

	peri_id = chan->private;
	return *peri_id == (unsigned long)param;
}
EXPORT_SYMBOL(pl330_filter);

static struct dma_chan *of_dma_pl330_xlate(struct of_phandle_args *dma_spec,
						struct of_dma *ofdma)
{
	int count = dma_spec->args_count;
	struct pl330_dmac *pl330 = ofdma->of_dma_data;
	unsigned int chan_id;

	if (!pl330)
		return NULL;

	if (count != 1)
		return NULL;

	chan_id = dma_spec->args[0];
	if (chan_id >= pl330->num_peripherals)
		return NULL;

	return dma_get_slave_channel(&pl330->peripherals[chan_id].chan);
}

static int pl330_alloc_chan_resources(struct dma_chan *chan)
{
	struct dma_pl330_chan *pch = to_pchan(chan);
	struct pl330_dmac *pl330 = pch->dmac;
	unsigned long flags;

	spin_lock_irqsave(&pch->lock, flags);

	dma_cookie_init(chan);
	pch->cyclic = false;

	pch->thread = pl330_request_channel(pl330);
	if (!pch->thread) {
		spin_unlock_irqrestore(&pch->lock, flags);
		return -ENOMEM;
	}

	tasklet_init(&pch->task, pl330_tasklet, (unsigned long) pch);

	spin_unlock_irqrestore(&pch->lock, flags);

	return 1;
}

static int pl330_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd, unsigned long arg)
{
	struct dma_pl330_chan *pch = to_pchan(chan);
	struct dma_pl330_desc *desc;
	unsigned long flags;
	struct pl330_dmac *pl330 = pch->dmac;
	struct dma_slave_config *slave_config;
	LIST_HEAD(list);

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		spin_lock_irqsave(&pch->lock, flags);

		spin_lock(&pl330->lock);
		_stop(pch->thread);
		spin_unlock(&pl330->lock);

		pch->thread->req[0].desc = NULL;
		pch->thread->req[1].desc = NULL;
		pch->thread->req_running = -1;

		/* Mark all desc done */
		list_for_each_entry(desc, &pch->submitted_list, node) {
			desc->status = FREE;
			dma_cookie_complete(&desc->txd);
		}

		list_for_each_entry(desc, &pch->work_list , node) {
			desc->status = FREE;
			dma_cookie_complete(&desc->txd);
		}

		list_for_each_entry(desc, &pch->completed_list , node) {
			desc->status = FREE;
			dma_cookie_complete(&desc->txd);
		}

		list_splice_tail_init(&pch->submitted_list, &pl330->desc_pool);
		list_splice_tail_init(&pch->work_list, &pl330->desc_pool);
		list_splice_tail_init(&pch->completed_list, &pl330->desc_pool);
		spin_unlock_irqrestore(&pch->lock, flags);
		break;
	case DMA_SLAVE_CONFIG:
		slave_config = (struct dma_slave_config *)arg;

		if (slave_config->direction == DMA_MEM_TO_DEV) {
			if (slave_config->dst_addr)
				pch->fifo_addr = slave_config->dst_addr;
			if (slave_config->dst_addr_width)
				pch->burst_sz = __ffs(slave_config->dst_addr_width);
			if (slave_config->dst_maxburst)
				pch->burst_len = slave_config->dst_maxburst;
		} else if (slave_config->direction == DMA_DEV_TO_MEM) {
			if (slave_config->src_addr)
				pch->fifo_addr = slave_config->src_addr;
			if (slave_config->src_addr_width)
				pch->burst_sz = __ffs(slave_config->src_addr_width);
			if (slave_config->src_maxburst)
				pch->burst_len = slave_config->src_maxburst;
		}
		break;
	default:
		dev_err(pch->dmac->ddma.dev, "Not supported command.\n");
		return -ENXIO;
	}

	return 0;
}

static void pl330_free_chan_resources(struct dma_chan *chan)
{
	struct dma_pl330_chan *pch = to_pchan(chan);
	unsigned long flags;

	tasklet_kill(&pch->task);

	spin_lock_irqsave(&pch->lock, flags);

	pl330_release_channel(pch->thread);
	pch->thread = NULL;

	if (pch->cyclic)
		list_splice_tail_init(&pch->work_list, &pch->dmac->desc_pool);

	spin_unlock_irqrestore(&pch->lock, flags);
}

static enum dma_status
pl330_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
		 struct dma_tx_state *txstate)
{
	return dma_cookie_status(chan, cookie, txstate);
}

static void pl330_issue_pending(struct dma_chan *chan)
{
	struct dma_pl330_chan *pch = to_pchan(chan);
	unsigned long flags;

	spin_lock_irqsave(&pch->lock, flags);
	list_splice_tail_init(&pch->submitted_list, &pch->work_list);
	spin_unlock_irqrestore(&pch->lock, flags);

	pl330_tasklet((unsigned long)pch);
}

/*
 * We returned the last one of the circular list of descriptor(s)
 * from prep_xxx, so the argument to submit corresponds to the last
 * descriptor of the list.
 */
static dma_cookie_t pl330_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct dma_pl330_desc *desc, *last = to_desc(tx);
	struct dma_pl330_chan *pch = to_pchan(tx->chan);
	dma_cookie_t cookie;
	unsigned long flags;

	spin_lock_irqsave(&pch->lock, flags);

	/* Assign cookies to all nodes */
	while (!list_empty(&last->node)) {
		desc = list_entry(last->node.next, struct dma_pl330_desc, node);
		if (pch->cyclic) {
			desc->txd.callback = last->txd.callback;
			desc->txd.callback_param = last->txd.callback_param;
		}

		dma_cookie_assign(&desc->txd);

		list_move_tail(&desc->node, &pch->submitted_list);
	}

	cookie = dma_cookie_assign(&last->txd);
	list_add_tail(&last->node, &pch->submitted_list);
	spin_unlock_irqrestore(&pch->lock, flags);

	return cookie;
}

static inline void _init_desc(struct dma_pl330_desc *desc)
{
	desc->rqcfg.swap = SWAP_NO;
	desc->rqcfg.scctl = CCTRL0;
	desc->rqcfg.dcctl = CCTRL0;
	desc->txd.tx_submit = pl330_tx_submit;

	INIT_LIST_HEAD(&desc->node);
}

/* Returns the number of descriptors added to the DMAC pool */
static int add_desc(struct pl330_dmac *pl330, gfp_t flg, int count)
{
	struct dma_pl330_desc *desc;
	unsigned long flags;
	int i;

	desc = kcalloc(count, sizeof(*desc), flg);
	if (!desc)
		return 0;

	spin_lock_irqsave(&pl330->pool_lock, flags);

	for (i = 0; i < count; i++) {
		_init_desc(&desc[i]);
		list_add_tail(&desc[i].node, &pl330->desc_pool);
	}

	spin_unlock_irqrestore(&pl330->pool_lock, flags);

	return count;
}

static struct dma_pl330_desc *pluck_desc(struct pl330_dmac *pl330)
{
	struct dma_pl330_desc *desc = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pl330->pool_lock, flags);

	if (!list_empty(&pl330->desc_pool)) {
		desc = list_entry(pl330->desc_pool.next,
				struct dma_pl330_desc, node);

		list_del_init(&desc->node);

		desc->status = PREP;
		desc->txd.callback = NULL;
	}

	spin_unlock_irqrestore(&pl330->pool_lock, flags);

	return desc;
}

static struct dma_pl330_desc *pl330_get_desc(struct dma_pl330_chan *pch)
{
	struct pl330_dmac *pl330 = pch->dmac;
	u8 *peri_id = pch->chan.private;
	struct dma_pl330_desc *desc;

	/* Pluck one desc from the pool of DMAC */
	desc = pluck_desc(pl330);

	/* If the DMAC pool is empty, alloc new */
	if (!desc) {
		if (!add_desc(pl330, GFP_ATOMIC, 1))
			return NULL;

		/* Try again */
		desc = pluck_desc(pl330);
		if (!desc) {
			dev_err(pch->dmac->ddma.dev,
				"%s:%d ALERT!\n", __func__, __LINE__);
			return NULL;
		}
	}

	/* Initialize the descriptor */
	desc->pchan = pch;
	desc->txd.cookie = 0;
	async_tx_ack(&desc->txd);

	desc->peri = peri_id ? pch->chan.chan_id : 0;
	desc->rqcfg.pcfg = &pch->dmac->pcfg;

	dma_async_tx_descriptor_init(&desc->txd, &pch->chan);

	return desc;
}

static inline void fill_px(struct pl330_xfer *px,
		dma_addr_t dst, dma_addr_t src, size_t len)
{
	px->bytes = len;
	px->dst_addr = dst;
	px->src_addr = src;
}

static struct dma_pl330_desc *
__pl330_prep_dma_memcpy(struct dma_pl330_chan *pch, dma_addr_t dst,
		dma_addr_t src, size_t len)
{
	struct dma_pl330_desc *desc = pl330_get_desc(pch);

	if (!desc) {
		dev_err(pch->dmac->ddma.dev, "%s:%d Unable to fetch desc\n",
			__func__, __LINE__);
		return NULL;
	}

	/*
	 * Ideally we should lookout for reqs bigger than
	 * those that can be programmed with 256 bytes of
	 * MC buffer, but considering a req size is seldom
	 * going to be word-unaligned and more than 200MB,
	 * we take it easy.
	 * Also, should the limit is reached we'd rather
	 * have the platform increase MC buffer size than
	 * complicating this API driver.
	 */
	fill_px(&desc->px, dst, src, len);

	return desc;
}

/* Call after fixing burst size */
static inline int get_burst_len(struct dma_pl330_desc *desc, size_t len)
{
	struct dma_pl330_chan *pch = desc->pchan;
	struct pl330_dmac *pl330 = pch->dmac;
	int burst_len;

	burst_len = pl330->pcfg.data_bus_width / 8;
	burst_len *= pl330->pcfg.data_buf_dep / pl330->pcfg.num_chan;
	burst_len >>= desc->rqcfg.brst_size;

	/* src/dst_burst_len can't be more than 16 */
	if (burst_len > 16)
		burst_len = 16;

	while (burst_len > 1) {
		if (!(len % (burst_len << desc->rqcfg.brst_size)))
			break;
		burst_len--;
	}

	return burst_len;
}

static struct dma_async_tx_descriptor *pl330_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t dma_addr, size_t len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct dma_pl330_desc *desc = NULL, *first = NULL;
	struct dma_pl330_chan *pch = to_pchan(chan);
	struct pl330_dmac *pl330 = pch->dmac;
	unsigned int i;
	dma_addr_t dst;
	dma_addr_t src;

	if (len % period_len != 0)
		return NULL;

	if (!is_slave_direction(direction)) {
		dev_err(pch->dmac->ddma.dev, "%s:%d Invalid dma direction\n",
		__func__, __LINE__);
		return NULL;
	}

	for (i = 0; i < len / period_len; i++) {
		desc = pl330_get_desc(pch);
		if (!desc) {
			dev_err(pch->dmac->ddma.dev, "%s:%d Unable to fetch desc\n",
				__func__, __LINE__);

			if (!first)
				return NULL;

			spin_lock_irqsave(&pl330->pool_lock, flags);

			while (!list_empty(&first->node)) {
				desc = list_entry(first->node.next,
						struct dma_pl330_desc, node);
				list_move_tail(&desc->node, &pl330->desc_pool);
			}

			list_move_tail(&first->node, &pl330->desc_pool);

			spin_unlock_irqrestore(&pl330->pool_lock, flags);

			return NULL;
		}

		switch (direction) {
		case DMA_MEM_TO_DEV:
			desc->rqcfg.src_inc = 1;
			desc->rqcfg.dst_inc = 0;
			src = dma_addr;
			dst = pch->fifo_addr;
			break;
		case DMA_DEV_TO_MEM:
			desc->rqcfg.src_inc = 0;
			desc->rqcfg.dst_inc = 1;
			src = pch->fifo_addr;
			dst = dma_addr;
			break;
		default:
			break;
		}

		desc->rqtype = direction;
		desc->rqcfg.brst_size = pch->burst_sz;
		desc->rqcfg.brst_len = 1;
		fill_px(&desc->px, dst, src, period_len);

		if (!first)
			first = desc;
		else
			list_add_tail(&desc->node, &first->node);

		dma_addr += period_len;
	}

	if (!desc)
		return NULL;

	pch->cyclic = true;
	desc->txd.flags = flags;

	return &desc->txd;
}

static struct dma_async_tx_descriptor *
pl330_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dst,
		dma_addr_t src, size_t len, unsigned long flags)
{
	struct dma_pl330_desc *desc;
	struct dma_pl330_chan *pch = to_pchan(chan);
	struct pl330_dmac *pl330 = pch->dmac;
	int burst;

	if (unlikely(!pch || !len))
		return NULL;

	desc = __pl330_prep_dma_memcpy(pch, dst, src, len);
	if (!desc)
		return NULL;

	desc->rqcfg.src_inc = 1;
	desc->rqcfg.dst_inc = 1;
	desc->rqtype = DMA_MEM_TO_MEM;

	/* Select max possible burst size */
	burst = pl330->pcfg.data_bus_width / 8;

	/*
	 * Make sure we use a burst size that aligns with all the memcpy
	 * parameters because our DMA programming algorithm doesn't cope with
	 * transfers which straddle an entry in the DMA device's MFIFO.
	 */
	while ((src | dst | len) & (burst - 1))
		burst /= 2;

	desc->rqcfg.brst_size = 0;
	while (burst != (1 << desc->rqcfg.brst_size))
		desc->rqcfg.brst_size++;

	/*
	 * If burst size is smaller than bus width then make sure we only
	 * transfer one at a time to avoid a burst stradling an MFIFO entry.
	 */
	if (desc->rqcfg.brst_size * 8 < pl330->pcfg.data_bus_width)
		desc->rqcfg.brst_len = 1;

	desc->rqcfg.brst_len = get_burst_len(desc, len);

	desc->txd.flags = flags;

	return &desc->txd;
}

static void __pl330_giveback_desc(struct pl330_dmac *pl330,
				  struct dma_pl330_desc *first)
{
	unsigned long flags;
	struct dma_pl330_desc *desc;

	if (!first)
		return;

	spin_lock_irqsave(&pl330->pool_lock, flags);

	while (!list_empty(&first->node)) {
		desc = list_entry(first->node.next,
				struct dma_pl330_desc, node);
		list_move_tail(&desc->node, &pl330->desc_pool);
	}

	list_move_tail(&first->node, &pl330->desc_pool);

	spin_unlock_irqrestore(&pl330->pool_lock, flags);
}

static struct dma_async_tx_descriptor *
pl330_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flg, void *context)
{
	struct dma_pl330_desc *first, *desc = NULL;
	struct dma_pl330_chan *pch = to_pchan(chan);
	struct scatterlist *sg;
	int i;
	dma_addr_t addr;

	if (unlikely(!pch || !sgl || !sg_len))
		return NULL;

	addr = pch->fifo_addr;

	first = NULL;

	for_each_sg(sgl, sg, sg_len, i) {

		desc = pl330_get_desc(pch);
		if (!desc) {
			struct pl330_dmac *pl330 = pch->dmac;

			dev_err(pch->dmac->ddma.dev,
				"%s:%d Unable to fetch desc\n",
				__func__, __LINE__);
			__pl330_giveback_desc(pl330, first);

			return NULL;
		}

		if (!first)
			first = desc;
		else
			list_add_tail(&desc->node, &first->node);

		if (direction == DMA_MEM_TO_DEV) {
			desc->rqcfg.src_inc = 1;
			desc->rqcfg.dst_inc = 0;
			fill_px(&desc->px,
				addr, sg_dma_address(sg), sg_dma_len(sg));
		} else {
			desc->rqcfg.src_inc = 0;
			desc->rqcfg.dst_inc = 1;
			fill_px(&desc->px,
				sg_dma_address(sg), addr, sg_dma_len(sg));
		}

		desc->rqcfg.brst_size = pch->burst_sz;
		desc->rqcfg.brst_len = 1;
		desc->rqtype = direction;
	}

	/* Return the last desc in the chain */
	desc->txd.flags = flg;
	return &desc->txd;
}

static irqreturn_t pl330_irq_handler(int irq, void *data)
{
	if (pl330_update(data))
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

#define PL330_DMA_BUSWIDTHS \
	BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED) | \
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
	BIT(DMA_SLAVE_BUSWIDTH_8_BYTES)

static int pl330_dma_device_slave_caps(struct dma_chan *dchan,
	struct dma_slave_caps *caps)
{
	caps->src_addr_widths = PL330_DMA_BUSWIDTHS;
	caps->dstn_addr_widths = PL330_DMA_BUSWIDTHS;
	caps->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	caps->cmd_pause = false;
	caps->cmd_terminate = true;
	caps->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	return 0;
}

static int
pl330_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct dma_pl330_platdata *pdat;
	struct pl330_config *pcfg;
	struct pl330_dmac *pl330;
	struct dma_pl330_chan *pch, *_p;
	struct dma_device *pd;
	struct resource *res;
	int i, ret, irq;
	int num_chan;

	pdat = dev_get_platdata(&adev->dev);

	ret = dma_set_mask_and_coherent(&adev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	/* Allocate a new DMAC and its Channels */
	pl330 = devm_kzalloc(&adev->dev, sizeof(*pl330), GFP_KERNEL);
	if (!pl330) {
		dev_err(&adev->dev, "unable to allocate mem\n");
		return -ENOMEM;
	}

	pl330->mcbufsz = pdat ? pdat->mcbuf_sz : 0;

	res = &adev->res;
	pl330->base = devm_ioremap_resource(&adev->dev, res);
	if (IS_ERR(pl330->base))
		return PTR_ERR(pl330->base);

	amba_set_drvdata(adev, pl330);

	for (i = 0; i < AMBA_NR_IRQS; i++) {
		irq = adev->irq[i];
		if (irq) {
			ret = devm_request_irq(&adev->dev, irq,
					       pl330_irq_handler, 0,
					       dev_name(&adev->dev), pl330);
			if (ret)
				return ret;
		} else {
			break;
		}
	}

	pcfg = &pl330->pcfg;

	pcfg->periph_id = adev->periphid;
	ret = pl330_add(pl330);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&pl330->desc_pool);
	spin_lock_init(&pl330->pool_lock);

	/* Create a descriptor pool of default size */
	if (!add_desc(pl330, GFP_KERNEL, NR_DEFAULT_DESC))
		dev_warn(&adev->dev, "unable to allocate desc\n");

	pd = &pl330->ddma;
	INIT_LIST_HEAD(&pd->channels);

	/* Initialize channel parameters */
	if (pdat)
		num_chan = max_t(int, pdat->nr_valid_peri, pcfg->num_chan);
	else
		num_chan = max_t(int, pcfg->num_peri, pcfg->num_chan);

	pl330->num_peripherals = num_chan;

	pl330->peripherals = kzalloc(num_chan * sizeof(*pch), GFP_KERNEL);
	if (!pl330->peripherals) {
		ret = -ENOMEM;
		dev_err(&adev->dev, "unable to allocate pl330->peripherals\n");
		goto probe_err2;
	}

	for (i = 0; i < num_chan; i++) {
		pch = &pl330->peripherals[i];
		if (!adev->dev.of_node)
			pch->chan.private = pdat ? &pdat->peri_id[i] : NULL;
		else
			pch->chan.private = adev->dev.of_node;

		INIT_LIST_HEAD(&pch->submitted_list);
		INIT_LIST_HEAD(&pch->work_list);
		INIT_LIST_HEAD(&pch->completed_list);
		spin_lock_init(&pch->lock);
		pch->thread = NULL;
		pch->chan.device = pd;
		pch->dmac = pl330;

		/* Add the channel to the DMAC list */
		list_add_tail(&pch->chan.device_node, &pd->channels);
	}

	pd->dev = &adev->dev;
	if (pdat) {
		pd->cap_mask = pdat->cap_mask;
	} else {
		dma_cap_set(DMA_MEMCPY, pd->cap_mask);
		if (pcfg->num_peri) {
			dma_cap_set(DMA_SLAVE, pd->cap_mask);
			dma_cap_set(DMA_CYCLIC, pd->cap_mask);
			dma_cap_set(DMA_PRIVATE, pd->cap_mask);
		}
	}

	pd->device_alloc_chan_resources = pl330_alloc_chan_resources;
	pd->device_free_chan_resources = pl330_free_chan_resources;
	pd->device_prep_dma_memcpy = pl330_prep_dma_memcpy;
	pd->device_prep_dma_cyclic = pl330_prep_dma_cyclic;
	pd->device_tx_status = pl330_tx_status;
	pd->device_prep_slave_sg = pl330_prep_slave_sg;
	pd->device_control = pl330_control;
	pd->device_issue_pending = pl330_issue_pending;
	pd->device_slave_caps = pl330_dma_device_slave_caps;

	ret = dma_async_device_register(pd);
	if (ret) {
		dev_err(&adev->dev, "unable to register DMAC\n");
		goto probe_err3;
	}

	if (adev->dev.of_node) {
		ret = of_dma_controller_register(adev->dev.of_node,
					 of_dma_pl330_xlate, pl330);
		if (ret) {
			dev_err(&adev->dev,
			"unable to register DMA to the generic DT DMA helpers\n");
		}
	}

	adev->dev.dma_parms = &pl330->dma_parms;

	/*
	 * This is the limit for transfers with a buswidth of 1, larger
	 * buswidths will have larger limits.
	 */
	ret = dma_set_max_seg_size(&adev->dev, 1900800);
	if (ret)
		dev_err(&adev->dev, "unable to set the seg size\n");


	dev_info(&adev->dev,
		"Loaded driver for PL330 DMAC-%x\n", adev->periphid);
	dev_info(&adev->dev,
		"\tDBUFF-%ux%ubytes Num_Chans-%u Num_Peri-%u Num_Events-%u\n",
		pcfg->data_buf_dep, pcfg->data_bus_width / 8, pcfg->num_chan,
		pcfg->num_peri, pcfg->num_events);

	return 0;
probe_err3:
	/* Idle the DMAC */
	list_for_each_entry_safe(pch, _p, &pl330->ddma.channels,
			chan.device_node) {

		/* Remove the channel */
		list_del(&pch->chan.device_node);

		/* Flush the channel */
		if (pch->thread) {
			pl330_control(&pch->chan, DMA_TERMINATE_ALL, 0);
			pl330_free_chan_resources(&pch->chan);
		}
	}
probe_err2:
	pl330_del(pl330);

	return ret;
}

static int pl330_remove(struct amba_device *adev)
{
	struct pl330_dmac *pl330 = amba_get_drvdata(adev);
	struct dma_pl330_chan *pch, *_p;

	if (adev->dev.of_node)
		of_dma_controller_free(adev->dev.of_node);

	dma_async_device_unregister(&pl330->ddma);

	/* Idle the DMAC */
	list_for_each_entry_safe(pch, _p, &pl330->ddma.channels,
			chan.device_node) {

		/* Remove the channel */
		list_del(&pch->chan.device_node);

		/* Flush the channel */
		if (pch->thread) {
			pl330_control(&pch->chan, DMA_TERMINATE_ALL, 0);
			pl330_free_chan_resources(&pch->chan);
		}
	}

	pl330_del(pl330);

	return 0;
}

static struct amba_id pl330_ids[] = {
	{
		.id	= 0x00041330,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, pl330_ids);

static struct amba_driver pl330_driver = {
	.drv = {
		.owner = THIS_MODULE,
		.name = "dma-pl330",
	},
	.id_table = pl330_ids,
	.probe = pl330_probe,
	.remove = pl330_remove,
};

module_amba_driver(pl330_driver);

MODULE_AUTHOR("Jaswinder Singh <jassi.brar@samsung.com>");
MODULE_DESCRIPTION("API Driver for PL330 DMAC");
MODULE_LICENSE("GPL");
