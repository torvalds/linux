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
#define EDMA_TCD_ATTR_DSIZE_8BIT	0
#define EDMA_TCD_ATTR_DSIZE_16BIT	BIT(0)
#define EDMA_TCD_ATTR_DSIZE_32BIT	BIT(1)
#define EDMA_TCD_ATTR_DSIZE_64BIT	(BIT(0) | BIT(1))
#define EDMA_TCD_ATTR_DSIZE_32BYTE	(BIT(3) | BIT(0))
#define EDMA_TCD_ATTR_SSIZE_8BIT	0
#define EDMA_TCD_ATTR_SSIZE_16BIT	(EDMA_TCD_ATTR_DSIZE_16BIT << 8)
#define EDMA_TCD_ATTR_SSIZE_32BIT	(EDMA_TCD_ATTR_DSIZE_32BIT << 8)
#define EDMA_TCD_ATTR_SSIZE_64BIT	(EDMA_TCD_ATTR_DSIZE_64BIT << 8)
#define EDMA_TCD_ATTR_SSIZE_32BYTE	(EDMA_TCD_ATTR_DSIZE_32BYTE << 8)

#define EDMA_TCD_CITER_CITER(x)		((x) & GENMASK(14, 0))
#define EDMA_TCD_BITER_BITER(x)		((x) & GENMASK(14, 0))

#define EDMA_TCD_CSR_START		BIT(0)
#define EDMA_TCD_CSR_INT_MAJOR		BIT(1)
#define EDMA_TCD_CSR_INT_HALF		BIT(2)
#define EDMA_TCD_CSR_D_REQ		BIT(3)
#define EDMA_TCD_CSR_E_SG		BIT(4)
#define EDMA_TCD_CSR_E_LINK		BIT(5)
#define EDMA_TCD_CSR_ACTIVE		BIT(6)
#define EDMA_TCD_CSR_DONE		BIT(7)

#define EDMAMUX_CHCFG_DIS		0x0
#define EDMAMUX_CHCFG_ENBL		0x80
#define EDMAMUX_CHCFG_SOURCE(n)		((n) & 0x3F)

#define DMAMUX_NR	2

#define FSL_EDMA_BUSWIDTHS	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
				 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))
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
	struct fsl_edma_hw_tcd __iomem *tcd;
};

struct fsl_edma_sw_tcd {
	dma_addr_t			ptcd;
	struct fsl_edma_hw_tcd		*vtcd;
};

struct fsl_edma_chan {
	struct virt_dma_chan		vchan;
	enum dma_status			status;
	enum fsl_edma_pm_state		pm_state;
	bool				idle;
	u32				slave_id;
	struct fsl_edma_engine		*edma;
	struct fsl_edma_desc		*edesc;
	struct dma_slave_config		cfg;
	u32				attr;
	struct dma_pool			*tcd_pool;
	dma_addr_t			dma_dev_addr;
	u32				dma_dev_size;
	enum dma_data_direction		dma_dir;
	char				chan_name[16];
};

struct fsl_edma_desc {
	struct virt_dma_desc		vdesc;
	struct fsl_edma_chan		*echan;
	bool				iscyclic;
	enum dma_transfer_direction	dirn;
	unsigned int			n_tcds;
	struct fsl_edma_sw_tcd		tcd[];
};

enum edma_version {
	v1, /* 32ch, Vybrid, mpc57x, etc */
	v2, /* 64ch Coldfire */
	v3, /* 32ch, i.mx7ulp */
};

struct fsl_edma_drvdata {
	enum edma_version	version;
	u32			dmamuxs;
	bool			has_dmaclk;
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
	struct fsl_edma_chan	chans[];
};

/*
 * R/W functions for big- or little-endian registers:
 * The eDMA controller's endian is independent of the CPU core's endian.
 * For the big-endian IP module, the offset for 8-bit or 16-bit registers
 * should also be swapped opposite to that in little-endian IP.
 */
static inline u32 edma_readl(struct fsl_edma_engine *edma, void __iomem *addr)
{
	if (edma->big_endian)
		return ioread32be(addr);
	else
		return ioread32(addr);
}

static inline void edma_writeb(struct fsl_edma_engine *edma,
			       u8 val, void __iomem *addr)
{
	/* swap the reg offset for these in big-endian mode */
	if (edma->big_endian)
		iowrite8(val, (void __iomem *)((unsigned long)addr ^ 0x3));
	else
		iowrite8(val, addr);
}

static inline void edma_writew(struct fsl_edma_engine *edma,
			       u16 val, void __iomem *addr)
{
	/* swap the reg offset for these in big-endian mode */
	if (edma->big_endian)
		iowrite16be(val, (void __iomem *)((unsigned long)addr ^ 0x2));
	else
		iowrite16(val, addr);
}

static inline void edma_writel(struct fsl_edma_engine *edma,
			       u32 val, void __iomem *addr)
{
	if (edma->big_endian)
		iowrite32be(val, addr);
	else
		iowrite32(val, addr);
}

static inline struct fsl_edma_chan *to_fsl_edma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct fsl_edma_chan, vchan.chan);
}

static inline struct fsl_edma_desc *to_fsl_edma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct fsl_edma_desc, vdesc);
}

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
void fsl_edma_xfer_desc(struct fsl_edma_chan *fsl_chan);
void fsl_edma_issue_pending(struct dma_chan *chan);
int fsl_edma_alloc_chan_resources(struct dma_chan *chan);
void fsl_edma_free_chan_resources(struct dma_chan *chan);
void fsl_edma_cleanup_vchan(struct dma_device *dmadev);
void fsl_edma_setup_regs(struct fsl_edma_engine *edma);

#endif /* _FSL_EDMA_COMMON_H_ */
