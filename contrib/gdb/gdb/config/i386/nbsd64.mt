# Target: NetBSD/amd64
TDEPFILES= amd64-tdep.o amd64nbsd-tdep.o i386-tdep.o i387-tdep.o nbsd-tdep.o \
	corelow.o solib.o solib-svr4.o
TM_FILE= tm-nbsd.h
