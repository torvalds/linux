#!/usr/bin/perl -w
#
# winucase_convert.pl -- convert "Windows 8 Upper Case Mapping Table.txt" to
#                        a two-level set of C arrays.
#
#   Copyright 2013: Jeff Layton <jlayton@redhat.com>
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

while(<>) {
	next if (!/^0x(..)(..)\t0x(....)\t/);
	$firstchar = hex($1);
	$secondchar = hex($2);
	$uppercase = hex($3);

	$top[$firstchar][$secondchar] = $uppercase;
}

for ($i = 0; $i < 256; $i++) {
	next if (!$top[$i]);

	printf("static const wchar_t t2_%2.2x[256] = {", $i);
	for ($j = 0; $j < 256; $j++) {
		if (($j % 8) == 0) {
			print "\n\t";
		} else {
			print " ";
		}
		printf("0x%4.4x,", $top[$i][$j] ? $top[$i][$j] : 0);
	}
	print "\n};\n\n";
}

printf("static const wchar_t *const toplevel[256] = {", $i);
for ($i = 0; $i < 256; $i++) {
	if (($i % 8) == 0) {
		print "\n\t";
	} elsif ($top[$i]) {
		print " ";
	} else {
		print "  ";
	}

	if ($top[$i]) {
		printf("t2_%2.2x,", $i);
	} else {
		print "NULL,";
	}
}
print "\n};\n\n";
