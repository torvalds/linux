#include <linux/types.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/dmi.h>
#include <linux/efi.h>
#include <linux/bootmem.h>
#include <linux/random.h>
#include <asm/dmi.h>
#include <asm/unaligned.h>

struct kobject *dmi_kobj;
EXPORT_SYMBOL_GPL(dmi_kobj);

/*
 * DMI stands for "Desktop Management Interface".  It is part
 * of and an antecedent to, SMBIOS, which stands for System
 * Management BIOS.  See further: http://www.dmtf.org/standards
 */
static const char dmi_empty_string[] = "";

static u32 dmi_ver __initdata;
static u32 dmi_len;
static u16 dmi_num;
static u8 smbios_entry_point[32];
static int smbios_entry_point_size;

/* DMI system identification string used during boot */
static char dmi_ids_string[128] __initdata;

static struct dmi_memdev_info {
	const char *device;
	const char *bank;
	u64 size;		/* bytes */
	u16 handle;
} *dmi_memdev;
static int dmi_memdev_nr;

static const char * __init dmi_string_nosave(const struct dmi_header *dm, u8 s)
{
	const u8 *bp = ((u8 *) dm) + dm->length;
	const u8 *nsp;

	if (s) {
		while (--s > 0 && *bp)
			bp += strlen(bp) + 1;

		/* Strings containing only spaces are considered empty */
		nsp = bp;
		while (*nsp == ' ')
			nsp++;
		if (*nsp != '\0')
			return bp;
	}

	return dmi_empty_string;
}

static const char * __init dmi_string(const struct dmi_header *dm, u8 s)
{
	const char *bp = dmi_string_nosave(dm, s);
	char *str;
	size_t len;

	if (bp == dmi_empty_string)
		return dmi_empty_string;

	len = strlen(bp) + 1;
	str = dmi_alloc(len);
	if (str != NULL)
		strcpy(str, bp);

	return str;
}

/*
 *	We have to be cautious here. We have seen BIOSes with DMI pointers
 *	pointing to completely the wrong place for example
 */
static void dmi_decode_table(u8 *buf,
			     void (*decode)(const struct dmi_header *, void *),
			     void *private_data)
{
	u8 *data = buf;
	int i = 0;

	/*
	 * Stop when we have seen all the items the table claimed to have
	 * (SMBIOS < 3.0 only) OR we reach an end-of-table marker (SMBIOS
	 * >= 3.0 only) OR we run off the end of the table (should never
	 * happen but sometimes does on bogus implementations.)
	 */
	while ((!dmi_num || i < dmi_num) &&
	       (data - buf + sizeof(struct dmi_header)) <= dmi_len) {
		const struct dmi_header *dm = (const struct dmi_header *)data;

		/*
		 *  We want to know the total length (formatted area and
		 *  strings) before decoding to make sure we won't run off the
		 *  table in dmi_decode or dmi_string
		 */
		data += dm->length;
		while ((data - buf < dmi_len - 1) && (data[0] || data[1]))
			data++;
		if (data - buf < dmi_len - 1)
			decode(dm, private_data);

		data += 2;
		i++;

		/*
		 * 7.45 End-of-Table (Type 127) [SMBIOS reference spec v3.0.0]
		 * For tables behind a 64-bit entry point, we have no item
		 * count and no exact table length, so stop on end-of-table
		 * marker. For tables behind a 32-bit entry point, we have
		 * seen OEM structures behind the end-of-table marker on
		 * some systems, so don't trust it.
		 */
		if (!dmi_num && dm->type == DMI_ENTRY_END_OF_TABLE)
			break;
	}

	/* Trim DMI table length if needed */
	if (dmi_len > data - buf)
		dmi_len = data - buf;
}

static phys_addr_t dmi_base;

static int __init dmi_walk_early(void (*decode)(const struct dmi_header *,
		void *))
{
	u8 *buf;
	u32 orig_dmi_len = dmi_len;

	buf = dmi_early_remap(dmi_base, orig_dmi_len);
	if (buf == NULL)
		return -ENOMEM;

	dmi_decode_table(buf, decode, NULL);

	add_device_randomness(buf, dmi_len);

	dmi_early_unmap(buf, orig_dmi_len);
	return 0;
}

static int __init dmi_checksum(const u8 *buf, u8 len)
{
	u8 sum = 0;
	int a;

	for (a = 0; a < len; a++)
		sum += buf[a];

	return sum == 0;
}

