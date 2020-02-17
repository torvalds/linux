#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mcb.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <uapi/linux/serial_core.h>

#define MEN_UART_ID_Z025 0x19
#define MEN_UART_ID_Z057 0x39
#define MEN_UART_ID_Z125 0x7d

#define MEN_UART_MEM_SIZE 0x10

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
static u32 men_lookup_uartclk(struct mcb_device *mdev)
{
	/* use default value if board is not available below */
	u32 clkval = 1041666;

	dev_info(&mdev->dev, "%s on board %s\n",
		dev_name(&mdev->dev),
		mdev->bus->name);
	if  (strncmp(mdev->bus->name, "F075", 4) == 0)
		clkval = 1041666;
	else if (strncmp(mdev->bus->name, "F216", 4) == 0)
		clkval = 1843200;
	else if (strncmp(mdev->bus->name, "G215", 4) == 0)
		clkval = 1843200;
	else if (strncmp(mdev->bus->name, "F210", 4) == 0)
		clkval = 115200;
	else
		dev_info(&mdev->dev,
			 "board not detected, using default uartclk\n");

	clkval = clkval  << 4;

	return clkval;
}

static unsigned int get_num_ports(struct mcb_device *mdev,
				  void __iomem *membase)
{
	switch (mdev->id) {
	case MEN_UART_ID_Z125:
		return 1U;
	case MEN_UART_ID_Z025:
		return readb(membase) >> 4;
	case MEN_UART_ID_Z057:
		return 4U;
	default:
		dev_err(&mdev->dev, "no supported device!\n");
		return -ENODEV;
	}
}

static int serial_8250_men_mcb_probe(struct mcb_device *mdev,
				     const struct mcb_device_id *id)
{
	struct serial_8250_men_mcb_data *data;
	struct resource *mem;
	int num_ports;
	int i;
	void __iomem *membase;

	mem = mcb_get_resource(mdev, IORESOURCE_MEM);
	if (mem == NULL)
		return -ENXIO;
	membase = devm_ioremap_resource(&mdev->dev, mem);
	if (IS_ERR(membase))
		return PTR_ERR_OR_ZERO(membase);

	num_ports = get_num_ports(mdev, membase);

	dev_dbg(&mdev->dev, "found a 16z%03u with %u ports\n",
		mdev->id, num_ports);

	if (num_ports <= 0 || num_ports > 4) {
		dev_err(&mdev->dev, "unexpected number of ports: %u\n",
			num_ports);
		return -ENODEV;
	}

	data = devm_kcalloc(&mdev->dev, num_ports,
			    sizeof(struct serial_8250_men_mcb_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mcb_set_drvdata(mdev, data);

	for (i = 0; i < num_ports; i++) {
		data[i].uart.port.dev = mdev->dma_dev;
		spin_lock_init(&data[i].uart.port.lock);

		data[i].uart.port.type = PORT_16550;
		data[i].uart.port.flags = UPF_SKIP_TEST | UPF_SHARE_IRQ
					  | UPF_FIXED_TYPE;
		data[i].uart.port.iotype = UPIO_MEM;
		data[i].uart.port.uartclk = men_lookup_uartclk(mdev);
		data[i].uart.port.regshift = 0;
		data[i].uart.port.irq = mcb_get_irq(mdev);
		data[i].uart.port.membase = membase;
		data[i].uart.port.fifosize = 60;
		data[i].uart.port.mapbase = (unsigned long) mem->start
					    + i * MEN_UART_MEM_SIZE;
		data[i].uart.port.iobase = data[i].uart.port.mapbase;

		/* ok, register the port */
		data[i].line = serial8250_register_8250_port(&data[i].uart);
		if (data[i].line < 0) {
			dev_err(&mdev->dev, "unable to register UART port\n");
			return data[i].line;
		}
		dev_info(&mdev->dev, "found MCB UART: ttyS%d\n", data[i].line);
	}

	return 0;
}

static void serial_8250_men_mcb_remove(struct mcb_device *mdev)
{
	int num_ports, i;
	struct serial_8250_men_mcb_data *data = mcb_get_drvdata(mdev);

	if (!data)
		return;

	num_ports = get_num_ports(mdev, data[0].uart.port.membase);
	if (num_ports < 0 || num_ports > 4) {
		dev_err(&mdev->dev, "error retrieving number of ports!\n");
		return;
	}

	for (i = 0; i < num_ports; i++)
		serial8250_unregister_port(data[i].line);
}

static const struct mcb_device_id serial_8250_men_mcb_ids[] = {
	{ .device = MEN_UART_ID_Z025 },
	{ .device = MEN_UART_ID_Z057 },
	{ .device = MEN_UART_ID_Z125 },
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
MODULE_DESCRIPTION("MEN 8250 UART driver");
MODULE_AUTHOR("Michael Moese <michael.moese@men.de");
MODULE_ALIAS("mcb:16z125");
MODULE_ALIAS("mcb:16z025");
MODULE_ALIAS("mcb:16z057");
