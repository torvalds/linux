#!/usr/bin/perl -w
# vi: set sw=4 ts=4:
# Copyright (c) 2001 David Schleef <ds@schleef.org>
# Copyright (c) 2001 Erik Andersen <andersen@codepoet.org>
# Copyright (c) 2001 Stuart Hughes <seh@zee2.com>
# Copyright (c) 2002 Steven J. Hill <shill@broadcom.com>
# Copyright (c) 2006 Freescale Semiconductor, Inc <stuarth@freescale.com>
#
# History:
# March 2006: Stuart Hughes <stuarth@freescale.com>.
#             Significant updates, including implementing the '-F' option
#             and adding support for 2.6 kernels.

# This program is free software; you can redistribute it and/or modify it
# under the same terms as Perl itself.
use Getopt::Long qw(:config no_auto_abbrev no_ignore_case);
use File::Find;
use strict;

# Set up some default values
my $kdir="";
my $basedir="";
my $kernel="";
my $kernelsyms="";
my $symprefix="";
my $all=0;
my $quick=0;
my $errsyms=0;
my $stdout=0;
my $verbose=0;
my $help=0;
my $nm = $ENV{'NM'} || "nm";

# more globals
my (@liblist) = ();
my $exp = {};
my $dep = {};
my $mod = {};

my $usage = <<TXT;
$0 -b basedir { -k <vmlinux> | -F <System.map> } [options]...
  Where:
   -h --help          : Show this help screen
   -b --basedir       : Modules base directory (e.g /lib/modules/<2.x.y>)
   -k --kernel        : Kernel binary for the target (e.g. vmlinux)
   -F --kernelsyms    : Kernel symbol file (e.g. System.map)
   -n --stdout        : Write to stdout instead of <basedir>/modules.dep
   -v --verbose       : Print out lots of debugging stuff
   -P --symbol-prefix : Symbol prefix
   -a --all           : Probe all modules (default/only thing supported)
   -e --errsyms       : Report any symbols not supplied by modules/kernel
TXT

# get command-line options
GetOptions(
	"help|h"            => \$help,
	"basedir|b=s"       => \$basedir,
	"kernel|k=s"        => \$kernel,
	"kernelsyms|F=s"    => \$kernelsyms,
	"stdout|n"          => \$stdout,
	"verbose|v"         => \$verbose,
	"symbol-prefix|P=s" => \$symprefix,
	"all|a"             => \$all,
	# unsupported options
	"quick|A"           => \$quick,
	# ignored options (for historical usage)
	"quiet|q",
	"root|r",
	"unresolved-error|u"
);

die $usage if $help;
die $usage unless $basedir && ( $kernel || $kernelsyms );
die "can't use both -k and -F\n\n$usage" if $kernel && $kernelsyms;
die "sorry, -A/--quick is not supported" if $quick;
die "--errsyms requires --kernelsyms" if $errsyms && !$kernelsyms;

# Strip any trailing or multiple slashes from basedir
$basedir =~ s-/+$--g;

# The base directory should contain /lib/modules somewhere
if($basedir !~ m-/lib/modules-) {
    warn "WARNING: base directory does not match ..../lib/modules\n";
}

# if no kernel version is contained in the basedir, try to find one
if($basedir !~ m-/lib/modules/\d\.\d-) {
    opendir(BD, $basedir) or die "can't open basedir $basedir : $!\n";
    foreach ( readdir(BD) ) {
        next if /^\.\.?$/;
        next unless -d "$basedir/$_";
        warn "dir = $_\n" if $verbose;
        if( /^\d\.\d/ ) {
            $kdir = $_;
            warn("Guessed module directory as $basedir/$kdir\n");
            last;
        }
    }
    closedir(BD);
    die "Cannot find a kernel version under $basedir\n" unless $kdir;
    $basedir = "$basedir/$kdir";
}

# Find the list of .o or .ko files living under $basedir
warn "**** Locating all modules\n" if $verbose;
find sub {
    my $file;
	if ( -f $_  && ! -d $_ ) {
		$file = $File::Find::name;
		if ( $file =~ /\.k?o$/ ) {
			push(@liblist, $file);
			warn "$file\n" if $verbose;
		}
	}
}, $basedir;
warn "**** Finished locating modules\n" if $verbose;

foreach my $obj ( @liblist ){
    # turn the input file name into a target tag name
    my ($tgtname) = $obj =~ m-(/lib/modules/.*)$-;

    warn "\nMODULE = $tgtname\n" if $verbose;

    # get a list of symbols
	my @output=`$nm $obj`;

    build_ref_tables($tgtname, \@output, $exp, $dep);
}


# vmlinux is a special name that is only used to resolve symbols
my $tgtname = 'vmlinux';
my @output = $kernelsyms ? `cat $kernelsyms` : `$nm $kernel`;
warn "\nMODULE = $tgtname\n" if $verbose;
build_ref_tables($tgtname, \@output, $exp, $dep);

# resolve the dependencies for each module
# reduce dependencies: remove unresolvable and resolved from vmlinux/System.map
# remove duplicates
foreach my $module (keys %$dep) {
    warn "reducing module: $module\n" if $verbose;
    $mod->{$module} = {};
    foreach (@{$dep->{$module}}) {
        if( $exp->{$_} ) {
            warn "resolved symbol $_ in file $exp->{$_}\n" if $verbose;
            next if $exp->{$_} =~ /vmlinux/;
            $mod->{$module}{$exp->{$_}} = 1;
        } else {
            warn "unresolved symbol $_ in file $module\n";
        }
    }
}

