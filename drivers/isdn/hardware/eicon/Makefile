# Makefile for the Eicon DIVA ISDN drivers.

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DIVAS)		+= divadidd.o divas.o
obj-$(CONFIG_ISDN_DIVAS_MAINT)		+= diva_mnt.o
obj-$(CONFIG_ISDN_DIVAS_USERIDI)	+= diva_idi.o
obj-$(CONFIG_ISDN_DIVAS_DIVACAPI)	+= divacapi.o

# Multipart objects. 

divas-y					:= divasmain.o divasfunc.o di.o io.o istream.o \
					   diva.o divasproc.o diva_dma.o
divas-$(CONFIG_ISDN_DIVAS_BRIPCI)	+= os_bri.o  s_bri.o os_4bri.o s_4bri.o
divas-$(CONFIG_ISDN_DIVAS_PRIPCI)	+= os_pri.o  s_pri.o

divacapi-y				:= capimain.o capifunc.o message.o capidtmf.o

divadidd-y				:= diva_didd.o diddfunc.o dadapter.o

diva_mnt-y				:= divamnt.o mntfunc.o debug.o maintidi.o

diva_idi-y				:= divasi.o idifunc.o um_idi.o dqueue.o
