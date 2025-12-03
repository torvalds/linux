// SPDX-License-Identifier: GPL-2.0
/* ICSSG Ethernet driver
 *
 * Copyright (C) 2022 Texas Instruments Incorporated - https://www.ti.com
 */

#include <linux/iopoll.h>
#include <linux/regmap.h>
#include <uapi/linux/if_ether.h>
#include "icssg_config.h"
#include "icssg_prueth.h"
#include "icssg_switch_map.h"
#include "icssg_mii_rt.h"

/* TX IPG Values to be set for 100M link speed. These values are
 * in ocp_clk cycles. So need change if ocp_clk is changed for a specific
 * h/w design.
 */

/* IPG is in core_clk cycles */
#define MII_RT_TX_IPG_100M	0x17
#define MII_RT_TX_IPG_1G	0xb
#define MII_RT_TX_IPG_100M_SR1	0x166
#define MII_RT_TX_IPG_1G_SR1	0x1a

#define	ICSSG_QUEUES_MAX		64
#define	ICSSG_QUEUE_OFFSET		0xd00
#define	ICSSG_QUEUE_PEEK_OFFSET		0xe00
#define	ICSSG_QUEUE_CNT_OFFSET		0xe40
#define	ICSSG_QUEUE_RESET_OFFSET	0xf40

#define	ICSSG_NUM_TX_QUEUES	8

#define	RECYCLE_Q_SLICE0	16
#define	RECYCLE_Q_SLICE1	17

#define	ICSSG_NUM_OTHER_QUEUES	5	/* port, host and special queues */

#define	PORT_HI_Q_SLICE0	32
#define	PORT_LO_Q_SLICE0	33
#define	HOST_HI_Q_SLICE0	34
#define	HOST_LO_Q_SLICE0	35
#define	HOST_SPL_Q_SLICE0	40	/* Special Queue */

#define	PORT_HI_Q_SLICE1	36
#define	PORT_LO_Q_SLICE1	37
#define	HOST_HI_Q_SLICE1	38
#define	HOST_LO_Q_SLICE1	39
#define	HOST_SPL_Q_SLICE1	41	/* Special Queue */

#define MII_RXCFG_DEFAULT	(PRUSS_MII_RT_RXCFG_RX_ENABLE | \
				 PRUSS_MII_RT_RXCFG_RX_DATA_RDY_MODE_DIS | \
				 PRUSS_MII_RT_RXCFG_RX_L2_EN | \
				 PRUSS_MII_RT_RXCFG_RX_L2_EOF_SCLR_DIS)

#define MII_TXCFG_DEFAULT	(PRUSS_MII_RT_TXCFG_TX_ENABLE | \
				 PRUSS_MII_RT_TXCFG_TX_AUTO_PREAMBLE | \
				 PRUSS_MII_RT_TXCFG_TX_32_MODE_EN | \
				 PRUSS_MII_RT_TXCFG_TX_IPG_WIRE_CLK_EN)

#define ICSSG_CFG_DEFAULT	(ICSSG_CFG_TX_L1_EN | \
				 ICSSG_CFG_TX_L2_EN | ICSSG_CFG_RX_L2_G_EN | \
				 ICSSG_CFG_TX_PRU_EN | \
				 ICSSG_CFG_SGMII_MODE)

#define FDB_GEN_CFG1		0x60
#define SMEM_VLAN_OFFSET	8
#define SMEM_VLAN_OFFSET_MASK	GENMASK(25, 8)
#define FDB_HASH_SIZE_MASK	GENMASK(6, 3)
#define FDB_HASH_SIZE_SHIFT	3
#define FDB_HASH_SIZE		3

#define FDB_GEN_CFG2		0x64
#define FDB_VLAN_EN		BIT(6)
#define FDB_HOST_EN		BIT(2)
#define FDB_PRU1_EN		BIT(1)
#define FDB_PRU0_EN		BIT(0)
#define FDB_EN_ALL		(FDB_PRU0_EN | FDB_PRU1_EN | \
				 FDB_HOST_EN | FDB_VLAN_EN)

/**
 * struct map - ICSSG Queue Map
 * @queue: Queue number
 * @pd_addr_start: Packet descriptor queue reserved memory
 * @flags: Flags
 * @special: Indicates whether this queue is a special queue or not
 */
struct map {
	int queue;
	u32 pd_addr_start;
	u32 flags;
	bool special;
};

/* Hardware queue map for ICSSG */
static const struct map hwq_map[2][ICSSG_NUM_OTHER_QUEUES] = {
	{
		{ PORT_HI_Q_SLICE0, PORT_DESC0_HI, 0x200000, 0 },
		{ PORT_LO_Q_SLICE0, PORT_DESC0_LO, 0, 0 },
		{ HOST_HI_Q_SLICE0, HOST_DESC0_HI, 0x200000, 0 },
		{ HOST_LO_Q_SLICE0, HOST_DESC0_LO, 0, 0 },
		{ HOST_SPL_Q_SLICE0, HOST_SPPD0, 0x400000, 1 },
	},
	{
		{ PORT_HI_Q_SLICE1, PORT_DESC1_HI, 0xa00000, 0 },
		{ PORT_LO_Q_SLICE1, PORT_DESC1_LO, 0x800000, 0 },
		{ HOST_HI_Q_SLICE1, HOST_DESC1_HI, 0xa00000, 0 },
		{ HOST_LO_Q_SLICE1, HOST_DESC1_LO, 0x800000, 0 },
		{ HOST_SPL_Q_SLICE1, HOST_SPPD1, 0xc00000, 1 },
	},
};

