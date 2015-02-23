#include <asm/byteorder.h>
#include "vphn.h"

/*
 * Convert the associativity domain numbers returned from the hypervisor
 * to the sequence they would appear in the ibm,associativity property.
 */
int vphn_unpack_associativity(const long *packed, __be32 *unpacked)
{
	__be64 be_packed[VPHN_REGISTER_COUNT];
	int i, nr_assoc_doms = 0;
	const __be16 *field = (const __be16 *) be_packed;

#define VPHN_FIELD_UNUSED	(0xffff)
#define VPHN_FIELD_MSB		(0x8000)
#define VPHN_FIELD_MASK		(~VPHN_FIELD_MSB)

	/* Let's recreate the original stream. */
	for (i = 0; i < VPHN_REGISTER_COUNT; i++)
		be_packed[i] = cpu_to_be64(packed[i]);

	for (i = 1; i < VPHN_ASSOC_BUFSIZE; i++) {
		if (be16_to_cpup(field) == VPHN_FIELD_UNUSED) {
			/* All significant fields processed, and remaining
			 * fields contain the reserved value of all 1's.
			 * Just store them.
			 */
			unpacked[i] = *((__be32 *)field);
			field += 2;
		} else if (be16_to_cpup(field) & VPHN_FIELD_MSB) {
			/* Data is in the lower 15 bits of this field */
			unpacked[i] = cpu_to_be32(
				be16_to_cpup(field) & VPHN_FIELD_MASK);
			field++;
			nr_assoc_doms++;
		} else {
			/* Data is in the lower 15 bits of this field
			 * concatenated with the next 16 bit field
			 */
			unpacked[i] = *((__be32 *)field);
			field += 2;
			nr_assoc_doms++;
		}
	}

	/* The first cell contains the length of the property */
	unpacked[0] = cpu_to_be32(nr_assoc_doms);

	return nr_assoc_doms;
}
