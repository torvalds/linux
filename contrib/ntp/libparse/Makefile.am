NULL=
BUILT_SOURCES = $(VPHACK) info_trimble.c $(VPHACK_AFTER)
CLEANFILES =
noinst_LIBRARIES = @MAKE_LIBPARSE@ @MAKE_LIBPARSE_KERNEL@
EXTRA_LIBRARIES = libparse.a libparse_kernel.a
EXTRA_PROGRAMS = parsestreams parsesolaris
noinst_PROGRAMS = @MAKE_PARSEKMODULE@

K_CFLAGS = -DPARSESTREAM -DNTP_NEED_BOPS

# info_trimble.c was mistakenly created in the build directory
# previously.  It is located in $(srcdir) and any updates must
# end up there.

#
# VPHACK and VPHACK_AFTER are enabled on non-GNU makes (such as
# BSD make) to work around build/dependency issues where we want
# built source files to live in srcdir, as opposed to builddir.
#

if VPATH_HACK
VPHACK=	vphack
VPHACK_AFTER= vphack_after
else
VPHACK=
VPHACK_AFTER=
endif

vphack:
	test -e info_trimble.c || ln -s $(srcdir)/info_trimble.c .

vphack_after:
	test -L info_trimble.c || ( \
		mv info_trimble.c $(srcdir)/info_trimble.c && \
		ln -s $(srcdir)/info_trimble.c . \
		)

libparse_a_SOURCES = parse.c \
		     parse_conf.c \
		     clk_meinberg.c \
		     clk_schmid.c \
		     clk_rawdcf.c \
		     clk_trimtsip.c \
		     clk_dcf7000.c \
		     clk_trimtaip.c \
		     clk_rcc8000.c \
		     clk_hopf6021.c \
		     clk_computime.c \
		     clk_wharton.c \
		     clk_varitext.c \
		     clk_sel240x.c \
		     data_mbg.c \
		     info_trimble.c \
		     trim_info.c \
		     binio.c \
		     ieee754io.c \
		     mfp_mul.c \
		     gpstolfp.c \
		     $(NULL)

libparse_kernel_a_SOURCES = 
libparse_kernel_a_LIBADD =	\
	kparse.o		\
	kparse_conf.o		\
	kclk_computime.o	\
	kclk_dcf7000.o		\
	kclk_hopf6021.o		\
	kclk_meinberg.o		\
	kclk_rawdcf.o		\
	kclk_rcc8000.o		\
	kclk_schmid.o		\
	kclk_trimtaip.o		\
	kclk_trimtsip.o		\
	kclk_varitext.o		\
	kclk_wharton.o		\
	kclk_sel240x.o		\
	kbinio.o		\
	kieee754io.o		\
	kmfp_mul.o		\
	kgpstolfp.o		\
	$(NULL)

AM_CFLAGS = $(CFLAGS_NTP)
AM_CFLAGS += $(NTP_HARD_CFLAGS)

AM_CPPFLAGS  = $(NTP_INCS)
AM_CPPFLAGS += -I$(top_srcdir)/kernel
AM_CPPFLAGS += $(CPPFLAGS_NTP)
AM_CPPFLAGS += $(NTP_HARD_CPPFLAGS)

AM_LDFLAGS = $(NTP_HARD_LDFLAGS)

EXTRA_DIST = parsesolaris.c parsestreams.c mkinfo_scmd.sed mkinfo_rcmd.sed info_trimble.c

info_trimble.c: $(top_srcdir)/include/trimble.h $(srcdir)/Makefile.am $(srcdir)/mkinfo_scmd.sed $(srcdir)/mkinfo_rcmd.sed
	sed -n -f $(srcdir)/mkinfo_scmd.sed $(top_srcdir)/include/trimble.h  > info_trimble.new
	sed -n -f $(srcdir)/mkinfo_rcmd.sed $(top_srcdir)/include/trimble.h >> info_trimble.new
	mv -f info_trimble.new $@

#$(srcdir)/info_trimble.c: $(top_srcdir)/include/trimble.h $(srcdir)/Makefile.am $(srcdir)/mkinfo_scmd.sed $(srcdir)/mkinfo_rcmd.sed
#	sed -n -f $(srcdir)/mkinfo_scmd.sed $(top_srcdir)/include/trimble.h  > info_trimble.new
#	sed -n -f $(srcdir)/mkinfo_rcmd.sed $(top_srcdir)/include/trimble.h >> info_trimble.new
#	mv -f info_trimble.new $@

kieee754io.o: $(srcdir)/ieee754io.c
	 $(COMPILE) $(K_CFLAGS) -c $(srcdir)/ieee754io.c -o $@

kmfp_mul.o: $(srcdir)/mfp_mul.c
	 $(COMPILE) $(K_CFLAGS) -c $(srcdir)/mfp_mul.c -o $@

kgpstolfp.o: $(srcdir)/gpstolfp.c
	 $(COMPILE) $(K_CFLAGS) -c $(srcdir)/gpstolfp.c -o $@

kbinio.o: $(srcdir)/binio.c
	 $(COMPILE) $(K_CFLAGS) -c $(srcdir)/binio.c -o $@

kclk_computime.o: $(srcdir)/clk_computime.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_computime.c -o $@

kclk_dcf7000.o: $(srcdir)/clk_dcf7000.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_dcf7000.c -o $@

kclk_hopf6021.o: $(srcdir)/clk_hopf6021.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_hopf6021.c -o $@

kclk_meinberg.o: $(srcdir)/clk_meinberg.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_meinberg.c -o $@

kclk_rawdcf.o: $(srcdir)/clk_rawdcf.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_rawdcf.c -o $@

kclk_rcc8000.o: $(srcdir)/clk_rcc8000.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_rcc8000.c -o $@

kclk_schmid.o: $(srcdir)/clk_schmid.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_schmid.c -o $@

kclk_trimtaip.o: $(srcdir)/clk_trimtaip.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_trimtaip.c -o $@

kclk_trimtsip.o: $(srcdir)/clk_trimtsip.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_trimtsip.c -o $@

kclk_varitext.o: $(srcdir)/clk_varitext.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_varitext.c -o $@

kclk_wharton.o: $(srcdir)/clk_wharton.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_wharton.c -o $@

kclk_sel240x.o: $(srcdir)/clk_sel240x.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/clk_sel240x.c -o $@

kparse.o: $(srcdir)/parse.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/parse.c -o $@

kparse_conf.o: $(srcdir)/parse_conf.c
	$(COMPILE) $(K_CFLAGS) -c $(srcdir)/parse_conf.c -o $@

parsestreams.loadable_module.o: $(parsestreams_OBJECTS) libparse_kernel.a ../libntp/libntp.a
	$(LD) -r -o $@ $(parsestreams_OBJECTS) libparse_kernel.a ../libntp/libntp.a

parse: $(parsesolaris_OBJECTS) libparse_kernel.a ../libntp/libntp.a
	$(LD) -r -o $@ $(parsesolaris_OBJECTS) libparse_kernel.a ../libntp/libntp.a

parsesolaris.o: sys/systm.h

sys/systm.h:
	-mkdir sys
	sed -e '/ffs(.*)/d' < /usr/include/sys/systm.h > sys/systm.h

## check-libparse is invoked by ntpd/Makefile.am
check-libparse: $(noinst_LIBRARIES)
	@: do-nothing action to avoid default SCCS get

include $(top_srcdir)/sntp/check-libntp.mf
include $(top_srcdir)/depsver.mf
include $(top_srcdir)/includes.mf
