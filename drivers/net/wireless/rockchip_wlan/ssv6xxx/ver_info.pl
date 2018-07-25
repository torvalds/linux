#!/usr/bin/perl
use strict;

my @tmp_string;
my $svn_status;
my $svn_filename;
my $svn_version;

my $svn_root = ".";
my $svn_root_version;
my $svn_root_url;

my @header_defines = (
    "#ifndef _SSV_VERSION_H_",
    "#define _SSV_VERSION_H_",
    "",
);

sub get_version {
    foreach (@_) {
        if($_ =~ m/^Last Changed Rev: (\d*)/) {
            return $1;
        }
    } 
    # file doesn't exist on svn
    return -1;
}

sub get_url {
    foreach (@_) {
        if($_ =~ m/^URL: (.*)/) {
            return $1;
        }
    } 
    # file doesn't exist on svn
    return -1;
}

printf("## script to generate version infomation header ##\n");

# step-0: get root svn number 
$svn_root_version = get_version(qx(svn info $svn_root));

if ($svn_root_version == -1) {
    exit 0;
}

# step-1: get root svn url 
$svn_root_url = get_url(qx(svn info $svn_root));

OUTPUT_HEADER:
# step-3: output header files
if (defined($ARGV[0])) {
    open HEADER, ">", $ARGV[0];
    select HEADER;
}
else {
    print "Error! Please specify header file\n";
}


foreach (@header_defines) {
    printf("%s\n", $_);
}

##
printf("static u32 ssv_root_version = %d;\n\n", $svn_root_version);
printf("#define SSV_ROOT_URl \"$svn_root_url\"\n");

use Sys::Hostname;
my $host = hostname();
printf("#define COMPILERHOST \"$host\"\n");

use POSIX qw(strftime);
my $date = strftime "%m-%d-%Y-%H:%M:%S", localtime;
printf("#define COMPILERDATE \"$date\"\n");
##

use Config;
printf("#define COMPILEROS \"$Config{osname}\"\n");
printf("#define COMPILEROSARCH \"$Config{archname}\"\n");

printf("\n#endif\n\n");

exit 1;
