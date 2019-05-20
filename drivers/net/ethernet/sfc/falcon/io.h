/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EF4_IO_H
#define EF4_IO_H

#include <linux/io.h>
#include <linux/spinlock.h>

/**************************************************************************
 *
 * NIC register I/O
 *
 **************************************************************************
 *
 * Notes on locking strategy for the Falcon architecture:
 *
 * Many CSRs are very wide and cannot be read or written atomically.
 * Writes from the host are buffered by the Bus Interface Unit (BIU)
 * up to 128 bits.  Whenever the host writes part of such a register,
 * the BIU collects the written value and does not write to the
 * underlying register until all 4 dwords have been written.  A
 * similar buffering scheme applies to host access to the NIC's 64-bit
 * SRAM.
 *
 * Writes to different CSRs and 64-bit SRAM words must be serialised,
 * since interleaved access can result in lost writes.  We use
 * ef4_nic::biu_lock for this.
 *
 * We also serialise reads from 128-bit CSRs and SRAM with the same
 * spinlock.  This may not be necessary, but it doesn't really matter
 * as there are no such reads on the fast path.
 *
 * The DMA descriptor pointers (RX_DESC_UPD and TX_DESC_UPD) are
 * 128-bit but are special-cased in the BIU to avoid the need for
 * locking in the host:
 *
 * - They are write-only.
 * - The semantics of writing to these registers are such that
 *   replacing the low 96 bits with zero does not affect functionality.
 * - If the host writes to the last dword address of such a register
 *   (i.e. the high 32 bits) the underlying register will always be
 *   written.  If the collector and the current write together do not
 *   provide values for all 128 bits of the register, the low 96 bits
 *   will be written as zero.
 * - If the host writes to the address of any other part of such a
 *   register while the collector already holds values for some other
 *   register, the write is discarded and the collector maintains its
 *   current state.
 *
 * The EF10 architecture exposes very few registers to the host and
 * most of them are only 32 bits wide.  The only exceptions are the MC
 * doorbell register pair, which has its own latching, and
 * TX_DESC_UPD, which works in a similar way to the Falcon
 * architecture.
 */

#if BITS_PER_LONG == 64
#define EF4_USE_QWORD_IO 1
#endif

#ifdef EF4_USE_QWORD_IO
static inline void _ef4_writeq(struct ef4_nic *efx, __le64 value,
				  unsigned int reg)
{
	__raw_writeq((__force u64)value, efx->membase + reg);
}
static inline __le64 _ef4_readq(struct ef4_nic *efx, unsigned int reg)
{
	return (__force __le64)__raw_readq(efx->membase + reg);
}
#endif

static inline void _ef4_writed(struct ef4_nic *efx, __le32 value,
				  unsigned int reg)
{
	__raw_writel((__force u32)value, efx->membase + reg);
}
static inline __le32 _ef4_readd(struct ef4_nic *efx, unsigned int reg)
{
	return (__force __le32)__raw_readl(efx->membase + reg);
}

/* Write a normal 128-bit CSR, locking as appropriate. */
static inline void ef4_writeo(struct ef4_nic *efx, const ef4_oword_t *value,
			      unsigned int reg)
{
	unsigned long flags __attribute__ ((unused));

	netif_vdbg(efx, hw, efx->net_dev,
		   "writing register %x with " EF4_OWORD_FMT "\n", reg,
		   EF4_OWORD_VAL(*value));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef EF4_USE_QWORD_IO
	_ef4_writeq(efx, value->u64[0], reg + 0);
	_ef4_writeq(efx, value->u64[1], reg + 8);
#else
	_ef4_writed(efx, value->u32[0], reg + 0);
	_ef4_writed(efx, value->u32[1], reg + 4);
	_ef4_writed(efx, value->u32[2], reg + 8);
	_ef4_writed(efx, value->u32[3], reg + 12);
#endif
	spin_unlock_irqrestore(&efx->biu_lock, flags);
}

/* Write 64-bit SRAM through the supplied mapping, locking as appropriate. */
static inline void ef4_sram_writeq(struct ef4_nic *efx, void __iomem *membase,
				   const ef4_qword_t *value, unsigned int index)
{
	unsigned int addr = index * sizeof(*value);
	unsigned long flags __attribute__ ((unused));

	netif_vdbg(efx, hw, efx->net_dev,
		   "writing SRAM address %x with " EF4_QWORD_FMT "\n",
		   addr, EF4_QWORD_VAL(*value));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef EF4_USE_QWORD_IO
	__raw_writeq((__force u64)value->u64[0], membase + addr);
#else
	__raw_writel((__force u32)value->u32[0], membase + addr);
	__raw_writel((__force u32)value->u32[1], membase + addr + 4);
#endif
	spin_unlock_irqrestore(&efx->biu_lock, flags);
}

/* Write a 32-bit CSR or the last dword of a special 128-bit CSR */
static inline void ef4_writed(struct ef4_nic *efx, const ef4_dword_t *value,
			      unsigned int reg)
{
	netif_vdbg(efx, hw, efx->net_dev,
		   "writing register %x with "EF4_DWORD_FMT"\n",
		   reg, EF4_DWORD_VAL(*value));

	/* No lock required */
	_ef4_writed(efx, value->u32[0], reg);
}

