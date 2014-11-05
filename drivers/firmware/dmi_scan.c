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

/*
 * DMI stands for "Desktop Management Interface".  It is part
 * of and an antecedent to, SMBIOS, which stands for System
 * Management BIOS.  See further: http://www.dmtf.org/standards
 */
static const char dmi_empty_string[] = "        ";

static u16 __initdata dmi_ver;
/*
 * Catch too early calls to dmi_check_system():
 */
static int dmi_initialized;

/* DMI system identification string used during boot */
static char dmi_ids_string[128] __initdata;

static struct dmi_memdev_info {
	const char *device;
	const char *bank;
	u16 handle;
} *dmi_memdev;
static int dmi_memdev_nr;

static const char * __init dmi_string_nosave(const struct dmi_header *dm, u8 s)
{
	const u8 *bp = ((u8 *) dm) + dm->length;

	if (s) {
		s--;
		while (s > 0 && *bp) {
			bp += strlen(bp) + 1;
			s--;
		}

		if (*bp != 0) {
			size_t len = strlen(bp)+1;
			size_t cmp_len = len > 8 ? 8 : len;

			if (!memcmp(bp, dmi_empty_string, cmp_len))
				return dmi_empty_string;
			return bp;
		}
	}

	return "";
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
static void dmi_table(u8 *buf, int len, int num,
		      void (*decode)(const struct dmi_header *, void *),
		      void *private_data)
{
	u8 *data = buf;
	int i = 0;

	/*
	 *	Stop when we see all the items the table claimed to have
	 *	OR we run off the end of the table (also happens)
	 */
	while ((i < num) && (data - buf + sizeof(struct dmi_header)) <= len) {
		const struct dmi_header *dm = (const struct dmi_header *)data;

		/*
		 * 7.45 End-of-Table (Type 127) [SMBIOS reference spec v3.0.0]
		 */
		if (dm->type == DMI_ENTRY_END_OF_TABLE)
			break;

		/*
		 *  We want to know the total length (formatted area and
		 *  strings) before decoding to make sure we won't run off the
		 *  table in dmi_decode or dmi_string
		 */
		data += dm->length;
		while ((data - buf < len - 1) && (data[0] || data[1]))
			data++;
		if (data - buf < len - 1)
			decode(dm, private_data);
		data += 2;
		i++;
	}
}

static phys_addr_t dmi_base;
static u16 dmi_len;
static u16 dmi_num;

static int __init dmi_walk_early(void (*decode)(const struct dmi_header *,
		void *))
{
	u8 *buf;

	buf = dmi_early_remap(dmi_base, dmi_len);
	if (buf == NULL)
		return -1;

	dmi_table(buf, dmi_len, dmi_num, decode, NULL);

	add_device_randomness(buf, dmi_len);

	dmi_early_unmap(buf, dmi_len);
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

	if (dmi_ident[slot])
		return;

	p = dmi_string(dm, d[string]);
	if (p == NULL)
		return;

	dmi_ident[slot] = p;
}

static void __init dmi_save_uuid(const struct dmi_header *dm, int slot,
		int index)
{
	const u8 *d = (u8 *) dm + index;
	char *s;
	int is_ff = 1, is_00 = 1, i;

	if (dmi_ident[slot])
		return;

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
	if (dmi_ver >= 0x0206)
		sprintf(s, "%pUL", d);
	else
		sprintf(s, "%pUB", d);

	dmi_ident[slot] = s;
}

static void __init dmi_save_type(const struct dmi_header *dm, int slot,
		int index)
{
	const u8 *d = (u8 *) dm + index;
	char *s;

	if (dmi_ident[slot])
		return;

	s = dmi_alloc(4);
	if (!s)
		return;

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
	int i, count = *(u8 *)(dm + 1);
	struct dmi_device *dev;

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

static void __init dmi_save_dev_onboard(int instance, int segment, int bus,
					int devfn, const char *name)
{
	struct dmi_dev_onboard *onboard_dev;

	onboard_dev = dmi_alloc(sizeof(*onboard_dev) + strlen(name) + 1);
	if (!onboard_dev)
		return;

	onboard_dev->instance = instance;
	onboard_dev->segment = segment;
	onboard_dev->bus = bus;
	onboard_dev->devfn = devfn;

	strcpy((char *)&onboard_dev[1], name);
	onboard_dev->dev.type = DMI_DEV_TYPE_DEV_ONBOARD;
	onboard_dev->dev.name = (char *)&onboard_dev[1];
	onboard_dev->dev.device_data = onboard_dev;

	list_add(&onboard_dev->dev.list, &dmi_devices);
}

static void __init dmi_save_extended_devices(const struct dmi_header *dm)
{
	const u8 *d = (u8 *) dm + 5;

	/* Skip disabled device */
	if ((*d & 0x80) == 0)
		return;

	dmi_save_dev_onboard(*(d+1), *(u16 *)(d+2), *(d+4), *(d+5),
			     dmi_string_nosave(dm, *(d-1)));
	dmi_save_one_device(*d & 0x7f, dmi_string_nosave(dm, *(d - 1)));
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

	if (dm->type != DMI_ENTRY_MEM_DEVICE)
		return;
	if (nr >= dmi_memdev_nr) {
		pr_warn(FW_BUG "Too many DIMM entries in SMBIOS table\n");
		return;
	}
	dmi_memdev[nr].handle = get_unaligned(&dm->handle);
	dmi_memdev[nr].device = dmi_string(dm, d[0x10]);
	dmi_memdev[nr].bank = dmi_string(dm, d[0x11]);
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
	int smbios_ver;

	if (memcmp(buf, "_SM_", 4) == 0 &&
	    buf[5] < 32 && dmi_checksum(buf, buf[5])) {
		smbios_ver = get_unaligned_be16(buf + 6);

		/* Some BIOS report weird SMBIOS version, fix that up */
		switch (smbios_ver) {
		case 0x021F:
		case 0x0221:
			pr_debug("SMBIOS version fixup(2.%d->2.%d)\n",
				 smbios_ver & 0xFF, 3);
			smbios_ver = 0x0203;
			break;
		case 0x0233:
			pr_debug("SMBIOS version fixup(2.%d->2.%d)\n", 51, 6);
			smbios_ver = 0x0206;
			break;
		}
	} else {
		smbios_ver = 0;
	}

	buf += 16;

	if (memcmp(buf, "_DMI_", 5) == 0 && dmi_checksum(buf, 15)) {
		dmi_num = get_unaligned_le16(buf + 12);
		dmi_len = get_unaligned_le16(buf + 6);
		dmi_base = get_unaligned_le32(buf + 8);

		if (dmi_walk_early(dmi_decode) == 0) {
			if (smbios_ver) {
				dmi_ver = smbios_ver;
				pr_info("SMBIOS %d.%d present.\n",
				       dmi_ver >> 8, dmi_ver & 0xFF);
			} else {
				dmi_ver = (buf[14] & 0xF0) << 4 |
					   (buf[14] & 0x0F);
				pr_info("Legacy DMI %d.%d present.\n",
				       dmi_ver >> 8, dmi_ver & 0xFF);
			}
			dmi_format_ids(dmi_ids_string, sizeof(dmi_ids_string));
			printk(KERN_DEBUG "DMI: %s\n", dmi_ids_string);
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
		dmi_ver = get_unaligned_be16(buf + 7);
		dmi_len = get_unaligned_le32(buf + 12);
		dmi_base = get_unaligned_le64(buf + 16);

		/*
		 * The 64-bit SMBIOS 3.0 entry point no longer has a field
		 * containing the number of structures present in the table.
		 * Instead, it defines the table size as a maximum size, and
		 * relies on the end-of-table structure type (#127) to be used
		 * to signal the end of the table.
		 * So let's define dmi_num as an upper bound as well: each
		 * structure has a 4 byte header, so dmi_len / 4 is an upper
		 * bound for the number of structures in the table.
		 */
		dmi_num = dmi_len / 4;

		if (dmi_walk_early(dmi_decode) == 0) {
			pr_info("SMBIOS %d.%d present.\n",
				dmi_ver >> 8, dmi_ver & 0xFF);
			dmi_format_ids(dmi_ids_string, sizeof(dmi_ids_string));
			pr_debug("DMI: %s\n", dmi_ids_string);
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
				goto out;
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
			goto out;
		}
	} else if (IS_ENABLED(CONFIG_DMI_SCAN_MACHINE_NON_EFI_FALLBACK)) {
		p = dmi_early_remap(0xF0000, 0x10000);
		if (p == NULL)
			goto error;

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
			if (!dmi_smbios3_present(buf) || !dmi_present(buf)) {
				dmi_available = 1;
				dmi_early_unmap(p, 0x10000);
				goto out;
			}
			memcpy(buf, buf + 16, 16);
		}
		dmi_early_unmap(p, 0x10000);
	}
 error:
	pr_info("DMI not present or invalid.\n");
 out:
	dmi_initialized = 1;
}

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

	WARN(!dmi_initialized, KERN_ERR "dmi check: not initialized yet.\n");

	for (i = 0; i < ARRAY_SIZE(dmi->matches); i++) {
		int s = dmi->matches[i].slot;
		if (s == DMI_NONE)
			break;
		if (dmi_ident[s]) {
			if (!dmi->matches[i].exact_match &&
			    strstr(dmi_ident[s], dmi->matches[i].substr))
				continue;
			else if (dmi->matches[i].exact_match &&
				 !strcmp(dmi_ident[s], dmi->matches[i].substr))
				continue;
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
 *	found with a matching @vendor and @device, a pointer to its device
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
 *	dmi_walk - Walk the DMI table and get called back for every record
 *	@decode: Callback function
 *	@private_data: Private data to be passed to the callback function
 *
 *	Returns -1 when the DMI table can't be reached, 0 on success.
 */
int dmi_walk(void (*decode)(const struct dmi_header *, void *),
	     void *private_data)
{
	u8 *buf;

	if (!dmi_available)
		return -1;

	buf = dmi_remap(dmi_base, dmi_len);
	if (buf == NULL)
		return -1;

	dmi_table(buf, dmi_len, dmi_num, decode, private_data);

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
