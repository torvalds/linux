/*
 * arch/arm/plat-omap/include/mach/cpu.h
 *
 * OMAP cpu type detection
 *
 * Copyright (C) 2004, 2008 Nokia Corporation
 *
 * Copyright (C) 2009-11 Texas Instruments.
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * Added OMAP4 specific defines - Santosh Shilimkar<santosh.shilimkar@ti.com>
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

#include <linux/bitops.h>
#include <plat/multi.h>

/*
 * Omap device type i.e. EMU/HS/TST/GP/BAD
 */
#define OMAP2_DEVICE_TYPE_TEST		0
#define OMAP2_DEVICE_TYPE_EMU		1
#define OMAP2_DEVICE_TYPE_SEC		2
#define OMAP2_DEVICE_TYPE_GP		3
#define OMAP2_DEVICE_TYPE_BAD		4

int omap_type(void);

/*
 * omap_rev bits:
 * CPU id bits	(0730, 1510, 1710, 2422...)	[31:16]
 * CPU revision	(See _REV_ defined in cpu.h)	[15:08]
 * CPU class bits (15xx, 16xx, 24xx, 34xx...)	[07:00]
 */
unsigned int omap_rev(void);

/*
 * Get the CPU revision for OMAP devices
 */
#define GET_OMAP_REVISION()	((omap_rev() >> 8) & 0xff)

/*
 * Macros to group OMAP into cpu classes.
 * These can be used in most places.
 * cpu_is_omap7xx():	True for OMAP730, OMAP850
 * cpu_is_omap15xx():	True for OMAP1510, OMAP5910 and OMAP310
 * cpu_is_omap16xx():	True for OMAP1610, OMAP5912 and OMAP1710
 * cpu_is_omap24xx():	True for OMAP2420, OMAP2422, OMAP2423, OMAP2430
 * cpu_is_omap242x():	True for OMAP2420, OMAP2422, OMAP2423
 * cpu_is_omap243x():	True for OMAP2430
 * cpu_is_omap343x():	True for OMAP3430
 * cpu_is_omap443x():	True for OMAP4430
 * cpu_is_omap446x():	True for OMAP4460
 * cpu_is_omap447x():	True for OMAP4470
 */
#define GET_OMAP_CLASS	(omap_rev() & 0xff)

#define IS_OMAP_CLASS(class, id)			\
static inline int is_omap ##class (void)		\
{							\
	return (GET_OMAP_CLASS == (id)) ? 1 : 0;	\
}

#define GET_AM_CLASS	((omap_rev() >> 24) & 0xff)

#define IS_AM_CLASS(class, id)				\
static inline int is_am ##class (void)			\
{							\
	return (GET_AM_CLASS == (id)) ? 1 : 0;		\
}

#define GET_TI_CLASS	((omap_rev() >> 24) & 0xff)

#define IS_TI_CLASS(class, id)			\
static inline int is_ti ##class (void)		\
{							\
	return (GET_TI_CLASS == (id)) ? 1 : 0;	\
}

#define GET_OMAP_SUBCLASS	((omap_rev() >> 20) & 0x0fff)

#define IS_OMAP_SUBCLASS(subclass, id)			\
static inline int is_omap ##subclass (void)		\
{							\
	return (GET_OMAP_SUBCLASS == (id)) ? 1 : 0;	\
}

#define IS_TI_SUBCLASS(subclass, id)			\
static inline int is_ti ##subclass (void)		\
{							\
	return (GET_OMAP_SUBCLASS == (id)) ? 1 : 0;	\
}

#define IS_AM_SUBCLASS(subclass, id)			\
static inline int is_am ##subclass (void)		\
{							\
	return (GET_OMAP_SUBCLASS == (id)) ? 1 : 0;	\
}

IS_OMAP_CLASS(7xx, 0x07)
IS_OMAP_CLASS(15xx, 0x15)
IS_OMAP_CLASS(16xx, 0x16)
IS_OMAP_CLASS(24xx, 0x24)
IS_OMAP_CLASS(34xx, 0x34)
IS_OMAP_CLASS(44xx, 0x44)
IS_AM_CLASS(35xx, 0x35)
IS_AM_CLASS(33xx, 0x33)