static void icssg_config_mii_init_fw_offload(struct prueth_emac *emac)
{
	struct prueth *prueth = emac->prueth;
	int mii = prueth_emac_slice(emac);
	u32 txcfg_reg, pcnt_reg, txcfg;
	struct regmap *mii_rt;

	mii_rt = prueth->mii_rt;

	txcfg_reg = (mii == ICSS_MII0) ? PRUSS_MII_RT_TXCFG0 :
				       PRUSS_MII_RT_TXCFG1;
	pcnt_reg = (mii == ICSS_MII0) ? PRUSS_MII_RT_RX_PCNT0 :
				       PRUSS_MII_RT_RX_PCNT1;

	txcfg = PRUSS_MII_RT_TXCFG_TX_ENABLE |
		PRUSS_MII_RT_TXCFG_TX_AUTO_PREAMBLE |
		PRUSS_MII_RT_TXCFG_TX_IPG_WIRE_CLK_EN;

	if (emac->phy_if == PHY_INTERFACE_MODE_MII && mii == ICSS_MII1)
		txcfg |= PRUSS_MII_RT_TXCFG_TX_MUX_SEL;
	else if (emac->phy_if != PHY_INTERFACE_MODE_MII && mii == ICSS_MII0)
		txcfg |= PRUSS_MII_RT_TXCFG_TX_MUX_SEL;

	regmap_write(mii_rt, txcfg_reg, txcfg);
	regmap_write(mii_rt, pcnt_reg, 0x1);
}

static void icssg_config_mii_init(struct prueth_emac *emac)
{
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);
	u32 txcfg, txcfg_reg, pcnt_reg;
	struct regmap *mii_rt;

	mii_rt = prueth->mii_rt;

	txcfg_reg = (slice == ICSS_MII0) ? PRUSS_MII_RT_TXCFG0 :
				       PRUSS_MII_RT_TXCFG1;
	pcnt_reg = (slice == ICSS_MII0) ? PRUSS_MII_RT_RX_PCNT0 :
				       PRUSS_MII_RT_RX_PCNT1;

	txcfg = MII_TXCFG_DEFAULT;

	/* In MII mode TX lines swapped inside ICSSG, so TX_MUX_SEL cfg need
	 * to be swapped also comparing to RGMII mode.
	 */
	if (emac->phy_if == PHY_INTERFACE_MODE_MII && slice == ICSS_MII0)
		txcfg |= PRUSS_MII_RT_TXCFG_TX_MUX_SEL;
	else if (emac->phy_if != PHY_INTERFACE_MODE_MII && slice == ICSS_MII1)
		txcfg |= PRUSS_MII_RT_TXCFG_TX_MUX_SEL;

	regmap_write(mii_rt, txcfg_reg, txcfg);
	regmap_write(mii_rt, pcnt_reg, 0x1);
}

static void icssg_miig_queues_init(struct prueth *prueth, int slice)
{
	struct regmap *miig_rt = prueth->miig_rt;
	void __iomem *smem = prueth->shram.va;
	u8 pd[ICSSG_SPECIAL_PD_SIZE];
	int queue = 0, i, j;
	u32 *pdword;

	/* reset hwqueues */
	if (slice)
		queue = ICSSG_NUM_TX_QUEUES;

	for (i = 0; i < ICSSG_NUM_TX_QUEUES; i++) {
		regmap_write(miig_rt, ICSSG_QUEUE_RESET_OFFSET, queue);
		queue++;
	}

	queue = slice ? RECYCLE_Q_SLICE1 : RECYCLE_Q_SLICE0;
	regmap_write(miig_rt, ICSSG_QUEUE_RESET_OFFSET, queue);

	for (i = 0; i < ICSSG_NUM_OTHER_QUEUES; i++) {
		regmap_write(miig_rt, ICSSG_QUEUE_RESET_OFFSET,
			     hwq_map[slice][i].queue);
	}

	/* initialize packet descriptors in SMEM */
	/* push pakcet descriptors to hwqueues */

	pdword = (u32 *)pd;
	for (j = 0; j < ICSSG_NUM_OTHER_QUEUES; j++) {
		const struct map *mp;
		int pd_size, num_pds;
		u32 pdaddr;

		mp = &hwq_map[slice][j];
		if (mp->special) {
			pd_size = ICSSG_SPECIAL_PD_SIZE;
			num_pds = ICSSG_NUM_SPECIAL_PDS;
		} else	{
			pd_size = ICSSG_NORMAL_PD_SIZE;
			num_pds = ICSSG_NUM_NORMAL_PDS;
		}

		for (i = 0; i < num_pds; i++) {
			memset(pd, 0, pd_size);

			pdword[0] &= ICSSG_FLAG_MASK;
			pdword[0] |= mp->flags;
			pdaddr = mp->pd_addr_start + i * pd_size;

			memcpy_toio(smem + pdaddr, pd, pd_size);
			queue = mp->queue;
			regmap_write(miig_rt, ICSSG_QUEUE_OFFSET + 4 * queue,
				     pdaddr);
		}
	}
}

