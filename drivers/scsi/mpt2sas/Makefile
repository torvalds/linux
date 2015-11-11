# mpt2sas makefile

# share the official mpi headers from the mpt3sas driver
ccflags-y += -I$(src)/../mpt3sas
ccflags-y += -DSCSI_MPT2SAS

# use the common object files from mpt3sas driver
obj-$(CONFIG_SCSI_MPT2SAS) += mpt2sas.o
mpt2sas-y +=  ../mpt3sas/mpt3sas_base.o        \
		../mpt3sas/mpt3sas_config.o    \
		../mpt3sas/mpt3sas_scsih.o     \
		../mpt3sas/mpt3sas_transport.o \
		../mpt3sas/mpt3sas_ctl.o       \
		../mpt3sas/mpt3sas_trigger_diag.o  \
		mpt2sas_module.o
