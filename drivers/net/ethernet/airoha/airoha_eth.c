// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 AIROHA Inc
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/tcp.h>
#include <linux/u64_stats_sync.h>
#include <net/dst_metadata.h>
#include <net/page_pool/helpers.h>
#include <net/pkt_cls.h>
#include <uapi/linux/ppp_defs.h>

#include "airoha_regs.h"
#include "airoha_eth.h"

u32 airoha_rr(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

void airoha_wr(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + offset);
}

u32 airoha_rmw(void __iomem *base, u32 offset, u32 mask, u32 val)
{
	val |= (airoha_rr(base, offset) & ~mask);
	airoha_wr(base, offset, val);

	return val;
}

static void airoha_qdma_set_irqmask(struct airoha_irq_bank *irq_bank,
				    int index, u32 clear, u32 set)
{
	struct airoha_qdma *qdma = irq_bank->qdma;
	int bank = irq_bank - &qdma->irq_banks[0];
	unsigned long flags;

	if (WARN_ON_ONCE(index >= ARRAY_SIZE(irq_bank->irqmask)))
		return;

	spin_lock_irqsave(&irq_bank->irq_lock, flags);

	irq_bank->irqmask[index] &= ~clear;
	irq_bank->irqmask[index] |= set;
	airoha_qdma_wr(qdma, REG_INT_ENABLE(bank, index),
		       irq_bank->irqmask[index]);
	/* Read irq_enable register in order to guarantee the update above
	 * completes in the spinlock critical section.
	 */
	airoha_qdma_rr(qdma, REG_INT_ENABLE(bank, index));

	spin_unlock_irqrestore(&irq_bank->irq_lock, flags);
}

static void airoha_qdma_irq_enable(struct airoha_irq_bank *irq_bank,
				   int index, u32 mask)
{
	airoha_qdma_set_irqmask(irq_bank, index, 0, mask);
}

static void airoha_qdma_irq_disable(struct airoha_irq_bank *irq_bank,
				    int index, u32 mask)
{
	airoha_qdma_set_irqmask(irq_bank, index, mask, 0);
}

static int airoha_set_macaddr(struct airoha_gdm_dev *dev, const u8 *addr)
{
	u8 ref_addr[ETH_ALEN] __aligned(2);
	struct airoha_eth *eth = dev->eth;
	u32 reg, val, lmin, lmax;
	int i;

	eth_zero_addr(ref_addr);
	lmin = (addr[3] << 16) | (addr[4] << 8) | addr[5];
	lmax = lmin;

	for (i = 0; i < ARRAY_SIZE(eth->ports); i++) {
		struct airoha_gdm_port *port = eth->ports[i];
		int j;

		if (!port)
			continue;

		for (j = 0; j < ARRAY_SIZE(port->devs); j++) {
			struct airoha_gdm_dev *iter_dev;
			struct net_device *netdev;

			iter_dev = port->devs[j];
			if (!iter_dev || iter_dev == dev)
				continue;

			if (airoha_is_lan_gdm_dev(iter_dev) !=
			    airoha_is_lan_gdm_dev(dev))
				continue;

			netdev = netdev_from_priv(iter_dev);
			if (netdev->reg_state != NETREG_REGISTERED)
				continue;

			ether_addr_copy(ref_addr, netdev->dev_addr);
			val = (netdev->dev_addr[3] << 16) |
			      (netdev->dev_addr[4] << 8) | netdev->dev_addr[5];
			if (val < lmin)
				lmin = val;
			if (val > lmax)
				lmax = val;
		}
	}

	if (!is_zero_ether_addr(ref_addr) && memcmp(ref_addr, addr, 3)) {
		/* According to the HW design, hw mac address MSBs must be
		 * the same for each net_device with the same LAN/WAN
		 * configuration.
		 */
		struct net_device *netdev = netdev_from_priv(dev);

		dev_warn(eth->dev,
			 "%s: wrong mac addr, MSBs must be %02x:%02x:%02x\n",
			 netdev->name, ref_addr[0], ref_addr[1],
			 ref_addr[2]);
		dev_warn(eth->dev, "FE hw forwarding won't work properly\n");

		return -EINVAL;
	}

	reg = airoha_is_lan_gdm_dev(dev) ? REG_FE_LAN_MAC_H : REG_FE_WAN_MAC_H;
	val = (addr[0] << 16) | (addr[1] << 8) | addr[2];
	airoha_fe_wr(eth, reg, val);

	airoha_fe_wr(eth, REG_FE_MAC_LMIN(reg), lmin);
	airoha_fe_wr(eth, REG_FE_MAC_LMAX(reg), lmax);

	airoha_ppe_init_upd_mem(dev, addr);

	return 0;
}

static void airoha_set_gdm_port_fwd_cfg(struct airoha_eth *eth, u32 addr,
					u32 val)
{
	airoha_fe_rmw(eth, addr, GDM_OCFQ_MASK,
		      FIELD_PREP(GDM_OCFQ_MASK, val));
	airoha_fe_rmw(eth, addr, GDM_MCFQ_MASK,
		      FIELD_PREP(GDM_MCFQ_MASK, val));
	airoha_fe_rmw(eth, addr, GDM_BCFQ_MASK,
		      FIELD_PREP(GDM_BCFQ_MASK, val));
	airoha_fe_rmw(eth, addr, GDM_UCFQ_MASK,
		      FIELD_PREP(GDM_UCFQ_MASK, val));
}

static int airoha_set_vip_for_gdm_port(struct airoha_gdm_dev *dev, bool enable)
{
	struct airoha_gdm_port *port = dev->port;
	struct airoha_eth *eth = dev->eth;
	u32 vip_port;

	vip_port = eth->soc->ops.get_vip_port(port, dev->nbq);
	if (enable) {
		airoha_fe_set(eth, REG_FE_VIP_PORT_EN, vip_port);
		airoha_fe_set(eth, REG_FE_IFC_PORT_EN, vip_port);
	} else {
		airoha_fe_clear(eth, REG_FE_VIP_PORT_EN, vip_port);
		airoha_fe_clear(eth, REG_FE_IFC_PORT_EN, vip_port);
	}

	return 0;
}

static void airoha_fe_maccr_init(struct airoha_eth *eth)
{
	int p;

	for (p = 1; p <= ARRAY_SIZE(eth->ports); p++) {
		airoha_fe_set(eth, REG_GDM_FWD_CFG(p),
			      GDM_TCP_CKSUM_MASK | GDM_UDP_CKSUM_MASK |
			      GDM_IP4_CKSUM_MASK | GDM_DROP_CRC_ERR_MASK);
		airoha_fe_rmw(eth, REG_GDM_LEN_CFG(p),
			      GDM_SHORT_LEN_MASK | GDM_LONG_LEN_MASK,
			      FIELD_PREP(GDM_SHORT_LEN_MASK, 60) |
			      FIELD_PREP(GDM_LONG_LEN_MASK, AIROHA_MAX_RX_SIZE));
	}

	airoha_fe_rmw(eth, REG_CDM_VLAN_CTRL(1), CDM_VLAN_MASK,
		      FIELD_PREP(CDM_VLAN_MASK, 0x8100));

	airoha_fe_set(eth, REG_FE_CPORT_CFG, FE_CPORT_PAD);
}

static void airoha_fe_vip_setup(struct airoha_eth *eth)
{
	airoha_fe_wr(eth, REG_FE_VIP_PATN(3), ETH_P_PPP_DISC);
	airoha_fe_wr(eth, REG_FE_VIP_EN(3), PATN_FCPU_EN_MASK | PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(4), PPP_LCP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(4),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(6), PPP_IPCP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(6),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(7), PPP_CHAP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(7),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	/* BOOTP (0x43) */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(8), 0x43);
	airoha_fe_wr(eth, REG_FE_VIP_EN(8),
		     PATN_FCPU_EN_MASK | PATN_SP_EN_MASK |
		     FIELD_PREP(PATN_TYPE_MASK, 4) | PATN_EN_MASK);

	/* BOOTP (0x44) */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(9), 0x44);
	airoha_fe_wr(eth, REG_FE_VIP_EN(9),
		     PATN_FCPU_EN_MASK | PATN_SP_EN_MASK |
		     FIELD_PREP(PATN_TYPE_MASK, 4) | PATN_EN_MASK);

	/* ISAKMP */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(10), 0x1f401f4);
	airoha_fe_wr(eth, REG_FE_VIP_EN(10),
		     PATN_FCPU_EN_MASK | PATN_DP_EN_MASK | PATN_SP_EN_MASK |
		     FIELD_PREP(PATN_TYPE_MASK, 4) | PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(11), PPP_IPV6CP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(11),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	/* DHCPv6 */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(12), 0x2220223);
	airoha_fe_wr(eth, REG_FE_VIP_EN(12),
		     PATN_FCPU_EN_MASK | PATN_DP_EN_MASK | PATN_SP_EN_MASK |
		     FIELD_PREP(PATN_TYPE_MASK, 4) | PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(19), PPP_PAP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(19),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	/* ETH->ETH_P_1905 (0x893a) */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(20), 0x893a);
	airoha_fe_wr(eth, REG_FE_VIP_EN(20),
		     PATN_FCPU_EN_MASK | PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(21), ETH_P_LLDP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(21),
		     PATN_FCPU_EN_MASK | PATN_EN_MASK);
}

static u32 airoha_fe_get_pse_queue_rsv_pages(struct airoha_eth *eth,
					     u32 port, u32 queue)
{
	airoha_fe_rmw(eth, REG_FE_PSE_QUEUE_CFG_WR,
		      PSE_CFG_PORT_ID_MASK | PSE_CFG_QUEUE_ID_MASK,
		      FIELD_PREP(PSE_CFG_PORT_ID_MASK, port) |
		      FIELD_PREP(PSE_CFG_QUEUE_ID_MASK, queue));

	return airoha_fe_get(eth, REG_FE_PSE_QUEUE_CFG_VAL,
			     PSE_CFG_OQ_RSV_MASK);
}

static void airoha_fe_set_pse_queue_rsv_pages(struct airoha_eth *eth,
					      u32 port, u32 queue, u32 val)
{
	airoha_fe_rmw(eth, REG_FE_PSE_QUEUE_CFG_VAL, PSE_CFG_OQ_RSV_MASK,
		      FIELD_PREP(PSE_CFG_OQ_RSV_MASK, val));
	airoha_fe_rmw(eth, REG_FE_PSE_QUEUE_CFG_WR,
		      PSE_CFG_PORT_ID_MASK | PSE_CFG_QUEUE_ID_MASK |
		      PSE_CFG_WR_EN_MASK | PSE_CFG_OQRSV_SEL_MASK,
		      FIELD_PREP(PSE_CFG_PORT_ID_MASK, port) |
		      FIELD_PREP(PSE_CFG_QUEUE_ID_MASK, queue) |
		      PSE_CFG_WR_EN_MASK | PSE_CFG_OQRSV_SEL_MASK);
}

static u32 airoha_fe_get_pse_all_rsv(struct airoha_eth *eth)
{
	return airoha_fe_get(eth, REG_FE_PSE_BUF_SET, PSE_ALLRSV_MASK);
}

static int airoha_fe_set_pse_oq_rsv(struct airoha_eth *eth,
				    u32 port, u32 queue, u32 val)
{
	u32 orig_val = airoha_fe_get_pse_queue_rsv_pages(eth, port, queue);
	u32 tmp, all_rsv, fq_limit;

	airoha_fe_set_pse_queue_rsv_pages(eth, port, queue, val);

	/* modify all rsv */
	all_rsv = airoha_fe_get_pse_all_rsv(eth);
	all_rsv += (val - orig_val);
	airoha_fe_rmw(eth, REG_FE_PSE_BUF_SET, PSE_ALLRSV_MASK,
		      FIELD_PREP(PSE_ALLRSV_MASK, all_rsv));

	/* modify hthd */
	fq_limit = airoha_fe_get(eth, PSE_FQ_CFG, PSE_FQ_LIMIT_MASK);
	tmp = fq_limit - all_rsv - 0x20;
	airoha_fe_rmw(eth, REG_PSE_SHARE_USED_THD,
		      PSE_SHARE_USED_HTHD_MASK,
		      FIELD_PREP(PSE_SHARE_USED_HTHD_MASK, tmp));

	tmp = fq_limit - all_rsv - 0x100;
	airoha_fe_rmw(eth, REG_PSE_SHARE_USED_THD,
		      PSE_SHARE_USED_MTHD_MASK,
		      FIELD_PREP(PSE_SHARE_USED_MTHD_MASK, tmp));
	tmp = (3 * tmp) >> 2;
	airoha_fe_rmw(eth, REG_FE_PSE_BUF_SET,
		      PSE_SHARE_USED_LTHD_MASK,
		      FIELD_PREP(PSE_SHARE_USED_LTHD_MASK, tmp));

	return 0;
}

static void airoha_fe_pse_ports_init(struct airoha_eth *eth)
{
	const u32 pse_port_num_queues[] = {
		[FE_PSE_PORT_CDM1] = 6,
		[FE_PSE_PORT_GDM1] = 6,
		[FE_PSE_PORT_GDM2] = 32,
		[FE_PSE_PORT_GDM3] = 6,
		[FE_PSE_PORT_PPE1] = 4,
		[FE_PSE_PORT_CDM2] = 6,
		[FE_PSE_PORT_CDM3] = 8,
		[FE_PSE_PORT_CDM4] = 10,
		[FE_PSE_PORT_PPE2] = 4,
		[FE_PSE_PORT_GDM4] = 2,
		[FE_PSE_PORT_CDM5] = 2,
	};
	int q;

	if (airoha_ppe_is_enabled(eth, 1)) {
		u32 all_rsv;

		/* hw misses PPE2 oq rsv */
		all_rsv = airoha_fe_get_pse_all_rsv(eth);
		all_rsv += PSE_RSV_PAGES *
			   pse_port_num_queues[FE_PSE_PORT_PPE2];
		airoha_fe_rmw(eth, REG_FE_PSE_BUF_SET, PSE_ALLRSV_MASK,
			      FIELD_PREP(PSE_ALLRSV_MASK, all_rsv));
	}

	/* CDM1 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_CDM1]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM1, q,
					 PSE_QUEUE_RSV_PAGES);
	/* GDM1 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_GDM1]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_GDM1, q,
					 PSE_QUEUE_RSV_PAGES);
	/* GDM2 */
	for (q = 6; q < pse_port_num_queues[FE_PSE_PORT_GDM2]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_GDM2, q, 0);
	/* GDM3 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_GDM3]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_GDM3, q,
					 PSE_QUEUE_RSV_PAGES);
	/* PPE1 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_PPE1]; q++) {
		if (q < pse_port_num_queues[FE_PSE_PORT_PPE1] / 2)
			airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_PPE1, q,
						 PSE_QUEUE_RSV_PAGES);
		else
			airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_PPE1, q, 0);
	}
	/* CDM2 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_CDM2]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM2, q,
					 PSE_QUEUE_RSV_PAGES);
	/* CDM3 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_CDM3] - 1; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM3, q, 0);
	/* CDM4 */
	for (q = 4; q < pse_port_num_queues[FE_PSE_PORT_CDM4]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM4, q,
					 PSE_QUEUE_RSV_PAGES);
	if (airoha_ppe_is_enabled(eth, 1)) {
		/* PPE2 */
		for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_PPE2]; q++) {
			if (q < pse_port_num_queues[FE_PSE_PORT_PPE2] / 2)
				airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_PPE2,
							 q,
							 PSE_QUEUE_RSV_PAGES);
			else
				airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_PPE2,
							 q, 0);
		}
	}
	/* GDM4 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_GDM4]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_GDM4, q,
					 PSE_QUEUE_RSV_PAGES);
	/* CDM5 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_CDM5]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM5, q,
					 PSE_QUEUE_RSV_PAGES);
}

static int airoha_fe_mc_vlan_clear(struct airoha_eth *eth)
{
	int i;

	for (i = 0; i < AIROHA_FE_MC_MAX_VLAN_TABLE; i++) {
		int err, j;
		u32 val;

		airoha_fe_wr(eth, REG_MC_VLAN_DATA, 0x0);

		val = FIELD_PREP(MC_VLAN_CFG_TABLE_ID_MASK, i) |
		      MC_VLAN_CFG_TABLE_SEL_MASK | MC_VLAN_CFG_RW_MASK;
		airoha_fe_wr(eth, REG_MC_VLAN_CFG, val);
		err = read_poll_timeout(airoha_fe_rr, val,
					val & MC_VLAN_CFG_CMD_DONE_MASK,
					USEC_PER_MSEC, 5 * USEC_PER_MSEC,
					false, eth, REG_MC_VLAN_CFG);
		if (err)
			return err;

		for (j = 0; j < AIROHA_FE_MC_MAX_VLAN_PORT; j++) {
			airoha_fe_wr(eth, REG_MC_VLAN_DATA, 0x0);

			val = FIELD_PREP(MC_VLAN_CFG_TABLE_ID_MASK, i) |
			      FIELD_PREP(MC_VLAN_CFG_PORT_ID_MASK, j) |
			      MC_VLAN_CFG_RW_MASK;
			airoha_fe_wr(eth, REG_MC_VLAN_CFG, val);
			err = read_poll_timeout(airoha_fe_rr, val,
						val & MC_VLAN_CFG_CMD_DONE_MASK,
						USEC_PER_MSEC,
						5 * USEC_PER_MSEC, false, eth,
						REG_MC_VLAN_CFG);
			if (err)
				return err;
		}
	}

	return 0;
}

static void airoha_fe_crsn_qsel_init(struct airoha_eth *eth)
{
	/* CDM1_CRSN_QSEL */
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(1, CRSN_22 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_22),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_22),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(1, CRSN_08 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_08),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_08),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(1, CRSN_21 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_21),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_21),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(1, CRSN_24 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_24),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_24),
				 CDM_CRSN_QSEL_Q6));
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(1, CRSN_25 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_25),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_25),
				 CDM_CRSN_QSEL_Q1));
	/* CDM2_CRSN_QSEL */
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(2, CRSN_08 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_08),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_08),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(2, CRSN_21 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_21),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_21),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(2, CRSN_22 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_22),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_22),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(2, CRSN_24 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_24),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_24),
				 CDM_CRSN_QSEL_Q6));
	airoha_fe_rmw(eth, REG_CDM_CRSN_QSEL(2, CRSN_25 >> 2),
		      CDM_CRSN_QSEL_REASON_MASK(CRSN_25),
		      FIELD_PREP(CDM_CRSN_QSEL_REASON_MASK(CRSN_25),
				 CDM_CRSN_QSEL_Q1));
}

