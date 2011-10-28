obj-$(CONFIG_CXT1E1)	+= cxt1e1.o

ccflags-y := -DSBE_PMCC4_ENABLE
ccflags-y += -DSBE_ISR_TASKLET
ccflags-y += -DSBE_INCLUDE_SYMBOLS

cxt1e1-y := 	\
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
