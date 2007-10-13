/*
 * File:         arch/blackfin/kernel/bfin_gpio.c
 * Based on:
 * Author:       Michael Hennerich (hennerich@blackfin.uclinux.org)
 *
 * Created:
 * Description:  GPIO Abstraction Layer
 *
 * Modified:
 *               Copyright 2006 Analog Devices Inc.
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

/*
*  Number     BF537/6/4    BF561    BF533/2/1
*
*  GPIO_0       PF0         PF0        PF0
*  GPIO_1       PF1         PF1        PF1
*  GPIO_2       PF2         PF2        PF2
*  GPIO_3       PF3         PF3        PF3
*  GPIO_4       PF4         PF4        PF4
*  GPIO_5       PF5         PF5        PF5
*  GPIO_6       PF6         PF6        PF6
*  GPIO_7       PF7         PF7        PF7
*  GPIO_8       PF8         PF8        PF8
*  GPIO_9       PF9         PF9        PF9
*  GPIO_10      PF10        PF10       PF10
*  GPIO_11      PF11        PF11       PF11
*  GPIO_12      PF12        PF12       PF12
*  GPIO_13      PF13        PF13       PF13
*  GPIO_14      PF14        PF14       PF14
*  GPIO_15      PF15        PF15       PF15
*  GPIO_16      PG0         PF16
*  GPIO_17      PG1         PF17
*  GPIO_18      PG2         PF18
*  GPIO_19      PG3         PF19
*  GPIO_20      PG4         PF20
*  GPIO_21      PG5         PF21
*  GPIO_22      PG6         PF22
*  GPIO_23      PG7         PF23
*  GPIO_24      PG8         PF24
*  GPIO_25      PG9         PF25
*  GPIO_26      PG10        PF26
*  GPIO_27      PG11        PF27
*  GPIO_28      PG12        PF28
*  GPIO_29      PG13        PF29
*  GPIO_30      PG14        PF30
*  GPIO_31      PG15        PF31
*  GPIO_32      PH0         PF32
*  GPIO_33      PH1         PF33
*  GPIO_34      PH2         PF34
*  GPIO_35      PH3         PF35
*  GPIO_36      PH4         PF36
*  GPIO_37      PH5         PF37
*  GPIO_38      PH6         PF38
*  GPIO_39      PH7         PF39
*  GPIO_40      PH8         PF40
*  GPIO_41      PH9         PF41
*  GPIO_42      PH10        PF42
*  GPIO_43      PH11        PF43
*  GPIO_44      PH12        PF44
*  GPIO_45      PH13        PF45
*  GPIO_46      PH14        PF46
*  GPIO_47      PH15        PF47
*/

#include <linux/module.h>
#include <linux/err.h>
#include <asm/blackfin.h>
#include <asm/gpio.h>
#include <asm/portmux.h>
#include <linux/irq.h>

#ifdef BF533_FAMILY
static struct gpio_port_t *gpio_bankb[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(struct gpio_port_t *) FIO_FLAG_D,
};
#endif

#ifdef BF537_FAMILY
static struct gpio_port_t *gpio_bankb[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(struct gpio_port_t *) PORTFIO,
	(struct gpio_port_t *) PORTGIO,
	(struct gpio_port_t *) PORTHIO,
};

static unsigned short *port_fer[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(unsigned short *) PORTF_FER,
	(unsigned short *) PORTG_FER,
	(unsigned short *) PORTH_FER,
};

#endif

#ifdef BF561_FAMILY
static struct gpio_port_t *gpio_bankb[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(struct gpio_port_t *) FIO0_FLAG_D,
	(struct gpio_port_t *) FIO1_FLAG_D,
	(struct gpio_port_t *) FIO2_FLAG_D,
};
#endif

static unsigned short reserved_gpio_map[gpio_bank(MAX_BLACKFIN_GPIOS)];
static unsigned short reserved_peri_map[gpio_bank(MAX_BLACKFIN_GPIOS + 16)];
char *str_ident = NULL;

