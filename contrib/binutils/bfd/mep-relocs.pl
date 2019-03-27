#!/usr/bin/perl
# -*- perl -*-
#
# Toshiba MeP Media Engine Relocation Generator
# Copyright (C) 2001, 2007 Free Software Foundation, Inc.
# This file is part of BFD.
# Originally written by DJ Delorie <dj@redhat.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */


# Usage: Run this anywhere inside your source tree.  It will read
# include/elf/mep.h and scan the comments therein.  It will renumber
# the relocs to be sequential (this is needed so that bfd/elf32-mep.h
# works) if needed.  It will then update the reloc list in bfd/reloc.c
# and the howto, mapping, and apply routines in bfd/elf32-mep.c.  You
# can then regenerate bfd-in2.h and check everything in.

# FIXME: After the relocation list is finalized, change this to
# *verify* the reloc list, rather than resequence it.

while (! -f "include/elf/mep.h" && ! -f "bfd/reloc.c") {
    chdir "..";
    $pwd = `pwd`;
    if ($pwd !~ m@/.*/@) {
	print STDERR "Cannot find include/elf/mep.h or bfd/reloc.h\n";
	exit 1;
    }
}
$pwd = `pwd`;
print "srctop is $pwd";

printf "Reading include/elf/mep.h ...\n";
open(MEPH, "include/elf/mep.h");
open(MEPHO, "> include/elf/mep.h.new") || die("mep.h.new create: $!");
$val = 0;
while (<MEPH>) {
    if (($pre,$rel,$rest) = /(.*RELOC_NUMBER \()([^,]+), *\d+(.*)/) {
	$rest =~ s/[\r\n]+$//;
	print (MEPHO "$pre$rel, $val$rest\n") || die("mep.h.new write: $!");
	$val ++;
	$rel =~ s/R_MEP_//;
	push(@relocs, $rel);

	$rest =~ s@.*/\* @@;
	($pattern, $sign, $attrs) = $rest =~ m@(.*) ([US]) (.*)\*/@;
	$pattern =~ s/ //g;
	push(@pattern, $pattern);
	push(@sign, $sign);
	push(@attrs, $attrs);

	printf "%4d $rel p=`$pattern' s=`$sign' a=`$attrs'\n", $#pattern;

    } else {
	print(MEPHO) || die("mep.h.new write: $!");
    }
}
close(MEPH);
close(MEPHO) || die("mep.h.new close: $!");

&swapfile("include/elf/mep.h");

redo_file ("bfd/reloc.c",
	   "",
	   "ENUMDOC\n  Toshiba Media Processor Relocations.\n\nCOMMENT\n",
	   "ENUM\n  BFD_RELOC_MEP_%s\n",
	   "");

$autogen = "    /* This section generated from bfd/mep-relocs.pl from include/elf/mep.h.  */\n";

redo_file ("bfd/elf32-mep.c",
	   "MEPRELOC:HOWTO",
	   $autogen,
	   "MEPRELOC:END",
	   "",
	   "&emit_howto();",
	   "MEPRELOC:MAP",
	   $autogen,
	   "MEPRELOC:END",
	   "",
	   "    MAP(%s);\n",
	   "MEPRELOC:APPLY",
	   $autogen,
	   "MEPRELOC:END",
	   "",
	   "&emit_apply();",
	   );

sub mask2shifts {
    my ($mask) = @_;
    my ($bits, $left, $right, $ci, $c, $cv);
    $bits = 0;
    $left = 0;
    $right = 32;
    for ($ci=0; $ci<length($mask); $ci++) {
	$c = substr($mask, $ci, 1);
	$left++;
	next if $c eq '-';
	$left = 0;
	$cv = ord($c) - ord('0');
	$cv -= ord('a') - ord('9') - 1 if $cv > 9;
	$right = $cv unless $right < $cv;
	$bits = $cv+1 unless $bits > $cv+1;
    }
    $mask =~ tr/-/1/c;
    $mask =~ tr/-/0/;
    ($rmask = $mask) =~ tr/01/10/;
    $mask = unpack("H*", pack("B*", $mask));
    $rmask = unpack("H*", pack("B*", $rmask));
    return ($bits, $left, $right, $mask, $rmask);
}

sub emit_howto {
    for ($i=2; $i<=$#relocs; $i++) {
	$mask = $pattern[$i];

	if (length($mask) == 8)     { $bytesize = 0; }
	elsif (length($mask) == 16) { $bytesize = 1; }
	elsif (length($mask) == 32) { $bytesize = 2; }

	($bits, $left, $right, $mask) = mask2shifts ($mask);
	$bits[$i] = $bits;
	$pcrel = 0;
	$pcrel = 1 if $attrs[$i] =~ /pc-rel/i;
	$overflow = $sign[$i];
	$overflow = 'N' if $attrs[$i] =~ /no-overflow/;

	$c = "$relocs[$i],";
	printf(NEW "  MEPREL (R_MEP_%-10s%d,%3d,%2d,%2d,%2d,%2s, 0x%s),\n",
	       $c, $bytesize, $bits, $left, $right, $pcrel, $overflow, $mask);
    }
}

sub emit_apply {
    for ($i=2; $i<=$#relocs; $i++) {
	$v = "u";
	$v = "s" if $sign[$i] =~ /S/;
	if (length($pattern[$i]) == 8) {
	    $e = ''; # no endian swap for bytes
	} elsif ($pattern[$i] =~ /-/ || length($pattern[$i]) == 16) {
	    $e = '^e2'; # endian swap - 2byte words only
	} else {
	    $e = '^e4' # endian swap for data
	}
	print NEW "    case R_MEP_$relocs[$i]: /* $pattern[$i] */\n";
	if ($attrs[$i] =~ /tp-rel/i) {
	    print NEW "      $v -= mep_tpoff_base(rel->r_offset);\n";
	}
	if ($attrs[$i] =~ /gp-rel/i) {
	    print NEW "      $v -= mep_sdaoff_base(rel->r_offset);\n";
	}
	if ($attrs[$i] !~ /no-overflow/ && $bits[$i] < 32) {
	    if ($v eq "u") {
		$max = (1 << $bits[$i]) - 1;
		print NEW "      if (u > $max) r = bfd_reloc_overflow;\n";
	    } else {
		$min = -(1 << ($bits[$i]-1));
		$max = (1 << ($bits[$i]-1)) - 1;
		print NEW "      if ($min > s || s > $max) r = bfd_reloc_overflow;\n";
	    }
	}
	for ($b=0; $b<length($pattern[$i]); $b += 8) {
	    $mask = substr($pattern[$i], $b, 8);
	    ($bits, $left, $right, $mask, $rmask) = mask2shifts ($mask);
	    if ($left > $right) { $left -= $right; $right = 0; }
	    else { $right -= $left; $left = 0; }

	    if ($mask ne "00") {
		$bb = $b / 8;
		print NEW "      byte[$bb$e] = ";
		print NEW "(byte[$bb$e] & 0x$rmask) | " if $rmask ne "00";
		if ($left) {
		    print NEW "(($v << $left) & 0x$mask)";
		} elsif ($right) {
		    print NEW "(($v >> $right) & 0x$mask)";
		} else {
		    print NEW "($v & 0x$mask)";
		}
		print NEW ";\n";
	    }
	}
	print NEW "      break;\n";
    }
}


#-----------------------------------------------------------------------------

sub redo_file {
    my ($file, @control) = @_;
    open(OLD, $file);
    open(NEW, "> $file.new") || die("$file.new create: $!");

    print "Scanning file $file ...\n";

    while (1) {
	$start = shift @control;
	$prefix = shift @control;
	$end = shift @control;
	$suffix = shift @control;
	$pattern = shift @control;

	if (!$start) {
	    print NEW while <OLD>;
	    last;
	}

	print "  looking for $start\n";
	while (<OLD>) {
	    print NEW;
	    last if /\Q$start\E/;
	}
	print "can't find $start\n" unless $_;
	last unless $_;

	print NEW $prefix;
	if ($pattern =~ /^\&/) {
	    eval $pattern;
	    die("$pattern: $@") if $@;
	} else {
	    for $i (2..$#relocs) {
		printf (NEW "$pattern", $relocs[$i]) || die("$file.new write: $!");
		$pattern =~ s/^ENUM\n/ENUMX\n/;
	    }
	}
	print NEW $suffix;
	while (<OLD>) {
	    last if /\Q$end\E/;
	}
	print NEW;
    }

    close(OLD);
    close(NEW) || die("$file.new close: $!");
    &swapfile($file);
}

#-----------------------------------------------------------------------------

sub swapfile {
    my ($f) = @_;
    if ( ! -f "$f.save") {
	system "cp $f $f.save";
    }
    open(ORIG, $f);
    open(NEW, "$f.new");
    while (<ORIG>) {
	$n = <NEW>;
	if ($n ne $_) {
	    close(ORIG);
	    close(NEW);
	    print "  Updating $f\n";
	    rename "$f", "$f.old";
	    rename "$f.new", "$f";
	    return;
	}
    }
    close(ORIG);
    close(NEW);
    print "  No change to $f\n";
    unlink "$f.new";
}
