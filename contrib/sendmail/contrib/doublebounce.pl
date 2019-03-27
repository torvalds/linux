#!/usr/bin/perl
# doublebounce.pl
#
# Return a doubly-bounced e-mail to postmaster.  Specific to sendmail,
# updated to work on sendmail 8.12.6.
#
# Based on the original doublebounce.pl code by jr@terra.net, 12/4/97.
# Updated by bicknell@ufp.org, 12/4/2002 to understand new sendmail DSN
# bounces.  Code cleanup also performed, mainly making things more
# robust.
#
# Original intro included below, lines with ##
##	attempt to return a doubly-bounced email to a postmaster
##	jr@terra.net, 12/4/97
##
##	invoke by creating an mail alias such as:
##		doublebounce:	"|/usr/local/sbin/doublebounce"
##	then adding this line to your sendmail.cf:
##		O DoubleBounceAddress=doublebounce
##
##	optionally, add a "-d" flag in the aliases file, to send a
##	debug trace to your own postmaster showing what is going on
##
##	this allows the "postmaster" address to still go to a human being,
##	while bounce messages can go to this script, which will bounce them
##	back to the postmaster at the sending site.
##
##	the algorithm is to scan the double-bounce error report generated
##	by sendmail on stdin, for the original message (it starts after the
##	second "Orignal message follows" marker), look for From, Sender, and
##	Received headers from the point closest to the sender back to the point
##	closest to us, and try to deliver a double-bounce report back to a
##	postmaster at one of these sites in the hope that they can
##	return the message to the original sender, or do something about
##	the fact that that sender's return address is not valid.

use Socket;
use Getopt::Std;
use File::Temp;
use Sys::Syslog qw(:DEFAULT setlogsock);
use strict;
use vars qw( $opt_d $tmpfile);

# parseaddr()
#	parse hostname from From: header
#
sub parseaddr {
  my($hdr) = @_;
  my($addr);

  if ($hdr =~ /<.*>/) {
    ($addr) = $hdr =~ m/<(.*)>/;
    $addr =~ s/.*\@//;
    return $addr;
  }
  if ($addr =~ /\s*\(/) {
    ($addr) = $hdr =~ m/\s*(.*)\s*\(/;
    $addr =~ s/.*\@//;
    return $addr;
  }
  ($addr) = $hdr =~ m/\s*(.*)\s*/;
  $addr =~ s/.*\@//;
  return $addr;
}

# sendbounce()
#	send bounce to postmaster
#
#	this re-invokes sendmail in immediate and quiet mode to try
#	to deliver to a postmaster.  sendmail's exit status tells us
#	whether the delivery attempt really was successful.
#
sub send_bounce {
  my($addr, $from) = @_;
  my($st);
  my($result);

  my($dest) = "postmaster\@" . parseaddr($addr);

  if ($opt_d) {
    syslog ('info', "Attempting to send to user $dest");
  }
  open(MAIL, "| /usr/sbin/sendmail -oeq $dest");
  print MAIL <<EOT;
From: Mail Delivery Subsystem <mail-router>
Subject: Postmaster notify: double bounce
Reply-To: nobody
Errors-To: nobody
Precedence: junk
Auto-Submitted: auto-generated (postmaster notification)

The following message was received for an invalid recipient.  The
sender's address was also invalid.  Since the message originated
at or transited through your mailer, this notification is being
sent to you in the hope that you will determine the real originator
and have them correct their From or Sender address.

The from header on the original e-mail was: $from.

   ----- The following is a double bounce -----

EOT

  open(MSG, "<$tmpfile");
  print MAIL <MSG>;
  close(MSG);
  $result = close(MAIL);
  if ($result) {
    syslog('info', 'doublebounce successfully sent to %s', $dest);
  }
  return $result;
}

sub main {
  # Get our command line options
  getopts('d');

  # Set up syslog
  setlogsock('unix');
  openlog('doublebounce', 'pid', 'mail');
 
  if ($opt_d) {
    syslog('info', 'Processing a doublebounce.');
  }

  # The bounced e-mail may be large, so we'd better not try to buffer
  # it in memory, get a temporary file.
  $tmpfile = tmpnam();

  if (!open(MSG, ">$tmpfile")) {
    syslog('err', "Unable to open temporary file $tmpfile");
    exit(75); # 75 is a temporary failure, sendmail should retry
  }
  print(MSG <STDIN>);
  close(MSG);
  if (!open(MSG, "<$tmpfile")) {
    syslog('err', "Unable to reopen temporary file $tmpfile");
    exit(74); # 74 is an IO error
  }

  # Ok, now we can get down to business, find the original message
  my($skip_lines, $in_header, $headers_found, @addresses);
  $skip_lines = 0;
  $in_header = 0;
  $headers_found = 0;
  while (<MSG>) {
    if ($skip_lines > 0) {
      $skip_lines--;
      next;
    }
    chomp;
    # Starting message depends on your version of sendmail
    if (/^   ----- Original message follows -----$/ ||
        /^   ----Unsent message follows----$/ ||
        /^Content-Type: message\/rfc822$/) {
      # Found the original message
      $skip_lines++;
      $in_header = 1;
      $headers_found++;
      next;
    }
    if (/^$/) {
      if ($headers_found >= 2) {
         # We only process two deep, even if there are more
         last;
      }
      if ($in_header) {
         # We've found the end of a header, scan for the next one
         $in_header = 0;
      }
      next;
    }
    if ($in_header) {
      if (! /^[ \t]/) {
        # New Header
        if (/^(received): (.*)/i ||
            /^(reply-to): (.*)/i ||    
            /^(sender): (.*)/i ||    
            /^(from): (.*)/i ) {
          $addresses[$headers_found]{$1} = $2;
        }
        next;
      } else {
        # continuation header
        # we should really process these, but we don't yet
        next;
      }
    } else {
      # Nothing to do if we're not in a header
      next;
    }
  }
  close(MSG);

  # Start with the original (inner) sender
  my($addr, $sent);
  foreach $addr (keys %{$addresses[2]}) {
    if ($opt_d) {
      syslog('info', "Trying to send to $addresses[2]{$addr} - $addresses[2]{\"From\"}");
    }
    $sent = send_bounce($addresses[2]{$addr}, $addresses[2]{"From"});
    last if $sent;
  }
  if (!$sent && $opt_d) {
    if ($opt_d) {
      syslog('info', 'Unable to find original sender, falling back.');
    }
    foreach $addr (keys %{$addresses[1]}) {
      if ($opt_d) {
        syslog('info', "Trying to send to $addresses[2]{$addr} - $addresses[2]{\"From\"}");
      }
      $sent = send_bounce($addresses[1]{$addr}, $addresses[2]{"From"});
      last if $sent;
    }
    if (!$sent) {
      syslog('info', 'Unable to find anyone to send a doublebounce notification');
    }
  }

  unlink($tmpfile);
}

main();
exit(0);