static int airoha_fe_init(struct airoha_eth *eth)
{
	airoha_fe_maccr_init(eth);

	/* PSE IQ reserve */
	airoha_fe_rmw(eth, REG_PSE_IQ_REV1, PSE_IQ_RES1_P2_MASK,
		      FIELD_PREP(PSE_IQ_RES1_P2_MASK, 0x10));
	airoha_fe_rmw(eth, REG_PSE_IQ_REV2,
		      PSE_IQ_RES2_P5_MASK | PSE_IQ_RES2_P4_MASK,
		      FIELD_PREP(PSE_IQ_RES2_P5_MASK, 0x40) |
		      FIELD_PREP(PSE_IQ_RES2_P4_MASK, 0x34));

	/* enable FE copy engine for KA/DPI */
	airoha_fe_wr(eth, REG_FE_PCE_CFG, PCE_DPI_EN_MASK | PCE_KA_EN_MASK);
	/* set vip queue selection to ring 1 */
	airoha_fe_rmw(eth, REG_CDM_FWD_CFG(1), CDM_VIP_QSEL_MASK,
		      FIELD_PREP(CDM_VIP_QSEL_MASK, 0x4));
	airoha_fe_rmw(eth, REG_CDM_FWD_CFG(2), CDM_VIP_QSEL_MASK,
		      FIELD_PREP(CDM_VIP_QSEL_MASK, 0x4));
	/* set GDM4 source interface offset to 8 */
	airoha_fe_rmw(eth, REG_GDM_SRC_PORT_SET(4),
		      GDM_SPORT_OFF2_MASK |
		      GDM_SPORT_OFF1_MASK |
		      GDM_SPORT_OFF0_MASK,
		      FIELD_PREP(GDM_SPORT_OFF2_MASK, 8) |
		      FIELD_PREP(GDM_SPORT_OFF1_MASK, 8) |
		      FIELD_PREP(GDM_SPORT_OFF0_MASK, 8));

	/* set PSE Page as 128B */
	airoha_fe_rmw(eth, REG_FE_DMA_GLO_CFG,
		      FE_DMA_GLO_L2_SPACE_MASK | FE_DMA_GLO_PG_SZ_MASK,
		      FIELD_PREP(FE_DMA_GLO_L2_SPACE_MASK, 2) |
		      FE_DMA_GLO_PG_SZ_MASK);
	airoha_fe_wr(eth, REG_FE_RST_GLO_CFG,
		     FE_RST_CORE_MASK | FE_RST_GDM3_MBI_ARB_MASK |
		     FE_RST_GDM4_MBI_ARB_MASK);
	usleep_range(1000, 2000);

	/* connect RxRing1 and RxRing15 to PSE Port0 OQ-1
	 * connect other rings to PSE Port0 OQ-0
	 */
	airoha_fe_wr(eth, REG_FE_CDM1_OQ_MAP0, BIT(4));
	airoha_fe_wr(eth, REG_FE_CDM1_OQ_MAP1, BIT(28));
	airoha_fe_wr(eth, REG_FE_CDM1_OQ_MAP2, BIT(4));
	airoha_fe_wr(eth, REG_FE_CDM1_OQ_MAP3, BIT(28));

	airoha_fe_vip_setup(eth);
	airoha_fe_pse_ports_init(eth);

	airoha_fe_set(eth, REG_GDM_MISC_CFG,
		      GDM2_RDM_ACK_WAIT_PREF_MASK |
		      GDM2_CHN_VLD_MODE_MASK);
	airoha_fe_rmw(eth, REG_CDM_FWD_CFG(2), CDM_OAM_QSEL_MASK,
		      FIELD_PREP(CDM_OAM_QSEL_MASK, 15));

	/* init fragment and assemble Force Port */
	/* NPU Core-3, NPU Bridge Channel-3 */
	airoha_fe_rmw(eth, REG_IP_FRAG_FP,
		      IP_FRAGMENT_PORT_MASK | IP_FRAGMENT_NBQ_MASK,
		      FIELD_PREP(IP_FRAGMENT_PORT_MASK, 6) |
		      FIELD_PREP(IP_FRAGMENT_NBQ_MASK, 3));
	/* QDMA LAN, RX Ring-22 */
	airoha_fe_rmw(eth, REG_IP_FRAG_FP,
		      IP_ASSEMBLE_PORT_MASK | IP_ASSEMBLE_NBQ_MASK,
		      FIELD_PREP(IP_ASSEMBLE_PORT_MASK, 0) |
		      FIELD_PREP(IP_ASSEMBLE_NBQ_MASK, 22));

	airoha_fe_set(eth, REG_GDM_FWD_CFG(AIROHA_GDM3_IDX), GDM_PAD_EN_MASK);
	airoha_fe_set(eth, REG_GDM_FWD_CFG(AIROHA_GDM4_IDX), GDM_PAD_EN_MASK);

	/* Enable split for MIB counters for GDM3 and GDM4 */
	airoha_fe_set(eth, REG_FE_GDM_MIB_CFG(AIROHA_GDM3_IDX),
		      FE_GDM_TX_MIB_SPLIT_EN_MASK |
		      FE_GDM_RX_MIB_SPLIT_EN_MASK);
	airoha_fe_set(eth, REG_FE_GDM_MIB_CFG(AIROHA_GDM4_IDX),
		      FE_GDM_TX_MIB_SPLIT_EN_MASK |
		      FE_GDM_RX_MIB_SPLIT_EN_MASK);

	airoha_fe_crsn_qsel_init(eth);

	airoha_fe_clear(eth, REG_FE_CPORT_CFG, FE_CPORT_QUEUE_XFC_MASK);
	airoha_fe_set(eth, REG_FE_CPORT_CFG, FE_CPORT_PORT_XFC_MASK);

	/* default aging mode for mbi unlock issue */
	airoha_fe_rmw(eth, REG_GDM_CHN_RLS(2),
		      MBI_RX_AGE_SEL_MASK | MBI_TX_AGE_SEL_MASK,
		      FIELD_PREP(MBI_RX_AGE_SEL_MASK, 3) |
		      FIELD_PREP(MBI_TX_AGE_SEL_MASK, 3));

	/* disable IFC by default */
	airoha_fe_clear(eth, REG_FE_CSR_IFC_CFG, FE_IFC_EN_MASK);

	/* enable 1:N vlan action, init vlan table */
	airoha_fe_set(eth, REG_MC_VLAN_EN, MC_VLAN_EN_MASK);

	return airoha_fe_mc_vlan_clear(eth);
}

static int airoha_qdma_fill_rx_queue(struct airoha_queue *q)
{
	struct airoha_qdma *qdma = q->qdma;
	int qid = q - &qdma->q_rx[0];
	int nframes = 0;

	while (q->queued < q->ndesc - 1) {
		struct airoha_queue_entry *e = &q->entry[q->head];
		struct airoha_qdma_desc *desc = &q->desc[q->head];
		struct page *page;
		int offset;
		u32 val;

		page = page_pool_dev_alloc_frag(q->page_pool, &offset,
						q->buf_size);
		if (!page)
			break;

		q->head = (q->head + 1) % q->ndesc;
		q->queued++;
		nframes++;

		offset += AIROHA_RX_HEADROOM;
		e->buf = page_address(page) + offset;
		e->dma_addr = page_pool_get_dma_addr(page) + offset;
		e->dma_len = SKB_WITH_OVERHEAD(AIROHA_RX_LEN(q->buf_size));

		val = FIELD_PREP(QDMA_DESC_LEN_MASK, e->dma_len);
		WRITE_ONCE(desc->ctrl, cpu_to_le32(val));
		WRITE_ONCE(desc->addr, cpu_to_le32(e->dma_addr));
		val = FIELD_PREP(QDMA_DESC_NEXT_ID_MASK, q->head);
		WRITE_ONCE(desc->data, cpu_to_le32(val));
		WRITE_ONCE(desc->msg0, 0);
		WRITE_ONCE(desc->msg1, 0);
		WRITE_ONCE(desc->msg2, 0);
		WRITE_ONCE(desc->msg3, 0);
	}

	if (nframes)
		airoha_qdma_rmw(qdma, REG_RX_CPU_IDX(qid),
				RX_RING_CPU_IDX_MASK,
				FIELD_PREP(RX_RING_CPU_IDX_MASK, q->head));

	return nframes;
}

static struct airoha_gdm_dev *
airoha_qdma_get_gdm_dev(struct airoha_eth *eth, struct airoha_qdma_desc *desc)
{
	struct airoha_gdm_port *port;
	u16 p, d;

	if (eth->soc->ops.get_dev_from_sport(desc, &p, &d))
		return ERR_PTR(-ENODEV);

	if (p >= ARRAY_SIZE(eth->ports))
		return ERR_PTR(-ENODEV);

	port = eth->ports[p];
	if (!port)
		return ERR_PTR(-ENODEV);

	if (d >= ARRAY_SIZE(port->devs))
		return ERR_PTR(-ENODEV);

	return port->devs[d] ? port->devs[d] : ERR_PTR(-ENODEV);
}

static int airoha_qdma_rx_process(struct airoha_queue *q, int budget)
{
	enum dma_data_direction dir = page_pool_get_dma_dir(q->page_pool);
	struct airoha_qdma *qdma = q->qdma;
	struct airoha_eth *eth = qdma->eth;
	int qid = q - &qdma->q_rx[0];
	int done = 0;

	while (done < budget) {
		struct airoha_queue_entry *e = &q->entry[q->tail];
		struct airoha_qdma_desc *desc = &q->desc[q->tail];
		u32 hash, reason, msg1, desc_ctrl;
		struct airoha_gdm_dev *dev;
		struct net_device *netdev;
		int data_len, len;
		struct page *page;

		desc_ctrl = le32_to_cpu(READ_ONCE(desc->ctrl));
		if (!(desc_ctrl & QDMA_DESC_DONE_MASK))
			break;

		dma_rmb();

		q->tail = (q->tail + 1) % q->ndesc;
		q->queued--;

		dma_sync_single_for_cpu(eth->dev, e->dma_addr, e->dma_len,
					dir);

		page = virt_to_head_page(e->buf);
		len = FIELD_GET(QDMA_DESC_LEN_MASK, desc_ctrl);
		data_len = q->skb ? AIROHA_RX_LEN(q->buf_size) : e->dma_len;
		if (!len || data_len < len)
			goto free_frag;

		dev = airoha_qdma_get_gdm_dev(eth, desc);
		if (IS_ERR(dev))
			goto free_frag;

		netdev = netdev_from_priv(dev);
		if (!q->skb) { /* first buffer */
			q->skb = napi_build_skb(e->buf - AIROHA_RX_HEADROOM,
						q->buf_size);
			if (!q->skb)
				goto free_frag;

			skb_reserve(q->skb, AIROHA_RX_HEADROOM);
			__skb_put(q->skb, len);
			skb_mark_for_recycle(q->skb);
			q->skb->dev = netdev;
			q->skb->protocol = eth_type_trans(q->skb, netdev);
			q->skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb_record_rx_queue(q->skb, qid);
		} else { /* scattered frame */
			struct skb_shared_info *shinfo = skb_shinfo(q->skb);
			int nr_frags = shinfo->nr_frags;

			if (nr_frags >= ARRAY_SIZE(shinfo->frags))
				goto free_frag;

			skb_add_rx_frag(q->skb, nr_frags, page,
					e->buf - page_address(page), len,
					q->buf_size);
		}

		if (FIELD_GET(QDMA_DESC_MORE_MASK, desc_ctrl))
			continue;

		if (netdev_uses_dsa(netdev)) {
			struct airoha_gdm_port *port = dev->port;

			/* PPE module requires untagged packets to work
			 * properly and it provides DSA port index via the
			 * DMA descriptor. Report DSA tag to the DSA stack
			 * via skb dst info.
			 */
			u32 msg0 = le32_to_cpu(READ_ONCE(desc->msg0));
			u32 sptag = FIELD_GET(QDMA_ETH_RXMSG_SPTAG, msg0);

			if (sptag < ARRAY_SIZE(port->dsa_meta) &&
			    port->dsa_meta[sptag])
				skb_dst_set_noref(q->skb,
						  &port->dsa_meta[sptag]->dst);
		}

		msg1 = le32_to_cpu(READ_ONCE(desc->msg1));
		hash = FIELD_GET(AIROHA_RXD4_FOE_ENTRY, msg1);
		if (hash != AIROHA_RXD4_FOE_ENTRY)
			skb_set_hash(q->skb, jhash_1word(hash, 0),
				     PKT_HASH_TYPE_L4);

		reason = FIELD_GET(AIROHA_RXD4_PPE_CPU_REASON, msg1);
		if (reason == PPE_CPU_REASON_HIT_UNBIND_RATE_REACHED)
			airoha_ppe_check_skb(&eth->ppe->dev, q->skb, hash,
					     false);

		done++;
		napi_gro_receive(&q->napi, q->skb);
		q->skb = NULL;
		continue;
free_frag:
		if (q->skb) {
			dev_kfree_skb(q->skb);
			q->skb = NULL;
		}
		page_pool_put_full_page(q->page_pool, page, true);
	}
	airoha_qdma_fill_rx_queue(q);

	return done;
}

static int airoha_qdma_rx_napi_poll(struct napi_struct *napi, int budget)
{
	struct airoha_queue *q = container_of(napi, struct airoha_queue, napi);
	int cur, done = 0;

	do {
		cur = airoha_qdma_rx_process(q, budget - done);
		done += cur;
	} while (cur && done < budget);

	if (done < budget && napi_complete(napi)) {
		struct airoha_qdma *qdma = q->qdma;
		int i, qid = q - &qdma->q_rx[0];
		int intr_reg = qid < RX_DONE_HIGH_OFFSET ? QDMA_INT_REG_IDX1
							 : QDMA_INT_REG_IDX2;

		for (i = 0; i < ARRAY_SIZE(qdma->irq_banks); i++) {
			if (!(BIT(qid) & RX_IRQ_BANK_PIN_MASK(i)))
				continue;

			airoha_qdma_irq_enable(&qdma->irq_banks[i], intr_reg,
					       BIT(qid % RX_DONE_HIGH_OFFSET));
		}
	}

	return done;
}

static int airoha_qdma_init_rx_queue(struct airoha_queue *q,
				     struct airoha_qdma *qdma, int ndesc)
{
	const struct page_pool_params pp_params = {
		.order = 0,
		.pool_size = 256,
		.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.dma_dir = DMA_FROM_DEVICE,
		.max_len = PAGE_SIZE,
		.nid = NUMA_NO_NODE,
		.dev = qdma->eth->dev,
		.napi = &q->napi,
	};
	struct airoha_eth *eth = qdma->eth;
	int qid = q - &qdma->q_rx[0], thr;
	dma_addr_t dma_addr;

	q->buf_size = PAGE_SIZE / 2;
	q->qdma = qdma;

	q->entry = devm_kzalloc(eth->dev, ndesc * sizeof(*q->entry),
				GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	q->desc = dmam_alloc_coherent(eth->dev, ndesc * sizeof(*q->desc),
				      &dma_addr, GFP_KERNEL);
	if (!q->desc)
		return -ENOMEM;

	q->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(q->page_pool)) {
		int err = PTR_ERR(q->page_pool);

		q->page_pool = NULL;
		return err;
	}

	q->ndesc = ndesc;
	netif_napi_add(eth->napi_dev, &q->napi, airoha_qdma_rx_napi_poll);

	airoha_qdma_wr(qdma, REG_RX_RING_BASE(qid), dma_addr);
	airoha_qdma_rmw(qdma, REG_RX_RING_SIZE(qid),
			RX_RING_SIZE_MASK,
			FIELD_PREP(RX_RING_SIZE_MASK, ndesc));

