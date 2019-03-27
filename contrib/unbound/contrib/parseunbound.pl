#!/usr/local/bin/perl -w
#
# Script to parse the output from the unbound namedaemon.
# Unbound supports a threading model, and outputs a multiline log-blob for
# every thread.
#
# This script should parse all threads of the once, and store it
# in a local cached file for speedy results when queried lots.
#
use strict;
use POSIX qw(SEEK_END);
use Storable;
use FileHandle;
use Carp qw(croak carp);
use constant UNBOUND_CACHE => "/var/tmp/unbound-cache.stor";

my $run_from_cron = @ARGV && $ARGV[0] eq "--cron" && shift;
my $DEBUG = -t STDERR;

# NB. VERY IMPORTANTES: set this when running this script.
my $numthreads = 4;

### if cache exists, read it in. and is newer than 3 minutes
if ( -r UNBOUND_CACHE ) {
    my $result = retrieve(UNBOUND_CACHE);
    if (-M _ < 3/24/60 && !$run_from_cron ) {
        print STDERR "Cached results:\n" if $DEBUG;
        print join("\n", @$result), "\n";
        exit;
    }
}
my $logfile = shift or die "Usage: parseunbound.pl --cron unboundlogfile";
my $in = new FileHandle $logfile or die "Cannot open $logfile: $!\n";

# there is a special key 'thread' that indicates the thread. its not used, but returned anyway.
my @records = ('thread', 'queries', 'cachehits', 'recursions', 'recursionavg',
        'outstandingmax', 'outstandingavg', 'outstandingexc',
        'median25', 'median50', 'median75',
        'us_0', 'us_1', 'us_2', 'us_4', 'us_8', 'us_16', 'us_32',
        'us_64', 'us_128', 'us_256', 'us_512', 'us_1024', 'us_2048',
        'us_4096', 'us_8192', 'us_16384', 'us_32768', 'us_65536',
        'us_131072', 'us_262144', 'us_524288', 's_1', 's_2', 's_4',
        's_8', 's_16', 's_32', 's_64', 's_128', 's_256', 's_512');
# Stats hash containing one or more keys. for every thread, 1 key.
my %allstats = (); # key="$threadid", stats={key => value}
my %startstats = (); # when we got a queries entry for this thread
my %donestats = (); # same, but only when we got a histogram entry for it
# stats hash contains name/value pairs of the actual numbers for that thread.
my $offset = 0;
my $inthread=0;
my $inpid;

# We should continue looping untill we meet these conditions:
# a) more total queries than the previous run (which defaults to 0) AND
# b) parsed all $numthreads threads in the log.
my $numqueries = $previousresult ? $previousresult->[1] : 0;

# Main loop
while ( scalar keys %startstats < $numthreads || scalar keys %donestats < $numthreads) {
    $offset += 10000;
    if ( $offset > -s $logfile or $offset > 10_000_000 ) {
        die "Cannot find stats in $logfile\n";
    }
    $in->seek(-$offset, SEEK_END) or croak "cannot seek $logfile: $!\n";

    for my $line ( <$in> ) {
        chomp($line);

        #[1208777234] unbound[6705:0] 
        if ($line =~ m/^\[\d+\] unbound\[\d+:(\d+)\]/) {
            $inthread = $1;
            if ($inthread + 1 > $numthreads) {
                die "Hey. lazy. change \$numthreads in this script to ($inthread)\n";
            }
        }
        # this line doesn't contain a pid:thread. skip.
        else {
            next;
        }

        if ( $line =~ m/info: server stats for thread \d+: (\d+) queries, (\d+) answers from cache, (\d+) recursions/ ) {
            $startstats{$inthread} = 1;
            $allstats{$inthread}->{thread} = $inthread;
            $allstats{$inthread}->{queries} = $1;
            $allstats{$inthread}->{cachehits} = $2;
            $allstats{$inthread}->{recursions} = $3;
        }
        elsif ( $line =~ m/info: server stats for thread (\d+): requestlist max (\d+) avg ([0-9\.]+) exceeded (\d+)/ ) {
            $allstats{$inthread}->{outstandingmax} = $2;
            $allstats{$inthread}->{outstandingavg} = int($3); # This is a float; rrdtool only handles ints.
            $allstats{$inthread}->{outstandingexc} = $4;
        }
        elsif ( $line =~ m/info: average recursion processing time ([0-9\.]+) sec/ ) {
            $allstats{$inthread}->{recursionavg} = int($1 * 1000); # change sec to millisec.
        }
        elsif ( $line =~ m/info: histogram of recursion processing times/ ) {
            next;
        }
        elsif ( $line =~ m/info: \[25%\]=([0-9\.]+) median\[50%\]=([0-9\.]+) \[75%\]=([0-9\.]+)/ ) {
            $allstats{$inthread}->{median25} = int($1 * 1000000); # change seconds to usec
            $allstats{$inthread}->{median50} = int($2 * 1000000);
            $allstats{$inthread}->{median75} = int($3 * 1000000);
        }
        elsif ( $line =~ m/info: lower\(secs\) upper\(secs\) recursions/ ) {
            # since after this line we're unsure if we get these numbers
            # at all, we should consider this marker as the end of the
            # block. Chances that we're parsing a file halfway written
            # at this stage are small. Bold statement.
            $donestats{$inthread} = 1;
            next;
        }
        elsif ( $line =~ m/info:\s+(\d+)\.(\d+)\s+(\d+)\.(\d+)\s+(\d+)/ ) {
            my ($froms, $fromus, $toms, $tous, $counter) = ($1, $2, $3, $4, $5);
            my $prefix = '';
            if ($froms > 0) {
                $allstats{$inthread}->{'s_' . int($froms)} = $counter;
            } else {
                $allstats{$inthread}->{'us_' . int($fromus)} = $counter;
            }
        }
    }
}

my @result;
# loop on the records we want to store
for my $key ( @records ) {
    my $sum = 0;
    # these are the different threads parsed
    foreach my $thread ( 0 .. $numthreads - 1 ) {
        $sum += ($allstats{$thread}->{$key} || 0);
    }
    print STDERR "$key = " . $sum . "\n" if $DEBUG;
    push @result, $sum;
}
print join("\n", @result), "\n";
store \@result, UNBOUND_CACHE;

if ($DEBUG) {
    print STDERR "Threads: " . (scalar keys %allstats) . "\n";
}
