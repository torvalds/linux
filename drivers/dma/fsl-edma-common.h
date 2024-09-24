/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2013-2014 Freescale Semiconductor, Inc.
 * Copyright 2018 Angelo Dureghello <angelo@sysam.it>
 */
#ifndef _FSL_EDMA_COMMON_H_
#define _FSL_EDMA_COMMON_H_

#include <linux/dma-direction.h>
#include <linux/platform_device.h>
#include "virt-dma.h"

#define EDMA_CR_EDBG		BIT(1)
#define EDMA_CR_ERCA		BIT(2)
#define EDMA_CR_ERGA		BIT(3)
#define EDMA_CR_HOE		BIT(4)
#define EDMA_CR_HALT		BIT(5)
#define EDMA_CR_CLM		BIT(6)
#define EDMA_CR_EMLM		BIT(7)
#define EDMA_CR_ECX		BIT(16)
#define EDMA_CR_CX		BIT(17)

#define EDMA_SEEI_SEEI(x)	((x) & GENMASK(4, 0))
#define EDMA_CEEI_CEEI(x)	((x) & GENMASK(4, 0))
#define EDMA_CINT_CINT(x)	((x) & GENMASK(4, 0))
#define EDMA_CERR_CERR(x)	((x) & GENMASK(4, 0))

#define EDMA_TCD_ATTR_DSIZE(x)		(((x) & GENMASK(2, 0)))
#define EDMA_TCD_ATTR_DMOD(x)		(((x) & GENMASK(4, 0)) << 3)
#define EDMA_TCD_ATTR_SSIZE(x)		(((x) & GENMASK(2, 0)) << 8)
#define EDMA_TCD_ATTR_SMOD(x)		(((x) & GENMASK(4, 0)) << 11)

#define EDMA_TCD_ITER_MASK		GENMASK(14, 0)
#define EDMA_TCD_CITER_CITER(x)		((x) & EDMA_TCD_ITER_MASK)
#define EDMA_TCD_BITER_BITER(x)		((x) & EDMA_TCD_ITER_MASK)

#define EDMA_TCD_CSR_START		BIT(0)
#define EDMA_TCD_CSR_INT_MAJOR		BIT(1)
#define EDMA_TCD_CSR_INT_HALF		BIT(2)
#define EDMA_TCD_CSR_D_REQ		BIT(3)
#define EDMA_TCD_CSR_E_SG		BIT(4)
#define EDMA_TCD_CSR_E_LINK		BIT(5)
#define EDMA_TCD_CSR_ACTIVE		BIT(6)
#define EDMA_TCD_CSR_DONE		BIT(7)

#define EDMA_V3_TCD_NBYTES_MLOFF_NBYTES(x) ((x) & GENMASK(9, 0))
#define EDMA_V3_TCD_NBYTES_MLOFF(x)        (x << 10)
#define EDMA_V3_TCD_NBYTES_DMLOE           (1 << 30)
#define EDMA_V3_TCD_NBYTES_SMLOE           (1 << 31)

#define EDMAMUX_CHCFG_DIS		0x0
#define EDMAMUX_CHCFG_ENBL		0x80
#define EDMAMUX_CHCFG_SOURCE(n)		((n) & 0x3F)

#define DMAMUX_NR	2

#define EDMA_TCD                0x1000

#define FSL_EDMA_BUSWIDTHS	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
				 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))

#define EDMA_V3_CH_SBR_RD          BIT(22)
#define EDMA_V3_CH_SBR_WR          BIT(21)
#define EDMA_V3_CH_CSR_ERQ         BIT(0)
#define EDMA_V3_CH_CSR_EARQ        BIT(1)
#define EDMA_V3_CH_CSR_EEI         BIT(2)
#define EDMA_V3_CH_CSR_DONE        BIT(30)
#define EDMA_V3_CH_CSR_ACTIVE      BIT(31)

enum fsl_edma_pm_state {
	RUNNING = 0,
	SUSPENDED,
};

struct fsl_edma_hw_tcd {
	__le32	saddr;
	__le16	soff;
	__le16	attr;
	__le32	nbytes;
	__le32	slast;
	__le32	daddr;
	__le16	doff;
	__le16	citer;
	__le32	dlast_sga;
	__le16	csr;
	__le16	biter;
};

