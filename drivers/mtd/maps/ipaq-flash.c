/*
 * Flash memory access on iPAQ Handhelds (either SA1100 or PXA250 based)
 *
 * (C) 2000 Nicolas Pitre <nico@cam.org>
 * (C) 2002 Hewlett-Packard Company <jamey.hicks@hp.com>
 * (C) 2003 Christian Pellegrin <chri@ascensit.com>, <chri@infis.univ.ts.it>: concatenation of multiple flashes
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <asm/mach-types.h>
#include <asm/system.h>
#include <asm/errno.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#ifdef CONFIG_MTD_CONCAT
#include <linux/mtd/concat.h>
#endif

#include <mach/hardware.h>
#include <mach/h3600.h>
#include <asm/io.h>


#ifndef CONFIG_IPAQ_HANDHELD
#error This is for iPAQ Handhelds only
#endif
#ifdef CONFIG_SA1100_JORNADA56X

static void jornada56x_set_vpp(struct map_info *map, int vpp)
{
	if (vpp)
		GPSR = GPIO_GPIO26;
	else
		GPCR = GPIO_GPIO26;
	GPDR |= GPIO_GPIO26;
}

#endif

#ifdef CONFIG_SA1100_JORNADA720

static void jornada720_set_vpp(struct map_info *map, int vpp)
{
	if (vpp)
		PPSR |= 0x80;
	else
		PPSR &= ~0x80;
	PPDR |= 0x80;
}

#endif

#define MAX_IPAQ_CS 2		/* Number of CS we are going to test */

#define IPAQ_MAP_INIT(X) \
	{ \
		name:		"IPAQ flash " X, \
	}


static struct map_info ipaq_map[MAX_IPAQ_CS] = {
	IPAQ_MAP_INIT("bank 1"),
	IPAQ_MAP_INIT("bank 2")
};

static struct mtd_info *my_sub_mtd[MAX_IPAQ_CS] = {
	NULL,
	NULL
};

/*
 * Here are partition information for all known IPAQ-based devices.
 * See include/linux/mtd/partitions.h for definition of the mtd_partition
 * structure.
 *
 * The *_max_flash_size is the maximum possible mapped flash size which
 * is not necessarily the actual flash size.  It must be no more than
 * the value specified in the "struct map_desc *_io_desc" mapping
 * definition for the corresponding machine.
 *
 * Please keep these in alphabetical order, and formatted as per existing
 * entries.  Thanks.
 */

#ifdef CONFIG_IPAQ_HANDHELD
static unsigned long h3xxx_max_flash_size = 0x04000000;
static struct mtd_partition h3xxx_partitions[] = {
	{
		name:		"H3XXX boot firmware",
#ifndef CONFIG_LAB
		size:		0x00040000,
#else
		size:		0x00080000,
#endif
		offset:		0,
#ifndef CONFIG_LAB
		mask_flags:	MTD_WRITEABLE,  /* force read-only */
#endif
	},
	{
		name:		"H3XXX root jffs2",
#ifndef CONFIG_LAB
		size:		0x2000000 - 2*0x40000, /* Warning, this is fixed later */
		offset:		0x00040000,
#else
		size:		0x2000000 - 0x40000 - 0x80000, /* Warning, this is fixed later */
		offset:		0x00080000,
#endif
	},
	{
		name:		"asset",
		size:		0x40000,
		offset:		0x2000000 - 0x40000, /* Warning, this is fixed later */
		mask_flags:	MTD_WRITEABLE,  /* force read-only */
	}
};

#ifndef CONFIG_MTD_CONCAT
static struct mtd_partition h3xxx_partitions_bank2[] = {
	/* this is used only on 2 CS machines when concat is not present */
	{
		name:		"second H3XXX root jffs2",
		size:		0x1000000 - 0x40000, /* Warning, this is fixed later */
		offset:		0x00000000,
	},
	{
		name:		"second asset",
		size:		0x40000,
		offset:		0x1000000 - 0x40000, /* Warning, this is fixed later */
		mask_flags:	MTD_WRITEABLE,  /* force read-only */
	}
};
#endif

