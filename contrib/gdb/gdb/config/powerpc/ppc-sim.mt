# Target: PowerPC running eabi and including the simulator
TDEPFILES= rs6000-tdep.o monitor.o dsrec.o ppcbug-rom.o dink32-rom.o ppc-bdm.o ocd.o remote-sds.o ppc-sysv-tdep.o solib.o solib-svr4.o
TM_FILE= tm-ppc-eabi.h

SIM_OBS = remote-sim.o
SIM = ../sim/ppc/libsim.a