struct fsl_edma_hw_tcd64 {
	__le64  saddr;
	__le16  soff;
	__le16  attr;
	__le32  nbytes;
	__le64  slast;
	__le64  daddr;
	__le64  dlast_sga;
	__le16  doff;
	__le16  citer;
	__le16  csr;
	__le16  biter;
} __packed;

struct fsl_edma3_ch_reg {
	__le32	ch_csr;
	__le32	ch_es;
	__le32	ch_int;
	__le32	ch_sbr;
	__le32	ch_pri;
	__le32	ch_mux;
	__le32  ch_mattr; /* edma4, reserved for edma3 */
	__le32  ch_reserved;
	union {
		struct fsl_edma_hw_tcd tcd;
		struct fsl_edma_hw_tcd64 tcd64;
	};
} __packed;

/*
 * These are iomem pointers, for both v32 and v64.
 */
struct edma_regs {
	void __iomem *cr;
	void __iomem *es;
	void __iomem *erqh;
	void __iomem *erql;	/* aka erq on v32 */
	void __iomem *eeih;
	void __iomem *eeil;	/* aka eei on v32 */
	void __iomem *seei;
	void __iomem *ceei;
	void __iomem *serq;
	void __iomem *cerq;
	void __iomem *cint;
	void __iomem *cerr;
	void __iomem *ssrt;
	void __iomem *cdne;
	void __iomem *inth;
	void __iomem *intl;
	void __iomem *errh;
	void __iomem *errl;
};

struct fsl_edma_sw_tcd {
	dma_addr_t			ptcd;
	void				*vtcd;
};

struct fsl_edma_chan {
	struct virt_dma_chan		vchan;
	enum dma_status			status;
	enum fsl_edma_pm_state		pm_state;
	struct fsl_edma_engine		*edma;
	struct fsl_edma_desc		*edesc;
	struct dma_slave_config		cfg;
	u32				attr;
	bool                            is_sw;
	struct dma_pool			*tcd_pool;
	dma_addr_t			dma_dev_addr;
	u32				dma_dev_size;
	enum dma_data_direction		dma_dir;
	char				chan_name[32];
	void __iomem			*tcd;
	void __iomem			*mux_addr;
	u32				real_count;
	struct work_struct		issue_worker;
	struct platform_device		*pdev;
	struct device			*pd_dev;
	u32				srcid;
	struct clk			*clk;
	int                             priority;
	int				hw_chanid;
	int				txirq;
	irqreturn_t			(*irq_handler)(int irq, void *dev_id);
	bool				is_rxchan;
	bool				is_remote;
	bool				is_multi_fifo;
};

struct fsl_edma_desc {
	struct virt_dma_desc		vdesc;
	struct fsl_edma_chan		*echan;
	bool				iscyclic;
	enum dma_transfer_direction	dirn;
	unsigned int			n_tcds;
	struct fsl_edma_sw_tcd		tcd[];
};

#define FSL_EDMA_DRV_HAS_DMACLK		BIT(0)
#define FSL_EDMA_DRV_MUX_SWAP		BIT(1)
#define FSL_EDMA_DRV_CONFIG32		BIT(2)
#define FSL_EDMA_DRV_WRAP_IO		BIT(3)
#define FSL_EDMA_DRV_EDMA64		BIT(4)
#define FSL_EDMA_DRV_HAS_PD		BIT(5)
#define FSL_EDMA_DRV_HAS_CHCLK		BIT(6)
#define FSL_EDMA_DRV_HAS_CHMUX		BIT(7)
#define FSL_EDMA_DRV_MEM_REMOTE		BIT(8)
/* control and status register is in tcd address space, edma3 reg layout */
#define FSL_EDMA_DRV_SPLIT_REG		BIT(9)
#define FSL_EDMA_DRV_BUS_8BYTE		BIT(10)
#define FSL_EDMA_DRV_DEV_TO_DEV		BIT(11)
#define FSL_EDMA_DRV_ALIGN_64BYTE	BIT(12)
/* Need clean CHn_CSR DONE before enable TCD's ESG */
#define FSL_EDMA_DRV_CLEAR_DONE_E_SG	BIT(13)
/* Need clean CHn_CSR DONE before enable TCD's MAJORELINK */
#define FSL_EDMA_DRV_CLEAR_DONE_E_LINK	BIT(14)
#define FSL_EDMA_DRV_TCD64		BIT(15)

