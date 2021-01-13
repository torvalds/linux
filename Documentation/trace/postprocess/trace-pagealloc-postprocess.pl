#!/usr/bin/perl
# This is a POC (proof of concept or piece of crap, take your pick) for reading the
# text representation of trace output related to page allocation. It makes an attempt
# to extract some high-level information on what is going on. The accuracy of the parser
# may vary considerably
#
# Example usage: trace-pagealloc-postprocess.pl < /sys/kernel/debug/tracing/trace_pipe
# other options
#   --prepend-parent	Report on the parent proc and PID
#   --read-procstat	If the trace lacks process info, get it from /proc
#   --ignore-pid	Aggregate processes of the same name together
#
# Copyright (c) IBM Corporation 2009
# Author: Mel Gorman <mel@csn.ul.ie>
use strict;
use Getopt::Long;

# Tracepoint events
use constant MM_PAGE_ALLOC		=> 1;
use constant MM_PAGE_FREE		=> 2;
use constant MM_PAGE_FREE_BATCHED	=> 3;
use constant MM_PAGE_PCPU_DRAIN		=> 4;
use constant MM_PAGE_ALLOC_ZONE_LOCKED	=> 5;
use constant MM_PAGE_ALLOC_EXTFRAG	=> 6;
use constant EVENT_UNKNOWN		=> 7;

# Constants used to track state
use constant STATE_PCPU_PAGES_DRAINED	=> 8;
use constant STATE_PCPU_PAGES_REFILLED	=> 9;

# High-level events extrapolated from tracepoints
use constant HIGH_PCPU_DRAINS		=> 10;
use constant HIGH_PCPU_REFILLS		=> 11;
use constant HIGH_EXT_FRAGMENT		=> 12;
use constant HIGH_EXT_FRAGMENT_SEVERE	=> 13;
use constant HIGH_EXT_FRAGMENT_MODERATE	=> 14;
use constant HIGH_EXT_FRAGMENT_CHANGED	=> 15;

my %perprocesspid;
my %perprocess;
my $opt_ignorepid;
my $opt_read_procstat;
my $opt_prepend_parent;

# Catch sigint and exit on request
my $sigint_report = 0;
my $sigint_exit = 0;
my $sigint_pending = 0;
my $sigint_received = 0;
sub sigint_handler {
	my $current_time = time;
	if ($current_time - 2 > $sigint_received) {
		print "SIGINT received, report pending. Hit ctrl-c again to exit\n";
		$sigint_report = 1;
	} else {
		if (!$sigint_exit) {
			print "Second SIGINT received quickly, exiting\n";
		}
		$sigint_exit++;
	}

	if ($sigint_exit > 3) {
		print "Many SIGINTs received, exiting now without report\n";
		exit;
	}

	$sigint_received = $current_time;
	$sigint_pending = 1;
}
$SIG{INT} = "sigint_handler";

# Parse command line options
GetOptions(
	'ignore-pid'	 =>	\$opt_ignorepid,
	'read-procstat'	 =>	\$opt_read_procstat,
	'prepend-parent' =>	\$opt_prepend_parent,
);

# Defaults for dynamically discovered regex's
my $regex_fragdetails_default = 'page=([0-9a-f]*) pfn=([0-9]*) alloc_order=([-0-9]*) fallback_order=([-0-9]*) pageblock_order=([-0-9]*) alloc_migratetype=([-0-9]*) fallback_migratetype=([-0-9]*) fragmenting=([-0-9]) change_ownership=([-0-9])';

# Dyanically discovered regex
my $regex_fragdetails;

# Static regex used. Specified like this for readability and for use with /o
#                      (process_pid)     (cpus      )   ( time  )   (tpoint    ) (details)
my $regex_traceevent = '\s*([a-zA-Z0-9-]*)\s*(\[[0-9]*\])\s*([0-9.]*):\s*([a-zA-Z_]*):\s*(.*)';
my $regex_statname = '[-0-9]*\s\((.*)\).*';
my $regex_statppid = '[-0-9]*\s\(.*\)\s[A-Za-z]\s([0-9]*).*';

