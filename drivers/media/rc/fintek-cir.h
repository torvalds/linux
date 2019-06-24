/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Feature Integration Technology Inc. (aka Fintek) LPC CIR
 *
 * Copyright (C) 2011 Jarod Wilson <jarod@redhat.com>
 *
 * Special thanks to Fintek for providing hardware and spec sheets.
 * This driver is based upon the nuvoton, ite and ene drivers for
 * similar hardware.
 */

#include <linux/spinlock.h>
#include <linux/ioctl.h>

/* platform driver name to register */
#define FINTEK_DRIVER_NAME	"fintek-cir"
#define FINTEK_DESCRIPTION	"Fintek LPC SuperIO Consumer IR Transceiver"
#define VENDOR_ID_FINTEK	0x1934


/* debugging module parameter */
static int debug;

#define fit_pr(level, text, ...) \
	printk(level KBUILD_MODNAME ": " text, ## __VA_ARGS__)

#define fit_dbg(text, ...) \
	if (debug) \
		printk(KERN_DEBUG \
			KBUILD_MODNAME ": " text "\n" , ## __VA_ARGS__)

#define fit_dbg_verbose(text, ...) \
	if (debug > 1) \
		printk(KERN_DEBUG \
			KBUILD_MODNAME ": " text "\n" , ## __VA_ARGS__)

#define fit_dbg_wake(text, ...) \
	if (debug > 2) \
		printk(KERN_DEBUG \
			KBUILD_MODNAME ": " text "\n" , ## __VA_ARGS__)


#define TX_BUF_LEN 256
#define RX_BUF_LEN 32

struct fintek_dev {
	struct pnp_dev *pdev;
	struct rc_dev *rdev;

	spinlock_t fintek_lock;

	/* for rx */
	u8 buf[RX_BUF_LEN];
	unsigned int pkts;

	struct {
		spinlock_t lock;
		u8 buf[TX_BUF_LEN];
		unsigned int buf_count;
		unsigned int cur_buf_num;
		wait_queue_head_t queue;
	} tx;

	/* Config register index/data port pair */
	u32 cr_ip;
	u32 cr_dp;

	/* hardware I/O settings */
	unsigned long cir_addr;
	int cir_irq;
	int cir_port_len;

	/* hardware id */
	u8 chip_major;
	u8 chip_minor;
	u16 chip_vendor;
	u8 logical_dev_cir;

	/* hardware features */
	bool hw_learning_capable;
	bool hw_tx_capable;

	/* rx settings */
	bool learning_enabled;
	bool carrier_detect_enabled;

	enum {
		CMD_HEADER = 0,
		SUBCMD,
		CMD_DATA,
		PARSE_IRDATA,
	} parser_state;

	u8 cmd, rem;

	/* carrier period = 1 / frequency */
	u32 carrier;
};

/* buffer packet constants, largely identical to mceusb.c */
#define BUF_PULSE_BIT		0x80
#define BUF_LEN_MASK		0x1f
#define BUF_SAMPLE_MASK		0x7f

#define BUF_COMMAND_HEADER	0x9f
#define BUF_COMMAND_MASK	0xe0
#define BUF_COMMAND_NULL	0x00
#define BUF_HW_CMD_HEADER	0xff
#define BUF_CMD_G_REVISION	0x0b
#define BUF_CMD_S_CARRIER	0x06
#define BUF_CMD_S_TIMEOUT	0x0c
#define BUF_CMD_SIG_END		0x01
#define BUF_CMD_S_TXMASK	0x08
#define BUF_CMD_S_RXSENSOR	0x14
#define BUF_RSP_PULSE_COUNT	0x15

#define CIR_SAMPLE_PERIOD	50

/*
 * Configuration Register:
 *  Index Port
 *  Data Port
 */
#define CR_INDEX_PORT		0x2e
#define CR_DATA_PORT		0x2f

/* Possible alternate values, depends on how the chip is wired */
#define CR_INDEX_PORT2		0x4e
#define CR_DATA_PORT2		0x4f

/*
 * GCR_CONFIG_PORT_SEL bit 4 specifies which Index Port value is
 * active. 1 = 0x4e, 0 = 0x2e
 */
#define PORT_SEL_PORT_4E_EN	0x10

/* Extended Function Mode enable/disable magic values */
#define CONFIG_REG_ENABLE	0x87
#define CONFIG_REG_DISABLE	0xaa

/* Chip IDs found in CR_CHIP_ID_{HI,LO} */
#define CHIP_ID_HIGH_F71809U	0x04
#define CHIP_ID_LOW_F71809U	0x08

/*
 * Global control regs we need to care about:
 *      Global Control                  def.
 *      Register name           addr    val. */
#define GCR_SOFTWARE_RESET	0x02 /* 0x00 */
#define GCR_LOGICAL_DEV_NO	0x07 /* 0x00 */
#define GCR_CHIP_ID_HI		0x20 /* 0x04 */
#define GCR_CHIP_ID_LO		0x21 /* 0x08 */
#define GCR_VENDOR_ID_HI	0x23 /* 0x19 */
#define GCR_VENDOR_ID_LO	0x24 /* 0x34 */
#define GCR_CONFIG_PORT_SEL	0x25 /* 0x01 */
#define GCR_KBMOUSE_WAKEUP	0x27

#define LOGICAL_DEV_DISABLE	0x00
#define LOGICAL_DEV_ENABLE	0x01

/* Logical device number of the CIR function */
#define LOGICAL_DEV_CIR_REV1	0x05
#define LOGICAL_DEV_CIR_REV2	0x08

/* CIR Logical Device (LDN 0x08) config registers */
#define CIR_CR_COMMAND_INDEX	0x04
#define CIR_CR_IRCS		0x05 /* Before host writes command to IR, host
					must set to 1. When host finshes write
					command to IR, host must clear to 0. */
#define CIR_CR_COMMAND_DATA	0x06 /* Host read or write command data */
#define CIR_CR_CLASS		0x07 /* 0xff = rx-only, 0x66 = rx + 2 tx,
					0x33 = rx + 1 tx */
#define CIR_CR_DEV_EN		0x30 /* bit0 = 1 enables CIR */
#define CIR_CR_BASE_ADDR_HI	0x60 /* MSB of CIR IO base addr */
#define CIR_CR_BASE_ADDR_LO	0x61 /* LSB of CIR IO base addr */
#define CIR_CR_IRQ_SEL		0x70 /* bits3-0 store CIR IRQ */
#define CIR_CR_PSOUT_STATUS	0xf1
#define CIR_CR_WAKE_KEY3_ADDR	0xf8
#define CIR_CR_WAKE_KEY3_CODE	0xf9
#define CIR_CR_WAKE_KEY3_DC	0xfa
#define CIR_CR_WAKE_CONTROL	0xfb
#define CIR_CR_WAKE_KEY12_ADDR	0xfc
#define CIR_CR_WAKE_KEY4_ADDR	0xfd
#define CIR_CR_WAKE_KEY5_ADDR	0xfe

#define CLASS_RX_ONLY		0xff
#define CLASS_RX_2TX		0x66
#define CLASS_RX_1TX		0x33

/* CIR device registers */
#define CIR_STATUS		0x00
#define CIR_RX_DATA		0x01
#define CIR_TX_CONTROL		0x02
#define CIR_TX_DATA		0x03
#define CIR_CONTROL		0x04

/* Bits to enable CIR wake */
#define LOGICAL_DEV_ACPI	0x01
#define LDEV_ACPI_WAKE_EN_REG	0xe8
#define ACPI_WAKE_EN_CIR_BIT	0x04

#define LDEV_ACPI_PME_EN_REG	0xf0
#define LDEV_ACPI_PME_CLR_REG	0xf1
#define ACPI_PME_CIR_BIT	0x02

#define LDEV_ACPI_STATE_REG	0xf4
#define ACPI_STATE_CIR_BIT	0x20

/*
 * CIR status register (0x00):
 *   7 - CIR_IRQ_EN (1 = enable CIR IRQ, 0 = disable)
 *   3 - TX_FINISH (1 when TX finished, write 1 to clear)
 *   2 - TX_UNDERRUN (1 on TX underrun, write 1 to clear)
 *   1 - RX_TIMEOUT (1 on RX timeout, write 1 to clear)
 *   0 - RX_RECEIVE (1 on RX receive, write 1 to clear)
 */
#define CIR_STATUS_IRQ_EN	0x80
#define CIR_STATUS_TX_FINISH	0x08
#define CIR_STATUS_TX_UNDERRUN	0x04
#define CIR_STATUS_RX_TIMEOUT	0x02
#define CIR_STATUS_RX_RECEIVE	0x01
#define CIR_STATUS_IRQ_MASK	0x0f

/*
 * CIR TX control register (0x02):
 *   7 - TX_START (1 to indicate TX start, auto-cleared when done)
 *   6 - TX_END (1 to indicate TX data written to TX fifo)
 */
#define CIR_TX_CONTROL_TX_START	0x80
#define CIR_TX_CONTROL_TX_END	0x40

