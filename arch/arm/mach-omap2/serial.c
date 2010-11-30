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
#include <linux/serial_reg.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/serial_8250.h>
#include <linux/pm_runtime.h>
#include <linux/console.h>

#ifdef CONFIG_SERIAL_OMAP
#include <plat/omap-serial.h>
#endif

#include <plat/common.h>
#include <plat/board.h>
#include <plat/clock.h>
#include <plat/dma.h>
#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>

#include "prm.h"
#include "pm.h"
#include "cm.h"
#include "prm-regbits-34xx.h"
#include "control.h"

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

#define MAX_UART_HWMOD_NAME_LEN		16

struct omap_uart_state {
	int num;
	int can_sleep;
	struct timer_list timer;
	u32 timeout;

	void __iomem *wk_st;
	void __iomem *wk_en;
	u32 wk_mask;
	u32 padconf;
	u32 dma_enabled;

	struct clk *ick;
	struct clk *fck;
	int clocked;

	int irq;
	int regshift;
	int irqflags;
	void __iomem *membase;
	resource_size_t mapbase;

	struct list_head node;
	struct omap_hwmod *oh;
	struct platform_device *pdev;

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
static u8 num_uarts;

/*
 * Since these idle/enable hooks are used in the idle path itself
 * which has interrupts disabled, use the non-locking versions of
 * the hwmod enable/disable functions.
 */
static int uart_idle_hwmod(struct omap_device *od)
{
	_omap_hwmod_idle(od->hwmods[0]);

	return 0;
}

static int uart_enable_hwmod(struct omap_device *od)
{
	_omap_hwmod_enable(od->hwmods[0]);

	return 0;
}

static struct omap_device_pm_latency omap_uart_latency[] = {
	{
		.deactivate_func = uart_idle_hwmod,
		.activate_func	 = uart_enable_hwmod,
		.flags = OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	},
};

static inline unsigned int __serial_read_reg(struct uart_port *up,
					     int offset)
{
	offset <<= up->regshift;
	return (unsigned int)__raw_readb(up->membase + offset);
}

static inline unsigned int serial_read_reg(struct omap_uart_state *uart,
					   int offset)
{
	offset <<= uart->regshift;
	return (unsigned int)__raw_readb(uart->membase + offset);
}

static inline void __serial_write_reg(struct uart_port *up, int offset,
		int value)
{
	offset <<= up->regshift;
	__raw_writeb(value, up->membase + offset);
}

static inline void serial_write_reg(struct omap_uart_state *uart, int offset,
				    int value)
{
	offset <<= uart->regshift;
	__raw_writeb(value, uart->membase + offset);
}

/*
 * Internal UARTs need to be initialized for the 8250 autoconfig to work
 * properly. Note that the TX watermark initialization may not be needed
 * once the 8250.c watermark handling code is merged.
 */

static inline void __init omap_uart_reset(struct omap_uart_state *uart)
{
	serial_write_reg(uart, UART_OMAP_MDR1, UART_OMAP_MDR1_DISABLE);
	serial_write_reg(uart, UART_OMAP_SCR, 0x08);
	serial_write_reg(uart, UART_OMAP_MDR1, UART_OMAP_MDR1_16X_MODE);
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
	u8 timeout = 255;

	serial_write_reg(uart, UART_OMAP_MDR1, mdr1_val);
	udelay(2);
	serial_write_reg(uart, UART_FCR, fcr_val | UART_FCR_CLEAR_XMIT |
			UART_FCR_CLEAR_RCVR);
	/*
	 * Wait for FIFO to empty: when empty, RX_FIFO_E bit is 0 and
	 * TX_FIFO_E bit is 1.
	 */
	while (UART_LSR_THRE != (serial_read_reg(uart, UART_LSR) &
				(UART_LSR_THRE | UART_LSR_DR))) {
		timeout--;
		if (!timeout) {
			/* Should *never* happen. we warn and carry on */
			dev_crit(&uart->pdev->dev, "Errata i202: timedout %x\n",
			serial_read_reg(uart, UART_LSR));
			break;
		}
		udelay(1);
	}
}

static void omap_uart_save_context(struct omap_uart_state *uart)
{
	u16 lcr = 0;

	if (!enable_off_mode)
		return;

	lcr = serial_read_reg(uart, UART_LCR);
	serial_write_reg(uart, UART_LCR, 0xBF);
	uart->dll = serial_read_reg(uart, UART_DLL);
	uart->dlh = serial_read_reg(uart, UART_DLM);
	serial_write_reg(uart, UART_LCR, lcr);
	uart->ier = serial_read_reg(uart, UART_IER);
	uart->sysc = serial_read_reg(uart, UART_OMAP_SYSC);
	uart->scr = serial_read_reg(uart, UART_OMAP_SCR);
	uart->wer = serial_read_reg(uart, UART_OMAP_WER);
	serial_write_reg(uart, UART_LCR, 0x80);
	uart->mcr = serial_read_reg(uart, UART_MCR);
	serial_write_reg(uart, UART_LCR, lcr);

	uart->context_valid = 1;
}

static void omap_uart_restore_context(struct omap_uart_state *uart)
{
	u16 efr = 0;

	if (!enable_off_mode)
		return;

	if (!uart->context_valid)
		return;

	uart->context_valid = 0;

	if (uart->errata & UART_ERRATA_i202_MDR1_ACCESS)
		omap_uart_mdr1_errataset(uart, UART_OMAP_MDR1_DISABLE, 0xA0);
	else
		serial_write_reg(uart, UART_OMAP_MDR1, UART_OMAP_MDR1_DISABLE);

	serial_write_reg(uart, UART_LCR, 0xBF); /* Config B mode */
	efr = serial_read_reg(uart, UART_EFR);
	serial_write_reg(uart, UART_EFR, UART_EFR_ECB);
	serial_write_reg(uart, UART_LCR, 0x0); /* Operational mode */
	serial_write_reg(uart, UART_IER, 0x0);
	serial_write_reg(uart, UART_LCR, 0xBF); /* Config B mode */
	serial_write_reg(uart, UART_DLL, uart->dll);
	serial_write_reg(uart, UART_DLM, uart->dlh);
	serial_write_reg(uart, UART_LCR, 0x0); /* Operational mode */
	serial_write_reg(uart, UART_IER, uart->ier);
	serial_write_reg(uart, UART_LCR, 0x80);
	serial_write_reg(uart, UART_MCR, uart->mcr);
	serial_write_reg(uart, UART_LCR, 0xBF); /* Config B mode */
	serial_write_reg(uart, UART_EFR, efr);
	serial_write_reg(uart, UART_LCR, UART_LCR_WLEN8);
	serial_write_reg(uart, UART_OMAP_SCR, uart->scr);
	serial_write_reg(uart, UART_OMAP_WER, uart->wer);
	serial_write_reg(uart, UART_OMAP_SYSC, uart->sysc);

	if (uart->errata & UART_ERRATA_i202_MDR1_ACCESS)
		omap_uart_mdr1_errataset(uart, UART_OMAP_MDR1_16X_MODE, 0xA1);
	else
		/* UART 16x mode */
		serial_write_reg(uart, UART_OMAP_MDR1,
				UART_OMAP_MDR1_16X_MODE);
}
#else
static inline void omap_uart_save_context(struct omap_uart_state *uart) {}
static inline void omap_uart_restore_context(struct omap_uart_state *uart) {}
#endif /* CONFIG_PM && CONFIG_ARCH_OMAP3 */

static inline void omap_uart_enable_clocks(struct omap_uart_state *uart)
{
	if (uart->clocked)
		return;

	omap_device_enable(uart->pdev);
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
	omap_device_idle(uart->pdev);
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
	u8 idlemode;

	if (enable) {
		/**
		 * Errata 2.15: [UART]:Cannot Acknowledge Idle Requests
		 * in Smartidle Mode When Configured for DMA Operations.
		 */
		if (uart->dma_enabled)
			idlemode = HWMOD_IDLEMODE_FORCE;
		else
			idlemode = HWMOD_IDLEMODE_SMART;
	} else {
		idlemode = HWMOD_IDLEMODE_NO;
	}

	omap_hwmod_set_slave_idlemode(uart->oh, idlemode);
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
	if (device_may_wakeup(&uart->pdev->dev))
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
		if (num == uart->num && uart->can_sleep) {
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
/* static int first_interrupt; */
static irqreturn_t omap_uart_interrupt(int irq, void *dev_id)
{
	struct omap_uart_state *uart = dev_id;

	omap_uart_block_sleep(uart);

	return IRQ_NONE;
}

static void omap_uart_idle_init(struct omap_uart_state *uart)
{
	int ret;

	uart->can_sleep = 0;
	uart->timeout = DEFAULT_TIMEOUT;
	setup_timer(&uart->timer, omap_uart_idle_timer,
		    (unsigned long) uart);
	if (uart->timeout)
		mod_timer(&uart->timer, jiffies + uart->timeout);
	omap_uart_smart_idle_enable(uart, 0);

	if (cpu_is_omap34xx()) {
		u32 mod = (uart->num > 1) ? OMAP3430_PER_MOD : CORE_MOD;
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
		case 3:
			wk_mask = OMAP3630_ST_UART4_MASK;
			padconf = 0x0d2;
			break;
		}
		uart->wk_mask = wk_mask;
		uart->padconf = padconf;
	} else if (cpu_is_omap24xx()) {
		u32 wk_mask = 0;
		u32 wk_en = PM_WKEN1, wk_st = PM_WKST1;

		switch (uart->num) {
		case 0:
			wk_mask = OMAP24XX_ST_UART1_MASK;
			break;
		case 1:
			wk_mask = OMAP24XX_ST_UART2_MASK;
			break;
		case 2:
			wk_en = OMAP24XX_PM_WKEN2;
			wk_st = OMAP24XX_PM_WKST2;
			wk_mask = OMAP24XX_ST_UART3_MASK;
			break;
		}
		uart->wk_mask = wk_mask;
		if (cpu_is_omap2430()) {
			uart->wk_en = OMAP2430_PRM_REGADDR(CORE_MOD, wk_en);
			uart->wk_st = OMAP2430_PRM_REGADDR(CORE_MOD, wk_st);
		} else if (cpu_is_omap2420()) {
			uart->wk_en = OMAP2420_PRM_REGADDR(CORE_MOD, wk_en);
			uart->wk_st = OMAP2420_PRM_REGADDR(CORE_MOD, wk_st);
		}
	} else {
		uart->wk_en = NULL;
		uart->wk_st = NULL;
		uart->wk_mask = 0;
		uart->padconf = 0;
	}

	uart->irqflags |= IRQF_SHARED;
	ret = request_threaded_irq(uart->irq, NULL, omap_uart_interrupt,
				   IRQF_SHARED, "serial idle", (void *)uart);
	WARN_ON(ret);
}

void omap_uart_enable_irqs(int enable)
{
	int ret;
	struct omap_uart_state *uart;

	list_for_each_entry(uart, &uart_list, node) {
		if (enable) {
			pm_runtime_put_sync(&uart->pdev->dev);
			ret = request_threaded_irq(uart->irq, NULL,
						   omap_uart_interrupt,
						   IRQF_SHARED,
						   "serial idle",
						   (void *)uart);
		} else {
			pm_runtime_get_noresume(&uart->pdev->dev);
			free_irq(uart->irq, (void *)uart);
		}
	}
}

static ssize_t sleep_timeout_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_device *odev = to_omap_device(pdev);
	struct omap_uart_state *uart = odev->hwmods[0]->dev_attr;

	return sprintf(buf, "%u\n", uart->timeout / HZ);
}

static ssize_t sleep_timeout_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t n)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_device *odev = to_omap_device(pdev);
	struct omap_uart_state *uart = odev->hwmods[0]->dev_attr;
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
static void omap_uart_block_sleep(struct omap_uart_state *uart)
{
	/* Needed to enable UART clocks when built without CONFIG_PM */
	omap_uart_enable_clocks(uart);
}
#define DEV_CREATE_FILE(dev, attr)
#endif /* CONFIG_PM */

