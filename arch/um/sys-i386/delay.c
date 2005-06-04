#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/param.h>

void __delay(unsigned long time)
{
	/* Stolen from the i386 __loop_delay */
	int d0;
	__asm__ __volatile__(
		"\tjmp 1f\n"
		".align 16\n"
		"1:\tjmp 2f\n"
		".align 16\n"
		"2:\tdecl %0\n\tjns 2b"
		:"=&a" (d0)
		:"0" (time));
}

void __udelay(unsigned long usecs)
{
	int i, n;

	n = (loops_per_jiffy * HZ * usecs) / MILLION;
        for(i=0;i<n;i++)
                cpu_relax();
}

EXPORT_SYMBOL(__udelay);

void __const_udelay(unsigned long usecs)
{
	int i, n;

	n = (loops_per_jiffy * HZ * usecs) / MILLION;
        for(i=0;i<n;i++)
                cpu_relax();
}

EXPORT_SYMBOL(__const_udelay);
