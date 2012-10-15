#ifndef __ASM_SYNC_BITOPS_H__
#define __ASM_SYNC_BITOPS_H__

#include <asm/bitops.h>
#include <asm/system.h>

/* sync_bitops functions are equivalent to the SMP implementation of the
 * original functions, independently from CONFIG_SMP being defined.
 *
 * We need them because _set_bit etc are not SMP safe if !CONFIG_SMP. But
 * under Xen you might be communicating with a completely external entity
 * who might be on another CPU (e.g. two uniprocessor guests communicating
 * via event channels and grant tables). So we need a variant of the bit
 * ops which are SMP safe even on a UP kernel.
 */

#define sync_set_bit(nr, p)		_set_bit(nr, p)
#define sync_clear_bit(nr, p)		_clear_bit(nr, p)
#define sync_change_bit(nr, p)		_change_bit(nr, p)
#define sync_test_and_set_bit(nr, p)	_test_and_set_bit(nr, p)
#define sync_test_and_clear_bit(nr, p)	_test_and_clear_bit(nr, p)
#define sync_test_and_change_bit(nr, p)	_test_and_change_bit(nr, p)
#define sync_test_bit(nr, addr)		test_bit(nr, addr)
#define sync_cmpxchg			cmpxchg


#endif
