#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/basic_mmio_gpio.h>

#define ETRAX_FS_rw_pa_dout	0
#define ETRAX_FS_r_pa_din	4
#define ETRAX_FS_rw_pa_oe	8
#define ETRAX_FS_rw_intr_cfg	12
#define ETRAX_FS_rw_intr_mask	16
#define ETRAX_FS_rw_ack_intr	20
#define ETRAX_FS_r_intr		24
#define ETRAX_FS_rw_pb_dout	32
#define ETRAX_FS_r_pb_din	36
#define ETRAX_FS_rw_pb_oe	40
#define ETRAX_FS_rw_pc_dout	48
#define ETRAX_FS_r_pc_din	52
#define ETRAX_FS_rw_pc_oe	56
#define ETRAX_FS_rw_pd_dout	64
#define ETRAX_FS_r_pd_din	68
#define ETRAX_FS_rw_pd_oe	72
#define ETRAX_FS_rw_pe_dout	80
#define ETRAX_FS_r_pe_din	84
#define ETRAX_FS_rw_pe_oe	88

#define ARTPEC3_r_pa_din	0
#define ARTPEC3_rw_pa_dout	4
#define ARTPEC3_rw_pa_oe	8
#define ARTPEC3_r_pb_din	44
#define ARTPEC3_rw_pb_dout	48
#define ARTPEC3_rw_pb_oe	52
#define ARTPEC3_r_pc_din	88
#define ARTPEC3_rw_pc_dout	92
#define ARTPEC3_rw_pc_oe	96
#define ARTPEC3_r_pd_din	116

struct etraxfs_gpio_port {
	const char *label;
	unsigned int oe;
	unsigned int dout;
	unsigned int din;
	unsigned int ngpio;
};

struct etraxfs_gpio_info {
	unsigned int num_ports;
	const struct etraxfs_gpio_port *ports;
};

static const struct etraxfs_gpio_port etraxfs_gpio_etraxfs_ports[] = {
	{
		.label	= "A",
		.ngpio	= 8,
		.oe	= ETRAX_FS_rw_pa_oe,
		.dout	= ETRAX_FS_rw_pa_dout,
		.din	= ETRAX_FS_r_pa_din,
	},
	{
		.label	= "B",
		.ngpio	= 18,
		.oe	= ETRAX_FS_rw_pb_oe,
		.dout	= ETRAX_FS_rw_pb_dout,
		.din	= ETRAX_FS_r_pb_din,
	},
	{
		.label	= "C",
		.ngpio	= 18,
		.oe	= ETRAX_FS_rw_pc_oe,
		.dout	= ETRAX_FS_rw_pc_dout,
		.din	= ETRAX_FS_r_pc_din,
	},
	{
		.label	= "D",
		.ngpio	= 18,
		.oe	= ETRAX_FS_rw_pd_oe,
		.dout	= ETRAX_FS_rw_pd_dout,
		.din	= ETRAX_FS_r_pd_din,
	},
	{
		.label	= "E",
		.ngpio	= 18,
		.oe	= ETRAX_FS_rw_pe_oe,
		.dout	= ETRAX_FS_rw_pe_dout,
		.din	= ETRAX_FS_r_pe_din,
	},
};

static const struct etraxfs_gpio_info etraxfs_gpio_etraxfs = {
	.num_ports = ARRAY_SIZE(etraxfs_gpio_etraxfs_ports),
	.ports = etraxfs_gpio_etraxfs_ports,
};

static const struct etraxfs_gpio_port etraxfs_gpio_artpec3_ports[] = {
	{
		.label	= "A",
		.ngpio	= 32,
		.oe	= ARTPEC3_rw_pa_oe,
		.dout	= ARTPEC3_rw_pa_dout,
		.din	= ARTPEC3_r_pa_din,
	},
	{
		.label	= "B",
		.ngpio	= 32,
		.oe	= ARTPEC3_rw_pb_oe,
		.dout	= ARTPEC3_rw_pb_dout,
		.din	= ARTPEC3_r_pb_din,
	},
	{
		.label	= "C",
		.ngpio	= 16,
		.oe	= ARTPEC3_rw_pc_oe,
		.dout	= ARTPEC3_rw_pc_dout,
		.din	= ARTPEC3_r_pc_din,
	},
	{
		.label	= "D",
		.ngpio	= 32,
		.din	= ARTPEC3_r_pd_din,
	},
};

static const struct etraxfs_gpio_info etraxfs_gpio_artpec3 = {
	.num_ports = ARRAY_SIZE(etraxfs_gpio_artpec3_ports),
	.ports = etraxfs_gpio_artpec3_ports,
};

static int etraxfs_gpio_of_xlate(struct gpio_chip *gc,
			       const struct of_phandle_args *gpiospec,
			       u32 *flags)
{
	/*
	 * Port numbers are A to E, and the properties are integers, so we
	 * specify them as 0xA - 0xE.
	 */
	if (gc->label[0] - 'A' + 0xA != gpiospec->args[2])
		return -EINVAL;

	return of_gpio_simple_xlate(gc, gpiospec, flags);
}

static const struct of_device_id etraxfs_gpio_of_table[] = {
	{
		.compatible = "axis,etraxfs-gio",
		.data = &etraxfs_gpio_etraxfs,
	},
	{
		.compatible = "axis,artpec3-gio",
		.data = &etraxfs_gpio_artpec3,
	},
	{},
};

static int etraxfs_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct etraxfs_gpio_info *info;
	const struct of_device_id *match;
	struct bgpio_chip *chips;
	struct resource *res;
	void __iomem *regs;
	int ret;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	match = of_match_node(etraxfs_gpio_of_table, dev->of_node);
	if (!match)
		return -EINVAL;

	info = match->data;

	chips = devm_kzalloc(dev, sizeof(*chips) * info->num_ports, GFP_KERNEL);
	if (!chips)
		return -ENOMEM;

	for (i = 0; i < info->num_ports; i++) {
		struct bgpio_chip *bgc = &chips[i];
		const struct etraxfs_gpio_port *port = &info->ports[i];
		unsigned long flags = BGPIOF_READ_OUTPUT_REG_SET;
		void __iomem *dat = regs + port->din;
		void __iomem *set = regs + port->dout;
		void __iomem *dirout = regs + port->oe;

		if (dirout == set) {
			dirout = set = NULL;
			flags = BGPIOF_NO_OUTPUT;
		}

		ret = bgpio_init(bgc, dev, 4,
				 dat, set, NULL, dirout, NULL,
				 flags);
		if (ret)
			return ret;

		bgc->gc.ngpio = port->ngpio;
		bgc->gc.label = port->label;

		bgc->gc.of_node = dev->of_node;
		bgc->gc.of_gpio_n_cells = 3;
		bgc->gc.of_xlate = etraxfs_gpio_of_xlate;

		ret = gpiochip_add(&bgc->gc);
		if (ret)
			dev_err(dev, "Unable to register port %s\n",
				bgc->gc.label);
	}

	return 0;
}

static struct platform_driver etraxfs_gpio_driver = {
	.driver = {
		.name		= "etraxfs-gpio",
		.of_match_table = of_match_ptr(etraxfs_gpio_of_table),
	},
	.probe	= etraxfs_gpio_probe,
};

static int __init etraxfs_gpio_init(void)
{
	return platform_driver_register(&etraxfs_gpio_driver);
}

device_initcall(etraxfs_gpio_init);
