# $NetBSD: t_sort.sh,v 1.1 2012/03/17 16:33:15 jruoho Exp $
#
# Copyright (c) 2008, 2009, 2010 The NetBSD Foundation, Inc.
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
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

atf_test_case basic
basic_head()
{
	atf_set "descr" "Basic functionality test"
}
basic_body()
{
	cat >in <<EOF
z b m f
y c o e
x a n h
x a n g
EOF

	cat >expout <<EOF
x a n g
x a n h
y c o e
z b m f
EOF

	atf_check -o file:expout sort in
}

atf_test_case empty_file
empty_file_head()
{
	atf_set "descr" "Tests sorting an empty file"
}
empty_file_body()
{
	touch empty
	atf_check -o empty sort -S empty
	atf_check sort -S -c empty
	atf_check sort -S -c -u empty
}

atf_test_case end_of_options
end_of_options_head()
{
	atf_set "descr" "Determination of end of option list"
}
end_of_options_body()
{
	echo x >-k
	atf_check -o file:-k -x "sort -S -- -k </dev/null"
	atf_check -s not-exit:1 -e ignore -x "sort -S - -c </dev/null"
}

atf_test_case missing_newline
missing_newline_head()
{
	atf_set "descr" "Tests with missing new line in input file"
}
missing_newline_body()
{
	printf '%s' x >in
	atf_check -o inline:'x\n' sort in
}

atf_test_case null_bytes
null_bytes_head()
{
	atf_set "descr" "Tests the behavior of null bytes"
}
null_bytes_body()
{
	printf '\0b\n\0a\n' >in
	atf_check -o inline:'\0a\n\0b\n' sort -S in
}

atf_test_case long_records
long_records_head()
{
	atf_set "descr" "Tests long lines and keys"
}
long_records_body()
{
	awk 'BEGIN {	x="x"
	for(i=1; i<=12; i++) x = x x
	for(i=15; i<=25; i++) print x i
}' >in

	awk 'BEGIN {	x="x"
	for(i=1; i<=12; i++) x = x x
	for(i=25; i>=15; i--) print x i
}' >out

	atf_check -o file:out sort -r in
	atf_check -o file:out sort -k 1,1r -k 1 in
}

atf_test_case long_file
long_file_head()
{
	atf_set "descr" "Tests with a long file to try to force intermediate" \
	    "files"
}
long_file_body()
{
	awk 'BEGIN { for(i=0; i<20000; i++) print rand() }' >in
	sort -S -r in | awk '$0 "x" != x { print ; x = $0 "x" }' >out
	atf_check -o file:out sort -u -r in
}

atf_test_case any_char
any_char_head()
{
	atf_set "descr" "Tests with files containing non-printable/extended" \
	    "characters"
}
any_char_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_any_char_dflag_out.txt \
	    sort -d -k 2 $(atf_get_srcdir)/d_any_char_in.txt

	atf_check -o file:$(atf_get_srcdir)/d_any_char_fflag_out.txt \
	    sort -f -k 2 $(atf_get_srcdir)/d_any_char_in.txt

	atf_check -o file:$(atf_get_srcdir)/d_any_char_iflag_out.txt \
	    sort -i -k 2 $(atf_get_srcdir)/d_any_char_in.txt
}

atf_test_case bflag
bflag_head()
{
	atf_set "descr" "Tests the -b flag"
}
bflag_body()
{
	cat >in <<EOF
  b
 a
EOF

	atf_check -o file:in sort -b in
	atf_check -o file:in -x "sort -b <in"
	atf_check -s exit:1 -o ignore -e ignore -x "sort in | sort -c -r"
}

atf_test_case cflag
cflag_head()
{
	atf_set "descr" "Tests the -c flag"
}
cflag_body()
{
	cat >in <<EOF
b
a
EOF

	atf_check -s exit:1 -e ignore sort -S -c in
}

atf_test_case kflag_one_field
kflag_one_field_head()
{
	atf_set "descr" "Tests the -k flag with one field"
}
kflag_one_field_body()
{
	cat >in <<EOF
z b m f
y c o e
x a n h
x a n g
EOF

	cat >expout <<EOF
x a n g
x a n h
z b m f
y c o e
EOF

	atf_check -o file:expout sort -k2.1 in
}

