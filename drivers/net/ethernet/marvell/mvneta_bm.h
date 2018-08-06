/*
 * Driver for Marvell NETA network controller Buffer Manager.
 *
 * Copyright (C) 2015 Marvell
 *
 * Marcin Wojtas <mw@semihalf.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _MVNETA_BM_H_
#define _MVNETA_BM_H_

/* BM Configuration Register */
#define MVNETA_BM_CONFIG_REG			0x0
#define    MVNETA_BM_STATUS_MASK		0x30
#define    MVNETA_BM_ACTIVE_MASK		BIT(4)
#define    MVNETA_BM_MAX_IN_BURST_SIZE_MASK	0x60000
#define    MVNETA_BM_MAX_IN_BURST_SIZE_16BP	BIT(18)
#define    MVNETA_BM_EMPTY_LIMIT_MASK		BIT(19)

/* BM Activation Register */
#define MVNETA_BM_COMMAND_REG			0x4
#define    MVNETA_BM_START_MASK			BIT(0)
#define    MVNETA_BM_STOP_MASK			BIT(1)
#define    MVNETA_BM_PAUSE_MASK			BIT(2)

/* BM Xbar interface Register */
#define MVNETA_BM_XBAR_01_REG			0x8
#define MVNETA_BM_XBAR_23_REG			0xc
#define MVNETA_BM_XBAR_POOL_REG(pool)		\
		(((pool) < 2) ? MVNETA_BM_XBAR_01_REG : MVNETA_BM_XBAR_23_REG)
#define     MVNETA_BM_TARGET_ID_OFFS(pool)	(((pool) & 1) ? 16 : 0)
#define     MVNETA_BM_TARGET_ID_MASK(pool)	\
		(0xf << MVNETA_BM_TARGET_ID_OFFS(pool))
#define     MVNETA_BM_TARGET_ID_VAL(pool, id)	\
		((id) << MVNETA_BM_TARGET_ID_OFFS(pool))
#define     MVNETA_BM_XBAR_ATTR_OFFS(pool)	(((pool) & 1) ? 20 : 4)
#define     MVNETA_BM_XBAR_ATTR_MASK(pool)	\
		(0xff << MVNETA_BM_XBAR_ATTR_OFFS(pool))
#define     MVNETA_BM_XBAR_ATTR_VAL(pool, attr)	\
		((attr) << MVNETA_BM_XBAR_ATTR_OFFS(pool))

/* Address of External Buffer Pointers Pool Register */
#define MVNETA_BM_POOL_BASE_REG(pool)		(0x10 + ((pool) << 4))
#define     MVNETA_BM_POOL_ENABLE_MASK		BIT(0)

/* External Buffer Pointers Pool RD pointer Register */
#define MVNETA_BM_POOL_READ_PTR_REG(pool)	(0x14 + ((pool) << 4))
#define     MVNETA_BM_POOL_SET_READ_PTR_MASK	0xfffc
#define     MVNETA_BM_POOL_GET_READ_PTR_OFFS	16
#define     MVNETA_BM_POOL_GET_READ_PTR_MASK	0xfffc0000

/* External Buffer Pointers Pool WR pointer */
#define MVNETA_BM_POOL_WRITE_PTR_REG(pool)	(0x18 + ((pool) << 4))
#define     MVNETA_BM_POOL_SET_WRITE_PTR_OFFS	0
#define     MVNETA_BM_POOL_SET_WRITE_PTR_MASK	0xfffc
#define     MVNETA_BM_POOL_GET_WRITE_PTR_OFFS	16
#define     MVNETA_BM_POOL_GET_WRITE_PTR_MASK	0xfffc0000

/* External Buffer Pointers Pool Size Register */
#define MVNETA_BM_POOL_SIZE_REG(pool)		(0x1c + ((pool) << 4))
#define     MVNETA_BM_POOL_SIZE_MASK		0x3fff

/* BM Interrupt Cause Register */
#define MVNETA_BM_INTR_CAUSE_REG		(0x50)

/* BM interrupt Mask Register */
#define MVNETA_BM_INTR_MASK_REG			(0x54)

/* Other definitions */
#define MVNETA_BM_SHORT_PKT_SIZE		256
#define MVNETA_BM_POOLS_NUM			4
#define MVNETA_BM_POOL_CAP_MIN			128
#define MVNETA_BM_POOL_CAP_DEF			2048
#define MVNETA_BM_POOL_CAP_MAX			\
		(16 * 1024 - MVNETA_BM_POOL_CAP_ALIGN)
