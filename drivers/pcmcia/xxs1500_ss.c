/*
 * PCMCIA socket code for the MyCable XXS1500 system.
 *
 * Copyright (c) 2009 Manuel Lauss <manuel.lauss@gmail.com>
 *
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/resource.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>

#include <asm/irq.h>
#include <asm/mach-au1x00/au1000.h>

#define MEM_MAP_SIZE	0x400000
#define IO_MAP_SIZE	0x1000


/*
 * 3.3V cards only; all interfacing is done via gpios:
 *
 * 0/1:  carddetect (00 = card present, xx = huh)
 * 4:	 card irq
 * 204:  reset (high-act)
 * 205:  buffer enable (low-act)
 * 208/209: card voltage key (00,01,10,11)
 * 210:  battwarn
 * 211:  batdead
 * 214:  power (low-act)
 */
#define GPIO_CDA	0
#define GPIO_CDB	1
#define GPIO_CARDIRQ	4
#define GPIO_RESET	204
#define GPIO_OUTEN	205
#define GPIO_VSL	208
#define GPIO_VSH	209
#define GPIO_BATTDEAD	210
#define GPIO_BATTWARN	211
#define GPIO_POWER	214

struct xxs1500_pcmcia_sock {
	struct pcmcia_socket	socket;
	void		*virt_io;

	phys_addr_t	phys_io;
	phys_addr_t	phys_attr;
	phys_addr_t	phys_mem;

	/* previous flags for set_socket() */
	unsigned int old_flags;
};

#define to_xxs_socket(x) container_of(x, struct xxs1500_pcmcia_sock, socket)

static irqreturn_t cdirq(int irq, void *data)
{
	struct xxs1500_pcmcia_sock *sock = data;

	pcmcia_parse_events(&sock->socket, SS_DETECT);

	return IRQ_HANDLED;
}

static int xxs1500_pcmcia_configure(struct pcmcia_socket *skt,
				    struct socket_state_t *state)
{
	struct xxs1500_pcmcia_sock *sock = to_xxs_socket(skt);
	unsigned int changed;

	/* power control */
	switch (state->Vcc) {
	case 0:
		gpio_set_value(GPIO_POWER, 1);	/* power off */
		break;
	case 33:
		gpio_set_value(GPIO_POWER, 0);	/* power on */
		break;
	case 50:
	default:
		return -EINVAL;
	}

	changed = state->flags ^ sock->old_flags;

	if (changed & SS_RESET) {
		if (state->flags & SS_RESET) {
			gpio_set_value(GPIO_RESET, 1);	/* assert reset */
			gpio_set_value(GPIO_OUTEN, 1);	/* buffers off */
		} else {
			gpio_set_value(GPIO_RESET, 0);	/* deassert reset */
			gpio_set_value(GPIO_OUTEN, 0);	/* buffers on */
			msleep(500);
		}
	}

	sock->old_flags = state->flags;

	return 0;
}

static int xxs1500_pcmcia_get_status(struct pcmcia_socket *skt,
				     unsigned int *value)
{
	unsigned int status;
	int i;

	status = 0;

	/* check carddetects: GPIO[0:1] must both be low */
	if (!gpio_get_value(GPIO_CDA) && !gpio_get_value(GPIO_CDB))
		status |= SS_DETECT;

	/* determine card voltage: GPIO[208:209] binary value */
	i = (!!gpio_get_value(GPIO_VSL)) | ((!!gpio_get_value(GPIO_VSH)) << 1);

	switch (i) {
	case 0:
	case 1:
	case 2:
		status |= SS_3VCARD;	/* 3V card */
		break;
	case 3:				/* 5V card, unsupported */
	default:
		status |= SS_XVCARD;	/* treated as unsupported in core */
	}

	/* GPIO214: low active power switch */
	status |= gpio_get_value(GPIO_POWER) ? 0 : SS_POWERON;

	/* GPIO204: high-active reset line */
	status |= gpio_get_value(GPIO_RESET) ? SS_RESET : SS_READY;

	/* other stuff */
	status |= gpio_get_value(GPIO_BATTDEAD) ? 0 : SS_BATDEAD;
	status |= gpio_get_value(GPIO_BATTWARN) ? 0 : SS_BATWARN;

	*value = status;

	return 0;
}

static int xxs1500_pcmcia_sock_init(struct pcmcia_socket *skt)
{
	gpio_direction_input(GPIO_CDA);
	gpio_direction_input(GPIO_CDB);
	gpio_direction_input(GPIO_VSL);
	gpio_direction_input(GPIO_VSH);
	gpio_direction_input(GPIO_BATTDEAD);
	gpio_direction_input(GPIO_BATTWARN);
	gpio_direction_output(GPIO_RESET, 1);	/* assert reset */
	gpio_direction_output(GPIO_OUTEN, 1);	/* disable buffers */
	gpio_direction_output(GPIO_POWER, 1);	/* power off */

	return 0;
}

static int xxs1500_pcmcia_sock_suspend(struct pcmcia_socket *skt)
{
	return 0;
}

static int au1x00_pcmcia_set_io_map(struct pcmcia_socket *skt,
				    struct pccard_io_map *map)
{
	struct xxs1500_pcmcia_sock *sock = to_xxs_socket(skt);

