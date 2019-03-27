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

if len(sys.argv) != 3:
    print "usage: %s UnicodeData.txt out-dir" % sys.argv[0]
    sys.exit(1)

ud = UnicodeData.read(sys.argv[1])

trans = {}
for k,v in ud.items():
    if int(v[2]) != 0 :
        trans[k] = [int(v[2]), v[1]]

# trans = [(x[0], int(x[3]), x[1]) for x in UnicodeData.read() if int(x[3]) != 0]

combining_h = generate.Header('%s/combining_table.h' % sys.argv[2])
combining_c = generate.Implementation('%s/combining_table.c' % sys.argv[2])

combining_h.file.write(
'''
#include <krb5-types.h>

struct translation {
  uint32_t key;
  unsigned combining_class;	
};

extern const struct translation _wind_combining_table[];

extern const size_t _wind_combining_table_size;
''')

combining_c.file.write(
'''
#include <stdlib.h>
#include "combining_table.h"

const struct translation _wind_combining_table[] = {
''')

s = trans.keys()
s.sort()
for k in s:
    v = trans[k]
    combining_c.file.write("{0x%x, %u}, /* %s */\n"
                           % (k, v[0], v[1]))
    

#trans.sort()
#for x in trans:
#    combining_c.file.write("{0x%x, %u}, /* %s */\n"
#                           % (x[0], x[1], x[2]))

combining_c.file.write(
'''
};
''')

combining_c.file.write(
    "const size_t _wind_combining_table_size = %u;\n" % len(trans))


combining_h.close()
combining_c.close()
