/*
 * arch/arm/mach-omap2/serial.c
 *
 * OMAP2 serial support.
 *
 * Copyright (C) 2005-2008 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *
 * Major rework for PM support by Kevin Hilman
 *
 * Based off of arch/arm/mach-omap/omap1/serial.c
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <plat/common.h>
#include <plat/board.h>
#include <plat/clock.h>
#include <plat/control.h>

#include "prm.h"
#include "pm.h"
#include "prm-regbits-34xx.h"

#define UART_OMAP_NO_EMPTY_FIFO_READ_IP_REV	0x52
#define UART_OMAP_WER		0x17	/* Wake-up enable register */

#define UART_ERRATA_FIFO_FULL_ABORT	(0x1 << 0)
#define UART_ERRATA_i202_MDR1_ACCESS	(0x1 << 1)

/*
 * NOTE: By default the serial timeout is disabled as it causes lost characters
 * over the serial ports. This means that the UART clocks will stay on until
 * disabled via sysfs. This also causes that any deeper omap sleep states are
 * blocked. 
 */
#define DEFAULT_TIMEOUT 0

struct omap_uart_state {
	int num;
	int can_sleep;
	struct timer_list timer;
	u32 timeout;

	void __iomem *wk_st;
	void __iomem *wk_en;
	u32 wk_mask;
	u32 padconf;

	struct clk *ick;
	struct clk *fck;
	int clocked;

	struct plat_serial8250_port *p;
	struct list_head node;
	struct platform_device pdev;

	u32 errata;
#if defined(CONFIG_ARCH_OMAP3) && defined(CONFIG_PM)
	int context_valid;

	/* Registers to be saved/restored for OFF-mode */
	u16 dll;
	u16 dlh;
	u16 ier;
	u16 sysc;
	u16 scr;
	u16 wer;
	u16 mcr;
#endif
};

static LIST_HEAD(uart_list);

static struct plat_serial8250_port serial_platform_data0[] = {
	{
		.irq		= 72,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= OMAP24XX_BASE_BAUD * 16,
	}, {
		.flags		= 0
	}
};

static struct plat_serial8250_port serial_platform_data1[] = {
	{
		.irq		= 73,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= OMAP24XX_BASE_BAUD * 16,
	}, {
		.flags		= 0
	}
};

static struct plat_serial8250_port serial_platform_data2[] = {
	{
		.irq		= 74,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= OMAP24XX_BASE_BAUD * 16,
	}, {
		.flags		= 0
	}
};

static struct plat_serial8250_port serial_platform_data3[] = {
	{
		.irq		= 70,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= OMAP24XX_BASE_BAUD * 16,
	}, {
		.flags		= 0
	}
};

void __init omap2_set_globals_uart(struct omap_globals *omap2_globals)
{
	serial_platform_data0[0].mapbase = omap2_globals->uart1_phys;
	serial_platform_data1[0].mapbase = omap2_globals->uart2_phys;
	serial_platform_data2[0].mapbase = omap2_globals->uart3_phys;
	serial_platform_data3[0].mapbase = omap2_globals->uart4_phys;
}

static inline unsigned int __serial_read_reg(struct uart_port *up,
					   int offset)
{
	offset <<= up->regshift;
	return (unsigned int)__raw_readb(up->membase + offset);
}

static inline unsigned int serial_read_reg(struct plat_serial8250_port *up,
					   int offset)
{
	offset <<= up->regshift;
	return (unsigned int)__raw_readb(up->membase + offset);
}

static inline void __serial_write_reg(struct uart_port *up, int offset,
		int value)
{
	offset <<= up->regshift;
	__raw_writeb(value, up->membase + offset);
}

static inline void serial_write_reg(struct plat_serial8250_port *p, int offset,
				    int value)
{
	offset <<= p->regshift;
	__raw_writeb(value, p->membase + offset);
}

/*
 * Internal UARTs need to be initialized for the 8250 autoconfig to work
 * properly. Note that the TX watermark initialization may not be needed
 * once the 8250.c watermark handling code is merged.
 */
static inline void __init omap_uart_reset(struct omap_uart_state *uart)
{
	struct plat_serial8250_port *p = uart->p;

	serial_write_reg(p, UART_OMAP_MDR1, 0x07);
	serial_write_reg(p, UART_OMAP_SCR, 0x08);
	serial_write_reg(p, UART_OMAP_MDR1, 0x00);
	serial_write_reg(p, UART_OMAP_SYSC, (0x02 << 3) | (1 << 2) | (1 << 0));
}

