// SPDX-License-Identifier: GPL-2.0
#include <asm/byteorder.h>
#include "vphn.h"

/*
 * The associativity domain numbers are returned from the hypervisor as a
 * stream of mixed 16-bit and 32-bit fields. The stream is terminated by the
 * special value of "all ones" (aka. 0xffff) and its size may not exceed 48
 * bytes.
 *
 *    --- 16-bit fields -->
 *  _________________________
 *  |  0  |  1  |  2  |  3  |   be_packed[0]
 *  ------+-----+-----+------
 *  _________________________
 *  |  4  |  5  |  6  |  7  |   be_packed[1]
 *  -------------------------
 *            ...
 *  _________________________
 *  | 20  | 21  | 22  | 23  |   be_packed[5]
 *  -------------------------
 *
 * Convert to the sequence they would appear in the ibm,associativity property.
 */
int vphn_unpack_associativity(const long *packed, __be32 *unpacked)
{
	__be64 be_packed[VPHN_REGISTER_COUNT];
	int i, nr_assoc_doms = 0;
	const __be16 *field = (const __be16 *) be_packed;
	u16 last = 0;
	bool is_32bit = false;

#define VPHN_FIELD_UNUSED	(0xffff)
#define VPHN_FIELD_MSB		(0x8000)
#define VPHN_FIELD_MASK		(~VPHN_FIELD_MSB)

	/* Let's fix the values returned by plpar_hcall9() */
	for (i = 0; i < VPHN_REGISTER_COUNT; i++)
		be_packed[i] = cpu_to_be64(packed[i]);

	for (i = 1; i < VPHN_ASSOC_BUFSIZE; i++) {
		u16 new = be16_to_cpup(field++);

		if (is_32bit) {
			/*
			 * Let's concatenate the 16 bits of this field to the
			 * 15 lower bits of the previous field
			 */
			unpacked[++nr_assoc_doms] =
				cpu_to_be32(last << 16 | new);
			is_32bit = false;
		} else if (new == VPHN_FIELD_UNUSED)
			/* This is the list terminator */
			break;
		else if (new & VPHN_FIELD_MSB) {
			/* Data is in the lower 15 bits of this field */
			unpacked[++nr_assoc_doms] =
				cpu_to_be32(new & VPHN_FIELD_MASK);
		} else {
			/*
			 * Data is in the lower 15 bits of this field
			 * concatenated with the next 16 bit field
			 */
			last = new;
			is_32bit = true;
		}
	}

	/* The first cell contains the length of the property */
	unpacked[0] = cpu_to_be32(nr_assoc_doms);

	return nr_assoc_doms;
}
