/*
 * driver for ENE KB3926 B/C/D CIR (also known as ENE0XXX)
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
#define ENE_MAX_IO		4

/* 8 bytes of samples, divided in 2 halfs*/
#define ENE_SAMPLE_BUFFER	0xF8F0	/* regular sample buffer */
#define ENE_SAMPLE_SPC_MASK	0x80	/* sample is space */
#define ENE_SAMPLE_VALUE_MASK	0x7F
#define ENE_SAMPLE_OVERFLOW	0x7F
#define ENE_SAMPLES_SIZE	4

/* fan input sample buffer */
#define ENE_SAMPLE_BUFFER_FAN	0xF8FB	/* this buffer holds high byte of */
					/* each sample of normal buffer */
#define ENE_FAN_SMPL_PULS_MSK	0x8000	/* this bit of combined sample */
					/* if set, says that sample is pulse */
#define ENE_FAN_VALUE_MASK	0x0FFF  /* mask for valid bits of the value */

/* first firmware register */
#define ENE_FW1			0xF8F8
#define	ENE_FW1_ENABLE		0x01	/* enable fw processing */
#define ENE_FW1_TXIRQ		0x02	/* TX interrupt pending */
#define ENE_FW1_WAKE		0x40	/* enable wake from S3 */
#define ENE_FW1_IRQ		0x80	/* enable interrupt */

/* second firmware register */
#define ENE_FW2			0xF8F9
#define ENE_FW2_BUF_HIGH	0x01	/* which half of the buffer to read */
#define ENE_FW2_IRQ_CLR		0x04	/* clear this on IRQ */
#define ENE_FW2_GP40_AS_LEARN	0x08	/* normal input is used as */
					/* learning input */
#define ENE_FW2_FAN_AS_NRML_IN	0x40	/* fan is used as normal input */
#define ENE_FW2_LEARNING	0x80	/* hardware supports learning and TX */

/* transmitter ports */
#define ENE_TX_PORT2		0xFC01	/* this enables one or both */
#define ENE_TX_PORT2_EN		0x20	/* TX ports */
#define ENE_TX_PORT1		0xFC08
#define ENE_TX_PORT1_EN		0x02

/* IRQ registers block (for revision B) */
#define ENEB_IRQ		0xFD09	/* IRQ number */
#define ENEB_IRQ_UNK1		0xFD17	/* unknown setting = 1 */
#define ENEB_IRQ_STATUS		0xFD80	/* irq status */
#define ENEB_IRQ_STATUS_IR	0x20	/* IR irq */

/* fan as input settings - only if learning capable */
#define ENE_FAN_AS_IN1		0xFE30  /* fan init reg 1 */
#define ENE_FAN_AS_IN1_EN	0xCD
#define ENE_FAN_AS_IN2		0xFE31  /* fan init reg 2 */
#define ENE_FAN_AS_IN2_EN	0x03
#define ENE_SAMPLE_PERIOD_FAN   61	/* fan input has fixed sample period */

/* IRQ registers block (for revision C,D) */
#define ENEC_IRQ		0xFE9B	/* new irq settings register */
#define ENEC_IRQ_MASK		0x0F	/* irq number mask */
#define ENEC_IRQ_UNK_EN		0x10	/* always enabled */
#define ENEC_IRQ_STATUS		0x20	/* irq status and ACK */

/* CIR block settings */
#define ENE_CIR_CONF1		0xFEC0
#define ENE_CIR_CONF1_TX_CLEAR	0x01	/* clear that on revC */
					/* while transmitting */
#define ENE_CIR_CONF1_RX_ON	0x07	/* normal receiver enabled */
#define ENE_CIR_CONF1_LEARN1	0x08	/* enabled on learning mode */
#define ENE_CIR_CONF1_TX_ON	0x30	/* enabled on transmit */
#define ENE_CIR_CONF1_TX_CARR	0x80	/* send TX carrier or not */

#define ENE_CIR_CONF2		0xFEC1	/* unknown setting = 0 */
#define ENE_CIR_CONF2_LEARN2	0x10	/* set on enable learning */
#define ENE_CIR_CONF2_GPIO40DIS	0x20	/* disable input via gpio40 */

#define ENE_CIR_SAMPLE_PERIOD	0xFEC8	/* sample period in us */
#define ENE_CIR_SAMPLE_OVERFLOW	0x80	/* interrupt on overflows if set */