#if defined(CONFIG_PM) && defined(CONFIG_ARCH_OMAP3)

/*
 * Work Around for Errata i202 (3430 - 1.12, 3630 - 1.6)
 * The access to uart register after MDR1 Access
 * causes UART to corrupt data.
 *
 * Need a delay =
 * 5 L4 clock cycles + 5 UART functional clock cycle (@48MHz = ~0.2uS)
 * give 10 times as much
 */
static void omap_uart_mdr1_errataset(struct omap_uart_state *uart, u8 mdr1_val,
		u8 fcr_val)
{
	struct plat_serial8250_port *p = uart->p;
	u8 timeout = 255;

	serial_write_reg(p, UART_OMAP_MDR1, mdr1_val);
	udelay(2);
	serial_write_reg(p, UART_FCR, fcr_val | UART_FCR_CLEAR_XMIT |
			UART_FCR_CLEAR_RCVR);
	/*
	 * Wait for FIFO to empty: when empty, RX_FIFO_E bit is 0 and
	 * TX_FIFO_E bit is 1.
	 */
	while (UART_LSR_THRE != (serial_read_reg(p, UART_LSR) &
				(UART_LSR_THRE | UART_LSR_DR))) {
		timeout--;
		if (!timeout) {
			/* Should *never* happen. we warn and carry on */
			dev_crit(&uart->pdev.dev, "Errata i202: timedout %x\n",
				serial_read_reg(p, UART_LSR));
			break;
		}
		udelay(1);
	}
}

static void omap_uart_save_context(struct omap_uart_state *uart)
{
	u16 lcr = 0;
	struct plat_serial8250_port *p = uart->p;

	if (!enable_off_mode)
		return;

	lcr = serial_read_reg(p, UART_LCR);
	serial_write_reg(p, UART_LCR, 0xBF);
	uart->dll = serial_read_reg(p, UART_DLL);
	uart->dlh = serial_read_reg(p, UART_DLM);
	serial_write_reg(p, UART_LCR, lcr);
	uart->ier = serial_read_reg(p, UART_IER);
	uart->sysc = serial_read_reg(p, UART_OMAP_SYSC);
	uart->scr = serial_read_reg(p, UART_OMAP_SCR);
	uart->wer = serial_read_reg(p, UART_OMAP_WER);
	serial_write_reg(p, UART_LCR, 0x80);
	uart->mcr = serial_read_reg(p, UART_MCR);
	serial_write_reg(p, UART_LCR, lcr);

	uart->context_valid = 1;
}

static void omap_uart_restore_context(struct omap_uart_state *uart)
{
	u16 efr = 0;
	struct plat_serial8250_port *p = uart->p;

	if (!enable_off_mode)
		return;

	if (!uart->context_valid)
		return;

	uart->context_valid = 0;

	if (uart->errata & UART_ERRATA_i202_MDR1_ACCESS)
		omap_uart_mdr1_errataset(uart, 0x07, 0xA0);
	else
		serial_write_reg(p, UART_OMAP_MDR1, 0x7);
	serial_write_reg(p, UART_LCR, 0xBF); /* Config B mode */
	efr = serial_read_reg(p, UART_EFR);
	serial_write_reg(p, UART_EFR, UART_EFR_ECB);
	serial_write_reg(p, UART_LCR, 0x0); /* Operational mode */
	serial_write_reg(p, UART_IER, 0x0);
	serial_write_reg(p, UART_LCR, 0xBF); /* Config B mode */
	serial_write_reg(p, UART_DLL, uart->dll);
	serial_write_reg(p, UART_DLM, uart->dlh);
	serial_write_reg(p, UART_LCR, 0x0); /* Operational mode */
	serial_write_reg(p, UART_IER, uart->ier);
	serial_write_reg(p, UART_LCR, 0x80);
	serial_write_reg(p, UART_MCR, uart->mcr);
	serial_write_reg(p, UART_LCR, 0xBF); /* Config B mode */
	serial_write_reg(p, UART_EFR, efr);
	serial_write_reg(p, UART_LCR, UART_LCR_WLEN8);
	serial_write_reg(p, UART_OMAP_SCR, uart->scr);
	serial_write_reg(p, UART_OMAP_WER, uart->wer);
	serial_write_reg(p, UART_OMAP_SYSC, uart->sysc);
	if (uart->errata & UART_ERRATA_i202_MDR1_ACCESS)
		omap_uart_mdr1_errataset(uart, 0x00, 0xA1);
	else
		serial_write_reg(p, UART_OMAP_MDR1, 0x00); /* UART 16x mode */
}
#else
static inline void omap_uart_save_context(struct omap_uart_state *uart) {}
static inline void omap_uart_restore_context(struct omap_uart_state *uart) {}
#endif /* CONFIG_PM && CONFIG_ARCH_OMAP3 */