void icssg_config_ipg(struct prueth_emac *emac)
{
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);
	u32 ipg;

	switch (emac->speed) {
	case SPEED_1000:
		ipg = emac->is_sr1 ? MII_RT_TX_IPG_1G_SR1 : MII_RT_TX_IPG_1G;
		break;
	case SPEED_100:
		ipg = emac->is_sr1 ? MII_RT_TX_IPG_100M_SR1 : MII_RT_TX_IPG_100M;
		break;
	case SPEED_10:
		/* Firmware hardcodes IPG for SR1.0 */
		if (emac->is_sr1)
			return;
		/* IPG for 10M is same as 100M */
		ipg = MII_RT_TX_IPG_100M;
		break;
	default:
		/* Other links speeds not supported */
		netdev_err(emac->ndev, "Unsupported link speed\n");
		return;
	}

	icssg_mii_update_ipg(prueth->mii_rt, slice, ipg);
}
EXPORT_SYMBOL_GPL(icssg_config_ipg);

static void emac_r30_cmd_init(struct prueth_emac *emac)
{
	struct icssg_r30_cmd __iomem *p;
	int i;

	p = emac->dram.va + MGR_R30_CMD_OFFSET;

	for (i = 0; i < 4; i++)
		writel(EMAC_NONE, &p->cmd[i]);
}

static int emac_r30_is_done(struct prueth_emac *emac)
{
	const struct icssg_r30_cmd __iomem *p;
	u32 cmd;
	int i;

	p = emac->dram.va + MGR_R30_CMD_OFFSET;

	for (i = 0; i < 4; i++) {
		cmd = readl(&p->cmd[i]);
		if (cmd != EMAC_NONE)
			return 0;
	}

	return 1;
}

static int prueth_fw_offload_buffer_setup(struct prueth_emac *emac)
{
	struct icssg_buffer_pool_cfg __iomem *bpool_cfg;
	struct icssg_rxq_ctx __iomem *rxq_ctx;
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);
	u32 addr;
	int i;

	addr = lower_32_bits(prueth->msmcram.pa);
	if (slice) {
		if (prueth->pdata.banked_ms_ram)
			addr += MSMC_RAM_BANK_SIZE;
		else
			addr += PRUETH_SW_TOTAL_BUF_SIZE_PER_SLICE;
	}

	if (addr % SZ_64K) {
		dev_warn(prueth->dev, "buffer pool needs to be 64KB aligned\n");
		return -EINVAL;
	}

	bpool_cfg = emac->dram.va + BUFFER_POOL_0_ADDR_OFFSET;

	/* Configure buffer pools for forwarding buffers
	 * - used by firmware to store packets to be forwarded to other port
	 * - 8 total pools per slice
	 */
	for (i = 0; i <  PRUETH_NUM_FWD_BUF_POOLS_PER_SLICE; i++) {
		writel(addr, &bpool_cfg[i].addr);
		writel(PRUETH_SW_FWD_BUF_POOL_SIZE, &bpool_cfg[i].len);
		addr += PRUETH_SW_FWD_BUF_POOL_SIZE;
	}

	/* Configure buffer pools for Local Injection buffers
	 *  - used by firmware to store packets received from host core
	 *  - 16 total pools per slice
	 */
	for (i = 0; i < PRUETH_NUM_LI_BUF_POOLS_PER_SLICE; i++) {
		int cfg_idx = i + PRUETH_NUM_FWD_BUF_POOLS_PER_SLICE;

		/* The driver only uses first 4 queues per PRU,
		 * so only initialize buffer for them
		 */
		if ((i % PRUETH_NUM_LI_BUF_POOLS_PER_PORT_PER_SLICE)
			 < PRUETH_SW_USED_LI_BUF_POOLS_PER_PORT_PER_SLICE) {
			writel(addr, &bpool_cfg[cfg_idx].addr);
			writel(PRUETH_SW_LI_BUF_POOL_SIZE,
			       &bpool_cfg[cfg_idx].len);
			addr += PRUETH_SW_LI_BUF_POOL_SIZE;
		} else {
			writel(0, &bpool_cfg[cfg_idx].addr);
			writel(0, &bpool_cfg[cfg_idx].len);
		}
	}

	/* Express RX buffer queue
	 *  - used by firmware to store express packets to be transmitted
	 *    to the host core
	 */
	rxq_ctx = emac->dram.va + HOST_RX_Q_EXP_CONTEXT_OFFSET;
	for (i = 0; i < 3; i++)
		writel(addr, &rxq_ctx->start[i]);

	addr += PRUETH_SW_HOST_EXP_BUF_POOL_SIZE;
	writel(addr, &rxq_ctx->end);

	/* Pre-emptible RX buffer queue
	 *  - used by firmware to store preemptible packets to be transmitted
	 *    to the host core
	 */
	rxq_ctx = emac->dram.va + HOST_RX_Q_PRE_CONTEXT_OFFSET;
	for (i = 0; i < 3; i++)
		writel(addr, &rxq_ctx->start[i]);

	addr += PRUETH_SW_HOST_PRE_BUF_POOL_SIZE;
	writel(addr, &rxq_ctx->end);

	/* Set pointer for default dropped packet write
	 *  - used by firmware to temporarily store packet to be dropped
	 */
	rxq_ctx = emac->dram.va + DEFAULT_MSMC_Q_OFFSET;
	writel(addr, &rxq_ctx->start[0]);

	return 0;
}