atf_test_case kflag_two_fields
kflag_two_fields_head()
{
	atf_set "descr" "Tests the -k flag with two fields"
}
kflag_two_fields_body()
{
	cat >in <<EOF
z b m f
y c o e
x a n h
x a n g
EOF

	cat >expout <<EOF
x a n h
x a n g
z b m f
y c o e
EOF
	atf_check -o file:expout sort -k2.1,2.0 in
}

atf_test_case kflag_many_fields
kflag_many_fields_head()
{
	atf_set "descr" "Tests the -k flag with many fields"
}
kflag_many_fields_body()
{
	cat >in <<EOF
0:2:3:4:5:6:7:8:9
1:1:3:4:5:6:7:8:9
1:2:2:4:5:6:7:8:9
1:2:3:3:5:6:7:8:9
1:2:3:4:4:6:7:8:9
1:2:3:4:5:5:7:8:9
1:2:3:4:5:6:6:8:9
1:2:3:4:5:6:7:7:9
1:2:3:4:5:6:7:8:8
EOF

	cat >out <<EOF
1:2:3:4:5:6:7:8:8
1:2:3:4:5:6:7:7:9
1:2:3:4:5:6:6:8:9
1:2:3:4:5:5:7:8:9
1:2:3:4:4:6:7:8:9
1:2:3:3:5:6:7:8:9
1:2:2:4:5:6:7:8:9
1:1:3:4:5:6:7:8:9
0:2:3:4:5:6:7:8:9
EOF

	atf_check -o file:out sort -t: -k9 -k8 -k7 -k6 -k5 -k4 -k3 \
	    -k2 -k1 in
}

atf_test_case kflag_outofbounds
kflag_outofbounds_head()
{
	atf_set "descr" "Tests the -k flag with out of bounds fields"
}
kflag_outofbounds_body()
{
	cat >in <<EOF
0 5
1 4
2 3
3 2
4 1
5 0
EOF

	atf_check -o file:in sort -k2.2,2.1 -k2.3,2.4 in
}

atf_test_case kflag_nonmonotone
kflag_nonmonotone_head()
{
	atf_set "descr" "Tests the -k flag with apparently nonmonotone" \
	    "field specs"
}
kflag_nonmonotone_body()
{
	cat >in <<EOF
aaaa c
x a
0 b
EOF

	atf_check -o file:in sort -k2,1.3 -k2.5,2.5 in
}

atf_test_case kflag_limits
kflag_limits_head()
{
	atf_set "descr" "Tests the -k flag field limits"
}
kflag_limits_body()
{
	cat >in <<EOF
a	2
a	1
b	2
b	1
EOF

	cat >out <<EOF
b	2
b	1
a	2
a	1
EOF

	atf_check -o file:out sort -r -k1,1 -k2n in
}

atf_test_case kflag_alpha
kflag_alpha_head()
{
	atf_set "descr" "Tests the -k flag with various alpha fields"
}
kflag_alpha_body()
{
	sort >in <<EOF
01:04:19:01:16:01:21:01 a
02:03:13:15:13:19:15:02  a
03:02:07:09:07:13:09:03   a
04:01:01:03:01:07:03:04    a
05:08:20:16:17:02:20:05 aa
06:07:14:18:14:20:14:06  aa
07:06:08:10:08:14:08:07   aa
08:05:02:04:02:08:02:08    aa
09:16:22:02:22:04:24:13 b
10:15:16:20:19:22:18:14  b
11:14:10:12:10:16:12:15   b
12:13:04:06:04:10:06:16    b
13:24:24:22:24:06:22:21 bb
14:23:18:24:21:24:16:22  bb
15:22:12:14:12:18:10:23   bb
16:21:06:08:06:12:04:24    bb
17:12:21:21:18:03:19:09 ab
18:11:15:19:15:21:13:10  ab
19:10:09:11:09:15:07:11   ab
20:09:03:05:03:09:01:12    ab
21:20:23:17:23:05:23:17 ba
22:19:17:23:20:23:17:18  ba
23:18:11:13:11:17:11:19   ba
24:17:05:07:05:11:05:20    ba
EOF

	atf_check -x "sort -S -k2b -k2 in >xx"
	atf_check -e ignore sort -c -t: -k2n xx

	atf_check -x "sort -S -k2,2.1b -k2 in >xx"
	atf_check -e ignore sort -c -t: -k3n xx

	atf_check -x "sort -S -k2.3 -k2 in >xx"
	atf_check -e ignore sort -c -t: -k4n xx

	atf_check -x "sort -S -k2b,2.3 -k2 in >xx"
	atf_check -e ignore sort -c -t: -k5n xx

	atf_check -x "sort -S -k2.3,2.1b -k2 in >xx"
	atf_check -e ignore sort -c -t: -k6n xx

	atf_check -x "sort -S -k2,2.1b -k2r in >xx"
	atf_check -e ignore sort -c -t: -k7n xx

	atf_check -x "sort -S -b -k2,2 -k2 in >xx"
	atf_check -e ignore sort -c -t: -k8n xx

	# XXX This test is broken.  The standard is not clear on the behavior.
	#atf_check -x "sort -S -b -k2,2b -k2 in >xx"
	#atf_check -e ignore sort -c -t: -k3n xx
}

