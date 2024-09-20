/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 NXP */

#ifndef __DPAA2_QDMA_H
#define __DPAA2_QDMA_H

#define DPAA2_QDMA_STORE_SIZE 16
#define NUM_CH 8
#define DPAA2_QDMA_DEFAULT_PRIORITY 0

struct dpaa2_qdma_sd_d {
	u32 rsv:32;
	union {
		struct {
			u32 ssd:12; /* souce stride distance */
			u32 sss:12; /* souce stride size */
			u32 rsv1:8;
		} sdf;
		struct {
			u32 dsd:12; /* Destination stride distance */
			u32 dss:12; /* Destination stride size */
			u32 rsv2:8;
		} ddf;
	} df;
	u32 rbpcmd;	/* Route-by-port command */
	u32 cmd;
} __attribute__((__packed__));

/* Source descriptor command read transaction type for RBP=0: */
/* coherent copy of cacheable memory */
#define QDMA_SD_CMD_RDTTYPE_COHERENT (0xb << 28)
/* Destination descriptor command write transaction type for RBP=0: */
/* coherent copy of cacheable memory */
#define QDMA_DD_CMD_WRTTYPE_COHERENT (0x6 << 28)
#define LX2160_QDMA_DD_CMD_WRTTYPE_COHERENT (0xb << 28)

#define QMAN_FD_FMT_ENABLE	BIT(0) /* frame list table enable */
#define QMAN_FD_BMT_ENABLE	BIT(15) /* bypass memory translation */
#define QMAN_FD_BMT_DISABLE	(0) /* bypass memory translation */
#define QMAN_FD_SL_DISABLE	(0) /* short lengthe disabled */
#define QMAN_FD_SL_ENABLE	BIT(14) /* short lengthe enabled */

#define QDMA_FINAL_BIT_DISABLE	(0) /* final bit disable */
#define QDMA_FINAL_BIT_ENABLE	BIT(31) /* final bit enable */

#define QDMA_FD_SHORT_FORMAT	BIT(11) /* short format */
#define QDMA_FD_LONG_FORMAT	(0) /* long format */
#define QDMA_SER_DISABLE	(8) /* no notification */
#define QDMA_SER_CTX		BIT(8) /* notification by FQD_CTX[fqid] */
#define QDMA_SER_DEST		(2 << 8) /* notification by destination desc */
#define QDMA_SER_BOTH		(3 << 8) /* soruce and dest notification */
#define QDMA_FD_SPF_ENALBE	BIT(30) /* source prefetch enable */

#define QMAN_FD_VA_ENABLE	BIT(14) /* Address used is virtual address */
#define QMAN_FD_VA_DISABLE	(0)/* Address used is a real address */
/* Flow Context: 49bit physical address */
#define QMAN_FD_CBMT_ENABLE	BIT(15)
#define QMAN_FD_CBMT_DISABLE	(0) /* Flow Context: 64bit virtual address */
#define QMAN_FD_SC_DISABLE	(0) /* stashing control */

#define QDMA_FL_FMT_SBF		(0x0) /* Single buffer frame */
#define QDMA_FL_FMT_SGE		(0x2) /* Scatter gather frame */
#define QDMA_FL_BMT_ENABLE	BIT(15) /* enable bypass memory translation */
#define QDMA_FL_BMT_DISABLE	(0x0) /* enable bypass memory translation */
#define QDMA_FL_SL_LONG		(0x0)/* long length */
#define QDMA_FL_SL_SHORT	(0x1) /* short length */
#define QDMA_FL_F		(0x1)/* last frame list bit */

/*Description of Frame list table structure*/
struct dpaa2_qdma_chan {
	struct dpaa2_qdma_engine	*qdma;
	struct virt_dma_chan		vchan;
	struct virt_dma_desc		vdesc;
	enum dma_status			status;
	u32				fqid;

	/* spinlock used by dpaa2 qdma driver */
	spinlock_t			queue_lock;
	struct dma_pool			*fd_pool;
	struct dma_pool			*fl_pool;
	struct dma_pool			*sdd_pool;

	struct list_head		comp_used;
	struct list_head		comp_free;

};

struct dpaa2_qdma_comp {
	dma_addr_t		fd_bus_addr;
	dma_addr_t		fl_bus_addr;
	dma_addr_t		desc_bus_addr;
	struct dpaa2_fd		*fd_virt_addr;
	struct dpaa2_fl_entry	*fl_virt_addr;
	struct dpaa2_qdma_sd_d	*desc_virt_addr;
	struct dpaa2_qdma_chan	*qchan;
	struct virt_dma_desc	vdesc;
	struct list_head	list;
};

struct dpaa2_qdma_engine {
	struct dma_device	dma_dev;
	u32			n_chans;
	struct dpaa2_qdma_chan	chans[NUM_CH];
	int			qdma_wrtype_fixup;
	int			desc_allocated;

	struct dpaa2_qdma_priv *priv;
};

/*
 * dpaa2_qdma_priv - driver private data
 */
struct dpaa2_qdma_priv {
	int dpqdma_id;

	struct iommu_domain	*iommu_domain;
	struct dpdmai_attr	dpdmai_attr;
	struct device		*dev;
	struct fsl_mc_io	*mc_io;
	struct fsl_mc_device	*dpdmai_dev;
	u8			num_pairs;

	struct dpaa2_qdma_engine	*dpaa2_qdma;
	struct dpaa2_qdma_priv_per_prio	*ppriv;

	struct dpdmai_rx_queue_attr rx_queue_attr[DPDMAI_MAX_QUEUE_NUM];
	struct dpdmai_tx_queue_attr tx_queue_attr[DPDMAI_MAX_QUEUE_NUM];
};

struct dpaa2_qdma_priv_per_prio {
	int req_fqid;
	int rsp_fqid;
	int prio;

	struct dpaa2_io_store *store;
	struct dpaa2_io_notification_ctx nctx;

	struct dpaa2_qdma_priv *priv;
};

static struct soc_device_attribute soc_fixup_tuning[] = {
	{ .family = "QorIQ LX2160A"},
	{ /* sentinel */ }
};

/* FD pool size: one FD + 3 Frame list + 2 source/destination descriptor */
#define FD_POOL_SIZE (sizeof(struct dpaa2_fd) + \
		sizeof(struct dpaa2_fl_entry) * 3 + \
		sizeof(struct dpaa2_qdma_sd_d) * 2)

static void dpaa2_dpdmai_free_channels(struct dpaa2_qdma_engine *dpaa2_qdma);
static void dpaa2_dpdmai_free_comp(struct dpaa2_qdma_chan *qchan,
				   struct list_head *head);
#endif /* __DPAA2_QDMA_H */
