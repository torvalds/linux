/*
 * Common routines for the Marvell/Galileo Discovery line of host bridges
 * (gt64260, mv64360, mv64460, ...).
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/mv643xx.h>
#include <linux/platform_device.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/delay.h>
#include <asm/mv64x60.h>


u8 mv64x60_pci_exclude_bridge = 1;
DEFINE_SPINLOCK(mv64x60_lock);

static phys_addr_t 	mv64x60_bridge_pbase;
static void 		__iomem *mv64x60_bridge_vbase;
static u32		mv64x60_bridge_type = MV64x60_TYPE_INVALID;
static u32		mv64x60_bridge_rev;
#if defined(CONFIG_SYSFS) && !defined(CONFIG_GT64260)
static struct pci_controller	sysfs_hose_a;
#endif

static u32 gt64260_translate_size(u32 base, u32 size, u32 num_bits);
static u32 gt64260_untranslate_size(u32 base, u32 size, u32 num_bits);
static void gt64260_set_pci2mem_window(struct pci_controller *hose, u32 bus,
	u32 window, u32 base);
static void gt64260_set_pci2regs_window(struct mv64x60_handle *bh,
	struct pci_controller *hose, u32 bus, u32 base);
static u32 gt64260_is_enabled_32bit(struct mv64x60_handle *bh, u32 window);
static void gt64260_enable_window_32bit(struct mv64x60_handle *bh, u32 window);
static void gt64260_disable_window_32bit(struct mv64x60_handle *bh, u32 window);
static void gt64260_enable_window_64bit(struct mv64x60_handle *bh, u32 window);
static void gt64260_disable_window_64bit(struct mv64x60_handle *bh, u32 window);
static void gt64260_disable_all_windows(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si);
static void gt64260a_chip_specific_init(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si);
static void gt64260b_chip_specific_init(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si);

static u32 mv64360_translate_size(u32 base, u32 size, u32 num_bits);
static u32 mv64360_untranslate_size(u32 base, u32 size, u32 num_bits);
static void mv64360_set_pci2mem_window(struct pci_controller *hose, u32 bus,
	u32 window, u32 base);
static void mv64360_set_pci2regs_window(struct mv64x60_handle *bh,
	struct pci_controller *hose, u32 bus, u32 base);
static u32 mv64360_is_enabled_32bit(struct mv64x60_handle *bh, u32 window);
static void mv64360_enable_window_32bit(struct mv64x60_handle *bh, u32 window);
static void mv64360_disable_window_32bit(struct mv64x60_handle *bh, u32 window);
static void mv64360_enable_window_64bit(struct mv64x60_handle *bh, u32 window);
static void mv64360_disable_window_64bit(struct mv64x60_handle *bh, u32 window);
static void mv64360_disable_all_windows(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si);
static void mv64360_config_io2mem_windows(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si,
	u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2]);
static void mv64360_set_mpsc2regs_window(struct mv64x60_handle *bh, u32 base);
static void mv64360_chip_specific_init(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si);
static void mv64460_chip_specific_init(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si);


/*
 * Define tables that have the chip-specific info for each type of
 * Marvell bridge chip.
 */
static struct mv64x60_chip_info gt64260a_ci __initdata = { /* GT64260A */
	.translate_size		= gt64260_translate_size,
	.untranslate_size	= gt64260_untranslate_size,
	.set_pci2mem_window	= gt64260_set_pci2mem_window,
	.set_pci2regs_window	= gt64260_set_pci2regs_window,
	.is_enabled_32bit	= gt64260_is_enabled_32bit,
	.enable_window_32bit	= gt64260_enable_window_32bit,
	.disable_window_32bit	= gt64260_disable_window_32bit,
	.enable_window_64bit	= gt64260_enable_window_64bit,
	.disable_window_64bit	= gt64260_disable_window_64bit,
	.disable_all_windows	= gt64260_disable_all_windows,
	.chip_specific_init	= gt64260a_chip_specific_init,
	.window_tab_32bit	= gt64260_32bit_windows,
	.window_tab_64bit	= gt64260_64bit_windows,
};

static struct mv64x60_chip_info gt64260b_ci __initdata = { /* GT64260B */
	.translate_size		= gt64260_translate_size,
	.untranslate_size	= gt64260_untranslate_size,
	.set_pci2mem_window	= gt64260_set_pci2mem_window,
	.set_pci2regs_window	= gt64260_set_pci2regs_window,
	.is_enabled_32bit	= gt64260_is_enabled_32bit,
	.enable_window_32bit	= gt64260_enable_window_32bit,
	.disable_window_32bit	= gt64260_disable_window_32bit,
	.enable_window_64bit	= gt64260_enable_window_64bit,
	.disable_window_64bit	= gt64260_disable_window_64bit,
	.disable_all_windows	= gt64260_disable_all_windows,
	.chip_specific_init	= gt64260b_chip_specific_init,
	.window_tab_32bit	= gt64260_32bit_windows,
	.window_tab_64bit	= gt64260_64bit_windows,
};

static struct mv64x60_chip_info mv64360_ci __initdata = { /* MV64360 */
	.translate_size		= mv64360_translate_size,
	.untranslate_size	= mv64360_untranslate_size,
	.set_pci2mem_window	= mv64360_set_pci2mem_window,
	.set_pci2regs_window	= mv64360_set_pci2regs_window,
	.is_enabled_32bit	= mv64360_is_enabled_32bit,
	.enable_window_32bit	= mv64360_enable_window_32bit,
	.disable_window_32bit	= mv64360_disable_window_32bit,
	.enable_window_64bit	= mv64360_enable_window_64bit,
	.disable_window_64bit	= mv64360_disable_window_64bit,
	.disable_all_windows	= mv64360_disable_all_windows,
	.config_io2mem_windows	= mv64360_config_io2mem_windows,
	.set_mpsc2regs_window	= mv64360_set_mpsc2regs_window,
	.chip_specific_init	= mv64360_chip_specific_init,
	.window_tab_32bit	= mv64360_32bit_windows,
	.window_tab_64bit	= mv64360_64bit_windows,
};

static struct mv64x60_chip_info mv64460_ci __initdata = { /* MV64460 */
	.translate_size		= mv64360_translate_size,
	.untranslate_size	= mv64360_untranslate_size,
	.set_pci2mem_window	= mv64360_set_pci2mem_window,
	.set_pci2regs_window	= mv64360_set_pci2regs_window,
	.is_enabled_32bit	= mv64360_is_enabled_32bit,
	.enable_window_32bit	= mv64360_enable_window_32bit,
	.disable_window_32bit	= mv64360_disable_window_32bit,
	.enable_window_64bit	= mv64360_enable_window_64bit,
	.disable_window_64bit	= mv64360_disable_window_64bit,
	.disable_all_windows	= mv64360_disable_all_windows,
	.config_io2mem_windows	= mv64360_config_io2mem_windows,
	.set_mpsc2regs_window	= mv64360_set_mpsc2regs_window,
	.chip_specific_init	= mv64460_chip_specific_init,
	.window_tab_32bit	= mv64360_32bit_windows,
	.window_tab_64bit	= mv64360_64bit_windows,
};

/*
 *****************************************************************************
 *
 *	Platform Device Definitions
 *
 *****************************************************************************
 */
#ifdef CONFIG_SERIAL_MPSC
static struct mpsc_shared_pdata mv64x60_mpsc_shared_pdata = {
	.mrr_val		= 0x3ffffe38,
	.rcrr_val		= 0,
	.tcrr_val		= 0,
	.intr_cause_val		= 0,
	.intr_mask_val		= 0,
};