atf_test_case kflag_no_end
kflag_no_end_head()
{
	atf_set "descr" "Tests the -k flag with a field without end"
}
kflag_no_end_body()
{
	cat >in <<EOF
a-B
a+b
a b
A+b
a	b
EOF

	cat >out <<EOF
a	b
a b
A+b
a-B
a+b
EOF

	atf_check -o file:out sort -df -k 1 -k 1d <in
}

atf_test_case mflag
mflag_head()
{
	atf_set "descr" "Tests the -m flag"
}
mflag_body()
{
	cat >in1 <<EOF
a
ab
ab
bc
ca
EOF
	cat >in2 <<EOF
Z
a
aa
ac
c
EOF
	cat >out <<EOF
Z
a
a
aa
ab
ab
ac
bc
c
ca
EOF

	atf_check -o file:out sort -S -m in1 in2
}

atf_test_case mflag_uflag
mflag_uflag_head()
{
	atf_set "descr" "Tests the -m flag together with -u"
}
mflag_uflag_body()
{
	cat >in <<EOF
a
b
c
d
EOF

	atf_check -o file:in sort -m -u in
}

atf_test_case mflag_uflag_first
mflag_uflag_first_head()
{
	atf_set "descr" "Tests that the -m flag together with -u picks the" \
	    "first among equal"
}
mflag_uflag_first_body()
{
	cat >in <<EOF
3B
3b
3B2
~3B2
4.1
41
5
5.
EOF

	cat >out <<EOF
3B
3B2
4.1
5
EOF

	atf_check -o file:out sort -mudf in
	atf_check -o file:out sort -mudf -k1 in
}

atf_test_case nflag
nflag_head()
{
	atf_set "descr" "Tests the -n flag"
}
nflag_body()
{
	cat >in <<EOF
-99.0
-99.1
-.0002
-10
2
0010.000000000000000000000000000000000001
10
3x
x
EOF

	cat >expout <<EOF
-99.1
-99.0
-10
-.0002
x
2
3x
10
0010.000000000000000000000000000000000001
EOF

	atf_check -o file:expout sort -n in
}

atf_test_case nflag_rflag
nflag_rflag_head()
{
	atf_set "descr" "Tests the -n and -r flag combination"
}
nflag_rflag_body()
{
	cat >in <<EOF
1
123
2
EOF

	cat >expout <<EOF
123
2
1
EOF

	atf_check -o file:expout sort -rn in
}

atf_test_case oflag
oflag_head()
{
	atf_set "descr" "Tests the -o flag"
}
oflag_body()
{
	cat >in <<EOF
1
1
2
2
3
3
4
4
EOF

	atf_check sort -u -o in in

	cat >expout <<EOF
1
2
3
4
EOF

	atf_check -o file:expout cat in
}

atf_test_case oflag_displaced
oflag_displaced_head()
{
	atf_set "descr" "Tests the -o flag after the file names"
}
oflag_displaced_body()
{
	atf_check sort -S /dev/null -o out
	test -f out || atf_fail "File not created"
}

atf_test_case rflag
rflag_head()
{
	atf_set "descr" "Tests the -r flag"
}
rflag_body()
{
	cat >in <<EOF
z b m f
y c o e
x a n h
x a n g
EOF

	cat >expout <<EOF
z b m f
y c o e
x a n h
x a n g
EOF

	atf_check -o file:expout sort -r in
}

