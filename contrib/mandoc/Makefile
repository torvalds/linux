# $Id: Makefile,v 1.519 2018/07/31 15:34:00 schwarze Exp $
#
# Copyright (c) 2010, 2011, 2012 Kristaps Dzonsons <kristaps@bsd.lv>
# Copyright (c) 2011, 2013-2018 Ingo Schwarze <schwarze@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

VERSION = 1.14.4

# === LIST OF FILES ====================================================

TESTSRCS	 = test-be32toh.c \
		   test-cmsg.c \
		   test-dirent-namlen.c \
		   test-EFTYPE.c \
		   test-err.c \
		   test-fts.c \
		   test-getline.c \
		   test-getsubopt.c \
		   test-isblank.c \
		   test-mkdtemp.c \
		   test-nanosleep.c \
		   test-noop.c \
		   test-ntohl.c \
		   test-O_DIRECTORY.c \
		   test-ohash.c \
		   test-PATH_MAX.c \
		   test-pledge.c \
		   test-progname.c \
		   test-recvmsg.c \
		   test-reallocarray.c \
		   test-recallocarray.c \
		   test-rewb-bsd.c \
		   test-rewb-sysv.c \
		   test-sandbox_init.c \
		   test-strcasestr.c \
		   test-stringlist.c \
		   test-strlcat.c \
		   test-strlcpy.c \
		   test-strndup.c \
		   test-strptime.c \
		   test-strsep.c \
		   test-strtonum.c \
		   test-vasprintf.c \
		   test-wchar.c

SRCS		 = att.c \
		   catman.c \
		   cgi.c \
		   chars.c \
		   compat_err.c \
		   compat_fts.c \
		   compat_getline.c \
		   compat_getsubopt.c \
		   compat_isblank.c \
		   compat_mkdtemp.c \
		   compat_ohash.c \
		   compat_progname.c \
		   compat_reallocarray.c \
		   compat_recallocarray.c \
		   compat_strcasestr.c \
		   compat_stringlist.c \
		   compat_strlcat.c \
		   compat_strlcpy.c \
		   compat_strndup.c \
		   compat_strsep.c \
		   compat_strtonum.c \
		   compat_vasprintf.c \
		   dba.c \
		   dba_array.c \
		   dba_read.c \
		   dba_write.c \
		   dbm.c \
		   dbm_map.c \
		   demandoc.c \
		   eqn.c \
		   eqn_html.c \
		   eqn_term.c \
		   html.c \
		   lib.c \
		   main.c \
		   man.c \
		   man_html.c \
		   man_macro.c \
		   man_term.c \
		   man_validate.c \
		   mandoc.c \
		   mandoc_aux.c \
		   mandoc_ohash.c \
		   mandoc_xr.c \
		   mandocd.c \
		   mandocdb.c \
		   manpath.c \
		   mansearch.c \
		   mdoc.c \
		   mdoc_argv.c \
		   mdoc_html.c \
		   mdoc_macro.c \
		   mdoc_man.c \
		   mdoc_markdown.c \
		   mdoc_state.c \
		   mdoc_term.c \
		   mdoc_validate.c \
		   msec.c \
		   out.c \
		   preconv.c \
		   read.c \
		   roff.c \
		   roff_html.c \
		   roff_term.c \
		   roff_validate.c \
		   soelim.c \
		   st.c \
		   tag.c \
		   tbl.c \
		   tbl_data.c \
		   tbl_html.c \
		   tbl_layout.c \
		   tbl_opts.c \
		   tbl_term.c \
		   term.c \
		   term_ascii.c \
		   term_ps.c \
		   term_tab.c \
		   tree.c

