#!/usr/bin/perl

# Copyright © 2009 IBM Corporation

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.

# This script checks the relcoations of a vmlinux for "suspicious"
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

	# Only look at relcoation lines.
	next if (!/\s+R_/);

	# These relocations are okay
	next if (/R_PPC64_RELATIVE/ or /R_PPC64_NONE/ or
	         /R_PPC64_ADDR64\s+mach_/);

	# If we see this type of relcoation it's an idication that
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
	print "WARNING: You need at binutils >= 2.19 to build a ".
	      "CONFIG_RELCOATABLE kernel\n";
}
