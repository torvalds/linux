/*
 * Copyright (C) 2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ASM_ESR_H
#define __ASM_ESR_H

#define ESR_EL1_EC_SHIFT	(26)
#define ESR_EL1_IL		(1U << 25)

#define ESR_EL1_EC_UNKNOWN	(0x00)
#define ESR_EL1_EC_WFI		(0x01)
#define ESR_EL1_EC_CP15_32	(0x03)
#define ESR_EL1_EC_CP15_64	(0x04)
#define ESR_EL1_EC_CP14_MR	(0x05)
#define ESR_EL1_EC_CP14_LS	(0x06)
#define ESR_EL1_EC_FP_ASIMD	(0x07)
#define ESR_EL1_EC_CP10_ID	(0x08)
#define ESR_EL1_EC_CP14_64	(0x0C)
#define ESR_EL1_EC_ILL_ISS	(0x0E)
#define ESR_EL1_EC_SVC32	(0x11)
#define ESR_EL1_EC_SVC64	(0x15)
#define ESR_EL1_EC_SYS64	(0x18)
#define ESR_EL1_EC_IABT_EL0	(0x20)
#define ESR_EL1_EC_IABT_EL1	(0x21)
#define ESR_EL1_EC_PC_ALIGN	(0x22)
#define ESR_EL1_EC_DABT_EL0	(0x24)
#define ESR_EL1_EC_DABT_EL1	(0x25)
#define ESR_EL1_EC_SP_ALIGN	(0x26)
#define ESR_EL1_EC_FP_EXC32	(0x28)
#define ESR_EL1_EC_FP_EXC64	(0x2C)
#define ESR_EL1_EC_SERROR	(0x2F)
#define ESR_EL1_EC_BREAKPT_EL0	(0x30)
#define ESR_EL1_EC_BREAKPT_EL1	(0x31)
#define ESR_EL1_EC_SOFTSTP_EL0	(0x32)
#define ESR_EL1_EC_SOFTSTP_EL1	(0x33)
#define ESR_EL1_EC_WATCHPT_EL0	(0x34)
#define ESR_EL1_EC_WATCHPT_EL1	(0x35)
#define ESR_EL1_EC_BKPT32	(0x38)
#define ESR_EL1_EC_BRK64	(0x3C)

#endif /* __ASM_ESR_H */
