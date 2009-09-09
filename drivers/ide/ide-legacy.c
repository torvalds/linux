#include <linux/kernel.h>
#include <linux/ide.h>

static void ide_legacy_init_one(struct ide_hw **hws, struct ide_hw *hw,
				u8 port_no, const struct ide_port_info *d,
				unsigned long config)
{
	unsigned long base, ctl;
	int irq;

	if (port_no == 0) {
		base = 0x1f0;
		ctl  = 0x3f6;
		irq  = 14;
	} else {
		base = 0x170;
		ctl  = 0x376;
		irq  = 15;
	}

	if (!request_region(base, 8, d->name)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX-0x%lX not free.\n",
				d->name, base, base + 7);
		return;
	}

	if (!request_region(ctl, 1, d->name)) {
		printk(KERN_ERR "%s: I/O resource 0x%lX not free.\n",
				d->name, ctl);
		release_region(base, 8);
		return;
	}

	ide_std_init_ports(hw, base, ctl);
	hw->irq = irq;
	hw->config = config;

	hws[port_no] = hw;
}

int ide_legacy_device_add(const struct ide_port_info *d, unsigned long config)
{
	struct ide_hw hw[2], *hws[] = { NULL, NULL };

	memset(&hw, 0, sizeof(hw));

	if ((d->host_flags & IDE_HFLAG_QD_2ND_PORT) == 0)
		ide_legacy_init_one(hws, &hw[0], 0, d, config);
	ide_legacy_init_one(hws, &hw[1], 1, d, config);

	if (hws[0] == NULL && hws[1] == NULL &&
	    (d->host_flags & IDE_HFLAG_SINGLE))
		return -ENOENT;

	return ide_host_add(d, hws, 2, NULL);
}
EXPORT_SYMBOL_GPL(ide_legacy_device_add);
