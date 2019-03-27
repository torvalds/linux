##############################################################################
# Copyright (c) 1998-2013,2014 Free Software Foundation, Inc.                #
#                                                                            #
# Permission is hereby granted, free of charge, to any person obtaining a    #
# copy of this software and associated documentation files (the "Software"), #
# to deal in the Software without restriction, including without limitation  #
# the rights to use, copy, modify, merge, publish, distribute, distribute    #
# with modifications, sublicense, and/or sell copies of the Software, and to #
# permit persons to whom the Software is furnished to do so, subject to the  #
# following conditions:                                                      #
#                                                                            #
# The above copyright notice and this permission notice shall be included in #
# all copies or substantial portions of the Software.                        #
#                                                                            #
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR #
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   #
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    #
# THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER      #
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    #
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        #
# DEALINGS IN THE SOFTWARE.                                                  #
#                                                                            #
# Except as contained in this notice, the name(s) of the above copyright     #
# holders shall not be used in advertising or otherwise to promote the sale, #
# use or other dealings in this Software without prior written               #
# authorization.                                                             #
##############################################################################
# $Id: dist.mk,v 1.973 2014/02/22 16:55:12 tom Exp $
# Makefile for creating ncurses distributions.
#
# This only needs to be used directly as a makefile by developers, but
# configure mines the current version number out of here.  To move
# to a new version number, just edit this file and run configure.
#
SHELL = /bin/sh

# These define the major/minor/patch versions of ncurses.
NCURSES_MAJOR = 5
NCURSES_MINOR = 9
NCURSES_PATCH = 20140222

# We don't append the patch to the version, since this only applies to releases
VERSION = $(NCURSES_MAJOR).$(NCURSES_MINOR)

# The most recent html files were generated with lynx 2.8.6 (or later), using
# ncurses configured with
#	--without-manpage-renames
# on Debian/testing.  The -scrollbar and -width options are used to make lynx
# use 79 columns as it did in 2.8.5 and before.
DUMP	= lynx -dump -scrollbar=0 -width=79
DUMP2	= $(DUMP) -nolist

# gcc's file is "gnathtml.pl"
GNATHTML= gnathtml

# man2html 3.0.1 is a Perl script which assumes that pages are fixed size.
# Not all man programs agree with this assumption; some use half-spacing, which
# has the effect of lengthening the text portion of the page -- so man2html
# would remove some text.  The man program on Redhat 6.1 appears to work with
# man2html if we set the top/bottom margins to 6 (the default is 7).  Newer
# versions of 'man' leave no margin (and make it harder to sync with pages).
MAN2HTML= man2html -botm=0 -topm=0 -cgiurl '$$title.$$section$$subsection.html'

ALL	= ANNOUNCE doc/html/announce.html doc/ncurses-intro.doc doc/hackguide.doc manhtml adahtml

all :	$(ALL)

dist:	$(ALL)
	(cd ..;  tar cvf ncurses-$(VERSION).tar `sed <ncurses-$(VERSION)/MANIFEST 's/^./ncurses-$(VERSION)/'`;  gzip ncurses-$(VERSION).tar)

distclean:
	rm -f $(ALL) subst.tmp subst.sed

# Don't mess with announce.html.in unless you have lynx available!
doc/html/announce.html: announce.html.in
	sed 's,@VERSION@,$(VERSION),' <announce.html.in > $@

ANNOUNCE : doc/html/announce.html
	$(DUMP) doc/html/announce.html > $@

doc/ncurses-intro.doc: doc/html/ncurses-intro.html
	$(DUMP2) doc/html/ncurses-intro.html > $@
doc/hackguide.doc: doc/html/hackguide.html
	$(DUMP2) doc/html/hackguide.html > $@

