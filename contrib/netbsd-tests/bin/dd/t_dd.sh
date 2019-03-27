# $NetBSD: t_dd.sh,v 1.1 2012/03/17 16:33:11 jruoho Exp $
#
# Copyright (c) 2007 The NetBSD Foundation, Inc.
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

test_dd_length() {
	result=$1
	cmd=$2
	set -- x `eval $cmd | wc -c`
	res=$2
	if [ x"$res" != x"$result" ]; then
		atf_fail "Expected $result bytes of output, got $res: $cmd"
	fi
}

atf_test_case length
length_head() {
	# XXX The PR should be stored in a tag.
	atf_set "descr" "Test for result messages accidentally pumped into" \
	                "the output file if the standard IO descriptors are" \
	                "closed.  The last of the three following tests is" \
	                "the one expected to fail.  (NetBSD PR bin/8521)"
}
length_body() {
	# Begin FreeBSD
	if ! df /dev/fd | grep -q '^fdescfs'; then
		atf_skip "fdescfs is not mounted on /dev/fd"
	fi
	# End FreeBSD

	test_dd_length 512 \
	    "dd if=/dev/zero of=/dev/fd/5 count=1 5>&1 >/dev/null 2>/dev/null"
	test_dd_length 512 \
	    "dd if=/dev/zero of=/dev/fd/5 count=1 5>&1 >&- 2>/dev/null"
	test_dd_length 512 \
	    "dd if=/dev/zero of=/dev/fd/5 count=1 5>&1 >&- 2>&-"
}

test_dd_io() {
	res="`echo -n "$2" | eval $1`"
        if [ x"$res" != x"$3" ]; then
		atf_fail "Expected \"$3\", got \"$res\": $1"
	fi
}

allbits1="\000\001\002\003\004\005\006\007\010\011\012\013\014\015\016\017\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037\040\041\042\043\044\045\046\047\050\051\052\053\054\055\056\057\060\061\062\063\064\065\066\067\070\071\072\073\074\075\076\077\100\101\102\103\104\105\106\107\110\111\112\113\114\115\116\117\120\121\122\123\124\125\126\127\130\131\132\133\134\135\136\137\140\141\142\143\144\145\146\147\150\151\152\153\154\155\156\157\160\161\162\163\164\165\166\167\170\171\172\173\174\175\176\177\200\201\202\203\204\205\206\207\210\211\212\213\214\215\216\217\220\221\222\223\224\225\226\227\230\231\232\233\234\235\236\237\240\241\242\243\244\245\246\247\250\251\252\253\254\255\256\257\260\261\262\263\264\265\266\267\270\271\272\273\274\275\276\277\300\301\302\303\304\305\306\307\310\311\312\313\314\315\316\317\320\321\322\323\324\325\326\327\330\331\332\333\334\335\336\337\340\341\342\343\344\345\346\347\350\351\352\353\354\355\356\357\360\361\362\363\364\365\366\367\370\371\372\373\374\375\376\377"

ebcdicbits1="\000\001\002\003\067\055\056\057\026\005\045\013\014\015\016\017\020\021\022\023\074\075\062\046\030\031\077\047\034\035\036\037\100\132\177\173\133\154\120\175\115\135\134\116\153\140\113\141\360\361\362\363\364\365\366\367\370\371\172\136\114\176\156\157\174\301\302\303\304\305\306\307\310\311\321\322\323\324\325\326\327\330\331\342\343\344\345\346\347\350\351\255\340\275\232\155\171\201\202\203\204\205\206\207\210\211\221\222\223\224\225\226\227\230\231\242\243\244\245\246\247\250\251\300\117\320\137\007\040\041\042\043\044\025\006\027\050\051\052\053\054\011\012\033\060\061\032\063\064\065\066\010\070\071\072\073\004\024\076\341\101\102\103\104\105\106\107\110\111\121\122\123\124\125\126\127\130\131\142\143\144\145\146\147\150\151\160\161\162\163\164\165\166\167\170\200\212\213\214\215\216\217\220\152\233\234\235\236\237\240\252\253\254\112\256\257\260\261\262\263\264\265\266\267\270\271\272\273\274\241\276\277\312\313\314\315\316\317\332\333\334\335\336\337\352\353\354\355\356\357\372\373\374\375\376\377"

allvisbits=`echo -n "$allbits1" | unvis | vis`
ebcdicvisbits=`echo -n "$ebcdicbits1" | unvis | vis`

atf_test_case io
io_head() {
	atf_set "descr" "This checks the combination of bs= with" \
	                "conv=ebcdic.  Prior to revision 1.24 of dd's" \
	                "args.c, the conv option would be ignored."
}
io_body() {
	test_dd_io "unvis | dd 2>/dev/null | vis" \
	           "$allvisbits" "$allvisbits"
	test_dd_io "unvis | dd ibs=1 2>/dev/null | vis" \
	           "$allvisbits" "$allvisbits"
	test_dd_io "unvis | dd obs=1 2>/dev/null | vis" \
	           "$allvisbits" "$allvisbits"
	test_dd_io "unvis | dd bs=1 2>/dev/null | vis" \
	           "$allvisbits" "$allvisbits"

	test_dd_io "unvis | dd conv=ebcdic 2>/dev/null | vis" \
	           "$allvisbits" "$ebcdicvisbits"
	test_dd_io "unvis | dd conv=ebcdic ibs=512 2>/dev/null | vis" \
	           "$allvisbits" "$ebcdicvisbits"
	test_dd_io "unvis | dd conv=ebcdic obs=512 2>/dev/null | vis" \
	           "$allvisbits" "$ebcdicvisbits"
	test_dd_io "unvis | dd conv=ebcdic bs=512 2>/dev/null | vis" \
	           "$allvisbits" "$ebcdicvisbits"

	test_dd_io "unvis | dd conv=ebcdic 2>/dev/null | vis" \
	           "$allvisbits" "$ebcdicvisbits"
	test_dd_io "unvis | dd conv=ebcdic ibs=1 2>/dev/null | vis" \
	           "$allvisbits" "$ebcdicvisbits"
	test_dd_io "unvis | dd conv=ebcdic obs=1 2>/dev/null | vis" \
	           "$allvisbits" "$ebcdicvisbits"
	test_dd_io "unvis | dd conv=ebcdic bs=1 2>/dev/null | vis" \
	           "$allvisbits" "$ebcdicvisbits"
}

atf_test_case seek
seek_head() {
	atf_set "descr" "Tests output file seeking"
}

seek_body() {
	echo TEST1234 > testfile
	atf_check -s exit:0 -e ignore \
	    dd if=/dev/zero of=testfile seek=1 bs=8k count=1
	atf_check -s exit:0 -e ignore -o match:'^TEST1234$' dd if=testfile
	eval $(stat -s testfile)
	atf_check_equal $st_size $((2*8192))

	echo -n TEST1234 > tf2
	atf_check -s exit:0 -e ignore -x \
	    'dd bs=4 if=/dev/zero count=1 | tr \\0 \\n | dd of=tf2 bs=4 seek=1'
	atf_check -s exit:0 -e ignore -o match:'^TEST$' dd if=tf2
	eval $(stat -s tf2)
	atf_check_equal $st_size 8
}

atf_init_test_cases()
{
	atf_add_test_case length
	atf_add_test_case io
	atf_add_test_case seek
}
