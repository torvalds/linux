#!/usr/bin/perl
#
# Generate the x86_cap_flags[] array from include/asm-x86/cpufeature.h
#

($in, $out) = @ARGV;

open(IN, "< $in\0")   or die "$0: cannot open: $in: $!\n";
open(OUT, "> $out\0") or die "$0: cannot create: $out: $!\n";

print OUT "#include <asm/cpufeature.h>\n\n";
print OUT "const char * const x86_cap_flags[NCAPINTS*32] = {\n";

while (defined($line = <IN>)) {
	if ($line =~ /^\s*\#\s*define\s+(X86_FEATURE_(\S+))\s+(.*)$/) {
		$macro = $1;
		$feature = $2;
		$tail = $3;
		if ($tail =~ /\/\*\s*\"([^"]*)\".*\*\//) {
			$feature = $1;
		}

		if ($feature ne '') {
			printf OUT "\t%-32s = \"%s\",\n",
				"[$macro]", "\L$feature";
		}
	}
}
print OUT "};\n";

close(IN);
close(OUT);
