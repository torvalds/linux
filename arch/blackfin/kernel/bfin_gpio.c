/*
 * File:         arch/blackfin/kernel/bfin_gpio.c
 * Based on:
 * Author:       Michael Hennerich (hennerich@blackfin.uclinux.org)
 *
 * Created:
 * Description:  GPIO Abstraction Layer
 *
 * Modified:
 *               Copyright 2008 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <asm/blackfin.h>
#include <asm/gpio.h>
#include <asm/portmux.h>
#include <linux/irq.h>

#if ANOMALY_05000311 || ANOMALY_05000323
enum {
	AWA_data = SYSCR,
	AWA_data_clear = SYSCR,
	AWA_data_set = SYSCR,
	AWA_toggle = SYSCR,
	AWA_maska = BFIN_UART_SCR,
	AWA_maska_clear = BFIN_UART_SCR,
	AWA_maska_set = BFIN_UART_SCR,
	AWA_maska_toggle = BFIN_UART_SCR,
	AWA_maskb = BFIN_UART_GCTL,
	AWA_maskb_clear = BFIN_UART_GCTL,
	AWA_maskb_set = BFIN_UART_GCTL,
	AWA_maskb_toggle = BFIN_UART_GCTL,
	AWA_dir = SPORT1_STAT,
	AWA_polar = SPORT1_STAT,
	AWA_edge = SPORT1_STAT,
	AWA_both = SPORT1_STAT,
#if ANOMALY_05000311
	AWA_inen = TIMER_ENABLE,
#elif ANOMALY_05000323
	AWA_inen = DMA1_1_CONFIG,
#endif
};
	/* Anomaly Workaround */
#define AWA_DUMMY_READ(name) bfin_read16(AWA_ ## name)
#else
#define AWA_DUMMY_READ(...)  do { } while (0)
#endif

static struct gpio_port_t * const gpio_array[] = {
#if defined(BF533_FAMILY) || defined(BF538_FAMILY)
	(struct gpio_port_t *) FIO_FLAG_D,
#elif defined(BF527_FAMILY) || defined(BF537_FAMILY) || defined(BF518_FAMILY)
	(struct gpio_port_t *) PORTFIO,
	(struct gpio_port_t *) PORTGIO,
	(struct gpio_port_t *) PORTHIO,
#elif defined(BF561_FAMILY)
	(struct gpio_port_t *) FIO0_FLAG_D,
	(struct gpio_port_t *) FIO1_FLAG_D,
	(struct gpio_port_t *) FIO2_FLAG_D,
#elif defined(BF548_FAMILY)
	(struct gpio_port_t *)PORTA_FER,
	(struct gpio_port_t *)PORTB_FER,
	(struct gpio_port_t *)PORTC_FER,
	(struct gpio_port_t *)PORTD_FER,
	(struct gpio_port_t *)PORTE_FER,
	(struct gpio_port_t *)PORTF_FER,
	(struct gpio_port_t *)PORTG_FER,
	(struct gpio_port_t *)PORTH_FER,
	(struct gpio_port_t *)PORTI_FER,
	(struct gpio_port_t *)PORTJ_FER,
#else
# error no gpio arrays defined
#endif
};

#if defined(BF527_FAMILY) || defined(BF537_FAMILY) || defined(BF518_FAMILY)
static unsigned short * const port_fer[] = {
	(unsigned short *) PORTF_FER,
	(unsigned short *) PORTG_FER,
	(unsigned short *) PORTH_FER,
};

# if !defined(BF537_FAMILY)
static unsigned short * const port_mux[] = {
	(unsigned short *) PORTF_MUX,
	(unsigned short *) PORTG_MUX,
	(unsigned short *) PORTH_MUX,
};

static const
u8 pmux_offset[][16] = {
#  if defined(BF527_FAMILY)
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 4, 6, 8, 8, 10, 10 }, /* PORTF */
	{ 0, 0, 0, 0, 0, 2, 2, 4, 4, 6, 8, 10, 10, 10, 12, 12 }, /* PORTG */
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 4, 4, 4, 4, 4 }, /* PORTH */
#  elif defined(BF518_FAMILY)
	{ 0, 2, 2, 2, 2, 2, 2, 4, 6, 6, 6, 8, 8, 8, 8, 10 }, /* PORTF */
	{ 0, 0, 0, 2, 4, 6, 6, 6, 8, 10, 10, 12, 14, 14, 14, 14 }, /* PORTG */
	{ 0, 0, 0, 0, 2, 2, 4, 6, 10, 10, 10, 10, 10, 10, 10, 10 }, /* PORTH */
#  endif
};
# endif

#endif

static unsigned short reserved_gpio_map[GPIO_BANK_NUM];
static unsigned short reserved_peri_map[gpio_bank(MAX_RESOURCES)];
static unsigned short reserved_gpio_irq_map[GPIO_BANK_NUM];

#define RESOURCE_LABEL_SIZE 	16

static struct str_ident {
	char name[RESOURCE_LABEL_SIZE];
} str_ident[MAX_RESOURCES];

