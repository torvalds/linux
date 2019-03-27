#!/usr/local/bin/perl -w
#
# bounce-resender: constructs mail queue from bounce spool for
#  subsequent reprocessing by sendmail
#
# usage: given a mail spool full of (only) bounced mail called "bounces":
#        # mkdir -m0700 bqueue; cd bqueue && bounce-resender < ../bounces
#        # cd ..
#        # chown -R root bqueue; chmod 600 bqueue/*
#        # /usr/lib/sendmail -bp -oQ`pwd`/bqueue | more   # does it look OK?
#        # /usr/lib/sendmail -q -oQ`pwd`/bqueue -oT99d &  # run the queue
#
# ** also read messages at end! **
#
# Brian R. Gaeke <brg@EECS.Berkeley.EDU> Thu Feb 18 13:40:10 PST 1999
#
#############################################################################
# This script has NO WARRANTY, NO BUG FIXES, and NO SUPPORT.  You will
# need to modify it for your site and for your operating system, unless
# you are in the EECS Instructional group at UC Berkeley. (Search forward
# for two occurrences of "FIXME".)
#

$state = "MSG_START";
$ctr = 0;
$lineno = 0;
$getnrl = 0;
$nrl = "";
$uname = "PhilOS";  # You don't want to change this here.
$myname = $0;
$myname =~ s,.*/([^/]*),$1,;

chomp($hostname = `hostname`);
chomp($uname = `uname`);

# FIXME: Define the functions "major" and "minor" for your OS.
if ($uname eq "SunOS") {
	# from h2ph < /usr/include/sys/sysmacros.h on 
	# SunOS torus.CS.Berkeley.EDU 5.6 Generic_105182-11 i86pc i386 i86pc
    eval 'sub O_BITSMINOR () {8;}' unless defined(&O_BITSMINOR);
    eval 'sub O_MAXMAJ () {0x7f;}' unless defined(&O_MAXMAJ);
    eval 'sub O_MAXMIN () {0xff;}' unless defined(&O_MAXMIN);
	eval 'sub major {
	    local($x) = @_;
	    eval "((($x) >>  &O_BITSMINOR)   &O_MAXMAJ)";
	}' unless defined(&major);
	eval 'sub minor {
	    local($x) = @_;
	    eval "(($x)   &O_MAXMIN)";
	}' unless defined(&minor);
} else {
	die "How do you calculate major and minor device numbers on $uname?\n";
}

sub ignorance { $ignored{$state}++; }

sub unmunge {
	my($addr) = @_;
	$addr =~ s/_FNORD_/ /g;
	# remove (Real Name)
	$addr =~ s/^(.*)\([^\)]*\)(.*)$/$1$2/
		if $addr =~ /^.*\([^\)]*\).*$/;
	# extract <user@host> if it appears
	$addr =~ s/^.*<([^>]*)>.*$/$1/
		if $addr =~ /^.*<[^>]*>.*$/;
	# strip leading, trailing blanks
	$addr =~ s/^\s*(.*)\s*/$1/;
	# nuke local domain
    # FIXME: Add a regular expression for your local domain here.
	$addr =~
	 s/@(cory|po|pasteur|torus|parker|cochise|franklin).(ee)?cs.berkeley.edu//i;
	return $addr;
}

print STDERR "$0: running on $hostname ($uname)\n";

open(INPUT,$ARGV[0]) || die "$ARGV[0]: $!\n";

sub working { 
	my($now);
	$now = localtime;
	print STDERR "$myname: Working... $now\n";
}

&working();

