// SPDX-License-Identifier: GPL-2.0
/*
 * Simple program to generate bitmaps, for example facility lists.
 *
 *    Copyright IBM Corp. 2015, 2018
 *
 */

#include <getopt.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct bitmap_def {
	char *name;
	int *bits;
	int inverted;
	int maxbit;
};

static struct bitmap_def facility_defs[] = {
	{
		/*
		 * FACILITIES_ALS contains the list of facilities that are
		 * required to run a kernel that is compiled e.g. with
		 * -march=<machine>.
		 */
		.name = "FACILITIES_ALS",
		.inverted = 1,
		.bits = (int[]){
			0,  /* N3 instructions */
			1,  /* z/Arch mode installed */
			3,  /* dat-enhancement 1 */
			18, /* long displacement facility */
			21, /* extended-immediate facility */
			25, /* store clock fast */
			27, /* mvcos */
			32, /* compare and swap and store */
			33, /* compare and swap and store 2 */
			34, /* general instructions extension */
			35, /* execute extensions */
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
			45, /* fast-BCR, etc. */
#endif
#ifdef CONFIG_HAVE_MARCH_ZEC12_FEATURES
			49, /* misc-instruction-extensions */
			52, /* interlocked facility 2 */
#endif
#ifdef CONFIG_HAVE_MARCH_Z13_FEATURES
			53, /* load-and-zero-rightmost-byte, etc. */
			129, /* vector */
#endif
#ifdef CONFIG_HAVE_MARCH_Z14_FEATURES
			58, /* miscellaneous-instruction-extension 2 */
#endif
#ifdef CONFIG_HAVE_MARCH_Z15_FEATURES
			61, /* miscellaneous-instruction-extension 3 */
#endif
#ifdef CONFIG_HAVE_MARCH_Z17_FEATURES
			84, /* miscellaneous-instruction-extension 4 */
#endif
			-1 /* END */
		}
	},
	{
		/*
		 * FACILITIES_KVM contains the list of facilities that are part
		 * of the default facility mask and list that are passed to the
		 * initial CPU model. If no CPU model is used, this, together
		 * with the non-hypervisor managed bits, is the maximum list of
		 * guest facilities supported by KVM.
		 */
		.name = "FACILITIES_KVM",
		.inverted = 1,
		.bits = (int[]){
			0,  /* N3 instructions */
			1,  /* z/Arch mode installed */
			2,  /* z/Arch mode active */
			3,  /* DAT-enhancement */
			4,  /* idte segment table */
			5,  /* idte region table */
			6,  /* ASN-and-LX reuse */
			7,  /* stfle */
			8,  /* enhanced-DAT 1 */
			9,  /* sense-running-status */
			10, /* conditional sske */
			13, /* ipte-range */
			14, /* nonquiescing key-setting */
			73, /* transactional execution */
			75, /* access-exception-fetch/store indication */
			76, /* msa extension 3 */
			77, /* msa extension 4 */
			78, /* enhanced-DAT 2 */
			130, /* instruction-execution-protection */
			131, /* enhanced-SOP 2 and side-effect */
			139, /* multiple epoch facility */
			146, /* msa extension 8 */
			150, /* enhanced sort */
			151, /* deflate conversion */
			155, /* msa extension 9 */
			165, /* nnpa facility */
			170, /* ineffective-nonconstrained-transaction facility */
			193, /* bear enhancement facility */
			194, /* rdp enhancement facility */
			196, /* processor activity instrumentation facility */
			197, /* processor activity instrumentation extension 1 */
			201, /* concurrent-functions facility */
			-1  /* END */
		}
	},
	{
		/*
		 * FACILITIES_KVM_CPUMODEL contains the list of facilities
		 * that can be enabled by CPU model code if the host supports
		 * it. These facilities are not passed to the guest without
		 * CPU model support.
		 */

		.name = "FACILITIES_KVM_CPUMODEL",
		.inverted = 1,
		.bits = (int[]){
			12, /* AP Query Configuration Information */
			15, /* AP Facilities Test */
			156, /* etoken facility */
			-1  /* END */
		}
	},
};

static struct bitmap_def pgm_check_defs[] = {
	{
		.name = "PGM_CHECKS_NULLIFIED",
		.inverted = 0,
		.maxbit = 127,
		.bits = (int[]){
			0x10, /* Segment Translation */
			0x11, /* Page Translation */
			0x16, /* Trace Table */
			0x20, /* AFX Translation */
			0x21, /* ASX Translation */
			0x22, /* LX Translation */
			0x23, /* EX Translation */
			0x24, /* Primary Authority */
			0x25, /* Secondary Authority */
			0x26, /* LFX Translation */
			0x27, /* LSX Translation */
			0x29, /* ALEN Translation */
			0x2a, /* ALE Sequence */
			0x2b, /* ASTE Validity */
			0x2c, /* ASTE Sequence */
			0x2d, /* Extended Authority */
			0x2e, /* LSTE Sequence */
			0x2f, /* ASTE Instance */
			0x30, /* Stack Full */
			0x31, /* Stack Empty */
			0x32, /* Stack Specification */
			0x33, /* Stack Type */
			0x34, /* Stack Operation */
			0x38, /* ASCE Type */
			0x39, /* Region First Translation */
			0x3a, /* Region Second Translation */
			0x3b, /* Region Third Translation */
			0x3d, /* Secure Storage Access */
			-1  /* END */
		}
	},
};

static void print_bitmap(struct bitmap_def *def)
{
	unsigned int high, bit, dword, i;
	unsigned long long *array;

	high = def->maxbit ? def->maxbit / 64 : 0;
	array = calloc(high + 1, 8);
	if (!array)
		exit(EXIT_FAILURE);
	for (i = 0; def->bits[i] != -1; i++) {
		bit = def->inverted ? 63 - (def->bits[i] & 63) : (def->bits[i] & 63);
		dword = def->bits[i] / 64;
		if (dword > high) {
			array = realloc(array, (dword + 1) * 8);
			if (!array)
				exit(EXIT_FAILURE);
			memset(array + high + 1, 0, (dword - high) * 8);
			high = dword;
		}
		array[dword] |= 1ULL << bit;
	}
	printf("#define %s ", def->name);
	for (i = 0; i <= high; i++)
		printf("_AC(0x%016llx,UL)%c", array[i], i < high ? ',' : '\n');
	free(array);
}

static void print_header(const char *guard, struct bitmap_def *defs, size_t num)
{
	unsigned int i;

	printf("#ifndef __ASM_S390_%s_DEFS__\n", guard);
	printf("#define __ASM_S390_%s_DEFS__\n", guard);
	printf("/*\n");
	printf(" * DO NOT MODIFY.\n");
	printf(" *\n");
	printf(" * This file was generated by %s\n", __FILE__);
	printf(" */\n\n");
	printf("#include <linux/const.h>\n\n");
	for (i = 0; i < num; i++)
		print_bitmap(&defs[i]);
	printf("\n#endif\n");
}

static const struct option long_options[] = {
	{ "facility", no_argument, NULL, 'f' },
	{ "pgmcheck", no_argument, NULL, 'p' },
	{ NULL, 0, NULL, 0 }
};

int main(int argc, char **argv)
{
	int opt;

	opt = getopt_long(argc, argv, "", long_options, NULL);
	if (opt == -1)
		return 1;
	switch (opt) {
	case 'f':
		print_header("FACILITY", facility_defs, ARRAY_SIZE(facility_defs));
		break;
	case 'p':
		print_header("PGMCHECK", pgm_check_defs, ARRAY_SIZE(pgm_check_defs));
		break;
	default:
		return 1;
	}
	return 0;
}
