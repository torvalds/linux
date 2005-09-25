/*
 *  arch/ppc/platforms/pmac_feature.c
 *
 *  Copyright (C) 1996-2001 Paul Mackerras (paulus@cs.anu.edu.au)
 *                          Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  TODO:
 *
 *   - Replace mdelay with some schedule loop if possible
 *   - Shorten some obfuscated delays on some routines (like modem
 *     power)
 *   - Refcount some clocks (see darwin)
 *   - Split split split...
 *
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <asm/sections.h>
#include <asm/errno.h>
#include <asm/keylargo.h>
#include <asm/uninorth.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/dbdma.h>
#include <asm/pci-bridge.h>
#include <asm/pmac_low_i2c.h>

#undef DEBUG_FEATURE

#ifdef DEBUG_FEATURE
#define DBG(fmt...) printk(KERN_DEBUG fmt)
#else
#define DBG(fmt...)
#endif

/*
 * We use a single global lock to protect accesses. Each driver has
 * to take care of its own locking
 */
static DEFINE_SPINLOCK(feature_lock  __pmacdata);

#define LOCK(flags)	spin_lock_irqsave(&feature_lock, flags);
#define UNLOCK(flags)	spin_unlock_irqrestore(&feature_lock, flags);


/*
 * Instance of some macio stuffs
 */
struct macio_chip macio_chips[MAX_MACIO_CHIPS]  __pmacdata;

