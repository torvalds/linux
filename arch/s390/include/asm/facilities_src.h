/*
 *    Copyright IBM Corp. 2015
 */

#ifndef S390_GEN_FACILITIES_C
#error "This file can only be included by gen_facilities.c"
#endif

#include <linux/kconfig.h>

struct facility_def {
	char *name;
	int *bits;
};

static struct facility_def facility_defs[] = {
	{
		/*
		 * FACILITIES_ALS contains the list of facilities that are
		 * required to run a kernel that is compiled e.g. with
		 * -march=<machine>.
		 */
		.name = "FACILITIES_ALS",
		.bits = (int[]){
#ifdef CONFIG_HAVE_MARCH_Z900_FEATURES
			0,  /* N3 instructions */
			1,  /* z/Arch mode installed */
#endif
#ifdef CONFIG_HAVE_MARCH_Z990_FEATURES
			18, /* long displacement facility */
#endif
#ifdef CONFIG_HAVE_MARCH_Z9_109_FEATURES
			7,  /* stfle */
			16, /* extended translation facility 2 */
			17, /* message security assist */
			20, /* HFP-multiply-and-add */
			21, /* extended-immediate facility */
			22, /* extended-translation facility 3 */
			23, /* HFP-unnormalized-extension */
			24, /* ETF2-enhancement */
			25, /* store clock fast */
			30, /* ETF3-enhancement */
#endif
#ifdef CONFIG_HAVE_MARCH_Z10_FEATURES
			26, /* parsing enhancement facility */
			27, /* mvcos */
			32, /* compare and swap and store */
			33, /* compare and swap and store 2 */
			34, /* general extension facility */
			35, /* execute extensions */
			41, /* floating point support enhancement */
			42, /* DFP facility */
			44, /* PFPO */
#endif
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
			37, /* floating point extension */
			45, /* fast-BCR, etc. */
#endif
#ifdef CONFIG_HAVE_MARCH_ZEC12_FEATURES
			48, /* decimal floating point zoned */
			49, /* misc-instruction-extensions */
			52, /* interlocked facility 2 */
#endif
			-1 /* END */
		}
	},
};
