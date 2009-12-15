/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_IO_H
#define EFX_IO_H

#include <linux/io.h>
#include <linux/spinlock.h>

/**************************************************************************
 *
 * NIC register I/O
 *
 **************************************************************************
 *
 * Notes on locking strategy:
 *
 * Most NIC registers require 16-byte (or 8-byte, for SRAM) atomic writes
 * which necessitates locking.
 * Under normal operation few writes to NIC registers are made and these
 * registers (EVQ_RPTR_REG, RX_DESC_UPD_REG and TX_DESC_UPD_REG) are special
 * cased to allow 4-byte (hence lockless) accesses.
 *
 * It *is* safe to write to these 4-byte registers in the middle of an
 * access to an 8-byte or 16-byte register.  We therefore use a
 * spinlock to protect accesses to the larger registers, but no locks
 * for the 4-byte registers.
 *
 * A write barrier is needed to ensure that DW3 is written after DW0/1/2
 * due to the way the 16byte registers are "collected" in the BIU.
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

#if BITS_PER_LONG == 64
#define EFX_USE_QWORD_IO 1
#endif

#ifdef EFX_USE_QWORD_IO
static inline void _efx_writeq(struct efx_nic *efx, __le64 value,
				  unsigned int reg)
{
	__raw_writeq((__force u64)value, efx->membase + reg);
}
static inline __le64 _efx_readq(struct efx_nic *efx, unsigned int reg)
{
	return (__force __le64)__raw_readq(efx->membase + reg);
}
#endif

static inline void _efx_writed(struct efx_nic *efx, __le32 value,
				  unsigned int reg)
{
	__raw_writel((__force u32)value, efx->membase + reg);
}
static inline __le32 _efx_readd(struct efx_nic *efx, unsigned int reg)
{
	return (__force __le32)__raw_readl(efx->membase + reg);
}

/* Writes to a normal 16-byte Efx register, locking as appropriate. */
static inline void efx_writeo(struct efx_nic *efx, efx_oword_t *value,
			      unsigned int reg)
{
	unsigned long flags __attribute__ ((unused));

	EFX_REGDUMP(efx, "writing register %x with " EFX_OWORD_FMT "\n", reg,
		    EFX_OWORD_VAL(*value));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef EFX_USE_QWORD_IO
	_efx_writeq(efx, value->u64[0], reg + 0);
	wmb();
	_efx_writeq(efx, value->u64[1], reg + 8);
#else
	_efx_writed(efx, value->u32[0], reg + 0);
	_efx_writed(efx, value->u32[1], reg + 4);
	_efx_writed(efx, value->u32[2], reg + 8);
	wmb();
	_efx_writed(efx, value->u32[3], reg + 12);
#endif
	mmiowb();
	spin_unlock_irqrestore(&efx->biu_lock, flags);
}

/* Write an 8-byte NIC SRAM entry through the supplied mapping,
 * locking as appropriate. */
static inline void efx_sram_writeq(struct efx_nic *efx, void __iomem *membase,
				   efx_qword_t *value, unsigned int index)
{
	unsigned int addr = index * sizeof(*value);
	unsigned long flags __attribute__ ((unused));

	EFX_REGDUMP(efx, "writing SRAM address %x with " EFX_QWORD_FMT "\n",
		    addr, EFX_QWORD_VAL(*value));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef EFX_USE_QWORD_IO
	__raw_writeq((__force u64)value->u64[0], membase + addr);
#else
	__raw_writel((__force u32)value->u32[0], membase + addr);
	wmb();
	__raw_writel((__force u32)value->u32[1], membase + addr + 4);
#endif
	mmiowb();
	spin_unlock_irqrestore(&efx->biu_lock, flags);
}

/* Write dword to NIC register that allows partial writes
 *
 * Some registers (EVQ_RPTR_REG, RX_DESC_UPD_REG and
 * TX_DESC_UPD_REG) can be written to as a single dword.  This allows
 * for lockless writes.
 */
static inline void efx_writed(struct efx_nic *efx, efx_dword_t *value,
			      unsigned int reg)
{
	EFX_REGDUMP(efx, "writing partial register %x with "EFX_DWORD_FMT"\n",
		    reg, EFX_DWORD_VAL(*value));

	/* No lock required */
	_efx_writed(efx, value->u32[0], reg);
}

/* Read from a NIC register
 *
 * This reads an entire 16-byte register in one go, locking as
 * appropriate.  It is essential to read the first dword first, as this
 * prompts the NIC to load the current value into the shadow register.
 */
