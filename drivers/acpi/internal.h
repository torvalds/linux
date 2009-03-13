/* For use by Linux/ACPI infrastructure, not drivers */

/* --------------------------------------------------------------------------
                                  Power Resource
   -------------------------------------------------------------------------- */

int acpi_device_sleep_wake(struct acpi_device *dev,
                           int enable, int sleep_state, int dev_state);
int acpi_enable_wakeup_device_power(struct acpi_device *dev, int sleep_state);
int acpi_disable_wakeup_device_power(struct acpi_device *dev);
int acpi_power_get_inferred_state(struct acpi_device *device);
int acpi_power_transition(struct acpi_device *device, int state);
extern int acpi_power_nocheck;

/* --------------------------------------------------------------------------
                                  Embedded Controller
   -------------------------------------------------------------------------- */
int acpi_ec_ecdt_probe(void);
int acpi_boot_ec_enable(void);

/*--------------------------------------------------------------------------
                                  Suspend/Resume
  -------------------------------------------------------------------------- */
extern int acpi_sleep_init(void);