static const char *dmi_ident[DMI_STRING_MAX];
static LIST_HEAD(dmi_devices);
int dmi_available;

/*
 *	Save a DMI string
 */
static void __init dmi_save_ident(const struct dmi_header *dm, int slot,
		int string)
{
	const char *d = (const char *) dm;
	const char *p;

	if (dmi_ident[slot] || dm->length <= string)
		return;

	p = dmi_string(dm, d[string]);
	if (p == NULL)
		return;

	dmi_ident[slot] = p;
}

static void __init dmi_save_uuid(const struct dmi_header *dm, int slot,
		int index)
{
	const u8 *d;
	char *s;
	int is_ff = 1, is_00 = 1, i;

	if (dmi_ident[slot] || dm->length < index + 16)
		return;

	d = (u8 *) dm + index;
	for (i = 0; i < 16 && (is_ff || is_00); i++) {
		if (d[i] != 0x00)
			is_00 = 0;
		if (d[i] != 0xFF)
			is_ff = 0;
	}

	if (is_ff || is_00)
		return;

	s = dmi_alloc(16*2+4+1);
	if (!s)
		return;

	/*
	 * As of version 2.6 of the SMBIOS specification, the first 3 fields of
	 * the UUID are supposed to be little-endian encoded.  The specification
	 * says that this is the defacto standard.
	 */
	if (dmi_ver >= 0x020600)
		sprintf(s, "%pUl", d);
	else
		sprintf(s, "%pUb", d);

	dmi_ident[slot] = s;
}

static void __init dmi_save_type(const struct dmi_header *dm, int slot,
		int index)
{
	const u8 *d;
	char *s;

	if (dmi_ident[slot] || dm->length <= index)
		return;

	s = dmi_alloc(4);
	if (!s)
		return;

	d = (u8 *) dm + index;
	sprintf(s, "%u", *d & 0x7F);
	dmi_ident[slot] = s;
}

static void __init dmi_save_one_device(int type, const char *name)
{
	struct dmi_device *dev;

	/* No duplicate device */
	if (dmi_find_device(type, name, NULL))
		return;

	dev = dmi_alloc(sizeof(*dev) + strlen(name) + 1);
	if (!dev)
		return;

	dev->type = type;
	strcpy((char *)(dev + 1), name);
	dev->name = (char *)(dev + 1);
	dev->device_data = NULL;
	list_add(&dev->list, &dmi_devices);
}

static void __init dmi_save_devices(const struct dmi_header *dm)
{
	int i, count = (dm->length - sizeof(struct dmi_header)) / 2;

	for (i = 0; i < count; i++) {
		const char *d = (char *)(dm + 1) + (i * 2);

		/* Skip disabled device */
		if ((*d & 0x80) == 0)
			continue;

		dmi_save_one_device(*d & 0x7f, dmi_string_nosave(dm, *(d + 1)));
	}
}

static void __init dmi_save_oem_strings_devices(const struct dmi_header *dm)
{
	int i, count;
	struct dmi_device *dev;

	if (dm->length < 0x05)
		return;

	count = *(u8 *)(dm + 1);
	for (i = 1; i <= count; i++) {
		const char *devname = dmi_string(dm, i);

		if (devname == dmi_empty_string)
			continue;

		dev = dmi_alloc(sizeof(*dev));
		if (!dev)
			break;

		dev->type = DMI_DEV_TYPE_OEM_STRING;
		dev->name = devname;
		dev->device_data = NULL;

		list_add(&dev->list, &dmi_devices);
	}
}

static void __init dmi_save_ipmi_device(const struct dmi_header *dm)
{
	struct dmi_device *dev;
	void *data;

	data = dmi_alloc(dm->length);
	if (data == NULL)
		return;

	memcpy(data, dm, dm->length);

	dev = dmi_alloc(sizeof(*dev));
	if (!dev)
		return;

	dev->type = DMI_DEV_TYPE_IPMI;
	dev->name = "IPMI controller";
	dev->device_data = data;

	list_add_tail(&dev->list, &dmi_devices);
}

static void __init dmi_save_dev_pciaddr(int instance, int segment, int bus,
					int devfn, const char *name, int type)
{
	struct dmi_dev_onboard *dev;

	/* Ignore invalid values */
	if (type == DMI_DEV_TYPE_DEV_SLOT &&
	    segment == 0xFFFF && bus == 0xFF && devfn == 0xFF)
		return;

	dev = dmi_alloc(sizeof(*dev) + strlen(name) + 1);
	if (!dev)
		return;

	dev->instance = instance;
	dev->segment = segment;
	dev->bus = bus;
	dev->devfn = devfn;

	strcpy((char *)&dev[1], name);
	dev->dev.type = type;
	dev->dev.name = (char *)&dev[1];
	dev->dev.device_data = dev;

	list_add(&dev->dev.list, &dmi_devices);
}