#define MVNETA_BM_POOL_CAP_ALIGN		32
#define MVNETA_BM_POOL_PTR_ALIGN		32

#define MVNETA_BM_POOL_ACCESS_OFFS		8

#define MVNETA_BM_BPPI_SIZE			0x100000

#define MVNETA_RX_BUF_SIZE(pkt_size)   ((pkt_size) + NET_SKB_PAD)

enum mvneta_bm_type {
	MVNETA_BM_FREE,
	MVNETA_BM_LONG,
	MVNETA_BM_SHORT
};

struct mvneta_bm {
	void __iomem *reg_base;
	struct clk *clk;
	struct platform_device *pdev;

	struct gen_pool *bppi_pool;
	/* BPPI virtual base address */
	void __iomem *bppi_virt_addr;
	/* BPPI physical base address */
	dma_addr_t bppi_phys_addr;

	/* BM pools */
	struct mvneta_bm_pool *bm_pools;
};

struct mvneta_bm_pool {
	struct hwbm_pool hwbm_pool;
	/* Pool number in the range 0-3 */
	u8 id;
	enum mvneta_bm_type type;

	/* Packet size */
	int pkt_size;
	/* Size of the buffer acces through DMA*/
	u32 buf_size;

	/* BPPE virtual base address */
	u32 *virt_addr;
	/* BPPE physical base address */
	dma_addr_t phys_addr;

	/* Ports using BM pool */
	u8 port_map;

	struct mvneta_bm *priv;
};

/* Declarations and definitions */
#if IS_ENABLED(CONFIG_MVNETA_BM)
struct mvneta_bm *mvneta_bm_get(struct device_node *node);
void mvneta_bm_put(struct mvneta_bm *priv);

void mvneta_bm_pool_destroy(struct mvneta_bm *priv,
			    struct mvneta_bm_pool *bm_pool, u8 port_map);
void mvneta_bm_bufs_free(struct mvneta_bm *priv, struct mvneta_bm_pool *bm_pool,
			 u8 port_map);
int mvneta_bm_construct(struct hwbm_pool *hwbm_pool, void *buf);
int mvneta_bm_pool_refill(struct mvneta_bm *priv,
			  struct mvneta_bm_pool *bm_pool);
struct mvneta_bm_pool *mvneta_bm_pool_use(struct mvneta_bm *priv, u8 pool_id,
					  enum mvneta_bm_type type, u8 port_id,
					  int pkt_size);

static inline void mvneta_bm_pool_put_bp(struct mvneta_bm *priv,
					 struct mvneta_bm_pool *bm_pool,
					 dma_addr_t buf_phys_addr)
{
	writel_relaxed(buf_phys_addr, priv->bppi_virt_addr +
		       (bm_pool->id << MVNETA_BM_POOL_ACCESS_OFFS));
}

static inline u32 mvneta_bm_pool_get_bp(struct mvneta_bm *priv,
					struct mvneta_bm_pool *bm_pool)
{
	return readl_relaxed(priv->bppi_virt_addr +
			     (bm_pool->id << MVNETA_BM_POOL_ACCESS_OFFS));
}
#else
void mvneta_bm_pool_destroy(struct mvneta_bm *priv,
			    struct mvneta_bm_pool *bm_pool, u8 port_map) {}
void mvneta_bm_bufs_free(struct mvneta_bm *priv, struct mvneta_bm_pool *bm_pool,
			 u8 port_map) {}
int mvneta_bm_construct(struct hwbm_pool *hwbm_pool, void *buf) { return 0; }
int mvneta_bm_pool_refill(struct mvneta_bm *priv,
			  struct mvneta_bm_pool *bm_pool) {return 0; }
struct mvneta_bm_pool *mvneta_bm_pool_use(struct mvneta_bm *priv, u8 pool_id,
					  enum mvneta_bm_type type, u8 port_id,
					  int pkt_size) { return NULL; }

static inline void mvneta_bm_pool_put_bp(struct mvneta_bm *priv,
					 struct mvneta_bm_pool *bm_pool,
					 dma_addr_t buf_phys_addr) {}

static inline u32 mvneta_bm_pool_get_bp(struct mvneta_bm *priv,
					struct mvneta_bm_pool *bm_pool)
{ return 0; }
struct mvneta_bm *mvneta_bm_get(struct device_node *node) { return NULL; }
void mvneta_bm_put(struct mvneta_bm *priv) {}
#endif /* CONFIG_MVNETA_BM */
#endif