# This is the original command:
#	MANPROG	= tbl | nroff -man
#
# This happens to work for groff 1.18.1 on Debian.  At some point groff's
# maintainer changed the line-length (we do not want/need that here).
#
# The distributed html files are formatted using
#	configure --without-manpage-renames
#
# The edit_man.sed script is built as a side-effect of installing the manpages.
# If that conflicts with the --without-manpage-renames, you can install those
# in a different location using the --with-install-prefix option of the
# configure script.
MANPROG	= tbl | nroff -mandoc -rLL=65n -rLT=71n -Tascii

manhtml:
	@for f in doc/html/man/*.html; do \
	   test -f $$f || continue; \
	   case $$f in \
	   */index.html) ;; \
	   *) rm -f $$f ;; \
	   esac; \
	done
	@mkdir -p doc/html/man
	@rm -f subst.tmp ;
	@for f in man/*.[0-9]*; do \
	   m=`basename $$f` ;\
	   x=`echo $$m | awk -F. '{print $$2;}'` ;\
	   xu=`echo $$x | dd conv=ucase 2>/dev/null` ;\
	   if [ "$${x}" != "$${xu}" ]; then \
	     echo "s/$${xu}/$${x}/g" >> subst.tmp ;\
	   fi ;\
	done
	# change some things to make weblint happy:
	@cat man_alias.sed           >> subst.tmp
	@echo 's/<B>/<STRONG>/g'     >> subst.tmp
	@echo 's/<\/B>/<\/STRONG>/g' >> subst.tmp
	@echo 's/<I>/<EM>/g'         >> subst.tmp
	@echo 's/<\/I>/<\/EM>/g'     >> subst.tmp
	@misc/csort < subst.tmp | uniq > subst.sed
	@echo '/<\/TITLE>/a\' >> subst.sed
	@echo '<link rev=made href="mailto:bug-ncurses@gnu.org">\' >> subst.sed
	@echo '<meta http-equiv="Content-Type" content="text\/html; charset=iso-8859-1">' >> subst.sed
	@rm -f subst.tmp
	@for f in man/*.[0-9]* ; do \
	   m=`basename $$f` ;\
	   T=`egrep '^.TH' $$f|sed -e 's/^.TH //' -e s'/"//g' -e 's/[ 	]\+$$//'` ; \
	   g=$${m}.html ;\
	   if [ -f doc/html/$$g ]; then chmod +w doc/html/$$g; fi;\
	   echo "Converting $$m to HTML" ;\
	   echo '<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01//EN">' > doc/html/man/$$g ;\
	   echo '<!-- ' >> doc/html/man/$$g ;\
	   egrep '^.\\"[^#]' $$f | \
	   	sed	-e 's/\$$/@/g' \
			-e 's/^.../  */' \
			-e 's/</\&lt;/g' \
			-e 's/>/\&gt;/g' \
	   >> doc/html/man/$$g ;\
	   echo '-->' >> doc/html/man/$$g ;\
	   ./edit_man.sh normal editing /usr/man man $$f | \
		   $(MANPROG) | \
		   tr '\255' '-' | \
		   $(MAN2HTML) \
		   	-title "$$T" \
			-aliases man/manhtml.aliases \
			-externs man/manhtml.externs | \
		   sed -f subst.sed |\
		   sed -e 's/"curses.3x.html"/"ncurses.3x.html"/g' \
	   >> doc/html/man/$$g ;\
	done
	@rm -f subst.sed

#
# Please note that this target can only be properly built if the build of the
# Ada95 subdir has been done.  The reason is, that the gnathtml tool uses the
# .ali files generated by the Ada95 compiler during the build process.  These
# .ali files contain cross referencing information required by gnathtml.
adahtml:
	if [ ! -z "$(GNATHTML)" ]; then \
	  (cd ./Ada95/gen ; make html GNATHTML=$(GNATHTML) ) ;\
	fi

# This only works on a clean source tree, of course.
MANIFEST:
	-rm -f $@
	touch $@
	find . -type f -print |misc/csort | fgrep -v .lsm |fgrep -v .spec >$@

TAGS:
	etags */*.[ch]

# Makefile ends here
