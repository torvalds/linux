# Target: MIPS running NetBSD
TDEPFILES= mips-tdep.o mipsnbsd-tdep.o corelow.o solib.o solib-svr4.o \
	nbsd-tdep.o
TM_FILE= tm-nbsd.h

SIM_OBS = remote-sim.o
SIM = ../sim/mips/libsim.a