static int prueth_emac_buffer_setup(struct prueth_emac *emac)
{
	struct icssg_buffer_pool_cfg __iomem *bpool_cfg;
	struct icssg_rxq_ctx __iomem *rxq_ctx;
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);
	u32 addr;
	int i;

	addr = lower_32_bits(prueth->msmcram.pa);
	if (slice) {
		if (prueth->pdata.banked_ms_ram)
			addr += MSMC_RAM_BANK_SIZE;
		else
			addr += PRUETH_EMAC_TOTAL_BUF_SIZE_PER_SLICE;
	}

	if (addr % SZ_64K) {
		dev_warn(prueth->dev, "buffer pool needs to be 64KB aligned\n");
		return -EINVAL;
	}

	bpool_cfg = emac->dram.va + BUFFER_POOL_0_ADDR_OFFSET;

	/* Configure buffer pools for forwarding buffers
	 *  - in mac mode - no forwarding so initialize all pools to 0
	 *  - 8 total pools per slice
	 */
	for (i = 0; i <  PRUETH_NUM_FWD_BUF_POOLS_PER_SLICE; i++) {
		writel(0, &bpool_cfg[i].addr);
		writel(0, &bpool_cfg[i].len);
	}

	/* Configure buffer pools for Local Injection buffers
	 *  - used by firmware to store packets received from host core
	 *  - 16 total pools per slice
	 */
	bpool_cfg = emac->dram.va + BUFFER_POOL_0_ADDR_OFFSET;
	for (i = 0; i < PRUETH_NUM_LI_BUF_POOLS_PER_SLICE; i++) {
		int cfg_idx = i + PRUETH_NUM_FWD_BUF_POOLS_PER_SLICE;

		/* In EMAC mode, only first 4 buffers are used,
		 * as 1 slice needs to handle only 1 port
		 */
		if (i < PRUETH_EMAC_USED_LI_BUF_POOLS_PER_PORT_PER_SLICE) {
			writel(addr, &bpool_cfg[cfg_idx].addr);
			writel(PRUETH_EMAC_LI_BUF_POOL_SIZE,
			       &bpool_cfg[cfg_idx].len);
			addr += PRUETH_EMAC_LI_BUF_POOL_SIZE;
		} else {
			writel(0, &bpool_cfg[cfg_idx].addr);
			writel(0, &bpool_cfg[cfg_idx].len);
		}
	}

	/* Express RX buffer queue
	 *  - used by firmware to store express packets to be transmitted
	 *    to host core
	 */
	rxq_ctx = emac->dram.va + HOST_RX_Q_EXP_CONTEXT_OFFSET;
	for (i = 0; i < 3; i++)
		writel(addr, &rxq_ctx->start[i]);

	addr += PRUETH_EMAC_HOST_EXP_BUF_POOL_SIZE;
	writel(addr, &rxq_ctx->end);

	/* Pre-emptible RX buffer queue
	 *  - used by firmware to store preemptible packets to be transmitted
	 *    to host core
	 */
	rxq_ctx = emac->dram.va + HOST_RX_Q_PRE_CONTEXT_OFFSET;
	for (i = 0; i < 3; i++)
		writel(addr, &rxq_ctx->start[i]);

	addr += PRUETH_EMAC_HOST_PRE_BUF_POOL_SIZE;
	writel(addr, &rxq_ctx->end);

	/* Set pointer for default dropped packet write
	 *  - used by firmware to temporarily store packet to be dropped
	 */
	rxq_ctx = emac->dram.va + DEFAULT_MSMC_Q_OFFSET;
	writel(addr, &rxq_ctx->start[0]);

	return 0;
}

