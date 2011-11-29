// ------------------------------------------------------------------
// Copyright (c) 2004-2007 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
// ------------------------------------------------------------------
//===================================================================
// Author(s): ="Atheros"
//===================================================================


#ifndef _MDIO_REG_REG_H_
#define _MDIO_REG_REG_H_

#define MDIO_REG_ADDRESS                         0x00000000
#define MDIO_REG_OFFSET                          0x00000000
#define MDIO_REG_VALUE_MSB                       15
#define MDIO_REG_VALUE_LSB                       0
#define MDIO_REG_VALUE_MASK                      0x0000ffff
#define MDIO_REG_VALUE_GET(x)                    (((x) & MDIO_REG_VALUE_MASK) >> MDIO_REG_VALUE_LSB)
#define MDIO_REG_VALUE_SET(x)                    (((x) << MDIO_REG_VALUE_LSB) & MDIO_REG_VALUE_MASK)

#define MDIO_ISR_ADDRESS                         0x00000020
#define MDIO_ISR_OFFSET                          0x00000020
#define MDIO_ISR_MASK_MSB                        15
#define MDIO_ISR_MASK_LSB                        8
#define MDIO_ISR_MASK_MASK                       0x0000ff00
#define MDIO_ISR_MASK_GET(x)                     (((x) & MDIO_ISR_MASK_MASK) >> MDIO_ISR_MASK_LSB)
#define MDIO_ISR_MASK_SET(x)                     (((x) << MDIO_ISR_MASK_LSB) & MDIO_ISR_MASK_MASK)
#define MDIO_ISR_REGS_MSB                        7
#define MDIO_ISR_REGS_LSB                        0
#define MDIO_ISR_REGS_MASK                       0x000000ff
#define MDIO_ISR_REGS_GET(x)                     (((x) & MDIO_ISR_REGS_MASK) >> MDIO_ISR_REGS_LSB)
#define MDIO_ISR_REGS_SET(x)                     (((x) << MDIO_ISR_REGS_LSB) & MDIO_ISR_REGS_MASK)

#define PHY_ADDR_ADDRESS                         0x00000024
#define PHY_ADDR_OFFSET                          0x00000024
#define PHY_ADDR_VAL_MSB                         2
#define PHY_ADDR_VAL_LSB                         0
#define PHY_ADDR_VAL_MASK                        0x00000007
#define PHY_ADDR_VAL_GET(x)                      (((x) & PHY_ADDR_VAL_MASK) >> PHY_ADDR_VAL_LSB)
#define PHY_ADDR_VAL_SET(x)                      (((x) << PHY_ADDR_VAL_LSB) & PHY_ADDR_VAL_MASK)


#ifndef __ASSEMBLER__

typedef struct mdio_reg_reg_s {
  volatile unsigned int mdio_reg[8];
  volatile unsigned int mdio_isr;
  volatile unsigned int phy_addr;
} mdio_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _MDIO_REG_H_ */
