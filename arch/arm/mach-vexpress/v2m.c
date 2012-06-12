/*
 * Versatile Express V2M Motherboard Support
 */
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/smsc911x.h>
#include <linux/spinlock.h>
#include <linux/usb/isp1760.h>
#include <linux/clkdev.h>
#include <linux/mtd/physmap.h>

#include <asm/arch_timer.h>
#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/smp_twd.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/timer-sp.h>
#include <asm/hardware/sp810.h>

#include <mach/ct-ca9x4.h>
#include <mach/motherboard.h>

#include <plat/sched_clock.h>

#include "core.h"

#define V2M_PA_CS0	0x40000000
#define V2M_PA_CS1	0x44000000
#define V2M_PA_CS2	0x48000000
#define V2M_PA_CS3	0x4c000000
#define V2M_PA_CS7	0x10000000

static struct map_desc v2m_io_desc[] __initdata = {
	{
		.virtual	= V2M_PERIPH,
		.pfn		= __phys_to_pfn(V2M_PA_CS7),
		.length		= SZ_128K,
		.type		= MT_DEVICE,
	},
};

static void __iomem *v2m_sysreg_base;

static void __init v2m_sysctl_init(void __iomem *base)
{
	u32 scctrl;

	if (WARN_ON(!base))
		return;

	/* Select 1MHz TIMCLK as the reference clock for SP804 timers */
	scctrl = readl(base + SCCTRL);
	scctrl |= SCCTRL_TIMEREN0SEL_TIMCLK;
	scctrl |= SCCTRL_TIMEREN1SEL_TIMCLK;
	writel(scctrl, base + SCCTRL);
}

static void __init v2m_sp804_init(void __iomem *base, unsigned int irq)
{
	if (WARN_ON(!base || irq == NO_IRQ))
		return;

	writel(0, base + TIMER_1_BASE + TIMER_CTRL);
	writel(0, base + TIMER_2_BASE + TIMER_CTRL);

	sp804_clocksource_init(base + TIMER_2_BASE, "v2m-timer1");
	sp804_clockevents_init(base + TIMER_1_BASE, irq, "v2m-timer0");
}

static void __init v2m_timer_init(void)
{
	v2m_sysctl_init(ioremap(V2M_SYSCTL, SZ_4K));
	v2m_sp804_init(ioremap(V2M_TIMER01, SZ_4K), IRQ_V2M_TIMER0);
}

static struct sys_timer v2m_timer = {
	.init	= v2m_timer_init,
};


static DEFINE_SPINLOCK(v2m_cfg_lock);

int v2m_cfg_write(u32 devfn, u32 data)
{
	/* Configuration interface broken? */
	u32 val;

	printk("%s: writing %08x to %08x\n", __func__, data, devfn);

	devfn |= SYS_CFG_START | SYS_CFG_WRITE;

	spin_lock(&v2m_cfg_lock);
	val = readl(v2m_sysreg_base + V2M_SYS_CFGSTAT);
	writel(val & ~SYS_CFG_COMPLETE, v2m_sysreg_base + V2M_SYS_CFGSTAT);

	writel(data, v2m_sysreg_base +  V2M_SYS_CFGDATA);
	writel(devfn, v2m_sysreg_base + V2M_SYS_CFGCTRL);

	do {
		val = readl(v2m_sysreg_base + V2M_SYS_CFGSTAT);
	} while (val == 0);
	spin_unlock(&v2m_cfg_lock);

	return !!(val & SYS_CFG_ERR);
}

int v2m_cfg_read(u32 devfn, u32 *data)
{
	u32 val;

	devfn |= SYS_CFG_START;

	spin_lock(&v2m_cfg_lock);
	writel(0, v2m_sysreg_base + V2M_SYS_CFGSTAT);
	writel(devfn, v2m_sysreg_base + V2M_SYS_CFGCTRL);

	mb();

	do {
		cpu_relax();
		val = readl(v2m_sysreg_base + V2M_SYS_CFGSTAT);
	} while (val == 0);

	*data = readl(v2m_sysreg_base + V2M_SYS_CFGDATA);
	spin_unlock(&v2m_cfg_lock);

	return !!(val & SYS_CFG_ERR);
}