static void __init dmi_save_extended_devices(const struct dmi_header *dm)
{
	const char *name;
	const u8 *d = (u8 *)dm;

	if (dm->length < 0x0B)
		return;

	/* Skip disabled device */
	if ((d[0x5] & 0x80) == 0)
		return;

	name = dmi_string_nosave(dm, d[0x4]);
	dmi_save_dev_pciaddr(d[0x6], *(u16 *)(d + 0x7), d[0x9], d[0xA], name,
			     DMI_DEV_TYPE_DEV_ONBOARD);
	dmi_save_one_device(d[0x5] & 0x7f, name);
}

static void __init dmi_save_system_slot(const struct dmi_header *dm)
{
	const u8 *d = (u8 *)dm;

	/* Need SMBIOS 2.6+ structure */
	if (dm->length < 0x11)
		return;
	dmi_save_dev_pciaddr(*(u16 *)(d + 0x9), *(u16 *)(d + 0xD), d[0xF],
			     d[0x10], dmi_string_nosave(dm, d[0x4]),
			     DMI_DEV_TYPE_DEV_SLOT);
}

static void __init count_mem_devices(const struct dmi_header *dm, void *v)
{
	if (dm->type != DMI_ENTRY_MEM_DEVICE)
		return;
	dmi_memdev_nr++;
}

static void __init save_mem_devices(const struct dmi_header *dm, void *v)
{
	const char *d = (const char *)dm;
	static int nr;
	u64 bytes;
	u16 size;

	if (dm->type != DMI_ENTRY_MEM_DEVICE || dm->length < 0x12)
		return;
	if (nr >= dmi_memdev_nr) {
		pr_warn(FW_BUG "Too many DIMM entries in SMBIOS table\n");
		return;
	}
	dmi_memdev[nr].handle = get_unaligned(&dm->handle);
	dmi_memdev[nr].device = dmi_string(dm, d[0x10]);
	dmi_memdev[nr].bank = dmi_string(dm, d[0x11]);

	size = get_unaligned((u16 *)&d[0xC]);
	if (size == 0)
		bytes = 0;
	else if (size == 0xffff)
		bytes = ~0ull;
	else if (size & 0x8000)
		bytes = (u64)(size & 0x7fff) << 10;
	else if (size != 0x7fff)
		bytes = (u64)size << 20;
	else
		bytes = (u64)get_unaligned((u32 *)&d[0x1C]) << 20;

	dmi_memdev[nr].size = bytes;
	nr++;
}

void __init dmi_memdev_walk(void)
{
	if (!dmi_available)
		return;

	if (dmi_walk_early(count_mem_devices) == 0 && dmi_memdev_nr) {
		dmi_memdev = dmi_alloc(sizeof(*dmi_memdev) * dmi_memdev_nr);
		if (dmi_memdev)
			dmi_walk_early(save_mem_devices);
	}
}

/*
 *	Process a DMI table entry. Right now all we care about are the BIOS
 *	and machine entries. For 2.5 we should pull the smbus controller info
 *	out of here.
 */
