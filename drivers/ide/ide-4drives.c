
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ide.h>

#define DRV_NAME "ide-4drives"

static int probe_4drives;

module_param_named(probe, probe_4drives, bool, 0);
MODULE_PARM_DESC(probe, "probe for generic IDE chipset with 4 drives/port");

static void ide_4drives_init_dev(ide_drive_t *drive)
{
	if (drive->hwif->channel)
		drive->select ^= 0x20;
}

static const struct ide_port_ops ide_4drives_port_ops = {
	.init_dev		= ide_4drives_init_dev,
};

static const struct ide_port_info ide_4drives_port_info = {
	.port_ops		= &ide_4drives_port_ops,
	.host_flags		= IDE_HFLAG_SERIALIZE | IDE_HFLAG_NO_DMA |
				  IDE_HFLAG_4DRIVES,
	.chipset		= ide_4drives,
};

static int __init ide_4drives_init(void)
{
	unsigned long base = 0x1f0, ctl = 0x3f6;
	struct ide_hw hw, *hws[] = { &hw, &hw };

	if (probe_4drives == 0)
		return -ENODEV;

	if (!request_region(base, 8, DRV_NAME)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX-0x%lX not free.\n",
				DRV_NAME, base, base + 7);
		return -EBUSY;
	}

	if (!request_region(ctl, 1, DRV_NAME)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX not free.\n",
				DRV_NAME, ctl);
		release_region(base, 8);
		return -EBUSY;
	}

	memset(&hw, 0, sizeof(hw));

	ide_std_init_ports(&hw, base, ctl);
	hw.irq = 14;

	return ide_host_add(&ide_4drives_port_info, hws, 2, NULL);
}

module_init(ide_4drives_init);

MODULE_AUTHOR("Bartlomiej Zolnierkiewicz");
MODULE_DESCRIPTION("generic IDE chipset with 4 drives/port support");
MODULE_LICENSE("GPL");
