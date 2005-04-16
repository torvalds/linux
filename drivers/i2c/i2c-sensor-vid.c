/*
    i2c-sensor-vid.c -  Part of lm_sensors, Linux kernel modules for hardware
        		monitoring

    Copyright (c) 2004 Rudolf Marek <r.marek@sh.cvut.cz>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>

struct vrm_model {
	u8 vendor;
	u8 eff_family;
	u8 eff_model;
	int vrm_type;
};

#define ANY 0xFF

#ifdef CONFIG_X86

static struct vrm_model vrm_models[] = {
	{X86_VENDOR_AMD, 0x6, ANY, 90},		/* Athlon Duron etc */
	{X86_VENDOR_AMD, 0xF, ANY, 24},		/* Athlon 64, Opteron */
	{X86_VENDOR_INTEL, 0x6, 0x9, 85},	/* 0.13um too */
	{X86_VENDOR_INTEL, 0x6, 0xB, 85},	/* 0xB Tualatin */
	{X86_VENDOR_INTEL, 0x6, ANY, 82},	/* any P6 */
	{X86_VENDOR_INTEL, 0x7, ANY, 0},	/* Itanium */
	{X86_VENDOR_INTEL, 0xF, 0x3, 100},	/* P4 Prescott */
	{X86_VENDOR_INTEL, 0xF, ANY, 90},	/* P4 before Prescott */
	{X86_VENDOR_INTEL, 0x10,ANY, 0},	/* Itanium 2 */
	{X86_VENDOR_UNKNOWN, ANY, ANY, 0}	/* stop here */
	};

static int find_vrm(u8 eff_family, u8 eff_model, u8 vendor)
{
	int i = 0;

	while (vrm_models[i].vendor!=X86_VENDOR_UNKNOWN) {
		if (vrm_models[i].vendor==vendor)
			if ((vrm_models[i].eff_family==eff_family)&& \
			((vrm_models[i].eff_model==eff_model)|| \
			(vrm_models[i].eff_model==ANY)))
				return vrm_models[i].vrm_type;
		i++;
	}

	return 0;
}

int i2c_which_vrm(void)
{
	struct cpuinfo_x86 *c = cpu_data;
	u32 eax;
	u8 eff_family, eff_model;
	int vrm_ret;

	if (c->x86 < 6) return 0;	/* any CPU with familly lower than 6
				 	dont have VID and/or CPUID */
	eax = cpuid_eax(1);
	eff_family = ((eax & 0x00000F00)>>8);
	eff_model  = ((eax & 0x000000F0)>>4);
	if (eff_family == 0xF) {	/* use extended model & family */
		eff_family += ((eax & 0x00F00000)>>20);
		eff_model += ((eax & 0x000F0000)>>16)<<4;
	}
	vrm_ret = find_vrm(eff_family,eff_model,c->x86_vendor);
	if (vrm_ret == 0)
		printk(KERN_INFO "i2c-sensor.o: Unknown VRM version of your"
		" x86 CPU\n");
	return vrm_ret;
}

/* and now for something completely different for Non-x86 world*/
#else
int i2c_which_vrm(void)
{
	printk(KERN_INFO "i2c-sensor.o: Unknown VRM version of your CPU\n");
	return 0;
}
#endif

EXPORT_SYMBOL(i2c_which_vrm);
