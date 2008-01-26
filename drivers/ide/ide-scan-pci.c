#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ide.h>

static int __init ide_scan_pci(void)
{
	return ide_scan_pcibus();
}

module_init(ide_scan_pci);
