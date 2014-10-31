#!/usr/bin/perl
#
# Calculate the amount of space needed to run the kernel, including room for
# the .bss and .brk sections.
#
# Usage:
# objdump -h a.out | perl calc_run_size.pl
use strict;

my $mem_size = 0;
my $file_offset = 0;

my $sections=" *[0-9]+ \.(?:bss|brk) +";
while (<>) {
	if (/^$sections([0-9a-f]+) +(?:[0-9a-f]+ +){2}([0-9a-f]+)/) {
		my $size = hex($1);
		my $offset = hex($2);
		$mem_size += $size;
		if ($file_offset == 0) {
			$file_offset = $offset;
		} elsif ($file_offset != $offset) {
			die ".bss and .brk lack common file offset\n";
		}
	}
}

if ($file_offset == 0) {
	die "Never found .bss or .brk file offset\n";
}
printf("%d\n", $mem_size + $file_offset);
