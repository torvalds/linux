/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************
 *
 * Driver for the IFX spi modem.
 *
 * Copyright (C) 2009, 2010 Intel Corp
 * Jim Stanley <jim.stanley@intel.com>
 *
 *****************************************************************************/
#ifndef _IFX6X60_H
#define _IFX6X60_H

struct gpio_desc;

#define DRVNAME				"ifx6x60"
#define TTYNAME				"ttyIFX"

#define IFX_SPI_MAX_MINORS		1
#define IFX_SPI_TRANSFER_SIZE		2048
#define IFX_SPI_FIFO_SIZE		4096

#define IFX_SPI_HEADER_OVERHEAD		4
#define IFX_RESET_TIMEOUT		msecs_to_jiffies(50)

/* device flags bitfield definitions */
#define IFX_SPI_STATE_PRESENT		0
#define IFX_SPI_STATE_IO_IN_PROGRESS	1
#define IFX_SPI_STATE_IO_READY		2
#define IFX_SPI_STATE_TIMER_PENDING	3
#define IFX_SPI_STATE_IO_AVAILABLE	4

/* flow control bitfields */
#define IFX_SPI_DCD			0
#define IFX_SPI_CTS			1
#define IFX_SPI_DSR			2
#define IFX_SPI_RI			3
#define IFX_SPI_DTR			4
#define IFX_SPI_RTS			5
#define IFX_SPI_TX_FC			6
#define IFX_SPI_RX_FC			7
#define IFX_SPI_UPDATE			8

#define IFX_SPI_PAYLOAD_SIZE		(IFX_SPI_TRANSFER_SIZE - \
						IFX_SPI_HEADER_OVERHEAD)

#define IFX_SPI_IRQ_TYPE		DETECT_EDGE_RISING
#define IFX_SPI_GPIO_TARGET		0
#define IFX_SPI_GPIO0			0x105

#define IFX_SPI_STATUS_TIMEOUT		(2000*HZ)

/* values for bits in power status byte */
#define IFX_SPI_POWER_DATA_PENDING	1
#define IFX_SPI_POWER_SRDY		2

struct ifx_spi_device {
	/* Our SPI device */
	struct spi_device *spi_dev;

	/* Port specific data */
	struct kfifo tx_fifo;
	spinlock_t fifo_lock;
	unsigned long signal_state;

	/* TTY Layer logic */
	struct tty_port tty_port;
	struct device *tty_dev;
	int minor;

	/* Low level I/O work */
	struct tasklet_struct io_work_tasklet;
	unsigned long flags;
	dma_addr_t rx_dma;
	dma_addr_t tx_dma;

	int modem;		/* Modem type */
	int use_dma;		/* provide dma-able addrs in SPI msg */
	long max_hz;		/* max SPI frequency */

	spinlock_t write_lock;
	int write_pending;
	spinlock_t power_lock;
	unsigned char power_status;

	unsigned char *rx_buffer;
	unsigned char *tx_buffer;
	dma_addr_t rx_bus;
	dma_addr_t tx_bus;
	unsigned char spi_more;
	unsigned char spi_slave_cts;

	struct timer_list spi_timer;

	struct spi_message spi_msg;
	struct spi_transfer spi_xfer;

	struct {
		/* gpio lines */
		struct gpio_desc *srdy;		/* slave-ready gpio */
		struct gpio_desc *mrdy;		/* master-ready gpio */
		struct gpio_desc *reset;	/* modem-reset gpio */
		struct gpio_desc *po;		/* modem-on gpio */
		struct gpio_desc *reset_out;	/* modem-in-reset gpio */
		struct gpio_desc *pmu_reset;	/* PMU reset gpio */
		/* state/stats */
		int unack_srdy_int_nb;
	} gpio;

	/* modem reset */
	unsigned long mdm_reset_state;
#define MR_START	0
#define MR_INPROGRESS	1
#define MR_COMPLETE	2
	wait_queue_head_t mdm_reset_wait;
	void (*swap_buf)(unsigned char *buf, int len, void *end);
};

#endif /* _IFX6X60_H */
