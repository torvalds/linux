/* ePAPR hypervisor byte channel device driver
 *
 * Copyright 2009-2011 Freescale Semiconductor, Inc.
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 * This driver support three distinct interfaces, all of which are related to
 * ePAPR hypervisor byte channels.
 *
 * 1) An early-console (udbg) driver.  This provides early console output
 * through a byte channel.  The byte channel handle must be specified in a
 * Kconfig option.
 *
 * 2) A normal console driver.  Output is sent to the byte channel designated
 * for stdout in the device tree.  The console driver is for handling kernel
 * printk calls.
 *
 * 3) A tty driver, which is used to handle user-space input and output.  The
 * byte channel used for the console is designated as the default tty.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <asm/epapr_hcalls.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/circ_buf.h>
#include <asm/udbg.h>

/* The size of the transmit circular buffer.  This must be a power of two. */
#define BUF_SIZE	2048

/* Per-byte channel private data */
struct ehv_bc_data {
	struct device *dev;
	struct tty_port port;
	uint32_t handle;
	unsigned int rx_irq;
	unsigned int tx_irq;

	spinlock_t lock;	/* lock for transmit buffer */
	unsigned char buf[BUF_SIZE];	/* transmit circular buffer */
	unsigned int head;	/* circular buffer head */
	unsigned int tail;	/* circular buffer tail */

	int tx_irq_enabled;	/* true == TX interrupt is enabled */
};

/* Array of byte channel objects */
static struct ehv_bc_data *bcs;

/* Byte channel handle for stdout (and stdin), taken from device tree */
static unsigned int stdout_bc;

/* Virtual IRQ for the byte channel handle for stdin, taken from device tree */
static unsigned int stdout_irq;

/**************************** SUPPORT FUNCTIONS ****************************/

/*
 * Enable the transmit interrupt
 *
 * Unlike a serial device, byte channels have no mechanism for disabling their
 * own receive or transmit interrupts.  To emulate that feature, we toggle
 * the IRQ in the kernel.
 *
 * We cannot just blindly call enable_irq() or disable_irq(), because these
 * calls are reference counted.  This means that we cannot call enable_irq()
 * if interrupts are already enabled.  This can happen in two situations:
 *
 * 1. The tty layer makes two back-to-back calls to ehv_bc_tty_write()
 * 2. A transmit interrupt occurs while executing ehv_bc_tx_dequeue()
 *
 * To work around this, we keep a flag to tell us if the IRQ is enabled or not.
 */
static void enable_tx_interrupt(struct ehv_bc_data *bc)
{
	if (!bc->tx_irq_enabled) {
		enable_irq(bc->tx_irq);
		bc->tx_irq_enabled = 1;
	}
}

static void disable_tx_interrupt(struct ehv_bc_data *bc)
{
	if (bc->tx_irq_enabled) {
		disable_irq_nosync(bc->tx_irq);
		bc->tx_irq_enabled = 0;
	}
}

/*
 * find the byte channel handle to use for the console
 *
 * The byte channel to be used for the console is specified via a "stdout"
 * property in the /chosen node.
 *
 * For compatible with legacy device trees, we also look for a "stdout" alias.
 */
static int find_console_handle(void)
{
	struct device_node *np, *np2;
	const char *sprop = NULL;
	const uint32_t *iprop;

	np = of_find_node_by_path("/chosen");
	if (np)
		sprop = of_get_property(np, "stdout-path", NULL);

	if (!np || !sprop) {
		of_node_put(np);
		np = of_find_node_by_name(NULL, "aliases");
		if (np)
			sprop = of_get_property(np, "stdout", NULL);
	}

	if (!sprop) {
		of_node_put(np);
		return 0;
	}

	/* We don't care what the aliased node is actually called.  We only
	 * care if it's compatible with "epapr,hv-byte-channel", because that
	 * indicates that it's a byte channel node.  We use a temporary
	 * variable, 'np2', because we can't release 'np' until we're done with
	 * 'sprop'.
	 */
	np2 = of_find_node_by_path(sprop);
	of_node_put(np);
	np = np2;
	if (!np) {
		pr_warning("ehv-bc: stdout node '%s' does not exist\n", sprop);
		return 0;
	}

	/* Is it a byte channel? */
	if (!of_device_is_compatible(np, "epapr,hv-byte-channel")) {
		of_node_put(np);
		return 0;
	}

	stdout_irq = irq_of_parse_and_map(np, 0);
	if (stdout_irq == NO_IRQ) {
		pr_err("ehv-bc: no 'interrupts' property in %s node\n", sprop);
		of_node_put(np);
		return 0;
	}

	/*
	 * The 'hv-handle' property contains the handle for this byte channel.
	 */
	iprop = of_get_property(np, "hv-handle", NULL);
	if (!iprop) {
		pr_err("ehv-bc: no 'hv-handle' property in %s node\n",
		       np->name);
		of_node_put(np);
		return 0;
	}
	stdout_bc = be32_to_cpu(*iprop);

	of_node_put(np);
	return 1;
}

