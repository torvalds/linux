/*
 * Simulator hook mechanism
 */

#include "vcs_hook.h"
#include <asm/io.h>
#include <stdarg.h>

#define HOOK_TRIG_ADDR      0xb7000000
#define HOOK_MEM_BASE_ADDR  0xce000000

static volatile unsigned *hook_base;

#define HOOK_DATA(offset) hook_base[offset]
#define VHOOK_DATA(offset) hook_base[offset]
#define HOOK_TRIG(funcid) \
	do { \
		*((unsigned *) HOOK_TRIG_ADDR) = funcid; \
	} while (0)
#define HOOK_DATA_BYTE(offset) ((unsigned char *)hook_base)[offset]

static void hook_init(void)
{
	static int first = 1;
	if (first) {
		first = 0;
		hook_base = ioremap(HOOK_MEM_BASE_ADDR, 8192);
	}
}

static unsigned hook_trig(unsigned id)
{
	unsigned ret;

	/* preempt_disable(); */

	/* Dummy read from mem to make sure data has propagated to memory
	 * before trigging */
	ret = *hook_base;

	/* trigger hook */
	HOOK_TRIG(id);

	/* wait for call to finish */
	while (VHOOK_DATA(0) > 0) ;

	/* extract return value */

	ret = VHOOK_DATA(1);

	return ret;
}

int hook_call(unsigned id, unsigned pcnt, ...)
{
	va_list ap;
	int i;
	unsigned ret;

	hook_init();

	HOOK_DATA(0) = id;

	va_start(ap, pcnt);
	for (i = 1; i <= pcnt; i++)
		HOOK_DATA(i) = va_arg(ap, unsigned);
	va_end(ap);

	ret = hook_trig(id);

	return ret;
}

int hook_call_str(unsigned id, unsigned size, const char *str)
{
	int i;
	unsigned ret;

	hook_init();

	HOOK_DATA(0) = id;
	HOOK_DATA(1) = size;

	for (i = 0; i < size; i++)
		HOOK_DATA_BYTE(8 + i) = str[i];
	HOOK_DATA_BYTE(8 + i) = 0;

	ret = hook_trig(id);

	return ret;
}

void print_str(const char *str)
{
	int i;
	/* find null at end of string */
	for (i = 1; str[i]; i++) ;
	hook_call(hook_print_str, i, str);
}

void CPU_WATCHDOG_TIMEOUT(unsigned t)
{
}
