/*
 * Driver for ITE Tech Inc. IT8712F/IT8512F CIR
 *
 * Copyright (C) 2010 Juan Jesús García de Soria <skandalfo@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA.
 */

/* platform driver name to register */
#define ITE_DRIVER_NAME "ite-cir"

/* logging macros */
#define ite_pr(level, text, ...) \
	printk(level KBUILD_MODNAME ": " text, ## __VA_ARGS__)
#define ite_dbg(text, ...) do { \
	if (debug) \
		printk(KERN_DEBUG \
			KBUILD_MODNAME ": " text "\n" , ## __VA_ARGS__); \
} while (0)

#define ite_dbg_verbose(text, ...) do {\
	if (debug > 1) \
		printk(KERN_DEBUG \
			KBUILD_MODNAME ": " text "\n" , ## __VA_ARGS__); \
} while (0)

/* FIFO sizes */
#define ITE_TX_FIFO_LEN 32
#define ITE_RX_FIFO_LEN 32

/* interrupt types */
#define ITE_IRQ_TX_FIFO        1
#define ITE_IRQ_RX_FIFO        2
#define ITE_IRQ_RX_FIFO_OVERRUN    4

/* forward declaration */
struct ite_dev;

/* struct for storing the parameters of different recognized devices */
struct ite_dev_params {
	/* model of the device */
	const char *model;

	/* size of the I/O region */
	int io_region_size;

	/* IR pnp I/O resource number */
	int io_rsrc_no;

	/* true if the hardware supports transmission */
	bool hw_tx_capable;

	/* base sampling period, in ns */
	u32 sample_period;

	/* rx low carrier frequency, in Hz, 0 means no demodulation */
	unsigned int rx_low_carrier_freq;

	/* tx high carrier frequency, in Hz, 0 means no demodulation */
	unsigned int rx_high_carrier_freq;

	/* tx carrier frequency, in Hz */
	unsigned int tx_carrier_freq;

	/* duty cycle, 0-100 */
	int tx_duty_cycle;

	/* hw-specific operation function pointers; most of these must be
	 * called while holding the spin lock, except for the TX FIFO length
	 * one */
	/* get pending interrupt causes */
	int (*get_irq_causes) (struct ite_dev *dev);

	/* enable rx */
	void (*enable_rx) (struct ite_dev *dev);

	/* make rx enter the idle state; keep listening for a pulse, but stop
	 * streaming space bytes */
	void (*idle_rx) (struct ite_dev *dev);

	/* disable rx completely */
	void (*disable_rx) (struct ite_dev *dev);

	/* read bytes from RX FIFO; return read count */
	int (*get_rx_bytes) (struct ite_dev *dev, u8 *buf, int buf_size);

	/* enable tx FIFO space available interrupt */
	void (*enable_tx_interrupt) (struct ite_dev *dev);

	/* disable tx FIFO space available interrupt */
	void (*disable_tx_interrupt) (struct ite_dev *dev);

	/* get number of full TX FIFO slots */
	int (*get_tx_used_slots) (struct ite_dev *dev);

	/* put a byte to the TX FIFO */
	void (*put_tx_byte) (struct ite_dev *dev, u8 value);

	/* disable hardware completely */
	void (*disable) (struct ite_dev *dev);

	/* initialize the hardware */
	void (*init_hardware) (struct ite_dev *dev);

	/* set the carrier parameters */
	void (*set_carrier_params) (struct ite_dev *dev, bool high_freq,
				    bool use_demodulator, u8 carrier_freq_bits,
				    u8 allowance_bits, u8 pulse_width_bits);
};

/* ITE CIR device structure */
struct ite_dev {
	struct pnp_dev *pdev;
	struct rc_dev *rdev;
	struct ir_raw_event rawir;

	/* sync data */
	spinlock_t lock;
	bool in_use, transmitting;

	/* transmit support */
	int tx_fifo_allowance;
	wait_queue_head_t tx_queue, tx_ended;

	/* hardware I/O settings */
	unsigned long cir_addr;
	int cir_irq;

	/* overridable copy of model parameters */
	struct ite_dev_params params;
};

/* common values for all kinds of hardware */

/* baud rate divisor default */
#define ITE_BAUDRATE_DIVISOR		1

/* low-speed carrier frequency limits (Hz) */
#define ITE_LCF_MIN_CARRIER_FREQ	27000
#define ITE_LCF_MAX_CARRIER_FREQ	58000

/* high-speed carrier frequency limits (Hz) */
#define ITE_HCF_MIN_CARRIER_FREQ	400000
#define ITE_HCF_MAX_CARRIER_FREQ	500000

/* default carrier freq for when demodulator is off (Hz) */
#define ITE_DEFAULT_CARRIER_FREQ	38000

/* default idling timeout in ns (0.2 seconds) */
#define ITE_IDLE_TIMEOUT		200000000UL

/* limit timeout values */
#define ITE_MIN_IDLE_TIMEOUT		100000000UL
#define ITE_MAX_IDLE_TIMEOUT		1000000000UL

/* convert bits to us */
#define ITE_BITS_TO_NS(bits, sample_period) \
((u32) ((bits) * ITE_BAUDRATE_DIVISOR * sample_period))

/*
 * n in RDCR produces a tolerance of +/- n * 6.25% around the center
 * carrier frequency...
 *
 * From two limit frequencies, L (low) and H (high), we can get both the
 * center frequency F = (L + H) / 2 and the variation from the center
 * frequency A = (H - L) / (H + L). We can use this in order to honor the
 * s_rx_carrier_range() call in ir-core. We'll suppose that any request
 * setting L=0 means we must shut down the demodulator.
 */
#define ITE_RXDCR_PER_10000_STEP 625

/* high speed carrier freq values */
#define ITE_CFQ_400		0x03
#define ITE_CFQ_450		0x08
#define ITE_CFQ_480		0x0b
#define ITE_CFQ_500		0x0d

/* values for pulse widths */
#define ITE_TXMPW_A		0x02
#define ITE_TXMPW_B		0x03
#define ITE_TXMPW_C		0x04
#define ITE_TXMPW_D		0x05
#define ITE_TXMPW_E		0x06

/* values for demodulator carrier range allowance */
#define ITE_RXDCR_DEFAULT	0x01	/* default carrier range */
#define ITE_RXDCR_MAX		0x07	/* default carrier range */

/* DR TX bits */
#define ITE_TX_PULSE		0x00
#define ITE_TX_SPACE		0x80
#define ITE_TX_MAX_RLE		0x80
#define ITE_TX_RLE_MASK		0x7f

/*
 * IT8712F
 *
 * hardware data obtained from:
 *
 * IT8712F
 * Environment Control – Low Pin Count Input / Output
 * (EC - LPC I/O)
 * Preliminary Specification V0. 81
 */

/* register offsets */
#define IT87_DR		0x00	/* data register */
#define IT87_IER	0x01	/* interrupt enable register */
#define IT87_RCR	0x02	/* receiver control register */
#define IT87_TCR1	0x03	/* transmitter control register 1 */
#define IT87_TCR2	0x04	/* transmitter control register 2 */
#define IT87_TSR	0x05	/* transmitter status register */
#define IT87_RSR	0x06	/* receiver status register */
#define IT87_BDLR	0x05	/* baud rate divisor low byte register */
#define IT87_BDHR	0x06	/* baud rate divisor high byte register */
#define IT87_IIR	0x07	/* interrupt identification register */

#define IT87_IOREG_LENGTH 0x08	/* length of register file */

/* IER bits */
#define IT87_TLDLIE	0x01	/* transmitter low data interrupt enable */
#define IT87_RDAIE	0x02	/* receiver data available interrupt enable */
#define IT87_RFOIE	0x04	/* receiver FIFO overrun interrupt enable */
#define IT87_IEC	0x08	/* interrupt enable control */
#define IT87_BR		0x10	/* baud rate register enable */
#define IT87_RESET	0x20	/* reset */

/* RCR bits */
#define IT87_RXDCR	0x07	/* receiver demodulation carrier range mask */
#define IT87_RXACT	0x08	/* receiver active */
#define IT87_RXEND	0x10	/* receiver demodulation enable */
#define IT87_RXEN	0x20	/* receiver enable */
#define IT87_HCFS	0x40	/* high-speed carrier frequency select */
#define IT87_RDWOS	0x80	/* receiver data without sync */

/* TCR1 bits */
#define IT87_TXMPM	0x03	/* transmitter modulation pulse mode mask */
#define IT87_TXMPM_DEFAULT 0x00	/* modulation pulse mode default */
#define IT87_TXENDF	0x04	/* transmitter deferral */
#define IT87_TXRLE	0x08	/* transmitter run length enable */
#define IT87_FIFOTL	0x30	/* FIFO level threshold mask */
#define IT87_FIFOTL_DEFAULT 0x20	/* FIFO level threshold default
					 * 0x00 -> 1, 0x10 -> 7, 0x20 -> 17,
					 * 0x30 -> 25 */
#define IT87_ILE	0x40	/* internal loopback enable */
#define IT87_FIFOCLR	0x80	/* FIFO clear bit */

/* TCR2 bits */
#define IT87_TXMPW	0x07	/* transmitter modulation pulse width mask */
#define IT87_TXMPW_DEFAULT 0x04	/* default modulation pulse width */
#define IT87_CFQ	0xf8	/* carrier frequency mask */
#define IT87_CFQ_SHIFT	3	/* carrier frequency bit shift */

/* TSR bits */
#define IT87_TXFBC	0x3f	/* transmitter FIFO byte count mask */

/* RSR bits */
#define IT87_RXFBC	0x3f	/* receiver FIFO byte count mask */
#define IT87_RXFTO	0x80	/* receiver FIFO time-out */

/* IIR bits */
#define IT87_IP		0x01	/* interrupt pending */
#define IT87_II		0x06	/* interrupt identification mask */
#define IT87_II_NOINT	0x00	/* no interrupt */
#define IT87_II_TXLDL	0x02	/* transmitter low data level */
#define IT87_II_RXDS	0x04	/* receiver data stored */
#define IT87_II_RXFO	0x06	/* receiver FIFO overrun */

/*
 * IT8512E/F
 *
 * Hardware data obtained from:
 *
 * IT8512E/F
 * Embedded Controller
 * Preliminary Specification V0.4.1
 *
 * Note that the CIR registers are not directly available to the host, because
 * they only are accessible to the integrated microcontroller. Thus, in order
 * use it, some kind of bridging is required. As the bridging may depend on
 * the controller firmware in use, we are going to use the PNP ID in order to
 * determine the strategy and ports available. See after these generic
 * IT8512E/F register definitions for register definitions for those
 * strategies.
 */

/* register offsets */
#define IT85_C0DR	0x00	/* data register */
#define IT85_C0MSTCR	0x01	/* master control register */
#define IT85_C0IER	0x02	/* interrupt enable register */
#define IT85_C0IIR	0x03	/* interrupt identification register */
#define IT85_C0CFR	0x04	/* carrier frequency register */
#define IT85_C0RCR	0x05	/* receiver control register */
#define IT85_C0TCR	0x06	/* transmitter control register */
#define IT85_C0SCK	0x07	/* slow clock control register */
#define IT85_C0BDLR	0x08	/* baud rate divisor low byte register */
#define IT85_C0BDHR	0x09	/* baud rate divisor high byte register */
#define IT85_C0TFSR	0x0a	/* transmitter FIFO status register */
#define IT85_C0RFSR	0x0b	/* receiver FIFO status register */
#define IT85_C0WCL	0x0d	/* wakeup code length register */
#define IT85_C0WCR	0x0e	/* wakeup code read/write register */
#define IT85_C0WPS	0x0f	/* wakeup power control/status register */

#define IT85_IOREG_LENGTH 0x10	/* length of register file */

/* C0MSTCR bits */
#define IT85_RESET	0x01	/* reset */
#define IT85_FIFOCLR	0x02	/* FIFO clear bit */
#define IT85_FIFOTL	0x0c	/* FIFO level threshold mask */
#define IT85_FIFOTL_DEFAULT 0x08	/* FIFO level threshold default
					 * 0x00 -> 1, 0x04 -> 7, 0x08 -> 17,
					 * 0x0c -> 25 */
#define IT85_ILE	0x10	/* internal loopback enable */
#define IT85_ILSEL	0x20	/* internal loopback select */

/* C0IER bits */
#define IT85_TLDLIE	0x01	/* TX low data level interrupt enable */
#define IT85_RDAIE	0x02	/* RX data available interrupt enable */
#define IT85_RFOIE	0x04	/* RX FIFO overrun interrupt enable */
#define IT85_IEC	0x80	/* interrupt enable function control */

/* C0IIR bits */
#define IT85_TLDLI	0x01	/* transmitter low data level interrupt */
#define IT85_RDAI	0x02	/* receiver data available interrupt */
#define IT85_RFOI	0x04	/* receiver FIFO overrun interrupt */
#define IT85_NIP	0x80	/* no interrupt pending */

/* C0CFR bits */
#define IT85_CFQ	0x1f	/* carrier frequency mask */
#define IT85_HCFS	0x20	/* high speed carrier frequency select */

/* C0RCR bits */
#define IT85_RXDCR	0x07	/* receiver demodulation carrier range mask */
#define IT85_RXACT	0x08	/* receiver active */
#define IT85_RXEND	0x10	/* receiver demodulation enable */
#define IT85_RDWOS	0x20	/* receiver data without sync */
#define IT85_RXEN	0x80	/* receiver enable */

/* C0TCR bits */
#define IT85_TXMPW	0x07	/* transmitter modulation pulse width mask */
#define IT85_TXMPW_DEFAULT 0x04	/* default modulation pulse width */
#define IT85_TXMPM	0x18	/* transmitter modulation pulse mode mask */
#define IT85_TXMPM_DEFAULT 0x00	/* modulation pulse mode default */
#define IT85_TXENDF	0x20	/* transmitter deferral */
#define IT85_TXRLE	0x40	/* transmitter run length enable */

/* C0SCK bits */
#define IT85_SCKS	0x01	/* slow clock select */
#define IT85_TXDCKG	0x02	/* TXD clock gating */
#define IT85_DLL1P8E	0x04	/* DLL 1.8432M enable */
#define IT85_DLLTE	0x08	/* DLL test enable */
#define IT85_BRCM	0x70	/* baud rate count mode */
#define IT85_DLLOCK	0x80	/* DLL lock */

/* C0TFSR bits */
#define IT85_TXFBC	0x3f	/* transmitter FIFO count mask */

/* C0RFSR bits */
#define IT85_RXFBC	0x3f	/* receiver FIFO count mask */
#define IT85_RXFTO	0x80	/* receiver FIFO time-out */

/* C0WCL bits */
#define IT85_WCL	0x3f	/* wakeup code length mask */

/* C0WPS bits */
#define IT85_CIRPOSIE	0x01	/* power on/off status interrupt enable */
#define IT85_CIRPOIS	0x02	/* power on/off interrupt status */
#define IT85_CIRPOII	0x04	/* power on/off interrupt identification */
#define IT85_RCRST	0x10	/* wakeup code reading counter reset bit */
#define IT85_WCRST	0x20	/* wakeup code writing counter reset bit */

/*
 * ITE8708
 *
 * Hardware data obtained from hacked driver for IT8512 in this forum post:
 *
 *  http://ubuntuforums.org/showthread.php?t=1028640
 *
 * Although there's no official documentation for that driver, analysis would
 * suggest that it maps the 16 registers of IT8512 onto two 8-register banks,
 * selectable by a single bank-select bit that's mapped onto both banks. The
 * IT8512 registers are mapped in a different order, so that the first bank
 * maps the ones that are used more often, and two registers that share a
 * reserved high-order bit are placed at the same offset in both banks in
 * order to reuse the reserved bit as the bank select bit.
 */

/* register offsets */

/* mapped onto both banks */
#define IT8708_BANKSEL	0x07	/* bank select register */
#define IT8708_HRAE	0x80	/* high registers access enable */

/* mapped onto the low bank */
#define IT8708_C0DR	0x00	/* data register */
#define IT8708_C0MSTCR	0x01	/* master control register */
#define IT8708_C0IER	0x02	/* interrupt enable register */
#define IT8708_C0IIR	0x03	/* interrupt identification register */
#define IT8708_C0RFSR	0x04	/* receiver FIFO status register */
#define IT8708_C0RCR	0x05	/* receiver control register */
#define IT8708_C0TFSR	0x06	/* transmitter FIFO status register */
#define IT8708_C0TCR	0x07	/* transmitter control register */

/* mapped onto the high bank */
#define IT8708_C0BDLR	0x01	/* baud rate divisor low byte register */
#define IT8708_C0BDHR	0x02	/* baud rate divisor high byte register */
#define IT8708_C0CFR	0x04	/* carrier frequency register */

/* registers whose bank mapping we don't know, since they weren't being used
 * in the hacked driver... most probably they belong to the high bank too,
 * since they fit in the holes the other registers leave */
#define IT8708_C0SCK	0x03	/* slow clock control register */
#define IT8708_C0WCL	0x05	/* wakeup code length register */
#define IT8708_C0WCR	0x06	/* wakeup code read/write register */
#define IT8708_C0WPS	0x07	/* wakeup power control/status register */

#define IT8708_IOREG_LENGTH 0x08	/* length of register file */

/* two more registers that are defined in the hacked driver, but can't be
 * found in the data sheets; no idea what they are or how they are accessed,
 * since the hacked driver doesn't seem to use them */
#define IT8708_CSCRR	0x00
#define IT8708_CGPINTR	0x01

/* CSCRR bits */
#define IT8708_CSCRR_SCRB 0x3f
#define IT8708_CSCRR_PM	0x80

/* CGPINTR bits */
#define IT8708_CGPINT	0x01

/*
 * ITE8709
 *
 * Hardware interfacing data obtained from the original lirc_ite8709 driver.
 * Verbatim from its sources:
 *
 * The ITE8709 device seems to be the combination of IT8512 superIO chip and
 * a specific firmware running on the IT8512's embedded micro-controller.
 * In addition of the embedded micro-controller, the IT8512 chip contains a
 * CIR module and several other modules. A few modules are directly accessible
 * by the host CPU, but most of them are only accessible by the
 * micro-controller. The CIR module is only accessible by the
 * micro-controller.
 *
 * The battery-backed SRAM module is accessible by the host CPU and the
 * micro-controller. So one of the MC's firmware role is to act as a bridge
 * between the host CPU and the CIR module. The firmware implements a kind of
 * communication protocol using the SRAM module as a shared memory. The IT8512
 * specification is publicly available on ITE's web site, but the
 * communication protocol is not, so it was reverse-engineered.
 */

/* register offsets */
#define IT8709_RAM_IDX	0x00	/* index into the SRAM module bytes */
#define IT8709_RAM_VAL	0x01	/* read/write data to the indexed byte */

#define IT8709_IOREG_LENGTH 0x02	/* length of register file */

/* register offsets inside the SRAM module */
#define IT8709_MODE	0x1a	/* request/ack byte */
#define IT8709_REG_IDX	0x1b	/* index of the CIR register to access */
#define IT8709_REG_VAL	0x1c	/* value read/to be written */
#define IT8709_IIR	0x1e	/* interrupt identification register */
#define IT8709_RFSR	0x1f	/* receiver FIFO status register */
#define IT8709_FIFO	0x20	/* start of in RAM RX FIFO copy */

/* MODE values */
#define IT8709_IDLE	0x00
#define IT8709_WRITE	0x01
#define IT8709_READ	0x02
