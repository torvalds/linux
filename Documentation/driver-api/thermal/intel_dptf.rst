.. SPDX-License-Identifier: GPL-2.0

===============================================================
Intel(R) Dynamic Platform and Thermal Framework Sysfs Interface
===============================================================

:Copyright: © 2022 Intel Corporation

:Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>

Introduction
------------

Intel(R) Dynamic Platform and Thermal Framework (DPTF) is a platform
level hardware/software solution for power and thermal management.

As a container for multiple power/thermal technologies, DPTF provides
a coordinated approach for different policies to effect the hardware
state of a system.

Since it is a platform level framework, this has several components.
Some parts of the technology is implemented in the firmware and uses
ACPI and PCI devices to expose various features for monitoring and
control. Linux has a set of kernel drivers exposing hardware interface
to user space. This allows user space thermal solutions like
"Linux Thermal Daemon" to read platform specific thermal and power
tables to deliver adequate performance while keeping the system under
thermal limits.

DPTF ACPI Drivers interface
----------------------------

:file:`/sys/bus/platform/devices/<N>/uuids`, where <N>
=INT3400|INTC1040|INTC1041|INTC10A0

``available_uuids`` (RO)
	A set of UUIDs strings presenting available policies
	which should be notified to the firmware when the
	user space can support those policies.

	UUID strings:

	"42A441D6-AE6A-462b-A84B-4A8CE79027D3" : Passive 1

	"3A95C389-E4B8-4629-A526-C52C88626BAE" : Active

	"97C68AE7-15FA-499c-B8C9-5DA81D606E0A" : Critical

	"63BE270F-1C11-48FD-A6F7-3AF253FF3E2D" : Adaptive performance

	"5349962F-71E6-431D-9AE8-0A635B710AEE" : Emergency call

	"9E04115A-AE87-4D1C-9500-0F3E340BFE75" : Passive 2

	"F5A35014-C209-46A4-993A-EB56DE7530A1" : Power Boss

	"6ED722A7-9240-48A5-B479-31EEF723D7CF" : Virtual Sensor

	"16CAF1B7-DD38-40ED-B1C1-1B8A1913D531" : Cooling mode

	"BE84BABF-C4D4-403D-B495-3128FD44dAC1" : HDC

``current_uuid`` (RW)
	User space can write strings from available UUIDs, one at a
	time.

:file:`/sys/bus/platform/devices/<N>/`, where <N>
=INT3400|INTC1040|INTC1041|INTC10A0

``imok`` (WO)
	User space daemon write 1 to respond to firmware event
	for sending keep alive notification. User space receives
	THERMAL_EVENT_KEEP_ALIVE kobject uevent notification when
	firmware calls for user space to respond with imok ACPI
	method.

``odvp*`` (RO)
	Firmware thermal status variable values. Thermal tables
	calls for different processing based on these variable
	values.

``data_vault`` (RO)
	Binary thermal table. Refer to
	https:/github.com/intel/thermal_daemon for decoding
	thermal table.

``production_mode`` (RO)
	When different from zero, manufacturer locked thermal configuration
	from further changes.

ACPI Thermal Relationship table interface
------------------------------------------

:file:`/dev/acpi_thermal_rel`

	This device provides IOCTL interface to read standard ACPI
	thermal relationship tables via ACPI methods _TRT and _ART.
	These IOCTLs are defined in
	drivers/thermal/intel/int340x_thermal/acpi_thermal_rel.h

	IOCTLs:

	ACPI_THERMAL_GET_TRT_LEN: Get length of TRT table

	ACPI_THERMAL_GET_ART_LEN: Get length of ART table

	ACPI_THERMAL_GET_TRT_COUNT: Number of records in TRT table

	ACPI_THERMAL_GET_ART_COUNT: Number of records in ART table

	ACPI_THERMAL_GET_TRT: Read binary TRT table, length to read is
	provided via argument to ioctl().

	ACPI_THERMAL_GET_ART: Read binary ART table, length to read is
	provided via argument to ioctl().

DPTF ACPI Sensor drivers
-------------------------

DPTF Sensor drivers are presented as standard thermal sysfs thermal_zone.


DPTF ACPI Cooling drivers
--------------------------

DPTF cooling drivers are presented as standard thermal sysfs cooling_device.


DPTF Processor thermal PCI Driver interface
--------------------------------------------

:file:`/sys/bus/pci/devices/0000\:00\:04.0/power_limits/`

Refer to Documentation/power/powercap/powercap.rst for powercap
ABI.