# build a complete dependency list for each module and make sure it
# is kept in order proper order
my $mod2 = {};
sub maybe_unshift
{
	my ($array, $ele) = @_;
	# chop off the leading path /lib/modules/<kver>/ as modprobe
	# will handle relative paths just fine
	$ele =~ s:^/lib/modules/[^/]*/::;
	foreach (@{$array}) {
		if ($_ eq $ele) {
			return;
		}
	}
	unshift (@{$array}, $ele);
}
sub add_mod_deps
{
	my ($depth, $mod, $mod2, $module, $this_module) = @_;

	$depth .= " ";
	warn "${depth}loading deps of module: $this_module\n" if $verbose;
	if (length($depth) > 50) {
		die "too much recursion (circular dependencies in modules?)";
	}

	foreach my $md (keys %{$mod->{$this_module}}) {
		add_mod_deps ($depth, $mod, $mod2, $module, $md);
		warn "${depth} outputting $md\n" if $verbose;
		maybe_unshift (\@{$$mod2->{$module}}, $md);
	}

	if (!%{$mod->{$this_module}}) {
		warn "${depth} no deps\n" if $verbose;
	}
}
foreach my $module (keys %$mod) {
	warn "filling out module: $module\n" if $verbose;
	@{$mod2->{$module}} = ();
	add_mod_deps ("", $mod, \$mod2, $module, $module);
}

# figure out where the output should go
if ($stdout == 0) {
	warn "writing $basedir/modules.dep\n" if $verbose;
    open(STDOUT, ">$basedir/modules.dep")
                             or die "cannot open $basedir/modules.dep: $!";
}
my $kseries = $basedir =~ m,/2\.4\.[^/]*, ? '2.4' : 'others';

foreach my $module ( keys %$mod ) {
    if($kseries eq '2.4') {
	    print "$module:\t";
	    my @sorted = sort bydep keys %{$mod->{$module}};
	    print join(" \\\n\t",@sorted);
	    print "\n\n";
    } else {
	    my $shortmod = $module;
	    $shortmod =~ s:^/lib/modules/[^/]*/::;
	    print "$shortmod:";
	    my @sorted = @{$mod2->{$module}};
	    printf " " if @sorted;
	    print join(" ",@sorted);
	    print "\n";
    }
}


sub build_ref_tables
{
    my ($name, $sym_ar, $exp, $dep) = @_;

	my $ksymtab = grep m/ ${symprefix}__ksymtab/, @$sym_ar;

    # gather the exported symbols
	if($ksymtab){
        # explicitly exported
        foreach ( @$sym_ar ) {
            / ${symprefix}__ksymtab_(.*)$/ and do {
                my $sym = ${symprefix} . $1;
                warn "sym = $sym\n" if $verbose;
                $exp->{$sym} = $name;
            };
        }
	} else {
        # exporting all symbols
        foreach ( @$sym_ar ) {
            / [ABCDGRSTW] (.*)$/ and do {
                warn "syma = $1\n" if $verbose;
                $exp->{$1} = $name;
            };
        }
	}

    # this takes makes sure modules with no dependencies get listed
    push @{$dep->{$name}}, $symprefix . 'printk' unless $name eq 'vmlinux';

    # gather the unresolved symbols
    foreach ( @$sym_ar ) {
        !/ ${symprefix}__this_module/ && / U (.*)$/ and do {
            warn "und = $1\n" if $verbose;
            push @{$dep->{$name}}, $1;
        };
    }
}

sub bydep
{
    foreach my $f ( keys %{$mod->{$b}} ) {
        if($f eq $a) {
            return 1;
        }
    }
    return -1;
}



__END__

=head1 NAME

depmod.pl - a cross platform script to generate kernel module
dependency lists (modules.conf) which can then be used by modprobe
on the target platform.

It supports Linux 2.4 and 2.6 styles of modules.conf (auto-detected)

=head1 SYNOPSIS

depmod.pl [OPTION]... [basedir]...

Example:

	depmod.pl -F linux/System.map -b target/lib/modules/2.6.11

=head1 DESCRIPTION

The purpose of this script is to automagically generate a list of of kernel
module dependencies.  This script produces dependency lists that should be
identical to the depmod program from the modutils package.  Unlike the depmod
binary, however, depmod.pl is designed to be run on your host system, not
on your target system.

This script was written by David Schleef <ds@schleef.org> to be used in
conjunction with the BusyBox modprobe applet.

=head1 OPTIONS

=over 4

=item B<-h --help>

This displays the help message.

=item B<-b --basedir>

The base directory uner which the target's modules will be found.  This
defaults to the /lib/modules directory.

If you don't specify the kernel version, this script will search for
one under the specified based directory and use the first thing that
looks like a kernel version.

=item B<-k --kernel>

Kernel binary for the target (vmlinux).  You must either supply a kernel binary
or a kernel symbol file (using the -F option).

=item B<-F --kernelsyms>

Kernel symbol file for the target (System.map).

=item B<-n --stdout>

Write to stdout instead of modules.dep
kernel binary for the target (using the -k option).

=item B<--verbose>

Verbose (debug) output

=back

=head1 COPYRIGHT AND LICENSE

 Copyright (c) 2001 David Schleef <ds@schleef.org>
 Copyright (c) 2001 Erik Andersen <andersen@codepoet.org>
 Copyright (c) 2001 Stuart Hughes <seh@zee2.com>
 Copyright (c) 2002 Steven J. Hill <shill@broadcom.com>
 Copyright (c) 2006 Freescale Semiconductor, Inc <stuarth@freescale.com>

This program is free software; you can redistribute it and/or modify it
under the same terms as Perl itself.

=head1 AUTHOR

David Schleef <ds@schleef.org>

=cut
