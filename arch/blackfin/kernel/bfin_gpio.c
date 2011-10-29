/*
 * GPIO Abstraction Layer
 *
 * Copyright 2006-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/blackfin.h>
#include <asm/gpio.h>
#include <asm/portmux.h>
#include <linux/irq.h>
#include <asm/irq_handler.h>

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
#elif defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
	(struct gpio_port_t *) PORTFIO,
	(struct gpio_port_t *) PORTGIO,
	(struct gpio_port_t *) PORTHIO,
#elif defined(BF561_FAMILY)
	(struct gpio_port_t *) FIO0_FLAG_D,
	(struct gpio_port_t *) FIO1_FLAG_D,
	(struct gpio_port_t *) FIO2_FLAG_D,
#elif defined(CONFIG_BF54x)
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

#if defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
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
#  if defined(CONFIG_BF52x)
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 4, 6, 8, 8, 10, 10 }, /* PORTF */
	{ 0, 0, 0, 0, 0, 2, 2, 4, 4, 6, 8, 10, 10, 10, 12, 12 }, /* PORTG */
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 4, 4, 4, 4, 4 }, /* PORTH */
#  elif defined(CONFIG_BF51x)
	{ 0, 2, 2, 2, 2, 2, 2, 4, 6, 6, 6, 8, 8, 8, 8, 10 }, /* PORTF */
	{ 0, 0, 0, 2, 4, 6, 6, 6, 8, 10, 10, 12, 14, 14, 14, 14 }, /* PORTG */
	{ 0, 0, 0, 0, 2, 2, 4, 6, 10, 10, 10, 10, 10, 10, 10, 10 }, /* PORTH */
#  endif
};
# endif

#elif defined(BF538_FAMILY)
static unsigned short * const port_fer[] = {
	(unsigned short *) PORTCIO_FER,
	(unsigned short *) PORTDIO_FER,
	(unsigned short *) PORTEIO_FER,
};
#endif

#define RESOURCE_LABEL_SIZE	16

static struct str_ident {
	char name[RESOURCE_LABEL_SIZE];
} str_ident[MAX_RESOURCES];

#if defined(CONFIG_PM)
static struct gpio_port_s gpio_bank_saved[GPIO_BANK_NUM];
# ifdef BF538_FAMILY
static unsigned short port_fer_saved[3];
# endif
#endif

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

#define map_entry(m, i)      reserved_##m##_map[gpio_bank(i)]
#define is_reserved(m, i, e) (map_entry(m, i) & gpio_bit(i))
#define reserve(m, i)        (map_entry(m, i) |= gpio_bit(i))
#define unreserve(m, i)      (map_entry(m, i) &= ~gpio_bit(i))
#define DECLARE_RESERVED_MAP(m, c) static unsigned short reserved_##m##_map[c]

DECLARE_RESERVED_MAP(gpio, GPIO_BANK_NUM);
DECLARE_RESERVED_MAP(peri, DIV_ROUND_UP(MAX_RESOURCES, GPIO_BANKSIZE));
DECLARE_RESERVED_MAP(gpio_irq, GPIO_BANK_NUM);

inline int check_gpio(unsigned gpio)
{
#if defined(CONFIG_BF54x)
	if (gpio == GPIO_PB15 || gpio == GPIO_PC14 || gpio == GPIO_PC15
	    || gpio == GPIO_PH14 || gpio == GPIO_PH15
	    || gpio == GPIO_PJ14 || gpio == GPIO_PJ15)
		return -EINVAL;
#endif
	if (gpio >= MAX_BLACKFIN_GPIOS)
		return -EINVAL;
	return 0;
}

static void port_setup(unsigned gpio, unsigned short usage)
{
#if defined(BF538_FAMILY)
	/*
	 * BF538/9 Port C,D and E are special.
	 * Inverted PORT_FER polarity on CDE and no PORF_FER on F
	 * Regular PORT F GPIOs are handled here, CDE are exclusively
	 * managed by GPIOLIB
	 */

	if (gpio < MAX_BLACKFIN_GPIOS || gpio >= MAX_RESOURCES)
		return;

	gpio -= MAX_BLACKFIN_GPIOS;

	if (usage == GPIO_USAGE)
		*port_fer[gpio_bank(gpio)] |= gpio_bit(gpio);
	else
		*port_fer[gpio_bank(gpio)] &= ~gpio_bit(gpio);
	SSYNC();
	return;
#endif

	if (check_gpio(gpio))
		return;

#if defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
	if (usage == GPIO_USAGE)
		*port_fer[gpio_bank(gpio)] &= ~gpio_bit(gpio);
	else
		*port_fer[gpio_bank(gpio)] |= gpio_bit(gpio);
	SSYNC();
#elif defined(CONFIG_BF54x)
	if (usage == GPIO_USAGE)
		gpio_array[gpio_bank(gpio)]->port_fer &= ~gpio_bit(gpio);
	else
		gpio_array[gpio_bank(gpio)]->port_fer |= gpio_bit(gpio);
	SSYNC();
#endif
}