static struct resource mv64x60_mpsc_shared_resources[] = {
	/* Do not change the order of the IORESOURCE_MEM resources */
	[0] = {
		.name	= "mpsc routing base",
		.start	= MV64x60_MPSC_ROUTING_OFFSET,
		.end	= MV64x60_MPSC_ROUTING_OFFSET +
			MPSC_ROUTING_REG_BLOCK_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "sdma intr base",
		.start	= MV64x60_SDMA_INTR_OFFSET,
		.end	= MV64x60_SDMA_INTR_OFFSET +
			MPSC_SDMA_INTR_REG_BLOCK_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device mpsc_shared_device = { /* Shared device */
	.name		= MPSC_SHARED_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mv64x60_mpsc_shared_resources),
	.resource	= mv64x60_mpsc_shared_resources,
	.dev = {
		.platform_data = &mv64x60_mpsc_shared_pdata,
	},
};

static struct mpsc_pdata mv64x60_mpsc0_pdata = {
	.mirror_regs		= 0,
	.cache_mgmt		= 0,
	.max_idle		= 0,
	.default_baud		= 9600,
	.default_bits		= 8,
	.default_parity		= 'n',
	.default_flow		= 'n',
	.chr_1_val		= 0x00000000,
	.chr_2_val		= 0x00000000,
	.chr_10_val		= 0x00000003,
	.mpcr_val		= 0,
	.bcr_val		= 0,
	.brg_can_tune		= 0,
	.brg_clk_src		= 8,		/* Default to TCLK */
	.brg_clk_freq		= 100000000,	/* Default to 100 MHz */
};

static struct resource mv64x60_mpsc0_resources[] = {
	/* Do not change the order of the IORESOURCE_MEM resources */
	[0] = {
		.name	= "mpsc 0 base",
		.start	= MV64x60_MPSC_0_OFFSET,
		.end	= MV64x60_MPSC_0_OFFSET + MPSC_REG_BLOCK_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "sdma 0 base",
		.start	= MV64x60_SDMA_0_OFFSET,
		.end	= MV64x60_SDMA_0_OFFSET + MPSC_SDMA_REG_BLOCK_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.name	= "brg 0 base",
		.start	= MV64x60_BRG_0_OFFSET,
		.end	= MV64x60_BRG_0_OFFSET + MPSC_BRG_REG_BLOCK_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.name	= "sdma 0 irq",
		.start	= MV64x60_IRQ_SDMA_0,
		.end	= MV64x60_IRQ_SDMA_0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mpsc0_device = {
	.name		= MPSC_CTLR_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mv64x60_mpsc0_resources),
	.resource	= mv64x60_mpsc0_resources,
	.dev = {
		.platform_data = &mv64x60_mpsc0_pdata,
	},
};

static struct mpsc_pdata mv64x60_mpsc1_pdata = {
	.mirror_regs		= 0,
	.cache_mgmt		= 0,
	.max_idle		= 0,
	.default_baud		= 9600,
	.default_bits		= 8,
	.default_parity		= 'n',
	.default_flow		= 'n',
	.chr_1_val		= 0x00000000,
	.chr_1_val		= 0x00000000,
	.chr_2_val		= 0x00000000,
	.chr_10_val		= 0x00000003,
	.mpcr_val		= 0,
	.bcr_val		= 0,
	.brg_can_tune		= 0,
	.brg_clk_src		= 8,		/* Default to TCLK */
	.brg_clk_freq		= 100000000,	/* Default to 100 MHz */
};

static struct resource mv64x60_mpsc1_resources[] = {
	/* Do not change the order of the IORESOURCE_MEM resources */
	[0] = {
		.name	= "mpsc 1 base",
		.start	= MV64x60_MPSC_1_OFFSET,
		.end	= MV64x60_MPSC_1_OFFSET + MPSC_REG_BLOCK_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "sdma 1 base",
		.start	= MV64x60_SDMA_1_OFFSET,
		.end	= MV64x60_SDMA_1_OFFSET + MPSC_SDMA_REG_BLOCK_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.name	= "brg 1 base",
		.start	= MV64x60_BRG_1_OFFSET,
		.end	= MV64x60_BRG_1_OFFSET + MPSC_BRG_REG_BLOCK_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.name	= "sdma 1 irq",
		.start	= MV64360_IRQ_SDMA_1,
		.end	= MV64360_IRQ_SDMA_1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mpsc1_device = {
	.name		= MPSC_CTLR_NAME,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(mv64x60_mpsc1_resources),
	.resource	= mv64x60_mpsc1_resources,
	.dev = {
		.platform_data = &mv64x60_mpsc1_pdata,
	},
};
#endif

#if defined(CONFIG_MV643XX_ETH) || defined(CONFIG_MV643XX_ETH_MODULE)
static struct resource mv64x60_eth_shared_resources[] = {
	[0] = {
		.name	= "ethernet shared base",
		.start	= MV643XX_ETH_SHARED_REGS,
		.end	= MV643XX_ETH_SHARED_REGS +
					MV643XX_ETH_SHARED_REGS_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device mv64x60_eth_shared_device = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mv64x60_eth_shared_resources),
	.resource	= mv64x60_eth_shared_resources,
};

#ifdef CONFIG_MV643XX_ETH_0
static struct resource mv64x60_eth0_resources[] = {
	[0] = {
		.name	= "eth0 irq",
		.start	= MV64x60_IRQ_ETH_0,
		.end	= MV64x60_IRQ_ETH_0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv643xx_eth_platform_data eth0_pd = {
	.port_number	= 0,
};

static struct platform_device eth0_device = {
	.name		= MV643XX_ETH_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mv64x60_eth0_resources),
	.resource	= mv64x60_eth0_resources,
	.dev = {
		.platform_data = &eth0_pd,
	},
};
#endif

#ifdef CONFIG_MV643XX_ETH_1
static struct resource mv64x60_eth1_resources[] = {
	[0] = {
		.name	= "eth1 irq",
		.start	= MV64x60_IRQ_ETH_1,
		.end	= MV64x60_IRQ_ETH_1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv643xx_eth_platform_data eth1_pd = {
	.port_number	= 1,
};

static struct platform_device eth1_device = {
	.name		= MV643XX_ETH_NAME,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(mv64x60_eth1_resources),
	.resource	= mv64x60_eth1_resources,
	.dev = {
		.platform_data = &eth1_pd,
	},
};
#endif

#ifdef CONFIG_MV643XX_ETH_2
static struct resource mv64x60_eth2_resources[] = {
	[0] = {
		.name	= "eth2 irq",
		.start	= MV64x60_IRQ_ETH_2,
		.end	= MV64x60_IRQ_ETH_2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv643xx_eth_platform_data eth2_pd = {
	.port_number	= 2,
};

static struct platform_device eth2_device = {
	.name		= MV643XX_ETH_NAME,
	.id		= 2,
	.num_resources	= ARRAY_SIZE(mv64x60_eth2_resources),
	.resource	= mv64x60_eth2_resources,
	.dev = {
		.platform_data = &eth2_pd,
	},
};
#endif
#endif

#ifdef	CONFIG_I2C_MV64XXX
static struct mv64xxx_i2c_pdata mv64xxx_i2c_pdata = {
	.freq_m			= 8,
	.freq_n			= 3,
	.timeout		= 1000, /* Default timeout of 1 second */
	.retries		= 1,
};

static struct resource mv64xxx_i2c_resources[] = {
	/* Do not change the order of the IORESOURCE_MEM resources */
	[0] = {
		.name	= "mv64xxx i2c base",
		.start	= MV64XXX_I2C_OFFSET,
		.end	= MV64XXX_I2C_OFFSET + MV64XXX_I2C_REG_BLOCK_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "mv64xxx i2c irq",
		.start	= MV64x60_IRQ_I2C,
		.end	= MV64x60_IRQ_I2C,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device i2c_device = {
	.name		= MV64XXX_I2C_CTLR_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mv64xxx_i2c_resources),
	.resource	= mv64xxx_i2c_resources,
	.dev = {
		.platform_data = &mv64xxx_i2c_pdata,
	},
};
#endif

#if defined(CONFIG_SYSFS) && !defined(CONFIG_GT64260)
static struct mv64xxx_pdata mv64xxx_pdata = {
	.hs_reg_valid	= 0,
};

static struct platform_device mv64xxx_device = { /* general mv64x60 stuff */
	.name	= MV64XXX_DEV_NAME,
	.id	= 0,
	.dev = {
		.platform_data = &mv64xxx_pdata,
	},
};
#endif

static struct platform_device *mv64x60_pd_devs[] __initdata = {
#ifdef CONFIG_SERIAL_MPSC
	&mpsc_shared_device,
	&mpsc0_device,
	&mpsc1_device,
#endif
#if defined(CONFIG_MV643XX_ETH) || defined(CONFIG_MV643XX_ETH_MODULE)
	&mv64x60_eth_shared_device,
#endif
#ifdef CONFIG_MV643XX_ETH_0
	&eth0_device,
#endif
#ifdef CONFIG_MV643XX_ETH_1
	&eth1_device,
#endif
#ifdef CONFIG_MV643XX_ETH_2
	&eth2_device,
#endif
#ifdef	CONFIG_I2C_MV64XXX
	&i2c_device,
#endif
#if defined(CONFIG_SYSFS) && !defined(CONFIG_GT64260)
	&mv64xxx_device,
#endif
};

/*
 *****************************************************************************
 *
 *	Bridge Initialization Routines
 *
 *****************************************************************************
 */
/*
 * mv64x60_init()
 *
 * Initialize the bridge based on setting passed in via 'si'.  The bridge
 * handle, 'bh', will be set so that it can be used to make subsequent
 * calls to routines in this file.
 */
int __init
mv64x60_init(struct mv64x60_handle *bh, struct mv64x60_setup_info *si)
{
	u32	mem_windows[MV64x60_CPU2MEM_WINDOWS][2];

	if (ppc_md.progress)
		ppc_md.progress("mv64x60 initialization", 0x0);

	spin_lock_init(&mv64x60_lock);
	mv64x60_early_init(bh, si);

	if (mv64x60_get_type(bh) || mv64x60_setup_for_chip(bh)) {
		iounmap(bh->v_base);
		bh->v_base = 0;
		if (ppc_md.progress)
			ppc_md.progress("mv64x60_init: Can't determine chip",0);
		return -1;
	}

	bh->ci->disable_all_windows(bh, si);
	mv64x60_get_mem_windows(bh, mem_windows);
	mv64x60_config_cpu2mem_windows(bh, si, mem_windows);

	if (bh->ci->config_io2mem_windows)
		bh->ci->config_io2mem_windows(bh, si, mem_windows);
	if (bh->ci->set_mpsc2regs_window)
		bh->ci->set_mpsc2regs_window(bh, si->phys_reg_base);

	if (si->pci_1.enable_bus) {
		bh->io_base_b = (u32)ioremap(si->pci_1.pci_io.cpu_base,
			si->pci_1.pci_io.size);
		isa_io_base = bh->io_base_b;
	}

	if (si->pci_0.enable_bus) {
		bh->io_base_a = (u32)ioremap(si->pci_0.pci_io.cpu_base,
			si->pci_0.pci_io.size);
		isa_io_base = bh->io_base_a;

		mv64x60_alloc_hose(bh, MV64x60_PCI0_CONFIG_ADDR,
			MV64x60_PCI0_CONFIG_DATA, &bh->hose_a);
		mv64x60_config_resources(bh->hose_a, &si->pci_0, bh->io_base_a);
		mv64x60_config_pci_params(bh->hose_a, &si->pci_0);

		mv64x60_config_cpu2pci_windows(bh, &si->pci_0, 0);
		mv64x60_config_pci2mem_windows(bh, bh->hose_a, &si->pci_0, 0,
			mem_windows);
		bh->ci->set_pci2regs_window(bh, bh->hose_a, 0,
			si->phys_reg_base);
	}

	if (si->pci_1.enable_bus) {
		mv64x60_alloc_hose(bh, MV64x60_PCI1_CONFIG_ADDR,
			MV64x60_PCI1_CONFIG_DATA, &bh->hose_b);
		mv64x60_config_resources(bh->hose_b, &si->pci_1, bh->io_base_b);
		mv64x60_config_pci_params(bh->hose_b, &si->pci_1);

		mv64x60_config_cpu2pci_windows(bh, &si->pci_1, 1);
		mv64x60_config_pci2mem_windows(bh, bh->hose_b, &si->pci_1, 1,
			mem_windows);
		bh->ci->set_pci2regs_window(bh, bh->hose_b, 1,
			si->phys_reg_base);
	}

	bh->ci->chip_specific_init(bh, si);
	mv64x60_pd_fixup(bh, mv64x60_pd_devs, ARRAY_SIZE(mv64x60_pd_devs));

	return 0;
}

/*
 * mv64x60_early_init()
 *
 * Do some bridge work that must take place before we start messing with
 * the bridge for real.
 */
void __init
mv64x60_early_init(struct mv64x60_handle *bh, struct mv64x60_setup_info *si)
{
	struct pci_controller	hose_a, hose_b;

	memset(bh, 0, sizeof(*bh));

	bh->p_base = si->phys_reg_base;
	bh->v_base = ioremap(bh->p_base, MV64x60_INTERNAL_SPACE_SIZE);

	mv64x60_bridge_pbase = bh->p_base;
	mv64x60_bridge_vbase = bh->v_base;

	/* Assuming pci mode [reserved] bits 4:5 on 64260 are 0 */
	bh->pci_mode_a = mv64x60_read(bh, MV64x60_PCI0_MODE) &
		MV64x60_PCIMODE_MASK;
	bh->pci_mode_b = mv64x60_read(bh, MV64x60_PCI1_MODE) &
		MV64x60_PCIMODE_MASK;

	/* Need temporary hose structs to call mv64x60_set_bus() */
	memset(&hose_a, 0, sizeof(hose_a));
	memset(&hose_b, 0, sizeof(hose_b));
	setup_indirect_pci_nomap(&hose_a, bh->v_base + MV64x60_PCI0_CONFIG_ADDR,
		bh->v_base + MV64x60_PCI0_CONFIG_DATA);
	setup_indirect_pci_nomap(&hose_b, bh->v_base + MV64x60_PCI1_CONFIG_ADDR,
		bh->v_base + MV64x60_PCI1_CONFIG_DATA);
	bh->hose_a = &hose_a;
	bh->hose_b = &hose_b;

#if defined(CONFIG_SYSFS) && !defined(CONFIG_GT64260)
	/* Save a copy of hose_a for sysfs functions -- hack */
	memcpy(&sysfs_hose_a, &hose_a, sizeof(hose_a));
#endif

	mv64x60_set_bus(bh, 0, 0);
	mv64x60_set_bus(bh, 1, 0);

	bh->hose_a = NULL;
	bh->hose_b = NULL;

	/* Clear bit 0 of PCI addr decode control so PCI->CPU remap 1:1 */
	mv64x60_clr_bits(bh, MV64x60_PCI0_PCI_DECODE_CNTL, 0x00000001);
	mv64x60_clr_bits(bh, MV64x60_PCI1_PCI_DECODE_CNTL, 0x00000001);

	/* Bit 12 MUST be 0; set bit 27--don't auto-update cpu remap regs */
	mv64x60_clr_bits(bh, MV64x60_CPU_CONFIG, (1<<12));
	mv64x60_set_bits(bh, MV64x60_CPU_CONFIG, (1<<27));

	mv64x60_set_bits(bh, MV64x60_PCI0_TO_RETRY, 0xffff);
	mv64x60_set_bits(bh, MV64x60_PCI1_TO_RETRY, 0xffff);
}

/*
 *****************************************************************************
 *
 *	Window Config Routines
 *
 *****************************************************************************
 */
/*
 * mv64x60_get_32bit_window()
 *
 * Determine the base address and size of a 32-bit window on the bridge.
 */
void __init
mv64x60_get_32bit_window(struct mv64x60_handle *bh, u32 window,
	u32 *base, u32 *size)
{
	u32	val, base_reg, size_reg, base_bits, size_bits;
	u32	(*get_from_field)(u32 val, u32 num_bits);

	base_reg = bh->ci->window_tab_32bit[window].base_reg;

	if (base_reg != 0) {
		size_reg  = bh->ci->window_tab_32bit[window].size_reg;
		base_bits = bh->ci->window_tab_32bit[window].base_bits;
		size_bits = bh->ci->window_tab_32bit[window].size_bits;
		get_from_field= bh->ci->window_tab_32bit[window].get_from_field;

		val = mv64x60_read(bh, base_reg);
		*base = get_from_field(val, base_bits);

		if (size_reg != 0) {
			val = mv64x60_read(bh, size_reg);
			val = get_from_field(val, size_bits);
			*size = bh->ci->untranslate_size(*base, val, size_bits);
		} else
			*size = 0;
	} else {
		*base = 0;
		*size = 0;
	}

	pr_debug("get 32bit window: %d, base: 0x%x, size: 0x%x\n",
		window, *base, *size);
}

/*
 * mv64x60_set_32bit_window()
 *
 * Set the base address and size of a 32-bit window on the bridge.
 */
void __init
mv64x60_set_32bit_window(struct mv64x60_handle *bh, u32 window,
	u32 base, u32 size, u32 other_bits)
{
	u32	val, base_reg, size_reg, base_bits, size_bits;
	u32	(*map_to_field)(u32 val, u32 num_bits);

	pr_debug("set 32bit window: %d, base: 0x%x, size: 0x%x, other: 0x%x\n",
		window, base, size, other_bits);

	base_reg = bh->ci->window_tab_32bit[window].base_reg;

	if (base_reg != 0) {
		size_reg  = bh->ci->window_tab_32bit[window].size_reg;
		base_bits = bh->ci->window_tab_32bit[window].base_bits;
		size_bits = bh->ci->window_tab_32bit[window].size_bits;
		map_to_field = bh->ci->window_tab_32bit[window].map_to_field;

		val = map_to_field(base, base_bits) | other_bits;
		mv64x60_write(bh, base_reg, val);

		if (size_reg != 0) {
			val = bh->ci->translate_size(base, size, size_bits);
			val = map_to_field(val, size_bits);
			mv64x60_write(bh, size_reg, val);
		}

		(void)mv64x60_read(bh, base_reg); /* Flush FIFO */
	}
}

/*
 * mv64x60_get_64bit_window()
 *
 * Determine the base address and size of a 64-bit window on the bridge.
 */
void __init
mv64x60_get_64bit_window(struct mv64x60_handle *bh, u32 window,
	u32 *base_hi, u32 *base_lo, u32 *size)
{
	u32	val, base_lo_reg, size_reg, base_lo_bits, size_bits;
	u32	(*get_from_field)(u32 val, u32 num_bits);

	base_lo_reg = bh->ci->window_tab_64bit[window].base_lo_reg;

	if (base_lo_reg != 0) {
		size_reg = bh->ci->window_tab_64bit[window].size_reg;
		base_lo_bits = bh->ci->window_tab_64bit[window].base_lo_bits;
		size_bits = bh->ci->window_tab_64bit[window].size_bits;
		get_from_field= bh->ci->window_tab_64bit[window].get_from_field;

		*base_hi = mv64x60_read(bh,
			bh->ci->window_tab_64bit[window].base_hi_reg);

		val = mv64x60_read(bh, base_lo_reg);
		*base_lo = get_from_field(val, base_lo_bits);

		if (size_reg != 0) {
			val = mv64x60_read(bh, size_reg);
			val = get_from_field(val, size_bits);
			*size = bh->ci->untranslate_size(*base_lo, val,
								size_bits);
		} else
			*size = 0;
	} else {
		*base_hi = 0;
		*base_lo = 0;
		*size = 0;
	}

	pr_debug("get 64bit window: %d, base hi: 0x%x, base lo: 0x%x, "
		"size: 0x%x\n", window, *base_hi, *base_lo, *size);
}

/*
 * mv64x60_set_64bit_window()
 *
 * Set the base address and size of a 64-bit window on the bridge.
 */
void __init
mv64x60_set_64bit_window(struct mv64x60_handle *bh, u32 window,
	u32 base_hi, u32 base_lo, u32 size, u32 other_bits)
{
	u32	val, base_lo_reg, size_reg, base_lo_bits, size_bits;
	u32	(*map_to_field)(u32 val, u32 num_bits);

	pr_debug("set 64bit window: %d, base hi: 0x%x, base lo: 0x%x, "
		"size: 0x%x, other: 0x%x\n",
		window, base_hi, base_lo, size, other_bits);

	base_lo_reg = bh->ci->window_tab_64bit[window].base_lo_reg;

	if (base_lo_reg != 0) {
		size_reg = bh->ci->window_tab_64bit[window].size_reg;
		base_lo_bits = bh->ci->window_tab_64bit[window].base_lo_bits;
		size_bits = bh->ci->window_tab_64bit[window].size_bits;
		map_to_field = bh->ci->window_tab_64bit[window].map_to_field;

		mv64x60_write(bh, bh->ci->window_tab_64bit[window].base_hi_reg,
			base_hi);

		val = map_to_field(base_lo, base_lo_bits) | other_bits;
		mv64x60_write(bh, base_lo_reg, val);

		if (size_reg != 0) {
			val = bh->ci->translate_size(base_lo, size, size_bits);
			val = map_to_field(val, size_bits);
			mv64x60_write(bh, size_reg, val);
		}

		(void)mv64x60_read(bh, base_lo_reg); /* Flush FIFO */
	}
}

/*
 * mv64x60_mask()
 *
 * Take the high-order 'num_bits' of 'val' & mask off low bits.
 */
u32 __init
mv64x60_mask(u32 val, u32 num_bits)
{
	return val & (0xffffffff << (32 - num_bits));
}

/*
 * mv64x60_shift_left()
 *
 * Take the low-order 'num_bits' of 'val', shift left to align at bit 31 (MSB).
 */
u32 __init
mv64x60_shift_left(u32 val, u32 num_bits)
{
	return val << (32 - num_bits);
}

/*
 * mv64x60_shift_right()
 *
 * Take the high-order 'num_bits' of 'val', shift right to align at bit 0 (LSB).
 */
u32 __init
mv64x60_shift_right(u32 val, u32 num_bits)
{
	return val >> (32 - num_bits);
}

/*
 *****************************************************************************
 *
 *	Chip Identification Routines
 *
 *****************************************************************************
 */
/*
 * mv64x60_get_type()
 *
 * Determine the type of bridge chip we have.
 */
int __init
mv64x60_get_type(struct mv64x60_handle *bh)
{
	struct pci_controller hose;
	u16	val;
	u8	save_exclude;

	memset(&hose, 0, sizeof(hose));
	setup_indirect_pci_nomap(&hose, bh->v_base + MV64x60_PCI0_CONFIG_ADDR,
		bh->v_base + MV64x60_PCI0_CONFIG_DATA);

	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;
	/* Sanity check of bridge's Vendor ID */
	early_read_config_word(&hose, 0, PCI_DEVFN(0, 0), PCI_VENDOR_ID, &val);

	if (val != PCI_VENDOR_ID_MARVELL) {
		mv64x60_pci_exclude_bridge = save_exclude;
		return -1;
	}

	/* Get the revision of the chip */
	early_read_config_word(&hose, 0, PCI_DEVFN(0, 0), PCI_CLASS_REVISION,
		&val);
	bh->rev = (u32)(val & 0xff);

	/* Figure out the type of Marvell bridge it is */
	early_read_config_word(&hose, 0, PCI_DEVFN(0, 0), PCI_DEVICE_ID, &val);
	mv64x60_pci_exclude_bridge = save_exclude;

	switch (val) {
	case PCI_DEVICE_ID_MARVELL_GT64260:
		switch (bh->rev) {
		case GT64260_REV_A:
			bh->type = MV64x60_TYPE_GT64260A;
			break;

		default:
			printk(KERN_WARNING "Unsupported GT64260 rev %04x\n",
				bh->rev);
			/* Assume its similar to a 'B' rev and fallthru */
		case GT64260_REV_B:
			bh->type = MV64x60_TYPE_GT64260B;
			break;
		}
		break;

	case PCI_DEVICE_ID_MARVELL_MV64360:
		/* Marvell won't tell me how to distinguish a 64361 & 64362 */
		bh->type = MV64x60_TYPE_MV64360;
		break;

	case PCI_DEVICE_ID_MARVELL_MV64460:
		bh->type = MV64x60_TYPE_MV64460;
		break;

	default:
		printk(KERN_ERR "Unknown Marvell bridge type %04x\n", val);
		return -1;
	}

	/* Hang onto bridge type & rev for PIC code */
	mv64x60_bridge_type = bh->type;
	mv64x60_bridge_rev = bh->rev;

	return 0;
}

/*
 * mv64x60_setup_for_chip()
 *
 * Set 'bh' to use the proper set of routine for the bridge chip that we have.
 */
int __init
mv64x60_setup_for_chip(struct mv64x60_handle *bh)
{
	int	rc = 0;

	/* Set up chip-specific info based on the chip/bridge type */
	switch(bh->type) {
	case MV64x60_TYPE_GT64260A:
		bh->ci = &gt64260a_ci;
		break;

	case MV64x60_TYPE_GT64260B:
		bh->ci = &gt64260b_ci;
		break;

	case MV64x60_TYPE_MV64360:
		bh->ci = &mv64360_ci;
		break;

	case MV64x60_TYPE_MV64460:
		bh->ci = &mv64460_ci;
		break;

	case MV64x60_TYPE_INVALID:
	default:
		if (ppc_md.progress)
			ppc_md.progress("mv64x60: Unsupported bridge", 0x0);
		printk(KERN_ERR "mv64x60: Unsupported bridge\n");
		rc = -1;
	}

	return rc;
}

/*
 * mv64x60_get_bridge_vbase()
 *
 * Return the virtual address of the bridge's registers.
 */
void __iomem *
mv64x60_get_bridge_vbase(void)
{
	return mv64x60_bridge_vbase;
}

/*
 * mv64x60_get_bridge_type()
 *
 * Return the type of bridge on the platform.
 */
u32
mv64x60_get_bridge_type(void)
{
	return mv64x60_bridge_type;
}

/*
 * mv64x60_get_bridge_rev()
 *
 * Return the revision of the bridge on the platform.
 */
u32
mv64x60_get_bridge_rev(void)
{
	return mv64x60_bridge_rev;
}

/*
 *****************************************************************************
 *
 *	System Memory Window Related Routines
 *
 *****************************************************************************
 */
/*
 * mv64x60_get_mem_size()
 *
 * Calculate the amount of memory that the memory controller is set up for.
 * This should only be used by board-specific code if there is no other
 * way to determine the amount of memory in the system.
 */
u32 __init
mv64x60_get_mem_size(u32 bridge_base, u32 chip_type)
{
	struct mv64x60_handle	bh;
	u32	mem_windows[MV64x60_CPU2MEM_WINDOWS][2];
	u32	rc = 0;

	memset(&bh, 0, sizeof(bh));

	bh.type = chip_type;
	bh.v_base = (void *)bridge_base;

	if (!mv64x60_setup_for_chip(&bh)) {
		mv64x60_get_mem_windows(&bh, mem_windows);
		rc = mv64x60_calc_mem_size(&bh, mem_windows);
	}

	return rc;
}

/*
 * mv64x60_get_mem_windows()
 *
 * Get the values in the memory controller & return in the 'mem_windows' array.
 */
void __init
mv64x60_get_mem_windows(struct mv64x60_handle *bh,
	u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2])
{
	u32	i, win;

	for (win=MV64x60_CPU2MEM_0_WIN,i=0;win<=MV64x60_CPU2MEM_3_WIN;win++,i++)
		if (bh->ci->is_enabled_32bit(bh, win))
			mv64x60_get_32bit_window(bh, win,
				&mem_windows[i][0], &mem_windows[i][1]);
		else {
			mem_windows[i][0] = 0;
			mem_windows[i][1] = 0;
		}
}

/*
 * mv64x60_calc_mem_size()
 *
 * Using the memory controller register values in 'mem_windows', determine
 * how much memory it is set up for.
 */
u32 __init
mv64x60_calc_mem_size(struct mv64x60_handle *bh,
	u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2])
{
	u32	i, total = 0;

	for (i=0; i<MV64x60_CPU2MEM_WINDOWS; i++)
		total += mem_windows[i][1];

	return total;
}

/*
 *****************************************************************************
 *
 *	CPU->System MEM, PCI Config Routines
 *
 *****************************************************************************
 */
/*
 * mv64x60_config_cpu2mem_windows()
 *
 * Configure CPU->Memory windows on the bridge.
 */
static u32 prot_tab[] __initdata = {
	MV64x60_CPU_PROT_0_WIN, MV64x60_CPU_PROT_1_WIN,
	MV64x60_CPU_PROT_2_WIN, MV64x60_CPU_PROT_3_WIN
};

static u32 cpu_snoop_tab[] __initdata = {
	MV64x60_CPU_SNOOP_0_WIN, MV64x60_CPU_SNOOP_1_WIN,
	MV64x60_CPU_SNOOP_2_WIN, MV64x60_CPU_SNOOP_3_WIN
};

void __init
mv64x60_config_cpu2mem_windows(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si,
	u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2])
{
	u32	i, win;

	/* Set CPU protection & snoop windows */
	for (win=MV64x60_CPU2MEM_0_WIN,i=0;win<=MV64x60_CPU2MEM_3_WIN;win++,i++)
		if (bh->ci->is_enabled_32bit(bh, win)) {
			mv64x60_set_32bit_window(bh, prot_tab[i],
				mem_windows[i][0], mem_windows[i][1],
				si->cpu_prot_options[i]);
			bh->ci->enable_window_32bit(bh, prot_tab[i]);

			if (bh->ci->window_tab_32bit[cpu_snoop_tab[i]].
								base_reg != 0) {
				mv64x60_set_32bit_window(bh, cpu_snoop_tab[i],
					mem_windows[i][0], mem_windows[i][1],
					si->cpu_snoop_options[i]);
				bh->ci->enable_window_32bit(bh,
					cpu_snoop_tab[i]);
			}

		}
}

/*
 * mv64x60_config_cpu2pci_windows()
 *
 * Configure the CPU->PCI windows for one of the PCI buses.
 */
static u32 win_tab[2][4] __initdata = {
	{ MV64x60_CPU2PCI0_IO_WIN, MV64x60_CPU2PCI0_MEM_0_WIN,
	  MV64x60_CPU2PCI0_MEM_1_WIN, MV64x60_CPU2PCI0_MEM_2_WIN },
	{ MV64x60_CPU2PCI1_IO_WIN, MV64x60_CPU2PCI1_MEM_0_WIN,
	  MV64x60_CPU2PCI1_MEM_1_WIN, MV64x60_CPU2PCI1_MEM_2_WIN },
};

static u32 remap_tab[2][4] __initdata = {
	{ MV64x60_CPU2PCI0_IO_REMAP_WIN, MV64x60_CPU2PCI0_MEM_0_REMAP_WIN,
	  MV64x60_CPU2PCI0_MEM_1_REMAP_WIN, MV64x60_CPU2PCI0_MEM_2_REMAP_WIN },
	{ MV64x60_CPU2PCI1_IO_REMAP_WIN, MV64x60_CPU2PCI1_MEM_0_REMAP_WIN,
	  MV64x60_CPU2PCI1_MEM_1_REMAP_WIN, MV64x60_CPU2PCI1_MEM_2_REMAP_WIN }
};

void __init
mv64x60_config_cpu2pci_windows(struct mv64x60_handle *bh,
	struct mv64x60_pci_info *pi, u32 bus)
{
	int	i;

	if (pi->pci_io.size > 0) {
		mv64x60_set_32bit_window(bh, win_tab[bus][0],
			pi->pci_io.cpu_base, pi->pci_io.size, pi->pci_io.swap);
		mv64x60_set_32bit_window(bh, remap_tab[bus][0],
			pi->pci_io.pci_base_lo, 0, 0);
		bh->ci->enable_window_32bit(bh, win_tab[bus][0]);
	} else /* Actually, the window should already be disabled */
		bh->ci->disable_window_32bit(bh, win_tab[bus][0]);

	for (i=0; i<3; i++)
		if (pi->pci_mem[i].size > 0) {
			mv64x60_set_32bit_window(bh, win_tab[bus][i+1],
				pi->pci_mem[i].cpu_base, pi->pci_mem[i].size,
				pi->pci_mem[i].swap);
			mv64x60_set_64bit_window(bh, remap_tab[bus][i+1],
				pi->pci_mem[i].pci_base_hi,
				pi->pci_mem[i].pci_base_lo, 0, 0);
			bh->ci->enable_window_32bit(bh, win_tab[bus][i+1]);
		} else /* Actually, the window should already be disabled */
			bh->ci->disable_window_32bit(bh, win_tab[bus][i+1]);
}

/*
 *****************************************************************************
 *
 *	PCI->System MEM Config Routines
 *
 *****************************************************************************
 */
/*
 * mv64x60_config_pci2mem_windows()
 *
 * Configure the PCI->Memory windows on the bridge.
 */
static u32 pci_acc_tab[2][4] __initdata = {
	{ MV64x60_PCI02MEM_ACC_CNTL_0_WIN, MV64x60_PCI02MEM_ACC_CNTL_1_WIN,
	  MV64x60_PCI02MEM_ACC_CNTL_2_WIN, MV64x60_PCI02MEM_ACC_CNTL_3_WIN },
	{ MV64x60_PCI12MEM_ACC_CNTL_0_WIN, MV64x60_PCI12MEM_ACC_CNTL_1_WIN,
	  MV64x60_PCI12MEM_ACC_CNTL_2_WIN, MV64x60_PCI12MEM_ACC_CNTL_3_WIN }
};

static u32 pci_snoop_tab[2][4] __initdata = {
	{ MV64x60_PCI02MEM_SNOOP_0_WIN, MV64x60_PCI02MEM_SNOOP_1_WIN,
	  MV64x60_PCI02MEM_SNOOP_2_WIN, MV64x60_PCI02MEM_SNOOP_3_WIN },
	{ MV64x60_PCI12MEM_SNOOP_0_WIN, MV64x60_PCI12MEM_SNOOP_1_WIN,
	  MV64x60_PCI12MEM_SNOOP_2_WIN, MV64x60_PCI12MEM_SNOOP_3_WIN }
};

static u32 pci_size_tab[2][4] __initdata = {
	{ MV64x60_PCI0_MEM_0_SIZE, MV64x60_PCI0_MEM_1_SIZE,
	  MV64x60_PCI0_MEM_2_SIZE, MV64x60_PCI0_MEM_3_SIZE },
	{ MV64x60_PCI1_MEM_0_SIZE, MV64x60_PCI1_MEM_1_SIZE,
	  MV64x60_PCI1_MEM_2_SIZE, MV64x60_PCI1_MEM_3_SIZE }
};

void __init
mv64x60_config_pci2mem_windows(struct mv64x60_handle *bh,
	struct pci_controller *hose, struct mv64x60_pci_info *pi,
	u32 bus, u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2])
{
	u32	i, win;

	/*
	 * Set the access control, snoop, BAR size, and window base addresses.
	 * PCI->MEM windows base addresses will match exactly what the
	 * CPU->MEM windows are.
	 */
	for (win=MV64x60_CPU2MEM_0_WIN,i=0;win<=MV64x60_CPU2MEM_3_WIN;win++,i++)
		if (bh->ci->is_enabled_32bit(bh, win)) {
			mv64x60_set_64bit_window(bh,
				pci_acc_tab[bus][i], 0,
				mem_windows[i][0], mem_windows[i][1],
				pi->acc_cntl_options[i]);
			bh->ci->enable_window_64bit(bh, pci_acc_tab[bus][i]);

			if (bh->ci->window_tab_64bit[
				pci_snoop_tab[bus][i]].base_lo_reg != 0) {

				mv64x60_set_64bit_window(bh,
					pci_snoop_tab[bus][i], 0,
					mem_windows[i][0], mem_windows[i][1],
					pi->snoop_options[i]);
				bh->ci->enable_window_64bit(bh,
					pci_snoop_tab[bus][i]);
			}

			bh->ci->set_pci2mem_window(hose, bus, i,
				mem_windows[i][0]);
			mv64x60_write(bh, pci_size_tab[bus][i],
				mv64x60_mask(mem_windows[i][1] - 1, 20));

			/* Enable the window */
			mv64x60_clr_bits(bh, ((bus == 0) ?
				MV64x60_PCI0_BAR_ENABLE :
				MV64x60_PCI1_BAR_ENABLE), (1 << i));
		}
}

/*
 *****************************************************************************
 *
 *	Hose & Resource Alloc/Init Routines
 *
 *****************************************************************************
 */
/*
 * mv64x60_alloc_hoses()
 *
 * Allocate the PCI hose structures for the bridge's PCI buses.
 */
void __init
mv64x60_alloc_hose(struct mv64x60_handle *bh, u32 cfg_addr, u32 cfg_data,
	struct pci_controller **hose)
{
	*hose = pcibios_alloc_controller();
	setup_indirect_pci_nomap(*hose, bh->v_base + cfg_addr,
		bh->v_base + cfg_data);
}

/*
 * mv64x60_config_resources()
 *
 * Calculate the offsets, etc. for the hose structures to reflect all of
 * the address remapping that happens as you go from CPU->PCI and PCI->MEM.
 */
void __init
mv64x60_config_resources(struct pci_controller *hose,
	struct mv64x60_pci_info *pi, u32 io_base)
{
	int		i;
	/* 2 hoses; 4 resources/hose; string <= 64 bytes */
	static char	s[2][4][64];

	if (pi->pci_io.size != 0) {
		sprintf(s[hose->index][0], "PCI hose %d I/O Space",
			hose->index);
		pci_init_resource(&hose->io_resource, io_base - isa_io_base,
			io_base - isa_io_base + pi->pci_io.size - 1,
			IORESOURCE_IO, s[hose->index][0]);
		hose->io_space.start = pi->pci_io.pci_base_lo;
		hose->io_space.end = pi->pci_io.pci_base_lo + pi->pci_io.size-1;
		hose->io_base_phys = pi->pci_io.cpu_base;
		hose->io_base_virt = (void *)isa_io_base;
	}

	for (i=0; i<3; i++)
		if (pi->pci_mem[i].size != 0) {
			sprintf(s[hose->index][i+1], "PCI hose %d MEM Space %d",
				hose->index, i);
			pci_init_resource(&hose->mem_resources[i],
				pi->pci_mem[i].cpu_base,
				pi->pci_mem[i].cpu_base + pi->pci_mem[i].size-1,
				IORESOURCE_MEM, s[hose->index][i+1]);
		}

	hose->mem_space.end = pi->pci_mem[0].pci_base_lo +
						pi->pci_mem[0].size - 1;
	hose->pci_mem_offset = pi->pci_mem[0].cpu_base -
						pi->pci_mem[0].pci_base_lo;
}

/*
 * mv64x60_config_pci_params()
 *
 * Configure a hose's PCI config space parameters.
 */
void __init
mv64x60_config_pci_params(struct pci_controller *hose,
	struct mv64x60_pci_info *pi)
{
	u32	devfn;
	u16	u16_val;
	u8	save_exclude;

	devfn = PCI_DEVFN(0,0);

	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;

	/* Set class code to indicate host bridge */
	u16_val = PCI_CLASS_BRIDGE_HOST; /* 0x0600 (host bridge) */
	early_write_config_word(hose, 0, devfn, PCI_CLASS_DEVICE, u16_val);

	/* Enable bridge to be PCI master & respond to PCI MEM cycles */
	early_read_config_word(hose, 0, devfn, PCI_COMMAND, &u16_val);
	u16_val &= ~(PCI_COMMAND_IO | PCI_COMMAND_INVALIDATE |
		PCI_COMMAND_PARITY | PCI_COMMAND_SERR | PCI_COMMAND_FAST_BACK);
	u16_val |= pi->pci_cmd_bits | PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
	early_write_config_word(hose, 0, devfn, PCI_COMMAND, u16_val);

	/* Set latency timer, cache line size, clear BIST */
	u16_val = (pi->latency_timer << 8) | (L1_CACHE_BYTES >> 2);
	early_write_config_word(hose, 0, devfn, PCI_CACHE_LINE_SIZE, u16_val);

	mv64x60_pci_exclude_bridge = save_exclude;
}

/*
 *****************************************************************************
 *
 *	PCI Related Routine
 *
 *****************************************************************************
 */
/*
 * mv64x60_set_bus()
 *
 * Set the bus number for the hose directly under the bridge.
 */
void __init
mv64x60_set_bus(struct mv64x60_handle *bh, u32 bus, u32 child_bus)
{
	struct pci_controller	*hose;
	u32	pci_mode, p2p_cfg, pci_cfg_offset, val;
	u8	save_exclude;

	if (bus == 0) {
		pci_mode = bh->pci_mode_a;
		p2p_cfg = MV64x60_PCI0_P2P_CONFIG;
		pci_cfg_offset = 0x64;
		hose = bh->hose_a;
	} else {
		pci_mode = bh->pci_mode_b;
		p2p_cfg = MV64x60_PCI1_P2P_CONFIG;
		pci_cfg_offset = 0xe4;
		hose = bh->hose_b;
	}

	child_bus &= 0xff;
	val = mv64x60_read(bh, p2p_cfg);

	if (pci_mode == MV64x60_PCIMODE_CONVENTIONAL) {
		val &= 0xe0000000; /* Force dev num to 0, turn off P2P bridge */
		val |= (child_bus << 16) | 0xff;
		mv64x60_write(bh, p2p_cfg, val);
		(void)mv64x60_read(bh, p2p_cfg); /* Flush FIFO */
	} else { /* PCI-X */
		/*
		 * Need to use the current bus/dev number (that's in the
		 * P2P CONFIG reg) to access the bridge's pci config space.
		 */
		save_exclude = mv64x60_pci_exclude_bridge;
		mv64x60_pci_exclude_bridge = 0;
		early_write_config_dword(hose, (val & 0x00ff0000) >> 16,
			PCI_DEVFN(((val & 0x1f000000) >> 24), 0),
			pci_cfg_offset, child_bus << 8);
		mv64x60_pci_exclude_bridge = save_exclude;
	}
}

/*
 * mv64x60_pci_exclude_device()
 *
 * This routine is used to make the bridge not appear when the
 * PCI subsystem is accessing PCI devices (in PCI config space).
 */
int
mv64x60_pci_exclude_device(u8 bus, u8 devfn)
{
	struct pci_controller	*hose;

	hose = pci_bus_to_hose(bus);

	/* Skip slot 0 on both hoses */
	if ((mv64x60_pci_exclude_bridge == 1) && (PCI_SLOT(devfn) == 0) &&
		(hose->first_busno == bus))

		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
} /* mv64x60_pci_exclude_device() */

/*
 *****************************************************************************
 *
 *	Platform Device Routines
 *
 *****************************************************************************
 */

/*
 * mv64x60_pd_fixup()
 *
 * Need to add the base addr of where the bridge's regs are mapped in the
 * physical addr space so drivers can ioremap() them.
 */
void __init
mv64x60_pd_fixup(struct mv64x60_handle *bh, struct platform_device *pd_devs[],
	u32 entries)
{
	struct resource	*r;
	u32		i, j;

	for (i=0; i<entries; i++) {
		j = 0;

		while ((r = platform_get_resource(pd_devs[i],IORESOURCE_MEM,j))
			!= NULL) {

			r->start += bh->p_base;
			r->end += bh->p_base;
			j++;
		}
	}
}

/*
 * mv64x60_add_pds()
 *
 * Add the mv64x60 platform devices to the list of platform devices.
 */
static int __init
mv64x60_add_pds(void)
{
	return platform_add_devices(mv64x60_pd_devs,
		ARRAY_SIZE(mv64x60_pd_devs));
}
arch_initcall(mv64x60_add_pds);

/*
 *****************************************************************************
 *
 *	GT64260-Specific Routines
 *
 *****************************************************************************
 */
/*
 * gt64260_translate_size()
 *
 * On the GT64260, the size register is really the "top" address of the window.
 */
static u32 __init
gt64260_translate_size(u32 base, u32 size, u32 num_bits)
{
	return base + mv64x60_mask(size - 1, num_bits);
}

/*
 * gt64260_untranslate_size()
 *
 * Translate the top address of a window into a window size.
 */
static u32 __init
gt64260_untranslate_size(u32 base, u32 size, u32 num_bits)
{
	if (size >= base)
		size = size - base + (1 << (32 - num_bits));
	else
		size = 0;

	return size;
}

/*
 * gt64260_set_pci2mem_window()
 *
 * The PCI->MEM window registers are actually in PCI config space so need
 * to set them by setting the correct config space BARs.
 */
static u32 gt64260_reg_addrs[2][4] __initdata = {
	{ 0x10, 0x14, 0x18, 0x1c }, { 0x90, 0x94, 0x98, 0x9c }
};

static void __init
gt64260_set_pci2mem_window(struct pci_controller *hose, u32 bus, u32 window,
	u32 base)
{
	u8	save_exclude;

	pr_debug("set pci->mem window: %d, hose: %d, base: 0x%x\n", window,
		hose->index, base);

	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;
	early_write_config_dword(hose, 0, PCI_DEVFN(0, 0),
		gt64260_reg_addrs[bus][window], mv64x60_mask(base, 20) | 0x8);
	mv64x60_pci_exclude_bridge = save_exclude;
}

/*
 * gt64260_set_pci2regs_window()
 *
 * Set where the bridge's registers appear in PCI MEM space.
 */
static u32 gt64260_offset[2] __initdata = {0x20, 0xa0};

static void __init
gt64260_set_pci2regs_window(struct mv64x60_handle *bh,
	struct pci_controller *hose, u32 bus, u32 base)
{
	u8	save_exclude;

	pr_debug("set pci->internal regs hose: %d, base: 0x%x\n", hose->index,
		base);

	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;
	early_write_config_dword(hose, 0, PCI_DEVFN(0,0), gt64260_offset[bus],
		(base << 16));
	mv64x60_pci_exclude_bridge = save_exclude;
}

/*
 * gt64260_is_enabled_32bit()
 *
 * On a GT64260, a window is enabled iff its top address is >= to its base
 * address.
 */
static u32 __init
gt64260_is_enabled_32bit(struct mv64x60_handle *bh, u32 window)
{
	u32	rc = 0;

	if ((gt64260_32bit_windows[window].base_reg != 0) &&
		(gt64260_32bit_windows[window].size_reg != 0) &&
		((mv64x60_read(bh, gt64260_32bit_windows[window].size_reg) &
			((1 << gt64260_32bit_windows[window].size_bits) - 1)) >=
		 (mv64x60_read(bh, gt64260_32bit_windows[window].base_reg) &
			((1 << gt64260_32bit_windows[window].base_bits) - 1))))

		rc = 1;

	return rc;
}

/*
 * gt64260_enable_window_32bit()
 *
 * On the GT64260, a window is enabled iff the top address is >= to the base
 * address of the window.  Since the window has already been configured by
 * the time this routine is called, we have nothing to do here.
 */
static void __init
gt64260_enable_window_32bit(struct mv64x60_handle *bh, u32 window)
{
	pr_debug("enable 32bit window: %d\n", window);
}

/*
 * gt64260_disable_window_32bit()
 *
 * On a GT64260, you disable a window by setting its top address to be less
 * than its base address.
 */
static void __init
gt64260_disable_window_32bit(struct mv64x60_handle *bh, u32 window)
{
	pr_debug("disable 32bit window: %d, base_reg: 0x%x, size_reg: 0x%x\n",
		window, gt64260_32bit_windows[window].base_reg,
		gt64260_32bit_windows[window].size_reg);

	if ((gt64260_32bit_windows[window].base_reg != 0) &&
		(gt64260_32bit_windows[window].size_reg != 0)) {

		/* To disable, make bottom reg higher than top reg */
		mv64x60_write(bh, gt64260_32bit_windows[window].base_reg,0xfff);
		mv64x60_write(bh, gt64260_32bit_windows[window].size_reg, 0);
	}
}

/*
 * gt64260_enable_window_64bit()
 *
 * On the GT64260, a window is enabled iff the top address is >= to the base
 * address of the window.  Since the window has already been configured by
 * the time this routine is called, we have nothing to do here.
 */
static void __init
gt64260_enable_window_64bit(struct mv64x60_handle *bh, u32 window)
{
	pr_debug("enable 64bit window: %d\n", window);
}

/*
 * gt64260_disable_window_64bit()
 *
 * On a GT64260, you disable a window by setting its top address to be less
 * than its base address.
 */
static void __init
gt64260_disable_window_64bit(struct mv64x60_handle *bh, u32 window)
{
	pr_debug("disable 64bit window: %d, base_reg: 0x%x, size_reg: 0x%x\n",
		window, gt64260_64bit_windows[window].base_lo_reg,
		gt64260_64bit_windows[window].size_reg);

	if ((gt64260_64bit_windows[window].base_lo_reg != 0) &&
		(gt64260_64bit_windows[window].size_reg != 0)) {

		/* To disable, make bottom reg higher than top reg */
		mv64x60_write(bh, gt64260_64bit_windows[window].base_lo_reg,
									0xfff);
		mv64x60_write(bh, gt64260_64bit_windows[window].base_hi_reg, 0);
		mv64x60_write(bh, gt64260_64bit_windows[window].size_reg, 0);
	}
}

/*
 * gt64260_disable_all_windows()
 *
 * The GT64260 has several windows that aren't represented in the table of
 * windows at the top of this file.  This routine turns all of them off
 * except for the memory controller windows, of course.
 */
static void __init
gt64260_disable_all_windows(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si)
{
	u32	i, preserve;

	/* Disable 32bit windows (don't disable cpu->mem windows) */
	for (i=MV64x60_CPU2DEV_0_WIN; i<MV64x60_32BIT_WIN_COUNT; i++) {
		if (i < 32)
			preserve = si->window_preserve_mask_32_lo & (1 << i);
		else
			preserve = si->window_preserve_mask_32_hi & (1<<(i-32));

		if (!preserve)
			gt64260_disable_window_32bit(bh, i);
	}

	/* Disable 64bit windows */
	for (i=0; i<MV64x60_64BIT_WIN_COUNT; i++)
		if (!(si->window_preserve_mask_64 & (1<<i)))
			gt64260_disable_window_64bit(bh, i);

	/* Turn off cpu protection windows not in gt64260_32bit_windows[] */
	mv64x60_write(bh, GT64260_CPU_PROT_BASE_4, 0xfff);
	mv64x60_write(bh, GT64260_CPU_PROT_SIZE_4, 0);
	mv64x60_write(bh, GT64260_CPU_PROT_BASE_5, 0xfff);
	mv64x60_write(bh, GT64260_CPU_PROT_SIZE_5, 0);
	mv64x60_write(bh, GT64260_CPU_PROT_BASE_6, 0xfff);
	mv64x60_write(bh, GT64260_CPU_PROT_SIZE_6, 0);
	mv64x60_write(bh, GT64260_CPU_PROT_BASE_7, 0xfff);
	mv64x60_write(bh, GT64260_CPU_PROT_SIZE_7, 0);

	/* Turn off PCI->MEM access cntl wins not in gt64260_64bit_windows[] */
	mv64x60_write(bh, MV64x60_PCI0_ACC_CNTL_4_BASE_LO, 0xfff);
	mv64x60_write(bh, MV64x60_PCI0_ACC_CNTL_4_BASE_HI, 0);
	mv64x60_write(bh, MV64x60_PCI0_ACC_CNTL_4_SIZE, 0);
	mv64x60_write(bh, MV64x60_PCI0_ACC_CNTL_5_BASE_LO, 0xfff);
	mv64x60_write(bh, MV64x60_PCI0_ACC_CNTL_5_BASE_HI, 0);
	mv64x60_write(bh, MV64x60_PCI0_ACC_CNTL_5_SIZE, 0);
	mv64x60_write(bh, GT64260_PCI0_ACC_CNTL_6_BASE_LO, 0xfff);
	mv64x60_write(bh, GT64260_PCI0_ACC_CNTL_6_BASE_HI, 0);
	mv64x60_write(bh, GT64260_PCI0_ACC_CNTL_6_SIZE, 0);
	mv64x60_write(bh, GT64260_PCI0_ACC_CNTL_7_BASE_LO, 0xfff);
	mv64x60_write(bh, GT64260_PCI0_ACC_CNTL_7_BASE_HI, 0);
	mv64x60_write(bh, GT64260_PCI0_ACC_CNTL_7_SIZE, 0);

	mv64x60_write(bh, MV64x60_PCI1_ACC_CNTL_4_BASE_LO, 0xfff);
	mv64x60_write(bh, MV64x60_PCI1_ACC_CNTL_4_BASE_HI, 0);
	mv64x60_write(bh, MV64x60_PCI1_ACC_CNTL_4_SIZE, 0);
	mv64x60_write(bh, MV64x60_PCI1_ACC_CNTL_5_BASE_LO, 0xfff);
	mv64x60_write(bh, MV64x60_PCI1_ACC_CNTL_5_BASE_HI, 0);
	mv64x60_write(bh, MV64x60_PCI1_ACC_CNTL_5_SIZE, 0);
	mv64x60_write(bh, GT64260_PCI1_ACC_CNTL_6_BASE_LO, 0xfff);
	mv64x60_write(bh, GT64260_PCI1_ACC_CNTL_6_BASE_HI, 0);
	mv64x60_write(bh, GT64260_PCI1_ACC_CNTL_6_SIZE, 0);
	mv64x60_write(bh, GT64260_PCI1_ACC_CNTL_7_BASE_LO, 0xfff);
	mv64x60_write(bh, GT64260_PCI1_ACC_CNTL_7_BASE_HI, 0);
	mv64x60_write(bh, GT64260_PCI1_ACC_CNTL_7_SIZE, 0);

	/* Disable all PCI-><whatever> windows */
	mv64x60_set_bits(bh, MV64x60_PCI0_BAR_ENABLE, 0x07fffdff);
	mv64x60_set_bits(bh, MV64x60_PCI1_BAR_ENABLE, 0x07fffdff);

	/*
	 * Some firmwares enable a bunch of intr sources
	 * for the PCI INT output pins.
	 */
	mv64x60_write(bh, GT64260_IC_CPU_INTR_MASK_LO, 0);
	mv64x60_write(bh, GT64260_IC_CPU_INTR_MASK_HI, 0);
	mv64x60_write(bh, GT64260_IC_PCI0_INTR_MASK_LO, 0);
	mv64x60_write(bh, GT64260_IC_PCI0_INTR_MASK_HI, 0);
	mv64x60_write(bh, GT64260_IC_PCI1_INTR_MASK_LO, 0);
	mv64x60_write(bh, GT64260_IC_PCI1_INTR_MASK_HI, 0);
	mv64x60_write(bh, GT64260_IC_CPU_INT_0_MASK, 0);
	mv64x60_write(bh, GT64260_IC_CPU_INT_1_MASK, 0);
	mv64x60_write(bh, GT64260_IC_CPU_INT_2_MASK, 0);
	mv64x60_write(bh, GT64260_IC_CPU_INT_3_MASK, 0);
}

/*
 * gt64260a_chip_specific_init()
 *
 * Implement errata workarounds for the GT64260A.
 */
static void __init
gt64260a_chip_specific_init(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si)
{
#ifdef CONFIG_SERIAL_MPSC
	struct resource	*r;
#endif
#if !defined(CONFIG_NOT_COHERENT_CACHE)
	u32	val;
	u8	save_exclude;
#endif

	if (si->pci_0.enable_bus)
		mv64x60_set_bits(bh, MV64x60_PCI0_CMD,
			((1<<4) | (1<<5) | (1<<9) | (1<<13)));

	if (si->pci_1.enable_bus)
		mv64x60_set_bits(bh, MV64x60_PCI1_CMD,
			((1<<4) | (1<<5) | (1<<9) | (1<<13)));

	/*
	 * Dave Wilhardt found that bit 4 in the PCI Command registers must
	 * be set if you are using cache coherency.
	 */
#if !defined(CONFIG_NOT_COHERENT_CACHE)
	/* Res #MEM-4 -- cpu read buffer to buffer 1 */
	if ((mv64x60_read(bh, MV64x60_CPU_MODE) & 0xf0) == 0x40)
		mv64x60_set_bits(bh, GT64260_SDRAM_CONFIG, (1<<26));

	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;
	if (si->pci_0.enable_bus) {
		early_read_config_dword(bh->hose_a, 0, PCI_DEVFN(0,0),
			PCI_COMMAND, &val);
		val |= PCI_COMMAND_INVALIDATE;
		early_write_config_dword(bh->hose_a, 0, PCI_DEVFN(0,0),
			PCI_COMMAND, val);
	}

	if (si->pci_1.enable_bus) {
		early_read_config_dword(bh->hose_b, 0, PCI_DEVFN(0,0),
			PCI_COMMAND, &val);
		val |= PCI_COMMAND_INVALIDATE;
		early_write_config_dword(bh->hose_b, 0, PCI_DEVFN(0,0),
			PCI_COMMAND, val);
	}
	mv64x60_pci_exclude_bridge = save_exclude;
#endif

	/* Disable buffer/descriptor snooping */
	mv64x60_clr_bits(bh, 0xf280, (1<< 6) | (1<<14) | (1<<22) | (1<<30));
	mv64x60_clr_bits(bh, 0xf2c0, (1<< 6) | (1<<14) | (1<<22) | (1<<30));

#ifdef CONFIG_SERIAL_MPSC
	mv64x60_mpsc0_pdata.mirror_regs = 1;
	mv64x60_mpsc0_pdata.cache_mgmt = 1;
	mv64x60_mpsc1_pdata.mirror_regs = 1;
	mv64x60_mpsc1_pdata.cache_mgmt = 1;

	if ((r = platform_get_resource(&mpsc1_device, IORESOURCE_IRQ, 0))
			!= NULL) {
		r->start = MV64x60_IRQ_SDMA_0;
		r->end = MV64x60_IRQ_SDMA_0;
	}
#endif
}

/*
 * gt64260b_chip_specific_init()
 *
 * Implement errata workarounds for the GT64260B.
 */
static void __init
gt64260b_chip_specific_init(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si)
{
#ifdef CONFIG_SERIAL_MPSC
	struct resource	*r;
#endif
#if !defined(CONFIG_NOT_COHERENT_CACHE)
	u32	val;
	u8	save_exclude;
#endif

	if (si->pci_0.enable_bus)
		mv64x60_set_bits(bh, MV64x60_PCI0_CMD,
			((1<<4) | (1<<5) | (1<<9) | (1<<13)));

	if (si->pci_1.enable_bus)
		mv64x60_set_bits(bh, MV64x60_PCI1_CMD,
			((1<<4) | (1<<5) | (1<<9) | (1<<13)));

	/*
	 * Dave Wilhardt found that bit 4 in the PCI Command registers must
	 * be set if you are using cache coherency.
	 */
#if !defined(CONFIG_NOT_COHERENT_CACHE)
	mv64x60_set_bits(bh, GT64260_CPU_WB_PRIORITY_BUFFER_DEPTH, 0xf);

	/* Res #MEM-4 -- cpu read buffer to buffer 1 */
	if ((mv64x60_read(bh, MV64x60_CPU_MODE) & 0xf0) == 0x40)
		mv64x60_set_bits(bh, GT64260_SDRAM_CONFIG, (1<<26));

	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;
	if (si->pci_0.enable_bus) {
		early_read_config_dword(bh->hose_a, 0, PCI_DEVFN(0,0),
			PCI_COMMAND, &val);
		val |= PCI_COMMAND_INVALIDATE;
		early_write_config_dword(bh->hose_a, 0, PCI_DEVFN(0,0),
			PCI_COMMAND, val);
	}

	if (si->pci_1.enable_bus) {
		early_read_config_dword(bh->hose_b, 0, PCI_DEVFN(0,0),
			PCI_COMMAND, &val);
		val |= PCI_COMMAND_INVALIDATE;
		early_write_config_dword(bh->hose_b, 0, PCI_DEVFN(0,0),
			PCI_COMMAND, val);
	}
	mv64x60_pci_exclude_bridge = save_exclude;
#endif

	/* Disable buffer/descriptor snooping */
	mv64x60_clr_bits(bh, 0xf280, (1<< 6) | (1<<14) | (1<<22) | (1<<30));
	mv64x60_clr_bits(bh, 0xf2c0, (1<< 6) | (1<<14) | (1<<22) | (1<<30));

#ifdef CONFIG_SERIAL_MPSC
	/*
	 * The 64260B is not supposed to have the bug where the MPSC & ENET
	 * can't access cache coherent regions.  However, testing has shown
	 * that the MPSC, at least, still has this bug.
	 */
	mv64x60_mpsc0_pdata.cache_mgmt = 1;
	mv64x60_mpsc1_pdata.cache_mgmt = 1;

	if ((r = platform_get_resource(&mpsc1_device, IORESOURCE_IRQ, 0))
			!= NULL) {
		r->start = MV64x60_IRQ_SDMA_0;
		r->end = MV64x60_IRQ_SDMA_0;
	}
#endif
}

/*
 *****************************************************************************
 *
 *	MV64360-Specific Routines
 *
 *****************************************************************************
 */
/*
 * mv64360_translate_size()
 *
 * On the MV64360, the size register is set similar to the size you get
 * from a pci config space BAR register.  That is, programmed from LSB to MSB
 * as a sequence of 1's followed by a sequence of 0's. IOW, "size -1" with the
 * assumption that the size is a power of 2.
 */
static u32 __init
mv64360_translate_size(u32 base_addr, u32 size, u32 num_bits)
{
	return mv64x60_mask(size - 1, num_bits);
}

/*
 * mv64360_untranslate_size()
 *
 * Translate the size register value of a window into a window size.
 */
static u32 __init
mv64360_untranslate_size(u32 base_addr, u32 size, u32 num_bits)
{
	if (size > 0) {
		size >>= (32 - num_bits);
		size++;
		size <<= (32 - num_bits);
	}

	return size;
}

/*
 * mv64360_set_pci2mem_window()
 *
 * The PCI->MEM window registers are actually in PCI config space so need
 * to set them by setting the correct config space BARs.
 */
struct {
	u32	fcn;
	u32	base_hi_bar;
	u32	base_lo_bar;
} static mv64360_reg_addrs[2][4] __initdata = {
	{{ 0, 0x14, 0x10 }, { 0, 0x1c, 0x18 },
	 { 1, 0x14, 0x10 }, { 1, 0x1c, 0x18 }},
	{{ 0, 0x94, 0x90 }, { 0, 0x9c, 0x98 },
	 { 1, 0x94, 0x90 }, { 1, 0x9c, 0x98 }}
};

static void __init
mv64360_set_pci2mem_window(struct pci_controller *hose, u32 bus, u32 window,
	u32 base)
{
	u8 save_exclude;

	pr_debug("set pci->mem window: %d, hose: %d, base: 0x%x\n", window,
		hose->index, base);

	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;
	early_write_config_dword(hose, 0,
		PCI_DEVFN(0, mv64360_reg_addrs[bus][window].fcn),
		mv64360_reg_addrs[bus][window].base_hi_bar, 0);
	early_write_config_dword(hose, 0,
		PCI_DEVFN(0, mv64360_reg_addrs[bus][window].fcn),
		mv64360_reg_addrs[bus][window].base_lo_bar,
		mv64x60_mask(base,20) | 0xc);
	mv64x60_pci_exclude_bridge = save_exclude;
}

/*
 * mv64360_set_pci2regs_window()
 *
 * Set where the bridge's registers appear in PCI MEM space.
 */
static u32 mv64360_offset[2][2] __initdata = {{0x20, 0x24}, {0xa0, 0xa4}};

static void __init
mv64360_set_pci2regs_window(struct mv64x60_handle *bh,
	struct pci_controller *hose, u32 bus, u32 base)
{
	u8	save_exclude;

	pr_debug("set pci->internal regs hose: %d, base: 0x%x\n", hose->index,
		base);

	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;
	early_write_config_dword(hose, 0, PCI_DEVFN(0,0),
		mv64360_offset[bus][0], (base << 16));
	early_write_config_dword(hose, 0, PCI_DEVFN(0,0),
		mv64360_offset[bus][1], 0);
	mv64x60_pci_exclude_bridge = save_exclude;
}

/*
 * mv64360_is_enabled_32bit()
 *
 * On a MV64360, a window is enabled by either clearing a bit in the
 * CPU BAR Enable reg or setting a bit in the window's base reg.
 * Note that this doesn't work for windows on the PCI slave side but we don't
 * check those so its okay.
 */
static u32 __init
mv64360_is_enabled_32bit(struct mv64x60_handle *bh, u32 window)
{
	u32	extra, rc = 0;

	if (((mv64360_32bit_windows[window].base_reg != 0) &&
		(mv64360_32bit_windows[window].size_reg != 0)) ||
		(window == MV64x60_CPU2SRAM_WIN)) {

		extra = mv64360_32bit_windows[window].extra;

		switch (extra & MV64x60_EXTRA_MASK) {
		case MV64x60_EXTRA_CPUWIN_ENAB:
			rc = (mv64x60_read(bh, MV64360_CPU_BAR_ENABLE) &
				(1 << (extra & 0x1f))) == 0;
			break;

		case MV64x60_EXTRA_CPUPROT_ENAB:
			rc = (mv64x60_read(bh,
				mv64360_32bit_windows[window].base_reg) &
					(1 << (extra & 0x1f))) != 0;
			break;

		case MV64x60_EXTRA_ENET_ENAB:
			rc = (mv64x60_read(bh, MV64360_ENET2MEM_BAR_ENABLE) &
				(1 << (extra & 0x7))) == 0;
			break;

		case MV64x60_EXTRA_MPSC_ENAB:
			rc = (mv64x60_read(bh, MV64360_MPSC2MEM_BAR_ENABLE) &
				(1 << (extra & 0x3))) == 0;
			break;

		case MV64x60_EXTRA_IDMA_ENAB:
			rc = (mv64x60_read(bh, MV64360_IDMA2MEM_BAR_ENABLE) &
				(1 << (extra & 0x7))) == 0;
			break;

		default:
			printk(KERN_ERR "mv64360_is_enabled: %s\n",
				"32bit table corrupted");
		}
	}

	return rc;
}

/*
 * mv64360_enable_window_32bit()
 *
 * On a MV64360, a window is enabled by either clearing a bit in the
 * CPU BAR Enable reg or setting a bit in the window's base reg.
 */
static void __init
mv64360_enable_window_32bit(struct mv64x60_handle *bh, u32 window)
{
	u32	extra;

	pr_debug("enable 32bit window: %d\n", window);

	if (((mv64360_32bit_windows[window].base_reg != 0) &&
		(mv64360_32bit_windows[window].size_reg != 0)) ||
		(window == MV64x60_CPU2SRAM_WIN)) {

		extra = mv64360_32bit_windows[window].extra;

		switch (extra & MV64x60_EXTRA_MASK) {
		case MV64x60_EXTRA_CPUWIN_ENAB:
			mv64x60_clr_bits(bh, MV64360_CPU_BAR_ENABLE,
				(1 << (extra & 0x1f)));
			break;

		case MV64x60_EXTRA_CPUPROT_ENAB:
			mv64x60_set_bits(bh,
				mv64360_32bit_windows[window].base_reg,
				(1 << (extra & 0x1f)));
			break;

		case MV64x60_EXTRA_ENET_ENAB:
			mv64x60_clr_bits(bh, MV64360_ENET2MEM_BAR_ENABLE,
				(1 << (extra & 0x7)));
			break;

		case MV64x60_EXTRA_MPSC_ENAB:
			mv64x60_clr_bits(bh, MV64360_MPSC2MEM_BAR_ENABLE,
				(1 << (extra & 0x3)));
			break;

		case MV64x60_EXTRA_IDMA_ENAB:
			mv64x60_clr_bits(bh, MV64360_IDMA2MEM_BAR_ENABLE,
				(1 << (extra & 0x7)));
			break;

		default:
			printk(KERN_ERR "mv64360_enable: %s\n",
				"32bit table corrupted");
		}
	}
}

/*
 * mv64360_disable_window_32bit()
 *
 * On a MV64360, a window is disabled by either setting a bit in the
 * CPU BAR Enable reg or clearing a bit in the window's base reg.
 */
static void __init
mv64360_disable_window_32bit(struct mv64x60_handle *bh, u32 window)
{
	u32	extra;

	pr_debug("disable 32bit window: %d, base_reg: 0x%x, size_reg: 0x%x\n",
		window, mv64360_32bit_windows[window].base_reg,
		mv64360_32bit_windows[window].size_reg);

	if (((mv64360_32bit_windows[window].base_reg != 0) &&
		(mv64360_32bit_windows[window].size_reg != 0)) ||
		(window == MV64x60_CPU2SRAM_WIN)) {

		extra = mv64360_32bit_windows[window].extra;

		switch (extra & MV64x60_EXTRA_MASK) {
		case MV64x60_EXTRA_CPUWIN_ENAB:
			mv64x60_set_bits(bh, MV64360_CPU_BAR_ENABLE,
				(1 << (extra & 0x1f)));
			break;

		case MV64x60_EXTRA_CPUPROT_ENAB:
			mv64x60_clr_bits(bh,
				mv64360_32bit_windows[window].base_reg,
				(1 << (extra & 0x1f)));
			break;

		case MV64x60_EXTRA_ENET_ENAB:
			mv64x60_set_bits(bh, MV64360_ENET2MEM_BAR_ENABLE,
				(1 << (extra & 0x7)));
			break;

		case MV64x60_EXTRA_MPSC_ENAB:
			mv64x60_set_bits(bh, MV64360_MPSC2MEM_BAR_ENABLE,
				(1 << (extra & 0x3)));
			break;

		case MV64x60_EXTRA_IDMA_ENAB:
			mv64x60_set_bits(bh, MV64360_IDMA2MEM_BAR_ENABLE,
				(1 << (extra & 0x7)));
			break;

		default:
			printk(KERN_ERR "mv64360_disable: %s\n",
				"32bit table corrupted");
		}
	}
}

/*
 * mv64360_enable_window_64bit()
 *
 * On the MV64360, a 64-bit window is enabled by setting a bit in the window's
 * base reg.
 */
static void __init
mv64360_enable_window_64bit(struct mv64x60_handle *bh, u32 window)
{
	pr_debug("enable 64bit window: %d\n", window);

	if ((mv64360_64bit_windows[window].base_lo_reg!= 0) &&
		(mv64360_64bit_windows[window].size_reg != 0)) {

		if ((mv64360_64bit_windows[window].extra & MV64x60_EXTRA_MASK)
				== MV64x60_EXTRA_PCIACC_ENAB)
			mv64x60_set_bits(bh,
				mv64360_64bit_windows[window].base_lo_reg,
				(1 << (mv64360_64bit_windows[window].extra &
									0x1f)));
		else
			printk(KERN_ERR "mv64360_enable: %s\n",
				"64bit table corrupted");
	}
}

/*
 * mv64360_disable_window_64bit()
 *
 * On a MV64360, a 64-bit window is disabled by clearing a bit in the window's
 * base reg.
 */
static void __init
mv64360_disable_window_64bit(struct mv64x60_handle *bh, u32 window)
{
	pr_debug("disable 64bit window: %d, base_reg: 0x%x, size_reg: 0x%x\n",
		window, mv64360_64bit_windows[window].base_lo_reg,
		mv64360_64bit_windows[window].size_reg);

	if ((mv64360_64bit_windows[window].base_lo_reg != 0) &&
			(mv64360_64bit_windows[window].size_reg != 0)) {
		if ((mv64360_64bit_windows[window].extra & MV64x60_EXTRA_MASK)
				== MV64x60_EXTRA_PCIACC_ENAB)
			mv64x60_clr_bits(bh,
				mv64360_64bit_windows[window].base_lo_reg,
				(1 << (mv64360_64bit_windows[window].extra &
									0x1f)));
		else
			printk(KERN_ERR "mv64360_disable: %s\n",
				"64bit table corrupted");
	}
}

/*
 * mv64360_disable_all_windows()
 *
 * The MV64360 has a few windows that aren't represented in the table of
 * windows at the top of this file.  This routine turns all of them off
 * except for the memory controller windows, of course.
 */
static void __init
mv64360_disable_all_windows(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si)
{
	u32	preserve, i;

	/* Disable 32bit windows (don't disable cpu->mem windows) */
	for (i=MV64x60_CPU2DEV_0_WIN; i<MV64x60_32BIT_WIN_COUNT; i++) {
		if (i < 32)
			preserve = si->window_preserve_mask_32_lo & (1 << i);
		else
			preserve = si->window_preserve_mask_32_hi & (1<<(i-32));

		if (!preserve)
			mv64360_disable_window_32bit(bh, i);
	}

	/* Disable 64bit windows */
	for (i=0; i<MV64x60_64BIT_WIN_COUNT; i++)
		if (!(si->window_preserve_mask_64 & (1<<i)))
			mv64360_disable_window_64bit(bh, i);

	/* Turn off PCI->MEM access cntl wins not in mv64360_64bit_windows[] */
	mv64x60_clr_bits(bh, MV64x60_PCI0_ACC_CNTL_4_BASE_LO, 0);
	mv64x60_clr_bits(bh, MV64x60_PCI0_ACC_CNTL_5_BASE_LO, 0);
	mv64x60_clr_bits(bh, MV64x60_PCI1_ACC_CNTL_4_BASE_LO, 0);
	mv64x60_clr_bits(bh, MV64x60_PCI1_ACC_CNTL_5_BASE_LO, 0);

	/* Disable all PCI-><whatever> windows */
	mv64x60_set_bits(bh, MV64x60_PCI0_BAR_ENABLE, 0x0000f9ff);
	mv64x60_set_bits(bh, MV64x60_PCI1_BAR_ENABLE, 0x0000f9ff);
}

/*
 * mv64360_config_io2mem_windows()
 *
 * ENET, MPSC, and IDMA ctlrs on the MV64[34]60 have separate windows that
 * must be set up so that the respective ctlr can access system memory.
 */
static u32 enet_tab[MV64x60_CPU2MEM_WINDOWS] __initdata = {
	MV64x60_ENET2MEM_0_WIN, MV64x60_ENET2MEM_1_WIN,
	MV64x60_ENET2MEM_2_WIN, MV64x60_ENET2MEM_3_WIN,
};

static u32 mpsc_tab[MV64x60_CPU2MEM_WINDOWS] __initdata = {
	MV64x60_MPSC2MEM_0_WIN, MV64x60_MPSC2MEM_1_WIN,
	MV64x60_MPSC2MEM_2_WIN, MV64x60_MPSC2MEM_3_WIN,
};

static u32 idma_tab[MV64x60_CPU2MEM_WINDOWS] __initdata = {
	MV64x60_IDMA2MEM_0_WIN, MV64x60_IDMA2MEM_1_WIN,
	MV64x60_IDMA2MEM_2_WIN, MV64x60_IDMA2MEM_3_WIN,
};

static u32 dram_selects[MV64x60_CPU2MEM_WINDOWS] __initdata =
	{ 0xe, 0xd, 0xb, 0x7 };

static void __init
mv64360_config_io2mem_windows(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si,
	u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2])
{
	u32	i, win;

	pr_debug("config_io2regs_windows: enet, mpsc, idma -> bridge regs\n");

	mv64x60_write(bh, MV64360_ENET2MEM_ACC_PROT_0, 0);
	mv64x60_write(bh, MV64360_ENET2MEM_ACC_PROT_1, 0);
	mv64x60_write(bh, MV64360_ENET2MEM_ACC_PROT_2, 0);

	mv64x60_write(bh, MV64360_MPSC2MEM_ACC_PROT_0, 0);
	mv64x60_write(bh, MV64360_MPSC2MEM_ACC_PROT_1, 0);

	mv64x60_write(bh, MV64360_IDMA2MEM_ACC_PROT_0, 0);
	mv64x60_write(bh, MV64360_IDMA2MEM_ACC_PROT_1, 0);
	mv64x60_write(bh, MV64360_IDMA2MEM_ACC_PROT_2, 0);
	mv64x60_write(bh, MV64360_IDMA2MEM_ACC_PROT_3, 0);

	/* Assume that mem ctlr has no more windows than embedded I/O ctlr */
	for (win=MV64x60_CPU2MEM_0_WIN,i=0;win<=MV64x60_CPU2MEM_3_WIN;win++,i++)
		if (bh->ci->is_enabled_32bit(bh, win)) {
			mv64x60_set_32bit_window(bh, enet_tab[i],
				mem_windows[i][0], mem_windows[i][1],
				(dram_selects[i] << 8) |
				(si->enet_options[i] & 0x3000));
			bh->ci->enable_window_32bit(bh, enet_tab[i]);

			/* Give enet r/w access to memory region */
			mv64x60_set_bits(bh, MV64360_ENET2MEM_ACC_PROT_0,
				(0x3 << (i << 1)));
			mv64x60_set_bits(bh, MV64360_ENET2MEM_ACC_PROT_1,
				(0x3 << (i << 1)));
			mv64x60_set_bits(bh, MV64360_ENET2MEM_ACC_PROT_2,
				(0x3 << (i << 1)));

			mv64x60_set_32bit_window(bh, mpsc_tab[i],
				mem_windows[i][0], mem_windows[i][1],
				(dram_selects[i] << 8) |
				(si->mpsc_options[i] & 0x3000));
			bh->ci->enable_window_32bit(bh, mpsc_tab[i]);

			/* Give mpsc r/w access to memory region */
			mv64x60_set_bits(bh, MV64360_MPSC2MEM_ACC_PROT_0,
				(0x3 << (i << 1)));
			mv64x60_set_bits(bh, MV64360_MPSC2MEM_ACC_PROT_1,
				(0x3 << (i << 1)));

			mv64x60_set_32bit_window(bh, idma_tab[i],
				mem_windows[i][0], mem_windows[i][1],
				(dram_selects[i] << 8) |
				(si->idma_options[i] & 0x3000));
			bh->ci->enable_window_32bit(bh, idma_tab[i]);

			/* Give idma r/w access to memory region */
			mv64x60_set_bits(bh, MV64360_IDMA2MEM_ACC_PROT_0,
				(0x3 << (i << 1)));
			mv64x60_set_bits(bh, MV64360_IDMA2MEM_ACC_PROT_1,
				(0x3 << (i << 1)));
			mv64x60_set_bits(bh, MV64360_IDMA2MEM_ACC_PROT_2,
				(0x3 << (i << 1)));
			mv64x60_set_bits(bh, MV64360_IDMA2MEM_ACC_PROT_3,
				(0x3 << (i << 1)));
		}
}

/*
 * mv64360_set_mpsc2regs_window()
 *
 * MPSC has a window to the bridge's internal registers.  Call this routine
 * to change that window so it doesn't conflict with the windows mapping the
 * mpsc to system memory.
 */
static void __init
mv64360_set_mpsc2regs_window(struct mv64x60_handle *bh, u32 base)
{
	pr_debug("set mpsc->internal regs, base: 0x%x\n", base);
	mv64x60_write(bh, MV64360_MPSC2REGS_BASE, base & 0xffff0000);
}

/*
 * mv64360_chip_specific_init()
 *
 * Implement errata workarounds for the MV64360.
 */
static void __init
mv64360_chip_specific_init(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si)
{
#if !defined(CONFIG_NOT_COHERENT_CACHE)
	mv64x60_set_bits(bh, MV64360_D_UNIT_CONTROL_HIGH, (1<<24));
#endif
#ifdef CONFIG_SERIAL_MPSC
	mv64x60_mpsc0_pdata.brg_can_tune = 1;
	mv64x60_mpsc0_pdata.cache_mgmt = 1;
	mv64x60_mpsc1_pdata.brg_can_tune = 1;
	mv64x60_mpsc1_pdata.cache_mgmt = 1;
#endif
}

/*
 * mv64460_chip_specific_init()
 *
 * Implement errata workarounds for the MV64460.
 */
static void __init
mv64460_chip_specific_init(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si)
{
#if !defined(CONFIG_NOT_COHERENT_CACHE)
	mv64x60_set_bits(bh, MV64360_D_UNIT_CONTROL_HIGH, (1<<24) | (1<<25));
	mv64x60_set_bits(bh, MV64460_D_UNIT_MMASK, (1<<1) | (1<<4));
#endif
#ifdef CONFIG_SERIAL_MPSC
	mv64x60_mpsc0_pdata.brg_can_tune = 1;
	mv64x60_mpsc0_pdata.cache_mgmt = 1;
	mv64x60_mpsc1_pdata.brg_can_tune = 1;
	mv64x60_mpsc1_pdata.cache_mgmt = 1;
#endif
}


#if defined(CONFIG_SYSFS) && !defined(CONFIG_GT64260)
/* Export the hotswap register via sysfs for enum event monitoring */
#define	VAL_LEN_MAX	11 /* 32-bit hex or dec stringified number + '\n' */

DECLARE_MUTEX(mv64xxx_hs_lock);

static ssize_t
mv64xxx_hs_reg_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	u32	v;
	u8	save_exclude;

	if (off > 0)
		return 0;
	if (count < VAL_LEN_MAX)
		return -EINVAL;

	if (down_interruptible(&mv64xxx_hs_lock))
		return -ERESTARTSYS;
	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;
	early_read_config_dword(&sysfs_hose_a, 0, PCI_DEVFN(0, 0),
			MV64360_PCICFG_CPCI_HOTSWAP, &v);
	mv64x60_pci_exclude_bridge = save_exclude;
	up(&mv64xxx_hs_lock);

	return sprintf(buf, "0x%08x\n", v);
}

static ssize_t
mv64xxx_hs_reg_write(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	u32	v;
	u8	save_exclude;

	if (off > 0)
		return 0;
	if (count <= 0)
		return -EINVAL;

	if (sscanf(buf, "%i", &v) == 1) {
		if (down_interruptible(&mv64xxx_hs_lock))
			return -ERESTARTSYS;
		save_exclude = mv64x60_pci_exclude_bridge;
		mv64x60_pci_exclude_bridge = 0;
		early_write_config_dword(&sysfs_hose_a, 0, PCI_DEVFN(0, 0),
				MV64360_PCICFG_CPCI_HOTSWAP, v);
		mv64x60_pci_exclude_bridge = save_exclude;
		up(&mv64xxx_hs_lock);
	}
	else
		count = -EINVAL;

	return count;
}

static struct bin_attribute mv64xxx_hs_reg_attr = { /* Hotswap register */
	.attr = {
		.name = "hs_reg",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size  = VAL_LEN_MAX,
	.read  = mv64xxx_hs_reg_read,
	.write = mv64xxx_hs_reg_write,
};

/* Provide sysfs file indicating if this platform supports the hs_reg */
static ssize_t
mv64xxx_hs_reg_valid_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct platform_device	*pdev;
	struct mv64xxx_pdata	*pdp;
	u32			v;

	pdev = container_of(dev, struct platform_device, dev);
	pdp = (struct mv64xxx_pdata *)pdev->dev.platform_data;

	if (down_interruptible(&mv64xxx_hs_lock))
		return -ERESTARTSYS;
	v = pdp->hs_reg_valid;
	up(&mv64xxx_hs_lock);

	return sprintf(buf, "%i\n", v);
}
static DEVICE_ATTR(hs_reg_valid, S_IRUGO, mv64xxx_hs_reg_valid_show, NULL);

static int __init
mv64xxx_sysfs_init(void)
{
	sysfs_create_bin_file(&mv64xxx_device.dev.kobj, &mv64xxx_hs_reg_attr);
	sysfs_create_file(&mv64xxx_device.dev.kobj,&dev_attr_hs_reg_valid.attr);
	return 0;
}
subsys_initcall(mv64xxx_sysfs_init);
#endif
