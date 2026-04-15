/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Copyright (c) 2026 Luminex Network Intelligence
 */
#ifndef _MV88E6XXX_TCAM_H_
#define _MV88E6XXX_TCAM_H_

#define PAGE0_MATCH_SIZE 22
#define PAGE1_MATCH_SIZE 26

#define DPV_MODE_NOP		0x0
#define DPV_MODE_AND		0x1
#define DPV_MODE_OR		0x2
#define DPV_MODE_REPLACE	0x3

int mv88e6xxx_tcam_entry_add(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_tcam_entry *entry);
int mv88e6xxx_tcam_entry_del(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_tcam_entry *entry);
struct mv88e6xxx_tcam_entry *
mv88e6xxx_tcam_entry_find(struct mv88e6xxx_chip *chip, unsigned long cookie);
#define mv88e6xxx_tcam_match_set(key, _offset, data, mask) \
	do { \
		typeof(_offset) (offset) = (_offset); \
		BUILD_BUG_ON((offset) + sizeof((data)) > TCAM_MATCH_SIZE); \
		__mv88e6xxx_tcam_match_set(key, offset, sizeof(data), \
					   (u8 *)&(data), (u8 *)&(mask)); \
	} while (0)

static inline void __mv88e6xxx_tcam_match_set(struct mv88e6xxx_tcam_key *key,
					      unsigned int offset, size_t size,
					      u8 *data, u8 *mask)
{
	memcpy(&key->frame_data[offset], data, size);
	memcpy(&key->frame_mask[offset], mask, size);
}

extern const struct mv88e6xxx_tcam_ops mv88e6390_tcam_ops;
extern const struct mv88e6xxx_tcam_ops mv88e6393_tcam_ops;
#endif