#ifdef BF537_FAMILY
static const s8 port_mux[] = {
	[GPIO_PF0] = 3,
	[GPIO_PF1] = 3,
	[GPIO_PF2] = 4,
	[GPIO_PF3] = 4,
	[GPIO_PF4] = 5,
	[GPIO_PF5] = 6,
	[GPIO_PF6] = 7,
	[GPIO_PF7] = 8,
	[GPIO_PF8 ... GPIO_PF15] = -1,
	[GPIO_PG0 ... GPIO_PG7] = -1,
	[GPIO_PG8] = 9,
	[GPIO_PG9] = 9,
	[GPIO_PG10] = 10,
	[GPIO_PG11] = 10,
	[GPIO_PG12] = 10,
	[GPIO_PG13] = 11,
	[GPIO_PG14] = 11,
	[GPIO_PG15] = 11,
	[GPIO_PH0 ... GPIO_PH15] = -1,
	[PORT_PJ0 ... PORT_PJ3] = -1,
	[PORT_PJ4] = 1,
	[PORT_PJ5] = 1,
	[PORT_PJ6 ... PORT_PJ9] = -1,
	[PORT_PJ10] = 0,
	[PORT_PJ11] = 0,
};

static int portmux_group_check(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);
	s8 offset = port_mux[ident];
	u16 m, pmux, pfunc;

	if (offset < 0)
		return 0;

	pmux = bfin_read_PORT_MUX();
	for (m = 0; m < ARRAY_SIZE(port_mux); ++m) {
		if (m == ident)
			continue;
		if (port_mux[m] != offset)
			continue;
		if (!is_reserved(peri, m, 1))
			continue;

		if (offset == 1)
			pfunc = (pmux >> offset) & 3;
		else
			pfunc = (pmux >> offset) & 1;
		if (pfunc != function) {
			pr_err("pin group conflict! request pin %d func %d conflict with pin %d func %d\n",
				ident, function, m, pfunc);
			return -EINVAL;
		}
	}

	return 0;
}

static void portmux_setup(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);
	s8 offset = port_mux[ident];
	u16 pmux;

	if (offset == -1)
		return;

	pmux = bfin_read_PORT_MUX();
	if (offset != 1)
		pmux &= ~(1 << offset);
	else
		pmux &= ~(3 << 1);
	pmux |= (function << offset);
	bfin_write_PORT_MUX(pmux);
}
#elif defined(CONFIG_BF54x)
inline void portmux_setup(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);
	u32 pmux;

	pmux = gpio_array[gpio_bank(ident)]->port_mux;

	pmux &= ~(0x3 << (2 * gpio_sub_n(ident)));
	pmux |= (function & 0x3) << (2 * gpio_sub_n(ident));

	gpio_array[gpio_bank(ident)]->port_mux = pmux;
}

inline u16 get_portmux(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u32 pmux = gpio_array[gpio_bank(ident)]->port_mux;
	return (pmux >> (2 * gpio_sub_n(ident)) & 0x3);
}
static int portmux_group_check(unsigned short per)
{
	return 0;
}
#elif defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
static int portmux_group_check(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);
	u8 offset = pmux_offset[gpio_bank(ident)][gpio_sub_n(ident)];
	u16 pin, gpiopin, pfunc;

	for (pin = 0; pin < GPIO_BANKSIZE; ++pin) {
		if (offset != pmux_offset[gpio_bank(ident)][pin])
			continue;

		gpiopin = gpio_bank(ident) * GPIO_BANKSIZE + pin;
		if (gpiopin == ident)
			continue;
		if (!is_reserved(peri, gpiopin, 1))
			continue;

		pfunc = *port_mux[gpio_bank(ident)];
		pfunc = (pfunc >> offset) & 3;
		if (pfunc != function) {
			pr_err("pin group conflict! request pin %d func %d conflict with pin %d func %d\n",
				ident, function, gpiopin, pfunc);
			return -EINVAL;
		}
	}

	return 0;
}

