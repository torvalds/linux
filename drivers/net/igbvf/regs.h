/*******************************************************************************

  Intel(R) 82576 Virtual Function Linux driver
  Copyright(c) 2009 Intel Corporation.

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
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_REGS_H_
#define _E1000_REGS_H_

#define E1000_CTRL      0x00000 /* Device Control - RW */
#define E1000_STATUS    0x00008 /* Device Status - RO */
#define E1000_ITR       0x000C4 /* Interrupt Throttling Rate - RW */
#define E1000_EICR      0x01580 /* Ext. Interrupt Cause Read - R/clr */
#define E1000_EITR(_n)  (0x01680 + (0x4 * (_n)))
#define E1000_EICS      0x01520 /* Ext. Interrupt Cause Set - W0 */
#define E1000_EIMS      0x01524 /* Ext. Interrupt Mask Set/Read - RW */
#define E1000_EIMC      0x01528 /* Ext. Interrupt Mask Clear - WO */
#define E1000_EIAC      0x0152C /* Ext. Interrupt Auto Clear - RW */
#define E1000_EIAM      0x01530 /* Ext. Interrupt Ack Auto Clear Mask - RW */
#define E1000_IVAR0     0x01700 /* Interrupt Vector Allocation (array) - RW */
#define E1000_IVAR_MISC 0x01740 /* IVAR for "other" causes - RW */
/*
 * Convenience macros
 *
 * Note: "_n" is the queue number of the register to be written to.
 *
 * Example usage:
 * E1000_RDBAL_REG(current_rx_queue)
 */
#define E1000_RDBAL(_n)      ((_n) < 4 ? (0x02800 + ((_n) * 0x100)) : \
                                         (0x0C000 + ((_n) * 0x40)))
#define E1000_RDBAH(_n)      ((_n) < 4 ? (0x02804 + ((_n) * 0x100)) : \
                                         (0x0C004 + ((_n) * 0x40)))
#define E1000_RDLEN(_n)      ((_n) < 4 ? (0x02808 + ((_n) * 0x100)) : \
                                         (0x0C008 + ((_n) * 0x40)))
#define E1000_SRRCTL(_n)     ((_n) < 4 ? (0x0280C + ((_n) * 0x100)) : \
                                         (0x0C00C + ((_n) * 0x40)))
#define E1000_RDH(_n)        ((_n) < 4 ? (0x02810 + ((_n) * 0x100)) : \
                                         (0x0C010 + ((_n) * 0x40)))
#define E1000_RDT(_n)        ((_n) < 4 ? (0x02818 + ((_n) * 0x100)) : \
                                         (0x0C018 + ((_n) * 0x40)))
#define E1000_RXDCTL(_n)     ((_n) < 4 ? (0x02828 + ((_n) * 0x100)) : \
                                         (0x0C028 + ((_n) * 0x40)))
#define E1000_TDBAL(_n)      ((_n) < 4 ? (0x03800 + ((_n) * 0x100)) : \
                                         (0x0E000 + ((_n) * 0x40)))
#define E1000_TDBAH(_n)      ((_n) < 4 ? (0x03804 + ((_n) * 0x100)) : \
                                         (0x0E004 + ((_n) * 0x40)))
#define E1000_TDLEN(_n)      ((_n) < 4 ? (0x03808 + ((_n) * 0x100)) : \
                                         (0x0E008 + ((_n) * 0x40)))
#define E1000_TDH(_n)        ((_n) < 4 ? (0x03810 + ((_n) * 0x100)) : \
                                         (0x0E010 + ((_n) * 0x40)))
#define E1000_TDT(_n)        ((_n) < 4 ? (0x03818 + ((_n) * 0x100)) : \
                                         (0x0E018 + ((_n) * 0x40)))
#define E1000_TXDCTL(_n)     ((_n) < 4 ? (0x03828 + ((_n) * 0x100)) : \
                                         (0x0E028 + ((_n) * 0x40)))
#define E1000_DCA_TXCTRL(_n) (0x03814 + (_n << 8))
#define E1000_DCA_RXCTRL(_n) (0x02814 + (_n << 8))
#define E1000_RAL(_i)  (((_i) <= 15) ? (0x05400 + ((_i) * 8)) : \
                                       (0x054E0 + ((_i - 16) * 8)))
#define E1000_RAH(_i)  (((_i) <= 15) ? (0x05404 + ((_i) * 8)) : \
                                       (0x054E4 + ((_i - 16) * 8)))

/* Statistics registers */
#define E1000_VFGPRC    0x00F10
#define E1000_VFGORC    0x00F18
#define E1000_VFMPRC    0x00F3C
#define E1000_VFGPTC    0x00F14
#define E1000_VFGOTC    0x00F34
#define E1000_VFGOTLBC  0x00F50
#define E1000_VFGPTLBC  0x00F44
#define E1000_VFGORLBC  0x00F48
#define E1000_VFGPRLBC  0x00F40

/* These act per VF so an array friendly macro is used */
#define E1000_V2PMAILBOX(_n)   (0x00C40 + (4 * (_n)))
#define E1000_VMBMEM(_n)       (0x00800 + (64 * (_n)))

/* Define macros for handling registers */
#define er32(reg) readl(hw->hw_addr + E1000_##reg)
#define ew32(reg, val) writel((val), hw->hw_addr +  E1000_##reg)
#define array_er32(reg, offset) \
	readl(hw->hw_addr + E1000_##reg + (offset << 2))
#define array_ew32(reg, offset, val) \
	writel((val), hw->hw_addr +  E1000_##reg + (offset << 2))
#define e1e_flush() er32(STATUS)

#endif
