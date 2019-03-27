# Target: NetBSD/sparc64
TDEPFILES= sparc64-tdep.o sparc64nbsd-tdep.o \
	sparc-tdep.o sparcnbsd-tdep.o nbsd-tdep.o \
	corelow.o solib.o solib-svr4.o
TM_FILE= tm-nbsd.h