static void __init dmi_decode(const struct dmi_header *dm, void *dummy)
{
	switch (dm->type) {
	case 0:		/* BIOS Information */
		dmi_save_ident(dm, DMI_BIOS_VENDOR, 4);
		dmi_save_ident(dm, DMI_BIOS_VERSION, 5);
		dmi_save_ident(dm, DMI_BIOS_DATE, 8);
		break;
	case 1:		/* System Information */
		dmi_save_ident(dm, DMI_SYS_VENDOR, 4);
		dmi_save_ident(dm, DMI_PRODUCT_NAME, 5);
		dmi_save_ident(dm, DMI_PRODUCT_VERSION, 6);
		dmi_save_ident(dm, DMI_PRODUCT_SERIAL, 7);
		dmi_save_uuid(dm, DMI_PRODUCT_UUID, 8);
		dmi_save_ident(dm, DMI_PRODUCT_SKU, 25);
		dmi_save_ident(dm, DMI_PRODUCT_FAMILY, 26);
		break;
	case 2:		/* Base Board Information */
		dmi_save_ident(dm, DMI_BOARD_VENDOR, 4);
		dmi_save_ident(dm, DMI_BOARD_NAME, 5);
		dmi_save_ident(dm, DMI_BOARD_VERSION, 6);
		dmi_save_ident(dm, DMI_BOARD_SERIAL, 7);
		dmi_save_ident(dm, DMI_BOARD_ASSET_TAG, 8);
		break;
	case 3:		/* Chassis Information */
		dmi_save_ident(dm, DMI_CHASSIS_VENDOR, 4);
		dmi_save_type(dm, DMI_CHASSIS_TYPE, 5);
		dmi_save_ident(dm, DMI_CHASSIS_VERSION, 6);
		dmi_save_ident(dm, DMI_CHASSIS_SERIAL, 7);
		dmi_save_ident(dm, DMI_CHASSIS_ASSET_TAG, 8);
		break;
	case 9:		/* System Slots */
		dmi_save_system_slot(dm);
		break;
	case 10:	/* Onboard Devices Information */
		dmi_save_devices(dm);
		break;
	case 11:	/* OEM Strings */
		dmi_save_oem_strings_devices(dm);
		break;
	case 38:	/* IPMI Device Information */
		dmi_save_ipmi_device(dm);
		break;
	case 41:	/* Onboard Devices Extended Information */
		dmi_save_extended_devices(dm);
	}
}

static int __init print_filtered(char *buf, size_t len, const char *info)
{
	int c = 0;
	const char *p;

	if (!info)
		return c;

	for (p = info; *p; p++)
		if (isprint(*p))
			c += scnprintf(buf + c, len - c, "%c", *p);
		else
			c += scnprintf(buf + c, len - c, "\\x%02x", *p & 0xff);
	return c;
}

static void __init dmi_format_ids(char *buf, size_t len)
{
	int c = 0;
	const char *board;	/* Board Name is optional */

	c += print_filtered(buf + c, len - c,
			    dmi_get_system_info(DMI_SYS_VENDOR));
	c += scnprintf(buf + c, len - c, " ");
	c += print_filtered(buf + c, len - c,
			    dmi_get_system_info(DMI_PRODUCT_NAME));

	board = dmi_get_system_info(DMI_BOARD_NAME);
	if (board) {
		c += scnprintf(buf + c, len - c, "/");
		c += print_filtered(buf + c, len - c, board);
	}
	c += scnprintf(buf + c, len - c, ", BIOS ");
	c += print_filtered(buf + c, len - c,
			    dmi_get_system_info(DMI_BIOS_VERSION));
	c += scnprintf(buf + c, len - c, " ");
	c += print_filtered(buf + c, len - c,
			    dmi_get_system_info(DMI_BIOS_DATE));
}

/*
 * Check for DMI/SMBIOS headers in the system firmware image.  Any
 * SMBIOS header must start 16 bytes before the DMI header, so take a
 * 32 byte buffer and check for DMI at offset 16 and SMBIOS at offset
 * 0.  If the DMI header is present, set dmi_ver accordingly (SMBIOS
 * takes precedence) and return 0.  Otherwise return 1.
 */
static int __init dmi_present(const u8 *buf)
{
	u32 smbios_ver;

	if (memcmp(buf, "_SM_", 4) == 0 &&
	    buf[5] < 32 && dmi_checksum(buf, buf[5])) {
		smbios_ver = get_unaligned_be16(buf + 6);
		smbios_entry_point_size = buf[5];
		memcpy(smbios_entry_point, buf, smbios_entry_point_size);

		/* Some BIOS report weird SMBIOS version, fix that up */
		switch (smbios_ver) {
		case 0x021F:
		case 0x0221:
			pr_debug("SMBIOS version fixup (2.%d->2.%d)\n",
				 smbios_ver & 0xFF, 3);
			smbios_ver = 0x0203;
			break;
		case 0x0233:
			pr_debug("SMBIOS version fixup (2.%d->2.%d)\n", 51, 6);
			smbios_ver = 0x0206;
			break;
		}
	} else {
		smbios_ver = 0;
	}

	buf += 16;

	if (memcmp(buf, "_DMI_", 5) == 0 && dmi_checksum(buf, 15)) {
		if (smbios_ver)
			dmi_ver = smbios_ver;
		else
			dmi_ver = (buf[14] & 0xF0) << 4 | (buf[14] & 0x0F);
		dmi_ver <<= 8;
		dmi_num = get_unaligned_le16(buf + 12);
		dmi_len = get_unaligned_le16(buf + 6);
		dmi_base = get_unaligned_le32(buf + 8);

		if (dmi_walk_early(dmi_decode) == 0) {
			if (smbios_ver) {
				pr_info("SMBIOS %d.%d present.\n",
					dmi_ver >> 16, (dmi_ver >> 8) & 0xFF);
			} else {
				smbios_entry_point_size = 15;
				memcpy(smbios_entry_point, buf,
				       smbios_entry_point_size);
				pr_info("Legacy DMI %d.%d present.\n",
					dmi_ver >> 16, (dmi_ver >> 8) & 0xFF);
			}
			dmi_format_ids(dmi_ids_string, sizeof(dmi_ids_string));
			pr_info("DMI: %s\n", dmi_ids_string);
			return 0;
		}
	}

	return 1;
}

