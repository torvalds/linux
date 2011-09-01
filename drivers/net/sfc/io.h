/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2010 Solarflare Communications Inc.
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
 * Most CSRs are 128-bit (oword) and therefore cannot be read or
 * written atomically.  Access from the host is buffered by the Bus
 * Interface Unit (BIU).  Whenever the host reads from the lowest
 * address of such a register, or from the address of a different such
 * register, the BIU latches the register's value.  Subsequent reads
 * from higher addresses of the same register will read the latched
 * value.  Whenever the host writes part of such a register, the BIU
 * collects the written value and does not write to the underlying
 * register until all 4 dwords have been written.  A similar buffering
 * scheme applies to host access to the NIC's 64-bit SRAM.
 *
 * Access to different CSRs and 64-bit SRAM words must be serialised,
 * since interleaved access can result in lost writes or lost
 * information from read-to-clear fields.  We use efx_nic::biu_lock
 * for this.  (We could use separate locks for read and write, but
 * this is not normally a performance bottleneck.)
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
 *   written.  If the collector does not hold values for the low 96
 *   bits of the register, they will be written as zero.  Writing to
 *   the last qword does not have this effect and must not be done.
 * - If the host writes to the address of any other part of such a
 *   register while the collector already holds values for some other
 *   register, the write is discarded and the collector maintains its
 *   current state.
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

/* Write a normal 128-bit CSR, locking as appropriate. */
static inline void efx_writeo(struct efx_nic *efx, efx_oword_t *value,
			      unsigned int reg)
{
	unsigned long flags __attribute__ ((unused));

	netif_vdbg(efx, hw, efx->net_dev,
		   "writing register %x with " EFX_OWORD_FMT "\n", reg,
		   EFX_OWORD_VAL(*value));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef EFX_USE_QWORD_IO
	_efx_writeq(efx, value->u64[0], reg + 0);
	_efx_writeq(efx, value->u64[1], reg + 8);
#else
	_efx_writed(efx, value->u32[0], reg + 0);
	_efx_writed(efx, value->u32[1], reg + 4);
	_efx_writed(efx, value->u32[2], reg + 8);
	_efx_writed(efx, value->u32[3], reg + 12);
#endif
	mmiowb();
	spin_unlock_irqrestore(&efx->biu_lock, flags);
}

/* Write 64-bit SRAM through the supplied mapping, locking as appropriate. */
static inline void efx_sram_writeq(struct efx_nic *efx, void __iomem *membase,
				   efx_qword_t *value, unsigned int index)
{
	unsigned int addr = index * sizeof(*value);
	unsigned long flags __attribute__ ((unused));

	netif_vdbg(efx, hw, efx->net_dev,
		   "writing SRAM address %x with " EFX_QWORD_FMT "\n",
		   addr, EFX_QWORD_VAL(*value));

	spin_lock_irqsave(&efx->biu_lock, flags);
#ifdef EFX_USE_QWORD_IO
	__raw_writeq((__force u64)value->u64[0], membase + addr);
#else
	__raw_writel((__force u32)value->u32[0], membase + addr);
	__raw_writel((__force u32)value->u32[1], membase + addr + 4);
#endif
	mmiowb();
	spin_unlock_irqrestore(&efx->biu_lock, flags);
}

/* Write a 32-bit CSR or the last dword of a special 128-bit CSR */
static inline void efx_writed(struct efx_nic *efx, efx_dword_t *value,
			      unsigned int reg)
{
	netif_vdbg(efx, hw, efx->net_dev,
		   "writing register %x with "EFX_DWORD_FMT"\n",
		   reg, EFX_DWORD_VAL(*value));

	/* No lock required */
	_efx_writed(efx, value->u32[0], reg);
}

/* Read a 128-bit CSR, locking as appropriate. */
static inline void efx_reado(struct efx_nic *efx, efx_oword_t *value,
			     unsigned int reg)
{
	unsigned long flags __attribute__ ((unused));

	spin_lock_irqsave(&efx->biu_lock, flags);
	value->u32[0] = _efx_readd(efx, reg + 0);
	value->u32[1] = _efx_readd(efx, reg + 4);
	value->u32[2] = _efx_readd(efx, reg + 8);
	value->u32[3] = _efx_readd(efx, reg + 12);
	spin_unlock_irqrestore(&efx->biu_lock, flags);

	netif_vdbg(efx, hw, efx->net_dev,
		   "read from register %x, got " EFX_OWORD_FMT "\n", reg,
		   EFX_OWORD_VAL(*value));
}

