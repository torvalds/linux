/*
 * Copyright (C) 2016 IBM Corporation
 *
 * Authors:
 *      Nayna Jain <nayna@linux.vnet.ibm.com>
 *
 * Access to TPM 2.0 event log as written by Firmware.
 * It assumes that writer of event log has followed TCG Specification
 * for Family "2.0" and written the event data in little endian.
 * With that, it doesn't need any endian conversion for structure
 * content.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tpm_eventlog.h>

#include "../tpm.h"
#include "common.h"

/*
 * calc_tpm2_event_size() - calculate the event size, where event
 * is an entry in the TPM 2.0 event log. The event is of type Crypto
 * Agile Log Entry Format as defined in TCG EFI Protocol Specification
 * Family "2.0".

 * @event: event whose size is to be calculated.
 * @event_header: the first event in the event log.
 *
 * Returns size of the event. If it is an invalid event, returns 0.
 */
static size_t calc_tpm2_event_size(struct tcg_pcr_event2_head *event,
				   struct tcg_pcr_event *event_header)
{
	struct tcg_efi_specid_event_head *efispecid;
	struct tcg_event_field *event_field;
	void *marker;
	void *marker_start;
	u32 halg_size;
	size_t size;
	u16 halg;
	int i;
	int j;

	marker = event;
	marker_start = marker;
	marker = marker + sizeof(event->pcr_idx) + sizeof(event->event_type)
		+ sizeof(event->count);

	efispecid = (struct tcg_efi_specid_event_head *)event_header->event;

	/* Check if event is malformed. */
	if (event->count > efispecid->num_algs)
		return 0;

	for (i = 0; i < event->count; i++) {
		halg_size = sizeof(event->digests[i].alg_id);
		memcpy(&halg, marker, halg_size);
		marker = marker + halg_size;
		for (j = 0; j < efispecid->num_algs; j++) {
			if (halg == efispecid->digest_sizes[j].alg_id) {
				marker +=
					efispecid->digest_sizes[j].digest_size;
				break;
			}
		}
		/* Algorithm without known length. Such event is unparseable. */
		if (j == efispecid->num_algs)
			return 0;
	}

	event_field = (struct tcg_event_field *)marker;
	marker = marker + sizeof(event_field->event_size)
		+ event_field->event_size;
	size = marker - marker_start;

	if ((event->event_type == 0) && (event_field->event_size == 0))
		return 0;

	return size;
}

static void *tpm2_bios_measurements_start(struct seq_file *m, loff_t *pos)
{
	struct tpm_chip *chip = m->private;
	struct tpm_bios_log *log = &chip->log;
	void *addr = log->bios_event_log;
	void *limit = log->bios_event_log_end;
	struct tcg_pcr_event *event_header;
	struct tcg_pcr_event2_head *event;
	size_t size;
	int i;

	event_header = addr;
	size = sizeof(struct tcg_pcr_event) - sizeof(event_header->event)
		+ event_header->event_size;

	if (*pos == 0) {
		if (addr + size < limit) {
			if ((event_header->event_type == 0) &&
			    (event_header->event_size == 0))
				return NULL;
			return SEQ_START_TOKEN;
		}
	}

	if (*pos > 0) {
		addr += size;
		event = addr;
		size = calc_tpm2_event_size(event, event_header);
		if ((addr + size >=  limit) || (size == 0))
			return NULL;
	}

	for (i = 0; i < (*pos - 1); i++) {
		event = addr;
		size = calc_tpm2_event_size(event, event_header);

		if ((addr + size >= limit) || (size == 0))
			return NULL;
		addr += size;
	}

	return addr;
}

static void *tpm2_bios_measurements_next(struct seq_file *m, void *v,
					 loff_t *pos)
{
	struct tcg_pcr_event *event_header;
	struct tcg_pcr_event2_head *event;
	struct tpm_chip *chip = m->private;
	struct tpm_bios_log *log = &chip->log;
	void *limit = log->bios_event_log_end;
	size_t event_size;
	void *marker;

	event_header = log->bios_event_log;

	if (v == SEQ_START_TOKEN) {
		event_size = sizeof(struct tcg_pcr_event) -
			sizeof(event_header->event) + event_header->event_size;
		marker = event_header;
	} else {
		event = v;
		event_size = calc_tpm2_event_size(event, event_header);
		if (event_size == 0)
			return NULL;
		marker = event;
	}

	marker = marker + event_size;
	if (marker >= limit)
		return NULL;
	v = marker;
	event = v;

	event_size = calc_tpm2_event_size(event, event_header);
	if (((v + event_size) >= limit) || (event_size == 0))
		return NULL;

	(*pos)++;
	return v;
}

static void tpm2_bios_measurements_stop(struct seq_file *m, void *v)
{
}

static int tpm2_binary_bios_measurements_show(struct seq_file *m, void *v)
{
	struct tpm_chip *chip = m->private;
	struct tpm_bios_log *log = &chip->log;
	struct tcg_pcr_event *event_header = log->bios_event_log;
	struct tcg_pcr_event2_head *event = v;
	void *temp_ptr;
	size_t size;

	if (v == SEQ_START_TOKEN) {
		size = sizeof(struct tcg_pcr_event) -
			sizeof(event_header->event) + event_header->event_size;

		temp_ptr = event_header;

		if (size > 0)
			seq_write(m, temp_ptr, size);
	} else {
		size = calc_tpm2_event_size(event, event_header);
		temp_ptr = event;
		if (size > 0)
			seq_write(m, temp_ptr, size);
	}

	return 0;
}

const struct seq_operations tpm2_binary_b_measurements_seqops = {
	.start = tpm2_bios_measurements_start,
	.next = tpm2_bios_measurements_next,
	.stop = tpm2_bios_measurements_stop,
	.show = tpm2_binary_bios_measurements_show,
};