/*************************** EARLY CONSOLE DRIVER ***************************/

#ifdef CONFIG_PPC_EARLY_DEBUG_EHV_BC

/*
 * send a byte to a byte channel, wait if necessary
 *
 * This function sends a byte to a byte channel, and it waits and
 * retries if the byte channel is full.  It returns if the character
 * has been sent, or if some error has occurred.
 *
 */
static void byte_channel_spin_send(const char data)
{
	int ret, count;

	do {
		count = 1;
		ret = ev_byte_channel_send(CONFIG_PPC_EARLY_DEBUG_EHV_BC_HANDLE,
					   &count, &data);
	} while (ret == EV_EAGAIN);
}

/*
 * The udbg subsystem calls this function to display a single character.
 * We convert CR to a CR/LF.
 */
static void ehv_bc_udbg_putc(char c)
{
	if (c == '\n')
		byte_channel_spin_send('\r');

	byte_channel_spin_send(c);
}

/*
 * early console initialization
 *
 * PowerPC kernels support an early printk console, also known as udbg.
 * This function must be called via the ppc_md.init_early function pointer.
 * At this point, the device tree has been unflattened, so we can obtain the
 * byte channel handle for stdout.
 *
 * We only support displaying of characters (putc).  We do not support
 * keyboard input.
 */
void __init udbg_init_ehv_bc(void)
{
	unsigned int rx_count, tx_count;
	unsigned int ret;

	/* Verify the byte channel handle */
	ret = ev_byte_channel_poll(CONFIG_PPC_EARLY_DEBUG_EHV_BC_HANDLE,
				   &rx_count, &tx_count);
	if (ret)
		return;

	udbg_putc = ehv_bc_udbg_putc;
	register_early_udbg_console();

	udbg_printf("ehv-bc: early console using byte channel handle %u\n",
		    CONFIG_PPC_EARLY_DEBUG_EHV_BC_HANDLE);
}

#endif

/****************************** CONSOLE DRIVER ******************************/

static struct tty_driver *ehv_bc_driver;

/*
 * Byte channel console sending worker function.
 *
 * For consoles, if the output buffer is full, we should just spin until it
 * clears.
 */
static int ehv_bc_console_byte_channel_send(unsigned int handle, const char *s,
			     unsigned int count)
{
	unsigned int len;
	int ret = 0;

	while (count) {
		len = min_t(unsigned int, count, EV_BYTE_CHANNEL_MAX_BYTES);
		do {
			ret = ev_byte_channel_send(handle, &len, s);
		} while (ret == EV_EAGAIN);
		count -= len;
		s += len;
	}

	return ret;
}

/*
 * write a string to the console
 *
 * This function gets called to write a string from the kernel, typically from
 * a printk().  This function spins until all data is written.
 *
 * We copy the data to a temporary buffer because we need to insert a \r in
 * front of every \n.  It's more efficient to copy the data to the buffer than
 * it is to make multiple hcalls for each character or each newline.
 */
static void ehv_bc_console_write(struct console *co, const char *s,
				 unsigned int count)
{
	char s2[EV_BYTE_CHANNEL_MAX_BYTES];
	unsigned int i, j = 0;
	char c;

	for (i = 0; i < count; i++) {
		c = *s++;

		if (c == '\n')
			s2[j++] = '\r';

		s2[j++] = c;
		if (j >= (EV_BYTE_CHANNEL_MAX_BYTES - 1)) {
			if (ehv_bc_console_byte_channel_send(stdout_bc, s2, j))
				return;
			j = 0;
		}
	}

	if (j)
		ehv_bc_console_byte_channel_send(stdout_bc, s2, j);
}

/*
 * When /dev/console is opened, the kernel iterates the console list looking
 * for one with ->device and then calls that method. On success, it expects
 * the passed-in int* to contain the minor number to use.
 */
static struct tty_driver *ehv_bc_console_device(struct console *co, int *index)
{
	*index = co->index;

	return ehv_bc_driver;
}

