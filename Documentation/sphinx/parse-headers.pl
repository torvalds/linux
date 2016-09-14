#!/usr/bin/perl
use strict;
use Text::Tabs;

# Uncomment if debug is needed
#use Data::Dumper;

# change to 1 to generate some debug prints
my $debug = 0;

if (scalar @ARGV < 2 || scalar @ARGV > 3) {
	die "Usage:\n\t$0 <file in> <file out> [<exceptions file>]\n";
}

my ($file_in, $file_out, $file_exceptions) = @ARGV;

my $data;
my %ioctls;
my %defines;
my %typedefs;
my %enums;
my %enum_symbols;
my %structs;

#
# read the file and get identifiers
#

my $is_enum = 0;
my $is_comment = 0;
open IN, $file_in or die "Can't open $file_in";
while (<IN>) {
	$data .= $_;

	my $ln = $_;
	if (!$is_comment) {
		$ln =~ s,/\*.*(\*/),,g;

		$is_comment = 1 if ($ln =~ s,/\*.*,,);
	} else {
		if ($ln =~ s,^(.*\*/),,) {
			$is_comment = 0;
		} else {
			next;
		}
	}

	if ($is_enum && $ln =~ m/^\s*([_\w][\w\d_]+)\s*[\,=]?/) {
		my $s = $1;
		my $n = $1;
		$n =~ tr/A-Z/a-z/;
		$n =~ tr/_/-/;

		$enum_symbols{$s} = $n;

		$is_enum = 0 if ($is_enum && m/\}/);
		next;
	}
	$is_enum = 0 if ($is_enum && m/\}/);

	if ($ln =~ m/^\s*#\s*define\s+([_\w][\w\d_]+)\s+_IO/) {
		my $s = $1;
		my $n = $1;
		$n =~ tr/A-Z/a-z/;

		$ioctls{$s} = $n;
		next;
	}

	if ($ln =~ m/^\s*#\s*define\s+([_\w][\w\d_]+)\s+/) {
		my $s = $1;
		my $n = $1;
		$n =~ tr/A-Z/a-z/;
		$n =~ tr/_/-/;

		$defines{$s} = $n;
		next;
	}

	if ($ln =~ m/^\s*typedef\s+.*\s+([_\w][\w\d_]+);/) {
		my $s = $1;
		my $n = $1;
		$n =~ tr/A-Z/a-z/;
		$n =~ tr/_/-/;

		$typedefs{$s} = $n;
		next;
	}
	if ($ln =~ m/^\s*enum\s+([_\w][\w\d_]+)\s+\{/
	    || $ln =~ m/^\s*enum\s+([_\w][\w\d_]+)$/
	    || $ln =~ m/^\s*typedef\s*enum\s+([_\w][\w\d_]+)\s+\{/
	    || $ln =~ m/^\s*typedef\s*enum\s+([_\w][\w\d_]+)$/) {
		my $s = $1;
		my $n = $1;
		$n =~ tr/A-Z/a-z/;
		$n =~ tr/_/-/;

		$enums{$s} = $n;

		$is_enum = $1;
		next;
	}
	if ($ln =~ m/^\s*struct\s+([_\w][\w\d_]+)\s+\{/
	    || $ln =~ m/^\s*struct\s+([[_\w][\w\d_]+)$/
	    || $ln =~ m/^\s*typedef\s*struct\s+([_\w][\w\d_]+)\s+\{/
	    || $ln =~ m/^\s*typedef\s*struct\s+([[_\w][\w\d_]+)$/
	    ) {
		my $s = $1;
		my $n = $1;
		$n =~ tr/A-Z/a-z/;
		$n =~ tr/_/-/;

		$structs{$s} = $n;
		next;
	}
}
close IN;

#
# Handle multi-line typedefs
#

my @matches = ($data =~ m/typedef\s+struct\s+\S+?\s*\{[^\}]+\}\s*(\S+)\s*\;/g,
	       $data =~ m/typedef\s+enum\s+\S+?\s*\{[^\}]+\}\s*(\S+)\s*\;/g,);
foreach my $m (@matches) {
		my $s = $m;
		my $n = $m;
		$n =~ tr/A-Z/a-z/;
		$n =~ tr/_/-/;

		$typedefs{$s} = $n;
	next;
}

#
# Handle exceptions, if any
#

if ($file_exceptions) {
	open IN, $file_exceptions or die "Can't read $file_exceptions";
	while (<IN>) {
		next if (m/^\s*$/ || m/^\s*#/);

		# Parsers to ignore a symbol

		if (m/^ignore\s+ioctl\s+(\S+)/) {
			delete $ioctls{$1} if (exists($ioctls{$1}));
			next;
		}
		if (m/^ignore\s+define\s+(\S+)/) {
			delete $defines{$1} if (exists($defines{$1}));
			next;
		}
		if (m/^ignore\s+typedef\s+(\S+)/) {
			delete $typedefs{$1} if (exists($typedefs{$1}));
			next;
		}
		if (m/^ignore\s+enum\s+(\S+)/) {
			delete $enums{$1} if (exists($enums{$1}));
			next;
		}
		if (m/^ignore\s+struct\s+(\S+)/) {
			delete $structs{$1} if (exists($structs{$1}));
			next;
		}
		if (m/^ignore\s+symbol\s+(\S+)/) {
			delete $enum_symbols{$1} if (exists($enum_symbols{$1}));
			next;
		}

		# Parsers to replace a symbol

		if (m/^replace\s+ioctl\s+(\S+)\s+(\S+)/) {
			$ioctls{$1} = $2 if (exists($ioctls{$1}));
			next;
		}
		if (m/^replace\s+define\s+(\S+)\s+(\S+)/) {
			$defines{$1} = $2 if (exists($defines{$1}));
			next;
		}
		if (m/^replace\s+typedef\s+(\S+)\s+(\S+)/) {
			$typedefs{$1} = $2 if (exists($typedefs{$1}));
			next;
		}
		if (m/^replace\s+enum\s+(\S+)\s+(\S+)/) {
			$enums{$1} = $2 if (exists($enums{$1}));
			next;
		}
		if (m/^replace\s+symbol\s+(\S+)\s+(\S+)/) {
			$enum_symbols{$1} = $2 if (exists($enum_symbols{$1}));
			next;
		}
		if (m/^replace\s+struct\s+(\S+)\s+(\S+)/) {
			$structs{$1} = $2 if (exists($structs{$1}));
			next;
		}

		die "Can't parse $file_exceptions: $_";
	}
}

if ($debug) {
	print Data::Dumper->Dump([\%ioctls], [qw(*ioctls)]) if (%ioctls);
	print Data::Dumper->Dump([\%typedefs], [qw(*typedefs)]) if (%typedefs);
	print Data::Dumper->Dump([\%enums], [qw(*enums)]) if (%enums);
	print Data::Dumper->Dump([\%structs], [qw(*structs)]) if (%structs);
	print Data::Dumper->Dump([\%defines], [qw(*defines)]) if (%defines);
	print Data::Dumper->Dump([\%enum_symbols], [qw(*enum_symbols)]) if (%enum_symbols);
}

#
# Align block
#
$data = expand($data);
$data = "    " . $data;
$data =~ s/\n/\n    /g;
$data =~ s/\n\s+$/\n/g;
$data =~ s/\n\s+\n/\n\n/g;

#
# Add escape codes for special characters
#
$data =~ s,([\_\`\*\<\>\&\\\\:\/\|]),\\$1,g;

$data =~ s,DEPRECATED,**DEPRECATED**,g;

#
# Add references
#

my $start_delim = "[ \n\t\(\=\*\@]";
my $end_delim = "(\\s|,|\\\\=|\\\\:|\\;|\\\)|\\}|\\{)";

foreach my $r (keys %ioctls) {
	my $n = $ioctls{$r};

	my $s = "\\ :ref:`$r <$n>`\\ ";

	$r =~ s,([\_\`\*\<\>\&\\\\:\/]),\\\\$1,g;

	print "$r -> $s\n" if ($debug);

	$data =~ s/($start_delim)($r)$end_delim/$1$s$3/g;
}

foreach my $r (keys %defines) {
	my $n = $defines{$r};

	my $s = "\\ :ref:`$r <$n>`\\ ";

	$r =~ s,([\_\`\*\<\>\&\\\\:\/]),\\\\$1,g;

	print "$r -> $s\n" if ($debug);

	$data =~ s/($start_delim)($r)$end_delim/$1$s$3/g;
}

foreach my $r (keys %enum_symbols) {
	my $n = $enum_symbols{$r};

	my $s = "\\ :ref:`$r <$n>`\\ ";

	$r =~ s,([\_\`\*\<\>\&\\\\:\/]),\\\\$1,g;

	print "$r -> $s\n" if ($debug);

	$data =~ s/($start_delim)($r)$end_delim/$1$s$3/g;
}

foreach my $r (keys %enums) {
	my $n = $enums{$r};

	my $s = "\\ :ref:`enum $r <$n>`\\ ";

	$r =~ s,([\_\`\*\<\>\&\\\\:\/]),\\\\$1,g;

	print "$r -> $s\n" if ($debug);

	$data =~ s/enum\s+($r)$end_delim/$s$2/g;
}

foreach my $r (keys %structs) {
	my $n = $structs{$r};

	my $s = "\\ :ref:`struct $r <$n>`\\ ";

	$r =~ s,([\_\`\*\<\>\&\\\\:\/]),\\\\$1,g;

	print "$r -> $s\n" if ($debug);

	$data =~ s/struct\s+($r)$end_delim/$s$2/g;
}

foreach my $r (keys %typedefs) {
	my $n = $typedefs{$r};

	my $s = "\\ :ref:`$r <$n>`\\ ";

	$r =~ s,([\_\`\*\<\>\&\\\\:\/]),\\\\$1,g;

	print "$r -> $s\n" if ($debug);

	$data =~ s/($start_delim)($r)$end_delim/$1$s$3/g;
}

$data =~ s/\\ \n/\n/g;

#
# Generate output file
#

my $title = $file_in;
$title =~ s,.*/,,;

open OUT, "> $file_out" or die "Can't open $file_out";
print OUT ".. -*- coding: utf-8; mode: rst -*-\n\n";
print OUT "$title\n";
print OUT "=" x length($title);
print OUT "\n\n.. parsed-literal::\n\n";
print OUT $data;
close OUT;