/*
 * Check for the SMBIOS 3.0 64-bit entry point signature. Unlike the legacy
 * 32-bit entry point, there is no embedded DMI header (_DMI_) in here.
 */
static int __init dmi_smbios3_present(const u8 *buf)
{
	if (memcmp(buf, "_SM3_", 5) == 0 &&
	    buf[6] < 32 && dmi_checksum(buf, buf[6])) {
		dmi_ver = get_unaligned_be32(buf + 6) & 0xFFFFFF;
		dmi_num = 0;			/* No longer specified */
		dmi_len = get_unaligned_le32(buf + 12);
		dmi_base = get_unaligned_le64(buf + 16);
		smbios_entry_point_size = buf[6];
		memcpy(smbios_entry_point, buf, smbios_entry_point_size);

		if (dmi_walk_early(dmi_decode) == 0) {
			pr_info("SMBIOS %d.%d.%d present.\n",
				dmi_ver >> 16, (dmi_ver >> 8) & 0xFF,
				dmi_ver & 0xFF);
			dmi_format_ids(dmi_ids_string, sizeof(dmi_ids_string));
			pr_info("DMI: %s\n", dmi_ids_string);
			return 0;
		}
	}
	return 1;
}

void __init dmi_scan_machine(void)
{
	char __iomem *p, *q;
	char buf[32];

	if (efi_enabled(EFI_CONFIG_TABLES)) {
		/*
		 * According to the DMTF SMBIOS reference spec v3.0.0, it is
		 * allowed to define both the 64-bit entry point (smbios3) and
		 * the 32-bit entry point (smbios), in which case they should
		 * either both point to the same SMBIOS structure table, or the
		 * table pointed to by the 64-bit entry point should contain a
		 * superset of the table contents pointed to by the 32-bit entry
		 * point (section 5.2)
		 * This implies that the 64-bit entry point should have
		 * precedence if it is defined and supported by the OS. If we
		 * have the 64-bit entry point, but fail to decode it, fall
		 * back to the legacy one (if available)
		 */
		if (efi.smbios3 != EFI_INVALID_TABLE_ADDR) {
			p = dmi_early_remap(efi.smbios3, 32);
			if (p == NULL)
				goto error;
			memcpy_fromio(buf, p, 32);
			dmi_early_unmap(p, 32);

			if (!dmi_smbios3_present(buf)) {
				dmi_available = 1;
				return;
			}
		}
		if (efi.smbios == EFI_INVALID_TABLE_ADDR)
			goto error;

		/* This is called as a core_initcall() because it isn't
		 * needed during early boot.  This also means we can
		 * iounmap the space when we're done with it.
		 */
		p = dmi_early_remap(efi.smbios, 32);
		if (p == NULL)
			goto error;
		memcpy_fromio(buf, p, 32);
		dmi_early_unmap(p, 32);

		if (!dmi_present(buf)) {
			dmi_available = 1;
			return;
		}
	} else if (IS_ENABLED(CONFIG_DMI_SCAN_MACHINE_NON_EFI_FALLBACK)) {
		p = dmi_early_remap(0xF0000, 0x10000);
		if (p == NULL)
			goto error;

		/*
		 * Same logic as above, look for a 64-bit entry point
		 * first, and if not found, fall back to 32-bit entry point.
		 */
		memcpy_fromio(buf, p, 16);
		for (q = p + 16; q < p + 0x10000; q += 16) {
			memcpy_fromio(buf + 16, q, 16);
			if (!dmi_smbios3_present(buf)) {
				dmi_available = 1;
				dmi_early_unmap(p, 0x10000);
				return;
			}
			memcpy(buf, buf + 16, 16);
		}

		/*
		 * Iterate over all possible DMI header addresses q.
		 * Maintain the 32 bytes around q in buf.  On the
		 * first iteration, substitute zero for the
		 * out-of-range bytes so there is no chance of falsely
		 * detecting an SMBIOS header.
		 */
		memset(buf, 0, 16);
		for (q = p; q < p + 0x10000; q += 16) {
			memcpy_fromio(buf + 16, q, 16);
			if (!dmi_present(buf)) {
				dmi_available = 1;
				dmi_early_unmap(p, 0x10000);
				return;
			}
			memcpy(buf, buf + 16, 16);
		}
		dmi_early_unmap(p, 0x10000);
	}
 error:
	pr_info("DMI not present or invalid.\n");
}