#define FSL_EDMA_DRV_EDMA3	(FSL_EDMA_DRV_SPLIT_REG |	\
				 FSL_EDMA_DRV_BUS_8BYTE |	\
				 FSL_EDMA_DRV_DEV_TO_DEV |	\
				 FSL_EDMA_DRV_ALIGN_64BYTE |	\
				 FSL_EDMA_DRV_CLEAR_DONE_E_SG |	\
				 FSL_EDMA_DRV_CLEAR_DONE_E_LINK)

#define FSL_EDMA_DRV_EDMA4	(FSL_EDMA_DRV_SPLIT_REG |	\
				 FSL_EDMA_DRV_BUS_8BYTE |	\
				 FSL_EDMA_DRV_DEV_TO_DEV |	\
				 FSL_EDMA_DRV_ALIGN_64BYTE |	\
				 FSL_EDMA_DRV_CLEAR_DONE_E_LINK)

struct fsl_edma_drvdata {
	u32			dmamuxs; /* only used before v3 */
	u32			chreg_off;
	u32			chreg_space_sz;
	u32			flags;
	u32			mux_off;	/* channel mux register offset */
	u32			mux_skip;	/* how much skip for each channel */
	int			(*setup_irq)(struct platform_device *pdev,
					     struct fsl_edma_engine *fsl_edma);
};

struct fsl_edma_engine {
	struct dma_device	dma_dev;
	void __iomem		*membase;
	void __iomem		*muxbase[DMAMUX_NR];
	struct clk		*muxclk[DMAMUX_NR];
	struct clk		*dmaclk;
	struct mutex		fsl_edma_mutex;
	const struct fsl_edma_drvdata *drvdata;
	u32			n_chans;
	int			txirq;
	int			errirq;
	bool			big_endian;
	struct edma_regs	regs;
	u64			chan_masked;
	struct fsl_edma_chan	chans[] __counted_by(n_chans);
};

static inline u32 fsl_edma_drvflags(struct fsl_edma_chan *fsl_chan)
{
	return fsl_chan->edma->drvdata->flags;
}

#define edma_read_tcdreg_c(chan, _tcd,  __name)				\
_Generic(((_tcd)->__name),						\
	__iomem __le64 : edma_readq(chan->edma, &(_tcd)->__name),		\
	__iomem __le32 : edma_readl(chan->edma, &(_tcd)->__name),		\
	__iomem __le16 : edma_readw(chan->edma, &(_tcd)->__name)		\
	)

#define edma_read_tcdreg(chan, __name)								\
((fsl_edma_drvflags(chan) & FSL_EDMA_DRV_TCD64) ?						\
	edma_read_tcdreg_c(chan, ((struct fsl_edma_hw_tcd64 __iomem *)chan->tcd), __name) :	\
	edma_read_tcdreg_c(chan, ((struct fsl_edma_hw_tcd __iomem *)chan->tcd), __name)		\
)

#define edma_write_tcdreg_c(chan, _tcd, _val, __name)					\
_Generic((_tcd->__name),								\
	__iomem __le64 : edma_writeq(chan->edma, (u64 __force)(_val), &_tcd->__name),	\
	__iomem __le32 : edma_writel(chan->edma, (u32 __force)(_val), &_tcd->__name),	\
	__iomem __le16 : edma_writew(chan->edma, (u16 __force)(_val), &_tcd->__name),	\
	__iomem u8 : edma_writeb(chan->edma, _val, &_tcd->__name)			\
	)

#define edma_write_tcdreg(chan, val, __name)							   \
do {												   \
	struct fsl_edma_hw_tcd64 __iomem *tcd64_r = (struct fsl_edma_hw_tcd64 __iomem *)chan->tcd; \
	struct fsl_edma_hw_tcd __iomem *tcd_r = (struct fsl_edma_hw_tcd __iomem *)chan->tcd;	   \
												   \
	if (fsl_edma_drvflags(chan) & FSL_EDMA_DRV_TCD64)					   \
		edma_write_tcdreg_c(chan, tcd64_r, val, __name);				   \
	else											   \
		edma_write_tcdreg_c(chan, tcd_r, val, __name);					   \
} while (0)

