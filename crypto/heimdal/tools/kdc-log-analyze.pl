#! /usr/pkg/bin/perl
# -*- mode: perl; perl-indent-level: 8 -*-
# 
# Copyright (c) 2003 Kungliga Tekniska Högskolan
# (Royal Institute of Technology, Stockholm, Sweden). 
# All rights reserved. 
# 
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions 
# are met: 
# 
# 1. Redistributions of source code must retain the above copyright 
#    notice, this list of conditions and the following disclaimer. 
# 
# 2. Redistributions in binary form must reproduce the above copyright 
#    notice, this list of conditions and the following disclaimer in the 
#    documentation and/or other materials provided with the distribution. 
# 
# 3. Neither the name of the Institute nor the names of its contributors 
#    may be used to endorse or promote products derived from this software 
#    without specific prior written permission. 
# 
# THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
# ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
# SUCH DAMAGE. 
#
# $Id$
#
# kdc-log-analyze - Analyze a KDC log file and give a report on the contents
#
# Note: The parts you want likely want to customize are the variable $notlocal,
# the array @local_network_re and the array @local_realms.
#
# Idea and implemetion for MIT Kerberos was done first by 
# Ken Hornstein <kenh@cmf.nrl.navy.mil>, this program wouldn't exists
# without his help.
#

use strict;
use Sys::Hostname;

my $notlocal = 'not SU';
my @local_realms = ( "SU.SE" );
my @local_networks_re = 
    ( 
      "130\.237",
      "193\.11\.3[0-9]\.",
      "130.242.128",
      "2001:6b0:5:"
      );

my $as_req = 0;
my %as_req_addr;
my %as_req_addr_nonlocal;
my %as_req_client;
my %as_req_server;
my %addr_uses_des;
my %princ_uses_des;
my $five24_req = 0;
my %five24_req_addr;
my %five24_req_addr_nonlocal;
my %five24_req_server;
my %five24_req_client;
my $as_req_successful = 0;
my $as_req_error = 0;
my $no_such_princ = 0;
my %no_such_princ_princ;
my %no_such_princ_addr;
my %no_such_princ_addr_nonlocal;
my $as_req_etype_odd = 0;
my %bw_addr;
my $pa_alt_princ_request = 0;
my $pa_alt_princ_verify = 0;
my $tgs_req = 0;
my %tgs_req_addr;
my %tgs_req_addr_nonlocal;
my %tgs_req_client;
my %tgs_req_server;
my $tgs_xrealm_out = 0;
my %tgs_xrealm_out_realm;
my %tgs_xrealm_out_princ;
my $tgs_xrealm_in = 0;
my %tgs_xrealm_in_realm;
my %tgs_xrealm_in_princ;
my %enctype_session;
my %enctype_ticket;
my $restarts = 0;
my $forward_non_forward = 0;
my $v4_req = 0;
my %v4_req_addr;
my %v4_req_addr_nonlocal;
my $v4_cross = 0;
my %v4_cross_realm;
my $v5_cross = 0;
my %v5_cross_realm;
my $referrals = 0;
my %referral_princ;
my %referral_realm;
my %strange_tcp_data;
my $http_malformed = 0;
my %http_malformed_addr;
my $http_non_kdc = 0;
my %http_non_kdc_addr;
my $tcp_conn_timeout = 0;
my %tcp_conn_timeout_addr;
my $failed_processing = 0;
my %failed_processing_addr;
my $connection_closed = 0;
my %connection_closed_addr;
my $pa_failed = 0;
my %pa_failed_princ;
my %pa_failed_addr;
my %ip;

$ip{'4'} = $ip{'6'} = 0;

while (<>) {
	process_line($_);
}

print "Kerberos KDC Log Report for ", 
    hostname, " on ", scalar localtime, "\n\n";

print "General Statistics\n\n";

print "\tNumber of IPv4 requests: $ip{'4'}\n";
print "\tNumber of IPv6 requests: $ip{'6'}\n\n";

