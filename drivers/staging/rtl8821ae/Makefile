PCI_MAIN_OBJS	:= base.o	\
		rc.o	\
		debug.o	\
		regd.o	\
		efuse.o	\
		cam.o	\
		ps.o	\
		core.o	\
		stats.o	\
		pci.o	\

BT_COEXIST_OBJS:=	btcoexist/halbtc8192e2ant.o\
			btcoexist/halbtc8723b1ant.o\
			btcoexist/halbtc8723b2ant.o\
			btcoexist/halbtcoutsrc.o\
			btcoexist/rtl_btc.o	\

PCI_8821AE_HAL_OBJS:=	\
	rtl8821ae/hw.o		\
	rtl8821ae/table.o		\
	rtl8821ae/sw.o		\
	rtl8821ae/trx.o		\
	rtl8821ae/led.o		\
	rtl8821ae/fw.o		\
	rtl8821ae/phy.o		\
	rtl8821ae/rf.o		\
	rtl8821ae/dm.o		\
	rtl8821ae/pwrseq.o	\
	rtl8821ae/pwrseqcmd.o	\
	rtl8821ae/hal_btc.o	\
	rtl8821ae/hal_bt_coexist.o	\

rtl8821ae-objs += $(BT_COEXIST_OBJS) $(PCI_MAIN_OBJS) $(PCI_8821AE_HAL_OBJS)

obj-$(CONFIG_R8821AE) += rtl8821ae.o