IS_TI_CLASS(81xx, 0x81)

IS_OMAP_SUBCLASS(242x, 0x242)
IS_OMAP_SUBCLASS(243x, 0x243)
IS_OMAP_SUBCLASS(343x, 0x343)
IS_OMAP_SUBCLASS(363x, 0x363)
IS_OMAP_SUBCLASS(443x, 0x443)
IS_OMAP_SUBCLASS(446x, 0x446)
IS_OMAP_SUBCLASS(447x, 0x447)

IS_TI_SUBCLASS(816x, 0x816)
IS_TI_SUBCLASS(814x, 0x814)
IS_AM_SUBCLASS(335x, 0x335)

#define cpu_is_omap7xx()		0
#define cpu_is_omap15xx()		0
#define cpu_is_omap16xx()		0
#define cpu_is_omap24xx()		0
#define cpu_is_omap242x()		0
#define cpu_is_omap243x()		0
#define cpu_is_omap34xx()		0
#define cpu_is_omap343x()		0
#define cpu_is_ti81xx()			0
#define cpu_is_ti816x()			0
#define cpu_is_ti814x()			0
#define soc_is_am35xx()			0
#define soc_is_am33xx()			0
#define soc_is_am335x()			0
#define cpu_is_omap44xx()		0
#define cpu_is_omap443x()		0
#define cpu_is_omap446x()		0
#define cpu_is_omap447x()		0

#if defined(MULTI_OMAP1)
# if defined(CONFIG_ARCH_OMAP730)
#  undef  cpu_is_omap7xx
#  define cpu_is_omap7xx()		is_omap7xx()
# endif
# if defined(CONFIG_ARCH_OMAP850)
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
# if defined(CONFIG_ARCH_OMAP850)
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
# if defined(CONFIG_ARCH_OMAP2)
#  undef  cpu_is_omap24xx
#  define cpu_is_omap24xx()		is_omap24xx()
# endif
# if defined (CONFIG_SOC_OMAP2420)
#  undef  cpu_is_omap242x
#  define cpu_is_omap242x()		is_omap242x()
# endif
# if defined (CONFIG_SOC_OMAP2430)
#  undef  cpu_is_omap243x
#  define cpu_is_omap243x()		is_omap243x()
# endif
# if defined(CONFIG_ARCH_OMAP3)
#  undef  cpu_is_omap34xx
#  undef  cpu_is_omap343x
#  define cpu_is_omap34xx()		is_omap34xx()
#  define cpu_is_omap343x()		is_omap343x()
# endif
#else
# if defined(CONFIG_ARCH_OMAP2)
#  undef  cpu_is_omap24xx
#  define cpu_is_omap24xx()		1
# endif
# if defined(CONFIG_SOC_OMAP2420)
#  undef  cpu_is_omap242x
#  define cpu_is_omap242x()		1
# endif
# if defined(CONFIG_SOC_OMAP2430)
#  undef  cpu_is_omap243x
#  define cpu_is_omap243x()		1
# endif
# if defined(CONFIG_ARCH_OMAP3)
#  undef  cpu_is_omap34xx
#  define cpu_is_omap34xx()		1
# endif
# if defined(CONFIG_SOC_OMAP3430)
#  undef  cpu_is_omap343x
#  define cpu_is_omap343x()		1
# endif
#endif