static ssize_t raw_table_read(struct file *file, struct kobject *kobj,
			      struct bin_attribute *attr, char *buf,
			      loff_t pos, size_t count)
{
	memcpy(buf, attr->private + pos, count);
	return count;
}

static BIN_ATTR(smbios_entry_point, S_IRUSR, raw_table_read, NULL, 0);
static BIN_ATTR(DMI, S_IRUSR, raw_table_read, NULL, 0);

static int __init dmi_init(void)
{
	struct kobject *tables_kobj;
	u8 *dmi_table;
	int ret = -ENOMEM;

	if (!dmi_available)
		return 0;

	/*
	 * Set up dmi directory at /sys/firmware/dmi. This entry should stay
	 * even after farther error, as it can be used by other modules like
	 * dmi-sysfs.
	 */
	dmi_kobj = kobject_create_and_add("dmi", firmware_kobj);
	if (!dmi_kobj)
		goto err;

	tables_kobj = kobject_create_and_add("tables", dmi_kobj);
	if (!tables_kobj)
		goto err;

	dmi_table = dmi_remap(dmi_base, dmi_len);
	if (!dmi_table)
		goto err_tables;

	bin_attr_smbios_entry_point.size = smbios_entry_point_size;
	bin_attr_smbios_entry_point.private = smbios_entry_point;
	ret = sysfs_create_bin_file(tables_kobj, &bin_attr_smbios_entry_point);
	if (ret)
		goto err_unmap;

	bin_attr_DMI.size = dmi_len;
	bin_attr_DMI.private = dmi_table;
	ret = sysfs_create_bin_file(tables_kobj, &bin_attr_DMI);
	if (!ret)
		return 0;

	sysfs_remove_bin_file(tables_kobj,
			      &bin_attr_smbios_entry_point);
 err_unmap:
	dmi_unmap(dmi_table);
 err_tables:
	kobject_del(tables_kobj);
	kobject_put(tables_kobj);
 err:
	pr_err("dmi: Firmware registration failed.\n");

	return ret;
}
subsys_initcall(dmi_init);

/**
 * dmi_set_dump_stack_arch_desc - set arch description for dump_stack()
 *
 * Invoke dump_stack_set_arch_desc() with DMI system information so that
 * DMI identifiers are printed out on task dumps.  Arch boot code should
 * call this function after dmi_scan_machine() if it wants to print out DMI
 * identifiers on task dumps.
 */
void __init dmi_set_dump_stack_arch_desc(void)
{
	dump_stack_set_arch_desc("%s", dmi_ids_string);
}

/**
 *	dmi_matches - check if dmi_system_id structure matches system DMI data
 *	@dmi: pointer to the dmi_system_id structure to check
 */
static bool dmi_matches(const struct dmi_system_id *dmi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dmi->matches); i++) {
		int s = dmi->matches[i].slot;
		if (s == DMI_NONE)
			break;
		if (s == DMI_OEM_STRING) {
			/* DMI_OEM_STRING must be exact match */
			const struct dmi_device *valid;

			valid = dmi_find_device(DMI_DEV_TYPE_OEM_STRING,
						dmi->matches[i].substr, NULL);
			if (valid)
				continue;
		} else if (dmi_ident[s]) {
			if (dmi->matches[i].exact_match) {
				if (!strcmp(dmi_ident[s],
					    dmi->matches[i].substr))
					continue;
			} else {
				if (strstr(dmi_ident[s],
					   dmi->matches[i].substr))
					continue;
			}
		}

		/* No match */
		return false;
	}
	return true;
}

/**
 *	dmi_is_end_of_table - check for end-of-table marker
 *	@dmi: pointer to the dmi_system_id structure to check
 */