struct macio_chip* __pmac macio_find(struct device_node* child, int type)
{
	while(child) {
		int	i;

		for (i=0; i < MAX_MACIO_CHIPS && macio_chips[i].of_node; i++)
			if (child == macio_chips[i].of_node &&
			    (!type || macio_chips[i].type == type))
				return &macio_chips[i];
		child = child->parent;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(macio_find);

static const char* macio_names[] __pmacdata =
{
	"Unknown",
	"Grand Central",
	"OHare",
	"OHareII",
	"Heathrow",
	"Gatwick",
	"Paddington",
	"Keylargo",
	"Pangea",
	"Intrepid",
	"K2"
};



/*
 * Uninorth reg. access. Note that Uni-N regs are big endian
 */

#define UN_REG(r)	(uninorth_base + ((r) >> 2))
#define UN_IN(r)	(in_be32(UN_REG(r)))
#define UN_OUT(r,v)	(out_be32(UN_REG(r), (v)))
#define UN_BIS(r,v)	(UN_OUT((r), UN_IN(r) | (v)))
#define UN_BIC(r,v)	(UN_OUT((r), UN_IN(r) & ~(v)))

static struct device_node* uninorth_node __pmacdata;
static u32* uninorth_base __pmacdata;
static u32 uninorth_rev __pmacdata;
static void *u3_ht;

extern struct device_node *k2_skiplist[2];

/*
 * For each motherboard family, we have a table of functions pointers
 * that handle the various features.
 */

typedef long (*feature_call)(struct device_node* node, long param, long value);

struct feature_table_entry {
	unsigned int	selector;
	feature_call	function;
};

struct pmac_mb_def
{
	const char*			model_string;
	const char*			model_name;
	int				model_id;
	struct feature_table_entry* 	features;
	unsigned long			board_flags;
};
static struct pmac_mb_def pmac_mb __pmacdata;

/*
 * Here are the chip specific feature functions
 */


static long __pmac g5_read_gpio(struct device_node* node, long param, long value)
{
	struct macio_chip* macio = &macio_chips[0];

	return MACIO_IN8(param);
}


static long __pmac g5_write_gpio(struct device_node* node, long param, long value)
{
	struct macio_chip* macio = &macio_chips[0];

	MACIO_OUT8(param, (u8)(value & 0xff));
	return 0;
}

static long __pmac g5_gmac_enable(struct device_node* node, long param, long value)
{
	struct macio_chip* macio = &macio_chips[0];
	unsigned long flags;

	if (node == NULL)
		return -ENODEV;

	LOCK(flags);
	if (value) {
		MACIO_BIS(KEYLARGO_FCR1, K2_FCR1_GMAC_CLK_ENABLE);
		mb();
		k2_skiplist[0] = NULL;
	} else {
		k2_skiplist[0] = node;
		mb();
		MACIO_BIC(KEYLARGO_FCR1, K2_FCR1_GMAC_CLK_ENABLE);
	}
	
	UNLOCK(flags);
	mdelay(1);

	return 0;
}

static long __pmac g5_fw_enable(struct device_node* node, long param, long value)
{
	struct macio_chip* macio = &macio_chips[0];
	unsigned long flags;

	if (node == NULL)
		return -ENODEV;

	LOCK(flags);
	if (value) {
		MACIO_BIS(KEYLARGO_FCR1, K2_FCR1_FW_CLK_ENABLE);
		mb();
		k2_skiplist[1] = NULL;
	} else {
		k2_skiplist[1] = node;
		mb();
		MACIO_BIC(KEYLARGO_FCR1, K2_FCR1_FW_CLK_ENABLE);
	}
	
	UNLOCK(flags);
	mdelay(1);

	return 0;
}

static long __pmac g5_mpic_enable(struct device_node* node, long param, long value)
{
	unsigned long flags;

	if (node->parent == NULL || strcmp(node->parent->name, "u3"))
		return 0;

	LOCK(flags);
	UN_BIS(U3_TOGGLE_REG, U3_MPIC_RESET | U3_MPIC_OUTPUT_ENABLE);
	UNLOCK(flags);

	return 0;
}

static long __pmac g5_eth_phy_reset(struct device_node* node, long param, long value)
{
	struct macio_chip* macio = &macio_chips[0];
	struct device_node *phy;
	int need_reset;

	/*
	 * We must not reset the combo PHYs, only the BCM5221 found in
	 * the iMac G5.
	 */
	phy = of_get_next_child(node, NULL);
	if (!phy)
		return -ENODEV;
	need_reset = device_is_compatible(phy, "B5221");
	of_node_put(phy);
	if (!need_reset)
		return 0;

	/* PHY reset is GPIO 29, not in device-tree unfortunately */
	MACIO_OUT8(K2_GPIO_EXTINT_0 + 29,
		   KEYLARGO_GPIO_OUTPUT_ENABLE | KEYLARGO_GPIO_OUTOUT_DATA);
	/* Thankfully, this is now always called at a time when we can
	 * schedule by sungem.
	 */
	msleep(10);
	MACIO_OUT8(K2_GPIO_EXTINT_0 + 29, 0);

	return 0;
}

static long __pmac g5_i2s_enable(struct device_node *node, long param, long value)
{
	/* Very crude implementation for now */
	struct macio_chip* macio = &macio_chips[0];
	unsigned long flags;

	if (value == 0)
		return 0; /* don't disable yet */

	LOCK(flags);
	MACIO_BIS(KEYLARGO_FCR3, KL3_CLK45_ENABLE | KL3_CLK49_ENABLE |
		  KL3_I2S0_CLK18_ENABLE);
	udelay(10);
	MACIO_BIS(KEYLARGO_FCR1, K2_FCR1_I2S0_CELL_ENABLE |
		  K2_FCR1_I2S0_CLK_ENABLE_BIT | K2_FCR1_I2S0_ENABLE);
	udelay(10);
	MACIO_BIC(KEYLARGO_FCR1, K2_FCR1_I2S0_RESET);
	UNLOCK(flags);
	udelay(10);

	return 0;
}


#ifdef CONFIG_SMP
static long __pmac g5_reset_cpu(struct device_node* node, long param, long value)
{
	unsigned int reset_io = 0;
	unsigned long flags;
	struct macio_chip* macio;
	struct device_node* np;

	macio = &macio_chips[0];
	if (macio->type != macio_keylargo2)
		return -ENODEV;

	np = find_path_device("/cpus");
	if (np == NULL)
		return -ENODEV;
	for (np = np->child; np != NULL; np = np->sibling) {
		u32* num = (u32 *)get_property(np, "reg", NULL);
		u32* rst = (u32 *)get_property(np, "soft-reset", NULL);
		if (num == NULL || rst == NULL)
			continue;
		if (param == *num) {
			reset_io = *rst;
			break;
		}
	}
	if (np == NULL || reset_io == 0)
		return -ENODEV;

	LOCK(flags);
	MACIO_OUT8(reset_io, KEYLARGO_GPIO_OUTPUT_ENABLE);
	(void)MACIO_IN8(reset_io);
	udelay(1);
	MACIO_OUT8(reset_io, 0);
	(void)MACIO_IN8(reset_io);
	UNLOCK(flags);

	return 0;
}
#endif /* CONFIG_SMP */

/*
 * This can be called from pmac_smp so isn't static
 *
 * This takes the second CPU off the bus on dual CPU machines
 * running UP
 */
void __pmac g5_phy_disable_cpu1(void)
{
	UN_OUT(U3_API_PHY_CONFIG_1, 0);
}

static long __pmac generic_get_mb_info(struct device_node* node, long param, long value)
{
	switch(param) {
		case PMAC_MB_INFO_MODEL:
			return pmac_mb.model_id;
		case PMAC_MB_INFO_FLAGS:
			return pmac_mb.board_flags;
		case PMAC_MB_INFO_NAME:			
			/* hack hack hack... but should work */
			*((const char **)value) = pmac_mb.model_name;
			return 0;
	}
	return -EINVAL;
}


/*
 * Table definitions
 */

/* Used on any machine
 */
static struct feature_table_entry any_features[]  __pmacdata = {
	{ PMAC_FTR_GET_MB_INFO,		generic_get_mb_info },
	{ 0, NULL }
};

/* G5 features
 */
static struct feature_table_entry g5_features[]  __pmacdata = {
	{ PMAC_FTR_GMAC_ENABLE,		g5_gmac_enable },
	{ PMAC_FTR_1394_ENABLE,		g5_fw_enable },
	{ PMAC_FTR_ENABLE_MPIC,		g5_mpic_enable },
	{ PMAC_FTR_READ_GPIO,		g5_read_gpio },
	{ PMAC_FTR_WRITE_GPIO,		g5_write_gpio },
	{ PMAC_FTR_GMAC_PHY_RESET,	g5_eth_phy_reset },
	{ PMAC_FTR_SOUND_CHIP_ENABLE,	g5_i2s_enable },
#ifdef CONFIG_SMP
	{ PMAC_FTR_RESET_CPU,		g5_reset_cpu },
#endif /* CONFIG_SMP */
	{ 0, NULL }
};

static struct pmac_mb_def pmac_mb_defs[] __pmacdata = {
	{	"PowerMac7,2",			"PowerMac G5",
		PMAC_TYPE_POWERMAC_G5,		g5_features,
		0,
	},
	{	"PowerMac7,3",			"PowerMac G5",
		PMAC_TYPE_POWERMAC_G5,		g5_features,
		0,
	},
	{	"PowerMac8,1",			"iMac G5",
		PMAC_TYPE_IMAC_G5,		g5_features,
		0,
	},
	{	"PowerMac9,1",			"PowerMac G5",
		PMAC_TYPE_POWERMAC_G5_U3L,	g5_features,
		0,
	},
	{       "RackMac3,1",                   "XServe G5",
		PMAC_TYPE_XSERVE_G5,		g5_features,
		0,
	},
};

/*
 * The toplevel feature_call callback
 */
long __pmac pmac_do_feature_call(unsigned int selector, ...)
{
	struct device_node* node;
	long param, value;
	int i;
	feature_call func = NULL;
	va_list args;

	if (pmac_mb.features)
		for (i=0; pmac_mb.features[i].function; i++)
			if (pmac_mb.features[i].selector == selector) {
				func = pmac_mb.features[i].function;
				break;
			}
	if (!func)
		for (i=0; any_features[i].function; i++)
			if (any_features[i].selector == selector) {
				func = any_features[i].function;
				break;
			}
	if (!func)
		return -ENODEV;

	va_start(args, selector);
	node = (struct device_node*)va_arg(args, void*);
	param = va_arg(args, long);
	value = va_arg(args, long);
	va_end(args);

	return func(node, param, value);
}

static int __init probe_motherboard(void)
{
	int i;
	struct macio_chip* macio = &macio_chips[0];
	const char* model = NULL;
	struct device_node *dt;

	/* Lookup known motherboard type in device-tree. First try an
	 * exact match on the "model" property, then try a "compatible"
	 * match is none is found.
	 */
	dt = find_devices("device-tree");
	if (dt != NULL)
		model = (const char *) get_property(dt, "model", NULL);
	for(i=0; model && i<(sizeof(pmac_mb_defs)/sizeof(struct pmac_mb_def)); i++) {
	    if (strcmp(model, pmac_mb_defs[i].model_string) == 0) {
		pmac_mb = pmac_mb_defs[i];
		goto found;
	    }
	}
	for(i=0; i<(sizeof(pmac_mb_defs)/sizeof(struct pmac_mb_def)); i++) {
	    if (machine_is_compatible(pmac_mb_defs[i].model_string)) {
		pmac_mb = pmac_mb_defs[i];
		goto found;
	    }
	}

	/* Fallback to selection depending on mac-io chip type */
	switch(macio->type) {
	case macio_keylargo2:
		pmac_mb.model_id = PMAC_TYPE_UNKNOWN_K2;
		pmac_mb.model_name = "Unknown K2-based";
	    	pmac_mb.features = g5_features;
		
	default:
	    	return -ENODEV;
	}
found:
	/* Check for "mobile" machine */
	if (model && (strncmp(model, "PowerBook", 9) == 0
		   || strncmp(model, "iBook", 5) == 0))
		pmac_mb.board_flags |= PMAC_MB_MOBILE;


	printk(KERN_INFO "PowerMac motherboard: %s\n", pmac_mb.model_name);
	return 0;
}

/* Initialize the Core99 UniNorth host bridge and memory controller
 */
static void __init probe_uninorth(void)
{
	uninorth_node = of_find_node_by_name(NULL, "u3");
	if (uninorth_node && uninorth_node->n_addrs > 0) {
		/* Small hack until I figure out if parsing in prom.c is correct. I should
		 * get rid of those pre-parsed junk anyway
		 */
		unsigned long address = uninorth_node->addrs[0].address;
		uninorth_base = ioremap(address, 0x40000);
		uninorth_rev = in_be32(UN_REG(UNI_N_VERSION));
		u3_ht = ioremap(address + U3_HT_CONFIG_BASE, 0x1000);
	} else
		uninorth_node = NULL;

	if (!uninorth_node)
		return;

	printk(KERN_INFO "Found U3 memory controller & host bridge, revision: %d\n",
	       uninorth_rev);
	printk(KERN_INFO "Mapped at 0x%08lx\n", (unsigned long)uninorth_base);

}

static void __init probe_one_macio(const char* name, const char* compat, int type)
{
	struct device_node*	node;
	int			i;
	volatile u32*		base;
	u32*			revp;

	node = find_devices(name);
	if (!node || !node->n_addrs)
		return;
	if (compat)
		do {
			if (device_is_compatible(node, compat))
				break;
			node = node->next;
		} while (node);
	if (!node)
		return;
	for(i=0; i<MAX_MACIO_CHIPS; i++) {
		if (!macio_chips[i].of_node)
			break;
		if (macio_chips[i].of_node == node)
			return;
	}
	if (i >= MAX_MACIO_CHIPS) {
		printk(KERN_ERR "pmac_feature: Please increase MAX_MACIO_CHIPS !\n");
		printk(KERN_ERR "pmac_feature: %s skipped\n", node->full_name);
		return;
	}
	base = (volatile u32*)ioremap(node->addrs[0].address, node->addrs[0].size);
	if (!base) {
		printk(KERN_ERR "pmac_feature: Can't map mac-io chip !\n");
		return;
	}
	if (type == macio_keylargo) {
		u32* did = (u32 *)get_property(node, "device-id", NULL);
		if (*did == 0x00000025)
			type = macio_pangea;
		if (*did == 0x0000003e)
			type = macio_intrepid;
	}
	macio_chips[i].of_node	= node;
	macio_chips[i].type	= type;
	macio_chips[i].base	= base;
	macio_chips[i].flags	= MACIO_FLAG_SCCB_ON | MACIO_FLAG_SCCB_ON;
	macio_chips[i].name 	= macio_names[type];
	revp = (u32 *)get_property(node, "revision-id", NULL);
	if (revp)
		macio_chips[i].rev = *revp;
	printk(KERN_INFO "Found a %s mac-io controller, rev: %d, mapped at 0x%p\n",
		macio_names[type], macio_chips[i].rev, macio_chips[i].base);
}

static int __init
probe_macios(void)
{
	probe_one_macio("mac-io", "K2-Keylargo", macio_keylargo2);

	macio_chips[0].lbus.index = 0;
	macio_chips[1].lbus.index = 1;

	return (macio_chips[0].of_node == NULL) ? -ENODEV : 0;
}

static void __init
set_initial_features(void)
{
	struct device_node *np;

	if (macio_chips[0].type == macio_keylargo2) {
#ifndef CONFIG_SMP
		/* On SMP machines running UP, we have the second CPU eating
		 * bus cycles. We need to take it off the bus. This is done
		 * from pmac_smp for SMP kernels running on one CPU
		 */
		np = of_find_node_by_type(NULL, "cpu");
		if (np != NULL)
			np = of_find_node_by_type(np, "cpu");
		if (np != NULL) {
			g5_phy_disable_cpu1();
			of_node_put(np);
		}
#endif /* CONFIG_SMP */
		/* Enable GMAC for now for PCI probing. It will be disabled
		 * later on after PCI probe
		 */
		np = of_find_node_by_name(NULL, "ethernet");
		while(np) {
			if (device_is_compatible(np, "K2-GMAC"))
				g5_gmac_enable(np, 0, 1);
			np = of_find_node_by_name(np, "ethernet");
		}

		/* Enable FW before PCI probe. Will be disabled later on
		 * Note: We should have a batter way to check that we are
		 * dealing with uninorth internal cell and not a PCI cell
		 * on the external PCI. The code below works though.
		 */
		np = of_find_node_by_name(NULL, "firewire");
		while(np) {
			if (device_is_compatible(np, "pci106b,5811")) {
				macio_chips[0].flags |= MACIO_FLAG_FW_SUPPORTED;
				g5_fw_enable(np, 0, 1);
			}
			np = of_find_node_by_name(np, "firewire");
		}
	}
}

void __init
pmac_feature_init(void)
{
	/* Detect the UniNorth memory controller */
	probe_uninorth();

	/* Probe mac-io controllers */
	if (probe_macios()) {
		printk(KERN_WARNING "No mac-io chip found\n");
		return;
	}

	/* Setup low-level i2c stuffs */
	pmac_init_low_i2c();

	/* Probe machine type */
	if (probe_motherboard())
		printk(KERN_WARNING "Unknown PowerMac !\n");

	/* Set some initial features (turn off some chips that will
	 * be later turned on)
	 */
	set_initial_features();
}

int __init pmac_feature_late_init(void)
{
#if 0
	struct device_node* np;

	/* Request some resources late */
	if (uninorth_node)
		request_OF_resource(uninorth_node, 0, NULL);
	np = find_devices("hammerhead");
	if (np)
		request_OF_resource(np, 0, NULL);
	np = find_devices("interrupt-controller");
	if (np)
		request_OF_resource(np, 0, NULL);
#endif
	return 0;
}

device_initcall(pmac_feature_late_init);

#if 0
static void dump_HT_speeds(char *name, u32 cfg, u32 frq)
{
	int	freqs[16] = { 200,300,400,500,600,800,1000,0,0,0,0,0,0,0,0,0 };
	int	bits[8] = { 8,16,0,32,2,4,0,0 };
	int	freq = (frq >> 8) & 0xf;

	if (freqs[freq] == 0)
		printk("%s: Unknown HT link frequency %x\n", name, freq);
	else
		printk("%s: %d MHz on main link, (%d in / %d out) bits width\n",
		       name, freqs[freq],
		       bits[(cfg >> 28) & 0x7], bits[(cfg >> 24) & 0x7]);
}
#endif

void __init pmac_check_ht_link(void)
{
#if 0 /* Disabled for now */
	u32	ufreq, freq, ucfg, cfg;
	struct device_node *pcix_node;
	struct pci_dn *pdn;
	u8  	px_bus, px_devfn;
	struct pci_controller *px_hose;

	(void)in_be32(u3_ht + U3_HT_LINK_COMMAND);
	ucfg = cfg = in_be32(u3_ht + U3_HT_LINK_CONFIG);
	ufreq = freq = in_be32(u3_ht + U3_HT_LINK_FREQ);
	dump_HT_speeds("U3 HyperTransport", cfg, freq);

	pcix_node = of_find_compatible_node(NULL, "pci", "pci-x");
	if (pcix_node == NULL) {
		printk("No PCI-X bridge found\n");
		return;
	}
	pdn = pcix_node->data;
	px_hose = pdn->phb;
	px_bus = pdn->busno;
	px_devfn = pdn->devfn;
	
	early_read_config_dword(px_hose, px_bus, px_devfn, 0xc4, &cfg);
	early_read_config_dword(px_hose, px_bus, px_devfn, 0xcc, &freq);
	dump_HT_speeds("PCI-X HT Uplink", cfg, freq);
	early_read_config_dword(px_hose, px_bus, px_devfn, 0xc8, &cfg);
	early_read_config_dword(px_hose, px_bus, px_devfn, 0xd0, &freq);
	dump_HT_speeds("PCI-X HT Downlink", cfg, freq);
#endif
}

/*
 * Early video resume hook
 */

static void (*pmac_early_vresume_proc)(void *data) __pmacdata;
static void *pmac_early_vresume_data __pmacdata;

void pmac_set_early_video_resume(void (*proc)(void *data), void *data)
{
	if (_machine != _MACH_Pmac)
		return;
	preempt_disable();
	pmac_early_vresume_proc = proc;
	pmac_early_vresume_data = data;
	preempt_enable();
}
EXPORT_SYMBOL(pmac_set_early_video_resume);


/*
 * AGP related suspend/resume code
 */

static struct pci_dev *pmac_agp_bridge __pmacdata;
static int (*pmac_agp_suspend)(struct pci_dev *bridge) __pmacdata;
static int (*pmac_agp_resume)(struct pci_dev *bridge) __pmacdata;

void __pmac pmac_register_agp_pm(struct pci_dev *bridge,
				 int (*suspend)(struct pci_dev *bridge),
				 int (*resume)(struct pci_dev *bridge))
{
	if (suspend || resume) {
		pmac_agp_bridge = bridge;
		pmac_agp_suspend = suspend;
		pmac_agp_resume = resume;
		return;
	}
	if (bridge != pmac_agp_bridge)
		return;
	pmac_agp_suspend = pmac_agp_resume = NULL;
	return;
}
EXPORT_SYMBOL(pmac_register_agp_pm);

void __pmac pmac_suspend_agp_for_card(struct pci_dev *dev)
{
	if (pmac_agp_bridge == NULL || pmac_agp_suspend == NULL)
		return;
	if (pmac_agp_bridge->bus != dev->bus)
		return;
	pmac_agp_suspend(pmac_agp_bridge);
}
EXPORT_SYMBOL(pmac_suspend_agp_for_card);

void __pmac pmac_resume_agp_for_card(struct pci_dev *dev)
{
	if (pmac_agp_bridge == NULL || pmac_agp_resume == NULL)
		return;
	if (pmac_agp_bridge->bus != dev->bus)
		return;
	pmac_agp_resume(pmac_agp_bridge);
}
EXPORT_SYMBOL(pmac_resume_agp_for_card);
