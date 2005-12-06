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
CFLAGS_seagate.o =   -DARBITRATE -DPARITY -DSEAGATE_USE_ASM

subdir-$(CONFIG_PCMCIA)		+= pcmcia

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

obj-$(CONFIG_ISCSI_TCP) 	+= iscsi_tcp.o
obj-$(CONFIG_SCSI_AMIGA7XX)	+= amiga7xx.o	53c7xx.o
obj-$(CONFIG_A3000_SCSI)	+= a3000.o	wd33c93.o
obj-$(CONFIG_A2091_SCSI)	+= a2091.o	wd33c93.o
obj-$(CONFIG_GVP11_SCSI)	+= gvp11.o	wd33c93.o
obj-$(CONFIG_MVME147_SCSI)	+= mvme147.o	wd33c93.o
obj-$(CONFIG_SGIWD93_SCSI)	+= sgiwd93.o	wd33c93.o
obj-$(CONFIG_CYBERSTORM_SCSI)	+= NCR53C9x.o	cyberstorm.o
obj-$(CONFIG_CYBERSTORMII_SCSI)	+= NCR53C9x.o	cyberstormII.o
obj-$(CONFIG_BLZ2060_SCSI)	+= NCR53C9x.o	blz2060.o
obj-$(CONFIG_BLZ1230_SCSI)	+= NCR53C9x.o	blz1230.o
obj-$(CONFIG_FASTLANE_SCSI)	+= NCR53C9x.o	fastlane.o
obj-$(CONFIG_OKTAGON_SCSI)	+= NCR53C9x.o	oktagon_esp.o	oktagon_io.o
obj-$(CONFIG_ATARI_SCSI)	+= atari_scsi.o
obj-$(CONFIG_MAC_SCSI)		+= mac_scsi.o
obj-$(CONFIG_SCSI_MAC_ESP)	+= mac_esp.o	NCR53C9x.o
obj-$(CONFIG_SUN3_SCSI)		+= sun3_scsi.o  sun3_scsi_vme.o
obj-$(CONFIG_MVME16x_SCSI)	+= mvme16x.o	53c7xx.o
obj-$(CONFIG_BVME6000_SCSI)	+= bvme6000.o	53c7xx.o
obj-$(CONFIG_SCSI_SIM710)	+= 53c700.o	sim710.o
obj-$(CONFIG_SCSI_ADVANSYS)	+= advansys.o
obj-$(CONFIG_SCSI_PSI240I)	+= psi240i.o
obj-$(CONFIG_SCSI_BUSLOGIC)	+= BusLogic.o
obj-$(CONFIG_SCSI_DPT_I2O)	+= dpt_i2o.o
obj-$(CONFIG_SCSI_U14_34F)	+= u14-34f.o
obj-$(CONFIG_SCSI_ULTRASTOR)	+= ultrastor.o
obj-$(CONFIG_SCSI_AHA152X)	+= aha152x.o
obj-$(CONFIG_SCSI_AHA1542)	+= aha1542.o
obj-$(CONFIG_SCSI_AHA1740)	+= aha1740.o
obj-$(CONFIG_SCSI_AIC7XXX)	+= aic7xxx/
obj-$(CONFIG_SCSI_AIC79XX)	+= aic7xxx/
obj-$(CONFIG_SCSI_AACRAID)	+= aacraid/
obj-$(CONFIG_SCSI_AIC7XXX_OLD)	+= aic7xxx_old.o
obj-$(CONFIG_SCSI_IPS)		+= ips.o
obj-$(CONFIG_SCSI_FD_MCS)	+= fd_mcs.o
obj-$(CONFIG_SCSI_FUTURE_DOMAIN)+= fdomain.o
obj-$(CONFIG_SCSI_IN2000)	+= in2000.o
obj-$(CONFIG_SCSI_GENERIC_NCR5380) += g_NCR5380.o
obj-$(CONFIG_SCSI_GENERIC_NCR5380_MMIO) += g_NCR5380_mmio.o
obj-$(CONFIG_SCSI_NCR53C406A)	+= NCR53c406a.o
obj-$(CONFIG_SCSI_NCR_D700)	+= 53c700.o NCR_D700.o
obj-$(CONFIG_SCSI_NCR_Q720)	+= NCR_Q720_mod.o
obj-$(CONFIG_SCSI_SYM53C416)	+= sym53c416.o
obj-$(CONFIG_SCSI_QLOGIC_FAS)	+= qlogicfas408.o	qlogicfas.o
obj-$(CONFIG_PCMCIA_QLOGIC)	+= qlogicfas408.o
obj-$(CONFIG_SCSI_QLOGIC_FC)	+= qlogicfc.o 
obj-$(CONFIG_SCSI_QLOGIC_1280)	+= qla1280.o 
obj-$(CONFIG_SCSI_QLA2XXX)	+= qla2xxx/
obj-$(CONFIG_SCSI_LPFC)		+= lpfc/
obj-$(CONFIG_SCSI_PAS16)	+= pas16.o
obj-$(CONFIG_SCSI_SEAGATE)	+= seagate.o
obj-$(CONFIG_SCSI_FD_8xx)	+= seagate.o
obj-$(CONFIG_SCSI_T128)		+= t128.o
obj-$(CONFIG_SCSI_DMX3191D)	+= dmx3191d.o
obj-$(CONFIG_SCSI_DTC3280)	+= dtc.o
obj-$(CONFIG_SCSI_SYM53C8XX_2)	+= sym53c8xx_2/
obj-$(CONFIG_SCSI_ZALON)	+= zalon7xx.o
obj-$(CONFIG_SCSI_EATA_PIO)	+= eata_pio.o
obj-$(CONFIG_SCSI_7000FASST)	+= wd7000.o
obj-$(CONFIG_SCSI_MCA_53C9X)	+= NCR53C9x.o	mca_53c9x.o
obj-$(CONFIG_SCSI_IBMMCA)	+= ibmmca.o
obj-$(CONFIG_SCSI_EATA)		+= eata.o
obj-$(CONFIG_SCSI_DC395x)	+= dc395x.o
obj-$(CONFIG_SCSI_DC390T)	+= tmscsim.o
obj-$(CONFIG_MEGARAID_LEGACY)	+= megaraid.o
obj-$(CONFIG_MEGARAID_NEWGEN)	+= megaraid/
obj-$(CONFIG_MEGARAID_SAS)	+= megaraid/
obj-$(CONFIG_SCSI_ACARD)	+= atp870u.o
obj-$(CONFIG_SCSI_SUNESP)	+= esp.o
obj-$(CONFIG_SCSI_GDTH)		+= gdth.o
obj-$(CONFIG_SCSI_INITIO)	+= initio.o
obj-$(CONFIG_SCSI_INIA100)	+= a100u2w.o
obj-$(CONFIG_SCSI_QLOGICPTI)	+= qlogicpti.o
obj-$(CONFIG_BLK_DEV_IDESCSI)	+= ide-scsi.o
obj-$(CONFIG_SCSI_MESH)		+= mesh.o
obj-$(CONFIG_SCSI_MAC53C94)	+= mac53c94.o
obj-$(CONFIG_SCSI_PLUTO)	+= pluto.o
obj-$(CONFIG_SCSI_DECNCR)	+= NCR53C9x.o	dec_esp.o
obj-$(CONFIG_BLK_DEV_3W_XXXX_RAID) += 3w-xxxx.o
obj-$(CONFIG_SCSI_3W_9XXX)	+= 3w-9xxx.o
obj-$(CONFIG_SCSI_PPA)		+= ppa.o
obj-$(CONFIG_SCSI_IMM)		+= imm.o
obj-$(CONFIG_JAZZ_ESP)		+= NCR53C9x.o	jazz_esp.o
obj-$(CONFIG_SUN3X_ESP)		+= NCR53C9x.o	sun3x_esp.o
obj-$(CONFIG_SCSI_DEBUG)	+= scsi_debug.o
obj-$(CONFIG_SCSI_FCAL)		+= fcal.o
obj-$(CONFIG_SCSI_LASI700)	+= 53c700.o lasi700.o
obj-$(CONFIG_SCSI_NSP32)	+= nsp32.o
obj-$(CONFIG_SCSI_IPR)		+= ipr.o
obj-$(CONFIG_SCSI_IBMVSCSI)	+= ibmvscsi/
obj-$(CONFIG_SCSI_SATA_AHCI)	+= libata.o ahci.o
obj-$(CONFIG_SCSI_SATA_SVW)	+= libata.o sata_svw.o
obj-$(CONFIG_SCSI_ATA_PIIX)	+= libata.o ata_piix.o
obj-$(CONFIG_SCSI_SATA_PROMISE)	+= libata.o sata_promise.o
obj-$(CONFIG_SCSI_SATA_QSTOR)	+= libata.o sata_qstor.o
obj-$(CONFIG_SCSI_SATA_SIL)	+= libata.o sata_sil.o
obj-$(CONFIG_SCSI_SATA_SIL24)	+= libata.o sata_sil24.o
obj-$(CONFIG_SCSI_SATA_VIA)	+= libata.o sata_via.o
obj-$(CONFIG_SCSI_SATA_VITESSE)	+= libata.o sata_vsc.o
obj-$(CONFIG_SCSI_SATA_SIS)	+= libata.o sata_sis.o
obj-$(CONFIG_SCSI_SATA_SX4)	+= libata.o sata_sx4.o
obj-$(CONFIG_SCSI_SATA_NV)	+= libata.o sata_nv.o
obj-$(CONFIG_SCSI_SATA_ULI)	+= libata.o sata_uli.o
obj-$(CONFIG_SCSI_SATA_MV)	+= libata.o sata_mv.o
obj-$(CONFIG_SCSI_PDC_ADMA)	+= libata.o pdc_adma.o

