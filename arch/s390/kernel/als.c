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
	strcat(mach_str, "\n");
	sclp_early_printk(mach_str);
}

static void __init u16_to_decimal(char *str, u16 val)
{
	int div = 1;

	while (div * 10 <= val)
		div *= 10;
	while (div) {
		*str++ = '0' + val / div;
		val %= div;
		div /= 10;
	}
	*str = '\0';
}

static void __init print_missing_facilities(void)
{
	static char als_str[80] __initdata = "Missing facilities: ";
	unsigned long val;
	char val_str[6];
	int i, j, first;

	first = 1;
	for (i = 0; i < ARRAY_SIZE(als); i++) {
		val = ~S390_lowcore.stfle_fac_list[i] & als[i];
		for (j = 0; j < BITS_PER_LONG; j++) {
			if (!(val & (1UL << (BITS_PER_LONG - 1 - j))))
				continue;
			if (!first)
				strcat(als_str, ",");
			/*
			 * Make sure we stay within one line. Consider that
			 * each facility bit adds up to five characters and
			 * z/VM adds a four character prefix.
			 */
			if (strlen(als_str) > 70) {
				strcat(als_str, "\n");
				sclp_early_printk(als_str);
				*als_str = '\0';
			}
			u16_to_decimal(val_str, i * BITS_PER_LONG + j);
			strcat(als_str, val_str);
			first = 0;
		}
	}
	strcat(als_str, "\n");
	sclp_early_printk(als_str);
	sclp_early_printk("See Principles of Operations for facility bits\n");
}

static void __init facility_mismatch(void)
{
	sclp_early_printk("The Linux kernel requires more recent processor hardware\n");
	print_machine_type();
	print_missing_facilities();
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
