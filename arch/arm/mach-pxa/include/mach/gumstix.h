/*
 *  arch/arm/mach-pxa/include/mach/gumstix.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


/* BTRESET - Reset line to Bluetooth module, active low signal. */
#define GPIO_GUMSTIX_BTRESET          7
#define GPIO_GUMSTIX_BTRESET_MD		(GPIO_GUMSTIX_BTRESET | GPIO_OUT)


/*
GPIOn - Input from MAX823 (or equiv), normalizing USB +5V into a clean
interrupt signal for determining cable presence. On the gumstix F,
this moves to GPIO17 and GPIO37. */

/* GPIOx - Connects to USB D+ and used as a pull-up after GPIOn
has detected a cable insertion; driven low otherwise. */

#define GPIO_GUMSTIX_USB_GPIOn		35
#define GPIO_GUMSTIX_USB_GPIOx		41

/* usb state change */
#define GUMSTIX_USB_INTR_IRQ		IRQ_GPIO(GPIO_GUMSTIX_USB_GPIOn)

#define GPIO_GUMSTIX_USB_GPIOn_MD	(GPIO_GUMSTIX_USB_GPIOn | GPIO_IN)
#define GPIO_GUMSTIX_USB_GPIOx_CON_MD	(GPIO_GUMSTIX_USB_GPIOx | GPIO_OUT)
#define GPIO_GUMSTIX_USB_GPIOx_DIS_MD	(GPIO_GUMSTIX_USB_GPIOx | GPIO_IN)

/*
 * SD/MMC definitions
 */
#define GUMSTIX_GPIO_nSD_WP		22 /* SD Write Protect */
#define GUMSTIX_GPIO_nSD_DETECT		11 /* MMC/SD Card Detect */
#define GUMSTIX_IRQ_GPIO_nSD_DETECT	IRQ_GPIO(GUMSTIX_GPIO_nSD_DETECT)

/*
 * SMC Ethernet definitions
 * ETH_RST provides a hardware reset line to the ethernet chip
 * ETH is the IRQ line in from the ethernet chip to the PXA
 */
#define GPIO_GUMSTIX_ETH0_RST		80
#define GPIO_GUMSTIX_ETH0_RST_MD	(GPIO_GUMSTIX_ETH0_RST | GPIO_OUT)
#define GPIO_GUMSTIX_ETH1_RST		52
#define GPIO_GUMSTIX_ETH1_RST_MD	(GPIO_GUMSTIX_ETH1_RST | GPIO_OUT)

#define GPIO_GUMSTIX_ETH0		36
#define GPIO_GUMSTIX_ETH0_MD		(GPIO_GUMSTIX_ETH0 | GPIO_IN)
#define GUMSTIX_ETH0_IRQ		IRQ_GPIO(GPIO_GUMSTIX_ETH0)
#define GPIO_GUMSTIX_ETH1		27
#define GPIO_GUMSTIX_ETH1_MD		(GPIO_GUMSTIX_ETH1 | GPIO_IN)
#define GUMSTIX_ETH1_IRQ		IRQ_GPIO(GPIO_GUMSTIX_ETH1)


/* CF reset line */
#define GPIO8_RESET			8

/* CF slot 0 */
#define GPIO4_nBVD1			4
#define GPIO4_nSTSCHG			GPIO4_nBVD1
#define GPIO11_nCD			11
#define GPIO26_PRDY_nBSY		26
#define GUMSTIX_S0_nSTSCHG_IRQ		IRQ_GPIO(GPIO4_nSTSCHG)
#define GUMSTIX_S0_nCD_IRQ		IRQ_GPIO(GPIO11_nCD)
#define GUMSTIX_S0_PRDY_nBSY_IRQ	IRQ_GPIO(GPIO26_PRDY_nBSY)

/* CF slot 1 */
#define GPIO18_nBVD1			18
#define GPIO18_nSTSCHG			GPIO18_nBVD1
#define GPIO36_nCD			36
#define GPIO27_PRDY_nBSY		27
#define GUMSTIX_S1_nSTSCHG_IRQ		IRQ_GPIO(GPIO18_nSTSCHG)
#define GUMSTIX_S1_nCD_IRQ		IRQ_GPIO(GPIO36_nCD)
#define GUMSTIX_S1_PRDY_nBSY_IRQ	IRQ_GPIO(GPIO27_PRDY_nBSY)

/* CF GPIO line modes */
#define GPIO4_nSTSCHG_MD		(GPIO4_nSTSCHG | GPIO_IN)
#define GPIO8_RESET_MD			(GPIO8_RESET | GPIO_OUT)
#define GPIO11_nCD_MD			(GPIO11_nCD | GPIO_IN)
#define GPIO18_nSTSCHG_MD		(GPIO18_nSTSCHG | GPIO_IN)
#define GPIO26_PRDY_nBSY_MD		(GPIO26_PRDY_nBSY | GPIO_IN)
#define GPIO27_PRDY_nBSY_MD		(GPIO27_PRDY_nBSY | GPIO_IN)
#define GPIO36_nCD_MD			(GPIO36_nCD | GPIO_IN)

/* for expansion boards that can't be programatically detected */
extern int am200_init(void);
extern int am300_init(void);

