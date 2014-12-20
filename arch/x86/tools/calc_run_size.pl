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
			# BFD linker shows the same file offset in ELF.
			# Gold linker shows them as consecutive.
			next if ($file_offset + $mem_size == $offset + $size);

			printf STDERR "file_offset: 0x%lx\n", $file_offset;
			printf STDERR "mem_size: 0x%lx\n", $mem_size;
			printf STDERR "offset: 0x%lx\n", $offset;
			printf STDERR "size: 0x%lx\n", $size;

			die ".bss and .brk are non-contiguous\n";
		}
	}
}

if ($file_offset == 0) {
	die "Never found .bss or .brk file offset\n";
}
printf("%d\n", $mem_size + $file_offset);
