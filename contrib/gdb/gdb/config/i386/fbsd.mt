# Target: FreeBSD/i386
TDEPFILES= i386-tdep.o i387-tdep.o i386bsd-tdep.o i386fbsd-tdep.o \
	corelow.o solib.o solib-svr4.o
TM_FILE= tm-fbsd.h
