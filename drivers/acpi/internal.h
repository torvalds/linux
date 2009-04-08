/* For use by Linux/ACPI infrastructure, not drivers */

int init_acpi_device_notify(void);
int acpi_scan_init(void);
int acpi_system_init(void);

#ifdef CONFIG_ACPI_DEBUG
int acpi_debug_init(void);
#else
static inline int acpi_debug_init(void) { return 0; }
#endif

/* --------------------------------------------------------------------------
                                  Power Resource
   -------------------------------------------------------------------------- */
int acpi_power_init(void);
int acpi_device_sleep_wake(struct acpi_device *dev,
                           int enable, int sleep_state, int dev_state);
int acpi_enable_wakeup_device_power(struct acpi_device *dev, int sleep_state);
int acpi_disable_wakeup_device_power(struct acpi_device *dev);
int acpi_power_get_inferred_state(struct acpi_device *device);
int acpi_power_transition(struct acpi_device *device, int state);
extern int acpi_power_nocheck;

int acpi_wakeup_device_init(void);

/* --------------------------------------------------------------------------
                                  Embedded Controller
   -------------------------------------------------------------------------- */
int acpi_ec_init(void);
int acpi_ec_ecdt_probe(void);
int acpi_boot_ec_enable(void);

/*--------------------------------------------------------------------------
                                  Suspend/Resume
  -------------------------------------------------------------------------- */
extern int acpi_sleep_init(void);

#ifdef CONFIG_ACPI_SLEEP
int acpi_sleep_proc_init(void);
#else
static inline int acpi_sleep_proc_init(void) { return 0; }
#endif