#if defined(CONFIG_PM)
static struct gpio_port_s gpio_bank_saved[GPIO_BANK_NUM];
#endif

inline int check_gpio(unsigned gpio)
{
#if defined(BF548_FAMILY)
	if (gpio == GPIO_PB15 || gpio == GPIO_PC14 || gpio == GPIO_PC15
	    || gpio == GPIO_PH14 || gpio == GPIO_PH15
	    || gpio == GPIO_PJ14 || gpio == GPIO_PJ15)
		return -EINVAL;
#endif
	if (gpio >= MAX_BLACKFIN_GPIOS)
		return -EINVAL;
	return 0;
}

static void gpio_error(unsigned gpio)
{
	printk(KERN_ERR "bfin-gpio: GPIO %d wasn't requested!\n", gpio);
}

static void set_label(unsigned short ident, const char *label)
{
	if (label) {
		strncpy(str_ident[ident].name, label,
			 RESOURCE_LABEL_SIZE);
		str_ident[ident].name[RESOURCE_LABEL_SIZE - 1] = 0;
	}
}

static char *get_label(unsigned short ident)
{
	return (*str_ident[ident].name ? str_ident[ident].name : "UNKNOWN");
}

static int cmp_label(unsigned short ident, const char *label)
{
	if (label == NULL) {
		dump_stack();
		printk(KERN_ERR "Please provide none-null label\n");
	}

	if (label)
		return strcmp(str_ident[ident].name, label);
	else
		return -EINVAL;
}

static void port_setup(unsigned gpio, unsigned short usage)
{
	if (check_gpio(gpio))
		return;

#if defined(BF527_FAMILY) || defined(BF537_FAMILY) || defined(BF518_FAMILY)
	if (usage == GPIO_USAGE)
		*port_fer[gpio_bank(gpio)] &= ~gpio_bit(gpio);
	else
		*port_fer[gpio_bank(gpio)] |= gpio_bit(gpio);
	SSYNC();
#elif defined(BF548_FAMILY)
	if (usage == GPIO_USAGE)
		gpio_array[gpio_bank(gpio)]->port_fer &= ~gpio_bit(gpio);
	else
		gpio_array[gpio_bank(gpio)]->port_fer |= gpio_bit(gpio);
	SSYNC();
#endif
}

#ifdef BF537_FAMILY
static struct {
	unsigned short res;
	unsigned short offset;
} port_mux_lut[] = {
	{.res = P_PPI0_D13, .offset = 11},
	{.res = P_PPI0_D14, .offset = 11},
	{.res = P_PPI0_D15, .offset = 11},
	{.res = P_SPORT1_TFS, .offset = 11},
	{.res = P_SPORT1_TSCLK, .offset = 11},
	{.res = P_SPORT1_DTPRI, .offset = 11},
	{.res = P_PPI0_D10, .offset = 10},
	{.res = P_PPI0_D11, .offset = 10},
	{.res = P_PPI0_D12, .offset = 10},
	{.res = P_SPORT1_RSCLK, .offset = 10},
	{.res = P_SPORT1_RFS, .offset = 10},
	{.res = P_SPORT1_DRPRI, .offset = 10},
	{.res = P_PPI0_D8, .offset = 9},
	{.res = P_PPI0_D9, .offset = 9},
	{.res = P_SPORT1_DRSEC, .offset = 9},
	{.res = P_SPORT1_DTSEC, .offset = 9},
	{.res = P_TMR2, .offset = 8},
	{.res = P_PPI0_FS3, .offset = 8},
	{.res = P_TMR3, .offset = 7},
	{.res = P_SPI0_SSEL4, .offset = 7},
	{.res = P_TMR4, .offset = 6},
	{.res = P_SPI0_SSEL5, .offset = 6},
	{.res = P_TMR5, .offset = 5},
	{.res = P_SPI0_SSEL6, .offset = 5},
	{.res = P_UART1_RX, .offset = 4},
	{.res = P_UART1_TX, .offset = 4},
	{.res = P_TMR6, .offset = 4},
	{.res = P_TMR7, .offset = 4},
	{.res = P_UART0_RX, .offset = 3},
	{.res = P_UART0_TX, .offset = 3},
	{.res = P_DMAR0, .offset = 3},
	{.res = P_DMAR1, .offset = 3},
	{.res = P_SPORT0_DTSEC, .offset = 1},
	{.res = P_SPORT0_DRSEC, .offset = 1},
	{.res = P_CAN0_RX, .offset = 1},
	{.res = P_CAN0_TX, .offset = 1},
	{.res = P_SPI0_SSEL7, .offset = 1},
	{.res = P_SPORT0_TFS, .offset = 0},
	{.res = P_SPORT0_DTPRI, .offset = 0},
	{.res = P_SPI0_SSEL2, .offset = 0},
	{.res = P_SPI0_SSEL3, .offset = 0},
};

