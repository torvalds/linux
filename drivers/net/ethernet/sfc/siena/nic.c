// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/cpu_rmap.h>
#include "net_driver.h"
#include "bitfield.h"
#include "efx.h"
#include "nic.h"
#include "farch_regs.h"
#include "io.h"
#include "workarounds.h"
#include "mcdi_pcol.h"

/**************************************************************************
 *
 * Generic buffer handling
 * These buffers are used for interrupt status, MAC stats, etc.
 *
 **************************************************************************/

int efx_siena_alloc_buffer(struct efx_nic *efx, struct efx_buffer *buffer,
			   unsigned int len, gfp_t gfp_flags)
{
	buffer->addr = dma_alloc_coherent(&efx->pci_dev->dev, len,
					  &buffer->dma_addr, gfp_flags);
	if (!buffer->addr)
		return -ENOMEM;
	buffer->len = len;
	return 0;
}

void efx_siena_free_buffer(struct efx_nic *efx, struct efx_buffer *buffer)
{
	if (buffer->addr) {
		dma_free_coherent(&efx->pci_dev->dev, buffer->len,
				  buffer->addr, buffer->dma_addr);
		buffer->addr = NULL;
	}
}

/* Check whether an event is present in the eventq at the current
 * read pointer.  Only useful for self-test.
 */
bool efx_siena_event_present(struct efx_channel *channel)
{
	return efx_event_present(efx_event(channel, channel->eventq_read_ptr));
}

void efx_siena_event_test_start(struct efx_channel *channel)
{
	channel->event_test_cpu = -1;
	smp_wmb();
	channel->efx->type->ev_test_generate(channel);
}

int efx_siena_irq_test_start(struct efx_nic *efx)
{
	efx->last_irq_cpu = -1;
	smp_wmb();
	return efx->type->irq_test_generate(efx);
}

/* Hook interrupt handler(s)
 * Try MSI and then legacy interrupts.
 */
int efx_siena_init_interrupt(struct efx_nic *efx)
{
	struct efx_channel *channel;
	unsigned int n_irqs;
	int rc;

	if (!EFX_INT_MODE_USE_MSI(efx)) {
		rc = request_irq(efx->legacy_irq,
				 efx->type->irq_handle_legacy, IRQF_SHARED,
				 efx->name, efx);
		if (rc) {
			netif_err(efx, drv, efx->net_dev,
				  "failed to hook legacy IRQ %d\n",
				  efx->pci_dev->irq);
			goto fail1;
		}
		efx->irqs_hooked = true;
		return 0;
	}

#ifdef CONFIG_RFS_ACCEL
	if (efx->interrupt_mode == EFX_INT_MODE_MSIX) {
		efx->net_dev->rx_cpu_rmap =
			alloc_irq_cpu_rmap(efx->n_rx_channels);
		if (!efx->net_dev->rx_cpu_rmap) {
			rc = -ENOMEM;
			goto fail1;
		}
	}
#endif

	/* Hook MSI or MSI-X interrupt */
	n_irqs = 0;
	efx_for_each_channel(channel, efx) {
		rc = request_irq(channel->irq, efx->type->irq_handle_msi,
				 IRQF_PROBE_SHARED, /* Not shared */
				 efx->msi_context[channel->channel].name,
				 &efx->msi_context[channel->channel]);
		if (rc) {
			netif_err(efx, drv, efx->net_dev,
				  "failed to hook IRQ %d\n", channel->irq);
			goto fail2;
		}
		++n_irqs;

#ifdef CONFIG_RFS_ACCEL
		if (efx->interrupt_mode == EFX_INT_MODE_MSIX &&
		    channel->channel < efx->n_rx_channels) {
			rc = irq_cpu_rmap_add(efx->net_dev->rx_cpu_rmap,
					      channel->irq);
			if (rc)
				goto fail2;
		}
#endif
	}

	efx->irqs_hooked = true;
	return 0;

 fail2:
#ifdef CONFIG_RFS_ACCEL
	free_irq_cpu_rmap(efx->net_dev->rx_cpu_rmap);
	efx->net_dev->rx_cpu_rmap = NULL;
#endif
	efx_for_each_channel(channel, efx) {
		if (n_irqs-- == 0)
			break;
		free_irq(channel->irq, &efx->msi_context[channel->channel]);
	}
 fail1:
	return rc;
}

