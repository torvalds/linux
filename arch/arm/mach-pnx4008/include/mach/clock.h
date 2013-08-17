/*
 * arch/arm/mach-pnx4008/include/mach/clock.h
 *
 * Clock control driver for PNX4008 - header file
 *
 * Authors: Vitaly Wool, Dmitry Chigirev <source@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __PNX4008_CLOCK_H__
#define __PNX4008_CLOCK_H__

struct module;
struct clk;

#define PWRMAN_VA_BASE		IO_ADDRESS(PNX4008_PWRMAN_BASE)
#define HCLKDIVCTRL_REG		(PWRMAN_VA_BASE + 0x40)
#define PWRCTRL_REG		(PWRMAN_VA_BASE + 0x44)
#define PLLCTRL_REG		(PWRMAN_VA_BASE + 0x48)
#define OSC13CTRL_REG		(PWRMAN_VA_BASE + 0x4c)
#define SYSCLKCTRL_REG		(PWRMAN_VA_BASE + 0x50)
#define HCLKPLLCTRL_REG		(PWRMAN_VA_BASE + 0x58)
#define USBCTRL_REG		(PWRMAN_VA_BASE + 0x64)
#define SDRAMCLKCTRL_REG	(PWRMAN_VA_BASE + 0x68)
#define MSCTRL_REG		(PWRMAN_VA_BASE + 0x80)
#define BTCLKCTRL		(PWRMAN_VA_BASE + 0x84)
#define DUMCLKCTRL_REG		(PWRMAN_VA_BASE + 0x90)
#define I2CCLKCTRL_REG		(PWRMAN_VA_BASE + 0xac)
#define KEYCLKCTRL_REG		(PWRMAN_VA_BASE + 0xb0)
#define TSCLKCTRL_REG		(PWRMAN_VA_BASE + 0xb4)
#define PWMCLKCTRL_REG		(PWRMAN_VA_BASE + 0xb8)
#define TIMCLKCTRL_REG		(PWRMAN_VA_BASE + 0xbc)
#define SPICTRL_REG		(PWRMAN_VA_BASE + 0xc4)
#define FLASHCLKCTRL_REG	(PWRMAN_VA_BASE + 0xc8)
#define UART3CLK_REG		(PWRMAN_VA_BASE + 0xd0)
#define UARTCLKCTRL_REG		(PWRMAN_VA_BASE + 0xe4)
#define DMACLKCTRL_REG		(PWRMAN_VA_BASE + 0xe8)
#define AUTOCLK_CTRL		(PWRMAN_VA_BASE + 0xec)
#define JPEGCLKCTRL_REG		(PWRMAN_VA_BASE + 0xfc)

#define AUDIOCONFIG_VA_BASE	IO_ADDRESS(PNX4008_AUDIOCONFIG_BASE)
#define DSPPLLCTRL_REG		(AUDIOCONFIG_VA_BASE + 0x60)
#define DSPCLKCTRL_REG		(AUDIOCONFIG_VA_BASE + 0x64)
#define AUDIOCLKCTRL_REG	(AUDIOCONFIG_VA_BASE + 0x68)
#define AUDIOPLLCTRL_REG	(AUDIOCONFIG_VA_BASE + 0x6C)

#define USB_OTG_CLKCTRL_REG	IO_ADDRESS(PNX4008_USB_CONFIG_BASE + 0xff4)

#define VFP9CLKCTRL_REG		IO_ADDRESS(PNX4008_DEBUG_BASE)

#define CLK_RATE_13MHZ 13000
#define CLK_RATE_1MHZ 1000
#define CLK_RATE_208MHZ 208000
#define CLK_RATE_48MHZ 48000
#define CLK_RATE_32KHZ 32

#define PNX4008_UART_CLK CLK_RATE_13MHZ * 1000 /* in MHz */

#endif
