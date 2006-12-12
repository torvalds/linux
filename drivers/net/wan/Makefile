#
# Makefile for the Linux network (wan) device drivers.
#
# 3 Aug 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#

cyclomx-y                       := cycx_main.o
cyclomx-$(CONFIG_CYCLOMX_X25)	+= cycx_x25.o
cyclomx-objs			:= $(cyclomx-y)  

obj-$(CONFIG_HDLC)		+= hdlc.o
obj-$(CONFIG_HDLC_RAW)		+= hdlc_raw.o
obj-$(CONFIG_HDLC_RAW_ETH)	+= hdlc_raw_eth.o
obj-$(CONFIG_HDLC_CISCO)	+= hdlc_cisco.o
obj-$(CONFIG_HDLC_FR)		+= hdlc_fr.o
obj-$(CONFIG_HDLC_PPP)		+= hdlc_ppp.o	syncppp.o
obj-$(CONFIG_HDLC_X25)		+= hdlc_x25.o

pc300-y				:= pc300_drv.o
pc300-$(CONFIG_PC300_MLPPP)	+= pc300_tty.o
pc300-objs			:= $(pc300-y)

obj-$(CONFIG_HOSTESS_SV11)	+= z85230.o	syncppp.o	hostess_sv11.o
obj-$(CONFIG_SEALEVEL_4021)	+= z85230.o	syncppp.o	sealevel.o
obj-$(CONFIG_COSA)		+=		syncppp.o	cosa.o
obj-$(CONFIG_FARSYNC)		+=		syncppp.o	farsync.o
obj-$(CONFIG_DSCC4)             +=				dscc4.o
obj-$(CONFIG_LANMEDIA)		+=		syncppp.o
obj-$(CONFIG_X25_ASY)		+= x25_asy.o

obj-$(CONFIG_LANMEDIA)		+= lmc/

obj-$(CONFIG_DLCI)		+= dlci.o 
obj-$(CONFIG_SDLA)		+= sdla.o
obj-$(CONFIG_CYCLADES_SYNC)	+= cycx_drv.o cyclomx.o
obj-$(CONFIG_LAPBETHER)		+= lapbether.o
obj-$(CONFIG_SBNI)		+= sbni.o
obj-$(CONFIG_PC300)		+= pc300.o
obj-$(CONFIG_N2)		+= n2.o
obj-$(CONFIG_C101)		+= c101.o
obj-$(CONFIG_WANXL)		+= wanxl.o
obj-$(CONFIG_PCI200SYN)		+= pci200syn.o

clean-files := wanxlfw.inc
$(obj)/wanxl.o:	$(obj)/wanxlfw.inc

ifeq ($(CONFIG_WANXL_BUILD_FIRMWARE),y)
ifeq ($(ARCH),m68k)
  AS68K = $(AS)
  LD68K = $(LD)
else
  AS68K = as68k
  LD68K = ld68k
endif

quiet_cmd_build_wanxlfw = BLD FW  $@
      cmd_build_wanxlfw = \
	$(CPP) -Wp,-MD,$(depfile) -I$(srctree)/include $< | $(AS68K) -m68360 -o $(obj)/wanxlfw.o; \
	$(LD68K) --oformat binary -Ttext 0x1000 $(obj)/wanxlfw.o -o $(obj)/wanxlfw.bin; \
	hexdump -ve '"\n" 16/1 "0x%02X,"' $(obj)/wanxlfw.bin | sed 's/0x  ,//g;1s/^/static u8 firmware[]={/;$$s/,$$/\n};\n/' >$(obj)/wanxlfw.inc; \
	rm -f $(obj)/wanxlfw.bin $(obj)/wanxlfw.o

$(obj)/wanxlfw.inc:	$(src)/wanxlfw.S
	$(call if_changed_dep,build_wanxlfw)
targets += wanxlfw.inc
endif