sub generate_traceevent_regex {
	my $event = shift;
	my $default = shift;
	my $regex;

	# Read the event format or use the default
	if (!open (FORMAT, "/sys/kernel/debug/tracing/events/$event/format")) {
		$regex = $default;
	} else {
		my $line;
		while (!eof(FORMAT)) {
			$line = <FORMAT>;
			if ($line =~ /^print fmt:\s"(.*)",.*/) {
				$regex = $1;
				$regex =~ s/%p/\([0-9a-f]*\)/g;
				$regex =~ s/%d/\([-0-9]*\)/g;
				$regex =~ s/%lu/\([0-9]*\)/g;
			}
		}
	}

	# Verify fields are in the right order
	my $tuple;
	foreach $tuple (split /\s/, $regex) {
		my ($key, $value) = split(/=/, $tuple);
		my $expected = shift;
		if ($key ne $expected) {
			print("WARNING: Format not as expected '$key' != '$expected'");
			$regex =~ s/$key=\((.*)\)/$key=$1/;
		}
	}

	if (defined shift) {
		die("Fewer fields than expected in format");
	}

	return $regex;
}
$regex_fragdetails = generate_traceevent_regex("kmem/mm_page_alloc_extfrag",
			$regex_fragdetails_default,
			"page", "pfn",
			"alloc_order", "fallback_order", "pageblock_order",
			"alloc_migratetype", "fallback_migratetype",
			"fragmenting", "change_ownership");

sub read_statline($) {
	my $pid = $_[0];
	my $statline;

	if (open(STAT, "/proc/$pid/stat")) {
		$statline = <STAT>;
		close(STAT);
	}

	if ($statline eq '') {
		$statline = "-1 (UNKNOWN_PROCESS_NAME) R 0";
	}

	return $statline;
}

sub guess_process_pid($$) {
	my $pid = $_[0];
	my $statline = $_[1];

	if ($pid == 0) {
		return "swapper-0";
	}

	if ($statline !~ /$regex_statname/o) {
		die("Failed to math stat line for process name :: $statline");
	}
	return "$1-$pid";
}

sub parent_info($$) {
	my $pid = $_[0];
	my $statline = $_[1];
	my $ppid;

	if ($pid == 0) {
		return "NOPARENT-0";
	}

	if ($statline !~ /$regex_statppid/o) {
		die("Failed to match stat line process ppid:: $statline");
	}

	# Read the ppid stat line
	$ppid = $1;
	return guess_process_pid($ppid, read_statline($ppid));
}

sub process_events {
	my $traceevent;
	my $process_pid;
	my $cpus;
	my $timestamp;
	my $tracepoint;
	my $details;
	my $statline;

	# Read each line of the event log
EVENT_PROCESS:
	while ($traceevent = <STDIN>) {
		if ($traceevent =~ /$regex_traceevent/o) {
			$process_pid = $1;
			$tracepoint = $4;

			if ($opt_read_procstat || $opt_prepend_parent) {
				$process_pid =~ /(.*)-([0-9]*)$/;
				my $process = $1;
				my $pid = $2;

				$statline = read_statline($pid);

				if ($opt_read_procstat && $process eq '') {
					$process_pid = guess_process_pid($pid, $statline);
				}

				if ($opt_prepend_parent) {
					$process_pid = parent_info($pid, $statline) . " :: $process_pid";
				}
			}

			# Unnecessary in this script. Uncomment if required
			# $cpus = $2;
			# $timestamp = $3;
		} else {
			next;
		}

		# Perl Switch() sucks majorly
		if ($tracepoint eq "mm_page_alloc") {
			$perprocesspid{$process_pid}->{MM_PAGE_ALLOC}++;
		} elsif ($tracepoint eq "mm_page_free") {
			$perprocesspid{$process_pid}->{MM_PAGE_FREE}++
		} elsif ($tracepoint eq "mm_page_free_batched") {
			$perprocesspid{$process_pid}->{MM_PAGE_FREE_BATCHED}++;
		} elsif ($tracepoint eq "mm_page_pcpu_drain") {
			$perprocesspid{$process_pid}->{MM_PAGE_PCPU_DRAIN}++;
			$perprocesspid{$process_pid}->{STATE_PCPU_PAGES_DRAINED}++;
		} elsif ($tracepoint eq "mm_page_alloc_zone_locked") {
			$perprocesspid{$process_pid}->{MM_PAGE_ALLOC_ZONE_LOCKED}++;
			$perprocesspid{$process_pid}->{STATE_PCPU_PAGES_REFILLED}++;
		} elsif ($tracepoint eq "mm_page_alloc_extfrag") {

			# Extract the details of the event now
			$details = $5;

			my ($page, $pfn);
			my ($alloc_order, $fallback_order, $pageblock_order);
			my ($alloc_migratetype, $fallback_migratetype);
			my ($fragmenting, $change_ownership);

			if ($details !~ /$regex_fragdetails/o) {
				print "WARNING: Failed to parse mm_page_alloc_extfrag as expected\n";
				next;
			}

			$perprocesspid{$process_pid}->{MM_PAGE_ALLOC_EXTFRAG}++;
			$page = $1;
			$pfn = $2;
			$alloc_order = $3;
			$fallback_order = $4;
			$pageblock_order = $5;
			$alloc_migratetype = $6;
			$fallback_migratetype = $7;
			$fragmenting = $8;
			$change_ownership = $9;

			if ($fragmenting) {
				$perprocesspid{$process_pid}->{HIGH_EXT_FRAG}++;
				if ($fallback_order <= 3) {
					$perprocesspid{$process_pid}->{HIGH_EXT_FRAGMENT_SEVERE}++;
				} else {
					$perprocesspid{$process_pid}->{HIGH_EXT_FRAGMENT_MODERATE}++;
				}
			}
			if ($change_ownership) {
				$perprocesspid{$process_pid}->{HIGH_EXT_FRAGMENT_CHANGED}++;
			}
		} else {
			$perprocesspid{$process_pid}->{EVENT_UNKNOWN}++;
		}

		# Catch a full pcpu drain event
		if ($perprocesspid{$process_pid}->{STATE_PCPU_PAGES_DRAINED} &&
				$tracepoint ne "mm_page_pcpu_drain") {

			$perprocesspid{$process_pid}->{HIGH_PCPU_DRAINS}++;
			$perprocesspid{$process_pid}->{STATE_PCPU_PAGES_DRAINED} = 0;
		}

		# Catch a full pcpu refill event
		if ($perprocesspid{$process_pid}->{STATE_PCPU_PAGES_REFILLED} &&
				$tracepoint ne "mm_page_alloc_zone_locked") {
			$perprocesspid{$process_pid}->{HIGH_PCPU_REFILLS}++;
			$perprocesspid{$process_pid}->{STATE_PCPU_PAGES_REFILLED} = 0;
		}

		if ($sigint_pending) {
			last EVENT_PROCESS;
		}
	}
}