#define RESOURCE_LABEL_SIZE 16

#ifdef CONFIG_PM
static unsigned short wakeup_map[gpio_bank(MAX_BLACKFIN_GPIOS)];
static unsigned char wakeup_flags_map[MAX_BLACKFIN_GPIOS];
static struct gpio_port_s gpio_bank_saved[gpio_bank(MAX_BLACKFIN_GPIOS)];

#ifdef BF533_FAMILY
static unsigned int sic_iwr_irqs[gpio_bank(MAX_BLACKFIN_GPIOS)] = {IRQ_PROG_INTB};
#endif

#ifdef BF537_FAMILY
static unsigned int sic_iwr_irqs[gpio_bank(MAX_BLACKFIN_GPIOS)] = {IRQ_PROG_INTB, IRQ_PORTG_INTB, IRQ_MAC_TX};
#endif

#ifdef BF561_FAMILY
static unsigned int sic_iwr_irqs[gpio_bank(MAX_BLACKFIN_GPIOS)] = {IRQ_PROG0_INTB, IRQ_PROG1_INTB, IRQ_PROG2_INTB};
#endif

#endif /* CONFIG_PM */

inline int check_gpio(unsigned short gpio)
{
	if (gpio >= MAX_BLACKFIN_GPIOS)
		return -EINVAL;
	return 0;
}

static void set_label(unsigned short ident, const char *label)
{

	if (label && str_ident) {
		strncpy(str_ident + ident * RESOURCE_LABEL_SIZE, label,
			 RESOURCE_LABEL_SIZE);
		str_ident[ident * RESOURCE_LABEL_SIZE +
			 RESOURCE_LABEL_SIZE - 1] = 0;
	}
}

static char *get_label(unsigned short ident)
{
	if (!str_ident)
		return "UNKNOWN";

	return (str_ident[ident * RESOURCE_LABEL_SIZE] ?
		(str_ident + ident * RESOURCE_LABEL_SIZE) : "UNKNOWN");
}

static int cmp_label(unsigned short ident, const char *label)
{
	if (label && str_ident)
		return strncmp(str_ident + ident * RESOURCE_LABEL_SIZE,
				 label, strlen(label));
	else
		return -EINVAL;
}

#ifdef BF537_FAMILY
static void port_setup(unsigned short gpio, unsigned short usage)
{
	if (!check_gpio(gpio)) {
		if (usage == GPIO_USAGE) {
			*port_fer[gpio_bank(gpio)] &= ~gpio_bit(gpio);
		} else
			*port_fer[gpio_bank(gpio)] |= gpio_bit(gpio);
		SSYNC();
	}
}
#else
# define port_setup(...)  do { } while (0)
#endif

#ifdef BF537_FAMILY

#define PMUX_LUT_RES		0
#define PMUX_LUT_OFFSET		1
#define PMUX_LUT_ENTRIES	41
#define PMUX_LUT_SIZE		2

static unsigned short port_mux_lut[PMUX_LUT_ENTRIES][PMUX_LUT_SIZE] = {
	{P_PPI0_D13, 11}, {P_PPI0_D14, 11}, {P_PPI0_D15, 11},
	{P_SPORT1_TFS, 11}, {P_SPORT1_TSCLK, 11}, {P_SPORT1_DTPRI, 11},
	{P_PPI0_D10, 10}, {P_PPI0_D11, 10}, {P_PPI0_D12, 10},
	{P_SPORT1_RSCLK, 10}, {P_SPORT1_RFS, 10}, {P_SPORT1_DRPRI, 10},
	{P_PPI0_D8, 9}, {P_PPI0_D9, 9}, {P_SPORT1_DRSEC, 9},
	{P_SPORT1_DTSEC, 9}, {P_TMR2, 8}, {P_PPI0_FS3, 8}, {P_TMR3, 7},
	{P_SPI0_SSEL4, 7}, {P_TMR4, 6}, {P_SPI0_SSEL5, 6}, {P_TMR5, 5},
	{P_SPI0_SSEL6, 5}, {P_UART1_RX, 4}, {P_UART1_TX, 4}, {P_TMR6, 4},
	{P_TMR7, 4}, {P_UART0_RX, 3}, {P_UART0_TX, 3}, {P_DMAR0, 3},
	{P_DMAR1, 3}, {P_SPORT0_DTSEC, 1}, {P_SPORT0_DRSEC, 1},
	{P_CAN0_RX, 1}, {P_CAN0_TX, 1}, {P_SPI0_SSEL7, 1},
	{P_SPORT0_TFS, 0}, {P_SPORT0_DTPRI, 0}, {P_SPI0_SSEL2, 0},
	{P_SPI0_SSEL3, 0}
};