#define edma_cp_tcd_to_reg(chan, __tcd, __name)							   \
do {	\
	struct fsl_edma_hw_tcd64 __iomem *tcd64_r = (struct fsl_edma_hw_tcd64 __iomem *)chan->tcd; \
	struct fsl_edma_hw_tcd __iomem *tcd_r = (struct fsl_edma_hw_tcd __iomem *)chan->tcd;	   \
	struct fsl_edma_hw_tcd64 *tcd64_m = (struct fsl_edma_hw_tcd64 *)__tcd;			   \
	struct fsl_edma_hw_tcd *tcd_m = (struct fsl_edma_hw_tcd *)__tcd;			   \
												   \
	if (fsl_edma_drvflags(chan) & FSL_EDMA_DRV_TCD64)					   \
		edma_write_tcdreg_c(chan, tcd64_r,  tcd64_m->__name, __name);			   \
	else											   \
		edma_write_tcdreg_c(chan, tcd_r, tcd_m->__name, __name);			   \
} while (0)

#define edma_readl_chreg(chan, __name)				\
	edma_readl(chan->edma,					\
		   (void __iomem *)&(container_of(((__force void *)chan->tcd),\
						  struct fsl_edma3_ch_reg, tcd)->__name))

#define edma_writel_chreg(chan, val,  __name)			\
	edma_writel(chan->edma, val,				\
		   (void __iomem *)&(container_of(((__force void *)chan->tcd),\
						  struct fsl_edma3_ch_reg, tcd)->__name))

#define fsl_edma_get_tcd(_chan, _tcd, _field)			\
(fsl_edma_drvflags(_chan) & FSL_EDMA_DRV_TCD64 ? (((struct fsl_edma_hw_tcd64 *)_tcd)->_field) : \
						 (((struct fsl_edma_hw_tcd *)_tcd)->_field))

#define fsl_edma_le_to_cpu(x)						\
_Generic((x),								\
	__le64 : le64_to_cpu((x)),					\
	__le32 : le32_to_cpu((x)),					\
	__le16 : le16_to_cpu((x))					\
)

#define fsl_edma_get_tcd_to_cpu(_chan, _tcd, _field)				\
(fsl_edma_drvflags(_chan) & FSL_EDMA_DRV_TCD64 ?				\
	fsl_edma_le_to_cpu(((struct fsl_edma_hw_tcd64 *)_tcd)->_field) :	\
	fsl_edma_le_to_cpu(((struct fsl_edma_hw_tcd *)_tcd)->_field))

#define fsl_edma_set_tcd_to_le_c(_tcd, _val, _field)					\
_Generic(((_tcd)->_field),								\
	__le64 : (_tcd)->_field = cpu_to_le64(_val),					\
	__le32 : (_tcd)->_field = cpu_to_le32(_val),					\
	__le16 : (_tcd)->_field = cpu_to_le16(_val)					\
)

#define fsl_edma_set_tcd_to_le(_chan, _tcd, _val, _field)	\
do {								\
	if (fsl_edma_drvflags(_chan) & FSL_EDMA_DRV_TCD64)	\
		fsl_edma_set_tcd_to_le_c((struct fsl_edma_hw_tcd64 *)_tcd, _val, _field);	\
	else											\
		fsl_edma_set_tcd_to_le_c((struct fsl_edma_hw_tcd *)_tcd, _val, _field);		\
} while (0)

/* Need after struct defination */
#include "fsl-edma-trace.h"

/*
 * R/W functions for big- or little-endian registers:
 * The eDMA controller's endian is independent of the CPU core's endian.
 * For the big-endian IP module, the offset for 8-bit or 16-bit registers
 * should also be swapped opposite to that in little-endian IP.
 */
static inline u64 edma_readq(struct fsl_edma_engine *edma, void __iomem *addr)
{
	u64 l, h;

	if (edma->big_endian) {
		l = ioread32be(addr);
		h = ioread32be(addr + 4);
	} else {
		l = ioread32(addr);
		h = ioread32(addr + 4);
	}

	trace_edma_readl(edma, addr, l);
	trace_edma_readl(edma, addr + 4, h);

	return (h << 32) | l;
}