print "\tNumber of restarts: $restarts\n";
print "\tNumber of V4 requests: $v4_req\n";
if ($v4_req > 0) {
	print "\tTop ten IP addresses performing V4 requests:\n";
	topten(\%v4_req_addr);
}
if (int(keys %v4_req_addr_nonlocal) > 0) {
	print "\tTop ten $notlocal IP addresses performing V4 requests:\n";
	topten(\%v4_req_addr_nonlocal);

}
print "\n";

print "\tNumber of V4 cross realms (krb4 and 524) requests: $v4_cross\n";
if ($v4_cross > 0) {
	print "\tTop ten realms performing V4 cross requests:\n";
	topten(\%v4_cross_realm);
}
print "\n";

print "\tNumber of V45 cross realms requests: $v5_cross\n";
if ($v5_cross > 0) {
	print "\tTop ten realms performing V4 cross requests:\n";
	topten(\%v5_cross_realm);
}
print "\n";

print "\tNumber of failed lookups: $no_such_princ\n";
if ($no_such_princ > 0) {
	print "\tTop ten IP addresses failing to find principal:\n";
	topten(\%no_such_princ_addr);
	print "\tTop ten $notlocal IP addresses failing find principal:\n";
	topten(\%no_such_princ_addr_nonlocal);
	print "\tTop ten failed to find principals\n";
	topten(\%no_such_princ_princ);
}
print "\n";

print "\tBandwidth pigs:\n";
topten(\%bw_addr);
print "\n";

print "\tStrange TCP data clients: ", int(keys %strange_tcp_data),"\n";
topten(\%strange_tcp_data);
print "\n";

print "\tTimeout waiting on TCP requests: ", $tcp_conn_timeout,"\n";
if ($tcp_conn_timeout > 0) {
	print "\tTop ten TCP timeout request clients\n";
	topten(\%tcp_conn_timeout_addr);
}
print "\n";

print "\tFailed processing requests: ", $failed_processing,"\n";
if ($failed_processing > 0) {
	print "\tTop ten failed processing request clients\n";
	topten(\%failed_processing_addr);
}
print "\n";

print "\tConnection closed requests: ", $connection_closed,"\n";
if ($connection_closed > 0) {
	print "\tTop ten connection closed request clients\n";
	topten(\%connection_closed_addr);
}
print "\n";

print "\tMalformed HTTP requests: ", $http_malformed,"\n";
if ($http_malformed > 0) {
	print "\tTop ten malformed HTTP request clients\n";
	topten(\%http_malformed_addr);
}
print "\n";

print "\tHTTP non kdc requests: ", $http_non_kdc,"\n";
if ($http_non_kdc > 0) {
	print "\tTop ten HTTP non KDC request clients\n";
	topten(\%http_non_kdc_addr);
}
print "\n";

print "Report on AS_REQ requests\n\n";
print "Overall AS_REQ statistics\n\n";

print "\tTotal number: $as_req\n";

print "\nAS_REQ client/server statistics\n\n";

print "\tDistinct IP Addresses performing requests: ", 
    int(keys %as_req_addr),"\n";
print "\tOverall top ten IP addresses\n";
topten(\%as_req_addr);

print "\tDistinct non-local ($notlocal) IP Addresses performing requests: ",
					int(keys %as_req_addr_nonlocal), "\n";
print "\tTop ten non-local ($notlocal) IP address:\n";
topten(\%as_req_addr_nonlocal);

print "\n\tPreauth failed for for: ", $pa_failed, " requests\n";
if ($pa_failed) {
	print "\tPreauth failed top ten IP addresses:\n";
	topten(\%pa_failed_addr);
	print "\tPreauth failed top ten principals:\n";
	topten(\%pa_failed_princ);
}

print "\n\tDistinct clients performing requests: ", 
    int(keys %as_req_client), "\n";
print "\tTop ten clients:\n";
topten(\%as_req_client);

print "\tDistinct services requested: ", int(keys %as_req_server), "\n";
print "\tTop ten requested services:\n";
topten(\%as_req_server);

