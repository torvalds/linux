#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>

#include <asm/sibyte/board.h>
#include <asm/sibyte/sb1250_genbus.h>
#include <asm/sibyte/sb1250_regs.h>

#if defined(CONFIG_SIBYTE_SWARM) || defined(CONFIG_SIBYTE_LITTLESUR)

#define DRV_NAME	"pata-swarm"

#define SWARM_IDE_SHIFT	5
#define SWARM_IDE_BASE	0x1f0
#define SWARM_IDE_CTRL	0x3f6

static struct resource swarm_pata_resource[] = {
	{
		.name	= "Swarm GenBus IDE",
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "Swarm GenBus IDE",
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "Swarm GenBus IDE",
		.flags	= IORESOURCE_IRQ,
		.start	= K_INT_GB_IDE,
		.end	= K_INT_GB_IDE,
	},
};

static struct pata_platform_info pata_platform_data = {
	.ioport_shift	= SWARM_IDE_SHIFT,
};

static struct platform_device swarm_pata_device = {
	.name		= "pata_platform",
	.id		= -1,
	.resource	= swarm_pata_resource,
	.num_resources	= ARRAY_SIZE(swarm_pata_resource),
	.dev  = {
		.platform_data		= &pata_platform_data,
		.coherent_dma_mask	= ~0,	/* grumble */
	},
};

static int __init swarm_pata_init(void)
{
	u8 __iomem *base;
	phys_t offset, size;
	struct resource *r;

	if (!SIBYTE_HAVE_IDE)
		return -ENODEV;

	base = ioremap(A_IO_EXT_BASE, 0x800);
	offset = __raw_readq(base + R_IO_EXT_REG(R_IO_EXT_START_ADDR, IDE_CS));
	size = __raw_readq(base + R_IO_EXT_REG(R_IO_EXT_MULT_SIZE, IDE_CS));
	iounmap(base);

	offset = G_IO_START_ADDR(offset) << S_IO_ADDRBASE;
	size = (G_IO_MULT_SIZE(size) + 1) << S_IO_REGSIZE;
	if (offset < A_PHYS_GENBUS || offset >= A_PHYS_GENBUS_END) {
		pr_info(DRV_NAME ": PATA interface at GenBus disabled\n");

		return -EBUSY;
	}

	pr_info(DRV_NAME ": PATA interface at GenBus slot %i\n", IDE_CS);

	r = swarm_pata_resource;
	r[0].start = offset + (SWARM_IDE_BASE << SWARM_IDE_SHIFT);
	r[0].end   = offset + ((SWARM_IDE_BASE + 8) << SWARM_IDE_SHIFT) - 1;
	r[1].start = offset + (SWARM_IDE_CTRL << SWARM_IDE_SHIFT);
	r[1].end   = offset + ((SWARM_IDE_CTRL + 1) << SWARM_IDE_SHIFT) - 1;

	return platform_device_register(&swarm_pata_device);
}

device_initcall(swarm_pata_init);

#endif /* defined(CONFIG_SIBYTE_SWARM) || defined(CONFIG_SIBYTE_LITTLESUR) */

#define sb1250_dev_struct(num) \
	static struct resource sb1250_res##num = {		\
		.name = "SB1250 MAC " __stringify(num),		\
		.flags = IORESOURCE_MEM,		\
		.start = A_MAC_CHANNEL_BASE(num),	\
		.end = A_MAC_CHANNEL_BASE(num + 1) -1,	\
	};\
	static struct platform_device sb1250_dev##num = {	\
		.name = "sb1250-mac",			\
	.id = num,					\
	.resource = &sb1250_res##num,			\
	.num_resources = 1,				\
	}

sb1250_dev_struct(0);
sb1250_dev_struct(1);
sb1250_dev_struct(2);
sb1250_dev_struct(3);

static struct platform_device *sb1250_devs[] __initdata = {
	&sb1250_dev0,
	&sb1250_dev1,
	&sb1250_dev2,
	&sb1250_dev3,
};

static int __init sb1250_device_init(void)
{
	int ret;

	/* Set the number of available units based on the SOC type.  */
	switch (soc_type) {
	case K_SYS_SOC_TYPE_BCM1250:
	case K_SYS_SOC_TYPE_BCM1250_ALT:
		ret = platform_add_devices(sb1250_devs, 3);
		break;
	case K_SYS_SOC_TYPE_BCM1120:
	case K_SYS_SOC_TYPE_BCM1125:
	case K_SYS_SOC_TYPE_BCM1125H:
	case K_SYS_SOC_TYPE_BCM1250_ALT2:       /* Hybrid */
		ret = platform_add_devices(sb1250_devs, 2);
		break;
	case K_SYS_SOC_TYPE_BCM1x55:
	case K_SYS_SOC_TYPE_BCM1x80:
		ret = platform_add_devices(sb1250_devs, 4);
		break;
	default:
		ret = -ENODEV;
		break;
	}
	return ret;
}
device_initcall(sb1250_device_init);
