/*
 * The following devices are accessable using this driver using
 * GPIO_MAJOR (120) and a couple of minor numbers.
 *
 * For ETRAX 100LX (CONFIG_ETRAX_ARCH_V10):
 * /dev/gpioa  minor 0, 8 bit GPIO, each bit can change direction
 * /dev/gpiob  minor 1, 8 bit GPIO, each bit can change direction
 * /dev/leds   minor 2, Access to leds depending on kernelconfig
 * /dev/gpiog  minor 3
 *       g0dir, g8_15dir, g16_23dir, g24 dir configurable in R_GEN_CONFIG
 *       g1-g7 and g25-g31 is both input and outputs but on different pins
 *       Also note that some bits change pins depending on what interfaces
 *       are enabled.
 *
 * For ETRAX FS (CONFIG_ETRAXFS):
 * /dev/gpioa  minor 0,  8 bit GPIO, each bit can change direction
 * /dev/gpiob  minor 1, 18 bit GPIO, each bit can change direction
 * /dev/gpioc  minor 3, 18 bit GPIO, each bit can change direction
 * /dev/gpiod  minor 4, 18 bit GPIO, each bit can change direction
 * /dev/gpioe  minor 5, 18 bit GPIO, each bit can change direction
 * /dev/leds   minor 2, Access to leds depending on kernelconfig
 *
 * For ARTPEC-3 (CONFIG_CRIS_MACH_ARTPEC3):
 * /dev/gpioa  minor 0, 32 bit GPIO, each bit can change direction
 * /dev/gpiob  minor 1, 32 bit GPIO, each bit can change direction
 * /dev/gpioc  minor 3, 16 bit GPIO, each bit can change direction
 * /dev/gpiod  minor 4, 32 bit GPIO, input only
 * /dev/leds   minor 2, Access to leds depending on kernelconfig
 * /dev/pwm0   minor 16, PWM channel 0 on PA30
 * /dev/pwm1   minor 17, PWM channel 1 on PA31
 * /dev/pwm2   minor 18, PWM channel 2 on PB26
 * /dev/ppwm   minor 19, PPWM channel
 *
 */
#ifndef _ASM_ETRAXGPIO_H
#define _ASM_ETRAXGPIO_H

#define GPIO_MINOR_FIRST 0

#define ETRAXGPIO_IOCTYPE 43

/* etraxgpio _IOC_TYPE, bits 8 to 15 in ioctl cmd */
#ifdef CONFIG_ETRAX_ARCH_V10
#define GPIO_MINOR_A 0
#define GPIO_MINOR_B 1
#define GPIO_MINOR_LEDS 2
#define GPIO_MINOR_G 3
#define GPIO_MINOR_LAST 3
#define GPIO_MINOR_LAST_REAL GPIO_MINOR_LAST
#endif

#ifdef CONFIG_ETRAXFS
#define GPIO_MINOR_A 0
#define GPIO_MINOR_B 1
#define GPIO_MINOR_LEDS 2
#define GPIO_MINOR_C 3
#define GPIO_MINOR_D 4
#define GPIO_MINOR_E 5
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
#define GPIO_MINOR_V 6
#define GPIO_MINOR_LAST 6
#else
#define GPIO_MINOR_LAST 5
#endif
#define GPIO_MINOR_LAST_REAL GPIO_MINOR_LAST
#endif

#ifdef CONFIG_CRIS_MACH_ARTPEC3
#define GPIO_MINOR_A 0
#define GPIO_MINOR_B 1
#define GPIO_MINOR_LEDS 2
#define GPIO_MINOR_C 3
#define GPIO_MINOR_D 4
#ifdef CONFIG_ETRAX_VIRTUAL_GPIO
#define GPIO_MINOR_V 6
#define GPIO_MINOR_LAST 6
#else
#define GPIO_MINOR_LAST 4
#endif
#define GPIO_MINOR_FIRST_PWM 16
#define GPIO_MINOR_PWM0 (GPIO_MINOR_FIRST_PWM+0)
#define GPIO_MINOR_PWM1 (GPIO_MINOR_FIRST_PWM+1)
#define GPIO_MINOR_PWM2 (GPIO_MINOR_FIRST_PWM+2)
#define GPIO_MINOR_PPWM (GPIO_MINOR_FIRST_PWM+3)
#define GPIO_MINOR_LAST_PWM GPIO_MINOR_PPWM
#define GPIO_MINOR_LAST_REAL GPIO_MINOR_LAST_PWM
#endif



/* supported ioctl _IOC_NR's */

#define IO_READBITS  0x1  /* read and return current port bits (obsolete) */
#define IO_SETBITS   0x2  /* set the bits marked by 1 in the argument */
#define IO_CLRBITS   0x3  /* clear the bits marked by 1 in the argument */

/* the alarm is waited for by select() */

#define IO_HIGHALARM 0x4  /* set alarm on high for bits marked by 1 */
#define IO_LOWALARM  0x5  /* set alarm on low for bits marked by 1 */
#define IO_CLRALARM  0x6  /* clear alarm for bits marked by 1 */

/* LED ioctl */
#define IO_LEDACTIVE_SET 0x7 /* set active led
                              * 0=off, 1=green, 2=red, 3=yellow */

/* GPIO direction ioctl's */
#define IO_READDIR    0x8  /* Read direction 0=input 1=output  (obsolete) */
#define IO_SETINPUT   0x9  /* Set direction for bits set, 0=unchanged 1=input,
                              returns mask with current inputs (obsolete) */
