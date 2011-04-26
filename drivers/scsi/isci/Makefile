#TODO kill SCIC_SDS_4_ENABLED it is always true for this
#generation of silicon
EXTRA_CFLAGS += -DSCIC_SDS_4_ENABLED

EXTRA_CFLAGS += -Idrivers/scsi/isci/core/ -Idrivers/scsi/isci/
obj-$(CONFIG_SCSI_ISCI) += isci.o
isci-objs := init.o phy.o request.o sata.o \
	     remote_device.o port.o timers.o \
	     host.o task.o probe_roms.o \
	     stp_remote_device.o \
	     smp_remote_device.o \
	     remote_node_context.o \
	     remote_node_table.o \
	     core/scic_sds_controller.o  \
	     core/scic_sds_request.o \
	     core/scic_sds_stp_request.o \
	     core/scic_sds_port.o \
	     core/scic_sds_port_configuration_agent.o \
	     core/scic_sds_phy.o \
	     core/scic_sds_ssp_request.o \
	     core/scic_sds_smp_request.o \
	     core/scic_sds_unsolicited_frame_control.o \
	     core/sci_base_state_machine.o \
	     core/sci_util.o