static DEFINE_SPINLOCK(ipaq_vpp_lock);

static void h3xxx_set_vpp(struct map_info *map, int vpp)
{
	static int nest = 0;

	spin_lock(&ipaq_vpp_lock);
	if (vpp)
		nest++;
	else
		nest--;
	if (nest)
		assign_h3600_egpio(IPAQ_EGPIO_VPP_ON, 1);
	else
		assign_h3600_egpio(IPAQ_EGPIO_VPP_ON, 0);
	spin_unlock(&ipaq_vpp_lock);
}

#endif

#if defined(CONFIG_SA1100_JORNADA56X) || defined(CONFIG_SA1100_JORNADA720)
static unsigned long jornada_max_flash_size = 0x02000000;
static struct mtd_partition jornada_partitions[] = {
	{
		name:		"Jornada boot firmware",
		size:		0x00040000,
		offset:		0,
		mask_flags:	MTD_WRITEABLE,  /* force read-only */
	}, {
		name:		"Jornada root jffs2",
		size:		MTDPART_SIZ_FULL,
		offset:		0x00040000,
	}
};
#endif


static struct mtd_partition *parsed_parts;
static struct mtd_info *mymtd;

static unsigned long cs_phys[] = {
#ifdef CONFIG_ARCH_SA1100
	SA1100_CS0_PHYS,
	SA1100_CS1_PHYS,
	SA1100_CS2_PHYS,
	SA1100_CS3_PHYS,
	SA1100_CS4_PHYS,
	SA1100_CS5_PHYS,
#else
	PXA_CS0_PHYS,
	PXA_CS1_PHYS,
	PXA_CS2_PHYS,
	PXA_CS3_PHYS,
	PXA_CS4_PHYS,
	PXA_CS5_PHYS,
#endif
};

static const char *part_probes[] = { "cmdlinepart", "RedBoot", NULL };

static int __init h1900_special_case(void);

