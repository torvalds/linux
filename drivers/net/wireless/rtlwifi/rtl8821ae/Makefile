obj-m := rtl8821ae.o


rtl8821ae-objs :=		\
		dm.o		\
		fw.o		\
		hw.o		\
		led.o		\
		phy.o		\
		pwrseq.o	\
		rf.o		\
		sw.o		\
		table.o		\
		trx.o		\


obj-$(CONFIG_RTL8821AE) += rtl8821ae.o

ccflags-y += -D__CHECK_ENDIAN__
