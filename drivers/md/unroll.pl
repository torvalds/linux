#!/usr/bin/perl
#
# Take a piece of C code and for each line which contains the sequence $$
# repeat n times with $ replaced by 0...n-1; the sequence $# is replaced
# by the unrolling factor, and $* with a single $
#

($n) = @ARGV;
$n += 0;

while ( defined($line = <STDIN>) ) {
    if ( $line =~ /\$\$/ ) {
	$rep = $n;
    } else {
	$rep = 1;
    }
    for ( $i = 0 ; $i < $rep ; $i++ ) {
	$tmp = $line;
	$tmp =~ s/\$\$/$i/g;
	$tmp =~ s/\$\#/$n/g;
	$tmp =~ s/\$\*/\$/g;
	print $tmp;
    }
}
