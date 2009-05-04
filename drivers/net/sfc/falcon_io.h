/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_FALCON_IO_H
#define EFX_FALCON_IO_H

#include <linux/io.h>
#include <linux/spinlock.h>

/**************************************************************************
 *
 * Falcon hardware access
 *
 **************************************************************************
 *
 * Notes on locking strategy:
 *
 * Most Falcon registers require 16-byte (or 8-byte, for SRAM
 * registers) atomic writes which necessitates locking.
 * Under normal operation few writes to the Falcon BAR are made and these
 * registers (EVQ_RPTR_REG, RX_DESC_UPD_REG and TX_DESC_UPD_REG) are special
 * cased to allow 4-byte (hence lockless) accesses.
 *
 * It *is* safe to write to these 4-byte registers in the middle of an
 * access to an 8-byte or 16-byte register.  We therefore use a
 * spinlock to protect accesses to the larger registers, but no locks
 * for the 4-byte registers.
 *
 * A write barrier is needed to ensure that DW3 is written after DW0/1/2
 * due to the way the 16byte registers are "collected" in the Falcon BIU
 *
 * We also lock when carrying out reads, to ensure consistency of the
 * data (made possible since the BIU reads all 128 bits into a cache).
 * Reads are very rare, so this isn't a significant performance
 * impact.  (Most data transferred from NIC to host is DMAed directly
 * into host memory).
 *
 * I/O BAR access uses locks for both reads and writes (but is only provided
 * for testing purposes).
 */

/* Special buffer descriptors (Falcon SRAM) */
#define BUF_TBL_KER_A1 0x18000
#define BUF_TBL_KER_B0 0x800000


#if BITS_PER_LONG == 64
#define FALCON_USE_QWORD_IO 1
#endif

#ifdef FALCON_USE_QWORD_IO
static inline void _falcon_writeq(struct efx_nic *efx, __le64 value,
				  unsigned int reg)
{
	__raw_writeq((__force u64)value, efx->membase + reg);
}
static inline __le64 _falcon_readq(struct efx_nic *efx, unsigned int reg)
{
	return (__force __le64)__raw_readq(efx->membase + reg);
}
#endif

static inline void _falcon_writel(struct efx_nic *efx, __le32 value,
				  unsigned int reg)
{
	__raw_writel((__force u32)value, efx->membase + reg);
}
static inline __le32 _falcon_readl(struct efx_nic *efx, unsigned int reg)
{
	return (__force __le32)__raw_readl(efx->membase + reg);
}

/* Writes to a normal 16-byte Falcon register, locking as appropriate. */
static inline void falcon_write(struct efx_nic *efx, efx_oword_t *value,
				unsigned int reg)
{
	unsigned long flags;

	EFX_REGDUMP(efx, "writing register %x with " EFX_OWORD_FMT "\n", reg,
		    EFX_OWORD_VAL(*value));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef FALCON_USE_QWORD_IO
	_falcon_writeq(efx, value->u64[0], reg + 0);
	wmb();
	_falcon_writeq(efx, value->u64[1], reg + 8);
#else
	_falcon_writel(efx, value->u32[0], reg + 0);
	_falcon_writel(efx, value->u32[1], reg + 4);
	_falcon_writel(efx, value->u32[2], reg + 8);
	wmb();
	_falcon_writel(efx, value->u32[3], reg + 12);
#endif
	mmiowb();
	spin_unlock_irqrestore(&efx->biu_lock, flags);
}

/* Writes to an 8-byte Falcon SRAM register, locking as appropriate. */
static inline void falcon_write_sram(struct efx_nic *efx, efx_qword_t *value,
				     unsigned int index)
{
	unsigned int reg = efx->type->buf_tbl_base + (index * sizeof(*value));
	unsigned long flags;

	EFX_REGDUMP(efx, "writing SRAM register %x with " EFX_QWORD_FMT "\n",
		    reg, EFX_QWORD_VAL(*value));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef FALCON_USE_QWORD_IO
	_falcon_writeq(efx, value->u64[0], reg + 0);
#else
	_falcon_writel(efx, value->u32[0], reg + 0);
	wmb();
	_falcon_writel(efx, value->u32[1], reg + 4);
#endif
	mmiowb();
	spin_unlock_irqrestore(&efx->biu_lock, flags);
}

/* Write dword to Falcon register that allows partial writes
 *
 * Some Falcon registers (EVQ_RPTR_REG, RX_DESC_UPD_REG and
 * TX_DESC_UPD_REG) can be written to as a single dword.  This allows
 * for lockless writes.
 */