inline void portmux_setup(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);
	u8 offset = pmux_offset[gpio_bank(ident)][gpio_sub_n(ident)];
	u16 pmux;

	pmux = *port_mux[gpio_bank(ident)];
	if  (((pmux >> offset) & 3) == function)
		return;
	pmux &= ~(3 << offset);
	pmux |= (function & 3) << offset;
	*port_mux[gpio_bank(ident)] = pmux;
	SSYNC();
}
#else
# define portmux_setup(...)  do { } while (0)
static int portmux_group_check(unsigned short per)
{
	return 0;
}
#endif

#ifndef CONFIG_BF54x
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
	flags = hard_local_irq_save(); \
	if (arg) \
		gpio_array[gpio_bank(gpio)]->name |= gpio_bit(gpio); \
	else \
		gpio_array[gpio_bank(gpio)]->name &= ~gpio_bit(gpio); \
	AWA_DUMMY_READ(name); \
	hard_local_irq_restore(flags); \
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
		flags = hard_local_irq_save(); \
	if (arg) \
		gpio_array[gpio_bank(gpio)]->name ## _set = gpio_bit(gpio); \
	else \
		gpio_array[gpio_bank(gpio)]->name ## _clear = gpio_bit(gpio); \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		hard_local_irq_restore(flags); \
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
		flags = hard_local_irq_save();
	gpio_array[gpio_bank(gpio)]->toggle = gpio_bit(gpio);
	if (ANOMALY_05000311 || ANOMALY_05000323) {
		AWA_DUMMY_READ(toggle);
		hard_local_irq_restore(flags);
	}
}
EXPORT_SYMBOL(set_gpio_toggle);


/*Set current PORT date (16-bit word)*/

#define SET_GPIO_P(name) \
void set_gpiop_ ## name(unsigned gpio, unsigned short arg) \
{ \
	unsigned long flags; \
	if (ANOMALY_05000311 || ANOMALY_05000323) \
		flags = hard_local_irq_save(); \
	gpio_array[gpio_bank(gpio)]->name = arg; \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		hard_local_irq_restore(flags); \
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
		flags = hard_local_irq_save(); \
	ret = 0x01 & (gpio_array[gpio_bank(gpio)]->name >> gpio_sub_n(gpio)); \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		hard_local_irq_restore(flags); \
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
		flags = hard_local_irq_save(); \
	ret = (gpio_array[gpio_bank(gpio)]->name); \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		hard_local_irq_restore(flags); \
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
DECLARE_RESERVED_MAP(wakeup, GPIO_BANK_NUM);

static const unsigned int sic_iwr_irqs[] = {
#if defined(BF533_FAMILY)
	IRQ_PROG_INTB
#elif defined(BF537_FAMILY)
	IRQ_PF_INTB_WATCH, IRQ_PORTG_INTB, IRQ_PH_INTB_MAC_TX
#elif defined(BF538_FAMILY)
	IRQ_PORTF_INTB
#elif defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
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
int gpio_pm_wakeup_ctrl(unsigned gpio, unsigned ctrl)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	flags = hard_local_irq_save();
	if (ctrl)
		reserve(wakeup, gpio);
	else
		unreserve(wakeup, gpio);

	set_gpio_maskb(gpio, ctrl);
	hard_local_irq_restore(flags);

	return 0;
}

int bfin_pm_standby_ctrl(unsigned ctrl)
{
	u16 bank, mask, i;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		mask = map_entry(wakeup, i);
		bank = gpio_bank(i);

		if (mask)
			bfin_internal_set_wake(sic_iwr_irqs[bank], ctrl);
	}
	return 0;
}

void bfin_gpio_pm_hibernate_suspend(void)
{
	int i, bank;

#ifdef BF538_FAMILY
	for (i = 0; i < ARRAY_SIZE(port_fer_saved); ++i)
		port_fer_saved[i] = *port_fer[i];
#endif

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

#if defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
		gpio_bank_saved[bank].fer = *port_fer[bank];
#if defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
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

#ifdef BFIN_SPECIAL_GPIO_BANKS
	bfin_special_gpio_pm_hibernate_suspend();
#endif

	AWA_DUMMY_READ(maska);
}