	thr = clamp(ndesc >> 3, 1, 32);
	airoha_qdma_rmw(qdma, REG_RX_RING_SIZE(qid), RX_RING_THR_MASK,
			FIELD_PREP(RX_RING_THR_MASK, thr));
	airoha_qdma_rmw(qdma, REG_RX_DMA_IDX(qid), RX_RING_DMA_IDX_MASK,
			FIELD_PREP(RX_RING_DMA_IDX_MASK, q->head));
	airoha_qdma_set(qdma, REG_RX_SCATTER_CFG(qid), RX_RING_SG_EN_MASK);

	airoha_qdma_fill_rx_queue(q);

	return 0;
}

static void airoha_qdma_cleanup_rx_queue(struct airoha_queue *q)
{
	struct airoha_qdma *qdma = q->qdma;
	struct airoha_eth *eth = qdma->eth;
	int qid = q - &qdma->q_rx[0];

	while (q->queued) {
		struct airoha_queue_entry *e = &q->entry[q->tail];
		struct airoha_qdma_desc *desc = &q->desc[q->tail];
		struct page *page = virt_to_head_page(e->buf);

		dma_sync_single_for_cpu(eth->dev, e->dma_addr, e->dma_len,
					page_pool_get_dma_dir(q->page_pool));
		page_pool_put_full_page(q->page_pool, page, false);
		/* Reset DMA descriptor */
		WRITE_ONCE(desc->ctrl, 0);
		WRITE_ONCE(desc->addr, 0);
		WRITE_ONCE(desc->data, 0);
		WRITE_ONCE(desc->msg0, 0);
		WRITE_ONCE(desc->msg1, 0);
		WRITE_ONCE(desc->msg2, 0);
		WRITE_ONCE(desc->msg3, 0);

		q->tail = (q->tail + 1) % q->ndesc;
		q->queued--;
	}

	q->head = q->tail;
	/* Set RX_DMA_IDX to RX_CPU_IDX to notify the hw the QDMA RX ring is
	 * empty.
	 */
	airoha_qdma_rmw(qdma, REG_RX_CPU_IDX(qid), RX_RING_CPU_IDX_MASK,
			FIELD_PREP(RX_RING_CPU_IDX_MASK, q->head));
	airoha_qdma_rmw(qdma, REG_RX_DMA_IDX(qid), RX_RING_DMA_IDX_MASK,
			FIELD_PREP(RX_RING_DMA_IDX_MASK, q->tail));
}

static int airoha_qdma_init_rx(struct airoha_qdma *qdma)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		int err;

		if (!(RX_DONE_INT_MASK & BIT(i))) {
			/* rx-queue not binded to irq */
			continue;
		}

		err = airoha_qdma_init_rx_queue(&qdma->q_rx[i], qdma,
						RX_DSCP_NUM(i));
		if (err)
			return err;
	}

	return 0;
}

static void airoha_qdma_wake_netdev_txqs(struct airoha_queue *q)
{
	struct airoha_qdma *qdma = q->qdma;
	struct airoha_eth *eth = qdma->eth;
	int i, qid = q - &qdma->q_tx[0];

	for (i = 0; i < ARRAY_SIZE(eth->ports); i++) {
		struct airoha_gdm_port *port = eth->ports[i];
		int d;

		if (!port)
			continue;

		for (d = 0; d < ARRAY_SIZE(port->devs); d++) {
			struct airoha_gdm_dev *dev = port->devs[d];
			struct net_device *netdev;
			int j;

			if (!dev)
				continue;

			if (dev->qdma != qdma)
				continue;

			netdev = netdev_from_priv(dev);
			for (j = 0; j < netdev->num_tx_queues; j++) {
				if (airoha_qdma_get_txq(qdma, j) != qid)
					continue;

				netif_wake_subqueue(netdev, j);
			}
		}
	}
	q->txq_stopped = false;
}

static void airoha_unmap_xmit_buf(struct airoha_eth *eth,
				  struct airoha_queue_entry *e)
{
	switch (e->dma_type) {
	case AIROHA_DMA_MAP_PAGE:
		dma_unmap_page(eth->dev, e->dma_addr, e->dma_len,
			       DMA_TO_DEVICE);
		break;
	case AIROHA_DMA_MAP_SINGLE:
		dma_unmap_single(eth->dev, e->dma_addr, e->dma_len,
				 DMA_TO_DEVICE);
		break;
	case AIROHA_DMA_UNMAPPED:
	default:
		break;
	}
	e->dma_type = AIROHA_DMA_UNMAPPED;
}

static int airoha_qdma_tx_napi_poll(struct napi_struct *napi, int budget)
{
	struct airoha_tx_irq_queue *irq_q;
	int id, done = 0, irq_queued;
	struct airoha_qdma *qdma;
	struct airoha_eth *eth;
	u32 status, head;

	irq_q = container_of(napi, struct airoha_tx_irq_queue, napi);
	qdma = irq_q->qdma;
	id = irq_q - &qdma->q_tx_irq[0];
	eth = qdma->eth;

	status = airoha_qdma_rr(qdma, REG_IRQ_STATUS(id));
	head = FIELD_GET(IRQ_HEAD_IDX_MASK, status);
	head = head % irq_q->size;
	irq_queued = FIELD_GET(IRQ_ENTRY_LEN_MASK, status);

	while (irq_queued > 0 && done < budget) {
		u32 qid, val = irq_q->q[head];
		struct airoha_qdma_desc *desc;
		struct airoha_queue_entry *e;
		struct airoha_queue *q;
		u32 index, desc_ctrl;
		struct sk_buff *skb;

		if (val == 0xff)
			break;

		irq_q->q[head] = 0xff; /* mark as done */
		head = (head + 1) % irq_q->size;
		irq_queued--;
		done++;

		qid = FIELD_GET(IRQ_RING_IDX_MASK, val);
		if (qid >= ARRAY_SIZE(qdma->q_tx))
			continue;

		q = &qdma->q_tx[qid];
		if (!q->ndesc)
			continue;

		index = FIELD_GET(IRQ_DESC_IDX_MASK, val);
		if (index >= q->ndesc)
			continue;

		spin_lock_bh(&q->lock);

		if (!q->queued)
			goto unlock;

		desc = &q->desc[index];
		desc_ctrl = le32_to_cpu(desc->ctrl);

		if (!(desc_ctrl & QDMA_DESC_DONE_MASK) &&
		    !(desc_ctrl & QDMA_DESC_DROP_MASK))
			goto unlock;

		e = &q->entry[index];
		skb = e->skb;
		e->skb = NULL;

		airoha_unmap_xmit_buf(eth, e);
		list_add_tail(&e->list, &q->tx_list);

		WRITE_ONCE(desc->msg0, 0);
		WRITE_ONCE(desc->msg1, 0);
		q->queued--;

		if (skb) {
			struct netdev_queue *txq;

			txq = skb_get_tx_queue(skb->dev, skb);
			netdev_tx_completed_queue(txq, 1, skb->len);
			dev_kfree_skb_any(skb);
		}

		if (q->txq_stopped && q->ndesc - q->queued >= q->free_thr) {
			/* Since multiple net_device TX queues can share the
			 * same hw QDMA TX queue, there is no guarantee we have
			 * inflight packets queued in hw belonging to a
			 * net_device TX queue stopped in the xmit path.
			 * In order to avoid any potential net_device TX queue
			 * stall, we need to wake all the net_device TX queues
			 * feeding the same hw QDMA TX queue.
			 */
			airoha_qdma_wake_netdev_txqs(q);
		}

unlock:
		spin_unlock_bh(&q->lock);
	}

	if (done) {
		int i, len = done >> 7;

		for (i = 0; i < len; i++)
			airoha_qdma_rmw(qdma, REG_IRQ_CLEAR_LEN(id),
					IRQ_CLEAR_LEN_MASK, 0x80);
		airoha_qdma_rmw(qdma, REG_IRQ_CLEAR_LEN(id),
				IRQ_CLEAR_LEN_MASK, (done & 0x7f));
	}

	if (done < budget && napi_complete(napi))
		airoha_qdma_irq_enable(&qdma->irq_banks[0], QDMA_INT_REG_IDX0,
				       TX_DONE_INT_MASK(id));

	return done;
}

static int airoha_qdma_init_tx_queue(struct airoha_queue *q,
				     struct airoha_qdma *qdma, int size)
{
	struct airoha_eth *eth = qdma->eth;
	int i, qid = q - &qdma->q_tx[0];
	dma_addr_t dma_addr;

	spin_lock_init(&q->lock);
	q->qdma = qdma;
	q->free_thr = 1 + MAX_SKB_FRAGS;
	INIT_LIST_HEAD(&q->tx_list);

	q->entry = devm_kzalloc(eth->dev, size * sizeof(*q->entry),
				GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	q->desc = dmam_alloc_coherent(eth->dev, size * sizeof(*q->desc),
				      &dma_addr, GFP_KERNEL);
	if (!q->desc)
		return -ENOMEM;

	for (i = 0; i < size; i++) {
		u32 val = FIELD_PREP(QDMA_DESC_DONE_MASK, 1);

		list_add_tail(&q->entry[i].list, &q->tx_list);
		WRITE_ONCE(q->desc[i].ctrl, cpu_to_le32(val));
	}
	q->ndesc = size;

	/* xmit ring drop default setting */
	airoha_qdma_set(qdma, REG_TX_RING_BLOCKING(qid),
			TX_RING_IRQ_BLOCKING_TX_DROP_EN_MASK);

	airoha_qdma_wr(qdma, REG_TX_RING_BASE(qid), dma_addr);
	airoha_qdma_rmw(qdma, REG_TX_CPU_IDX(qid), TX_RING_CPU_IDX_MASK,
			FIELD_PREP(TX_RING_CPU_IDX_MASK, 0));
	airoha_qdma_rmw(qdma, REG_TX_DMA_IDX(qid), TX_RING_DMA_IDX_MASK,
			FIELD_PREP(TX_RING_DMA_IDX_MASK, 0));

	return 0;
}

static int airoha_qdma_tx_irq_init(struct airoha_tx_irq_queue *irq_q,
				   struct airoha_qdma *qdma, int size)
{
	int id = irq_q - &qdma->q_tx_irq[0];
	struct airoha_eth *eth = qdma->eth;
	dma_addr_t dma_addr;

	irq_q->q = dmam_alloc_coherent(eth->dev, size * sizeof(u32),
				       &dma_addr, GFP_KERNEL);
	if (!irq_q->q)
		return -ENOMEM;

	memset(irq_q->q, 0xff, size * sizeof(u32));
	irq_q->size = size;
	irq_q->qdma = qdma;

	netif_napi_add_tx(eth->napi_dev, &irq_q->napi,
			  airoha_qdma_tx_napi_poll);

	airoha_qdma_wr(qdma, REG_TX_IRQ_BASE(id), dma_addr);
	airoha_qdma_rmw(qdma, REG_TX_IRQ_CFG(id), TX_IRQ_DEPTH_MASK,
			FIELD_PREP(TX_IRQ_DEPTH_MASK, size));
	airoha_qdma_rmw(qdma, REG_TX_IRQ_CFG(id), TX_IRQ_THR_MASK,
			FIELD_PREP(TX_IRQ_THR_MASK, 1));

	return 0;
}

static int airoha_qdma_init_tx(struct airoha_qdma *qdma)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx_irq); i++) {
		err = airoha_qdma_tx_irq_init(&qdma->q_tx_irq[i], qdma,
					      IRQ_QUEUE_LEN(i));
		if (err)
			return err;
	}

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx); i++) {
		err = airoha_qdma_init_tx_queue(&qdma->q_tx[i], qdma,
						TX_DSCP_NUM);
		if (err)
			return err;
	}

	return 0;
}

static void airoha_qdma_tx_cleanup(struct airoha_qdma *qdma)
{
	u32 status;
	int i;

	airoha_qdma_clear(qdma, REG_QDMA_GLOBAL_CFG,
			  GLOBAL_CFG_TX_DMA_EN_MASK);
	if (read_poll_timeout(airoha_qdma_rr, status,
			      !(status & GLOBAL_CFG_TX_DMA_BUSY_MASK),
			      USEC_PER_MSEC, 50 * USEC_PER_MSEC, true,
			      qdma, REG_QDMA_GLOBAL_CFG))
		dev_warn(qdma->eth->dev, "QDMA TX DMA busy timeout\n");

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx); i++) {
		struct airoha_queue *q = &qdma->q_tx[i];
		u16 index = 0;
		int j;

		if (!q->ndesc)
			continue;

		spin_lock_bh(&q->lock);

		q->flushing = true;
		for (j = 0; j < q->ndesc; j++) {
			struct airoha_queue_entry *e = &q->entry[j];
			struct airoha_qdma_desc *desc = &q->desc[j];
			struct sk_buff *skb = e->skb;

			if (e->dma_type == AIROHA_DMA_UNMAPPED)
				continue;

			airoha_unmap_xmit_buf(qdma->eth, e);
			list_add_tail(&e->list, &q->tx_list);

			WRITE_ONCE(desc->ctrl, 0);
			WRITE_ONCE(desc->addr, 0);
			WRITE_ONCE(desc->data, 0);
			WRITE_ONCE(desc->msg0, 0);
			WRITE_ONCE(desc->msg1, 0);
			WRITE_ONCE(desc->msg2, 0);

			if (skb) {
				struct netdev_queue *txq;

				txq = skb_get_tx_queue(skb->dev, skb);
				netdev_tx_completed_queue(txq, 1, skb->len);
				dev_kfree_skb_any(skb);
				e->skb = NULL;
			}

			q->queued--;
		}

		if (!list_empty(&q->tx_list)) {
			struct airoha_queue_entry *e;

			e = list_first_entry(&q->tx_list,
					     struct airoha_queue_entry, list);
			index = e - q->entry;
		}
		airoha_qdma_rmw(qdma, REG_TX_CPU_IDX(i), TX_RING_CPU_IDX_MASK,
				FIELD_PREP(TX_RING_CPU_IDX_MASK, index));
		airoha_qdma_rmw(qdma, REG_TX_DMA_IDX(i), TX_RING_DMA_IDX_MASK,
				FIELD_PREP(TX_RING_DMA_IDX_MASK, index));

		spin_unlock_bh(&q->lock);
	}
}

static int airoha_qdma_init_hfwd_queues(struct airoha_qdma *qdma)
{
	int size, index, num_desc = HW_DSCP_NUM;
	struct airoha_eth *eth = qdma->eth;
	int id = qdma - &eth->qdma[0];
	u32 status, buf_size;
	dma_addr_t dma_addr;
	const char *name;

	name = devm_kasprintf(eth->dev, GFP_KERNEL, "qdma%d-buf", id);
	if (!name)
		return -ENOMEM;

	buf_size = id ? AIROHA_MAX_PACKET_SIZE / 2 : AIROHA_MAX_PACKET_SIZE;
	index = of_property_match_string(eth->dev->of_node,
					 "memory-region-names", name);
	if (index >= 0) {
		struct reserved_mem *rmem;
		struct device_node *np;

		/* Consume reserved memory for hw forwarding buffers queue if
		 * available in the DTS
		 */
		np = of_parse_phandle(eth->dev->of_node, "memory-region",
				      index);
		if (!np)
			return -ENODEV;

		rmem = of_reserved_mem_lookup(np);
		of_node_put(np);
		if (!rmem)
			return -ENODEV;

		dma_addr = rmem->base;
		/* Compute the number of hw descriptors according to the
		 * reserved memory size and the payload buffer size
		 */
		num_desc = div_u64(rmem->size, buf_size);
	} else {
		size = buf_size * num_desc;
		if (!dmam_alloc_coherent(eth->dev, size, &dma_addr,
					 GFP_KERNEL))
			return -ENOMEM;
	}

	airoha_qdma_wr(qdma, REG_FWD_BUF_BASE, dma_addr);

	size = num_desc * sizeof(struct airoha_qdma_fwd_desc);
	if (!dmam_alloc_coherent(eth->dev, size, &dma_addr, GFP_KERNEL))
		return -ENOMEM;

	airoha_qdma_wr(qdma, REG_FWD_DSCP_BASE, dma_addr);
	/* QDMA0: 2KB. QDMA1: 1KB */
	airoha_qdma_rmw(qdma, REG_HW_FWD_DSCP_CFG,
			HW_FWD_DSCP_PAYLOAD_SIZE_MASK,
			FIELD_PREP(HW_FWD_DSCP_PAYLOAD_SIZE_MASK, !!id));
	airoha_qdma_rmw(qdma, REG_FWD_DSCP_LOW_THR, FWD_DSCP_LOW_THR_MASK,
			FIELD_PREP(FWD_DSCP_LOW_THR_MASK, 128));
	airoha_qdma_rmw(qdma, REG_LMGR_INIT_CFG,
			LMGR_INIT_START | LMGR_SRAM_MODE_MASK |
			HW_FWD_DESC_NUM_MASK,
			FIELD_PREP(HW_FWD_DESC_NUM_MASK, num_desc) |
			LMGR_INIT_START | LMGR_SRAM_MODE_MASK);

	return read_poll_timeout(airoha_qdma_rr, status,
				 !(status & LMGR_INIT_START), USEC_PER_MSEC,
				 30 * USEC_PER_MSEC, true, qdma,
				 REG_LMGR_INIT_CFG);
}