static struct console ehv_bc_console = {
	.name		= "ttyEHV",
	.write		= ehv_bc_console_write,
	.device		= ehv_bc_console_device,
	.flags		= CON_PRINTBUFFER | CON_ENABLED,
};

/*
 * Console initialization
 *
 * This is the first function that is called after the device tree is
 * available, so here is where we determine the byte channel handle and IRQ for
 * stdout/stdin, even though that information is used by the tty and character
 * drivers.
 */
static int __init ehv_bc_console_init(void)
{
	if (!find_console_handle()) {
		pr_debug("ehv-bc: stdout is not a byte channel\n");
		return -ENODEV;
	}

#ifdef CONFIG_PPC_EARLY_DEBUG_EHV_BC
	/* Print a friendly warning if the user chose the wrong byte channel
	 * handle for udbg.
	 */
	if (stdout_bc != CONFIG_PPC_EARLY_DEBUG_EHV_BC_HANDLE)
		pr_warning("ehv-bc: udbg handle %u is not the stdout handle\n",
			   CONFIG_PPC_EARLY_DEBUG_EHV_BC_HANDLE);
#endif

	/* add_preferred_console() must be called before register_console(),
	   otherwise it won't work.  However, we don't want to enumerate all the
	   byte channels here, either, since we only care about one. */

	add_preferred_console(ehv_bc_console.name, ehv_bc_console.index, NULL);
	register_console(&ehv_bc_console);

	pr_info("ehv-bc: registered console driver for byte channel %u\n",
		stdout_bc);

	return 0;
}
console_initcall(ehv_bc_console_init);

/******************************** TTY DRIVER ********************************/

/*
 * byte channel receive interupt handler
 *
 * This ISR is called whenever data is available on a byte channel.
 */
static irqreturn_t ehv_bc_tty_rx_isr(int irq, void *data)
{
	struct ehv_bc_data *bc = data;
	unsigned int rx_count, tx_count, len;
	int count;
	char buffer[EV_BYTE_CHANNEL_MAX_BYTES];
	int ret;

	/* Find out how much data needs to be read, and then ask the TTY layer
	 * if it can handle that much.  We want to ensure that every byte we
	 * read from the byte channel will be accepted by the TTY layer.
	 */
	ev_byte_channel_poll(bc->handle, &rx_count, &tx_count);
	count = tty_buffer_request_room(&bc->port, rx_count);

	/* 'count' is the maximum amount of data the TTY layer can accept at
	 * this time.  However, during testing, I was never able to get 'count'
	 * to be less than 'rx_count'.  I'm not sure whether I'm calling it
	 * correctly.
	 */

	while (count > 0) {
		len = min_t(unsigned int, count, sizeof(buffer));

		/* Read some data from the byte channel.  This function will
		 * never return more than EV_BYTE_CHANNEL_MAX_BYTES bytes.
		 */
		ev_byte_channel_receive(bc->handle, &len, buffer);

		/* 'len' is now the amount of data that's been received. 'len'
		 * can't be zero, and most likely it's equal to one.
		 */

		/* Pass the received data to the tty layer. */
		ret = tty_insert_flip_string(&bc->port, buffer, len);

		/* 'ret' is the number of bytes that the TTY layer accepted.
		 * If it's not equal to 'len', then it means the buffer is
		 * full, which should never happen.  If it does happen, we can
		 * exit gracefully, but we drop the last 'len - ret' characters
		 * that we read from the byte channel.
		 */
		if (ret != len)
			break;

		count -= len;
	}

	/* Tell the tty layer that we're done. */
	tty_flip_buffer_push(&bc->port);

	return IRQ_HANDLED;
}

/*
 * dequeue the transmit buffer to the hypervisor
 *
 * This function, which can be called in interrupt context, dequeues as much
 * data as possible from the transmit buffer to the byte channel.
 */
static void ehv_bc_tx_dequeue(struct ehv_bc_data *bc)
{
	unsigned int count;
	unsigned int len, ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&bc->lock, flags);
		len = min_t(unsigned int,
			    CIRC_CNT_TO_END(bc->head, bc->tail, BUF_SIZE),
			    EV_BYTE_CHANNEL_MAX_BYTES);

		ret = ev_byte_channel_send(bc->handle, &len, bc->buf + bc->tail);

		/* 'len' is valid only if the return code is 0 or EV_EAGAIN */
		if (!ret || (ret == EV_EAGAIN))
			bc->tail = (bc->tail + len) & (BUF_SIZE - 1);

		count = CIRC_CNT(bc->head, bc->tail, BUF_SIZE);
		spin_unlock_irqrestore(&bc->lock, flags);
	} while (count && !ret);

	spin_lock_irqsave(&bc->lock, flags);
	if (CIRC_CNT(bc->head, bc->tail, BUF_SIZE))
		/*
		 * If we haven't emptied the buffer, then enable the TX IRQ.
		 * We'll get an interrupt when there's more room in the
		 * hypervisor's output buffer.
		 */
		enable_tx_interrupt(bc);
	else
		disable_tx_interrupt(bc);
	spin_unlock_irqrestore(&bc->lock, flags);
}

