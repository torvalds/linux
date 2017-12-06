# SPDX-License-Identifier: GPL-2.0
#
# Makefile for linux/drivers/scsi
#
# 30 May 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#
# 20 Sep 2000, Torben Mathiasen <tmm@image.dk>
# Changed link order to reflect new scsi initialization.
#
# *!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!
# The link order must be, SCSI Core, SCSI HBA drivers, and
# lastly SCSI peripheral drivers (disk/tape/cdrom/etc.) to
# satisfy certain initialization assumptions in the SCSI layer.
# *!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!


CFLAGS_aha152x.o =   -DAHA152X_STAT -DAUTOCONF
CFLAGS_gdth.o    = # -DDEBUG_GDTH=2 -D__SERIAL__ -D__COM2__ -DGDTH_STATISTICS

obj-$(CONFIG_PCMCIA)		+= pcmcia/

obj-$(CONFIG_SCSI)		+= scsi_mod.o

obj-$(CONFIG_RAID_ATTRS)	+= raid_class.o

# --- NOTE ORDERING HERE ---
# For kernel non-modular link, transport attributes need to
# be initialised before drivers
# --------------------------
obj-$(CONFIG_SCSI_SPI_ATTRS)	+= scsi_transport_spi.o
obj-$(CONFIG_SCSI_FC_ATTRS) 	+= scsi_transport_fc.o
obj-$(CONFIG_SCSI_ISCSI_ATTRS)	+= scsi_transport_iscsi.o
obj-$(CONFIG_SCSI_SAS_ATTRS)	+= scsi_transport_sas.o
obj-$(CONFIG_SCSI_SAS_LIBSAS)	+= libsas/
obj-$(CONFIG_SCSI_SRP_ATTRS)	+= scsi_transport_srp.o
obj-$(CONFIG_SCSI_DH)		+= device_handler/

