# Target: OpenBSD/amd64
TDEPFILES= amd64-tdep.o amd64obsd-tdep.o \
	i386-tdep.o i387-tdep.o i386bsd-tdep.o \
	corelow.o solib.o solib-svr4.o
TM_FILE= tm-nbsd.h
