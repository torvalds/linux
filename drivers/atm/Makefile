#
# Makefile for the Linux network (ATM) device drivers.
#

fore_200e-objs	:= fore200e.o
hostprogs-y	:= fore200e_mkfirm

# Files generated that shall be removed upon make clean
clean-files := atmsar11.bin atmsar11.bin1 atmsar11.bin2 pca200e.bin \
	pca200e.bin1 pca200e.bin2 pca200e_ecd.bin pca200e_ecd.bin1 \
	pca200e_ecd.bin2 sba200e_ecd.bin sba200e_ecd.bin1 sba200e_ecd.bin2
# Firmware generated that shall be removed upon make clean
clean-files += fore200e_pca_fw.c fore200e_sba_fw.c

obj-$(CONFIG_ATM_ZATM)		+= zatm.o uPD98402.o
obj-$(CONFIG_ATM_NICSTAR)	+= nicstar.o
obj-$(CONFIG_ATM_AMBASSADOR)	+= ambassador.o
obj-$(CONFIG_ATM_HORIZON)	+= horizon.o
obj-$(CONFIG_ATM_IA)		+= iphase.o suni.o
obj-$(CONFIG_ATM_FORE200E)	+= fore_200e.o
obj-$(CONFIG_ATM_ENI)		+= eni.o suni.o
obj-$(CONFIG_ATM_IDT77252)	+= idt77252.o

ifeq ($(CONFIG_ATM_NICSTAR_USE_SUNI),y)
  obj-$(CONFIG_ATM_NICSTAR)	+= suni.o
endif
ifeq ($(CONFIG_ATM_NICSTAR_USE_IDT77105),y)
  obj-$(CONFIG_ATM_NICSTAR)	+= idt77105.o
endif
ifeq ($(CONFIG_ATM_IDT77252_USE_SUNI),y)
  obj-$(CONFIG_ATM_IDT77252)	+= suni.o
endif

obj-$(CONFIG_ATM_TCP)		+= atmtcp.o
obj-$(CONFIG_ATM_FIRESTREAM)	+= firestream.o
obj-$(CONFIG_ATM_LANAI)		+= lanai.o

ifeq ($(CONFIG_ATM_FORE200E_PCA),y)
  fore_200e-objs		+= fore200e_pca_fw.o
  # guess the target endianess to choose the right PCA-200E firmware image
  ifeq ($(CONFIG_ATM_FORE200E_PCA_DEFAULT_FW),y)
    byteorder.h			:= include$(if $(patsubst $(srctree),,$(objtree)),2)/asm/byteorder.h
    CONFIG_ATM_FORE200E_PCA_FW	:= $(obj)/pca200e$(if $(shell $(CC) -E -dM $(byteorder.h) | grep ' __LITTLE_ENDIAN '),.bin,_ecd.bin2)
  endif
endif

ifeq ($(CONFIG_ATM_FORE200E_SBA),y)
  fore_200e-objs		+= fore200e_sba_fw.o
  ifeq ($(CONFIG_ATM_FORE200E_SBA_DEFAULT_FW),y)
    CONFIG_ATM_FORE200E_SBA_FW	:= $(obj)/sba200e_ecd.bin2
  endif
endif
obj-$(CONFIG_ATM_HE)		+= he.o
ifeq ($(CONFIG_ATM_HE_USE_SUNI),y)
  obj-$(CONFIG_ATM_HE)		+= suni.o
endif

# FORE Systems 200E-series firmware magic
$(obj)/fore200e_pca_fw.c: $(patsubst "%", %, $(CONFIG_ATM_FORE200E_PCA_FW)) \
			  $(obj)/fore200e_mkfirm
	$(obj)/fore200e_mkfirm -k -b _fore200e_pca_fw \
	  -i $(CONFIG_ATM_FORE200E_PCA_FW) -o $@

$(obj)/fore200e_sba_fw.c: $(patsubst "%", %, $(CONFIG_ATM_FORE200E_SBA_FW)) \
			  $(obj)/fore200e_mkfirm
	$(obj)/fore200e_mkfirm -k -b _fore200e_sba_fw \
	  -i $(CONFIG_ATM_FORE200E_SBA_FW) -o $@

# deal with the various suffixes of the binary firmware images
$(obj)/%.bin $(obj)/%.bin1 $(obj)/%.bin2: $(src)/%.data
	objcopy -Iihex $< -Obinary $@.gz
	gzip -n -df $@.gz
