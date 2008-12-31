#
# Makefile for the various Linux/SuperH timers
#

obj-y	:= timer.o

obj-$(CONFIG_SH_TMU)		+= timer-tmu.o
obj-$(CONFIG_SH_MTU2)		+= timer-mtu2.o
obj-$(CONFIG_SH_CMT)		+= timer-cmt.o

obj-$(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST)	+= timer-broadcast.o
