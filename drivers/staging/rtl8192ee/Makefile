
PCI_MAIN_OBJS	:= base.o	\
		cam.o	\
		core.o	\
		debug.o	\
		efuse.o	\
		pci.o	\
		ps.o	\
		rc.o	\
		regd.o	\
		stats.o

PCI_8192EE_HAL_OBJS:=		\
	rtl8192ee/dm.o		\
	rtl8192ee/fw.o		\
	rtl8192ee/hw.o		\
	rtl8192ee/led.o		\
	rtl8192ee/phy.o		\
	rtl8192ee/pwrseq.o	\
	rtl8192ee/pwrseqcmd.o	\
	rtl8192ee/rf.o		\
	rtl8192ee/sw.o		\
	rtl8192ee/table.o	\
	rtl8192ee/trx.o



BT_COEXIST_OBJS:=	btcoexist/halbtc8192e2ant.o\
			btcoexist/halbtc8723b1ant.o\
			btcoexist/halbtc8723b2ant.o\
			btcoexist/halbtc8821a1ant.o\
			btcoexist/halbtc8821a2ant.o\
			btcoexist/halbtcoutsrc.o\
			btcoexist/rtl_btc.o

r8192ee-objs += $(PCI_MAIN_OBJS) $(PCI_8192EE_HAL_OBJS) $(BT_COEXIST_OBJS)

obj-$(CONFIG_R8192EE) += r8192ee.o

ccflags-y += -D__CHECK_ENDIAN__
