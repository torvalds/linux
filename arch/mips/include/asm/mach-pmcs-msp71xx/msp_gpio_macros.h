/*
 *
 * Macros for external SMP-safe access to the PMC MSP71xx reference
 * board GPIO pins
 *
 * Copyright 2010 PMC-Sierra, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __MSP_GPIO_MACROS_H__
#define __MSP_GPIO_MACROS_H__

#include <msp_regops.h>
#include <msp_regs.h>

#ifdef CONFIG_PMC_MSP7120_GW
#define MSP_NUM_GPIOS		20
#else
#define MSP_NUM_GPIOS		28
#endif

/* -- GPIO Enumerations -- */
enum msp_gpio_data {
	MSP_GPIO_LO = 0,
	MSP_GPIO_HI = 1,
	MSP_GPIO_NONE,		/* Special - Means pin is out of range */
	MSP_GPIO_TOGGLE,	/* Special - Sets pin to opposite */
};

enum msp_gpio_mode {
	MSP_GPIO_INPUT		= 0x0,
	/* MSP_GPIO_ INTERRUPT	= 0x1,	Not supported yet */
	MSP_GPIO_UART_INPUT	= 0x2,	/* Only GPIO 4 or 5 */
	MSP_GPIO_OUTPUT		= 0x8,
	MSP_GPIO_UART_OUTPUT	= 0x9,	/* Only GPIO 2 or 3 */
	MSP_GPIO_PERIF_TIMERA	= 0x9,	/* Only GPIO 0 or 1 */
	MSP_GPIO_PERIF_TIMERB	= 0xa,	/* Only GPIO 0 or 1 */
	MSP_GPIO_UNKNOWN	= 0xb,	/* No such GPIO or mode */
};

/* -- Static Tables -- */

/* Maps pins to data register */
static volatile u32 * const MSP_GPIO_DATA_REGISTER[] = {
	/* GPIO 0 and 1 on the first register */
	GPIO_DATA1_REG, GPIO_DATA1_REG,
	/* GPIO 2, 3, 4, and 5 on the second register */
	GPIO_DATA2_REG, GPIO_DATA2_REG, GPIO_DATA2_REG, GPIO_DATA2_REG,
	/* GPIO 6, 7, 8, and 9 on the third register */
	GPIO_DATA3_REG, GPIO_DATA3_REG, GPIO_DATA3_REG, GPIO_DATA3_REG,
	/* GPIO 10, 11, 12, 13, 14, and 15 on the fourth register */
	GPIO_DATA4_REG, GPIO_DATA4_REG, GPIO_DATA4_REG, GPIO_DATA4_REG,
	GPIO_DATA4_REG, GPIO_DATA4_REG,
	/* GPIO 16 - 23 on the first strange EXTENDED register */
	EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG,
	EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG,
	EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG,
	/* GPIO 24 - 27 on the second strange EXTENDED register */
	EXTENDED_GPIO2_REG, EXTENDED_GPIO2_REG, EXTENDED_GPIO2_REG,
	EXTENDED_GPIO2_REG,
};

/* Maps pins to mode register */
static volatile u32 * const MSP_GPIO_MODE_REGISTER[] = {
	/* GPIO 0 and 1 on the first register */
	GPIO_CFG1_REG, GPIO_CFG1_REG,
	/* GPIO 2, 3, 4, and 5 on the second register */
	GPIO_CFG2_REG, GPIO_CFG2_REG, GPIO_CFG2_REG, GPIO_CFG2_REG,
	/* GPIO 6, 7, 8, and 9 on the third register */
	GPIO_CFG3_REG, GPIO_CFG3_REG, GPIO_CFG3_REG, GPIO_CFG3_REG,
	/* GPIO 10, 11, 12, 13, 14, and 15 on the fourth register */
	GPIO_CFG4_REG, GPIO_CFG4_REG, GPIO_CFG4_REG, GPIO_CFG4_REG,
	GPIO_CFG4_REG, GPIO_CFG4_REG,
	/* GPIO 16 - 23 on the first strange EXTENDED register */
	EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG,
	EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG,
	EXTENDED_GPIO1_REG, EXTENDED_GPIO1_REG,
	/* GPIO 24 - 27 on the second strange EXTENDED register */
	EXTENDED_GPIO2_REG, EXTENDED_GPIO2_REG, EXTENDED_GPIO2_REG,
	EXTENDED_GPIO2_REG,
};

/* Maps 'basic' pins to relative offset from 0 per register */
static int MSP_GPIO_OFFSET[] = {
	/* GPIO 0 and 1 on the first register */
	0, 0,
	/* GPIO 2, 3, 4, and 5 on the second register */
	2, 2, 2, 2,
	/* GPIO 6, 7, 8, and 9 on the third register */
	6, 6, 6, 6,
	/* GPIO 10, 11, 12, 13, 14, and 15 on the fourth register */
	10, 10, 10, 10, 10, 10,
};

