/*
 * driver for ENE KB3926 B/C/D/E/F CIR (also known as ENE0XXX)
 *
 * Copyright (C) 2010 Maxim Levitsky <maximlevitsky@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <linux/spinlock.h>


/* hardware address */
#define ENE_STATUS		0	/* hardware status - unused */
#define ENE_ADDR_HI		1	/* hi byte of register address */
#define ENE_ADDR_LO		2	/* low byte of register address */
#define ENE_IO			3	/* read/write window */
#define ENE_IO_SIZE		4

/* 8 bytes of samples, divided in 2 packets*/
#define ENE_FW_SAMPLE_BUFFER	0xF8F0	/* sample buffer */
#define ENE_FW_SAMPLE_SPACE	0x80	/* sample is space */
#define ENE_FW_PACKET_SIZE	4

/* first firmware flag register */
#define ENE_FW1			0xF8F8  /* flagr */
#define	ENE_FW1_ENABLE		0x01	/* enable fw processing */
#define ENE_FW1_TXIRQ		0x02	/* TX interrupt pending */
#define ENE_FW1_HAS_EXTRA_BUF	0x04	/* fw uses extra buffer*/
#define ENE_FW1_EXTRA_BUF_HND	0x08	/* extra buffer handshake bit*/
#define ENE_FW1_LED_ON		0x10	/* turn on a led */

#define ENE_FW1_WPATTERN	0x20	/* enable wake pattern */
#define ENE_FW1_WAKE		0x40	/* enable wake from S3 */
#define ENE_FW1_IRQ		0x80	/* enable interrupt */

/* second firmware flag register */
#define ENE_FW2			0xF8F9  /* flagw */
#define ENE_FW2_BUF_WPTR	0x01	/* which half of the buffer to read */
#define ENE_FW2_RXIRQ		0x04	/* RX IRQ pending*/
#define ENE_FW2_GP0A		0x08	/* Use GPIO0A for demodulated input */
#define ENE_FW2_EMMITER1_CONN	0x10	/* TX emmiter 1 connected */
#define ENE_FW2_EMMITER2_CONN	0x20	/* TX emmiter 2 connected */

#define ENE_FW2_FAN_INPUT	0x40	/* fan input used for demodulated data*/
#define ENE_FW2_LEARNING	0x80	/* hardware supports learning and TX */

/* firmware RX pointer for new style buffer */
#define ENE_FW_RX_POINTER	0xF8FA

/* high parts of samples for fan input (8 samples)*/
#define ENE_FW_SMPL_BUF_FAN	0xF8FB
#define ENE_FW_SMPL_BUF_FAN_PLS	0x8000	/* combined sample is pulse */
#define ENE_FW_SMPL_BUF_FAN_MSK	0x0FFF  /* combined sample maximum value */
#define ENE_FW_SAMPLE_PERIOD_FAN 61	/* fan input has fixed sample period */

/* transmitter ports */
#define ENE_GPIOFS1		0xFC01
#define ENE_GPIOFS1_GPIO0D	0x20	/* enable tx output on GPIO0D */
#define ENE_GPIOFS8		0xFC08
#define ENE_GPIOFS8_GPIO41	0x02	/* enable tx output on GPIO40 */

/* IRQ registers block (for revision B) */
#define ENEB_IRQ		0xFD09	/* IRQ number */
#define ENEB_IRQ_UNK1		0xFD17	/* unknown setting = 1 */
#define ENEB_IRQ_STATUS		0xFD80	/* irq status */
#define ENEB_IRQ_STATUS_IR	0x20	/* IR irq */

/* fan as input settings */
#define ENE_FAN_AS_IN1		0xFE30  /* fan init reg 1 */
#define ENE_FAN_AS_IN1_EN	0xCD
#define ENE_FAN_AS_IN2		0xFE31  /* fan init reg 2 */
#define ENE_FAN_AS_IN2_EN	0x03

/* IRQ registers block (for revision C,D) */
#define ENE_IRQ			0xFE9B	/* new irq settings register */
#define ENE_IRQ_MASK		0x0F	/* irq number mask */
#define ENE_IRQ_UNK_EN		0x10	/* always enabled */
#define ENE_IRQ_STATUS		0x20	/* irq status and ACK */

/* CIR Config register #1 */
#define ENE_CIRCFG		0xFEC0
#define ENE_CIRCFG_RX_EN	0x01	/* RX enable */
#define ENE_CIRCFG_RX_IRQ	0x02	/* Enable hardware interrupt */
#define ENE_CIRCFG_REV_POL	0x04	/* Input polarity reversed */
#define ENE_CIRCFG_CARR_DEMOD	0x08	/* Enable carrier demodulator */

#define ENE_CIRCFG_TX_EN	0x10	/* TX enable */
#define ENE_CIRCFG_TX_IRQ	0x20	/* Send interrupt on TX done */
#define ENE_CIRCFG_TX_POL_REV	0x40	/* TX polarity reversed */
#define ENE_CIRCFG_TX_CARR	0x80	/* send TX carrier or not */

/* CIR config register #2 */
#define ENE_CIRCFG2		0xFEC1
#define ENE_CIRCFG2_RLC		0x00
#define ENE_CIRCFG2_RC5		0x01
#define ENE_CIRCFG2_RC6		0x02
#define ENE_CIRCFG2_NEC		0x03
#define ENE_CIRCFG2_CARR_DETECT	0x10	/* Enable carrier detection */
#define ENE_CIRCFG2_GPIO0A	0x20	/* Use GPIO0A instead of GPIO40 for input */
#define ENE_CIRCFG2_FAST_SAMPL1	0x40	/* Fast leading pulse detection for RC6 */
#define ENE_CIRCFG2_FAST_SAMPL2	0x80	/* Fast data detection for RC6 */

