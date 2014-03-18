#!/usr/bin/perl
# This is a POC for reading the text representation of trace output related to
# page reclaim. It makes an attempt to extract some high-level information on
# what is going on. The accuracy of the parser may vary
#
# Example usage: trace-vmscan-postprocess.pl < /sys/kernel/debug/tracing/trace_pipe
# other options
#   --read-procstat	If the trace lacks process info, get it from /proc
#   --ignore-pid	Aggregate processes of the same name together
#
# Copyright (c) IBM Corporation 2009
# Author: Mel Gorman <mel@csn.ul.ie>
use strict;
use Getopt::Long;

# Tracepoint events
use constant MM_VMSCAN_DIRECT_RECLAIM_BEGIN	=> 1;
use constant MM_VMSCAN_DIRECT_RECLAIM_END	=> 2;
use constant MM_VMSCAN_KSWAPD_WAKE		=> 3;
use constant MM_VMSCAN_KSWAPD_SLEEP		=> 4;
use constant MM_VMSCAN_LRU_SHRINK_ACTIVE	=> 5;
use constant MM_VMSCAN_LRU_SHRINK_INACTIVE	=> 6;
use constant MM_VMSCAN_LRU_ISOLATE		=> 7;
use constant MM_VMSCAN_WRITEPAGE_FILE_SYNC	=> 8;
use constant MM_VMSCAN_WRITEPAGE_ANON_SYNC	=> 9;
use constant MM_VMSCAN_WRITEPAGE_FILE_ASYNC	=> 10;
use constant MM_VMSCAN_WRITEPAGE_ANON_ASYNC	=> 11;
use constant MM_VMSCAN_WRITEPAGE_ASYNC		=> 12;
use constant EVENT_UNKNOWN			=> 13;

# Per-order events
use constant MM_VMSCAN_DIRECT_RECLAIM_BEGIN_PERORDER => 11;
use constant MM_VMSCAN_WAKEUP_KSWAPD_PERORDER 	=> 12;
use constant MM_VMSCAN_KSWAPD_WAKE_PERORDER	=> 13;
use constant HIGH_KSWAPD_REWAKEUP_PERORDER	=> 14;

# Constants used to track state
use constant STATE_DIRECT_BEGIN 		=> 15;
use constant STATE_DIRECT_ORDER 		=> 16;
use constant STATE_KSWAPD_BEGIN			=> 17;
use constant STATE_KSWAPD_ORDER			=> 18;

# High-level events extrapolated from tracepoints
use constant HIGH_DIRECT_RECLAIM_LATENCY	=> 19;
use constant HIGH_KSWAPD_LATENCY		=> 20;
use constant HIGH_KSWAPD_REWAKEUP		=> 21;
use constant HIGH_NR_SCANNED			=> 22;
use constant HIGH_NR_TAKEN			=> 23;
use constant HIGH_NR_RECLAIMED			=> 24;
use constant HIGH_NR_CONTIG_DIRTY		=> 25;

my %perprocesspid;
my %perprocess;
my %last_procmap;
my $opt_ignorepid;
my $opt_read_procstat;

my $total_wakeup_kswapd;
my ($total_direct_reclaim, $total_direct_nr_scanned);
my ($total_direct_latency, $total_kswapd_latency);
my ($total_direct_nr_reclaimed);
my ($total_direct_writepage_file_sync, $total_direct_writepage_file_async);
my ($total_direct_writepage_anon_sync, $total_direct_writepage_anon_async);
my ($total_kswapd_nr_scanned, $total_kswapd_wake);
my ($total_kswapd_writepage_file_sync, $total_kswapd_writepage_file_async);
my ($total_kswapd_writepage_anon_sync, $total_kswapd_writepage_anon_async);
my ($total_kswapd_nr_reclaimed);

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
);