atf_test_case sflag
sflag_head()
{
	atf_set "descr" "Tests the -s flag"
}
sflag_body()
{
	cat >in <<EOF
a 2
b 1
c 2
a 1
b 2
c 1
EOF

	cat >out <<EOF
a 2
a 1
b 1
b 2
c 2
c 1
EOF

	atf_check -o file:out sort -s -k1,1 in
}

atf_test_case sflag_many_files
sflag_many_files_head()
{
	atf_set "descr" "Tests the -s flag with multiple files"
}
sflag_many_files_body()
{
	cat >in1 <<EOF
c 2
a 2
EOF

	cat >in2 <<EOF
c 1
b 1
a 1
EOF

	cat >out <<EOF
c 2
b 1
a 2
EOF

	atf_check -o file:out sort -smru -k1,1 in1 in1 in2 in2
}

atf_test_case tflag
tflag_head()
{
	atf_set "descr" "Tests the -t flag"
}
tflag_body()
{
	cat >in <<EOF
a:
a!
EOF

	atf_check -o file:in sort -t : -r +0 in
	atf_check -o file:in sort -t : +0 -1 in
	atf_check -o file:in sort -t : -r -k 1 in
	atf_check -o file:in sort -t : -k 1,1 in
}

atf_test_case tflag_alphabetic
tflag_alphabetic_head()
{
	atf_set "descr" "Tests the -t flag with a character as the delimiter"
}
tflag_alphabetic_body()
{
	cat >in <<EOF
zXa
yXa
zXb
EOF

	atf_check -o file:in sort -tX -k2 -k1r,1 in
}

atf_test_case tflag_char_pos
tflag_char_pos_head()
{
	atf_set "descr" "Tests the -t flag with character positions in fields"
}
tflag_char_pos_body()
{
	cat >in <<EOF
: ab
:bac
EOF

	cat >out <<EOF
:bac
: ab
EOF

	atf_check -o file:out sort -b -t: +1.1 in
	atf_check -o file:out sort -t: +1.1r in
	atf_check -o file:out sort -b -t: -k 2.2 in
	atf_check -o file:out sort -t: -k 2.2r in
}

atf_test_case tflag_whitespace
tflag_whitespace_head()
{
	atf_set "descr" "Tests the -t flag with spaces and tabs as the" \
	    "delimiter"
}
tflag_whitespace_body()
{
	cat >in <<EOF
 b c
 b	c
	b c
EOF

	atf_check -o file:in sort -t ' ' -k2,2 in
	atf_check -o file:in sort -t ' ' -k2.1,2.0 in

	cat >out <<EOF
 b c
	b c
 b	c
EOF

	atf_check -o file:out sort -t '	' -k2,2 in
	atf_check -o file:out sort -t '	' -k2.1,2.0 in

	cat >out <<EOF
 b	c
	b c
 b c
EOF

	atf_check -o file:out sort -S -k2 in

	cat >out <<EOF
	b c
 b	c
 b c
EOF

	atf_check -o file:out sort -S -k2b in
}

atf_test_case uflag
uflag_head()
{
	atf_set "descr" "Tests the -u flag"
}
uflag_body()
{
	cat >in <<EOF
a
aa
aaa
aa
EOF

	cat >expout <<EOF
a
aa
aaa
EOF

	atf_check -o file:expout sort -u in
}

atf_test_case uflag_rflag
uflag_rflag_head()
{
	atf_set "descr" "Tests the -u and -r flag combination"
}
uflag_rflag_body()
{
	cat >in <<EOF
a
aa
aaa
aa
EOF

	cat >expout <<EOF
aaa
aa
a
EOF

	atf_check -o file:expout sort -ru in
}

atf_test_case plus_one
plus_one_head()
{
	atf_set "descr" "Tests +- addressing: +1 should become -k2.1"
}
plus_one_body()
{
	cat >in <<EOF
z b m f
y c o e
x a n h
x a n g
EOF

	cat >expout <<EOF
x a n g
x a n h
z b m f
y c o e
EOF

	atf_check -o file:expout sort +1 in
}

atf_test_case plus_one_minus_two
plus_one_minus_two_head()
{
	atf_set "descr" "Tests +- addressing: +1 -2 should become -k2.1,2.0"
}
plus_one_minus_two_body()
{
	cat >in <<EOF
z b m f
y c o e
x a n h
x a n g
EOF

	cat >expout <<EOF
x a n h
x a n g
z b m f
y c o e
EOF

	atf_check -o file:expout sort +1 -2 in
}