DISTFILES	 = INSTALL \
		   LICENSE \
		   Makefile \
		   Makefile.depend \
		   NEWS \
		   TODO \
		   apropos.1 \
		   catman.8 \
		   cgi.h.example \
		   compat_fts.h \
		   compat_ohash.h \
		   compat_stringlist.h \
		   configure \
		   configure.local.example \
		   dba.h \
		   dba_array.h \
		   dba_write.h \
		   dbm.h \
		   dbm_map.h \
		   demandoc.1 \
		   eqn.7 \
		   gmdiff \
		   html.h \
		   lib.in \
		   libman.h \
		   libmandoc.h \
		   libmdoc.h \
		   libroff.h \
		   main.h \
		   makewhatis.8 \
		   man.1 \
		   man.7 \
		   man.cgi.3 \
		   man.cgi.8 \
		   man.conf.5 \
		   man.h \
		   man.options.1 \
		   manconf.h \
		   mandoc.1 \
		   mandoc.3 \
		   mandoc.css \
		   mandoc.db.5 \
		   mandoc.h \
		   mandoc_aux.h \
		   mandoc_char.7 \
		   mandoc_escape.3 \
		   mandoc_headers.3 \
		   mandoc_html.3 \
		   mandoc_malloc.3 \
		   mandoc_ohash.h \
		   mandoc_xr.h \
		   mandocd.8 \
		   mansearch.3 \
		   mansearch.h \
		   mchars_alloc.3 \
		   mdoc.7 \
		   mdoc.h \
		   msec.in \
		   out.h \
		   predefs.in \
		   roff.7 \
		   roff.h \
		   roff_int.h \
		   soelim.1 \
		   st.in \
		   tag.h \
		   tbl.3 \
		   tbl.7 \
		   term.h \
		   $(SRCS) \
		   $(TESTSRCS)

LIBMAN_OBJS	 = man.o \
		   man_macro.o \
		   man_validate.o

LIBMDOC_OBJS	 = att.o \
		   lib.o \
		   mdoc.o \
		   mdoc_argv.o \
		   mdoc_macro.o \
		   mdoc_state.o \
		   mdoc_validate.o \
		   st.o

LIBROFF_OBJS	 = eqn.o \
		   roff.o \
		   roff_validate.o \
		   tbl.o \
		   tbl_data.o \
		   tbl_layout.o \
		   tbl_opts.o

LIBMANDOC_OBJS	 = $(LIBMAN_OBJS) \
		   $(LIBMDOC_OBJS) \
		   $(LIBROFF_OBJS) \
		   chars.o \
		   mandoc.o \
		   mandoc_aux.o \
		   mandoc_ohash.o \
		   mandoc_xr.o \
		   msec.o \
		   preconv.o \
		   read.o

COMPAT_OBJS	 = compat_err.o \
		   compat_fts.o \
		   compat_getline.o \
		   compat_getsubopt.o \
		   compat_isblank.o \
		   compat_mkdtemp.o \
		   compat_ohash.o \
		   compat_progname.o \
		   compat_reallocarray.o \
		   compat_recallocarray.o \
		   compat_strcasestr.o \
		   compat_strlcat.o \
		   compat_strlcpy.o \
		   compat_strndup.o \
		   compat_strsep.o \
		   compat_strtonum.o \
		   compat_vasprintf.o

MANDOC_HTML_OBJS = eqn_html.o \
		   html.o \
		   man_html.o \
		   mdoc_html.o \
		   roff_html.o \
		   tbl_html.o

MANDOC_TERM_OBJS = eqn_term.o \
		   man_term.o \
		   mdoc_term.o \
		   roff_term.o \
		   term.o \
		   term_ascii.o \
		   term_ps.o \
		   term_tab.o \
		   tbl_term.o

DBM_OBJS	 = dbm.o \
		   dbm_map.o \
		   mansearch.o

DBA_OBJS	 = dba.o \
		   dba_array.o \
		   dba_read.o \
		   dba_write.o \
		   mandocdb.o

MAIN_OBJS	 = $(MANDOC_HTML_OBJS) \
		   $(MANDOC_MAN_OBJS) \
		   $(MANDOC_TERM_OBJS) \
		   $(DBM_OBJS) \
		   $(DBA_OBJS) \
		   main.o \
		   manpath.o \
		   mdoc_man.o \
		   mdoc_markdown.o \
		   out.o \
		   tag.o \
		   tree.o

CGI_OBJS	 = $(MANDOC_HTML_OBJS) \
		   $(DBM_OBJS) \
		   cgi.o \
		   out.o

MANDOCD_OBJS	 = $(MANDOC_HTML_OBJS) \
		   $(MANDOC_TERM_OBJS) \
		   mandocd.o \
		   out.o \
		   tag.o

DEMANDOC_OBJS	 = demandoc.o

SOELIM_OBJS	 = soelim.o \
		   compat_err.o \
		   compat_getline.o \
		   compat_progname.o \
		   compat_reallocarray.o \
		   compat_stringlist.o

