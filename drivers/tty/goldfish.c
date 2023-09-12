// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (C) 2012 Intel, Inc.
 * Copyright (C) 2017 Imagination Technologies Ltd.
 */

#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/goldfish.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/serial_core.h>

/* Goldfish tty register's offsets */
#define	GOLDFISH_TTY_REG_BYTES_READY	0x04
#define	GOLDFISH_TTY_REG_CMD		0x08
#define	GOLDFISH_TTY_REG_DATA_PTR	0x10
#define	GOLDFISH_TTY_REG_DATA_LEN	0x14
#define	GOLDFISH_TTY_REG_DATA_PTR_HIGH	0x18
#define	GOLDFISH_TTY_REG_VERSION	0x20

/* Goldfish tty commands */
#define	GOLDFISH_TTY_CMD_INT_DISABLE	0
#define	GOLDFISH_TTY_CMD_INT_ENABLE	1
#define	GOLDFISH_TTY_CMD_WRITE_BUFFER	2
#define	GOLDFISH_TTY_CMD_READ_BUFFER	3

struct goldfish_tty {
	struct tty_port port;
	spinlock_t lock;
	void __iomem *base;
	u32 irq;
	int opencount;
	struct console console;
	u32 version;
	struct device *dev;
};

static DEFINE_MUTEX(goldfish_tty_lock);
static struct tty_driver *goldfish_tty_driver;
static u32 goldfish_tty_line_count = 8;
static u32 goldfish_tty_current_line_count;
static struct goldfish_tty *goldfish_ttys;

static void do_rw_io(struct goldfish_tty *qtty,
		     unsigned long address,
		     unsigned int count,
		     int is_write)
{
	unsigned long irq_flags;
	void __iomem *base = qtty->base;

	spin_lock_irqsave(&qtty->lock, irq_flags);
	gf_write_ptr((void *)address, base + GOLDFISH_TTY_REG_DATA_PTR,
		     base + GOLDFISH_TTY_REG_DATA_PTR_HIGH);
	gf_iowrite32(count, base + GOLDFISH_TTY_REG_DATA_LEN);

	if (is_write)
		gf_iowrite32(GOLDFISH_TTY_CMD_WRITE_BUFFER,
		       base + GOLDFISH_TTY_REG_CMD);
	else
		gf_iowrite32(GOLDFISH_TTY_CMD_READ_BUFFER,
		       base + GOLDFISH_TTY_REG_CMD);

	spin_unlock_irqrestore(&qtty->lock, irq_flags);
}

static void goldfish_tty_rw(struct goldfish_tty *qtty,
			    unsigned long addr,
			    unsigned int count,
			    int is_write)
{
	dma_addr_t dma_handle;
	enum dma_data_direction dma_dir;

	dma_dir = (is_write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (qtty->version > 0) {
		/*
		 * Goldfish TTY for Ranchu platform uses
		 * physical addresses and DMA for read/write operations
		 */
		unsigned long addr_end = addr + count;

		while (addr < addr_end) {
			unsigned long pg_end = (addr & PAGE_MASK) + PAGE_SIZE;
			unsigned long next =
					pg_end < addr_end ? pg_end : addr_end;
			unsigned long avail = next - addr;

			/*
			 * Map the buffer's virtual address to the DMA address
			 * so the buffer can be accessed by the device.
			 */
			dma_handle = dma_map_single(qtty->dev, (void *)addr,
						    avail, dma_dir);

			if (dma_mapping_error(qtty->dev, dma_handle)) {
				dev_err(qtty->dev, "tty: DMA mapping error.\n");
				return;
			}
			do_rw_io(qtty, dma_handle, avail, is_write);

			/*
			 * Unmap the previously mapped region after
			 * the completion of the read/write operation.
			 */
			dma_unmap_single(qtty->dev, dma_handle, avail, dma_dir);

			addr += avail;
		}
	} else {
		/*
		 * Old style Goldfish TTY used on the Goldfish platform
		 * uses virtual addresses.
		 */
		do_rw_io(qtty, addr, count, is_write);
	}
}

static void goldfish_tty_do_write(int line, const u8 *buf, unsigned int count)
{
	struct goldfish_tty *qtty = &goldfish_ttys[line];
	unsigned long address = (unsigned long)(void *)buf;

	goldfish_tty_rw(qtty, address, count, 1);
}

static irqreturn_t goldfish_tty_interrupt(int irq, void *dev_id)
{
	struct goldfish_tty *qtty = dev_id;
	void __iomem *base = qtty->base;
	unsigned long address;
	unsigned char *buf;
	u32 count;

	count = gf_ioread32(base + GOLDFISH_TTY_REG_BYTES_READY);
	if (count == 0)
		return IRQ_NONE;

	count = tty_prepare_flip_string(&qtty->port, &buf, count);

	address = (unsigned long)(void *)buf;
	goldfish_tty_rw(qtty, address, count, 0);

	tty_flip_buffer_push(&qtty->port);
	return IRQ_HANDLED;
}

static int goldfish_tty_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct goldfish_tty *qtty = container_of(port, struct goldfish_tty,
									port);
	gf_iowrite32(GOLDFISH_TTY_CMD_INT_ENABLE, qtty->base + GOLDFISH_TTY_REG_CMD);
	return 0;
}