obj-$(CONFIG_LIBFC)		+= libfc/
obj-$(CONFIG_LIBFCOE)		+= fcoe/
obj-$(CONFIG_FCOE)		+= fcoe/
obj-$(CONFIG_FCOE_FNIC)		+= fnic/
obj-$(CONFIG_SCSI_SNIC)		+= snic/
obj-$(CONFIG_SCSI_BNX2X_FCOE)	+= libfc/ fcoe/ bnx2fc/
obj-$(CONFIG_QEDF)		+= qedf/
obj-$(CONFIG_ISCSI_TCP) 	+= libiscsi.o	libiscsi_tcp.o iscsi_tcp.o
obj-$(CONFIG_INFINIBAND_ISER) 	+= libiscsi.o
obj-$(CONFIG_ISCSI_BOOT_SYSFS)	+= iscsi_boot_sysfs.o
obj-$(CONFIG_SCSI_A4000T)	+= 53c700.o	a4000t.o
obj-$(CONFIG_SCSI_ZORRO7XX)	+= 53c700.o	zorro7xx.o
obj-$(CONFIG_A3000_SCSI)	+= a3000.o	wd33c93.o
obj-$(CONFIG_A2091_SCSI)	+= a2091.o	wd33c93.o
obj-$(CONFIG_GVP11_SCSI)	+= gvp11.o	wd33c93.o
obj-$(CONFIG_MVME147_SCSI)	+= mvme147.o	wd33c93.o
obj-$(CONFIG_SGIWD93_SCSI)	+= sgiwd93.o	wd33c93.o
obj-$(CONFIG_ATARI_SCSI)	+= atari_scsi.o
obj-$(CONFIG_MAC_SCSI)		+= mac_scsi.o
obj-$(CONFIG_SCSI_MAC_ESP)	+= esp_scsi.o	mac_esp.o
obj-$(CONFIG_SUN3_SCSI)		+= sun3_scsi.o  sun3_scsi_vme.o
obj-$(CONFIG_MVME16x_SCSI)	+= 53c700.o	mvme16x_scsi.o
obj-$(CONFIG_BVME6000_SCSI)	+= 53c700.o	bvme6000_scsi.o
obj-$(CONFIG_SCSI_SIM710)	+= 53c700.o	sim710.o
obj-$(CONFIG_SCSI_ADVANSYS)	+= advansys.o
obj-$(CONFIG_SCSI_BUSLOGIC)	+= BusLogic.o
obj-$(CONFIG_SCSI_DPT_I2O)	+= dpt_i2o.o
obj-$(CONFIG_SCSI_ARCMSR)	+= arcmsr/
obj-$(CONFIG_SCSI_AHA152X)	+= aha152x.o
obj-$(CONFIG_SCSI_AHA1542)	+= aha1542.o
obj-$(CONFIG_SCSI_AHA1740)	+= aha1740.o
obj-$(CONFIG_SCSI_AIC7XXX)	+= aic7xxx/
obj-$(CONFIG_SCSI_AIC79XX)	+= aic7xxx/
obj-$(CONFIG_SCSI_AACRAID)	+= aacraid/
obj-$(CONFIG_SCSI_AIC94XX)	+= aic94xx/
obj-$(CONFIG_SCSI_PM8001)	+= pm8001/
obj-$(CONFIG_SCSI_ISCI)		+= isci/
obj-$(CONFIG_SCSI_IPS)		+= ips.o
obj-$(CONFIG_SCSI_FUTURE_DOMAIN)+= fdomain.o
obj-$(CONFIG_SCSI_GENERIC_NCR5380) += g_NCR5380.o
obj-$(CONFIG_SCSI_NCR53C406A)	+= NCR53c406a.o
obj-$(CONFIG_SCSI_NCR_D700)	+= 53c700.o NCR_D700.o
obj-$(CONFIG_SCSI_NCR_Q720)	+= NCR_Q720_mod.o
obj-$(CONFIG_SCSI_SYM53C416)	+= sym53c416.o
obj-$(CONFIG_SCSI_QLOGIC_FAS)	+= qlogicfas408.o	qlogicfas.o
obj-$(CONFIG_PCMCIA_QLOGIC)	+= qlogicfas408.o
obj-$(CONFIG_SCSI_QLOGIC_1280)	+= qla1280.o 
obj-$(CONFIG_SCSI_QLA_FC)	+= qla2xxx/
obj-$(CONFIG_SCSI_QLA_ISCSI)	+= libiscsi.o qla4xxx/
obj-$(CONFIG_SCSI_LPFC)		+= lpfc/
obj-$(CONFIG_SCSI_BFA_FC)	+= bfa/
obj-$(CONFIG_SCSI_CHELSIO_FCOE)	+= csiostor/
obj-$(CONFIG_SCSI_DMX3191D)	+= dmx3191d.o
obj-$(CONFIG_SCSI_HPSA)		+= hpsa.o
obj-$(CONFIG_SCSI_SMARTPQI)	+= smartpqi/
obj-$(CONFIG_SCSI_SYM53C8XX_2)	+= sym53c8xx_2/
obj-$(CONFIG_SCSI_ZALON)	+= zalon7xx.o
obj-$(CONFIG_SCSI_EATA_PIO)	+= eata_pio.o
obj-$(CONFIG_SCSI_EATA)		+= eata.o
obj-$(CONFIG_SCSI_DC395x)	+= dc395x.o
obj-$(CONFIG_SCSI_AM53C974)	+= esp_scsi.o	am53c974.o
obj-$(CONFIG_CXLFLASH)		+= cxlflash/
obj-$(CONFIG_MEGARAID_LEGACY)	+= megaraid.o
obj-$(CONFIG_MEGARAID_NEWGEN)	+= megaraid/
obj-$(CONFIG_MEGARAID_SAS)	+= megaraid/
obj-$(CONFIG_SCSI_MPT3SAS)	+= mpt3sas/
obj-$(CONFIG_SCSI_UFSHCD)	+= ufs/
obj-$(CONFIG_SCSI_ACARD)	+= atp870u.o
obj-$(CONFIG_SCSI_SUNESP)	+= esp_scsi.o	sun_esp.o
obj-$(CONFIG_SCSI_GDTH)		+= gdth.o
obj-$(CONFIG_SCSI_INITIO)	+= initio.o
obj-$(CONFIG_SCSI_INIA100)	+= a100u2w.o
obj-$(CONFIG_SCSI_QLOGICPTI)	+= qlogicpti.o
obj-$(CONFIG_SCSI_MESH)		+= mesh.o
obj-$(CONFIG_SCSI_MAC53C94)	+= mac53c94.o
obj-$(CONFIG_BLK_DEV_3W_XXXX_RAID) += 3w-xxxx.o
obj-$(CONFIG_SCSI_3W_9XXX)	+= 3w-9xxx.o
obj-$(CONFIG_SCSI_3W_SAS)	+= 3w-sas.o
obj-$(CONFIG_SCSI_PPA)		+= ppa.o
obj-$(CONFIG_SCSI_IMM)		+= imm.o
obj-$(CONFIG_JAZZ_ESP)		+= esp_scsi.o	jazz_esp.o
obj-$(CONFIG_SUN3X_ESP)		+= esp_scsi.o	sun3x_esp.o
obj-$(CONFIG_SCSI_LASI700)	+= 53c700.o lasi700.o
obj-$(CONFIG_SCSI_SNI_53C710)	+= 53c700.o sni_53c710.o
obj-$(CONFIG_SCSI_NSP32)	+= nsp32.o
obj-$(CONFIG_SCSI_IPR)		+= ipr.o
obj-$(CONFIG_SCSI_IBMVSCSI)	+= ibmvscsi/
obj-$(CONFIG_SCSI_IBMVSCSIS)	+= ibmvscsi_tgt/
obj-$(CONFIG_SCSI_IBMVFC)	+= ibmvscsi/
obj-$(CONFIG_SCSI_HPTIOP)	+= hptiop.o
obj-$(CONFIG_SCSI_STEX)		+= stex.o
obj-$(CONFIG_SCSI_MVSAS)	+= mvsas/
obj-$(CONFIG_SCSI_MVUMI)	+= mvumi.o
obj-$(CONFIG_PS3_ROM)		+= ps3rom.o
obj-$(CONFIG_SCSI_CXGB3_ISCSI)	+= libiscsi.o libiscsi_tcp.o cxgbi/
obj-$(CONFIG_SCSI_CXGB4_ISCSI)	+= libiscsi.o libiscsi_tcp.o cxgbi/
obj-$(CONFIG_SCSI_BNX2_ISCSI)	+= libiscsi.o bnx2i/
obj-$(CONFIG_QEDI)          += libiscsi.o qedi/
obj-$(CONFIG_BE2ISCSI)		+= libiscsi.o be2iscsi/
obj-$(CONFIG_SCSI_ESAS2R)	+= esas2r/
obj-$(CONFIG_SCSI_PMCRAID)	+= pmcraid.o
obj-$(CONFIG_SCSI_VIRTIO)	+= virtio_scsi.o
obj-$(CONFIG_VMWARE_PVSCSI)	+= vmw_pvscsi.o
obj-$(CONFIG_XEN_SCSI_FRONTEND)	+= xen-scsifront.o
obj-$(CONFIG_HYPERV_STORAGE)	+= hv_storvsc.o
obj-$(CONFIG_SCSI_WD719X)	+= wd719x.o