void icssg_init_emac_mode(struct prueth *prueth)
{
	/* When the device is configured as a bridge and it is being brought
	 * back to the emac mode, the host mac address has to be set as 0.
	 */
	u32 addr = prueth->shram.pa + EMAC_ICSSG_SWITCH_DEFAULT_VLAN_TABLE_OFFSET;
	int i;
	u8 mac[ETH_ALEN] = { 0 };

	/* Set VLAN TABLE address base */
	regmap_update_bits(prueth->miig_rt, FDB_GEN_CFG1, SMEM_VLAN_OFFSET_MASK,
			   addr <<  SMEM_VLAN_OFFSET);
	regmap_update_bits(prueth->miig_rt, FDB_GEN_CFG1, FDB_HASH_SIZE_MASK,
			   FDB_HASH_SIZE << FDB_HASH_SIZE_SHIFT);
	/* Set enable VLAN aware mode, and FDBs for all PRUs */
	regmap_write(prueth->miig_rt, FDB_GEN_CFG2, (FDB_PRU0_EN | FDB_PRU1_EN | FDB_HOST_EN));
	prueth->vlan_tbl = (struct prueth_vlan_tbl __force *)(prueth->shram.va +
			    EMAC_ICSSG_SWITCH_DEFAULT_VLAN_TABLE_OFFSET);
	for (i = 0; i < SZ_4K - 1; i++) {
		prueth->vlan_tbl[i].fid = i;
		prueth->vlan_tbl[i].fid_c1 = 0;
	}
	/* Clear host MAC address */
	icssg_class_set_host_mac_addr(prueth->miig_rt, mac);
}
EXPORT_SYMBOL_GPL(icssg_init_emac_mode);

void icssg_init_fw_offload_mode(struct prueth *prueth)
{
	u32 addr = prueth->shram.pa + EMAC_ICSSG_SWITCH_DEFAULT_VLAN_TABLE_OFFSET;
	int i;

	/* Set VLAN TABLE address base */
	regmap_update_bits(prueth->miig_rt, FDB_GEN_CFG1, SMEM_VLAN_OFFSET_MASK,
			   addr <<  SMEM_VLAN_OFFSET);
	regmap_update_bits(prueth->miig_rt, FDB_GEN_CFG1, FDB_HASH_SIZE_MASK,
			   FDB_HASH_SIZE << FDB_HASH_SIZE_SHIFT);
	/* Set enable VLAN aware mode, and FDBs for all PRUs */
	regmap_write(prueth->miig_rt, FDB_GEN_CFG2, FDB_EN_ALL);
	prueth->vlan_tbl = (struct prueth_vlan_tbl __force *)(prueth->shram.va +
			    EMAC_ICSSG_SWITCH_DEFAULT_VLAN_TABLE_OFFSET);
	for (i = 0; i < SZ_4K - 1; i++) {
		prueth->vlan_tbl[i].fid = i;
		prueth->vlan_tbl[i].fid_c1 = 0;
	}

	if (prueth->hw_bridge_dev)
		icssg_class_set_host_mac_addr(prueth->miig_rt, prueth->hw_bridge_dev->dev_addr);
	icssg_set_pvid(prueth, prueth->default_vlan, PRUETH_PORT_HOST);
}
EXPORT_SYMBOL_GPL(icssg_init_fw_offload_mode);

int icssg_config(struct prueth *prueth, struct prueth_emac *emac, int slice)
{
	void __iomem *config = emac->dram.va + ICSSG_CONFIG_OFFSET;
	struct icssg_flow_cfg __iomem *flow_cfg;
	int ret;

	memset_io(config, 0, TAS_GATE_MASK_LIST0);
	icssg_miig_queues_init(prueth, slice);

	emac->speed = SPEED_1000;
	emac->duplex = DUPLEX_FULL;
	if (!phy_interface_mode_is_rgmii(emac->phy_if)) {
		emac->speed = SPEED_100;
		emac->duplex = DUPLEX_FULL;
	}
	regmap_update_bits(prueth->miig_rt, ICSSG_CFG_OFFSET,
			   ICSSG_CFG_DEFAULT, ICSSG_CFG_DEFAULT);
	icssg_miig_set_interface_mode(prueth->miig_rt, slice, emac->phy_if);
	if (prueth->is_switch_mode || prueth->is_hsr_offload_mode)
		icssg_config_mii_init_fw_offload(emac);
	else
		icssg_config_mii_init(emac);
	icssg_config_ipg(emac);
	icssg_update_rgmii_cfg(prueth->miig_rt, emac);

	/* set GPI mode */
	pruss_cfg_gpimode(prueth->pruss, prueth->pru_id[slice],
			  PRUSS_GPI_MODE_MII);

	/* enable XFR shift for PRU and RTU */
	pruss_cfg_xfr_enable(prueth->pruss, PRU_TYPE_PRU, true);
	pruss_cfg_xfr_enable(prueth->pruss, PRU_TYPE_RTU, true);

	/* set C28 to 0x100 */
	pru_rproc_set_ctable(prueth->pru[slice], PRU_C28, 0x100 << 8);
	pru_rproc_set_ctable(prueth->rtu[slice], PRU_C28, 0x100 << 8);
	pru_rproc_set_ctable(prueth->txpru[slice], PRU_C28, 0x100 << 8);

	flow_cfg = config + PSI_L_REGULAR_FLOW_ID_BASE_OFFSET;
	writew(emac->rx_flow_id_base, &flow_cfg->rx_base_flow);
	writew(0, &flow_cfg->mgm_base_flow);
	writeb(0, config + SPL_PKT_DEFAULT_PRIORITY);
	writeb(0, config + QUEUE_NUM_UNTAGGED);

	if (prueth->is_switch_mode || prueth->is_hsr_offload_mode)
		ret = prueth_fw_offload_buffer_setup(emac);
	else
		ret = prueth_emac_buffer_setup(emac);
	if (ret)
		return ret;

	emac_r30_cmd_init(emac);

	return 0;
}
EXPORT_SYMBOL_GPL(icssg_config);

