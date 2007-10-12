
obj-$(CONFIG_ATA)		+= libata.o

obj-$(CONFIG_SATA_AHCI)		+= ahci.o
obj-$(CONFIG_SATA_SVW)		+= sata_svw.o
obj-$(CONFIG_ATA_PIIX)		+= ata_piix.o
obj-$(CONFIG_SATA_PROMISE)	+= sata_promise.o
obj-$(CONFIG_SATA_QSTOR)	+= sata_qstor.o
obj-$(CONFIG_SATA_SIL)		+= sata_sil.o
obj-$(CONFIG_SATA_SIL24)	+= sata_sil24.o
obj-$(CONFIG_SATA_VIA)		+= sata_via.o
obj-$(CONFIG_SATA_VITESSE)	+= sata_vsc.o
obj-$(CONFIG_SATA_SIS)		+= sata_sis.o
obj-$(CONFIG_SATA_SX4)		+= sata_sx4.o
obj-$(CONFIG_SATA_NV)		+= sata_nv.o
obj-$(CONFIG_SATA_ULI)		+= sata_uli.o
obj-$(CONFIG_SATA_MV)		+= sata_mv.o
obj-$(CONFIG_SATA_INIC162X)	+= sata_inic162x.o
obj-$(CONFIG_PDC_ADMA)		+= pdc_adma.o

obj-$(CONFIG_PATA_ALI)		+= pata_ali.o
obj-$(CONFIG_PATA_AMD)		+= pata_amd.o
obj-$(CONFIG_PATA_ARTOP)	+= pata_artop.o
obj-$(CONFIG_PATA_AT32)		+= pata_at32.o
obj-$(CONFIG_PATA_ATIIXP)	+= pata_atiixp.o
obj-$(CONFIG_PATA_CMD640_PCI)	+= pata_cmd640.o
obj-$(CONFIG_PATA_CMD64X)	+= pata_cmd64x.o
obj-$(CONFIG_PATA_CS5520)	+= pata_cs5520.o
obj-$(CONFIG_PATA_CS5530)	+= pata_cs5530.o
obj-$(CONFIG_PATA_CS5535)	+= pata_cs5535.o
obj-$(CONFIG_PATA_CYPRESS)	+= pata_cypress.o
obj-$(CONFIG_PATA_EFAR)		+= pata_efar.o
obj-$(CONFIG_PATA_HPT366)	+= pata_hpt366.o
obj-$(CONFIG_PATA_HPT37X)	+= pata_hpt37x.o
obj-$(CONFIG_PATA_HPT3X2N)	+= pata_hpt3x2n.o
obj-$(CONFIG_PATA_HPT3X3)	+= pata_hpt3x3.o
obj-$(CONFIG_PATA_ISAPNP)	+= pata_isapnp.o
obj-$(CONFIG_PATA_IT821X)	+= pata_it821x.o
obj-$(CONFIG_PATA_IT8213)	+= pata_it8213.o
obj-$(CONFIG_PATA_JMICRON)	+= pata_jmicron.o
obj-$(CONFIG_PATA_NETCELL)	+= pata_netcell.o
obj-$(CONFIG_PATA_NS87410)	+= pata_ns87410.o
obj-$(CONFIG_PATA_NS87415)	+= pata_ns87415.o
obj-$(CONFIG_PATA_OPTI)		+= pata_opti.o
obj-$(CONFIG_PATA_OPTIDMA)	+= pata_optidma.o
obj-$(CONFIG_PATA_MPC52xx)	+= pata_mpc52xx.o
obj-$(CONFIG_PATA_MARVELL)	+= pata_marvell.o
obj-$(CONFIG_PATA_MPIIX)	+= pata_mpiix.o
obj-$(CONFIG_PATA_OLDPIIX)	+= pata_oldpiix.o
obj-$(CONFIG_PATA_PCMCIA)	+= pata_pcmcia.o
obj-$(CONFIG_PATA_PDC2027X)	+= pata_pdc2027x.o
obj-$(CONFIG_PATA_PDC_OLD)	+= pata_pdc202xx_old.o
obj-$(CONFIG_PATA_QDI)		+= pata_qdi.o
obj-$(CONFIG_PATA_RADISYS)	+= pata_radisys.o
obj-$(CONFIG_PATA_RZ1000)	+= pata_rz1000.o
obj-$(CONFIG_PATA_SC1200)	+= pata_sc1200.o
obj-$(CONFIG_PATA_SERVERWORKS)	+= pata_serverworks.o
obj-$(CONFIG_PATA_SIL680)	+= pata_sil680.o
obj-$(CONFIG_PATA_VIA)		+= pata_via.o
obj-$(CONFIG_PATA_WINBOND)	+= pata_sl82c105.o
obj-$(CONFIG_PATA_WINBOND_VLB)	+= pata_winbond.o
obj-$(CONFIG_PATA_SIS)		+= pata_sis.o
obj-$(CONFIG_PATA_TRIFLEX)	+= pata_triflex.o
obj-$(CONFIG_PATA_IXP4XX_CF)	+= pata_ixp4xx_cf.o
obj-$(CONFIG_PATA_SCC)		+= pata_scc.o
obj-$(CONFIG_PATA_BF54X)	+= pata_bf54x.o
obj-$(CONFIG_PATA_PLATFORM)	+= pata_platform.o
obj-$(CONFIG_PATA_ICSIDE)	+= pata_icside.o
# Should be last but two libata driver
obj-$(CONFIG_PATA_ACPI)		+= pata_acpi.o
# Should be last but one libata driver
obj-$(CONFIG_ATA_GENERIC)	+= ata_generic.o
# Should be last libata driver
obj-$(CONFIG_PATA_LEGACY)	+= pata_legacy.o

libata-objs	:= libata-core.o libata-scsi.o libata-sff.o libata-eh.o \
		   libata-pmp.o
libata-$(CONFIG_ATA_ACPI)	+= libata-acpi.o