/*
 * byte channel transmit interupt handler
 *
 * This ISR is called whenever space becomes available for transmitting
 * characters on a byte channel.
 */
static irqreturn_t ehv_bc_tty_tx_isr(int irq, void *data)
{
	struct ehv_bc_data *bc = data;
	struct tty_struct *ttys = tty_port_tty_get(&bc->port);

	ehv_bc_tx_dequeue(bc);
	if (ttys) {
		tty_wakeup(ttys);
		tty_kref_put(ttys);
	}

	return IRQ_HANDLED;
}

/*
 * This function is called when the tty layer has data for us send.  We store
 * the data first in a circular buffer, and then dequeue as much of that data
 * as possible.
 *
 * We don't need to worry about whether there is enough room in the buffer for
 * all the data.  The purpose of ehv_bc_tty_write_room() is to tell the tty
 * layer how much data it can safely send to us.  We guarantee that
 * ehv_bc_tty_write_room() will never lie, so the tty layer will never send us
 * too much data.
 */
static int ehv_bc_tty_write(struct tty_struct *ttys, const unsigned char *s,
			    int count)
{
	struct ehv_bc_data *bc = ttys->driver_data;
	unsigned long flags;
	unsigned int len;
	unsigned int written = 0;

	while (1) {
		spin_lock_irqsave(&bc->lock, flags);
		len = CIRC_SPACE_TO_END(bc->head, bc->tail, BUF_SIZE);
		if (count < len)
			len = count;
		if (len) {
			memcpy(bc->buf + bc->head, s, len);
			bc->head = (bc->head + len) & (BUF_SIZE - 1);
		}
		spin_unlock_irqrestore(&bc->lock, flags);
		if (!len)
			break;

		s += len;
		count -= len;
		written += len;
	}

	ehv_bc_tx_dequeue(bc);

	return written;
}

/*
 * This function can be called multiple times for a given tty_struct, which is
 * why we initialize bc->ttys in ehv_bc_tty_port_activate() instead.
 *
 * The tty layer will still call this function even if the device was not
 * registered (i.e. tty_register_device() was not called).  This happens
 * because tty_register_device() is optional and some legacy drivers don't
 * use it.  So we need to check for that.
 */
static int ehv_bc_tty_open(struct tty_struct *ttys, struct file *filp)
{
	struct ehv_bc_data *bc = &bcs[ttys->index];

	if (!bc->dev)
		return -ENODEV;

	return tty_port_open(&bc->port, ttys, filp);
}

/*
 * Amazingly, if ehv_bc_tty_open() returns an error code, the tty layer will
 * still call this function to close the tty device.  So we can't assume that
 * the tty port has been initialized.
 */
static void ehv_bc_tty_close(struct tty_struct *ttys, struct file *filp)
{
	struct ehv_bc_data *bc = &bcs[ttys->index];

	if (bc->dev)
		tty_port_close(&bc->port, ttys, filp);
}

/*
 * Return the amount of space in the output buffer
 *
 * This is actually a contract between the driver and the tty layer outlining
 * how much write room the driver can guarantee will be sent OR BUFFERED.  This
 * driver MUST honor the return value.
 */
static int ehv_bc_tty_write_room(struct tty_struct *ttys)
{
	struct ehv_bc_data *bc = ttys->driver_data;
	unsigned long flags;
	int count;

	spin_lock_irqsave(&bc->lock, flags);
	count = CIRC_SPACE(bc->head, bc->tail, BUF_SIZE);
	spin_unlock_irqrestore(&bc->lock, flags);

	return count;
}

/*
 * Stop sending data to the tty layer
 *
 * This function is called when the tty layer's input buffers are getting full,
 * so the driver should stop sending it data.  The easiest way to do this is to
 * disable the RX IRQ, which will prevent ehv_bc_tty_rx_isr() from being
 * called.
 *
 * The hypervisor will continue to queue up any incoming data.  If there is any
 * data in the queue when the RX interrupt is enabled, we'll immediately get an
 * RX interrupt.
 */