static bool dmi_is_end_of_table(const struct dmi_system_id *dmi)
{
	return dmi->matches[0].slot == DMI_NONE;
}

/**
 *	dmi_check_system - check system DMI data
 *	@list: array of dmi_system_id structures to match against
 *		All non-null elements of the list must match
 *		their slot's (field index's) data (i.e., each
 *		list string must be a substring of the specified
 *		DMI slot's string data) to be considered a
 *		successful match.
 *
 *	Walk the blacklist table running matching functions until someone
 *	returns non zero or we hit the end. Callback function is called for
 *	each successful match. Returns the number of matches.
 *
 *	dmi_scan_machine must be called before this function is called.
 */
int dmi_check_system(const struct dmi_system_id *list)
{
	int count = 0;
	const struct dmi_system_id *d;

	for (d = list; !dmi_is_end_of_table(d); d++)
		if (dmi_matches(d)) {
			count++;
			if (d->callback && d->callback(d))
				break;
		}

	return count;
}
EXPORT_SYMBOL(dmi_check_system);

/**
 *	dmi_first_match - find dmi_system_id structure matching system DMI data
 *	@list: array of dmi_system_id structures to match against
 *		All non-null elements of the list must match
 *		their slot's (field index's) data (i.e., each
 *		list string must be a substring of the specified
 *		DMI slot's string data) to be considered a
 *		successful match.
 *
 *	Walk the blacklist table until the first match is found.  Return the
 *	pointer to the matching entry or NULL if there's no match.
 *
 *	dmi_scan_machine must be called before this function is called.
 */
const struct dmi_system_id *dmi_first_match(const struct dmi_system_id *list)
{
	const struct dmi_system_id *d;

	for (d = list; !dmi_is_end_of_table(d); d++)
		if (dmi_matches(d))
			return d;

	return NULL;
}
EXPORT_SYMBOL(dmi_first_match);

/**
 *	dmi_get_system_info - return DMI data value
 *	@field: data index (see enum dmi_field)
 *
 *	Returns one DMI data value, can be used to perform
 *	complex DMI data checks.
 */
const char *dmi_get_system_info(int field)
{
	return dmi_ident[field];
}
EXPORT_SYMBOL(dmi_get_system_info);

/**
 * dmi_name_in_serial - Check if string is in the DMI product serial information
 * @str: string to check for
 */
int dmi_name_in_serial(const char *str)
{
	int f = DMI_PRODUCT_SERIAL;
	if (dmi_ident[f] && strstr(dmi_ident[f], str))
		return 1;
	return 0;
}

/**
 *	dmi_name_in_vendors - Check if string is in the DMI system or board vendor name
 *	@str: Case sensitive Name
 */
