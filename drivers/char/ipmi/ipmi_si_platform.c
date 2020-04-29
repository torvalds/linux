// SPDX-License-Identifier: GPL-2.0+
/*
 * ipmi_si_platform.c
 *
 * Handling for platform devices in IPMI (ACPI, OF, and things
 * coming from the platform.
 */

#define pr_fmt(fmt) "ipmi_platform: " fmt
#define dev_fmt pr_fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/acpi.h>
#include "ipmi_si.h"
#include "ipmi_dmi.h"

static bool platform_registered;
static bool si_tryplatform = true;
#ifdef CONFIG_ACPI
static bool          si_tryacpi = true;
#endif
#ifdef CONFIG_OF
static bool          si_tryopenfirmware = true;
#endif
#ifdef CONFIG_DMI
static bool          si_trydmi = true;
#else
static bool          si_trydmi = false;
#endif

module_param_named(tryplatform, si_tryplatform, bool, 0);
MODULE_PARM_DESC(tryplatform, "Setting this to zero will disable the"
		 " default scan of the interfaces identified via platform"
		 " interfaces besides ACPI, OpenFirmware, and DMI");
#ifdef CONFIG_ACPI
module_param_named(tryacpi, si_tryacpi, bool, 0);
MODULE_PARM_DESC(tryacpi, "Setting this to zero will disable the"
		 " default scan of the interfaces identified via ACPI");
#endif
#ifdef CONFIG_OF
module_param_named(tryopenfirmware, si_tryopenfirmware, bool, 0);
MODULE_PARM_DESC(tryopenfirmware, "Setting this to zero will disable the"
		 " default scan of the interfaces identified via OpenFirmware");
#endif
#ifdef CONFIG_DMI
module_param_named(trydmi, si_trydmi, bool, 0);
MODULE_PARM_DESC(trydmi, "Setting this to zero will disable the"
		 " default scan of the interfaces identified via DMI");
#endif

#ifdef CONFIG_ACPI
/* For GPE-type interrupts. */
static u32 ipmi_acpi_gpe(acpi_handle gpe_device,
	u32 gpe_number, void *context)
{
	struct si_sm_io *io = context;

	ipmi_si_irq_handler(io->irq, io->irq_handler_data);
	return ACPI_INTERRUPT_HANDLED;
}

static void acpi_gpe_irq_cleanup(struct si_sm_io *io)
{
	if (!io->irq)
		return;

	ipmi_irq_start_cleanup(io);
	acpi_remove_gpe_handler(NULL, io->irq, &ipmi_acpi_gpe);
}

static int acpi_gpe_irq_setup(struct si_sm_io *io)
{
	acpi_status status;

	if (!io->irq)
		return 0;

	status = acpi_install_gpe_handler(NULL,
					  io->irq,
					  ACPI_GPE_LEVEL_TRIGGERED,
					  &ipmi_acpi_gpe,
					  io);
	if (status != AE_OK) {
		dev_warn(io->dev,
			 "Unable to claim ACPI GPE %d, running polled\n",
			 io->irq);
		io->irq = 0;
		return -EINVAL;
	} else {
		io->irq_cleanup = acpi_gpe_irq_cleanup;
		ipmi_irq_finish_setup(io);
		dev_info(io->dev, "Using ACPI GPE %d\n", io->irq);
		return 0;
	}
}
#endif

static struct resource *
ipmi_get_info_from_resources(struct platform_device *pdev,
			     struct si_sm_io *io)
{
	struct resource *res, *res_second;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (res) {
		io->addr_space = IPMI_IO_ADDR_SPACE;
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (res)
			io->addr_space = IPMI_MEM_ADDR_SPACE;
	}
	if (!res) {
		dev_err(&pdev->dev, "no I/O or memory address\n");
		return NULL;
	}
	io->addr_data = res->start;

	io->regspacing = DEFAULT_REGSPACING;
	res_second = platform_get_resource(pdev,
			       (io->addr_space == IPMI_IO_ADDR_SPACE) ?
					IORESOURCE_IO : IORESOURCE_MEM,
			       1);
	if (res_second) {
		if (res_second->start > io->addr_data)
			io->regspacing = res_second->start - io->addr_data;
	}

	return res;
}

