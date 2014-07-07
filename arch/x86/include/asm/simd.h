
#include <asm/i387.h>

/*
 * may_use_simd - whether it is allowable at this time to issue SIMD
 *                instructions or access the SIMD register file
 */
static __must_check inline bool may_use_simd(void)
{
	return irq_fpu_usable();
}