/* Maps MODE to allowed pin mask */
static unsigned int MSP_GPIO_MODE_ALLOWED[] = {
	0xffffffff,	/* Mode 0 - INPUT */
	0x00000,	/* Mode 1 - INTERRUPT */
	0x00030,	/* Mode 2 - UART_INPUT (GPIO 4, 5)*/
	0, 0, 0, 0, 0,	/* Modes 3, 4, 5, 6, and 7 are reserved */
	0xffffffff,	/* Mode 8 - OUTPUT */
	0x0000f,	/* Mode 9 - UART_OUTPUT/
				PERF_TIMERA (GPIO 0, 1, 2, 3) */
	0x00003,	/* Mode a - PERF_TIMERB (GPIO 0, 1) */
	0x00000,	/* Mode b - Not really a mode! */
};

/* -- Bit masks -- */

/* This gives you the 'register relative offset gpio' number */
#define OFFSET_GPIO_NUMBER(gpio)	(gpio - MSP_GPIO_OFFSET[gpio])

/* These take the 'register relative offset gpio' number */
#define BASIC_DATA_REG_MASK(ogpio)		(1 << ogpio)
#define BASIC_MODE_REG_VALUE(mode, ogpio)	\
	(mode << BASIC_MODE_REG_SHIFT(ogpio))
#define BASIC_MODE_REG_MASK(ogpio)		\
	BASIC_MODE_REG_VALUE(0xf, ogpio)
#define BASIC_MODE_REG_SHIFT(ogpio)		(ogpio * 4)
#define BASIC_MODE_REG_FROM_REG(data, ogpio)	\
	((data & BASIC_MODE_REG_MASK(ogpio)) >> BASIC_MODE_REG_SHIFT(ogpio))

/* These take the actual GPIO number (0 through 15) */
#define BASIC_DATA_MASK(gpio)	\
	BASIC_DATA_REG_MASK(OFFSET_GPIO_NUMBER(gpio))
#define BASIC_MODE_MASK(gpio)	\
	BASIC_MODE_REG_MASK(OFFSET_GPIO_NUMBER(gpio))
#define BASIC_MODE(mode, gpio)	\
	BASIC_MODE_REG_VALUE(mode, OFFSET_GPIO_NUMBER(gpio))
#define BASIC_MODE_SHIFT(gpio)	\
	BASIC_MODE_REG_SHIFT(OFFSET_GPIO_NUMBER(gpio))
#define BASIC_MODE_FROM_REG(data, gpio) \
	BASIC_MODE_REG_FROM_REG(data, OFFSET_GPIO_NUMBER(gpio))

/*
 * Each extended GPIO register is 32 bits long and is responsible for up to
 * eight GPIOs. The least significant 16 bits contain the set and clear bit
 * pair for each of the GPIOs. The most significant 16 bits contain the
 * disable and enable bit pair for each of the GPIOs. For example, the
 * extended GPIO reg for GPIOs 16-23 is as follows:
 *
 *	31: GPIO23_DISABLE
 *	...
 *	19: GPIO17_DISABLE
 *	18: GPIO17_ENABLE
 *	17: GPIO16_DISABLE
 *	16: GPIO16_ENABLE
 *	...
 *	3:  GPIO17_SET
 *	2:  GPIO17_CLEAR
 *	1:  GPIO16_SET
 *	0:  GPIO16_CLEAR
 */

/* This gives the 'register relative offset gpio' number */
#define EXTENDED_OFFSET_GPIO(gpio)	(gpio < 24 ? gpio - 16 : gpio - 24)

/* These take the 'register relative offset gpio' number */
#define EXTENDED_REG_DISABLE(ogpio)	(0x2 << ((ogpio * 2) + 16))
#define EXTENDED_REG_ENABLE(ogpio)	(0x1 << ((ogpio * 2) + 16))
#define EXTENDED_REG_SET(ogpio)		(0x2 << (ogpio * 2))
#define EXTENDED_REG_CLR(ogpio)		(0x1 << (ogpio * 2))

/* These take the actual GPIO number (16 through 27) */
#define EXTENDED_DISABLE(gpio)	\
	EXTENDED_REG_DISABLE(EXTENDED_OFFSET_GPIO(gpio))
#define EXTENDED_ENABLE(gpio)	\
	EXTENDED_REG_ENABLE(EXTENDED_OFFSET_GPIO(gpio))
#define EXTENDED_SET(gpio)	\
	EXTENDED_REG_SET(EXTENDED_OFFSET_GPIO(gpio))
#define EXTENDED_CLR(gpio)	\
	EXTENDED_REG_CLR(EXTENDED_OFFSET_GPIO(gpio))

#define EXTENDED_FULL_MASK		(0xffffffff)

/* -- API inline-functions -- */

/*
 * Gets the current value of the specified pin
 */
