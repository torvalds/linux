# Adaptec aacraid

obj-$(CONFIG_SCSI_AACRAID) := aacraid.o

aacraid-objs	:= linit.o aachba.o commctrl.o comminit.o commsup.o \
		   dpcsup.o rx.o sa.o rkt.o nark.o src.o

ccflags-y	:= -Idrivers/scsi
