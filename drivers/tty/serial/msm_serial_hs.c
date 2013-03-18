/*
 * MSM 7k/8k High speed uart driver
 *
 * Copyright (c) 2007-2011, Code Aurora Forum. All rights reserved.
 * Copyright (c) 2008 Google Inc.
 * Modified: Nick Pelly <npelly@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * Has optional support for uart power management independent of linux
 * suspend/resume:
 *
 * RX wakeup.
 * UART wakeup can be triggered by RX activity (using a wakeup GPIO on the
 * UART RX pin). This should only be used if there is not a wakeup
 * GPIO on the UART CTS, and the first RX byte is known (for example, with the
 * Bluetooth Texas Instruments HCILL protocol), since the first RX byte will
 * always be lost. RTS will be asserted even while the UART is off in this mode
 * of operation. See msm_serial_hs_platform_data.rx_wakeup_irq.
 */

#include <linux/module.h>

#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <linux/atomic.h>
#include <asm/irq.h>

#include <mach/hardware.h>
#include <mach/dma.h>
#include <linux/platform_data/msm_serial_hs.h>

/* HSUART Registers */
#define UARTDM_MR1_ADDR 0x0
#define UARTDM_MR2_ADDR 0x4

/* Data Mover result codes */
#define RSLT_FIFO_CNTR_BMSK (0xE << 28)
#define RSLT_VLD            BIT(1)

/* write only register */
#define UARTDM_CSR_ADDR 0x8
#define UARTDM_CSR_115200 0xFF
#define UARTDM_CSR_57600  0xEE
#define UARTDM_CSR_38400  0xDD
#define UARTDM_CSR_28800  0xCC
#define UARTDM_CSR_19200  0xBB
#define UARTDM_CSR_14400  0xAA
#define UARTDM_CSR_9600   0x99
#define UARTDM_CSR_7200   0x88
#define UARTDM_CSR_4800   0x77
#define UARTDM_CSR_3600   0x66
#define UARTDM_CSR_2400   0x55
#define UARTDM_CSR_1200   0x44
#define UARTDM_CSR_600    0x33
#define UARTDM_CSR_300    0x22
#define UARTDM_CSR_150    0x11
#define UARTDM_CSR_75     0x00

/* write only register */
#define UARTDM_TF_ADDR 0x70
#define UARTDM_TF2_ADDR 0x74
#define UARTDM_TF3_ADDR 0x78
#define UARTDM_TF4_ADDR 0x7C

/* write only register */
#define UARTDM_CR_ADDR 0x10
#define UARTDM_IMR_ADDR 0x14

#define UARTDM_IPR_ADDR 0x18
#define UARTDM_TFWR_ADDR 0x1c
#define UARTDM_RFWR_ADDR 0x20
#define UARTDM_HCR_ADDR 0x24
#define UARTDM_DMRX_ADDR 0x34
#define UARTDM_IRDA_ADDR 0x38
#define UARTDM_DMEN_ADDR 0x3c

/* UART_DM_NO_CHARS_FOR_TX */
#define UARTDM_NCF_TX_ADDR 0x40

#define UARTDM_BADR_ADDR 0x44

#define UARTDM_SIM_CFG_ADDR 0x80
/* Read Only register */
#define UARTDM_SR_ADDR 0x8

/* Read Only register */
#define UARTDM_RF_ADDR  0x70
#define UARTDM_RF2_ADDR 0x74
#define UARTDM_RF3_ADDR 0x78
#define UARTDM_RF4_ADDR 0x7C

/* Read Only register */
#define UARTDM_MISR_ADDR 0x10

/* Read Only register */
#define UARTDM_ISR_ADDR 0x14
#define UARTDM_RX_TOTAL_SNAP_ADDR 0x38

#define UARTDM_RXFS_ADDR 0x50

/* Register field Mask Mapping */
#define UARTDM_SR_PAR_FRAME_BMSK        BIT(5)
#define UARTDM_SR_OVERRUN_BMSK          BIT(4)
#define UARTDM_SR_TXEMT_BMSK            BIT(3)
#define UARTDM_SR_TXRDY_BMSK            BIT(2)
#define UARTDM_SR_RXRDY_BMSK            BIT(0)

#define UARTDM_CR_TX_DISABLE_BMSK       BIT(3)
#define UARTDM_CR_RX_DISABLE_BMSK       BIT(1)
#define UARTDM_CR_TX_EN_BMSK            BIT(2)
#define UARTDM_CR_RX_EN_BMSK            BIT(0)

/* UARTDM_CR channel_comman bit value (register field is bits 8:4) */
#define RESET_RX                0x10
#define RESET_TX                0x20
#define RESET_ERROR_STATUS      0x30
#define RESET_BREAK_INT         0x40
#define START_BREAK             0x50
#define STOP_BREAK              0x60
#define RESET_CTS               0x70
#define RESET_STALE_INT         0x80
#define RFR_LOW                 0xD0
#define RFR_HIGH                0xE0
#define CR_PROTECTION_EN        0x100
#define STALE_EVENT_ENABLE      0x500
#define STALE_EVENT_DISABLE     0x600
#define FORCE_STALE_EVENT       0x400
#define CLEAR_TX_READY          0x300
#define RESET_TX_ERROR          0x800
#define RESET_TX_DONE           0x810

#define UARTDM_MR1_AUTO_RFR_LEVEL1_BMSK 0xffffff00
#define UARTDM_MR1_AUTO_RFR_LEVEL0_BMSK 0x3f
#define UARTDM_MR1_CTS_CTL_BMSK 0x40
#define UARTDM_MR1_RX_RDY_CTL_BMSK 0x80

#define UARTDM_MR2_ERROR_MODE_BMSK 0x40
#define UARTDM_MR2_BITS_PER_CHAR_BMSK 0x30

/* bits per character configuration */
#define FIVE_BPC  (0 << 4)
#define SIX_BPC   (1 << 4)
#define SEVEN_BPC (2 << 4)
#define EIGHT_BPC (3 << 4)

#define UARTDM_MR2_STOP_BIT_LEN_BMSK 0xc
#define STOP_BIT_ONE (1 << 2)
#define STOP_BIT_TWO (3 << 2)

#define UARTDM_MR2_PARITY_MODE_BMSK 0x3

/* Parity configuration */
#define NO_PARITY 0x0
#define EVEN_PARITY 0x1
#define ODD_PARITY 0x2
#define SPACE_PARITY 0x3

#define UARTDM_IPR_STALE_TIMEOUT_MSB_BMSK 0xffffff80
#define UARTDM_IPR_STALE_LSB_BMSK 0x1f

/* These can be used for both ISR and IMR register */
#define UARTDM_ISR_TX_READY_BMSK        BIT(7)
#define UARTDM_ISR_CURRENT_CTS_BMSK     BIT(6)
#define UARTDM_ISR_DELTA_CTS_BMSK       BIT(5)
#define UARTDM_ISR_RXLEV_BMSK           BIT(4)
#define UARTDM_ISR_RXSTALE_BMSK         BIT(3)
#define UARTDM_ISR_RXBREAK_BMSK         BIT(2)
#define UARTDM_ISR_RXHUNT_BMSK          BIT(1)
#define UARTDM_ISR_TXLEV_BMSK           BIT(0)

/* Field definitions for UART_DM_DMEN*/
#define UARTDM_TX_DM_EN_BMSK 0x1
#define UARTDM_RX_DM_EN_BMSK 0x2

#define UART_FIFOSIZE 64
#define UARTCLK 7372800

/* Rx DMA request states */
enum flush_reason {
	FLUSH_NONE,
	FLUSH_DATA_READY,
	FLUSH_DATA_INVALID,  /* values after this indicate invalid data */
	FLUSH_IGNORE = FLUSH_DATA_INVALID,
	FLUSH_STOP,
	FLUSH_SHUTDOWN,
};

/* UART clock states */
enum msm_hs_clk_states_e {
	MSM_HS_CLK_PORT_OFF,     /* port not in use */
	MSM_HS_CLK_OFF,          /* clock disabled */
	MSM_HS_CLK_REQUEST_OFF,  /* disable after TX and RX flushed */
	MSM_HS_CLK_ON,           /* clock enabled */
};

/* Track the forced RXSTALE flush during clock off sequence.
 * These states are only valid during MSM_HS_CLK_REQUEST_OFF */
enum msm_hs_clk_req_off_state_e {
	CLK_REQ_OFF_START,
	CLK_REQ_OFF_RXSTALE_ISSUED,
	CLK_REQ_OFF_FLUSH_ISSUED,
	CLK_REQ_OFF_RXSTALE_FLUSHED,
};