static inline u32 edma_readl(struct fsl_edma_engine *edma, void __iomem *addr)
{
	u32 val;

	if (edma->big_endian)
		val = ioread32be(addr);
	else
		val = ioread32(addr);

	trace_edma_readl(edma, addr, val);

	return val;
}

static inline u16 edma_readw(struct fsl_edma_engine *edma, void __iomem *addr)
{
	u16 val;

	if (edma->big_endian)
		val = ioread16be(addr);
	else
		val = ioread16(addr);

	trace_edma_readw(edma, addr, val);

	return val;
}

static inline void edma_writeb(struct fsl_edma_engine *edma,
			       u8 val, void __iomem *addr)
{
	/* swap the reg offset for these in big-endian mode */
	if (edma->big_endian)
		iowrite8(val, (void __iomem *)((unsigned long)addr ^ 0x3));
	else
		iowrite8(val, addr);

	trace_edma_writeb(edma, addr, val);
}

static inline void edma_writew(struct fsl_edma_engine *edma,
			       u16 val, void __iomem *addr)
{
	/* swap the reg offset for these in big-endian mode */
	if (edma->big_endian)
		iowrite16be(val, (void __iomem *)((unsigned long)addr ^ 0x2));
	else
		iowrite16(val, addr);

	trace_edma_writew(edma, addr, val);
}

static inline void edma_writel(struct fsl_edma_engine *edma,
			       u32 val, void __iomem *addr)
{
	if (edma->big_endian)
		iowrite32be(val, addr);
	else
		iowrite32(val, addr);

	trace_edma_writel(edma, addr, val);
}

static inline void edma_writeq(struct fsl_edma_engine *edma,
			       u64 val, void __iomem *addr)
{
	if (edma->big_endian) {
		iowrite32be(val & 0xFFFFFFFF, addr);
		iowrite32be(val >> 32, addr + 4);
	} else {
		iowrite32(val & 0xFFFFFFFF, addr);
		iowrite32(val >> 32, addr + 4);
	}

	trace_edma_writel(edma, addr, val & 0xFFFFFFFF);
	trace_edma_writel(edma, addr + 4, val >> 32);
}

static inline struct fsl_edma_chan *to_fsl_edma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct fsl_edma_chan, vchan.chan);
}

static inline struct fsl_edma_desc *to_fsl_edma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct fsl_edma_desc, vdesc);
}

static inline void fsl_edma_err_chan_handler(struct fsl_edma_chan *fsl_chan)
{
	fsl_chan->status = DMA_ERROR;
}

void fsl_edma_tx_chan_handler(struct fsl_edma_chan *fsl_chan);
void fsl_edma_disable_request(struct fsl_edma_chan *fsl_chan);
void fsl_edma_chan_mux(struct fsl_edma_chan *fsl_chan,
			unsigned int slot, bool enable);
void fsl_edma_free_desc(struct virt_dma_desc *vdesc);
int fsl_edma_terminate_all(struct dma_chan *chan);
int fsl_edma_pause(struct dma_chan *chan);
int fsl_edma_resume(struct dma_chan *chan);
int fsl_edma_slave_config(struct dma_chan *chan,
				 struct dma_slave_config *cfg);
enum dma_status fsl_edma_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie, struct dma_tx_state *txstate);
struct dma_async_tx_descriptor *fsl_edma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t dma_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags);
struct dma_async_tx_descriptor *fsl_edma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context);
struct dma_async_tx_descriptor *fsl_edma_prep_memcpy(
		struct dma_chan *chan, dma_addr_t dma_dst, dma_addr_t dma_src,
		size_t len, unsigned long flags);
void fsl_edma_xfer_desc(struct fsl_edma_chan *fsl_chan);
void fsl_edma_issue_pending(struct dma_chan *chan);
int fsl_edma_alloc_chan_resources(struct dma_chan *chan);
void fsl_edma_free_chan_resources(struct dma_chan *chan);
void fsl_edma_cleanup_vchan(struct dma_device *dmadev);
void fsl_edma_setup_regs(struct fsl_edma_engine *edma);

#endif /* _FSL_EDMA_COMMON_H_ */
