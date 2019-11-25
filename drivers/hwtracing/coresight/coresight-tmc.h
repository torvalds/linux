/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#ifndef _CORESIGHT_TMC_H
#define _CORESIGHT_TMC_H

#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/refcount.h>

#define TMC_RSZ			0x004
#define TMC_STS			0x00c
#define TMC_RRD			0x010
#define TMC_RRP			0x014
#define TMC_RWP			0x018
#define TMC_TRG			0x01c
#define TMC_CTL			0x020
#define TMC_RWD			0x024
#define TMC_MODE		0x028
#define TMC_LBUFLEVEL		0x02c
#define TMC_CBUFLEVEL		0x030
#define TMC_BUFWM		0x034
#define TMC_RRPHI		0x038
#define TMC_RWPHI		0x03c
#define TMC_AXICTL		0x110
#define TMC_DBALO		0x118
#define TMC_DBAHI		0x11c
#define TMC_FFSR		0x300
#define TMC_FFCR		0x304
#define TMC_PSCR		0x308
#define TMC_ITMISCOP0		0xee0
#define TMC_ITTRFLIN		0xee8
#define TMC_ITATBDATA0		0xeec
#define TMC_ITATBCTR2		0xef0
#define TMC_ITATBCTR1		0xef4
#define TMC_ITATBCTR0		0xef8
#define TMC_AUTHSTATUS		0xfb8

/* register description */
/* TMC_CTL - 0x020 */
#define TMC_CTL_CAPT_EN		BIT(0)
/* TMC_STS - 0x00C */
#define TMC_STS_TMCREADY_BIT	2
#define TMC_STS_FULL		BIT(0)
#define TMC_STS_TRIGGERED	BIT(1)
#define TMC_STS_MEMERR		BIT(5)
/*
 * TMC_AXICTL - 0x110
 *
 * TMC AXICTL format for SoC-400
 *	Bits [0-1]	: ProtCtrlBit0-1
 *	Bits [2-5]	: CacheCtrlBits 0-3 (AXCACHE)
 *	Bit  6		: Reserved
 *	Bit  7		: ScatterGatherMode
 *	Bits [8-11]	: WrBurstLen
 *	Bits [12-31]	: Reserved.
 * TMC AXICTL format for SoC-600, as above except:
 *	Bits [2-5]	: AXI WCACHE
 *	Bits [16-19]	: AXI RCACHE
 *	Bits [20-31]	: Reserved
 */
#define TMC_AXICTL_CLEAR_MASK 0xfbf
#define TMC_AXICTL_ARCACHE_MASK (0xf << 16)

#define TMC_AXICTL_PROT_CTL_B0	BIT(0)
#define TMC_AXICTL_PROT_CTL_B1	BIT(1)
#define TMC_AXICTL_SCT_GAT_MODE	BIT(7)
#define TMC_AXICTL_WR_BURST_16	0xF00
/* Write-back Read and Write-allocate */
#define TMC_AXICTL_AXCACHE_OS	(0xf << 2)
#define TMC_AXICTL_ARCACHE_OS	(0xf << 16)

/* TMC_FFCR - 0x304 */
#define TMC_FFCR_FLUSHMAN_BIT	6
#define TMC_FFCR_EN_FMT		BIT(0)
#define TMC_FFCR_EN_TI		BIT(1)
#define TMC_FFCR_FON_FLIN	BIT(4)
#define TMC_FFCR_FON_TRIG_EVT	BIT(5)
#define TMC_FFCR_TRIGON_TRIGIN	BIT(8)
#define TMC_FFCR_STOP_ON_FLUSH	BIT(12)


#define TMC_DEVID_NOSCAT	BIT(24)

#define TMC_DEVID_AXIAW_VALID	BIT(16)
#define TMC_DEVID_AXIAW_SHIFT	17
#define TMC_DEVID_AXIAW_MASK	0x7f

#define TMC_AUTH_NSID_MASK	GENMASK(1, 0)

enum tmc_config_type {
	TMC_CONFIG_TYPE_ETB,
	TMC_CONFIG_TYPE_ETR,
	TMC_CONFIG_TYPE_ETF,
};

enum tmc_mode {
	TMC_MODE_CIRCULAR_BUFFER,
	TMC_MODE_SOFTWARE_FIFO,
	TMC_MODE_HARDWARE_FIFO,
};

enum tmc_mem_intf_width {
	TMC_MEM_INTF_WIDTH_32BITS	= 1,
	TMC_MEM_INTF_WIDTH_64BITS	= 2,
	TMC_MEM_INTF_WIDTH_128BITS	= 4,
	TMC_MEM_INTF_WIDTH_256BITS	= 8,
};