/**
 * struct msm_hs_tx
 * @tx_ready_int_en: ok to dma more tx?
 * @dma_in_flight: tx dma in progress
 * @xfer: top level DMA command pointer structure
 * @command_ptr: third level command struct pointer
 * @command_ptr_ptr: second level command list struct pointer
 * @mapped_cmd_ptr: DMA view of third level command struct
 * @mapped_cmd_ptr_ptr: DMA view of second level command list struct
 * @tx_count: number of bytes to transfer in DMA transfer
 * @dma_base: DMA view of UART xmit buffer
 *
 * This structure describes a single Tx DMA transaction. MSM DMA
 * commands have two levels of indirection. The top level command
 * ptr points to a list of command ptr which in turn points to a
 * single DMA 'command'. In our case each Tx transaction consists
 * of a single second level pointer pointing to a 'box type' command.
 */
struct msm_hs_tx {
	unsigned int tx_ready_int_en;
	unsigned int dma_in_flight;
	struct msm_dmov_cmd xfer;
	dmov_box *command_ptr;
	u32 *command_ptr_ptr;
	dma_addr_t mapped_cmd_ptr;
	dma_addr_t mapped_cmd_ptr_ptr;
	int tx_count;
	dma_addr_t dma_base;
};

/**
 * struct msm_hs_rx
 * @flush: Rx DMA request state
 * @xfer: top level DMA command pointer structure
 * @cmdptr_dmaaddr: DMA view of second level command structure
 * @command_ptr: third level DMA command pointer structure
 * @command_ptr_ptr: second level DMA command list pointer
 * @mapped_cmd_ptr: DMA view of the third level command structure
 * @wait: wait for DMA completion before shutdown
 * @buffer: destination buffer for RX DMA
 * @rbuffer: DMA view of buffer
 * @pool: dma pool out of which coherent rx buffer is allocated
 * @tty_work: private work-queue for tty flip buffer push task
 *
 * This structure describes a single Rx DMA transaction. Rx DMA
 * transactions use box mode DMA commands.
 */
struct msm_hs_rx {
	enum flush_reason flush;
	struct msm_dmov_cmd xfer;
	dma_addr_t cmdptr_dmaaddr;
	dmov_box *command_ptr;
	u32 *command_ptr_ptr;
	dma_addr_t mapped_cmd_ptr;
	wait_queue_head_t wait;
	dma_addr_t rbuffer;
	unsigned char *buffer;
	struct dma_pool *pool;
	struct work_struct tty_work;
};

/**
 * struct msm_hs_rx_wakeup
 * @irq: IRQ line to be configured as interrupt source on Rx activity
 * @ignore: boolean value. 1 = ignore the wakeup interrupt
 * @rx_to_inject: extra character to be inserted to Rx tty on wakeup
 * @inject_rx: 1 = insert rx_to_inject. 0 = do not insert extra character
 *
 * This is an optional structure required for UART Rx GPIO IRQ based
 * wakeup from low power state. UART wakeup can be triggered by RX activity
 * (using a wakeup GPIO on the UART RX pin). This should only be used if
 * there is not a wakeup GPIO on the UART CTS, and the first RX byte is
 * known (eg., with the Bluetooth Texas Instruments HCILL protocol),
 * since the first RX byte will always be lost. RTS will be asserted even
 * while the UART is clocked off in this mode of operation.
 */
struct msm_hs_rx_wakeup {
	int irq;  /* < 0 indicates low power wakeup disabled */
	unsigned char ignore;
	unsigned char inject_rx;
	char rx_to_inject;
};

/**
 * struct msm_hs_port
 * @uport: embedded uart port structure
 * @imr_reg: shadow value of UARTDM_IMR
 * @clk: uart input clock handle
 * @tx: Tx transaction related data structure
 * @rx: Rx transaction related data structure
 * @dma_tx_channel: Tx DMA command channel
 * @dma_rx_channel Rx DMA command channel
 * @dma_tx_crci: Tx channel rate control interface number
 * @dma_rx_crci: Rx channel rate control interface number
 * @clk_off_timer: Timer to poll DMA event completion before clock off
 * @clk_off_delay: clk_off_timer poll interval
 * @clk_state: overall clock state
 * @clk_req_off_state: post flush clock states
 * @rx_wakeup: optional rx_wakeup feature related data
 * @exit_lpm_cb: optional callback to exit low power mode
 *
 * Low level serial port structure.
 */
struct msm_hs_port {
	struct uart_port uport;
	unsigned long imr_reg;
	struct clk *clk;
	struct msm_hs_tx tx;
	struct msm_hs_rx rx;

	int dma_tx_channel;
	int dma_rx_channel;
	int dma_tx_crci;
	int dma_rx_crci;

	struct hrtimer clk_off_timer;
	ktime_t clk_off_delay;
	enum msm_hs_clk_states_e clk_state;
	enum msm_hs_clk_req_off_state_e clk_req_off_state;

	struct msm_hs_rx_wakeup rx_wakeup;
	void (*exit_lpm_cb)(struct uart_port *);
};

#define MSM_UARTDM_BURST_SIZE 16   /* DM burst size (in bytes) */
#define UARTDM_TX_BUF_SIZE UART_XMIT_SIZE
#define UARTDM_RX_BUF_SIZE 512

#define UARTDM_NR 2

static struct msm_hs_port q_uart_port[UARTDM_NR];
static struct platform_driver msm_serial_hs_platform_driver;
static struct uart_driver msm_hs_driver;
static struct uart_ops msm_hs_ops;
static struct workqueue_struct *msm_hs_workqueue;

#define UARTDM_TO_MSM(uart_port) \
	container_of((uart_port), struct msm_hs_port, uport)

static unsigned int use_low_power_rx_wakeup(struct msm_hs_port
						   *msm_uport)
{
	return (msm_uport->rx_wakeup.irq >= 0);
}

static unsigned int msm_hs_read(struct uart_port *uport,
				       unsigned int offset)
{
	return ioread32(uport->membase + offset);
}

static void msm_hs_write(struct uart_port *uport, unsigned int offset,
				 unsigned int value)
{
	iowrite32(value, uport->membase + offset);
}

static void msm_hs_release_port(struct uart_port *port)
{
	iounmap(port->membase);
}

static int msm_hs_request_port(struct uart_port *port)
{
	port->membase = ioremap(port->mapbase, PAGE_SIZE);
	if (unlikely(!port->membase))
		return -ENOMEM;

	/* configure the CR Protection to Enable */
	msm_hs_write(port, UARTDM_CR_ADDR, CR_PROTECTION_EN);
	return 0;
}

static int msm_hs_remove(struct platform_device *pdev)
{

	struct msm_hs_port *msm_uport;
	struct device *dev;

	if (pdev->id < 0 || pdev->id >= UARTDM_NR) {
		printk(KERN_ERR "Invalid plaform device ID = %d\n", pdev->id);
		return -EINVAL;
	}

	msm_uport = &q_uart_port[pdev->id];
	dev = msm_uport->uport.dev;

	dma_unmap_single(dev, msm_uport->rx.mapped_cmd_ptr, sizeof(dmov_box),
			 DMA_TO_DEVICE);
	dma_pool_free(msm_uport->rx.pool, msm_uport->rx.buffer,
		      msm_uport->rx.rbuffer);
	dma_pool_destroy(msm_uport->rx.pool);

	dma_unmap_single(dev, msm_uport->rx.cmdptr_dmaaddr, sizeof(u32),
			 DMA_TO_DEVICE);
	dma_unmap_single(dev, msm_uport->tx.mapped_cmd_ptr_ptr, sizeof(u32),
			 DMA_TO_DEVICE);
	dma_unmap_single(dev, msm_uport->tx.mapped_cmd_ptr, sizeof(dmov_box),
			 DMA_TO_DEVICE);

	uart_remove_one_port(&msm_hs_driver, &msm_uport->uport);
	clk_put(msm_uport->clk);

	/* Free the tx resources */
	kfree(msm_uport->tx.command_ptr);
	kfree(msm_uport->tx.command_ptr_ptr);

	/* Free the rx resources */
	kfree(msm_uport->rx.command_ptr);
	kfree(msm_uport->rx.command_ptr_ptr);

	iounmap(msm_uport->uport.membase);

	return 0;
}

