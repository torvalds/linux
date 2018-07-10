/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2005 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation; either
 *	version 2.1 of the License, or (at your option) any later version.
 *
 *	This library is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *	Lesser General Public License for more details.
 *
 *	You should have received a copy of the GNU Lesser General Public
 *	License along with this library; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "volume_id_internal.h"

void volume_id_set_unicode16(char *str, size_t len, const uint8_t *buf, enum endian endianess, size_t count)
{
	unsigned i, j;
	unsigned c;

	j = 0;
	for (i = 0; i + 2 <= count; i += 2) {
		if (endianess == LE)
			c = (buf[i+1] << 8) | buf[i];
		else
			c = (buf[i] << 8) | buf[i+1];
		if (c == 0)
			break;
		if (j+1 >= len)
			break;
		if (c < 0x80) {
			/* 0xxxxxxx */
		} else {
			uint8_t topbits = 0xc0;
			if (j+2 >= len)
				break;
			if (c < 0x800) {
				/* 110yyyxx 10xxxxxx */
			} else {
				if (j+3 >= len)
					break;
				/* 1110yyyy 10yyyyxx 10xxxxxx */
				str[j++] = (uint8_t) (0xe0 | (c >> 12));
				topbits = 0x80;
			}
			str[j++] = (uint8_t) (topbits | ((c >> 6) & 0x3f));
			c = 0x80 | (c & 0x3f);
		}
		str[j++] = (uint8_t) c;
	}
	str[j] = '\0';
}

#ifdef UNUSED
static const char *usage_to_string(enum volume_id_usage usage_id)
{
	switch (usage_id) {
	case VOLUME_ID_FILESYSTEM:
		return "filesystem";
	case VOLUME_ID_PARTITIONTABLE:
		return "partitiontable";
	case VOLUME_ID_OTHER:
		return "other";
	case VOLUME_ID_RAID:
		return "raid";
	case VOLUME_ID_DISKLABEL:
		return "disklabel";
	case VOLUME_ID_CRYPTO:
		return "crypto";
	case VOLUME_ID_UNPROBED:
		return "unprobed";
	case VOLUME_ID_UNUSED:
		return "unused";
	}
	return NULL;
}

void volume_id_set_usage_part(struct volume_id_partition *part, enum volume_id_usage usage_id)
{
	part->usage_id = usage_id;
	part->usage = usage_to_string(usage_id);
}

void volume_id_set_usage(struct volume_id *id, enum volume_id_usage usage_id)
{
	id->usage_id = usage_id;
	id->usage = usage_to_string(usage_id);
}

void volume_id_set_label_raw(struct volume_id *id, const uint8_t *buf, size_t count)
{
	memcpy(id->label_raw, buf, count);
	id->label_raw_len = count;
}
#endif

#ifdef NOT_NEEDED
static size_t strnlen(const char *s, size_t maxlen)
{
	size_t i;
	if (!maxlen) return 0;
	if (!s) return 0;
	for (i = 0; *s && i < maxlen; ++s) ++i;
	return i;
}
#endif

void volume_id_set_label_string(struct volume_id *id, const uint8_t *buf, size_t count)
{
	unsigned i;

	memcpy(id->label, buf, count);

	/* remove trailing whitespace */
	i = strnlen(id->label, count);
	while (i--) {
		if (!isspace(id->label[i]))
			break;
	}
	id->label[i+1] = '\0';
}

void volume_id_set_label_unicode16(struct volume_id *id, const uint8_t *buf, enum endian endianess, size_t count)
{
	volume_id_set_unicode16(id->label, sizeof(id->label), buf, endianess, count);
}

