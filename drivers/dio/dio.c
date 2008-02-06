/* Code to support devices on the DIO and DIO-II bus
 * Copyright (C) 05/1998 Peter Maydell <pmaydell@chiark.greenend.org.uk>
 * Copyright (C) 2004 Jochen Friedrich <jochen@scram.de>
 * 
 * This code has basically these routines at the moment:
 * int dio_find(u_int deviceid)
 *    Search the list of DIO devices and return the select code
 *    of the next unconfigured device found that matches the given device ID.
 *    Note that the deviceid parameter should be the encoded ID.
 *    This means that framebuffers should pass it as 
 *    DIO_ENCODE_ID(DIO_ID_FBUFFER,DIO_ID2_TOPCAT)
 *    (or whatever); everybody else just uses DIO_ID_FOOBAR.
 * unsigned long dio_scodetophysaddr(int scode)
 *    Return the physical address corresponding to the given select code.
 * int dio_scodetoipl(int scode)
 *    Every DIO card has a fixed interrupt priority level. This function 
 *    returns it, whatever it is.
 * const char *dio_scodetoname(int scode)
 *    Return a character string describing this board [might be "" if 
 *    not CONFIG_DIO_CONSTANTS]
 * void dio_config_board(int scode)     mark board as configured in the list
 * void dio_unconfig_board(int scode)   mark board as no longer configured
 *
 * This file is based on the way the Amiga port handles Zorro II cards, 
 * although we aren't so complicated...
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dio.h>
#include <linux/slab.h>                         /* kmalloc() */
#include <asm/uaccess.h>
#include <asm/io.h>                             /* readb() */

struct dio_bus dio_bus = {
	.resources = {
		/* DIO range */
		{ .name = "DIO mem", .start = 0x00600000, .end = 0x007fffff },
		/* DIO-II range */
		{ .name = "DIO-II mem", .start = 0x01000000, .end = 0x1fffffff }
	},
	.name = "DIO bus"
};

/* not a real config option yet! */
#define CONFIG_DIO_CONSTANTS

#ifdef CONFIG_DIO_CONSTANTS
/* We associate each numeric ID with an appropriate descriptive string
 * using a constant array of these structs.
 * FIXME: we should be able to arrange to throw away most of the strings
 * using the initdata stuff. Then we wouldn't need to worry about 
 * carrying them around...
 * I think we do this by copying them into newly kmalloc()ed memory and 
 * marking the names[] array as .initdata ?
 */
struct dioname
{
        int id;
        const char *name;
};

/* useful macro */
#define DIONAME(x) { DIO_ID_##x, DIO_DESC_##x }
#define DIOFBNAME(x) { DIO_ENCODE_ID( DIO_ID_FBUFFER, DIO_ID2_##x), DIO_DESC2_##x }

static struct dioname names[] = 
{
        DIONAME(DCA0), DIONAME(DCA0REM), DIONAME(DCA1), DIONAME(DCA1REM),
        DIONAME(DCM), DIONAME(DCMREM),
        DIONAME(LAN),
        DIONAME(FHPIB), DIONAME(NHPIB),
        DIONAME(SCSI0), DIONAME(SCSI1), DIONAME(SCSI2), DIONAME(SCSI3),
        DIONAME(FBUFFER),
        DIONAME(PARALLEL), DIONAME(VME), DIONAME(DCL), DIONAME(DCLREM),
        DIONAME(MISC0), DIONAME(MISC1), DIONAME(MISC2), DIONAME(MISC3),
        DIONAME(MISC4), DIONAME(MISC5), DIONAME(MISC6), DIONAME(MISC7),
        DIONAME(MISC8), DIONAME(MISC9), DIONAME(MISC10), DIONAME(MISC11), 
        DIONAME(MISC12), DIONAME(MISC13),
        DIOFBNAME(GATORBOX), DIOFBNAME(TOPCAT), DIOFBNAME(RENAISSANCE),
        DIOFBNAME(LRCATSEYE), DIOFBNAME(HRCCATSEYE), DIOFBNAME(HRMCATSEYE),
        DIOFBNAME(DAVINCI), DIOFBNAME(XXXCATSEYE), DIOFBNAME(HYPERION),
        DIOFBNAME(XGENESIS), DIOFBNAME(TIGER), DIOFBNAME(YGENESIS)   
};

#undef DIONAME
#undef DIOFBNAME

static const char *unknowndioname 
        = "unknown DIO board -- please email <linux-m68k@lists.linux-m68k.org>!";

static const char *dio_getname(int id)
{
        /* return pointer to a constant string describing the board with given ID */
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(names); i++)
                if (names[i].id == id) 
                        return names[i].name;

        return unknowndioname;
}

#else

static char dio_no_name[] = { 0 };
#define dio_getname(_id)	(dio_no_name)

#endif /* CONFIG_DIO_CONSTANTS */