/*
 * Macros to detect individual cpu types.
 * These are only rarely needed.
 * cpu_is_omap310():	True for OMAP310
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
#define GET_OMAP_TYPE	((omap_rev() >> 16) & 0xffff)

#define IS_OMAP_TYPE(type, id)				\
static inline int is_omap ##type (void)			\
{							\
	return (GET_OMAP_TYPE == (id)) ? 1 : 0;		\
}

IS_OMAP_TYPE(310, 0x0310)
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
#define cpu_is_omap3630()		0

/*
 * Whether we have MULTI_OMAP1 or not, we still need to distinguish
 * between 310 vs. 1510 and 1611B/5912 vs. 1710.
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

#if defined(CONFIG_ARCH_OMAP2)
# undef  cpu_is_omap2420
# undef  cpu_is_omap2422
# undef  cpu_is_omap2423
# undef  cpu_is_omap2430
# define cpu_is_omap2420()		is_omap2420()
# define cpu_is_omap2422()		is_omap2422()
# define cpu_is_omap2423()		is_omap2423()
# define cpu_is_omap2430()		is_omap2430()
#endif

#if defined(CONFIG_ARCH_OMAP3)
# undef cpu_is_omap3430
# undef cpu_is_ti81xx
# undef cpu_is_ti816x
# undef cpu_is_ti814x
# undef soc_is_am35xx
# define cpu_is_omap3430()		is_omap3430()
# undef cpu_is_omap3630
# define cpu_is_omap3630()		is_omap363x()
# define cpu_is_ti81xx()		is_ti81xx()
# define cpu_is_ti816x()		is_ti816x()
# define cpu_is_ti814x()		is_ti814x()
# define soc_is_am35xx()		is_am35xx()
#endif

# if defined(CONFIG_SOC_AM33XX)
# undef soc_is_am33xx
# undef soc_is_am335x
# define soc_is_am33xx()		is_am33xx()
# define soc_is_am335x()		is_am335x()
#endif

# if defined(CONFIG_ARCH_OMAP4)
# undef cpu_is_omap44xx
# undef cpu_is_omap443x
# undef cpu_is_omap446x
# undef cpu_is_omap447x
# define cpu_is_omap44xx()		is_omap44xx()
# define cpu_is_omap443x()		is_omap443x()
# define cpu_is_omap446x()		is_omap446x()
# define cpu_is_omap447x()		is_omap447x()
# endif

/* Macros to detect if we have OMAP1 or OMAP2 */
#define cpu_class_is_omap1()	(cpu_is_omap7xx() || cpu_is_omap15xx() || \
				cpu_is_omap16xx())
#define cpu_class_is_omap2()	(cpu_is_omap24xx() || cpu_is_omap34xx() || \
				cpu_is_omap44xx())

/* Various silicon revisions for omap2 */
#define OMAP242X_CLASS		0x24200024
#define OMAP2420_REV_ES1_0	OMAP242X_CLASS
#define OMAP2420_REV_ES2_0	(OMAP242X_CLASS | (0x1 << 8))

#define OMAP243X_CLASS		0x24300024
#define OMAP2430_REV_ES1_0	OMAP243X_CLASS

#define OMAP343X_CLASS		0x34300034
#define OMAP3430_REV_ES1_0	OMAP343X_CLASS
#define OMAP3430_REV_ES2_0	(OMAP343X_CLASS | (0x1 << 8))
#define OMAP3430_REV_ES2_1	(OMAP343X_CLASS | (0x2 << 8))
#define OMAP3430_REV_ES3_0	(OMAP343X_CLASS | (0x3 << 8))
#define OMAP3430_REV_ES3_1	(OMAP343X_CLASS | (0x4 << 8))
#define OMAP3430_REV_ES3_1_2	(OMAP343X_CLASS | (0x5 << 8))

#define OMAP363X_CLASS		0x36300034
#define OMAP3630_REV_ES1_0	OMAP363X_CLASS
#define OMAP3630_REV_ES1_1	(OMAP363X_CLASS | (0x1 << 8))
#define OMAP3630_REV_ES1_2	(OMAP363X_CLASS | (0x2 << 8))

#define TI816X_CLASS		0x81600034
#define TI8168_REV_ES1_0	TI816X_CLASS
#define TI8168_REV_ES1_1	(TI816X_CLASS | (0x1 << 8))

#define TI814X_CLASS		0x81400034
#define TI8148_REV_ES1_0	TI814X_CLASS
#define TI8148_REV_ES2_0	(TI814X_CLASS | (0x1 << 8))
#define TI8148_REV_ES2_1	(TI814X_CLASS | (0x2 << 8))

#define AM35XX_CLASS		0x35170034
#define AM35XX_REV_ES1_0	AM35XX_CLASS
#define AM35XX_REV_ES1_1	(AM35XX_CLASS | (0x1 << 8))