static void airoha_qdma_init_qos(struct airoha_qdma *qdma)
{
	airoha_qdma_clear(qdma, REG_TXWRR_MODE_CFG, TWRR_WEIGHT_SCALE_MASK);
	airoha_qdma_set(qdma, REG_TXWRR_MODE_CFG, TWRR_WEIGHT_BASE_MASK);

	airoha_qdma_clear(qdma, REG_PSE_BUF_USAGE_CFG,
			  PSE_BUF_ESTIMATE_EN_MASK);

	airoha_qdma_set(qdma, REG_EGRESS_RATE_METER_CFG,
			EGRESS_RATE_METER_EN_MASK |
			EGRESS_RATE_METER_EQ_RATE_EN_MASK);
	/* 2047us x 31 = 63.457ms */
	airoha_qdma_rmw(qdma, REG_EGRESS_RATE_METER_CFG,
			EGRESS_RATE_METER_WINDOW_SZ_MASK,
			FIELD_PREP(EGRESS_RATE_METER_WINDOW_SZ_MASK, 0x1f));
	airoha_qdma_rmw(qdma, REG_EGRESS_RATE_METER_CFG,
			EGRESS_RATE_METER_TIMESLICE_MASK,
			FIELD_PREP(EGRESS_RATE_METER_TIMESLICE_MASK, 0x7ff));

	/* ratelimit init */
	airoha_qdma_set(qdma, REG_GLB_TRTCM_CFG, GLB_TRTCM_EN_MASK);
	/* fast-tick 25us */
	airoha_qdma_rmw(qdma, REG_GLB_TRTCM_CFG, GLB_FAST_TICK_MASK,
			FIELD_PREP(GLB_FAST_TICK_MASK, 25));
	airoha_qdma_rmw(qdma, REG_GLB_TRTCM_CFG, GLB_SLOW_TICK_RATIO_MASK,
			FIELD_PREP(GLB_SLOW_TICK_RATIO_MASK, 40));

	airoha_qdma_set(qdma, REG_EGRESS_TRTCM_CFG, EGRESS_TRTCM_EN_MASK);
	airoha_qdma_rmw(qdma, REG_EGRESS_TRTCM_CFG, EGRESS_FAST_TICK_MASK,
			FIELD_PREP(EGRESS_FAST_TICK_MASK, 25));
	airoha_qdma_rmw(qdma, REG_EGRESS_TRTCM_CFG,
			EGRESS_SLOW_TICK_RATIO_MASK,
			FIELD_PREP(EGRESS_SLOW_TICK_RATIO_MASK, 40));

	airoha_qdma_set(qdma, REG_INGRESS_TRTCM_CFG, INGRESS_TRTCM_EN_MASK);
	airoha_qdma_clear(qdma, REG_INGRESS_TRTCM_CFG,
			  INGRESS_TRTCM_MODE_MASK);
	airoha_qdma_rmw(qdma, REG_INGRESS_TRTCM_CFG, INGRESS_FAST_TICK_MASK,
			FIELD_PREP(INGRESS_FAST_TICK_MASK, 125));
	airoha_qdma_rmw(qdma, REG_INGRESS_TRTCM_CFG,
			INGRESS_SLOW_TICK_RATIO_MASK,
			FIELD_PREP(INGRESS_SLOW_TICK_RATIO_MASK, 8));

	airoha_qdma_set(qdma, REG_SLA_TRTCM_CFG, SLA_TRTCM_EN_MASK);
	airoha_qdma_rmw(qdma, REG_SLA_TRTCM_CFG, SLA_FAST_TICK_MASK,
			FIELD_PREP(SLA_FAST_TICK_MASK, 25));
	airoha_qdma_rmw(qdma, REG_SLA_TRTCM_CFG, SLA_SLOW_TICK_RATIO_MASK,
			FIELD_PREP(SLA_SLOW_TICK_RATIO_MASK, 40));
}

static void airoha_qdma_init_qos_stats(struct airoha_qdma *qdma)
{
	int i;

	for (i = 0; i < AIROHA_NUM_QOS_CHANNELS; i++) {
		/* Tx-cpu transferred count */
		airoha_qdma_wr(qdma, REG_CNTR_VAL(i << 1), 0);
		airoha_qdma_wr(qdma, REG_CNTR_CFG(i << 1),
			       CNTR_EN_MASK | CNTR_ALL_QUEUE_EN_MASK |
			       CNTR_ALL_DSCP_RING_EN_MASK |
			       FIELD_PREP(CNTR_CHAN_MASK, i));
		/* Tx-fwd transferred count */
		airoha_qdma_wr(qdma, REG_CNTR_VAL((i << 1) + 1), 0);
		airoha_qdma_wr(qdma, REG_CNTR_CFG((i << 1) + 1),
			       CNTR_EN_MASK | CNTR_ALL_QUEUE_EN_MASK |
			       CNTR_ALL_DSCP_RING_EN_MASK |
			       FIELD_PREP(CNTR_SRC_MASK, 1) |
			       FIELD_PREP(CNTR_CHAN_MASK, i));
	}
}

static int airoha_qdma_hw_init(struct airoha_qdma *qdma)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qdma->irq_banks); i++) {
		/* clear pending irqs */
		airoha_qdma_wr(qdma, REG_INT_STATUS(i), 0xffffffff);
		/* setup rx irqs */
		airoha_qdma_irq_enable(&qdma->irq_banks[i], QDMA_INT_REG_IDX0,
				       INT_RX0_MASK(RX_IRQ_BANK_PIN_MASK(i)));
		airoha_qdma_irq_enable(&qdma->irq_banks[i], QDMA_INT_REG_IDX1,
				       INT_RX1_MASK(RX_IRQ_BANK_PIN_MASK(i)));
		airoha_qdma_irq_enable(&qdma->irq_banks[i], QDMA_INT_REG_IDX2,
				       INT_RX2_MASK(RX_IRQ_BANK_PIN_MASK(i)));
		airoha_qdma_irq_enable(&qdma->irq_banks[i], QDMA_INT_REG_IDX3,
				       INT_RX3_MASK(RX_IRQ_BANK_PIN_MASK(i)));
	}
	/* setup tx irqs */
	airoha_qdma_irq_enable(&qdma->irq_banks[0], QDMA_INT_REG_IDX0,
			       TX_COHERENT_LOW_INT_MASK | INT_TX_MASK);
	airoha_qdma_irq_enable(&qdma->irq_banks[0], QDMA_INT_REG_IDX4,
			       TX_COHERENT_HIGH_INT_MASK);

	/* setup irq binding */
	for (i = 0; i < ARRAY_SIZE(qdma->q_tx); i++) {
		if (!qdma->q_tx[i].ndesc)
			continue;

		if (TX_RING_IRQ_BLOCKING_MAP_MASK & BIT(i))
			airoha_qdma_set(qdma, REG_TX_RING_BLOCKING(i),
					TX_RING_IRQ_BLOCKING_CFG_MASK);
		else
			airoha_qdma_clear(qdma, REG_TX_RING_BLOCKING(i),
					  TX_RING_IRQ_BLOCKING_CFG_MASK);
	}

	airoha_qdma_wr(qdma, REG_QDMA_GLOBAL_CFG,
		       FIELD_PREP(GLOBAL_CFG_DMA_PREFERENCE_MASK, 3) |
		       GLOBAL_CFG_CPU_TXR_RR_MASK |
		       GLOBAL_CFG_PAYLOAD_BYTE_SWAP_MASK |
		       GLOBAL_CFG_MULTICAST_MODIFY_FP_MASK |
		       GLOBAL_CFG_MULTICAST_EN_MASK |
		       GLOBAL_CFG_IRQ0_EN_MASK | GLOBAL_CFG_IRQ1_EN_MASK |
		       GLOBAL_CFG_TX_WB_DONE_MASK |
		       FIELD_PREP(GLOBAL_CFG_MAX_ISSUE_NUM_MASK, 2));

	airoha_qdma_init_qos(qdma);

	/* disable qdma rx delay interrupt */
	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		if (!qdma->q_rx[i].ndesc)
			continue;

		airoha_qdma_clear(qdma, REG_RX_DELAY_INT_IDX(i),
				  RX_DELAY_INT_MASK);
	}

	airoha_qdma_set(qdma, REG_TXQ_CNGST_CFG,
			TXQ_CNGST_DROP_EN | TXQ_CNGST_DEI_DROP_EN);
	airoha_qdma_init_qos_stats(qdma);

	return 0;
}

static irqreturn_t airoha_irq_handler(int irq, void *dev_instance)
{
	struct airoha_irq_bank *irq_bank = dev_instance;
	struct airoha_qdma *qdma = irq_bank->qdma;
	u32 rx_intr_mask = 0, rx_intr1, rx_intr2;
	u32 intr[ARRAY_SIZE(irq_bank->irqmask)];
	int i;

	for (i = 0; i < ARRAY_SIZE(intr); i++) {
		intr[i] = airoha_qdma_rr(qdma, REG_INT_STATUS(i));
		intr[i] &= irq_bank->irqmask[i];
		airoha_qdma_wr(qdma, REG_INT_STATUS(i), intr[i]);
	}

	if (!test_bit(DEV_STATE_INITIALIZED, &qdma->eth->state))
		return IRQ_NONE;

	rx_intr1 = intr[1] & RX_DONE_LOW_INT_MASK;
	if (rx_intr1) {
		airoha_qdma_irq_disable(irq_bank, QDMA_INT_REG_IDX1, rx_intr1);
		rx_intr_mask |= rx_intr1;
	}

	rx_intr2 = intr[2] & RX_DONE_HIGH_INT_MASK;
	if (rx_intr2) {
		airoha_qdma_irq_disable(irq_bank, QDMA_INT_REG_IDX2, rx_intr2);
		rx_intr_mask |= (rx_intr2 << 16);
	}

	for (i = 0; rx_intr_mask && i < ARRAY_SIZE(qdma->q_rx); i++) {
		if (!qdma->q_rx[i].ndesc)
			continue;

		if (rx_intr_mask & BIT(i))
			napi_schedule(&qdma->q_rx[i].napi);
	}

	if (intr[0] & INT_TX_MASK) {
		for (i = 0; i < ARRAY_SIZE(qdma->q_tx_irq); i++) {
			if (!(intr[0] & TX_DONE_INT_MASK(i)))
				continue;

			airoha_qdma_irq_disable(irq_bank, QDMA_INT_REG_IDX0,
						TX_DONE_INT_MASK(i));
			napi_schedule(&qdma->q_tx_irq[i].napi);
		}
	}

	return IRQ_HANDLED;
}

static int airoha_qdma_init_irq_banks(struct platform_device *pdev,
				      struct airoha_qdma *qdma)
{
	struct airoha_eth *eth = qdma->eth;
	int i, id = qdma - &eth->qdma[0];

	for (i = 0; i < ARRAY_SIZE(qdma->irq_banks); i++) {
		struct airoha_irq_bank *irq_bank = &qdma->irq_banks[i];
		int err, irq_index = 4 * id + i;
		const char *name;

		spin_lock_init(&irq_bank->irq_lock);
		irq_bank->qdma = qdma;

		irq_bank->irq = platform_get_irq(pdev, irq_index);
		if (irq_bank->irq < 0)
			return irq_bank->irq;

		name = devm_kasprintf(eth->dev, GFP_KERNEL,
				      KBUILD_MODNAME ".%d", irq_index);
		if (!name)
			return -ENOMEM;

		err = devm_request_irq(eth->dev, irq_bank->irq,
				       airoha_irq_handler, IRQF_SHARED, name,
				       irq_bank);
		if (err)
			return err;
	}

	return 0;
}

static int airoha_qdma_init(struct platform_device *pdev,
			    struct airoha_eth *eth,
			    struct airoha_qdma *qdma)
{
	int err, id = qdma - &eth->qdma[0];
	const char *res;

	qdma->eth = eth;
	res = devm_kasprintf(eth->dev, GFP_KERNEL, "qdma%d", id);
	if (!res)
		return -ENOMEM;

	qdma->regs = devm_platform_ioremap_resource_byname(pdev, res);
	if (IS_ERR(qdma->regs))
		return dev_err_probe(eth->dev, PTR_ERR(qdma->regs),
				     "failed to iomap qdma%d regs\n", id);

	err = airoha_qdma_init_irq_banks(pdev, qdma);
	if (err)
		return err;

	err = airoha_qdma_init_rx(qdma);
	if (err)
		return err;

	err = airoha_qdma_init_tx(qdma);
	if (err)
		return err;

	err = airoha_qdma_init_hfwd_queues(qdma);
	if (err)
		return err;

	return airoha_qdma_hw_init(qdma);
}

static void airoha_qdma_cleanup(struct airoha_eth *eth,
				struct airoha_qdma *qdma)
{
	int i;

	if (test_bit(DEV_STATE_INITIALIZED, &eth->state)) {
		u32 status;

		airoha_qdma_clear(qdma, REG_QDMA_GLOBAL_CFG,
				  GLOBAL_CFG_RX_DMA_EN_MASK);
		if (read_poll_timeout(airoha_qdma_rr, status,
				      !(status & GLOBAL_CFG_RX_DMA_BUSY_MASK),
				      USEC_PER_MSEC, 50 * USEC_PER_MSEC, true,
				      qdma, REG_QDMA_GLOBAL_CFG))
			dev_warn(eth->dev, "QDMA RX DMA busy timeout\n");
	}

	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		if (!qdma->q_rx[i].ndesc)
			continue;

		netif_napi_del(&qdma->q_rx[i].napi);
		airoha_qdma_cleanup_rx_queue(&qdma->q_rx[i]);
		if (qdma->q_rx[i].page_pool) {
			page_pool_destroy(qdma->q_rx[i].page_pool);
			qdma->q_rx[i].page_pool = NULL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx_irq); i++) {
		if (!qdma->q_tx_irq[i].size)
			continue;

		netif_napi_del(&qdma->q_tx_irq[i].napi);
	}

}

static int airoha_hw_init(struct platform_device *pdev,
			  struct airoha_eth *eth)
{
	int err, i;

	/* disable xsi */
	err = reset_control_bulk_assert(eth->soc->num_xsi_rsts, eth->xsi_rsts);
	if (err)
		return err;

	err = reset_control_bulk_assert(ARRAY_SIZE(eth->rsts), eth->rsts);
	if (err)
		return err;

	msleep(20);
	err = reset_control_bulk_deassert(ARRAY_SIZE(eth->rsts), eth->rsts);
	if (err)
		return err;

	msleep(20);
	err = airoha_fe_init(eth);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++) {
		err = airoha_qdma_init(pdev, eth, &eth->qdma[i]);
		if (err)
			goto error;
	}

	err = airoha_ppe_init(eth);
	if (err)
		goto error;

	set_bit(DEV_STATE_INITIALIZED, &eth->state);

	return 0;
error:
	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++)
		airoha_qdma_cleanup(eth, &eth->qdma[i]);

	return err;
}

static void airoha_hw_cleanup(struct airoha_eth *eth)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++)
		airoha_qdma_cleanup(eth, &eth->qdma[i]);
	airoha_ppe_deinit(eth);
}

static void airoha_qdma_start_napi(struct airoha_qdma *qdma)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx_irq); i++)
		napi_enable(&qdma->q_tx_irq[i].napi);

	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		if (!qdma->q_rx[i].ndesc)
			continue;

		napi_enable(&qdma->q_rx[i].napi);
	}
}

static void airoha_qdma_stop_napi(struct airoha_qdma *qdma)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx_irq); i++)
		napi_disable(&qdma->q_tx_irq[i].napi);

	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		if (!qdma->q_rx[i].ndesc)
			continue;

		napi_disable(&qdma->q_rx[i].napi);
	}
}