/* TMC ETR Capability bit definitions */
#define TMC_ETR_SG			(0x1U << 0)
/* ETR has separate read/write cache encodings */
#define TMC_ETR_AXI_ARCACHE		(0x1U << 1)
/*
 * TMC_ETR_SAVE_RESTORE - Values of RRP/RWP/STS.Full are
 * retained when TMC leaves Disabled state, allowing us to continue
 * the tracing from a point where we stopped. This also implies that
 * the RRP/RWP/STS.Full should always be programmed to the correct
 * value. Unfortunately this is not advertised by the hardware,
 * so we have to rely on PID of the IP to detect the functionality.
 */
#define TMC_ETR_SAVE_RESTORE		(0x1U << 2)

/* Coresight SoC-600 TMC-ETR unadvertised capabilities */
#define CORESIGHT_SOC_600_ETR_CAPS	\
	(TMC_ETR_SAVE_RESTORE | TMC_ETR_AXI_ARCACHE)

enum etr_mode {
	ETR_MODE_FLAT,		/* Uses contiguous flat buffer */
	ETR_MODE_ETR_SG,	/* Uses in-built TMC ETR SG mechanism */
	ETR_MODE_CATU,		/* Use SG mechanism in CATU */
};

struct etr_buf_operations;

/**
 * struct etr_buf - Details of the buffer used by ETR
 * refcount	; Number of sources currently using this etr_buf.
 * @mode	: Mode of the ETR buffer, contiguous, Scatter Gather etc.
 * @full	: Trace data overflow
 * @size	: Size of the buffer.
 * @hwaddr	: Address to be programmed in the TMC:DBA{LO,HI}
 * @offset	: Offset of the trace data in the buffer for consumption.
 * @len		: Available trace data @buf (may round up to the beginning).
 * @ops		: ETR buffer operations for the mode.
 * @private	: Backend specific information for the buf
 */
struct etr_buf {
	refcount_t			refcount;
	enum etr_mode			mode;
	bool				full;
	ssize_t				size;
	dma_addr_t			hwaddr;
	unsigned long			offset;
	s64				len;
	const struct etr_buf_operations	*ops;
	void				*private;
};

/**
 * struct tmc_drvdata - specifics associated to an TMC component
 * @base:	memory mapped base address for this component.
 * @csdev:	component vitals needed by the framework.
 * @miscdev:	specifics to handle "/dev/xyz.tmc" entry.
 * @spinlock:	only one at a time pls.
 * @pid:	Process ID of the process being monitored by the session
 *		that is using this component.
 * @buf:	Snapshot of the trace data for ETF/ETB.
 * @etr_buf:	details of buffer used in TMC-ETR
 * @len:	size of the available trace for ETF/ETB.
 * @size:	trace buffer size for this TMC (common for all modes).
 * @mode:	how this TMC is being used.
 * @config_type: TMC variant, must be of type @tmc_config_type.
 * @memwidth:	width of the memory interface databus, in bytes.
 * @trigger_cntr: amount of words to store after a trigger.
 * @etr_caps:	Bitmask of capabilities of the TMC ETR, inferred from the
 *		device configuration register (DEVID)
 * @idr:	Holds etr_bufs allocated for this ETR.
 * @idr_mutex:	Access serialisation for idr.
 * @sysfs_buf:	SYSFS buffer for ETR.
 * @perf_buf:	PERF buffer for ETR.
 */
struct tmc_drvdata {
	void __iomem		*base;
	struct coresight_device	*csdev;
	struct miscdevice	miscdev;
	spinlock_t		spinlock;
	pid_t			pid;
	bool			reading;
	union {
		char		*buf;		/* TMC ETB */
		struct etr_buf	*etr_buf;	/* TMC ETR */
	};
	u32			len;
	u32			size;
	u32			mode;
	enum tmc_config_type	config_type;
	enum tmc_mem_intf_width	memwidth;
	u32			trigger_cntr;
	u32			etr_caps;
	struct idr		idr;
	struct mutex		idr_mutex;
	struct etr_buf		*sysfs_buf;
	struct etr_buf		*perf_buf;
};

struct etr_buf_operations {
	int (*alloc)(struct tmc_drvdata *drvdata, struct etr_buf *etr_buf,
		     int node, void **pages);
	void (*sync)(struct etr_buf *etr_buf, u64 rrp, u64 rwp);
	ssize_t (*get_data)(struct etr_buf *etr_buf, u64 offset, size_t len,
			    char **bufpp);
	void (*free)(struct etr_buf *etr_buf);
};

