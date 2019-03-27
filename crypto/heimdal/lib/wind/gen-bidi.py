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
import rfc3454

if len(sys.argv) != 3:
    print "usage: %s rfc3454.txt outdir" % sys.argv[0]
    sys.exit(1)

tables = rfc3454.read(sys.argv[1])

bidi_h = generate.Header('%s/bidi_table.h' % sys.argv[2])

bidi_c = generate.Implementation('%s/bidi_table.c' % sys.argv[2])

bidi_h.file.write(
'''
#include <krb5-types.h>

struct range_entry {
  uint32_t start;
  unsigned len;
};

extern const struct range_entry _wind_ral_table[];
extern const struct range_entry _wind_l_table[];

extern const size_t _wind_ral_table_size;
extern const size_t _wind_l_table_size;

''')

bidi_c.file.write(
'''
#include <stdlib.h>
#include "bidi_table.h"

''')

def printTable(file, table, variable):
    """print table to file named as variable"""
    file.write("const struct range_entry %s[] = {\n" % variable)
    count = 0
    for l in tables[table]:
        m = re.search('^ *([0-9A-F]+)-([0-9A-F]+) *$', l)
        if m:
            start = int(m.group(1), 0x10)
            end   = int(m.group(2), 0x10)
            file.write("  {0x%x, 0x%x},\n" % (start, end - start + 1))
            count += 1
        else:
            m = re.search('^ *([0-9A-F]+) *$', l)
            if m:
                v = int(m.group(1), 0x10)
                file.write("  {0x%x, 1},\n" % v)
                count += 1
    file.write("};\n\n")
    file.write("const size_t %s_size = %u;\n\n" % (variable, count))

printTable(bidi_c.file, 'D.1', '_wind_ral_table')
printTable(bidi_c.file, 'D.2', '_wind_l_table')

bidi_h.close()
bidi_c.close()
