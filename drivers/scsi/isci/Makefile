EXTRA_CFLAGS += -Idrivers/scsi/isci/core/ -Idrivers/scsi/isci/
obj-$(CONFIG_SCSI_ISCI) += isci.o
isci-objs := init.o phy.o request.o sata.o \
	     remote_device.o port.o timers.o \
	     host.o task.o probe_roms.o \
	     state_machine.o \
	     remote_node_context.o \
	     remote_node_table.o \
	     unsolicited_frame_control.o \
	     core/scic_sds_request.o \
	     core/scic_sds_stp_request.o \
	     core/scic_sds_port.o \
	     core/scic_sds_port_configuration_agent.o \
	     core/scic_sds_phy.o \
	     core/scic_sds_ssp_request.o \
	     core/scic_sds_smp_request.o \
	     core/sci_util.o