static void portmux_setup(unsigned short per, unsigned short function)
{
	u16 y, muxreg, offset;

	for (y = 0; y < PMUX_LUT_ENTRIES; y++) {
		if (port_mux_lut[y][PMUX_LUT_RES] == per) {

			/* SET PORTMUX REG */

			offset = port_mux_lut[y][PMUX_LUT_OFFSET];
			muxreg = bfin_read_PORT_MUX();

			if (offset != 1) {
				muxreg &= ~(1 << offset);
			} else {
				muxreg &= ~(3 << 1);
			}

			muxreg |= (function << offset);
			bfin_write_PORT_MUX(muxreg);
		}
	}
}

#else
# define portmux_setup(...)  do { } while (0)
#endif

static void default_gpio(unsigned short gpio)
{
	unsigned short bank, bitmask;

	bank = gpio_bank(gpio);
	bitmask = gpio_bit(gpio);

	gpio_bankb[bank]->maska_clear = bitmask;
	gpio_bankb[bank]->maskb_clear = bitmask;
	SSYNC();
	gpio_bankb[bank]->inen &= ~bitmask;
	gpio_bankb[bank]->dir &= ~bitmask;
	gpio_bankb[bank]->polar &= ~bitmask;
	gpio_bankb[bank]->both &= ~bitmask;
	gpio_bankb[bank]->edge &= ~bitmask;
}

static int __init bfin_gpio_init(void)
{

	str_ident = kzalloc(RESOURCE_LABEL_SIZE * 256, GFP_KERNEL);
	if (!str_ident)
		return -ENOMEM;

	printk(KERN_INFO "Blackfin GPIO Controller\n");

	return 0;

}

arch_initcall(bfin_gpio_init);


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
void set_gpio_ ## name(unsigned short gpio, unsigned short arg) \
{ \
	unsigned long flags; \
	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))); \
	local_irq_save(flags); \
	if (arg) \
		gpio_bankb[gpio_bank(gpio)]->name |= gpio_bit(gpio); \
	else \
		gpio_bankb[gpio_bank(gpio)]->name &= ~gpio_bit(gpio); \
	local_irq_restore(flags); \
} \
EXPORT_SYMBOL(set_gpio_ ## name);

SET_GPIO(dir)
SET_GPIO(inen)
SET_GPIO(polar)
SET_GPIO(edge)
SET_GPIO(both)


#define SET_GPIO_SC(name) \
void set_gpio_ ## name(unsigned short gpio, unsigned short arg) \
{ \
	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))); \
	if (arg) \
		gpio_bankb[gpio_bank(gpio)]->name ## _set = gpio_bit(gpio); \
	else \
		gpio_bankb[gpio_bank(gpio)]->name ## _clear = gpio_bit(gpio); \
} \
EXPORT_SYMBOL(set_gpio_ ## name);

SET_GPIO_SC(maska)
SET_GPIO_SC(maskb)

#if defined(ANOMALY_05000311)
void set_gpio_data(unsigned short gpio, unsigned short arg)
{
	unsigned long flags;
	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)));
	local_irq_save(flags);
	if (arg)
		gpio_bankb[gpio_bank(gpio)]->data_set = gpio_bit(gpio);
	else
		gpio_bankb[gpio_bank(gpio)]->data_clear = gpio_bit(gpio);
	bfin_read_CHIPID();
	local_irq_restore(flags);
}
EXPORT_SYMBOL(set_gpio_data);
#else
SET_GPIO_SC(data)
#endif