void bfin_gpio_pm_hibernate_restore(void)
{
	int i, bank;

#ifdef BF538_FAMILY
	for (i = 0; i < ARRAY_SIZE(port_fer_saved); ++i)
		*port_fer[i] = port_fer_saved[i];
#endif

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

#if defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
#if defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
		*port_mux[bank] = gpio_bank_saved[bank].mux;
#else
		if (bank == 0)
			bfin_write_PORT_MUX(gpio_bank_saved[bank].mux);
#endif
		*port_fer[bank] = gpio_bank_saved[bank].fer;
#endif
		gpio_array[bank]->inen  = gpio_bank_saved[bank].inen;
		gpio_array[bank]->data_set = gpio_bank_saved[bank].data
						& gpio_bank_saved[bank].dir;
		gpio_array[bank]->dir   = gpio_bank_saved[bank].dir;
		gpio_array[bank]->polar = gpio_bank_saved[bank].polar;
		gpio_array[bank]->edge  = gpio_bank_saved[bank].edge;
		gpio_array[bank]->both  = gpio_bank_saved[bank].both;
		gpio_array[bank]->maska = gpio_bank_saved[bank].maska;
	}

#ifdef BFIN_SPECIAL_GPIO_BANKS
	bfin_special_gpio_pm_hibernate_restore();
#endif

	AWA_DUMMY_READ(maska);
}


#endif
#else /* CONFIG_BF54x */
#ifdef CONFIG_PM

int bfin_pm_standby_ctrl(unsigned ctrl)
{
	return 0;
}

void bfin_gpio_pm_hibernate_suspend(void)
{
	int i, bank;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

		gpio_bank_saved[bank].fer = gpio_array[bank]->port_fer;
		gpio_bank_saved[bank].mux = gpio_array[bank]->port_mux;
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
		gpio_array[bank]->data_set = gpio_bank_saved[bank].data
						& gpio_bank_saved[bank].dir;
		gpio_array[bank]->dir_set = gpio_bank_saved[bank].dir;
	}
}
#endif

unsigned short get_gpio_dir(unsigned gpio)
{
	return (0x01 & (gpio_array[gpio_bank(gpio)]->dir_clear >> gpio_sub_n(gpio)));
}
EXPORT_SYMBOL(get_gpio_dir);

#endif /* CONFIG_BF54x */

/***********************************************************
*
* FUNCTIONS:	Blackfin Peripheral Resource Allocation
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

	BUG_ON(ident >= MAX_RESOURCES);

	flags = hard_local_irq_save();

	/* If a pin can be muxed as either GPIO or peripheral, make
	 * sure it is not already a GPIO pin when we request it.
	 */
	if (unlikely(!check_gpio(ident) && is_reserved(gpio, ident, 1))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		printk(KERN_ERR
		       "%s: Peripheral %d is already reserved as GPIO by %s !\n",
		       __func__, ident, get_label(ident));
		hard_local_irq_restore(flags);
		return -EBUSY;
	}

	if (unlikely(is_reserved(peri, ident, 1))) {

		/*
		 * Pin functions like AMC address strobes my
		 * be requested and used by several drivers
		 */

#ifdef CONFIG_BF54x
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

			if (system_state == SYSTEM_BOOTING)
				dump_stack();
			printk(KERN_ERR
			       "%s: Peripheral %d function %d is already reserved by %s !\n",
			       __func__, ident, P_FUNCT2MUX(per), get_label(ident));
			hard_local_irq_restore(flags);
			return -EBUSY;
		}
	}

	if (unlikely(portmux_group_check(per))) {
		hard_local_irq_restore(flags);
		return -EBUSY;
	}
 anyway:
	reserve(peri, ident);

	portmux_setup(per);
	port_setup(ident, PERIPHERAL_USAGE);

	hard_local_irq_restore(flags);
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

	flags = hard_local_irq_save();

	if (unlikely(!is_reserved(peri, ident, 0))) {
		hard_local_irq_restore(flags);
		return;
	}

	if (!(per & P_MAYSHARE))
		port_setup(ident, GPIO_USAGE);

	unreserve(peri, ident);

	set_label(ident, "free");

	hard_local_irq_restore(flags);
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

	flags = hard_local_irq_save();

	/*
	 * Allow that the identical GPIO can
	 * be requested from the same driver twice
	 * Do nothing and return -
	 */

	if (cmp_label(gpio, label) == 0) {
		hard_local_irq_restore(flags);
		return 0;
	}

	if (unlikely(is_reserved(gpio, gpio, 1))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		printk(KERN_ERR "bfin-gpio: GPIO %d is already reserved by %s !\n",
		       gpio, get_label(gpio));
		hard_local_irq_restore(flags);
		return -EBUSY;
	}
	if (unlikely(is_reserved(peri, gpio, 1))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));
		hard_local_irq_restore(flags);
		return -EBUSY;
	}
	if (unlikely(is_reserved(gpio_irq, gpio, 1))) {
		printk(KERN_NOTICE "bfin-gpio: GPIO %d is already reserved as gpio-irq!"
		       " (Documentation/blackfin/bfin-gpio-notes.txt)\n", gpio);
	}