static void airoha_dev_get_hw_stats(struct airoha_gdm_dev *dev)
{
	struct airoha_gdm_port *port = dev->port;
	struct airoha_eth *eth = dev->eth;
	u32 val, i = 0;

	/* Read relevant MIB for GDM with multiple port attached */
	if (port->id == AIROHA_GDM3_IDX || port->id == AIROHA_GDM4_IDX)
		airoha_fe_rmw(eth, REG_FE_GDM_MIB_CFG(port->id),
			      FE_TX_MIB_ID_MASK | FE_RX_MIB_ID_MASK,
			      FIELD_PREP(FE_TX_MIB_ID_MASK, dev->nbq) |
			      FIELD_PREP(FE_RX_MIB_ID_MASK, dev->nbq));

	u64_stats_update_begin(&dev->stats.syncp);

	/* TX */
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_OK_PKT_CNT_H(port->id));
	dev->stats.tx_ok_pkts += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_OK_PKT_CNT_L(port->id));
	dev->stats.tx_ok_pkts += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_OK_BYTE_CNT_H(port->id));
	dev->stats.tx_ok_bytes += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_OK_BYTE_CNT_L(port->id));
	dev->stats.tx_ok_bytes += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_DROP_CNT(port->id));
	dev->stats.tx_drops += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_BC_CNT(port->id));
	dev->stats.tx_broadcast += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_MC_CNT(port->id));
	dev->stats.tx_multicast += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_RUNT_CNT(port->id));
	dev->stats.tx_len[i] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_E64_CNT_H(port->id));
	dev->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_E64_CNT_L(port->id));
	dev->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L64_CNT_H(port->id));
	dev->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L64_CNT_L(port->id));
	dev->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L127_CNT_H(port->id));
	dev->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L127_CNT_L(port->id));
	dev->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L255_CNT_H(port->id));
	dev->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L255_CNT_L(port->id));
	dev->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L511_CNT_H(port->id));
	dev->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L511_CNT_L(port->id));
	dev->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L1023_CNT_H(port->id));
	dev->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L1023_CNT_L(port->id));
	dev->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_LONG_CNT(port->id));
	dev->stats.tx_len[i++] += val;

	/* RX */
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OK_PKT_CNT_H(port->id));
	dev->stats.rx_ok_pkts += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OK_PKT_CNT_L(port->id));
	dev->stats.rx_ok_pkts += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OK_BYTE_CNT_H(port->id));
	dev->stats.rx_ok_bytes += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OK_BYTE_CNT_L(port->id));
	dev->stats.rx_ok_bytes += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_DROP_CNT(port->id));
	dev->stats.rx_drops += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_BC_CNT(port->id));
	dev->stats.rx_broadcast += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_MC_CNT(port->id));
	dev->stats.rx_multicast += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ERROR_DROP_CNT(port->id));
	dev->stats.rx_errors += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_CRC_ERR_CNT(port->id));
	dev->stats.rx_crc_error += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OVERFLOW_DROP_CNT(port->id));
	dev->stats.rx_over_errors += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_FRAG_CNT(port->id));
	dev->stats.rx_fragment += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_JABBER_CNT(port->id));
	dev->stats.rx_jabber += val;

	i = 0;
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_RUNT_CNT(port->id));
	dev->stats.rx_len[i] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_E64_CNT_H(port->id));
	dev->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_E64_CNT_L(port->id));
	dev->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L64_CNT_H(port->id));
	dev->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L64_CNT_L(port->id));
	dev->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L127_CNT_H(port->id));
	dev->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L127_CNT_L(port->id));
	dev->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L255_CNT_H(port->id));
	dev->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L255_CNT_L(port->id));
	dev->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L511_CNT_H(port->id));
	dev->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L511_CNT_L(port->id));
	dev->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L1023_CNT_H(port->id));
	dev->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L1023_CNT_L(port->id));
	dev->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_LONG_CNT(port->id));
	dev->stats.rx_len[i++] += val;

	u64_stats_update_end(&dev->stats.syncp);
}

static void airoha_update_hw_stats(struct airoha_gdm_dev *dev)
{
	struct airoha_gdm_port *port = dev->port;
	int i;

	spin_lock(&port->stats_lock);

	for (i = 0; i < ARRAY_SIZE(port->devs); i++) {
		if (port->devs[i])
			airoha_dev_get_hw_stats(port->devs[i]);
	}

	/* Reset MIB counters */
	airoha_fe_set(dev->eth, REG_FE_GDM_MIB_CLEAR(port->id),
		      FE_GDM_MIB_RX_CLEAR_MASK | FE_GDM_MIB_TX_CLEAR_MASK);

	spin_unlock(&port->stats_lock);
}

static void airoha_dev_set_xmit_frame_size(struct net_device *netdev)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);

	airoha_ppe_set_xmit_frame_size(dev);
	if (!airoha_is_lan_gdm_dev(dev))
		airoha_fe_rmw(dev->eth, REG_WAN_MTU0, WAN_MTU0_MASK,
			      FIELD_PREP(WAN_MTU0_MASK,
					 VLAN_ETH_HLEN + netdev->mtu));
}

static int airoha_dev_open(struct net_device *netdev)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_gdm_port *port = dev->port;
	struct airoha_qdma *qdma = dev->qdma;
	u32 pse_port = FE_PSE_PORT_PPE1;
	int err;

	netif_tx_start_all_queues(netdev);
	err = airoha_set_vip_for_gdm_port(dev, true);
	if (err)
		return err;

	if (netdev_uses_dsa(netdev))
		airoha_fe_set(qdma->eth, REG_GDM_INGRESS_CFG(port->id),
			      GDM_STAG_EN_MASK);
	else
		airoha_fe_clear(qdma->eth, REG_GDM_INGRESS_CFG(port->id),
				GDM_STAG_EN_MASK);

	airoha_dev_set_xmit_frame_size(netdev);
	port->users++;

	if (!airoha_is_lan_gdm_dev(dev) &&
	    airoha_ppe_is_enabled(qdma->eth, 1))
		pse_port = FE_PSE_PORT_PPE2;
	airoha_set_gdm_port_fwd_cfg(qdma->eth, REG_GDM_FWD_CFG(port->id),
				    pse_port);

	return 0;
}

static int airoha_dev_stop(struct net_device *netdev)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_gdm_port *port = dev->port;
	struct airoha_qdma *qdma = dev->qdma;

	netif_tx_disable(netdev);
	airoha_set_vip_for_gdm_port(dev, false);

	if (--port->users)
		airoha_ppe_set_xmit_frame_size(dev);
	else
		airoha_set_gdm_port_fwd_cfg(qdma->eth,
					    REG_GDM_FWD_CFG(port->id),
					    FE_PSE_PORT_DROP);
	return 0;
}

static int airoha_dev_set_macaddr(struct net_device *netdev, void *p)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct sockaddr *addr = p;
	int err;

	err = eth_prepare_mac_addr_change(netdev, p);
	if (err)
		return err;

	err = airoha_set_macaddr(dev, addr->sa_data);
	if (err)
		return err;

	eth_commit_mac_addr_change(netdev, p);

	return 0;
}

static int airoha_enable_gdm2_loopback(struct airoha_gdm_dev *dev)
{
	struct airoha_gdm_port *port = dev->port;
	struct airoha_eth *eth = dev->eth;
	u32 val, pse_port, chan;
	int i, src_port;

	src_port = eth->soc->ops.get_sport(port, dev->nbq);
	if (src_port < 0)
		return src_port;

	airoha_set_gdm_port_fwd_cfg(eth, REG_GDM_FWD_CFG(AIROHA_GDM2_IDX),
				    FE_PSE_PORT_DROP);
	airoha_fe_clear(eth, REG_GDM_FWD_CFG(AIROHA_GDM2_IDX),
			GDM_STRIP_CRC_MASK);

	/* Enable GDM2 loopback */
	airoha_fe_wr(eth, REG_GDM_TXCHN_EN(AIROHA_GDM2_IDX), 0xffffffff);
	airoha_fe_wr(eth, REG_GDM_RXCHN_EN(AIROHA_GDM2_IDX), 0xffff);

	chan = port->id == AIROHA_GDM3_IDX ? airoha_is_7581(eth) ? 4 : 3 : 0;
	airoha_fe_rmw(eth, REG_GDM_LPBK_CFG(AIROHA_GDM2_IDX),
		      LPBK_CHAN_MASK | LPBK_MODE_MASK | LPBK_EN_MASK,
		      FIELD_PREP(LPBK_CHAN_MASK, chan) |
		      LBK_GAP_MODE_MASK | LBK_LEN_MODE_MASK |
		      LBK_CHAN_MODE_MASK | LPBK_EN_MASK);
	/* Forward the traffic to the proper GDM port */
	pse_port = port->id == AIROHA_GDM3_IDX ? FE_PSE_PORT_GDM3
					       : FE_PSE_PORT_GDM4;
	airoha_set_gdm_port_fwd_cfg(eth, REG_GDM_FWD_CFG(AIROHA_GDM2_IDX),
				    pse_port);

	/* Disable VIP and IFC for GDM2 */
	airoha_fe_clear(eth, REG_FE_VIP_PORT_EN, BIT(AIROHA_GDM2_IDX));
	airoha_fe_clear(eth, REG_FE_IFC_PORT_EN, BIT(AIROHA_GDM2_IDX));

	airoha_fe_rmw(eth, REG_FE_WAN_PORT,
		      WAN1_EN_MASK | WAN1_MASK | WAN0_MASK,
		      FIELD_PREP(WAN0_MASK, src_port));
	val = src_port & SP_CPORT_DFT_MASK;
	airoha_fe_rmw(eth,
		      REG_SP_DFT_CPORT(src_port >> fls(SP_CPORT_DFT_MASK)),
		      SP_CPORT_MASK(val),
		      __field_prep(SP_CPORT_MASK(val), FE_PSE_PORT_CDM2));

	for (i = 0; i < eth->soc->num_ppe; i++)
		airoha_ppe_set_cpu_port(dev, i, AIROHA_GDM2_IDX);

	if (port->id == AIROHA_GDM4_IDX && airoha_is_7581(eth)) {
		u32 mask = FC_ID_OF_SRC_PORT_MASK(dev->nbq);

		airoha_fe_rmw(eth, REG_SRC_PORT_FC_MAP6, mask,
			      __field_prep(mask, AIROHA_GDM2_IDX));
	}

	return 0;
}

static struct airoha_gdm_dev *
airoha_get_wan_gdm_dev(struct airoha_eth *eth)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(eth->ports); i++) {
		struct airoha_gdm_port *port = eth->ports[i];
		int j;

		if (!port)
			continue;

		for (j = 0; j < ARRAY_SIZE(port->devs); j++) {
			struct airoha_gdm_dev *dev = port->devs[j];

			if (dev && !airoha_is_lan_gdm_dev(dev))
				return dev;
		}
	}

	return NULL;
}

static void airoha_dev_set_qdma(struct airoha_gdm_dev *dev)
{
	struct net_device *netdev = netdev_from_priv(dev);
	struct airoha_eth *eth = dev->eth;
	int ppe_id;

	/* QDMA0 is used for lan ports while QDMA1 is used for WAN ports */
	dev->qdma = &eth->qdma[!airoha_is_lan_gdm_dev(dev)];
	netdev->irq = dev->qdma->irq_banks[0].irq;

	ppe_id = !airoha_is_lan_gdm_dev(dev) && airoha_ppe_is_enabled(eth, 1);
	airoha_ppe_set_cpu_port(dev, ppe_id, airoha_get_fe_port(dev));
}

static int airoha_dev_init(struct net_device *netdev)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_gdm_port *port = dev->port;

	switch (port->id) {
	case AIROHA_GDM3_IDX:
	case AIROHA_GDM4_IDX:
		if (airoha_get_wan_gdm_dev(dev->eth))
			break;
		fallthrough;
	case AIROHA_GDM2_IDX:
		/* GDM2 is always used as wan */
		dev->flags |= AIROHA_PRIV_F_WAN;
		break;
	default:
		break;
	}

	airoha_dev_set_qdma(dev);
	airoha_set_macaddr(dev, netdev->dev_addr);

	if (!airoha_is_lan_gdm_dev(dev) &&
	    (port->id == AIROHA_GDM3_IDX || port->id == AIROHA_GDM4_IDX)) {
		int err;

		err = airoha_enable_gdm2_loopback(dev);
		if (err)
			return err;
	}

	return 0;
}

static void airoha_dev_get_stats64(struct net_device *netdev,
				   struct rtnl_link_stats64 *storage)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	unsigned int start;

	airoha_update_hw_stats(dev);
	do {
		start = u64_stats_fetch_begin(&dev->stats.syncp);
		storage->rx_packets = dev->stats.rx_ok_pkts;
		storage->tx_packets = dev->stats.tx_ok_pkts;
		storage->rx_bytes = dev->stats.rx_ok_bytes;
		storage->tx_bytes = dev->stats.tx_ok_bytes;
		storage->multicast = dev->stats.rx_multicast;
		storage->rx_errors = dev->stats.rx_errors;
		storage->rx_dropped = dev->stats.rx_drops;
		storage->tx_dropped = dev->stats.tx_drops;
		storage->rx_crc_errors = dev->stats.rx_crc_error;
		storage->rx_over_errors = dev->stats.rx_over_errors;
	} while (u64_stats_fetch_retry(&dev->stats.syncp, start));
}

static int airoha_dev_change_mtu(struct net_device *netdev, int mtu)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_gdm_port *port = dev->port;

	WRITE_ONCE(netdev->mtu, mtu);
	if (port->users)
		airoha_dev_set_xmit_frame_size(netdev);

	return 0;
}

static u16 airoha_dev_select_queue(struct net_device *netdev,
				   struct sk_buff *skb,
				   struct net_device *sb_dev)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_gdm_port *port = dev->port;
	int queue, channel;

	/* For dsa device select QoS channel according to the dsa user port
	 * index, rely on port id otherwise. Select QoS queue based on the
	 * skb priority.
	 */
	channel = netdev_uses_dsa(netdev) ? skb_get_queue_mapping(skb) : port->id;
	channel = channel % AIROHA_NUM_QOS_CHANNELS;
	queue = skb->priority % AIROHA_NUM_QOS_QUEUES;
	queue = channel * AIROHA_NUM_QOS_QUEUES + queue;

	return queue < netdev->num_tx_queues ? queue : 0;
}

static u32 airoha_get_dsa_tag(struct sk_buff *skb, struct net_device *dev)
{
#if IS_ENABLED(CONFIG_NET_DSA)
	struct ethhdr *ehdr;
	u8 xmit_tpid;
	u16 tag;

	if (!netdev_uses_dsa(dev))
		return 0;

	if (dev->dsa_ptr->tag_ops->proto != DSA_TAG_PROTO_MTK)
		return 0;

	if (skb_cow_head(skb, 0))
		return 0;

	ehdr = (struct ethhdr *)skb->data;
	tag = be16_to_cpu(ehdr->h_proto);
	xmit_tpid = tag >> 8;

	switch (xmit_tpid) {
	case MTK_HDR_XMIT_TAGGED_TPID_8100:
		ehdr->h_proto = cpu_to_be16(ETH_P_8021Q);
		tag &= ~(MTK_HDR_XMIT_TAGGED_TPID_8100 << 8);
		break;
	case MTK_HDR_XMIT_TAGGED_TPID_88A8:
		ehdr->h_proto = cpu_to_be16(ETH_P_8021AD);
		tag &= ~(MTK_HDR_XMIT_TAGGED_TPID_88A8 << 8);
		break;
	default:
		/* PPE module requires untagged DSA packets to work properly,
		 * so move DSA tag to DMA descriptor.
		 */
		memmove(skb->data + MTK_HDR_LEN, skb->data, 2 * ETH_ALEN);
		__skb_pull(skb, MTK_HDR_LEN);
		break;
	}

	return tag;
#else
	return 0;
#endif
}

int airoha_get_fe_port(struct airoha_gdm_dev *dev)
{
	struct airoha_gdm_port *port = dev->port;
	struct airoha_eth *eth = dev->eth;

	switch (eth->soc->version) {
	case 0x7583:
		return port->id == AIROHA_GDM3_IDX ? FE_PSE_PORT_GDM3
						   : port->id;
	case 0x7581:
	default:
		return port->id == AIROHA_GDM4_IDX ? FE_PSE_PORT_GDM4
						   : port->id;
	}
}

