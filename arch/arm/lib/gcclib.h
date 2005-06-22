/* gcclib.h -- definitions for various functions 'borrowed' from gcc-2.95.3 */
/* I Molton     29/07/01 */

#include <linux/types.h>

#define BITS_PER_UNIT	8
#define SI_TYPE_SIZE	(sizeof(s32) * BITS_PER_UNIT)

#ifdef __ARMEB__
struct DIstruct {
	s32 high, low;
};
#else
struct DIstruct {
	s32 low, high;
};
#endif

typedef union {
	struct DIstruct s;
	s64 ll;
} DIunion;