	map->start = (u32)sock->virt_io;
	map->stop = map->start + IO_MAP_SIZE;

	return 0;
}

static int au1x00_pcmcia_set_mem_map(struct pcmcia_socket *skt,
				     struct pccard_mem_map *map)
{
	struct xxs1500_pcmcia_sock *sock = to_xxs_socket(skt);

	if (map->flags & MAP_ATTRIB)
		map->static_start = sock->phys_attr + map->card_start;
	else
		map->static_start = sock->phys_mem + map->card_start;

	return 0;
}

static struct pccard_operations xxs1500_pcmcia_operations = {
	.init			= xxs1500_pcmcia_sock_init,
	.suspend		= xxs1500_pcmcia_sock_suspend,
	.get_status		= xxs1500_pcmcia_get_status,
	.set_socket		= xxs1500_pcmcia_configure,
	.set_io_map		= au1x00_pcmcia_set_io_map,
	.set_mem_map		= au1x00_pcmcia_set_mem_map,
};

static int xxs1500_pcmcia_probe(struct platform_device *pdev)
{
	struct xxs1500_pcmcia_sock *sock;
	struct resource *r;
	int ret, irq;

	sock = kzalloc(sizeof(struct xxs1500_pcmcia_sock), GFP_KERNEL);
	if (!sock)
		return -ENOMEM;

	ret = -ENODEV;

	/* 36bit PCMCIA Attribute area address */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcmcia-attr");
	if (!r) {
		dev_err(&pdev->dev, "missing 'pcmcia-attr' resource!\n");
		goto out0;
	}
	sock->phys_attr = r->start;

	/* 36bit PCMCIA Memory area address */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcmcia-mem");
	if (!r) {
		dev_err(&pdev->dev, "missing 'pcmcia-mem' resource!\n");
		goto out0;
	}
	sock->phys_mem = r->start;

	/* 36bit PCMCIA IO area address */
	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pcmcia-io");
	if (!r) {
		dev_err(&pdev->dev, "missing 'pcmcia-io' resource!\n");
		goto out0;
	}
	sock->phys_io = r->start;


	/*
	 * PCMCIA client drivers use the inb/outb macros to access
	 * the IO registers.  Since mips_io_port_base is added
	 * to the access address of the mips implementation of
	 * inb/outb, we need to subtract it here because we want
	 * to access the I/O or MEM address directly, without
	 * going through this "mips_io_port_base" mechanism.
	 */
	sock->virt_io = (void *)(ioremap(sock->phys_io, IO_MAP_SIZE) -
				 mips_io_port_base);

	if (!sock->virt_io) {
		dev_err(&pdev->dev, "cannot remap IO area\n");
		ret = -ENOMEM;
		goto out0;
	}

	sock->socket.ops	= &xxs1500_pcmcia_operations;
	sock->socket.owner	= THIS_MODULE;
	sock->socket.pci_irq	= gpio_to_irq(GPIO_CARDIRQ);
	sock->socket.features	= SS_CAP_STATIC_MAP | SS_CAP_PCCARD;
	sock->socket.map_size	= MEM_MAP_SIZE;
	sock->socket.io_offset	= (unsigned long)sock->virt_io;
	sock->socket.dev.parent	= &pdev->dev;
	sock->socket.resource_ops = &pccard_static_ops;

	platform_set_drvdata(pdev, sock);

	/* setup carddetect irq: use one of the 2 GPIOs as an
	 * edge detector.
	 */
	irq = gpio_to_irq(GPIO_CDA);
	irq_set_irq_type(irq, IRQ_TYPE_EDGE_BOTH);
	ret = request_irq(irq, cdirq, 0, "pcmcia_carddetect", sock);
	if (ret) {
		dev_err(&pdev->dev, "cannot setup cd irq\n");
		goto out1;
	}

	ret = pcmcia_register_socket(&sock->socket);
	if (ret) {
		dev_err(&pdev->dev, "failed to register\n");
		goto out2;
	}

	printk(KERN_INFO "MyCable XXS1500 PCMCIA socket services\n");

	return 0;

out2:
	free_irq(gpio_to_irq(GPIO_CDA), sock);
out1:
	iounmap((void *)(sock->virt_io + (u32)mips_io_port_base));
out0:
	kfree(sock);
	return ret;
}

static int xxs1500_pcmcia_remove(struct platform_device *pdev)
{
	struct xxs1500_pcmcia_sock *sock = platform_get_drvdata(pdev);

	pcmcia_unregister_socket(&sock->socket);
	free_irq(gpio_to_irq(GPIO_CDA), sock);
	iounmap((void *)(sock->virt_io + (u32)mips_io_port_base));
	kfree(sock);

	return 0;
}

static struct platform_driver xxs1500_pcmcia_socket_driver = {
	.driver	= {
		.name	= "xxs1500_pcmcia",
	},
	.probe		= xxs1500_pcmcia_probe,
	.remove		= xxs1500_pcmcia_remove,
};

module_platform_driver(xxs1500_pcmcia_socket_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCMCIA Socket Services for MyCable XXS1500 systems");
MODULE_AUTHOR("Manuel Lauss");