static inline void omap_uart_enable_clocks(struct omap_uart_state *uart)
{
	if (uart->clocked)
		return;

	clk_enable(uart->ick);
	clk_enable(uart->fck);
	uart->clocked = 1;
	omap_uart_restore_context(uart);
}

#ifdef CONFIG_PM

static inline void omap_uart_disable_clocks(struct omap_uart_state *uart)
{
	if (!uart->clocked)
		return;

	omap_uart_save_context(uart);
	uart->clocked = 0;
	clk_disable(uart->ick);
	clk_disable(uart->fck);
}

static void omap_uart_enable_wakeup(struct omap_uart_state *uart)
{
	/* Set wake-enable bit */
	if (uart->wk_en && uart->wk_mask) {
		u32 v = __raw_readl(uart->wk_en);
		v |= uart->wk_mask;
		__raw_writel(v, uart->wk_en);
	}

	/* Ensure IOPAD wake-enables are set */
	if (cpu_is_omap34xx() && uart->padconf) {
		u16 v = omap_ctrl_readw(uart->padconf);
		v |= OMAP3_PADCONF_WAKEUPENABLE0;
		omap_ctrl_writew(v, uart->padconf);
	}
}

static void omap_uart_disable_wakeup(struct omap_uart_state *uart)
{
	/* Clear wake-enable bit */
	if (uart->wk_en && uart->wk_mask) {
		u32 v = __raw_readl(uart->wk_en);
		v &= ~uart->wk_mask;
		__raw_writel(v, uart->wk_en);
	}

	/* Ensure IOPAD wake-enables are cleared */
	if (cpu_is_omap34xx() && uart->padconf) {
		u16 v = omap_ctrl_readw(uart->padconf);
		v &= ~OMAP3_PADCONF_WAKEUPENABLE0;
		omap_ctrl_writew(v, uart->padconf);
	}
}

static void omap_uart_smart_idle_enable(struct omap_uart_state *uart,
					  int enable)
{
	struct plat_serial8250_port *p = uart->p;
	u16 sysc;

	sysc = serial_read_reg(p, UART_OMAP_SYSC) & 0x7;
	if (enable)
		sysc |= 0x2 << 3;
	else
		sysc |= 0x1 << 3;

	serial_write_reg(p, UART_OMAP_SYSC, sysc);
}

static void omap_uart_block_sleep(struct omap_uart_state *uart)
{
	omap_uart_enable_clocks(uart);

	omap_uart_smart_idle_enable(uart, 0);
	uart->can_sleep = 0;
	if (uart->timeout)
		mod_timer(&uart->timer, jiffies + uart->timeout);
	else
		del_timer(&uart->timer);
}

static void omap_uart_allow_sleep(struct omap_uart_state *uart)
{
	if (device_may_wakeup(&uart->pdev.dev))
		omap_uart_enable_wakeup(uart);
	else
		omap_uart_disable_wakeup(uart);

	if (!uart->clocked)
		return;

	omap_uart_smart_idle_enable(uart, 1);
	uart->can_sleep = 1;
	del_timer(&uart->timer);
}

static void omap_uart_idle_timer(unsigned long data)
{
	struct omap_uart_state *uart = (struct omap_uart_state *)data;

	omap_uart_allow_sleep(uart);
}

void omap_uart_prepare_idle(int num)
{
	struct omap_uart_state *uart;

	list_for_each_entry(uart, &uart_list, node) {
		if (num == uart->num && uart->can_sleep) {
			omap_uart_disable_clocks(uart);
			return;
		}
	}
}

