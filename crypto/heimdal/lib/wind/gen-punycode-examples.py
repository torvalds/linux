#!/usr/local/bin/python
# -*- coding: iso-8859-1 -*-

# $Id$

# Copyright (c) 2004 Kungliga Tekniska Högskolan
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

import re
import string
import sys

import generate

if len(sys.argv) != 3:
    print "usage: %s rfc3492.txt" % sys.argv[0]
    sys.exit(1)

f = open(sys.argv[1], 'r')

examples_h = generate.Header('%s/punycode_examples.h' % sys.argv[2])
examples_c = generate.Header('%s/punycode_examples.c' % sys.argv[2])

start = False

while True:
    l = f.readline()
    if not l:
        break
    if l[-2:] == "\\\n":
        l2 = f.readline()
        if not l2:
            raise Exception("EOF in backslash escape")
        l2 = re.sub('^ *', '', l2)
        l = l[:-2] + l2
    if start:
        if re.match('7\.2', l):
            start = False
        else:
            m = re.search('^ *\([A-Z]\) *(.*)$', l);
            if m:
                desc = m.group(1)
                codes = []
            else:
                m = re.search('^ *([uU]+.*) *$', l)
                if m:
                    codes.extend(string.split(m.group(1), ' '))
                else:
                    m = re.search('^ *Punycode: (.*) *$', l)
                    if m:
                        cases.append([codes, m.group(1), desc])
    else:
        if re.match('^7\.1', l):
            start = True
            cases = []
            
f.close()

examples_h.file.write(
'''
#include <krb5-types.h>

#define MAX_LENGTH 40

struct punycode_example {
    size_t len;
    uint32_t val[MAX_LENGTH];
    const char *pc;
    const char *description;
};

extern const struct punycode_example punycode_examples[];

extern const size_t punycode_examples_size;
''')

examples_c.file.write(
'''
#include <stdlib.h>
#include "punycode_examples.h"

const struct punycode_example punycode_examples[] = {
''')

for x in cases:
    [cp, pc, desc] = x
    examples_c.file.write(
        "  {%u, {%s}, \"%s\", \"%s\"},\n" %
        (len(cp),
         string.join([re.sub('[uU]\+', '0x', x) for x in cp], ', '),
         pc,
         desc))

examples_c.file.write(
'''};

''')

examples_c.file.write(
    "const size_t punycode_examples_size = %u;\n\n" % len(cases))

examples_h.close()
examples_c.close()
