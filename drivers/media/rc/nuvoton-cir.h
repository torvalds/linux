/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Nuvoton Technology Corporation w83667hg/w83677hg-i CIR
 *
 * Copyright (C) 2010 Jarod Wilson <jarod@redhat.com>
 * Copyright (C) 2009 Nuvoton PS Team
 *
 * Special thanks to Nuvoton for providing hardware, spec sheets and
 * sample code upon which portions of this driver are based. Indirect
 * thanks also to Maxim Levitsky, whose ene_ir driver this driver is
 * modeled after.
 */

#include <linux/spinlock.h>
#include <linux/ioctl.h>

/* platform driver name to register */
#define NVT_DRIVER_NAME "nuvoton-cir"

/* debugging module parameter */
static int debug;


#define nvt_dbg(text, ...) \
	if (debug) \
		printk(KERN_DEBUG \
			KBUILD_MODNAME ": " text "\n" , ## __VA_ARGS__)

#define nvt_dbg_verbose(text, ...) \
	if (debug > 1) \
		printk(KERN_DEBUG \
			KBUILD_MODNAME ": " text "\n" , ## __VA_ARGS__)

#define nvt_dbg_wake(text, ...) \
	if (debug > 2) \
		printk(KERN_DEBUG \
			KBUILD_MODNAME ": " text "\n" , ## __VA_ARGS__)


#define RX_BUF_LEN 32

#define SIO_ID_MASK 0xfff0

enum nvt_chip_ver {
	NVT_UNKNOWN	= 0,
	NVT_W83667HG	= 0xa510,
	NVT_6775F	= 0xb470,
	NVT_6776F	= 0xc330,
	NVT_6779D	= 0xc560,
	NVT_INVALID	= 0xffff,
};

struct nvt_chip {
	const char *name;
	enum nvt_chip_ver chip_ver;
};

struct nvt_dev {
	struct rc_dev *rdev;

	spinlock_t lock;

	/* for rx */
	u8 buf[RX_BUF_LEN];
	unsigned int pkts;

	/* EFER Config register index/data pair */
	u32 cr_efir;
	u32 cr_efdr;

	/* hardware I/O settings */
	unsigned long cir_addr;
	unsigned long cir_wake_addr;
	int cir_irq;

	enum nvt_chip_ver chip_ver;
	/* hardware id */
	u8 chip_major;
	u8 chip_minor;

	/* carrier period = 1 / frequency */
	u32 carrier;
};

/* buffer packet constants */
#define BUF_PULSE_BIT	0x80
#define BUF_LEN_MASK	0x7f
#define BUF_REPEAT_BYTE	0x70
#define BUF_REPEAT_MASK	0xf0

/* CIR settings */

/* total length of CIR and CIR WAKE */
#define CIR_IOREG_LENGTH	0x0f

/* RX limit length, 8 high bits for SLCH, 8 low bits for SLCL */
#define CIR_RX_LIMIT_COUNT  (IR_DEFAULT_TIMEOUT / SAMPLE_PERIOD)

/* CIR Regs */
#define CIR_IRCON	0x00
#define CIR_IRSTS	0x01
#define CIR_IREN	0x02
#define CIR_RXFCONT	0x03
#define CIR_CP		0x04
#define CIR_CC		0x05
#define CIR_SLCH	0x06
#define CIR_SLCL	0x07
#define CIR_FIFOCON	0x08
#define CIR_IRFIFOSTS	0x09
#define CIR_SRXFIFO	0x0a
#define CIR_TXFCONT	0x0b
#define CIR_STXFIFO	0x0c
#define CIR_FCCH	0x0d
#define CIR_FCCL	0x0e
#define CIR_IRFSM	0x0f

/* CIR IRCON settings */
#define CIR_IRCON_RECV	 0x80
#define CIR_IRCON_WIREN	 0x40
#define CIR_IRCON_TXEN	 0x20
#define CIR_IRCON_RXEN	 0x10
#define CIR_IRCON_WRXINV 0x08
#define CIR_IRCON_RXINV	 0x04

#define CIR_IRCON_SAMPLE_PERIOD_SEL_1	0x00
#define CIR_IRCON_SAMPLE_PERIOD_SEL_25	0x01
#define CIR_IRCON_SAMPLE_PERIOD_SEL_50	0x02
#define CIR_IRCON_SAMPLE_PERIOD_SEL_100	0x03

/* FIXME: make this a runtime option */
/* select sample period as 50us */
#define CIR_IRCON_SAMPLE_PERIOD_SEL	CIR_IRCON_SAMPLE_PERIOD_SEL_50

/* CIR IRSTS settings */
#define CIR_IRSTS_RDR	0x80
#define CIR_IRSTS_RTR	0x40
#define CIR_IRSTS_PE	0x20
#define CIR_IRSTS_RFO	0x10
#define CIR_IRSTS_TE	0x08
#define CIR_IRSTS_TTR	0x04
#define CIR_IRSTS_TFU	0x02
#define CIR_IRSTS_GH	0x01

/* CIR IREN settings */
#define CIR_IREN_RDR	0x80
#define CIR_IREN_RTR	0x40
#define CIR_IREN_PE	0x20
#define CIR_IREN_RFO	0x10
#define CIR_IREN_TE	0x08
#define CIR_IREN_TTR	0x04
#define CIR_IREN_TFU	0x02
#define CIR_IREN_GH	0x01

/* CIR FIFOCON settings */
#define CIR_FIFOCON_TXFIFOCLR		0x80

#define CIR_FIFOCON_TX_TRIGGER_LEV_31	0x00
#define CIR_FIFOCON_TX_TRIGGER_LEV_24	0x10
#define CIR_FIFOCON_TX_TRIGGER_LEV_16	0x20
#define CIR_FIFOCON_TX_TRIGGER_LEV_8	0x30

/* FIXME: make this a runtime option */
/* select TX trigger level as 16 */
#define CIR_FIFOCON_TX_TRIGGER_LEV	CIR_FIFOCON_TX_TRIGGER_LEV_16

#define CIR_FIFOCON_RXFIFOCLR		0x08

#define CIR_FIFOCON_RX_TRIGGER_LEV_1	0x00
#define CIR_FIFOCON_RX_TRIGGER_LEV_8	0x01
#define CIR_FIFOCON_RX_TRIGGER_LEV_16	0x02
#define CIR_FIFOCON_RX_TRIGGER_LEV_24	0x03

/* FIXME: make this a runtime option */
/* select RX trigger level as 24 */
#define CIR_FIFOCON_RX_TRIGGER_LEV	CIR_FIFOCON_RX_TRIGGER_LEV_24

/* CIR IRFIFOSTS settings */
#define CIR_IRFIFOSTS_IR_PENDING	0x80
#define CIR_IRFIFOSTS_RX_GS		0x40
#define CIR_IRFIFOSTS_RX_FTA		0x20
#define CIR_IRFIFOSTS_RX_EMPTY		0x10
#define CIR_IRFIFOSTS_RX_FULL		0x08
#define CIR_IRFIFOSTS_TX_FTA		0x04
#define CIR_IRFIFOSTS_TX_EMPTY		0x02
#define CIR_IRFIFOSTS_TX_FULL		0x01


/* CIR WAKE UP Regs */
#define CIR_WAKE_IRCON			0x00
#define CIR_WAKE_IRSTS			0x01
#define CIR_WAKE_IREN			0x02
#define CIR_WAKE_FIFO_CMP_DEEP		0x03
#define CIR_WAKE_FIFO_CMP_TOL		0x04
#define CIR_WAKE_FIFO_COUNT		0x05
#define CIR_WAKE_SLCH			0x06
#define CIR_WAKE_SLCL			0x07
#define CIR_WAKE_FIFOCON		0x08
#define CIR_WAKE_SRXFSTS		0x09
#define CIR_WAKE_SAMPLE_RX_FIFO		0x0a
#define CIR_WAKE_WR_FIFO_DATA		0x0b
#define CIR_WAKE_RD_FIFO_ONLY		0x0c
#define CIR_WAKE_RD_FIFO_ONLY_IDX	0x0d
#define CIR_WAKE_FIFO_IGNORE		0x0e
#define CIR_WAKE_IRFSM			0x0f

/* CIR WAKE UP IRCON settings */
#define CIR_WAKE_IRCON_DEC_RST		0x80
#define CIR_WAKE_IRCON_MODE1		0x40
#define CIR_WAKE_IRCON_MODE0		0x20
#define CIR_WAKE_IRCON_RXEN		0x10
#define CIR_WAKE_IRCON_R		0x08
#define CIR_WAKE_IRCON_RXINV		0x04

/* FIXME/jarod: make this a runtime option */
/* select a same sample period like cir register */
#define CIR_WAKE_IRCON_SAMPLE_PERIOD_SEL	CIR_IRCON_SAMPLE_PERIOD_SEL_50

/* CIR WAKE IRSTS Bits */
#define CIR_WAKE_IRSTS_RDR		0x80
#define CIR_WAKE_IRSTS_RTR		0x40
#define CIR_WAKE_IRSTS_PE		0x20
#define CIR_WAKE_IRSTS_RFO		0x10
#define CIR_WAKE_IRSTS_GH		0x08
#define CIR_WAKE_IRSTS_IR_PENDING	0x01

/* CIR WAKE UP IREN Bits */
#define CIR_WAKE_IREN_RDR		0x80
#define CIR_WAKE_IREN_RTR		0x40
#define CIR_WAKE_IREN_PE		0x20
#define CIR_WAKE_IREN_RFO		0x10
#define CIR_WAKE_IREN_GH		0x08

/* CIR WAKE FIFOCON settings */
#define CIR_WAKE_FIFOCON_RXFIFOCLR	0x08

#define CIR_WAKE_FIFOCON_RX_TRIGGER_LEV_67	0x00
#define CIR_WAKE_FIFOCON_RX_TRIGGER_LEV_66	0x01
#define CIR_WAKE_FIFOCON_RX_TRIGGER_LEV_65	0x02
#define CIR_WAKE_FIFOCON_RX_TRIGGER_LEV_64	0x03

/* FIXME: make this a runtime option */
/* select WAKE UP RX trigger level as 67 */
#define CIR_WAKE_FIFOCON_RX_TRIGGER_LEV	CIR_WAKE_FIFOCON_RX_TRIGGER_LEV_67

/* CIR WAKE SRXFSTS settings */
#define CIR_WAKE_IRFIFOSTS_RX_GS	0x80
#define CIR_WAKE_IRFIFOSTS_RX_FTA	0x40
#define CIR_WAKE_IRFIFOSTS_RX_EMPTY	0x20
#define CIR_WAKE_IRFIFOSTS_RX_FULL	0x10

/*
 * The CIR Wake FIFO buffer is 67 bytes long, but the stock remote wakes
 * the system comparing only 65 bytes (fails with this set to 67)
 */
#define CIR_WAKE_FIFO_CMP_BYTES		65
/* CIR Wake byte comparison tolerance */
#define CIR_WAKE_CMP_TOLERANCE		5

/*
 * Extended Function Enable Registers:
 *  Extended Function Index Register
 *  Extended Function Data Register
 */
#define CR_EFIR			0x2e
#define CR_EFDR			0x2f

/* Possible alternate EFER values, depends on how the chip is wired */
#define CR_EFIR2		0x4e
#define CR_EFDR2		0x4f

/* Extended Function Mode enable/disable magic values */
#define EFER_EFM_ENABLE		0x87
#define EFER_EFM_DISABLE	0xaa

/* Config regs we need to care about */
#define CR_SOFTWARE_RESET	0x02
#define CR_LOGICAL_DEV_SEL	0x07
#define CR_CHIP_ID_HI		0x20
#define CR_CHIP_ID_LO		0x21
#define CR_DEV_POWER_DOWN	0x22 /* bit 2 is CIR power, default power on */
#define CR_OUTPUT_PIN_SEL	0x27
#define CR_MULTIFUNC_PIN_SEL	0x2c
#define CR_LOGICAL_DEV_EN	0x30 /* valid for all logical devices */
/* next three regs valid for both the CIR and CIR_WAKE logical devices */
#define CR_CIR_BASE_ADDR_HI	0x60
#define CR_CIR_BASE_ADDR_LO	0x61
#define CR_CIR_IRQ_RSRC		0x70
/* next three regs valid only for ACPI logical dev */
#define CR_ACPI_CIR_WAKE	0xe0
#define CR_ACPI_IRQ_EVENTS	0xf6
#define CR_ACPI_IRQ_EVENTS2	0xf7

/* Logical devices that we need to care about */
#define LOGICAL_DEV_LPT		0x01
#define LOGICAL_DEV_CIR		0x06
#define LOGICAL_DEV_ACPI	0x0a
#define LOGICAL_DEV_CIR_WAKE	0x0e

#define LOGICAL_DEV_DISABLE	0x00
#define LOGICAL_DEV_ENABLE	0x01

#define CIR_WAKE_ENABLE_BIT	0x08
#define PME_INTR_CIR_PASS_BIT	0x08

/* w83677hg CIR pin config */
#define OUTPUT_PIN_SEL_MASK	0xbc
#define OUTPUT_ENABLE_CIR	0x01 /* Pin95=CIRRX, Pin96=CIRTX1 */
#define OUTPUT_ENABLE_CIRWB	0x40 /* enable wide-band sensor */

/* w83667hg CIR pin config */
#define MULTIFUNC_PIN_SEL_MASK	0x1f
#define MULTIFUNC_ENABLE_CIR	0x80 /* Pin75=CIRRX, Pin76=CIRTX1 */
#define MULTIFUNC_ENABLE_CIRWB	0x20 /* enable wide-band sensor */

/* MCE CIR signal length, related on sample period */

/* MCE CIR controller signal length: about 43ms
 * 43ms / 50us (sample period) * 0.85 (inaccuracy)
 */
#define CONTROLLER_BUF_LEN_MIN 830

/* MCE CIR keyboard signal length: about 26ms
 * 26ms / 50us (sample period) * 0.85 (inaccuracy)
 */
#define KEYBOARD_BUF_LEN_MAX 650
#define KEYBOARD_BUF_LEN_MIN 610

/* MCE CIR mouse signal length: about 24ms
 * 24ms / 50us (sample period) * 0.85 (inaccuracy)
 */
#define MOUSE_BUF_LEN_MIN 565

#define CIR_SAMPLE_PERIOD 50
#define CIR_SAMPLE_LOW_INACCURACY 0.85

/* MAX silence time that driver will sent to lirc */
#define MAX_SILENCE_TIME 60000

#if CIR_IRCON_SAMPLE_PERIOD_SEL == CIR_IRCON_SAMPLE_PERIOD_SEL_100
#define SAMPLE_PERIOD 100

#elif CIR_IRCON_SAMPLE_PERIOD_SEL == CIR_IRCON_SAMPLE_PERIOD_SEL_50
#define SAMPLE_PERIOD 50

#elif CIR_IRCON_SAMPLE_PERIOD_SEL == CIR_IRCON_SAMPLE_PERIOD_SEL_25
#define SAMPLE_PERIOD 25

#else
#define SAMPLE_PERIOD 1
#endif

/* as VISTA MCE definition, valid carrier value */
#define MAX_CARRIER 60000
#define MIN_CARRIER 30000

/* max wakeup sequence length */
#define WAKEUP_MAX_SIZE 65
