/* SPDX-License-Identifier: GPL-2.0 */

extern void acpi_enable_wakeup_devices(u8 sleep_state);
extern void acpi_disable_wakeup_devices(u8 sleep_state);

extern struct list_head acpi_wakeup_device_list;
extern struct mutex acpi_device_lock;

extern void acpi_resume_power_resources(void);
extern void acpi_turn_off_unused_power_resources(void);

static inline acpi_status acpi_set_waking_vector(u32 wakeup_address)
{
	return acpi_set_firmware_waking_vector(
				(acpi_physical_address)wakeup_address, 0);
}