void efx_siena_fini_interrupt(struct efx_nic *efx)
{
	struct efx_channel *channel;

#ifdef CONFIG_RFS_ACCEL
	free_irq_cpu_rmap(efx->net_dev->rx_cpu_rmap);
	efx->net_dev->rx_cpu_rmap = NULL;
#endif

	if (!efx->irqs_hooked)
		return;
	if (EFX_INT_MODE_USE_MSI(efx)) {
		/* Disable MSI/MSI-X interrupts */
		efx_for_each_channel(channel, efx)
			free_irq(channel->irq,
				 &efx->msi_context[channel->channel]);
	} else {
		/* Disable legacy interrupt */
		free_irq(efx->legacy_irq, efx);
	}
	efx->irqs_hooked = false;
}

/* Register dump */

#define REGISTER_REVISION_FA	1
#define REGISTER_REVISION_FB	2
#define REGISTER_REVISION_FC	3
#define REGISTER_REVISION_FZ	3	/* last Falcon arch revision */
#define REGISTER_REVISION_ED	4
#define REGISTER_REVISION_EZ	4	/* latest EF10 revision */

struct efx_nic_reg {
	u32 offset:24;
	u32 min_revision:3, max_revision:3;
};

#define REGISTER(name, arch, min_rev, max_rev) {			\
	arch ## R_ ## min_rev ## max_rev ## _ ## name,			\
	REGISTER_REVISION_ ## arch ## min_rev,				\
	REGISTER_REVISION_ ## arch ## max_rev				\
}
#define REGISTER_AA(name) REGISTER(name, F, A, A)
#define REGISTER_AB(name) REGISTER(name, F, A, B)
#define REGISTER_AZ(name) REGISTER(name, F, A, Z)
#define REGISTER_BB(name) REGISTER(name, F, B, B)
#define REGISTER_BZ(name) REGISTER(name, F, B, Z)
#define REGISTER_CZ(name) REGISTER(name, F, C, Z)

