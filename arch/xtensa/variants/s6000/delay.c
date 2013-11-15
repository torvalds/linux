#include <asm/timex.h>
#include <asm/io.h>
#include <variant/hardware.h>

#define LOOPS 10
void platform_calibrate_ccount(void)
{
	u32 uninitialized_var(a);
	u32 uninitialized_var(u);
	u32 b;
	u32 tstamp = S6_REG_GREG1 + S6_GREG1_GLOBAL_TIMER;
	int i = LOOPS+1;
	do {
		u32 t = u;
		asm volatile(
		"1:	l32i %0, %2, 0 ;"
		"	beq %0, %1, 1b ;"
		: "=&a"(u) : "a"(t), "a"(tstamp));
		b = get_ccount();
		if (i == LOOPS)
			a = b;
	} while (--i >= 0);
	b -= a;
	ccount_freq = b * (100000UL / LOOPS);
}