static int msm_hs_init_clk_locked(struct uart_port *uport)
{
	int ret;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	ret = clk_enable(msm_uport->clk);
	if (ret) {
		printk(KERN_ERR "Error could not turn on UART clk\n");
		return ret;
	}

	/* Set up the MREG/NREG/DREG/MNDREG */
	ret = clk_set_rate(msm_uport->clk, uport->uartclk);
	if (ret) {
		printk(KERN_WARNING "Error setting clock rate on UART\n");
		clk_disable(msm_uport->clk);
		return ret;
	}

	msm_uport->clk_state = MSM_HS_CLK_ON;
	return 0;
}

/* Enable and Disable clocks  (Used for power management) */
static void msm_hs_pm(struct uart_port *uport, unsigned int state,
		      unsigned int oldstate)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	if (use_low_power_rx_wakeup(msm_uport) ||
	    msm_uport->exit_lpm_cb)
		return;  /* ignore linux PM states,
			    use msm_hs_request_clock API */

	switch (state) {
	case 0:
		clk_enable(msm_uport->clk);
		break;
	case 3:
		clk_disable(msm_uport->clk);
		break;
	default:
		dev_err(uport->dev, "msm_serial: Unknown PM state %d\n",
			state);
	}
}

/*
 * programs the UARTDM_CSR register with correct bit rates
 *
 * Interrupts should be disabled before we are called, as
 * we modify Set Baud rate
 * Set receive stale interrupt level, dependent on Bit Rate
 * Goal is to have around 8 ms before indicate stale.
 * roundup (((Bit Rate * .008) / 10) + 1
 */
static void msm_hs_set_bps_locked(struct uart_port *uport,
				  unsigned int bps)
{
	unsigned long rxstale;
	unsigned long data;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	switch (bps) {
	case 300:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_75);
		rxstale = 1;
		break;
	case 600:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_150);
		rxstale = 1;
		break;
	case 1200:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_300);
		rxstale = 1;
		break;
	case 2400:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_600);
		rxstale = 1;
		break;
	case 4800:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_1200);
		rxstale = 1;
		break;
	case 9600:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_2400);
		rxstale = 2;
		break;
	case 14400:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_3600);
		rxstale = 3;
		break;
	case 19200:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_4800);
		rxstale = 4;
		break;
	case 28800:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_7200);
		rxstale = 6;
		break;
	case 38400:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_9600);
		rxstale = 8;
		break;
	case 57600:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_14400);
		rxstale = 16;
		break;
	case 76800:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_19200);
		rxstale = 16;
		break;
	case 115200:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_28800);
		rxstale = 31;
		break;
	case 230400:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_57600);
		rxstale = 31;
		break;
	case 460800:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_115200);
		rxstale = 31;
		break;
	case 4000000:
	case 3686400:
	case 3200000:
	case 3500000:
	case 3000000:
	case 2500000:
	case 1500000:
	case 1152000:
	case 1000000:
	case 921600:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_115200);
		rxstale = 31;
		break;
	default:
		msm_hs_write(uport, UARTDM_CSR_ADDR, UARTDM_CSR_2400);
		/* default to 9600 */
		bps = 9600;
		rxstale = 2;
		break;
	}
	if (bps > 460800)
		uport->uartclk = bps * 16;
	else
		uport->uartclk = UARTCLK;

	if (clk_set_rate(msm_uport->clk, uport->uartclk)) {
		printk(KERN_WARNING "Error setting clock rate on UART\n");
		return;
	}

	data = rxstale & UARTDM_IPR_STALE_LSB_BMSK;
	data |= UARTDM_IPR_STALE_TIMEOUT_MSB_BMSK & (rxstale << 2);

	msm_hs_write(uport, UARTDM_IPR_ADDR, data);
}

/*
 * termios :  new ktermios
 * oldtermios:  old ktermios previous setting
 *
 * Configure the serial port
 */
static void msm_hs_set_termios(struct uart_port *uport,
			       struct ktermios *termios,
			       struct ktermios *oldtermios)
{
	unsigned int bps;
	unsigned long data;
	unsigned long flags;
	unsigned int c_cflag = termios->c_cflag;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	spin_lock_irqsave(&uport->lock, flags);
	clk_enable(msm_uport->clk);

	/* 300 is the minimum baud support by the driver  */
	bps = uart_get_baud_rate(uport, termios, oldtermios, 200, 4000000);

	/* Temporary remapping  200 BAUD to 3.2 mbps */
	if (bps == 200)
		bps = 3200000;

	msm_hs_set_bps_locked(uport, bps);

	data = msm_hs_read(uport, UARTDM_MR2_ADDR);
	data &= ~UARTDM_MR2_PARITY_MODE_BMSK;
	/* set parity */
	if (PARENB == (c_cflag & PARENB)) {
		if (PARODD == (c_cflag & PARODD))
			data |= ODD_PARITY;
		else if (CMSPAR == (c_cflag & CMSPAR))
			data |= SPACE_PARITY;
		else
			data |= EVEN_PARITY;
	}

	/* Set bits per char */
	data &= ~UARTDM_MR2_BITS_PER_CHAR_BMSK;

	switch (c_cflag & CSIZE) {
	case CS5:
		data |= FIVE_BPC;
		break;
	case CS6:
		data |= SIX_BPC;
		break;
	case CS7:
		data |= SEVEN_BPC;
		break;
	default:
		data |= EIGHT_BPC;
		break;
	}
	/* stop bits */
	if (c_cflag & CSTOPB) {
		data |= STOP_BIT_TWO;
	} else {
		/* otherwise 1 stop bit */
		data |= STOP_BIT_ONE;
	}
	data |= UARTDM_MR2_ERROR_MODE_BMSK;
	/* write parity/bits per char/stop bit configuration */
	msm_hs_write(uport, UARTDM_MR2_ADDR, data);

	/* Configure HW flow control */
	data = msm_hs_read(uport, UARTDM_MR1_ADDR);

	data &= ~(UARTDM_MR1_CTS_CTL_BMSK | UARTDM_MR1_RX_RDY_CTL_BMSK);

	if (c_cflag & CRTSCTS) {
		data |= UARTDM_MR1_CTS_CTL_BMSK;
		data |= UARTDM_MR1_RX_RDY_CTL_BMSK;
	}

	msm_hs_write(uport, UARTDM_MR1_ADDR, data);

	uport->ignore_status_mask = termios->c_iflag & INPCK;
	uport->ignore_status_mask |= termios->c_iflag & IGNPAR;
	uport->read_status_mask = (termios->c_cflag & CREAD);

	msm_hs_write(uport, UARTDM_IMR_ADDR, 0);

	/* Set Transmit software time out */
	uart_update_timeout(uport, c_cflag, bps);

	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_RX);
	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_TX);

	if (msm_uport->rx.flush == FLUSH_NONE) {
		msm_uport->rx.flush = FLUSH_IGNORE;
		msm_dmov_stop_cmd(msm_uport->dma_rx_channel, NULL, 1);
	}

	msm_hs_write(uport, UARTDM_IMR_ADDR, msm_uport->imr_reg);

	clk_disable(msm_uport->clk);
	spin_unlock_irqrestore(&uport->lock, flags);
}

/*
 *  Standard API, Transmitter
 *  Any character in the transmit shift register is sent
 */
static unsigned int msm_hs_tx_empty(struct uart_port *uport)
{
	unsigned int data;
	unsigned int ret = 0;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	clk_enable(msm_uport->clk);

	data = msm_hs_read(uport, UARTDM_SR_ADDR);
	if (data & UARTDM_SR_TXEMT_BMSK)
		ret = TIOCSER_TEMT;

	clk_disable(msm_uport->clk);

	return ret;
}

/*
 *  Standard API, Stop transmitter.
 *  Any character in the transmit shift register is sent as
 *  well as the current data mover transfer .
 */
static void msm_hs_stop_tx_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	msm_uport->tx.tx_ready_int_en = 0;
}

/*
 *  Standard API, Stop receiver as soon as possible.
 *
 *  Function immediately terminates the operation of the
 *  channel receiver and any incoming characters are lost. None
 *  of the receiver status bits are affected by this command and
 *  characters that are already in the receive FIFO there.
 */
static void msm_hs_stop_rx_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	unsigned int data;

	clk_enable(msm_uport->clk);

	/* disable dlink */
	data = msm_hs_read(uport, UARTDM_DMEN_ADDR);
	data &= ~UARTDM_RX_DM_EN_BMSK;
	msm_hs_write(uport, UARTDM_DMEN_ADDR, data);

	/* Disable the receiver */
	if (msm_uport->rx.flush == FLUSH_NONE)
		msm_dmov_stop_cmd(msm_uport->dma_rx_channel, NULL, 1);

	if (msm_uport->rx.flush != FLUSH_SHUTDOWN)
		msm_uport->rx.flush = FLUSH_STOP;

	clk_disable(msm_uport->clk);
}

