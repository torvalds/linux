/*
 * IR SIR driver, (C) 2000 Milan Pikula <www@fornax.sk>
 *
 * sir_ir - Device driver for use with SIR (serial infra red)
 * mode of IrDA on many notebooks.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/serial_reg.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#include <media/rc-core.h>

/* SECTION: Definitions */
#define PULSE '['

/* 9bit * 1s/115200bit in milli seconds = 78.125ms*/
#define TIME_CONST (9000000ul / 115200ul)

/* timeout for sequences in jiffies (=5/100s), must be longer than TIME_CONST */
#define SIR_TIMEOUT	(HZ * 5 / 100)

/* onboard sir ports are typically com3 */
static int io = 0x3e8;
static int irq = 4;
static int threshold = 3;

static DEFINE_SPINLOCK(timer_lock);
static struct timer_list timerlist;
/* time of last signal change detected */
static ktime_t last;
/* time of last UART data ready interrupt */
static ktime_t last_intr_time;
static int last_value;
static struct rc_dev *rcdev;

static struct platform_device *sir_ir_dev;

static DEFINE_SPINLOCK(hardware_lock);

/* SECTION: Prototypes */

/* Communication with user-space */
static void add_read_queue(int flag, unsigned long val);
static int init_chrdev(void);
/* Hardware */
static irqreturn_t sir_interrupt(int irq, void *dev_id);
static void send_space(unsigned long len);
static void send_pulse(unsigned long len);
static int init_hardware(void);
static void drop_hardware(void);
/* Initialisation */
static int init_port(void);
static void drop_port(void);

static inline unsigned int sinp(int offset)
{
	return inb(io + offset);
}

static inline void soutp(int offset, int value)
{
	outb(value, io + offset);
}

/* SECTION: Communication with user-space */
static int sir_tx_ir(struct rc_dev *dev, unsigned int *tx_buf,
		     unsigned int count)
{
	unsigned long flags;
	int i;

	local_irq_save(flags);
	for (i = 0; i < count;) {
		if (tx_buf[i])
			send_pulse(tx_buf[i]);
		i++;
		if (i >= count)
			break;
		if (tx_buf[i])
			send_space(tx_buf[i]);
		i++;
	}
	local_irq_restore(flags);

	return count;
}

static void add_read_queue(int flag, unsigned long val)
{
	DEFINE_IR_RAW_EVENT(ev);

	pr_debug("add flag %d with val %lu\n", flag, val);

	/*
	 * statistically, pulses are ~TIME_CONST/2 too long. we could
	 * maybe make this more exact, but this is good enough
	 */
	if (flag) {
		/* pulse */
		if (val > TIME_CONST / 2)
			val -= TIME_CONST / 2;
		else /* should not ever happen */
			val = 1;
		ev.pulse = true;
	} else {
		val += TIME_CONST / 2;
	}
	ev.duration = US_TO_NS(val);

	ir_raw_event_store_with_filter(rcdev, &ev);
}

static int init_chrdev(void)
{
	rcdev = devm_rc_allocate_device(&sir_ir_dev->dev, RC_DRIVER_IR_RAW);
	if (!rcdev)
		return -ENOMEM;

	rcdev->input_name = "SIR IrDA port";
	rcdev->input_phys = KBUILD_MODNAME "/input0";
	rcdev->input_id.bustype = BUS_HOST;
	rcdev->input_id.vendor = 0x0001;
	rcdev->input_id.product = 0x0001;
	rcdev->input_id.version = 0x0100;
	rcdev->tx_ir = sir_tx_ir;
	rcdev->allowed_protocols = RC_BIT_ALL_IR_DECODER;
	rcdev->driver_name = KBUILD_MODNAME;
	rcdev->map_name = RC_MAP_RC6_MCE;
	rcdev->timeout = IR_DEFAULT_TIMEOUT;
	rcdev->dev.parent = &sir_ir_dev->dev;

	return devm_rc_register_device(&sir_ir_dev->dev, rcdev);
}

