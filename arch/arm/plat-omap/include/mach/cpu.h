/*
 * arch/arm/plat-omap/include/mach/cpu.h
 *
 * OMAP cpu type detection
 *
 * Copyright (C) 2004, 2008 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __ASM_ARCH_OMAP_CPU_H
#define __ASM_ARCH_OMAP_CPU_H

struct omap_chip_id {
	u8 oc;
};

#define OMAP_CHIP_INIT(x)	{ .oc = x }

extern unsigned int system_rev;

#define omap2_cpu_rev()		((system_rev >> 12) & 0x0f)

/*
 * Test if multicore OMAP support is needed
 */
#undef MULTI_OMAP1
#undef MULTI_OMAP2
#undef OMAP_NAME

#ifdef CONFIG_ARCH_OMAP730
# ifdef OMAP_NAME
#  undef  MULTI_OMAP1
#  define MULTI_OMAP1
# else
#  define OMAP_NAME omap730
# endif
#endif
#ifdef CONFIG_ARCH_OMAP15XX
# ifdef OMAP_NAME
#  undef  MULTI_OMAP1
#  define MULTI_OMAP1
# else
#  define OMAP_NAME omap1510
# endif
#endif
#ifdef CONFIG_ARCH_OMAP16XX
# ifdef OMAP_NAME
#  undef  MULTI_OMAP1
#  define MULTI_OMAP1
# else
#  define OMAP_NAME omap16xx
# endif
#endif
#if (defined(CONFIG_ARCH_OMAP24XX) || defined(CONFIG_ARCH_OMAP34XX))
# if (defined(OMAP_NAME) || defined(MULTI_OMAP1))
#  error "OMAP1 and OMAP2 can't be selected at the same time"
# endif
#endif
#ifdef CONFIG_ARCH_OMAP2420
# ifdef OMAP_NAME
#  undef  MULTI_OMAP2
#  define MULTI_OMAP2
# else
#  define OMAP_NAME omap2420
# endif
#endif
#ifdef CONFIG_ARCH_OMAP2430
# ifdef OMAP_NAME
#  undef  MULTI_OMAP2
#  define MULTI_OMAP2
# else
#  define OMAP_NAME omap2430
# endif
#endif
#ifdef CONFIG_ARCH_OMAP3430
# ifdef OMAP_NAME
#  undef  MULTI_OMAP2
#  define MULTI_OMAP2
# else
#  define OMAP_NAME omap3430
# endif
#endif

/*
 * Macros to group OMAP into cpu classes.
 * These can be used in most places.
 * cpu_is_omap7xx():	True for OMAP730
 * cpu_is_omap15xx():	True for OMAP1510, OMAP5910 and OMAP310
 * cpu_is_omap16xx():	True for OMAP1610, OMAP5912 and OMAP1710
 * cpu_is_omap24xx():	True for OMAP2420, OMAP2422, OMAP2423, OMAP2430
 * cpu_is_omap242x():	True for OMAP2420, OMAP2422, OMAP2423
 * cpu_is_omap243x():	True for OMAP2430
 * cpu_is_omap343x():	True for OMAP3430
 */
#define GET_OMAP_CLASS	((system_rev >> 24) & 0xff)

#define IS_OMAP_CLASS(class, id)			\
static inline int is_omap ##class (void)		\
{							\
	return (GET_OMAP_CLASS == (id)) ? 1 : 0;	\
}

#define GET_OMAP_SUBCLASS	((system_rev >> 20) & 0x0fff)

#define IS_OMAP_SUBCLASS(subclass, id)			\
static inline int is_omap ##subclass (void)		\
{							\
	return (GET_OMAP_SUBCLASS == (id)) ? 1 : 0;	\
}

IS_OMAP_CLASS(7xx, 0x07)
IS_OMAP_CLASS(15xx, 0x15)
IS_OMAP_CLASS(16xx, 0x16)
IS_OMAP_CLASS(24xx, 0x24)
IS_OMAP_CLASS(34xx, 0x34)

IS_OMAP_SUBCLASS(242x, 0x242)
IS_OMAP_SUBCLASS(243x, 0x243)
IS_OMAP_SUBCLASS(343x, 0x343)

#define cpu_is_omap7xx()		0
#define cpu_is_omap15xx()		0
#define cpu_is_omap16xx()		0
#define cpu_is_omap24xx()		0
#define cpu_is_omap242x()		0
#define cpu_is_omap243x()		0
#define cpu_is_omap34xx()		0
#define cpu_is_omap343x()		0

#if defined(MULTI_OMAP1)
# if defined(CONFIG_ARCH_OMAP730)
#  undef  cpu_is_omap7xx
#  define cpu_is_omap7xx()		is_omap7xx()
# endif
# if defined(CONFIG_ARCH_OMAP15XX)
#  undef  cpu_is_omap15xx
#  define cpu_is_omap15xx()		is_omap15xx()
# endif
# if defined(CONFIG_ARCH_OMAP16XX)
#  undef  cpu_is_omap16xx
#  define cpu_is_omap16xx()		is_omap16xx()
# endif
#else
# if defined(CONFIG_ARCH_OMAP730)
#  undef  cpu_is_omap7xx
#  define cpu_is_omap7xx()		1
# endif
# if defined(CONFIG_ARCH_OMAP15XX)
#  undef  cpu_is_omap15xx
#  define cpu_is_omap15xx()		1
# endif
# if defined(CONFIG_ARCH_OMAP16XX)
#  undef  cpu_is_omap16xx
#  define cpu_is_omap16xx()		1
# endif
#endif

