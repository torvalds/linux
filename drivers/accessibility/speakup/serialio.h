/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPEAKUP_SERIAL_H
#define _SPEAKUP_SERIAL_H

#include <linux/serial.h>	/* for rs_table, serial constants */
#include <linux/serial_reg.h>	/* for more serial constants */
#include <linux/serial_core.h>

#include "spk_priv.h"

/*
 * this is cut&paste from 8250.h. Get rid of the structure, the definitions
 * and this whole broken driver.
 */
struct old_serial_port {
	unsigned int uart; /* unused */
	unsigned int baud_base;
	unsigned int port;
	unsigned int irq;
	upf_t flags; /* unused */
};

/* countdown values for serial timeouts in us */
#define SPK_SERIAL_TIMEOUT SPK_SYNTH_TIMEOUT
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

#define spk_serial_tx_busy() \
	((inb(speakup_info.port_tts + UART_LSR) & BOTH_EMPTY) != BOTH_EMPTY)

#endif