void volume_id_set_uuid(struct volume_id *id, const uint8_t *buf, enum uuid_format format)
{
	unsigned i;
	unsigned count = (format == UUID_DCE_STRING ? VOLUME_ID_UUID_SIZE : 4 << format);

//	memcpy(id->uuid_raw, buf, count);
//	id->uuid_raw_len = count;

	/* if set, create string in the same format, the native platform uses */
	for (i = 0; i < count; i++)
		if (buf[i] != 0)
			goto set;
	return; /* all bytes are zero, leave it empty ("") */

set:
	switch (format) {
	case UUID_DOS:
		sprintf(id->uuid, "%02X%02X-%02X%02X",
			buf[3], buf[2], buf[1], buf[0]);
		break;
	case UUID_NTFS:
		sprintf(id->uuid, "%02X%02X%02X%02X%02X%02X%02X%02X",
			buf[7], buf[6], buf[5], buf[4],
			buf[3], buf[2], buf[1], buf[0]);
		break;
	case UUID_DCE:
		sprintf(id->uuid,
			"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			buf[0], buf[1], buf[2], buf[3],
			buf[4], buf[5],
			buf[6], buf[7],
			buf[8], buf[9],
			buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
		break;
	case UUID_DCE_STRING:
		memcpy(id->uuid, buf, count);
		id->uuid[count] = '\0';
		break;
	}
}

/* Do not use xlseek here. With it, single corrupted filesystem
 * may result in attempt to seek past device -> exit.
 * It's better to ignore such fs and continue.  */
void *volume_id_get_buffer(struct volume_id *id, uint64_t off, size_t len)
{
	uint8_t *dst;
	unsigned small_off;
	ssize_t read_len;

	dbg("get buffer off 0x%llx(%llu), len 0x%zx",
		(unsigned long long) off, (unsigned long long) off, len);

	/* check if requested area fits in superblock buffer */
	if (off + len <= SB_BUFFER_SIZE
	 /* && off <= SB_BUFFER_SIZE - want this paranoid overflow check? */
	) {
		if (id->sbbuf == NULL) {
			id->sbbuf = xmalloc(SB_BUFFER_SIZE);
		}
		small_off = off;
		dst = id->sbbuf;

		/* check if we need to read */
		len += off;
		if (len <= id->sbbuf_len)
			goto ret; /* we already have it */

		dbg("read sbbuf len:0x%x", (unsigned) len);
		id->sbbuf_len = len;
		off = 0;
		goto do_read;
	}

	if (len > SEEK_BUFFER_SIZE) {
		dbg("seek buffer too small %d", SEEK_BUFFER_SIZE);
		return NULL;
	}
	dst = id->seekbuf;

	/* check if we need to read */
	if ((off >= id->seekbuf_off)
	 && ((off + len) <= (id->seekbuf_off + id->seekbuf_len))
	) {
		small_off = off - id->seekbuf_off; /* can't overflow */
		goto ret; /* we already have it */
	}

	id->seekbuf_off = off;
	id->seekbuf_len = len;
	id->seekbuf = xrealloc(id->seekbuf, len);
	small_off = 0;
	dst = id->seekbuf;
	dbg("read seekbuf off:0x%llx len:0x%zx",
				(unsigned long long) off, len);
 do_read:
	if (lseek(id->fd, off, SEEK_SET) != off) {
		dbg("seek(0x%llx) failed", (unsigned long long) off);
		goto err;
	}
	read_len = full_read(id->fd, dst, len);
	if (read_len != len) {
		dbg("requested 0x%x bytes, got 0x%x bytes",
				(unsigned) len, (unsigned) read_len);
 err:
		/* No filesystem can be this tiny. It's most likely
		 * non-associated loop device, empty drive and so on.
		 * Flag it, making it possible to short circuit future
		 * accesses. Rationale:
		 * users complained of slow blkid due to empty floppy drives.
		 */
		if (off < 64*1024)
			id->error = 1;
		/* id->seekbuf_len or id->sbbuf_len is wrong now! Fixing. */
		volume_id_free_buffer(id);
		return NULL;
	}
 ret:
	return dst + small_off;
}

void volume_id_free_buffer(struct volume_id *id)
{
	free(id->sbbuf);
	id->sbbuf = NULL;
	id->sbbuf_len = 0;
	free(id->seekbuf);
	id->seekbuf = NULL;
	id->seekbuf_len = 0;
	id->seekbuf_off = 0; /* paranoia */
}
