#!/usr/bin/perl
#
# re-mqueue -- requeue messages from queueA to queueB based on age.
#
#	Contributed by Paul Pomes <ppomes@Qualcomm.COM>.
#		http://www.qualcomm.com/~ppomes/
#
# Usage: re-mqueue [-d] queueA queueB seconds
#
#  -d		enable debugging
#  queueA	source directory
#  queueB	destination directory
#  seconds	select files older than this number of seconds 
#
# Example: re-mqueue /var/spool/mqueue /var/spool/mqueue2 2700
#
# Moves the qf* and df* files for a message from /var/spool/mqueue to
# /var/spool/mqueue2 if the df* file is over 2700 seconds old.
#
# The qf* file can't be used for age checking as it's partially re-written
# with the results of the last queue run.
#
# Rationale: With a limited number of sendmail processes allowed to run,
# messages that can't be delivered immediately slow down the ones that can.
# This becomes especially important when messages are being queued instead
# of delivered right away, or when the queue becomes excessively deep.
# By putting messages that have already failed one or more delivery attempts
# into another queue, the primary queue can be kept small and fast.
#
# On postoffice.cso.uiuc.edu, the primary sendmail daemon runs the queue
# every thirty minutes.  Messages over 45 minutues old are moved to
# /var/spool/mqueue2 where sendmail runs every hour.  Messages more than
# 3.25 hours old are moved to /var/spool/mqueue3 where sendmail runs every
# four hours.  Messages more than a day old are moved to /var/spool/mqueue4
# where sendmail runs three times a day.  The idea is that a message is
# tried at least twice in the first three queues before being moved to the
# old-age ghetto.
#
# (Each must be re-formed into a single line before using in crontab)
#
# 08 * * * *	/usr/local/libexec/re-mqueue /var/spool/mqueue ##						/var/spool/mqueue2 2700
# 11 * * * *	/usr/lib/sendmail -oQ/var/spool/mqueue2 -q > ##							> /var/log/mqueue2 2>&1
# 38 * * * *	/usr/local/libexec/re-mqueue /var/spool/mqueue2
#					/var/spool/mqueue3 11700
# 41 1,5,9,13,17,21 * * * /usr/lib/sendmail -oQ/var/spool/mqueue3 -q ##							> /var/log/mqueue3 2>&1
# 48 * * * *	/usr/local/libexec/re-mqueue /var/spool/mqueue3
#					/var/spool/mqueue4 100000
#53 3,11,19 * * * /usr/lib/sendmail -oQ/var/spool/mqueue4 -q > ##							> /var/log/mqueue4 2>&1
#
#
# N.B., the moves are done with link().  This has two effects: 1) the mqueue*
# directories must all be on the same filesystem, and 2) the file modification
# times are not changed.  All times must be cumulative from when the df*
# file was created.
#
# Copyright (c) 1995 University of Illinois Board of Trustees and Paul Pomes
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
#       This product includes software developed by the University of
#       Illinois at Urbana and their contributors.
# 4. Neither the name of the University nor the names of their contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE TRUSTEES AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE TRUSTEES OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# @(#)$OrigId: re-mqueue,v 1.3 1995/05/25 18:14:53 p-pomes Exp $
#
# Updated by Graeme Hewson <ghewson@uk.oracle.com> May 1999
#
#	'use Sys::Syslog' for Perl 5
#	Move transcript (xf) files if they exist
#	Allow zero-length df files (empty message body)
#	Preserve $! for error messages
#
# Updated by Graeme Hewson <ghewson@uk.oracle.com> April 2000
#
#	Improve handling of race between re-mqueue and sendmail
#
# Updated by Graeme Hewson <graeme.hewson@oracle.com> June 2000
#
#	Don't exit(0) at end so can be called as subroutine
#
# NB This program can't handle separate qf/df/xf subdirectories
# as introduced in sendmail 8.10.0.
#

use Sys::Syslog;

$LOCK_EX = 2;
$LOCK_NB = 4;
$LOCK_UN = 8;