static void goldfish_tty_shutdown(struct tty_port *port)
{
	struct goldfish_tty *qtty = container_of(port, struct goldfish_tty,
									port);
	gf_iowrite32(GOLDFISH_TTY_CMD_INT_DISABLE, qtty->base + GOLDFISH_TTY_REG_CMD);
}

static int goldfish_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct goldfish_tty *qtty = &goldfish_ttys[tty->index];
	return tty_port_open(&qtty->port, tty, filp);
}

static void goldfish_tty_close(struct tty_struct *tty, struct file *filp)
{
	tty_port_close(tty->port, tty, filp);
}

static void goldfish_tty_hangup(struct tty_struct *tty)
{
	tty_port_hangup(tty->port);
}

static ssize_t goldfish_tty_write(struct tty_struct *tty, const u8 *buf,
				  size_t count)
{
	goldfish_tty_do_write(tty->index, buf, count);
	return count;
}

static unsigned int goldfish_tty_write_room(struct tty_struct *tty)
{
	return 0x10000;
}

static unsigned int goldfish_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct goldfish_tty *qtty = &goldfish_ttys[tty->index];
	void __iomem *base = qtty->base;
	return gf_ioread32(base + GOLDFISH_TTY_REG_BYTES_READY);
}

static void goldfish_tty_console_write(struct console *co, const char *b,
								unsigned count)
{
	goldfish_tty_do_write(co->index, b, count);
}

static struct tty_driver *goldfish_tty_console_device(struct console *c,
								int *index)
{
	*index = c->index;
	return goldfish_tty_driver;
}

static int goldfish_tty_console_setup(struct console *co, char *options)
{
	if ((unsigned)co->index >= goldfish_tty_line_count)
		return -ENODEV;
	if (!goldfish_ttys[co->index].base)
		return -ENODEV;
	return 0;
}

static const struct tty_port_operations goldfish_port_ops = {
	.activate = goldfish_tty_activate,
	.shutdown = goldfish_tty_shutdown
};

static const struct tty_operations goldfish_tty_ops = {
	.open = goldfish_tty_open,
	.close = goldfish_tty_close,
	.hangup = goldfish_tty_hangup,
	.write = goldfish_tty_write,
	.write_room = goldfish_tty_write_room,
	.chars_in_buffer = goldfish_tty_chars_in_buffer,
};

static int goldfish_tty_create_driver(void)
{
	int ret;
	struct tty_driver *tty;

	goldfish_ttys = kcalloc(goldfish_tty_line_count,
				sizeof(*goldfish_ttys),
				GFP_KERNEL);
	if (goldfish_ttys == NULL) {
		ret = -ENOMEM;
		goto err_alloc_goldfish_ttys_failed;
	}
	tty = tty_alloc_driver(goldfish_tty_line_count,
			TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW |
			TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(tty)) {
		ret = PTR_ERR(tty);
		goto err_tty_alloc_driver_failed;
	}
	tty->driver_name = "goldfish";
	tty->name = "ttyGF";
	tty->type = TTY_DRIVER_TYPE_SERIAL;
	tty->subtype = SERIAL_TYPE_NORMAL;
	tty->init_termios = tty_std_termios;
	tty_set_operations(tty, &goldfish_tty_ops);
	ret = tty_register_driver(tty);
	if (ret)
		goto err_tty_register_driver_failed;

	goldfish_tty_driver = tty;
	return 0;

err_tty_register_driver_failed:
	tty_driver_kref_put(tty);
err_tty_alloc_driver_failed:
	kfree(goldfish_ttys);
	goldfish_ttys = NULL;
err_alloc_goldfish_ttys_failed:
	return ret;
}

static void goldfish_tty_delete_driver(void)
{
	tty_unregister_driver(goldfish_tty_driver);
	tty_driver_kref_put(goldfish_tty_driver);
	goldfish_tty_driver = NULL;
	kfree(goldfish_ttys);
	goldfish_ttys = NULL;
}

