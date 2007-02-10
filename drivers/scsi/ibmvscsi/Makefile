obj-$(CONFIG_SCSI_IBMVSCSI)	+= ibmvscsic.o

ibmvscsic-y			+= ibmvscsi.o
ifndef CONFIG_PPC_PSERIES
ibmvscsic-$(CONFIG_PPC_ISERIES)	+= iseries_vscsi.o 
endif
ibmvscsic-$(CONFIG_PPC_PSERIES)	+= rpa_vscsi.o 

obj-$(CONFIG_SCSI_IBMVSCSIS)	+= ibmvstgt.o
