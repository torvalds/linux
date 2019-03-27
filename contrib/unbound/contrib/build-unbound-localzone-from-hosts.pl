#!/usr/bin/perl -WT

use strict;
use warnings;

my $hostsfile = '/etc/hosts';
my $localzonefile = '/etc/unbound/localzone.conf.new';

my $localzone = 'example.com';

open( HOSTS,"<${hostsfile}" ) or die( "Could not open ${hostsfile}: $!" );
open( ZONE,">${localzonefile}" ) or die( "Could not open ${localzonefile}: $!" );

print ZONE "server:\n\n";
print ZONE "local-zone: \"${localzone}\" transparent\n\n";

my %ptrhash;

while ( my $hostline = <HOSTS> ) {

	# Skip comments
	if ( $hostline !~ "^#" and $hostline !~ '^\s+$' ) {

		my @entries = split( /\s+/, $hostline );

		my $ip;

		my $count = 0;
		foreach my $entry ( @entries ) {
			if ( $count == 0 ) {
				$ip = $entry;
			} else {

				if ( $count == 1) {

					# Only return localhost for 127.0.0.1 and ::1
					if ( ($ip ne '127.0.0.1' and $ip ne '::1') or $entry =~ 'localhost' ) {
						if ( ! defined $ptrhash{$ip} ) {
							$ptrhash{$ip} = $entry;
							print ZONE "local-data-ptr: \"$ip $entry\"\n";
						}
					}

				}

				# Use AAAA for IPv6 addresses
				my $a = 'A';
				if ( $ip =~ ':' ) {
					$a = 'AAAA';
				}

				print ZONE "local-data: \"$entry ${a} $ip\"\n";

			}
			$count++;
		}
		print ZONE "\n";


	}
}




__END__

