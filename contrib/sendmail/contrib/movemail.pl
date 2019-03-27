#!/usr/bin/perl -w
#
# Move old mail messages between queues by calling re-mqueue.pl.
#
# movemail.pl [config-script]
#
# Default config script is /usr/local/etc/movemail.conf.
#
# Graeme Hewson <graeme.hewson@oracle.com>, June 2000
#

use strict;

# Load external program as subroutine to avoid
# compilation overhead on each call

sub loadsub {
    my $fn = shift
    	or die "Filename not specified";
    my $len = (stat($fn))[7]
    	or die "Can't stat $fn: $!";
    open PROG, "< $fn"
    	or die "Can't open $fn: $!";
    my $prog;
    read PROG, $prog, $len
    	or die "Can't read $fn: $!";
    close PROG;
    eval join "",
	'return sub { my @ARGV = @_; $0 = $fn; no strict;',
    	"$prog",
	'};';
}

my $progname = $0;
my $lastage = -1;
my $LOCK_EX = 2;
my $LOCK_NB = 4;

# Load and eval config script

my $conffile = shift || "/usr/local/etc/movemail.conf";
my $len = (stat($conffile))[7]
    or die "Can't stat $conffile: $!";
open CONF, "< $conffile"
    or die "Can't open $conffile: $!";
my $conf;
read CONF, $conf, $len
    or die "Can't read $conffile: $!";
close CONF;
use vars qw(@queues $subqbase @ages $remqueue $lockfile);
eval $conf;

if ($#queues < 1) {
    print "$progname: there must be at least two queues\n";
    exit 1;
}

if ($#ages != ($#queues - 1)) {
    print "$progname: wrong number of ages (should be one less than number of queues)\n";
    exit 1;
}

# Get lock or exit quietly.  Useful when running from cron.

if ($lockfile) {
    open LOCK, ">>$lockfile"
	or die "Can't open lock file: $!";
    unless (flock LOCK, $LOCK_EX|$LOCK_NB) {
	close LOCK;
	exit 0;
    }
}

my $remsub = loadsub($remqueue);

# Go through directories in reverse order so as to check spool files only once

for (my $n = $#queues - 1; $n >= 0; $n--) {
    unless ($ages[$n] =~ /^\d+$/) {
	print "$progname: invalid number $ages[$n] in ages array\n";
	exit 1;
    }
    unless ($lastage < 0 || $ages[$n] < $lastage) {
	print "$progname: age $lastage is not > previous value $ages[$n]\n";
	exit 1;
    }
    $lastage = $ages[$n];
    if ($subqbase) {
	my $subdir;
	opendir(DIR, $queues[$n])
	    or die "Can't open $queues[$n]: $!";
	foreach $subdir ( grep { /^$subqbase/ } readdir DIR) {
	    &$remsub("$queues[$n]/$subdir", "$queues[$n+1]/$subdir",
		$ages[$n]);
	}
	closedir(DIR);
    } else {
	# Not using subdirectories
	&$remsub($queues[$n], $queues[$n+1], $ages[$n]);
    }
}

if ($lockfile) {
    unlink $lockfile;
    close LOCK;
}
