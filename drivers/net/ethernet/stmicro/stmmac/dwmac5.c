// SPDX-License-Identifier: (GPL-2.0 OR MIT)
// Copyright (c) 2017 Synopsys, Inc. and/or its affiliates.
// stmmac Support for 5.xx Ethernet QoS cores

#include <linux/bitops.h>
#include <linux/iopoll.h>
#include "common.h"
#include "dwmac4.h"
#include "dwmac5.h"

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

int dwmac5_safety_feat_config(void __iomem *ioaddr, unsigned int asp)
{
	u32 value;

	if (!asp)
		return -EINVAL;

	/* 1. Enable Safety Features */
	value = readl(ioaddr + MTL_ECC_CONTROL);
	value |= TSOEE; /* TSO ECC */
	value |= MRXPEE; /* MTL RX Parser ECC */
	value |= MESTEE; /* MTL EST ECC */
	value |= MRXEE; /* MTL RX FIFO ECC */
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
	value |= PRTYEN; /* FSM Parity Feature */
	value |= TMOUTEN; /* FSM Timeout Feature */
	writel(value, ioaddr + MAC_FSM_CONTROL);

	/* 4. Enable Data Parity Protection */
	value = readl(ioaddr + MTL_DPP_CONTROL);
	value |= EDPP;
	writel(value, ioaddr + MTL_DPP_CONTROL);

	/*
	 * All the Automotive Safety features are selected without the "Parity
	 * Port Enable for external interface" feature.
	 */
	if (asp <= 0x2)
		return 0;

	value |= EPSI;
	writel(value, ioaddr + MTL_DPP_CONTROL);
	return 0;
}

bool dwmac5_safety_feat_irq_status(struct net_device *ndev,
		void __iomem *ioaddr, unsigned int asp,
		struct stmmac_safety_stats *stats)
{
	bool ret = false, err, corr;
	u32 mtl, dma;

	if (!asp)
		return false;

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

const char *dwmac5_safety_feat_dump(struct stmmac_safety_stats *stats,
			int index, unsigned long *count)
{
	int module = index / 32, offset = index % 32;
	unsigned long *ptr = (unsigned long *)stats;

	if (module >= ARRAY_SIZE(dwmac5_all_errors))
		return NULL;
	if (!dwmac5_all_errors[module].desc[offset].valid)
		return NULL;
	if (count)
		*count = *(ptr + index);
	return dwmac5_all_errors[module].desc[offset].desc;
}
