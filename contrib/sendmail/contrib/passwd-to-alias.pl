#!/bin/perl

#
#  Convert GECOS information in password files to alias syntax.
#
#  Contributed by Kari E. Hurtta <Kari.Hurtta@ozone.fmi.fi>
#

print "# Generated from passwd by $0\n";

$wordpat = '([a-zA-Z]+?[a-zA-Z0-9-]*)?[a-zA-Z0-9]';	# 'DB2'
while (@a = getpwent) {
    ($name,$passwd,$uid,$gid,$quota,$comment,$gcos,$dir,$shell) = @a;

    ($fullname = $gcos) =~ s/,.*$//;

    if (!-d $dir || !-x $shell || $shell =~ m!/bin/(false|true)$!) {
	print "$name: root\n";				# handle pseudo user
    }

    $fullname =~ s/\.*[ _]+\.*/./g;
    $fullname =~ tr [Â‰Èˆ¸≈ƒ÷‹] [aaeouAAOU];  # <hakan@af.lu.se> 1997-06-15
    next if (!$fullname || lc($fullname) eq $name);	# avoid nonsense
    if ($fullname =~ /^$wordpat(\.$wordpat)*$/o) {	# Ulrich Windl
	print "$fullname: $name\n";
    } else {
	print "# $fullname: $name\n";			# avoid strange names
    }
};

endpwent;