#if defined(MULTI_OMAP2)
# if defined(CONFIG_ARCH_OMAP24XX)
#  undef  cpu_is_omap24xx
#  undef  cpu_is_omap242x
#  undef  cpu_is_omap243x
#  define cpu_is_omap24xx()		is_omap24xx()
#  define cpu_is_omap242x()		is_omap242x()
#  define cpu_is_omap243x()		is_omap243x()
# endif
# if defined(CONFIG_ARCH_OMAP34XX)
#  undef  cpu_is_omap34xx
#  undef  cpu_is_omap343x
#  define cpu_is_omap34xx()		is_omap34xx()
#  define cpu_is_omap343x()		is_omap343x()
# endif
#else
# if defined(CONFIG_ARCH_OMAP24XX)
#  undef  cpu_is_omap24xx
#  define cpu_is_omap24xx()		1
# endif
# if defined(CONFIG_ARCH_OMAP2420)
#  undef  cpu_is_omap242x
#  define cpu_is_omap242x()		1
# endif
# if defined(CONFIG_ARCH_OMAP2430)
#  undef  cpu_is_omap243x
#  define cpu_is_omap243x()		1
# endif
# if defined(CONFIG_ARCH_OMAP34XX)
#  undef  cpu_is_omap34xx
#  define cpu_is_omap34xx()		1
# endif
# if defined(CONFIG_ARCH_OMAP3430)
#  undef  cpu_is_omap343x
#  define cpu_is_omap343x()		1
# endif
#endif

/*
 * Macros to detect individual cpu types.
 * These are only rarely needed.
 * cpu_is_omap330():	True for OMAP330
 * cpu_is_omap730():	True for OMAP730
 * cpu_is_omap1510():	True for OMAP1510
 * cpu_is_omap1610():	True for OMAP1610
 * cpu_is_omap1611():	True for OMAP1611
 * cpu_is_omap5912():	True for OMAP5912
 * cpu_is_omap1621():	True for OMAP1621
 * cpu_is_omap1710():	True for OMAP1710
 * cpu_is_omap2420():	True for OMAP2420
 * cpu_is_omap2422():	True for OMAP2422
 * cpu_is_omap2423():	True for OMAP2423
 * cpu_is_omap2430():	True for OMAP2430
 * cpu_is_omap3430():	True for OMAP3430
 */
#define GET_OMAP_TYPE	((system_rev >> 16) & 0xffff)

#define IS_OMAP_TYPE(type, id)				\
static inline int is_omap ##type (void)			\
{							\
	return (GET_OMAP_TYPE == (id)) ? 1 : 0;		\
}

IS_OMAP_TYPE(310, 0x0310)
IS_OMAP_TYPE(730, 0x0730)
IS_OMAP_TYPE(1510, 0x1510)
IS_OMAP_TYPE(1610, 0x1610)
IS_OMAP_TYPE(1611, 0x1611)
IS_OMAP_TYPE(5912, 0x1611)
IS_OMAP_TYPE(1621, 0x1621)
IS_OMAP_TYPE(1710, 0x1710)
IS_OMAP_TYPE(2420, 0x2420)
IS_OMAP_TYPE(2422, 0x2422)
IS_OMAP_TYPE(2423, 0x2423)
IS_OMAP_TYPE(2430, 0x2430)
IS_OMAP_TYPE(3430, 0x3430)

#define cpu_is_omap310()		0
#define cpu_is_omap730()		0
#define cpu_is_omap1510()		0
#define cpu_is_omap1610()		0
#define cpu_is_omap5912()		0
#define cpu_is_omap1611()		0
#define cpu_is_omap1621()		0
#define cpu_is_omap1710()		0
#define cpu_is_omap2420()		0
#define cpu_is_omap2422()		0
#define cpu_is_omap2423()		0
#define cpu_is_omap2430()		0
#define cpu_is_omap3430()		0

#if defined(MULTI_OMAP1)
# if defined(CONFIG_ARCH_OMAP730)
#  undef  cpu_is_omap730
#  define cpu_is_omap730()		is_omap730()
# endif
#else
# if defined(CONFIG_ARCH_OMAP730)
#  undef  cpu_is_omap730
#  define cpu_is_omap730()		1
# endif
#endif

/*
 * Whether we have MULTI_OMAP1 or not, we still need to distinguish
 * between 330 vs. 1510 and 1611B/5912 vs. 1710.
 */
#if defined(CONFIG_ARCH_OMAP15XX)
# undef  cpu_is_omap310
# undef  cpu_is_omap1510
# define cpu_is_omap310()		is_omap310()
# define cpu_is_omap1510()		is_omap1510()
#endif

