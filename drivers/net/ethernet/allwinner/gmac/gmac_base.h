#ifndef __GMAC_BASE_H__
#define __GMAC_BASE_H__

/* CSR1 enables the transmit DMA to check for new descriptor */
static inline void dma_en_tx(void __iomem *ioaddr)
{
	writel(1, ioaddr + GDMA_XMT_POLL);
}

static inline void dma_en_irq(void __iomem *ioaddr)
{
	writel(GDMA_DEF_INTR, ioaddr + GDMA_INTR_ENA);
}

static inline void dma_dis_irq(void __iomem *ioaddr)
{
	writel(0, ioaddr + GDMA_INTR_ENA);
}

static inline void dma_start_tx(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + GDMA_OP_MODE);
	value |= OP_MODE_ST;
	writel(value, ioaddr + GDMA_OP_MODE);
}

static inline void dma_stop_tx(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + GDMA_OP_MODE);
	value &= ~OP_MODE_ST;
	writel(value, ioaddr + GDMA_OP_MODE);
}

static inline void dma_start_rx(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + GDMA_OP_MODE);
	value |= OP_MODE_SR;
	writel(value, ioaddr + GDMA_OP_MODE);
}

static inline void dma_stop_rx(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + GDMA_OP_MODE);
	value &= ~OP_MODE_SR;
	writel(value, ioaddr + GDMA_OP_MODE);
}

int gdma_init(void __iomem *ioaddr, int pbl, u32 dma_tx, u32 dma_rx);
void dma_oper_mode(void __iomem *ioaddr, int txmode, int rxmode);
int dma_interrupt(void __iomem *ioaddr, struct gmac_extra_stats *x);
void dma_flush_tx_fifo(void __iomem *ioaddr);
void dma_dump_regs(void __iomem *ioaddr);

void core_init(void __iomem *ioaddr);
int core_en_rx_coe(void __iomem *ioaddr);
void core_set_filter(struct net_device *dev);
void core_flow_ctrl(void __iomem *ioaddr, unsigned int duplex,
					unsigned int fc, unsigned int pause_time);
void core_irq_status(void __iomem *ioaddr);
void core_dump_regs(void __iomem *ioaddr);

void gmac_set_umac_addr(void __iomem *ioaddr, unsigned char *addr,
						unsigned int reg_n);
void gmac_set_tx_rx(void __iomem *ioaddr, bool enable);
void gmac_get_umac_addr(void __iomem *ioaddr, unsigned char *addr,
						unsigned int reg_n);
#endif //__GMAC_BASE_H__