/*  Transmit the next chunk of data */
static void msm_hs_submit_tx_locked(struct uart_port *uport)
{
	int left;
	int tx_count;
	dma_addr_t src_addr;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct msm_hs_tx *tx = &msm_uport->tx;
	struct circ_buf *tx_buf = &msm_uport->uport.state->xmit;

	if (uart_circ_empty(tx_buf) || uport->state->port.tty->stopped) {
		msm_hs_stop_tx_locked(uport);
		return;
	}

	tx->dma_in_flight = 1;

	tx_count = uart_circ_chars_pending(tx_buf);

	if (UARTDM_TX_BUF_SIZE < tx_count)
		tx_count = UARTDM_TX_BUF_SIZE;

	left = UART_XMIT_SIZE - tx_buf->tail;

	if (tx_count > left)
		tx_count = left;

	src_addr = tx->dma_base + tx_buf->tail;
	dma_sync_single_for_device(uport->dev, src_addr, tx_count,
				   DMA_TO_DEVICE);

	tx->command_ptr->num_rows = (((tx_count + 15) >> 4) << 16) |
				     ((tx_count + 15) >> 4);
	tx->command_ptr->src_row_addr = src_addr;

	dma_sync_single_for_device(uport->dev, tx->mapped_cmd_ptr,
				   sizeof(dmov_box), DMA_TO_DEVICE);

	*tx->command_ptr_ptr = CMD_PTR_LP | DMOV_CMD_ADDR(tx->mapped_cmd_ptr);

	dma_sync_single_for_device(uport->dev, tx->mapped_cmd_ptr_ptr,
				   sizeof(u32), DMA_TO_DEVICE);

	/* Save tx_count to use in Callback */
	tx->tx_count = tx_count;
	msm_hs_write(uport, UARTDM_NCF_TX_ADDR, tx_count);

	/* Disable the tx_ready interrupt */
	msm_uport->imr_reg &= ~UARTDM_ISR_TX_READY_BMSK;
	msm_hs_write(uport, UARTDM_IMR_ADDR, msm_uport->imr_reg);
	msm_dmov_enqueue_cmd(msm_uport->dma_tx_channel, &tx->xfer);
}

/* Start to receive the next chunk of data */
static void msm_hs_start_rx_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_STALE_INT);
	msm_hs_write(uport, UARTDM_DMRX_ADDR, UARTDM_RX_BUF_SIZE);
	msm_hs_write(uport, UARTDM_CR_ADDR, STALE_EVENT_ENABLE);
	msm_uport->imr_reg |= UARTDM_ISR_RXLEV_BMSK;
	msm_hs_write(uport, UARTDM_IMR_ADDR, msm_uport->imr_reg);

	msm_uport->rx.flush = FLUSH_NONE;
	msm_dmov_enqueue_cmd(msm_uport->dma_rx_channel, &msm_uport->rx.xfer);

	/* might have finished RX and be ready to clock off */
	hrtimer_start(&msm_uport->clk_off_timer, msm_uport->clk_off_delay,
			HRTIMER_MODE_REL);
}

/* Enable the transmitter Interrupt */
static void msm_hs_start_tx_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	clk_enable(msm_uport->clk);

	if (msm_uport->exit_lpm_cb)
		msm_uport->exit_lpm_cb(uport);

	if (msm_uport->tx.tx_ready_int_en == 0) {
		msm_uport->tx.tx_ready_int_en = 1;
		msm_hs_submit_tx_locked(uport);
	}

	clk_disable(msm_uport->clk);
}

/*
 *  This routine is called when we are done with a DMA transfer
 *
 *  This routine is registered with Data mover when we set
 *  up a Data Mover transfer. It is called from Data mover ISR
 *  when the DMA transfer is done.
 */
static void msm_hs_dmov_tx_callback(struct msm_dmov_cmd *cmd_ptr,
					unsigned int result,
					struct msm_dmov_errdata *err)
{
	unsigned long flags;
	struct msm_hs_port *msm_uport;

	/* DMA did not finish properly */
	WARN_ON((((result & RSLT_FIFO_CNTR_BMSK) >> 28) == 1) &&
		!(result & RSLT_VLD));

	msm_uport = container_of(cmd_ptr, struct msm_hs_port, tx.xfer);

	spin_lock_irqsave(&msm_uport->uport.lock, flags);
	clk_enable(msm_uport->clk);

	msm_uport->imr_reg |= UARTDM_ISR_TX_READY_BMSK;
	msm_hs_write(&msm_uport->uport, UARTDM_IMR_ADDR, msm_uport->imr_reg);

	clk_disable(msm_uport->clk);
	spin_unlock_irqrestore(&msm_uport->uport.lock, flags);
}

/*
 * This routine is called when we are done with a DMA transfer or the
 * a flush has been sent to the data mover driver.
 *
 * This routine is registered with Data mover when we set up a Data Mover
 *  transfer. It is called from Data mover ISR when the DMA transfer is done.
 */
static void msm_hs_dmov_rx_callback(struct msm_dmov_cmd *cmd_ptr,
					unsigned int result,
					struct msm_dmov_errdata *err)
{
	int retval;
	int rx_count;
	unsigned long status;
	unsigned int error_f = 0;
	unsigned long flags;
	unsigned int flush;
	struct tty_struct *tty;
	struct tty_port *port;
	struct uart_port *uport;
	struct msm_hs_port *msm_uport;

	msm_uport = container_of(cmd_ptr, struct msm_hs_port, rx.xfer);
	uport = &msm_uport->uport;

	spin_lock_irqsave(&uport->lock, flags);
	clk_enable(msm_uport->clk);

	port = &uport->state->port;
	tty = port->tty;

	msm_hs_write(uport, UARTDM_CR_ADDR, STALE_EVENT_DISABLE);

	status = msm_hs_read(uport, UARTDM_SR_ADDR);

	/* overflow is not connect to data in a FIFO */
	if (unlikely((status & UARTDM_SR_OVERRUN_BMSK) &&
		     (uport->read_status_mask & CREAD))) {
		tty_insert_flip_char(port, 0, TTY_OVERRUN);
		uport->icount.buf_overrun++;
		error_f = 1;
	}

	if (!(uport->ignore_status_mask & INPCK))
		status = status & ~(UARTDM_SR_PAR_FRAME_BMSK);

	if (unlikely(status & UARTDM_SR_PAR_FRAME_BMSK)) {
		/* Can not tell difference between parity & frame error */
		uport->icount.parity++;
		error_f = 1;
		if (uport->ignore_status_mask & IGNPAR)
			tty_insert_flip_char(port, 0, TTY_PARITY);
	}

	if (error_f)
		msm_hs_write(uport, UARTDM_CR_ADDR, RESET_ERROR_STATUS);

	if (msm_uport->clk_req_off_state == CLK_REQ_OFF_FLUSH_ISSUED)
		msm_uport->clk_req_off_state = CLK_REQ_OFF_RXSTALE_FLUSHED;

	flush = msm_uport->rx.flush;
	if (flush == FLUSH_IGNORE)
		msm_hs_start_rx_locked(uport);
	if (flush == FLUSH_STOP)
		msm_uport->rx.flush = FLUSH_SHUTDOWN;
	if (flush >= FLUSH_DATA_INVALID)
		goto out;

	rx_count = msm_hs_read(uport, UARTDM_RX_TOTAL_SNAP_ADDR);

	if (0 != (uport->read_status_mask & CREAD)) {
		retval = tty_insert_flip_string(port, msm_uport->rx.buffer,
						rx_count);
		BUG_ON(retval != rx_count);
	}

	msm_hs_start_rx_locked(uport);

out:
	clk_disable(msm_uport->clk);

	spin_unlock_irqrestore(&uport->lock, flags);

	if (flush < FLUSH_DATA_INVALID)
		queue_work(msm_hs_workqueue, &msm_uport->rx.tty_work);
}

static void msm_hs_tty_flip_buffer_work(struct work_struct *work)
{
	struct msm_hs_port *msm_uport =
			container_of(work, struct msm_hs_port, rx.tty_work);

	tty_flip_buffer_push(&msm_uport->uport.state->port);
}

/*
 *  Standard API, Current states of modem control inputs
 *
 * Since CTS can be handled entirely by HARDWARE we always
 * indicate clear to send and count on the TX FIFO to block when
 * it fills up.
 *
 * - TIOCM_DCD
 * - TIOCM_CTS
 * - TIOCM_DSR
 * - TIOCM_RI
 *  (Unsupported) DCD and DSR will return them high. RI will return low.
 */
