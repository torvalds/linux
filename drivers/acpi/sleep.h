/* SPDX-License-Identifier: GPL-2.0 */

extern void acpi_enable_wakeup_devices(u8 sleep_state);
extern void acpi_disable_wakeup_devices(u8 sleep_state);
extern bool acpi_check_wakeup_handlers(void);

extern struct list_head acpi_wakeup_device_list;
extern struct mutex acpi_device_lock;

extern void acpi_resume_power_resources(void);

static inline acpi_status acpi_set_waking_vector(u32 wakeup_address)
{
	return acpi_set_firmware_waking_vector(
				(acpi_physical_address)wakeup_address, 0);
}

extern int acpi_s2idle_begin(void);
extern int acpi_s2idle_prepare(void);
extern bool acpi_s2idle_wake(void);
extern void acpi_s2idle_restore(void);
extern void acpi_s2idle_end(void);
#if defined(CONFIG_X86) && defined(CONFIG_SUSPEND)
extern bool acpi_x86_s2idle_check_ixx_gpes(void);
#else
static inline bool acpi_x86_s2idle_check_ixx_gpes(void)
{
	return acpi_any_gpe_status_set(U32_MAX);
}
#endif

extern void acpi_s2idle_setup(void);

#ifdef CONFIG_ACPI_SLEEP
extern bool acpi_sleep_default_s3;
#else
#define acpi_sleep_default_s3	(1)
#endif
