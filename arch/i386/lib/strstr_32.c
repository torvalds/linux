#include <linux/string.h>

char * strstr(const char * cs,const char * ct)
{
int	d0, d1;
register char * __res;
__asm__ __volatile__(
	"movl %6,%%edi\n\t"
	"repne\n\t"
	"scasb\n\t"
	"notl %%ecx\n\t"
	"decl %%ecx\n\t"	/* NOTE! This also sets Z if searchstring='' */
	"movl %%ecx,%%edx\n"
	"1:\tmovl %6,%%edi\n\t"
	"movl %%esi,%%eax\n\t"
	"movl %%edx,%%ecx\n\t"
	"repe\n\t"
	"cmpsb\n\t"
	"je 2f\n\t"		/* also works for empty string, see above */
	"xchgl %%eax,%%esi\n\t"
	"incl %%esi\n\t"
	"cmpb $0,-1(%%eax)\n\t"
	"jne 1b\n\t"
	"xorl %%eax,%%eax\n\t"
	"2:"
	:"=a" (__res), "=&c" (d0), "=&S" (d1)
	:"0" (0), "1" (0xffffffff), "2" (cs), "g" (ct)
	:"dx", "di");
return __res;
}

