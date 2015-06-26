// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2010, 2014 The Linux Foundation. All rights reserved.  */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/kfifo.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/spinlock.h>

#include <asm/dcc.h>
#include <asm/processor.h>

#include "hvc_console.h"

/* DCC Status Bits */
#define DCC_STATUS_RX		(1 << 30)
#define DCC_STATUS_TX		(1 << 29)

static void dcc_uart_console_putchar(struct uart_port *port, int ch)
{
	while (__dcc_getstatus() & DCC_STATUS_TX)
		cpu_relax();

	__dcc_putchar(ch);
}

static void dcc_early_write(struct console *con, const char *s, unsigned n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, dcc_uart_console_putchar);
}

static int __init dcc_early_console_setup(struct earlycon_device *device,
					  const char *opt)
{
	device->con->write = dcc_early_write;

	return 0;
}

EARLYCON_DECLARE(dcc, dcc_early_console_setup);

static int hvc_dcc_put_chars(uint32_t vt, const char *buf, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		while (__dcc_getstatus() & DCC_STATUS_TX)
			cpu_relax();

		__dcc_putchar(buf[i]);
	}

	return count;
}

static int hvc_dcc_get_chars(uint32_t vt, char *buf, int count)
{
	int i;

	for (i = 0; i < count; ++i)
		if (__dcc_getstatus() & DCC_STATUS_RX)
			buf[i] = __dcc_getchar();
		else
			break;

	return i;
}

/*
 * Check if the DCC is enabled.  If CONFIG_HVC_DCC_SERIALIZE_SMP is enabled,
 * then we assume then this function will be called first on core 0.  That
 * way, dcc_core0_available will be true only if it's available on core 0.
 */
static bool hvc_dcc_check(void)
{
	unsigned long time = jiffies + (HZ / 10);

#ifdef CONFIG_HVC_DCC_SERIALIZE_SMP
	static bool dcc_core0_available;

	/*
	 * If we're not on core 0, but we previously confirmed that DCC is
	 * active, then just return true.
	 */
	if (smp_processor_id() && dcc_core0_available)
		return true;
#endif

	/* Write a test character to check if it is handled */
	__dcc_putchar('\n');

	while (time_is_after_jiffies(time)) {
		if (!(__dcc_getstatus() & DCC_STATUS_TX)) {
#ifdef CONFIG_HVC_DCC_SERIALIZE_SMP
			dcc_core0_available = true;
#endif
			return true;
		}
	}

	return false;
}

#ifdef CONFIG_HVC_DCC_SERIALIZE_SMP

static void dcc_put_work_fn(struct work_struct *work);
static void dcc_get_work_fn(struct work_struct *work);
static DECLARE_WORK(dcc_pwork, dcc_put_work_fn);
static DECLARE_WORK(dcc_gwork, dcc_get_work_fn);
static DEFINE_SPINLOCK(dcc_lock);
static DEFINE_KFIFO(inbuf, unsigned char, 128);
static DEFINE_KFIFO(outbuf, unsigned char, 1024);

/*
 * Workqueue function that writes the output FIFO to the DCC on core 0.
 */
static void dcc_put_work_fn(struct work_struct *work)
{
	unsigned char ch;

	spin_lock(&dcc_lock);

	/* While there's data in the output FIFO, write it to the DCC */
	while (kfifo_get(&outbuf, &ch))
		hvc_dcc_put_chars(0, &ch, 1);

	/* While we're at it, check for any input characters */
	while (!kfifo_is_full(&inbuf)) {
		if (!hvc_dcc_get_chars(0, &ch, 1))
			break;
		kfifo_put(&inbuf, ch);
	}

	spin_unlock(&dcc_lock);
}

/*
 * Workqueue function that reads characters from DCC and puts them into the
 * input FIFO.
 */
static void dcc_get_work_fn(struct work_struct *work)
{
	unsigned char ch;

	/*
	 * Read characters from DCC and put them into the input FIFO, as
	 * long as there is room and we have characters to read.
	 */
	spin_lock(&dcc_lock);

	while (!kfifo_is_full(&inbuf)) {
		if (!hvc_dcc_get_chars(0, &ch, 1))
			break;
		kfifo_put(&inbuf, ch);
	}
	spin_unlock(&dcc_lock);
}

/*
 * Write characters directly to the DCC if we're on core 0 and the FIFO
 * is empty, or write them to the FIFO if we're not.
 */
static int hvc_dcc0_put_chars(uint32_t vt, const char *buf,
					     int count)
{
	int len;

	spin_lock(&dcc_lock);
	if (smp_processor_id() || (!kfifo_is_empty(&outbuf))) {
		len = kfifo_in(&outbuf, buf, count);
		spin_unlock(&dcc_lock);
		/*
		 * We just push data to the output FIFO, so schedule the
		 * workqueue that will actually write that data to DCC.
		 */
		schedule_work_on(0, &dcc_pwork);
		return len;
	}

	/*
	 * If we're already on core 0, and the FIFO is empty, then just
	 * write the data to DCC.
	 */
	len = hvc_dcc_put_chars(vt, buf, count);
	spin_unlock(&dcc_lock);

	return len;
}

/*
 * Read characters directly from the DCC if we're on core 0 and the FIFO
 * is empty, or read them from the FIFO if we're not.
 */
static int hvc_dcc0_get_chars(uint32_t vt, char *buf, int count)
{
	int len;

	spin_lock(&dcc_lock);

	if (smp_processor_id() || (!kfifo_is_empty(&inbuf))) {
		len = kfifo_out(&inbuf, buf, count);
		spin_unlock(&dcc_lock);

		/*
		 * If the FIFO was empty, there may be characters in the DCC
		 * that we haven't read yet.  Schedule a workqueue to fill
		 * the input FIFO, so that the next time this function is
		 * called, we'll have data.
		*/
		if (!len)
			schedule_work_on(0, &dcc_gwork);

		return len;
	}

	/*
	 * If we're already on core 0, and the FIFO is empty, then just
	 * read the data from DCC.
	 */
	len = hvc_dcc_get_chars(vt, buf, count);
	spin_unlock(&dcc_lock);

	return len;
}

static const struct hv_ops hvc_dcc_get_put_ops = {
	.get_chars = hvc_dcc0_get_chars,
	.put_chars = hvc_dcc0_put_chars,
};

#else

static const struct hv_ops hvc_dcc_get_put_ops = {
	.get_chars = hvc_dcc_get_chars,
	.put_chars = hvc_dcc_put_chars,
};

#endif

static int __init hvc_dcc_console_init(void)
{
	int ret;

	if (!hvc_dcc_check())
		return -ENODEV;

	/* Returns -1 if error */
	ret = hvc_instantiate(0, 0, &hvc_dcc_get_put_ops);

	return ret < 0 ? -ENODEV : 0;
}
console_initcall(hvc_dcc_console_init);

static int __init hvc_dcc_init(void)
{
	struct hvc_struct *p;

	if (!hvc_dcc_check())
		return -ENODEV;

	p = hvc_alloc(0, 0, &hvc_dcc_get_put_ops, 128);

	return PTR_ERR_OR_ZERO(p);
}
device_initcall(hvc_dcc_init);