#define IO_SETOUTPUT  0xA  /* Set direction for bits set, 0=unchanged 1=output,
                              returns mask with current outputs (obsolete)*/

/* LED ioctl extended */
#define IO_LED_SETBIT 0xB
#define IO_LED_CLRBIT 0xC

/* SHUTDOWN ioctl */
#define IO_SHUTDOWN   0xD
#define IO_GET_PWR_BT 0xE

/* Bit toggling in driver settings */
/* bit set in low byte0 is CLK mask (0x00FF),
   bit set in byte1 is DATA mask    (0xFF00)
   msb, data_mask[7:0] , clk_mask[7:0]
 */
#define IO_CFG_WRITE_MODE 0xF
#define IO_CFG_WRITE_MODE_VALUE(msb, data_mask, clk_mask) \
	( (((msb)&1) << 16) | (((data_mask) &0xFF) << 8) | ((clk_mask) & 0xFF) )

/* The following 4 ioctl's take a pointer as argument and handles
 * 32 bit ports (port G) properly.
 * These replaces IO_READBITS,IO_SETINPUT AND IO_SETOUTPUT
 */
#define IO_READ_INBITS   0x10 /* *arg is result of reading the input pins */
#define IO_READ_OUTBITS  0x11 /* *arg is result of reading the output shadow */
#define IO_SETGET_INPUT  0x12 /* bits set in *arg is set to input, */
			      /* *arg updated with current input pins. */
#define IO_SETGET_OUTPUT 0x13 /* bits set in *arg is set to output, */
			      /* *arg updated with current output pins. */

/* The following ioctl's are applicable to the PWM channels only */

#define IO_PWM_SET_MODE     0x20

enum io_pwm_mode {
	PWM_OFF = 0,		/* disabled, deallocated */
	PWM_STANDARD = 1,	/* 390 kHz, duty cycle 0..255/256 */
	PWM_FAST = 2,		/* variable freq, w/ 10ns active pulse len */
	PWM_VARFREQ = 3,	/* individually configurable high/low periods */
	PWM_SOFT = 4		/* software generated */
};

struct io_pwm_set_mode {
	enum io_pwm_mode mode;
};

/* Only for mode PWM_VARFREQ. Period lo/high set in increments of 10ns
 * from 10ns (value = 0) to 81920ns (value = 8191)
 * (Resulting frequencies range from 50 MHz (10ns + 10ns) down to
 * 6.1 kHz (81920ns + 81920ns) at 50% duty cycle, to 12.2 kHz at min/max duty
 * cycle (81920 + 10ns or 10ns + 81920ns, respectively).)
 */
#define IO_PWM_SET_PERIOD   0x21

struct io_pwm_set_period {
	unsigned int lo;		/* 0..8191 */
	unsigned int hi;		/* 0..8191 */
};

/* Only for modes PWM_STANDARD and PWM_FAST.
 * For PWM_STANDARD, set duty cycle of 390 kHz PWM output signal, from
 * 0 (value = 0) to 255/256 (value = 255).
 * For PWM_FAST, set duty cycle of PWM output signal from
 * 0% (value = 0) to 100% (value = 255). Output signal in this mode
 * is a 10ns pulse surrounded by a high or low level depending on duty
 * cycle (except for 0% and 100% which result in a constant output).
 * Resulting output frequency varies from 50 MHz at 50% duty cycle,
 * down to 390 kHz at min/max duty cycle.
 */
#define IO_PWM_SET_DUTY     0x22

struct io_pwm_set_duty {
	int duty;		/* 0..255 */
};

/* Returns information about the latest PWM pulse.
 * lo: Length of the latest low period, in units of 10ns.
 * hi: Length of the latest high period, in units of 10ns.
 * cnt: Time since last detected edge, in units of 10ns.
 *
 * The input source to PWM is decied by IO_PWM_SET_INPUT_SRC.
 *
 * NOTE: All PWM devices is connected to the same input source.
 */
#define IO_PWM_GET_PERIOD   0x23

struct io_pwm_get_period {
	unsigned int lo;
	unsigned int hi;
	unsigned int cnt;
};

/* Sets the input source for the PWM input. For the src value see the
 * register description for gio:rw_pwm_in_cfg.
 *
 * NOTE: All PWM devices is connected to the same input source.
 */
#define IO_PWM_SET_INPUT_SRC   0x24
struct io_pwm_set_input_src {
	unsigned int src;	/* 0..7 */
};

/* Sets the duty cycles in steps of 1/256, 0 = 0%, 255 = 100% duty cycle */
#define IO_PPWM_SET_DUTY     0x25

struct io_ppwm_set_duty {
	int duty;		/* 0..255 */
};

/* Configuraton struct for the IO_PWMCLK_SET_CONFIG ioctl to configure
 * PWM capable gpio pins:
 */
#define IO_PWMCLK_SETGET_CONFIG 0x26
struct gpio_pwmclk_conf {
  unsigned int gpiopin; /* The pin number based on the opened device */
  unsigned int baseclk; /* The base clock to use, or sw will select one close*/
  unsigned int low;     /* The number of low periods of the baseclk */
  unsigned int high;    /* The number of high periods of the baseclk */
};

/* Examples:
 * To get a symmetric 12 MHz clock without knowing anything about the hardware:
 * baseclk = 12000000, low = 0, high = 0
 * To just get info of current setting:
 * baseclk = 0, low = 0, high = 0, the values will be updated by driver.
 */

#endif