static void portmux_setup(unsigned short per)
{
	u16 y, offset, muxreg;
	u16 function = P_FUNCT2MUX(per);

	for (y = 0; y < ARRAY_SIZE(port_mux_lut); y++) {
		if (port_mux_lut[y].res == per) {

			/* SET PORTMUX REG */

			offset = port_mux_lut[y].offset;
			muxreg = bfin_read_PORT_MUX();

			if (offset != 1)
				muxreg &= ~(1 << offset);
			else
				muxreg &= ~(3 << 1);

			muxreg |= (function << offset);
			bfin_write_PORT_MUX(muxreg);
		}
	}
}
#elif defined(BF548_FAMILY)
inline void portmux_setup(unsigned short per)
{
	u32 pmux;
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);

	pmux = gpio_array[gpio_bank(ident)]->port_mux;

	pmux &= ~(0x3 << (2 * gpio_sub_n(ident)));
	pmux |= (function & 0x3) << (2 * gpio_sub_n(ident));

	gpio_array[gpio_bank(ident)]->port_mux = pmux;
}

inline u16 get_portmux(unsigned short per)
{
	u32 pmux;
	u16 ident = P_IDENT(per);

	pmux = gpio_array[gpio_bank(ident)]->port_mux;

	return (pmux >> (2 * gpio_sub_n(ident)) & 0x3);
}
#elif defined(BF527_FAMILY) || defined(BF518_FAMILY)
inline void portmux_setup(unsigned short per)
{
	u16 pmux, ident = P_IDENT(per), function = P_FUNCT2MUX(per);
	u8 offset = pmux_offset[gpio_bank(ident)][gpio_sub_n(ident)];

	pmux = *port_mux[gpio_bank(ident)];
	pmux &= ~(3 << offset);
	pmux |= (function & 3) << offset;
	*port_mux[gpio_bank(ident)] = pmux;
	SSYNC();
}
#else
# define portmux_setup(...)  do { } while (0)
#endif

static int __init bfin_gpio_init(void)
{
	printk(KERN_INFO "Blackfin GPIO Controller\n");

	return 0;
}
arch_initcall(bfin_gpio_init);


#ifndef BF548_FAMILY
/***********************************************************
*
* FUNCTIONS: Blackfin General Purpose Ports Access Functions
*
* INPUTS/OUTPUTS:
* gpio - GPIO Number between 0 and MAX_BLACKFIN_GPIOS
*
*
* DESCRIPTION: These functions abstract direct register access
*              to Blackfin processor General Purpose
*              Ports Regsiters
*
* CAUTION: These functions do not belong to the GPIO Driver API
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

/* Set a specific bit */