static const struct efx_nic_reg efx_nic_regs[] = {
	REGISTER_AZ(ADR_REGION),
	REGISTER_AZ(INT_EN_KER),
	REGISTER_BZ(INT_EN_CHAR),
	REGISTER_AZ(INT_ADR_KER),
	REGISTER_BZ(INT_ADR_CHAR),
	/* INT_ACK_KER is WO */
	/* INT_ISR0 is RC */
	REGISTER_AZ(HW_INIT),
	REGISTER_CZ(USR_EV_CFG),
	REGISTER_AB(EE_SPI_HCMD),
	REGISTER_AB(EE_SPI_HADR),
	REGISTER_AB(EE_SPI_HDATA),
	REGISTER_AB(EE_BASE_PAGE),
	REGISTER_AB(EE_VPD_CFG0),
	/* EE_VPD_SW_CNTL and EE_VPD_SW_DATA are not used */
	/* PMBX_DBG_IADDR and PBMX_DBG_IDATA are indirect */
	/* PCIE_CORE_INDIRECT is indirect */
	REGISTER_AB(NIC_STAT),
	REGISTER_AB(GPIO_CTL),
	REGISTER_AB(GLB_CTL),
	/* FATAL_INTR_KER and FATAL_INTR_CHAR are partly RC */
	REGISTER_BZ(DP_CTRL),
	REGISTER_AZ(MEM_STAT),
	REGISTER_AZ(CS_DEBUG),
	REGISTER_AZ(ALTERA_BUILD),
	REGISTER_AZ(CSR_SPARE),
	REGISTER_AB(PCIE_SD_CTL0123),
	REGISTER_AB(PCIE_SD_CTL45),
	REGISTER_AB(PCIE_PCS_CTL_STAT),
	/* DEBUG_DATA_OUT is not used */
	/* DRV_EV is WO */
	REGISTER_AZ(EVQ_CTL),
	REGISTER_AZ(EVQ_CNT1),
	REGISTER_AZ(EVQ_CNT2),
	REGISTER_AZ(BUF_TBL_CFG),
	REGISTER_AZ(SRM_RX_DC_CFG),
	REGISTER_AZ(SRM_TX_DC_CFG),
	REGISTER_AZ(SRM_CFG),
	/* BUF_TBL_UPD is WO */
	REGISTER_AZ(SRM_UPD_EVQ),
	REGISTER_AZ(SRAM_PARITY),
	REGISTER_AZ(RX_CFG),
	REGISTER_BZ(RX_FILTER_CTL),
	/* RX_FLUSH_DESCQ is WO */
	REGISTER_AZ(RX_DC_CFG),
	REGISTER_AZ(RX_DC_PF_WM),
	REGISTER_BZ(RX_RSS_TKEY),
	/* RX_NODESC_DROP is RC */
	REGISTER_AA(RX_SELF_RST),
	/* RX_DEBUG, RX_PUSH_DROP are not used */
	REGISTER_CZ(RX_RSS_IPV6_REG1),
	REGISTER_CZ(RX_RSS_IPV6_REG2),
	REGISTER_CZ(RX_RSS_IPV6_REG3),
	/* TX_FLUSH_DESCQ is WO */
	REGISTER_AZ(TX_DC_CFG),
	REGISTER_AA(TX_CHKSM_CFG),
	REGISTER_AZ(TX_CFG),
	/* TX_PUSH_DROP is not used */
	REGISTER_AZ(TX_RESERVED),
	REGISTER_BZ(TX_PACE),
	/* TX_PACE_DROP_QID is RC */
	REGISTER_BB(TX_VLAN),
	REGISTER_BZ(TX_IPFIL_PORTEN),
	REGISTER_AB(MD_TXD),
	REGISTER_AB(MD_RXD),
	REGISTER_AB(MD_CS),
	REGISTER_AB(MD_PHY_ADR),
	REGISTER_AB(MD_ID),
	/* MD_STAT is RC */
	REGISTER_AB(MAC_STAT_DMA),
	REGISTER_AB(MAC_CTRL),
	REGISTER_BB(GEN_MODE),
	REGISTER_AB(MAC_MC_HASH_REG0),
	REGISTER_AB(MAC_MC_HASH_REG1),
	REGISTER_AB(GM_CFG1),
	REGISTER_AB(GM_CFG2),
	/* GM_IPG and GM_HD are not used */
	REGISTER_AB(GM_MAX_FLEN),
	/* GM_TEST is not used */
	REGISTER_AB(GM_ADR1),
	REGISTER_AB(GM_ADR2),
	REGISTER_AB(GMF_CFG0),
	REGISTER_AB(GMF_CFG1),
	REGISTER_AB(GMF_CFG2),
	REGISTER_AB(GMF_CFG3),
	REGISTER_AB(GMF_CFG4),
	REGISTER_AB(GMF_CFG5),
	REGISTER_BB(TX_SRC_MAC_CTL),
	REGISTER_AB(XM_ADR_LO),
	REGISTER_AB(XM_ADR_HI),
	REGISTER_AB(XM_GLB_CFG),
	REGISTER_AB(XM_TX_CFG),
	REGISTER_AB(XM_RX_CFG),
	REGISTER_AB(XM_MGT_INT_MASK),
	REGISTER_AB(XM_FC),
	REGISTER_AB(XM_PAUSE_TIME),
	REGISTER_AB(XM_TX_PARAM),
	REGISTER_AB(XM_RX_PARAM),
	/* XM_MGT_INT_MSK (note no 'A') is RC */
	REGISTER_AB(XX_PWR_RST),
	REGISTER_AB(XX_SD_CTL),
	REGISTER_AB(XX_TXDRV_CTL),
	/* XX_PRBS_CTL, XX_PRBS_CHK and XX_PRBS_ERR are not used */
	/* XX_CORE_STAT is partly RC */
};

struct efx_nic_reg_table {
	u32 offset:24;
	u32 min_revision:3, max_revision:3;
	u32 step:6, rows:21;
};

