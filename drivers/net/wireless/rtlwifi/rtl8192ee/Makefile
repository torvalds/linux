obj-m := rtl8192ee.o


rtl8192ee-objs :=		\
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


obj-$(CONFIG_RTL8821AE) += rtl8192ee.o

ccflags-y += -D__CHECK_ENDIAN__