obj-$(CONFIG_ARM)		+= arm/

obj-$(CONFIG_CHR_DEV_ST)	+= st.o
obj-$(CONFIG_CHR_DEV_OSST)	+= osst.o
obj-$(CONFIG_BLK_DEV_SD)	+= sd_mod.o
obj-$(CONFIG_BLK_DEV_SR)	+= sr_mod.o
obj-$(CONFIG_CHR_DEV_SG)	+= sg.o
obj-$(CONFIG_CHR_DEV_SCH)	+= ch.o
obj-$(CONFIG_SCSI_ENCLOSURE)	+= ses.o

obj-$(CONFIG_SCSI_OSD_INITIATOR) += osd/
obj-$(CONFIG_SCSI_HISI_SAS) += hisi_sas/

# This goes last, so that "real" scsi devices probe earlier
obj-$(CONFIG_SCSI_DEBUG)	+= scsi_debug.o
scsi_mod-y			+= scsi.o hosts.o scsi_ioctl.o \
				   scsicam.o scsi_error.o scsi_lib.o
scsi_mod-y			+= scsi_common.o
scsi_mod-$(CONFIG_SCSI_CONSTANTS) += constants.o
scsi_mod-$(CONFIG_SCSI_DMA)	+= scsi_lib_dma.o
scsi_mod-y			+= scsi_scan.o scsi_sysfs.o scsi_devinfo.o
scsi_mod-$(CONFIG_SCSI_NETLINK)	+= scsi_netlink.o
scsi_mod-$(CONFIG_SYSCTL)	+= scsi_sysctl.o
scsi_mod-$(CONFIG_SCSI_PROC_FS)	+= scsi_proc.o
scsi_mod-$(CONFIG_BLK_DEBUG_FS)	+= scsi_debugfs.o
scsi_mod-y			+= scsi_trace.o scsi_logging.o
scsi_mod-$(CONFIG_PM)		+= scsi_pm.o
scsi_mod-$(CONFIG_SCSI_DH)	+= scsi_dh.o

