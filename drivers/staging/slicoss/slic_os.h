/**************************************************************************
 *
 * Copyright (c)2000-2002 Alacritech, Inc.  All rights reserved.
 *
 * $Id: slic_os.h,v 1.2 2006/03/27 15:10:15 mook Exp $
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: slic_os.h
 *
 * These are the Linux-specific definitions required for the SLICOSS
 * driver, which should allow for greater portability to other OSes.
 */
#ifndef _SLIC_OS_SPECIFIC_H_
#define _SLIC_OS_SPECIFIC_H_

typedef unsigned char       uchar;
typedef u64               ulong64;
typedef char              *pchar;
typedef unsigned char     *puchar;
typedef u16             *pushort;
typedef u32               ulong32;
typedef u32             *pulong32;
typedef int               *plong32;
typedef unsigned int      *puint;
typedef void                *pvoid;
typedef unsigned long      *pulong;
typedef unsigned int        boolean;
typedef unsigned int        wchar;
typedef unsigned int       *pwchar;
typedef unsigned char       UCHAR;
typedef u32               ULONG;
typedef s32               LONG;
#define FALSE               (0)
#define TRUE                (1)

#define SLIC_INIT_SPINLOCK(x)                                                 \
      {                                                                       \
	spin_lock_init(&((x).lock));                                         \
      }
#define SLIC_ACQUIRE_SPINLOCK(x)                                              \
      {                                                                       \
	spin_lock(&((x).lock));                                              \
      }

#define SLIC_RELEASE_SPINLOCK(x)                                              \
      {                                                                       \
	spin_unlock(&((x).lock));                                            \
      }

#define SLIC_ACQUIRE_IRQ_SPINLOCK(x)                                          \
      {                                                                       \
	spin_lock_irqsave(&((x).lock), (x).flags);                           \
      }

#define SLIC_RELEASE_IRQ_SPINLOCK(x)                                          \
      {                                                                       \
	spin_unlock_irqrestore(&((x).lock), (x).flags);                      \
      }

#define ATK_DEBUG  1

#if ATK_DEBUG
#define SLIC_TIMESTAMP(value) {                                             \
	struct timeval  timev;                                              \
	do_gettimeofday(&timev);                                            \
	value = timev.tv_sec*1000000 + timev.tv_usec;                       \
}
#else
#define SLIC_TIMESTAMP(value)
#endif

#define SLIC_ALLOCATE_MEM(len, flag)         kmalloc(len, flag)
#define SLIC_DEALLOCATE_MEM(mem)             kfree(mem)
#define SLIC_DEALLOCATE_IRQ_MEM(mem)         free(mem)
#define SLIC_ALLOCATE_PAGE(x)                (pulong32)get_free_page(GFP_KERNEL)
#define SLIC_DEALLOCATE_PAGE(addr)           free_page((ulong32)addr)
#define SLIC_ALLOCATE_PCIMEM(a, sz, physp)    \
		pci_alloc_consistent((a)->pcidev, (sz), &(physp))
#define SLIC_DEALLOCATE_PCIMEM(a, sz, vp, pp) \
		pci_free_consistent((a)->pcidev, (sz), (vp), (pp))
#define SLIC_GET_PHYSICAL_ADDRESS(addr)       virt_to_bus((addr))
#define SLIC_GET_PHYSICAL_ADDRESS_HIGH(addr)  0

#define SLIC_GET_DMA_ADDRESS_WRITE(a, ptr, sz)  \
		pci_map_single((a)->pcidev, (ptr), (sz), PCI_DMA_TODEVICE)
#define SLIC_GET_DMA_ADDRESS_READ(a, ptr, sz)   \
		pci_map_single((a)->pcidev, (ptr), (sz), PCI_DMA_FROMDEVICE)
#define SLIC_UNGET_DMA_ADDRESS_WRITE(a, pa, sz) \
		pci_unmap_single((a)->pcidev, (pa), (sz), PCI_DMA_TODEVICE)
#define SLIC_UNGET_DMA_ADDRESS_READ(a, pa, sz)  \
		pci_unmap_single((a)->pcidev, (pa), (sz), PCI_DMA_FROMDEVICE)

#define SLIC_ZERO_MEMORY(p, sz)            memset((p), 0, (sz))
#define SLIC_EQUAL_MEMORY(src1, src2, len) (!memcmp(src1, src2, len))
#define SLIC_MOVE_MEMORY(dst, src, len)    memcpy((dst), (src), (len))

#define SLIC_SECS_TO_JIFFS(x)  ((x) * HZ)
#define SLIC_MS_TO_JIFFIES(x)  (SLIC_SECS_TO_JIFFS((x)) / 1000)

#ifdef DEBUG_REGISTER_TRACE
#define WRITE_REG(reg, value, flush)                                      \
	{                                                           \
		adapter->card->reg_type[adapter->card->debug_ix] = 0;   \
		adapter->card->reg_offset[adapter->card->debug_ix] = \
			((puchar)(&reg)) - ((puchar)adapter->slic_regs); \
		adapter->card->reg_value[adapter->card->debug_ix++] = value;  \
		if (adapter->card->debug_ix == 32) \
			adapter->card->debug_ix = 0;                      \
		slic_reg32_write((&reg), (value), (flush));            \
	}
#define WRITE_REG64(a, reg, value, regh, valh, flush)                        \
	{                                                           \
		adapter->card->reg_type[adapter->card->debug_ix] = 1;        \
		adapter->card->reg_offset[adapter->card->debug_ix] = \
			((puchar)(&reg)) - ((puchar)adapter->slic_regs); \
		adapter->card->reg_value[adapter->card->debug_ix] = value;   \
		adapter->card->reg_valueh[adapter->card->debug_ix++] = valh;  \
		if (adapter->card->debug_ix == 32) \
			adapter->card->debug_ix = 0;                      \
		slic_reg64_write((a), (&reg), (value), (&regh), (valh), \
				(flush));\
	}
#else
#define WRITE_REG(reg, value, flush) \
	slic_reg32_write((&reg), (value), (flush))
#define WRITE_REG64(a, reg, value, regh, valh, flush) \
	slic_reg64_write((a), (&reg), (value), (&regh), (valh), (flush))
#endif
#define READ_REG(reg, flush)                    slic_reg32_read((&reg), (flush))
#define READ_REGP16(reg, flush)                 slic_reg16_read((&reg), (flush))

#endif  /* _SLIC_OS_SPECIFIC_H_  */