static int platform_ipmi_probe(struct platform_device *pdev)
{
	struct si_sm_io io;
	u8 type, slave_addr, addr_source, regsize, regshift;
	int rv;

	rv = device_property_read_u8(&pdev->dev, "addr-source", &addr_source);
	if (rv)
		addr_source = SI_PLATFORM;
	if (addr_source >= SI_LAST)
		return -EINVAL;

	if (addr_source == SI_SMBIOS) {
		if (!si_trydmi)
			return -ENODEV;
	} else if (addr_source != SI_HARDCODED) {
		if (!si_tryplatform)
			return -ENODEV;
	}

	rv = device_property_read_u8(&pdev->dev, "ipmi-type", &type);
	if (rv)
		return -ENODEV;

	memset(&io, 0, sizeof(io));
	io.addr_source = addr_source;
	dev_info(&pdev->dev, "probing via %s\n",
		 ipmi_addr_src_to_str(addr_source));

	switch (type) {
	case SI_KCS:
	case SI_SMIC:
	case SI_BT:
		io.si_type = type;
		break;
	case SI_TYPE_INVALID: /* User disabled this in hardcode. */
		return -ENODEV;
	default:
		dev_err(&pdev->dev, "ipmi-type property is invalid\n");
		return -EINVAL;
	}

	io.regsize = DEFAULT_REGSIZE;
	rv = device_property_read_u8(&pdev->dev, "reg-size", &regsize);
	if (!rv)
		io.regsize = regsize;

	io.regshift = 0;
	rv = device_property_read_u8(&pdev->dev, "reg-shift", &regshift);
	if (!rv)
		io.regshift = regshift;

	if (!ipmi_get_info_from_resources(pdev, &io))
		return -EINVAL;

	rv = device_property_read_u8(&pdev->dev, "slave-addr", &slave_addr);
	if (rv)
		io.slave_addr = 0x20;
	else
		io.slave_addr = slave_addr;

	io.irq = platform_get_irq_optional(pdev, 0);
	if (io.irq > 0)
		io.irq_setup = ipmi_std_irq_setup;
	else
		io.irq = 0;

	io.dev = &pdev->dev;

	pr_info("ipmi_si: %s: %s %#lx regsize %d spacing %d irq %d\n",
		ipmi_addr_src_to_str(addr_source),
		(io.addr_space == IPMI_IO_ADDR_SPACE) ? "io" : "mem",
		io.addr_data, io.regsize, io.regspacing, io.irq);

	ipmi_si_add_smi(&io);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id of_ipmi_match[] = {
	{ .type = "ipmi", .compatible = "ipmi-kcs",
	  .data = (void *)(unsigned long) SI_KCS },
	{ .type = "ipmi", .compatible = "ipmi-smic",
	  .data = (void *)(unsigned long) SI_SMIC },
	{ .type = "ipmi", .compatible = "ipmi-bt",
	  .data = (void *)(unsigned long) SI_BT },
	{},
};
MODULE_DEVICE_TABLE(of, of_ipmi_match);

static int of_ipmi_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct si_sm_io io;
	struct resource resource;
	const __be32 *regsize, *regspacing, *regshift;
	struct device_node *np = pdev->dev.of_node;
	int ret;
	int proplen;

	if (!si_tryopenfirmware)
		return -ENODEV;

	dev_info(&pdev->dev, "probing via device tree\n");

	match = of_match_device(of_ipmi_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	if (!of_device_is_available(np))
		return -EINVAL;

	ret = of_address_to_resource(np, 0, &resource);
	if (ret) {
		dev_warn(&pdev->dev, "invalid address from OF\n");
		return ret;
	}

	regsize = of_get_property(np, "reg-size", &proplen);
	if (regsize && proplen != 4) {
		dev_warn(&pdev->dev, "invalid regsize from OF\n");
		return -EINVAL;
	}

	regspacing = of_get_property(np, "reg-spacing", &proplen);
	if (regspacing && proplen != 4) {
		dev_warn(&pdev->dev, "invalid regspacing from OF\n");
		return -EINVAL;
	}

	regshift = of_get_property(np, "reg-shift", &proplen);
	if (regshift && proplen != 4) {
		dev_warn(&pdev->dev, "invalid regshift from OF\n");
		return -EINVAL;
	}

	memset(&io, 0, sizeof(io));
	io.si_type	= (enum si_type) match->data;
	io.addr_source	= SI_DEVICETREE;
	io.irq_setup	= ipmi_std_irq_setup;

	if (resource.flags & IORESOURCE_IO)
		io.addr_space = IPMI_IO_ADDR_SPACE;
	else
		io.addr_space = IPMI_MEM_ADDR_SPACE;

	io.addr_data	= resource.start;

	io.regsize	= regsize ? be32_to_cpup(regsize) : DEFAULT_REGSIZE;
	io.regspacing	= regspacing ? be32_to_cpup(regspacing) : DEFAULT_REGSPACING;
	io.regshift	= regshift ? be32_to_cpup(regshift) : 0;

	io.irq		= irq_of_parse_and_map(pdev->dev.of_node, 0);
	io.dev		= &pdev->dev;

	dev_dbg(&pdev->dev, "addr 0x%lx regsize %d spacing %d irq %d\n",
		io.addr_data, io.regsize, io.regspacing, io.irq);

	return ipmi_si_add_smi(&io);
}
#else
#define of_ipmi_match NULL
static int of_ipmi_probe(struct platform_device *dev)
{
	return -ENODEV;
}
#endif

#ifdef CONFIG_ACPI
static int find_slave_address(struct si_sm_io *io, int slave_addr)
{
#ifdef CONFIG_IPMI_DMI_DECODE
	if (!slave_addr)
		slave_addr = ipmi_dmi_get_slave_addr(io->si_type,
						     io->addr_space,
						     io->addr_data);
#endif

	return slave_addr;
}

static int acpi_ipmi_probe(struct platform_device *pdev)
{
	struct si_sm_io io;
	acpi_handle handle;
	acpi_status status;
	unsigned long long tmp;
	struct resource *res;
	int rv = -EINVAL;

	if (!si_tryacpi)
		return -ENODEV;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	memset(&io, 0, sizeof(io));
	io.addr_source = SI_ACPI;
	dev_info(&pdev->dev, "probing via ACPI\n");

	io.addr_info.acpi_info.acpi_handle = handle;

	/* _IFT tells us the interface type: KCS, BT, etc */
	status = acpi_evaluate_integer(handle, "_IFT", NULL, &tmp);
	if (ACPI_FAILURE(status)) {
		dev_err(&pdev->dev,
			"Could not find ACPI IPMI interface type\n");
		goto err_free;
	}

	switch (tmp) {
	case 1:
		io.si_type = SI_KCS;
		break;
	case 2:
		io.si_type = SI_SMIC;
		break;
	case 3:
		io.si_type = SI_BT;
		break;
	case 4: /* SSIF, just ignore */
		rv = -ENODEV;
		goto err_free;
	default:
		dev_info(&pdev->dev, "unknown IPMI type %lld\n", tmp);
		goto err_free;
	}

	io.regsize = DEFAULT_REGSIZE;
	io.regshift = 0;

	res = ipmi_get_info_from_resources(pdev, &io);
	if (!res) {
		rv = -EINVAL;
		goto err_free;
	}

	/* If _GPE exists, use it; otherwise use standard interrupts */
	status = acpi_evaluate_integer(handle, "_GPE", NULL, &tmp);
	if (ACPI_SUCCESS(status)) {
		io.irq = tmp;
		io.irq_setup = acpi_gpe_irq_setup;
	} else {
		int irq = platform_get_irq_optional(pdev, 0);

		if (irq > 0) {
			io.irq = irq;
			io.irq_setup = ipmi_std_irq_setup;
		}
	}

	io.slave_addr = find_slave_address(&io, io.slave_addr);

	io.dev = &pdev->dev;

	dev_info(io.dev, "%pR regsize %d spacing %d irq %d\n",
		 res, io.regsize, io.regspacing, io.irq);

	return ipmi_si_add_smi(&io);

err_free:
	return rv;
}

static const struct acpi_device_id acpi_ipmi_match[] = {
	{ "IPI0001", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, acpi_ipmi_match);
#else
static int acpi_ipmi_probe(struct platform_device *dev)
{
	return -ENODEV;
}
#endif

static int ipmi_probe(struct platform_device *pdev)
{
	if (pdev->dev.of_node && of_ipmi_probe(pdev) == 0)
		return 0;

	if (acpi_ipmi_probe(pdev) == 0)
		return 0;

	return platform_ipmi_probe(pdev);
}

static int ipmi_remove(struct platform_device *pdev)
{
	return ipmi_si_remove_by_dev(&pdev->dev);
}

static int pdev_match_name(struct device *dev, const void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const char *name = data;

	return strcmp(pdev->name, name) == 0;
}

void ipmi_remove_platform_device_by_name(char *name)
{
	struct device *dev;

	while ((dev = bus_find_device(&platform_bus_type, NULL, name,
				      pdev_match_name))) {
		struct platform_device *pdev = to_platform_device(dev);

		platform_device_unregister(pdev);
		put_device(dev);
	}
}

static const struct platform_device_id si_plat_ids[] = {
	{ "dmi-ipmi-si", 0 },
	{ "hardcode-ipmi-si", 0 },
	{ "hotmod-ipmi-si", 0 },
	{ }
};

struct platform_driver ipmi_platform_driver = {
	.driver = {
		.name = SI_DEVICE_NAME,
		.of_match_table = of_ipmi_match,
		.acpi_match_table = ACPI_PTR(acpi_ipmi_match),
	},
	.probe		= ipmi_probe,
	.remove		= ipmi_remove,
	.id_table       = si_plat_ids
};

void ipmi_si_platform_init(void)
{
	int rv = platform_driver_register(&ipmi_platform_driver);
	if (rv)
		pr_err("Unable to register driver: %d\n", rv);
	else
		platform_registered = true;
}

void ipmi_si_platform_shutdown(void)
{
	if (platform_registered)
		platform_driver_unregister(&ipmi_platform_driver);
}