sub dump_stats {
	my $hashref = shift;
	my %stats = %$hashref;

	# Dump per-process stats
	my $process_pid;
	my $max_strlen = 0;

	# Get the maximum process name
	foreach $process_pid (keys %perprocesspid) {
		my $len = length($process_pid);
		if ($len > $max_strlen) {
			$max_strlen = $len;
		}
	}
	$max_strlen += 2;

	printf("\n");
	printf("%-" . $max_strlen . "s %8s %10s   %8s %8s   %8s %8s %8s   %8s %8s %8s %8s %8s %8s\n",
		"Process", "Pages",  "Pages",      "Pages", "Pages", "PCPU",  "PCPU",   "PCPU",    "Fragment",  "Fragment", "MigType", "Fragment", "Fragment", "Unknown");
	printf("%-" . $max_strlen . "s %8s %10s   %8s %8s   %8s %8s %8s   %8s %8s %8s %8s %8s %8s\n",
		"details", "allocd", "allocd",     "freed", "freed", "pages", "drains", "refills", "Fallback", "Causing",   "Changed", "Severe", "Moderate", "");

	printf("%-" . $max_strlen . "s %8s %10s   %8s %8s   %8s %8s %8s   %8s %8s %8s %8s %8s %8s\n",
		"",        "",       "under lock", "direct", "pagevec", "drain", "", "", "", "", "", "", "", "");

	foreach $process_pid (keys %stats) {
		# Dump final aggregates
		if ($stats{$process_pid}->{STATE_PCPU_PAGES_DRAINED}) {
			$stats{$process_pid}->{HIGH_PCPU_DRAINS}++;
			$stats{$process_pid}->{STATE_PCPU_PAGES_DRAINED} = 0;
		}
		if ($stats{$process_pid}->{STATE_PCPU_PAGES_REFILLED}) {
			$stats{$process_pid}->{HIGH_PCPU_REFILLS}++;
			$stats{$process_pid}->{STATE_PCPU_PAGES_REFILLED} = 0;
		}

		printf("%-" . $max_strlen . "s %8d %10d   %8d %8d   %8d %8d %8d   %8d %8d %8d %8d %8d %8d\n",
			$process_pid,
			$stats{$process_pid}->{MM_PAGE_ALLOC},
			$stats{$process_pid}->{MM_PAGE_ALLOC_ZONE_LOCKED},
			$stats{$process_pid}->{MM_PAGE_FREE},
			$stats{$process_pid}->{MM_PAGE_FREE_BATCHED},
			$stats{$process_pid}->{MM_PAGE_PCPU_DRAIN},
			$stats{$process_pid}->{HIGH_PCPU_DRAINS},
			$stats{$process_pid}->{HIGH_PCPU_REFILLS},
			$stats{$process_pid}->{MM_PAGE_ALLOC_EXTFRAG},
			$stats{$process_pid}->{HIGH_EXT_FRAG},
			$stats{$process_pid}->{HIGH_EXT_FRAGMENT_CHANGED},
			$stats{$process_pid}->{HIGH_EXT_FRAGMENT_SEVERE},
			$stats{$process_pid}->{HIGH_EXT_FRAGMENT_MODERATE},
			$stats{$process_pid}->{EVENT_UNKNOWN});
	}
}