/* Bitmask for ICSSG r30 commands */
static const struct icssg_r30_cmd emac_r32_bitmask[] = {
	{{0xffff0004, 0xffff0100, 0xffff0004, EMAC_NONE}},	/* EMAC_PORT_DISABLE */
	{{0xfffb0040, 0xfeff0200, 0xfeff0200, EMAC_NONE}},	/* EMAC_PORT_BLOCK */
	{{0xffbb0000, 0xfcff0000, 0xdcfb0000, EMAC_NONE}},	/* EMAC_PORT_FORWARD */
	{{0xffbb0000, 0xfcff0000, 0xfcff2000, EMAC_NONE}},	/* EMAC_PORT_FORWARD_WO_LEARNING */
	{{0xffff0001, EMAC_NONE,  EMAC_NONE, EMAC_NONE}},	/* ACCEPT ALL */
	{{0xfffe0002, EMAC_NONE,  EMAC_NONE, EMAC_NONE}},	/* ACCEPT TAGGED */
	{{0xfffc0000, EMAC_NONE,  EMAC_NONE, EMAC_NONE}},	/* ACCEPT UNTAGGED and PRIO */
	{{EMAC_NONE,  0xffff0020, EMAC_NONE, EMAC_NONE}},	/* TAS Trigger List change */
	{{EMAC_NONE,  0xdfff1000, EMAC_NONE, EMAC_NONE}},	/* TAS set state ENABLE*/
	{{EMAC_NONE,  0xefff2000, EMAC_NONE, EMAC_NONE}},	/* TAS set state RESET*/
	{{EMAC_NONE,  0xcfff0000, EMAC_NONE, EMAC_NONE}},	/* TAS set state DISABLE*/
	{{EMAC_NONE,  EMAC_NONE,  0xffff0400, EMAC_NONE}},	/* UC flooding ENABLE*/
	{{EMAC_NONE,  EMAC_NONE,  0xfbff0000, EMAC_NONE}},	/* UC flooding DISABLE*/
	{{EMAC_NONE,  EMAC_NONE,  0xffff0800, EMAC_NONE}},	/* MC flooding ENABLE*/
	{{EMAC_NONE,  EMAC_NONE,  0xf7ff0000, EMAC_NONE}},	/* MC flooding DISABLE*/
	{{EMAC_NONE,  0xffff4000, EMAC_NONE, EMAC_NONE}},	/* Preemption on Tx ENABLE*/
	{{EMAC_NONE,  0xbfff0000, EMAC_NONE, EMAC_NONE}},	/* Preemption on Tx DISABLE*/
	{{0xffff0010,  EMAC_NONE, 0xffff0010, EMAC_NONE}},	/* VLAN AWARE*/
	{{0xffef0000,  EMAC_NONE, 0xffef0000, EMAC_NONE}},	/* VLAN UNWARE*/
	{{0xffff2000, EMAC_NONE, EMAC_NONE, EMAC_NONE}},	/* HSR_RX_OFFLOAD_ENABLE */
	{{0xdfff0000, EMAC_NONE, EMAC_NONE, EMAC_NONE}}		/* HSR_RX_OFFLOAD_DISABLE */
};

int icssg_set_port_state(struct prueth_emac *emac,
			 enum icssg_port_state_cmd cmd)
{
	struct icssg_r30_cmd __iomem *p;
	int ret = -ETIMEDOUT;
	int done = 0;
	int i;

	p = emac->dram.va + MGR_R30_CMD_OFFSET;

	if (cmd >= ICSSG_EMAC_PORT_MAX_COMMANDS) {
		netdev_err(emac->ndev, "invalid port command\n");
		return -EINVAL;
	}

	/* only one command at a time allowed to firmware */
	mutex_lock(&emac->cmd_lock);

	for (i = 0; i < 4; i++)
		writel(emac_r32_bitmask[cmd].cmd[i], &p->cmd[i]);

	/* wait for done */
	ret = read_poll_timeout(emac_r30_is_done, done, done == 1,
				1000, 10000, false, emac);

	if (ret == -ETIMEDOUT)
		netdev_err(emac->ndev, "timeout waiting for command done\n");