int __init dio_find(int deviceid)
{
	/* Called to find a DIO device before the full bus scan has run.
	 * Only used by the console driver.
	 */
	int scode, id;
	u_char prid, secid, i;
	mm_segment_t fs;

	for (scode = 0; scode < DIO_SCMAX; scode++) {
		void *va;
		unsigned long pa;

                if (DIO_SCINHOLE(scode))
                        continue;

                pa = dio_scodetophysaddr(scode);

		if (!pa)
			continue;

		if (scode < DIOII_SCBASE)
			va = (void *)(pa + DIO_VIRADDRBASE);
		else
			va = ioremap(pa, PAGE_SIZE);

		fs = get_fs();
		set_fs(KERNEL_DS);

                if (get_user(i, (unsigned char *)va + DIO_IDOFF)) {
			set_fs(fs);
			if (scode >= DIOII_SCBASE)
				iounmap(va);
                        continue;             /* no board present at that select code */
		}

		set_fs(fs);
		prid = DIO_ID(va);

                if (DIO_NEEDSSECID(prid)) {
                        secid = DIO_SECID(va);
                        id = DIO_ENCODE_ID(prid, secid);
                } else
			id = prid;

		if (id == deviceid) {
			if (scode >= DIOII_SCBASE)
				iounmap(va);
			return scode;
		}
	}

	return -1;
}

/* This is the function that scans the DIO space and works out what
 * hardware is actually present.
 */
static int __init dio_init(void)
{
	int scode;
	mm_segment_t fs;
	int i;
	struct dio_dev *dev;

	if (!MACH_IS_HP300)
		return 0;

        printk(KERN_INFO "Scanning for DIO devices...\n");

	/* Initialize the DIO bus */ 
	INIT_LIST_HEAD(&dio_bus.devices);
	strcpy(dio_bus.dev.bus_id, "dio");
	device_register(&dio_bus.dev);

	/* Request all resources */
	dio_bus.num_resources = (hp300_model == HP_320 ? 1 : 2);
	for (i = 0; i < dio_bus.num_resources; i++)
		request_resource(&iomem_resource, &dio_bus.resources[i]);

	/* Register all devices */
        for (scode = 0; scode < DIO_SCMAX; ++scode)
        {
                u_char prid, secid = 0;        /* primary, secondary ID bytes */
                u_char *va;
		unsigned long pa;
                
                if (DIO_SCINHOLE(scode))
                        continue;

		pa = dio_scodetophysaddr(scode);

		if (!pa)
			continue;

		if (scode < DIOII_SCBASE)
			va = (void *)(pa + DIO_VIRADDRBASE);
		else
			va = ioremap(pa, PAGE_SIZE);

		fs = get_fs();
		set_fs(KERNEL_DS);

                if (get_user(i, (unsigned char *)va + DIO_IDOFF)) {
			set_fs(fs);
			if (scode >= DIOII_SCBASE)
				iounmap(va);
                        continue;              /* no board present at that select code */
		}

		set_fs(fs);

                /* Found a board, allocate it an entry in the list */
		dev = kzalloc(sizeof(struct dio_dev), GFP_KERNEL);
		if (!dev)
			return 0;

		dev->bus = &dio_bus;
		dev->dev.parent = &dio_bus.dev;
		dev->dev.bus = &dio_bus_type;
		dev->scode = scode;
		dev->resource.start = pa;
		dev->resource.end = pa + DIO_SIZE(scode, va);
		sprintf(dev->dev.bus_id,"%02x", scode);

                /* read the ID byte(s) and encode if necessary. */
		prid = DIO_ID(va);

                if (DIO_NEEDSSECID(prid)) {
                        secid = DIO_SECID(va);
                        dev->id = DIO_ENCODE_ID(prid, secid);
                } else
                        dev->id = prid;

                dev->ipl = DIO_IPL(va);
                strcpy(dev->name,dio_getname(dev->id));
                printk(KERN_INFO "select code %3d: ipl %d: ID %02X", dev->scode, dev->ipl, prid);
                if (DIO_NEEDSSECID(prid))
                        printk(":%02X", secid);
                printk(": %s\n", dev->name);

		if (scode >= DIOII_SCBASE)
			iounmap(va);
		device_register(&dev->dev);
		dio_create_sysfs_dev_files(dev);
        }
	return 0;
}

subsys_initcall(dio_init);

/* Bear in mind that this is called in the very early stages of initialisation
 * in order to get the address of the serial port for the console...
 */
unsigned long dio_scodetophysaddr(int scode)
{
        if (scode >= DIOII_SCBASE) {
                return (DIOII_BASE + (scode - 132) * DIOII_DEVSIZE);
        } else if (scode > DIO_SCMAX || scode < 0)
                return 0;
        else if (DIO_SCINHOLE(scode))
                return 0;

        return (DIO_BASE + scode * DIO_DEVSIZE);
}
