/*
    hwmon-vid.c - VID/VRM/VRD voltage conversions

    Copyright (c) 2004 Rudolf Marek <r.marek@sh.cvut.cz>

    Partly imported from i2c-vid.h of the lm_sensors project
    Copyright (c) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>
    With assistance from Trent Piepho <xyzzy@speakeasy.org>

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
#include <linux/hwmon-vid.h>

/*
    Common code for decoding VID pins.

    References:

    For VRM 8.4 to 9.1, "VRM x.y DC-DC Converter Design Guidelines",
    available at http://developer.intel.com/.

    For VRD 10.0 and up, "VRD x.y Design Guide",
    available at http://developer.intel.com/.

    AMD Opteron processors don't follow the Intel specifications.
    I'm going to "make up" 2.4 as the spec number for the Opterons.
    No good reason just a mnemonic for the 24x Opteron processor
    series.

    Opteron VID encoding is:
       00000  =  1.550 V
       00001  =  1.525 V
        . . . .
       11110  =  0.800 V
       11111  =  0.000 V (off)
*/

/* vrm is the VRM/VRD document version multiplied by 10.
   val is the 4-, 5- or 6-bit VID code.
   Returned value is in mV to avoid floating point in the kernel. */
int vid_from_reg(int val, int vrm)
{
	int vid;

	switch(vrm) {

	case  0:
		return 0;

	case 100:               /* VRD 10.0 */
		if((val & 0x1f) == 0x1f)
			return 0;
		if((val & 0x1f) <= 0x09 || val == 0x0a)
			vid = 10875 - (val & 0x1f) * 250;
		else
			vid = 18625 - (val & 0x1f) * 250;
		if(val & 0x20)
			vid -= 125;
		vid /= 10;      /* only return 3 dec. places for now */
		return vid;

	case 24:                /* Opteron processor */
		return(val == 0x1f ? 0 : 1550 - val * 25);

	case 91:		/* VRM 9.1 */
	case 90:		/* VRM 9.0 */
		return(val == 0x1f ? 0 :
		                       1850 - val * 25);

	case 85:		/* VRM 8.5 */
		return((val & 0x10  ? 25 : 0) +
		       ((val & 0x0f) > 0x04 ? 2050 : 1250) -
		       ((val & 0x0f) * 50));

	case 84:		/* VRM 8.4 */
		val &= 0x0f;
				/* fall through */
	default:		/* VRM 8.2 */
		return(val == 0x1f ? 0 :
		       val & 0x10  ? 5100 - (val) * 100 :
		                     2050 - (val) * 50);
	}
}


/*
    After this point is the code to automatically determine which
    VRM/VRD specification should be used depending on the CPU.
*/

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
	{X86_VENDOR_INTEL, 0x6, 0xB, 85},	/* Tualatin */
	{X86_VENDOR_INTEL, 0x6, ANY, 82},	/* any P6 */
	{X86_VENDOR_INTEL, 0x7, ANY, 0},	/* Itanium */
	{X86_VENDOR_INTEL, 0xF, 0x0, 90},	/* P4 */
	{X86_VENDOR_INTEL, 0xF, 0x1, 90},	/* P4 Willamette */
	{X86_VENDOR_INTEL, 0xF, 0x2, 90},	/* P4 Northwood */
	{X86_VENDOR_INTEL, 0xF, 0x3, 100},	/* P4 Prescott */
	{X86_VENDOR_INTEL, 0xF, 0x4, 100},	/* P4 Prescott */
	{X86_VENDOR_INTEL, 0x10,ANY, 0},	/* Itanium 2 */
	{X86_VENDOR_UNKNOWN, ANY, ANY, 0}	/* stop here */
};

static int find_vrm(u8 eff_family, u8 eff_model, u8 vendor)
{
	int i = 0;

	while (vrm_models[i].vendor!=X86_VENDOR_UNKNOWN) {
		if (vrm_models[i].vendor==vendor)
			if ((vrm_models[i].eff_family==eff_family)
			 && ((vrm_models[i].eff_model==eff_model) ||
			     (vrm_models[i].eff_model==ANY)))
				return vrm_models[i].vrm_type;
		i++;
	}

	return 0;
}

int vid_which_vrm(void)
{
	struct cpuinfo_x86 *c = cpu_data;
	u32 eax;
	u8 eff_family, eff_model;
	int vrm_ret;

	if (c->x86 < 6)		/* Any CPU with family lower than 6 */
		return 0;	/* doesn't have VID and/or CPUID */

	eax = cpuid_eax(1);
	eff_family = ((eax & 0x00000F00)>>8);
	eff_model  = ((eax & 0x000000F0)>>4);
	if (eff_family == 0xF) {	/* use extended model & family */
		eff_family += ((eax & 0x00F00000)>>20);
		eff_model += ((eax & 0x000F0000)>>16)<<4;
	}
	vrm_ret = find_vrm(eff_family,eff_model,c->x86_vendor);
	if (vrm_ret == 0)
		printk(KERN_INFO "hwmon-vid: Unknown VRM version of your "
		       "x86 CPU\n");
	return vrm_ret;
}

/* and now something completely different for the non-x86 world */
#else
int vid_which_vrm(void)
{
	printk(KERN_INFO "hwmon-vid: Unknown VRM version of your CPU\n");
	return 0;
}
#endif

EXPORT_SYMBOL(vid_from_reg);
EXPORT_SYMBOL(vid_which_vrm);

MODULE_AUTHOR("Rudolf Marek <r.marek@sh.cvut.cz>");

MODULE_DESCRIPTION("hwmon-vid driver");
MODULE_LICENSE("GPL");