obj-$(CONFIG_ARM)		+= arm/

obj-$(CONFIG_CHR_DEV_ST)	+= st.o
obj-$(CONFIG_CHR_DEV_OSST)	+= osst.o
obj-$(CONFIG_BLK_DEV_SD)	+= sd_mod.o
obj-$(CONFIG_BLK_DEV_SR)	+= sr_mod.o
obj-$(CONFIG_CHR_DEV_SG)	+= sg.o
obj-$(CONFIG_CHR_DEV_SCH)	+= ch.o

scsi_mod-y			+= scsi.o hosts.o scsi_ioctl.o constants.o \
				   scsicam.o scsi_error.o scsi_lib.o \
				   scsi_scan.o scsi_sysfs.o \
				   scsi_devinfo.o
scsi_mod-$(CONFIG_SYSCTL)	+= scsi_sysctl.o
scsi_mod-$(CONFIG_SCSI_PROC_FS)	+= scsi_proc.o

sd_mod-objs	:= sd.o
sr_mod-objs	:= sr.o sr_ioctl.o sr_vendor.o
ncr53c8xx-flags-$(CONFIG_SCSI_ZALON) \
		:= -DCONFIG_NCR53C8XX_PREFETCH -DSCSI_NCR_BIG_ENDIAN \
			-DCONFIG_SCSI_NCR53C8XX_NO_WORD_TRANSFERS