/* Read a 128-bit CSR, locking as appropriate. */
static inline void ef4_reado(struct ef4_nic *efx, ef4_oword_t *value,
			     unsigned int reg)
{
	unsigned long flags __attribute__ ((unused));

	spin_lock_irqsave(&efx->biu_lock, flags);
	value->u32[0] = _ef4_readd(efx, reg + 0);
	value->u32[1] = _ef4_readd(efx, reg + 4);
	value->u32[2] = _ef4_readd(efx, reg + 8);
	value->u32[3] = _ef4_readd(efx, reg + 12);
	spin_unlock_irqrestore(&efx->biu_lock, flags);

	netif_vdbg(efx, hw, efx->net_dev,
		   "read from register %x, got " EF4_OWORD_FMT "\n", reg,
		   EF4_OWORD_VAL(*value));
}

/* Read 64-bit SRAM through the supplied mapping, locking as appropriate. */
static inline void ef4_sram_readq(struct ef4_nic *efx, void __iomem *membase,
				  ef4_qword_t *value, unsigned int index)
{
	unsigned int addr = index * sizeof(*value);
	unsigned long flags __attribute__ ((unused));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef EF4_USE_QWORD_IO
	value->u64[0] = (__force __le64)__raw_readq(membase + addr);
#else
	value->u32[0] = (__force __le32)__raw_readl(membase + addr);
	value->u32[1] = (__force __le32)__raw_readl(membase + addr + 4);
#endif
	spin_unlock_irqrestore(&efx->biu_lock, flags);

	netif_vdbg(efx, hw, efx->net_dev,
		   "read from SRAM address %x, got "EF4_QWORD_FMT"\n",
		   addr, EF4_QWORD_VAL(*value));
}

/* Read a 32-bit CSR or SRAM */
static inline void ef4_readd(struct ef4_nic *efx, ef4_dword_t *value,
				unsigned int reg)
{
	value->u32[0] = _ef4_readd(efx, reg);
	netif_vdbg(efx, hw, efx->net_dev,
		   "read from register %x, got "EF4_DWORD_FMT"\n",
		   reg, EF4_DWORD_VAL(*value));
}

/* Write a 128-bit CSR forming part of a table */
static inline void
ef4_writeo_table(struct ef4_nic *efx, const ef4_oword_t *value,
		 unsigned int reg, unsigned int index)
{
	ef4_writeo(efx, value, reg + index * sizeof(ef4_oword_t));
}

/* Read a 128-bit CSR forming part of a table */
static inline void ef4_reado_table(struct ef4_nic *efx, ef4_oword_t *value,
				     unsigned int reg, unsigned int index)
{
	ef4_reado(efx, value, reg + index * sizeof(ef4_oword_t));
}

/* Page size used as step between per-VI registers */
#define EF4_VI_PAGE_SIZE 0x2000

/* Calculate offset to page-mapped register */
#define EF4_PAGED_REG(page, reg) \
	((page) * EF4_VI_PAGE_SIZE + (reg))

/* Write the whole of RX_DESC_UPD or TX_DESC_UPD */
static inline void _ef4_writeo_page(struct ef4_nic *efx, ef4_oword_t *value,
				    unsigned int reg, unsigned int page)
{
	reg = EF4_PAGED_REG(page, reg);

	netif_vdbg(efx, hw, efx->net_dev,
		   "writing register %x with " EF4_OWORD_FMT "\n", reg,
		   EF4_OWORD_VAL(*value));

#ifdef EF4_USE_QWORD_IO
	_ef4_writeq(efx, value->u64[0], reg + 0);
	_ef4_writeq(efx, value->u64[1], reg + 8);
#else
	_ef4_writed(efx, value->u32[0], reg + 0);
	_ef4_writed(efx, value->u32[1], reg + 4);
	_ef4_writed(efx, value->u32[2], reg + 8);
	_ef4_writed(efx, value->u32[3], reg + 12);
#endif
}
#define ef4_writeo_page(efx, value, reg, page)				\
	_ef4_writeo_page(efx, value,					\
			 reg +						\
			 BUILD_BUG_ON_ZERO((reg) != 0x830 && (reg) != 0xa10), \
			 page)

/* Write a page-mapped 32-bit CSR (EVQ_RPTR, EVQ_TMR (EF10), or the
 * high bits of RX_DESC_UPD or TX_DESC_UPD)
 */
static inline void
_ef4_writed_page(struct ef4_nic *efx, const ef4_dword_t *value,
		 unsigned int reg, unsigned int page)
{
	ef4_writed(efx, value, EF4_PAGED_REG(page, reg));
}
#define ef4_writed_page(efx, value, reg, page)				\
	_ef4_writed_page(efx, value,					\
			 reg +						\
			 BUILD_BUG_ON_ZERO((reg) != 0x400 &&		\
					   (reg) != 0x420 &&		\
					   (reg) != 0x830 &&		\
					   (reg) != 0x83c &&		\
					   (reg) != 0xa18 &&		\
					   (reg) != 0xa1c),		\
			 page)

/* Write TIMER_COMMAND.  This is a page-mapped 32-bit CSR, but a bug
 * in the BIU means that writes to TIMER_COMMAND[0] invalidate the
 * collector register.
 */
static inline void _ef4_writed_page_locked(struct ef4_nic *efx,
					   const ef4_dword_t *value,
					   unsigned int reg,
					   unsigned int page)
{
	unsigned long flags __attribute__ ((unused));

	if (page == 0) {
		spin_lock_irqsave(&efx->biu_lock, flags);
		ef4_writed(efx, value, EF4_PAGED_REG(page, reg));
		spin_unlock_irqrestore(&efx->biu_lock, flags);
	} else {
		ef4_writed(efx, value, EF4_PAGED_REG(page, reg));
	}
}
#define ef4_writed_page_locked(efx, value, reg, page)			\
	_ef4_writed_page_locked(efx, value,				\
				reg + BUILD_BUG_ON_ZERO((reg) != 0x420), \
				page)

#endif /* EF4_IO_H */