WWW_MANS	 = apropos.1.html \
		   demandoc.1.html \
		   man.1.html \
		   mandoc.1.html \
		   soelim.1.html \
		   man.cgi.3.html \
		   mandoc.3.html \
		   mandoc_escape.3.html \
		   mandoc_headers.3.html \
		   mandoc_html.3.html \
		   mandoc_malloc.3.html \
		   mansearch.3.html \
		   mchars_alloc.3.html \
		   tbl.3.html \
		   man.conf.5.html \
		   mandoc.db.5.html \
		   eqn.7.html \
		   man.7.html \
		   mandoc_char.7.html \
		   mandocd.8.html \
		   mdoc.7.html \
		   roff.7.html \
		   tbl.7.html \
		   catman.8.html \
		   makewhatis.8.html \
		   man.cgi.8.html \
		   man.h.html \
		   manconf.h.html \
		   mandoc.h.html \
		   mandoc_aux.h.html \
		   mansearch.h.html \
		   mdoc.h.html \
		   roff.h.html

# === USER CONFIGURATION ===============================================

include Makefile.local

# === DEPENDENCY HANDLING ==============================================

all: mandoc demandoc soelim $(BUILD_TARGETS) Makefile.local

install: base-install $(INSTALL_TARGETS)

www: $(WWW_MANS)

$(WWW_MANS): mandoc

.PHONY: base-install cgi-install install www-install
.PHONY: clean distclean depend

include Makefile.depend

# === TARGETS CONTAINING SHELL COMMANDS ================================

distclean: clean
	rm -f Makefile.local config.h config.h.old config.log config.log.old

clean:
	rm -f libmandoc.a $(LIBMANDOC_OBJS) $(COMPAT_OBJS)
	rm -f mandoc $(MAIN_OBJS)
	rm -f man.cgi $(CGI_OBJS)
	rm -f mandocd catman catman.o $(MANDOCD_OBJS)
	rm -f demandoc $(DEMANDOC_OBJS)
	rm -f soelim $(SOELIM_OBJS)
	rm -f $(WWW_MANS) mandoc.tar.gz mandoc.sha256
	rm -rf *.dSYM

base-install: mandoc demandoc soelim
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(SBINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(MANDIR)/man5
	mkdir -p $(DESTDIR)$(MANDIR)/man7
	mkdir -p $(DESTDIR)$(MANDIR)/man8
	$(INSTALL_PROGRAM) mandoc demandoc $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) soelim $(DESTDIR)$(BINDIR)/$(BINM_SOELIM)
	cd $(DESTDIR)$(BINDIR) && $(LN) mandoc $(BINM_MAN)
	cd $(DESTDIR)$(BINDIR) && $(LN) mandoc $(BINM_APROPOS)
	cd $(DESTDIR)$(BINDIR) && $(LN) mandoc $(BINM_WHATIS)
	cd $(DESTDIR)$(SBINDIR) && \
		$(LN) ${BIN_FROM_SBIN}/mandoc $(BINM_MAKEWHATIS)
	$(INSTALL_MAN) mandoc.1 demandoc.1 $(DESTDIR)$(MANDIR)/man1
	$(INSTALL_MAN) soelim.1 $(DESTDIR)$(MANDIR)/man1/$(BINM_SOELIM).1
	$(INSTALL_MAN) man.1 $(DESTDIR)$(MANDIR)/man1/$(BINM_MAN).1
	$(INSTALL_MAN) apropos.1 $(DESTDIR)$(MANDIR)/man1/$(BINM_APROPOS).1
	cd $(DESTDIR)$(MANDIR)/man1 && $(LN) $(BINM_APROPOS).1 $(BINM_WHATIS).1
	$(INSTALL_MAN) man.conf.5 $(DESTDIR)$(MANDIR)/man5/$(MANM_MANCONF).5
	$(INSTALL_MAN) mandoc.db.5 $(DESTDIR)$(MANDIR)/man5
	$(INSTALL_MAN) man.7 $(DESTDIR)$(MANDIR)/man7/$(MANM_MAN).7
	$(INSTALL_MAN) mdoc.7 $(DESTDIR)$(MANDIR)/man7/$(MANM_MDOC).7
	$(INSTALL_MAN) roff.7 $(DESTDIR)$(MANDIR)/man7/$(MANM_ROFF).7
	$(INSTALL_MAN) eqn.7 $(DESTDIR)$(MANDIR)/man7/$(MANM_EQN).7
	$(INSTALL_MAN) tbl.7 $(DESTDIR)$(MANDIR)/man7/$(MANM_TBL).7
	$(INSTALL_MAN) mandoc_char.7 $(DESTDIR)$(MANDIR)/man7
	$(INSTALL_MAN) makewhatis.8 \
		$(DESTDIR)$(MANDIR)/man8/$(BINM_MAKEWHATIS).8

