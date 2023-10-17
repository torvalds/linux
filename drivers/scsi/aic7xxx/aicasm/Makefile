# SPDX-License-Identifier: GPL-2.0
PROG=	aicasm

OUTDIR ?= ./

.SUFFIXES= .l .y .c .h

CSRCS=	aicasm.c aicasm_symbol.c
YSRCS=	aicasm_gram.y aicasm_macro_gram.y
LSRCS=	aicasm_scan.l aicasm_macro_scan.l

GENHDRS=	$(addprefix ${OUTDIR}/,aicdb.h $(YSRCS:.y=.h))
GENSRCS=	$(addprefix ${OUTDIR}/,$(YSRCS:.y=.c) $(LSRCS:.l=.c))

SRCS=	${CSRCS} ${GENSRCS}
LIBS=	-ldb
clean-files:= ${GENSRCS} ${GENHDRS} $(YSRCS:.y=.output) $(PROG)
# Override default kernel CFLAGS.  This is a userland app.
AICASM_CFLAGS:= -I/usr/include -I. -I$(OUTDIR)
LEX= flex
YACC= bison
YFLAGS= -d

NOMAN=	noman

ifneq ($(HOSTCC),)
AICASM_CC= $(HOSTCC)
else
AICASM_CC= $(CC)
endif

ifdef DEBUG
CFLAGS+= -DDEBUG -g
YFLAGS+= -t -v
LFLAGS= -d
endif

$(PROG):  $(OUTDIR) ${GENHDRS} $(SRCS)
	$(AICASM_CC) $(AICASM_CFLAGS) $(SRCS) -o $(OUTDIR)/$(PROG) $(LIBS)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/aicdb.h:
	@if [ -e "/usr/include/db4/db_185.h" ]; then		\
		echo "#include <db4/db_185.h>" > $@;	\
	 elif [ -e "/usr/include/db3/db_185.h" ]; then		\
		echo "#include <db3/db_185.h>" > $@;	\
	 elif [ -e "/usr/include/db2/db_185.h" ]; then		\
		echo "#include <db2/db_185.h>" > $@;	\
	 elif [ -e "/usr/include/db1/db_185.h" ]; then		\
		echo "#include <db1/db_185.h>" > $@;	\
	 elif [ -e "/usr/include/db/db_185.h" ]; then		\
		echo "#include <db/db_185.h>" > $@;	\
	 elif [ -e "/usr/include/db_185.h" ]; then		\
		echo "#include <db_185.h>" > $@;		\
	 else							\
		echo "*** Install db development libraries";	\
	 fi

clean:
	rm -f $(clean-files)

$(OUTDIR)/aicasm_gram.c $(OUTDIR)/aicasm_gram.h: aicasm_gram.y
	$(YACC) $(YFLAGS) -b $(<:.y=) $< -o $(OUTDIR)/$(<:.y=.c)

$(OUTDIR)/aicasm_macro_gram.c $(OUTDIR)/aicasm_macro_gram.h: aicasm_macro_gram.y
	$(YACC) $(YFLAGS) -b $(<:.y=) -p mm $< -o $(OUTDIR)/$(<:.y=.c)

$(OUTDIR)/aicasm_scan.c: aicasm_scan.l
	$(LEX) $(LFLAGS) -o $@ $<

$(OUTDIR)/aicasm_macro_scan.c: aicasm_macro_scan.l
	$(LEX) $(LFLAGS) -Pmm -o $@ $<
