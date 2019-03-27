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
import UnicodeData
import util

if len(sys.argv) != 4:
    print "usage: %s UnicodeData.txt"
    " CompositionExclusions-3.2.0.txt out-dir" % sys.argv[0]
    sys.exit(1)

ud = UnicodeData.read(sys.argv[1])

def sortedKeys(d):
    """Return a sorted list of the keys of a dict"""
    keys = d.keys()
    keys.sort()
    return keys

trans = dict([(k, [re.sub('<[a-zA-Z]+>', '', v[4]), v[0]])
              for k,v in ud.items() if v[4]])

maxLength = 0
for v in trans.values():
    maxLength = max(maxLength, len(v[0].split()))

normalize_h = generate.Header('%s/normalize_table.h' % sys.argv[3])
normalize_c = generate.Implementation('%s/normalize_table.c' % sys.argv[3])

normalize_h.file.write(
'''
#include <krb5-types.h>

#define MAX_LENGTH_CANON %u

struct translation {
  uint32_t key;
  unsigned short val_len;
  unsigned short val_offset;
};

extern const struct translation _wind_normalize_table[];

extern const uint32_t _wind_normalize_val_table[];

extern const size_t _wind_normalize_table_size;

struct canon_node {
  uint32_t val;
  unsigned char next_start;
  unsigned char next_end;
  unsigned short next_offset;
};

extern const struct canon_node _wind_canon_table[];

extern const unsigned short _wind_canon_next_table[];
''' % maxLength)

normalize_c.file.write(
'''
#include <stdlib.h>
#include "normalize_table.h"

const struct translation _wind_normalize_table[] = {
''')

normalizeValTable = []

for k in sortedKeys(trans) :
    v = trans[k]
    (key, value, description) = k, v[0], v[1]
    vec = [int(x, 0x10) for x in value.split()];
    offset = util.subList(normalizeValTable, vec)
    if not offset:
        offset = len(normalizeValTable)
        normalizeValTable.extend(vec) # [("0x%s" % i) for i in vec])
    normalize_c.file.write("  {0x%x, %u, %u}, /* %s */\n"
                           % (key, len(vec), offset, description))

normalize_c.file.write(
'''};

''')

normalize_c.file.write(
    "const size_t _wind_normalize_table_size = %u;\n\n" % len(trans))

normalize_c.file.write("const uint32_t _wind_normalize_val_table[] = {\n")

for v in normalizeValTable:
    normalize_c.file.write("  0x%x,\n" % v)

normalize_c.file.write("};\n\n");

exclusions = UnicodeData.read(sys.argv[2])

inv = dict([(''.join(["%05x" % int(x, 0x10) for x in v[4].split(' ')]),
             [k, v[0]])
            for k,v in ud.items()
            if v[4] and not re.search('<[a-zA-Z]+> *', v[4]) and not exclusions.has_key(k)])

table = 0

tables = {}

def createTable():
    """add a new table"""
    global table, tables
    ret = table
    table += 1
    tables[ret] = [0] + [None] * 16
    return ret

def add(table, k, v):
    """add an entry (k, v) to table (recursively)"""
    if len(k) == 0:
        table[0] = v[0]
    else:
        i = int(k[0], 0x10) + 1
        if table[i] == None:
            table[i] = createTable()
        add(tables[table[i]], k[1:], v)

top = createTable()

for k,v in inv.items():
    add(tables[top], k, v)

next_table  = []
tableToNext = {}
tableEnd    = {}
tableStart  = {}

for k in sortedKeys(tables) :
    t = tables[k]
    tableToNext[k] = len(next_table)
    l = t[1:]
    start = 0
    while start < 16 and l[start] == None:
        start += 1
    end = 16
    while end > start and l[end - 1] == None:
        end -= 1
    tableStart[k] = start
    tableEnd[k]   = end
    n = []
    for i in range(start, end):
        x = l[i]
        if x:
            n.append(x)
        else:
            n.append(0)
    next_table.extend(n)

normalize_c.file.write("const struct canon_node _wind_canon_table[] = {\n")

for k in sortedKeys(tables) :
    t = tables[k]
    normalize_c.file.write("  {0x%x, %u, %u, %u},\n" %
                           (t[0], tableStart[k], tableEnd[k], tableToNext[k]))

normalize_c.file.write("};\n\n")

normalize_c.file.write("const unsigned short _wind_canon_next_table[] = {\n")

for k in next_table:
    normalize_c.file.write("  %u,\n" % k)

normalize_c.file.write("};\n\n")

normalize_h.close()
normalize_c.close()
