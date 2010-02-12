/*
 * arch/arm/plat-omap/include/mach/memory.h
 *
 * Memory map for OMAP-1510 and 1610
 *
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * This file was derived from arch/arm/mach-intergrator/include/mach/memory.h
 * Copyright (C) 1999 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/*
 * Physical DRAM offset.
 */
#if defined(CONFIG_ARCH_OMAP1)
#define PHYS_OFFSET		UL(0x10000000)
#else
#define PHYS_OFFSET		UL(0x80000000)
#endif

/*
 * Bus address is physical address, except for OMAP-1510 Local Bus.
 * OMAP-1510 bus address is translated into a Local Bus address if the
 * OMAP bus type is lbus. We do the address translation based on the
 * device overriding the defaults used in the dma-mapping API.
 * Note that the is_lbus_device() test is not very efficient on 1510
 * because of the strncmp().
 */
#ifdef CONFIG_ARCH_OMAP15XX

/*
 * OMAP-1510 Local Bus address offset
 */
#define OMAP1510_LB_OFFSET	UL(0x30000000)

#define virt_to_lbus(x)		((x) - PAGE_OFFSET + OMAP1510_LB_OFFSET)
#define lbus_to_virt(x)		((x) - OMAP1510_LB_OFFSET + PAGE_OFFSET)
#define is_lbus_device(dev)	(cpu_is_omap15xx() && dev && (strncmp(dev_name(dev), "ohci", 4) == 0))

#define __arch_page_to_dma(dev, page)	\
	({ dma_addr_t __dma = page_to_phys(page); \
	   if (is_lbus_device(dev)) \
		__dma = __dma - PHYS_OFFSET + OMAP1510_LB_OFFSET; \
	   __dma; })

#define __arch_dma_to_page(dev, addr)	\
	({ dma_addr_t __dma = addr;				\
	   if (is_lbus_device(dev))				\
		__dma += PHYS_OFFSET - OMAP1510_LB_OFFSET;	\
	   phys_to_page(__dma);					\
	})

#define __arch_dma_to_virt(dev, addr)	({ (void *) (is_lbus_device(dev) ? \
						lbus_to_virt(addr) : \
						__phys_to_virt(addr)); })

#define __arch_virt_to_dma(dev, addr)	({ unsigned long __addr = (unsigned long)(addr); \
					   (dma_addr_t) (is_lbus_device(dev) ? \
						virt_to_lbus(__addr) : \
						__virt_to_phys(__addr)); })

#endif	/* CONFIG_ARCH_OMAP15XX */

/* Override the ARM default */
#ifdef CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE

#if (CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE == 0)
#undef CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE
#define CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE 2
#endif

#define CONSISTENT_DMA_SIZE \
	(((CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE + 1) & ~1) * 1024 * 1024)

#endif

#endif

