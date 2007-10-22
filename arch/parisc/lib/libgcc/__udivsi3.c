#include "libgcc.h"

u32 __udivsi3(u32 num, u32 den)
{
	return __udivmodsi4(num, den, NULL);
}
EXPORT_SYMBOL(__udivsi3);
