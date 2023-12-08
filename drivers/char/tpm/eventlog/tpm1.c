// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2005, 2012 IBM Corporation
 *
 * Authors:
 *	Kent Yoder <key@linux.vnet.ibm.com>
 *	Seiji Munetoh <munetoh@jp.ibm.com>
 *	Stefan Berger <stefanb@us.ibm.com>
 *	Reiner Sailer <sailer@watson.ibm.com>
 *	Kylene Hall <kjhall@us.ibm.com>
 *	Nayna Jain <nayna@linux.vnet.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Access to the event log created by a system's firmware / BIOS
 */

#include <linux/seq_file.h>
#include <linux/efi.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tpm_eventlog.h>

#include "../tpm.h"
#include "common.h"


static const char* tcpa_event_type_strings[] = {
	"PREBOOT",
	"POST CODE",
	"",
	"NO ACTION",
	"SEPARATOR",
	"ACTION",
	"EVENT TAG",
	"S-CRTM Contents",
	"S-CRTM Version",
	"CPU Microcode",
	"Platform Config Flags",
	"Table of Devices",
	"Compact Hash",
	"IPL",
	"IPL Partition Data",
	"Non-Host Code",
	"Non-Host Config",
	"Non-Host Info"
};

static const char* tcpa_pc_event_id_strings[] = {
	"",
	"SMBIOS",
	"BIS Certificate",
	"POST BIOS ",
	"ESCD ",
	"CMOS",
	"NVRAM",
	"Option ROM",
	"Option ROM config",
	"",
	"Option ROM microcode ",
	"S-CRTM Version",
	"S-CRTM Contents ",
	"POST Contents ",
	"Table of Devices",
};

/* returns pointer to start of pos. entry of tcg log */
static void *tpm1_bios_measurements_start(struct seq_file *m, loff_t *pos)
{
	loff_t i = 0;
	struct tpm_chip *chip = m->private;
	struct tpm_bios_log *log = &chip->log;
	void *addr = log->bios_event_log;
	void *limit = log->bios_event_log_end;
	struct tcpa_event *event;
	u32 converted_event_size;
	u32 converted_event_type;

	/* read over *pos measurements */
	do {
		event = addr;

		/* check if current entry is valid */
		if (addr + sizeof(struct tcpa_event) > limit)
			return NULL;

		converted_event_size =
		    do_endian_conversion(event->event_size);
		converted_event_type =
		    do_endian_conversion(event->event_type);

		if (((converted_event_type == 0) && (converted_event_size == 0))
		    || ((addr + sizeof(struct tcpa_event) + converted_event_size)
			> limit))
			return NULL;

		if (i++ == *pos)
			break;

		addr += (sizeof(struct tcpa_event) + converted_event_size);
	} while (1);

	return addr;
}

static void *tpm1_bios_measurements_next(struct seq_file *m, void *v,
					loff_t *pos)
{
	struct tcpa_event *event = v;
	struct tpm_chip *chip = m->private;
	struct tpm_bios_log *log = &chip->log;
	void *limit = log->bios_event_log_end;
	u32 converted_event_size;
	u32 converted_event_type;

	(*pos)++;
	converted_event_size = do_endian_conversion(event->event_size);

	v += sizeof(struct tcpa_event) + converted_event_size;

	/* now check if current entry is valid */
	if ((v + sizeof(struct tcpa_event)) > limit)
		return NULL;

	event = v;

	converted_event_size = do_endian_conversion(event->event_size);
	converted_event_type = do_endian_conversion(event->event_type);

	if (((converted_event_type == 0) && (converted_event_size == 0)) ||
	    ((v + sizeof(struct tcpa_event) + converted_event_size) > limit))
		return NULL;

	return v;
}

static void tpm1_bios_measurements_stop(struct seq_file *m, void *v)
{
}