# Count arguments, exit if wrong in any way.
die "Usage: $0 [-d] queueA queueB seconds\n" if ($#ARGV < 2);

while ($_ = $ARGV[0], /^-/) {
    shift;
    last if /^--$/;
    /^-d/ && $debug++;
}

$queueA = shift;
$queueB = shift;
$age = shift;

die "$0: $queueA not a directory\n" if (! -d $queueA);
die "$0: $queueB not a directory\n" if (! -d $queueB);
die "$0: $age isn't a valid number of seconds for age\n" if ($age =~ /\D/);

# chdir to $queueA and read the directory.  When a df* file is found, stat it.
# If it's older than $age, lock the corresponding qf* file.  If the lock
# fails, give up and move on.  Once the lock is obtained, verify that files
# of the same name *don't* already exist in $queueB and move on if they do.
# Otherwise re-link the qf* and df* files into $queueB then release the lock.

chdir "$queueA" || die "$0: can't cd to $queueA: $!\n";
opendir (QA, ".") || die "$0: can't open directory $queueA for reading: $!\n";
@dfiles = grep(/^df/, readdir(QA));
$now = time();
($program = $0) =~ s,.*/,,;
&openlog($program, 'pid', 'mail');

# Loop through the dfiles
while ($dfile = pop(@dfiles)) {
    print "Checking $dfile\n" if ($debug);
    ($qfile = $dfile) =~ s/^d/q/;
    ($xfile = $dfile) =~ s/^d/x/;
    ($mfile = $dfile) =~ s/^df//;
    if (! -e $qfile || -z $qfile) {
	print "$qfile is gone or zero bytes - skipping\n" if ($debug);
	next;
    }

    ($dev,$ino,$mode,$nlink,$uid,$gid,$rdev,$size,
     $atime,$mtime,$ctime,$blksize,$blocks) = stat($dfile);
    if (! defined $mtime) {
	print "$dfile is gone - skipping\n" if ($debug);
	next;
    }

    # Compare timestamps
    if (($mtime + $age) > $now) {
	printf ("%s is %d seconds old - skipping\n", $dfile, $now-$mtime) if ($debug);
	next;
    }

    # See if files of the same name already exist in $queueB
    if (-e "$queueB/$dfile") {
	print "$queueb/$dfile already exists - skipping\n" if ($debug);
	next;
    }
    if (-e "$queueB/$qfile") {
	print "$queueb/$qfile already exists - skipping\n" if ($debug);
	next;
    }
    if (-e "$queueB/$xfile") {
	print "$queueb/$xfile already exists - skipping\n" if ($debug);
	next;
    }

    # Try and lock qf* file
    unless (open(QF, ">>$qfile")) {
	print "$qfile: $!\n" if ($debug);
	next;
    }
    $retval = flock(QF, $LOCK_EX|$LOCK_NB) || ($retval = -1);
    if ($retval == -1) {
	print "$qfile already flock()ed - skipping\n" if ($debug);
	close(QF);
	next;
    }
    print "$qfile now flock()ed\n" if ($debug);

    # Check df* file again in case sendmail got in
    if (! -e $dfile) {
	print "$mfile sent - skipping\n" if ($debug);
	# qf* file created by ourselves at open? (Almost certainly)
	if (-z $qfile) {
	   unlink($qfile);
	}
	close(QF);
	next;
    }

    # Show time!  Do the link()s
    if (link("$dfile", "$queueB/$dfile") == 0) {
	$bang = $!;
	&syslog('err', 'link(%s, %s/%s): %s', $dfile, $queueB, $dfile, $bang);
	print STDERR "$0: link($dfile, $queueB/$dfile): $bang\n";
	exit (1);
    }
    if (link("$qfile", "$queueB/$qfile") == 0) {
	$bang = $!;
	&syslog('err', 'link(%s, %s/%s): %s', $qfile, $queueB, $qfile, $bang);
	print STDERR "$0: link($qfile, $queueB/$qfile): $bang\n";
	unlink("$queueB/$dfile");
	exit (1);
    }
    if (-e "$xfile") {
	if (link("$xfile", "$queueB/$xfile") == 0) {
	    $bang = $!;
	    &syslog('err', 'link(%s, %s/%s): %s', $xfile, $queueB, $xfile, $bang);
	    print STDERR "$0: link($xfile, $queueB/$xfile): $bang\n";
	    unlink("$queueB/$dfile");
	    unlink("$queueB/$qfile");
	    exit (1);
	}
    }

    # Links created successfully.  Unlink the original files, release the
    # lock, and close the file.
    print "links ok\n" if ($debug);
    if (unlink($qfile) == 0) {
	$bang = $!;
	&syslog('err', 'unlink(%s): %s', $qfile, $bang);
	print STDERR "$0: unlink($qfile): $bang\n";
	exit (1);
    }
    if (unlink($dfile) == 0) {
	$bang = $!;
	&syslog('err', 'unlink(%s): %s', $dfile, $bang);
	print STDERR "$0: unlink($dfile): $bang\n";
	exit (1);
    }
    if (-e "$xfile") {
	if (unlink($xfile) == 0) {
	    $bang = $!;
	    &syslog('err', 'unlink(%s): %s', $xfile, $bang);
	    print STDERR "$0: unlink($xfile): $bang\n";
	    exit (1);
	}
    }
    flock(QF, $LOCK_UN);
    close(QF);
    &syslog('info', '%s moved to %s', $mfile, $queueB);
    print "Done with $dfile $qfile\n\n" if ($debug);
}