static void ehv_bc_tty_throttle(struct tty_struct *ttys)
{
	struct ehv_bc_data *bc = ttys->driver_data;

	disable_irq(bc->rx_irq);
}

/*
 * Resume sending data to the tty layer
 *
 * This function is called after previously calling ehv_bc_tty_throttle().  The
 * tty layer's input buffers now have more room, so the driver can resume
 * sending it data.
 */
static void ehv_bc_tty_unthrottle(struct tty_struct *ttys)
{
	struct ehv_bc_data *bc = ttys->driver_data;

	/* If there is any data in the queue when the RX interrupt is enabled,
	 * we'll immediately get an RX interrupt.
	 */
	enable_irq(bc->rx_irq);
}

static void ehv_bc_tty_hangup(struct tty_struct *ttys)
{
	struct ehv_bc_data *bc = ttys->driver_data;

	ehv_bc_tx_dequeue(bc);
	tty_port_hangup(&bc->port);
}

/*
 * TTY driver operations
 *
 * If we could ask the hypervisor how much data is still in the TX buffer, or
 * at least how big the TX buffers are, then we could implement the
 * .wait_until_sent and .chars_in_buffer functions.
 */
static const struct tty_operations ehv_bc_ops = {
	.open		= ehv_bc_tty_open,
	.close		= ehv_bc_tty_close,
	.write		= ehv_bc_tty_write,
	.write_room	= ehv_bc_tty_write_room,
	.throttle	= ehv_bc_tty_throttle,
	.unthrottle	= ehv_bc_tty_unthrottle,
	.hangup		= ehv_bc_tty_hangup,
};

/*
 * initialize the TTY port
 *
 * This function will only be called once, no matter how many times
 * ehv_bc_tty_open() is called.  That's why we register the ISR here, and also
 * why we initialize tty_struct-related variables here.
 */
static int ehv_bc_tty_port_activate(struct tty_port *port,
				    struct tty_struct *ttys)
{
	struct ehv_bc_data *bc = container_of(port, struct ehv_bc_data, port);
	int ret;

	ttys->driver_data = bc;

	ret = request_irq(bc->rx_irq, ehv_bc_tty_rx_isr, 0, "ehv-bc", bc);
	if (ret < 0) {
		dev_err(bc->dev, "could not request rx irq %u (ret=%i)\n",
		       bc->rx_irq, ret);
		return ret;
	}

	/* request_irq also enables the IRQ */
	bc->tx_irq_enabled = 1;

	ret = request_irq(bc->tx_irq, ehv_bc_tty_tx_isr, 0, "ehv-bc", bc);
	if (ret < 0) {
		dev_err(bc->dev, "could not request tx irq %u (ret=%i)\n",
		       bc->tx_irq, ret);
		free_irq(bc->rx_irq, bc);
		return ret;
	}

	/* The TX IRQ is enabled only when we can't write all the data to the
	 * byte channel at once, so by default it's disabled.
	 */
	disable_tx_interrupt(bc);

	return 0;
}

static void ehv_bc_tty_port_shutdown(struct tty_port *port)
{
	struct ehv_bc_data *bc = container_of(port, struct ehv_bc_data, port);

	free_irq(bc->tx_irq, bc);
	free_irq(bc->rx_irq, bc);
}

static const struct tty_port_operations ehv_bc_tty_port_ops = {
	.activate = ehv_bc_tty_port_activate,
	.shutdown = ehv_bc_tty_port_shutdown,
};

static int ehv_bc_tty_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ehv_bc_data *bc;
	const uint32_t *iprop;
	unsigned int handle;
	int ret;
	static unsigned int index = 1;
	unsigned int i;

	iprop = of_get_property(np, "hv-handle", NULL);
	if (!iprop) {
		dev_err(&pdev->dev, "no 'hv-handle' property in %s node\n",
			np->name);
		return -ENODEV;
	}

	/* We already told the console layer that the index for the console
	 * device is zero, so we need to make sure that we use that index when
	 * we probe the console byte channel node.
	 */
	handle = be32_to_cpu(*iprop);
	i = (handle == stdout_bc) ? 0 : index++;
	bc = &bcs[i];

	bc->handle = handle;
	bc->head = 0;
	bc->tail = 0;
	spin_lock_init(&bc->lock);

	bc->rx_irq = irq_of_parse_and_map(np, 0);
	bc->tx_irq = irq_of_parse_and_map(np, 1);
	if ((bc->rx_irq == NO_IRQ) || (bc->tx_irq == NO_IRQ)) {
		dev_err(&pdev->dev, "no 'interrupts' property in %s node\n",
			np->name);
		ret = -ENODEV;
		goto error;
	}

	tty_port_init(&bc->port);
	bc->port.ops = &ehv_bc_tty_port_ops;

	bc->dev = tty_port_register_device(&bc->port, ehv_bc_driver, i,
			&pdev->dev);
	if (IS_ERR(bc->dev)) {
		ret = PTR_ERR(bc->dev);
		dev_err(&pdev->dev, "could not register tty (ret=%i)\n", ret);
		goto error;
	}

	dev_set_drvdata(&pdev->dev, bc);

	dev_info(&pdev->dev, "registered /dev/%s%u for byte channel %u\n",
		ehv_bc_driver->name, i, bc->handle);

	return 0;