/* Read 64-bit SRAM through the supplied mapping, locking as appropriate. */
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
	value->u32[1] = (__force __le32)__raw_readl(membase + addr + 4);
#endif
	spin_unlock_irqrestore(&efx->biu_lock, flags);

	netif_vdbg(efx, hw, efx->net_dev,
		   "read from SRAM address %x, got "EFX_QWORD_FMT"\n",
		   addr, EFX_QWORD_VAL(*value));
}

/* Read a 32-bit CSR or SRAM */
static inline void efx_readd(struct efx_nic *efx, efx_dword_t *value,
				unsigned int reg)
{
	value->u32[0] = _efx_readd(efx, reg);
	netif_vdbg(efx, hw, efx->net_dev,
		   "read from register %x, got "EFX_DWORD_FMT"\n",
		   reg, EFX_DWORD_VAL(*value));
}

/* Write a 128-bit CSR forming part of a table */
static inline void efx_writeo_table(struct efx_nic *efx, efx_oword_t *value,
				      unsigned int reg, unsigned int index)
{
	efx_writeo(efx, value, reg + index * sizeof(efx_oword_t));
}

/* Read a 128-bit CSR forming part of a table */
static inline void efx_reado_table(struct efx_nic *efx, efx_oword_t *value,
				     unsigned int reg, unsigned int index)
{
	efx_reado(efx, value, reg + index * sizeof(efx_oword_t));
}

/* Write a 32-bit CSR forming part of a table, or 32-bit SRAM */
static inline void efx_writed_table(struct efx_nic *efx, efx_dword_t *value,
				       unsigned int reg, unsigned int index)
{
	efx_writed(efx, value, reg + index * sizeof(efx_oword_t));
}

/* Read a 32-bit CSR forming part of a table, or 32-bit SRAM */
static inline void efx_readd_table(struct efx_nic *efx, efx_dword_t *value,
				   unsigned int reg, unsigned int index)
{
	efx_readd(efx, value, reg + index * sizeof(efx_dword_t));
}

/* Page-mapped register block size */
#define EFX_PAGE_BLOCK_SIZE 0x2000

/* Calculate offset to page-mapped register block */
#define EFX_PAGED_REG(page, reg) \
	((page) * EFX_PAGE_BLOCK_SIZE + (reg))

/* Write the whole of RX_DESC_UPD or TX_DESC_UPD */
static inline void _efx_writeo_page(struct efx_nic *efx, efx_oword_t *value,
				    unsigned int reg, unsigned int page)
{
	reg = EFX_PAGED_REG(page, reg);

	netif_vdbg(efx, hw, efx->net_dev,
		   "writing register %x with " EFX_OWORD_FMT "\n", reg,
		   EFX_OWORD_VAL(*value));

#ifdef EFX_USE_QWORD_IO
	_efx_writeq(efx, value->u64[0], reg + 0);
#else
	_efx_writed(efx, value->u32[0], reg + 0);
	_efx_writed(efx, value->u32[1], reg + 4);
#endif
	_efx_writed(efx, value->u32[2], reg + 8);
	_efx_writed(efx, value->u32[3], reg + 12);
}
#define efx_writeo_page(efx, value, reg, page)				\
	_efx_writeo_page(efx, value,					\
			 reg +						\
			 BUILD_BUG_ON_ZERO((reg) != 0x830 && (reg) != 0xa10), \
			 page)

/* Write a page-mapped 32-bit CSR (EVQ_RPTR or the high bits of
 * RX_DESC_UPD or TX_DESC_UPD)
 */
static inline void _efx_writed_page(struct efx_nic *efx, efx_dword_t *value,
				    unsigned int reg, unsigned int page)
{
	efx_writed(efx, value, EFX_PAGED_REG(page, reg));
}
#define efx_writed_page(efx, value, reg, page)				\
	_efx_writed_page(efx, value,					\
			 reg +						\
			 BUILD_BUG_ON_ZERO((reg) != 0x400 && (reg) != 0x83c \
					   && (reg) != 0xa1c),		\
			 page)

/* Write TIMER_COMMAND.  This is a page-mapped 32-bit CSR, but a bug
 * in the BIU means that writes to TIMER_COMMAND[0] invalidate the
 * collector register.
 */
static inline void _efx_writed_page_locked(struct efx_nic *efx,
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
#define efx_writed_page_locked(efx, value, reg, page)			\
	_efx_writed_page_locked(efx, value,				\
				reg + BUILD_BUG_ON_ZERO((reg) != 0x420), \
				page)

#endif /* EFX_IO_H */
