===================================
Dell Systems Management Base Driver
===================================

Overview
========

The Dell Systems Management Base Driver provides a sysfs interface for
systems management software such as Dell OpenManage to perform system
management interrupts and host control actions (system power cycle or
power off after OS shutdown) on certain Dell systems.

Dell OpenManage requires this driver on the following Dell PowerEdge systems:
300, 1300, 1400, 400SC, 500SC, 1500SC, 1550, 600SC, 1600SC, 650, 1655MC,
700, and 750.  Other Dell software such as the open source libsmbios project
is expected to make use of this driver, and it may include the use of this
driver on other Dell systems.

The Dell libsmbios project aims towards providing access to as much BIOS
information as possible.  See http://linux.dell.com/libsmbios/main/ for
more information about the libsmbios project.


System Management Interrupt
===========================

On some Dell systems, systems management software must access certain
management information via a system management interrupt (SMI).  The SMI data
buffer must reside in 32-bit address space, and the physical address of the
buffer is required for the SMI.  The driver maintains the memory required for
the SMI and provides a way for the application to generate the SMI.
The driver creates the following sysfs entries for systems management
software to perform these system management interrupts::

	/sys/devices/platform/dcdbas/smi_data
	/sys/devices/platform/dcdbas/smi_data_buf_phys_addr
	/sys/devices/platform/dcdbas/smi_data_buf_size
	/sys/devices/platform/dcdbas/smi_request

Systems management software must perform the following steps to execute
a SMI using this driver:

1) Lock smi_data.
2) Write system management command to smi_data.
3) Write "1" to smi_request to generate a calling interface SMI or
   "2" to generate a raw SMI.
4) Read system management command response from smi_data.
5) Unlock smi_data.


Host Control Action
===================

Dell OpenManage supports a host control feature that allows the administrator
to perform a power cycle or power off of the system after the OS has finished
shutting down.  On some Dell systems, this host control feature requires that
a driver perform a SMI after the OS has finished shutting down.

The driver creates the following sysfs entries for systems management software
to schedule the driver to perform a power cycle or power off host control
action after the system has finished shutting down:

/sys/devices/platform/dcdbas/host_control_action
/sys/devices/platform/dcdbas/host_control_smi_type
/sys/devices/platform/dcdbas/host_control_on_shutdown

Dell OpenManage performs the following steps to execute a power cycle or
power off host control action using this driver:

1) Write host control action to be performed to host_control_action.
2) Write type of SMI that driver needs to perform to host_control_smi_type.
3) Write "1" to host_control_on_shutdown to enable host control action.
4) Initiate OS shutdown.
   (Driver will perform host control SMI when it is notified that the OS
   has finished shutting down.)


Host Control SMI Type
=====================

The following table shows the value to write to host_control_smi_type to
perform a power cycle or power off host control action:

=================== =====================
PowerEdge System    Host Control SMI Type
=================== =====================
      300             HC_SMITYPE_TYPE1
     1300             HC_SMITYPE_TYPE1
     1400             HC_SMITYPE_TYPE2
      500SC           HC_SMITYPE_TYPE2
     1500SC           HC_SMITYPE_TYPE2
     1550             HC_SMITYPE_TYPE2
      600SC           HC_SMITYPE_TYPE2
     1600SC           HC_SMITYPE_TYPE2
      650             HC_SMITYPE_TYPE2
     1655MC           HC_SMITYPE_TYPE2
      700             HC_SMITYPE_TYPE3
      750             HC_SMITYPE_TYPE3
=================== =====================
