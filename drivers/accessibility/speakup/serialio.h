/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPEAKUP_SERIAL_H
#define _SPEAKUP_SERIAL_H

#include <linux/serial.h>	/* for rs_table, serial constants */
#include <linux/serial_reg.h>	/* for more serial constants */
#include <linux/serial_core.h>

#include "spk_priv.h"

/* countdown values for serial timeouts in us */
#define SPK_SERIAL_TIMEOUT SPK_SYNTH_TIMEOUT
/* countdown values transmitter/dsr timeouts in us */
#define SPK_XMITR_TIMEOUT 100000
/* countdown values cts timeouts in us */
#define SPK_CTS_TIMEOUT 100000
/* # of timeouts permitted before disable */
#define NUM_DISABLE_TIMEOUTS 3

#define spk_serial_tx_busy() \
	(!uart_lsr_tx_empty(inb(speakup_info.port_tts + UART_LSR)))

#endif
