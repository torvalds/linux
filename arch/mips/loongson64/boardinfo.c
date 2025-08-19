// SPDX-License-Identifier: GPL-2.0
#include <linux/kobject.h>
#include <linux/string.h>
#include <boot_param.h>

static ssize_t boardinfo_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	char board_manufacturer[64];
	char *tmp_board_manufacturer = board_manufacturer;
	char bios_vendor[64];
	char *tmp_bios_vendor = bios_vendor;

	strscpy_pad(board_manufacturer, eboard->name);
	strscpy_pad(bios_vendor, einter->description);

	return sprintf(buf,
		       "Board Info\n"
		       "Manufacturer\t\t: %s\n"
		       "Board Name\t\t: %s\n"
		       "Family\t\t\t: LOONGSON3\n\n"
		       "BIOS Info\n"
		       "Vendor\t\t\t: %s\n"
		       "Version\t\t\t: %s\n"
		       "Release Date\t\t: %s\n",
		       strsep(&tmp_board_manufacturer, "-"),
		       eboard->name,
		       strsep(&tmp_bios_vendor, "-"),
		       einter->description,
		       especial->special_name);
}
static struct kobj_attribute boardinfo_attr = __ATTR(boardinfo, 0444,
						     boardinfo_show, NULL);

static int __init boardinfo_init(void)
{
	struct kobject *lefi_kobj;

	lefi_kobj = kobject_create_and_add("lefi", firmware_kobj);
	if (!lefi_kobj) {
		pr_err("lefi: Firmware registration failed.\n");
		return -ENOMEM;
	}

	return sysfs_create_file(lefi_kobj, &boardinfo_attr.attr);
}
late_initcall(boardinfo_init);
