#!/usr/bin/python
# -*- coding: utf-8 -*-
#
# Copyright (c) 2010 Kungliga Tekniska Högskolan
# (Royal Institute of Technology, Stockholm, Sweden).
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# 3. Neither the name of the Institute nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

CONTROL_CHAR = 1
PRINTABLE = 2
RFC2253_QUOTE_FIRST = 4
RFC2253_QUOTE_LAST = 8
RFC2253_QUOTE = 16
RFC2253_HEX = 32

chars = []

for i in range(0, 256):
    chars.append(0);

for i in range(0, 256):
    if (i < 32 or i > 126):
        chars[i] |= CONTROL_CHAR | RFC2253_HEX;

for i in range(ord("A"), ord("Z") + 1):
    chars[i] |= PRINTABLE
for i in range(ord("a"), ord("z") + 1):
    chars[i] |= PRINTABLE
for i in range(ord("0"), ord("9") + 1):
    chars[i] |= PRINTABLE

chars[ord(' ')] |= PRINTABLE
chars[ord('+')] |= PRINTABLE
chars[ord(',')] |= PRINTABLE
chars[ord('-')] |= PRINTABLE
chars[ord('.')] |= PRINTABLE
chars[ord('/')] |= PRINTABLE
chars[ord(':')] |= PRINTABLE
chars[ord('=')] |= PRINTABLE
chars[ord('?')] |= PRINTABLE

chars[ord(' ')] |= RFC2253_QUOTE_FIRST | RFC2253_QUOTE_FIRST

chars[ord(',')] |= RFC2253_QUOTE
chars[ord('=')] |= RFC2253_QUOTE
chars[ord('+')] |= RFC2253_QUOTE
chars[ord('<')] |= RFC2253_QUOTE
chars[ord('>')] |= RFC2253_QUOTE
chars[ord('#')] |= RFC2253_QUOTE
chars[ord(';')] |= RFC2253_QUOTE

print "#define Q_CONTROL_CHAR		1"
print "#define Q_PRINTABLE		2"
print "#define Q_RFC2253_QUOTE_FIRST	4"
print "#define Q_RFC2253_QUOTE_LAST	8"
print "#define Q_RFC2253_QUOTE		16"
print "#define Q_RFC2253_HEX		32"
print ""
print "#define Q_RFC2253		(Q_RFC2253_QUOTE_FIRST|Q_RFC2253_QUOTE_LAST|Q_RFC2253_QUOTE|Q_RFC2253_HEX)"
print "\n" * 2




print "unsigned char char_map[] = {\n\t",
for x in range(0, 256):
    if (x % 8) == 0 and x != 0:
        print "\n\t",
    print "0x%(char)02x" % { 'char' : chars[x] },
    if x < 255:
        print ", ",
    else:
        print ""
print "};"