static unsigned int msm_hs_get_mctrl_locked(struct uart_port *uport)
{
	return TIOCM_DSR | TIOCM_CAR | TIOCM_CTS;
}

/*
 * True enables UART auto RFR, which indicates we are ready for data if the RX
 * buffer is not full. False disables auto RFR, and deasserts RFR to indicate
 * we are not ready for data. Must be called with UART clock on.
 */
static void set_rfr_locked(struct uart_port *uport, int auto_rfr)
{
	unsigned int data;

	data = msm_hs_read(uport, UARTDM_MR1_ADDR);

	if (auto_rfr) {
		/* enable auto ready-for-receiving */
		data |= UARTDM_MR1_RX_RDY_CTL_BMSK;
		msm_hs_write(uport, UARTDM_MR1_ADDR, data);
	} else {
		/* disable auto ready-for-receiving */
		data &= ~UARTDM_MR1_RX_RDY_CTL_BMSK;
		msm_hs_write(uport, UARTDM_MR1_ADDR, data);
		/* RFR is active low, set high */
		msm_hs_write(uport, UARTDM_CR_ADDR, RFR_HIGH);
	}
}

/*
 *  Standard API, used to set or clear RFR
 */
static void msm_hs_set_mctrl_locked(struct uart_port *uport,
				    unsigned int mctrl)
{
	unsigned int auto_rfr;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	clk_enable(msm_uport->clk);

	auto_rfr = TIOCM_RTS & mctrl ? 1 : 0;
	set_rfr_locked(uport, auto_rfr);

	clk_disable(msm_uport->clk);
}

/* Standard API, Enable modem status (CTS) interrupt  */
static void msm_hs_enable_ms_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	clk_enable(msm_uport->clk);

	/* Enable DELTA_CTS Interrupt */
	msm_uport->imr_reg |= UARTDM_ISR_DELTA_CTS_BMSK;
	msm_hs_write(uport, UARTDM_IMR_ADDR, msm_uport->imr_reg);

	clk_disable(msm_uport->clk);

}

/*
 *  Standard API, Break Signal
 *
 * Control the transmission of a break signal. ctl eq 0 => break
 * signal terminate ctl ne 0 => start break signal
 */
static void msm_hs_break_ctl(struct uart_port *uport, int ctl)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	clk_enable(msm_uport->clk);
	msm_hs_write(uport, UARTDM_CR_ADDR, ctl ? START_BREAK : STOP_BREAK);
	clk_disable(msm_uport->clk);
}

static void msm_hs_config_port(struct uart_port *uport, int cfg_flags)
{
	unsigned long flags;

	spin_lock_irqsave(&uport->lock, flags);
	if (cfg_flags & UART_CONFIG_TYPE) {
		uport->type = PORT_MSM;
		msm_hs_request_port(uport);
	}
	spin_unlock_irqrestore(&uport->lock, flags);
}

/*  Handle CTS changes (Called from interrupt handler) */
static void msm_hs_handle_delta_cts_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	clk_enable(msm_uport->clk);

	/* clear interrupt */
	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_CTS);
	uport->icount.cts++;

	clk_disable(msm_uport->clk);

	/* clear the IOCTL TIOCMIWAIT if called */
	wake_up_interruptible(&uport->state->port.delta_msr_wait);
}

/* check if the TX path is flushed, and if so clock off
 * returns 0 did not clock off, need to retry (still sending final byte)
 *        -1 did not clock off, do not retry
 *         1 if we clocked off
 */
static int msm_hs_check_clock_off_locked(struct uart_port *uport)
{
	unsigned long sr_status;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct circ_buf *tx_buf = &uport->state->xmit;

	/* Cancel if tx tty buffer is not empty, dma is in flight,
	 * or tx fifo is not empty, or rx fifo is not empty */
	if (msm_uport->clk_state != MSM_HS_CLK_REQUEST_OFF ||
	    !uart_circ_empty(tx_buf) || msm_uport->tx.dma_in_flight ||
	    (msm_uport->imr_reg & UARTDM_ISR_TXLEV_BMSK) ||
	    !(msm_uport->imr_reg & UARTDM_ISR_RXLEV_BMSK))  {
		return -1;
	}

	/* Make sure the uart is finished with the last byte */
	sr_status = msm_hs_read(uport, UARTDM_SR_ADDR);
	if (!(sr_status & UARTDM_SR_TXEMT_BMSK))
		return 0;  /* retry */

	/* Make sure forced RXSTALE flush complete */
	switch (msm_uport->clk_req_off_state) {
	case CLK_REQ_OFF_START:
		msm_uport->clk_req_off_state = CLK_REQ_OFF_RXSTALE_ISSUED;
		msm_hs_write(uport, UARTDM_CR_ADDR, FORCE_STALE_EVENT);
		return 0;  /* RXSTALE flush not complete - retry */
	case CLK_REQ_OFF_RXSTALE_ISSUED:
	case CLK_REQ_OFF_FLUSH_ISSUED:
		return 0;  /* RXSTALE flush not complete - retry */
	case CLK_REQ_OFF_RXSTALE_FLUSHED:
		break;  /* continue */
	}

	if (msm_uport->rx.flush != FLUSH_SHUTDOWN) {
		if (msm_uport->rx.flush == FLUSH_NONE)
			msm_hs_stop_rx_locked(uport);
		return 0;  /* come back later to really clock off */
	}

	/* we really want to clock off */
	clk_disable(msm_uport->clk);
	msm_uport->clk_state = MSM_HS_CLK_OFF;

	if (use_low_power_rx_wakeup(msm_uport)) {
		msm_uport->rx_wakeup.ignore = 1;
		enable_irq(msm_uport->rx_wakeup.irq);
	}
	return 1;
}

static enum hrtimer_restart msm_hs_clk_off_retry(struct hrtimer *timer)
{
	unsigned long flags;
	int ret = HRTIMER_NORESTART;
	struct msm_hs_port *msm_uport = container_of(timer, struct msm_hs_port,
						     clk_off_timer);
	struct uart_port *uport = &msm_uport->uport;

	spin_lock_irqsave(&uport->lock, flags);

	if (!msm_hs_check_clock_off_locked(uport)) {
		hrtimer_forward_now(timer, msm_uport->clk_off_delay);
		ret = HRTIMER_RESTART;
	}

	spin_unlock_irqrestore(&uport->lock, flags);

	return ret;
}

static irqreturn_t msm_hs_isr(int irq, void *dev)
{
	unsigned long flags;
	unsigned long isr_status;
	struct msm_hs_port *msm_uport = dev;
	struct uart_port *uport = &msm_uport->uport;
	struct circ_buf *tx_buf = &uport->state->xmit;
	struct msm_hs_tx *tx = &msm_uport->tx;
	struct msm_hs_rx *rx = &msm_uport->rx;

	spin_lock_irqsave(&uport->lock, flags);

	isr_status = msm_hs_read(uport, UARTDM_MISR_ADDR);

	/* Uart RX starting */
	if (isr_status & UARTDM_ISR_RXLEV_BMSK) {
		msm_uport->imr_reg &= ~UARTDM_ISR_RXLEV_BMSK;
		msm_hs_write(uport, UARTDM_IMR_ADDR, msm_uport->imr_reg);
	}
	/* Stale rx interrupt */
	if (isr_status & UARTDM_ISR_RXSTALE_BMSK) {
		msm_hs_write(uport, UARTDM_CR_ADDR, STALE_EVENT_DISABLE);
		msm_hs_write(uport, UARTDM_CR_ADDR, RESET_STALE_INT);

		if (msm_uport->clk_req_off_state == CLK_REQ_OFF_RXSTALE_ISSUED)
			msm_uport->clk_req_off_state =
					CLK_REQ_OFF_FLUSH_ISSUED;
		if (rx->flush == FLUSH_NONE) {
			rx->flush = FLUSH_DATA_READY;
			msm_dmov_stop_cmd(msm_uport->dma_rx_channel, NULL, 1);
		}
	}
	/* tx ready interrupt */
	if (isr_status & UARTDM_ISR_TX_READY_BMSK) {
		/* Clear  TX Ready */
		msm_hs_write(uport, UARTDM_CR_ADDR, CLEAR_TX_READY);

		if (msm_uport->clk_state == MSM_HS_CLK_REQUEST_OFF) {
			msm_uport->imr_reg |= UARTDM_ISR_TXLEV_BMSK;
			msm_hs_write(uport, UARTDM_IMR_ADDR,
				     msm_uport->imr_reg);
		}

		/* Complete DMA TX transactions and submit new transactions */
		tx_buf->tail = (tx_buf->tail + tx->tx_count) & ~UART_XMIT_SIZE;

		tx->dma_in_flight = 0;

		uport->icount.tx += tx->tx_count;
		if (tx->tx_ready_int_en)
			msm_hs_submit_tx_locked(uport);

		if (uart_circ_chars_pending(tx_buf) < WAKEUP_CHARS)
			uart_write_wakeup(uport);
	}
	if (isr_status & UARTDM_ISR_TXLEV_BMSK) {
		/* TX FIFO is empty */
		msm_uport->imr_reg &= ~UARTDM_ISR_TXLEV_BMSK;
		msm_hs_write(uport, UARTDM_IMR_ADDR, msm_uport->imr_reg);
		if (!msm_hs_check_clock_off_locked(uport))
			hrtimer_start(&msm_uport->clk_off_timer,
				      msm_uport->clk_off_delay,
				      HRTIMER_MODE_REL);
	}

	/* Change in CTS interrupt */
	if (isr_status & UARTDM_ISR_DELTA_CTS_BMSK)
		msm_hs_handle_delta_cts_locked(uport);

	spin_unlock_irqrestore(&uport->lock, flags);

	return IRQ_HANDLED;
}

