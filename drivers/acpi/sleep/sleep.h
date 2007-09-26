
extern u8 sleep_states[];
extern int acpi_suspend (u32 state);

extern void acpi_enable_wakeup_device_prep(u8 sleep_state);
extern void acpi_enable_wakeup_device(u8 sleep_state);
extern void acpi_disable_wakeup_device(u8 sleep_state);

extern int acpi_sleep_prepare(u32 acpi_state);
