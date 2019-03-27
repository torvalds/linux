#!/usr/local/bin/perl
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

require 5.8.4;

use File::Find;
use File::Basename;
use Getopt::Std;
use Cwd;
use Cwd 'abs_path';

$PNAME = $0;
$PNAME =~ s:.*/::;
$OPTSTR = 'abd:fghi:jlnqsx:';
$USAGE = "Usage: $PNAME [-abfghjlnqs] [-d dir] [-i isa] "
    . "[-x opt[=arg]] [file | dir ...]\n";
($MACH = `uname -p`) =~ s/\W*\n//;
($PLATFORM = `uname -i`) =~ s/\W*\n//;

@dtrace_argv = ();

$ksh_path = '/usr/local/bin/ksh';

@files = ();
%exceptions = ();
%results = ();
$errs = 0;

#
# If no test files are specified on the command-line, execute a find on "."
# and append any tst.*.d, tst.*.ksh, err.*.d or drp.*.d files found within
# the directory tree.
#
sub wanted
{
	push(@files, $File::Find::name)
	    if ($_ =~ /^(tst|err|drp)\..+\.(d|ksh)$/ && -f "$_");
}

sub dirname {
	my($s) = @_;
	my($i);

	$s = substr($s, 0, $i) if (($i = rindex($s, '/')) != -1);
	return $i == -1 ? '.' : $i == 0 ? '/' : $s;
}

sub usage
{
	print $USAGE;
	print "\t -a  execute test suite using anonymous enablings\n";
	print "\t -b  execute bad ioctl test program\n";
	print "\t -d  specify directory for test results files and cores\n";
	print "\t -g  enable libumem debugging when running tests\n";
	print "\t -f  force bypassed tests to run\n";
	print "\t -h  display verbose usage message\n";
	print "\t -i  specify ISA to test instead of isaexec(3C) default\n";
	print "\t -j  execute test suite using jdtrace (Java API) only\n";
	print "\t -l  save log file of results and PIDs used by tests\n";
	print "\t -n  execute test suite using dtrace(1m) only\n";
	print "\t -q  set quiet mode (only report errors and summary)\n";
	print "\t -s  save results files even for tests that pass\n";
	print "\t -x  pass corresponding -x argument to dtrace(1M)\n";
	exit(2);
}

sub errmsg
{
	my($msg) = @_;

	print STDERR $msg;
	print LOG $msg if ($opt_l);
	$errs++;
}

sub fail
{
	my(@parms) = @_;
	my($msg) = $parms[0];
	my($errfile) = $parms[1];
	my($n) = 0;
	my($dest) = basename($file);

	while (-d "$opt_d/failure.$n") {
		$n++;
	}

	unless (mkdir "$opt_d/failure.$n") {
		warn "ERROR: failed to make directory $opt_d/failure.$n: $!\n";
		exit(125);
	}

	open(README, ">$opt_d/failure.$n/README");
	print README "ERROR: " . $file . " " . $msg;

	if (scalar @parms > 1) {
		print README "; see $errfile\n";
	} else {
		if (-f "$opt_d/$pid.core") {
			print README "; see $pid.core\n";
		} else {
			print README "\n";
		}
	}

	close(README);

	if (-f "$opt_d/$pid.out") {
		rename("$opt_d/$pid.out", "$opt_d/failure.$n/$pid.out");
		link("$file.out", "$opt_d/failure.$n/$dest.out");
	}

	if (-f "$opt_d/$pid.err") {
		rename("$opt_d/$pid.err", "$opt_d/failure.$n/$pid.err");
		link("$file.err", "$opt_d/failure.$n/$dest.err");
	}

	if (-f "$opt_d/$pid.core") {
		rename("$opt_d/$pid.core", "$opt_d/failure.$n/$pid.core");
	}

	link("$file", "$opt_d/failure.$n/$dest");

	$msg = "ERROR: " . $dest . " " . $msg;

	if (scalar @parms > 1) {
		$msg = $msg . "; see $errfile in failure.$n\n";
	} else {
		$msg = $msg . "; details in failure.$n\n";
	}

	errmsg($msg);
}

