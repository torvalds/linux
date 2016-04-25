/*
 * Hardware definitions for HP iPAQ h5xxx Handheld Computers
 *
 * Copyright(20)02 Hewlett-Packard Company.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * COMPAQ COMPUTER CORPORATION MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
 * AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
 * FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 * Author: Jamey Hicks
 */

#ifndef __ASM_ARCH_H5000_H
#define __ASM_ARCH_H5000_H

#include "mfp-pxa25x.h"

/*
 * CPU GPIOs
 */

#define H5000_GPIO_POWER_BUTTON   (0)
#define H5000_GPIO_RESET_BUTTON_N (1)
#define H5000_GPIO_OPT_INT        (2)
#define H5000_GPIO_BACKUP_POWER   (3)
#define H5000_GPIO_ACTION_BUTTON  (4)
#define H5000_GPIO_COM_DCD_SOMETHING  (5) /* what is this really ? */
/* 6 not connected */
#define H5000_GPIO_RESET_BUTTON_AGAIN_N (7) /* connected to gpio 1 as well */
/* 8 not connected */
#define H5000_GPIO_RSO_N          (9)       /* reset output from max1702 which regulates 3.3 and 2.5 */
#define H5000_GPIO_ASIC_INT_N   (10)       /* from companion asic */
#define H5000_GPIO_BT_ENV_0     (11)       /* to LMX9814, set to 1 according to regdump */
/*(12) not connected */
#define H5000_GPIO_BT_ENV_1     (13)       /* to LMX9814, set to 1 according to regdump */
#define H5000_GPIO_BT_WU        (14)       /* from LMX9814, Defined as HOST_WAKEUP in the LMX9820 data sheet */
/*(15) is CS1# */
/*(16) not connected */
/*(17) not connected */
/*(18) is pcmcia ready */
/*(19) is dreq1 */
/*(20) is dreq0 */
#define H5000_GPIO_OE_RD_NWR	(21)       /* output enable on rd/nwr signal to companion asic */
/*(22) is not connected */
#define H5000_GPIO_OPT_SPI_CLK  (23)       /* to extension pack */
#define H5000_GPIO_OPT_SPI_CS_N (24)       /* to extension pack */
#define H5000_GPIO_OPT_SPI_DOUT (25)       /* to extension pack */
#define H5000_GPIO_OPT_SPI_DIN  (26)       /* to extension pack */
/*(27) not connected */
#define H5000_GPIO_I2S_BITCLK   (28)       /* connected to AC97 codec */
#define H5000_GPIO_I2S_DATAOUT  (29)       /* connected to AC97 codec */
#define H5000_GPIO_I2S_DATAIN   (30)       /* connected to AC97 codec */
#define H5000_GPIO_I2S_LRCLK    (31)       /* connected to AC97 codec */
#define H5000_GPIO_I2S_SYSCLK   (32)       /* connected to AC97 codec */
/*(33) is CS5# */
#define H5000_GPIO_COM_RXD      (34)       /* connected to cradle/cable connector */
#define H5000_GPIO_COM_CTS      (35)       /* connected to cradle/cable connector */
#define H5000_GPIO_COM_DCD      (36)       /* connected to cradle/cable connector */
#define H5000_GPIO_COM_DSR      (37)       /* connected to cradle/cable connector */
#define H5000_GPIO_COM_RI       (38)       /* connected to cradle/cable connector */
#define H5000_GPIO_COM_TXD      (39)       /* connected to cradle/cable connector */
#define H5000_GPIO_COM_DTR      (40)       /* connected to cradle/cable connector */
#define H5000_GPIO_COM_RTS      (41)       /* connected to cradle/cable connector */

#define H5000_GPIO_BT_RXD       (42)       /* connected to BT (LMX9814) */
#define H5000_GPIO_BT_TXD       (43)       /* connected to BT (LMX9814) */
#define H5000_GPIO_BT_CTS       (44)       /* connected to BT (LMX9814) */
#define H5000_GPIO_BT_RTS       (45)       /* connected to BT (LMX9814) */

#define H5000_GPIO_IRDA_RXD     (46)
#define H5000_GPIO_IRDA_TXD     (47)

#define H5000_GPIO_POE_N        (48)       /* used for pcmcia */
#define H5000_GPIO_PWE_N        (49)       /* used for pcmcia */
#define H5000_GPIO_PIOR_N       (50)       /* used for pcmcia */
#define H5000_GPIO_PIOW_N       (51)       /* used for pcmcia */
#define H5000_GPIO_PCE1_N       (52)       /* used for pcmcia */
#define H5000_GPIO_PCE2_N       (53)       /* used for pcmcia */
#define H5000_GPIO_PSKTSEL      (54)       /* used for pcmcia */
#define H5000_GPIO_PREG_N       (55)       /* used for pcmcia */
#define H5000_GPIO_PWAIT_N      (56)       /* used for pcmcia */
#define H5000_GPIO_IOIS16_N     (57)       /* used for pcmcia */

#define H5000_GPIO_IRDA_SD      (58)       /* to hsdl3002 sd */
/*(59) not connected */
#define H5000_GPIO_POWER_SD_N   (60)       /* controls power to SD */
#define H5000_GPIO_POWER_RS232_N	(61)       /* inverted FORCEON to rs232 transceiver */
#define H5000_GPIO_POWER_ACCEL_N	(62)       /* controls power to accel */
/*(63) is not connected */
#define H5000_GPIO_OPT_NVRAM    (64)       /* controls power to expansion pack */
#define H5000_GPIO_CHG_EN       (65)       /* to sc801 en */
#define H5000_GPIO_USB_PULLUP   (66)       /* USB d+ pullup via 1.5K resistor */
#define H5000_GPIO_BT_2V8_N     (67)       /* 2.8V used by bluetooth */
#define H5000_GPIO_EXT_CHG_RATE (68)       /* enables external charging rate */
/*(69) is not connected */
#define H5000_GPIO_CIR_RESET    (70)       /* consumer IR reset */
#define H5000_GPIO_POWER_LIGHT_SENSOR_N	(71)
#define H5000_GPIO_BT_M_RESET   (72)
#define H5000_GPIO_STD_CHG_RATE (73)
#define H5000_GPIO_SD_WP_N      (74)
#define H5000_GPIO_MOTOR_ON_N   (75)       /* external pullup on this */
#define H5000_GPIO_HEADPHONE_DETECT	(76)
#define H5000_GPIO_USB_CHG_RATE (77)       /* select rate for charging via usb */
/*(78) is CS2# */
/*(79) is CS3# */
/*(80) is CS4# */

#endif /* __ASM_ARCH_H5000_H */
