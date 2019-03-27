#!/usr/bin/perl -w
use strict;
use ExtUtils::MakeMaker qw(prompt);
use File::Find;

my $just_check = @ARGV ? $ARGV[0] eq '-c' : 0;
shift if $just_check;
my $dir = shift || '.';
my %names;

my $prefix = 'apr_';

while (<DATA>) {
    chomp;
    my($old, $new) = grep { s/^$prefix//o } split;
    next unless $old and $new;
    $names{$old} = $new;
}

my $pattern = join '|', keys %names;
#print "replacement pattern=$pattern\n";

find sub {
    chomp;
    return unless /\.[ch]$/;
    my $file = "$File::Find::dir/$_";
    print "looking in $file\n";

    replace($_, !$just_check);

}, $dir;

sub replace {
    my($file, $replace) = @_;
    local *IN, *OUT;
    my @lines;
    my $found = 0;

    open IN, $file or die "open $file: $!";

    while (<IN>) {
        for (m/[^_\"]*$prefix($pattern)\b/og) {
            $found++;
            print "   $file:$. apr_$_ -> apr_$names{$_}\n";
        }
        push @lines, $_ if $replace;
    }

    close IN;

    return unless $found and $replace;

#    my $ans = prompt("replace?", 'y');
#    return unless $ans =~ /^y/i;

    open OUT, ">$file" or die "open $file: $!";

    for (@lines) {
        unless (/^\#include/) {
            s/([^_\"]*$prefix)($pattern)\b/$1$names{$2}/og;
        }
        print OUT $_;
    }

    close OUT;
}

__DATA__
apr_time_t:
apr_implode_gmt              apr_time_exp_gmt_get

apr_socket_t:
apr_close_socket             apr_socket_close
apr_create_socket            apr_socket_create
apr_get_sockaddr             apr_socket_addr_get
apr_get_socketdata           apr_socket_data_get
apr_set_socketdata           apr_socket_data_set
apr_shutdown                 apr_socket_shutdown
apr_bind                     apr_socket_bind
apr_listen                   apr_socket_listen
apr_accept                   apr_socket_accept
apr_connect                  apr_socket_connect
apr_send                     apr_socket_send
apr_sendv                    apr_socket_sendv
apr_sendto                   apr_socket_sendto
apr_recvfrom                 apr_socket_recvfrom
apr_sendfile                 apr_socket_sendfile
apr_recv                     apr_socket_recv

apr_filepath_*:
apr_filename_of_pathname     apr_filepath_name_get

apr_gid_t:
apr_get_groupid              apr_gid_get
apr_get_groupname            apr_gid_name_get
apr_group_name_get           apr_gid_name_get
apr_compare_groups           apr_gid_compare

apr_uid_t:
apr_get_home_directory       apr_uid_homepath_get
apr_get_userid               apr_uid_get
apr_current_userid           apr_uid_current
apr_compare_users            apr_uid_compare
apr_get_username             apr_uid_name_get
apr_compare_users            apr_uid_compare

