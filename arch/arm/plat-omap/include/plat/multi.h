/*
 * Support for compiling in multiple OMAP processors
 *
 * Copyright (C) 2010 Nokia Corporation
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

#ifndef __PLAT_OMAP_MULTI_H
#define __PLAT_OMAP_MULTI_H

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
#ifdef CONFIG_ARCH_OMAP850
# ifdef OMAP_NAME
#  undef  MULTI_OMAP1
#  define MULTI_OMAP1
# else
#  define OMAP_NAME omap850
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
#ifdef CONFIG_ARCH_OMAP2PLUS
# if (defined(OMAP_NAME) || defined(MULTI_OMAP1))
#  error "OMAP1 and OMAP2PLUS can't be selected at the same time"
# endif
#endif
#ifdef CONFIG_SOC_OMAP2420
# ifdef OMAP_NAME
#  undef  MULTI_OMAP2
#  define MULTI_OMAP2
# else
#  define OMAP_NAME omap2420
# endif
#endif
#ifdef CONFIG_SOC_OMAP2430
# ifdef OMAP_NAME
#  undef  MULTI_OMAP2
#  define MULTI_OMAP2
# else
#  define OMAP_NAME omap2430
# endif
#endif
#ifdef CONFIG_ARCH_OMAP3
# ifdef OMAP_NAME
#  undef  MULTI_OMAP2
#  define MULTI_OMAP2
# else
#  define OMAP_NAME omap3
# endif
#endif
#ifdef CONFIG_ARCH_OMAP4
# ifdef OMAP_NAME
#  undef  MULTI_OMAP2
#  define MULTI_OMAP2
# else
#  define OMAP_NAME omap4
# endif
#endif

#endif	/* __PLAT_OMAP_MULTI_H */