static netdev_tx_t airoha_dev_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_qdma *qdma = dev->qdma;
	u32 nr_frags, tag, msg0, msg1, len;
	struct airoha_queue_entry *e;
	struct netdev_queue *txq;
	struct airoha_queue *q;
	LIST_HEAD(tx_list);
	dma_addr_t addr;
	int i = 0, qid;
	u16 index;
	u8 fport;

	qid = airoha_qdma_get_txq(qdma, skb_get_queue_mapping(skb));
	tag = airoha_get_dsa_tag(skb, netdev);

	msg0 = FIELD_PREP(QDMA_ETH_TXMSG_CHAN_MASK,
			  qid / AIROHA_NUM_QOS_QUEUES) |
	       FIELD_PREP(QDMA_ETH_TXMSG_QUEUE_MASK,
			  qid % AIROHA_NUM_QOS_QUEUES) |
	       FIELD_PREP(QDMA_ETH_TXMSG_SP_TAG_MASK, tag);
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		msg0 |= FIELD_PREP(QDMA_ETH_TXMSG_TCO_MASK, 1) |
			FIELD_PREP(QDMA_ETH_TXMSG_UCO_MASK, 1) |
			FIELD_PREP(QDMA_ETH_TXMSG_ICO_MASK, 1);

	/* TSO: fill MSS info in tcp checksum field */
	if (skb_is_gso(skb)) {
		if (skb_cow_head(skb, 0))
			goto error;

		if (skb_shinfo(skb)->gso_type & (SKB_GSO_TCPV4 |
						 SKB_GSO_TCPV6)) {
			__be16 csum = cpu_to_be16(skb_shinfo(skb)->gso_size);

			tcp_hdr(skb)->check = (__force __sum16)csum;
			msg0 |= FIELD_PREP(QDMA_ETH_TXMSG_TSO_MASK, 1);
		}
	}

	fport = airoha_get_fe_port(dev);
	msg1 = FIELD_PREP(QDMA_ETH_TXMSG_NBOQ_MASK, dev->nbq) |
	       FIELD_PREP(QDMA_ETH_TXMSG_FPORT_MASK, fport) |
	       FIELD_PREP(QDMA_ETH_TXMSG_METER_MASK, 0x7f);

	q = &qdma->q_tx[qid];
	if (WARN_ON_ONCE(!q->ndesc))
		goto error;

	spin_lock_bh(&q->lock);

	if (q->flushing)
		goto error_unlock;

	txq = skb_get_tx_queue(netdev, skb);
	nr_frags = 1 + skb_shinfo(skb)->nr_frags;

	if (q->queued + nr_frags >= q->ndesc) {
		/* not enough space in the queue */
		netif_tx_stop_queue(txq);
		q->txq_stopped = true;
		spin_unlock_bh(&q->lock);
		return NETDEV_TX_BUSY;
	}

	e = list_first_entry(&q->tx_list, struct airoha_queue_entry,
			     list);
	len = skb_headlen(skb);
	addr = dma_map_single(netdev->dev.parent, skb->data, len,
			      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(netdev->dev.parent, addr)))
		goto error_unlock;

	e->dma_type = AIROHA_DMA_MAP_SINGLE;
	index = e - q->entry;

	while (true) {
		struct airoha_qdma_desc *desc = &q->desc[index];
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		u32 val;

		list_move_tail(&e->list, &tx_list);
		e->skb = i == nr_frags - 1 ? skb : NULL;
		e->dma_addr = addr;
		e->dma_len = len;

		e = list_first_entry(&q->tx_list, struct airoha_queue_entry,
				     list);
		index = e - q->entry;

		val = FIELD_PREP(QDMA_DESC_LEN_MASK, len);
		if (i < nr_frags - 1)
			val |= FIELD_PREP(QDMA_DESC_MORE_MASK, 1);
		WRITE_ONCE(desc->ctrl, cpu_to_le32(val));
		WRITE_ONCE(desc->addr, cpu_to_le32(addr));
		val = FIELD_PREP(QDMA_DESC_NEXT_ID_MASK, index);
		WRITE_ONCE(desc->data, cpu_to_le32(val));
		WRITE_ONCE(desc->msg0, cpu_to_le32(msg0));
		WRITE_ONCE(desc->msg1, cpu_to_le32(msg1));
		WRITE_ONCE(desc->msg2, cpu_to_le32(0xffff));

		if (++i == nr_frags)
			break;

		len = skb_frag_size(frag);
		addr = skb_frag_dma_map(netdev->dev.parent, frag, 0, len,
					DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(netdev->dev.parent, addr)))
			goto error_unmap;

		e->dma_type = AIROHA_DMA_MAP_PAGE;
	}
	q->queued += i;

	skb_tx_timestamp(skb);
	netdev_tx_sent_queue(txq, skb->len);
	if (q->ndesc - q->queued < q->free_thr) {
		netif_tx_stop_queue(txq);
		q->txq_stopped = true;
	}

	if (netif_xmit_stopped(txq) || !netdev_xmit_more())
		airoha_qdma_rmw(qdma, REG_TX_CPU_IDX(qid),
				TX_RING_CPU_IDX_MASK,
				FIELD_PREP(TX_RING_CPU_IDX_MASK, index));

	spin_unlock_bh(&q->lock);

	return NETDEV_TX_OK;

error_unmap:
	list_for_each_entry(e, &tx_list, list)
		airoha_unmap_xmit_buf(dev->eth, e);
	list_splice(&tx_list, &q->tx_list);
error_unlock:
	spin_unlock_bh(&q->lock);
error:
	dev_kfree_skb_any(skb);
	netdev->stats.tx_dropped++;

	return NETDEV_TX_OK;
}

static void airoha_ethtool_get_drvinfo(struct net_device *netdev,
				       struct ethtool_drvinfo *info)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_eth *eth = dev->eth;

	strscpy(info->driver, eth->dev->driver->name, sizeof(info->driver));
	strscpy(info->bus_info, dev_name(eth->dev), sizeof(info->bus_info));
}

static void airoha_ethtool_get_mac_stats(struct net_device *netdev,
					 struct ethtool_eth_mac_stats *stats)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	unsigned int start;

	airoha_update_hw_stats(dev);
	do {
		start = u64_stats_fetch_begin(&dev->stats.syncp);
		stats->FramesTransmittedOK = dev->stats.tx_ok_pkts;
		stats->OctetsTransmittedOK = dev->stats.tx_ok_bytes;
		stats->MulticastFramesXmittedOK = dev->stats.tx_multicast;
		stats->BroadcastFramesXmittedOK = dev->stats.tx_broadcast;
		stats->FramesReceivedOK = dev->stats.rx_ok_pkts;
		stats->OctetsReceivedOK = dev->stats.rx_ok_bytes;
		stats->BroadcastFramesReceivedOK = dev->stats.rx_broadcast;
	} while (u64_stats_fetch_retry(&dev->stats.syncp, start));
}

static const struct ethtool_rmon_hist_range airoha_ethtool_rmon_ranges[] = {
	{    0,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1518 },
	{ 1519, 10239 },
	{},
};

static void
airoha_ethtool_get_rmon_stats(struct net_device *netdev,
			      struct ethtool_rmon_stats *stats,
			      const struct ethtool_rmon_hist_range **ranges)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_hw_stats *hw_stats = &dev->stats;
	unsigned int start;

	BUILD_BUG_ON(ARRAY_SIZE(airoha_ethtool_rmon_ranges) !=
		     ARRAY_SIZE(hw_stats->tx_len) + 1);
	BUILD_BUG_ON(ARRAY_SIZE(airoha_ethtool_rmon_ranges) !=
		     ARRAY_SIZE(hw_stats->rx_len) + 1);

	*ranges = airoha_ethtool_rmon_ranges;
	airoha_update_hw_stats(dev);
	do {
		int i;

		start = u64_stats_fetch_begin(&dev->stats.syncp);
		stats->fragments = hw_stats->rx_fragment;
		stats->jabbers = hw_stats->rx_jabber;
		for (i = 0; i < ARRAY_SIZE(airoha_ethtool_rmon_ranges) - 1;
		     i++) {
			stats->hist[i] = hw_stats->rx_len[i];
			stats->hist_tx[i] = hw_stats->tx_len[i];
		}
	} while (u64_stats_fetch_retry(&dev->stats.syncp, start));
}

static int airoha_qdma_set_chan_tx_sched(struct net_device *netdev,
					 int channel, enum tx_sched_mode mode,
					 const u16 *weights, u8 n_weights)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	int i;

	for (i = 0; i < AIROHA_NUM_QOS_QUEUES; i++)
		airoha_qdma_clear(dev->qdma, REG_QUEUE_CLOSE_CFG(channel),
				  TXQ_DISABLE_CHAN_QUEUE_MASK(channel, i));

	for (i = 0; i < n_weights; i++) {
		u32 status;
		int err;

		airoha_qdma_wr(dev->qdma, REG_TXWRR_WEIGHT_CFG,
			       TWRR_RW_CMD_MASK |
			       FIELD_PREP(TWRR_CHAN_IDX_MASK, channel) |
			       FIELD_PREP(TWRR_QUEUE_IDX_MASK, i) |
			       FIELD_PREP(TWRR_VALUE_MASK, weights[i]));
		err = read_poll_timeout(airoha_qdma_rr, status,
					status & TWRR_RW_CMD_DONE,
					USEC_PER_MSEC, 10 * USEC_PER_MSEC,
					true, dev->qdma, REG_TXWRR_WEIGHT_CFG);
		if (err)
			return err;
	}

	airoha_qdma_rmw(dev->qdma, REG_CHAN_QOS_MODE(channel >> 3),
			CHAN_QOS_MODE_MASK(channel),
			__field_prep(CHAN_QOS_MODE_MASK(channel), mode));

	return 0;
}

static int airoha_qdma_set_tx_prio_sched(struct net_device *dev, int channel)
{
	static const u16 w[AIROHA_NUM_QOS_QUEUES] = {};

	return airoha_qdma_set_chan_tx_sched(dev, channel, TC_SCH_SP, w,
					     ARRAY_SIZE(w));
}

static int airoha_qdma_set_tx_ets_sched(struct net_device *dev, int channel,
					struct tc_ets_qopt_offload *opt)
{
	struct tc_ets_qopt_offload_replace_params *p = &opt->replace_params;
	enum tx_sched_mode mode = TC_SCH_SP;
	u16 w[AIROHA_NUM_QOS_QUEUES] = {};
	int i, nstrict = 0;

	if (p->bands > AIROHA_NUM_QOS_QUEUES)
		return -EINVAL;

	for (i = 0; i < p->bands; i++) {
		if (!p->quanta[i])
			nstrict++;
	}

	/* this configuration is not supported by the hw */
	if (nstrict == AIROHA_NUM_QOS_QUEUES - 1)
		return -EINVAL;

	/* EN7581 SoC supports fixed QoS band priority where WRR queues have
	 * lowest priorities with respect to SP ones.
	 * e.g: WRR0, WRR1, .., WRRm, SP0, SP1, .., SPn
	 */
	for (i = 0; i < nstrict; i++) {
		if (p->priomap[p->bands - i - 1] != i)
			return -EINVAL;
	}

	for (i = 0; i < p->bands - nstrict; i++) {
		if (p->priomap[i] != nstrict + i)
			return -EINVAL;

		w[i] = p->weights[nstrict + i];
	}

	if (!nstrict)
		mode = TC_SCH_WRR8;
	else if (nstrict < AIROHA_NUM_QOS_QUEUES - 1)
		mode = nstrict + 1;

	return airoha_qdma_set_chan_tx_sched(dev, channel, mode, w,
					     ARRAY_SIZE(w));
}

static int airoha_qdma_get_tx_ets_stats(struct net_device *netdev, int channel,
					struct tc_ets_qopt_offload *opt)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_qdma *qdma = dev->qdma;

	u64 cpu_tx_packets = airoha_qdma_rr(qdma, REG_CNTR_VAL(channel << 1));
	u64 fwd_tx_packets = airoha_qdma_rr(qdma,
					    REG_CNTR_VAL((channel << 1) + 1));
	u64 tx_packets = (cpu_tx_packets - dev->cpu_tx_packets) +
			 (fwd_tx_packets - dev->fwd_tx_packets);

	_bstats_update(opt->stats.bstats, 0, tx_packets);
	dev->cpu_tx_packets = cpu_tx_packets;
	dev->fwd_tx_packets = fwd_tx_packets;

	return 0;
}

static int airoha_tc_setup_qdisc_ets(struct net_device *dev,
				     struct tc_ets_qopt_offload *opt)
{
	int channel;

	if (opt->parent == TC_H_ROOT)
		return -EINVAL;

	channel = TC_H_MAJ(opt->handle) >> 16;
	channel = channel % AIROHA_NUM_QOS_CHANNELS;

	switch (opt->command) {
	case TC_ETS_REPLACE:
		return airoha_qdma_set_tx_ets_sched(dev, channel, opt);
	case TC_ETS_DESTROY:
		/* PRIO is default qdisc scheduler */
		return airoha_qdma_set_tx_prio_sched(dev, channel);
	case TC_ETS_STATS:
		return airoha_qdma_get_tx_ets_stats(dev, channel, opt);
	default:
		return -EOPNOTSUPP;
	}
}

static int airoha_qdma_get_rl_param(struct airoha_qdma *qdma, int queue_id,
				    u32 addr, enum trtcm_param_type param,
				    u32 *val_low, u32 *val_high)
{
	u32 idx = QDMA_METER_IDX(queue_id), group = QDMA_METER_GROUP(queue_id);
	u32 val, config = FIELD_PREP(RATE_LIMIT_PARAM_TYPE_MASK, param) |
			  FIELD_PREP(RATE_LIMIT_METER_GROUP_MASK, group) |
			  FIELD_PREP(RATE_LIMIT_PARAM_INDEX_MASK, idx);

	airoha_qdma_wr(qdma, REG_TRTCM_CFG_PARAM(addr), config);
	if (read_poll_timeout(airoha_qdma_rr, val,
			      val & RATE_LIMIT_PARAM_RW_DONE_MASK,
			      USEC_PER_MSEC, 10 * USEC_PER_MSEC, true, qdma,
			      REG_TRTCM_CFG_PARAM(addr)))
		return -ETIMEDOUT;

	*val_low = airoha_qdma_rr(qdma, REG_TRTCM_DATA_LOW(addr));
	if (val_high)
		*val_high = airoha_qdma_rr(qdma, REG_TRTCM_DATA_HIGH(addr));

	return 0;
}

static int airoha_qdma_set_rl_param(struct airoha_qdma *qdma, int queue_id,
				    u32 addr, enum trtcm_param_type param,
				    u32 val)
{
	u32 idx = QDMA_METER_IDX(queue_id), group = QDMA_METER_GROUP(queue_id);
	u32 config = RATE_LIMIT_PARAM_RW_MASK |
		     FIELD_PREP(RATE_LIMIT_PARAM_TYPE_MASK, param) |
		     FIELD_PREP(RATE_LIMIT_METER_GROUP_MASK, group) |
		     FIELD_PREP(RATE_LIMIT_PARAM_INDEX_MASK, idx);

	airoha_qdma_wr(qdma, REG_TRTCM_DATA_LOW(addr), val);
	airoha_qdma_wr(qdma, REG_TRTCM_CFG_PARAM(addr), config);

	return read_poll_timeout(airoha_qdma_rr, val,
				 val & RATE_LIMIT_PARAM_RW_DONE_MASK,
				 USEC_PER_MSEC, 10 * USEC_PER_MSEC, true,
				 qdma, REG_TRTCM_CFG_PARAM(addr));
}

static int airoha_qdma_set_rl_config(struct airoha_qdma *qdma, int queue_id,
				     u32 addr, bool enable, u32 enable_mask)
{
	u32 val;
	int err;

	err = airoha_qdma_get_rl_param(qdma, queue_id, addr, TRTCM_MISC_MODE,
				       &val, NULL);
	if (err)
		return err;

	val = enable ? val | enable_mask : val & ~enable_mask;

	return airoha_qdma_set_rl_param(qdma, queue_id, addr, TRTCM_MISC_MODE,
					val);
}

static int airoha_qdma_set_rl_token_bucket(struct airoha_qdma *qdma,
					   int queue_id, u32 rate_val,
					   u32 bucket_size)
{
	u32 val, config, tick, unit, rate, rate_frac;
	int err;

	err = airoha_qdma_get_rl_param(qdma, queue_id, REG_INGRESS_TRTCM_CFG,
				       TRTCM_MISC_MODE, &config, NULL);
	if (err)
		return err;

	val = airoha_qdma_rr(qdma, REG_INGRESS_TRTCM_CFG);
	tick = FIELD_GET(INGRESS_FAST_TICK_MASK, val);
	if (config & TRTCM_TICK_SEL)
		tick *= FIELD_GET(INGRESS_SLOW_TICK_RATIO_MASK, val);
	if (!tick)
		return -EINVAL;

	unit = (config & TRTCM_PKT_MODE) ? 1000000 / tick : 8000 / tick;
	if (!unit)
		return -EINVAL;

	rate = rate_val / unit;
	rate_frac = rate_val % unit;
	rate_frac = FIELD_PREP(TRTCM_TOKEN_RATE_MASK, rate_frac) / unit;
	rate = FIELD_PREP(TRTCM_TOKEN_RATE_MASK, rate) |
	       FIELD_PREP(TRTCM_TOKEN_RATE_FRACTION_MASK, rate_frac);

	err = airoha_qdma_set_rl_param(qdma, queue_id, REG_INGRESS_TRTCM_CFG,
				       TRTCM_TOKEN_RATE_MODE, rate);
	if (err)
		return err;

	val = bucket_size;
	if (!(config & TRTCM_PKT_MODE))
		val = max_t(u32, val, MIN_TOKEN_SIZE);
	val = min_t(u32, __fls(val), MAX_TOKEN_SIZE_OFFSET);

	return airoha_qdma_set_rl_param(qdma, queue_id, REG_INGRESS_TRTCM_CFG,
					TRTCM_BUCKETSIZE_SHIFT_MODE, val);
}

static int airoha_qdma_init_rl_config(struct airoha_qdma *qdma, int queue_id,
				      bool enable, enum trtcm_unit_type unit)
{
	bool tick_sel = queue_id == 0 || queue_id == 2 || queue_id == 8;
	enum trtcm_param mode = TRTCM_METER_MODE;
	int err;

	mode |= unit == TRTCM_PACKET_UNIT ? TRTCM_PKT_MODE : 0;
	err = airoha_qdma_set_rl_config(qdma, queue_id, REG_INGRESS_TRTCM_CFG,
					enable, mode);
	if (err)
		return err;

	return airoha_qdma_set_rl_config(qdma, queue_id, REG_INGRESS_TRTCM_CFG,
					 tick_sel, TRTCM_TICK_SEL);
}