void msm_hs_request_clock_off_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	if (msm_uport->clk_state == MSM_HS_CLK_ON) {
		msm_uport->clk_state = MSM_HS_CLK_REQUEST_OFF;
		msm_uport->clk_req_off_state = CLK_REQ_OFF_START;
		if (!use_low_power_rx_wakeup(msm_uport))
			set_rfr_locked(uport, 0);
		msm_uport->imr_reg |= UARTDM_ISR_TXLEV_BMSK;
		msm_hs_write(uport, UARTDM_IMR_ADDR, msm_uport->imr_reg);
	}
}

/**
 * msm_hs_request_clock_off - request to (i.e. asynchronously) turn off uart
 * clock once pending TX is flushed and Rx DMA command is terminated.
 * @uport: uart_port structure for the device instance.
 *
 * This functions puts the device into a partially active low power mode. It
 * waits to complete all pending tx transactions, flushes ongoing Rx DMA
 * command and terminates UART side Rx transaction, puts UART HW in non DMA
 * mode and then clocks off the device. A client calls this when no UART
 * data is expected. msm_request_clock_on() must be called before any further
 * UART can be sent or received.
 */
void msm_hs_request_clock_off(struct uart_port *uport)
{
	unsigned long flags;

	spin_lock_irqsave(&uport->lock, flags);
	msm_hs_request_clock_off_locked(uport);
	spin_unlock_irqrestore(&uport->lock, flags);
}

void msm_hs_request_clock_on_locked(struct uart_port *uport)
{
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	unsigned int data;

	switch (msm_uport->clk_state) {
	case MSM_HS_CLK_OFF:
		clk_enable(msm_uport->clk);
		disable_irq_nosync(msm_uport->rx_wakeup.irq);
		/* fall-through */
	case MSM_HS_CLK_REQUEST_OFF:
		if (msm_uport->rx.flush == FLUSH_STOP ||
		    msm_uport->rx.flush == FLUSH_SHUTDOWN) {
			msm_hs_write(uport, UARTDM_CR_ADDR, RESET_RX);
			data = msm_hs_read(uport, UARTDM_DMEN_ADDR);
			data |= UARTDM_RX_DM_EN_BMSK;
			msm_hs_write(uport, UARTDM_DMEN_ADDR, data);
		}
		hrtimer_try_to_cancel(&msm_uport->clk_off_timer);
		if (msm_uport->rx.flush == FLUSH_SHUTDOWN)
			msm_hs_start_rx_locked(uport);
		if (!use_low_power_rx_wakeup(msm_uport))
			set_rfr_locked(uport, 1);
		if (msm_uport->rx.flush == FLUSH_STOP)
			msm_uport->rx.flush = FLUSH_IGNORE;
		msm_uport->clk_state = MSM_HS_CLK_ON;
		break;
	case MSM_HS_CLK_ON:
		break;
	case MSM_HS_CLK_PORT_OFF:
		break;
	}
}

/**
 * msm_hs_request_clock_on - Switch the device from partially active low
 * power mode to fully active (i.e. clock on) mode.
 * @uport: uart_port structure for the device.
 *
 * This function switches on the input clock, puts UART HW into DMA mode
 * and enqueues an Rx DMA command if the device was in partially active
 * mode. It has no effect if called with the device in inactive state.
 */
void msm_hs_request_clock_on(struct uart_port *uport)
{
	unsigned long flags;

	spin_lock_irqsave(&uport->lock, flags);
	msm_hs_request_clock_on_locked(uport);
	spin_unlock_irqrestore(&uport->lock, flags);
}

static irqreturn_t msm_hs_rx_wakeup_isr(int irq, void *dev)
{
	unsigned int wakeup = 0;
	unsigned long flags;
	struct msm_hs_port *msm_uport = dev;
	struct uart_port *uport = &msm_uport->uport;

	spin_lock_irqsave(&uport->lock, flags);
	if (msm_uport->clk_state == MSM_HS_CLK_OFF) {
		/* ignore the first irq - it is a pending irq that occurred
		 * before enable_irq() */
		if (msm_uport->rx_wakeup.ignore)
			msm_uport->rx_wakeup.ignore = 0;
		else
			wakeup = 1;
	}

	if (wakeup) {
		/* the uart was clocked off during an rx, wake up and
		 * optionally inject char into tty rx */
		msm_hs_request_clock_on_locked(uport);
		if (msm_uport->rx_wakeup.inject_rx) {
			tty_insert_flip_char(&uport->state->port,
					     msm_uport->rx_wakeup.rx_to_inject,
					     TTY_NORMAL);
			queue_work(msm_hs_workqueue, &msm_uport->rx.tty_work);
		}
	}

	spin_unlock_irqrestore(&uport->lock, flags);

	return IRQ_HANDLED;
}

static const char *msm_hs_type(struct uart_port *port)
{
	return (port->type == PORT_MSM) ? "MSM_HS_UART" : NULL;
}