#define REGISTER_TABLE_DIMENSIONS(_, offset, arch, min_rev, max_rev, step, rows) { \
	offset,								\
	REGISTER_REVISION_ ## arch ## min_rev,				\
	REGISTER_REVISION_ ## arch ## max_rev,				\
	step, rows							\
}
#define REGISTER_TABLE(name, arch, min_rev, max_rev)			\
	REGISTER_TABLE_DIMENSIONS(					\
		name, arch ## R_ ## min_rev ## max_rev ## _ ## name,	\
		arch, min_rev, max_rev,					\
		arch ## R_ ## min_rev ## max_rev ## _ ## name ## _STEP,	\
		arch ## R_ ## min_rev ## max_rev ## _ ## name ## _ROWS)
#define REGISTER_TABLE_AA(name) REGISTER_TABLE(name, F, A, A)
#define REGISTER_TABLE_AZ(name) REGISTER_TABLE(name, F, A, Z)
#define REGISTER_TABLE_BB(name) REGISTER_TABLE(name, F, B, B)
#define REGISTER_TABLE_BZ(name) REGISTER_TABLE(name, F, B, Z)
#define REGISTER_TABLE_BB_CZ(name)					\
	REGISTER_TABLE_DIMENSIONS(name, FR_BZ_ ## name, F, B, B,	\
				  FR_BZ_ ## name ## _STEP,		\
				  FR_BB_ ## name ## _ROWS),		\
	REGISTER_TABLE_DIMENSIONS(name, FR_BZ_ ## name, F, C, Z,	\
				  FR_BZ_ ## name ## _STEP,		\
				  FR_CZ_ ## name ## _ROWS)
#define REGISTER_TABLE_CZ(name) REGISTER_TABLE(name, F, C, Z)

static const struct efx_nic_reg_table efx_nic_reg_tables[] = {
	/* DRIVER is not used */
	/* EVQ_RPTR, TIMER_COMMAND, USR_EV and {RX,TX}_DESC_UPD are WO */
	REGISTER_TABLE_BB(TX_IPFIL_TBL),
	REGISTER_TABLE_BB(TX_SRC_MAC_TBL),
	REGISTER_TABLE_AA(RX_DESC_PTR_TBL_KER),
	REGISTER_TABLE_BB_CZ(RX_DESC_PTR_TBL),
	REGISTER_TABLE_AA(TX_DESC_PTR_TBL_KER),
	REGISTER_TABLE_BB_CZ(TX_DESC_PTR_TBL),
	REGISTER_TABLE_AA(EVQ_PTR_TBL_KER),
	REGISTER_TABLE_BB_CZ(EVQ_PTR_TBL),
	/* We can't reasonably read all of the buffer table (up to 8MB!).
	 * However this driver will only use a few entries.  Reading
	 * 1K entries allows for some expansion of queue count and
	 * size before we need to change the version. */
	REGISTER_TABLE_DIMENSIONS(BUF_FULL_TBL_KER, FR_AA_BUF_FULL_TBL_KER,
				  F, A, A, 8, 1024),
	REGISTER_TABLE_DIMENSIONS(BUF_FULL_TBL, FR_BZ_BUF_FULL_TBL,
				  F, B, Z, 8, 1024),
	REGISTER_TABLE_CZ(RX_MAC_FILTER_TBL0),
	REGISTER_TABLE_BB_CZ(TIMER_TBL),
	REGISTER_TABLE_BB_CZ(TX_PACE_TBL),
	REGISTER_TABLE_BZ(RX_INDIRECTION_TBL),
	/* TX_FILTER_TBL0 is huge and not used by this driver */
	REGISTER_TABLE_CZ(TX_MAC_FILTER_TBL0),
	REGISTER_TABLE_CZ(MC_TREG_SMEM),
	/* MSIX_PBA_TABLE is not mapped */
	/* SRM_DBG is not mapped (and is redundant with BUF_FLL_TBL) */
	REGISTER_TABLE_BZ(RX_FILTER_TBL0),
};

size_t efx_siena_get_regs_len(struct efx_nic *efx)
{
	const struct efx_nic_reg *reg;
	const struct efx_nic_reg_table *table;
	size_t len = 0;

	for (reg = efx_nic_regs;
	     reg < efx_nic_regs + ARRAY_SIZE(efx_nic_regs);
	     reg++)
		if (efx->type->revision >= reg->min_revision &&
		    efx->type->revision <= reg->max_revision)
			len += sizeof(efx_oword_t);

	for (table = efx_nic_reg_tables;
	     table < efx_nic_reg_tables + ARRAY_SIZE(efx_nic_reg_tables);
	     table++)
		if (efx->type->revision >= table->min_revision &&
		    efx->type->revision <= table->max_revision)
			len += table->rows * min_t(size_t, table->step, 16);

	return len;
}

void efx_siena_get_regs(struct efx_nic *efx, void *buf)
{
	const struct efx_nic_reg *reg;
	const struct efx_nic_reg_table *table;

	for (reg = efx_nic_regs;
	     reg < efx_nic_regs + ARRAY_SIZE(efx_nic_regs);
	     reg++) {
		if (efx->type->revision >= reg->min_revision &&
		    efx->type->revision <= reg->max_revision) {
			efx_reado(efx, (efx_oword_t *)buf, reg->offset);
			buf += sizeof(efx_oword_t);
		}
	}

	for (table = efx_nic_reg_tables;
	     table < efx_nic_reg_tables + ARRAY_SIZE(efx_nic_reg_tables);
	     table++) {
		size_t size, i;

		if (!(efx->type->revision >= table->min_revision &&
		      efx->type->revision <= table->max_revision))
			continue;

		size = min_t(size_t, table->step, 16);

		for (i = 0; i < table->rows; i++) {
			switch (table->step) {
			case 4: /* 32-bit SRAM */
				efx_readd(efx, buf, table->offset + 4 * i);
				break;
			case 8: /* 64-bit SRAM */
				efx_sram_readq(efx,
					       efx->membase + table->offset,
					       buf, i);
				break;
			case 16: /* 128-bit-readable register */
				efx_reado_table(efx, buf, table->offset, i);
				break;
			case 32: /* 128-bit register, interleaved */
				efx_reado_table(efx, buf, table->offset, 2 * i);
				break;
			default:
				WARN_ON(1);
				return;
			}
			buf += size;
		}
	}
}

/**
 * efx_siena_describe_stats - Describe supported statistics for ethtool
 * @desc: Array of &struct efx_hw_stat_desc describing the statistics
 * @count: Length of the @desc array
 * @mask: Bitmask of which elements of @desc are enabled
 * @names: Buffer to copy names to, or %NULL.  The names are copied
 *	starting at intervals of %ETH_GSTRING_LEN bytes.
 *
 * Returns the number of visible statistics, i.e. the number of set
 * bits in the first @count bits of @mask for which a name is defined.
 */
size_t efx_siena_describe_stats(const struct efx_hw_stat_desc *desc, size_t count,
				const unsigned long *mask, u8 *names)
{
	size_t visible = 0;
	size_t index;

	for_each_set_bit(index, mask, count) {
		if (desc[index].name) {
			if (names) {
				strscpy(names, desc[index].name,
					ETH_GSTRING_LEN);
				names += ETH_GSTRING_LEN;
			}
			++visible;
		}
	}

	return visible;
}

/**
 * efx_siena_update_stats - Convert statistics DMA buffer to array of u64
 * @desc: Array of &struct efx_hw_stat_desc describing the DMA buffer
 *	layout.  DMA widths of 0, 16, 32 and 64 are supported; where
 *	the width is specified as 0 the corresponding element of
 *	@stats is not updated.
 * @count: Length of the @desc array
 * @mask: Bitmask of which elements of @desc are enabled
 * @stats: Buffer to update with the converted statistics.  The length
 *	of this array must be at least @count.
 * @dma_buf: DMA buffer containing hardware statistics
 * @accumulate: If set, the converted values will be added rather than
 *	directly stored to the corresponding elements of @stats
 */
void efx_siena_update_stats(const struct efx_hw_stat_desc *desc, size_t count,
			    const unsigned long *mask,
			    u64 *stats, const void *dma_buf, bool accumulate)
{
	size_t index;

	for_each_set_bit(index, mask, count) {
		if (desc[index].dma_width) {
			const void *addr = dma_buf + desc[index].offset;
			u64 val;

			switch (desc[index].dma_width) {
			case 16:
				val = le16_to_cpup((__le16 *)addr);
				break;
			case 32:
				val = le32_to_cpup((__le32 *)addr);
				break;
			case 64:
				val = le64_to_cpup((__le64 *)addr);
				break;
			default:
				WARN_ON(1);
				val = 0;
				break;
			}

			if (accumulate)
				stats[index] += val;
			else
				stats[index] = val;
		}
	}
}

void efx_siena_fix_nodesc_drop_stat(struct efx_nic *efx, u64 *rx_nodesc_drops)
{
	/* if down, or this is the first update after coming up */
	if (!(efx->net_dev->flags & IFF_UP) || !efx->rx_nodesc_drops_prev_state)
		efx->rx_nodesc_drops_while_down +=
			*rx_nodesc_drops - efx->rx_nodesc_drops_total;
	efx->rx_nodesc_drops_total = *rx_nodesc_drops;
	efx->rx_nodesc_drops_prev_state = !!(efx->net_dev->flags & IFF_UP);
	*rx_nodesc_drops -= efx->rx_nodesc_drops_while_down;
}
