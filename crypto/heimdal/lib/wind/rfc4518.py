#!/usr/local/bin/python
# -*- coding: iso-8859-1 -*-

# $Id$

# Copyright (c) 2004, 2008 Kungliga Tekniska Högskolan
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

def read():
    """return a dict of tables from rfc4518"""

    ret = {}

#2.2.  Map
#
#   SOFT HYPHEN (U+00AD) and MONGOLIAN TODO SOFT HYPHEN (U+1806) code
#   points are mapped to nothing.  COMBINING GRAPHEME JOINER (U+034F) and
#   VARIATION SELECTORs (U+180B-180D, FF00-FE0F) code points are also
#   mapped to nothing.  The OBJECT REPLACEMENT CHARACTER (U+FFFC) is
#   mapped to nothing.

    t = []
    t.append(" 00AD; ; Map to nothing")
    t.append(" 1806; ; Map to nothing")
    t.append(" 034F; ; Map to nothing")

    t.append(" 180B; ; Map to nothing")
    t.append(" 180C; ; Map to nothing")
    t.append(" 180D; ; Map to nothing")

    t.append(" FE00; ; Map to nothing")
    t.append(" FE01; ; Map to nothing")
    t.append(" FE02; ; Map to nothing")
    t.append(" FE03; ; Map to nothing")
    t.append(" FE04; ; Map to nothing")
    t.append(" FE05; ; Map to nothing")
    t.append(" FE06; ; Map to nothing")
    t.append(" FE07; ; Map to nothing")
    t.append(" FE08; ; Map to nothing")
    t.append(" FE09; ; Map to nothing")
    t.append(" FE0A; ; Map to nothing")
    t.append(" FE0B; ; Map to nothing")
    t.append(" FE0C; ; Map to nothing")
    t.append(" FE0D; ; Map to nothing")
    t.append(" FE0E; ; Map to nothing")
    t.append(" FE0F; ; Map to nothing")

    t.append(" FFFC; ; Map to nothing")

#   CHARACTER TABULATION (U+0009), LINE FEED (LF) (U+000A), LINE
#   TABULATION (U+000B), FORM FEED (FF) (U+000C), CARRIAGE RETURN (CR)
#  (U+000D), and NEXT LINE (NEL) (U+0085) are mapped to SPACE (U+0020).

    t.append(" 0009; 0020 ; Map to SPACE")
    t.append(" 000A; 0020 ; Map to SPACE")
    t.append(" 000B; 0020 ; Map to SPACE")
    t.append(" 000C; 0020 ; Map to SPACE")
    t.append(" 000D; 0020 ; Map to SPACE")
    t.append(" 0085; 0020 ; Map to SPACE")

#   All other control code (e.g., Cc) points or code points with a
#   control function (e.g., Cf) are mapped to nothing.  The following is
#   a complete list of these code points: U+0000-0008, 000E-001F, 007F-
#   0084, 0086-009F, 06DD, 070F, 180E, 200C-200F, 202A-202E, 2060-2063,
#   206A-206F, FEFF, FFF9-FFFB, 1D173-1D17A, E0001, E0020-E007F.

    t.append(" 0000-0008; ; Map to nothing")
    t.append(" 000E-001F; ; Map to nothing")
    t.append(" 007F-0084; ; Map to nothing")
    t.append(" 0086-009F; ; Map to nothing")
    t.append(" 06DD; ; Map to nothing")
    t.append(" 070F; ; Map to nothing")
    t.append(" 180E; ; Map to nothing")
    t.append(" 200C-200F; ; Map to nothing")
    t.append(" 202A-202E; ; Map to nothing")
    t.append(" 2060-2063; ; Map to nothing")
    t.append(" 206A-206F; ; Map to nothing")
    t.append(" FEFF; ; Map to nothing")
    t.append(" FFF9-FFFB; ; Map to nothing")
    t.append(" 1D173-1D17A; ; Map to nothing")
    t.append(" E0001; ; Map to nothing")
    t.append(" E0020-E007F; ; Map to nothing")

#   ZERO WIDTH SPACE (U+200B) is mapped to nothing.  All other code
#   points with Separator (space, line, or paragraph) property (e.g., Zs,
#   Zl, or Zp) are mapped to SPACE (U+0020).  The following is a complete
#   list of these code points: U+0020, 00A0, 1680, 2000-200A, 2028-2029,
#   202F, 205F, 3000.

    t.append(" 200B; ; Map to nothing")
    t.append(" 0020; 0020; Map to SPACE")
    t.append(" 00A0; 0020; Map to SPACE")
    t.append(" 1680; 0020; Map to SPACE")
    t.append(" 2000-200A; 0020; Map to SPACE")
    t.append(" 2028-2029; 0020; Map to SPACE")
    t.append(" 202F; 0020; Map to SPACE")
    t.append(" 205F; 0020; Map to SPACE")
    t.append(" 3000; 0020; Map to SPACE")

    ret["rfc4518-map"] = t

#   For case ignore, numeric, and stored prefix string matching rules,
#   characters are case folded per B.2 of [RFC3454].

    t = []

#2.4.  Prohibit

#    The REPLACEMENT CHARACTER (U+FFFD) code point is prohibited.

    t.append(" FFFD;")

    ret["rfc4518-error"] = t

    t = []



    return ret