/* SECTION: Hardware */
static void sir_timeout(unsigned long data)
{
	/*
	 * if last received signal was a pulse, but receiving stopped
	 * within the 9 bit frame, we need to finish this pulse and
	 * simulate a signal change to from pulse to space. Otherwise
	 * upper layers will receive two sequences next time.
	 */

	unsigned long flags;
	unsigned long pulse_end;

	/* avoid interference with interrupt */
	spin_lock_irqsave(&timer_lock, flags);
	if (last_value) {
		/* clear unread bits in UART and restart */
		outb(UART_FCR_CLEAR_RCVR, io + UART_FCR);
		/* determine 'virtual' pulse end: */
		pulse_end = min_t(unsigned long,
				  ktime_us_delta(last, last_intr_time),
				  IR_MAX_DURATION);
		dev_dbg(&sir_ir_dev->dev, "timeout add %d for %lu usec\n",
			last_value, pulse_end);
		add_read_queue(last_value, pulse_end);
		last_value = 0;
		last = last_intr_time;
	}
	spin_unlock_irqrestore(&timer_lock, flags);
	ir_raw_event_handle(rcdev);
}

static irqreturn_t sir_interrupt(int irq, void *dev_id)
{
	unsigned char data;
	ktime_t curr_time;
	static unsigned long delt;
	unsigned long deltintr;
	unsigned long flags;
	int counter = 0;
	int iir, lsr;

	while ((iir = inb(io + UART_IIR) & UART_IIR_ID)) {
		if (++counter > 256) {
			dev_err(&sir_ir_dev->dev, "Trapped in interrupt");
			break;
		}

		switch (iir & UART_IIR_ID) { /* FIXME toto treba preriedit */
		case UART_IIR_MSI:
			(void)inb(io + UART_MSR);
			break;
		case UART_IIR_RLSI:
		case UART_IIR_THRI:
			(void)inb(io + UART_LSR);
			break;
		case UART_IIR_RDI:
			/* avoid interference with timer */
			spin_lock_irqsave(&timer_lock, flags);
			do {
				del_timer(&timerlist);
				data = inb(io + UART_RX);
				curr_time = ktime_get();
				delt = min_t(unsigned long,
					     ktime_us_delta(last, curr_time),
					     IR_MAX_DURATION);
				deltintr = min_t(unsigned long,
						 ktime_us_delta(last_intr_time,
								curr_time),
						 IR_MAX_DURATION);
				dev_dbg(&sir_ir_dev->dev, "t %lu, d %d\n",
					deltintr, (int)data);
				/*
				 * if nothing came in last X cycles,
				 * it was gap
				 */
				if (deltintr > TIME_CONST * threshold) {
					if (last_value) {
						dev_dbg(&sir_ir_dev->dev, "GAP\n");
						/* simulate signal change */
						add_read_queue(last_value,
							       delt -
							       deltintr);
						last_value = 0;
						last = last_intr_time;
						delt = deltintr;
					}
				}
				data = 1;
				if (data ^ last_value) {
					/*
					 * deltintr > 2*TIME_CONST, remember?
					 * the other case is timeout
					 */
					add_read_queue(last_value,
						       delt - TIME_CONST);
					last_value = data;
					last = curr_time;
					last = ktime_sub_us(last,
							    TIME_CONST);
				}
				last_intr_time = curr_time;
				if (data) {
					/*
					 * start timer for end of
					 * sequence detection
					 */
					timerlist.expires = jiffies +
								SIR_TIMEOUT;
					add_timer(&timerlist);
				}

				lsr = inb(io + UART_LSR);
			} while (lsr & UART_LSR_DR); /* data ready */
			spin_unlock_irqrestore(&timer_lock, flags);
			break;
		default:
			break;
		}
	}
	ir_raw_event_handle(rcdev);
	return IRQ_RETVAL(IRQ_HANDLED);
}

static void send_space(unsigned long len)
{
	usleep_range(len, len + 25);
}

static void send_pulse(unsigned long len)
{
	long bytes_out = len / TIME_CONST;

	if (bytes_out == 0)
		bytes_out++;

	while (bytes_out--) {
		outb(PULSE, io + UART_TX);
		/* FIXME treba seriozne cakanie z char/serial.c */
		while (!(inb(io + UART_LSR) & UART_LSR_THRE))
			;
	}
}