#define SET_GPIO(name) \
void set_gpio_ ## name(unsigned gpio, unsigned short arg) \
{ \
	unsigned long flags; \
	local_irq_save_hw(flags); \
	if (arg) \
		gpio_array[gpio_bank(gpio)]->name |= gpio_bit(gpio); \
	else \
		gpio_array[gpio_bank(gpio)]->name &= ~gpio_bit(gpio); \
	AWA_DUMMY_READ(name); \
	local_irq_restore_hw(flags); \
} \
EXPORT_SYMBOL(set_gpio_ ## name);

SET_GPIO(dir)   /* set_gpio_dir() */
SET_GPIO(inen)  /* set_gpio_inen() */
SET_GPIO(polar) /* set_gpio_polar() */
SET_GPIO(edge)  /* set_gpio_edge() */
SET_GPIO(both)  /* set_gpio_both() */


#define SET_GPIO_SC(name) \
void set_gpio_ ## name(unsigned gpio, unsigned short arg) \
{ \
	unsigned long flags; \
	if (ANOMALY_05000311 || ANOMALY_05000323) \
		local_irq_save_hw(flags); \
	if (arg) \
		gpio_array[gpio_bank(gpio)]->name ## _set = gpio_bit(gpio); \
	else \
		gpio_array[gpio_bank(gpio)]->name ## _clear = gpio_bit(gpio); \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		local_irq_restore_hw(flags); \
	} \
} \
EXPORT_SYMBOL(set_gpio_ ## name);

SET_GPIO_SC(maska)
SET_GPIO_SC(maskb)
SET_GPIO_SC(data)

void set_gpio_toggle(unsigned gpio)
{
	unsigned long flags;
	if (ANOMALY_05000311 || ANOMALY_05000323)
		local_irq_save_hw(flags);
	gpio_array[gpio_bank(gpio)]->toggle = gpio_bit(gpio);
	if (ANOMALY_05000311 || ANOMALY_05000323) {
		AWA_DUMMY_READ(toggle);
		local_irq_restore_hw(flags);
	}
}
EXPORT_SYMBOL(set_gpio_toggle);


/*Set current PORT date (16-bit word)*/

#define SET_GPIO_P(name) \
void set_gpiop_ ## name(unsigned gpio, unsigned short arg) \
{ \
	unsigned long flags; \
	if (ANOMALY_05000311 || ANOMALY_05000323) \
		local_irq_save_hw(flags); \
	gpio_array[gpio_bank(gpio)]->name = arg; \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		local_irq_restore_hw(flags); \
	} \
} \
EXPORT_SYMBOL(set_gpiop_ ## name);

SET_GPIO_P(data)
SET_GPIO_P(dir)
SET_GPIO_P(inen)
SET_GPIO_P(polar)
SET_GPIO_P(edge)
SET_GPIO_P(both)
SET_GPIO_P(maska)
SET_GPIO_P(maskb)

/* Get a specific bit */
#define GET_GPIO(name) \
unsigned short get_gpio_ ## name(unsigned gpio) \
{ \
	unsigned long flags; \
	unsigned short ret; \
	if (ANOMALY_05000311 || ANOMALY_05000323) \
		local_irq_save_hw(flags); \
	ret = 0x01 & (gpio_array[gpio_bank(gpio)]->name >> gpio_sub_n(gpio)); \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		local_irq_restore_hw(flags); \
	} \
	return ret; \
} \
EXPORT_SYMBOL(get_gpio_ ## name);

GET_GPIO(data)
GET_GPIO(dir)
GET_GPIO(inen)
GET_GPIO(polar)
GET_GPIO(edge)
GET_GPIO(both)
GET_GPIO(maska)
GET_GPIO(maskb)

/*Get current PORT date (16-bit word)*/

#define GET_GPIO_P(name) \
unsigned short get_gpiop_ ## name(unsigned gpio) \
{ \
	unsigned long flags; \
	unsigned short ret; \
	if (ANOMALY_05000311 || ANOMALY_05000323) \
		local_irq_save_hw(flags); \
	ret = (gpio_array[gpio_bank(gpio)]->name); \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		local_irq_restore_hw(flags); \
	} \
	return ret; \
} \
EXPORT_SYMBOL(get_gpiop_ ## name);

GET_GPIO_P(data)
GET_GPIO_P(dir)
GET_GPIO_P(inen)
GET_GPIO_P(polar)
GET_GPIO_P(edge)
GET_GPIO_P(both)
GET_GPIO_P(maska)
GET_GPIO_P(maskb)


#ifdef CONFIG_PM

static unsigned short wakeup_map[GPIO_BANK_NUM];
static unsigned char wakeup_flags_map[MAX_BLACKFIN_GPIOS];

static const unsigned int sic_iwr_irqs[] = {
#if defined(BF533_FAMILY)
	IRQ_PROG_INTB
#elif defined(BF537_FAMILY)
	IRQ_PROG_INTB, IRQ_PORTG_INTB, IRQ_MAC_TX
#elif defined(BF538_FAMILY)
	IRQ_PORTF_INTB
#elif defined(BF527_FAMILY) || defined(BF518_FAMILY)
	IRQ_PORTF_INTB, IRQ_PORTG_INTB, IRQ_PORTH_INTB
#elif defined(BF561_FAMILY)
	IRQ_PROG0_INTB, IRQ_PROG1_INTB, IRQ_PROG2_INTB
#else
# error no SIC_IWR defined
#endif
};

/***********************************************************
*
* FUNCTIONS: Blackfin PM Setup API
*
* INPUTS/OUTPUTS:
* gpio - GPIO Number between 0 and MAX_BLACKFIN_GPIOS
* type -
*	PM_WAKE_RISING
*	PM_WAKE_FALLING
*	PM_WAKE_HIGH
*	PM_WAKE_LOW
*	PM_WAKE_BOTH_EDGES
*
* DESCRIPTION: Blackfin PM Driver API
*
* CAUTION:
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/
int gpio_pm_wakeup_request(unsigned gpio, unsigned char type)
{
	unsigned long flags;

	if ((check_gpio(gpio) < 0) || !type)
		return -EINVAL;

	local_irq_save_hw(flags);
	wakeup_map[gpio_bank(gpio)] |= gpio_bit(gpio);
	wakeup_flags_map[gpio] = type;
	local_irq_restore_hw(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_pm_wakeup_request);

void gpio_pm_wakeup_free(unsigned gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	local_irq_save_hw(flags);

	wakeup_map[gpio_bank(gpio)] &= ~gpio_bit(gpio);

	local_irq_restore_hw(flags);
}
EXPORT_SYMBOL(gpio_pm_wakeup_free);

static int bfin_gpio_wakeup_type(unsigned gpio, unsigned char type)
{
	port_setup(gpio, GPIO_USAGE);
	set_gpio_dir(gpio, 0);
	set_gpio_inen(gpio, 1);

	if (type & (PM_WAKE_RISING | PM_WAKE_FALLING))
		set_gpio_edge(gpio, 1);
	 else
		set_gpio_edge(gpio, 0);

	if ((type & (PM_WAKE_BOTH_EDGES)) == (PM_WAKE_BOTH_EDGES))
		set_gpio_both(gpio, 1);
	else
		set_gpio_both(gpio, 0);

	if ((type & (PM_WAKE_FALLING | PM_WAKE_LOW)))
		set_gpio_polar(gpio, 1);
	else
		set_gpio_polar(gpio, 0);

	SSYNC();

	return 0;
}

u32 bfin_pm_standby_setup(void)
{
	u16 bank, mask, i, gpio;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		mask = wakeup_map[gpio_bank(i)];
		bank = gpio_bank(i);

		gpio_bank_saved[bank].maskb = gpio_array[bank]->maskb;
		gpio_array[bank]->maskb = 0;

		if (mask) {
#if defined(BF527_FAMILY) || defined(BF537_FAMILY) || defined(BF518_FAMILY)
			gpio_bank_saved[bank].fer   = *port_fer[bank];
#endif
			gpio_bank_saved[bank].inen  = gpio_array[bank]->inen;
			gpio_bank_saved[bank].polar = gpio_array[bank]->polar;
			gpio_bank_saved[bank].dir   = gpio_array[bank]->dir;
			gpio_bank_saved[bank].edge  = gpio_array[bank]->edge;
			gpio_bank_saved[bank].both  = gpio_array[bank]->both;
			gpio_bank_saved[bank].reserved =
						reserved_gpio_map[bank];

			gpio = i;

			while (mask) {
				if ((mask & 1) && (wakeup_flags_map[gpio] !=
					PM_WAKE_IGNORE)) {
					reserved_gpio_map[gpio_bank(gpio)] |=
							gpio_bit(gpio);
					bfin_gpio_wakeup_type(gpio,
						wakeup_flags_map[gpio]);
					set_gpio_data(gpio, 0); /*Clear*/
				}
				gpio++;
				mask >>= 1;
			}

			bfin_internal_set_wake(sic_iwr_irqs[bank], 1);
			gpio_array[bank]->maskb_set = wakeup_map[gpio_bank(i)];
		}
	}

	AWA_DUMMY_READ(maskb_set);

	return 0;
}