static int airoha_qdma_get_trtcm_param(struct airoha_qdma *qdma, int channel,
				       u32 addr, enum trtcm_param_type param,
				       enum trtcm_mode_type mode,
				       u32 *val_low, u32 *val_high)
{
	u32 idx = QDMA_METER_IDX(channel), group = QDMA_METER_GROUP(channel);
	u32 val, config = FIELD_PREP(TRTCM_PARAM_TYPE_MASK, param) |
			  FIELD_PREP(TRTCM_METER_GROUP_MASK, group) |
			  FIELD_PREP(TRTCM_PARAM_INDEX_MASK, idx) |
			  FIELD_PREP(TRTCM_PARAM_RATE_TYPE_MASK, mode);

	airoha_qdma_wr(qdma, REG_TRTCM_CFG_PARAM(addr), config);
	if (read_poll_timeout(airoha_qdma_rr, val,
			      val & TRTCM_PARAM_RW_DONE_MASK,
			      USEC_PER_MSEC, 10 * USEC_PER_MSEC, true,
			      qdma, REG_TRTCM_CFG_PARAM(addr)))
		return -ETIMEDOUT;

	*val_low = airoha_qdma_rr(qdma, REG_TRTCM_DATA_LOW(addr));
	if (val_high)
		*val_high = airoha_qdma_rr(qdma, REG_TRTCM_DATA_HIGH(addr));

	return 0;
}

static int airoha_qdma_set_trtcm_param(struct airoha_qdma *qdma, int channel,
				       u32 addr, enum trtcm_param_type param,
				       enum trtcm_mode_type mode, u32 val)
{
	u32 idx = QDMA_METER_IDX(channel), group = QDMA_METER_GROUP(channel);
	u32 config = TRTCM_PARAM_RW_MASK |
		     FIELD_PREP(TRTCM_PARAM_TYPE_MASK, param) |
		     FIELD_PREP(TRTCM_METER_GROUP_MASK, group) |
		     FIELD_PREP(TRTCM_PARAM_INDEX_MASK, idx) |
		     FIELD_PREP(TRTCM_PARAM_RATE_TYPE_MASK, mode);

	airoha_qdma_wr(qdma, REG_TRTCM_DATA_LOW(addr), val);
	airoha_qdma_wr(qdma, REG_TRTCM_CFG_PARAM(addr), config);

	return read_poll_timeout(airoha_qdma_rr, val,
				 val & TRTCM_PARAM_RW_DONE_MASK,
				 USEC_PER_MSEC, 10 * USEC_PER_MSEC, true,
				 qdma, REG_TRTCM_CFG_PARAM(addr));
}

static int airoha_qdma_set_trtcm_config(struct airoha_qdma *qdma, int channel,
					u32 addr, enum trtcm_mode_type mode,
					bool enable, u32 enable_mask)
{
	u32 val;

	if (airoha_qdma_get_trtcm_param(qdma, channel, addr, TRTCM_MISC_MODE,
					mode, &val, NULL))
		return -EINVAL;

	val = enable ? val | enable_mask : val & ~enable_mask;

	return airoha_qdma_set_trtcm_param(qdma, channel, addr, TRTCM_MISC_MODE,
					   mode, val);
}

static int airoha_qdma_set_trtcm_token_bucket(struct airoha_qdma *qdma,
					      int channel, u32 addr,
					      enum trtcm_mode_type mode,
					      u32 rate_val, u32 bucket_size)
{
	u32 val, config, tick, unit, rate, rate_frac;
	int err;

	if (airoha_qdma_get_trtcm_param(qdma, channel, addr, TRTCM_MISC_MODE,
					mode, &config, NULL))
		return -EINVAL;

	val = airoha_qdma_rr(qdma, addr);
	tick = FIELD_GET(INGRESS_FAST_TICK_MASK, val);
	if (config & TRTCM_TICK_SEL)
		tick *= FIELD_GET(INGRESS_SLOW_TICK_RATIO_MASK, val);
	if (!tick)
		return -EINVAL;

	unit = (config & TRTCM_PKT_MODE) ? 1000000 / tick : 8000 / tick;
	if (!unit)
		return -EINVAL;

	rate = rate_val / unit;
	rate_frac = rate_val % unit;
	rate_frac = FIELD_PREP(TRTCM_TOKEN_RATE_MASK, rate_frac) / unit;
	rate = FIELD_PREP(TRTCM_TOKEN_RATE_MASK, rate) |
	       FIELD_PREP(TRTCM_TOKEN_RATE_FRACTION_MASK, rate_frac);

	err = airoha_qdma_set_trtcm_param(qdma, channel, addr,
					  TRTCM_TOKEN_RATE_MODE, mode, rate);
	if (err)
		return err;

	val = max_t(u32, bucket_size, MIN_TOKEN_SIZE);
	val = min_t(u32, __fls(val), MAX_TOKEN_SIZE_OFFSET);

	return airoha_qdma_set_trtcm_param(qdma, channel, addr,
					   TRTCM_BUCKETSIZE_SHIFT_MODE,
					   mode, val);
}

static int airoha_qdma_set_tx_rate_limit(struct net_device *netdev,
					 int channel, u32 rate,
					 u32 bucket_size)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	int i, err;

	for (i = 0; i <= TRTCM_PEAK_MODE; i++) {
		err = airoha_qdma_set_trtcm_config(dev->qdma, channel,
						   REG_EGRESS_TRTCM_CFG, i,
						   !!rate, TRTCM_METER_MODE);
		if (err)
			return err;

		err = airoha_qdma_set_trtcm_token_bucket(dev->qdma, channel,
							 REG_EGRESS_TRTCM_CFG,
							 i, rate, bucket_size);
		if (err)
			return err;
	}

	return 0;
}

static int airoha_tc_htb_modify_queue(struct net_device *dev,
				      struct tc_htb_qopt_offload *opt)
{
	u32 channel = TC_H_MIN(opt->classid) % AIROHA_NUM_QOS_CHANNELS;
	u32 rate = div_u64(opt->rate, 1000) << 3; /* kbps */
	int err;

	if (opt->parent_classid != TC_HTB_CLASSID_ROOT) {
		NL_SET_ERR_MSG_MOD(opt->extack, "invalid parent classid");
		return -EINVAL;
	}

	err = airoha_qdma_set_tx_rate_limit(dev, channel, rate, opt->quantum);
	if (err)
		NL_SET_ERR_MSG_MOD(opt->extack,
				   "failed configuring htb offload");

	return err;
}

static int airoha_tc_htb_alloc_leaf_queue(struct net_device *netdev,
					  struct tc_htb_qopt_offload *opt)
{
	u32 channel = TC_H_MIN(opt->classid) % AIROHA_NUM_QOS_CHANNELS;
	int err, num_tx_queues = AIROHA_NUM_TX_RING + channel + 1;
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_qdma *qdma = dev->qdma;

	/* Here we need to check the requested QDMA channel is not already
	 * in use by another net_device running on the same QDMA block.
	 */
	if (test_and_set_bit(channel, qdma->qos_channel_map)) {
		NL_SET_ERR_MSG_MOD(opt->extack,
				   "qdma qos channel already in use");
		return -EBUSY;
	}

	err = airoha_tc_htb_modify_queue(netdev, opt);
	if (err)
		goto error;

	if (num_tx_queues > netdev->real_num_tx_queues) {
		err = netif_set_real_num_tx_queues(netdev, num_tx_queues);
		if (err) {
			airoha_qdma_set_tx_rate_limit(netdev, channel, 0,
						      opt->quantum);
			NL_SET_ERR_MSG_MOD(opt->extack,
					   "failed setting real_num_tx_queues");
			goto error;
		}
	}

	set_bit(channel, dev->qos_sq_bmap);
	opt->qid = AIROHA_NUM_TX_RING + channel;

	return 0;
error:
	clear_bit(channel, qdma->qos_channel_map);

	return err;
}

static int airoha_qdma_set_rx_meter(struct airoha_gdm_dev *dev,
				    u32 rate, u32 bucket_size,
				    enum trtcm_unit_type unit_type)
{
	struct airoha_qdma *qdma = dev->qdma;
	int i;

	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		int err;

		if (!qdma->q_rx[i].ndesc)
			continue;

		err = airoha_qdma_init_rl_config(qdma, i, !!rate, unit_type);
		if (err)
			return err;

		err = airoha_qdma_set_rl_token_bucket(qdma, i, rate,
						      bucket_size);
		if (err)
			return err;
	}

	return 0;
}

static int airoha_tc_matchall_act_validate(struct tc_cls_matchall_offload *f)
{
	const struct flow_action *actions = &f->rule->action;
	const struct flow_action_entry *act;

	if (!flow_action_has_entries(actions)) {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "filter run with no actions");
		return -EINVAL;
	}

	if (!flow_offload_has_one_action(actions)) {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "only once action per filter is supported");
		return -EOPNOTSUPP;
	}

	act = &actions->entries[0];
	if (act->id != FLOW_ACTION_POLICE) {
		NL_SET_ERR_MSG_MOD(f->common.extack, "unsupported action");
		return -EOPNOTSUPP;
	}

	if (act->police.exceed.act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "invalid exceed action id");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "invalid notexceed action id");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id == FLOW_ACTION_ACCEPT &&
	    !flow_action_is_last_entry(actions, act)) {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "action accept must be last");
		return -EOPNOTSUPP;
	}

	if (act->police.peakrate_bytes_ps || act->police.avrate ||
	    act->police.overhead || act->police.mtu) {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "peakrate/avrate/overhead/mtu unsupported");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int airoha_dev_tc_matchall(struct net_device *netdev,
				  struct tc_cls_matchall_offload *f)
{
	enum trtcm_unit_type unit_type = TRTCM_BYTE_UNIT;
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	u32 rate = 0, bucket_size = 0;

	switch (f->command) {
	case TC_CLSMATCHALL_REPLACE: {
		const struct flow_action_entry *act;
		int err;

		err = airoha_tc_matchall_act_validate(f);
		if (err)
			return err;

		act = &f->rule->action.entries[0];
		if (act->police.rate_pkt_ps) {
			rate = act->police.rate_pkt_ps;
			bucket_size = act->police.burst_pkt;
			unit_type = TRTCM_PACKET_UNIT;
		} else {
			rate = div_u64(act->police.rate_bytes_ps, 1000);
			rate = rate << 3; /* Kbps */
			bucket_size = act->police.burst;
		}
		fallthrough;
	}
	case TC_CLSMATCHALL_DESTROY:
		return airoha_qdma_set_rx_meter(dev, rate, bucket_size,
						unit_type);
	default:
		return -EOPNOTSUPP;
	}
}