/* Called when port is opened */
static int msm_hs_startup(struct uart_port *uport)
{
	int ret;
	int rfr_level;
	unsigned long flags;
	unsigned int data;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct circ_buf *tx_buf = &uport->state->xmit;
	struct msm_hs_tx *tx = &msm_uport->tx;
	struct msm_hs_rx *rx = &msm_uport->rx;

	rfr_level = uport->fifosize;
	if (rfr_level > 16)
		rfr_level -= 16;

	tx->dma_base = dma_map_single(uport->dev, tx_buf->buf, UART_XMIT_SIZE,
				      DMA_TO_DEVICE);

	/* do not let tty layer execute RX in global workqueue, use a
	 * dedicated workqueue managed by this driver */
	uport->state->port.low_latency = 1;

	/* turn on uart clk */
	ret = msm_hs_init_clk_locked(uport);
	if (unlikely(ret)) {
		printk(KERN_ERR "Turning uartclk failed!\n");
		goto err_msm_hs_init_clk;
	}

	/* Set auto RFR Level */
	data = msm_hs_read(uport, UARTDM_MR1_ADDR);
	data &= ~UARTDM_MR1_AUTO_RFR_LEVEL1_BMSK;
	data &= ~UARTDM_MR1_AUTO_RFR_LEVEL0_BMSK;
	data |= (UARTDM_MR1_AUTO_RFR_LEVEL1_BMSK & (rfr_level << 2));
	data |= (UARTDM_MR1_AUTO_RFR_LEVEL0_BMSK & rfr_level);
	msm_hs_write(uport, UARTDM_MR1_ADDR, data);

	/* Make sure RXSTALE count is non-zero */
	data = msm_hs_read(uport, UARTDM_IPR_ADDR);
	if (!data) {
		data |= 0x1f & UARTDM_IPR_STALE_LSB_BMSK;
		msm_hs_write(uport, UARTDM_IPR_ADDR, data);
	}

	/* Enable Data Mover Mode */
	data = UARTDM_TX_DM_EN_BMSK | UARTDM_RX_DM_EN_BMSK;
	msm_hs_write(uport, UARTDM_DMEN_ADDR, data);

	/* Reset TX */
	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_TX);
	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_RX);
	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_ERROR_STATUS);
	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_BREAK_INT);
	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_STALE_INT);
	msm_hs_write(uport, UARTDM_CR_ADDR, RESET_CTS);
	msm_hs_write(uport, UARTDM_CR_ADDR, RFR_LOW);
	/* Turn on Uart Receiver */
	msm_hs_write(uport, UARTDM_CR_ADDR, UARTDM_CR_RX_EN_BMSK);

	/* Turn on Uart Transmitter */
	msm_hs_write(uport, UARTDM_CR_ADDR, UARTDM_CR_TX_EN_BMSK);

	/* Initialize the tx */
	tx->tx_ready_int_en = 0;
	tx->dma_in_flight = 0;

	tx->xfer.complete_func = msm_hs_dmov_tx_callback;
	tx->xfer.execute_func = NULL;

	tx->command_ptr->cmd = CMD_LC |
	    CMD_DST_CRCI(msm_uport->dma_tx_crci) | CMD_MODE_BOX;

	tx->command_ptr->src_dst_len = (MSM_UARTDM_BURST_SIZE << 16)
					   | (MSM_UARTDM_BURST_SIZE);

	tx->command_ptr->row_offset = (MSM_UARTDM_BURST_SIZE << 16);

	tx->command_ptr->dst_row_addr =
	    msm_uport->uport.mapbase + UARTDM_TF_ADDR;


	/* Turn on Uart Receive */
	rx->xfer.complete_func = msm_hs_dmov_rx_callback;
	rx->xfer.execute_func = NULL;

	rx->command_ptr->cmd = CMD_LC |
	    CMD_SRC_CRCI(msm_uport->dma_rx_crci) | CMD_MODE_BOX;

	rx->command_ptr->src_dst_len = (MSM_UARTDM_BURST_SIZE << 16)
					   | (MSM_UARTDM_BURST_SIZE);
	rx->command_ptr->row_offset =  MSM_UARTDM_BURST_SIZE;
	rx->command_ptr->src_row_addr = uport->mapbase + UARTDM_RF_ADDR;


	msm_uport->imr_reg |= UARTDM_ISR_RXSTALE_BMSK;
	/* Enable reading the current CTS, no harm even if CTS is ignored */
	msm_uport->imr_reg |= UARTDM_ISR_CURRENT_CTS_BMSK;

	msm_hs_write(uport, UARTDM_TFWR_ADDR, 0);  /* TXLEV on empty TX fifo */


	ret = request_irq(uport->irq, msm_hs_isr, IRQF_TRIGGER_HIGH,
			  "msm_hs_uart", msm_uport);
	if (unlikely(ret)) {
		printk(KERN_ERR "Request msm_hs_uart IRQ failed!\n");
		goto err_request_irq;
	}
	if (use_low_power_rx_wakeup(msm_uport)) {
		ret = request_irq(msm_uport->rx_wakeup.irq,
				  msm_hs_rx_wakeup_isr,
				  IRQF_TRIGGER_FALLING,
				  "msm_hs_rx_wakeup", msm_uport);
		if (unlikely(ret)) {
			printk(KERN_ERR "Request msm_hs_rx_wakeup IRQ failed!\n");
			free_irq(uport->irq, msm_uport);
			goto err_request_irq;
		}
		disable_irq(msm_uport->rx_wakeup.irq);
	}

	spin_lock_irqsave(&uport->lock, flags);

	msm_hs_write(uport, UARTDM_RFWR_ADDR, 0);
	msm_hs_start_rx_locked(uport);

	spin_unlock_irqrestore(&uport->lock, flags);
	ret = pm_runtime_set_active(uport->dev);
	if (ret)
		dev_err(uport->dev, "set active error:%d\n", ret);
	pm_runtime_enable(uport->dev);

	return 0;

err_request_irq:
err_msm_hs_init_clk:
	dma_unmap_single(uport->dev, tx->dma_base,
				UART_XMIT_SIZE, DMA_TO_DEVICE);
	return ret;
}

/* Initialize tx and rx data structures */
static int uartdm_init_port(struct uart_port *uport)
{
	int ret = 0;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);
	struct msm_hs_tx *tx = &msm_uport->tx;
	struct msm_hs_rx *rx = &msm_uport->rx;

	/* Allocate the command pointer. Needs to be 64 bit aligned */
	tx->command_ptr = kmalloc(sizeof(dmov_box), GFP_KERNEL | __GFP_DMA);
	if (!tx->command_ptr)
		return -ENOMEM;

	tx->command_ptr_ptr = kmalloc(sizeof(u32), GFP_KERNEL | __GFP_DMA);
	if (!tx->command_ptr_ptr) {
		ret = -ENOMEM;
		goto err_tx_command_ptr_ptr;
	}

	tx->mapped_cmd_ptr = dma_map_single(uport->dev, tx->command_ptr,
					    sizeof(dmov_box), DMA_TO_DEVICE);
	tx->mapped_cmd_ptr_ptr = dma_map_single(uport->dev,
						tx->command_ptr_ptr,
						sizeof(u32), DMA_TO_DEVICE);
	tx->xfer.cmdptr = DMOV_CMD_ADDR(tx->mapped_cmd_ptr_ptr);

	init_waitqueue_head(&rx->wait);

	rx->pool = dma_pool_create("rx_buffer_pool", uport->dev,
				   UARTDM_RX_BUF_SIZE, 16, 0);
	if (!rx->pool) {
		pr_err("%s(): cannot allocate rx_buffer_pool", __func__);
		ret = -ENOMEM;
		goto err_dma_pool_create;
	}

	rx->buffer = dma_pool_alloc(rx->pool, GFP_KERNEL, &rx->rbuffer);
	if (!rx->buffer) {
		pr_err("%s(): cannot allocate rx->buffer", __func__);
		ret = -ENOMEM;
		goto err_dma_pool_alloc;
	}

	/* Allocate the command pointer. Needs to be 64 bit aligned */
	rx->command_ptr = kmalloc(sizeof(dmov_box), GFP_KERNEL | __GFP_DMA);
	if (!rx->command_ptr) {
		pr_err("%s(): cannot allocate rx->command_ptr", __func__);
		ret = -ENOMEM;
		goto err_rx_command_ptr;
	}

	rx->command_ptr_ptr = kmalloc(sizeof(u32), GFP_KERNEL | __GFP_DMA);
	if (!rx->command_ptr_ptr) {
		pr_err("%s(): cannot allocate rx->command_ptr_ptr", __func__);
		ret = -ENOMEM;
		goto err_rx_command_ptr_ptr;
	}

	rx->command_ptr->num_rows = ((UARTDM_RX_BUF_SIZE >> 4) << 16) |
					 (UARTDM_RX_BUF_SIZE >> 4);

	rx->command_ptr->dst_row_addr = rx->rbuffer;

	rx->mapped_cmd_ptr = dma_map_single(uport->dev, rx->command_ptr,
					    sizeof(dmov_box), DMA_TO_DEVICE);

	*rx->command_ptr_ptr = CMD_PTR_LP | DMOV_CMD_ADDR(rx->mapped_cmd_ptr);

	rx->cmdptr_dmaaddr = dma_map_single(uport->dev, rx->command_ptr_ptr,
					    sizeof(u32), DMA_TO_DEVICE);
	rx->xfer.cmdptr = DMOV_CMD_ADDR(rx->cmdptr_dmaaddr);

	INIT_WORK(&rx->tty_work, msm_hs_tty_flip_buffer_work);

	return ret;

err_rx_command_ptr_ptr:
	kfree(rx->command_ptr);
err_rx_command_ptr:
	dma_pool_free(msm_uport->rx.pool, msm_uport->rx.buffer,
						msm_uport->rx.rbuffer);
err_dma_pool_alloc:
	dma_pool_destroy(msm_uport->rx.pool);
err_dma_pool_create:
	dma_unmap_single(uport->dev, msm_uport->tx.mapped_cmd_ptr_ptr,
				sizeof(u32), DMA_TO_DEVICE);
	dma_unmap_single(uport->dev, msm_uport->tx.mapped_cmd_ptr,
				sizeof(dmov_box), DMA_TO_DEVICE);
	kfree(msm_uport->tx.command_ptr_ptr);
err_tx_command_ptr_ptr:
	kfree(msm_uport->tx.command_ptr);
	return ret;
}

