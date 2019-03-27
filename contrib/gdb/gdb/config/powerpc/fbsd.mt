# Target: FreeBSD/powerpc
TDEPFILES= rs6000-tdep.o ppc-sysv-tdep.o ppcfbsd-tdep.o \
	   corelow.o solib.o solib-svr4.o
TM_FILE= tm-ppc-eabi.h