print "\n\n\nReport on TGS_REQ requests:\n\n";
print "Overall TGS_REQ statistics\n\n";
print "\tTotal number: $tgs_req\n";

print "\nTGS_REQ client/server statistics\n\n";
print "\tDistinct IP addresses performing requests: ",
				int(keys %tgs_req_addr), "\n";
print "\tOverall top ten IP addresses\n";
topten(\%tgs_req_addr);

print "\tDistinct non-local ($notlocal) IP Addresses performing requests: ",
				int(keys %tgs_req_addr_nonlocal), "\n";
print "\tTop ten non-local ($notlocal) IP address:\n";
topten(\%tgs_req_addr_nonlocal);

print "\tDistinct clients performing requests: ",
				int(keys %tgs_req_client), "\n";
print "\tTop ten clients:\n";
topten(\%tgs_req_client);

print "\tDistinct services requested: ", int(keys %tgs_req_server), "\n";
print "\tTop ten requested services:\n";
topten(\%tgs_req_server);

print "\n\n\nReport on 524_REQ requests:\n\n";

print "\t524_REQ client/server statistics\n\n";

print "\tDistinct IP Addresses performing requests: ", 
    int(keys %five24_req_addr),"\n";
print "\tOverall top ten IP addresses\n";
topten(\%five24_req_addr);

print "\tDistinct non-local ($notlocal) IP Addresses performing requests: ",
					int(keys %five24_req_addr_nonlocal), "\n";
print "\tTop ten non-local ($notlocal) IP address:\n";
topten(\%five24_req_addr_nonlocal);

print "\tDistinct clients performing requests: ", int(keys %five24_req_client), "\n";
print "\tTop ten clients:\n";
topten(\%five24_req_client);

print "\tDistinct services requested: ", int(keys %five24_req_server), "\n";
print "\tTop ten requested services:\n";
topten(\%five24_req_server);
print "\n";

print "Cross realm statistics\n\n";

print "\tNumber of cross-realm tgs out: $tgs_xrealm_out\n";
if ($tgs_xrealm_out > 0) {
	print "\tTop ten realms used for out cross-realm:\n";
	topten(\%tgs_xrealm_out_realm);
	print "\tTop ten principals use out cross-realm:\n";
	topten(\%tgs_xrealm_out_princ);
}
print "\tNumber of cross-realm tgs in: $tgs_xrealm_in\n";
if ($tgs_xrealm_in > 0) {
	print "\tTop ten realms used for in cross-realm:\n";
	topten(\%tgs_xrealm_in_realm);
	print "\tTop ten principals use in cross-realm:\n";
	topten(\%tgs_xrealm_in_princ);
}

print "\n\nReport on referral:\n\n";

print "\tNumber of referrals: $referrals\n";
if ($referrals > 0) {
	print "\tTop ten referral-ed principals:\n";
	topten(\%referral_princ);
	print "\tTop ten to realm referrals:\n";
	topten(\%referral_realm);
}

print "\n\nEnctype Statistics:\n\n";
print "\tTop ten session enctypes:\n";
topten(\%enctype_session);
print "\tTop ten ticket enctypes:\n";
topten(\%enctype_ticket);

print "\tDistinct IP addresses using DES: ", int(keys %addr_uses_des), "\n";
print "\tTop IP addresses using DES:\n";
topten(\%addr_uses_des);
print "\tDistinct principals using DES: ", int(keys %princ_uses_des), "\n";
print "\tTop ten principals using DES:\n";
topten(\%princ_uses_des);

print "\n";

printf("Requests to forward non-forwardable ticket: $forward_non_forward\n");


exit 0;

my $last_addr = "";
my $last_principal = "";

