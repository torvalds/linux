/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com
 */

#ifndef K3_UDMA_H_
#define K3_UDMA_H_

#include <linux/soc/ti/ti_sci_protocol.h>

/* Global registers */
#define UDMA_REV_REG			0x0
#define UDMA_PERF_CTL_REG		0x4
#define UDMA_EMU_CTL_REG		0x8
#define UDMA_PSIL_TO_REG		0x10
#define UDMA_UTC_CTL_REG		0x1c
#define UDMA_CAP_REG(i)			(0x20 + ((i) * 4))
#define UDMA_RX_FLOW_ID_FW_OES_REG	0x80
#define UDMA_RX_FLOW_ID_FW_STATUS_REG	0x88

/* BCHANRT/TCHANRT/RCHANRT registers */
#define UDMA_CHAN_RT_CTL_REG		0x0
#define UDMA_CHAN_RT_SWTRIG_REG		0x8
#define UDMA_CHAN_RT_STDATA_REG		0x80

#define UDMA_CHAN_RT_PEER_REG(i)	(0x200 + ((i) * 0x4))
#define UDMA_CHAN_RT_PEER_STATIC_TR_XY_REG	\
	UDMA_CHAN_RT_PEER_REG(0)	/* PSI-L: 0x400 */
#define UDMA_CHAN_RT_PEER_STATIC_TR_Z_REG	\
	UDMA_CHAN_RT_PEER_REG(1)	/* PSI-L: 0x401 */
#define UDMA_CHAN_RT_PEER_BCNT_REG		\
	UDMA_CHAN_RT_PEER_REG(4)	/* PSI-L: 0x404 */
#define UDMA_CHAN_RT_PEER_RT_EN_REG		\
	UDMA_CHAN_RT_PEER_REG(8)	/* PSI-L: 0x408 */

#define UDMA_CHAN_RT_PCNT_REG		0x400
#define UDMA_CHAN_RT_BCNT_REG		0x408
#define UDMA_CHAN_RT_SBCNT_REG		0x410

/* UDMA_CAP Registers */
#define UDMA_CAP2_TCHAN_CNT(val)	((val) & 0x1ff)
#define UDMA_CAP2_ECHAN_CNT(val)	(((val) >> 9) & 0x1ff)
#define UDMA_CAP2_RCHAN_CNT(val)	(((val) >> 18) & 0x1ff)
#define UDMA_CAP3_RFLOW_CNT(val)	((val) & 0x3fff)
#define UDMA_CAP3_HCHAN_CNT(val)	(((val) >> 14) & 0x1ff)
#define UDMA_CAP3_UCHAN_CNT(val)	(((val) >> 23) & 0x1ff)

#define BCDMA_CAP2_BCHAN_CNT(val)	((val) & 0x1ff)
#define BCDMA_CAP2_TCHAN_CNT(val)	(((val) >> 9) & 0x1ff)
#define BCDMA_CAP2_RCHAN_CNT(val)	(((val) >> 18) & 0x1ff)
#define BCDMA_CAP3_HBCHAN_CNT(val)	(((val) >> 14) & 0x1ff)
#define BCDMA_CAP3_UBCHAN_CNT(val)	(((val) >> 23) & 0x1ff)
#define BCDMA_CAP4_HRCHAN_CNT(val)	((val) & 0xff)
#define BCDMA_CAP4_URCHAN_CNT(val)	(((val) >> 8) & 0xff)
#define BCDMA_CAP4_HTCHAN_CNT(val)	(((val) >> 16) & 0xff)
#define BCDMA_CAP4_UTCHAN_CNT(val)	(((val) >> 24) & 0xff)

#define PKTDMA_CAP4_TFLOW_CNT(val)	((val) & 0x3fff)

/* UDMA_CHAN_RT_CTL_REG */
#define UDMA_CHAN_RT_CTL_EN		BIT(31)
#define UDMA_CHAN_RT_CTL_TDOWN		BIT(30)
#define UDMA_CHAN_RT_CTL_PAUSE		BIT(29)
#define UDMA_CHAN_RT_CTL_FTDOWN		BIT(28)
#define UDMA_CHAN_RT_CTL_ERROR		BIT(0)

/* UDMA_CHAN_RT_PEER_RT_EN_REG */
#define UDMA_PEER_RT_EN_ENABLE		BIT(31)
#define UDMA_PEER_RT_EN_TEARDOWN	BIT(30)
#define UDMA_PEER_RT_EN_PAUSE		BIT(29)
#define UDMA_PEER_RT_EN_FLUSH		BIT(28)
#define UDMA_PEER_RT_EN_IDLE		BIT(1)

