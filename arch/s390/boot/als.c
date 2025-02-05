// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2016
 */
#include <linux/kernel.h>
#include <asm/processor.h>
#include <asm/facility.h>
#include <asm/lowcore.h>
#include <asm/sclp.h>
#include "boot.h"

static unsigned long als[] = { FACILITIES_ALS };

static void u16_to_decimal(char *str, u16 val)
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

void print_missing_facilities(void)
{
	static char als_str[80] = "Missing facilities: ";
	unsigned long val;
	char val_str[6];
	int i, j, first;

	first = 1;
	for (i = 0; i < ARRAY_SIZE(als); i++) {
		val = ~stfle_fac_list[i] & als[i];
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
				boot_emerg("%s\n", als_str);
				*als_str = '\0';
			}
			u16_to_decimal(val_str, i * BITS_PER_LONG + j);
			strcat(als_str, val_str);
			first = 0;
		}
	}
	boot_emerg("%s\n", als_str);
}

static void facility_mismatch(void)
{
	struct cpuid id;

	get_cpu_id(&id);
	boot_emerg("The Linux kernel requires more recent processor hardware\n");
	boot_emerg("Detected machine-type number: %4x\n", id.machine);
	print_missing_facilities();
	boot_emerg("See Principles of Operations for facility bits\n");
	disabled_wait();
}

void verify_facilities(void)
{
	int i;

	__stfle(stfle_fac_list, ARRAY_SIZE(stfle_fac_list));
	for (i = 0; i < ARRAY_SIZE(als); i++) {
		if ((stfle_fac_list[i] & als[i]) != als[i])
			facility_mismatch();
	}
}