#ifndef CONFIG_SERIAL_OMAP
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
#endif

void __init omap_serial_early_init(void)
{
	int i = 0;

	do {
		char oh_name[MAX_UART_HWMOD_NAME_LEN];
		struct omap_hwmod *oh;
		struct omap_uart_state *uart;

		snprintf(oh_name, MAX_UART_HWMOD_NAME_LEN,
			 "uart%d", i + 1);
		oh = omap_hwmod_lookup(oh_name);
		if (!oh)
			break;

		uart = kzalloc(sizeof(struct omap_uart_state), GFP_KERNEL);
		if (WARN_ON(!uart))
			return;

		uart->oh = oh;
		uart->num = i++;
		list_add_tail(&uart->node, &uart_list);
		num_uarts++;

		/*
		 * NOTE: omap_hwmod_init() has not yet been called,
		 *       so no hwmod functions will work yet.
		 */

		/*
		 * During UART early init, device need to be probed
		 * to determine SoC specific init before omap_device
		 * is ready.  Therefore, don't allow idle here
		 */
		uart->oh->flags |= HWMOD_INIT_NO_IDLE | HWMOD_INIT_NO_RESET;
	} while (1);
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
	struct omap_hwmod *oh;
	struct omap_device *od;
	void *pdata = NULL;
	u32 pdata_size = 0;
	char *name;
#ifndef CONFIG_SERIAL_OMAP
	struct plat_serial8250_port ports[2] = {
		{},
		{.flags = 0},
	};
	struct plat_serial8250_port *p = &ports[0];
#else
	struct omap_uart_port_info omap_up;
#endif

	if (WARN_ON(port < 0))
		return;
	if (WARN_ON(port >= num_uarts))
		return;

	list_for_each_entry(uart, &uart_list, node)
		if (port == uart->num)
			break;

	oh = uart->oh;
	uart->dma_enabled = 0;
#ifndef CONFIG_SERIAL_OMAP
	name = "serial8250";

	/*
	 * !! 8250 driver does not use standard IORESOURCE* It
	 * has it's own custom pdata that can be taken from
	 * the hwmod resource data.  But, this needs to be
	 * done after the build.
	 *
	 * ?? does it have to be done before the register ??
	 * YES, because platform_device_data_add() copies
	 * pdata, it does not use a pointer.
	 */
	p->flags = UPF_BOOT_AUTOCONF;
	p->iotype = UPIO_MEM;
	p->regshift = 2;
	p->uartclk = OMAP24XX_BASE_BAUD * 16;
	p->irq = oh->mpu_irqs[0].irq;
	p->mapbase = oh->slaves[0]->addr->pa_start;
	p->membase = omap_hwmod_get_mpu_rt_va(oh);
	p->irqflags = IRQF_SHARED;
	p->private_data = uart;

	/*
	 * omap44xx: Never read empty UART fifo
	 * omap3xxx: Never read empty UART fifo on UARTs
	 * with IP rev >=0x52
	 */
	uart->regshift = p->regshift;
	uart->membase = p->membase;
	if (cpu_is_omap44xx())
		uart->errata |= UART_ERRATA_FIFO_FULL_ABORT;
	else if ((serial_read_reg(uart, UART_OMAP_MVER) & 0xFF)
			>= UART_OMAP_NO_EMPTY_FIFO_READ_IP_REV)
		uart->errata |= UART_ERRATA_FIFO_FULL_ABORT;

	if (uart->errata & UART_ERRATA_FIFO_FULL_ABORT) {
		p->serial_in = serial_in_override;
		p->serial_out = serial_out_override;
	}

	pdata = &ports[0];
	pdata_size = 2 * sizeof(struct plat_serial8250_port);
#else

	name = DRIVER_NAME;

	omap_up.dma_enabled = uart->dma_enabled;
	omap_up.uartclk = OMAP24XX_BASE_BAUD * 16;
	omap_up.mapbase = oh->slaves[0]->addr->pa_start;
	omap_up.membase = omap_hwmod_get_mpu_rt_va(oh);
	omap_up.irqflags = IRQF_SHARED;
	omap_up.flags = UPF_BOOT_AUTOCONF | UPF_SHARE_IRQ;

	pdata = &omap_up;
	pdata_size = sizeof(struct omap_uart_port_info);
#endif

	if (WARN_ON(!oh))
		return;

	od = omap_device_build(name, uart->num, oh, pdata, pdata_size,
			       omap_uart_latency,
			       ARRAY_SIZE(omap_uart_latency), false);
	WARN(IS_ERR(od), "Could not build omap_device for %s: %s.\n",
	     name, oh->name);

	uart->irq = oh->mpu_irqs[0].irq;
	uart->regshift = 2;
	uart->mapbase = oh->slaves[0]->addr->pa_start;
	uart->membase = omap_hwmod_get_mpu_rt_va(oh);
	uart->pdev = &od->pdev;

	oh->dev_attr = uart;

	acquire_console_sem(); /* in case the earlycon is on the UART */

	/*
	 * Because of early UART probing, UART did not get idled
	 * on init.  Now that omap_device is ready, ensure full idle
	 * before doing omap_device_enable().
	 */
	omap_hwmod_idle(uart->oh);

	omap_device_enable(uart->pdev);
	omap_uart_idle_init(uart);
	omap_uart_reset(uart);
	omap_hwmod_enable_wakeup(uart->oh);
	omap_device_idle(uart->pdev);

	/*
	 * Need to block sleep long enough for interrupt driven
	 * driver to start.  Console driver is in polling mode
	 * so device needs to be kept enabled while polling driver
	 * is in use.
	 */
	if (uart->timeout)
		uart->timeout = (30 * HZ);
	omap_uart_block_sleep(uart);
	uart->timeout = DEFAULT_TIMEOUT;

	release_console_sem();

	if ((cpu_is_omap34xx() && uart->padconf) ||
	    (uart->wk_en && uart->wk_mask)) {
		device_init_wakeup(&od->pdev.dev, true);
		DEV_CREATE_FILE(&od->pdev.dev, &dev_attr_sleep_timeout);
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
	struct omap_uart_state *uart;

	list_for_each_entry(uart, &uart_list, node)
		omap_serial_init_port(uart->num);
}