/*
 * UDMA_TCHAN_RT_PEER_STATIC_TR_XY_REG /
 * UDMA_RCHAN_RT_PEER_STATIC_TR_XY_REG
 */
#define PDMA_STATIC_TR_X_MASK		GENMASK(26, 24)
#define PDMA_STATIC_TR_X_SHIFT		(24)
#define PDMA_STATIC_TR_Y_MASK		GENMASK(11, 0)
#define PDMA_STATIC_TR_Y_SHIFT		(0)

#define PDMA_STATIC_TR_Y(x)	\
	(((x) << PDMA_STATIC_TR_Y_SHIFT) & PDMA_STATIC_TR_Y_MASK)
#define PDMA_STATIC_TR_X(x)	\
	(((x) << PDMA_STATIC_TR_X_SHIFT) & PDMA_STATIC_TR_X_MASK)

#define PDMA_STATIC_TR_XY_ACC32		BIT(30)
#define PDMA_STATIC_TR_XY_BURST		BIT(31)

/*
 * UDMA_TCHAN_RT_PEER_STATIC_TR_Z_REG /
 * UDMA_RCHAN_RT_PEER_STATIC_TR_Z_REG
 */
#define PDMA_STATIC_TR_Z(x, mask)	((x) & (mask))

/* Address Space Select */
#define K3_ADDRESS_ASEL_SHIFT		48

struct udma_dev;
struct udma_tchan;
struct udma_rchan;
struct udma_rflow;

enum udma_rm_range {
	RM_RANGE_BCHAN = 0,
	RM_RANGE_TCHAN,
	RM_RANGE_RCHAN,
	RM_RANGE_RFLOW,
	RM_RANGE_TFLOW,
	RM_RANGE_LAST,
};

struct udma_tisci_rm {
	const struct ti_sci_handle *tisci;
	const struct ti_sci_rm_udmap_ops *tisci_udmap_ops;
	u32  tisci_dev_id;

	/* tisci information for PSI-L thread pairing/unpairing */
	const struct ti_sci_rm_psil_ops *tisci_psil_ops;
	u32  tisci_navss_dev_id;

	struct ti_sci_resource *rm_ranges[RM_RANGE_LAST];
};

/* Direct access to UDMA low lever resources for the glue layer */
int xudma_navss_psil_pair(struct udma_dev *ud, u32 src_thread, u32 dst_thread);
int xudma_navss_psil_unpair(struct udma_dev *ud, u32 src_thread,
			    u32 dst_thread);

struct udma_dev *of_xudma_dev_get(struct device_node *np, const char *property);
struct device *xudma_get_device(struct udma_dev *ud);
struct k3_ringacc *xudma_get_ringacc(struct udma_dev *ud);
u32 xudma_dev_get_psil_base(struct udma_dev *ud);
struct udma_tisci_rm *xudma_dev_get_tisci_rm(struct udma_dev *ud);

int xudma_alloc_gp_rflow_range(struct udma_dev *ud, int from, int cnt);
int xudma_free_gp_rflow_range(struct udma_dev *ud, int from, int cnt);

struct udma_tchan *xudma_tchan_get(struct udma_dev *ud, int id);
struct udma_rchan *xudma_rchan_get(struct udma_dev *ud, int id);
struct udma_rflow *xudma_rflow_get(struct udma_dev *ud, int id);

void xudma_tchan_put(struct udma_dev *ud, struct udma_tchan *p);
void xudma_rchan_put(struct udma_dev *ud, struct udma_rchan *p);
void xudma_rflow_put(struct udma_dev *ud, struct udma_rflow *p);

int xudma_tchan_get_id(struct udma_tchan *p);
int xudma_rchan_get_id(struct udma_rchan *p);
int xudma_rflow_get_id(struct udma_rflow *p);

u32 xudma_tchanrt_read(struct udma_tchan *tchan, int reg);
void xudma_tchanrt_write(struct udma_tchan *tchan, int reg, u32 val);
u32 xudma_rchanrt_read(struct udma_rchan *rchan, int reg);
void xudma_rchanrt_write(struct udma_rchan *rchan, int reg, u32 val);
bool xudma_rflow_is_gp(struct udma_dev *ud, int id);
int xudma_get_rflow_ring_offset(struct udma_dev *ud);

int xudma_is_pktdma(struct udma_dev *ud);

int xudma_pktdma_tflow_get_irq(struct udma_dev *ud, int udma_tflow_id);
int xudma_pktdma_rflow_get_irq(struct udma_dev *ud, int udma_rflow_id);
#endif /* K3_UDMA_H_ */
