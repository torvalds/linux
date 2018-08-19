/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2013-2014 Freescale Semiconductor, Inc.
 * Copyright 2018 Angelo Dureghello <angelo@sysam.it>
 */
#ifndef _FSL_EDMA_COMMON_H_
#define _FSL_EDMA_COMMON_H_

#include "virt-dma.h"

#define EDMA_CR			0x00
#define EDMA_ES			0x04
#define EDMA_ERQ		0x0C
#define EDMA_EEI		0x14
#define EDMA_SERQ		0x1B
#define EDMA_CERQ		0x1A
#define EDMA_SEEI		0x19
#define EDMA_CEEI		0x18
#define EDMA_CINT		0x1F
#define EDMA_CERR		0x1E
#define EDMA_SSRT		0x1D
#define EDMA_CDNE		0x1C
#define EDMA_INTR		0x24
#define EDMA_ERR		0x2C

#define EDMA_TCD_SADDR(x)	(0x1000 + 32 * (x))
#define EDMA_TCD_SOFF(x)	(0x1004 + 32 * (x))
#define EDMA_TCD_ATTR(x)	(0x1006 + 32 * (x))
#define EDMA_TCD_NBYTES(x)	(0x1008 + 32 * (x))
#define EDMA_TCD_SLAST(x)	(0x100C + 32 * (x))
#define EDMA_TCD_DADDR(x)	(0x1010 + 32 * (x))
#define EDMA_TCD_DOFF(x)	(0x1014 + 32 * (x))
#define EDMA_TCD_CITER_ELINK(x)	(0x1016 + 32 * (x))
#define EDMA_TCD_CITER(x)	(0x1016 + 32 * (x))
#define EDMA_TCD_DLAST_SGA(x)	(0x1018 + 32 * (x))
#define EDMA_TCD_CSR(x)		(0x101C + 32 * (x))
#define EDMA_TCD_BITER_ELINK(x)	(0x101E + 32 * (x))
#define EDMA_TCD_BITER(x)	(0x101E + 32 * (x))

#define EDMA_CR_EDBG		BIT(1)
#define EDMA_CR_ERCA		BIT(2)
#define EDMA_CR_ERGA		BIT(3)
#define EDMA_CR_HOE		BIT(4)
#define EDMA_CR_HALT		BIT(5)
#define EDMA_CR_CLM		BIT(6)
#define EDMA_CR_EMLM		BIT(7)
#define EDMA_CR_ECX		BIT(16)
#define EDMA_CR_CX		BIT(17)

#define EDMA_SEEI_SEEI(x)	((x) & 0x1F)
#define EDMA_CEEI_CEEI(x)	((x) & 0x1F)
#define EDMA_CINT_CINT(x)	((x) & 0x1F)
#define EDMA_CERR_CERR(x)	((x) & 0x1F)

#define EDMA_TCD_ATTR_DSIZE(x)		(((x) & 0x0007))
#define EDMA_TCD_ATTR_DMOD(x)		(((x) & 0x001F) << 3)
#define EDMA_TCD_ATTR_SSIZE(x)		(((x) & 0x0007) << 8)
#define EDMA_TCD_ATTR_SMOD(x)		(((x) & 0x001F) << 11)
#define EDMA_TCD_ATTR_SSIZE_8BIT	(0x0000)
#define EDMA_TCD_ATTR_SSIZE_16BIT	(0x0100)
#define EDMA_TCD_ATTR_SSIZE_32BIT	(0x0200)
#define EDMA_TCD_ATTR_SSIZE_64BIT	(0x0300)
#define EDMA_TCD_ATTR_SSIZE_32BYTE	(0x0500)
#define EDMA_TCD_ATTR_DSIZE_8BIT	(0x0000)
#define EDMA_TCD_ATTR_DSIZE_16BIT	(0x0001)
#define EDMA_TCD_ATTR_DSIZE_32BIT	(0x0002)
#define EDMA_TCD_ATTR_DSIZE_64BIT	(0x0003)
#define EDMA_TCD_ATTR_DSIZE_32BYTE	(0x0005)

#define EDMA_TCD_SOFF_SOFF(x)		(x)
#define EDMA_TCD_NBYTES_NBYTES(x)	(x)
#define EDMA_TCD_SLAST_SLAST(x)		(x)
#define EDMA_TCD_DADDR_DADDR(x)		(x)
#define EDMA_TCD_CITER_CITER(x)		((x) & 0x7FFF)
#define EDMA_TCD_DOFF_DOFF(x)		(x)
#define EDMA_TCD_DLAST_SGA_DLAST_SGA(x)	(x)
#define EDMA_TCD_BITER_BITER(x)		((x) & 0x7FFF)

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

struct fsl_edma_sw_tcd {
	dma_addr_t			ptcd;
	struct fsl_edma_hw_tcd		*vtcd;
};

struct fsl_edma_slave_config {
	enum dma_transfer_direction	dir;
	enum dma_slave_buswidth		addr_width;
	u32				dev_addr;
	u32				burst;
	u32				attr;
};

struct fsl_edma_chan {
	struct virt_dma_chan		vchan;
	enum dma_status			status;
	enum fsl_edma_pm_state		pm_state;
	bool				idle;
	u32				slave_id;
	struct fsl_edma_engine		*edma;
	struct fsl_edma_desc		*edesc;
	struct fsl_edma_slave_config	fsc;
	struct dma_pool			*tcd_pool;
};

struct fsl_edma_desc {
	struct virt_dma_desc		vdesc;
	struct fsl_edma_chan		*echan;
	bool				iscyclic;
	unsigned int			n_tcds;
	struct fsl_edma_sw_tcd		tcd[];
};

struct fsl_edma_engine {
	struct dma_device	dma_dev;
	void __iomem		*membase;
	void __iomem		*muxbase[DMAMUX_NR];
	struct clk		*muxclk[DMAMUX_NR];
	struct mutex		fsl_edma_mutex;
	u32			n_chans;
	int			txirq;
	int			errirq;
	bool			big_endian;
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

#endif /* _FSL_EDMA_COMMON_H_ */
