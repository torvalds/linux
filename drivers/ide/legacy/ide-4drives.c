
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ide.h>

int probe_4drives = 0;

module_param_named(probe, probe_4drives, bool, 0);
MODULE_PARM_DESC(probe, "probe for generic IDE chipset with 4 drives/port");

static int __init ide_4drives_init(void)
{
	ide_hwif_t *hwif, *mate;
	u8 idx[4] = { 0, 1, 0xff, 0xff };

	if (probe_4drives == 0)
		return -ENODEV;

	hwif = &ide_hwifs[0];
	mate = &ide_hwifs[1];

	memcpy(mate->io_ports, hwif->io_ports, sizeof(hwif->io_ports));

	mate->irq = hwif->irq;

	mate->chipset = hwif->chipset = ide_4drives;

	mate->drives[0].select.all ^= 0x20;
	mate->drives[1].select.all ^= 0x20;

	hwif->mate = mate;
	mate->mate = hwif;

	hwif->serialized = mate->serialized = 1;

	ide_device_add(idx, NULL);

	return 0;
}

module_init(ide_4drives_init);

MODULE_AUTHOR("Bartlomiej Zolnierkiewicz");
MODULE_DESCRIPTION("generic IDE chipset with 4 drives/port support");
MODULE_LICENSE("GPL");