CFLAGS_ncr53c8xx.o	:= $(ncr53c8xx-flags-y) $(ncr53c8xx-flags-m)
zalon7xx-objs	:= zalon.o ncr53c8xx.o
NCR_Q720_mod-objs	:= NCR_Q720.o ncr53c8xx.o
libata-objs	:= libata-core.o libata-scsi.o

# Files generated that shall be removed upon make clean
clean-files :=	53c7xx_d.h 53c700_d.h	\
		53c7xx_u.h 53c700_u.h

$(obj)/53c7xx.o:   $(obj)/53c7xx_d.h $(obj)/53c7xx_u.h
$(obj)/53c700.o $(MODVERDIR)/$(obj)/53c700.ver: $(obj)/53c700_d.h

# If you want to play with the firmware, uncomment
# GENERATE_FIRMWARE := 1

ifdef GENERATE_FIRMWARE

$(obj)/53c7xx_d.h: $(src)/53c7xx.scr $(src)/script_asm.pl
	$(CPP) -traditional -DCHIP=710 - < $< | grep -v '^#' | $(PERL) -s $(src)/script_asm.pl -ncr7x0_family $@ $(@:_d.h=_u.h)

$(obj)/53c7xx_u.h: $(obj)/53c7xx_d.h

$(obj)/53c700_d.h: $(src)/53c700.scr $(src)/script_asm.pl
	$(PERL) -s $(src)/script_asm.pl -ncr7x0_family $@ $(@:_d.h=_u.h) < $<

endif
