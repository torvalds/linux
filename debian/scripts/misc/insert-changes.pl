#!/usr/bin/perl -w

my $debian;
$droot = $ARGV[0] if (defined $ARGV[0]);
$droot = 'debian' if (!defined $droot);
$debian = $ARGV[1] if (defined $ARGV[1]);
$debian = 'debian.master' if (!defined $debian);

system("make -s -f $droot/rules printchanges > $debian/changes");

open(CHANGELOG, "< $debian/changelog") or die "Cannot open changelog";
open(CHANGES, "< $debian/changes") or die "Cannot open new changes";
open(NEW, "> $debian/changelog.new") or die "Cannot open new changelog";

$printed = 0;

while (<CHANGELOG>) {
	if (/^  CHANGELOG: /) {
		next if $printed;

		while (<CHANGES>) {
			print NEW;
		}

		$printed = 1;
	} else {
		print NEW;
	}
}

close(NEW);
close(CHANGES);
close(CHANGELOG);

rename("$debian/changelog.new", "$debian/changelog");
unlink("$debian/changes");