lib-install: libmandoc.a
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	$(INSTALL_LIB) libmandoc.a $(DESTDIR)$(LIBDIR)
	$(INSTALL_LIB) man.h mandoc.h mandoc_aux.h mdoc.h roff.h \
		$(DESTDIR)$(INCLUDEDIR)
	$(INSTALL_MAN) mandoc.3 mandoc_escape.3 mandoc_malloc.3 \
		mansearch.3 mchars_alloc.3 tbl.3 $(DESTDIR)$(MANDIR)/man3

cgi-install: man.cgi
	mkdir -p $(DESTDIR)$(CGIBINDIR)
	mkdir -p $(DESTDIR)$(HTDOCDIR)
	$(INSTALL_PROGRAM) man.cgi $(DESTDIR)$(CGIBINDIR)
	$(INSTALL_DATA) mandoc.css $(DESTDIR)$(HTDOCDIR)

catman-install: mandocd catman
	mkdir -p $(DESTDIR)$(SBINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man8
	$(INSTALL_PROGRAM) mandocd $(DESTDIR)$(SBINDIR)
	$(INSTALL_PROGRAM) catman $(DESTDIR)$(SBINDIR)/$(BINM_CATMAN)
	$(INSTALL_MAN) mandocd.8 $(DESTDIR)$(MANDIR)/man8
	$(INSTALL_MAN) catman.8 $(DESTDIR)$(MANDIR)/man8/$(BINM_CATMAN).8

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/mandoc
	rm -f $(DESTDIR)$(BINDIR)/demandoc
	rm -f $(DESTDIR)$(BINDIR)/$(BINM_SOELIM)
	rm -f $(DESTDIR)$(BINDIR)/$(BINM_MAN)
	rm -f $(DESTDIR)$(BINDIR)/$(BINM_APROPOS)
	rm -f $(DESTDIR)$(BINDIR)/$(BINM_WHATIS)
	rm -f $(DESTDIR)$(SBINDIR)/$(BINM_MAKEWHATIS)
	rm -f $(DESTDIR)$(MANDIR)/man1/mandoc.1
	rm -f $(DESTDIR)$(MANDIR)/man1/demandoc.1
	rm -f $(DESTDIR)$(MANDIR)/man1/$(BINM_SOELIM).1
	rm -f $(DESTDIR)$(MANDIR)/man1/$(BINM_MAN).1
	rm -f $(DESTDIR)$(MANDIR)/man1/$(BINM_APROPOS).1
	rm -f $(DESTDIR)$(MANDIR)/man1/$(BINM_WHATIS).1
	rm -f $(DESTDIR)$(MANDIR)/man5/$(MANM_MANCONF).5
	rm -f $(DESTDIR)$(MANDIR)/man5/mandoc.db.5
	rm -f $(DESTDIR)$(MANDIR)/man7/$(MANM_MAN).7
	rm -f $(DESTDIR)$(MANDIR)/man7/$(MANM_MDOC).7
	rm -f $(DESTDIR)$(MANDIR)/man7/$(MANM_ROFF).7
	rm -f $(DESTDIR)$(MANDIR)/man7/$(MANM_EQN).7
	rm -f $(DESTDIR)$(MANDIR)/man7/$(MANM_TBL).7
	rm -f $(DESTDIR)$(MANDIR)/man7/mandoc_char.7
	rm -f $(DESTDIR)$(MANDIR)/man8/$(BINM_MAKEWHATIS).8
	rm -f $(DESTDIR)$(CGIBINDIR)/man.cgi
	rm -f $(DESTDIR)$(HTDOCDIR)/mandoc.css
	rm -f $(DESTDIR)$(SBINDIR)/mandocd
	rm -f $(DESTDIR)$(SBINDIR)/$(BINM_CATMAN)
	rm -f $(DESTDIR)$(MANDIR)/man8/mandocd.8
	rm -f $(DESTDIR)$(MANDIR)/man8/$(BINM_CATMAN).8
	rm -f $(DESTDIR)$(LIBDIR)/libmandoc.a
	rm -f $(DESTDIR)$(MANDIR)/man3/mandoc.3
	rm -f $(DESTDIR)$(MANDIR)/man3/mandoc_escape.3
	rm -f $(DESTDIR)$(MANDIR)/man3/mandoc_malloc.3
	rm -f $(DESTDIR)$(MANDIR)/man3/mansearch.3
	rm -f $(DESTDIR)$(MANDIR)/man3/mchars_alloc.3
	rm -f $(DESTDIR)$(MANDIR)/man3/tbl.3
	rm -f $(DESTDIR)$(INCLUDEDIR)/man.h
	rm -f $(DESTDIR)$(INCLUDEDIR)/mandoc.h
	rm -f $(DESTDIR)$(INCLUDEDIR)/mandoc_aux.h
	rm -f $(DESTDIR)$(INCLUDEDIR)/mdoc.h
	rm -f $(DESTDIR)$(INCLUDEDIR)/roff.h
	[ ! -e $(DESTDIR)$(INCLUDEDIR) ] || rmdir $(DESTDIR)$(INCLUDEDIR)

regress: all
	cd regress && ./regress.pl

regress-clean:
	cd regress && ./regress.pl . clean

Makefile.local config.h: configure $(TESTSRCS)
	@echo "$@ is out of date; please run ./configure"
	@exit 1

libmandoc.a: $(COMPAT_OBJS) $(LIBMANDOC_OBJS)
	ar rs $@ $(COMPAT_OBJS) $(LIBMANDOC_OBJS)

mandoc: $(MAIN_OBJS) libmandoc.a
	$(CC) -o $@ $(LDFLAGS) $(MAIN_OBJS) libmandoc.a $(LDADD)

man.cgi: $(CGI_OBJS) libmandoc.a
	$(CC) $(STATIC) -o $@ $(LDFLAGS) $(CGI_OBJS) libmandoc.a $(LDADD)

mandocd: $(MANDOCD_OBJS) libmandoc.a
	$(CC) -o $@ $(LDFLAGS) $(MANDOCD_OBJS) libmandoc.a $(LDADD)

catman: catman.o libmandoc.a
	$(CC) -o $@ $(LDFLAGS) catman.o libmandoc.a $(LDADD)

demandoc: $(DEMANDOC_OBJS) libmandoc.a
	$(CC) -o $@ $(LDFLAGS) $(DEMANDOC_OBJS) libmandoc.a $(LDADD)

soelim: $(SOELIM_OBJS)
	$(CC) -o $@ $(LDFLAGS) $(SOELIM_OBJS)

# --- maintainer targets ---

www-install: www
	$(INSTALL_DATA) $(WWW_MANS) mandoc.css $(HTDOCDIR)

depend: config.h
	mkdep -f Makefile.depend $(CFLAGS) $(SRCS)
	perl -e 'undef $$/; $$_ = <>; s|/usr/include/\S+||g; \
		s|\\\n||g; s|  +| |g; s| $$||mg; print;' \
		Makefile.depend > Makefile.tmp
	mv Makefile.tmp Makefile.depend

regress-distclean:
	@find regress \
		-name '.#*' -o \
		-name '*.orig' -o \
		-name '*.rej' -o \
		-name '*.core' \
		-exec rm -i {} \;

regress-distcheck:
	@find regress ! -type d ! -type f
	@find regress -type f \
		! -path '*/CVS/*' \
		! -name Makefile \
		! -name Makefile.inc \
		! -name '*.in' \
		! -name '*.out_ascii' \
		! -name '*.out_utf8' \
		! -name '*.out_html' \
		! -name '*.out_markdown' \
		! -name '*.out_lint' \
		! -path regress/regress.pl \
		! -path regress/regress.pl.1

dist: mandoc-$(VERSION).sha256

mandoc-$(VERSION).sha256: mandoc-$(VERSION).tar.gz
	sha256 mandoc-$(VERSION).tar.gz > $@

mandoc-$(VERSION).tar.gz: $(DISTFILES)
	ls regress/*/*/*.mandoc_* && exit 1 || true
	mkdir -p .dist/mandoc-$(VERSION)/
	$(INSTALL) -m 0644 $(DISTFILES) .dist/mandoc-$(VERSION)
	cp -pR regress .dist/mandoc-$(VERSION)
	find .dist/mandoc-$(VERSION)/regress \
	    -type d -name CVS -print0 | xargs -0 rm -rf
	chmod 755 .dist/mandoc-$(VERSION)/configure
	( cd .dist/ && tar zcf ../$@ mandoc-$(VERSION) )
	rm -rf .dist/

# === SUFFIX RULES =====================================================

.SUFFIXES:	 .1       .3       .5       .7       .8       .h
.SUFFIXES:	 .1.html  .3.html  .5.html  .7.html  .8.html  .h.html

.h.h.html:
	highlight -I $< > $@

.1.1.html .3.3.html .5.5.html .7.7.html .8.8.html: mandoc
	./mandoc -Thtml -Wall,stop \
		-Ostyle=mandoc.css,man=%N.%S.html,includes=%I.html $< > $@