atf_test_case plus_zero
plus_zero_head()
{
	atf_set "descr" "Tests +- addressing: '-- +0' raised a '-k1.1: No" \
	    "such file or directory' error"
}
plus_zero_body()
{
	echo 'good contents' >./+0

	atf_check -o file:+0 sort -- +0
}

atf_test_case plus_nonmonotone
plus_nonmonotone_head()
{
	atf_set "descr" "Tests += addressing: apparently nonmonotone field" \
	    "specs"
}
plus_nonmonotone_body()
{
	cat >in <<EOF
aaaa c
x a
0 b
EOF

	atf_check -o file:in sort +1 -0.3 +1.4 -1.5 in
}

atf_test_case plus_as_path
plus_as_path_head()
{
	atf_set "descr" "Tests +- addressing: 'file +0' raised a '-k1.1: No" \
	    "such file or directory' error"
}
plus_as_path_body()
{
	echo 'good contents' >./+0
	echo 'more contents' >in
	cat ./+0 in >expout

	atf_check -o file:expout sort in +0
}

atf_test_case plus_bad_tempfile
plus_bad_tempfile_head()
{
	atf_set "descr" "Tests +- addressing: intermediate wrong behavior" \
	    "that raised a '+0: No such file or directory' error"
}
plus_bad_tempfile_body()
{
	echo 'good contents' >in
	atf_check -o file:in sort -T /tmp +0 in
}

atf_test_case plus_rflag_invalid
plus_rflag_invalid_head()
{
	atf_set "descr" "Tests +- addressing: invalid record delimiter"
}
plus_rflag_invalid_body()
{
	(
	    echo 'z b m f'
	    echo 'y c o e'
	    echo 'x a n h'
	    echo 'x a n g'
	) | tr '\n' '+' >in

	atf_check -o inline:'x a n g+x a n h+z b m f+y c o e+' \
	    sort -R + -k2 in
}

atf_test_case plus_tflag
plus_tflag_head()
{
	atf_set "descr" "Tests +- addressing: using -T caused a 'No such file" \
	    "or directory' error"
}
plus_tflag_body()
{
	mkdir ./+
	yes | sed 200000q | sort -T + >/dev/null || atf_fail "program failed"
}

atf_test_case plus_no_end
plus_no_end_head()
{
	atf_set "descr" "Tests +- addressing: field without end"
}
plus_no_end_body()
{
	cat >in <<EOF
a-B
a+b
a b
A+b
a	b
EOF

	cat >out <<EOF
a	b
a b
A+b
a-B
a+b
EOF

	atf_check -o file:out sort -df +0 +0d in
}

atf_init_test_cases()
{
	atf_add_test_case basic
	atf_add_test_case empty_file
	atf_add_test_case end_of_options
	atf_add_test_case missing_newline
	atf_add_test_case null_bytes
	atf_add_test_case long_records
	atf_add_test_case long_file
	atf_add_test_case any_char
	atf_add_test_case bflag
	atf_add_test_case cflag
	atf_add_test_case kflag_one_field
	atf_add_test_case kflag_two_fields
	atf_add_test_case kflag_many_fields
	atf_add_test_case kflag_outofbounds
	atf_add_test_case kflag_nonmonotone
	atf_add_test_case kflag_limits
	atf_add_test_case kflag_alpha
	atf_add_test_case kflag_no_end
	atf_add_test_case mflag
	atf_add_test_case mflag_uflag
	atf_add_test_case mflag_uflag_first
	atf_add_test_case nflag
	atf_add_test_case nflag_rflag
	atf_add_test_case oflag
	atf_add_test_case oflag_displaced
	atf_add_test_case rflag
	atf_add_test_case sflag
	atf_add_test_case sflag_many_files
	atf_add_test_case tflag
	atf_add_test_case tflag_alphabetic
	atf_add_test_case tflag_char_pos
	atf_add_test_case tflag_whitespace
	atf_add_test_case uflag
	atf_add_test_case uflag_rflag
	atf_add_test_case plus_one
	atf_add_test_case plus_one_minus_two
	atf_add_test_case plus_zero
	atf_add_test_case plus_nonmonotone
	atf_add_test_case plus_as_path
	atf_add_test_case plus_bad_tempfile
	atf_add_test_case plus_rflag_invalid
	atf_add_test_case plus_tflag
	atf_add_test_case plus_no_end
}