sub process_line {
	local($_) = @_;
	#
	# Eat these lines that are output as a result of startup (but
	# log the number of restarts)
	#
	if (/AS-REQ \(krb4\) (.*) from IPv([46]):([0-9\.:a-fA-F]+) for krbtgt.*$/){
		$v4_req++;
		$v4_req_addr{$3}++;
		$v4_req_addr_nonlocal{$3}++ if (!islocaladdr($3));
		$last_addr = $3;
		$last_principal = $1;
		$ip{$2}++;
	} elsif (/AS-REQ (.*) from IPv([46]):([0-9\.:a-fA-F]+) for (.*)$/) {
		$as_req++;
		$as_req_client{$1}++;
		$as_req_server{$4}++;
		$as_req_addr{$3}++;
		$as_req_addr_nonlocal{$3}++ if (!islocaladdr($3));
		$last_addr = $3;
		$last_principal = $1;
		$ip{$2}++;
	} elsif (/TGS-REQ \(krb4\)/) {
		#Nothing
	} elsif (/TGS-REQ (.+) from IPv([46]):([0-9\.:a-fA-F]+) for (.*?)( \[.*\]){0,1}$/) {
		$tgs_req++;
		$tgs_req_client{$1}++;
		$tgs_req_server{$4}++;
		$tgs_req_addr{$3}++;
		$tgs_req_addr_nonlocal{$3}++ if (!islocaladdr($3));
		$last_addr = $3;
		$last_principal = $1;
		$ip{$2}++;

		my $source = $1;
		my $dest = $4;
		
		if (!islocalrealm($source)) {
			$tgs_xrealm_in++;
			$tgs_xrealm_in_princ{$source}++;
			if ($source =~ /[^@]+@([^@]+)/ ) {
				$tgs_xrealm_in_realm{$1}++;
			}
		}
		if ($dest =~ /krbtgt\/([^@]+)@[^@]+/) {
			if (!islocalrealm($1)) {
				$tgs_xrealm_out++;
				$tgs_xrealm_out_realm{$1}++;
				$tgs_xrealm_out_princ{$source}++;
			}
		}
	} elsif (/524-REQ (.*) from IPv([46]):([0-9\.:a-fA-F]+) for (.*)$/) {
		$five24_req++;
		$five24_req_client{$1}++;
		$five24_req_server{$4}++;
		$five24_req_addr{$3}++;
		$five24_req_addr_nonlocal{$3}++ if (!islocaladdr($3));
		$last_addr = $3;
		$last_principal = $1;
		$ip{$2}++;
	} elsif (/TCP data of strange type from IPv[46]:([0-9\.:a-fA-F]+)/) {
		$strange_tcp_data{$1}++;
	} elsif (/Lookup (.*) failed: No such entry in the database/) {
		$no_such_princ++;
		$no_such_princ_addr{$last_addr}++;
		$no_such_princ_addr_nonlocal{$last_addr}++ if (!islocaladdr($last_addr));
		$no_such_princ_princ{$1}++;
	} elsif (/Lookup .* succeeded$/) {
		# Nothing
	} elsif (/Malformed HTTP request from IPv[46]:([0-9\.:a-fA-F]+)$/) {
		$http_malformed++;
		$http_malformed_addr{$1}++;
	} elsif (/TCP-connection from IPv[46]:([0-9\.:a-fA-F]+) expired after [0-9]+ bytes/) {
		$tcp_conn_timeout++;
		$tcp_conn_timeout_addr{$1}++;
	} elsif (/Failed processing [0-9]+ byte request from IPv[46]:([0-9\.:a-fA-F]+)/) {
		$failed_processing++;
		$failed_processing_addr{$1}++;
	} elsif (/connection closed before end of data after [0-9]+ bytes from IPv[46]:([0-9\.:a-fA-F]+)/) {
		$connection_closed++;
		$connection_closed_addr{$1}++;
	} elsif (/HTTP request from IPv[46]:([0-9\.:a-fA-F]+) is non KDC request/) {
		$http_non_kdc++;
		$http_non_kdc_addr{$1}++;
	} elsif (/returning a referral to realm (.*) for server (.*) that was not found/) {
		$referrals++;
		$referral_princ{$2}++;
		$referral_realm{$1}++;
	} elsif (/krb4 Cross-realm (.*) -> (.*) disabled/) {
		$v4_cross++;
		$v4_cross_realm{$1."->".$2}++;
	} elsif (/524 cross-realm (.*) -> (.*) disabled/) {
		$v4_cross++;
		$v4_cross_realm{$1."->".$2}++;
	} elsif (/cross-realm (.*) -> (.*): no transit through realm (.*)/) {
	} elsif (/cross-realm (.*) -> (.*) via \[([^\]]+)\]/) {
		$v5_cross++;
		$v5_cross_realm{$1."->".$2}++;
	} elsif (/cross-realm (.*) -> (.*)/) {
		$v5_cross++;
		$v5_cross_realm{$1."->".$2}++;
	} elsif (/sending ([0-9]+) bytes to IPv[46]:([0-9\.:a-fA-F]+)/) {
		$bw_addr{$2} += $1;
	} elsif (/Using ([-a-z0-9]+)\/([-a-z0-9]+)/) {
		$enctype_ticket{$1}++;
		$enctype_session{$2}++;

		my $ticket = $1;
		my $session = $2;

		if ($ticket =~ /des-cbc-(crc|md4|md5)/) {
			$addr_uses_des{$last_addr}++;
			$princ_uses_des{$last_principal}++;
		}

	} elsif (/Failed to decrypt PA-DATA -- (.+)$/) {
		$pa_failed++;
		$pa_failed_princ{$last_principal}++;
		$pa_failed_addr{$last_addr}++;

	} elsif (/Request to forward non-forwardable ticket/) {
		$forward_non_forward++;
	} elsif (/HTTP request:/) {
	} elsif (/krb_rd_req: Incorrect network address/) {
	} elsif (/krb_rd_req: Ticket expired \(krb_rd_req\)/) {
	} elsif (/Ticket expired \(.*\)/) {
	} elsif (/krb_rd_req: Can't decode authenticator \(krb_rd_req\)/) {
	} elsif (/Request from wrong address/) {
		# XXX
	} elsif (/UNKNOWN --/) {
		# XXX
	} elsif (/Too large time skew -- (.*)$/) {
		# XXX
	} elsif (/No PA-ENC-TIMESTAMP --/) {
		# XXX
	} elsif (/Looking for pa-data --/) {
		# XXX
	} elsif (/Pre-authentication succeded -- (.+)$/) {
		# XXX
	} elsif (/Bad request for ([,a-zA-Z0-9]+) ticket/) {
		# XXX
	} elsif (/Failed to verify AP-REQ: Ticket expired/) {
		# XXX 
	} elsif (/Client not found in database:/) {
		# XXX
	} elsif (/Server not found in database \(krb4\)/) {
	} elsif (/Server not found in database:/) {
		# XXX
	} elsif (/newsyslog.*logfile turned over/) {
		# Nothing
	} elsif (/Requested flags:/) {
		# Nothing
	} elsif (/shutting down/) {
		# Nothing
	} elsif (/listening on IP/) {
		# Nothing
	} elsif (/commencing operation/) {
		$restarts++;
	}
	#
	# Log it if we didn't parse the line
	#
	else {
		print "Unknown log file line: $_";
	}
}

sub topten {
	my ($list) = @_;
	my @keys;

	my $key;

	@keys = (sort {$$list{$b} <=> $$list{$a}} (keys %{$list}));
	splice @keys, 10;

	foreach $key (@keys) {
		print "\t\t$key - $$list{$key}\n";
	}
}

sub islocaladdr (\$) {
	my ($addr) = @_;
	my $net;

	foreach $net (@local_networks_re) {
		return 1 if ($addr =~ /$net/);
	}
	return 0;
}

sub islocalrealm (\$) {
	my ($princ) = @_;
	my $realm;

	foreach $realm (@local_realms) {
		return 1 if ($princ eq $realm);
		return 1 if ($princ =~ /[^@]+\@${realm}/);
	}
	return 0;
}