# Defaults for dynamically discovered regex's
my $regex_direct_begin_default = 'order=([0-9]*) may_writepage=([0-9]*) gfp_flags=([A-Z_|]*)';
my $regex_direct_end_default = 'nr_reclaimed=([0-9]*)';
my $regex_kswapd_wake_default = 'nid=([0-9]*) order=([0-9]*)';
my $regex_kswapd_sleep_default = 'nid=([0-9]*)';
my $regex_wakeup_kswapd_default = 'nid=([0-9]*) zid=([0-9]*) order=([0-9]*)';
my $regex_lru_isolate_default = 'isolate_mode=([0-9]*) order=([0-9]*) nr_requested=([0-9]*) nr_scanned=([0-9]*) nr_taken=([0-9]*) contig_taken=([0-9]*) contig_dirty=([0-9]*) contig_failed=([0-9]*)';
my $regex_lru_shrink_inactive_default = 'nid=([0-9]*) zid=([0-9]*) nr_scanned=([0-9]*) nr_reclaimed=([0-9]*) priority=([0-9]*) flags=([A-Z_|]*)';
my $regex_lru_shrink_active_default = 'lru=([A-Z_]*) nr_scanned=([0-9]*) nr_rotated=([0-9]*) priority=([0-9]*)';
my $regex_writepage_default = 'page=([0-9a-f]*) pfn=([0-9]*) flags=([A-Z_|]*)';

# Dyanically discovered regex
my $regex_direct_begin;
my $regex_direct_end;
my $regex_kswapd_wake;
my $regex_kswapd_sleep;
my $regex_wakeup_kswapd;
my $regex_lru_isolate;
my $regex_lru_shrink_inactive;
my $regex_lru_shrink_active;
my $regex_writepage;

# Static regex used. Specified like this for readability and for use with /o
#                      (process_pid)     (cpus      )   ( time  )   (tpoint    ) (details)
my $regex_traceevent = '\s*([a-zA-Z0-9-]*)\s*(\[[0-9]*\])(\s*[dX.][Nnp.][Hhs.][0-9a-fA-F.]*|)\s*([0-9.]*):\s*([a-zA-Z_]*):\s*(.*)';
my $regex_statname = '[-0-9]*\s\((.*)\).*';
my $regex_statppid = '[-0-9]*\s\(.*\)\s[A-Za-z]\s([0-9]*).*';

sub generate_traceevent_regex {
	my $event = shift;
	my $default = shift;
	my $regex;

	# Read the event format or use the default
	if (!open (FORMAT, "/sys/kernel/debug/tracing/events/$event/format")) {
		print("WARNING: Event $event format string not found\n");
		return $default;
	} else {
		my $line;
		while (!eof(FORMAT)) {
			$line = <FORMAT>;
			$line =~ s/, REC->.*//;
			if ($line =~ /^print fmt:\s"(.*)".*/) {
				$regex = $1;
				$regex =~ s/%s/\([0-9a-zA-Z|_]*\)/g;
				$regex =~ s/%p/\([0-9a-f]*\)/g;
				$regex =~ s/%d/\([-0-9]*\)/g;
				$regex =~ s/%ld/\([-0-9]*\)/g;
				$regex =~ s/%lu/\([0-9]*\)/g;
			}
		}
	}

	# Can't handle the print_flags stuff but in the context of this
	# script, it really doesn't matter
	$regex =~ s/\(REC.*\) \? __print_flags.*//;

	# Verify fields are in the right order
	my $tuple;
	foreach $tuple (split /\s/, $regex) {
		my ($key, $value) = split(/=/, $tuple);
		my $expected = shift;
		if ($key ne $expected) {
			print("WARNING: Format not as expected for event $event '$key' != '$expected'\n");
			$regex =~ s/$key=\((.*)\)/$key=$1/;
		}
	}

	if (defined shift) {
		die("Fewer fields than expected in format");
	}

	return $regex;
}

$regex_direct_begin = generate_traceevent_regex(
			"vmscan/mm_vmscan_direct_reclaim_begin",
			$regex_direct_begin_default,
			"order", "may_writepage",
			"gfp_flags");
$regex_direct_end = generate_traceevent_regex(
			"vmscan/mm_vmscan_direct_reclaim_end",
			$regex_direct_end_default,
			"nr_reclaimed");
$regex_kswapd_wake = generate_traceevent_regex(
			"vmscan/mm_vmscan_kswapd_wake",
			$regex_kswapd_wake_default,
			"nid", "order");
$regex_kswapd_sleep = generate_traceevent_regex(
			"vmscan/mm_vmscan_kswapd_sleep",
			$regex_kswapd_sleep_default,
			"nid");
