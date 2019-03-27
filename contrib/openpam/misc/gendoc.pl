#!/usr/bin/perl -w
#-
# Copyright (c) 2002-2003 Networks Associates Technology, Inc.
# Copyright (c) 2004-2017 Dag-Erling Smørgrav
# All rights reserved.
#
# This software was developed for the FreeBSD Project by ThinkSec AS and
# Network Associates Laboratories, the Security Research Division of
# Network Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
# ("CBOSS"), as part of the DARPA CHATS research program.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote
#    products derived from this software without specific prior written
#    permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $OpenPAM: gendoc.pl 938 2017-04-30 21:34:42Z des $
#

use strict;
use warnings;
use open qw(:utf8);
use utf8;
use Fcntl;
use Getopt::Std;
use POSIX qw(strftime);
use vars qw(%AUTHORS $TODAY %FUNCTIONS %PAMERR);

%AUTHORS = (
    THINKSEC => "developed for the
.Fx
Project by ThinkSec AS and Network Associates Laboratories, the
Security Research Division of Network Associates, Inc.\\& under
DARPA/SPAWAR contract N66001-01-C-8035
.Pq Dq CBOSS ,
as part of the DARPA CHATS research program.
.Pp
The OpenPAM library is maintained by
.An Dag-Erling Sm\\(/orgrav Aq Mt des\@des.no .",
    UIO => "developed for the University of Oslo by
.An Dag-Erling Sm\\(/orgrav Aq Mt des\@des.no .",
    DES => "developed by
.An Dag-Erling Sm\\(/orgrav Aq Mt des\@des.no .",
);

%PAMERR = (
    PAM_SUCCESS			=> "Success",
    PAM_OPEN_ERR		=> "Failed to load module",
    PAM_SYMBOL_ERR		=> "Invalid symbol",
    PAM_SERVICE_ERR		=> "Error in service module",
    PAM_SYSTEM_ERR		=> "System error",
    PAM_BUF_ERR			=> "Memory buffer error",
    PAM_CONV_ERR		=> "Conversation failure",
    PAM_PERM_DENIED		=> "Permission denied",
    PAM_MAXTRIES		=> "Maximum number of tries exceeded",
    PAM_AUTH_ERR		=> "Authentication error",
    PAM_NEW_AUTHTOK_REQD	=> "New authentication token required",
    PAM_CRED_INSUFFICIENT	=> "Insufficient credentials",
    PAM_AUTHINFO_UNAVAIL	=> "Authentication information is unavailable",
    PAM_USER_UNKNOWN		=> "Unknown user",
    PAM_CRED_UNAVAIL		=> "Failed to retrieve user credentials",
    PAM_CRED_EXPIRED		=> "User credentials have expired",
    PAM_CRED_ERR		=> "Failed to set user credentials",
    PAM_ACCT_EXPIRED		=> "User account has expired",
    PAM_AUTHTOK_EXPIRED		=> "Password has expired",
    PAM_SESSION_ERR		=> "Session failure",
    PAM_AUTHTOK_ERR		=> "Authentication token failure",
    PAM_AUTHTOK_RECOVERY_ERR	=> "Failed to recover old authentication token",
    PAM_AUTHTOK_LOCK_BUSY	=> "Authentication token lock busy",
    PAM_AUTHTOK_DISABLE_AGING	=> "Authentication token aging disabled",
    PAM_NO_MODULE_DATA		=> "Module data not found",
    PAM_IGNORE			=> "Ignore this module",
    PAM_ABORT			=> "General failure",
    PAM_TRY_AGAIN		=> "Try again",
    PAM_MODULE_UNKNOWN		=> "Unknown module type",
    PAM_DOMAIN_UNKNOWN		=> "Unknown authentication domain",
    PAM_BAD_HANDLE		=> "Invalid PAM handle",
    PAM_BAD_ITEM		=> "Unrecognized or restricted item",
    PAM_BAD_FEATURE		=> "Unrecognized or restricted feature",
    PAM_BAD_CONSTANT		=> "Bad constant",
);

sub parse_source($) {
    my $fn = shift;

    local *FILE;
    my $source;
    my $func;
    my $descr;
    my $type;
    my $args;
    my $argnames;
    my $man;
    my $inlist;
    my $intaglist;
    my $inliteral;
    my $customrv;
    my $deprecated;
    my $experimental;
    my $version;
    my %xref;
    my %errors;
    my $author;

    if ($fn !~ m,\.c$,) {
	warn("$fn: not C source, ignoring\n");
	return undef;
    }

    open(FILE, "<", "$fn")
	or die("$fn: open(): $!\n");
    $source = join('', <FILE>);
    close(FILE);

    return undef
	if ($source =~ m/^ \* NOPARSE\s*$/m);

    if ($source =~ m/(\$OpenPAM:[^\$]+\$)/) {
	$version = $1;
    }

    $author = 'THINKSEC';
    if ($source =~ s/^ \* AUTHOR\s+(\w*)\s*$//m) {
	$author = $1;
    }

    if ($source =~ s/^ \* DEPRECATED\s*(\w*)\s*$//m) {
	$deprecated = $1 // 0;
    }

    if ($source =~ s/^ \* EXPERIMENTAL\s*$//m) {
	$experimental = 1;
    }

    $func = $fn;
    $func =~ s,^(?:.*/)?([^/]+)\.c$,$1,;
    if ($source !~ m,\n \* ([\S ]+)\n \*/\n\n([\S ]+)\n$func\((.*?)\)\n\{,s) {
	warn("$fn: can't find $func\n");
	return undef;
    }
    ($descr, $type, $args) = ($1, $2, $3);
    $descr =~ s,^([A-Z][a-z]),lc($1),e;
    $descr =~ s,[\.\s]*$,,;
    while ($args =~ s/^((?:[^\(]|\([^\)]*\))*),\s*/$1\" \"/g) {
	# nothing
    }
    $args =~ s/,\s+/, /gs;
    $args = "\"$args\"";

    %xref = (
	3 => { 'pam' => 1 },
    );

    if ($type eq "int") {
	foreach (split("\n", $source)) {
	    next unless (m/^ \*\t(!?PAM_[A-Z_]+|=[a-z_]+)\s*(.*?)\s*$/);
	    $errors{$1} = $2;
	}
	++$xref{3}->{pam_strerror};
    }

    $argnames = $args;
    # extract names of regular arguments
    $argnames =~ s/\"[^\"]+\*?\b(\w+)\"/\"$1\"/g;
    # extract names of function pointer arguments
    $argnames =~ s/\"([\w\s\*]+)\(\*?(\w+)\)\([^\)]+\)\"/\"$2\"/g;
    # escape metacharacters (there shouldn't be any, but...)
    $argnames =~ s/([\|\[\]\(\)\.\*\+\?])/\\$1/g;
    # separate argument names with |
    $argnames =~ s/\" \"/|/g;
    # and surround with ()
    $argnames =~ s/^\"(.*)\"$/$1/;
    # $argnames is now a regexp that matches argument names
    $inliteral = $inlist = $intaglist = 0;
    foreach (split("\n", $source)) {
	s/\s*$//;
	if (!defined($man)) {
	    if (m/^\/\*\*$/) {
		$man = "";
	    }
	    next;
	}
	last if (m/^ \*\/$/);
	s/^ \* ?//;
	s/\\(.)/$1/gs;
	if (m/^$/) {
	    # paragraph separator
	    if ($inlist || $intaglist) {
		# either a blank line between list items, or a blank
		# line after the final list item.  The latter case
		# will be handled further down.
		next;
	    }
	    if ($man =~ m/\n\.Sh [^\n]+\n$/s) {
		# a blank line after a section header
		next;
	    }
	    if ($man ne "" && $man !~ m/\.Pp\n$/s) {
		if ($inliteral) {
		    $man .= "\0\n";
		} else {
		    $man .= ".Pp\n";
		}
	    }
	    next;
	}
	if (m/^>(\w+)(\s+\d)?$/) {
	    # "see also" cross-reference
	    my ($page, $sect) = ($1, $2 ? int($2) : 3);
	    ++$xref{$sect}->{$page};
	    next;
	}
	if (s/^([A-Z][0-9A-Z -]+)$/.Sh $1/) {
	    if ($1 eq "RETURN VALUES") {
		$customrv = $1;
	    }
	    $man =~ s/\n\.Pp$/\n/s;
	    $man .= "$_\n";
	    next;
	}
	if (s/^\s+-\s+//) {
	    # item in bullet list
	    if ($inliteral) {
		$man .= ".Ed\n";
		$inliteral = 0;
	    }
	    if ($intaglist) {
		$man .= ".El\n.Pp\n";
		$intaglist = 0;
	    }
	    if (!$inlist) {
		$man =~ s/\.Pp\n$//s;
		$man .= ".Bl -bullet\n";
		$inlist = 1;
	    }
	    $man .= ".It\n";
	    # fall through
	} elsif (s/^\s+(\S+):\s*/.It $1/) {
	    # item in tag list
	    if ($inliteral) {
		$man .= ".Ed\n";
		$inliteral = 0;
	    }
	    if ($inlist) {
		$man .= ".El\n.Pp\n";
		$inlist = 0;
	    }
	    if (!$intaglist) {
		$man =~ s/\.Pp\n$//s;
		$man .= ".Bl -tag -width 18n\n";
		$intaglist = 1;
	    }
	    s/^\.It [=;]([A-Za-z][0-9A-Za-z_]+)$/.It Dv $1/gs;
	    $man .= "$_\n";
	    next;
	} elsif (($inlist || $intaglist) && m/^\S/) {
	    # regular text after list
	    $man .= ".El\n.Pp\n";
	    $inlist = $intaglist = 0;
	} elsif ($inliteral && m/^\S/) {
	    # regular text after literal section
	    $man .= ".Ed\n";
	    $inliteral = 0;
	} elsif ($inliteral) {
	    # additional text within literal section
	    $man .= "$_\n";
	    next;
	} elsif ($inlist || $intaglist) {
	    # additional text within list
	    s/^\s+//;
	} elsif (m/^\s+/) {
	    # new literal section
	    $man .= ".Bd -literal\n";
	    $inliteral = 1;
	    $man .= "$_\n";
	    next;
	}
	s/\s*=($func)\b\s*/\n.Fn $1\n/gs;
	s/\s*=($argnames)\b\s*/\n.Fa $1\n/gs;
	s/\s*=((?:enum|struct|union) \w+(?: \*)?)\b\s*/\n.Vt $1\n/gs;
	s/\s*:([a-z][0-9a-z_]+)\b\s*/\n.Va $1\n/gs;
	s/\s*;([a-z][0-9a-z_]+)\b\s*/\n.Dv $1\n/gs;
	s/\s*=!([a-z][0-9a-z_]+)\b\s*/\n.Xr $1 3\n/gs;
	while (s/\s*=([a-z][0-9a-z_]+)\b\s*/\n.Xr $1 3\n/s) {
	    ++$xref{3}->{$1};
	}
	s/\s*\"(?=\w)/\n.Do\n/gs;
	s/\"(?!\w)\s*/\n.Dc\n/gs;
	s/\s*=([A-Z][0-9A-Z_]+)\b\s*(?![\.,:;])/\n.Dv $1\n/gs;
	s/\s*=([A-Z][0-9A-Z_]+)\b([\.,:;]+)\s*/\n.Dv $1 $2\n/gs;
	s/\s*{([A-Z][a-z] .*?)}\s*/\n.$1\n/gs;
	$man .= "$_\n";
    }
    if (defined($man)) {
	if ($inlist || $intaglist) {
	    $man .= ".El\n";
	    $inlist = $intaglist = 0;
	}
	if ($inliteral) {
	    $man .= ".Ed\n";
	    $inliteral = 0;
	}
	$man =~ s/\%/\\&\%/gs;
	$man =~ s/(\n\.[A-Z][a-z] [\w ]+)\n([.,:;-])\s+/$1 $2\n/gs;
	$man =~ s/\s*$/\n/gm;
	$man =~ s/\n+/\n/gs;
	$man =~ s/\0//gs;
	$man =~ s/\n\n\./\n\./gs;
	chomp($man);
    } else {
	$man = "No description available.";
    }

    $FUNCTIONS{$func} = {
	'source'	=> $fn,
	'version'	=> $version,
	'name'		=> $func,
	'descr'		=> $descr,
	'type'		=> $type,
	'args'		=> $args,
	'man'		=> $man,
	'xref'		=> \%xref,
	'errors'	=> \%errors,
	'author'	=> $author,
	'customrv'	=> $customrv,
	'deprecated'	=> $deprecated,
	'experimental'	=> $experimental,
    };
    if ($source =~ m/^ \* NODOC\s*$/m) {
	$FUNCTIONS{$func}->{nodoc} = 1;
    }
    if ($source !~ m/^ \* XSSO \d/m) {
	$FUNCTIONS{$func}->{openpam} = 1;
    }
    expand_errors($FUNCTIONS{$func});
    return $FUNCTIONS{$func};
}

sub expand_errors($);
sub expand_errors($) {
    my $func = shift;		# Ref to function hash

    my %errors;
    my $ref;
    my $fn;

    if (defined($$func{recursed})) {
	warn("$$func{name}(): loop in error spec\n");
	return qw();
    }
    $$func{recursed} = 1;

    foreach (keys %{$$func{errors}}) {
	if (m/^(PAM_[A-Z_]+)$/) {
	    if (!defined($PAMERR{$1})) {
		warn("$$func{name}(): unrecognized error: $1\n");
		next;
	    }
	    $errors{$1} = $$func{errors}->{$_};
	} elsif (m/^!(PAM_[A-Z_]+)$/) {
	    # treat negations separately
	} elsif (m/^=([a-z_]+)$/) {
	    $ref = $1;
	    if (!defined($FUNCTIONS{$ref})) {
		$fn = $$func{source};
		$fn =~ s/$$func{name}/$ref/;
		parse_source($fn);
	    }
	    if (!defined($FUNCTIONS{$ref})) {
		warn("$$func{name}(): reference to unknown $ref()\n");
		next;
	    }
	    foreach (keys %{$FUNCTIONS{$ref}->{errors}}) {
		$errors{$_} //= $FUNCTIONS{$ref}->{errors}->{$_};
	    }
	} else {
	    warn("$$func{name}(): invalid error specification: $_\n");
	}
    }
    foreach (keys %{$$func{errors}}) {
	if (m/^!(PAM_[A-Z_]+)$/) {
	    delete($errors{$1});
	}
    }
    delete($$func{recursed});
    $$func{errors} = \%errors;
}

sub dictionary_order($$) {
    my ($a, $b) = @_;

    $a =~ s/[^[:alpha:]]//g;
    $b =~ s/[^[:alpha:]]//g;
    $a cmp $b;
}

sub genxref($) {
    my $xref = shift;		# References

    my $mdoc = '';
    my @refs = ();
    foreach my $sect (sort(keys(%{$xref}))) {
	foreach my $page (sort(dictionary_order keys(%{$xref->{$sect}}))) {
	    push(@refs, "$page $sect");
	}
    }
    while ($_ = shift(@refs)) {
	$mdoc .= ".Xr $_" .
	    (@refs ? " ,\n" : "\n");
    }
    return $mdoc;
}

sub gendoc($) {
    my $func = shift;		# Ref to function hash

    local *FILE;
    my %errors;
    my $mdoc;
    my $fn;

    return if defined($$func{nodoc});

    $$func{source} =~ m/([^\/]+)$/;
    $mdoc = ".\\\" Generated from $1 by gendoc.pl\n";
    if ($$func{version}) {
	$mdoc .= ".\\\" $$func{version}\n";
    }
    $mdoc .= ".Dd $TODAY
.Dt " . uc($$func{name}) . " 3
.Os
.Sh NAME
.Nm $$func{name}
.Nd $$func{descr}
";
    if ($func =~ m/^(?:open)?pam_/) {
	$mdoc .= ".Sh LIBRARY
.Lb libpam
";
    }
    $mdoc .= ".Sh SYNOPSIS
.In sys/types.h
";
    if ($$func{args} =~ m/\bFILE \*\b/) {
	$mdoc .= ".In stdio.h\n";
    }
    if ($$func{name} =~ m/^(?:open)?pam/) {
	$mdoc .= ".In security/pam_appl.h
";
    }
    if ($$func{name} =~ m/_sm_/) {
	$mdoc .= ".In security/pam_modules.h\n";
    }
    if ($$func{name} =~ m/openpam/) {
	$mdoc .= ".In security/openpam.h\n";
    }
    $mdoc .= ".Ft \"$$func{type}\"
.Fn $$func{name} $$func{args}
.Sh DESCRIPTION
";
    if (defined($$func{deprecated})) {
	$mdoc .= ".Bf Sy\n" .
	    "This function is deprecated and may be removed " .
	    "in a future release without further warning.\n";
	if ($$func{deprecated}) {
	    $mdoc .= "The\n.Fn $$func{deprecated}\nfunction " .
		"may be used to achieve similar results.\n";
	}
	$mdoc .= ".Ef\n.Pp\n";
    }
    if ($$func{experimental}) {
	$mdoc .= ".Bf Sy\n" .
	    "This function is experimental and may be modified or removed " .
	    "in a future release without prior warning.\n";
	$mdoc .= ".Ef\n.Pp\n";
    }
    $mdoc .= "$$func{man}\n";
    %errors = %{$$func{errors}};
    if ($$func{customrv}) {
	# leave it
    } elsif ($$func{type} eq "int" && %errors) {
	$mdoc .= ".Sh RETURN VALUES
The
.Fn $$func{name}
function returns one of the following values:
.Bl -tag -width 18n
";
	delete($errors{PAM_SUCCESS});
	foreach ('PAM_SUCCESS', sort keys %errors) {
	    $mdoc .= ".It Bq Er $_\n" .
		($errors{$_} || $PAMERR{$_}) .
		".\n";
	}
	$mdoc .= ".El\n";
    } elsif ($$func{type} eq "int") {
	$mdoc .= ".Sh RETURN VALUES
The
.Fn $$func{name}
function returns 0 on success and -1 on failure.
";
    } elsif ($$func{type} =~ m/\*$/) {
	$mdoc .= ".Sh RETURN VALUES
The
.Fn $$func{name}
function returns
.Dv NULL
on failure.
";
    } elsif ($$func{type} ne "void") {
	warn("$$func{name}(): no error specification\n");
    }
    $mdoc .= ".Sh SEE ALSO\n" . genxref($$func{xref});
    $mdoc .= ".Sh STANDARDS\n";
    if ($$func{openpam}) {
	$mdoc .= "The
.Fn $$func{name}
function is an OpenPAM extension.
";
    } else {
	$mdoc .= ".Rs
.%T \"X/Open Single Sign-On Service (XSSO) - Pluggable Authentication Modules\"
.%D \"June 1997\"
.Re
";
    }
    $mdoc .= ".Sh AUTHORS
The
.Fn $$func{name}
function and this manual page were\n";
    $mdoc .= $AUTHORS{$$func{author} // 'THINKSEC_DARPA'} . "\n";
    $fn = "$$func{name}.3";
    if (open(FILE, ">", $fn)) {
	print(FILE $mdoc);
	close(FILE);
    } else {
	warn("$fn: open(): $!\n");
    }
}

sub readproto($) {
    my $fn = shift;		# File name

    local *FILE;
    my %func;

    open(FILE, "<", "$fn")
	or die("$fn: open(): $!\n");
    while (<FILE>) {
	if (m/^\.Nm ((?:(?:open)?pam)_.*?)\s*$/) {
	    $func{Nm} = $func{Nm} || $1;
	} elsif (m/^\.Ft (\S.*?)\s*$/) {
	    $func{Ft} = $func{Ft} || $1;
	} elsif (m/^\.Fn (\S.*?)\s*$/) {
	    $func{Fn} = $func{Fn} || $1;
	}
    }
    close(FILE);
    if ($func{Nm}) {
	$FUNCTIONS{$func{Nm}} = \%func;
    } else {
	warn("No function found\n");
    }
}

sub gensummary($) {
    my $page = shift;		# Which page to produce

    local *FILE;
    my $upage;
    my $func;
    my %xref;

    open(FILE, ">", "$page.3")
	or die("$page.3: $!\n");

    $page =~ m/(\w+)$/;
    $upage = uc($1);
    print FILE ".\\\" Generated by gendoc.pl
.Dd $TODAY
.Dt $upage 3
.Os
.Sh NAME
";
    my @funcs = sort(keys(%FUNCTIONS));
    while ($func = shift(@funcs)) {
	print FILE ".Nm $FUNCTIONS{$func}->{Nm}";
	print FILE " ,"
		if (@funcs);
	print FILE "\n";
    }
    print FILE ".Nd Pluggable Authentication Modules Library
.Sh LIBRARY
.Lb libpam
.Sh SYNOPSIS\n";
    if ($page eq 'pam') {
	print FILE ".In security/pam_appl.h\n";
    } else {
	print FILE ".In security/openpam.h\n";
    }
    foreach $func (sort(keys(%FUNCTIONS))) {
	print FILE ".Ft $FUNCTIONS{$func}->{Ft}\n";
	print FILE ".Fn $FUNCTIONS{$func}->{Fn}\n";
    }
    while (<STDIN>) {
	if (m/^\.Xr (\S+)\s*(\d)\s*$/) {
	    ++$xref{int($2)}->{$1};
	}
	print FILE $_;
    }

    if ($page eq 'pam') {
	print FILE ".Sh RETURN VALUES
The following return codes are defined by
.In security/pam_constants.h :
.Bl -tag -width 18n
";
	foreach (sort(keys(%PAMERR))) {
	    print FILE ".It Bq Er $_\n$PAMERR{$_}.\n";
	}
	print FILE ".El\n";
    }
    print FILE ".Sh SEE ALSO
";
    if ($page eq 'pam') {
	++$xref{3}->{openpam};
    }
    foreach $func (keys(%FUNCTIONS)) {
	++$xref{3}->{$func};
    }
    print FILE genxref(\%xref);
    print FILE ".Sh STANDARDS
.Rs
.%T \"X/Open Single Sign-On Service (XSSO) - Pluggable Authentication Modules\"
.%D \"June 1997\"
.Re
";
    print FILE ".Sh AUTHORS
The OpenPAM library and this manual page were $AUTHORS{THINKSEC}
";
    close(FILE);
}

sub usage() {

    print(STDERR "usage: gendoc [-op] source [...]\n");
    exit(1);
}

MAIN:{
    my %opts;

    usage()
	unless (@ARGV && getopts("op", \%opts));
    $TODAY = strftime("%B %e, %Y", localtime(time()));
    $TODAY =~ s,\s+, ,g;
    if ($opts{o} || $opts{p}) {
	foreach my $fn (@ARGV) {
	    readproto($fn);
	}
	gensummary('openpam')
	    if ($opts{o});
	gensummary('pam')
	    if ($opts{p});
    } else {
	foreach my $fn (@ARGV) {
	    my $func = parse_source($fn);
	    gendoc($func)
		if (defined($func));
	}
    }
    exit(0);
}