static int get_event_name(char *dest, struct tcpa_event *event,
			unsigned char * event_entry)
{
	const char *name = "";
	/* 41 so there is room for 40 data and 1 nul */
	char data[41] = "";
	int i, n_len = 0, d_len = 0;
	struct tcpa_pc_event *pc_event;

	switch (do_endian_conversion(event->event_type)) {
	case PREBOOT:
	case POST_CODE:
	case UNUSED:
	case NO_ACTION:
	case SCRTM_CONTENTS:
	case SCRTM_VERSION:
	case CPU_MICROCODE:
	case PLATFORM_CONFIG_FLAGS:
	case TABLE_OF_DEVICES:
	case COMPACT_HASH:
	case IPL:
	case IPL_PARTITION_DATA:
	case NONHOST_CODE:
	case NONHOST_CONFIG:
	case NONHOST_INFO:
		name = tcpa_event_type_strings[do_endian_conversion
						(event->event_type)];
		n_len = strlen(name);
		break;
	case SEPARATOR:
	case ACTION:
		if (MAX_TEXT_EVENT >
		    do_endian_conversion(event->event_size)) {
			name = event_entry;
			n_len = do_endian_conversion(event->event_size);
		}
		break;
	case EVENT_TAG:
		pc_event = (struct tcpa_pc_event *)event_entry;

		/* ToDo Row data -> Base64 */

		switch (do_endian_conversion(pc_event->event_id)) {
		case SMBIOS:
		case BIS_CERT:
		case CMOS:
		case NVRAM:
		case OPTION_ROM_EXEC:
		case OPTION_ROM_CONFIG:
		case S_CRTM_VERSION:
			name = tcpa_pc_event_id_strings[do_endian_conversion
							(pc_event->event_id)];
			n_len = strlen(name);
			break;
		/* hash data */
		case POST_BIOS_ROM:
		case ESCD:
		case OPTION_ROM_MICROCODE:
		case S_CRTM_CONTENTS:
		case POST_CONTENTS:
			name = tcpa_pc_event_id_strings[do_endian_conversion
							(pc_event->event_id)];
			n_len = strlen(name);
			for (i = 0; i < 20; i++)
				d_len += sprintf(&data[2*i], "%02x",
						pc_event->event_data[i]);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return snprintf(dest, MAX_TEXT_EVENT, "[%.*s%.*s]",
			n_len, name, d_len, data);

}

static int tpm1_binary_bios_measurements_show(struct seq_file *m, void *v)
{
	struct tcpa_event *event = v;
	struct tcpa_event temp_event;
	char *temp_ptr;
	int i;

	memcpy(&temp_event, event, sizeof(struct tcpa_event));

	/* convert raw integers for endianness */
	temp_event.pcr_index = do_endian_conversion(event->pcr_index);
	temp_event.event_type = do_endian_conversion(event->event_type);
	temp_event.event_size = do_endian_conversion(event->event_size);

	temp_ptr = (char *) &temp_event;

	for (i = 0; i < (sizeof(struct tcpa_event) - 1) ; i++)
		seq_putc(m, temp_ptr[i]);

	temp_ptr = (char *) v;

	for (i = (sizeof(struct tcpa_event) - 1);
	     i < (sizeof(struct tcpa_event) + temp_event.event_size); i++)
		seq_putc(m, temp_ptr[i]);

	return 0;

}

static int tpm1_ascii_bios_measurements_show(struct seq_file *m, void *v)
{
	int len = 0;
	char *eventname;
	struct tcpa_event *event = v;
	unsigned char *event_entry =
	    (unsigned char *)(v + sizeof(struct tcpa_event));

	eventname = kmalloc(MAX_TEXT_EVENT, GFP_KERNEL);
	if (!eventname) {
		printk(KERN_ERR "%s: ERROR - No Memory for event name\n ",
		       __func__);
		return -EFAULT;
	}

	/* 1st: PCR */
	seq_printf(m, "%2d ", do_endian_conversion(event->pcr_index));

	/* 2nd: SHA1 */
	seq_printf(m, "%20phN", event->pcr_value);

	/* 3rd: event type identifier */
	seq_printf(m, " %02x", do_endian_conversion(event->event_type));

	len += get_event_name(eventname, event, event_entry);

	/* 4th: eventname <= max + \'0' delimiter */
	seq_printf(m, " %s\n", eventname);

	kfree(eventname);
	return 0;
}

const struct seq_operations tpm1_ascii_b_measurements_seqops = {
	.start = tpm1_bios_measurements_start,
	.next = tpm1_bios_measurements_next,
	.stop = tpm1_bios_measurements_stop,
	.show = tpm1_ascii_bios_measurements_show,
};

const struct seq_operations tpm1_binary_b_measurements_seqops = {
	.start = tpm1_bios_measurements_start,
	.next = tpm1_bios_measurements_next,
	.stop = tpm1_bios_measurements_stop,
	.show = tpm1_binary_bios_measurements_show,
};
