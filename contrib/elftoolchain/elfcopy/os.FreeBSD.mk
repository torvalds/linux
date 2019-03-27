.if !defined(LIBELF_AR)
DPADD+= ${LIBBZ2}
LDADD+= -lbz2
.endif