sub logmsg
{
	my($msg) = @_;

	print STDOUT $msg unless ($opt_q);
	print LOG $msg if ($opt_l);
}

# Trim leading and trailing whitespace
sub trim {
	my($s) = @_;

	$s =~ s/^\s*//;
	$s =~ s/\s*$//;
	return $s;
}

# Load exception set of skipped tests from the file at the given
# pathname. The test names are assumed to be paths relative to $dt_tst,
# for example: common/aggs/tst.neglquant.d, and specify tests to be
# skipped.
sub load_exceptions {
	my($listfile) = @_;
	my($line) = "";

	%exceptions = ();
	if (length($listfile) > 0) {
		exit(123) unless open(STDIN, "<$listfile");
		while (<STDIN>) {
			chomp;
			$line = $_;
			# line is non-empty and not a comment
			if ((length($line) > 0) && ($line =~ /^\s*[^\s#]/ )) {
				$exceptions{trim($line)} = 1;
			}
		}
	}
}

# Return 1 if the test is found in the exception set, 0 otherwise.
sub is_exception {
	my($file) = @_;
	my($i) = -1;

	if (scalar(keys(%exceptions)) == 0) {
		return 0;
	}

	# hash absolute pathname after $dt_tst/
	$file = abs_path($file);
	$i = index($file, $dt_tst);
	if ($i == 0) {
		$file = substr($file, length($dt_tst) + 1);
		return $exceptions{$file};
	}
	return 0;
}

#
# Iterate over the set of test files specified on the command-line or by a find
# on "$defdir/common", "$defdir/$MACH" and "$defdir/$PLATFORM" and execute each
# one.  If the test file is executable, we fork and exec it. If the test is a
# .ksh file, we run it with $ksh_path. Otherwise we run dtrace -s on it.  If
# the file is named tst.* we assume it should return exit status 0.  If the
# file is named err.* we assume it should return exit status 1.  If the file is
# named err.D_[A-Z0-9]+[.*].d we use dtrace -xerrtags and examine stderr to
# ensure that a matching error tag was produced.  If the file is named
# drp.[A-Z0-9]+[.*].d we use dtrace -xdroptags and examine stderr to ensure
# that a matching drop tag was produced.  If any *.out or *.err files are found
# we perform output comparisons.
#
# run_tests takes two arguments: The first is the pathname of the dtrace
# command to invoke when running the tests. The second is the pathname
# of a file (may be the empty string) listing tests that ought to be
# skipped (skipped tests are listed as paths relative to $dt_tst, for
# example: common/aggs/tst.neglquant.d).
#
sub run_tests {
	my($dtrace, $exceptions_path) = @_;
	my($passed) = 0;
	my($bypassed) = 0;
	my($failed) = $errs;
	my($total) = 0;

	die "$PNAME: $dtrace not found\n" unless (-x "$dtrace");
	logmsg($dtrace . "\n");

	load_exceptions($exceptions_path);

	foreach $file (sort @files) {
		$file =~ m:.*/((.*)\.(\w+)):;
		$name = $1;
		$base = $2;
		$ext = $3;

		$dir = dirname($file);
		$isksh = 0;
		$tag = 0;
		$droptag = 0;

		if ($name =~ /^tst\./) {
			$isksh = ($ext eq 'ksh');
			$status = 0;
		} elsif ($name =~ /^err\.(D_[A-Z0-9_]+)\./) {
			$status = 1;
			$tag = $1;
		} elsif ($name =~ /^err\./) {
			$status = 1;
		} elsif ($name =~ /^drp\.([A-Z0-9_]+)\./) {
			$status = 0;
			$droptag = $1;
		} else {
			errmsg("ERROR: $file is not a valid test file name\n");
			next;
		}

		$fullname = "$dir/$name";
		$exe = "$dir/$base.exe";
		$exe_pid = -1;

		if ($opt_a && ($status != 0 || $tag != 0 || $droptag != 0 ||
		    -x $exe || $isksh || -x $fullname)) {
			$bypassed++;
			next;
		}

		if (!$opt_f && is_exception("$dir/$name")) {
			$bypassed++;
			next;
		}

		if (!$isksh && -x $exe) {
			if (($exe_pid = fork()) == -1) {
				errmsg(
				    "ERROR: failed to fork to run $exe: $!\n");
				next;
			}

			if ($exe_pid == 0) {
				open(STDIN, '</dev/null');

				exec($exe);

				warn "ERROR: failed to exec $exe: $!\n";
			}
		}

		logmsg("testing $file ... ");

		if (($pid = fork()) == -1) {
			errmsg("ERROR: failed to fork to run test $file: $!\n");
			next;
		}

		if ($pid == 0) {
			open(STDIN, '</dev/null');
			exit(125) unless open(STDOUT, ">$opt_d/$$.out");
			exit(125) unless open(STDERR, ">$opt_d/$$.err");

			unless (chdir($dir)) {
				warn "ERROR: failed to chdir for $file: $!\n";
				exit(126);
			}

			push(@dtrace_argv, '-xerrtags') if ($tag);
			push(@dtrace_argv, '-xdroptags') if ($droptag);
			push(@dtrace_argv, $exe_pid) if ($exe_pid != -1);

			if ($isksh) {
				exit(123) unless open(STDIN, "<$name");
				exec("$ksh_path /dev/stdin $dtrace");
			} elsif (-x $name) {
				warn "ERROR: $name is executable\n";
				exit(1);
			} else {
				if ($tag == 0 && $status == $0 && $opt_a) {
					push(@dtrace_argv, '-A');
				}

				push(@dtrace_argv, '-C');
				push(@dtrace_argv, '-s');
				push(@dtrace_argv, $name);
				exec($dtrace, @dtrace_argv);
			}

			warn "ERROR: failed to exec for $file: $!\n";
			exit(127);
		}

		if (waitpid($pid, 0) == -1) {
			errmsg("ERROR: timed out waiting for $file\n");
			kill(9, $exe_pid) if ($exe_pid != -1);
			kill(9, $pid);
			next;
		}

		kill(9, $exe_pid) if ($exe_pid != -1);

		if ($tag == 0 && $status == $0 && $opt_a) {
			#
			# We can chuck the earler output.
			#
			unlink($pid . '.out');
			unlink($pid . '.err');

			#
			# This is an anonymous enabling.  We need to get
			# the module unloaded.
			#
			system("dtrace -ae 1> /dev/null 2> /dev/null");
			system("svcadm disable -s " .
			    "svc:/network/nfs/mapid:default");
			system("modunload -i 0 ; modunload -i 0 ; " .
			    "modunload -i 0");
			if (!system("modinfo | grep dtrace")) {
				warn "ERROR: couldn't unload dtrace\n";
				system("svcadm enable " .
				    "-s svc:/network/nfs/mapid:default");
				exit(124);
			}

			#
			# DTrace is gone.  Now update_drv(1M), and rip
			# everything out again.
			#
			system("update_drv dtrace");
			system("dtrace -ae 1> /dev/null 2> /dev/null");
			system("modunload -i 0 ; modunload -i 0 ; " .
			    "modunload -i 0");
			if (!system("modinfo | grep dtrace")) {
				warn "ERROR: couldn't unload dtrace\n";
				system("svcadm enable " .
				    "-s svc:/network/nfs/mapid:default");
				exit(124);
			}

			#
			# Now bring DTrace back in.
			#
			system("sync ; sync");
			system("dtrace -l -n bogusprobe 1> /dev/null " .
			    "2> /dev/null");
			system("svcadm enable -s " .
			    "svc:/network/nfs/mapid:default");

			#
			# That should have caused DTrace to reload with
			# the new configuration file.  Now we can try to
			# snag our anonymous state.
			#
			if (($pid = fork()) == -1) {
				errmsg("ERROR: failed to fork to run " .
				    "test $file: $!\n");
				next;
			}

			if ($pid == 0) {
				open(STDIN, '</dev/null');
				exit(125) unless open(STDOUT, ">$opt_d/$$.out");
				exit(125) unless open(STDERR, ">$opt_d/$$.err");

				push(@dtrace_argv, '-a');

				unless (chdir($dir)) {
					warn "ERROR: failed to chdir " .
					    "for $file: $!\n";
					exit(126);
				}

				exec($dtrace, @dtrace_argv);
				warn "ERROR: failed to exec for $file: $!\n";
				exit(127);
			}

			if (waitpid($pid, 0) == -1) {
				errmsg("ERROR: timed out waiting for $file\n");
				kill(9, $pid);
				next;
			}
		}

		logmsg("[$pid]\n");
		$wstat = $?;
		$wifexited = ($wstat & 0xFF) == 0;
		$wexitstat = ($wstat >> 8) & 0xFF;
		$wtermsig = ($wstat & 0x7F);

		if (!$wifexited) {
			fail("died from signal $wtermsig");
			next;
		}

		if ($wexitstat == 125) {
			die "$PNAME: failed to create output file in $opt_d " .
			    "(cd elsewhere or use -d)\n";
		}

		if ($wexitstat != $status) {
			fail("returned $wexitstat instead of $status");
			next;
		}

		if (-f "$file.out" &&
		    system("cmp -s $file.out $opt_d/$pid.out") != 0) {
			fail("stdout mismatch", "$pid.out");
			next;
		}

		if (-f "$file.err" &&
		    system("cmp -s $file.err $opt_d/$pid.err") != 0) {
			fail("stderr mismatch: see $pid.err");
			next;
		}

		if ($tag) {
			open(TSTERR, "<$opt_d/$pid.err");
			$tsterr = <TSTERR>;
			close(TSTERR);

			unless ($tsterr =~ /: \[$tag\] line \d+:/) {
				fail("errtag mismatch: see $pid.err");
				next;
			}
		}

		if ($droptag) {
			$found = 0;
			open(TSTERR, "<$opt_d/$pid.err");

			while (<TSTERR>) {
				if (/\[$droptag\] /) {
					$found = 1;
					last;
				}
			}

			close (TSTERR);

			unless ($found) {
				fail("droptag mismatch: see $pid.err");
				next;
			}
		}

		unless ($opt_s) {
			unlink($pid . '.out');
			unlink($pid . '.err');
		}
	}

	if ($opt_a) {
		#
		# If we're running with anonymous enablings, we need to
		# restore the .conf file.
		#
		system("dtrace -A 1> /dev/null 2> /dev/null");
		system("dtrace -ae 1> /dev/null 2> /dev/null");
		system("modunload -i 0 ; modunload -i 0 ; modunload -i 0");
		system("update_drv dtrace");
	}

	$total = scalar(@files);
	$failed = $errs - $failed;
	$passed = ($total - $failed - $bypassed);
	$results{$dtrace} = {
		"passed" => $passed,
		"bypassed" => $bypassed,
		"failed" => $failed,
		"total" => $total
	};
}

die $USAGE unless (getopts($OPTSTR));
usage() if ($opt_h);

foreach $arg (@ARGV) {
	if (-f $arg) {
		push(@files, $arg);
	} elsif (-d $arg) {
		find(\&wanted, $arg);
	} else {
		die "$PNAME: $arg is not a valid file or directory\n";
	}
}

$dt_tst = '/opt/SUNWdtrt/tst';
$dt_bin = '/opt/SUNWdtrt/bin';
$defdir = -d $dt_tst ? $dt_tst : '.';
$bindir = -d $dt_bin ? $dt_bin : '.';

find(\&wanted, "$defdir/common") if (scalar(@ARGV) == 0);
find(\&wanted, "$defdir/$MACH") if (scalar(@ARGV) == 0);
find(\&wanted, "$defdir/$PLATFORM") if (scalar(@ARGV) == 0);
die $USAGE if (scalar(@files) == 0);

$dtrace_path = '/usr/sbin/dtrace';
$jdtrace_path = "$bindir/jdtrace";

%exception_lists = ("$jdtrace_path" => "$bindir/exception.lst");

if ($opt_j || $opt_n || $opt_i) {
	@dtrace_cmds = ();
	push(@dtrace_cmds, $dtrace_path) if ($opt_n);
	push(@dtrace_cmds, $jdtrace_path) if ($opt_j);
	push(@dtrace_cmds, "/usr/sbin/$opt_i/dtrace") if ($opt_i);
} else {
	@dtrace_cmds = ($dtrace_path, $jdtrace_path);
}

if ($opt_d) {
	die "$PNAME: -d arg must be absolute path\n" unless ($opt_d =~ /^\//);
	die "$PNAME: -d arg $opt_d is not a directory\n" unless (-d "$opt_d");
	system("coreadm -p $opt_d/%p.core");
} else {
	my $dir = getcwd;
	system("coreadm -p $dir/%p.core");
	$opt_d = '.';
}

if ($opt_x) {
	push(@dtrace_argv, '-x');
	push(@dtrace_argv, $opt_x);
}

die "$PNAME: failed to open $PNAME.$$.log: $!\n"
    unless (!$opt_l || open(LOG, ">$PNAME.$$.log"));

$ENV{'DTRACE_DEBUG_REGSET'} = 'true';

if ($opt_g) {
	$ENV{'UMEM_DEBUG'} = 'default,verbose';
	$ENV{'UMEM_LOGGING'} = 'fail,contents';
	$ENV{'LD_PRELOAD'} = 'libumem.so';
}

#
# Ensure that $PATH contains a cc(1) so that we can execute the
# test programs that require compilation of C code.
#
#$ENV{'PATH'} = $ENV{'PATH'} . ':/ws/onnv-tools/SUNWspro/SS11/bin';

if ($opt_b) {
	logmsg("badioctl'ing ... ");

	if (($badioctl = fork()) == -1) {
		errmsg("ERROR: failed to fork to run badioctl: $!\n");
		next;
	}

	if ($badioctl == 0) {
		open(STDIN, '</dev/null');
		exit(125) unless open(STDOUT, ">$opt_d/$$.out");
		exit(125) unless open(STDERR, ">$opt_d/$$.err");

		exec($bindir . "/badioctl");
		warn "ERROR: failed to exec badioctl: $!\n";
		exit(127);
	}


	logmsg("[$badioctl]\n");

	#
	# If we're going to be bad, we're just going to iterate over each
	# test file.
	#
	foreach $file (sort @files) {
		($name = $file) =~ s:.*/::;
		$dir = dirname($file);

		if (!($name =~ /^tst\./ && $name =~ /\.d$/)) {
			next;
		}

		logmsg("baddof'ing $file ... ");

		if (($pid = fork()) == -1) {
			errmsg("ERROR: failed to fork to run baddof: $!\n");
			next;
		}

		if ($pid == 0) {
			open(STDIN, '</dev/null');
			exit(125) unless open(STDOUT, ">$opt_d/$$.out");
			exit(125) unless open(STDERR, ">$opt_d/$$.err");

			unless (chdir($dir)) {
				warn "ERROR: failed to chdir for $file: $!\n";
				exit(126);
			}

			exec($bindir . "/baddof", $name);

			warn "ERROR: failed to exec for $file: $!\n";
			exit(127);
		}

		sleep 60;
		kill(9, $pid);
		waitpid($pid, 0);

		logmsg("[$pid]\n");

		unless ($opt_s) {
			unlink($pid . '.out');
			unlink($pid . '.err');
		}
	}

	kill(9, $badioctl);
	waitpid($badioctl, 0);

	unless ($opt_s) {
		unlink($badioctl . '.out');
		unlink($badioctl . '.err');
	}

	exit(0);
}

#
# Run all the tests specified on the command-line (the entire test suite
# by default) once for each dtrace command tested, skipping any tests
# not valid for that command.
#
foreach $dtrace_cmd (@dtrace_cmds) {
	run_tests($dtrace_cmd, $exception_lists{$dtrace_cmd});
}

$opt_q = 0; # force final summary to appear regardless of -q option

logmsg("\n==== TEST RESULTS ====\n");
foreach $key (keys %results) {
	my $passed = $results{$key}{"passed"};
	my $bypassed = $results{$key}{"bypassed"};
	my $failed = $results{$key}{"failed"};
	my $total = $results{$key}{"total"};

	logmsg("\n     mode: " . $key . "\n");
	logmsg("   passed: " . $passed . "\n");
	if ($bypassed) {
		logmsg(" bypassed: " . $bypassed . "\n");
	}
	logmsg("   failed: " . $failed . "\n");
	logmsg("    total: " . $total . "\n");
}

exit($errs != 0);