#if defined(ANOMALY_05000311)
void set_gpio_toggle(unsigned short gpio)
{
	unsigned long flags;
	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)));
	local_irq_save(flags);
	gpio_bankb[gpio_bank(gpio)]->toggle = gpio_bit(gpio);
	bfin_read_CHIPID();
	local_irq_restore(flags);
}
#else
void set_gpio_toggle(unsigned short gpio)
{
	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)));
	gpio_bankb[gpio_bank(gpio)]->toggle = gpio_bit(gpio);
}
#endif
EXPORT_SYMBOL(set_gpio_toggle);


/*Set current PORT date (16-bit word)*/

#define SET_GPIO_P(name) \
void set_gpiop_ ## name(unsigned short gpio, unsigned short arg) \
{ \
	gpio_bankb[gpio_bank(gpio)]->name = arg; \
} \
EXPORT_SYMBOL(set_gpiop_ ## name);

SET_GPIO_P(dir)
SET_GPIO_P(inen)
SET_GPIO_P(polar)
SET_GPIO_P(edge)
SET_GPIO_P(both)
SET_GPIO_P(maska)
SET_GPIO_P(maskb)


#if defined(ANOMALY_05000311)
void set_gpiop_data(unsigned short gpio, unsigned short arg)
{
	unsigned long flags;
	local_irq_save(flags);
	gpio_bankb[gpio_bank(gpio)]->data = arg;
	bfin_read_CHIPID();
	local_irq_restore(flags);
}
EXPORT_SYMBOL(set_gpiop_data);
#else
SET_GPIO_P(data)
#endif



/* Get a specific bit */

#define GET_GPIO(name) \
unsigned short get_gpio_ ## name(unsigned short gpio) \
{ \
	return (0x01 & (gpio_bankb[gpio_bank(gpio)]->name >> gpio_sub_n(gpio))); \
} \
EXPORT_SYMBOL(get_gpio_ ## name);

GET_GPIO(dir)
GET_GPIO(inen)
GET_GPIO(polar)
GET_GPIO(edge)
GET_GPIO(both)
GET_GPIO(maska)
GET_GPIO(maskb)


#if defined(ANOMALY_05000311)
unsigned short get_gpio_data(unsigned short gpio)
{
	unsigned long flags;
	unsigned short ret;
	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)));
	local_irq_save(flags);
	ret = 0x01 & (gpio_bankb[gpio_bank(gpio)]->data >> gpio_sub_n(gpio));
	bfin_read_CHIPID();
	local_irq_restore(flags);
	return ret;
}
EXPORT_SYMBOL(get_gpio_data);
#else
GET_GPIO(data)
#endif

/*Get current PORT date (16-bit word)*/

