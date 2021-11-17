#! /usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0


# convert an Intel HEX file into a set of C records usable by the firmware
# loading code in usb-serial.c (or others)

# accepts the .hex file(s) on stdin, a basename (to name the initialized
# array) as an argument, and prints the .h file to stdout. Typical usage:
#  perl ezusb_convert.pl foo <foo.hex >fw_foo.h


my $basename = $ARGV[0];
die "no base name specified" unless $basename;

while (<STDIN>) {
    # ':' <len> <addr> <type> <len-data> <crc> '\r'
    #  len, type, crc are 2-char hex, addr is 4-char hex. type is 00 for
    # normal records, 01 for EOF
    my($lenstring, $addrstring, $typestring, $reststring, $doscrap) =
      /^:(\w\w)(\w\w\w\w)(\w\w)(\w+)(\r?)$/;
    die "malformed line: $_" unless $reststring;
    last if $typestring eq '01';
    my($len) = hex($lenstring);
    my($addr) = hex($addrstring);
    my(@bytes) = unpack("C*", pack("H".(2*$len), $reststring));
    #pop(@bytes); # last byte is a CRC
    push(@records, [$addr, \@bytes]);
}

@sorted_records = sort { $a->[0] <=> $b->[0] } @records;

print <<"EOF";
/*
 * ${basename}_fw.h
 *
 * Generated from ${basename}.s by ezusb_convert.pl
 * This file is presumed to be under the same copyright as the source file
 * from which it was derived.
 */

EOF

print "static const struct ezusb_hex_record ${basename}_firmware[] = {\n";
foreach $r (@sorted_records) {
    printf("{ 0x%04x,\t%d,\t{", $r->[0], scalar(@{$r->[1]}));
    print join(", ", map {sprintf('0x%02x', $_);} @{$r->[1]});
    print "} },\n";
}
print "{ 0xffff,\t0,\t{0x00} }\n";
print "};\n";