/* Two byte tx buffer */
#define ENE_TX_INPUT1		0xFEC9
#define ENE_TX_INPUT2		0xFECA
#define ENE_TX_PULSE_MASK	0x80	/* Transmitted sample is pulse */
#define ENE_TX_SMLP_MASK	0x7F
#define ENE_TX_SMPL_PERIOD	50	/* transmit sample period - fixed */


/* Unknown TX setting - TX sample period ??? */
#define ENE_TX_UNK1		0xFECB	/* set to 0x63 */

/* Current received carrier period */
#define ENE_RX_CARRIER		0xFECC	/* RX period (500 ns) */
#define ENE_RX_CARRIER_VALID	0x80	/* Register content valid */


/* TX period (1/carrier) */
#define ENE_TX_PERIOD		0xFECE	/* TX period (500 ns) */
#define ENE_TX_PERIOD_UNKBIT	0x80	/* This bit set on transmit*/
#define ENE_TX_PERIOD_PULSE	0xFECF	/* TX pulse period (500 ns)*/

/* Hardware versions */
#define ENE_HW_VERSION		0xFF00	/* hardware revision */
#define ENE_PLLFRH		0xFF16
#define ENE_PLLFRL		0xFF17

#define ENE_HW_UNK		0xFF1D
#define ENE_HW_UNK_CLR		0x04
#define ENE_HW_VER_MAJOR	0xFF1E	/* chip version */
#define ENE_HW_VER_MINOR	0xFF1F
#define ENE_HW_VER_OLD		0xFD00

/* Normal/Learning carrier ranges - only valid if we have learning input*/
/* TODO: test */
#define ENE_NORMAL_RX_LOW	34
#define ENE_NORMAL_RX_HI	38

/* Tx carrier range */
/* Hardware might be able to do more, but this range is enough for
   all purposes */
#define ENE_TX_PERIOD_MAX	32	/* corresponds to 29.4 kHz */
#define ENE_TX_PERIOD_MIN	16	/* corrsponds to 62.5 kHz */



/* Minimal and maximal gaps */

/* Normal case:
	Minimal gap is 0x7F * sample period
	Maximum gap depends on hardware.
	For KB3926B, it is unlimited, for newer models its around
	250000, after which HW stops sending samples, and that is
	not possible to change */

/* Fan case:
	Both minimal and maximal gaps are same, and equal to 0xFFF * 0x61
	And there is nothing to change this setting
*/

#define ENE_MAXGAP		250000
#define ENE_MINGAP		(127 * sample_period)

/******************************************************************************/

#define ENE_DRIVER_NAME		"ene_ir"

#define ENE_IRQ_RX		1
#define ENE_IRQ_TX		2

#define  ENE_HW_B		1	/* 3926B */
#define  ENE_HW_C		2	/* 3926C */
#define  ENE_HW_D		3	/* 3926D */

#define ene_printk(level, text, ...) \
	printk(level ENE_DRIVER_NAME ": " text, ## __VA_ARGS__)

#define ene_dbg(text, ...) \
	if (debug) \
		printk(KERN_DEBUG \
			ENE_DRIVER_NAME ": " text "\n" , ## __VA_ARGS__)

#define ene_dbg_verbose(text, ...) \
	if (debug > 1) \
		printk(KERN_DEBUG \
			ENE_DRIVER_NAME ": " text "\n" , ## __VA_ARGS__)


struct ene_device {
	struct pnp_dev *pnp_dev;
	struct input_dev *idev;
	struct ir_dev_props *props;
	int in_use;

	/* hw IO settings */
	unsigned long hw_io;
	int irq;
	spinlock_t hw_lock;

	/* HW features */
	int hw_revision;			/* hardware revision */
	bool hw_learning_and_tx_capable;	/* learning capable */
	bool hw_gpio40_learning;		/* gpio40 is learning */
	bool hw_fan_as_normal_input;		/* fan input is used as */
						/* regular input */
	/* HW state*/
	int rx_pointer;				/* hw pointer to rx buffer */
	bool rx_fan_input_inuse;		/* is fan input in use for rx*/
	int tx_reg;				/* current reg used for TX */
	u8  saved_conf1;			/* saved FEC0 reg */

	/* TX sample handling */
	unsigned int tx_sample;			/* current sample for TX */
	bool tx_sample_pulse;			/* current sample is pulse */

	/* TX buffer */
	int *tx_buffer;				/* input samples buffer*/
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
	bool learning_enabled;			/* learning input enabled */
	bool carrier_detect_enabled;		/* carrier detect enabled */
	int rx_period_adjust;
};
