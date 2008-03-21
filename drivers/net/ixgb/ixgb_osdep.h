/*******************************************************************************

  Intel PRO/10GbE Linux driver
  Copyright(c) 1999 - 2006 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* glue for the OS independent part of ixgb
 * includes register access macros
 */

#ifndef _IXGB_OSDEP_H_
#define _IXGB_OSDEP_H_

#include <linux/types.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#undef ASSERT
#define ASSERT(x)	if(!(x)) BUG()
#define MSGOUT(S, A, B)	printk(KERN_DEBUG S "\n", A, B)

#ifdef DBG
#define DEBUGOUT(S)		printk(KERN_DEBUG S "\n")
#define DEBUGOUT1(S, A...)	printk(KERN_DEBUG S "\n", A)
#else
#define DEBUGOUT(S)
#define DEBUGOUT1(S, A...)
#endif

#define DEBUGFUNC(F) DEBUGOUT(F)
#define DEBUGOUT2 DEBUGOUT1
#define DEBUGOUT3 DEBUGOUT2
#define DEBUGOUT7 DEBUGOUT3

#define IXGB_WRITE_REG(a, reg, value) ( \
	writel((value), ((a)->hw_addr + IXGB_##reg)))

#define IXGB_READ_REG(a, reg) ( \
	readl((a)->hw_addr + IXGB_##reg))

#define IXGB_WRITE_REG_ARRAY(a, reg, offset, value) ( \
	writel((value), ((a)->hw_addr + IXGB_##reg + ((offset) << 2))))

#define IXGB_READ_REG_ARRAY(a, reg, offset) ( \
	readl((a)->hw_addr + IXGB_##reg + ((offset) << 2)))

#define IXGB_WRITE_FLUSH(a) IXGB_READ_REG(a, STATUS)

#define IXGB_MEMCPY memcpy

#endif /* _IXGB_OSDEP_H_ */