#define AM335X_CLASS		0x33500033
#define AM335X_REV_ES1_0	AM335X_CLASS

#define OMAP443X_CLASS		0x44300044
#define OMAP4430_REV_ES1_0	(OMAP443X_CLASS | (0x10 << 8))
#define OMAP4430_REV_ES2_0	(OMAP443X_CLASS | (0x20 << 8))
#define OMAP4430_REV_ES2_1	(OMAP443X_CLASS | (0x21 << 8))
#define OMAP4430_REV_ES2_2	(OMAP443X_CLASS | (0x22 << 8))
#define OMAP4430_REV_ES2_3	(OMAP443X_CLASS | (0x23 << 8))

#define OMAP446X_CLASS		0x44600044
#define OMAP4460_REV_ES1_0	(OMAP446X_CLASS | (0x10 << 8))
#define OMAP4460_REV_ES1_1	(OMAP446X_CLASS | (0x11 << 8))

#define OMAP447X_CLASS		0x44700044
#define OMAP4470_REV_ES1_0	(OMAP447X_CLASS | (0x10 << 8))

void omap2xxx_check_revision(void);
void omap3xxx_check_revision(void);
void omap4xxx_check_revision(void);
void omap3xxx_check_features(void);
void ti81xx_check_features(void);
void omap4xxx_check_features(void);

/*
 * Runtime detection of OMAP3 features
 *
 * OMAP3_HAS_IO_CHAIN_CTRL: Some later members of the OMAP3 chip
 *    family have OS-level control over the I/O chain clock.  This is
 *    to avoid a window during which wakeups could potentially be lost
 *    during powerdomain transitions.  If this bit is set, it
 *    indicates that the chip does support OS-level control of this
 *    feature.
 */
extern u32 omap_features;

#define OMAP3_HAS_L2CACHE		BIT(0)
#define OMAP3_HAS_IVA			BIT(1)
#define OMAP3_HAS_SGX			BIT(2)
#define OMAP3_HAS_NEON			BIT(3)
#define OMAP3_HAS_ISP			BIT(4)
#define OMAP3_HAS_192MHZ_CLK		BIT(5)
#define OMAP3_HAS_IO_WAKEUP		BIT(6)
#define OMAP3_HAS_SDRC			BIT(7)
#define OMAP3_HAS_IO_CHAIN_CTRL		BIT(8)
#define OMAP4_HAS_MPU_1GHZ		BIT(9)
#define OMAP4_HAS_MPU_1_2GHZ		BIT(10)
#define OMAP4_HAS_MPU_1_5GHZ		BIT(11)


#define OMAP3_HAS_FEATURE(feat,flag)			\
static inline unsigned int omap3_has_ ##feat(void)	\
{							\
	return omap_features & OMAP3_HAS_ ##flag;	\
}							\

OMAP3_HAS_FEATURE(l2cache, L2CACHE)
OMAP3_HAS_FEATURE(sgx, SGX)
OMAP3_HAS_FEATURE(iva, IVA)
OMAP3_HAS_FEATURE(neon, NEON)
OMAP3_HAS_FEATURE(isp, ISP)
OMAP3_HAS_FEATURE(192mhz_clk, 192MHZ_CLK)
OMAP3_HAS_FEATURE(io_wakeup, IO_WAKEUP)
OMAP3_HAS_FEATURE(sdrc, SDRC)
OMAP3_HAS_FEATURE(io_chain_ctrl, IO_CHAIN_CTRL)

/*
 * Runtime detection of OMAP4 features
 */
#define OMAP4_HAS_FEATURE(feat, flag)			\
static inline unsigned int omap4_has_ ##feat(void)	\
{							\
	return omap_features & OMAP4_HAS_ ##flag;	\
}							\

OMAP4_HAS_FEATURE(mpu_1ghz, MPU_1GHZ)
OMAP4_HAS_FEATURE(mpu_1_2ghz, MPU_1_2GHZ)
OMAP4_HAS_FEATURE(mpu_1_5ghz, MPU_1_5GHZ)

#endif
