/**
 * @file me8254_reg.h
 *
 * @brief 8254 counter register definitions.
 * @note Copyright (C) 2006 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

#ifndef _ME8254_REG_H_
#define _ME8254_REG_H_

#ifdef __KERNEL__

/* ME1400 A/B register offsets */
#define ME1400AB_8254_A_0_VAL_REG		0x0004 /**< Offset of 8254 A counter 0 value register. */
#define ME1400AB_8254_A_1_VAL_REG		0x0005 /**< Offset of 8254 A counter 1 value register. */
#define ME1400AB_8254_A_2_VAL_REG		0x0006 /**< Offset of 8254 A counter 2 value register. */
#define ME1400AB_8254_A_CTRL_REG		0x0007 /**< Offset of 8254 A control register. */

#define ME1400AB_8254_B_0_VAL_REG		0x000C /**< Offset of 8254 B counter 0 value register. */
#define ME1400AB_8254_B_1_VAL_REG		0x000D /**< Offset of 8254 B counter 1 value register. */
#define ME1400AB_8254_B_2_VAL_REG		0x000E /**< Offset of 8254 B counter 2 value register. */
#define ME1400AB_8254_B_CTRL_REG		0x000F /**< Offset of 8254 B control register. */

#define ME1400AB_CLK_SRC_REG			0x0010 /**< Offset of clock source register. */

/* ME1400 C register offsets */
#define ME1400C_8254_A_0_VAL_REG		0x0004 /**< Offset of 8254 A counter 0 value register. */
#define ME1400C_8254_A_1_VAL_REG		0x0005 /**< Offset of 8254 A counter 0 value register. */
#define ME1400C_8254_A_2_VAL_REG		0x0006 /**< Offset of 8254 A counter 0 value register. */
#define ME1400C_8254_A_CTRL_REG			0x0007 /**< Offset of 8254 A control register. */

#define ME1400C_8254_B_0_VAL_REG		0x000C /**< Offset of 8254 B counter 0 value register. */
#define ME1400C_8254_B_1_VAL_REG		0x000D /**< Offset of 8254 B counter 0 value register. */
#define ME1400C_8254_B_2_VAL_REG		0x000E /**< Offset of 8254 B counter 0 value register. */
#define ME1400C_8254_B_CTRL_REG			0x000F /**< Offset of 8254 B control register. */

#define ME1400C_8254_C_0_VAL_REG		0x0010 /**< Offset of 8254 C counter 0 value register. */
#define ME1400C_8254_C_1_VAL_REG		0x0011 /**< Offset of 8254 C counter 0 value register. */
#define ME1400C_8254_C_2_VAL_REG		0x0012 /**< Offset of 8254 C counter 0 value register. */
#define ME1400C_8254_C_CTRL_REG			0x0013 /**< Offset of 8254 C control register. */

#define ME1400C_8254_D_0_VAL_REG		0x0014 /**< Offset of 8254 D counter 0 value register. */
#define ME1400C_8254_D_1_VAL_REG		0x0015 /**< Offset of 8254 D counter 0 value register. */
#define ME1400C_8254_D_2_VAL_REG		0x0016 /**< Offset of 8254 D counter 0 value register. */
#define ME1400C_8254_D_CTRL_REG			0x0017 /**< Offset of 8254 D control register. */

#define ME1400C_8254_E_0_VAL_REG		0x0018 /**< Offset of 8254 E counter 0 value register. */
#define ME1400C_8254_E_1_VAL_REG		0x0019 /**< Offset of 8254 E counter 0 value register. */
#define ME1400C_8254_E_2_VAL_REG		0x001A /**< Offset of 8254 E counter 0 value register. */
#define ME1400C_8254_E_CTRL_REG			0x001B /**< Offset of 8254 E control register. */

#define ME1400C_CLK_SRC_0_REG  			0x001C /**< Offset of clock source register 0. */
#define ME1400C_CLK_SRC_1_REG  			0x001D /**< Offset of clock source register 1. */
#define ME1400C_CLK_SRC_2_REG  			0x001E /**< Offset of clock source register 2. */

/* ME1400 D register offsets */
#define ME1400D_8254_A_0_VAL_REG		0x0044 /**< Offset of 8254 A counter 0 value register. */
#define ME1400D_8254_A_1_VAL_REG		0x0045 /**< Offset of 8254 A counter 0 value register. */
#define ME1400D_8254_A_2_VAL_REG		0x0046 /**< Offset of 8254 A counter 0 value register. */
#define ME1400D_8254_A_CTRL_REG			0x0047 /**< Offset of 8254 A control register. */

