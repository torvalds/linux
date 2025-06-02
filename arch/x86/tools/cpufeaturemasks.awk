#!/usr/bin/awk
#
# Convert cpufeatures.h to a list of compile-time masks
# Note: this blithely assumes that each word has at least one
# feature defined in it; if not, something else is wrong!
#

BEGIN {
	printf "#ifndef _ASM_X86_CPUFEATUREMASKS_H\n";
	printf "#define _ASM_X86_CPUFEATUREMASKS_H\n\n";

	file = 0
}

FNR == 1 {
	++file;

	# arch/x86/include/asm/cpufeatures.h
	if (file == 1)
		FS = "[ \t()*+]+";

	# .config
	if (file == 2)
		FS = "=";
}

# Create a dictionary of sorts, containing all defined feature bits
file == 1 && $1 ~ /^#define$/ && $2 ~ /^X86_FEATURE_/ {
	nfeat = $3 * $4 + $5;
	feat = $2;
	sub(/^X86_FEATURE_/, "", feat);
	feats[nfeat] = feat;
}
file == 1 && $1 ~ /^#define$/ && $2 == "NCAPINTS" {
	ncapints = int($3);
}

# Create a dictionary featstat[REQUIRED|DISABLED, FEATURE_NAME] = on | off
file == 2 && $1 ~ /^CONFIG_X86_(REQUIRED|DISABLED)_FEATURE_/ {
	on = ($2 == "y");
	if (split($1, fs, "CONFIG_X86_|_FEATURE_") == 3)
		featstat[fs[2], fs[3]] = on;
}

END {
	sets[1] = "REQUIRED";
	sets[2] = "DISABLED";

	for (ns in sets) {
		s = sets[ns];

		printf "/*\n";
		printf " * %s features:\n", s;
		printf " *\n";
		fstr = "";
		for (i = 0; i < ncapints; i++) {
			mask = 0;
			for (j = 0; j < 32; j++) {
				feat = feats[i*32 + j];
				if (featstat[s, feat]) {
					nfstr = fstr " " feat;
					if (length(nfstr) > 72) {
						printf " *   %s\n", fstr;
						nfstr = " " feat;
					}
					fstr = nfstr;
					mask += (2 ^ j);
				}
			}
			masks[i] = mask;
		}
		printf " *   %s\n */\n", fstr;

		for (i = 0; i < ncapints; i++)
			printf "#define %s_MASK%d\t0x%08xU\n", s, i, masks[i];

		printf "\n#define %s_MASK_BIT_SET(x)\t\t\t\\\n", s;
		printf "\t((\t\t\t\t\t";
		for (i = 0; i < ncapints; i++) {
			if (masks[i])
				printf "\t\\\n\t\t((x) >> 5) == %2d ? %s_MASK%d :", i, s, i;
		}
		printf " 0\t\\\n";
		printf "\t) & (1U << ((x) & 31)))\n\n";
	}

	printf "#endif /* _ASM_X86_CPUFEATUREMASKS_H */\n";
}