hv_storvsc-y			:= storvsc_drv.o

sd_mod-objs	:= sd.o
sd_mod-$(CONFIG_BLK_DEV_INTEGRITY) += sd_dif.o
sd_mod-$(CONFIG_BLK_DEV_ZONED) += sd_zbc.o

sr_mod-objs	:= sr.o sr_ioctl.o sr_vendor.o
ncr53c8xx-flags-$(CONFIG_SCSI_ZALON) \
		:= -DCONFIG_NCR53C8XX_PREFETCH -DSCSI_NCR_BIG_ENDIAN \
			-DCONFIG_SCSI_NCR53C8XX_NO_WORD_TRANSFERS
CFLAGS_ncr53c8xx.o	:= $(ncr53c8xx-flags-y) $(ncr53c8xx-flags-m)
zalon7xx-objs	:= zalon.o ncr53c8xx.o
NCR_Q720_mod-objs	:= NCR_Q720.o ncr53c8xx.o
oktagon_esp_mod-objs	:= oktagon_esp.o oktagon_io.o

# Files generated that shall be removed upon make clean
clean-files :=	53c700_d.h 53c700_u.h

$(obj)/53c700.o $(MODVERDIR)/$(obj)/53c700.ver: $(obj)/53c700_d.h

$(obj)/scsi_sysfs.o: $(obj)/scsi_devinfo_tbl.c

quiet_cmd_bflags = GEN     $@
	cmd_bflags = sed -n 's/.*BLIST_\([A-Z0-9_]*\) *.*/BLIST_FLAG_NAME(\1),/p' $< > $@

$(obj)/scsi_devinfo_tbl.c: include/scsi/scsi_devinfo.h
	$(call if_changed,bflags)

# If you want to play with the firmware, uncomment
# GENERATE_FIRMWARE := 1

ifdef GENERATE_FIRMWARE

$(obj)/53c700_d.h: $(src)/53c700.scr $(src)/script_asm.pl
	$(PERL) -s $(src)/script_asm.pl -ncr7x0_family $@ $(@:_d.h=_u.h) < $<

endif