#define GET_GPIO_P(name) \
unsigned short get_gpiop_ ## name(unsigned short gpio) \
{ \
	return (gpio_bankb[gpio_bank(gpio)]->name);\
} \
EXPORT_SYMBOL(get_gpiop_ ## name);

GET_GPIO_P(dir)
GET_GPIO_P(inen)
GET_GPIO_P(polar)
GET_GPIO_P(edge)
GET_GPIO_P(both)
GET_GPIO_P(maska)
GET_GPIO_P(maskb)

#if defined(ANOMALY_05000311)
unsigned short get_gpiop_data(unsigned short gpio)
{
	unsigned long flags;
	unsigned short ret;
	local_irq_save(flags);
	ret = gpio_bankb[gpio_bank(gpio)]->data;
	bfin_read_CHIPID();
	local_irq_restore(flags);
	return ret;
}
EXPORT_SYMBOL(get_gpiop_data);
#else
GET_GPIO_P(data)
#endif

#ifdef CONFIG_PM
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
int gpio_pm_wakeup_request(unsigned short gpio, unsigned char type)
{
	unsigned long flags;

	if ((check_gpio(gpio) < 0) || !type)
		return -EINVAL;

	local_irq_save(flags);

	wakeup_map[gpio_bank(gpio)] |= gpio_bit(gpio);
	wakeup_flags_map[gpio] = type;
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_pm_wakeup_request);

void gpio_pm_wakeup_free(unsigned short gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	local_irq_save(flags);

	wakeup_map[gpio_bank(gpio)] &= ~gpio_bit(gpio);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_pm_wakeup_free);

static int bfin_gpio_wakeup_type(unsigned short gpio, unsigned char type)
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

u32 gpio_pm_setup(void)
{
	u32 sic_iwr = 0;
	u16 bank, mask, i, gpio;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		mask = wakeup_map[gpio_bank(i)];
		bank = gpio_bank(i);

		gpio_bank_saved[bank].maskb = gpio_bankb[bank]->maskb;
		gpio_bankb[bank]->maskb = 0;

		if (mask) {
#ifdef BF537_FAMILY
			gpio_bank_saved[bank].fer   = *port_fer[bank];
#endif
			gpio_bank_saved[bank].inen  = gpio_bankb[bank]->inen;
			gpio_bank_saved[bank].polar = gpio_bankb[bank]->polar;
			gpio_bank_saved[bank].dir   = gpio_bankb[bank]->dir;
			gpio_bank_saved[bank].edge  = gpio_bankb[bank]->edge;
			gpio_bank_saved[bank].both  = gpio_bankb[bank]->both;
			gpio_bank_saved[bank].reserved =
						reserved_gpio_map[bank];

			gpio = i;

			while (mask) {
				if (mask & 1) {
					reserved_gpio_map[gpio_bank(gpio)] |=
							gpio_bit(gpio);
					bfin_gpio_wakeup_type(gpio,
						wakeup_flags_map[gpio]);
					set_gpio_data(gpio, 0); /*Clear*/
				}
				gpio++;
				mask >>= 1;
			}

			sic_iwr |= 1 <<
				(sic_iwr_irqs[bank] - (IRQ_CORETMR + 1));
			gpio_bankb[bank]->maskb_set = wakeup_map[gpio_bank(i)];
		}
	}

	if (sic_iwr)
		return sic_iwr;
	else
		return IWR_ENABLE_ALL;
}

void gpio_pm_restore(void)
{
	u16 bank, mask, i;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		mask = wakeup_map[gpio_bank(i)];
		bank = gpio_bank(i);

		if (mask) {
#ifdef BF537_FAMILY
			*port_fer[bank]   	= gpio_bank_saved[bank].fer;
#endif
			gpio_bankb[bank]->inen  = gpio_bank_saved[bank].inen;
			gpio_bankb[bank]->dir   = gpio_bank_saved[bank].dir;
			gpio_bankb[bank]->polar = gpio_bank_saved[bank].polar;
			gpio_bankb[bank]->edge  = gpio_bank_saved[bank].edge;
			gpio_bankb[bank]->both  = gpio_bank_saved[bank].both;

			reserved_gpio_map[bank] =
					gpio_bank_saved[bank].reserved;

		}

		gpio_bankb[bank]->maskb = gpio_bank_saved[bank].maskb;
	}
}

