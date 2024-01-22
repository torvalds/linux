// SPDX-License-Identifier: (GPL-2.0 OR MIT)
// Copyright (c) 2017 Synopsys, Inc. and/or its affiliates.
// stmmac Support for 5.xx Ethernet QoS cores

#include <linux/bitops.h>
#include <linux/iopoll.h>
#include "common.h"
#include "dwmac4.h"
#include "dwmac5.h"
#include "stmmac.h"
#include "stmmac_ptp.h"

struct dwmac5_error_desc {
	bool valid;
	const char *desc;
	const char *detailed_desc;
};

#define STAT_OFF(field)		offsetof(struct stmmac_safety_stats, field)

static void dwmac5_log_error(struct net_device *ndev, u32 value, bool corr,
		const char *module_name, const struct dwmac5_error_desc *desc,
		unsigned long field_offset, struct stmmac_safety_stats *stats)
{
	unsigned long loc, mask;
	u8 *bptr = (u8 *)stats;
	unsigned long *ptr;

	ptr = (unsigned long *)(bptr + field_offset);

	mask = value;
	for_each_set_bit(loc, &mask, 32) {
		netdev_err(ndev, "Found %s error in %s: '%s: %s'\n", corr ?
				"correctable" : "uncorrectable", module_name,
				desc[loc].desc, desc[loc].detailed_desc);

		/* Update counters */
		ptr[loc]++;
	}
}

