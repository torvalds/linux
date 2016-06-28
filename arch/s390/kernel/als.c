/*
 *    Copyright IBM Corp. 2016
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/processor.h>
#include <asm/facility.h>
#include <asm/lowcore.h>
#include <asm/sclp.h>
#include "entry.h"

/*
 * The code within this file will be called very early. It may _not_
 * access anything within the bss section, since that is not cleared
 * yet and may contain data (e.g. initrd) that must be saved by other
 * code.
 * For temporary objects the stack (16k) should be used.
 */

static unsigned long als[] __initdata = { FACILITIES_ALS };

static void __init u16_to_hex(char *str, u16 val)
{
	int i, num;

	for (i = 1; i <= 4; i++) {
		num = (val >> (16 - 4 * i)) & 0xf;
		if (num >= 10)
			num += 7;
		*str++ = '0' + num;
	}
	*str = '\0';
}

static void __init print_machine_type(void)
{
	static char mach_str[80] __initdata = "Detected machine-type number: ";
	char type_str[5];
	struct cpuid id;

	get_cpu_id(&id);
	u16_to_hex(type_str, id.machine);
	strcat(mach_str, type_str);
	_sclp_print_early(mach_str);
}

static void __init facility_mismatch(void)
{
	_sclp_print_early("The Linux kernel requires more recent processor hardware");
	print_machine_type();
	disabled_wait(0x8badcccc);
}

void __init verify_facilities(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(S390_lowcore.stfle_fac_list); i++)
		S390_lowcore.stfle_fac_list[i] = 0;
	asm volatile(
		"	stfl	0(0)\n"
		: "=m" (S390_lowcore.stfl_fac_list));
	S390_lowcore.stfle_fac_list[0] = (u64)S390_lowcore.stfl_fac_list << 32;
	if (S390_lowcore.stfl_fac_list & 0x01000000) {
		register unsigned long reg0 asm("0") = ARRAY_SIZE(als) - 1;

		asm volatile(".insn s,0xb2b00000,0(%1)" /* stfle */
			     : "+d" (reg0)
			     : "a" (&S390_lowcore.stfle_fac_list)
			     : "memory", "cc");
	}
	for (i = 0; i < ARRAY_SIZE(als); i++) {
		if ((S390_lowcore.stfle_fac_list[i] & als[i]) != als[i])
			facility_mismatch();
	}
}