``power_limit_0_max_uw`` (RO)
	Maximum powercap sysfs constraint_0_power_limit_uw for Intel RAPL

``power_limit_0_step_uw`` (RO)
	Power limit increment/decrements for Intel RAPL constraint 0 power limit

``power_limit_0_min_uw`` (RO)
	Minimum powercap sysfs constraint_0_power_limit_uw for Intel RAPL

``power_limit_0_tmin_us`` (RO)
	Minimum powercap sysfs constraint_0_time_window_us for Intel RAPL

``power_limit_0_tmax_us`` (RO)
	Maximum powercap sysfs constraint_0_time_window_us for Intel RAPL

``power_limit_1_max_uw`` (RO)
	Maximum powercap sysfs constraint_1_power_limit_uw for Intel RAPL

``power_limit_1_step_uw`` (RO)
	Power limit increment/decrements for Intel RAPL constraint 1 power limit

``power_limit_1_min_uw`` (RO)
	Minimum powercap sysfs constraint_1_power_limit_uw for Intel RAPL

``power_limit_1_tmin_us`` (RO)
	Minimum powercap sysfs constraint_1_time_window_us for Intel RAPL

``power_limit_1_tmax_us`` (RO)
	Maximum powercap sysfs constraint_1_time_window_us for Intel RAPL

``power_floor_status`` (RO)
	When set to 1, the power floor of the system in the current
	configuration has been reached.  It needs to be reconfigured to allow
	power to be reduced any further.

``power_floor_enable`` (RW)
	When set to 1, enable reading and notification of the power floor
	status. Notifications are triggered for the power_floor_status
	attribute value changes.

:file:`/sys/bus/pci/devices/0000\:00\:04.0/`

``tcc_offset_degree_celsius`` (RW)
	TCC offset from the critical temperature where hardware will throttle
	CPU.

:file:`/sys/bus/pci/devices/0000\:00\:04.0/workload_request`

``workload_available_types`` (RO)
	Available workload types. User space can specify one of the workload type
	it is currently executing via workload_type. For example: idle, bursty,
	sustained etc.

``workload_type`` (RW)
	User space can specify any one of the available workload type using
	this interface.

DPTF Processor thermal RFIM interface
--------------------------------------------

RFIM interface allows adjustment of FIVR (Fully Integrated Voltage Regulator),
DDR (Double Data Rate) and DLVR (Digital Linear Voltage Regulator)
frequencies to avoid RF interference with WiFi and 5G.

Switching voltage regulators (VR) generate radiated EMI or RFI at the
fundamental frequency and its harmonics. Some harmonics may interfere
with very sensitive wireless receivers such as Wi-Fi and cellular that
are integrated into host systems like notebook PCs.  One of mitigation
methods is requesting SOC integrated VR (IVR) switching frequency to a
small % and shift away the switching noise harmonic interference from
radio channels.  OEM or ODMs can use the driver to control SOC IVR
operation within the range where it does not impact IVR performance.

Some products use DLVR instead of FIVR as switching voltage regulator.
In this case attributes of DLVR must be adjusted instead of FIVR.

While shifting the frequencies additional clock noise can be introduced,
which is compensated by adjusting Spread spectrum percent. This helps
to reduce the clock noise to meet regulatory compliance. This spreading
% increases bandwidth of signal transmission and hence reduces the
effects of interference, noise and signal fading.

DRAM devices of DDR IO interface and their power plane can generate EMI
at the data rates. Similar to IVR control mechanism, Intel offers a
mechanism by which DDR data rates can be changed if several conditions
are met: there is strong RFI interference because of DDR; CPU power
management has no other restriction in changing DDR data rates;
PC ODMs enable this feature (real time DDR RFI Mitigation referred to as
DDR-RFIM) for Wi-Fi from BIOS.


FIVR attributes

:file:`/sys/bus/pci/devices/0000\:00\:04.0/fivr/`

``vco_ref_code_lo`` (RW)
	The VCO reference code is an 11-bit field and controls the FIVR
	switching frequency. This is the 3-bit LSB field.

``vco_ref_code_hi`` (RW)
	The VCO reference code is an 11-bit field and controls the FIVR
	switching frequency. This is the 8-bit MSB field.

``spread_spectrum_pct`` (RW)
	Set the FIVR spread spectrum clocking percentage

``spread_spectrum_clk_enable`` (RW)
	Enable/disable of the FIVR spread spectrum clocking feature

``rfi_vco_ref_code`` (RW)
	This field is a read only status register which reflects the
	current FIVR switching frequency

