#ifndef _INCLUDE_SHANNON_H
#define _INCLUDE_SHANNON_H

/* taken from comp.os.inferno Tue, 12 Sep 2000 09:21:50 GMT,
 * written by <forsyth@vitanuova.com> */

#define SHANNON_GPIO_SPI_FLASH		GPIO_GPIO (0)	/* Output - Driven low, enables SPI to flash */
#define SHANNON_GPIO_SPI_DSP		GPIO_GPIO (1)	/* Output - Driven low, enables SPI to DSP */
/* lcd lower = GPIO 2-9 */
#define SHANNON_GPIO_SPI_OUTPUT		GPIO_GPIO (10)	/* Output - SPI output to DSP */
#define SHANNON_GPIO_SPI_INPUT		GPIO_GPIO (11)	/* Input  - SPI input from DSP */
#define SHANNON_GPIO_SPI_CLOCK		GPIO_GPIO (12)	/* Output - Clock for SPI */
#define SHANNON_GPIO_SPI_FRAME		GPIO_GPIO (13)	/* Output - Frame marker - not used */
#define SHANNON_GPIO_SPI_RTS		GPIO_GPIO (14)	/* Input  - SPI Ready to Send */
#define SHANNON_IRQ_GPIO_SPI_RTS	IRQ_GPIO14
#define SHANNON_GPIO_SPI_CTS		GPIO_GPIO (15)	/* Output - SPI Clear to Send */
#define SHANNON_GPIO_IRQ_CODEC		GPIO_GPIO (16)	/* in, irq from ucb1200 */
#define SHANNON_IRQ_GPIO_IRQ_CODEC	IRQ_GPIO16
#define SHANNON_GPIO_DSP_RESET		GPIO_GPIO (17)	/* Output - Drive low to reset the DSP */
#define SHANNON_GPIO_CODEC_RESET	GPIO_GPIO (18)	/* Output - Drive low to reset the UCB1x00 */
#define SHANNON_GPIO_U3_RTS		GPIO_GPIO (19)	/* ?? */
#define SHANNON_GPIO_U3_CTS		GPIO_GPIO (20)	/* ?? */
#define SHANNON_GPIO_SENSE_12V		GPIO_GPIO (21)	/* Input, 12v flash unprotect detected */
#define SHANNON_GPIO_DISP_EN		GPIO_GPIO (22)	/* out */
/* XXX GPIO 23 unaccounted for */
#define SHANNON_GPIO_EJECT_0		GPIO_GPIO (24)	/* in */
#define SHANNON_IRQ_GPIO_EJECT_0	IRQ_GPIO24
#define SHANNON_GPIO_EJECT_1		GPIO_GPIO (25)	/* in */
#define SHANNON_IRQ_GPIO_EJECT_1	IRQ_GPIO25
#define SHANNON_GPIO_RDY_0		GPIO_GPIO (26)	/* in */
#define SHANNON_IRQ_GPIO_RDY_0		IRQ_GPIO26
#define SHANNON_GPIO_RDY_1		GPIO_GPIO (27)	/* in */
#define SHANNON_IRQ_GPIO_RDY_1		IRQ_GPIO27

/* MCP UCB codec GPIO pins... */

#define SHANNON_UCB_GPIO_BACKLIGHT	9
#define SHANNON_UCB_GPIO_BRIGHT_MASK  	7
#define SHANNON_UCB_GPIO_BRIGHT		6
#define SHANNON_UCB_GPIO_CONTRAST_MASK	0x3f
#define SHANNON_UCB_GPIO_CONTRAST	0

#endif
