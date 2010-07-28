#
# Makefile for the linux kernel.
#

extra-y		:= head.o vmlinux.lds
EXTRA_AFLAGS	:= $(KBUILD_CFLAGS)
EXTRA_CFLAGS	:= -Werror -Wno-sign-compare

obj-y    := entry.o traps.o process.o init_task.o osf_sys.o irq.o \
	    irq_alpha.o signal.o setup.o ptrace.o time.o \
	    alpha_ksyms.o systbls.o err_common.o io.o

obj-$(CONFIG_VGA_HOSE)	+= console.o
obj-$(CONFIG_SMP)	+= smp.o
obj-$(CONFIG_PCI)	+= pci.o pci_iommu.o pci-sysfs.o
obj-$(CONFIG_SRM_ENV)	+= srm_env.o
obj-$(CONFIG_MODULES)	+= module.o

ifdef CONFIG_ALPHA_GENERIC

obj-y 	 += core_apecs.o core_cia.o core_irongate.o core_lca.o \
	    core_mcpcia.o core_polaris.o core_t2.o \
	    core_tsunami.o

obj-y	 += sys_alcor.o sys_cabriolet.o sys_dp264.o sys_eb64p.o sys_eiger.o \
	    sys_jensen.o sys_miata.o sys_mikasa.o sys_nautilus.o \
	    sys_noritake.o sys_rawhide.o sys_ruffian.o sys_rx164.o \
	    sys_sable.o sys_sio.o sys_sx164.o sys_takara.o

ifndef CONFIG_ALPHA_LEGACY_START_ADDRESS
obj-y	 += core_marvel.o core_titan.o core_wildfire.o
obj-y	 += sys_marvel.o sys_titan.o sys_wildfire.o
obj-y    += err_ev7.o err_titan.o err_marvel.o
endif

obj-y	 += irq_pyxis.o irq_i8259.o irq_srm.o
obj-y	 += err_ev6.o
obj-y	 += es1888.o smc37c669.o smc37c93x.o pc873xx.o gct.o
obj-y    += srmcons.o

else

# Misc support
obj-$(CONFIG_ALPHA_SRM)		+= srmcons.o

ifdef CONFIG_BINFMT_AOUT
obj-y	+= binfmt_loader.o
endif

# Core logic support
obj-$(CONFIG_ALPHA_APECS)	+= core_apecs.o
obj-$(CONFIG_ALPHA_CIA)		+= core_cia.o
obj-$(CONFIG_ALPHA_IRONGATE)	+= core_irongate.o
obj-$(CONFIG_ALPHA_LCA)		+= core_lca.o
obj-$(CONFIG_ALPHA_MARVEL)	+= core_marvel.o gct.o
obj-$(CONFIG_ALPHA_MCPCIA)	+= core_mcpcia.o
obj-$(CONFIG_ALPHA_POLARIS)	+= core_polaris.o
obj-$(CONFIG_ALPHA_T2)		+= core_t2.o
obj-$(CONFIG_ALPHA_TSUNAMI)	+= core_tsunami.o
obj-$(CONFIG_ALPHA_TITAN)	+= core_titan.o
obj-$(CONFIG_ALPHA_WILDFIRE)	+= core_wildfire.o

# Board support
obj-$(CONFIG_ALPHA_ALCOR)	+= sys_alcor.o irq_i8259.o irq_srm.o
obj-$(CONFIG_ALPHA_CABRIOLET)	+= sys_cabriolet.o irq_i8259.o irq_srm.o \
				   pc873xx.o
obj-$(CONFIG_ALPHA_EB164)	+= sys_cabriolet.o irq_i8259.o irq_srm.o \
				   pc873xx.o
obj-$(CONFIG_ALPHA_EB66P)	+= sys_cabriolet.o irq_i8259.o irq_srm.o \
				   pc873xx.o
obj-$(CONFIG_ALPHA_LX164)	+= sys_cabriolet.o irq_i8259.o irq_srm.o \
				   smc37c93x.o
obj-$(CONFIG_ALPHA_PC164)	+= sys_cabriolet.o irq_i8259.o irq_srm.o \
				   smc37c93x.o
obj-$(CONFIG_ALPHA_DP264)	+= sys_dp264.o irq_i8259.o es1888.o smc37c669.o
obj-$(CONFIG_ALPHA_SHARK)	+= sys_dp264.o irq_i8259.o es1888.o smc37c669.o
obj-$(CONFIG_ALPHA_TITAN)	+= sys_titan.o irq_i8259.o smc37c669.o
obj-$(CONFIG_ALPHA_EB64P)	+= sys_eb64p.o irq_i8259.o
obj-$(CONFIG_ALPHA_EB66)	+= sys_eb64p.o irq_i8259.o
obj-$(CONFIG_ALPHA_EIGER)	+= sys_eiger.o irq_i8259.o
obj-$(CONFIG_ALPHA_JENSEN)	+= sys_jensen.o pci-noop.o irq_i8259.o
obj-$(CONFIG_ALPHA_MARVEL)	+= sys_marvel.o 
obj-$(CONFIG_ALPHA_MIATA)	+= sys_miata.o irq_pyxis.o irq_i8259.o \
				   es1888.o smc37c669.o
obj-$(CONFIG_ALPHA_MIKASA)	+= sys_mikasa.o irq_i8259.o irq_srm.o
obj-$(CONFIG_ALPHA_NAUTILUS)	+= sys_nautilus.o irq_i8259.o irq_srm.o
obj-$(CONFIG_ALPHA_NORITAKE)	+= sys_noritake.o irq_i8259.o
obj-$(CONFIG_ALPHA_RAWHIDE)	+= sys_rawhide.o irq_i8259.o
obj-$(CONFIG_ALPHA_RUFFIAN)	+= sys_ruffian.o irq_pyxis.o irq_i8259.o
obj-$(CONFIG_ALPHA_RX164)	+= sys_rx164.o irq_i8259.o
obj-$(CONFIG_ALPHA_SABLE)	+= sys_sable.o
obj-$(CONFIG_ALPHA_LYNX)	+= sys_sable.o
obj-$(CONFIG_ALPHA_BOOK1)	+= sys_sio.o irq_i8259.o irq_srm.o pc873xx.o
obj-$(CONFIG_ALPHA_AVANTI)	+= sys_sio.o irq_i8259.o irq_srm.o pc873xx.o
obj-$(CONFIG_ALPHA_NONAME)	+= sys_sio.o irq_i8259.o irq_srm.o pc873xx.o
obj-$(CONFIG_ALPHA_P2K)		+= sys_sio.o irq_i8259.o irq_srm.o pc873xx.o
obj-$(CONFIG_ALPHA_XL)		+= sys_sio.o irq_i8259.o irq_srm.o pc873xx.o
obj-$(CONFIG_ALPHA_SX164)	+= sys_sx164.o irq_pyxis.o irq_i8259.o \
				   irq_srm.o smc37c669.o
obj-$(CONFIG_ALPHA_TAKARA)	+= sys_takara.o irq_i8259.o pc873xx.o
obj-$(CONFIG_ALPHA_WILDFIRE)	+= sys_wildfire.o irq_i8259.o

# Error support
obj-$(CONFIG_ALPHA_MARVEL)	+= err_ev7.o err_marvel.o
obj-$(CONFIG_ALPHA_NAUTILUS)	+= err_ev6.o
obj-$(CONFIG_ALPHA_TITAN)	+= err_ev6.o err_titan.o

endif # GENERIC