#if defined(CONFIG_ARCH_OMAP16XX)
# undef  cpu_is_omap1610
# undef  cpu_is_omap1611
# undef  cpu_is_omap5912
# undef  cpu_is_omap1621
# undef  cpu_is_omap1710
# define cpu_is_omap1610()		is_omap1610()
# define cpu_is_omap1611()		is_omap1611()
# define cpu_is_omap5912()		is_omap5912()
# define cpu_is_omap1621()		is_omap1621()
# define cpu_is_omap1710()		is_omap1710()
#endif

#if defined(CONFIG_ARCH_OMAP24XX)
# undef  cpu_is_omap2420
# undef  cpu_is_omap2422
# undef  cpu_is_omap2423
# undef  cpu_is_omap2430
# define cpu_is_omap2420()		is_omap2420()
# define cpu_is_omap2422()		is_omap2422()
# define cpu_is_omap2423()		is_omap2423()
# define cpu_is_omap2430()		is_omap2430()
#endif

#if defined(CONFIG_ARCH_OMAP34XX)
# undef cpu_is_omap3430
# define cpu_is_omap3430()		is_omap3430()
#endif

/* Macros to detect if we have OMAP1 or OMAP2 */
#define cpu_class_is_omap1()	(cpu_is_omap730() || cpu_is_omap15xx() || \
				cpu_is_omap16xx())
#define cpu_class_is_omap2()	(cpu_is_omap24xx() || cpu_is_omap34xx())

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
/*
 * Macros to detect silicon revision of OMAP2/3 processors.
 * is_sil_rev_greater_than:	true if passed cpu type & its rev is greater.
 * is_sil_rev_lesser_than:	true if passed cpu type & its rev is lesser.
 * is_sil_rev_equal_to:		true if passed cpu type & its rev is equal.
 * get_sil_rev:			return the silicon rev value.
 */
#define get_sil_omap_type(rev)	((rev & 0xffff0000) >> 16)
#define get_sil_revision(rev)	((rev & 0x0000f000) >> 12)

#define is_sil_rev_greater_than(rev) \
		((get_sil_omap_type(system_rev) == get_sil_omap_type(rev)) && \
		(get_sil_revision(system_rev) > get_sil_revision(rev)))

#define is_sil_rev_less_than(rev) \
		((get_sil_omap_type(system_rev) == get_sil_omap_type(rev)) && \
		(get_sil_revision(system_rev) < get_sil_revision(rev)))

#define is_sil_rev_equal_to(rev) \
		((get_sil_omap_type(system_rev) == get_sil_omap_type(rev)) && \
		(get_sil_revision(system_rev) == get_sil_revision(rev)))

#define get_sil_rev() \
		get_sil_revision(system_rev)

/* Various silicon macros defined here */
#define OMAP2420_REV_ES1_0	0x24200000
#define OMAP2420_REV_ES2_0	0x24201000
#define OMAP2430_REV_ES1_0	0x24300000
#define OMAP3430_REV_ES1_0	0x34300000
#define OMAP3430_REV_ES2_0	0x34301000
#define OMAP3430_REV_ES2_1	0x34302000
#define OMAP3430_REV_ES2_2	0x34303000

/*
 * omap_chip bits
 *
 * CHIP_IS_OMAP{2420,2430,3430} indicate that a particular structure is
 * valid on all chips of that type.  CHIP_IS_OMAP3430ES{1,2} indicates
 * something that is only valid on that particular ES revision.
 *
 * These bits may be ORed together to indicate structures that are
 * available on multiple chip types.
 *
 * To test whether a particular structure matches the current OMAP chip type,
 * use omap_chip_is().
 *
 */
#define CHIP_IS_OMAP2420       (1 << 0)
#define CHIP_IS_OMAP2430       (1 << 1)
#define CHIP_IS_OMAP3430       (1 << 2)
#define CHIP_IS_OMAP3430ES1    (1 << 3)
#define CHIP_IS_OMAP3430ES2    (1 << 4)

#define CHIP_IS_OMAP24XX       (CHIP_IS_OMAP2420 | CHIP_IS_OMAP2430)

int omap_chip_is(struct omap_chip_id oci);


/*
 * Macro to detect device type i.e. EMU/HS/TST/GP/BAD
 */
#define DEVICE_TYPE_TEST	0
#define DEVICE_TYPE_EMU		1
#define DEVICE_TYPE_SEC		2
#define DEVICE_TYPE_GP		3
#define DEVICE_TYPE_BAD		4

#define get_device_type()	((system_rev & 0x700) >> 8)
#define is_device_type_test()	(get_device_type() == DEVICE_TYPE_TEST)
#define is_device_type_emu()	(get_device_type() == DEVICE_TYPE_EMU)
#define is_device_type_sec()	(get_device_type() == DEVICE_TYPE_SEC)
#define is_device_type_gp()	(get_device_type() == DEVICE_TYPE_GP)
#define is_device_type_bad()	(get_device_type() == DEVICE_TYPE_BAD)

void omap2_check_revision(void);

#endif    /* defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3) */

#endif