static int goldfish_tty_probe(struct platform_device *pdev)
{
	struct goldfish_tty *qtty;
	int ret = -ENODEV;
	struct resource *r;
	struct device *ttydev;
	void __iomem *base;
	int irq;
	unsigned int line;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		pr_err("goldfish_tty: No MEM resource available!\n");
		return -ENOMEM;
	}

	base = ioremap(r->start, 0x1000);
	if (!base) {
		pr_err("goldfish_tty: Unable to ioremap base!\n");
		return -ENOMEM;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_unmap;
	}

	mutex_lock(&goldfish_tty_lock);

	if (pdev->id == PLATFORM_DEVID_NONE)
		line = goldfish_tty_current_line_count;
	else
		line = pdev->id;

	if (line >= goldfish_tty_line_count) {
		pr_err("goldfish_tty: Reached maximum tty number of %d.\n",
		       goldfish_tty_current_line_count);
		ret = -ENOMEM;
		goto err_unlock;
	}

	if (goldfish_tty_current_line_count == 0) {
		ret = goldfish_tty_create_driver();
		if (ret)
			goto err_unlock;
	}
	goldfish_tty_current_line_count++;

	qtty = &goldfish_ttys[line];
	spin_lock_init(&qtty->lock);
	tty_port_init(&qtty->port);
	qtty->port.ops = &goldfish_port_ops;
	qtty->base = base;
	qtty->irq = irq;
	qtty->dev = &pdev->dev;

	/*
	 * Goldfish TTY device used by the Goldfish emulator
	 * should identify itself with 0, forcing the driver
	 * to use virtual addresses. Goldfish TTY device
	 * on Ranchu emulator (qemu2) returns 1 here and
	 * driver will use physical addresses.
	 */
	qtty->version = gf_ioread32(base + GOLDFISH_TTY_REG_VERSION);

	/*
	 * Goldfish TTY device on Ranchu emulator (qemu2)
	 * will use DMA for read/write IO operations.
	 */
	if (qtty->version > 0) {
		/*
		 * Initialize dma_mask to 32-bits.
		 */
		if (!pdev->dev.dma_mask)
			pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
		ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pdev->dev, "No suitable DMA available.\n");
			goto err_dec_line_count;
		}
	}

	gf_iowrite32(GOLDFISH_TTY_CMD_INT_DISABLE, base + GOLDFISH_TTY_REG_CMD);

	ret = request_irq(irq, goldfish_tty_interrupt, IRQF_SHARED,
			  "goldfish_tty", qtty);
	if (ret) {
		pr_err("goldfish_tty: No IRQ available!\n");
		goto err_dec_line_count;
	}

	ttydev = tty_port_register_device(&qtty->port, goldfish_tty_driver,
					  line, &pdev->dev);
	if (IS_ERR(ttydev)) {
		ret = PTR_ERR(ttydev);
		goto err_tty_register_device_failed;
	}

	strcpy(qtty->console.name, "ttyGF");
	qtty->console.write = goldfish_tty_console_write;
	qtty->console.device = goldfish_tty_console_device;
	qtty->console.setup = goldfish_tty_console_setup;
	qtty->console.flags = CON_PRINTBUFFER;
	qtty->console.index = line;
	register_console(&qtty->console);
	platform_set_drvdata(pdev, qtty);

	mutex_unlock(&goldfish_tty_lock);
	return 0;

err_tty_register_device_failed:
	free_irq(irq, qtty);
err_dec_line_count:
	tty_port_destroy(&qtty->port);
	goldfish_tty_current_line_count--;
	if (goldfish_tty_current_line_count == 0)
		goldfish_tty_delete_driver();
err_unlock:
	mutex_unlock(&goldfish_tty_lock);
err_unmap:
	iounmap(base);
	return ret;
}

static int goldfish_tty_remove(struct platform_device *pdev)
{
	struct goldfish_tty *qtty = platform_get_drvdata(pdev);

	mutex_lock(&goldfish_tty_lock);

	unregister_console(&qtty->console);
	tty_unregister_device(goldfish_tty_driver, qtty->console.index);
	iounmap(qtty->base);
	qtty->base = NULL;
	free_irq(qtty->irq, qtty);
	tty_port_destroy(&qtty->port);
	goldfish_tty_current_line_count--;
	if (goldfish_tty_current_line_count == 0)
		goldfish_tty_delete_driver();
	mutex_unlock(&goldfish_tty_lock);
	return 0;
}

#ifdef CONFIG_GOLDFISH_TTY_EARLY_CONSOLE
static void gf_early_console_putchar(struct uart_port *port, unsigned char ch)
{
	gf_iowrite32(ch, port->membase);
}

static void gf_early_write(struct console *con, const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, gf_early_console_putchar);
}

static int __init gf_earlycon_setup(struct earlycon_device *device,
				    const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = gf_early_write;
	return 0;
}

OF_EARLYCON_DECLARE(early_gf_tty, "google,goldfish-tty", gf_earlycon_setup);
#endif

static const struct of_device_id goldfish_tty_of_match[] = {
	{ .compatible = "google,goldfish-tty", },
	{},
};

MODULE_DEVICE_TABLE(of, goldfish_tty_of_match);

static struct platform_driver goldfish_tty_platform_driver = {
	.probe = goldfish_tty_probe,
	.remove = goldfish_tty_remove,
	.driver = {
		.name = "goldfish_tty",
		.of_match_table = goldfish_tty_of_match,
	}
};

module_platform_driver(goldfish_tty_platform_driver);

MODULE_LICENSE("GPL v2");
