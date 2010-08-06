/*
 * driver for ENE KB3926 B/C/D CIR (also known as ENE0100)
 *
 * Copyright (C) 2009 Maxim Levitsky <maximlevitsky@gmail.com>
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

#include <media/lirc.h>
#include <media/lirc_dev.h>

/* hardware address */
#define ENE_STATUS		0	 /* hardware status - unused */
#define ENE_ADDR_HI		1	 /* hi byte of register address */
#define ENE_ADDR_LO		2	 /* low byte of register address */
#define ENE_IO			3	 /* read/write window */
#define ENE_MAX_IO		4

/* 8 bytes of samples, divided in 2 halfs*/
#define ENE_SAMPLE_BUFFER	0xF8F0	 /* regular sample buffer */
#define ENE_SAMPLE_SPC_MASK	(1 << 7) /* sample is space */
#define ENE_SAMPLE_VALUE_MASK	0x7F
#define ENE_SAMPLE_OVERFLOW	0x7F
#define ENE_SAMPLES_SIZE	4

/* fan input sample buffer */
#define ENE_SAMPLE_BUFFER_FAN	0xF8FB	 /* this buffer holds high byte of */
					 /* each sample of normal buffer */

#define ENE_FAN_SMPL_PULS_MSK	0x8000	 /* this bit of combined sample */
					 /* if set, says that sample is pulse */
#define ENE_FAN_VALUE_MASK	0x0FFF   /* mask for valid bits of the value */

/* first firmware register */
#define ENE_FW1			0xF8F8
#define	ENE_FW1_ENABLE		(1 << 0) /* enable fw processing */
#define ENE_FW1_TXIRQ		(1 << 1) /* TX interrupt pending */
#define ENE_FW1_WAKE		(1 << 6) /* enable wake from S3 */
#define ENE_FW1_IRQ		(1 << 7) /* enable interrupt */

/* second firmware register */
#define ENE_FW2			0xF8F9
#define ENE_FW2_BUF_HIGH	(1 << 0) /* which half of the buffer to read */
#define ENE_FW2_IRQ_CLR		(1 << 2) /* clear this on IRQ */
#define ENE_FW2_GP40_AS_LEARN	(1 << 4) /* normal input is used as */
					 /* learning input */
#define ENE_FW2_FAN_AS_NRML_IN	(1 << 6) /* fan is used as normal input */
#define ENE_FW2_LEARNING	(1 << 7) /* hardware supports learning and TX */

/* fan as input settings - only if learning capable */
#define ENE_FAN_AS_IN1		0xFE30   /* fan init reg 1 */
#define ENE_FAN_AS_IN1_EN	0xCD
#define ENE_FAN_AS_IN2		0xFE31   /* fan init reg 2 */
#define ENE_FAN_AS_IN2_EN	0x03
#define ENE_SAMPLE_PERIOD_FAN   61	 /* fan input has fixed sample period */

/* IRQ registers block (for revision B) */
#define ENEB_IRQ		0xFD09	 /* IRQ number */
#define ENEB_IRQ_UNK1		0xFD17	 /* unknown setting = 1 */
#define ENEB_IRQ_STATUS		0xFD80	 /* irq status */
#define ENEB_IRQ_STATUS_IR	(1 << 5) /* IR irq */

/* IRQ registers block (for revision C,D) */
#define ENEC_IRQ		0xFE9B	 /* new irq settings register */
#define ENEC_IRQ_MASK		0x0F	 /* irq number mask */
#define ENEC_IRQ_UNK_EN		(1 << 4) /* always enabled */
#define ENEC_IRQ_STATUS		(1 << 5) /* irq status and ACK */

/* CIR block settings */
#define ENE_CIR_CONF1		0xFEC0
#define ENE_CIR_CONF1_ADC_ON	0x7	 /* reciever on gpio40 enabled */
#define ENE_CIR_CONF1_LEARN1	(1 << 3) /* enabled on learning mode */
#define ENE_CIR_CONF1_TX_ON	0x30	 /* enabled on transmit */
#define ENE_CIR_CONF1_TX_CARR	(1 << 7) /* send TX carrier or not */

#define ENE_CIR_CONF2		0xFEC1	 /* unknown setting = 0 */
#define ENE_CIR_CONF2_LEARN2	(1 << 4) /* set on enable learning */
#define ENE_CIR_CONF2_GPIO40DIS	(1 << 5) /* disable normal input via gpio40 */

#define ENE_CIR_SAMPLE_PERIOD	0xFEC8	 /* sample period in us */
#define ENE_CIR_SAMPLE_OVERFLOW	(1 << 7) /* interrupt on overflows if set */


/* transmitter - not implemented yet */
/* KB3926C and higher */
/* transmission is very similiar to recieving, a byte is written to */
/* ENE_TX_INPUT, in same manner as it is read from sample buffer */
/* sample period is fixed*/


/* transmitter ports */
#define ENE_TX_PORT1		0xFC01	 /* this enables one or both */
#define ENE_TX_PORT1_EN		(1 << 5) /* TX ports */
#define ENE_TX_PORT2		0xFC08
#define ENE_TX_PORT2_EN		(1 << 1)

#define ENE_TX_INPUT		0xFEC9	 /* next byte to transmit */
#define ENE_TX_SPC_MASK		(1 << 7) /* Transmitted sample is space */
#define ENE_TX_UNK1		0xFECB	 /* set to 0x63 */
#define ENE_TX_SMPL_PERIOD	50	 /* transmit sample period */


#define ENE_TX_CARRIER		0xFECE	 /* TX carrier * 2 (khz) */
#define ENE_TX_CARRIER_UNKBIT	0x80	 /* This bit set on transmit */
#define ENE_TX_CARRIER_LOW	0xFECF	 /* TX carrier / 2 */

/* Hardware versions */
#define ENE_HW_VERSION		0xFF00	 /* hardware revision */
#define ENE_HW_UNK		0xFF1D
#define ENE_HW_UNK_CLR		(1 << 2)
#define ENE_HW_VER_MAJOR	0xFF1E	 /* chip version */
#define ENE_HW_VER_MINOR	0xFF1F
#define ENE_HW_VER_OLD		0xFD00

#define same_sign(a, b) ((((a) > 0) && (b) > 0) || ((a) < 0 && (b) < 0))

#define ENE_DRIVER_NAME		"enecir"
#define ENE_MAXGAP		250000	 /* this is amount of time we wait
					 before turning the sampler, chosen
					 arbitry */

#define space(len)	       (-(len))	 /* add a space */

/* software defines */
#define ENE_IRQ_RX		1
#define ENE_IRQ_TX		2

#define  ENE_HW_B		1	/* 3926B */
#define  ENE_HW_C		2	/* 3926C */
#define  ENE_HW_D		3	/* 3926D */

#define ene_printk(level, text, ...) \
	printk(level ENE_DRIVER_NAME ": " text, ## __VA_ARGS__)

struct ene_device {
	struct pnp_dev *pnp_dev;
	struct lirc_driver *lirc_driver;

	/* hw settings */
	unsigned long hw_io;
	int irq;

	int hw_revision;			/* hardware revision */
	int hw_learning_and_tx_capable;		/* learning capable */
	int hw_gpio40_learning;			/* gpio40 is learning */
	int hw_fan_as_normal_input;	/* fan input is used as regular input */

	/* device data */
	int idle;
	int fan_input_inuse;

	int sample;
	int in_use;

	struct timeval gap_start;
};
