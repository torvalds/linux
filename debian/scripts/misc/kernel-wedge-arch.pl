#!/usr/bin/perl
#
# kernel-wedge-arch.pl -- select only specifiers for the supplied arch.
#
use strict;

require Dpkg::Control;
require Dpkg::Deps;

my $fh = \*STDIN;

my @entries;

my $wanted = $ARGV[0];

my $entry;
while (!eof($fh)) {
	$entry = Dpkg::Control->new();
	$entry->parse($fh, '???');

	if ($entry->{'Architecture'} eq $wanted) {
		print("\n" . $entry);
	}
}

close($fh);
