#!/usr/pkg/bin/perl
#
# Sample password verifier for Heimdals external password
# verifier, see the chapter "Password changing" in the the info
# documentation for more information about the protocol used.
#
# Three checks
#  1. Check that password is not the principal name
#  2. Check that the password passes cracklib
#  3. Check that password isn't repeated for this principal
#
# The repeat check must be last because some clients ask
# twice when getting "no" back and thus the error message
# would be wrong.
#
# Prereqs (example versions): 
#
# * perl (5.8.5) http://www.perl.org/
# * cracklib (2.8.5) http://sourceforge.net/projects/cracklib
# * Crypt-Cracklib perlmodule (0.01) http://search.cpan.org/~daniel/
#
# Sample dictionaries:
#     cracklib-words (1.1) http://sourceforge.net/projects/cracklib
#     miscfiles (1.4.2) http://directory.fsf.org/miscfiles.html
#
# Configuration for krb5.conf or kdc.conf
#
#   [password_quality]
#     	policies = builtin:external-check
#     	external_program = <your-path>/check-cracklib.pl
#
# $Id$

use strict;
use Crypt::Cracklib;
use Digest::MD5;

# NEED TO CHANGE THESE TO MATCH YOUR SYSTEM
my $database = '/usr/lib/cracklib_dict';
my $historydb = '/var/heimdal/historydb';
# NEED TO CHANGE THESE TO MATCH YOUR SYSTEM

# seconds password reuse allowed (to catch retries from clients)
my $reusetime = 60; 

my %params;

sub check_basic
{
    my $principal = shift;
    my $passwd = shift;

    if ($principal eq $passwd) {
	return "Principal name as password is not allowed";
    }
    return "ok";
}

sub check_repeat
{
    my $principal = shift;
    my $passwd = shift;
    my $result  = 'Do not reuse passwords';
    my %DB;
    my $md5context = new Digest::MD5;
    my $timenow = scalar(time());

    $md5context->reset();
    $md5context->add($principal, ":", $passwd);

    my $key=$md5context->hexdigest();

    dbmopen(%DB,$historydb,0600) or die "Internal: Could not open $historydb";
    if (!$DB{$key} || ($timenow - $DB{$key} < $reusetime)) { 
	$result = "ok";
	$DB{$key}=$timenow;
    }
    dbmclose(%DB) or die "Internal: Could not close $historydb";
    return $result;
}

sub badpassword
{
    my $reason = shift;
    print "$reason\n";
    exit 0
}

while (<STDIN>) {
    last if /^end$/;
    if (!/^([^:]+): (.+)$/) {
	die "key value pair not correct: $_";
    }
    $params{$1} = $2;
}

die "missing principal" if (!defined $params{'principal'});
die "missing password" if (!defined $params{'new-password'});

my $reason;

$reason = check_basic($params{'principal'}, $params{'new-password'});
badpassword($reason) if ($reason ne "ok");

$reason = fascist_check($params{'new-password'}, $database);
badpassword($reason) if ($reason ne "ok");

$reason = check_repeat($params{'principal'}, $params{'new-password'});
badpassword($reason) if ($reason ne "ok");

print "APPROVED\n";
exit 0