#define ME1400D_8254_B_0_VAL_REG		0x004C /**< Offset of 8254 B counter 0 value register. */
#define ME1400D_8254_B_1_VAL_REG		0x004D /**< Offset of 8254 B counter 0 value register. */
#define ME1400D_8254_B_2_VAL_REG		0x004E /**< Offset of 8254 B counter 0 value register. */
#define ME1400D_8254_B_CTRL_REG			0x004F /**< Offset of 8254 B control register. */

#define ME1400D_8254_C_0_VAL_REG		0x0050 /**< Offset of 8254 C counter 0 value register. */
#define ME1400D_8254_C_1_VAL_REG		0x0051 /**< Offset of 8254 C counter 0 value register. */
#define ME1400D_8254_C_2_VAL_REG		0x0052 /**< Offset of 8254 C counter 0 value register. */
#define ME1400D_8254_C_CTRL_REG			0x0053 /**< Offset of 8254 C control register. */

#define ME1400D_8254_D_0_VAL_REG		0x0054 /**< Offset of 8254 D counter 0 value register. */
#define ME1400D_8254_D_1_VAL_REG		0x0055 /**< Offset of 8254 D counter 0 value register. */
#define ME1400D_8254_D_2_VAL_REG		0x0056 /**< Offset of 8254 D counter 0 value register. */
#define ME1400D_8254_D_CTRL_REG			0x0057 /**< Offset of 8254 D control register. */

#define ME1400D_8254_E_0_VAL_REG		0x0058 /**< Offset of 8254 E counter 0 value register. */
#define ME1400D_8254_E_1_VAL_REG		0x0059 /**< Offset of 8254 E counter 0 value register. */
#define ME1400D_8254_E_2_VAL_REG		0x005A /**< Offset of 8254 E counter 0 value register. */
#define ME1400D_8254_E_CTRL_REG			0x005B /**< Offset of 8254 E control register. */

#define ME1400D_CLK_SRC_0_REG  			0x005C /**< Offset of clock source register 0. */
#define ME1400D_CLK_SRC_1_REG  			0x005D /**< Offset of clock source register 1. */
#define ME1400D_CLK_SRC_2_REG  			0x005E /**< Offset of clock source register 2. */

/* ME4600 register offsets */
#define ME4600_8254_0_VAL_REG			0x0000 /**< Offset of 8254 A counter 0 value register. */
#define ME4600_8254_1_VAL_REG			0x0001 /**< Offset of 8254 A counter 0 value register. */
#define ME4600_8254_2_VAL_REG			0x0002 /**< Offset of 8254 A counter 0 value register. */
#define ME4600_8254_CTRL_REG			0x0003 /**< Offset of 8254 A control register. */

/* Command words for 8254 control register */
#define ME8254_CTRL_SC0   0x00	 /**< Counter 0 selection. */
#define ME8254_CTRL_SC1   0x40	 /**< Counter 1 selection. */
#define ME8254_CTRL_SC2   0x80	 /**< Counter 2 selection. */

#define ME8254_CTRL_TLO   0x00	 /**< Counter latching operation. */
#define ME8254_CTRL_LSB   0x10	 /**< Only read LSB. */
#define ME8254_CTRL_MSB   0x20	 /**< Only read MSB. */
#define ME8254_CTRL_LM    0x30	 /**< First read LSB, then MSB.  */

#define ME8254_CTRL_M0    0x00	 /**< Mode 0 selection. */
#define ME8254_CTRL_M1    0x02	 /**< Mode 1 selection. */
#define ME8254_CTRL_M2    0x04	 /**< Mode 2 selection. */
#define ME8254_CTRL_M3    0x06	 /**< Mode 3 selection. */
#define ME8254_CTRL_M4    0x08	 /**< Mode 4 selection. */
#define ME8254_CTRL_M5    0x0A	 /**< Mode 5 selection. */

#define ME8254_CTRL_BIN   0x00	 /**< Binary counter. */
#define ME8254_CTRL_BCD   0x01	 /**< BCD counter. */

/* ME-1400 A/B clock source register bits */
#define ME1400AB_8254_A_0_CLK_SRC_1MHZ		(0 << 7)	/**< 1MHz clock. */
#define ME1400AB_8254_A_0_CLK_SRC_10MHZ		(1 << 7)	/**< 10MHz clock. */
#define ME1400AB_8254_A_0_CLK_SRC_PIN		(0 << 6)	/**< CLK 0 to SUB-D. */
#define ME1400AB_8254_A_0_CLK_SRC_QUARZ		(1 << 6)	/**< Connect CLK 0 with quarz. */

#define ME1400AB_8254_A_1_CLK_SRC_PIN		(0 << 5)	/**< CLK 1 to SUB-D. */
#define ME1400AB_8254_A_1_CLK_SRC_PREV		(1 << 5)	/**< Connect OUT 0 with CLK 1. */

