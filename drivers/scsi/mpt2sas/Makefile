# mpt2sas makefile

# share the official mpi headers from the mpt3sas driver
ccflags-y += -I$(src)/../mpt3sas

obj-$(CONFIG_SCSI_MPT2SAS) += mpt2sas.o
mpt2sas-y +=  mpt2sas_base.o        \
		mpt2sas_config.o    \
		mpt2sas_scsih.o     \
		mpt2sas_transport.o \
		mpt2sas_ctl.o