$regex_wakeup_kswapd = generate_traceevent_regex(
			"vmscan/mm_vmscan_wakeup_kswapd",
			$regex_wakeup_kswapd_default,
			"nid", "zid", "order");
$regex_lru_isolate = generate_traceevent_regex(
			"vmscan/mm_vmscan_lru_isolate",
			$regex_lru_isolate_default,
			"isolate_mode", "order",
			"nr_requested", "nr_scanned", "nr_taken",
			"contig_taken", "contig_dirty", "contig_failed");
$regex_lru_shrink_inactive = generate_traceevent_regex(
			"vmscan/mm_vmscan_lru_shrink_inactive",
			$regex_lru_shrink_inactive_default,
			"nid", "zid",
			"nr_scanned", "nr_reclaimed", "priority",
			"flags");
$regex_lru_shrink_active = generate_traceevent_regex(
			"vmscan/mm_vmscan_lru_shrink_active",
			$regex_lru_shrink_active_default,
			"nid", "zid",
			"lru",
			"nr_scanned", "nr_rotated", "priority");
$regex_writepage = generate_traceevent_regex(
			"vmscan/mm_vmscan_writepage",
			$regex_writepage_default,
			"page", "pfn", "flags");

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

# Convert sec.usec timestamp format
sub timestamp_to_ms($) {
	my $timestamp = $_[0];

	my ($sec, $usec) = split (/\./, $timestamp);
	return ($sec * 1000) + ($usec / 1000);
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
			$timestamp = $4;
			$tracepoint = $5;

			$process_pid =~ /(.*)-([0-9]*)$/;
			my $process = $1;
			my $pid = $2;

			if ($process eq "") {
				$process = $last_procmap{$pid};
				$process_pid = "$process-$pid";
			}
			$last_procmap{$pid} = $process;

			if ($opt_read_procstat) {
				$statline = read_statline($pid);
				if ($opt_read_procstat && $process eq '') {
					$process_pid = guess_process_pid($pid, $statline);
				}
			}
		} else {
			next;
		}

		# Perl Switch() sucks majorly
		if ($tracepoint eq "mm_vmscan_direct_reclaim_begin") {
			$timestamp = timestamp_to_ms($timestamp);
			$perprocesspid{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN}++;
			$perprocesspid{$process_pid}->{STATE_DIRECT_BEGIN} = $timestamp;

			$details = $6;
			if ($details !~ /$regex_direct_begin/o) {
				print "WARNING: Failed to parse mm_vmscan_direct_reclaim_begin as expected\n";
				print "         $details\n";
				print "         $regex_direct_begin\n";
				next;
			}
			my $order = $1;
			$perprocesspid{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN_PERORDER}[$order]++;
			$perprocesspid{$process_pid}->{STATE_DIRECT_ORDER} = $order;
		} elsif ($tracepoint eq "mm_vmscan_direct_reclaim_end") {
			# Count the event itself
			my $index = $perprocesspid{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_END};
			$perprocesspid{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_END}++;

			# Record how long direct reclaim took this time
			if (defined $perprocesspid{$process_pid}->{STATE_DIRECT_BEGIN}) {
				$timestamp = timestamp_to_ms($timestamp);
				my $order = $perprocesspid{$process_pid}->{STATE_DIRECT_ORDER};
				my $latency = ($timestamp - $perprocesspid{$process_pid}->{STATE_DIRECT_BEGIN});
				$perprocesspid{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[$index] = "$order-$latency";
			}
		} elsif ($tracepoint eq "mm_vmscan_kswapd_wake") {
			$details = $6;
			if ($details !~ /$regex_kswapd_wake/o) {
				print "WARNING: Failed to parse mm_vmscan_kswapd_wake as expected\n";
				print "         $details\n";
				print "         $regex_kswapd_wake\n";
				next;
			}

			my $order = $2;
			$perprocesspid{$process_pid}->{STATE_KSWAPD_ORDER} = $order;
			if (!$perprocesspid{$process_pid}->{STATE_KSWAPD_BEGIN}) {
				$timestamp = timestamp_to_ms($timestamp);
				$perprocesspid{$process_pid}->{MM_VMSCAN_KSWAPD_WAKE}++;
				$perprocesspid{$process_pid}->{STATE_KSWAPD_BEGIN} = $timestamp;
				$perprocesspid{$process_pid}->{MM_VMSCAN_KSWAPD_WAKE_PERORDER}[$order]++;
			} else {
				$perprocesspid{$process_pid}->{HIGH_KSWAPD_REWAKEUP}++;
				$perprocesspid{$process_pid}->{HIGH_KSWAPD_REWAKEUP_PERORDER}[$order]++;
			}
		} elsif ($tracepoint eq "mm_vmscan_kswapd_sleep") {

			# Count the event itself
			my $index = $perprocesspid{$process_pid}->{MM_VMSCAN_KSWAPD_SLEEP};
			$perprocesspid{$process_pid}->{MM_VMSCAN_KSWAPD_SLEEP}++;

			# Record how long kswapd was awake
			$timestamp = timestamp_to_ms($timestamp);
			my $order = $perprocesspid{$process_pid}->{STATE_KSWAPD_ORDER};
			my $latency = ($timestamp - $perprocesspid{$process_pid}->{STATE_KSWAPD_BEGIN});
			$perprocesspid{$process_pid}->{HIGH_KSWAPD_LATENCY}[$index] = "$order-$latency";
			$perprocesspid{$process_pid}->{STATE_KSWAPD_BEGIN} = 0;
		} elsif ($tracepoint eq "mm_vmscan_wakeup_kswapd") {
			$perprocesspid{$process_pid}->{MM_VMSCAN_WAKEUP_KSWAPD}++;

			$details = $6;
			if ($details !~ /$regex_wakeup_kswapd/o) {
				print "WARNING: Failed to parse mm_vmscan_wakeup_kswapd as expected\n";
				print "         $details\n";
				print "         $regex_wakeup_kswapd\n";
				next;
			}
			my $order = $3;
			$perprocesspid{$process_pid}->{MM_VMSCAN_WAKEUP_KSWAPD_PERORDER}[$order]++;
		} elsif ($tracepoint eq "mm_vmscan_lru_isolate") {
			$details = $6;
			if ($details !~ /$regex_lru_isolate/o) {
				print "WARNING: Failed to parse mm_vmscan_lru_isolate as expected\n";
				print "         $details\n";
				print "         $regex_lru_isolate/o\n";
				next;
			}
			my $isolate_mode = $1;
			my $nr_scanned = $4;
			my $nr_contig_dirty = $7;

			# To closer match vmstat scanning statistics, only count isolate_both
			# and isolate_inactive as scanning. isolate_active is rotation
			# isolate_inactive == 1
			# isolate_active   == 2
			# isolate_both     == 3
			if ($isolate_mode != 2) {
				$perprocesspid{$process_pid}->{HIGH_NR_SCANNED} += $nr_scanned;
			}
			$perprocesspid{$process_pid}->{HIGH_NR_CONTIG_DIRTY} += $nr_contig_dirty;
		} elsif ($tracepoint eq "mm_vmscan_lru_shrink_inactive") {
			$details = $6;
			if ($details !~ /$regex_lru_shrink_inactive/o) {
				print "WARNING: Failed to parse mm_vmscan_lru_shrink_inactive as expected\n";
				print "         $details\n";
				print "         $regex_lru_shrink_inactive/o\n";
				next;
			}
			my $nr_reclaimed = $4;
			$perprocesspid{$process_pid}->{HIGH_NR_RECLAIMED} += $nr_reclaimed;
		} elsif ($tracepoint eq "mm_vmscan_writepage") {
			$details = $6;
			if ($details !~ /$regex_writepage/o) {
				print "WARNING: Failed to parse mm_vmscan_writepage as expected\n";
				print "         $details\n";
				print "         $regex_writepage\n";
				next;
			}

			my $flags = $3;
			my $file = 0;
			my $sync_io = 0;
			if ($flags =~ /RECLAIM_WB_FILE/) {
				$file = 1;
			}
			if ($flags =~ /RECLAIM_WB_SYNC/) {
				$sync_io = 1;
			}
			if ($sync_io) {
				if ($file) {
					$perprocesspid{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_SYNC}++;
				} else {
					$perprocesspid{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_SYNC}++;
				}
			} else {
				if ($file) {
					$perprocesspid{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_ASYNC}++;
				} else {
					$perprocesspid{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_ASYNC}++;
				}
			}
		} else {
			$perprocesspid{$process_pid}->{EVENT_UNKNOWN}++;
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

	# Work out latencies
	printf("\n") if !$opt_ignorepid;
	printf("Reclaim latencies expressed as order-latency_in_ms\n") if !$opt_ignorepid;
	foreach $process_pid (keys %stats) {

		if (!$stats{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[0] &&
				!$stats{$process_pid}->{HIGH_KSWAPD_LATENCY}[0]) {
			next;
		}

		printf "%-" . $max_strlen . "s ", $process_pid if !$opt_ignorepid;
		my $index = 0;
		while (defined $stats{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[$index] ||
			defined $stats{$process_pid}->{HIGH_KSWAPD_LATENCY}[$index]) {

			if ($stats{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[$index]) {
				printf("%s ", $stats{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[$index]) if !$opt_ignorepid;
				my ($dummy, $latency) = split(/-/, $stats{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[$index]);
				$total_direct_latency += $latency;
			} else {
				printf("%s ", $stats{$process_pid}->{HIGH_KSWAPD_LATENCY}[$index]) if !$opt_ignorepid;
				my ($dummy, $latency) = split(/-/, $stats{$process_pid}->{HIGH_KSWAPD_LATENCY}[$index]);
				$total_kswapd_latency += $latency;
			}
			$index++;
		}
		print "\n" if !$opt_ignorepid;
	}

	# Print out process activity
	printf("\n");
	printf("%-" . $max_strlen . "s %8s %10s   %8s %8s  %8s %8s %8s %8s\n", "Process", "Direct",  "Wokeup", "Pages",   "Pages",   "Pages",   "Pages",     "Time");
	printf("%-" . $max_strlen . "s %8s %10s   %8s %8s  %8s %8s %8s %8s\n", "details", "Rclms",   "Kswapd", "Scanned", "Rclmed",  "Sync-IO", "ASync-IO",  "Stalled");
	foreach $process_pid (keys %stats) {

		if (!$stats{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN}) {
			next;
		}

		$total_direct_reclaim += $stats{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN};
		$total_wakeup_kswapd += $stats{$process_pid}->{MM_VMSCAN_WAKEUP_KSWAPD};
		$total_direct_nr_scanned += $stats{$process_pid}->{HIGH_NR_SCANNED};
		$total_direct_nr_reclaimed += $stats{$process_pid}->{HIGH_NR_RECLAIMED};
		$total_direct_writepage_file_sync += $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_SYNC};
		$total_direct_writepage_anon_sync += $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_SYNC};
		$total_direct_writepage_file_async += $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_ASYNC};

		$total_direct_writepage_anon_async += $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_ASYNC};

		my $index = 0;
		my $this_reclaim_delay = 0;
		while (defined $stats{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[$index]) {
			 my ($dummy, $latency) = split(/-/, $stats{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[$index]);
			$this_reclaim_delay += $latency;
			$index++;
		}

		printf("%-" . $max_strlen . "s %8d %10d   %8u %8u  %8u %8u %8.3f",
			$process_pid,
			$stats{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN},
			$stats{$process_pid}->{MM_VMSCAN_WAKEUP_KSWAPD},
			$stats{$process_pid}->{HIGH_NR_SCANNED},
			$stats{$process_pid}->{HIGH_NR_RECLAIMED},
			$stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_SYNC} + $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_SYNC},
			$stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_ASYNC} + $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_ASYNC},
			$this_reclaim_delay / 1000);

		if ($stats{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN}) {
			print "      ";
			for (my $order = 0; $order < 20; $order++) {
				my $count = $stats{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN_PERORDER}[$order];
				if ($count != 0) {
					print "direct-$order=$count ";
				}
			}
		}
		if ($stats{$process_pid}->{MM_VMSCAN_WAKEUP_KSWAPD}) {
			print "      ";
			for (my $order = 0; $order < 20; $order++) {
				my $count = $stats{$process_pid}->{MM_VMSCAN_WAKEUP_KSWAPD_PERORDER}[$order];
				if ($count != 0) {
					print "wakeup-$order=$count ";
				}
			}
		}
		if ($stats{$process_pid}->{HIGH_NR_CONTIG_DIRTY}) {
			print "      ";
			my $count = $stats{$process_pid}->{HIGH_NR_CONTIG_DIRTY};
			if ($count != 0) {
				print "contig-dirty=$count ";
			}
		}

		print "\n";
	}

	# Print out kswapd activity
	printf("\n");
	printf("%-" . $max_strlen . "s %8s %10s   %8s   %8s %8s %8s\n", "Kswapd",   "Kswapd",  "Order",     "Pages",   "Pages",   "Pages",  "Pages");
	printf("%-" . $max_strlen . "s %8s %10s   %8s   %8s %8s %8s\n", "Instance", "Wakeups", "Re-wakeup", "Scanned", "Rclmed",  "Sync-IO", "ASync-IO");
	foreach $process_pid (keys %stats) {

		if (!$stats{$process_pid}->{MM_VMSCAN_KSWAPD_WAKE}) {
			next;
		}

		$total_kswapd_wake += $stats{$process_pid}->{MM_VMSCAN_KSWAPD_WAKE};
		$total_kswapd_nr_scanned += $stats{$process_pid}->{HIGH_NR_SCANNED};
		$total_kswapd_nr_reclaimed += $stats{$process_pid}->{HIGH_NR_RECLAIMED};
		$total_kswapd_writepage_file_sync += $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_SYNC};
		$total_kswapd_writepage_anon_sync += $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_SYNC};
		$total_kswapd_writepage_file_async += $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_ASYNC};
		$total_kswapd_writepage_anon_async += $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_ASYNC};

		printf("%-" . $max_strlen . "s %8d %10d   %8u %8u  %8i %8u",
			$process_pid,
			$stats{$process_pid}->{MM_VMSCAN_KSWAPD_WAKE},
			$stats{$process_pid}->{HIGH_KSWAPD_REWAKEUP},
			$stats{$process_pid}->{HIGH_NR_SCANNED},
			$stats{$process_pid}->{HIGH_NR_RECLAIMED},
			$stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_SYNC} + $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_SYNC},
			$stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_ASYNC} + $stats{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_ASYNC});

		if ($stats{$process_pid}->{MM_VMSCAN_KSWAPD_WAKE}) {
			print "      ";
			for (my $order = 0; $order < 20; $order++) {
				my $count = $stats{$process_pid}->{MM_VMSCAN_KSWAPD_WAKE_PERORDER}[$order];
				if ($count != 0) {
					print "wake-$order=$count ";
				}
			}
		}
		if ($stats{$process_pid}->{HIGH_KSWAPD_REWAKEUP}) {
			print "      ";
			for (my $order = 0; $order < 20; $order++) {
				my $count = $stats{$process_pid}->{HIGH_KSWAPD_REWAKEUP_PERORDER}[$order];
				if ($count != 0) {
					print "rewake-$order=$count ";
				}
			}
		}
		printf("\n");
	}

	# Print out summaries
	$total_direct_latency /= 1000;
	$total_kswapd_latency /= 1000;
	print "\nSummary\n";
	print "Direct reclaims:     			$total_direct_reclaim\n";
	print "Direct reclaim pages scanned:		$total_direct_nr_scanned\n";
	print "Direct reclaim pages reclaimed:		$total_direct_nr_reclaimed\n";
	print "Direct reclaim write file sync I/O:	$total_direct_writepage_file_sync\n";
	print "Direct reclaim write anon sync I/O:	$total_direct_writepage_anon_sync\n";
	print "Direct reclaim write file async I/O:	$total_direct_writepage_file_async\n";
	print "Direct reclaim write anon async I/O:	$total_direct_writepage_anon_async\n";
	print "Wake kswapd requests:			$total_wakeup_kswapd\n";
	printf "Time stalled direct reclaim: 		%-1.2f seconds\n", $total_direct_latency;
	print "\n";
	print "Kswapd wakeups:				$total_kswapd_wake\n";
	print "Kswapd pages scanned:			$total_kswapd_nr_scanned\n";
	print "Kswapd pages reclaimed:			$total_kswapd_nr_reclaimed\n";
	print "Kswapd reclaim write file sync I/O:	$total_kswapd_writepage_file_sync\n";
	print "Kswapd reclaim write anon sync I/O:	$total_kswapd_writepage_anon_sync\n";
	print "Kswapd reclaim write file async I/O:	$total_kswapd_writepage_file_async\n";
	print "Kswapd reclaim write anon async I/O:	$total_kswapd_writepage_anon_async\n";
	printf "Time kswapd awake:			%-1.2f seconds\n", $total_kswapd_latency;
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

		$perprocess{$process}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN} += $perprocesspid{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN};
		$perprocess{$process}->{MM_VMSCAN_KSWAPD_WAKE} += $perprocesspid{$process_pid}->{MM_VMSCAN_KSWAPD_WAKE};
		$perprocess{$process}->{MM_VMSCAN_WAKEUP_KSWAPD} += $perprocesspid{$process_pid}->{MM_VMSCAN_WAKEUP_KSWAPD};
		$perprocess{$process}->{HIGH_KSWAPD_REWAKEUP} += $perprocesspid{$process_pid}->{HIGH_KSWAPD_REWAKEUP};
		$perprocess{$process}->{HIGH_NR_SCANNED} += $perprocesspid{$process_pid}->{HIGH_NR_SCANNED};
		$perprocess{$process}->{HIGH_NR_RECLAIMED} += $perprocesspid{$process_pid}->{HIGH_NR_RECLAIMED};
		$perprocess{$process}->{MM_VMSCAN_WRITEPAGE_FILE_SYNC} += $perprocesspid{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_SYNC};
		$perprocess{$process}->{MM_VMSCAN_WRITEPAGE_ANON_SYNC} += $perprocesspid{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_SYNC};
		$perprocess{$process}->{MM_VMSCAN_WRITEPAGE_FILE_ASYNC} += $perprocesspid{$process_pid}->{MM_VMSCAN_WRITEPAGE_FILE_ASYNC};
		$perprocess{$process}->{MM_VMSCAN_WRITEPAGE_ANON_ASYNC} += $perprocesspid{$process_pid}->{MM_VMSCAN_WRITEPAGE_ANON_ASYNC};

		for (my $order = 0; $order < 20; $order++) {
			$perprocess{$process}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN_PERORDER}[$order] += $perprocesspid{$process_pid}->{MM_VMSCAN_DIRECT_RECLAIM_BEGIN_PERORDER}[$order];
			$perprocess{$process}->{MM_VMSCAN_WAKEUP_KSWAPD_PERORDER}[$order] += $perprocesspid{$process_pid}->{MM_VMSCAN_WAKEUP_KSWAPD_PERORDER}[$order];
			$perprocess{$process}->{MM_VMSCAN_KSWAPD_WAKE_PERORDER}[$order] += $perprocesspid{$process_pid}->{MM_VMSCAN_KSWAPD_WAKE_PERORDER}[$order];

		}

		# Aggregate direct reclaim latencies
		my $wr_index = $perprocess{$process}->{MM_VMSCAN_DIRECT_RECLAIM_END};
		my $rd_index = 0;
		while (defined $perprocesspid{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[$rd_index]) {
			$perprocess{$process}->{HIGH_DIRECT_RECLAIM_LATENCY}[$wr_index] = $perprocesspid{$process_pid}->{HIGH_DIRECT_RECLAIM_LATENCY}[$rd_index];
			$rd_index++;
			$wr_index++;
		}
		$perprocess{$process}->{MM_VMSCAN_DIRECT_RECLAIM_END} = $wr_index;

		# Aggregate kswapd latencies
		my $wr_index = $perprocess{$process}->{MM_VMSCAN_KSWAPD_SLEEP};
		my $rd_index = 0;
		while (defined $perprocesspid{$process_pid}->{HIGH_KSWAPD_LATENCY}[$rd_index]) {
			$perprocess{$process}->{HIGH_KSWAPD_LATENCY}[$wr_index] = $perprocesspid{$process_pid}->{HIGH_KSWAPD_LATENCY}[$rd_index];
			$rd_index++;
			$wr_index++;
		}
		$perprocess{$process}->{MM_VMSCAN_DIRECT_RECLAIM_END} = $wr_index;
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
