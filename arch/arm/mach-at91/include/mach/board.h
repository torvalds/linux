/*
 * arch/arm/mach-at91/include/mach/board.h
 *
 *  Copyright (C) 2005 HP Labs
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
 * These are data structures found in platform_device.dev.platform_data,
 * and describing board-specific data needed by drivers.  For example,
 * which pin is used for a given GPIO role.
 *
 * In 2.6, drivers should strongly avoid board-specific knowledge so
 * that supporting new boards normally won't require driver patches.
 * Most board-specific knowledge should be in arch/.../board-*.c files.
 */

#ifndef __ASM_ARCH_BOARD_H
#define __ASM_ARCH_BOARD_H

#include <linux/mtd/partitions.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/spi/spi.h>
#include <linux/usb/atmel_usba_udc.h>
#include <linux/atmel-mci.h>
#include <sound/atmel-ac97c.h>
#include <linux/serial.h>

 /* USB Device */
struct at91_udc_data {
	u8	vbus_pin;		/* high == host powering us */
	u8	vbus_active_low;	/* vbus polarity */
	u8	vbus_polled;		/* Use polling, not interrupt */
	u8	pullup_pin;		/* active == D+ pulled up */
	u8	pullup_active_low;	/* true == pullup_pin is active low */
};
extern void __init at91_add_device_udc(struct at91_udc_data *data);

 /* USB High Speed Device */
extern void __init at91_add_device_usba(struct usba_platform_data *data);

 /* Compact Flash */
struct at91_cf_data {
	u8	irq_pin;		/* I/O IRQ */
	u8	det_pin;		/* Card detect */
	u8	vcc_pin;		/* power switching */
	u8	rst_pin;		/* card reset */
	u8	chipselect;		/* EBI Chip Select number */
	u8	flags;
#define AT91_CF_TRUE_IDE	0x01
#define AT91_IDE_SWAP_A0_A2	0x02
};
extern void __init at91_add_device_cf(struct at91_cf_data *data);

 /* MMC / SD */
  /* at91_mci platform config */
struct at91_mmc_data {
	u8		det_pin;	/* card detect IRQ */
	unsigned	slot_b:1;	/* uses Slot B */
	unsigned	wire4:1;	/* (SD) supports DAT0..DAT3 */
	u8		wp_pin;		/* (SD) writeprotect detect */
	u8		vcc_pin;	/* power switching (high == on) */
};
extern void __init at91_add_device_mmc(short mmc_id, struct at91_mmc_data *data);

  /* atmel-mci platform config */
extern void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data);

 /* Ethernet (EMAC & MACB) */
struct at91_eth_data {
	u32		phy_mask;
	u8		phy_irq_pin;	/* PHY IRQ */
	u8		is_rmii;	/* using RMII interface? */
};
extern void __init at91_add_device_eth(struct at91_eth_data *data);

#if defined(CONFIG_ARCH_AT91SAM9260) || defined(CONFIG_ARCH_AT91SAM9263) || defined(CONFIG_ARCH_AT91SAM9G20) || defined(CONFIG_ARCH_AT91CAP9) \
	|| defined(CONFIG_ARCH_AT91SAM9G45)
#define eth_platform_data	at91_eth_data
#endif

 /* USB Host */
struct at91_usbh_data {
	u8		ports;		/* number of ports on root hub */
	u8		vbus_pin[2];	/* port power-control pin */
	u8              vbus_pin_inverted;
	u8              overcurrent_supported;
	u8              overcurrent_pin[2];
	u8              overcurrent_status[2];
	u8              overcurrent_changed[2];
};
extern void __init at91_add_device_usbh(struct at91_usbh_data *data);
extern void __init at91_add_device_usbh_ohci(struct at91_usbh_data *data);
extern void __init at91_add_device_usbh_ehci(struct at91_usbh_data *data);

 /* NAND / SmartMedia */
struct atmel_nand_data {
	u8		enable_pin;	/* chip enable */
	u8		det_pin;	/* card detect */
	u8		rdy_pin;	/* ready/busy */
	u8              rdy_pin_active_low;     /* rdy_pin value is inverted */
	u8		ale;		/* address line number connected to ALE */
	u8		cle;		/* address line number connected to CLE */
	u8		bus_width_16;	/* buswidth is 16 bit */
	struct mtd_partition *parts;
	unsigned int	num_parts;
};
extern void __init at91_add_device_nand(struct atmel_nand_data *data);

 /* I2C*/
#if defined(CONFIG_ARCH_AT91SAM9G45)
extern void __init at91_add_device_i2c(short i2c_id, struct i2c_board_info *devices, int nr_devices);
#else
extern void __init at91_add_device_i2c(struct i2c_board_info *devices, int nr_devices);
#endif

 /* SPI */
extern void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices);

 /* Serial */
#define ATMEL_UART_CTS	0x01
#define ATMEL_UART_RTS	0x02
#define ATMEL_UART_DSR	0x04
#define ATMEL_UART_DTR	0x08
#define ATMEL_UART_DCD	0x10
#define ATMEL_UART_RI	0x20

extern void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins);
extern void __init at91_set_serial_console(unsigned portnr);

extern struct platform_device *atmel_default_console_device;

struct atmel_uart_data {
	int			num;		/* port num */
	short			use_dma_tx;	/* use transmit DMA? */
	short			use_dma_rx;	/* use receive DMA? */
	void __iomem		*regs;		/* virt. base address, if any */
	struct serial_rs485	rs485;		/* rs485 settings */
};
extern void __init at91_add_device_serial(void);

/*
 * PWM
 */
#define AT91_PWM0	0
#define AT91_PWM1	1
#define AT91_PWM2	2
#define AT91_PWM3	3

extern void __init at91_add_device_pwm(u32 mask);

/*
 * SSC -- accessed through ssc_request(id).  Drivers don't bind to SSC
 * platform devices.  Their SSC ID is part of their configuration data,
 * along with information about which SSC signals they should use.
 */
#define ATMEL_SSC_TK	0x01
#define ATMEL_SSC_TF	0x02
#define ATMEL_SSC_TD	0x04
#define ATMEL_SSC_TX	(ATMEL_SSC_TK | ATMEL_SSC_TF | ATMEL_SSC_TD)

#define ATMEL_SSC_RK	0x10
#define ATMEL_SSC_RF	0x20
#define ATMEL_SSC_RD	0x40
#define ATMEL_SSC_RX	(ATMEL_SSC_RK | ATMEL_SSC_RF | ATMEL_SSC_RD)

extern void __init at91_add_device_ssc(unsigned id, unsigned pins);

 /* LCD Controller */
struct atmel_lcdfb_info;
extern void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data);

 /* AC97 */
extern void __init at91_add_device_ac97(struct ac97c_platform_data *data);

 /* ISI */
extern void __init at91_add_device_isi(void);

 /* Touchscreen Controller */
struct at91_tsadcc_data {
	unsigned int    adc_clock;
	u8		pendet_debounce;
	u8		ts_sample_hold_time;
};
extern void __init at91_add_device_tsadcc(struct at91_tsadcc_data *data);

/* CAN */
struct at91_can_data {
	void (*transceiver_switch)(int on);
};
extern void __init at91_add_device_can(struct at91_can_data *data);

 /* LEDs */
extern void __init at91_init_leds(u8 cpu_led, u8 timer_led);
extern void __init at91_gpio_leds(struct gpio_led *leds, int nr);
extern void __init at91_pwm_leds(struct gpio_led *leds, int nr);

/* FIXME: this needs a better location, but gets stuff building again */
extern int at91_suspend_entering_slow_clock(void);

#endif
