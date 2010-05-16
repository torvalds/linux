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

/*
 *   CPU     Stepping     CPU_ID         JTAG_ID
 *
 *  PXA210	B0	0x69052922	0x2926C013
 *  PXA210	B1	0x69052923	0x3926C013
 *  PXA210	B2	0x69052924	0x4926C013
 *  PXA210	C0	0x69052D25	0x5926C013
 *
 *  PXA250	A0	0x69052100	0x09264013
 *  PXA250	A1	0x69052101	0x19264013
 *  PXA250	B0	0x69052902	0x29264013
 *  PXA250	B1	0x69052903	0x39264013
 *  PXA250	B2	0x69052904	0x49264013
 *  PXA250	C0	0x69052D05	0x59264013
 *
 *  PXA255	A0	0x69052D06	0x69264013
 *
 *  PXA26x	A0	0x69052903	0x39264013
 *  PXA26x	B0	0x69052D05	0x59264013
 *
 *  PXA27x	A0	0x69054110	0x09265013
 *  PXA27x	A1	0x69054111	0x19265013
 *  PXA27x	B0	0x69054112	0x29265013
 *  PXA27x	B1	0x69054113	0x39265013
 *  PXA27x	C0	0x69054114	0x49265013
 *  PXA27x	C5	0x69054117	0x79265013
 *
 *  PXA30x	A0	0x69056880	0x0E648013
 *  PXA30x	A1	0x69056881	0x1E648013
 *  PXA31x	A0	0x69056890	0x0E649013
 *  PXA31x	A1	0x69056891	0x1E649013
 *  PXA31x	A2	0x69056892	0x2E649013
 *  PXA32x	B1	0x69056825	0x5E642013
 *  PXA32x	B2	0x69056826	0x6E642013
 *
 *  PXA930	B0	0x69056835	0x5E643013
 *  PXA930	B1	0x69056837	0x7E643013
 *  PXA930	B2	0x69056838	0x8E643013
 *
 *  PXA935	A0	0x56056931	0x1E653013
 *  PXA935	B0	0x56056936	0x6E653013
 *  PXA935	B1	0x56056938	0x8E653013
 */
#ifdef CONFIG_PXA25x
#define __cpu_is_pxa210(id)				\
	({						\
		unsigned int _id = (id) & 0xf3f0;	\
		_id == 0x2120;				\
	})

#define __cpu_is_pxa250(id)				\
	({						\
		unsigned int _id = (id) & 0xf3ff;	\
		_id <= 0x2105;				\
	})

#define __cpu_is_pxa255(id)				\
	({						\
		unsigned int _id = (id) & 0xffff;	\
		_id == 0x2d06;				\
	})

#define __cpu_is_pxa25x(id)				\
	({						\
		unsigned int _id = (id) & 0xf300;	\
		_id == 0x2100;				\
	})
#else
#define __cpu_is_pxa210(id)	(0)
#define __cpu_is_pxa250(id)	(0)
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
		_id == 0x683;				\
	 })
#else
#define __cpu_is_pxa930(id)	(0)
#endif

#ifdef CONFIG_CPU_PXA935
#define __cpu_is_pxa935(id)				\
	({						\
		unsigned int _id = (id) >> 4 & 0xfff;	\
		_id == 0x693;				\
	 })
#else
#define __cpu_is_pxa935(id)	(0)
#endif

#ifdef CONFIG_CPU_PXA950
#define __cpu_is_pxa950(id)                             \
	({                                              \
		unsigned int _id = (id) >> 4 & 0xfff;	\
		_id == 0x697;				\
	 })
#else
#define __cpu_is_pxa950(id)	(0)
#endif

#define cpu_is_pxa210()					\
	({						\
		__cpu_is_pxa210(read_cpuid_id());	\
	})

#define cpu_is_pxa250()					\
	({						\
		__cpu_is_pxa250(read_cpuid_id());	\
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
		__cpu_is_pxa930(read_cpuid_id());	\
	 })

#define cpu_is_pxa935()					\
	({						\
		__cpu_is_pxa935(read_cpuid_id());	\
	 })

#define cpu_is_pxa950()					\
	({						\
		__cpu_is_pxa950(read_cpuid_id());	\
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

#define __cpu_is_pxa93x(id)				\
	({						\
		unsigned int _id = (id) >> 4 & 0xfff;	\
		_id == 0x683 || _id == 0x693;		\
	 })

#define cpu_is_pxa2xx()					\
	({						\
		__cpu_is_pxa2xx(read_cpuid_id());	\
	 })

#define cpu_is_pxa3xx()					\
	({						\
		__cpu_is_pxa3xx(read_cpuid_id());	\
	 })

#define cpu_is_pxa93x()					\
	({						\
		__cpu_is_pxa93x(read_cpuid_id());	\
	 })
/*
 * return current memory and LCD clock frequency in units of 10kHz
 */
extern unsigned int get_memclk_frequency_10khz(void);

/* return the clock tick rate of the OS timer */
extern unsigned long get_clock_tick_rate(void);
#endif

#if defined(CONFIG_MACH_ARMCORE) && defined(CONFIG_PCI)
#define PCIBIOS_MIN_IO		0
#define PCIBIOS_MIN_MEM		0
#define pcibios_assign_all_busses()	1
#endif


#endif  /* _ASM_ARCH_HARDWARE_H */
