#!/usr/bin/perl -w
#
# Generate the x86_cap_flags[] array from include/asm-x86/cpufeature.h
#

($in, $out) = @ARGV;

open(IN, "< $in\0")   or die "$0: cannot open: $in: $!\n";
open(OUT, "> $out\0") or die "$0: cannot create: $out: $!\n";

print OUT "#ifndef _ASM_X86_CPUFEATURE_H\n";
print OUT "#include <asm/cpufeature.h>\n";
print OUT "#endif\n";
print OUT "\n";
print OUT "const char * const x86_cap_flags[NCAPINTS*32] = {\n";

%features = ();
$err = 0;

while (defined($line = <IN>)) {
	if ($line =~ /^\s*\#\s*define\s+(X86_FEATURE_(\S+))\s+(.*)$/) {
		$macro = $1;
		$feature = "\L$2";
		$tail = $3;
		if ($tail =~ /\/\*\s*\"([^"]*)\".*\*\//) {
			$feature = "\L$1";
		}

		next if ($feature eq '');

		if ($features{$feature}++) {
			print STDERR "$in: duplicate feature name: $feature\n";
			$err++;
		}
		printf OUT "\t%-32s = \"%s\",\n", "[$macro]", $feature;
	}
}
print OUT "};\n";

close(IN);
close(OUT);

if ($err) {
	unlink($out);
	exit(1);
}

exit(0);