/**
 * struct tmc_pages - Collection of pages used for SG.
 * @nr_pages:		Number of pages in the list.
 * @daddrs:		Array of DMA'able page address.
 * @pages:		Array pages for the buffer.
 */
struct tmc_pages {
	int nr_pages;
	dma_addr_t	*daddrs;
	struct page	**pages;
};

/*
 * struct tmc_sg_table - Generic SG table for TMC
 * @dev:		Device for DMA allocations
 * @table_vaddr:	Contiguous Virtual address for PageTable
 * @data_vaddr:		Contiguous Virtual address for Data Buffer
 * @table_daddr:	DMA address of the PageTable base
 * @node:		Node for Page allocations
 * @table_pages:	List of pages & dma address for Table
 * @data_pages:		List of pages & dma address for Data
 */
struct tmc_sg_table {
	struct device *dev;
	void *table_vaddr;
	void *data_vaddr;
	dma_addr_t table_daddr;
	int node;
	struct tmc_pages table_pages;
	struct tmc_pages data_pages;
};

/* Generic functions */
void tmc_wait_for_tmcready(struct tmc_drvdata *drvdata);
void tmc_flush_and_stop(struct tmc_drvdata *drvdata);
void tmc_enable_hw(struct tmc_drvdata *drvdata);
void tmc_disable_hw(struct tmc_drvdata *drvdata);
u32 tmc_get_memwidth_mask(struct tmc_drvdata *drvdata);

/* ETB/ETF functions */
int tmc_read_prepare_etb(struct tmc_drvdata *drvdata);
int tmc_read_unprepare_etb(struct tmc_drvdata *drvdata);
extern const struct coresight_ops tmc_etb_cs_ops;
extern const struct coresight_ops tmc_etf_cs_ops;

ssize_t tmc_etb_get_sysfs_trace(struct tmc_drvdata *drvdata,
				loff_t pos, size_t len, char **bufpp);
/* ETR functions */
int tmc_read_prepare_etr(struct tmc_drvdata *drvdata);
int tmc_read_unprepare_etr(struct tmc_drvdata *drvdata);
extern const struct coresight_ops tmc_etr_cs_ops;
ssize_t tmc_etr_get_sysfs_trace(struct tmc_drvdata *drvdata,
				loff_t pos, size_t len, char **bufpp);


#define TMC_REG_PAIR(name, lo_off, hi_off)				\
static inline u64							\
tmc_read_##name(struct tmc_drvdata *drvdata)				\
{									\
	return coresight_read_reg_pair(drvdata->base, lo_off, hi_off);	\
}									\
static inline void							\
tmc_write_##name(struct tmc_drvdata *drvdata, u64 val)			\
{									\
	coresight_write_reg_pair(drvdata->base, val, lo_off, hi_off);	\
}

TMC_REG_PAIR(rrp, TMC_RRP, TMC_RRPHI)
TMC_REG_PAIR(rwp, TMC_RWP, TMC_RWPHI)
TMC_REG_PAIR(dba, TMC_DBALO, TMC_DBAHI)

/* Initialise the caps from unadvertised static capabilities of the device */
static inline void tmc_etr_init_caps(struct tmc_drvdata *drvdata, u32 dev_caps)
{
	WARN_ON(drvdata->etr_caps);
	drvdata->etr_caps = dev_caps;
}

static inline void tmc_etr_set_cap(struct tmc_drvdata *drvdata, u32 cap)
{
	drvdata->etr_caps |= cap;
}

static inline bool tmc_etr_has_cap(struct tmc_drvdata *drvdata, u32 cap)
{
	return !!(drvdata->etr_caps & cap);
}

struct tmc_sg_table *tmc_alloc_sg_table(struct device *dev,
					int node,
					int nr_tpages,
					int nr_dpages,
					void **pages);
void tmc_free_sg_table(struct tmc_sg_table *sg_table);
void tmc_sg_table_sync_table(struct tmc_sg_table *sg_table);
void tmc_sg_table_sync_data_range(struct tmc_sg_table *table,
				  u64 offset, u64 size);
ssize_t tmc_sg_table_get_data(struct tmc_sg_table *sg_table,
			      u64 offset, size_t len, char **bufpp);
static inline unsigned long
tmc_sg_table_buf_size(struct tmc_sg_table *sg_table)
{
	return sg_table->data_pages.nr_pages << PAGE_SHIFT;
}

struct coresight_device *tmc_etr_get_catu_device(struct tmc_drvdata *drvdata);

#endif