while (! eof INPUT) {
	# get a new line
	if ($state eq "IN_MESSAGE_HEADER") {
		# handle multi-line headers
		if ($nrl ne "" || $getnrl != 0) {
			$_ = $nrl;
			$getnrl = 0;
			$nrl = "";
		} else {
			$_ = <INPUT>; $lineno++;
		}
		unless ($_ =~ /^\s*$/) {
			while ($nrl eq "") {
				$nrl = <INPUT>; $lineno++;
				if ($nrl =~ /^\s+[^\s].*$/) { # continuation line
					chomp($_);
					$_ .= "_FNORD_" . $nrl;
					$nrl = "";
				} elsif ($nrl =~ /^\s*$/) { # end of headers
					$getnrl++;
					last;
				}
			}
		}
	} else {
		# normal single line
		if ($nrl ne "") {
			$_ = $nrl; $nrl = "";
		} else {
			$_ = <INPUT>; $lineno++;
		}
	}

	if ($state eq "WAIT_FOR_FROM") {
		if (/^From \S+.*$/) {
			$state = "MSG_START";
		} else {
			&ignorance();
		}
	} elsif ($state eq "MSG_START") {
		if (/^\s+boundary=\"([^\"]*)\".*$/) {
			$boundary = $1;
			$state = "GOT_BOUNDARY";
			$ctr++;
		} else {
			&ignorance();
		}
	} elsif ($state eq "GOT_BOUNDARY") {
		if (/^--$boundary/) {
			$next = <INPUT>; $lineno++;
			if ($next =~ /^Content-Type: message\/rfc822/) {
				$hour = (localtime)[2];
				$char = chr(ord("A") + $hour);
				$ident = sprintf("%sAA%05d",$char,99999 - $ctr);
				$qf = "qf$ident";
				$df = "df$ident";
				@rcpt = ();
				open(MSGHDR,">$qf") || die "Can't write to $qf: $!\n";
				open(MSGBODY,">$df") || die "Can't write to $df: $!\n";
				chmod(0600, $qf, $df);
				$state = "IN_MESSAGE_HEADER";
				$header = $body = "";
				$messageid = "bounce-resender-$ctr";
				$fromline = "MAILER-DAEMON";
				$ctencod = "7BIT";
				# skip a bit, brother maynard (boundary is separated from
				#  the header by a blank line)
				$next = <INPUT>; $lineno++;
				unless ($next =~ /^\s*$/) {
					print MSGHDR $next;
				}
			}
		} else {
			&ignorance();
		}

		$next = $char = $hour = undef;
	} elsif ($state eq "IN_MESSAGE_HEADER") {
		if (!(/^--$boundary/ || /^\s*$/)) {
			if (/^Message-[iI][dD]:\s+<([^@]+)@[^>]*>.*$/) {
				$messageid = $1;
			} elsif (/^From:\s+(.*)$/) {
				$fromline = $sender = $1;
				$fromline = unmunge($fromline);
			} elsif (/^Content-[Tt]ransfer-[Ee]ncoding:\s+(.*)$/) {
				$ctencod = $1;
			} elsif (/^(To|[Cc][Cc]):\s+(.*)$/) {
				$toaddrs = $2;
				foreach $toaddr (split(/,/,$toaddrs)) {
					$toaddr = unmunge($toaddr);
					push(@rcpt,$toaddr);
				}
			}
			$headerline = $_; 
			# escape special chars
			# (Perhaps not. It doesn't seem to be necessary (yet)).
            #$headerline =~ s/([\(\)<>@,;:\\".\[\]])/\\$1/g;
			# purely heuristic ;-)
            $headerline =~ s/Return-Path:/?P?Return-Path:/g;
			# save H-line to write to qf, later
			$header .= "H$headerline";

			$headerline = $toaddr = $toaddrs = undef;
		} elsif (/^\s*$/) {
			# write to qf
			($dev, $ino) = (stat($df))[0 .. 1];
			($maj, $min) = (major($dev), minor($dev));
			$time = time();
			print MSGHDR "V2\n";
			print MSGHDR "B$ctencod\n";
			print MSGHDR "S$sender\n";
			print MSGHDR "I$maj/$min/$ino\n";
			print MSGHDR "K$time\n";
			print MSGHDR "T$time\n";
			print MSGHDR "D$df\n";
			print MSGHDR "N1\n";
			print MSGHDR "MDeferred: manually-requeued bounced message\n";
			foreach $r (@rcpt) {
				print MSGHDR "RP:$r\n";
			}
			$header =~ s/_FNORD_/\n/g;
			print MSGHDR $header;
			print MSGHDR "HMessage-ID: <$messageid@$hostname>\n"
				if ($messageid =~ /bounce-resender/);
			print MSGHDR ".\n";
			close MSGHDR;

			# jump to state waiting for message body
			$state = "IN_MESSAGE_BODY";

			$dev = $ino = $maj = $min = $r = $time = undef;
		} elsif (/^--$boundary/) {
			# signal an error
			print "$myname: Header without message! Line $lineno qf $qf\n";

			# write to qf anyway (SAME AS ABOVE, SHOULD BE A PROCEDURE)
			($dev, $ino) = (stat($df))[0 .. 1];
			($maj, $min) = (major($dev), minor($dev));
			$time = time();
			print MSGHDR "V2\n";
			print MSGHDR "B$ctencod\n";
			print MSGHDR "S$sender\n";
			print MSGHDR "I$maj/$min/$ino\n";
			print MSGHDR "K$time\n";
			print MSGHDR "T$time\n";
			print MSGHDR "D$df\n";
			print MSGHDR "N1\n";
			print MSGHDR "MDeferred: manually-requeued bounced message\n";
			foreach $r (@rcpt) {
				print MSGHDR "RP:$r\n";
			}
			$header =~ s/_FNORD_/\n/g;
			print MSGHDR $header;
			print MSGHDR "HMessage-ID: <$messageid@$hostname>\n"
				if ($messageid =~ /bounce-resender/);
			print MSGHDR ".\n";
			close MSGHDR;

			# jump to state waiting for next bounce message
			$state = "WAIT_FOR_FROM";

			$dev = $ino = $maj = $min = $r = $time = undef;
		} else {
			# never got here
			&ignorance();
		}
	} elsif ($state eq "IN_MESSAGE_BODY") {
		if (/^--$boundary/) {
			print MSGBODY $body;
			close MSGBODY;
			$state = "WAIT_FOR_FROM";
		} else {
			$body .= $_;
		}
	}
	if ($lineno % 1900 == 0) { &working(); }
}

close INPUT;

foreach $x (keys %ignored) {
	print STDERR
		"$myname: ignored $ignored{$x} lines of bounce spool in state $x\n";
}
print STDERR
	"$myname: processed $lineno lines of input and wrote $ctr messages\n";
print STDERR
	"$myname: remember to chown the queue files to root before running:\n";
chomp($pwd = `pwd`);
print STDERR "$myname:      # sendmail -q -oQ$pwd -oT99d &\n";

print STDERR "$myname: to test the newly generated queue:\n";
print STDERR "$myname:      # sendmail -bp -oQ$pwd | more\n";

exit 0;