static int airoha_dev_setup_tc_block_cb(enum tc_setup_type type,
					void *type_data, void *cb_priv)
{
	struct net_device *netdev = cb_priv;
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	struct airoha_eth *eth = dev->eth;

	if (!tc_can_offload(netdev))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return airoha_ppe_setup_tc_block_cb(&eth->ppe->dev, type_data);
	case TC_SETUP_CLSMATCHALL:
		return airoha_dev_tc_matchall(netdev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static int airoha_dev_setup_tc_block(struct net_device *dev,
				     struct flow_block_offload *f)
{
	flow_setup_cb_t *cb = airoha_dev_setup_tc_block_cb;
	static LIST_HEAD(block_cb_list);
	struct flow_block_cb *block_cb;

	if (f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	f->driver_block_list = &block_cb_list;
	switch (f->command) {
	case FLOW_BLOCK_BIND:
		block_cb = flow_block_cb_lookup(f->block, cb, dev);
		if (block_cb) {
			flow_block_cb_incref(block_cb);
			return 0;
		}
		block_cb = flow_block_cb_alloc(cb, dev, dev, NULL);
		if (IS_ERR(block_cb))
			return PTR_ERR(block_cb);

		flow_block_cb_incref(block_cb);
		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, &block_cb_list);
		return 0;
	case FLOW_BLOCK_UNBIND:
		block_cb = flow_block_cb_lookup(f->block, cb, dev);
		if (!block_cb)
			return -ENOENT;

		if (!flow_block_cb_decref(block_cb)) {
			flow_block_cb_remove(block_cb, f);
			list_del(&block_cb->driver_list);
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static void airoha_tc_remove_htb_queue(struct net_device *netdev, int queue)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	int num_tx_queues = AIROHA_NUM_TX_RING;
	struct airoha_qdma *qdma = dev->qdma;

	airoha_qdma_set_tx_rate_limit(netdev, queue, 0, 0);

	clear_bit(queue, qdma->qos_channel_map);
	clear_bit(queue, dev->qos_sq_bmap);

	if (!bitmap_empty(dev->qos_sq_bmap, AIROHA_NUM_QOS_CHANNELS))
		num_tx_queues += find_last_bit(dev->qos_sq_bmap,
					       AIROHA_NUM_QOS_CHANNELS) + 1;
	netif_set_real_num_tx_queues(netdev, num_tx_queues);
}

static int airoha_tc_htb_delete_leaf_queue(struct net_device *netdev,
					   struct tc_htb_qopt_offload *opt)
{
	u32 channel = TC_H_MIN(opt->classid) % AIROHA_NUM_QOS_CHANNELS;
	struct airoha_gdm_dev *dev = netdev_priv(netdev);

	if (!test_bit(channel, dev->qos_sq_bmap)) {
		NL_SET_ERR_MSG_MOD(opt->extack, "invalid queue id");
		return -EINVAL;
	}

	airoha_tc_remove_htb_queue(netdev, channel);

	return 0;
}

static int airoha_tc_htb_destroy(struct net_device *netdev)
{
	struct airoha_gdm_dev *dev = netdev_priv(netdev);
	int q;

	for_each_set_bit(q, dev->qos_sq_bmap, AIROHA_NUM_QOS_CHANNELS)
		airoha_tc_remove_htb_queue(netdev, q);

	return 0;
}

static int airoha_tc_get_htb_get_leaf_queue(struct net_device *netdev,
					    struct tc_htb_qopt_offload *opt)
{
	u32 channel = TC_H_MIN(opt->classid) % AIROHA_NUM_QOS_CHANNELS;
	struct airoha_gdm_dev *dev = netdev_priv(netdev);

	if (!test_bit(channel, dev->qos_sq_bmap)) {
		NL_SET_ERR_MSG_MOD(opt->extack, "invalid queue id");
		return -EINVAL;
	}

	opt->qid = AIROHA_NUM_TX_RING + channel;

	return 0;
}

static int airoha_tc_setup_qdisc_htb(struct net_device *dev,
				     struct tc_htb_qopt_offload *opt)
{
	switch (opt->command) {
	case TC_HTB_CREATE:
		break;
	case TC_HTB_DESTROY:
		return airoha_tc_htb_destroy(dev);
	case TC_HTB_NODE_MODIFY:
		return airoha_tc_htb_modify_queue(dev, opt);
	case TC_HTB_LEAF_ALLOC_QUEUE:
		return airoha_tc_htb_alloc_leaf_queue(dev, opt);
	case TC_HTB_LEAF_DEL:
	case TC_HTB_LEAF_DEL_LAST:
	case TC_HTB_LEAF_DEL_LAST_FORCE:
		return airoha_tc_htb_delete_leaf_queue(dev, opt);
	case TC_HTB_LEAF_QUERY_QUEUE:
		return airoha_tc_get_htb_get_leaf_queue(dev, opt);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int airoha_dev_tc_setup(struct net_device *dev,
			       enum tc_setup_type type, void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_ETS:
		return airoha_tc_setup_qdisc_ets(dev, type_data);
	case TC_SETUP_QDISC_HTB:
		return airoha_tc_setup_qdisc_htb(dev, type_data);
	case TC_SETUP_BLOCK:
	case TC_SETUP_FT:
		return airoha_dev_setup_tc_block(dev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops airoha_netdev_ops = {
	.ndo_init		= airoha_dev_init,
	.ndo_open		= airoha_dev_open,
	.ndo_stop		= airoha_dev_stop,
	.ndo_change_mtu		= airoha_dev_change_mtu,
	.ndo_select_queue	= airoha_dev_select_queue,
	.ndo_start_xmit		= airoha_dev_xmit,
	.ndo_get_stats64        = airoha_dev_get_stats64,
	.ndo_set_mac_address	= airoha_dev_set_macaddr,
	.ndo_setup_tc		= airoha_dev_tc_setup,
};

static const struct ethtool_ops airoha_ethtool_ops = {
	.get_drvinfo		= airoha_ethtool_get_drvinfo,
	.get_eth_mac_stats      = airoha_ethtool_get_mac_stats,
	.get_rmon_stats		= airoha_ethtool_get_rmon_stats,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.get_link		= ethtool_op_get_link,
};

static int airoha_metadata_dst_alloc(struct airoha_gdm_port *port)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(port->dsa_meta); i++) {
		struct metadata_dst *md_dst;

		md_dst = metadata_dst_alloc(0, METADATA_HW_PORT_MUX,
					    GFP_KERNEL);
		if (!md_dst)
			return -ENOMEM;

		md_dst->u.port_info.port_id = i;
		port->dsa_meta[i] = md_dst;
	}

	return 0;
}

static void airoha_metadata_dst_free(struct airoha_gdm_port *port)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(port->dsa_meta); i++) {
		if (!port->dsa_meta[i])
			continue;

		dst_release(&port->dsa_meta[i]->dst);
	}
}

bool airoha_is_valid_gdm_dev(struct airoha_eth *eth,
			     struct airoha_gdm_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(eth->ports); i++) {
		struct airoha_gdm_port *port = eth->ports[i];
		int j;

		if (!port)
			continue;

		for (j = 0; j < ARRAY_SIZE(port->devs); j++) {
			if (port->devs[j] == dev)
				return true;
		}
	}

	return false;
}

static int airoha_alloc_gdm_device(struct airoha_eth *eth,
				   struct airoha_gdm_port *port,
				   int nbq, struct device_node *np)
{
	struct net_device *netdev;
	struct airoha_gdm_dev *dev;
	u8 index;
	int err;

	netdev = devm_alloc_etherdev_mqs(eth->dev, sizeof(*dev),
					 AIROHA_NUM_NETDEV_TX_RINGS,
					 AIROHA_NUM_RX_RING);
	if (!netdev) {
		dev_err(eth->dev, "alloc_etherdev failed\n");
		return -ENOMEM;
	}

	netdev->netdev_ops = &airoha_netdev_ops;
	netdev->ethtool_ops = &airoha_ethtool_ops;
	netdev->max_mtu = AIROHA_MAX_MTU;
	netdev->watchdog_timeo = 5 * HZ;
	netdev->hw_features = NETIF_F_IP_CSUM | NETIF_F_RXCSUM | NETIF_F_TSO6 |
			      NETIF_F_IPV6_CSUM | NETIF_F_SG | NETIF_F_TSO |
			      NETIF_F_HW_TC;
	netdev->features |= netdev->hw_features;
	netdev->vlan_features = netdev->hw_features;
	SET_NETDEV_DEV(netdev, eth->dev);

	/* reserve hw queues for HTB offloading */
	err = netif_set_real_num_tx_queues(netdev, AIROHA_NUM_TX_RING);
	if (err)
		return err;

	err = of_get_ethdev_address(np, netdev);
	if (err) {
		if (err == -EPROBE_DEFER)
			return err;

		eth_hw_addr_random(netdev);
		dev_info(eth->dev, "generated random MAC address %pM\n",
			 netdev->dev_addr);
	}

	/* Allowed nbq for EN7581 on GDM3 port are 4 and 5 for PCIE0
	 * and PCIE1 respectively.
	 */
	index = nbq;
	if (index && airoha_is_7581(eth) && port->id == AIROHA_GDM3_IDX)
		index -= 4;

	if (index >= ARRAY_SIZE(port->devs) || port->devs[index]) {
		dev_err(eth->dev, "invalid nbq id: %d\n", nbq);
		return -EINVAL;
	}

	netdev->dev.of_node = of_node_get(np);
	dev = netdev_priv(netdev);
	u64_stats_init(&dev->stats.syncp);
	dev->port = port;
	dev->eth = eth;
	dev->nbq = nbq;
	port->devs[index] = dev;

	return 0;
}

static int airoha_alloc_gdm_port(struct airoha_eth *eth,
				 struct device_node *np)
{
	const __be32 *id_ptr = of_get_property(np, "reg", NULL);
	struct airoha_gdm_port *port;
	struct device_node *node;
	int err, nbq, p, d = 0;
	u32 id;

	if (!id_ptr) {
		dev_err(eth->dev, "missing gdm port id\n");
		return -EINVAL;
	}

	id = be32_to_cpup(id_ptr);
	p = id - 1;

	if (!id || id > ARRAY_SIZE(eth->ports)) {
		dev_err(eth->dev, "invalid gdm port id: %d\n", id);
		return -EINVAL;
	}

	if (eth->ports[p]) {
		dev_err(eth->dev, "duplicate gdm port id: %d\n", id);
		return -EINVAL;
	}

	port = devm_kzalloc(eth->dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->id = id;
	spin_lock_init(&port->stats_lock);
	eth->ports[p] = port;

	err = airoha_metadata_dst_alloc(port);
	if (err)
		return err;

	/* Default nbq value to ensure backward compatibility */
	nbq = id == AIROHA_GDM3_IDX && airoha_is_7581(eth) ? 4 : 0;

	for_each_child_of_node(np, node) {
		/* Multiple external serdes connected to the FE GDM port via an
		 * external arbiter.
		 */
		const __be32 *nbq_ptr;

		if (!of_device_is_compatible(node, "airoha,eth-port"))
			continue;

		d++;
		if (!of_device_is_available(node))
			continue;

		nbq_ptr = of_get_property(node, "reg", NULL);
		if (!nbq_ptr) {
			dev_err(eth->dev, "missing nbq id\n");
			of_node_put(node);
			return -EINVAL;
		}

		/* Verify the provided nbq parameter is valid */
		nbq = be32_to_cpup(nbq_ptr);
		err = eth->soc->ops.get_sport(port, nbq);
		if (err < 0) {
			of_node_put(node);
			return err;
		}

		err = airoha_alloc_gdm_device(eth, port, nbq, node);
		if (err) {
			of_node_put(node);
			return err;
		}
	}

	return !d ? airoha_alloc_gdm_device(eth, port, nbq, np) : 0;
}

static int airoha_register_gdm_devices(struct airoha_eth *eth)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(eth->ports); i++) {
		struct airoha_gdm_port *port = eth->ports[i];
		int j;

		if (!port)
			continue;

		for (j = 0; j < ARRAY_SIZE(port->devs); j++) {
			struct airoha_gdm_dev *dev = port->devs[j];
			int err;

			if (!dev)
				continue;

			err = register_netdev(netdev_from_priv(dev));
			if (err)
				return err;
		}
	}

	set_bit(DEV_STATE_REGISTERED, &eth->state);

	return 0;
}

static int airoha_probe(struct platform_device *pdev)
{
	struct reset_control_bulk_data *xsi_rsts;
	struct device_node *np;
	struct airoha_eth *eth;
	int i, err;

	eth = devm_kzalloc(&pdev->dev, sizeof(*eth), GFP_KERNEL);
	if (!eth)
		return -ENOMEM;

	eth->soc = of_device_get_match_data(&pdev->dev);
	if (!eth->soc)
		return -EINVAL;

	eth->dev = &pdev->dev;

	err = dma_set_mask_and_coherent(eth->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(eth->dev, "failed configuring DMA mask\n");
		return err;
	}

	eth->fe_regs = devm_platform_ioremap_resource_byname(pdev, "fe");
	if (IS_ERR(eth->fe_regs))
		return dev_err_probe(eth->dev, PTR_ERR(eth->fe_regs),
				     "failed to iomap fe regs\n");

	eth->rsts[0].id = "fe";
	eth->rsts[1].id = "pdma";
	eth->rsts[2].id = "qdma";
	err = devm_reset_control_bulk_get_exclusive(eth->dev,
						    ARRAY_SIZE(eth->rsts),
						    eth->rsts);
	if (err) {
		dev_err(eth->dev, "failed to get bulk reset lines\n");
		return err;
	}

	xsi_rsts = devm_kcalloc(eth->dev,
				eth->soc->num_xsi_rsts, sizeof(*xsi_rsts),
				GFP_KERNEL);
	if (!xsi_rsts)
		return -ENOMEM;

	eth->xsi_rsts = xsi_rsts;
	for (i = 0; i < eth->soc->num_xsi_rsts; i++)
		eth->xsi_rsts[i].id = eth->soc->xsi_rsts_names[i];

	err = devm_reset_control_bulk_get_exclusive(eth->dev,
						    eth->soc->num_xsi_rsts,
						    eth->xsi_rsts);
	if (err) {
		dev_err(eth->dev, "failed to get bulk xsi reset lines\n");
		return err;
	}

	eth->napi_dev = alloc_netdev_dummy(0);
	if (!eth->napi_dev)
		return -ENOMEM;

	/* Enable threaded NAPI by default */
	eth->napi_dev->threaded = true;
	strscpy(eth->napi_dev->name, "qdma_eth", sizeof(eth->napi_dev->name));
	platform_set_drvdata(pdev, eth);

	err = airoha_hw_init(pdev, eth);
	if (err)
		goto error_netdev_free;

	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++) {
		airoha_qdma_start_napi(&eth->qdma[i]);
		airoha_qdma_set(&eth->qdma[i], REG_QDMA_GLOBAL_CFG,
				GLOBAL_CFG_TX_DMA_EN_MASK |
				GLOBAL_CFG_RX_DMA_EN_MASK);
	}

	for_each_child_of_node(pdev->dev.of_node, np) {
		if (!of_device_is_compatible(np, "airoha,eth-mac"))
			continue;

		if (!of_device_is_available(np))
			continue;

		err = airoha_alloc_gdm_port(eth, np);
		if (err) {
			of_node_put(np);
			goto error_napi_stop;
		}
	}

	err = airoha_register_gdm_devices(eth);
	if (err)
		goto error_napi_stop;

	return 0;

error_napi_stop:
	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++) {
		airoha_qdma_stop_napi(&eth->qdma[i]);
		airoha_qdma_tx_cleanup(&eth->qdma[i]);
	}

	for (i = 0; i < ARRAY_SIZE(eth->ports); i++) {
		struct airoha_gdm_port *port = eth->ports[i];
		int j;

		if (!port)
			continue;

		for (j = 0; j < ARRAY_SIZE(port->devs); j++) {
			struct airoha_gdm_dev *dev = port->devs[j];
			struct net_device *netdev;

			if (!dev)
				continue;

			netdev = netdev_from_priv(dev);
			if (netdev->reg_state == NETREG_REGISTERED)
				unregister_netdev(netdev);
			of_node_put(netdev->dev.of_node);
		}
		airoha_metadata_dst_free(port);
	}
	airoha_hw_cleanup(eth);
error_netdev_free:
	free_netdev(eth->napi_dev);
	platform_set_drvdata(pdev, NULL);

	return err;
}

static void airoha_remove(struct platform_device *pdev)
{
	struct airoha_eth *eth = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++) {
		airoha_qdma_stop_napi(&eth->qdma[i]);
		airoha_qdma_tx_cleanup(&eth->qdma[i]);
	}

	for (i = 0; i < ARRAY_SIZE(eth->ports); i++) {
		struct airoha_gdm_port *port = eth->ports[i];
		int j;

		if (!port)
			continue;

		for (j = 0; j < ARRAY_SIZE(port->devs); j++) {
			struct airoha_gdm_dev *dev = port->devs[j];
			struct net_device *netdev;

			if (!dev)
				continue;

			netdev = netdev_from_priv(dev);
			unregister_netdev(netdev);
			of_node_put(netdev->dev.of_node);
		}
		airoha_metadata_dst_free(port);
	}
	airoha_hw_cleanup(eth);

	free_netdev(eth->napi_dev);
	platform_set_drvdata(pdev, NULL);
}

static const char * const en7581_xsi_rsts_names[] = {
	"xsi-mac",
	"hsi0-mac",
	"hsi1-mac",
	"hsi-mac",
	"xfp-mac",
};

static int airoha_en7581_get_sport(struct airoha_gdm_port *port, int nbq)
{
	switch (port->id) {
	case AIROHA_GDM3_IDX:
		/* 7581 SoC supports PCIe serdes on GDM3 port */
		if (nbq == 4)
			return HSGMII_LAN_7581_PCIE0_SRCPORT;
		if (nbq == 5)
			return HSGMII_LAN_7581_PCIE1_SRCPORT;
		break;
	case AIROHA_GDM4_IDX:
		/* 7581 SoC supports eth and usb serdes on GDM4 port */
		if (!nbq)
			return HSGMII_LAN_7581_ETH_SRCPORT;
		if (nbq == 1)
			return HSGMII_LAN_7581_USB_SRCPORT;
		break;
	default:
		break;
	}

	return -EINVAL;
}

static u32 airoha_en7581_get_vip_port(struct airoha_gdm_port *port, int nbq)
{
	switch (port->id) {
	case AIROHA_GDM3_IDX:
		if (nbq == 4)
			return XSI_PCIE0_VIP_PORT_MASK;
		if (nbq == 5)
			return XSI_PCIE1_VIP_PORT_MASK;
		break;
	case AIROHA_GDM4_IDX:
		if (!nbq)
			return XSI_ETH_VIP_PORT_MASK;
		if (nbq == 1)
			return XSI_USB_VIP_PORT_MASK;
		break;
	default:
		break;
	}

	return 0;
}

static int airoha_en7581_get_dev_from_sport(struct airoha_qdma_desc *desc,
					    u16 *port, u16 *dev)
{
	u32 sport = FIELD_GET(QDMA_ETH_RXMSG_SPORT_MASK,
			      le32_to_cpu(READ_ONCE(desc->msg1)));

	*dev = 0;
	switch (sport) {
	case 0x10 ... 0x14:
		*port = 0; /* GDM1 */
		break;
	case 0x2 ... 0x4:
		*port = sport - 1;
		break;
	case HSGMII_LAN_7581_PCIE1_SRCPORT:
		*dev = 1;
		fallthrough;
	case HSGMII_LAN_7581_PCIE0_SRCPORT:
		*port = 2; /* GDM3 */
		break;
	case HSGMII_LAN_7581_USB_SRCPORT:
		*dev = 1;
		fallthrough;
	case HSGMII_LAN_7581_ETH_SRCPORT:
		*port = 3; /* GDM4 */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const char * const an7583_xsi_rsts_names[] = {
	"xsi-mac",
	"hsi0-mac",
	"hsi1-mac",
	"xfp-mac",
};

static int airoha_an7583_get_sport(struct airoha_gdm_port *port, int nbq)
{
	switch (port->id) {
	case AIROHA_GDM3_IDX:
		/* 7583 SoC supports eth serdes on GDM3 port */
		if (!nbq)
			return HSGMII_LAN_7583_ETH_SRCPORT;
		break;
	case AIROHA_GDM4_IDX:
		/* 7583 SoC supports PCIe and USB serdes on GDM4 port */
		if (!nbq)
			return HSGMII_LAN_7583_PCIE_SRCPORT;
		if (nbq == 1)
			return HSGMII_LAN_7583_USB_SRCPORT;
		break;
	default:
		break;
	}

	return -EINVAL;
}

static u32 airoha_an7583_get_vip_port(struct airoha_gdm_port *port, int nbq)
{
	switch (port->id) {
	case AIROHA_GDM3_IDX:
		if (!nbq)
			return XSI_ETH_VIP_PORT_MASK;
		break;
	case AIROHA_GDM4_IDX:
		if (!nbq)
			return XSI_PCIE0_VIP_PORT_MASK;
		if (nbq == 1)
			return XSI_USB_VIP_PORT_MASK;
		break;
	default:
		break;
	}

	return 0;
}

static int airoha_an7583_get_dev_from_sport(struct airoha_qdma_desc *desc,
					    u16 *port, u16 *dev)
{
	u32 sport = FIELD_GET(QDMA_ETH_RXMSG_SPORT_MASK,
			      le32_to_cpu(READ_ONCE(desc->msg1)));

	*dev = 0;
	switch (sport) {
	case 0x10 ... 0x14:
		*port = 0; /* GDM1 */
		break;
	case 0x2 ... 0x4:
		*port = sport - 1;
		break;
	case HSGMII_LAN_7583_ETH_SRCPORT:
		*port = 2; /* GDM3 */
		break;
	case HSGMII_LAN_7583_USB_SRCPORT:
		*dev = 1;
		fallthrough;
	case HSGMII_LAN_7583_PCIE_SRCPORT:
		*port = 3; /* GDM4 */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct airoha_eth_soc_data en7581_soc_data = {
	.version = 0x7581,
	.xsi_rsts_names = en7581_xsi_rsts_names,
	.num_xsi_rsts = ARRAY_SIZE(en7581_xsi_rsts_names),
	.num_ppe = 2,
	.ops = {
		.get_sport = airoha_en7581_get_sport,
		.get_vip_port = airoha_en7581_get_vip_port,
		.get_dev_from_sport = airoha_en7581_get_dev_from_sport,
	},
};

static const struct airoha_eth_soc_data an7583_soc_data = {
	.version = 0x7583,
	.xsi_rsts_names = an7583_xsi_rsts_names,
	.num_xsi_rsts = ARRAY_SIZE(an7583_xsi_rsts_names),
	.num_ppe = 1,
	.ops = {
		.get_sport = airoha_an7583_get_sport,
		.get_vip_port = airoha_an7583_get_vip_port,
		.get_dev_from_sport = airoha_an7583_get_dev_from_sport,
	},
};

static const struct of_device_id of_airoha_match[] = {
	{ .compatible = "airoha,en7581-eth", .data = &en7581_soc_data },
	{ .compatible = "airoha,an7583-eth", .data = &an7583_soc_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_airoha_match);

static struct platform_driver airoha_driver = {
	.probe = airoha_probe,
	.remove = airoha_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_airoha_match,
	},
};
module_platform_driver(airoha_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_DESCRIPTION("Ethernet driver for Airoha SoC");