void __init v2m_flags_set(u32 data)
{
	writel(~0, v2m_sysreg_base + V2M_SYS_FLAGSCLR);
	writel(data, v2m_sysreg_base + V2M_SYS_FLAGSSET);
}

int v2m_get_master_site(void)
{
	u32 misc = readl(v2m_sysreg_base + V2M_SYS_MISC);

	return misc & SYS_MISC_MASTERSITE ? SYS_CFG_SITE_DB2 : SYS_CFG_SITE_DB1;
}


static struct resource v2m_pcie_i2c_resource = {
	.start	= V2M_SERIAL_BUS_PCI,
	.end	= V2M_SERIAL_BUS_PCI + SZ_4K - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device v2m_pcie_i2c_device = {
	.name		= "versatile-i2c",
	.id		= 0,
	.num_resources	= 1,
	.resource	= &v2m_pcie_i2c_resource,
};

static struct resource v2m_ddc_i2c_resource = {
	.start	= V2M_SERIAL_BUS_DVI,
	.end	= V2M_SERIAL_BUS_DVI + SZ_4K - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device v2m_ddc_i2c_device = {
	.name		= "versatile-i2c",
	.id		= 1,
	.num_resources	= 1,
	.resource	= &v2m_ddc_i2c_resource,
};

static struct resource v2m_eth_resources[] = {
	{
		.start	= V2M_LAN9118,
		.end	= V2M_LAN9118 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_V2M_LAN9118,
		.end	= IRQ_V2M_LAN9118,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config v2m_eth_config = {
	.flags		= SMSC911X_USE_32BIT,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_HIGH,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device v2m_eth_device = {
	.name		= "smsc911x",
	.id		= -1,
	.resource	= v2m_eth_resources,
	.num_resources	= ARRAY_SIZE(v2m_eth_resources),
	.dev.platform_data = &v2m_eth_config,
};

static struct resource v2m_usb_resources[] = {
	{
		.start	= V2M_ISP1761,
		.end	= V2M_ISP1761 + SZ_128K - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_V2M_ISP1761,
		.end	= IRQ_V2M_ISP1761,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct isp1760_platform_data v2m_usb_config = {
	.is_isp1761		= true,
	.bus_width_16		= false,
	.port1_otg		= true,
	.analog_oc		= false,
	.dack_polarity_high	= false,
	.dreq_polarity_high	= false,
};

static struct platform_device v2m_usb_device = {
	.name		= "isp1760",
	.id		= -1,
	.resource	= v2m_usb_resources,
	.num_resources	= ARRAY_SIZE(v2m_usb_resources),
	.dev.platform_data = &v2m_usb_config,
};

static void v2m_flash_set_vpp(struct platform_device *pdev, int on)
{
	writel(on != 0, v2m_sysreg_base + V2M_SYS_FLASH);
}

static struct physmap_flash_data v2m_flash_data = {
	.width		= 4,
	.set_vpp	= v2m_flash_set_vpp,
};

static struct resource v2m_flash_resources[] = {
	{
		.start	= V2M_NOR0,
		.end	= V2M_NOR0 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= V2M_NOR1,
		.end	= V2M_NOR1 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device v2m_flash_device = {
	.name		= "physmap-flash",
	.id		= -1,
	.resource	= v2m_flash_resources,
	.num_resources	= ARRAY_SIZE(v2m_flash_resources),
	.dev.platform_data = &v2m_flash_data,
};

static struct pata_platform_info v2m_pata_data = {
	.ioport_shift	= 2,
};

static struct resource v2m_pata_resources[] = {
	{
		.start	= V2M_CF,
		.end	= V2M_CF + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= V2M_CF + 0x100,
		.end	= V2M_CF + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device v2m_cf_device = {
	.name		= "pata_platform",
	.id		= -1,
	.resource	= v2m_pata_resources,
	.num_resources	= ARRAY_SIZE(v2m_pata_resources),
	.dev.platform_data = &v2m_pata_data,
};

static unsigned int v2m_mmci_status(struct device *dev)
{
	return readl(v2m_sysreg_base + V2M_SYS_MCI) & (1 << 0);
}

static struct mmci_platform_data v2m_mmci_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= v2m_mmci_status,
};

static AMBA_APB_DEVICE(aaci,  "mb:aaci",  0, V2M_AACI, IRQ_V2M_AACI, NULL);
static AMBA_APB_DEVICE(mmci,  "mb:mmci",  0, V2M_MMCI, IRQ_V2M_MMCI, &v2m_mmci_data);
static AMBA_APB_DEVICE(kmi0,  "mb:kmi0",  0, V2M_KMI0, IRQ_V2M_KMI0, NULL);
static AMBA_APB_DEVICE(kmi1,  "mb:kmi1",  0, V2M_KMI1, IRQ_V2M_KMI1, NULL);
static AMBA_APB_DEVICE(uart0, "mb:uart0", 0, V2M_UART0, IRQ_V2M_UART0, NULL);
static AMBA_APB_DEVICE(uart1, "mb:uart1", 0, V2M_UART1, IRQ_V2M_UART1, NULL);
static AMBA_APB_DEVICE(uart2, "mb:uart2", 0, V2M_UART2, IRQ_V2M_UART2, NULL);
static AMBA_APB_DEVICE(uart3, "mb:uart3", 0, V2M_UART3, IRQ_V2M_UART3, NULL);
static AMBA_APB_DEVICE(wdt,   "mb:wdt",   0, V2M_WDT, IRQ_V2M_WDT, NULL);
static AMBA_APB_DEVICE(rtc,   "mb:rtc",   0, V2M_RTC, IRQ_V2M_RTC, NULL);

static struct amba_device *v2m_amba_devs[] __initdata = {
	&aaci_device,
	&mmci_device,
	&kmi0_device,
	&kmi1_device,
	&uart0_device,
	&uart1_device,
	&uart2_device,
	&uart3_device,
	&wdt_device,
	&rtc_device,
};


static long v2m_osc_round(struct clk *clk, unsigned long rate)
{
	return rate;
}

static int v2m_osc1_set(struct clk *clk, unsigned long rate)
{
	return v2m_cfg_write(SYS_CFG_OSC | SYS_CFG_SITE(SYS_CFG_SITE_MB) | 1,
			rate);
}

static const struct clk_ops osc1_clk_ops = {
	.round	= v2m_osc_round,
	.set	= v2m_osc1_set,
};

static struct clk osc1_clk = {
	.ops	= &osc1_clk_ops,
	.rate	= 24000000,
};

static struct clk osc2_clk = {
	.rate	= 24000000,
};

static struct clk v2m_sp804_clk = {
	.rate	= 1000000,
};

static struct clk v2m_ref_clk = {
	.rate   = 32768,
};

static struct clk dummy_apb_pclk;

static struct clk_lookup v2m_lookups[] = {
	{	/* AMBA bus clock */
		.con_id		= "apb_pclk",
		.clk		= &dummy_apb_pclk,
	}, {	/* UART0 */
		.dev_id		= "mb:uart0",
		.clk		= &osc2_clk,
	}, {	/* UART1 */
		.dev_id		= "mb:uart1",
		.clk		= &osc2_clk,
	}, {	/* UART2 */
		.dev_id		= "mb:uart2",
		.clk		= &osc2_clk,
	}, {	/* UART3 */
		.dev_id		= "mb:uart3",
		.clk		= &osc2_clk,
	}, {	/* KMI0 */
		.dev_id		= "mb:kmi0",
		.clk		= &osc2_clk,
	}, {	/* KMI1 */
		.dev_id		= "mb:kmi1",
		.clk		= &osc2_clk,
	}, {	/* MMC0 */
		.dev_id		= "mb:mmci",
		.clk		= &osc2_clk,
	}, {	/* CLCD */
		.dev_id		= "mb:clcd",
		.clk		= &osc1_clk,
	}, {	/* SP805 WDT */
		.dev_id		= "mb:wdt",
		.clk		= &v2m_ref_clk,
	}, {	/* SP804 timers */
		.dev_id		= "sp804",
		.con_id		= "v2m-timer0",
		.clk		= &v2m_sp804_clk,
	}, {	/* SP804 timers */
		.dev_id		= "sp804",
		.con_id		= "v2m-timer1",
		.clk		= &v2m_sp804_clk,
	},
};

static void __init v2m_init_early(void)
{
	ct_desc->init_early();
	clkdev_add_table(v2m_lookups, ARRAY_SIZE(v2m_lookups));
	versatile_sched_clock_init(v2m_sysreg_base + V2M_SYS_24MHZ, 24000000);
}

static void v2m_power_off(void)
{
	if (v2m_cfg_write(SYS_CFG_SHUTDOWN | SYS_CFG_SITE(SYS_CFG_SITE_MB), 0))
		printk(KERN_EMERG "Unable to shutdown\n");
}

static void v2m_restart(char str, const char *cmd)
{
	if (v2m_cfg_write(SYS_CFG_REBOOT | SYS_CFG_SITE(SYS_CFG_SITE_MB), 0))
		printk(KERN_EMERG "Unable to reboot\n");
}

struct ct_desc *ct_desc;

static struct ct_desc *ct_descs[] __initdata = {
#ifdef CONFIG_ARCH_VEXPRESS_CA9X4
	&ct_ca9x4_desc,
#endif
};

static void __init v2m_populate_ct_desc(void)
{
	int i;
	u32 current_tile_id;

	ct_desc = NULL;
	current_tile_id = readl(v2m_sysreg_base + V2M_SYS_PROCID0)
				& V2M_CT_ID_MASK;

	for (i = 0; i < ARRAY_SIZE(ct_descs) && !ct_desc; ++i)
		if (ct_descs[i]->id == current_tile_id)
			ct_desc = ct_descs[i];

	if (!ct_desc)
		panic("vexpress: this kernel does not support core tile ID 0x%08x when booting via ATAGs.\n"
		      "You may need a device tree blob or a different kernel to boot on this board.\n",
		      current_tile_id);
}

static void __init v2m_map_io(void)
{
	iotable_init(v2m_io_desc, ARRAY_SIZE(v2m_io_desc));
	v2m_sysreg_base = ioremap(V2M_SYSREGS, SZ_4K);
	v2m_populate_ct_desc();
	ct_desc->map_io();
}

static void __init v2m_init_irq(void)
{
	ct_desc->init_irq();
}

static void __init v2m_init(void)
{
	int i;

	platform_device_register(&v2m_pcie_i2c_device);
	platform_device_register(&v2m_ddc_i2c_device);
	platform_device_register(&v2m_flash_device);
	platform_device_register(&v2m_cf_device);
	platform_device_register(&v2m_eth_device);
	platform_device_register(&v2m_usb_device);

	for (i = 0; i < ARRAY_SIZE(v2m_amba_devs); i++)
		amba_device_register(v2m_amba_devs[i], &iomem_resource);

	pm_power_off = v2m_power_off;

	ct_desc->init_tile();
}

MACHINE_START(VEXPRESS, "ARM-Versatile Express")
	.atag_offset	= 0x100,
	.map_io		= v2m_map_io,
	.init_early	= v2m_init_early,
	.init_irq	= v2m_init_irq,
	.timer		= &v2m_timer,
	.handle_irq	= gic_handle_irq,
	.init_machine	= v2m_init,
	.restart	= v2m_restart,
MACHINE_END

#if defined(CONFIG_ARCH_VEXPRESS_DT)

static struct map_desc v2m_rs1_io_desc __initdata = {
	.virtual	= V2M_PERIPH,
	.pfn		= __phys_to_pfn(0x1c000000),
	.length		= SZ_2M,
	.type		= MT_DEVICE,
};

static int __init v2m_dt_scan_memory_map(unsigned long node, const char *uname,
		int depth, void *data)
{
	const char **map = data;

	if (strcmp(uname, "motherboard") != 0)
		return 0;

	*map = of_get_flat_dt_prop(node, "arm,v2m-memory-map", NULL);

	return 1;
}

void __init v2m_dt_map_io(void)
{
	const char *map = NULL;

	of_scan_flat_dt(v2m_dt_scan_memory_map, &map);

	if (map && strcmp(map, "rs1") == 0)
		iotable_init(&v2m_rs1_io_desc, 1);
	else
		iotable_init(v2m_io_desc, ARRAY_SIZE(v2m_io_desc));

#if defined(CONFIG_SMP)
	vexpress_dt_smp_map_io();
#endif
}

static struct clk_lookup v2m_dt_lookups[] = {
	{	/* AMBA bus clock */
		.con_id		= "apb_pclk",
		.clk		= &dummy_apb_pclk,
	}, {	/* SP804 timers */
		.dev_id		= "sp804",
		.con_id		= "v2m-timer0",
		.clk		= &v2m_sp804_clk,
	}, {	/* SP804 timers */
		.dev_id		= "sp804",
		.con_id		= "v2m-timer1",
		.clk		= &v2m_sp804_clk,
	}, {	/* PL180 MMCI */
		.dev_id		= "mb:mmci", /* 10005000.mmci */
		.clk		= &osc2_clk,
	}, {	/* PL050 KMI0 */
		.dev_id		= "10006000.kmi",
		.clk		= &osc2_clk,
	}, {	/* PL050 KMI1 */
		.dev_id		= "10007000.kmi",
		.clk		= &osc2_clk,
	}, {	/* PL011 UART0 */
		.dev_id		= "10009000.uart",
		.clk		= &osc2_clk,
	}, {	/* PL011 UART1 */
		.dev_id		= "1000a000.uart",
		.clk		= &osc2_clk,
	}, {	/* PL011 UART2 */
		.dev_id		= "1000b000.uart",
		.clk		= &osc2_clk,
	}, {	/* PL011 UART3 */
		.dev_id		= "1000c000.uart",
		.clk		= &osc2_clk,
	}, {	/* SP805 WDT */
		.dev_id		= "1000f000.wdt",
		.clk		= &v2m_ref_clk,
	}, {	/* PL111 CLCD */
		.dev_id		= "1001f000.clcd",
		.clk		= &osc1_clk,
	},
	/* RS1 memory map */
	{	/* PL180 MMCI */
		.dev_id		= "mb:mmci", /* 1c050000.mmci */
		.clk		= &osc2_clk,
	}, {	/* PL050 KMI0 */
		.dev_id		= "1c060000.kmi",
		.clk		= &osc2_clk,
	}, {	/* PL050 KMI1 */
		.dev_id		= "1c070000.kmi",
		.clk		= &osc2_clk,
	}, {	/* PL011 UART0 */
		.dev_id		= "1c090000.uart",
		.clk		= &osc2_clk,
	}, {	/* PL011 UART1 */
		.dev_id		= "1c0a0000.uart",
		.clk		= &osc2_clk,
	}, {	/* PL011 UART2 */
		.dev_id		= "1c0b0000.uart",
		.clk		= &osc2_clk,
	}, {	/* PL011 UART3 */
		.dev_id		= "1c0c0000.uart",
		.clk		= &osc2_clk,
	}, {	/* SP805 WDT */
		.dev_id		= "1c0f0000.wdt",
		.clk		= &v2m_ref_clk,
	}, {	/* PL111 CLCD */
		.dev_id		= "1c1f0000.clcd",
		.clk		= &osc1_clk,
	},
};

void __init v2m_dt_init_early(void)
{
	struct device_node *node;
	u32 dt_hbi;

	node = of_find_compatible_node(NULL, NULL, "arm,vexpress-sysreg");
	v2m_sysreg_base = of_iomap(node, 0);
	if (WARN_ON(!v2m_sysreg_base))
		return;

	/* Confirm board type against DT property, if available */
	if (of_property_read_u32(allnodes, "arm,hbi", &dt_hbi) == 0) {
		int site = v2m_get_master_site();
		u32 id = readl(v2m_sysreg_base + (site == SYS_CFG_SITE_DB2 ?
				V2M_SYS_PROCID1 : V2M_SYS_PROCID0));
		u32 hbi = id & SYS_PROCIDx_HBI_MASK;

		if (WARN_ON(dt_hbi != hbi))
			pr_warning("vexpress: DT HBI (%x) is not matching "
					"hardware (%x)!\n", dt_hbi, hbi);
	}

	clkdev_add_table(v2m_dt_lookups, ARRAY_SIZE(v2m_dt_lookups));
}

static  struct of_device_id vexpress_irq_match[] __initdata = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init, },
	{}
};

static void __init v2m_dt_init_irq(void)
{
	of_irq_init(vexpress_irq_match);
}

static void __init v2m_dt_timer_init(void)
{
	struct device_node *node;
	const char *path;
	int err;

	node = of_find_compatible_node(NULL, NULL, "arm,sp810");
	v2m_sysctl_init(of_iomap(node, 0));

	err = of_property_read_string(of_aliases, "arm,v2m_timer", &path);
	if (WARN_ON(err))
		return;
	node = of_find_node_by_path(path);
	v2m_sp804_init(of_iomap(node, 0), irq_of_parse_and_map(node, 0));
	if (arch_timer_of_register() != 0)
		twd_local_timer_of_register();

	if (arch_timer_sched_clock_init() != 0)
		versatile_sched_clock_init(v2m_sysreg_base + V2M_SYS_24MHZ, 24000000);
}

static struct sys_timer v2m_dt_timer = {
	.init = v2m_dt_timer_init,
};

static struct of_dev_auxdata v2m_dt_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,vexpress-flash", V2M_NOR0, "physmap-flash",
			&v2m_flash_data),
	OF_DEV_AUXDATA("arm,primecell", V2M_MMCI, "mb:mmci", &v2m_mmci_data),
	/* RS1 memory map */
	OF_DEV_AUXDATA("arm,vexpress-flash", 0x08000000, "physmap-flash",
			&v2m_flash_data),
	OF_DEV_AUXDATA("arm,primecell", 0x1c050000, "mb:mmci", &v2m_mmci_data),
	{}
};

static void __init v2m_dt_init(void)
{
	l2x0_of_init(0x00400000, 0xfe0fffff);
	of_platform_populate(NULL, of_default_bus_match_table,
			v2m_dt_auxdata_lookup, NULL);
	pm_power_off = v2m_power_off;
}

const static char *v2m_dt_match[] __initconst = {
	"arm,vexpress",
	NULL,
};

DT_MACHINE_START(VEXPRESS_DT, "ARM-Versatile Express")
	.dt_compat	= v2m_dt_match,
	.map_io		= v2m_dt_map_io,
	.init_early	= v2m_dt_init_early,
	.init_irq	= v2m_dt_init_irq,
	.timer		= &v2m_dt_timer,
	.init_machine	= v2m_dt_init,
	.handle_irq	= gic_handle_irq,
	.restart	= v2m_restart,
MACHINE_END

#endif