#define ME1400AB_8254_A_2_CLK_SRC_PIN		(0 << 4)	/**< CLK 2 to SUB-D. */
#define ME1400AB_8254_A_2_CLK_SRC_PREV		(1 << 4)	/**< Connect OUT 1 with CLK 2. */

#define ME1400AB_8254_B_0_CLK_SRC_1MHZ		(0 << 3)	/**< 1MHz clock. */
#define ME1400AB_8254_B_0_CLK_SRC_10MHZ		(1 << 3)	/**< 10MHz clock. */
#define ME1400AB_8254_B_0_CLK_SRC_PIN		(0 << 2)	/**< CLK 0 to SUB-D. */
#define ME1400AB_8254_B_0_CLK_SRC_QUARZ		(1 << 2)	/**< Connect CLK 0 with quarz. */

#define ME1400AB_8254_B_1_CLK_SRC_PIN		(0 << 1)	/**< CLK 1 to SUB-D. */
#define ME1400AB_8254_B_1_CLK_SRC_PREV		(1 << 1)	/**< Connect OUT 0 with CLK 1. */

#define ME1400AB_8254_B_2_CLK_SRC_PIN		(0 << 0)	/**< CLK 2 to SUB-D. */
#define ME1400AB_8254_B_2_CLK_SRC_PREV		(1 << 0)	/**< Connect OUT 1 with CLK 2. */

/* ME-1400 C/D clock source registers bits */
#define ME1400CD_8254_ACE_0_CLK_SRC_MASK	0x03	/**< Masks all CLK source bits. */
#define ME1400CD_8254_ACE_0_CLK_SRC_PIN		0x00	/**< Connect CLK to SUB-D. */
#define ME1400CD_8254_ACE_0_CLK_SRC_1MHZ	0x01	/**< Connect CLK to 1MHz. */
#define ME1400CD_8254_ACE_0_CLK_SRC_10MHZ	0x02	/**< Connect CLK to 10MHz. */
#define ME1400CD_8254_ACE_0_CLK_SRC_PREV	0x03	/**< Connect CLK to previous counter output on ME-1400 D extension. */

#define ME1400CD_8254_ACE_1_CLK_SRC_MASK	0x04	/**< Masks all CLK source bits. */
#define ME1400CD_8254_ACE_1_CLK_SRC_PIN		0x00	/**< Connect CLK to SUB-D. */
#define ME1400CD_8254_ACE_1_CLK_SRC_PREV	0x04	/**< Connect CLK to previous counter output. */

#define ME1400CD_8254_ACE_2_CLK_SRC_MASK	0x08	/**< Masks all CLK source bits. */
#define ME1400CD_8254_ACE_2_CLK_SRC_PIN		0x00	/**< Connect to SUB-D. */
#define ME1400CD_8254_ACE_2_CLK_SRC_PREV	0x08	/**< Connect CLK to previous counter output. */

#define ME1400CD_8254_BD_0_CLK_SRC_MASK		0x30	/**< Masks all CLK source bits. */
#define ME1400CD_8254_BD_0_CLK_SRC_PIN		0x00	/**< Connect CLK to SUB-D. */
#define ME1400CD_8254_BD_0_CLK_SRC_1MHZ		0x10	/**< Connect CLK to 1MHz. */
#define ME1400CD_8254_BD_0_CLK_SRC_10MHZ	0x20	/**< Connect CLK to 10MHz. */
#define ME1400CD_8254_BD_0_CLK_SRC_PREV		0x30	/**< Connect CLK to previous counter output. */

#define ME1400CD_8254_BD_1_CLK_SRC_MASK		0x40	/**< Masks all CLK source bits. */
#define ME1400CD_8254_BD_1_CLK_SRC_PIN		0x00	/**< Connect CLK to SUB-D. */
#define ME1400CD_8254_BD_1_CLK_SRC_PREV		0x40	/**< Connect CLK to previous counter output. */

#define ME1400CD_8254_BD_2_CLK_SRC_MASK		0x80	/**< Masks all CLK source bits. */
#define ME1400CD_8254_BD_2_CLK_SRC_PIN		0x00	/**< Connect CLK to SUB-D. */
#define ME1400CD_8254_BD_2_CLK_SRC_PREV		0x80	/**< Connect CLK to previous counter output. */

/* ME-8100 counter registers */
#define ME8100_COUNTER_REG_0				0x18	//(r,w)
#define ME8100_COUNTER_REG_1				0x1A	//(r,w)
#define ME8100_COUNTER_REG_2				0x1C	//(r,w)
#define ME8100_COUNTER_CTRL_REG				0x1E	//(r,w)

#endif
#endif