static int __init ipaq_mtd_init(void)
{
	struct mtd_partition *parts = NULL;
	int nb_parts = 0;
	int parsed_nr_parts = 0;
	const char *part_type;
	int i; /* used when we have >1 flash chips */
	unsigned long tot_flashsize = 0; /* used when we have >1 flash chips */

	/* Default flash bankwidth */
	// ipaq_map.bankwidth = (MSC0 & MSC_RBW) ? 2 : 4;

	if (machine_is_h1900())
	{
		/* For our intents, the h1900 is not a real iPAQ, so we special-case it. */
		return h1900_special_case();
	}

	if (machine_is_h3100() || machine_is_h1900())
		for(i=0; i<MAX_IPAQ_CS; i++)
			ipaq_map[i].bankwidth = 2;
	else
		for(i=0; i<MAX_IPAQ_CS; i++)
			ipaq_map[i].bankwidth = 4;

	/*
	 * Static partition definition selection
	 */
	part_type = "static";

	simple_map_init(&ipaq_map[0]);
	simple_map_init(&ipaq_map[1]);

#ifdef CONFIG_IPAQ_HANDHELD
	if (machine_is_ipaq()) {
		parts = h3xxx_partitions;
		nb_parts = ARRAY_SIZE(h3xxx_partitions);
		for(i=0; i<MAX_IPAQ_CS; i++) {
			ipaq_map[i].size = h3xxx_max_flash_size;
			ipaq_map[i].set_vpp = h3xxx_set_vpp;
			ipaq_map[i].phys = cs_phys[i];
			ipaq_map[i].virt = ioremap(cs_phys[i], 0x04000000);
			if (machine_is_h3100 () || machine_is_h1900())
				ipaq_map[i].bankwidth = 2;
		}
		if (machine_is_h3600()) {
			/* No asset partition here */
			h3xxx_partitions[1].size += 0x40000;
			nb_parts--;
		}
	}
#endif
#ifdef CONFIG_ARCH_H5400
	if (machine_is_h5400()) {
		ipaq_map[0].size = 0x02000000;
		ipaq_map[1].size = 0x02000000;
		ipaq_map[1].phys = 0x02000000;
		ipaq_map[1].virt = ipaq_map[0].virt + 0x02000000;
	}
#endif
#ifdef CONFIG_ARCH_H1900
	if (machine_is_h1900()) {
		ipaq_map[0].size = 0x00400000;
		ipaq_map[1].size = 0x02000000;
		ipaq_map[1].phys = 0x00080000;
		ipaq_map[1].virt = ipaq_map[0].virt + 0x00080000;
	}
#endif

#ifdef CONFIG_SA1100_JORNADA56X
	if (machine_is_jornada56x()) {
		parts = jornada_partitions;
		nb_parts = ARRAY_SIZE(jornada_partitions);
		ipaq_map[0].size = jornada_max_flash_size;
		ipaq_map[0].set_vpp = jornada56x_set_vpp;
		ipaq_map[0].virt = (__u32)ioremap(0x0, 0x04000000);
	}
#endif
#ifdef CONFIG_SA1100_JORNADA720
	if (machine_is_jornada720()) {
		parts = jornada_partitions;
		nb_parts = ARRAY_SIZE(jornada_partitions);
		ipaq_map[0].size = jornada_max_flash_size;
		ipaq_map[0].set_vpp = jornada720_set_vpp;
	}
#endif


	if (machine_is_ipaq()) { /* for iPAQs only */
		for(i=0; i<MAX_IPAQ_CS; i++) {
			printk(KERN_NOTICE "iPAQ flash: probing %d-bit flash bus, window=%lx with CFI.\n", ipaq_map[i].bankwidth*8, ipaq_map[i].virt);
			my_sub_mtd[i] = do_map_probe("cfi_probe", &ipaq_map[i]);
			if (!my_sub_mtd[i]) {
				printk(KERN_NOTICE "iPAQ flash: probing %d-bit flash bus, window=%lx with JEDEC.\n", ipaq_map[i].bankwidth*8, ipaq_map[i].virt);
				my_sub_mtd[i] = do_map_probe("jedec_probe", &ipaq_map[i]);
			}
			if (!my_sub_mtd[i]) {
				printk(KERN_NOTICE "iPAQ flash: failed to find flash.\n");
				if (i)
					break;
				else
					return -ENXIO;
			} else
				printk(KERN_NOTICE "iPAQ flash: found %d bytes\n", my_sub_mtd[i]->size);

			/* do we really need this debugging? --joshua 20030703 */
			// printk("my_sub_mtd[%d]=%p\n", i, my_sub_mtd[i]);
			my_sub_mtd[i]->owner = THIS_MODULE;
			tot_flashsize += my_sub_mtd[i]->size;
		}
#ifdef CONFIG_MTD_CONCAT
		/* fix the asset location */
#	ifdef CONFIG_LAB
		h3xxx_partitions[1].size = tot_flashsize - 0x40000 - 0x80000 /* extra big boot block */;
#	else
		h3xxx_partitions[1].size = tot_flashsize - 2 * 0x40000;
#	endif
		h3xxx_partitions[2].offset = tot_flashsize - 0x40000;
		/* and concat the devices */
		mymtd = mtd_concat_create(&my_sub_mtd[0], i,
					  "ipaq");
		if (!mymtd) {
			printk("Cannot create iPAQ concat device\n");
			return -ENXIO;
		}
#else
		mymtd = my_sub_mtd[0];

		/*
		 *In the very near future, command line partition parsing
		 * will use the device name as 'mtd-id' instead of a value
		 * passed to the parse_cmdline_partitions() routine. Since
		 * the bootldr says 'ipaq', make sure it continues to work.
		 */
		mymtd->name = "ipaq";

		if ((machine_is_h3600())) {
#	ifdef CONFIG_LAB
			h3xxx_partitions[1].size = my_sub_mtd[0]->size - 0x80000;
#	else
			h3xxx_partitions[1].size = my_sub_mtd[0]->size - 0x40000;
#	endif
			nb_parts = 2;
		} else {
#	ifdef CONFIG_LAB
			h3xxx_partitions[1].size = my_sub_mtd[0]->size - 0x40000 - 0x80000; /* extra big boot block */
#	else
			h3xxx_partitions[1].size = my_sub_mtd[0]->size - 2*0x40000;
#	endif
			h3xxx_partitions[2].offset = my_sub_mtd[0]->size - 0x40000;
		}

		if (my_sub_mtd[1]) {
#	ifdef CONFIG_LAB
			h3xxx_partitions_bank2[0].size = my_sub_mtd[1]->size - 0x80000;
#	else
			h3xxx_partitions_bank2[0].size = my_sub_mtd[1]->size - 0x40000;
#	endif
			h3xxx_partitions_bank2[1].offset = my_sub_mtd[1]->size - 0x40000;
		}
#endif
	}
	else {
		/*
		 * Now let's probe for the actual flash.  Do it here since
		 * specific machine settings might have been set above.
		 */
		printk(KERN_NOTICE "IPAQ flash: probing %d-bit flash bus, window=%lx\n", ipaq_map[0].bankwidth*8, ipaq_map[0].virt);
		mymtd = do_map_probe("cfi_probe", &ipaq_map[0]);
		if (!mymtd)
			return -ENXIO;
		mymtd->owner = THIS_MODULE;
	}


	/*
	 * Dynamic partition selection stuff (might override the static ones)
	 */

	 i = parse_mtd_partitions(mymtd, part_probes, &parsed_parts, 0);

	 if (i > 0) {
		 nb_parts = parsed_nr_parts = i;
		 parts = parsed_parts;
		 part_type = "dynamic";
	 }

	 if (!parts) {
		 printk(KERN_NOTICE "IPAQ flash: no partition info available, registering whole flash at once\n");
		 add_mtd_device(mymtd);
#ifndef CONFIG_MTD_CONCAT
		 if (my_sub_mtd[1])
			 add_mtd_device(my_sub_mtd[1]);
#endif
	 } else {
		 printk(KERN_NOTICE "Using %s partition definition\n", part_type);
		 add_mtd_partitions(mymtd, parts, nb_parts);
#ifndef CONFIG_MTD_CONCAT
		 if (my_sub_mtd[1])
			 add_mtd_partitions(my_sub_mtd[1], h3xxx_partitions_bank2, ARRAY_SIZE(h3xxx_partitions_bank2));
#endif
	 }

	 return 0;
}

