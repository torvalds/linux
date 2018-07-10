/* vi: set sw=4 ts=4: */
/*
 * RPM structs and consts
 *
 * Copyright (C) 2001 by Laurence Anderson
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/* RPM file starts with this struct: */
struct rpm_lead {
	uint32_t magic;
	uint8_t  major, minor;
	uint16_t type;
	uint16_t archnum;
	char     name[66];
	uint16_t osnum;
	uint16_t signature_type;
	char     reserved[16];
};
struct BUG_rpm_lead {
	char bug[sizeof(struct rpm_lead) == 96 ? 1 : -1];
};
#define RPM_LEAD_MAGIC      0xedabeedb
#define RPM_LEAD_MAGIC_STR  "\355\253\356\333"

/* Then follows the header: */
struct rpm_header {
	uint32_t magic_and_ver; /* 3 byte magic: 0x8e 0xad 0xe8; 1 byte version: 0x01 */
	uint32_t reserved;      /* 4 bytes reserved */
	uint32_t entries;       /* Number of entries in header (4 bytes) */
	uint32_t size;          /* Size of store (4 bytes) */
};
struct BUG_rpm_header {
	char bug[sizeof(struct rpm_header) == 16 ? 1 : -1];
};
#define RPM_HEADER_MAGICnVER  0x8eade801
#define RPM_HEADER_MAGIC_STR  "\216\255\350"