void bfin_pm_standby_restore(void)
{
	u16 bank, mask, i;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		mask = wakeup_map[gpio_bank(i)];
		bank = gpio_bank(i);

		if (mask) {
#if defined(BF527_FAMILY) || defined(BF537_FAMILY) || defined(BF518_FAMILY)
			*port_fer[bank]   	= gpio_bank_saved[bank].fer;
#endif
			gpio_array[bank]->inen  = gpio_bank_saved[bank].inen;
			gpio_array[bank]->dir   = gpio_bank_saved[bank].dir;
			gpio_array[bank]->polar = gpio_bank_saved[bank].polar;
			gpio_array[bank]->edge  = gpio_bank_saved[bank].edge;
			gpio_array[bank]->both  = gpio_bank_saved[bank].both;

			reserved_gpio_map[bank] =
					gpio_bank_saved[bank].reserved;
			bfin_internal_set_wake(sic_iwr_irqs[bank], 0);
		}

		gpio_array[bank]->maskb = gpio_bank_saved[bank].maskb;
	}
	AWA_DUMMY_READ(maskb);
}

void bfin_gpio_pm_hibernate_suspend(void)
{
	int i, bank;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

#if defined(BF527_FAMILY) || defined(BF537_FAMILY) || defined(BF518_FAMILY)
		gpio_bank_saved[bank].fer = *port_fer[bank];
#if defined(BF527_FAMILY) || defined(BF518_FAMILY)
		gpio_bank_saved[bank].mux = *port_mux[bank];
#else
		if (bank == 0)
			gpio_bank_saved[bank].mux = bfin_read_PORT_MUX();
#endif
#endif
		gpio_bank_saved[bank].data  = gpio_array[bank]->data;
		gpio_bank_saved[bank].inen  = gpio_array[bank]->inen;
		gpio_bank_saved[bank].polar = gpio_array[bank]->polar;
		gpio_bank_saved[bank].dir   = gpio_array[bank]->dir;
		gpio_bank_saved[bank].edge  = gpio_array[bank]->edge;
		gpio_bank_saved[bank].both  = gpio_array[bank]->both;
		gpio_bank_saved[bank].maska = gpio_array[bank]->maska;
	}

	AWA_DUMMY_READ(maska);
}

void bfin_gpio_pm_hibernate_restore(void)
{
	int i, bank;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

#if defined(BF527_FAMILY) || defined(BF537_FAMILY) || defined(BF518_FAMILY)
#if defined(BF527_FAMILY) || defined(BF518_FAMILY)
		*port_mux[bank] = gpio_bank_saved[bank].mux;
#else
		if (bank == 0)
			bfin_write_PORT_MUX(gpio_bank_saved[bank].mux);
#endif
		*port_fer[bank] = gpio_bank_saved[bank].fer;
#endif
		gpio_array[bank]->inen  = gpio_bank_saved[bank].inen;
		gpio_array[bank]->dir   = gpio_bank_saved[bank].dir;
		gpio_array[bank]->polar = gpio_bank_saved[bank].polar;
		gpio_array[bank]->edge  = gpio_bank_saved[bank].edge;
		gpio_array[bank]->both  = gpio_bank_saved[bank].both;

		gpio_array[bank]->data_set = gpio_bank_saved[bank].data
						| gpio_bank_saved[bank].dir;

		gpio_array[bank]->maska = gpio_bank_saved[bank].maska;
	}
	AWA_DUMMY_READ(maska);
}


#endif
#else /* BF548_FAMILY */
#ifdef CONFIG_PM

u32 bfin_pm_standby_setup(void)
{
	return 0;
}

void bfin_pm_standby_restore(void)
{

}

