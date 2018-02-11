#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mcb.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <uapi/linux/serial_core.h>

struct serial_8250_men_mcb_data {
	struct uart_8250_port uart;
	int line;
};

/*
 * The Z125 16550-compatible UART has no fixed base clock assigned
 * So, depending on the board we're on, we need to adjust the
 * parameter in order to really set the correct baudrate, and
 * do so if possible without user interaction
 */
static u32 men_z125_lookup_uartclk(struct mcb_device *mdev)
{
	/* use default value if board is not available below */
	u32 clkval = 1041666;

	dev_info(&mdev->dev, "%s on board %s\n",
		dev_name(&mdev->dev),
		mdev->bus->name);
	if  (strncmp(mdev->bus->name, "F075", 4) == 0)
		clkval = 1041666;
	else if  (strncmp(mdev->bus->name, "F216", 4) == 0)
		clkval = 1843200;
	else if (strncmp(mdev->bus->name, "G215", 4) == 0)
		clkval = 1843200;
	else
		dev_info(&mdev->dev,
			 "board not detected, using default uartclk\n");

	clkval = clkval  << 4;

	return clkval;
}

static int serial_8250_men_mcb_probe(struct mcb_device *mdev,
				     const struct mcb_device_id *id)
{
	struct serial_8250_men_mcb_data *data;
	struct resource *mem;

	data = devm_kzalloc(&mdev->dev,
			    sizeof(struct serial_8250_men_mcb_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mcb_set_drvdata(mdev, data);
	data->uart.port.dev = mdev->dma_dev;
	spin_lock_init(&data->uart.port.lock);

	data->uart.port.type = PORT_16550;
	data->uart.port.flags = UPF_SKIP_TEST | UPF_SHARE_IRQ | UPF_FIXED_TYPE;
	data->uart.port.iotype = UPIO_MEM;
	data->uart.port.uartclk = men_z125_lookup_uartclk(mdev);
	data->uart.port.regshift = 0;
	data->uart.port.fifosize = 60;

	mem = mcb_get_resource(mdev, IORESOURCE_MEM);
	if (mem == NULL)
		return -ENXIO;

	data->uart.port.irq = mcb_get_irq(mdev);

	data->uart.port.membase = devm_ioremap_resource(&mdev->dev, mem);
	if (IS_ERR(data->uart.port.membase))
		return PTR_ERR_OR_ZERO(data->uart.port.membase);

	data->uart.port.mapbase = (unsigned long) mem->start;
	data->uart.port.iobase = data->uart.port.mapbase;

	/* ok, register the port */
	data->line = serial8250_register_8250_port(&data->uart);
	if (data->line < 0)
		return data->line;

	dev_info(&mdev->dev, "found 16Z125 UART: ttyS%d\n", data->line);

	return 0;
}

static void serial_8250_men_mcb_remove(struct mcb_device *mdev)
{
	struct serial_8250_men_mcb_data *data = mcb_get_drvdata(mdev);

	if (data)
		serial8250_unregister_port(data->line);
}

static const struct mcb_device_id serial_8250_men_mcb_ids[] = {
	{ .device = 0x7d },
	{ }
};
MODULE_DEVICE_TABLE(mcb, serial_8250_men_mcb_ids);

static struct mcb_driver mcb_driver = {
	.driver = {
		.name = "8250_men_mcb",
		.owner = THIS_MODULE,
	},
	.probe = serial_8250_men_mcb_probe,
	.remove = serial_8250_men_mcb_remove,
	.id_table = serial_8250_men_mcb_ids,
};
module_mcb_driver(mcb_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MEN 16z125 8250 UART driver");
MODULE_AUTHOR("Michael Moese <michael.moese@men.de");
MODULE_ALIAS("mcb:16z125");