	mutex_unlock(&emac->cmd_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(icssg_set_port_state);

void icssg_config_half_duplex(struct prueth_emac *emac)
{
	u32 val;

	if (!emac->half_duplex)
		return;

	val = get_random_u32();
	writel(val, emac->dram.va + HD_RAND_SEED_OFFSET);
}
EXPORT_SYMBOL_GPL(icssg_config_half_duplex);

void icssg_config_set_speed(struct prueth_emac *emac)
{
	u8 fw_speed;

	switch (emac->speed) {
	case SPEED_1000:
		fw_speed = FW_LINK_SPEED_1G;
		break;
	case SPEED_100:
		fw_speed = FW_LINK_SPEED_100M;
		break;
	case SPEED_10:
		fw_speed = FW_LINK_SPEED_10M;
		break;
	default:
		/* Other links speeds not supported */
		netdev_err(emac->ndev, "Unsupported link speed\n");
		return;
	}

	if (emac->duplex == DUPLEX_HALF)
		fw_speed |= FW_LINK_SPEED_HD;

	writeb(fw_speed, emac->dram.va + PORT_LINK_SPEED_OFFSET);
}
EXPORT_SYMBOL_GPL(icssg_config_set_speed);

int icssg_send_fdb_msg(struct prueth_emac *emac, struct mgmt_cmd *cmd,
		       struct mgmt_cmd_rsp *rsp)
{
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);
	int addr, ret;

	addr = icssg_queue_pop(prueth, slice == 0 ?
			       ICSSG_CMD_POP_SLICE0 : ICSSG_CMD_POP_SLICE1);
	if (addr < 0)
		return addr;

	/* First 4 bytes have FW owned buffer linking info which should
	 * not be touched
	 */
	memcpy_toio(prueth->shram.va + addr + 4, cmd, sizeof(*cmd));
	icssg_queue_push(prueth, slice == 0 ?
			 ICSSG_CMD_PUSH_SLICE0 : ICSSG_CMD_PUSH_SLICE1, addr);
	ret = read_poll_timeout(icssg_queue_pop, addr, addr >= 0,
				2000, 20000000, false, prueth, slice == 0 ?
				ICSSG_RSP_POP_SLICE0 : ICSSG_RSP_POP_SLICE1);
	if (ret) {
		netdev_err(emac->ndev, "Timedout sending HWQ message\n");
		return ret;
	}

	memcpy_fromio(rsp, prueth->shram.va + addr, sizeof(*rsp));
	/* Return buffer back for to pool */
	icssg_queue_push(prueth, slice == 0 ?
			 ICSSG_RSP_PUSH_SLICE0 : ICSSG_RSP_PUSH_SLICE1, addr);

	return 0;
}
EXPORT_SYMBOL_GPL(icssg_send_fdb_msg);

static void icssg_fdb_setup(struct prueth_emac *emac, struct mgmt_cmd *fdb_cmd,
			    const unsigned char *addr, u8 fid, int cmd)
{
	int slice = prueth_emac_slice(emac);
	u8 mac_fid[ETH_ALEN + 2];
	u16 fdb_slot;

	ether_addr_copy(mac_fid, addr);

	/* 1-1 VID-FID mapping is already setup */
	mac_fid[ETH_ALEN] = fid;
	mac_fid[ETH_ALEN + 1] = 0;

	fdb_slot = bitrev32(crc32_le(0, mac_fid, 8)) & PRUETH_SWITCH_FDB_MASK;

	fdb_cmd->header = ICSSG_FW_MGMT_CMD_HEADER;
	fdb_cmd->type   = ICSSG_FW_MGMT_FDB_CMD_TYPE;
	fdb_cmd->seqnum = ++(emac->prueth->icssg_hwcmdseq);
	fdb_cmd->param  = cmd;
	fdb_cmd->param |= (slice << 4);

	memcpy(&fdb_cmd->cmd_args[0], addr, 4);
	memcpy(&fdb_cmd->cmd_args[1], &addr[4], 2);
	fdb_cmd->cmd_args[2] = fdb_slot;

	netdev_dbg(emac->ndev, "MAC %pM slot %X FID %X\n", addr, fdb_slot, fid);
}

int icssg_fdb_add_del(struct prueth_emac *emac, const unsigned char *addr,
		      u8 vid, u8 fid_c2, bool add)
{
	struct mgmt_cmd_rsp fdb_cmd_rsp = { 0 };
	struct mgmt_cmd fdb_cmd = { 0 };
	u8 fid = vid;
	int ret;

	icssg_fdb_setup(emac, &fdb_cmd, addr, fid, add ? ICSS_CMD_ADD_FDB : ICSS_CMD_DEL_FDB);

	fid_c2 |= ICSSG_FDB_ENTRY_VALID;
	fdb_cmd.cmd_args[1] |= ((fid << 16) | (fid_c2 << 24));

	ret = icssg_send_fdb_msg(emac, &fdb_cmd, &fdb_cmd_rsp);
	if (ret)
		return ret;

	WARN_ON(fdb_cmd.seqnum != fdb_cmd_rsp.seqnum);
	if (fdb_cmd_rsp.status == 1)
		return 0;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(icssg_fdb_add_del);

int icssg_fdb_lookup(struct prueth_emac *emac, const unsigned char *addr,
		     u8 vid)
{
	struct mgmt_cmd_rsp fdb_cmd_rsp = { 0 };
	struct mgmt_cmd fdb_cmd = { 0 };
	struct prueth_fdb_slot *slot;
	u8 fid = vid;
	int ret, i;

	icssg_fdb_setup(emac, &fdb_cmd, addr, fid, ICSS_CMD_GET_FDB_SLOT);

	fdb_cmd.cmd_args[1] |= fid << 16;

	ret = icssg_send_fdb_msg(emac, &fdb_cmd, &fdb_cmd_rsp);
	if (ret)
		return ret;

	WARN_ON(fdb_cmd.seqnum != fdb_cmd_rsp.seqnum);

	slot = (struct prueth_fdb_slot __force *)(emac->dram.va + FDB_CMD_BUFFER);
	for (i = 0; i < 4; i++) {
		if (ether_addr_equal(addr, slot->mac) && vid == slot->fid)
			return (slot->fid_c2 & ~ICSSG_FDB_ENTRY_VALID);
		slot++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(icssg_fdb_lookup);

void icssg_vtbl_modify(struct prueth_emac *emac, u8 vid, u8 port_mask,
		       u8 untag_mask, bool add)
{
	struct prueth *prueth = emac->prueth;
	struct prueth_vlan_tbl *tbl;
	u8 fid_c1;

	tbl = prueth->vlan_tbl;
	spin_lock(&prueth->vtbl_lock);
	fid_c1 = tbl[vid].fid_c1;

	/* FID_C1: bit0..2 port membership mask,
	 * bit3..5 tagging mask for each port
	 * bit6 Stream VID (not handled currently)
	 * bit7 MC flood (not handled currently)
	 */
	if (add) {
		fid_c1 |= (port_mask | port_mask << 3);
		fid_c1 &= ~(untag_mask << 3);
	} else {
		fid_c1 &= ~(port_mask | port_mask << 3);
	}

	tbl[vid].fid_c1 = fid_c1;
	spin_unlock(&prueth->vtbl_lock);
}
EXPORT_SYMBOL_GPL(icssg_vtbl_modify);

u16 icssg_get_pvid(struct prueth_emac *emac)
{
	struct prueth *prueth = emac->prueth;
	u32 pvid;

	if (emac->port_id == PRUETH_PORT_MII0)
		pvid = readl(prueth->shram.va + EMAC_ICSSG_SWITCH_PORT1_DEFAULT_VLAN_OFFSET);
	else
		pvid = readl(prueth->shram.va + EMAC_ICSSG_SWITCH_PORT2_DEFAULT_VLAN_OFFSET);

	pvid = pvid >> 24;

	return pvid;
}
EXPORT_SYMBOL_GPL(icssg_get_pvid);

void icssg_set_pvid(struct prueth *prueth, u8 vid, u8 port)
{
	u32 pvid;

	/* only 256 VLANs are supported */
	pvid = (u32 __force)cpu_to_be32((ETH_P_8021Q << 16) | (vid & 0xff));

	if (port == PRUETH_PORT_MII0)
		writel(pvid, prueth->shram.va + EMAC_ICSSG_SWITCH_PORT1_DEFAULT_VLAN_OFFSET);
	else if (port == PRUETH_PORT_MII1)
		writel(pvid, prueth->shram.va + EMAC_ICSSG_SWITCH_PORT2_DEFAULT_VLAN_OFFSET);
	else
		writel(pvid, prueth->shram.va + EMAC_ICSSG_SWITCH_PORT0_DEFAULT_VLAN_OFFSET);
}
EXPORT_SYMBOL_GPL(icssg_set_pvid);

int emac_fdb_flow_id_updated(struct prueth_emac *emac)
{
	struct mgmt_cmd_rsp fdb_cmd_rsp = { 0 };
	int slice = prueth_emac_slice(emac);
	struct mgmt_cmd fdb_cmd = { 0 };
	int ret;

	fdb_cmd.header = ICSSG_FW_MGMT_CMD_HEADER;
	fdb_cmd.type   = ICSSG_FW_MGMT_FDB_CMD_TYPE_RX_FLOW;
	fdb_cmd.seqnum = ++(emac->prueth->icssg_hwcmdseq);
	fdb_cmd.param  = 0;

	fdb_cmd.param |= (slice << 4);
	fdb_cmd.cmd_args[0] = 0;

	ret = icssg_send_fdb_msg(emac, &fdb_cmd, &fdb_cmd_rsp);
	if (ret)
		return ret;

	WARN_ON(fdb_cmd.seqnum != fdb_cmd_rsp.seqnum);
	return fdb_cmd_rsp.status == 1 ? 0 : -EINVAL;
}
EXPORT_SYMBOL_GPL(emac_fdb_flow_id_updated);