void bfin_gpio_pm_hibernate_suspend(void)
{
	int i, bank;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

		gpio_bank_saved[bank].fer = gpio_array[bank]->port_fer;
		gpio_bank_saved[bank].mux = gpio_array[bank]->port_mux;
		gpio_bank_saved[bank].data = gpio_array[bank]->data;
		gpio_bank_saved[bank].data = gpio_array[bank]->data;
		gpio_bank_saved[bank].inen = gpio_array[bank]->inen;
		gpio_bank_saved[bank].dir = gpio_array[bank]->dir_set;
	}
}

void bfin_gpio_pm_hibernate_restore(void)
{
	int i, bank;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

		gpio_array[bank]->port_mux = gpio_bank_saved[bank].mux;
		gpio_array[bank]->port_fer = gpio_bank_saved[bank].fer;
		gpio_array[bank]->inen = gpio_bank_saved[bank].inen;
		gpio_array[bank]->dir_set = gpio_bank_saved[bank].dir;
		gpio_array[bank]->data_set = gpio_bank_saved[bank].data
						| gpio_bank_saved[bank].dir;
	}
}
#endif

unsigned short get_gpio_dir(unsigned gpio)
{
	return (0x01 & (gpio_array[gpio_bank(gpio)]->dir_clear >> gpio_sub_n(gpio)));
}
EXPORT_SYMBOL(get_gpio_dir);

#endif /* BF548_FAMILY */

/***********************************************************
*
* FUNCTIONS: 	Blackfin Peripheral Resource Allocation
*		and PortMux Setup
*
* INPUTS/OUTPUTS:
* per	Peripheral Identifier
* label	String
*
* DESCRIPTION: Blackfin Peripheral Resource Allocation and Setup API
*
* CAUTION:
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

int peripheral_request(unsigned short per, const char *label)
{
	unsigned long flags;
	unsigned short ident = P_IDENT(per);

	/*
	 * Don't cares are pins with only one dedicated function
	 */

	if (per & P_DONTCARE)
		return 0;

	if (!(per & P_DEFINED))
		return -ENODEV;

	local_irq_save_hw(flags);

	/* If a pin can be muxed as either GPIO or peripheral, make
	 * sure it is not already a GPIO pin when we request it.
	 */
	if (unlikely(!check_gpio(ident) &&
	    reserved_gpio_map[gpio_bank(ident)] & gpio_bit(ident))) {
		dump_stack();
		printk(KERN_ERR
		       "%s: Peripheral %d is already reserved as GPIO by %s !\n",
		       __func__, ident, get_label(ident));
		local_irq_restore_hw(flags);
		return -EBUSY;
	}

	if (unlikely(reserved_peri_map[gpio_bank(ident)] & gpio_bit(ident))) {

		/*
		 * Pin functions like AMC address strobes my
		 * be requested and used by several drivers
		 */

#ifdef BF548_FAMILY
		if (!((per & P_MAYSHARE) && get_portmux(per) == P_FUNCT2MUX(per))) {
#else
		if (!(per & P_MAYSHARE)) {
#endif
			/*
			 * Allow that the identical pin function can
			 * be requested from the same driver twice
			 */

			if (cmp_label(ident, label) == 0)
				goto anyway;

			dump_stack();
			printk(KERN_ERR
			       "%s: Peripheral %d function %d is already reserved by %s !\n",
			       __func__, ident, P_FUNCT2MUX(per), get_label(ident));
			local_irq_restore_hw(flags);
			return -EBUSY;
		}
	}

 anyway:
	reserved_peri_map[gpio_bank(ident)] |= gpio_bit(ident);

	portmux_setup(per);
	port_setup(ident, PERIPHERAL_USAGE);

	local_irq_restore_hw(flags);
	set_label(ident, label);

	return 0;
}
EXPORT_SYMBOL(peripheral_request);

