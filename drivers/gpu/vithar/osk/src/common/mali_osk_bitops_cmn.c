/*
 *
 * (C) COPYRIGHT 2008-2010 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */




#include <osk/mali_osk.h>

unsigned long osk_bitarray_find_first_zero_bit(const unsigned long *addr, unsigned long maxbit)
{
	unsigned long total;

	OSK_ASSERT(NULL != addr);

	for ( total = 0; total < maxbit; total += OSK_BITS_PER_LONG, ++addr )
	{
		if (OSK_ULONG_MAX != *addr)
		{
			int result;
			result = oskp_find_first_zero_bit( *addr );
			/* non-negative signifies the bit was found */
			if ( result >= 0 )
			{
				total += (unsigned long)result;
				break;
			}
		}
	}

	/* Now check if we reached maxbit or above */
	if ( total >= maxbit )
	{
		total = maxbit;
	}

	return total; /* either the found bit nr, or maxbit if not found */
}