error:
	tty_port_destroy(&bc->port);
	irq_dispose_mapping(bc->tx_irq);
	irq_dispose_mapping(bc->rx_irq);

	memset(bc, 0, sizeof(struct ehv_bc_data));
	return ret;
}

static int ehv_bc_tty_remove(struct platform_device *pdev)
{
	struct ehv_bc_data *bc = dev_get_drvdata(&pdev->dev);

	tty_unregister_device(ehv_bc_driver, bc - bcs);

	tty_port_destroy(&bc->port);
	irq_dispose_mapping(bc->tx_irq);
	irq_dispose_mapping(bc->rx_irq);

	return 0;
}

static const struct of_device_id ehv_bc_tty_of_ids[] = {
	{ .compatible = "epapr,hv-byte-channel" },
	{}
};

static struct platform_driver ehv_bc_tty_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ehv-bc",
		.of_match_table = ehv_bc_tty_of_ids,
	},
	.probe		= ehv_bc_tty_probe,
	.remove		= ehv_bc_tty_remove,
};

/**
 * ehv_bc_init - ePAPR hypervisor byte channel driver initialization
 *
 * This function is called when this module is loaded.
 */
static int __init ehv_bc_init(void)
{
	struct device_node *np;
	unsigned int count = 0; /* Number of elements in bcs[] */
	int ret;

	pr_info("ePAPR hypervisor byte channel driver\n");

	/* Count the number of byte channels */
	for_each_compatible_node(np, NULL, "epapr,hv-byte-channel")
		count++;

	if (!count)
		return -ENODEV;

	/* The array index of an element in bcs[] is the same as the tty index
	 * for that element.  If you know the address of an element in the
	 * array, then you can use pointer math (e.g. "bc - bcs") to get its
	 * tty index.
	 */
	bcs = kzalloc(count * sizeof(struct ehv_bc_data), GFP_KERNEL);
	if (!bcs)
		return -ENOMEM;

	ehv_bc_driver = alloc_tty_driver(count);
	if (!ehv_bc_driver) {
		ret = -ENOMEM;
		goto error;
	}

	ehv_bc_driver->driver_name = "ehv-bc";
	ehv_bc_driver->name = ehv_bc_console.name;
	ehv_bc_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	ehv_bc_driver->subtype = SYSTEM_TYPE_CONSOLE;
	ehv_bc_driver->init_termios = tty_std_termios;
	ehv_bc_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	tty_set_operations(ehv_bc_driver, &ehv_bc_ops);

	ret = tty_register_driver(ehv_bc_driver);
	if (ret) {
		pr_err("ehv-bc: could not register tty driver (ret=%i)\n", ret);
		goto error;
	}

	ret = platform_driver_register(&ehv_bc_tty_driver);
	if (ret) {
		pr_err("ehv-bc: could not register platform driver (ret=%i)\n",
		       ret);
		goto error;
	}

	return 0;

error:
	if (ehv_bc_driver) {
		tty_unregister_driver(ehv_bc_driver);
		put_tty_driver(ehv_bc_driver);
	}

	kfree(bcs);

	return ret;
}


/**
 * ehv_bc_exit - ePAPR hypervisor byte channel driver termination
 *
 * This function is called when this driver is unloaded.
 */
static void __exit ehv_bc_exit(void)
{
	tty_unregister_driver(ehv_bc_driver);
	put_tty_driver(ehv_bc_driver);
	kfree(bcs);
}

module_init(ehv_bc_init);
module_exit(ehv_bc_exit);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("ePAPR hypervisor byte channel driver");
MODULE_LICENSE("GPL v2");
