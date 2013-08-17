#ifndef _SPEAKUP_SERIAL_H
#define _SPEAKUP_SERIAL_H

#include <linux/serial.h>	/* for rs_table, serial constants &
				   serial_uart_config */
#include <linux/serial_reg.h>	/* for more serial constants */
#ifndef __sparc__
#include <asm/serial.h>
#endif

/*
 * this is cut&paste from 8250.h. Get rid of the structure, the definitions
 * and this whole broken driver.
 */
struct old_serial_port {
	unsigned int uart; /* unused */
	unsigned int baud_base;
	unsigned int port;
	unsigned int irq;
	unsigned int flags; /* unused */
};

/* countdown values for serial timeouts in us */
#define SPK_SERIAL_TIMEOUT 100000
/* countdown values transmitter/dsr timeouts in us */
#define SPK_XMITR_TIMEOUT 100000
/* countdown values cts timeouts in us */
#define SPK_CTS_TIMEOUT 100000
/* check ttyS0 ... ttyS3 */
#define SPK_LO_TTY 0
#define SPK_HI_TTY 3
/* # of timeouts permitted before disable */
#define NUM_DISABLE_TIMEOUTS 3
/* buffer timeout in ms */
#define SPK_TIMEOUT 100
#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

#define spk_serial_tx_busy() ((inb(speakup_info.port_tts + UART_LSR) & BOTH_EMPTY) != BOTH_EMPTY)

/* 2.6.22 doesn't have them any more, hardcode it for now (these values should
 * be fine for 99% cases) */
#ifndef BASE_BAUD
#define BASE_BAUD (1843200 / 16)
#endif
#ifndef STD_COM_FLAGS
#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ)
#define STD_COM4_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)
#define STD_COM4_FLAGS ASYNC_BOOT_AUTOCONF
#endif
#endif
#ifndef SERIAL_PORT_DFNS
#define SERIAL_PORT_DFNS			\
	/* UART CLK   PORT IRQ     FLAGS        */			\
	{ 0, BASE_BAUD, 0x3F8, 4, STD_COM_FLAGS },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0x2F8, 3, STD_COM_FLAGS },	/* ttyS1 */	\
	{ 0, BASE_BAUD, 0x3E8, 4, STD_COM_FLAGS },	/* ttyS2 */	\
	{ 0, BASE_BAUD, 0x2E8, 3, STD_COM4_FLAGS },	/* ttyS3 */
#endif
#ifndef IRQF_SHARED
#define IRQF_SHARED SA_SHIRQ
#endif

#endif