void omap_uart_resume_idle(int num)
{
	struct omap_uart_state *uart;

	list_for_each_entry(uart, &uart_list, node) {
		if (num == uart->num) {
			omap_uart_enable_clocks(uart);

			/* Check for IO pad wakeup */
			if (cpu_is_omap34xx() && uart->padconf) {
				u16 p = omap_ctrl_readw(uart->padconf);

				if (p & OMAP3_PADCONF_WAKEUPEVENT0)
					omap_uart_block_sleep(uart);
			}

			/* Check for normal UART wakeup */
			if (__raw_readl(uart->wk_st) & uart->wk_mask)
				omap_uart_block_sleep(uart);
			return;
		}
	}
}

void omap_uart_prepare_suspend(void)
{
	struct omap_uart_state *uart;

	list_for_each_entry(uart, &uart_list, node) {
		omap_uart_allow_sleep(uart);
	}
}

int omap_uart_can_sleep(void)
{
	struct omap_uart_state *uart;
	int can_sleep = 1;

	list_for_each_entry(uart, &uart_list, node) {
		if (!uart->clocked)
			continue;

		if (!uart->can_sleep) {
			can_sleep = 0;
			continue;
		}

		/* This UART can now safely sleep. */
		omap_uart_allow_sleep(uart);
	}

	return can_sleep;
}

/**
 * omap_uart_interrupt()
 *
 * This handler is used only to detect that *any* UART interrupt has
 * occurred.  It does _nothing_ to handle the interrupt.  Rather,
 * any UART interrupt will trigger the inactivity timer so the
 * UART will not idle or sleep for its timeout period.
 *
 **/
static irqreturn_t omap_uart_interrupt(int irq, void *dev_id)
{
	struct omap_uart_state *uart = dev_id;

	omap_uart_block_sleep(uart);

	return IRQ_NONE;
}

static void omap_uart_idle_init(struct omap_uart_state *uart)
{
	struct plat_serial8250_port *p = uart->p;
	int ret;

	uart->can_sleep = 0;
	uart->timeout = DEFAULT_TIMEOUT;
	setup_timer(&uart->timer, omap_uart_idle_timer,
		    (unsigned long) uart);
	if (uart->timeout)
		mod_timer(&uart->timer, jiffies + uart->timeout);
	omap_uart_smart_idle_enable(uart, 0);

	if (cpu_is_omap34xx()) {
		u32 mod = (uart->num == 2) ? OMAP3430_PER_MOD : CORE_MOD;
		u32 wk_mask = 0;
		u32 padconf = 0;

		uart->wk_en = OMAP34XX_PRM_REGADDR(mod, PM_WKEN1);
		uart->wk_st = OMAP34XX_PRM_REGADDR(mod, PM_WKST1);
		switch (uart->num) {
		case 0:
			wk_mask = OMAP3430_ST_UART1_MASK;
			padconf = 0x182;
			break;
		case 1:
			wk_mask = OMAP3430_ST_UART2_MASK;
			padconf = 0x17a;
			break;
		case 2:
			wk_mask = OMAP3430_ST_UART3_MASK;
			padconf = 0x19e;
			break;
		}
		uart->wk_mask = wk_mask;
		uart->padconf = padconf;
	} else if (cpu_is_omap24xx()) {
		u32 wk_mask = 0;

		if (cpu_is_omap2430()) {
			uart->wk_en = OMAP2430_PRM_REGADDR(CORE_MOD, PM_WKEN1);
			uart->wk_st = OMAP2430_PRM_REGADDR(CORE_MOD, PM_WKST1);
		} else if (cpu_is_omap2420()) {
			uart->wk_en = OMAP2420_PRM_REGADDR(CORE_MOD, PM_WKEN1);
			uart->wk_st = OMAP2420_PRM_REGADDR(CORE_MOD, PM_WKST1);
		}
		switch (uart->num) {
		case 0:
			wk_mask = OMAP24XX_ST_UART1_MASK;
			break;
		case 1:
			wk_mask = OMAP24XX_ST_UART2_MASK;
			break;
		case 2:
			wk_mask = OMAP24XX_ST_UART3_MASK;
			break;
		}
		uart->wk_mask = wk_mask;
	} else {
		uart->wk_en = NULL;
		uart->wk_st = NULL;
		uart->wk_mask = 0;
		uart->padconf = 0;
	}

	p->irqflags |= IRQF_SHARED;
	ret = request_irq(p->irq, omap_uart_interrupt, IRQF_SHARED,
			  "serial idle", (void *)uart);
	WARN_ON(ret);
}