/* Knobs for protocol decoding - will document when/if will use them */
#define ENE_CIRPF		0xFEC2
#define ENE_CIRHIGH		0xFEC3
#define ENE_CIRBIT		0xFEC4
#define ENE_CIRSTART		0xFEC5
#define ENE_CIRSTART2		0xFEC6

/* Actual register which contains RLC RX data - read by firmware */
#define ENE_CIRDAT_IN		0xFEC7


/* RLC configuration - sample period (1us resulution) + idle mode */
#define ENE_CIRRLC_CFG		0xFEC8
#define ENE_CIRRLC_CFG_OVERFLOW	0x80	/* interrupt on overflows if set */
#define ENE_DEFAULT_SAMPLE_PERIOD 50

/* Two byte RLC TX buffer */
#define ENE_CIRRLC_OUT0		0xFEC9
#define ENE_CIRRLC_OUT1		0xFECA
#define ENE_CIRRLC_OUT_PULSE	0x80	/* Transmitted sample is pulse */
#define ENE_CIRRLC_OUT_MASK	0x7F


/* Carrier detect setting
 * Low nibble  - number of carrier pulses to average
 * High nibble - number of initial carrier pulses to discard
 */
#define ENE_CIRCAR_PULS		0xFECB

/* detected RX carrier period (resolution: 500 ns) */
#define ENE_CIRCAR_PRD		0xFECC
#define ENE_CIRCAR_PRD_VALID	0x80	/* data valid content valid */

/* detected RX carrier pulse width (resolution: 500 ns) */
#define ENE_CIRCAR_HPRD		0xFECD

/* TX period (resolution: 500 ns, minimum 2)*/
#define ENE_CIRMOD_PRD		0xFECE
#define ENE_CIRMOD_PRD_POL	0x80	/* TX carrier polarity*/

#define ENE_CIRMOD_PRD_MAX	0x7F	/* 15.87 kHz */
#define ENE_CIRMOD_PRD_MIN	0x02	/* 1 Mhz */

/* TX pulse width (resolution: 500 ns)*/
#define ENE_CIRMOD_HPRD		0xFECF

/* Hardware versions */
#define ENE_ECHV		0xFF00	/* hardware revision */
#define ENE_PLLFRH		0xFF16
#define ENE_PLLFRL		0xFF17
#define ENE_DEFAULT_PLL_FREQ	1000

#define ENE_ECSTS		0xFF1D
#define ENE_ECSTS_RSRVD		0x04

#define ENE_ECVER_MAJOR		0xFF1E	/* chip version */
#define ENE_ECVER_MINOR		0xFF1F
#define ENE_HW_VER_OLD		0xFD00

/******************************************************************************/

#define ENE_DRIVER_NAME		"ene_ir"

#define ENE_IRQ_RX		1
#define ENE_IRQ_TX		2

#define  ENE_HW_B		1	/* 3926B */
#define  ENE_HW_C		2	/* 3926C */
#define  ENE_HW_D		3	/* 3926D or later */

#define __dbg(level, format, ...)				\
do {								\
	if (debug >= level)					\
		pr_debug(format "\n", ## __VA_ARGS__);		\
} while (0)

#define dbg(format, ...)		__dbg(1, format, ## __VA_ARGS__)
#define dbg_verbose(format, ...)	__dbg(2, format, ## __VA_ARGS__)
#define dbg_regs(format, ...)		__dbg(3, format, ## __VA_ARGS__)

struct ene_device {
	struct pnp_dev *pnp_dev;
	struct rc_dev *rdev;

	/* hw IO settings */
	long hw_io;
	int irq;
	spinlock_t hw_lock;

	/* HW features */
	int hw_revision;			/* hardware revision */
	bool hw_use_gpio_0a;			/* gpio0a is demodulated input*/
	bool hw_extra_buffer;			/* hardware has 'extra buffer' */
	bool hw_fan_input;			/* fan input is IR data source */
	bool hw_learning_and_tx_capable;	/* learning & tx capable */
	int  pll_freq;
	int buffer_len;

	/* Extra RX buffer location */
	int extra_buf1_address;
	int extra_buf1_len;
	int extra_buf2_address;
	int extra_buf2_len;

	/* HW state*/
	int r_pointer;				/* pointer to next sample to read */
	int w_pointer;				/* pointer to next sample hw will write */
	bool rx_fan_input_inuse;		/* is fan input in use for rx*/
	int tx_reg;				/* current reg used for TX */
	u8  saved_conf1;			/* saved FEC0 reg */
	unsigned int tx_sample;			/* current sample for TX */
	bool tx_sample_pulse;			/* current sample is pulse */

	/* TX buffer */
	unsigned *tx_buffer;			/* input samples buffer*/
	int tx_pos;				/* position in that bufer */
	int tx_len;				/* current len of tx buffer */
	int tx_done;				/* done transmitting */
						/* one more sample pending*/
	struct completion tx_complete;		/* TX completion */
	struct timer_list tx_sim_timer;

	/* TX settings */
	int tx_period;
	int tx_duty_cycle;
	int transmitter_mask;

	/* RX settings */
	bool learning_mode_enabled;		/* learning input enabled */
	bool carrier_detect_enabled;		/* carrier detect enabled */
	int rx_period_adjust;
	bool rx_enabled;
};

static int ene_irq_status(struct ene_device *dev);
static void ene_rx_read_hw_pointer(struct ene_device *dev);
