/*
 * Parser/loader for IHEX formatted data.
 *
 * Copyright © 2008 David Woodhouse <dwmw2@infradead.org>
 * Copyright © 2005 Jan Harkes <jaharkes@cs.cmu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdint.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#define _GNU_SOURCE
#include <getopt.h>


struct ihex_binrec {
	struct ihex_binrec *next; /* not part of the real data structure */
        uint32_t addr;
        uint16_t len;
        uint8_t data[];
};

/**
 * nybble/hex are little helpers to parse hexadecimal numbers to a byte value
 **/
static uint8_t nybble(const uint8_t n)
{
       if      (n >= '0' && n <= '9') return n - '0';
       else if (n >= 'A' && n <= 'F') return n - ('A' - 10);
       else if (n >= 'a' && n <= 'f') return n - ('a' - 10);
       return 0;
}

static uint8_t hex(const uint8_t *data, uint8_t *crc)
{
       uint8_t val = (nybble(data[0]) << 4) | nybble(data[1]);
       *crc += val;
       return val;
}

static int process_ihex(uint8_t *data, ssize_t size);
static void file_record(struct ihex_binrec *record);
static int output_records(int outfd);

static int sort_records = 0;
static int wide_records = 0;
static int include_jump = 0;

static int usage(void)
{
	fprintf(stderr, "ihex2fw: Convert ihex files into binary "
		"representation for use by Linux kernel\n");
	fprintf(stderr, "usage: ihex2fw [<options>] <src.HEX> <dst.fw>\n");
	fprintf(stderr, "       -w: wide records (16-bit length)\n");
	fprintf(stderr, "       -s: sort records by address\n");
	fprintf(stderr, "       -j: include records for CS:IP/EIP address\n");
	return 1;
}

int main(int argc, char **argv)
{
	int infd, outfd;
	struct stat st;
	uint8_t *data;
	int opt;

	while ((opt = getopt(argc, argv, "wsj")) != -1) {
		switch (opt) {
		case 'w':
			wide_records = 1;
			break;
		case 's':
			sort_records = 1;
			break;
		case 'j':
			include_jump = 1;
			break;
			return usage();
		}
	}

	if (optind + 2 != argc)
		return usage();

	if (!strcmp(argv[optind], "-"))
	    infd = 0;
	else
		infd = open(argv[optind], O_RDONLY);
	if (infd == -1) {
		fprintf(stderr, "Failed to open source file: %s",
			strerror(errno));
		return usage();
	}
	if (fstat(infd, &st)) {
		perror("stat");
		return 1;
	}
	data = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, infd, 0);
	if (data == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	if (!strcmp(argv[optind+1], "-"))
	    outfd = 1;
	else
		outfd = open(argv[optind+1], O_TRUNC|O_CREAT|O_WRONLY, 0644);
	if (outfd == -1) {
		fprintf(stderr, "Failed to open destination file: %s",
			strerror(errno));
		return usage();
	}
	if (process_ihex(data, st.st_size))
		return 1;

	output_records(outfd);
	return 0;
}

static int process_ihex(uint8_t *data, ssize_t size)
{
	struct ihex_binrec *record;
	uint32_t offset = 0;
	uint32_t data32;
	uint8_t type, crc = 0, crcbyte = 0;
	int i, j;
	int line = 1;
	int len;

	i = 0;
next_record:
	/* search for the start of record character */
	while (i < size) {
		if (data[i] == '\n') line++;
		if (data[i++] == ':') break;
	}

	/* Minimum record length would be about 10 characters */
	if (i + 10 > size) {
		fprintf(stderr, "Can't find valid record at line %d\n", line);
		return -EINVAL;
	}

	len = hex(data + i, &crc); i += 2;
	if (wide_records) {
		len <<= 8;
		len += hex(data + i, &crc); i += 2;
	}
	record = malloc((sizeof (*record) + len + 3) & ~3);
	if (!record) {
		fprintf(stderr, "out of memory for records\n");
		return -ENOMEM;
	}
	memset(record, 0, (sizeof(*record) + len + 3) & ~3);
	record->len = len;

	/* now check if we have enough data to read everything */
	if (i + 8 + (record->len * 2) > size) {
		fprintf(stderr, "Not enough data to read complete record at line %d\n",
			line);
		return -EINVAL;
	}

	record->addr  = hex(data + i, &crc) << 8; i += 2;
	record->addr |= hex(data + i, &crc); i += 2;
	type = hex(data + i, &crc); i += 2;

	for (j = 0; j < record->len; j++, i += 2)
		record->data[j] = hex(data + i, &crc);

	/* check CRC */
	crcbyte = hex(data + i, &crc); i += 2;
	if (crc != 0) {
		fprintf(stderr, "CRC failure at line %d: got 0x%X, expected 0x%X\n",
			line, crcbyte, (unsigned char)(crcbyte-crc));
		return -EINVAL;
	}

	/* Done reading the record */
	switch (type) {
	case 0:
		/* old style EOF record? */
		if (!record->len)
			break;

		record->addr += offset;
		file_record(record);
		goto next_record;

	case 1: /* End-Of-File Record */
		if (record->addr || record->len) {
			fprintf(stderr, "Bad EOF record (type 01) format at line %d",
				line);
			return -EINVAL;
		}
		break;

	case 2: /* Extended Segment Address Record (HEX86) */
	case 4: /* Extended Linear Address Record (HEX386) */
		if (record->addr || record->len != 2) {
			fprintf(stderr, "Bad HEX86/HEX386 record (type %02X) at line %d\n",
				type, line);
			return -EINVAL;
		}

		/* We shouldn't really be using the offset for HEX86 because
		 * the wraparound case is specified quite differently. */
		offset = record->data[0] << 8 | record->data[1];
		offset <<= (type == 2 ? 4 : 16);
		goto next_record;

	case 3: /* Start Segment Address Record */
	case 5: /* Start Linear Address Record */
		if (record->addr || record->len != 4) {
			fprintf(stderr, "Bad Start Address record (type %02X) at line %d\n",
				type, line);
			return -EINVAL;
		}

		memcpy(&data32, &record->data[0], sizeof(data32));
		data32 = htonl(data32);
		memcpy(&record->data[0], &data32, sizeof(data32));

		/* These records contain the CS/IP or EIP where execution
		 * starts. If requested output this as a record. */
		if (include_jump)
			file_record(record);
		goto next_record;

	default:
		fprintf(stderr, "Unknown record (type %02X)\n", type);
		return -EINVAL;
	}

	return 0;
}

static struct ihex_binrec *records;

static void file_record(struct ihex_binrec *record)
{
	struct ihex_binrec **p = &records;

	while ((*p) && (!sort_records || (*p)->addr < record->addr))
		p = &((*p)->next);

	record->next = *p;
	*p = record;
}

static int output_records(int outfd)
{
	unsigned char zeroes[6] = {0, 0, 0, 0, 0, 0};
	struct ihex_binrec *p = records;

	while (p) {
		uint16_t writelen = (p->len + 9) & ~3;

		p->addr = htonl(p->addr);
		p->len = htons(p->len);
		write(outfd, &p->addr, writelen);
		p = p->next;
	}
	/* EOF record is zero length, since we don't bother to represent
	   the type field in the binary version */
	write(outfd, zeroes, 6);
	return 0;
}