void omap_uart_enable_irqs(int enable)
{
	int ret;
	struct omap_uart_state *uart;

	list_for_each_entry(uart, &uart_list, node) {
		if (enable)
			ret = request_irq(uart->p->irq, omap_uart_interrupt,
				IRQF_SHARED, "serial idle", (void *)uart);
		else
			free_irq(uart->p->irq, (void *)uart);
	}
}

static ssize_t sleep_timeout_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct platform_device *pdev = container_of(dev,
					struct platform_device, dev);
	struct omap_uart_state *uart = container_of(pdev,
					struct omap_uart_state, pdev);

	return sprintf(buf, "%u\n", uart->timeout / HZ);
}

static ssize_t sleep_timeout_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t n)
{
	struct platform_device *pdev = container_of(dev,
					struct platform_device, dev);
	struct omap_uart_state *uart = container_of(pdev,
					struct omap_uart_state, pdev);
	unsigned int value;

	if (sscanf(buf, "%u", &value) != 1) {
		dev_err(dev, "sleep_timeout_store: Invalid value\n");
		return -EINVAL;
	}

	uart->timeout = value * HZ;
	if (uart->timeout)
		mod_timer(&uart->timer, jiffies + uart->timeout);
	else
		/* A zero value means disable timeout feature */
		omap_uart_block_sleep(uart);

	return n;
}

static DEVICE_ATTR(sleep_timeout, 0644, sleep_timeout_show,
		sleep_timeout_store);
#define DEV_CREATE_FILE(dev, attr) WARN_ON(device_create_file(dev, attr))
#else
static inline void omap_uart_idle_init(struct omap_uart_state *uart) {}
#define DEV_CREATE_FILE(dev, attr)
#endif /* CONFIG_PM */

static struct omap_uart_state omap_uart[] = {
	{
		.pdev = {
			.name			= "serial8250",
			.id			= PLAT8250_DEV_PLATFORM,
			.dev			= {
				.platform_data	= serial_platform_data0,
			},
		},
	}, {
		.pdev = {
			.name			= "serial8250",
			.id			= PLAT8250_DEV_PLATFORM1,
			.dev			= {
				.platform_data	= serial_platform_data1,
			},
		},
	}, {
		.pdev = {
			.name			= "serial8250",
			.id			= PLAT8250_DEV_PLATFORM2,
			.dev			= {
				.platform_data	= serial_platform_data2,
			},
		},
	},
#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	{
		.pdev = {
			.name			= "serial8250",
			.id			= 3,
			.dev			= {
				.platform_data	= serial_platform_data3,
			},
		},
	},
#endif
};

/*
 * Override the default 8250 read handler: mem_serial_in()
 * Empty RX fifo read causes an abort on omap3630 and omap4
 * This function makes sure that an empty rx fifo is not read on these silicons
 * (OMAP1/2/3430 are not affected)
 */
static unsigned int serial_in_override(struct uart_port *up, int offset)
{
	if (UART_RX == offset) {
		unsigned int lsr;
		lsr = __serial_read_reg(up, UART_LSR);
		if (!(lsr & UART_LSR_DR))
			return -EPERM;
	}

	return __serial_read_reg(up, offset);
}

