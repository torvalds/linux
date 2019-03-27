# Target: PowerPC, running NetBSD
TDEPFILES= rs6000-tdep.o ppc-sysv-tdep.o ppcnbsd-tdep.o nbsd-tdep.o corelow.o \
	solib.o solib-svr4.o
TM_FILE= tm-nbsd.h

SIM_OBS = remote-sim.o
SIM = ../sim/ppc/libsim.a
