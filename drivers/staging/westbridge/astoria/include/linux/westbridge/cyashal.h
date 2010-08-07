/* Cypress West Bridge API header file (cyashal.h)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street
## Fifth Floor, Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef _INCLUDED_CYASHAL_H_
#define _INCLUDED_CYASHAL_H_

#if !defined(__doxygen__)

/* The possible HAL layers defined and implemented by Cypress */

#ifdef __CY_ASTORIA_FPGA_HAL__
#ifdef CY_HAL_DEFINED
#error only one HAL layer can be defined
#endif

#define CY_HAL_DEFINED

#include "cyashalfpga.h"
#endif

/***** SCM User space HAL  ****/
#ifdef __CY_ASTORIA_SCM_HAL__
#ifdef CY_HAL_DEFINED
#error only one HAL layer can be defined
#endif

#define CY_HAL_DEFINEDŚŚ

#include "cyanhalscm.h"
#endif
/***** SCM User space HAL  ****/

/***** SCM Kernel HAL  ****/
#ifdef __CY_ASTORIA_SCM_KERNEL_HAL__
#ifdef CY_HAL_DEFINED
#error only one HAL layer can be defined
#endif

#define CY_HAL_DEFINEDŚ

#include "cyanhalscm_kernel.h"
#endif
/***** SCM Kernel HAL  ****/

/***** OMAP5912 Kernel HAL  ****/
#ifdef __CY_ASTORIA_OMAP_5912_KERNEL_HAL__
 #ifdef CY_HAL_DEFINED
  #error only one HAL layer can be defined
 #endif

 #define CY_HAL_DEFINED

 #include "cyanhalomap_kernel.h"
#endif
/***** eof OMAP5912 Kernel HAL  ****/



/***** OMAP3430 Kernel HAL  ****/
#ifdef CONFIG_MACH_OMAP3_WESTBRIDGE_AST_PNAND_HAL

 #ifdef CY_HAL_DEFINED
  #error only one HAL layer can be defined
 #endif

 #define CY_HAL_DEFINED
/* moved to staging location, eventual implementation
 * considered is here
 * #include mach/westbridge/westbridge-omap3-pnand-hal/cyashalomap_kernel.h>
*/
 #include "../../../arch/arm/plat-omap/include/mach/westbridge/westbridge-omap3-pnand-hal/cyashalomap_kernel.h"

#endif
/*****************************/


/******/
#ifdef __CY_ASTORIA_CUSTOMER_HAL__
#ifdef CY_HAL_DEFINED
#error only one HAL layer can be defined
#endif
br
#define CY_HAL_DEFINED
#include "cyashal_customer.h"

#endif

#endif			/* __doxygen__ */

#endif			/* _INCLUDED_CYASHAL_H_ */
