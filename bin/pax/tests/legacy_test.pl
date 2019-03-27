# $FreeBSD$

use strict;
use warnings;

use Test::More tests => 6;
use File::Path qw(rmtree mkpath);
use Cwd;

my $n = 0;
sub create_file {
    my $fn = shift;

    $n++;
    (my $dir = $fn) =~ s,/[^/]+$,,;
    mkpath $dir;
    open my $fd, ">", $fn or die "$fn: $!";
    print $fd "file $n\n";
}


ustar_pathnames: { SKIP: {
    # Prove that pax breaks up ustar pathnames properly

    my $top = getcwd . "/ustar-pathnames-1";
    skip "Current path is too long", 6 if length $top > 92;
    rmtree $top;
    my $subdir = "x" . "x" x (92 - length $top);
    my $work94 = "$top/$subdir";
    mkpath $work94;		# $work is 94 characters long

    my $x49 = "x" x 49;
    my $x50 = "x" x 50;
    my $x60 = "x" x 60;
    my $x95 = "x" x 95;

    my @paths = (
	"$work94/x099",		# 99 chars
	"$work94/xx100",		# 100 chars
	"$work94/xxx101",		# 101 chars
	"$work94/$x49/${x50}x199",	# 199 chars
	"$work94/$x49/${x50}xx200",	# 200 chars
	"$work94/$x49/${x50}xxx201",	# 201 chars
	"$work94/$x60/${x95}254",	# 254 chars
	"$work94/$x60/${x95}x255",	# 255 chars
    );

    my @l = map { length } @paths;

    my $n = 0;
    create_file $_ for @paths;
    system "pax -wf ustar.ok $work94";
    ok($? == 0, "Wrote 'ustar.ok' containing files with lengths @l");

    (my $orig = $top) =~ s,1$,2,;
    rmtree $orig;
    rename $top, $orig;

    system "pax -rf ustar.ok";
    ok($? == 0, "Restored 'ustar.ok' containing files with lengths @l");

    system "diff -ru $orig $top";
    ok($? == 0, "Restored files are identical");

    rmtree $top;
    rename $orig, $top;

    # 256 chars (with components < 100 chars) should not work
    push @paths, "$work94/x$x60/${x95}x256";	# 256 chars
    push @l, length $paths[-1];
    create_file $paths[-1];
    system "pax -wf ustar.fail1 $work94";
    ok($?, "Failed to write 'ustar.fail1' containing files with lengths @l");

    # Components with 100 chars shouldn't work
    unlink $paths[-1];
    $paths[-1] = "$work94/${x95}xc100";		# 100 char filename
    $l[-1] = length $paths[-1];
    create_file $paths[-1];
    system "pax -wf ustar.fail2 $work94";
    ok($?, "Failed to write 'ustar.fail2' with a 100 char filename");

    unlink $paths[-1];
    $paths[-1] = "$work94/${x95}xc100/x";	# 100 char component
    $l[-1] = length $paths[-1];
    create_file $paths[-1];
    system "pax -wf ustar.fail3 $work94";
    ok($?, "Failed to write 'ustar.fail3' with a 100 char component");
}}
