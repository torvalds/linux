# $NetBSD: t_df.sh,v 1.1 2012/03/17 16:33:11 jruoho Exp $
#
# Copyright (c) 2007, 2008 The NetBSD Foundation, Inc.
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

atf_test_case normal
normal_head() {
	atf_set "descr" "Checks that the output of df without flags is" \
	                "correct according to some already-known, sane" \
	                "output"
}
normal_body() {
	cat >expout <<EOF
Filesystem    1K-blocks       Used      Avail %Cap Mounted on
filer:/      1202716672     135168 1202581504   0% /filer
filer:/      1202716672          0 1202716672   0% /filer
filer:/      1202716672  240543334  962173337  20% /filer
filer:/      1202716672  721630003  481086668  60% /filer
filer:/      1202716672 1142580838   60135833  95% /filer
filer:/      1202716672 1202716672          0 100% /filer
filer:/      1202716672          0 1142580838   0% /filer
filer:/      1202716672  240543334  902037504  21% /filer
filer:/      1202716672  721630003  420950835  63% /filer
filer:/      1202716672 1142580838          0 100% /filer
filer:/      1202716672 1202716672  -60135833 105% /filer
filer:/      1202716672          0 1082445004   0% /filer
filer:/      1202716672  240543334  841901670  22% /filer
filer:/      1202716672  721630003  360815001  66% /filer
filer:/      1202716672 1142580838  -60135833 105% /filer
filer:/      1202716672 1202716672 -120271667 111% /filer
filer:/      1202716672          0 1022309171   0% /filer
filer:/      1202716672  240543334  781765836  23% /filer
filer:/      1202716672  721630003  300679168  70% /filer
filer:/      1202716672 1142580838 -120271667 111% /filer
filer:/      1202716672 1202716672 -180407500 117% /filer
/dev/ld0g    1308726116   17901268 1225388540   1% /anon-root
/dev/ld0g    1308726116          0 1308726116   0% /anon-root
/dev/ld0g    1308726116  261745224 1046980892  20% /anon-root
/dev/ld0g    1308726116  785235672  523490444  60% /anon-root
/dev/ld0g    1308726116 1243289812   65436304  95% /anon-root
/dev/ld0g    1308726116 1308726116          0 100% /anon-root
/dev/ld0g    1308726116          0 1243289808   0% /anon-root
/dev/ld0g    1308726116  261745224  981544584  21% /anon-root
/dev/ld0g    1308726116  785235672  458054140  63% /anon-root
/dev/ld0g    1308726116 1243289812          0 100% /anon-root
/dev/ld0g    1308726116 1308726116  -65436304 105% /anon-root
/dev/ld0g    1308726116          0 1177853504   0% /anon-root
/dev/ld0g    1308726116  261745224  916108280  22% /anon-root
/dev/ld0g    1308726116  785235672  392617832  66% /anon-root
/dev/ld0g    1308726116 1243289812  -65436304 105% /anon-root
/dev/ld0g    1308726116 1308726116 -130872608 111% /anon-root
/dev/ld0g    1308726116          0 1112417196   0% /anon-root
/dev/ld0g    1308726116  261745224  850671972  23% /anon-root
/dev/ld0g    1308726116  785235672  327181528  70% /anon-root
/dev/ld0g    1308726116 1243289812 -130872608 111% /anon-root
/dev/ld0g    1308726116 1308726116 -196308916 117% /anon-root
/dev/strpct  21474836476 10737418240 10737418236  50% /strpct
/dev/wd0e      10485688    2859932    7625756  27% /mount/windows/C
EOF
	atf_check -s eq:0 -o file:expout -e empty \
	    -x "BLOCKSIZE=1k $(atf_get_srcdir)/h_df -n"
}

atf_test_case hflag
hflag_head() {
	atf_set "descr" "Checks that the output of df is correct according" \
	                "to some already-known, sane output when using the" \
	                "human readable format"
}
hflag_body() {
	cat >expout <<EOF
Filesystem         Size       Used      Avail %Cap Mounted on
filer:/            1.1T       132M       1.1T   0% /filer
filer:/            1.1T         0B       1.1T   0% /filer
filer:/            1.1T       229G       918G  20% /filer
filer:/            1.1T       688G       459G  60% /filer
filer:/            1.1T       1.1T        57G  95% /filer
filer:/            1.1T       1.1T         0B 100% /filer
filer:/            1.1T         0B       1.1T   0% /filer
filer:/            1.1T       229G       860G  21% /filer
filer:/            1.1T       688G       401G  63% /filer
filer:/            1.1T       1.1T         0B 100% /filer
filer:/            1.1T       1.1T       -57G 105% /filer
filer:/            1.1T         0B       1.0T   0% /filer
filer:/            1.1T       229G       803G  22% /filer
filer:/            1.1T       688G       344G  66% /filer
filer:/            1.1T       1.1T       -57G 105% /filer
filer:/            1.1T       1.1T      -115G 111% /filer
filer:/            1.1T         0B       975G   0% /filer
filer:/            1.1T       229G       746G  23% /filer
filer:/            1.1T       688G       287G  70% /filer
filer:/            1.1T       1.1T      -115G 111% /filer
filer:/            1.1T       1.1T      -172G 117% /filer
/dev/ld0g          1.2T        17G       1.1T   1% /anon-root
/dev/ld0g          1.2T         0B       1.2T   0% /anon-root
/dev/ld0g          1.2T       250G       998G  20% /anon-root
/dev/ld0g          1.2T       749G       499G  60% /anon-root
/dev/ld0g          1.2T       1.2T        62G  95% /anon-root
/dev/ld0g          1.2T       1.2T         0B 100% /anon-root
/dev/ld0g          1.2T         0B       1.2T   0% /anon-root
/dev/ld0g          1.2T       250G       936G  21% /anon-root
/dev/ld0g          1.2T       749G       437G  63% /anon-root
/dev/ld0g          1.2T       1.2T         0B 100% /anon-root
/dev/ld0g          1.2T       1.2T       -62G 105% /anon-root
/dev/ld0g          1.2T         0B       1.1T   0% /anon-root
/dev/ld0g          1.2T       250G       874G  22% /anon-root
/dev/ld0g          1.2T       749G       374G  66% /anon-root
/dev/ld0g          1.2T       1.2T       -62G 105% /anon-root
/dev/ld0g          1.2T       1.2T      -125G 111% /anon-root
/dev/ld0g          1.2T         0B       1.0T   0% /anon-root
/dev/ld0g          1.2T       250G       811G  23% /anon-root
/dev/ld0g          1.2T       749G       312G  70% /anon-root
/dev/ld0g          1.2T       1.2T      -125G 111% /anon-root
/dev/ld0g          1.2T       1.2T      -187G 117% /anon-root
/dev/strpct         20T        10T        10T  50% /strpct
/dev/wd0e           10G       2.7G       7.3G  27% /mount/windows/C
EOF
	atf_check -s eq:0 -o file:expout -e empty \
	    -x "BLOCKSIZE=1k $(atf_get_srcdir)/h_df -hn"
}

atf_init_test_cases()
{
	atf_add_test_case normal
	atf_add_test_case hflag
}
