MAGIC	 = $(pkgdatadir)/magic
if FSECT5
man_MAGIC = magic.5
else
man_MAGIC = magic.4
endif
fsect = @fsect@
man_MANS = file.1 $(man_MAGIC) libmagic.3

EXTRA_DIST = file.man magic.man libmagic.man
CLEANFILES = $(man_MANS)

file.1:	Makefile file.man
	@rm -f $@
	sed -e s@__CSECTION__@1@g \
	    -e s@__FSECTION__@${fsect}@g \
	    -e s@__VERSION__@${VERSION}@g \
	    -e s@__MAGIC__@${MAGIC}@g $(srcdir)/file.man > $@

magic.${fsect}: Makefile magic.man
	@rm -f $@
	sed -e s@__CSECTION__@1@g \
	    -e s@__FSECTION__@${fsect}@g \
	    -e s@__VERSION__@${VERSION}@g \
	    -e s@__MAGIC__@${MAGIC}@g $(srcdir)/magic.man > $@

libmagic.3: Makefile libmagic.man
	@rm -f $@
	sed -e s@__CSECTION__@1@g \
	    -e s@__FSECTION__@${fsect}@g \
	    -e s@__VERSION__@${VERSION}@g \
	    -e s@__MAGIC__@${MAGIC}@g $(srcdir)/libmagic.man > $@