int peripheral_request_list(const unsigned short per[], const char *label)
{
	u16 cnt;
	int ret;

	for (cnt = 0; per[cnt] != 0; cnt++) {

		ret = peripheral_request(per[cnt], label);

		if (ret < 0) {
			for ( ; cnt > 0; cnt--)
				peripheral_free(per[cnt - 1]);

			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(peripheral_request_list);

void peripheral_free(unsigned short per)
{
	unsigned long flags;
	unsigned short ident = P_IDENT(per);

	if (per & P_DONTCARE)
		return;

	if (!(per & P_DEFINED))
		return;

	local_irq_save_hw(flags);

	if (unlikely(!(reserved_peri_map[gpio_bank(ident)] & gpio_bit(ident)))) {
		local_irq_restore_hw(flags);
		return;
	}

	if (!(per & P_MAYSHARE))
		port_setup(ident, GPIO_USAGE);

	reserved_peri_map[gpio_bank(ident)] &= ~gpio_bit(ident);

	set_label(ident, "free");

	local_irq_restore_hw(flags);
}
EXPORT_SYMBOL(peripheral_free);

void peripheral_free_list(const unsigned short per[])
{
	u16 cnt;
	for (cnt = 0; per[cnt] != 0; cnt++)
		peripheral_free(per[cnt]);
}
EXPORT_SYMBOL(peripheral_free_list);

/***********************************************************
*
* FUNCTIONS: Blackfin GPIO Driver
*
* INPUTS/OUTPUTS:
* gpio	PIO Number between 0 and MAX_BLACKFIN_GPIOS
* label	String
*
* DESCRIPTION: Blackfin GPIO Driver API
*
* CAUTION:
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

int bfin_gpio_request(unsigned gpio, const char *label)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	local_irq_save_hw(flags);

	/*
	 * Allow that the identical GPIO can
	 * be requested from the same driver twice
	 * Do nothing and return -
	 */

	if (cmp_label(gpio, label) == 0) {
		local_irq_restore_hw(flags);
		return 0;
	}

	if (unlikely(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		dump_stack();
		printk(KERN_ERR "bfin-gpio: GPIO %d is already reserved by %s !\n",
		       gpio, get_label(gpio));
		local_irq_restore_hw(flags);
		return -EBUSY;
	}
	if (unlikely(reserved_peri_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		dump_stack();
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));
		local_irq_restore_hw(flags);
		return -EBUSY;
	}
	if (unlikely(reserved_gpio_irq_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		printk(KERN_NOTICE "bfin-gpio: GPIO %d is already reserved as gpio-irq!"
		       " (Documentation/blackfin/bfin-gpio-notes.txt)\n", gpio);
	}
#ifndef BF548_FAMILY
	else {	/* Reset POLAR setting when acquiring a gpio for the first time */
		set_gpio_polar(gpio, 0);
	}
#endif

	reserved_gpio_map[gpio_bank(gpio)] |= gpio_bit(gpio);
	set_label(gpio, label);

	local_irq_restore_hw(flags);

	port_setup(gpio, GPIO_USAGE);

	return 0;
}
EXPORT_SYMBOL(bfin_gpio_request);

void bfin_gpio_free(unsigned gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	might_sleep();

	local_irq_save_hw(flags);

	if (unlikely(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)))) {
		dump_stack();
		gpio_error(gpio);
		local_irq_restore_hw(flags);
		return;
	}

	reserved_gpio_map[gpio_bank(gpio)] &= ~gpio_bit(gpio);

	set_label(gpio, "free");

	local_irq_restore_hw(flags);
}
EXPORT_SYMBOL(bfin_gpio_free);

int bfin_gpio_irq_request(unsigned gpio, const char *label)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	local_irq_save_hw(flags);

	if (unlikely(reserved_gpio_irq_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		dump_stack();
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as gpio-irq !\n",
		       gpio);
		local_irq_restore_hw(flags);
		return -EBUSY;
	}
	if (unlikely(reserved_peri_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		dump_stack();
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));
		local_irq_restore_hw(flags);
		return -EBUSY;
	}
	if (unlikely(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)))
		printk(KERN_NOTICE "bfin-gpio: GPIO %d is already reserved by %s! "
		       "(Documentation/blackfin/bfin-gpio-notes.txt)\n",
		       gpio, get_label(gpio));

	reserved_gpio_irq_map[gpio_bank(gpio)] |= gpio_bit(gpio);
	set_label(gpio, label);

	local_irq_restore_hw(flags);

	port_setup(gpio, GPIO_USAGE);

	return 0;
}

void bfin_gpio_irq_free(unsigned gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	local_irq_save_hw(flags);

	if (unlikely(!(reserved_gpio_irq_map[gpio_bank(gpio)] & gpio_bit(gpio)))) {
		dump_stack();
		gpio_error(gpio);
		local_irq_restore_hw(flags);
		return;
	}

	reserved_gpio_irq_map[gpio_bank(gpio)] &= ~gpio_bit(gpio);

	set_label(gpio, "free");

	local_irq_restore_hw(flags);
}

static inline void __bfin_gpio_direction_input(unsigned gpio)
{
#ifdef BF548_FAMILY
	gpio_array[gpio_bank(gpio)]->dir_clear = gpio_bit(gpio);
#else
	gpio_array[gpio_bank(gpio)]->dir &= ~gpio_bit(gpio);
#endif
	gpio_array[gpio_bank(gpio)]->inen |= gpio_bit(gpio);
}

