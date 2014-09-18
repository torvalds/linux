/*
 * arch/arm/mach-pxa/include/mach/pcm027.h
 *
 * (c) 2003 Phytec Messtechnik GmbH <armlinux@phytec.de>
 * (c) 2007 Juergen Beisert <j.beisert@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Definitions of CPU card resources only
 */

#include "irqs.h" /* PXA_GPIO_TO_IRQ */

/* phyCORE-PXA270 (PCM027) Interrupts */
#define PCM027_IRQ(x)          (IRQ_BOARD_START + (x))
#define PCM027_BTDET_IRQ       PCM027_IRQ(0)
#define PCM027_FF_RI_IRQ       PCM027_IRQ(1)
#define PCM027_MMCDET_IRQ      PCM027_IRQ(2)
#define PCM027_PM_5V_IRQ       PCM027_IRQ(3)

#define PCM027_NR_IRQS		(IRQ_BOARD_START + 32)

/* I2C RTC */
#define PCM027_RTC_IRQ_GPIO	0
#define PCM027_RTC_IRQ		PXA_GPIO_TO_IRQ(PCM027_RTC_IRQ_GPIO)
#define PCM027_RTC_IRQ_EDGE	IRQ_TYPE_EDGE_FALLING
#define ADR_PCM027_RTC		0x51	/* I2C address */

/* I2C EEPROM */
#define ADR_PCM027_EEPROM	0x54	/* I2C address */

/* Ethernet chip (SMSC91C111) */
#define PCM027_ETH_IRQ_GPIO	52
#define PCM027_ETH_IRQ		PXA_GPIO_TO_IRQ(PCM027_ETH_IRQ_GPIO)
#define PCM027_ETH_IRQ_EDGE	IRQ_TYPE_EDGE_RISING
#define PCM027_ETH_PHYS		PXA_CS5_PHYS
#define PCM027_ETH_SIZE		(1*1024*1024)

/* CAN controller SJA1000 (unsupported yet) */
#define PCM027_CAN_IRQ_GPIO	114
#define PCM027_CAN_IRQ		PXA_GPIO_TO_IRQ(PCM027_CAN_IRQ_GPIO)
#define PCM027_CAN_IRQ_EDGE	IRQ_TYPE_EDGE_FALLING
#define PCM027_CAN_PHYS		0x22000000
#define PCM027_CAN_SIZE		0x100

/* SPI GPIO expander (unsupported yet) */
#define PCM027_EGPIO_IRQ_GPIO	27
#define PCM027_EGPIO_IRQ	PXA_GPIO_TO_IRQ(PCM027_EGPIO_IRQ_GPIO)
#define PCM027_EGPIO_IRQ_EDGE	IRQ_TYPE_EDGE_FALLING
#define PCM027_EGPIO_CS		24
/*
 * TODO: Switch this pin from dedicated usage to GPIO if
 * more than the MAX7301 device is connected to this SPI bus
 */
#define PCM027_EGPIO_CS_MODE	GPIO24_SFRM_MD

/* Flash memory */
#define PCM027_FLASH_PHYS	0x00000000
#define PCM027_FLASH_SIZE	0x02000000

/* onboard LEDs connected to GPIO */
#define PCM027_LED_CPU		90
#define PCM027_LED_HEARD_BEAT	91

/*
 * This CPU module needs a baseboard to work. After basic initializing
 * its own devices, it calls baseboard's init function.
 * TODO: Add your own basebaord init function and call it from
 * inside pcm027_init(). This example here is for the developmen board.
 * Refer pcm990-baseboard.c
 */
extern void pcm990_baseboard_init(void);