static inline void falcon_writel(struct efx_nic *efx, efx_dword_t *value,
				 unsigned int reg)
{
	EFX_REGDUMP(efx, "writing partial register %x with "EFX_DWORD_FMT"\n",
		    reg, EFX_DWORD_VAL(*value));

	/* No lock required */
	_falcon_writel(efx, value->u32[0], reg);
}

/* Read from a Falcon register
 *
 * This reads an entire 16-byte Falcon register in one go, locking as
 * appropriate.  It is essential to read the first dword first, as this
 * prompts Falcon to load the current value into the shadow register.
 */
static inline void falcon_read(struct efx_nic *efx, efx_oword_t *value,
			       unsigned int reg)
{
	unsigned long flags;

	spin_lock_irqsave(&efx->biu_lock, flags);
	value->u32[0] = _falcon_readl(efx, reg + 0);
	rmb();
	value->u32[1] = _falcon_readl(efx, reg + 4);
	value->u32[2] = _falcon_readl(efx, reg + 8);
	value->u32[3] = _falcon_readl(efx, reg + 12);
	spin_unlock_irqrestore(&efx->biu_lock, flags);

	EFX_REGDUMP(efx, "read from register %x, got " EFX_OWORD_FMT "\n", reg,
		    EFX_OWORD_VAL(*value));
}

/* This reads an 8-byte Falcon SRAM entry in one go. */
static inline void falcon_read_sram(struct efx_nic *efx, efx_qword_t *value,
				    unsigned int index)
{
	unsigned int reg = efx->type->buf_tbl_base + (index * sizeof(*value));
	unsigned long flags;

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef FALCON_USE_QWORD_IO
	value->u64[0] = _falcon_readq(efx, reg + 0);
#else
	value->u32[0] = _falcon_readl(efx, reg + 0);
	rmb();
	value->u32[1] = _falcon_readl(efx, reg + 4);
#endif
	spin_unlock_irqrestore(&efx->biu_lock, flags);

	EFX_REGDUMP(efx, "read from SRAM register %x, got "EFX_QWORD_FMT"\n",
		    reg, EFX_QWORD_VAL(*value));
}

/* Read dword from Falcon register that allows partial writes (sic) */
static inline void falcon_readl(struct efx_nic *efx, efx_dword_t *value,
				unsigned int reg)
{
	value->u32[0] = _falcon_readl(efx, reg);
	EFX_REGDUMP(efx, "read from register %x, got "EFX_DWORD_FMT"\n",
		    reg, EFX_DWORD_VAL(*value));
}

/* Write to a register forming part of a table */
static inline void falcon_write_table(struct efx_nic *efx, efx_oword_t *value,
				      unsigned int reg, unsigned int index)
{
	falcon_write(efx, value, reg + index * sizeof(efx_oword_t));
}

/* Read to a register forming part of a table */
static inline void falcon_read_table(struct efx_nic *efx, efx_oword_t *value,
				     unsigned int reg, unsigned int index)
{
	falcon_read(efx, value, reg + index * sizeof(efx_oword_t));
}

/* Write to a dword register forming part of a table */
static inline void falcon_writel_table(struct efx_nic *efx, efx_dword_t *value,
				       unsigned int reg, unsigned int index)
{
	falcon_writel(efx, value, reg + index * sizeof(efx_oword_t));
}

/* Page-mapped register block size */
#define FALCON_PAGE_BLOCK_SIZE 0x2000

/* Calculate offset to page-mapped register block */
#define FALCON_PAGED_REG(page, reg) \
	((page) * FALCON_PAGE_BLOCK_SIZE + (reg))

/* As for falcon_write(), but for a page-mapped register. */
static inline void falcon_write_page(struct efx_nic *efx, efx_oword_t *value,
				     unsigned int reg, unsigned int page)
{
	falcon_write(efx, value, FALCON_PAGED_REG(page, reg));
}

/* As for falcon_writel(), but for a page-mapped register. */
static inline void falcon_writel_page(struct efx_nic *efx, efx_dword_t *value,
				      unsigned int reg, unsigned int page)
{
	falcon_writel(efx, value, FALCON_PAGED_REG(page, reg));
}

/* Write dword to Falcon page-mapped register with an extra lock.
 *
 * As for falcon_writel_page(), but for a register that suffers from
 * SFC bug 3181.  If writing to page 0, take out a lock so the BIU
 * collector cannot be confused.
 */
static inline void falcon_writel_page_locked(struct efx_nic *efx,
					     efx_dword_t *value,
					     unsigned int reg,
					     unsigned int page)
{
	unsigned long flags = 0;

	if (page == 0)
		spin_lock_irqsave(&efx->biu_lock, flags);
	falcon_writel(efx, value, FALCON_PAGED_REG(page, reg));
	if (page == 0)
		spin_unlock_irqrestore(&efx->biu_lock, flags);
}

#endif /* EFX_FALCON_IO_H */
