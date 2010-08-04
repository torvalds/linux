obj-$(CONFIG_CXT1E1)	+= cxt1e1.o

EXTRA_CFLAGS += -DSBE_PMCC4_ENABLE
EXTRA_CFLAGS += -DSBE_ISR_TASKLET
EXTRA_CFLAGS += -DSBE_INCLUDE_SYMBOLS

cxt1e1-objs += 	\
  ossiRelease.o 	\
  musycc.o 		\
  pmcc4_drv.o 		\
  comet.o 		\
  linux.o 		\
  functions.o 		\
  hwprobe.o 		\
  sbeproc.o 		\
  pmc93x6_eeprom.o 	\
  sbecrc.o 		\
  comet_tables.o 	\
  sbeid.o
