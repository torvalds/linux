# Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
#
# Permission is hereby granted, free of charge, to any person obtaining 
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be 
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# ======================================================================

# The lines below are a horrible hack that nonetheless works. On a
# "make" utility compatible with Single Unix v4 (this includes GNU and
# BSD make), the '\' at the end of a command line counts as an escape
# for the newline character, so the next line is still a comment.
# However, Microsoft's nmake.exe (that comes with Visual Studio) does
# not interpret the final '\' that way in a comment. The end result is
# that when using nmake.exe, this will include "mk/Win.mk", whereas
# GNU/BSD make will include "mk/Unix.mk".

# \
!ifndef 0 # \
!include mk/NMake.mk # \
!else
.POSIX:
include mk/SingleUnix.mk
# Extra hack for OpenBSD make.
ifndef: all
0: all
endif: all
# \
!endif
