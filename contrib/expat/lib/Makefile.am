#
#                          __  __            _
#                       ___\ \/ /_ __   __ _| |_
#                      / _ \\  /| '_ \ / _` | __|
#                     |  __//  \| |_) | (_| | |_
#                      \___/_/\_\ .__/ \__,_|\__|
#                               |_| XML parser
#
# Copyright (c) 2017 Expat development team
# Licensed under the MIT license:
#
# Permission is  hereby granted,  free of charge,  to any  person obtaining
# a  copy  of  this  software   and  associated  documentation  files  (the
# "Software"),  to  deal in  the  Software  without restriction,  including
# without  limitation the  rights  to use,  copy,  modify, merge,  publish,
# distribute, sublicense, and/or sell copies of the Software, and to permit
# persons  to whom  the Software  is  furnished to  do so,  subject to  the
# following conditions:
#
# The above copyright  notice and this permission notice  shall be included
# in all copies or substantial portions of the Software.
#
# THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
# EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
# NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.

include_HEADERS = \
    ../expat_config.h \
    expat.h \
    expat_external.h

lib_LTLIBRARIES = libexpat.la

libexpat_la_LDFLAGS = \
    -no-undefined \
    -version-info @LIBCURRENT@:@LIBREVISION@:@LIBAGE@

libexpat_la_SOURCES = \
    loadlibrary.c \
    xmlparse.c \
    xmltok.c \
    xmlrole.c

doc_DATA = \
    ../AUTHORS \
    ../Changes

install-data-hook:
	cd "$(DESTDIR)$(docdir)" && $(am__mv) Changes changelog

uninstall-local:
	$(RM) "$(DESTDIR)$(docdir)/changelog"

EXTRA_DIST = \
    ascii.h \
    asciitab.h \
    expat_external.h \
    expat.h \
    iasciitab.h \
    internal.h \
    latin1tab.h \
    libexpat.def \
    libexpatw.def \
    nametab.h \
    siphash.h \
    utf8tab.h \
    winconfig.h \
    xmlrole.h \
    xmltok.h \
    xmltok_impl.c \
    xmltok_impl.h \
    xmltok_ns.c