static const struct dwmac5_error_desc dwmac5_mac_errors[32]= {
	{ true, "ATPES", "Application Transmit Interface Parity Check Error" },
	{ true, "TPES", "TSO Data Path Parity Check Error" },
	{ true, "RDPES", "Read Descriptor Parity Check Error" },
	{ true, "MPES", "MTL Data Path Parity Check Error" },
	{ true, "MTSPES", "MTL TX Status Data Path Parity Check Error" },
	{ true, "ARPES", "Application Receive Interface Data Path Parity Check Error" },
	{ true, "CWPES", "CSR Write Data Path Parity Check Error" },
	{ true, "ASRPES", "AXI Slave Read Data Path Parity Check Error" },
	{ true, "TTES", "TX FSM Timeout Error" },
	{ true, "RTES", "RX FSM Timeout Error" },
	{ true, "CTES", "CSR FSM Timeout Error" },
	{ true, "ATES", "APP FSM Timeout Error" },
	{ true, "PTES", "PTP FSM Timeout Error" },
	{ true, "T125ES", "TX125 FSM Timeout Error" },
	{ true, "R125ES", "RX125 FSM Timeout Error" },
	{ true, "RVCTES", "REV MDC FSM Timeout Error" },
	{ true, "MSTTES", "Master Read/Write Timeout Error" },
	{ true, "SLVTES", "Slave Read/Write Timeout Error" },
	{ true, "ATITES", "Application Timeout on ATI Interface Error" },
	{ true, "ARITES", "Application Timeout on ARI Interface Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 20 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 21 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 22 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 23 */
	{ true, "FSMPES", "FSM State Parity Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 25 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 26 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 27 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 28 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 29 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 30 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 31 */
};

static void dwmac5_handle_mac_err(struct net_device *ndev,
		void __iomem *ioaddr, bool correctable,
		struct stmmac_safety_stats *stats)
{
	u32 value;

	value = readl(ioaddr + MAC_DPP_FSM_INT_STATUS);
	writel(value, ioaddr + MAC_DPP_FSM_INT_STATUS);

	dwmac5_log_error(ndev, value, correctable, "MAC", dwmac5_mac_errors,
			STAT_OFF(mac_errors), stats);
}

static const struct dwmac5_error_desc dwmac5_mtl_errors[32]= {
	{ true, "TXCES", "MTL TX Memory Error" },
	{ true, "TXAMS", "MTL TX Memory Address Mismatch Error" },
	{ true, "TXUES", "MTL TX Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 3 */
	{ true, "RXCES", "MTL RX Memory Error" },
	{ true, "RXAMS", "MTL RX Memory Address Mismatch Error" },
	{ true, "RXUES", "MTL RX Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 7 */
	{ true, "ECES", "MTL EST Memory Error" },
	{ true, "EAMS", "MTL EST Memory Address Mismatch Error" },
	{ true, "EUES", "MTL EST Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 11 */
	{ true, "RPCES", "MTL RX Parser Memory Error" },
	{ true, "RPAMS", "MTL RX Parser Memory Address Mismatch Error" },
	{ true, "RPUES", "MTL RX Parser Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 15 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 16 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 17 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 18 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 19 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 20 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 21 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 22 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 23 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 24 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 25 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 26 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 27 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 28 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 29 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 30 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 31 */
};

static void dwmac5_handle_mtl_err(struct net_device *ndev,
		void __iomem *ioaddr, bool correctable,
		struct stmmac_safety_stats *stats)
{
	u32 value;

	value = readl(ioaddr + MTL_ECC_INT_STATUS);
	writel(value, ioaddr + MTL_ECC_INT_STATUS);

	dwmac5_log_error(ndev, value, correctable, "MTL", dwmac5_mtl_errors,
			STAT_OFF(mtl_errors), stats);
}

static const struct dwmac5_error_desc dwmac5_dma_errors[32]= {
	{ true, "TCES", "DMA TSO Memory Error" },
	{ true, "TAMS", "DMA TSO Memory Address Mismatch Error" },
	{ true, "TUES", "DMA TSO Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 3 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 4 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 5 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 6 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 7 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 8 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 9 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 10 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 11 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 12 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 13 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 14 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 15 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 16 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 17 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 18 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 19 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 20 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 21 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 22 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 23 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 24 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 25 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 26 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 27 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 28 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 29 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 30 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 31 */
};

static void dwmac5_handle_dma_err(struct net_device *ndev,
		void __iomem *ioaddr, bool correctable,
		struct stmmac_safety_stats *stats)
{
	u32 value;

	value = readl(ioaddr + DMA_ECC_INT_STATUS);
	writel(value, ioaddr + DMA_ECC_INT_STATUS);

	dwmac5_log_error(ndev, value, correctable, "DMA", dwmac5_dma_errors,
			STAT_OFF(dma_errors), stats);
}

int dwmac5_safety_feat_config(void __iomem *ioaddr, unsigned int asp,
			      struct stmmac_safety_feature_cfg *safety_feat_cfg)
{
	struct stmmac_safety_feature_cfg all_safety_feats = {
		.tsoee = 1,
		.mrxpee = 1,
		.mestee = 1,
		.mrxee = 1,
		.mtxee = 1,
		.epsi = 1,
		.edpp = 1,
		.prtyen = 1,
		.tmouten = 1,
	};
	u32 value;

	if (!asp)
		return -EINVAL;

	if (!safety_feat_cfg)
		safety_feat_cfg = &all_safety_feats;

	/* 1. Enable Safety Features */
	value = readl(ioaddr + MTL_ECC_CONTROL);
	value |= MEEAO; /* MTL ECC Error Addr Status Override */
	if (safety_feat_cfg->tsoee)
		value |= TSOEE; /* TSO ECC */
	if (safety_feat_cfg->mrxpee)
		value |= MRXPEE; /* MTL RX Parser ECC */
	if (safety_feat_cfg->mestee)
		value |= MESTEE; /* MTL EST ECC */
	if (safety_feat_cfg->mrxee)
		value |= MRXEE; /* MTL RX FIFO ECC */
	if (safety_feat_cfg->mtxee)
		value |= MTXEE; /* MTL TX FIFO ECC */
	writel(value, ioaddr + MTL_ECC_CONTROL);

	/* 2. Enable MTL Safety Interrupts */
	value = readl(ioaddr + MTL_ECC_INT_ENABLE);
	value |= RPCEIE; /* RX Parser Memory Correctable Error */
	value |= ECEIE; /* EST Memory Correctable Error */
	value |= RXCEIE; /* RX Memory Correctable Error */
	value |= TXCEIE; /* TX Memory Correctable Error */
	writel(value, ioaddr + MTL_ECC_INT_ENABLE);

	/* 3. Enable DMA Safety Interrupts */
	value = readl(ioaddr + DMA_ECC_INT_ENABLE);
	value |= TCEIE; /* TSO Memory Correctable Error */
	writel(value, ioaddr + DMA_ECC_INT_ENABLE);

	/* Only ECC Protection for External Memory feature is selected */
	if (asp <= 0x1)
		return 0;

	/* 5. Enable Parity and Timeout for FSM */
	value = readl(ioaddr + MAC_FSM_CONTROL);
	if (safety_feat_cfg->prtyen)
		value |= PRTYEN; /* FSM Parity Feature */
	if (safety_feat_cfg->tmouten)
		value |= TMOUTEN; /* FSM Timeout Feature */
	writel(value, ioaddr + MAC_FSM_CONTROL);

	/* 4. Enable Data Parity Protection */
	value = readl(ioaddr + MTL_DPP_CONTROL);
	if (safety_feat_cfg->edpp)
		value |= EDPP;
	writel(value, ioaddr + MTL_DPP_CONTROL);

	/*
	 * All the Automotive Safety features are selected without the "Parity
	 * Port Enable for external interface" feature.
	 */
	if (asp <= 0x2)
		return 0;

	if (safety_feat_cfg->epsi)
		value |= EPSI;
	writel(value, ioaddr + MTL_DPP_CONTROL);
	return 0;
}

int dwmac5_safety_feat_irq_status(struct net_device *ndev,
		void __iomem *ioaddr, unsigned int asp,
		struct stmmac_safety_stats *stats)
{
	bool err, corr;
	u32 mtl, dma;
	int ret = 0;

	if (!asp)
		return -EINVAL;

	mtl = readl(ioaddr + MTL_SAFETY_INT_STATUS);
	dma = readl(ioaddr + DMA_SAFETY_INT_STATUS);

	err = (mtl & MCSIS) || (dma & MCSIS);
	corr = false;
	if (err) {
		dwmac5_handle_mac_err(ndev, ioaddr, corr, stats);
		ret |= !corr;
	}

	err = (mtl & (MEUIS | MECIS)) || (dma & (MSUIS | MSCIS));
	corr = (mtl & MECIS) || (dma & MSCIS);
	if (err) {
		dwmac5_handle_mtl_err(ndev, ioaddr, corr, stats);
		ret |= !corr;
	}

	err = dma & (DEUIS | DECIS);
	corr = dma & DECIS;
	if (err) {
		dwmac5_handle_dma_err(ndev, ioaddr, corr, stats);
		ret |= !corr;
	}

	return ret;
}

static const struct dwmac5_error {
	const struct dwmac5_error_desc *desc;
} dwmac5_all_errors[] = {
	{ dwmac5_mac_errors },
	{ dwmac5_mtl_errors },
	{ dwmac5_dma_errors },
};

int dwmac5_safety_feat_dump(struct stmmac_safety_stats *stats,
			int index, unsigned long *count, const char **desc)
{
	int module = index / 32, offset = index % 32;
	unsigned long *ptr = (unsigned long *)stats;

	if (module >= ARRAY_SIZE(dwmac5_all_errors))
		return -EINVAL;
	if (!dwmac5_all_errors[module].desc[offset].valid)
		return -EINVAL;
	if (count)
		*count = *(ptr + index);
	if (desc)
		*desc = dwmac5_all_errors[module].desc[offset].desc;
	return 0;
}

static int dwmac5_rxp_disable(void __iomem *ioaddr)
{
	u32 val;

	val = readl(ioaddr + MTL_OPERATION_MODE);
	val &= ~MTL_FRPE;
	writel(val, ioaddr + MTL_OPERATION_MODE);

	return readl_poll_timeout(ioaddr + MTL_RXP_CONTROL_STATUS, val,
			val & RXPI, 1, 10000);
}

static void dwmac5_rxp_enable(void __iomem *ioaddr)
{
	u32 val;

	val = readl(ioaddr + MTL_OPERATION_MODE);
	val |= MTL_FRPE;
	writel(val, ioaddr + MTL_OPERATION_MODE);
}

static int dwmac5_rxp_update_single_entry(void __iomem *ioaddr,
					  struct stmmac_tc_entry *entry,
					  int pos)
{
	int ret, i;

	for (i = 0; i < (sizeof(entry->val) / sizeof(u32)); i++) {
		int real_pos = pos * (sizeof(entry->val) / sizeof(u32)) + i;
		u32 val;

		/* Wait for ready */
		ret = readl_poll_timeout(ioaddr + MTL_RXP_IACC_CTRL_STATUS,
				val, !(val & STARTBUSY), 1, 10000);
		if (ret)
			return ret;

		/* Write data */
		val = *((u32 *)&entry->val + i);
		writel(val, ioaddr + MTL_RXP_IACC_DATA);

		/* Write pos */
		val = real_pos & ADDR;
		writel(val, ioaddr + MTL_RXP_IACC_CTRL_STATUS);

		/* Write OP */
		val |= WRRDN;
		writel(val, ioaddr + MTL_RXP_IACC_CTRL_STATUS);

		/* Start Write */
		val |= STARTBUSY;
		writel(val, ioaddr + MTL_RXP_IACC_CTRL_STATUS);

		/* Wait for done */
		ret = readl_poll_timeout(ioaddr + MTL_RXP_IACC_CTRL_STATUS,
				val, !(val & STARTBUSY), 1, 10000);
		if (ret)
			return ret;
	}

	return 0;
}

static struct stmmac_tc_entry *
dwmac5_rxp_get_next_entry(struct stmmac_tc_entry *entries, unsigned int count,
			  u32 curr_prio)
{
	struct stmmac_tc_entry *entry;
	u32 min_prio = ~0x0;
	int i, min_prio_idx;
	bool found = false;

	for (i = count - 1; i >= 0; i--) {
		entry = &entries[i];

		/* Do not update unused entries */
		if (!entry->in_use)
			continue;
		/* Do not update already updated entries (i.e. fragments) */
		if (entry->in_hw)
			continue;
		/* Let last entry be updated last */
		if (entry->is_last)
			continue;
		/* Do not return fragments */
		if (entry->is_frag)
			continue;
		/* Check if we already checked this prio */
		if (entry->prio < curr_prio)
			continue;
		/* Check if this is the minimum prio */
		if (entry->prio < min_prio) {
			min_prio = entry->prio;
			min_prio_idx = i;
			found = true;
		}
	}

	if (found)
		return &entries[min_prio_idx];
	return NULL;
}

int dwmac5_rxp_config(void __iomem *ioaddr, struct stmmac_tc_entry *entries,
		      unsigned int count)
{
	struct stmmac_tc_entry *entry, *frag;
	int i, ret, nve = 0;
	u32 curr_prio = 0;
	u32 old_val, val;

	/* Force disable RX */
	old_val = readl(ioaddr + GMAC_CONFIG);
	val = old_val & ~GMAC_CONFIG_RE;
	writel(val, ioaddr + GMAC_CONFIG);

	/* Disable RX Parser */
	ret = dwmac5_rxp_disable(ioaddr);
	if (ret)
		goto re_enable;

	/* Set all entries as NOT in HW */
	for (i = 0; i < count; i++) {
		entry = &entries[i];
		entry->in_hw = false;
	}

	/* Update entries by reverse order */
	while (1) {
		entry = dwmac5_rxp_get_next_entry(entries, count, curr_prio);
		if (!entry)
			break;

		curr_prio = entry->prio;
		frag = entry->frag_ptr;

		/* Set special fragment requirements */
		if (frag) {
			entry->val.af = 0;
			entry->val.rf = 0;
			entry->val.nc = 1;
			entry->val.ok_index = nve + 2;
		}

		ret = dwmac5_rxp_update_single_entry(ioaddr, entry, nve);
		if (ret)
			goto re_enable;

		entry->table_pos = nve++;
		entry->in_hw = true;

		if (frag && !frag->in_hw) {
			ret = dwmac5_rxp_update_single_entry(ioaddr, frag, nve);
			if (ret)
				goto re_enable;
			frag->table_pos = nve++;
			frag->in_hw = true;
		}
	}

	if (!nve)
		goto re_enable;

	/* Update all pass entry */
	for (i = 0; i < count; i++) {
		entry = &entries[i];
		if (!entry->is_last)
			continue;

		ret = dwmac5_rxp_update_single_entry(ioaddr, entry, nve);
		if (ret)
			goto re_enable;

		entry->table_pos = nve++;
	}

	/* Assume n. of parsable entries == n. of valid entries */
	val = (nve << 16) & NPE;
	val |= nve & NVE;
	writel(val, ioaddr + MTL_RXP_CONTROL_STATUS);

	/* Enable RX Parser */
	dwmac5_rxp_enable(ioaddr);

re_enable:
	/* Re-enable RX */
	writel(old_val, ioaddr + GMAC_CONFIG);
	return ret;
}

int dwmac5_flex_pps_config(void __iomem *ioaddr, int index,
			   struct stmmac_pps_cfg *cfg, bool enable,
			   u32 sub_second_inc, u32 systime_flags)
{
	u32 tnsec = readl(ioaddr + MAC_PPSx_TARGET_TIME_NSEC(index));
	u32 val = readl(ioaddr + MAC_PPS_CONTROL);
	u64 period;

	if (!cfg->available)
		return -EINVAL;
	if (tnsec & TRGTBUSY0)
		return -EBUSY;
	if (!sub_second_inc || !systime_flags)
		return -EINVAL;

	val &= ~PPSx_MASK(index);

	if (!enable) {
		val |= PPSCMDx(index, 0x5);
		val |= PPSEN0;
		writel(val, ioaddr + MAC_PPS_CONTROL);
		return 0;
	}

	val |= TRGTMODSELx(index, 0x2);
	val |= PPSEN0;
	writel(val, ioaddr + MAC_PPS_CONTROL);

	writel(cfg->start.tv_sec, ioaddr + MAC_PPSx_TARGET_TIME_SEC(index));

	if (!(systime_flags & PTP_TCR_TSCTRLSSR))
		cfg->start.tv_nsec = (cfg->start.tv_nsec * 1000) / 465;
	writel(cfg->start.tv_nsec, ioaddr + MAC_PPSx_TARGET_TIME_NSEC(index));

	period = cfg->period.tv_sec * 1000000000;
	period += cfg->period.tv_nsec;

	do_div(period, sub_second_inc);

	if (period <= 1)
		return -EINVAL;

	writel(period - 1, ioaddr + MAC_PPSx_INTERVAL(index));

	period >>= 1;
	if (period <= 1)
		return -EINVAL;

	writel(period - 1, ioaddr + MAC_PPSx_WIDTH(index));

	/* Finally, activate it */
	val |= PPSCMDx(index, 0x2);
	writel(val, ioaddr + MAC_PPS_CONTROL);
	return 0;
}

void dwmac5_fpe_configure(void __iomem *ioaddr, struct stmmac_fpe_cfg *cfg,
			  u32 num_txq, u32 num_rxq,
			  bool enable)
{
	u32 value;

	if (enable) {
		cfg->fpe_csr = EFPE;
		value = readl(ioaddr + GMAC_RXQ_CTRL1);
		value &= ~GMAC_RXQCTRL_FPRQ;
		value |= (num_rxq - 1) << GMAC_RXQCTRL_FPRQ_SHIFT;
		writel(value, ioaddr + GMAC_RXQ_CTRL1);
	} else {
		cfg->fpe_csr = 0;
	}
	writel(cfg->fpe_csr, ioaddr + MAC_FPE_CTRL_STS);
}

int dwmac5_fpe_irq_status(void __iomem *ioaddr, struct net_device *dev)
{
	u32 value;
	int status;

	status = FPE_EVENT_UNKNOWN;

	/* Reads from the MAC_FPE_CTRL_STS register should only be performed
	 * here, since the status flags of MAC_FPE_CTRL_STS are "clear on read"
	 */
	value = readl(ioaddr + MAC_FPE_CTRL_STS);

	if (value & TRSP) {
		status |= FPE_EVENT_TRSP;
		netdev_info(dev, "FPE: Respond mPacket is transmitted\n");
	}

	if (value & TVER) {
		status |= FPE_EVENT_TVER;
		netdev_info(dev, "FPE: Verify mPacket is transmitted\n");
	}

	if (value & RRSP) {
		status |= FPE_EVENT_RRSP;
		netdev_info(dev, "FPE: Respond mPacket is received\n");
	}

	if (value & RVER) {
		status |= FPE_EVENT_RVER;
		netdev_info(dev, "FPE: Verify mPacket is received\n");
	}

	return status;
}

void dwmac5_fpe_send_mpacket(void __iomem *ioaddr, struct stmmac_fpe_cfg *cfg,
			     enum stmmac_mpacket_type type)
{
	u32 value = cfg->fpe_csr;

	if (type == MPACKET_VERIFY)
		value |= SVER;
	else if (type == MPACKET_RESPONSE)
		value |= SRSP;

	writel(value, ioaddr + MAC_FPE_CTRL_STS);
}