#ifndef CONFIG_BF54x
	else {	/* Reset POLAR setting when acquiring a gpio for the first time */
		set_gpio_polar(gpio, 0);
	}
#endif

	reserve(gpio, gpio);
	set_label(gpio, label);

	hard_local_irq_restore(flags);

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

	flags = hard_local_irq_save();

	if (unlikely(!is_reserved(gpio, gpio, 0))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		gpio_error(gpio);
		hard_local_irq_restore(flags);
		return;
	}

	unreserve(gpio, gpio);

	set_label(gpio, "free");

	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL(bfin_gpio_free);

#ifdef BFIN_SPECIAL_GPIO_BANKS
DECLARE_RESERVED_MAP(special_gpio, gpio_bank(MAX_RESOURCES));

int bfin_special_gpio_request(unsigned gpio, const char *label)
{
	unsigned long flags;

	flags = hard_local_irq_save();

	/*
	 * Allow that the identical GPIO can
	 * be requested from the same driver twice
	 * Do nothing and return -
	 */

	if (cmp_label(gpio, label) == 0) {
		hard_local_irq_restore(flags);
		return 0;
	}

	if (unlikely(is_reserved(special_gpio, gpio, 1))) {
		hard_local_irq_restore(flags);
		printk(KERN_ERR "bfin-gpio: GPIO %d is already reserved by %s !\n",
		       gpio, get_label(gpio));

		return -EBUSY;
	}
	if (unlikely(is_reserved(peri, gpio, 1))) {
		hard_local_irq_restore(flags);
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));

		return -EBUSY;
	}

	reserve(special_gpio, gpio);
	reserve(peri, gpio);

	set_label(gpio, label);
	hard_local_irq_restore(flags);
	port_setup(gpio, GPIO_USAGE);

	return 0;
}
EXPORT_SYMBOL(bfin_special_gpio_request);

void bfin_special_gpio_free(unsigned gpio)
{
	unsigned long flags;

	might_sleep();

	flags = hard_local_irq_save();

	if (unlikely(!is_reserved(special_gpio, gpio, 0))) {
		gpio_error(gpio);
		hard_local_irq_restore(flags);
		return;
	}

	unreserve(special_gpio, gpio);
	unreserve(peri, gpio);
	set_label(gpio, "free");
	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL(bfin_special_gpio_free);
#endif


int bfin_gpio_irq_request(unsigned gpio, const char *label)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	flags = hard_local_irq_save();

	if (unlikely(is_reserved(peri, gpio, 1))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));
		hard_local_irq_restore(flags);
		return -EBUSY;
	}
	if (unlikely(is_reserved(gpio, gpio, 1)))
		printk(KERN_NOTICE "bfin-gpio: GPIO %d is already reserved by %s! "
		       "(Documentation/blackfin/bfin-gpio-notes.txt)\n",
		       gpio, get_label(gpio));

	reserve(gpio_irq, gpio);
	set_label(gpio, label);

	hard_local_irq_restore(flags);

	port_setup(gpio, GPIO_USAGE);

	return 0;
}

void bfin_gpio_irq_free(unsigned gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	flags = hard_local_irq_save();

	if (unlikely(!is_reserved(gpio_irq, gpio, 0))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		gpio_error(gpio);
		hard_local_irq_restore(flags);
		return;
	}

	unreserve(gpio_irq, gpio);

	set_label(gpio, "free");

	hard_local_irq_restore(flags);
}

static inline void __bfin_gpio_direction_input(unsigned gpio)
{
#ifdef CONFIG_BF54x
	gpio_array[gpio_bank(gpio)]->dir_clear = gpio_bit(gpio);
#else
	gpio_array[gpio_bank(gpio)]->dir &= ~gpio_bit(gpio);
#endif
	gpio_array[gpio_bank(gpio)]->inen |= gpio_bit(gpio);
}

