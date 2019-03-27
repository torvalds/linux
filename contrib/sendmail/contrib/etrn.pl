#!/usr/perl5/bin/perl -w
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
# Copyright (c) 1996-2000 by John T. Beck <john@beck.org>
# All rights reserved.
#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

require 5.8.4;				# minimal Perl version required
use strict;
use warnings;
use English;

use Socket;
use Getopt::Std;
our ($opt_v, $opt_b);

# system requirements:
# 	must have 'hostname' program.

my $port = 'smtp';
select(STDERR);

chop(my $name = `hostname || uname -n`);

my ($hostname) = (gethostbyname($name))[0];

my $usage = "Usage: $PROGRAM_NAME [-bv] host [args]";
getopts('bv');
my $verbose = $opt_v;
my $boot_check = $opt_b;
my $server = shift(@ARGV);
my @hosts = @ARGV;
die $usage unless $server;
my @cwfiles = ();
my $alarm_action = "";

if (!@hosts) {
	push(@hosts, $hostname);

	open(CF, "</etc/mail/sendmail.cf") ||
	    die "open /etc/mail/sendmail.cf: $ERRNO";
	while (<CF>){
		# look for a line starting with "Fw"
		if (/^Fw.*$/) {
			my $cwfile = $ARG;
			chop($cwfile);
			my $optional = /^Fw-o/;
			# extract the file name
			$cwfile =~ s,^Fw[^/]*,,;

			# strip the options after the filename
			$cwfile =~ s/ [^ ]+$//;

			if (-r $cwfile) {
				push (@cwfiles, $cwfile);
			} else {
				die "$cwfile is not readable" unless $optional;
			}
		}
		# look for a line starting with "Cw"
		if (/^Cw(.*)$/) {
			my @cws = split (' ', $1);
			while (@cws) {
				my $thishost = shift(@cws);
				push(@hosts, $thishost)
				    unless $thishost =~ "$hostname|localhost";
			}
		}
	}
	close(CF);

	for my $cwfile (@cwfiles) {
		if (open(CW, "<$cwfile")) {
			while (<CW>) {
			        next if /^\#/;
				my $thishost = $ARG;
				chop($thishost);
				push(@hosts, $thishost)
				    unless $thishost =~ $hostname;
			}
			close(CW);
		} else {
			die "open $cwfile: $ERRNO";
		}
	}
	# Do this automatically if no client hosts are specified.
	$boot_check = "yes";
}

my ($proto) = (getprotobyname('tcp'))[2];
($port) = (getservbyname($port, 'tcp'))[2]
	unless $port =~ /^\d+/;

if ($boot_check) {
	# first connect to localhost to verify that we can accept connections
	print "verifying that localhost is accepting SMTP connections\n"
		if ($verbose);
	my $localhost_ok = 0;
	($name, my $laddr) = (gethostbyname('localhost'))[0, 4];
	(!defined($name)) && die "gethostbyname failed, unknown host localhost";

	# get a connection
	my $sinl = sockaddr_in($port, $laddr);
	my $save_errno = 0;
	for (my $num_tries = 1; $num_tries < 5; $num_tries++) {
		socket(S, &PF_INET, &SOCK_STREAM, $proto)
			|| die "socket: $ERRNO";
		if (connect(S, $sinl)) {
			&alarm("sending 'quit' to $server");
			print S "quit\n";
			alarm(0);
			$localhost_ok = 1;
			close(S);
			alarm(0);
			last;
		}
		print STDERR "localhost connect failed ($num_tries)\n";
		$save_errno = $ERRNO;
		sleep(1 << $num_tries);
		close(S);
		alarm(0);
	}
	if (! $localhost_ok) {
		die "could not connect to localhost: $save_errno\n";
	}
}

# look it up

($name, my $thataddr) = (gethostbyname($server))[0, 4];
(!defined($name)) && die "gethostbyname failed, unknown host $server";

# get a connection
my $sinr = sockaddr_in($port, $thataddr);
socket(S, &PF_INET, &SOCK_STREAM, $proto)
	|| die "socket: $ERRNO";
print "server = $server\n" if (defined($verbose));
&alarm("connect to $server");
if (! connect(S, $sinr)) {
	die "cannot connect to $server: $ERRNO\n";
}
alarm(0);
select((select(S), $OUTPUT_AUTOFLUSH = 1)[0]);	# don't buffer output to S

# read the greeting
&alarm("greeting with $server");
while (<S>) {
	alarm(0);
	print if $verbose;
	if (/^(\d+)([- ])/) {
		# SMTP's initial greeting response code is 220.
		if ($1 != 220) {
			&alarm("giving up after bad response from $server");
			&read_response($2, $verbose);
			alarm(0);
			print STDERR "$server: NOT 220 greeting: $ARG"
				if ($verbose);
		}
		last if ($2 eq " ");
	} else {
		print STDERR "$server: NOT 220 greeting: $ARG"
			if ($verbose);
		close(S);
	}
	&alarm("greeting with $server");
}
alarm(0);
	
&alarm("sending ehlo to $server");
&ps("ehlo $hostname");
my $etrn_support = 0;
while (<S>) {
	if (/^250([- ])ETRN(.+)$/) {
		$etrn_support = 1;
	}
	print if $verbose;
	last if /^\d+ /;
}
alarm(0);

if ($etrn_support) {
	print "ETRN supported\n" if ($verbose);
	&alarm("sending etrn to $server");
	while (@hosts) {
		$server = shift(@hosts);
		&ps("etrn $server");
		while (<S>) {
			print if $verbose;
			last if /^\d+ /;
		}
		sleep(1);
	}
} else {
	print "\nETRN not supported\n\n"
}

&alarm("sending 'quit' to $server");
&ps("quit");
while (<S>) {
	print if $verbose;
	last if /^\d+ /;
}
close(S);
alarm(0);

select(STDOUT);
exit(0);

# print to the server (also to stdout, if -v)
sub ps
{
	my ($p) = @_;
	print ">>> $p\n" if $verbose;
	print S "$p\n";
}

sub alarm
{
	($alarm_action) = @_;
	alarm(10);
	$SIG{ALRM} = 'handle_alarm';
}

sub handle_alarm
{
	&giveup($alarm_action);
}

sub giveup
{
	my $reason = @_;
	(my $pk, my $file, my $line);
	($pk, $file, $line) = caller;

	print "Timed out during $reason\n" if $verbose;
	exit(1);
}

# read the rest of the current smtp daemon's response (and toss it away)
sub read_response
{
	(my $done, $verbose) = @_;
	(my @resp);
	print my $s if $verbose;
	while (($done eq "-") && ($s = <S>) && ($s =~ /^\d+([- ])/)) {
		print $s if $verbose;
		$done = $1;
		push(@resp, $s);
	}
	return @resp;
}