static inline enum msp_gpio_data msp_gpio_pin_get(unsigned int gpio)
{
	u32 pinhi_mask = 0, pinhi_mask2 = 0;

	if (gpio >= MSP_NUM_GPIOS)
		return MSP_GPIO_NONE;

	if (gpio < 16) {
		pinhi_mask = BASIC_DATA_MASK(gpio);
	} else {
		/*
		 * Two cases are possible with the EXTENDED register:
		 *  - In output mode (ENABLED flag set), check the CLR bit
		 *  - In input mode (ENABLED flag not set), check the SET bit
		 */
		pinhi_mask = EXTENDED_ENABLE(gpio) | EXTENDED_CLR(gpio);
		pinhi_mask2 = EXTENDED_SET(gpio);
	}
	if (((*MSP_GPIO_DATA_REGISTER[gpio] & pinhi_mask) == pinhi_mask) ||
	    (*MSP_GPIO_DATA_REGISTER[gpio] & pinhi_mask2))
		return MSP_GPIO_HI;
	else
		return MSP_GPIO_LO;
}

/* Sets the specified pin to the specified value */
static inline void msp_gpio_pin_set(enum msp_gpio_data data, unsigned int gpio)
{
	if (gpio >= MSP_NUM_GPIOS)
		return;

	if (gpio < 16) {
		if (data == MSP_GPIO_TOGGLE)
			toggle_reg32(MSP_GPIO_DATA_REGISTER[gpio],
					BASIC_DATA_MASK(gpio));
		else if (data == MSP_GPIO_HI)
			set_reg32(MSP_GPIO_DATA_REGISTER[gpio],
					BASIC_DATA_MASK(gpio));
		else
			clear_reg32(MSP_GPIO_DATA_REGISTER[gpio],
					BASIC_DATA_MASK(gpio));
	} else {
		if (data == MSP_GPIO_TOGGLE) {
			/* Special ugly case:
			 *   We have to read the CLR bit.
			 *   If set, we write the CLR bit.
			 *   If not, we write the SET bit.
			 */
			u32 tmpdata;

			custom_read_reg32(MSP_GPIO_DATA_REGISTER[gpio],
								tmpdata);
			if (tmpdata & EXTENDED_CLR(gpio))
				tmpdata = EXTENDED_CLR(gpio);
			else
				tmpdata = EXTENDED_SET(gpio);
			custom_write_reg32(MSP_GPIO_DATA_REGISTER[gpio],
								tmpdata);
		} else {
			u32 newdata;

			if (data == MSP_GPIO_HI)
				newdata = EXTENDED_SET(gpio);
			else
				newdata = EXTENDED_CLR(gpio);
			set_value_reg32(MSP_GPIO_DATA_REGISTER[gpio],
						EXTENDED_FULL_MASK, newdata);
		}
	}
}

/* Sets the specified pin to the specified value */
static inline void msp_gpio_pin_hi(unsigned int gpio)
{
	msp_gpio_pin_set(MSP_GPIO_HI, gpio);
}

/* Sets the specified pin to the specified value */
static inline void msp_gpio_pin_lo(unsigned int gpio)
{
	msp_gpio_pin_set(MSP_GPIO_LO, gpio);
}

/* Sets the specified pin to the opposite value */
static inline void msp_gpio_pin_toggle(unsigned int gpio)
{
	msp_gpio_pin_set(MSP_GPIO_TOGGLE, gpio);
}

/* Gets the mode of the specified pin */
static inline enum msp_gpio_mode msp_gpio_pin_get_mode(unsigned int gpio)
{
	enum msp_gpio_mode retval = MSP_GPIO_UNKNOWN;
	uint32_t data;

	if (gpio >= MSP_NUM_GPIOS)
		return retval;

	data = *MSP_GPIO_MODE_REGISTER[gpio];

	if (gpio < 16) {
		retval = BASIC_MODE_FROM_REG(data, gpio);
	} else {
		/* Extended pins can only be either INPUT or OUTPUT */
		if (data & EXTENDED_ENABLE(gpio))
			retval = MSP_GPIO_OUTPUT;
		else
			retval = MSP_GPIO_INPUT;
	}

	return retval;
}

/*
 * Sets the specified mode on the requested pin
 * Returns 0 on success, or -1 if that mode is not allowed on this pin
 */
static inline int msp_gpio_pin_mode(enum msp_gpio_mode mode, unsigned int gpio)
{
	u32 modemask, newmode;

	if ((1 << gpio) & ~MSP_GPIO_MODE_ALLOWED[mode])
		return -1;

	if (gpio >= MSP_NUM_GPIOS)
		return -1;

	if (gpio < 16) {
		modemask = BASIC_MODE_MASK(gpio);
		newmode =  BASIC_MODE(mode, gpio);
	} else {
		modemask = EXTENDED_FULL_MASK;
		if (mode == MSP_GPIO_INPUT)
			newmode = EXTENDED_DISABLE(gpio);
		else
			newmode = EXTENDED_ENABLE(gpio);
	}
	/* Do the set atomically */
	set_value_reg32(MSP_GPIO_MODE_REGISTER[gpio], modemask, newmode);

	return 0;
}

#endif /* __MSP_GPIO_MACROS_H__ */