int bfin_gpio_direction_input(unsigned gpio)
{
	unsigned long flags;

	if (unlikely(!is_reserved(gpio, gpio, 0))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	flags = hard_local_irq_save();
	__bfin_gpio_direction_input(gpio);
	AWA_DUMMY_READ(inen);
	hard_local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(bfin_gpio_direction_input);

void bfin_gpio_irq_prepare(unsigned gpio)
{
#ifdef CONFIG_BF54x
	unsigned long flags;
#endif

	port_setup(gpio, GPIO_USAGE);

#ifdef CONFIG_BF54x
	flags = hard_local_irq_save();
	__bfin_gpio_direction_input(gpio);
	hard_local_irq_restore(flags);
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

	if (unlikely(!is_reserved(gpio, gpio, 0))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	flags = hard_local_irq_save();

	gpio_array[gpio_bank(gpio)]->inen &= ~gpio_bit(gpio);
	gpio_set_value(gpio, value);
#ifdef CONFIG_BF54x
	gpio_array[gpio_bank(gpio)]->dir_set = gpio_bit(gpio);
#else
	gpio_array[gpio_bank(gpio)]->dir |= gpio_bit(gpio);
#endif

	AWA_DUMMY_READ(dir);
	hard_local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(bfin_gpio_direction_output);

int bfin_gpio_get_value(unsigned gpio)
{
#ifdef CONFIG_BF54x
	return (1 & (gpio_array[gpio_bank(gpio)]->data >> gpio_sub_n(gpio)));
#else
	unsigned long flags;

	if (unlikely(get_gpio_edge(gpio))) {
		int ret;
		flags = hard_local_irq_save();
		set_gpio_edge(gpio, 0);
		ret = get_gpio_data(gpio);
		set_gpio_edge(gpio, 1);
		hard_local_irq_restore(flags);
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
static int gpio_proc_show(struct seq_file *m, void *v)
{
	int c, irq, gpio;

	for (c = 0; c < MAX_RESOURCES; c++) {
		irq = is_reserved(gpio_irq, c, 1);
		gpio = is_reserved(gpio, c, 1);
		if (!check_gpio(c) && (gpio || irq))
			seq_printf(m, "GPIO_%d: \t%s%s \t\tGPIO %s\n", c,
				 get_label(c), (gpio && irq) ? " *" : "",
				 get_gpio_dir(c) ? "OUTPUT" : "INPUT");
		else if (is_reserved(peri, c, 1))
			seq_printf(m, "GPIO_%d: \t%s \t\tPeripheral\n", c, get_label(c));
		else
			continue;
	}

	return 0;
}

static int gpio_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, gpio_proc_show, NULL);
}

static const struct file_operations gpio_proc_ops = {
	.open		= gpio_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static __init int gpio_register_proc(void)
{
	struct proc_dir_entry *proc_gpio;

	proc_gpio = proc_create("gpio", S_IRUGO, NULL, &gpio_proc_ops);
	return proc_gpio != NULL;
}
__initcall(gpio_register_proc);
#endif

#ifdef CONFIG_GPIOLIB
static int bfin_gpiolib_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_direction_input(gpio);
}

static int bfin_gpiolib_direction_output(struct gpio_chip *chip, unsigned gpio, int level)
{
	return bfin_gpio_direction_output(gpio, level);
}

static int bfin_gpiolib_get_value(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_get_value(gpio);
}

static void bfin_gpiolib_set_value(struct gpio_chip *chip, unsigned gpio, int value)
{
	return bfin_gpio_set_value(gpio, value);
}

static int bfin_gpiolib_gpio_request(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_request(gpio, chip->label);
}

static void bfin_gpiolib_gpio_free(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_free(gpio);
}

static int bfin_gpiolib_gpio_to_irq(struct gpio_chip *chip, unsigned gpio)
{
	return gpio + GPIO_IRQ_BASE;
}

static struct gpio_chip bfin_chip = {
	.label			= "BFIN-GPIO",
	.direction_input	= bfin_gpiolib_direction_input,
	.get			= bfin_gpiolib_get_value,
	.direction_output	= bfin_gpiolib_direction_output,
	.set			= bfin_gpiolib_set_value,
	.request		= bfin_gpiolib_gpio_request,
	.free			= bfin_gpiolib_gpio_free,
	.to_irq			= bfin_gpiolib_gpio_to_irq,
	.base			= 0,
	.ngpio			= MAX_BLACKFIN_GPIOS,
};

static int __init bfin_gpiolib_setup(void)
{
	return gpiochip_add(&bfin_chip);
}
arch_initcall(bfin_gpiolib_setup);
#endif
