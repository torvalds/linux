// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mcb.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>

#define MEN_UART_ID_Z025 0x19
#define MEN_UART_ID_Z057 0x39
#define MEN_UART_ID_Z125 0x7d

/*
 * IP Cores Z025 and Z057 can have up to 4 UART
 * The UARTs available are stored in a global
 * register saved in physical address + 0x40
 * Is saved as follows:
 *
 * 7                                                              0
 * +------+-------+-------+-------+-------+-------+-------+-------+
 * |UART4 | UART3 | UART2 | UART1 | U4irq | U3irq | U2irq | U1irq |
 * +------+-------+-------+-------+-------+-------+-------+-------+
 */
#define MEN_UART1_MASK	0x01
#define MEN_UART2_MASK	0x02
#define MEN_UART3_MASK	0x04
#define MEN_UART4_MASK	0x08

#define MEN_Z125_UARTS_AVAILABLE	0x01

#define MEN_Z025_MAX_UARTS		4
#define MEN_UART_MEM_SIZE		0x10
#define MEM_UART_REGISTER_SIZE		0x01
#define MEN_Z025_REGISTER_OFFSET	0x40

#define MEN_UART1_OFFSET	0
#define MEN_UART2_OFFSET	(MEN_UART1_OFFSET + MEN_UART_MEM_SIZE)
#define MEN_UART3_OFFSET	(MEN_UART2_OFFSET + MEN_UART_MEM_SIZE)
#define MEN_UART4_OFFSET	(MEN_UART3_OFFSET + MEN_UART_MEM_SIZE)

#define MEN_READ_REGISTER(addr)	readb(addr)

#define MAX_PORTS	4

struct serial_8250_men_mcb_data {
	int num_ports;
	int line[MAX_PORTS];
	unsigned int offset[MAX_PORTS];
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
	else if (strncmp(mdev->bus->name, "F210", 4) == 0)
		clkval = 115200;
	else if (strstr(mdev->bus->name, "215"))
		clkval = 1843200;
	else
		dev_info(&mdev->dev,
			 "board not detected, using default uartclk\n");

	clkval = clkval  << 4;

	return clkval;
}

static int read_uarts_available_from_register(struct resource *mem_res,
					      u8 *uarts_available)
{
	void __iomem *mem;
	int reg_value;

	if (!request_mem_region(mem_res->start + MEN_Z025_REGISTER_OFFSET,
				MEM_UART_REGISTER_SIZE,  KBUILD_MODNAME)) {
		return -EBUSY;
	}

	mem = ioremap(mem_res->start + MEN_Z025_REGISTER_OFFSET,
		      MEM_UART_REGISTER_SIZE);
	if (!mem) {
		release_mem_region(mem_res->start + MEN_Z025_REGISTER_OFFSET,
				   MEM_UART_REGISTER_SIZE);
		return -ENOMEM;
	}

	reg_value = MEN_READ_REGISTER(mem);

	iounmap(mem);

	release_mem_region(mem_res->start + MEN_Z025_REGISTER_OFFSET,
			   MEM_UART_REGISTER_SIZE);

	*uarts_available = reg_value >> 4;

	return 0;
}

static int read_serial_data(struct mcb_device *mdev,
			    struct resource *mem_res,
			    struct serial_8250_men_mcb_data *serial_data)
{
	u8 uarts_available;
	int count = 0;
	int mask;
	int res;
	int i;

	res = read_uarts_available_from_register(mem_res, &uarts_available);
	if (res < 0)
		return res;

	for (i = 0; i < MAX_PORTS; i++) {
		mask = 0x1 << i;
		switch (uarts_available & mask) {
		case MEN_UART1_MASK:
			serial_data->offset[count] = MEN_UART1_OFFSET;
			count++;
			break;
		case MEN_UART2_MASK:
			serial_data->offset[count] = MEN_UART2_OFFSET;
			count++;
			break;
		case MEN_UART3_MASK:
			serial_data->offset[count] = MEN_UART3_OFFSET;
			count++;
			break;
		case MEN_UART4_MASK:
			serial_data->offset[count] = MEN_UART4_OFFSET;
			count++;
			break;
		default:
			return -EINVAL;
		}
	}

	if (count <= 0 || count > MAX_PORTS) {
		dev_err(&mdev->dev, "unexpected number of ports: %u\n",
			count);
		return -ENODEV;
	}

	serial_data->num_ports = count;

	return 0;
}

static int init_serial_data(struct mcb_device *mdev,
			    struct resource *mem_res,
			    struct serial_8250_men_mcb_data *serial_data)
{
	switch (mdev->id) {
	case MEN_UART_ID_Z125:
		serial_data->num_ports = 1;
		serial_data->offset[0] = 0;
		return 0;
	case MEN_UART_ID_Z025:
	case MEN_UART_ID_Z057:
		return read_serial_data(mdev, mem_res, serial_data);
	default:
		dev_err(&mdev->dev, "no supported device!\n");
		return -ENODEV;
	}
}

static int serial_8250_men_mcb_probe(struct mcb_device *mdev,
				     const struct mcb_device_id *id)
{
	struct uart_8250_port uart;
	struct serial_8250_men_mcb_data *data;
	struct resource *mem;
	int i;
	int res;

	mem = mcb_get_resource(mdev, IORESOURCE_MEM);
	if (mem == NULL)
		return -ENXIO;

	data = devm_kzalloc(&mdev->dev,
			    sizeof(struct serial_8250_men_mcb_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = init_serial_data(mdev, mem, data);
	if (res < 0)
		return res;

	dev_dbg(&mdev->dev, "found a 16z%03u with %u ports\n",
		mdev->id, data->num_ports);

	mcb_set_drvdata(mdev, data);

	for (i = 0; i < data->num_ports; i++) {
		memset(&uart, 0, sizeof(struct uart_8250_port));
		spin_lock_init(&uart.port.lock);

		uart.port.flags = UPF_SKIP_TEST |
				  UPF_SHARE_IRQ |
				  UPF_BOOT_AUTOCONF |
				  UPF_IOREMAP;
		uart.port.iotype = UPIO_MEM;
		uart.port.uartclk = men_lookup_uartclk(mdev);
		uart.port.irq = mcb_get_irq(mdev);
		uart.port.mapbase = (unsigned long) mem->start
					    + data->offset[i];

		/* ok, register the port */
		res = serial8250_register_8250_port(&uart);
		if (res < 0) {
			dev_err(&mdev->dev, "unable to register UART port\n");
			return res;
		}

		data->line[i] = res;
		dev_info(&mdev->dev, "found MCB UART: ttyS%d\n", data->line[i]);
	}

	return 0;
}

static void serial_8250_men_mcb_remove(struct mcb_device *mdev)
{
	int i;
	struct serial_8250_men_mcb_data *data = mcb_get_drvdata(mdev);

	if (!data)
		return;

	for (i = 0; i < data->num_ports; i++)
		serial8250_unregister_port(data->line[i]);
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
MODULE_IMPORT_NS("MCB");
