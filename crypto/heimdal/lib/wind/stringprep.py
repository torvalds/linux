#!/usr/local/bin/python
# -*- coding: iso-8859-1 -*-

# $Id$

# Copyright (c) 2008 Kungliga Tekniska Högskolan
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

def _merge_table(res, source):
    for table in source.keys():
        res[table] = res.get(table, []) + source.get(table, [])
        
name_error = ['C.1.2', 'C.2.2', 'C.3', 'C.4', 'C.5', 'C.6', 'C.7', 'C.8', 'C.9']
ldap_error = ['A.1', 'C.3', 'C.4', 'C.5', 'C.8', 'rfc4518-error' ]
sasl_error = ['C.1.2', 'C.2.1', 'C.2.2', 'C.3', 'C.4', 'C.5', 'C.6', 'C.7', 'C.8', 'C.9']

name_map = ['B.1', 'B.2']
ldap_map = ['rfc4518-map']
ldap_case_map = ['rfc4518-map', 'B.2']
sasl_map = ['C.1.2', 'B.1']

def symbols(tabledict, tables):
    """return CPP symbols to use for this symbols"""
    list = []
    for x in tables:
        list = list + tabledict.get(x, [])
    if len(list) == 0:
        return ""
    return "|".join(map(lambda x: "WIND_PROFILE_%s" % (string.upper(x)), list))

def get_errorlist():
    d = dict()
    _merge_table(d, dict(map(lambda x: [x, ['name']], name_error)))
    _merge_table(d, dict(map(lambda x: [x, ['ldap']], ldap_error)))
    _merge_table(d, dict(map(lambda x: [x, ['sasl']], sasl_error)))
    return d

def get_maplist():
    d = dict()
    _merge_table(d, dict(map(lambda x: [x, ['name']], name_map)))
    _merge_table(d, dict(map(lambda x: [x, ['ldap']], ldap_map)))
    _merge_table(d, dict(map(lambda x: [x, ['ldap_case']], ldap_case_map)))
    _merge_table(d, dict(map(lambda x: [x, ['sasl']], sasl_map)))
    return d

def sort_merge_trans(trans):
    trans.sort()
    ret = []
    last = 0
    for x in trans:
        if last:
            if last[0] == x[0]:
                last = (last[0], last[1], last[2], last[3] + x[3])
            else:
                ret.append(last)
                last = x
        else:
            last = x
    if last:
        ret.append(last)
    return ret
