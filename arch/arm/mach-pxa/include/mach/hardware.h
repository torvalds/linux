/*
 *  arch/arm/mach-pxa/include/mach/hardware.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/*
 * We requires absolute addresses.
 */
#define PCIO_BASE		0

/*
 * Workarounds for at least 2 errata so far require this.
 * The mapping is set in mach-pxa/generic.c.
 */
#define UNCACHED_PHYS_0		0xff000000
#define UNCACHED_ADDR		UNCACHED_PHYS_0

/*
 * Intel PXA2xx internal register mapping:
 *
 * 0x40000000 - 0x41ffffff <--> 0xf2000000 - 0xf3ffffff
 * 0x44000000 - 0x45ffffff <--> 0xf4000000 - 0xf5ffffff
 * 0x48000000 - 0x49ffffff <--> 0xf6000000 - 0xf7ffffff
 * 0x4c000000 - 0x4dffffff <--> 0xf8000000 - 0xf9ffffff
 * 0x50000000 - 0x51ffffff <--> 0xfa000000 - 0xfbffffff
 * 0x54000000 - 0x55ffffff <--> 0xfc000000 - 0xfdffffff
 * 0x58000000 - 0x59ffffff <--> 0xfe000000 - 0xffffffff
 *
 * Note that not all PXA2xx chips implement all those addresses, and the
 * kernel only maps the minimum needed range of this mapping.
 */
#define io_p2v(x) (0xf2000000 + ((x) & 0x01ffffff) + (((x) & 0x1c000000) >> 1))
#define io_v2p(x) (0x3c000000 + ((x) & 0x01ffffff) + (((x) & 0x0e000000) << 1))

#ifndef __ASSEMBLY__

# define __REG(x)	(*((volatile u32 *)io_p2v(x)))

/* With indexed regs we don't want to feed the index through io_p2v()
   especially if it is a variable, otherwise horrible code will result. */
# define __REG2(x,y)	\
	(*(volatile u32 *)((u32)&__REG(x) + (y)))

# define __PREG(x)	(io_v2p((u32)&(x)))

#else

# define __REG(x)	io_p2v(x)
# define __PREG(x)	io_v2p(x)

#endif

#ifndef __ASSEMBLY__

#include <asm/cputype.h>

#ifdef CONFIG_PXA25x
#define __cpu_is_pxa21x(id)				\
	({						\
		unsigned int _id = (id) >> 4 & 0xf3f;	\
		_id == 0x212;				\
	})

#define __cpu_is_pxa255(id)                             \
	({                                              \
		unsigned int _id = (id) >> 4 & 0xfff;   \
		_id == 0x2d0;                           \
	 })

#define __cpu_is_pxa25x(id)				\
	({						\
		unsigned int _id = (id) >> 4 & 0xfff;	\
		_id == 0x2d0 || _id == 0x290;		\
	})
#else
#define __cpu_is_pxa21x(id)	(0)
#define __cpu_is_pxa255(id)	(0)
#define __cpu_is_pxa25x(id)	(0)
#endif

#ifdef CONFIG_PXA27x
#define __cpu_is_pxa27x(id)				\
	({						\
		unsigned int _id = (id) >> 4 & 0xfff;	\
		_id == 0x411;				\
	})
#else
#define __cpu_is_pxa27x(id)	(0)
#endif

#ifdef CONFIG_CPU_PXA300
#define __cpu_is_pxa300(id)				\
	({						\
		unsigned int _id = (id) >> 4 & 0xfff;	\
		_id == 0x688;				\
	 })
#else
#define __cpu_is_pxa300(id)	(0)
#endif

#ifdef CONFIG_CPU_PXA310
#define __cpu_is_pxa310(id)				\
	({						\
		unsigned int _id = (id) >> 4 & 0xfff;	\
		_id == 0x689;				\
	 })
#else
#define __cpu_is_pxa310(id)	(0)
#endif

#ifdef CONFIG_CPU_PXA320
#define __cpu_is_pxa320(id)				\
	({						\
		unsigned int _id = (id) >> 4 & 0xfff;	\
		_id == 0x603 || _id == 0x682;		\
	 })
#else
#define __cpu_is_pxa320(id)	(0)
#endif

#ifdef CONFIG_CPU_PXA930
#define __cpu_is_pxa930(id)				\
	({						\
		unsigned int _id = (id) >> 4 & 0xfff;	\
		_id == 0x683;		\
	 })
#else
#define __cpu_is_pxa930(id)	(0)
#endif

#define cpu_is_pxa21x()					\
	({						\
		__cpu_is_pxa21x(read_cpuid_id());	\
	})

#define cpu_is_pxa255()                                 \
	({                                              \
		__cpu_is_pxa255(read_cpuid_id());       \
	})

#define cpu_is_pxa25x()					\
	({						\
		__cpu_is_pxa25x(read_cpuid_id());	\
	})

#define cpu_is_pxa27x()					\
	({						\
		__cpu_is_pxa27x(read_cpuid_id());	\
	})

#define cpu_is_pxa300()					\
	({						\
		__cpu_is_pxa300(read_cpuid_id());	\
	 })

#define cpu_is_pxa310()					\
	({						\
		__cpu_is_pxa310(read_cpuid_id());	\
	 })

#define cpu_is_pxa320()					\
	({						\
		__cpu_is_pxa320(read_cpuid_id());	\
	 })

#define cpu_is_pxa930()					\
	({						\
		unsigned int id = read_cpuid(CPUID_ID);	\
		__cpu_is_pxa930(id);			\
	 })

/*
 * CPUID Core Generation Bit
 * <= 0x2 for pxa21x/pxa25x/pxa26x/pxa27x
 * == 0x3 for pxa300/pxa310/pxa320
 */
#define __cpu_is_pxa2xx(id)				\
	({						\
		unsigned int _id = (id) >> 13 & 0x7;	\
		_id <= 0x2;				\
	 })

#define __cpu_is_pxa3xx(id)				\
	({						\
		unsigned int _id = (id) >> 13 & 0x7;	\
		_id == 0x3;				\
	 })

#define cpu_is_pxa2xx()					\
	({						\
		__cpu_is_pxa2xx(read_cpuid_id());	\
	 })

#define cpu_is_pxa3xx()					\
	({						\
		__cpu_is_pxa3xx(read_cpuid_id());	\
	 })

/*
 * Handy routine to set GPIO alternate functions
 */
extern int pxa_gpio_mode( int gpio_mode );

/*
 * Return GPIO level, nonzero means high, zero is low
 */
extern int pxa_gpio_get_value(unsigned gpio);

/*
 * Set output GPIO level
 */
extern void pxa_gpio_set_value(unsigned gpio, int value);

/*
 * return current memory and LCD clock frequency in units of 10kHz
 */
extern unsigned int get_memclk_frequency_10khz(void);

#endif

#if defined(CONFIG_MACH_ARMCORE) && defined(CONFIG_PCI)
#define PCIBIOS_MIN_IO		0
#define PCIBIOS_MIN_MEM		0
#define pcibios_assign_all_busses()	1
#endif

#endif  /* _ASM_ARCH_HARDWARE_H */
