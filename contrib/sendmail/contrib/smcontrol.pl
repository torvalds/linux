#!/usr/bin/perl -w

# $Id: smcontrol.pl,v 8.8 2008-07-21 21:31:43 ca Exp $

use strict;
use Getopt::Std;
use FileHandle;
use Socket;

my $sendmailDaemon = "/usr/sbin/sendmail -q30m -bd";

##########################################################################
#
#  &get_controlname -- read ControlSocketName option from sendmail.cf
#
#	Parameters:
#		none.
#
#	Returns:
#		control socket filename, undef if not found
#

sub get_controlname
{
	my $cn = undef;
	my $qd = undef;
 
	open(CF, "</etc/mail/sendmail.cf") or return $cn;
	while (<CF>)
	{
		chomp;
		if (/^O ControlSocketName\s*=\s*([^#]+)$/o)
		{
			$cn = $1;
		}
		if (/^O QueueDirectory\s*=\s*([^#]+)$/o)
		{
			$qd = $1;
		}
		if (/^OQ([^#]+)$/o)
		{
			$qd = $1;
		}
	}
	close(CF);
	if (not defined $cn)
	{
		return undef;
	}
	if ($cn !~ /^\//o)
	{
		return undef if (not defined $qd);
		
		$cn = $qd . "/" . $cn;
	}
	return $cn;
}

##########################################################################
#
#  &do_command -- send command to sendmail daemon view control socket
#
#	Parameters:
#		controlsocket -- filename for socket
#		command -- command to send
#
#	Returns:
#		reply from sendmail daemon
#

sub do_command
{
	my $controlsocket = shift;
	my $command = shift;
	my $proto = getprotobyname('ip');
	my @reply;
	my $i;

	socket(SOCK, PF_UNIX, SOCK_STREAM, $proto) or return undef;

	for ($i = 0; $i < 4; $i++)
	{
		if (!connect(SOCK, sockaddr_un($controlsocket)))
		{
			if ($i == 3)
			{
				close(SOCK);
				return undef;
			}
			sleep 1;
			next;
		}
		last;
	}
	autoflush SOCK 1;
	print SOCK "$command\n";
	@reply = <SOCK>;
	close(SOCK);
	return join '', @reply;
}

##########################################################################
#
#  &sendmail_running -- check if sendmail is running via SMTP
#
#	Parameters:
#		none
#
#	Returns:
#		1 if running, undef otherwise
#

sub sendmail_running
{
	my $port = getservbyname("smtp", "tcp") || 25;
	my $proto = getprotobyname("tcp");
	my $iaddr = inet_aton("localhost");
	my $paddr = sockaddr_in($port, $iaddr);

	socket(SOCK, PF_INET, SOCK_STREAM, $proto) or return undef;
	if (!connect(SOCK, $paddr))
	{
		close(SOCK);
		return undef;
	}
	autoflush SOCK 1;
	while (<SOCK>)
	{
		if (/^(\d{3})([ -])/)
		{
			if ($1 != 220)
			{
				close(SOCK);
				return undef;
			}
		}
		else
		{
			close(SOCK);
			return undef;
		}
		last if ($2 eq " ");
	}
	print SOCK "QUIT\n";
	while (<SOCK>)
	{
		last if (/^\d{3} /);
	}
	close(SOCK);
	return 1;
}

##########################################################################
#
#  &munge_status -- turn machine readable status into human readable text
#
#	Parameters:
#		raw -- raw results from sendmail daemon STATUS query
#
#	Returns:
#		human readable text
#

sub munge_status
{
	my $raw = shift;
	my $cooked = "";
	my $daemonStatus = "";

	if ($raw =~ /^(\d+)\/(\d+)\/(\d+)\/(\d+)/mg)
	{
		$cooked .= "Current number of children: $1";
		if ($2 > 0)
		{
			$cooked .= " (maximum $2)";
		}
		$cooked .= "\n";
		$cooked .= "QueueDir free disk space (in blocks): $3\n";
		$cooked .= "Load average: $4\n";
	}
	while ($raw =~ /^(\d+) (.*)$/mg)
	{
		if (not $daemonStatus)
		{
			$daemonStatus = "(process $1) " . ucfirst($2) . "\n";
		}
		else
		{
			$cooked .= "Child Process $1 Status: $2\n";
		}
	}
	return ($daemonStatus, $cooked);
}

##########################################################################
#
#  &start_daemon -- fork off a sendmail daemon
#
#	Parameters:
#		control -- control socket name
#
#	Returns:
#		Error message or "OK" if successful
#

sub start_daemon
{
	my $control = shift;
	my $pid;

	if ($pid = fork)
	{
		my $exitstat;

		waitpid $pid, 0 or return "Could not get status of created process: $!\n";
		$exitstat = $? / 256;
		if ($exitstat != 0)
		{
			return "sendmail daemon startup exited with exit value $exitstat";
		}
	}
	elsif (defined $pid)
	{
		exec($sendmailDaemon);
		die "Unable to start sendmail daemon: $!.\n";
	}
	else
	{
		return "Could not create new process: $!\n";
	}
	return "OK\n";
}

##########################################################################
#
#  &stop_daemon -- stop the sendmail daemon using control socket
#
#	Parameters:
#		control -- control socket name
#
#	Returns:
#		Error message or status message
#

sub stop_daemon
{
	my $control = shift;
	my $status;

	if (not defined $control)
	{
		return "The control socket is not configured so the daemon can not be stopped.\n";
	}
	return &do_command($control, "SHUTDOWN");
}

##########################################################################
#
#  &restart_daemon -- restart the sendmail daemon using control socket
#
#	Parameters:
#		control -- control socket name
#
#	Returns:
#		Error message or status message
#

sub restart_daemon
{
	my $control = shift;
	my $status;

	if (not defined $control)
	{
		return "The control socket is not configured so the daemon can not be restarted.";
	}
	return &do_command($control, "RESTART");
}

##########################################################################
#
#  &memdump -- get memdump from the daemon using the control socket
#
#	Parameters:
#		control -- control socket name
#
#	Returns:
#		Error message or status message
#

sub memdump
{
	my $control = shift;
	my $status;

	if (not defined $control)
	{
		return "The control socket is not configured so the daemon can not be queried for memdump.";
	}
	return &do_command($control, "MEMDUMP");
}

##########################################################################
#
#  &help -- get help from the daemon using the control socket
#
#	Parameters:
#		control -- control socket name
#
#	Returns:
#		Error message or status message
#

sub help
{
	my $control = shift;
	my $status;

	if (not defined $control)
	{
		return "The control socket is not configured so the daemon can not be queried for help.";
	}
	return &do_command($control, "HELP");
}

my $status = undef;
my $daemonStatus = undef;
my $opts = {};

getopts('f:', $opts) || die "Usage: $0 [-f /path/to/control/socket] command\n";

my $control = $opts->{f} || &get_controlname;
my $command = shift;

if (not defined $control)
{
	die "No control socket available.\n";
}
if (not defined $command)
{
	die "Usage: $0 [-f /path/to/control/socket] command\n";
}
if ($command eq "status")
{
	$status = &do_command($control, "STATUS");
	if (not defined $status)
	{
		# Not responding on control channel, query via SMTP
		if (&sendmail_running)
		{
			$daemonStatus = "Sendmail is running but not answering status queries.";
		}
		else
		{
			$daemonStatus = "Sendmail does not appear to be running.";
		}
	}
	else
	{
		# Munge control channel output
		($daemonStatus, $status) = &munge_status($status);
	}
}
elsif (lc($command) eq "shutdown")
{
	$status = &stop_daemon($control);
}
elsif (lc($command) eq "restart")
{
	$status = &restart_daemon($control);
}
elsif (lc($command) eq "start")
{
	$status = &start_daemon($control);
}
elsif (lc($command) eq "memdump")
{
	$status = &memdump($control);
}
elsif (lc($command) eq "help")
{
	$status = &help($control);
}
elsif (lc($command) eq "mstat")
{
	$status = &do_command($control, "mstat");
	if (not defined $status)
	{
		# Not responding on control channel, query via SMTP
		if (&sendmail_running)
		{
			$daemonStatus = "Sendmail is running but not answering status queries.";
		}
		else
		{
			$daemonStatus = "Sendmail does not appear to be running.";
		}
	}
}
else
{
	die "Unrecognized command $command\n";
}
if (defined $daemonStatus)
{
	print "Daemon Status: $daemonStatus\n";
}
if (defined $status)
{
	print "$status\n";
}
else
{
	die "No response\n";
}