#endif




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

	local_irq_save(flags);

	if (!check_gpio(ident)) {

	if (unlikely(reserved_gpio_map[gpio_bank(ident)] & gpio_bit(ident))) {
		printk(KERN_ERR
		       "%s: Peripheral %d is already reserved as GPIO by %s !\n",
		       __FUNCTION__, ident, get_label(ident));
		dump_stack();
		local_irq_restore(flags);
		return -EBUSY;
	}

	}

	if (unlikely(reserved_peri_map[gpio_bank(ident)] & gpio_bit(ident))) {

	/*
	 * Pin functions like AMC address strobes my
	 * be requested and used by several drivers
	 */

	if (!(per & P_MAYSHARE)) {

	/*
	 * Allow that the identical pin function can
	 * be requested from the same driver twice
	 */

		if (cmp_label(ident, label) == 0)
			goto anyway;

			printk(KERN_ERR
			       "%s: Peripheral %d function %d is already"
			       "reserved by %s !\n",
			       __FUNCTION__, ident, P_FUNCT2MUX(per),
				get_label(ident));
			dump_stack();
			local_irq_restore(flags);
			return -EBUSY;
		}

	}

anyway:


	portmux_setup(per, P_FUNCT2MUX(per));

	port_setup(ident, PERIPHERAL_USAGE);

	reserved_peri_map[gpio_bank(ident)] |= gpio_bit(ident);
	local_irq_restore(flags);
	set_label(ident, label);

	return 0;
}
EXPORT_SYMBOL(peripheral_request);

int peripheral_request_list(unsigned short per[], const char *label)
{
	u16 cnt;
	int ret;

	for (cnt = 0; per[cnt] != 0; cnt++) {
		ret = peripheral_request(per[cnt], label);
		if (ret < 0)
			return ret;
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

	if (check_gpio(ident) < 0)
		return;

	local_irq_save(flags);

	if (unlikely(!(reserved_peri_map[gpio_bank(ident)]
			 & gpio_bit(ident)))) {
		local_irq_restore(flags);
		return;
	}

	if (!(per & P_MAYSHARE)) {
		port_setup(ident, GPIO_USAGE);
	}

	reserved_peri_map[gpio_bank(ident)] &= ~gpio_bit(ident);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(peripheral_free);

void peripheral_free_list(unsigned short per[])
{
	u16 cnt;

	for (cnt = 0; per[cnt] != 0; cnt++) {
		peripheral_free(per[cnt]);
	}

}
EXPORT_SYMBOL(peripheral_free_list);

/***********************************************************
*
* FUNCTIONS: Blackfin GPIO Driver
*
* INPUTS/OUTPUTS:
* gpio - GPIO Number between 0 and MAX_BLACKFIN_GPIOS
*
*
* DESCRIPTION: Blackfin GPIO Driver API
*
* CAUTION:
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

int gpio_request(unsigned short gpio, const char *label)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	local_irq_save(flags);

	if (unlikely(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		printk(KERN_ERR "bfin-gpio: GPIO %d is already reserved!\n", gpio);
		dump_stack();
		local_irq_restore(flags);
		return -EBUSY;
	}
	reserved_gpio_map[gpio_bank(gpio)] |= gpio_bit(gpio);

	local_irq_restore(flags);

	port_setup(gpio, GPIO_USAGE);

	return 0;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned short gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	local_irq_save(flags);

	if (unlikely(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)))) {
		printk(KERN_ERR "bfin-gpio: GPIO %d wasn't reserved!\n", gpio);
		dump_stack();
		local_irq_restore(flags);
		return;
	}

	default_gpio(gpio);

	reserved_gpio_map[gpio_bank(gpio)] &= ~gpio_bit(gpio);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_free);

void gpio_direction_input(unsigned short gpio)
{
	unsigned long flags;

	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)));

	local_irq_save(flags);
	gpio_bankb[gpio_bank(gpio)]->dir &= ~gpio_bit(gpio);
	gpio_bankb[gpio_bank(gpio)]->inen |= gpio_bit(gpio);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_direction_input);

void gpio_direction_output(unsigned short gpio)
{
	unsigned long flags;

	BUG_ON(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)));

	local_irq_save(flags);
	gpio_bankb[gpio_bank(gpio)]->inen &= ~gpio_bit(gpio);
	gpio_bankb[gpio_bank(gpio)]->dir |= gpio_bit(gpio);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_direction_output);