static void serial_out_override(struct uart_port *up, int offset, int value)
{
	unsigned int status, tmout = 10000;

	status = __serial_read_reg(up, UART_LSR);
	while (!(status & UART_LSR_THRE)) {
		/* Wait up to 10ms for the character(s) to be sent. */
		if (--tmout == 0)
			break;
		udelay(1);
		status = __serial_read_reg(up, UART_LSR);
	}
	__serial_write_reg(up, offset, value);
}
void __init omap_serial_early_init(void)
{
	int i, nr_ports;
	char name[16];

	if (!(cpu_is_omap3630() || cpu_is_omap4430()))
		nr_ports = 3;
	else
		nr_ports = ARRAY_SIZE(omap_uart);

	/*
	 * Make sure the serial ports are muxed on at this point.
	 * You have to mux them off in device drivers later on
	 * if not needed.
	 */

	for (i = 0; i < nr_ports; i++) {
		struct omap_uart_state *uart = &omap_uart[i];
		struct platform_device *pdev = &uart->pdev;
		struct device *dev = &pdev->dev;
		struct plat_serial8250_port *p = dev->platform_data;

		/* Don't map zero-based physical address */
		if (p->mapbase == 0) {
			dev_warn(dev, "no physical address for uart#%d,"
				 " so skipping early_init...\n", i);
			continue;
		}
		/*
		 * Module 4KB + L4 interconnect 4KB
		 * Static mapping, never released
		 */
		p->membase = ioremap(p->mapbase, SZ_8K);
		if (!p->membase) {
			dev_err(dev, "ioremap failed for uart%i\n", i + 1);
			continue;
		}

		sprintf(name, "uart%d_ick", i + 1);
		uart->ick = clk_get(NULL, name);
		if (IS_ERR(uart->ick)) {
			dev_err(dev, "Could not get uart%d_ick\n", i + 1);
			uart->ick = NULL;
		}

		sprintf(name, "uart%d_fck", i+1);
		uart->fck = clk_get(NULL, name);
		if (IS_ERR(uart->fck)) {
			dev_err(dev, "Could not get uart%d_fck\n", i + 1);
			uart->fck = NULL;
		}

		/* FIXME: Remove this once the clkdev is ready */
		if (!cpu_is_omap44xx()) {
			if (!uart->ick || !uart->fck)
				continue;
		}

		uart->num = i;
		p->private_data = uart;
		uart->p = p;

		if (cpu_is_omap44xx())
			p->irq += 32;
	}
}

/**
 * omap_serial_init_port() - initialize single serial port
 * @port: serial port number (0-3)
 *
 * This function initialies serial driver for given @port only.
 * Platforms can call this function instead of omap_serial_init()
 * if they don't plan to use all available UARTs as serial ports.
 *
 * Don't mix calls to omap_serial_init_port() and omap_serial_init(),
 * use only one of the two.
 */
void __init omap_serial_init_port(int port)
{
	struct omap_uart_state *uart;
	struct platform_device *pdev;
	struct device *dev;

	BUG_ON(port < 0);
	BUG_ON(port >= ARRAY_SIZE(omap_uart));

	uart = &omap_uart[port];
	pdev = &uart->pdev;
	dev = &pdev->dev;

	/* Don't proceed if there's no clocks available */
	if (unlikely(!uart->ick || !uart->fck)) {
		WARN(1, "%s: can't init uart%d, no clocks available\n",
		     kobject_name(&dev->kobj), port);
		return;
	}

	omap_uart_enable_clocks(uart);

	omap_uart_reset(uart);
	omap_uart_idle_init(uart);

	list_add_tail(&uart->node, &uart_list);

	if (WARN_ON(platform_device_register(pdev)))
		return;

	if ((cpu_is_omap34xx() && uart->padconf) ||
	    (uart->wk_en && uart->wk_mask)) {
		device_init_wakeup(dev, true);
		DEV_CREATE_FILE(dev, &dev_attr_sleep_timeout);
	}

	/*
	 * omap44xx: Never read empty UART fifo
	 * omap3xxx: Never read empty UART fifo on UARTs
	 * with IP rev >=0x52
	 */
	if (cpu_is_omap44xx())
		uart->errata |= UART_ERRATA_FIFO_FULL_ABORT;
	else if ((serial_read_reg(uart->p, UART_OMAP_MVER) & 0xFF)
			>= UART_OMAP_NO_EMPTY_FIFO_READ_IP_REV)
		uart->errata |= UART_ERRATA_FIFO_FULL_ABORT;

	if (uart->errata & UART_ERRATA_FIFO_FULL_ABORT) {
		uart->p->serial_in = serial_in_override;
		uart->p->serial_out = serial_out_override;
	}

	/* Enable the MDR1 errata for OMAP3 */
	if (cpu_is_omap34xx())
		uart->errata |= UART_ERRATA_i202_MDR1_ACCESS;
}

/**
 * omap_serial_init() - intialize all supported serial ports
 *
 * Initializes all available UARTs as serial ports. Platforms
 * can call this function when they want to have default behaviour
 * for serial ports (e.g initialize them all as serial ports).
 */
void __init omap_serial_init(void)
{
	int i, nr_ports;

	if (!(cpu_is_omap3630() || cpu_is_omap4430()))
		nr_ports = 3;
	else
		nr_ports = ARRAY_SIZE(omap_uart);

	for (i = 0; i < nr_ports; i++)
		omap_serial_init_port(i);
}
