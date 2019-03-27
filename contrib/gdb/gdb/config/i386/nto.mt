# Target: Intel 386 running qnx6.
TDEPFILES = i386-tdep.o i387-tdep.o corelow.o solib.o solib-svr4.o \
	i386-nto-tdep.o nto-tdep.o remote-nto.o
TM_FILE = tm-nto.h