sub aggregate_perprocesspid() {
	my $process_pid;
	my $process;
	undef %perprocess;

	foreach $process_pid (keys %perprocesspid) {
		$process = $process_pid;
		$process =~ s/-([0-9])*$//;
		if ($process eq '') {
			$process = "NO_PROCESS_NAME";
		}

		$perprocess{$process}->{MM_PAGE_ALLOC} += $perprocesspid{$process_pid}->{MM_PAGE_ALLOC};
		$perprocess{$process}->{MM_PAGE_ALLOC_ZONE_LOCKED} += $perprocesspid{$process_pid}->{MM_PAGE_ALLOC_ZONE_LOCKED};
		$perprocess{$process}->{MM_PAGE_FREE} += $perprocesspid{$process_pid}->{MM_PAGE_FREE};
		$perprocess{$process}->{MM_PAGE_FREE_BATCHED} += $perprocesspid{$process_pid}->{MM_PAGE_FREE_BATCHED};
		$perprocess{$process}->{MM_PAGE_PCPU_DRAIN} += $perprocesspid{$process_pid}->{MM_PAGE_PCPU_DRAIN};
		$perprocess{$process}->{HIGH_PCPU_DRAINS} += $perprocesspid{$process_pid}->{HIGH_PCPU_DRAINS};
		$perprocess{$process}->{HIGH_PCPU_REFILLS} += $perprocesspid{$process_pid}->{HIGH_PCPU_REFILLS};
		$perprocess{$process}->{MM_PAGE_ALLOC_EXTFRAG} += $perprocesspid{$process_pid}->{MM_PAGE_ALLOC_EXTFRAG};
		$perprocess{$process}->{HIGH_EXT_FRAG} += $perprocesspid{$process_pid}->{HIGH_EXT_FRAG};
		$perprocess{$process}->{HIGH_EXT_FRAGMENT_CHANGED} += $perprocesspid{$process_pid}->{HIGH_EXT_FRAGMENT_CHANGED};
		$perprocess{$process}->{HIGH_EXT_FRAGMENT_SEVERE} += $perprocesspid{$process_pid}->{HIGH_EXT_FRAGMENT_SEVERE};
		$perprocess{$process}->{HIGH_EXT_FRAGMENT_MODERATE} += $perprocesspid{$process_pid}->{HIGH_EXT_FRAGMENT_MODERATE};
		$perprocess{$process}->{EVENT_UNKNOWN} += $perprocesspid{$process_pid}->{EVENT_UNKNOWN};
	}
}

sub report() {
	if (!$opt_ignorepid) {
		dump_stats(\%perprocesspid);
	} else {
		aggregate_perprocesspid();
		dump_stats(\%perprocess);
	}
}

# Process events or signals until neither is available
sub signal_loop() {
	my $sigint_processed;
	do {
		$sigint_processed = 0;
		process_events();

		# Handle pending signals if any
		if ($sigint_pending) {
			my $current_time = time;

			if ($sigint_exit) {
				print "Received exit signal\n";
				$sigint_pending = 0;
			}
			if ($sigint_report) {
				if ($current_time >= $sigint_received + 2) {
					report();
					$sigint_report = 0;
					$sigint_pending = 0;
					$sigint_processed = 1;
				}
			}
		}
	} while ($sigint_pending || $sigint_processed);
}

signal_loop();
report();