static int msm_hs_probe(struct platform_device *pdev)
{
	int ret;
	struct uart_port *uport;
	struct msm_hs_port *msm_uport;
	struct resource *resource;
	const struct msm_serial_hs_platform_data *pdata =
						pdev->dev.platform_data;

	if (pdev->id < 0 || pdev->id >= UARTDM_NR) {
		printk(KERN_ERR "Invalid plaform device ID = %d\n", pdev->id);
		return -EINVAL;
	}

	msm_uport = &q_uart_port[pdev->id];
	uport = &msm_uport->uport;

	uport->dev = &pdev->dev;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!resource))
		return -ENXIO;

	uport->mapbase = resource->start;
	uport->irq = platform_get_irq(pdev, 0);
	if (unlikely(uport->irq < 0))
		return -ENXIO;

	if (unlikely(irq_set_irq_wake(uport->irq, 1)))
		return -ENXIO;

	if (pdata == NULL || pdata->rx_wakeup_irq < 0)
		msm_uport->rx_wakeup.irq = -1;
	else {
		msm_uport->rx_wakeup.irq = pdata->rx_wakeup_irq;
		msm_uport->rx_wakeup.ignore = 1;
		msm_uport->rx_wakeup.inject_rx = pdata->inject_rx_on_wakeup;
		msm_uport->rx_wakeup.rx_to_inject = pdata->rx_to_inject;

		if (unlikely(msm_uport->rx_wakeup.irq < 0))
			return -ENXIO;

		if (unlikely(irq_set_irq_wake(msm_uport->rx_wakeup.irq, 1)))
			return -ENXIO;
	}

	if (pdata == NULL)
		msm_uport->exit_lpm_cb = NULL;
	else
		msm_uport->exit_lpm_cb = pdata->exit_lpm_cb;

	resource = platform_get_resource_byname(pdev, IORESOURCE_DMA,
						"uartdm_channels");
	if (unlikely(!resource))
		return -ENXIO;

	msm_uport->dma_tx_channel = resource->start;
	msm_uport->dma_rx_channel = resource->end;

	resource = platform_get_resource_byname(pdev, IORESOURCE_DMA,
						"uartdm_crci");
	if (unlikely(!resource))
		return -ENXIO;

	msm_uport->dma_tx_crci = resource->start;
	msm_uport->dma_rx_crci = resource->end;

	uport->iotype = UPIO_MEM;
	uport->fifosize = UART_FIFOSIZE;
	uport->ops = &msm_hs_ops;
	uport->flags = UPF_BOOT_AUTOCONF;
	uport->uartclk = UARTCLK;
	msm_uport->imr_reg = 0x0;
	msm_uport->clk = clk_get(&pdev->dev, "uartdm_clk");
	if (IS_ERR(msm_uport->clk))
		return PTR_ERR(msm_uport->clk);

	ret = uartdm_init_port(uport);
	if (unlikely(ret))
		return ret;

	msm_uport->clk_state = MSM_HS_CLK_PORT_OFF;
	hrtimer_init(&msm_uport->clk_off_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	msm_uport->clk_off_timer.function = msm_hs_clk_off_retry;
	msm_uport->clk_off_delay = ktime_set(0, 1000000);  /* 1ms */

	uport->line = pdev->id;
	return uart_add_one_port(&msm_hs_driver, uport);
}

static int __init msm_serial_hs_init(void)
{
	int ret, i;

	/* Init all UARTS as non-configured */
	for (i = 0; i < UARTDM_NR; i++)
		q_uart_port[i].uport.type = PORT_UNKNOWN;

	msm_hs_workqueue = create_singlethread_workqueue("msm_serial_hs");
	if (unlikely(!msm_hs_workqueue))
		return -ENOMEM;

	ret = uart_register_driver(&msm_hs_driver);
	if (unlikely(ret)) {
		printk(KERN_ERR "%s failed to load\n", __func__);
		goto err_uart_register_driver;
	}

	ret = platform_driver_register(&msm_serial_hs_platform_driver);
	if (ret) {
		printk(KERN_ERR "%s failed to load\n", __func__);
		goto err_platform_driver_register;
	}

	return ret;

err_platform_driver_register:
	uart_unregister_driver(&msm_hs_driver);
err_uart_register_driver:
	destroy_workqueue(msm_hs_workqueue);
	return ret;
}
module_init(msm_serial_hs_init);

/*
 *  Called by the upper layer when port is closed.
 *     - Disables the port
 *     - Unhook the ISR
 */
static void msm_hs_shutdown(struct uart_port *uport)
{
	unsigned long flags;
	struct msm_hs_port *msm_uport = UARTDM_TO_MSM(uport);

	BUG_ON(msm_uport->rx.flush < FLUSH_STOP);

	spin_lock_irqsave(&uport->lock, flags);
	clk_enable(msm_uport->clk);

	/* Disable the transmitter */
	msm_hs_write(uport, UARTDM_CR_ADDR, UARTDM_CR_TX_DISABLE_BMSK);
	/* Disable the receiver */
	msm_hs_write(uport, UARTDM_CR_ADDR, UARTDM_CR_RX_DISABLE_BMSK);

	pm_runtime_disable(uport->dev);
	pm_runtime_set_suspended(uport->dev);

	/* Free the interrupt */
	free_irq(uport->irq, msm_uport);
	if (use_low_power_rx_wakeup(msm_uport))
		free_irq(msm_uport->rx_wakeup.irq, msm_uport);

	msm_uport->imr_reg = 0;
	msm_hs_write(uport, UARTDM_IMR_ADDR, msm_uport->imr_reg);

	wait_event(msm_uport->rx.wait, msm_uport->rx.flush == FLUSH_SHUTDOWN);

	clk_disable(msm_uport->clk);  /* to balance local clk_enable() */
	if (msm_uport->clk_state != MSM_HS_CLK_OFF)
		clk_disable(msm_uport->clk);  /* to balance clk_state */
	msm_uport->clk_state = MSM_HS_CLK_PORT_OFF;

	dma_unmap_single(uport->dev, msm_uport->tx.dma_base,
			 UART_XMIT_SIZE, DMA_TO_DEVICE);

	spin_unlock_irqrestore(&uport->lock, flags);

	if (cancel_work_sync(&msm_uport->rx.tty_work))
		msm_hs_tty_flip_buffer_work(&msm_uport->rx.tty_work);
}

static void __exit msm_serial_hs_exit(void)
{
	flush_workqueue(msm_hs_workqueue);
	destroy_workqueue(msm_hs_workqueue);
	platform_driver_unregister(&msm_serial_hs_platform_driver);
	uart_unregister_driver(&msm_hs_driver);
}
module_exit(msm_serial_hs_exit);

#ifdef CONFIG_PM_RUNTIME
static int msm_hs_runtime_idle(struct device *dev)
{
	/*
	 * returning success from idle results in runtime suspend to be
	 * called
	 */
	return 0;
}

static int msm_hs_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct
						    platform_device, dev);
	struct msm_hs_port *msm_uport = &q_uart_port[pdev->id];

	msm_hs_request_clock_on(&msm_uport->uport);
	return 0;
}

static int msm_hs_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct
						    platform_device, dev);
	struct msm_hs_port *msm_uport = &q_uart_port[pdev->id];

	msm_hs_request_clock_off(&msm_uport->uport);
	return 0;
}
#else
#define msm_hs_runtime_idle NULL
#define msm_hs_runtime_resume NULL
#define msm_hs_runtime_suspend NULL
#endif

static const struct dev_pm_ops msm_hs_dev_pm_ops = {
	.runtime_suspend = msm_hs_runtime_suspend,
	.runtime_resume  = msm_hs_runtime_resume,
	.runtime_idle    = msm_hs_runtime_idle,
};

static struct platform_driver msm_serial_hs_platform_driver = {
	.probe = msm_hs_probe,
	.remove = msm_hs_remove,
	.driver = {
		.name = "msm_serial_hs",
		.owner = THIS_MODULE,
		.pm   = &msm_hs_dev_pm_ops,
	},
};

static struct uart_driver msm_hs_driver = {
	.owner = THIS_MODULE,
	.driver_name = "msm_serial_hs",
	.dev_name = "ttyHS",
	.nr = UARTDM_NR,
	.cons = 0,
};

static struct uart_ops msm_hs_ops = {
	.tx_empty = msm_hs_tx_empty,
	.set_mctrl = msm_hs_set_mctrl_locked,
	.get_mctrl = msm_hs_get_mctrl_locked,
	.stop_tx = msm_hs_stop_tx_locked,
	.start_tx = msm_hs_start_tx_locked,
	.stop_rx = msm_hs_stop_rx_locked,
	.enable_ms = msm_hs_enable_ms_locked,
	.break_ctl = msm_hs_break_ctl,
	.startup = msm_hs_startup,
	.shutdown = msm_hs_shutdown,
	.set_termios = msm_hs_set_termios,
	.pm = msm_hs_pm,
	.type = msm_hs_type,
	.config_port = msm_hs_config_port,
	.release_port = msm_hs_release_port,
	.request_port = msm_hs_request_port,
};

MODULE_DESCRIPTION("High Speed UART Driver for the MSM chipset");
MODULE_VERSION("1.2");
MODULE_LICENSE("GPL v2");
