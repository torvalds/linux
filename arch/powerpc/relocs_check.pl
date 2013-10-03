#!/usr/bin/perl

# Copyright © 2009 IBM Corporation

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.

# This script checks the relocations of a vmlinux for "suspicious"
# relocations.

use strict;
use warnings;

if ($#ARGV != 1) {
	die "$0 [path to objdump] [path to vmlinux]\n";
}

# Have Kbuild supply the path to objdump so we handle cross compilation.
my $objdump = shift;
my $vmlinux = shift;
my $bad_relocs_count = 0;
my $bad_relocs = "";
my $old_binutils = 0;

open(FD, "$objdump -R $vmlinux|") or die;
while (<FD>) {
	study $_;

	# Only look at relocation lines.
	next if (!/\s+R_/);

	# These relocations are okay
	# On PPC64:
	# 	R_PPC64_RELATIVE, R_PPC64_NONE, R_PPC64_ADDR64
	# On PPC:
	# 	R_PPC_RELATIVE, R_PPC_ADDR16_HI, 
	# 	R_PPC_ADDR16_HA,R_PPC_ADDR16_LO,
	# 	R_PPC_NONE

	next if (/\bR_PPC64_RELATIVE\b/ or /\bR_PPC64_NONE\b/ or
	         /\bR_PPC64_ADDR64\s+mach_/);
	next if (/\bR_PPC_ADDR16_LO\b/ or /\bR_PPC_ADDR16_HI\b/ or
		 /\bR_PPC_ADDR16_HA\b/ or /\bR_PPC_RELATIVE\b/ or
		 /\bR_PPC_NONE\b/);

	# If we see this type of relocation it's an idication that
	# we /may/ be using an old version of binutils.
	if (/R_PPC64_UADDR64/) {
		$old_binutils++;
	}

	$bad_relocs_count++;
	$bad_relocs .= $_;
}

if ($bad_relocs_count) {
	print "WARNING: $bad_relocs_count bad relocations\n";
	print $bad_relocs;
}

if ($old_binutils) {
	print "WARNING: You need at least binutils >= 2.19 to build a ".
	      "CONFIG_RELOCATABLE kernel\n";
}
