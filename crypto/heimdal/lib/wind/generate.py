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

import datetime
import string
import os

class GeneratedFile :
    "Represents a generated file"
    def __init__(self, name) :
        "Create a new GeneratedFile with name"
        self.name  = os.path.basename(name)
        self.file  = open(name, 'w')
        self.file.write('/* ' + name + ' */\n')
        self.file.write('/* Automatically generated at ' +
                        datetime.datetime.now().isoformat() +
                        ' */\n\n')
    
    def close(self) :
        """End and close the file header"""
        self.file.close()
    

class Header(GeneratedFile) :
    "Represents a generated header file"
    guardTrans = string.maketrans('-.', '__')
    def makeGuard(self) :
        """Return a name to be used as ifdef guard"""
        return string.upper(string.translate(self.name, self.guardTrans))

    def __init__(self, name) :
        "Create a new Header with name"
        GeneratedFile.__init__(self, name)
        self.guard = self.makeGuard()
        self.file.write('#ifndef ' + self.guard + '\n')
        self.file.write('#define ' + self.guard + ' 1\n')
    
    def close(self) :
        """End and close the file header"""
        self.file.write('#endif /* ' + self.guard + ' */\n')
        GeneratedFile.close(self)


class Implementation(GeneratedFile) :
    "Represents a generated implementation file"
    def __init__(self, name) :
        "Create a new Implementation with name"
        GeneratedFile.__init__(self, name)