int bfin_gpio_direction_input(unsigned gpio)
{
	unsigned long flags;

	if (!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	local_irq_save_hw(flags);
	__bfin_gpio_direction_input(gpio);
	AWA_DUMMY_READ(inen);
	local_irq_restore_hw(flags);

	return 0;
}
EXPORT_SYMBOL(bfin_gpio_direction_input);

void bfin_gpio_irq_prepare(unsigned gpio)
{
#ifdef BF548_FAMILY
	unsigned long flags;
#endif

	port_setup(gpio, GPIO_USAGE);

#ifdef BF548_FAMILY
	local_irq_save_hw(flags);
	__bfin_gpio_direction_input(gpio);
	local_irq_restore_hw(flags);
#endif
}

void bfin_gpio_set_value(unsigned gpio, int arg)
{
	if (arg)
		gpio_array[gpio_bank(gpio)]->data_set = gpio_bit(gpio);
	else
		gpio_array[gpio_bank(gpio)]->data_clear = gpio_bit(gpio);
}
EXPORT_SYMBOL(bfin_gpio_set_value);

int bfin_gpio_direction_output(unsigned gpio, int value)
{
	unsigned long flags;

	if (!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	local_irq_save_hw(flags);

	gpio_array[gpio_bank(gpio)]->inen &= ~gpio_bit(gpio);
	gpio_set_value(gpio, value);
#ifdef BF548_FAMILY
	gpio_array[gpio_bank(gpio)]->dir_set = gpio_bit(gpio);
#else
	gpio_array[gpio_bank(gpio)]->dir |= gpio_bit(gpio);
#endif

	AWA_DUMMY_READ(dir);
	local_irq_restore_hw(flags);

	return 0;
}
EXPORT_SYMBOL(bfin_gpio_direction_output);

int bfin_gpio_get_value(unsigned gpio)
{
#ifdef BF548_FAMILY
	return (1 & (gpio_array[gpio_bank(gpio)]->data >> gpio_sub_n(gpio)));
#else
	unsigned long flags;

	if (unlikely(get_gpio_edge(gpio))) {
		int ret;
		local_irq_save_hw(flags);
		set_gpio_edge(gpio, 0);
		ret = get_gpio_data(gpio);
		set_gpio_edge(gpio, 1);
		local_irq_restore_hw(flags);
		return ret;
	} else
		return get_gpio_data(gpio);
#endif
}
EXPORT_SYMBOL(bfin_gpio_get_value);

/* If we are booting from SPI and our board lacks a strong enough pull up,
 * the core can reset and execute the bootrom faster than the resistor can
 * pull the signal logically high.  To work around this (common) error in
 * board design, we explicitly set the pin back to GPIO mode, force /CS
 * high, and wait for the electrons to do their thing.
 *
 * This function only makes sense to be called from reset code, but it
 * lives here as we need to force all the GPIO states w/out going through
 * BUG() checks and such.
 */
void bfin_reset_boot_spi_cs(unsigned short pin)
{
	unsigned short gpio = P_IDENT(pin);
	port_setup(gpio, GPIO_USAGE);
	gpio_array[gpio_bank(gpio)]->data_set = gpio_bit(gpio);
	AWA_DUMMY_READ(data_set);
	udelay(1);
}

#if defined(CONFIG_PROC_FS)
static int gpio_proc_read(char *buf, char **start, off_t offset,
			  int len, int *unused_i, void *unused_v)
{
	int c, irq, gpio, outlen = 0;

	for (c = 0; c < MAX_RESOURCES; c++) {
		irq = reserved_gpio_irq_map[gpio_bank(c)] & gpio_bit(c);
		gpio = reserved_gpio_map[gpio_bank(c)] & gpio_bit(c);
		if (!check_gpio(c) && (gpio || irq))
			len = sprintf(buf, "GPIO_%d: \t%s%s \t\tGPIO %s\n", c,
				 get_label(c), (gpio && irq) ? " *" : "",
				 get_gpio_dir(c) ? "OUTPUT" : "INPUT");
		else if (reserved_peri_map[gpio_bank(c)] & gpio_bit(c))
			len = sprintf(buf, "GPIO_%d: \t%s \t\tPeripheral\n", c, get_label(c));
		else
			continue;
		buf += len;
		outlen += len;
	}
	return outlen;
}

static __init int gpio_register_proc(void)
{
	struct proc_dir_entry *proc_gpio;

	proc_gpio = create_proc_entry("gpio", S_IRUGO, NULL);
	if (proc_gpio)
		proc_gpio->read_proc = gpio_proc_read;
	return proc_gpio != NULL;
}
__initcall(gpio_register_proc);
#endif

#ifdef CONFIG_GPIOLIB
int bfin_gpiolib_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_direction_input(gpio);
}

int bfin_gpiolib_direction_output(struct gpio_chip *chip, unsigned gpio, int level)
{
	return bfin_gpio_direction_output(gpio, level);
}

int bfin_gpiolib_get_value(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_get_value(gpio);
}

void bfin_gpiolib_set_value(struct gpio_chip *chip, unsigned gpio, int value)
{
	return bfin_gpio_set_value(gpio, value);
}

int bfin_gpiolib_gpio_request(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_request(gpio, chip->label);
}

void bfin_gpiolib_gpio_free(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_free(gpio);
}

static struct gpio_chip bfin_chip = {
	.label			= "Blackfin-GPIOlib",
	.direction_input	= bfin_gpiolib_direction_input,
	.get			= bfin_gpiolib_get_value,
	.direction_output	= bfin_gpiolib_direction_output,
	.set			= bfin_gpiolib_set_value,
	.request		= bfin_gpiolib_gpio_request,
	.free			= bfin_gpiolib_gpio_free,
	.base			= 0,
	.ngpio			= MAX_BLACKFIN_GPIOS,
};

static int __init bfin_gpiolib_setup(void)
{
	return gpiochip_add(&bfin_chip);
}
arch_initcall(bfin_gpiolib_setup);
#endif