``fivr_fffc_rev`` (RW)
	This field indicated the revision of the FIVR HW.


DVFS attributes

:file:`/sys/bus/pci/devices/0000\:00\:04.0/dvfs/`

``rfi_restriction_run_busy`` (RW)
	Request the restriction of specific DDR data rate and set this
	value 1. Self reset to 0 after operation.

``rfi_restriction_err_code`` (RW)
	0 :Request is accepted, 1:Feature disabled,
	2: the request restricts more points than it is allowed

``rfi_restriction_data_rate_Delta`` (RW)
	Restricted DDR data rate for RFI protection: Lower Limit

``rfi_restriction_data_rate_Base`` (RW)
	Restricted DDR data rate for RFI protection: Upper Limit

``ddr_data_rate_point_0`` (RO)
	DDR data rate selection 1st point

``ddr_data_rate_point_1`` (RO)
	DDR data rate selection 2nd point

``ddr_data_rate_point_2`` (RO)
	DDR data rate selection 3rd point

``ddr_data_rate_point_3`` (RO)
	DDR data rate selection 4th point

``rfi_disable (RW)``
	Disable DDR rate change feature

DLVR attributes

:file:`/sys/bus/pci/devices/0000\:00\:04.0/dlvr/`

``dlvr_hardware_rev`` (RO)
	DLVR hardware revision.

``dlvr_freq_mhz`` (RO)
	Current DLVR PLL frequency in MHz.

``dlvr_freq_select`` (RW)
	Sets DLVR PLL clock frequency. Once set, and enabled via
	dlvr_rfim_enable, the dlvr_freq_mhz will show the current
	DLVR PLL frequency.

``dlvr_pll_busy`` (RO)
	PLL can't accept frequency change when set.

``dlvr_rfim_enable`` (RW)
	0: Disable RF frequency hopping, 1: Enable RF frequency hopping.

``dlvr_spread_spectrum_pct`` (RW)
	Sets DLVR spread spectrum percent value.

``dlvr_control_mode`` (RW)
        Specifies how frequencies are spread using spread spectrum.
        0: Down spread,
        1: Spread in the Center.

``dlvr_control_lock`` (RW)
    1: future writes are ignored.

DPTF Power supply and Battery Interface
----------------------------------------

Refer to Documentation/ABI/testing/sysfs-platform-dptf

DPTF Fan Control
----------------------------------------

Refer to Documentation/admin-guide/acpi/fan_performance_states.rst

Workload Type Hints
----------------------------------------

The firmware in Meteor Lake processor generation is capable of identifying
workload type and passing hints regarding it to the OS. A special sysfs
interface is provided to allow user space to obtain workload type hints from
the firmware and control the rate at which they are provided.

User space can poll attribute "workload_type_index" for the current hint or
can receive a notification whenever the value of this attribute is updated.

file:`/sys/bus/pci/devices/0000:00:04.0/workload_hint/`
Segment 0, bus 0, device 4, function 0 is reserved for the processor thermal
device on all Intel client processors. So, the above path doesn't change
based on the processor generation.

``workload_hint_enable`` (RW)
	Enable firmware to send workload type hints to user space.

``notification_delay_ms`` (RW)
	Minimum delay in milliseconds before firmware will notify OS. This is
	for the rate control of notifications. This delay is between changing
	the workload type prediction in the firmware and notifying the OS about
	the change. The default delay is 1024 ms. The delay of 0 is invalid.
	The delay is rounded up to the nearest power of 2 to simplify firmware
	programming of the delay value. The read of notification_delay_ms
	attribute shows the effective value used.

``workload_type_index`` (RO)
	Predicted workload type index. User space can get notification of
	change via existing sysfs attribute change notification mechanism.

	The supported index values and their meaning for the Meteor Lake
	processor generation are as follows:

	0 -  Idle: System performs no tasks, power and idle residency are
		consistently low for long periods of time.

	1 – Battery Life: Power is relatively low, but the processor may
		still be actively performing a task, such as video playback for
		a long period of time.

	2 – Sustained: Power level that is relatively high for a long period
		of time, with very few to no periods of idleness, which will
		eventually exhaust RAPL Power Limit 1 and 2.

	3 – Bursty: Consumes a relatively constant average amount of power, but
		periods of relative idleness are interrupted by bursts of
		activity. The bursts are relatively short and the periods of
		relative idleness between them typically prevent RAPL Power
		Limit 1 from being exhausted.

	4 – Unknown: Can't classify.