static inline void efx_reado(struct efx_nic *efx, efx_oword_t *value,
			     unsigned int reg)
{
	unsigned long flags __attribute__ ((unused));

	spin_lock_irqsave(&efx->biu_lock, flags);
	value->u32[0] = _efx_readd(efx, reg + 0);
	rmb();
	value->u32[1] = _efx_readd(efx, reg + 4);
	value->u32[2] = _efx_readd(efx, reg + 8);
	value->u32[3] = _efx_readd(efx, reg + 12);
	spin_unlock_irqrestore(&efx->biu_lock, flags);

	EFX_REGDUMP(efx, "read from register %x, got " EFX_OWORD_FMT "\n", reg,
		    EFX_OWORD_VAL(*value));
}

/* Read an 8-byte SRAM entry through supplied mapping,
 * locking as appropriate. */
static inline void efx_sram_readq(struct efx_nic *efx, void __iomem *membase,
				  efx_qword_t *value, unsigned int index)
{
	unsigned int addr = index * sizeof(*value);
	unsigned long flags __attribute__ ((unused));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef EFX_USE_QWORD_IO
	value->u64[0] = (__force __le64)__raw_readq(membase + addr);
#else
	value->u32[0] = (__force __le32)__raw_readl(membase + addr);
	rmb();
	value->u32[1] = (__force __le32)__raw_readl(membase + addr + 4);
#endif
	spin_unlock_irqrestore(&efx->biu_lock, flags);

	EFX_REGDUMP(efx, "read from SRAM address %x, got "EFX_QWORD_FMT"\n",
		    addr, EFX_QWORD_VAL(*value));
}

/* Read dword from register that allows partial writes (sic) */
static inline void efx_readd(struct efx_nic *efx, efx_dword_t *value,
				unsigned int reg)
{
	value->u32[0] = _efx_readd(efx, reg);
	EFX_REGDUMP(efx, "read from register %x, got "EFX_DWORD_FMT"\n",
		    reg, EFX_DWORD_VAL(*value));
}

/* Write to a register forming part of a table */
static inline void efx_writeo_table(struct efx_nic *efx, efx_oword_t *value,
				      unsigned int reg, unsigned int index)
{
	efx_writeo(efx, value, reg + index * sizeof(efx_oword_t));
}

/* Read to a register forming part of a table */
static inline void efx_reado_table(struct efx_nic *efx, efx_oword_t *value,
				     unsigned int reg, unsigned int index)
{
	efx_reado(efx, value, reg + index * sizeof(efx_oword_t));
}

/* Write to a dword register forming part of a table */
static inline void efx_writed_table(struct efx_nic *efx, efx_dword_t *value,
				       unsigned int reg, unsigned int index)
{
	efx_writed(efx, value, reg + index * sizeof(efx_oword_t));
}

/* Page-mapped register block size */
#define EFX_PAGE_BLOCK_SIZE 0x2000

/* Calculate offset to page-mapped register block */
#define EFX_PAGED_REG(page, reg) \
	((page) * EFX_PAGE_BLOCK_SIZE + (reg))

/* As for efx_writeo(), but for a page-mapped register. */
static inline void efx_writeo_page(struct efx_nic *efx, efx_oword_t *value,
				   unsigned int reg, unsigned int page)
{
	efx_writeo(efx, value, EFX_PAGED_REG(page, reg));
}

/* As for efx_writed(), but for a page-mapped register. */
static inline void efx_writed_page(struct efx_nic *efx, efx_dword_t *value,
				   unsigned int reg, unsigned int page)
{
	efx_writed(efx, value, EFX_PAGED_REG(page, reg));
}

/* Write dword to page-mapped register with an extra lock.
 *
 * As for efx_writed_page(), but for a register that suffers from
 * SFC bug 3181. Take out a lock so the BIU collector cannot be
 * confused. */
static inline void efx_writed_page_locked(struct efx_nic *efx,
					  efx_dword_t *value,
					  unsigned int reg,
					  unsigned int page)
{
	unsigned long flags __attribute__ ((unused));

	if (page == 0) {
		spin_lock_irqsave(&efx->biu_lock, flags);
		efx_writed(efx, value, EFX_PAGED_REG(page, reg));
		spin_unlock_irqrestore(&efx->biu_lock, flags);
	} else {
		efx_writed(efx, value, EFX_PAGED_REG(page, reg));
	}
}

#endif /* EFX_IO_H */
