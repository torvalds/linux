# Target: NetBSD/sparc
TDEPFILES= sparc-tdep.o sparcnbsd-tdep.o nbsd-tdep.o \
	corelow.o solib.o solib-svr4.o
TM_FILE= tm-nbsd.h
