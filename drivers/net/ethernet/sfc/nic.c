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
#include "ef10_regs.h"
#include "io.h"
#include "workarounds.h"
#include "mcdi_pcol.h"

/**************************************************************************
 *
 * Generic buffer handling
 * These buffers are used for interrupt status, MAC stats, etc.
 *
 **************************************************************************/

int efx_nic_alloc_buffer(struct efx_nic *efx, struct efx_buffer *buffer,
			 unsigned int len, gfp_t gfp_flags)
{
	buffer->addr = dma_alloc_coherent(&efx->pci_dev->dev, len,
					  &buffer->dma_addr, gfp_flags);
	if (!buffer->addr)
		return -ENOMEM;
	buffer->len = len;
	return 0;
}

void efx_nic_free_buffer(struct efx_nic *efx, struct efx_buffer *buffer)
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
bool efx_nic_event_present(struct efx_channel *channel)
{
	return efx_event_present(efx_event(channel, channel->eventq_read_ptr));
}

void efx_nic_event_test_start(struct efx_channel *channel)
{
	channel->event_test_cpu = -1;
	smp_wmb();
	channel->efx->type->ev_test_generate(channel);
}

int efx_nic_irq_test_start(struct efx_nic *efx)
{
	efx->last_irq_cpu = -1;
	smp_wmb();
	return efx->type->irq_test_generate(efx);
}

/* Hook interrupt handler(s)
 * Try MSI and then legacy interrupts.
 */
int efx_nic_init_interrupt(struct efx_nic *efx)
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

void efx_nic_fini_interrupt(struct efx_nic *efx)
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
#define REGISTER_DZ(name) REGISTER(name, E, D, Z)

static const struct efx_nic_reg efx_nic_regs[] = {
	/* XX_PRBS_CTL, XX_PRBS_CHK and XX_PRBS_ERR are not used */
	/* XX_CORE_STAT is partly RC */
	REGISTER_DZ(BIU_HW_REV_ID),
	REGISTER_DZ(MC_DB_LWRD),
	REGISTER_DZ(MC_DB_HWRD),
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
#define REGISTER_TABLE_DZ(name) REGISTER_TABLE(name, E, D, Z)

static const struct efx_nic_reg_table efx_nic_reg_tables[] = {
	REGISTER_TABLE_DZ(BIU_MC_SFT_STATUS),
};

size_t efx_nic_get_regs_len(struct efx_nic *efx)
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

void efx_nic_get_regs(struct efx_nic *efx, void *buf)
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
 * efx_nic_describe_stats - Describe supported statistics for ethtool
 * @desc: Array of &struct efx_hw_stat_desc describing the statistics
 * @count: Length of the @desc array
 * @mask: Bitmask of which elements of @desc are enabled
 * @names: Buffer to copy names to, or %NULL.  The names are copied
 *	starting at intervals of %ETH_GSTRING_LEN bytes.
 *
 * Returns the number of visible statistics, i.e. the number of set
 * bits in the first @count bits of @mask for which a name is defined.
 */
size_t efx_nic_describe_stats(const struct efx_hw_stat_desc *desc, size_t count,
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
 * efx_nic_copy_stats - Copy stats from the DMA buffer in to an
 *	intermediate buffer. This is used to get a consistent
 *	set of stats while the DMA buffer can be written at any time
 *	by the NIC.
 * @efx: The associated NIC.
 * @dest: Destination buffer. Must be the same size as the DMA buffer.
 */
int efx_nic_copy_stats(struct efx_nic *efx, __le64 *dest)
{
	__le64 *dma_stats = efx->stats_buffer.addr;
	__le64 generation_start, generation_end;
	int rc = 0, retry;

	if (!dest)
		return 0;

	if (!dma_stats)
		goto return_zeroes;

	/* If we're unlucky enough to read statistics during the DMA, wait
	 * up to 10ms for it to finish (typically takes <500us)
	 */
	for (retry = 0; retry < 100; ++retry) {
		generation_end = dma_stats[efx->num_mac_stats - 1];
		if (generation_end == EFX_MC_STATS_GENERATION_INVALID)
			goto return_zeroes;
		rmb();
		memcpy(dest, dma_stats, efx->num_mac_stats * sizeof(__le64));
		rmb();
		generation_start = dma_stats[MC_CMD_MAC_GENERATION_START];
		if (generation_end == generation_start)
			return 0; /* return good data */
		udelay(100);
	}

	rc = -EIO;

return_zeroes:
	memset(dest, 0, efx->num_mac_stats * sizeof(u64));
	return rc;
}

/**
 * efx_nic_update_stats - Convert statistics DMA buffer to array of u64
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
void efx_nic_update_stats(const struct efx_hw_stat_desc *desc, size_t count,
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

void efx_nic_fix_nodesc_drop_stat(struct efx_nic *efx, u64 *rx_nodesc_drops)
{
	/* if down, or this is the first update after coming up */
	if (!(efx->net_dev->flags & IFF_UP) || !efx->rx_nodesc_drops_prev_state)
		efx->rx_nodesc_drops_while_down +=
			*rx_nodesc_drops - efx->rx_nodesc_drops_total;
	efx->rx_nodesc_drops_total = *rx_nodesc_drops;
	efx->rx_nodesc_drops_prev_state = !!(efx->net_dev->flags & IFF_UP);
	*rx_nodesc_drops -= efx->rx_nodesc_drops_while_down;
}