int dmi_name_in_vendors(const char *str)
{
	static int fields[] = { DMI_SYS_VENDOR, DMI_BOARD_VENDOR, DMI_NONE };
	int i;
	for (i = 0; fields[i] != DMI_NONE; i++) {
		int f = fields[i];
		if (dmi_ident[f] && strstr(dmi_ident[f], str))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(dmi_name_in_vendors);

/**
 *	dmi_find_device - find onboard device by type/name
 *	@type: device type or %DMI_DEV_TYPE_ANY to match all device types
 *	@name: device name string or %NULL to match all
 *	@from: previous device found in search, or %NULL for new search.
 *
 *	Iterates through the list of known onboard devices. If a device is
 *	found with a matching @type and @name, a pointer to its device
 *	structure is returned.  Otherwise, %NULL is returned.
 *	A new search is initiated by passing %NULL as the @from argument.
 *	If @from is not %NULL, searches continue from next device.
 */
const struct dmi_device *dmi_find_device(int type, const char *name,
				    const struct dmi_device *from)
{
	const struct list_head *head = from ? &from->list : &dmi_devices;
	struct list_head *d;

	for (d = head->next; d != &dmi_devices; d = d->next) {
		const struct dmi_device *dev =
			list_entry(d, struct dmi_device, list);

		if (((type == DMI_DEV_TYPE_ANY) || (dev->type == type)) &&
		    ((name == NULL) || (strcmp(dev->name, name) == 0)))
			return dev;
	}

	return NULL;
}
EXPORT_SYMBOL(dmi_find_device);

/**
 *	dmi_get_date - parse a DMI date
 *	@field:	data index (see enum dmi_field)
 *	@yearp: optional out parameter for the year
 *	@monthp: optional out parameter for the month
 *	@dayp: optional out parameter for the day
 *
 *	The date field is assumed to be in the form resembling
 *	[mm[/dd]]/yy[yy] and the result is stored in the out
 *	parameters any or all of which can be omitted.
 *
 *	If the field doesn't exist, all out parameters are set to zero
 *	and false is returned.  Otherwise, true is returned with any
 *	invalid part of date set to zero.
 *
 *	On return, year, month and day are guaranteed to be in the
 *	range of [0,9999], [0,12] and [0,31] respectively.
 */
bool dmi_get_date(int field, int *yearp, int *monthp, int *dayp)
{
	int year = 0, month = 0, day = 0;
	bool exists;
	const char *s, *y;
	char *e;

	s = dmi_get_system_info(field);
	exists = s;
	if (!exists)
		goto out;

	/*
	 * Determine year first.  We assume the date string resembles
	 * mm/dd/yy[yy] but the original code extracted only the year
	 * from the end.  Keep the behavior in the spirit of no
	 * surprises.
	 */
	y = strrchr(s, '/');
	if (!y)
		goto out;

	y++;
	year = simple_strtoul(y, &e, 10);
	if (y != e && year < 100) {	/* 2-digit year */
		year += 1900;
		if (year < 1996)	/* no dates < spec 1.0 */
			year += 100;
	}
	if (year > 9999)		/* year should fit in %04d */
		year = 0;

	/* parse the mm and dd */
	month = simple_strtoul(s, &e, 10);
	if (s == e || *e != '/' || !month || month > 12) {
		month = 0;
		goto out;
	}

	s = e + 1;
	day = simple_strtoul(s, &e, 10);
	if (s == y || s == e || *e != '/' || day > 31)
		day = 0;
out:
	if (yearp)
		*yearp = year;
	if (monthp)
		*monthp = month;
	if (dayp)
		*dayp = day;
	return exists;
}
EXPORT_SYMBOL(dmi_get_date);

/**
 *	dmi_get_bios_year - get a year out of DMI_BIOS_DATE field
 *
 *	Returns year on success, -ENXIO if DMI is not selected,
 *	or a different negative error code if DMI field is not present
 *	or not parseable.
 */
int dmi_get_bios_year(void)
{
	bool exists;
	int year;

	exists = dmi_get_date(DMI_BIOS_DATE, &year, NULL, NULL);
	if (!exists)
		return -ENODATA;

	return year ? year : -ERANGE;
}
EXPORT_SYMBOL(dmi_get_bios_year);

/**
 *	dmi_walk - Walk the DMI table and get called back for every record
 *	@decode: Callback function
 *	@private_data: Private data to be passed to the callback function
 *
 *	Returns 0 on success, -ENXIO if DMI is not selected or not present,
 *	or a different negative error code if DMI walking fails.
 */
int dmi_walk(void (*decode)(const struct dmi_header *, void *),
	     void *private_data)
{
	u8 *buf;

	if (!dmi_available)
		return -ENXIO;

	buf = dmi_remap(dmi_base, dmi_len);
	if (buf == NULL)
		return -ENOMEM;

	dmi_decode_table(buf, decode, private_data);

	dmi_unmap(buf);
	return 0;
}
EXPORT_SYMBOL_GPL(dmi_walk);

/**
 * dmi_match - compare a string to the dmi field (if exists)
 * @f: DMI field identifier
 * @str: string to compare the DMI field to
 *
 * Returns true if the requested field equals to the str (including NULL).
 */
bool dmi_match(enum dmi_field f, const char *str)
{
	const char *info = dmi_get_system_info(f);

	if (info == NULL || str == NULL)
		return info == str;

	return !strcmp(info, str);
}
EXPORT_SYMBOL_GPL(dmi_match);

void dmi_memdev_name(u16 handle, const char **bank, const char **device)
{
	int n;

	if (dmi_memdev == NULL)
		return;

	for (n = 0; n < dmi_memdev_nr; n++) {
		if (handle == dmi_memdev[n].handle) {
			*bank = dmi_memdev[n].bank;
			*device = dmi_memdev[n].device;
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(dmi_memdev_name);

u64 dmi_memdev_size(u16 handle)
{
	int n;

	if (dmi_memdev) {
		for (n = 0; n < dmi_memdev_nr; n++) {
			if (handle == dmi_memdev[n].handle)
				return dmi_memdev[n].size;
		}
	}
	return ~0ull;
}
EXPORT_SYMBOL_GPL(dmi_memdev_size);
