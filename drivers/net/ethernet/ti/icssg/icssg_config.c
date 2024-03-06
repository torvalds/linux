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

static void icssg_config_mii_init(struct prueth_emac *emac)
{
	u32 rxcfg, txcfg, rxcfg_reg, txcfg_reg, pcnt_reg;
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);
	struct regmap *mii_rt;

	mii_rt = prueth->mii_rt;

	rxcfg_reg = (slice == ICSS_MII0) ? PRUSS_MII_RT_RXCFG0 :
				       PRUSS_MII_RT_RXCFG1;
	txcfg_reg = (slice == ICSS_MII0) ? PRUSS_MII_RT_TXCFG0 :
				       PRUSS_MII_RT_TXCFG1;
	pcnt_reg = (slice == ICSS_MII0) ? PRUSS_MII_RT_RX_PCNT0 :
				       PRUSS_MII_RT_RX_PCNT1;

	rxcfg = MII_RXCFG_DEFAULT;
	txcfg = MII_TXCFG_DEFAULT;

	if (slice == ICSS_MII1)
		rxcfg |= PRUSS_MII_RT_RXCFG_RX_MUX_SEL;

	/* In MII mode TX lines swapped inside ICSSG, so TX_MUX_SEL cfg need
	 * to be swapped also comparing to RGMII mode.
	 */
	if (emac->phy_if == PHY_INTERFACE_MODE_MII && slice == ICSS_MII0)
		txcfg |= PRUSS_MII_RT_TXCFG_TX_MUX_SEL;
	else if (emac->phy_if != PHY_INTERFACE_MODE_MII && slice == ICSS_MII1)
		txcfg |= PRUSS_MII_RT_TXCFG_TX_MUX_SEL;

	regmap_write(mii_rt, rxcfg_reg, rxcfg);
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

	switch (emac->speed) {
	case SPEED_1000:
		icssg_mii_update_ipg(prueth->mii_rt, slice, MII_RT_TX_IPG_1G);
		break;
	case SPEED_100:
		icssg_mii_update_ipg(prueth->mii_rt, slice, MII_RT_TX_IPG_100M);
		break;
	case SPEED_10:
		/* IPG for 10M is same as 100M */
		icssg_mii_update_ipg(prueth->mii_rt, slice, MII_RT_TX_IPG_100M);
		break;
	default:
		/* Other links speeds not supported */
		netdev_err(emac->ndev, "Unsupported link speed\n");
		return;
	}
}

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

static int prueth_emac_buffer_setup(struct prueth_emac *emac)
{
	struct icssg_buffer_pool_cfg __iomem *bpool_cfg;
	struct icssg_rxq_ctx __iomem *rxq_ctx;
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);
	u32 addr;
	int i;

	/* Layout to have 64KB aligned buffer pool
	 * |BPOOL0|BPOOL1|RX_CTX0|RX_CTX1|
	 */

	addr = lower_32_bits(prueth->msmcram.pa);
	if (slice)
		addr += PRUETH_NUM_BUF_POOLS * PRUETH_EMAC_BUF_POOL_SIZE;

	if (addr % SZ_64K) {
		dev_warn(prueth->dev, "buffer pool needs to be 64KB aligned\n");
		return -EINVAL;
	}

	bpool_cfg = emac->dram.va + BUFFER_POOL_0_ADDR_OFFSET;
	/* workaround for f/w bug. bpool 0 needs to be initilalized */
	writel(addr, &bpool_cfg[0].addr);
	writel(0, &bpool_cfg[0].len);

	for (i = PRUETH_EMAC_BUF_POOL_START;
	     i < PRUETH_EMAC_BUF_POOL_START + PRUETH_NUM_BUF_POOLS;
	     i++) {
		writel(addr, &bpool_cfg[i].addr);
		writel(PRUETH_EMAC_BUF_POOL_SIZE, &bpool_cfg[i].len);
		addr += PRUETH_EMAC_BUF_POOL_SIZE;
	}

	if (!slice)
		addr += PRUETH_NUM_BUF_POOLS * PRUETH_EMAC_BUF_POOL_SIZE;
	else
		addr += PRUETH_EMAC_RX_CTX_BUF_SIZE * 2;

	/* Pre-emptible RX buffer queue */
	rxq_ctx = emac->dram.va + HOST_RX_Q_PRE_CONTEXT_OFFSET;
	for (i = 0; i < 3; i++)
		writel(addr, &rxq_ctx->start[i]);

	addr += PRUETH_EMAC_RX_CTX_BUF_SIZE;
	writel(addr, &rxq_ctx->end);

	/* Express RX buffer queue */
	rxq_ctx = emac->dram.va + HOST_RX_Q_EXP_CONTEXT_OFFSET;
	for (i = 0; i < 3; i++)
		writel(addr, &rxq_ctx->start[i]);

	addr += PRUETH_EMAC_RX_CTX_BUF_SIZE;
	writel(addr, &rxq_ctx->end);

	return 0;
}

static void icssg_init_emac_mode(struct prueth *prueth)
{
	/* When the device is configured as a bridge and it is being brought
	 * back to the emac mode, the host mac address has to be set as 0.
	 */
	u8 mac[ETH_ALEN] = { 0 };

	if (prueth->emacs_initialized)
		return;

	regmap_update_bits(prueth->miig_rt, FDB_GEN_CFG1,
			   SMEM_VLAN_OFFSET_MASK, 0);
	regmap_write(prueth->miig_rt, FDB_GEN_CFG2, 0);
	/* Clear host MAC address */
	icssg_class_set_host_mac_addr(prueth->miig_rt, mac);
}

int icssg_config(struct prueth *prueth, struct prueth_emac *emac, int slice)
{
	void __iomem *config = emac->dram.va + ICSSG_CONFIG_OFFSET;
	struct icssg_flow_cfg __iomem *flow_cfg;
	int ret;

	icssg_init_emac_mode(prueth);

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

	ret = prueth_emac_buffer_setup(emac);
	if (ret)
		return ret;

	emac_r30_cmd_init(emac);

	return 0;
}

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
	{{0xffef0000,  EMAC_NONE, 0xffef0000, EMAC_NONE}}	/* VLAN UNWARE*/
};

int emac_set_port_state(struct prueth_emac *emac,
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

void icssg_config_half_duplex(struct prueth_emac *emac)
{
	u32 val;

	if (!emac->half_duplex)
		return;

	val = get_random_u32();
	writel(val, emac->dram.va + HD_RAND_SEED_OFFSET);
}

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