static int init_hardware(void)
{
	unsigned long flags;

	spin_lock_irqsave(&hardware_lock, flags);
	/* reset UART */
	outb(0, io + UART_MCR);
	outb(0, io + UART_IER);
	/* init UART */
	/* set DLAB, speed = 115200 */
	outb(UART_LCR_DLAB | UART_LCR_WLEN7, io + UART_LCR);
	outb(1, io + UART_DLL); outb(0, io + UART_DLM);
	/* 7N1+start = 9 bits at 115200 ~ 3 bits at 44000 */
	outb(UART_LCR_WLEN7, io + UART_LCR);
	/* FIFO operation */
	outb(UART_FCR_ENABLE_FIFO, io + UART_FCR);
	/* interrupts */
	/* outb(UART_IER_RLSI|UART_IER_RDI|UART_IER_THRI, io + UART_IER); */
	outb(UART_IER_RDI, io + UART_IER);
	/* turn on UART */
	outb(UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2, io + UART_MCR);
	spin_unlock_irqrestore(&hardware_lock, flags);
	return 0;
}

static void drop_hardware(void)
{
	unsigned long flags;

	spin_lock_irqsave(&hardware_lock, flags);

	/* turn off interrupts */
	outb(0, io + UART_IER);

	spin_unlock_irqrestore(&hardware_lock, flags);
}

/* SECTION: Initialisation */

static int init_port(void)
{
	int retval;

	setup_timer(&timerlist, sir_timeout, 0);

	/* get I/O port access and IRQ line */
	if (!request_region(io, 8, KBUILD_MODNAME)) {
		pr_err("i/o port 0x%.4x already in use.\n", io);
		return -EBUSY;
	}
	retval = request_irq(irq, sir_interrupt, 0,
			     KBUILD_MODNAME, NULL);
	if (retval < 0) {
		release_region(io, 8);
		pr_err("IRQ %d already in use.\n", irq);
		return retval;
	}
	pr_info("I/O port 0x%.4x, IRQ %d.\n", io, irq);

	return 0;
}

static void drop_port(void)
{
	free_irq(irq, NULL);
	del_timer_sync(&timerlist);
	release_region(io, 8);
}

static int init_sir_ir(void)
{
	int retval;

	retval = init_port();
	if (retval < 0)
		return retval;
	init_hardware();
	return 0;
}

static int sir_ir_probe(struct platform_device *dev)
{
	int retval;

	retval = init_chrdev();
	if (retval < 0)
		return retval;

	return init_sir_ir();
}

static int sir_ir_remove(struct platform_device *dev)
{
	return 0;
}

static struct platform_driver sir_ir_driver = {
	.probe		= sir_ir_probe,
	.remove		= sir_ir_remove,
	.driver		= {
		.name	= "sir_ir",
	},
};

static int __init sir_ir_init(void)
{
	int retval;

	retval = platform_driver_register(&sir_ir_driver);
	if (retval)
		return retval;

	sir_ir_dev = platform_device_alloc("sir_ir", 0);
	if (!sir_ir_dev) {
		retval = -ENOMEM;
		goto pdev_alloc_fail;
	}

	retval = platform_device_add(sir_ir_dev);
	if (retval)
		goto pdev_add_fail;

	return 0;

pdev_add_fail:
	platform_device_put(sir_ir_dev);
pdev_alloc_fail:
	platform_driver_unregister(&sir_ir_driver);
	return retval;
}

static void __exit sir_ir_exit(void)
{
	drop_hardware();
	drop_port();
	platform_device_unregister(sir_ir_dev);
	platform_driver_unregister(&sir_ir_driver);
}

module_init(sir_ir_init);
module_exit(sir_ir_exit);

MODULE_DESCRIPTION("Infrared receiver driver for SIR type serial ports");
MODULE_AUTHOR("Milan Pikula");
MODULE_LICENSE("GPL");

module_param(io, int, 0444);
MODULE_PARM_DESC(io, "I/O address base (0x3f8 or 0x2f8)");

module_param(irq, int, 0444);
MODULE_PARM_DESC(irq, "Interrupt (4 or 3)");

module_param(threshold, int, 0444);
MODULE_PARM_DESC(threshold, "space detection threshold (3)");
