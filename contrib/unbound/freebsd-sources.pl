#!/usr/bin/perl -w
#-
# Copyright (c) 2013 Dag-Erling Smørgrav
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
# $FreeBSD$
#

use strict;
use warnings;
use Text::Wrap;

our @targets = qw(LIBUNBOUND DAEMON UBANCHOR CHECKCONF CONTROL);

our %target_names = (
    LIBUNBOUND => "libunbound",
    DAEMON => "unbound",
    UBANCHOR => "unbound-anchor",
    CHECKCONF => "unbound-checkconf",
    CONTROL => "unbound-control",
);

sub get_sources($) {
    my ($target) = @_;
    local $/;

    open(MAKE, "-|", "make", "-V${target}_OBJ_LINK")
	or die("failed to exec make: $!\n");
    my $objs = <MAKE>;
    close(MAKE);
    chomp($objs);
    $objs =~ s/\.l?o\b/.c/g;
    return map {
	/lexer/ && s/c$/l/;
	/parser/ && s/c$/y/;
	$_;
    } split(/\s+/, $objs);
}

MAIN:{
    my %sources;
    foreach my $target (@targets) {
	$sources{$target} = {
	    map({ $_ => 1 }
		grep({ !exists($sources{LIBUNBOUND}->{$_}) }
		     get_sources($target)))
	};
	print("# $target_names{$target}\n");
	my $SRCS = fill("SRCS=\t", "\t", sort keys %{$sources{$target}});
	$SRCS =~ s/\n/ \\\n/gm;
	print("$SRCS\n");
    }
}

1;
