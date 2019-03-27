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
import rfc4518
import stringprep
import util

if len(sys.argv) != 3:
    print "usage: %s rfc3454.txt out-dir" % sys.argv[0]
    sys.exit(1)

tables = rfc3454.read(sys.argv[1])
t2 = rfc4518.read()

for x in t2.iterkeys():
    tables[x] = t2[x]

map_list = stringprep.get_maplist()

map_h = generate.Header('%s/map_table.h' % sys.argv[2])

map_c = generate.Implementation('%s/map_table.c' % sys.argv[2])

map_h.file.write(
'''
#include "windlocl.h"

struct translation {
  uint32_t key;
  unsigned short val_len;
  unsigned short val_offset;
  wind_profile_flags flags;
};

extern const struct translation _wind_map_table[];

extern const size_t _wind_map_table_size;

extern const uint32_t _wind_map_table_val[];

''')

map_c.file.write(
'''
#include "map_table.h"

const struct translation _wind_map_table[] = {
''')

trans=[]

for t in map_list.iterkeys():
    for l in tables[t]:
        m = re.search('^ *([0-9A-F]+)-([0-9A-F]+); *([^;]+); *(.*) *$', l)
        if m:
            start = int(m.group(1), 0x10)
            end   = int(m.group(2), 0x10)
            value = m.group(3)
            desc  = m.group(4)
            for key in xrange(start,end,1):
                trans.append((key, value, desc, [t]))
            continue
        m = re.search('^ *([^;]+); *([^;]+); *(.*) *$', l)
        if m:
            key   = int(m.group(1), 0x10)
            value = m.group(2)
            desc  = m.group(3)
            trans.append((key, value, desc, [t]))
            continue

valTable = []
offsetTable = {}

trans = stringprep.sort_merge_trans(trans)

for x in trans:
    if x[0] == 0xad:
        print "fooresult %s" % ",".join(x[3])

for x in trans:
    (key, value, description, table) = x
    v = value.split()
    i = util.subList(valTable, v)
    if i:
        offsetTable[key] = i
    else:
        offsetTable[key] = len(valTable)
        valTable.extend(v)

for x in trans:
    (key, value, description, tables) = x
    symbols = stringprep.symbols(map_list, tables)
    if len(symbols) == 0:
        print "no symbol for %s %s (%s)" % (key, description, tables)
        sys.exit(1)
    v = value.split()
    map_c.file.write("  {0x%x, %u, %u, %s}, /* %s: %s */\n"
                % (key, len(v), offsetTable[key], symbols, ",".join(tables), description))

map_c.file.write(
'''
};

''')

map_c.file.write(
    "const size_t _wind_map_table_size = %u;\n\n" % len(trans))

map_c.file.write(
    "const uint32_t _wind_map_table_val[] = {\n")

for x in valTable:
    map_c.file.write("  0x%s,\n" % x)

map_c.file.write(
    "};\n\n")

map_h.close()
map_c.close()
