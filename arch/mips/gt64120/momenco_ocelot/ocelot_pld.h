/*
 * Ocelot Board Register Definitions
 *
 * (C) 2001 Red Hat, Inc.
 *
 * GPL'd
 */
#ifndef __MOMENCO_OCELOT_PLD_H__
#define __MOMENCO_OCELOT_PLD_H__

#define OCELOT_CS0_ADDR (0xe0020000)

#define OCELOT_REG_BOARDREV (0)
#define OCELOT_REG_PLD1_ID (1)
#define OCELOT_REG_PLD2_ID (2)
#define OCELOT_REG_RESET_STATUS (3)
#define OCELOT_REG_BOARD_STATUS (4)
#define OCELOT_REG_CPCI_ID (5)
#define OCELOT_REG_I2C_CTRL (8)
#define OCELOT_REG_EEPROM_MODE (9)
#define OCELOT_REG_INTMASK (10)
#define OCELOT_REG_INTSTATUS (11)
#define OCELOT_REG_INTSET (12)
#define OCELOT_REG_INTCLR (13)

#define OCELOT_PLD_WRITE(x, y) writeb(x, OCELOT_CS0_ADDR + OCELOT_REG_##y)
#define OCELOT_PLD_READ(x) readb(OCELOT_CS0_ADDR + OCELOT_REG_##x)


#endif /* __MOMENCO_OCELOT_PLD_H__ */
