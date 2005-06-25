#include <linux/types.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/bootmem.h>


struct dmi_header {
	u8 type;
	u8 length;
	u16 handle;
};

#undef DMI_DEBUG

#ifdef DMI_DEBUG
#define dmi_printk(x) printk x
#else
#define dmi_printk(x)
#endif

static char * __init dmi_string(struct dmi_header *dm, u8 s)
{
	u8 *bp = ((u8 *) dm) + dm->length;

	if (!s)
		return "";
	s--;
	while (s > 0 && *bp) {
		bp += strlen(bp) + 1;
		s--;
	}
	return bp;
}

/*
 *	We have to be cautious here. We have seen BIOSes with DMI pointers
 *	pointing to completely the wrong place for example
 */
static int __init dmi_table(u32 base, int len, int num,
			    void (*decode)(struct dmi_header *))
{
	u8 *buf, *data;
	int i = 0;
		
	buf = bt_ioremap(base, len);
	if (buf == NULL)
		return -1;

	data = buf;

	/*
 	 *	Stop when we see all the items the table claimed to have
 	 *	OR we run off the end of the table (also happens)
 	 */
	while ((i < num) && (data - buf + sizeof(struct dmi_header)) <= len) {
		struct dmi_header *dm = (struct dmi_header *)data;
		/*
		 *  We want to know the total length (formated area and strings)
		 *  before decoding to make sure we won't run off the table in
		 *  dmi_decode or dmi_string
		 */
		data += dm->length;
		while ((data - buf < len - 1) && (data[0] || data[1]))
			data++;
		if (data - buf < len - 1)
			decode(dm);
		data += 2;
		i++;
	}
	bt_iounmap(buf, len);
	return 0;
}

static int __init dmi_checksum(u8 *buf)
{
	u8 sum = 0;
	int a;
	
	for (a = 0; a < 15; a++)
		sum += buf[a];

	return sum == 0;
}

static int __init dmi_iterate(void (*decode)(struct dmi_header *))
{
	u8 buf[15];
	char __iomem *p, *q;

	/*
	 * no iounmap() for that ioremap(); it would be a no-op, but it's
	 * so early in setup that sucker gets confused into doing what
	 * it shouldn't if we actually call it.
	 */
	p = ioremap(0xF0000, 0x10000);
	if (p == NULL)
		return -1;

	for (q = p; q < p + 0x10000; q += 16) {
		memcpy_fromio(buf, q, 15);
		if ((memcmp(buf, "_DMI_", 5) == 0) && dmi_checksum(buf)) {
			u16 num = (buf[13] << 8) | buf[12];
			u16 len = (buf[7] << 8) | buf[6];
			u32 base = (buf[11] << 24) | (buf[10] << 16) |
				   (buf[9] << 8) | buf[8];

			/*
			 * DMI version 0.0 means that the real version is taken from
			 * the SMBIOS version, which we don't know at this point.
			 */
			if (buf[14] != 0)
				printk(KERN_INFO "DMI %d.%d present.\n",
					buf[14] >> 4, buf[14] & 0xF);
			else
				printk(KERN_INFO "DMI present.\n");

			dmi_printk((KERN_INFO "%d structures occupying %d bytes.\n",
				num, len));
			dmi_printk((KERN_INFO "DMI table at 0x%08X.\n", base));

			if (dmi_table(base,len, num, decode) == 0)
				return 0;
		}
	}
	return -1;
}

static char *dmi_ident[DMI_STRING_MAX];

/*
 *	Save a DMI string
 */
static void __init dmi_save_ident(struct dmi_header *dm, int slot, int string)
{
	char *d = (char*)dm;
	char *p = dmi_string(dm, d[string]);

	if (p == NULL || *p == 0)
		return;
	if (dmi_ident[slot])
		return;

	dmi_ident[slot] = alloc_bootmem(strlen(p) + 1);
	if(dmi_ident[slot])
		strcpy(dmi_ident[slot], p);
	else
		printk(KERN_ERR "dmi_save_ident: out of memory.\n");
}

/*
 *	Process a DMI table entry. Right now all we care about are the BIOS
 *	and machine entries. For 2.5 we should pull the smbus controller info
 *	out of here.
 */
static void __init dmi_decode(struct dmi_header *dm)
{
	u8 *data __attribute__((__unused__)) = (u8 *)dm;
	
	switch(dm->type) {
	case  0:
		dmi_printk(("BIOS Vendor: %s\n", dmi_string(dm, data[4])));
		dmi_save_ident(dm, DMI_BIOS_VENDOR, 4);
		dmi_printk(("BIOS Version: %s\n", dmi_string(dm, data[5])));
		dmi_save_ident(dm, DMI_BIOS_VERSION, 5);
		dmi_printk(("BIOS Release: %s\n", dmi_string(dm, data[8])));
		dmi_save_ident(dm, DMI_BIOS_DATE, 8);
		break;
	case 1:
		dmi_printk(("System Vendor: %s\n", dmi_string(dm, data[4])));
		dmi_save_ident(dm, DMI_SYS_VENDOR, 4);
		dmi_printk(("Product Name: %s\n", dmi_string(dm, data[5])));
		dmi_save_ident(dm, DMI_PRODUCT_NAME, 5);
		dmi_printk(("Version: %s\n", dmi_string(dm, data[6])));
		dmi_save_ident(dm, DMI_PRODUCT_VERSION, 6);
		dmi_printk(("Serial Number: %s\n", dmi_string(dm, data[7])));
		dmi_save_ident(dm, DMI_PRODUCT_SERIAL, 7);
		break;
	case 2:
		dmi_printk(("Board Vendor: %s\n", dmi_string(dm, data[4])));
		dmi_save_ident(dm, DMI_BOARD_VENDOR, 4);
		dmi_printk(("Board Name: %s\n", dmi_string(dm, data[5])));
		dmi_save_ident(dm, DMI_BOARD_NAME, 5);
		dmi_printk(("Board Version: %s\n", dmi_string(dm, data[6])));
		dmi_save_ident(dm, DMI_BOARD_VERSION, 6);
		break;
	}
}

void __init dmi_scan_machine(void)
{
	if (dmi_iterate(dmi_decode))
		printk(KERN_INFO "DMI not present.\n");
}


/**
 *	dmi_check_system - check system DMI data
 *	@list: array of dmi_system_id structures to match against
 *
 *	Walk the blacklist table running matching functions until someone
 *	returns non zero or we hit the end. Callback function is called for
 *	each successfull match. Returns the number of matches.
 */
int dmi_check_system(struct dmi_system_id *list)
{
	int i, count = 0;
	struct dmi_system_id *d = list;

	while (d->ident) {
		for (i = 0; i < ARRAY_SIZE(d->matches); i++) {
			int s = d->matches[i].slot;
			if (s == DMI_NONE)
				continue;
			if (dmi_ident[s] && strstr(dmi_ident[s], d->matches[i].substr))
				continue;
			/* No match */
			goto fail;
		}
		if (d->callback && d->callback(d))
			break;
		count++;
fail:		d++;
	}

	return count;
}
EXPORT_SYMBOL(dmi_check_system);

/**
 *	dmi_get_system_info - return DMI data value
 *	@field: data index (see enum dmi_filed)
 *
 *	Returns one DMI data value, can be used to perform
 *	complex DMI data checks.
 */
char *dmi_get_system_info(int field)
{
	return dmi_ident[field];
}
EXPORT_SYMBOL(dmi_get_system_info);