static void __exit ipaq_mtd_cleanup(void)
{
	int i;

	if (mymtd) {
		del_mtd_partitions(mymtd);
#ifndef CONFIG_MTD_CONCAT
		if (my_sub_mtd[1])
			del_mtd_partitions(my_sub_mtd[1]);
#endif
		map_destroy(mymtd);
#ifdef CONFIG_MTD_CONCAT
		for(i=0; i<MAX_IPAQ_CS; i++)
#else
			for(i=1; i<MAX_IPAQ_CS; i++)
#endif
			{
				if (my_sub_mtd[i])
					map_destroy(my_sub_mtd[i]);
			}
		kfree(parsed_parts);
	}
}

static int __init h1900_special_case(void)
{
	/* The iPAQ h1900 is a special case - it has weird ROM. */
	simple_map_init(&ipaq_map[0]);
	ipaq_map[0].size = 0x80000;
	ipaq_map[0].set_vpp = h3xxx_set_vpp;
	ipaq_map[0].phys = 0x0;
	ipaq_map[0].virt = ioremap(0x0, 0x04000000);
	ipaq_map[0].bankwidth = 2;

	printk(KERN_NOTICE "iPAQ flash: probing %d-bit flash bus, window=%lx with JEDEC.\n", ipaq_map[0].bankwidth*8, ipaq_map[0].virt);
	mymtd = do_map_probe("jedec_probe", &ipaq_map[0]);
	if (!mymtd)
		return -ENODEV;
	add_mtd_device(mymtd);
	printk(KERN_NOTICE "iPAQ flash: registered h1910 flash\n");

	return 0;
}

module_init(ipaq_mtd_init);
module_exit(ipaq_mtd_cleanup);

MODULE_AUTHOR("Jamey Hicks");
MODULE_DESCRIPTION("IPAQ CFI map driver");
MODULE_LICENSE("MIT");
