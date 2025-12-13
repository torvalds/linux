// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI quirks for GPIO ACPI helpers
 *
 * Author: Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/dmi.h>
#include <linux/kstrtox.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/types.h>

#include "gpiolib-acpi.h"

static int run_edge_events_on_boot = -1;
module_param(run_edge_events_on_boot, int, 0444);
MODULE_PARM_DESC(run_edge_events_on_boot,
		 "Run edge _AEI event-handlers at boot: 0=no, 1=yes, -1=auto");

static char *ignore_wake;
module_param(ignore_wake, charp, 0444);
MODULE_PARM_DESC(ignore_wake,
		 "controller@pin combos on which to ignore the ACPI wake flag "
		 "ignore_wake=controller@pin[,controller@pin[,...]]");

static char *ignore_interrupt;
module_param(ignore_interrupt, charp, 0444);
MODULE_PARM_DESC(ignore_interrupt,
		 "controller@pin combos on which to ignore interrupt "
		 "ignore_interrupt=controller@pin[,controller@pin[,...]]");

/*
 * For GPIO chips which call acpi_gpiochip_request_interrupts() before late_init
 * (so builtin drivers) we register the ACPI GpioInt IRQ handlers from a
 * late_initcall_sync() handler, so that other builtin drivers can register their
 * OpRegions before the event handlers can run. This list contains GPIO chips
 * for which the acpi_gpiochip_request_irqs() call has been deferred.
 */
static DEFINE_MUTEX(acpi_gpio_deferred_req_irqs_lock);
static LIST_HEAD(acpi_gpio_deferred_req_irqs_list);
static bool acpi_gpio_deferred_req_irqs_done;

bool acpi_gpio_add_to_deferred_list(struct list_head *list)
{
	bool defer;

	mutex_lock(&acpi_gpio_deferred_req_irqs_lock);
	defer = !acpi_gpio_deferred_req_irqs_done;
	if (defer)
		list_add(list, &acpi_gpio_deferred_req_irqs_list);
	mutex_unlock(&acpi_gpio_deferred_req_irqs_lock);

	return defer;
}

void acpi_gpio_remove_from_deferred_list(struct list_head *list)
{
	mutex_lock(&acpi_gpio_deferred_req_irqs_lock);
	if (!list_empty(list))
		list_del_init(list);
	mutex_unlock(&acpi_gpio_deferred_req_irqs_lock);
}

int acpi_gpio_need_run_edge_events_on_boot(void)
{
	return run_edge_events_on_boot;
}

bool acpi_gpio_in_ignore_list(enum acpi_gpio_ignore_list list,
			      const char *controller_in, unsigned int pin_in)
{
	const char *ignore_list, *controller, *pin_str;
	unsigned int pin;
	char *endp;
	int len;

	switch (list) {
	case ACPI_GPIO_IGNORE_WAKE:
		ignore_list = ignore_wake;
		break;
	case ACPI_GPIO_IGNORE_INTERRUPT:
		ignore_list = ignore_interrupt;
		break;
	default:
		return false;
	}

	controller = ignore_list;
	while (controller) {
		pin_str = strchr(controller, '@');
		if (!pin_str)
			goto err;

		len = pin_str - controller;
		if (len == strlen(controller_in) &&
		    strncmp(controller, controller_in, len) == 0) {
			pin = simple_strtoul(pin_str + 1, &endp, 10);
			if (*endp != 0 && *endp != ',')
				goto err;

			if (pin == pin_in)
				return true;
		}

		controller = strchr(controller, ',');
		if (controller)
			controller++;
	}

	return false;
err:
	pr_err_once("Error: Invalid value for gpiolib_acpi.ignore_...: %s\n", ignore_list);
	return false;
}

/* Run deferred acpi_gpiochip_request_irqs() */
static int __init acpi_gpio_handle_deferred_request_irqs(void)
{
	mutex_lock(&acpi_gpio_deferred_req_irqs_lock);
	acpi_gpio_process_deferred_list(&acpi_gpio_deferred_req_irqs_list);
	acpi_gpio_deferred_req_irqs_done = true;
	mutex_unlock(&acpi_gpio_deferred_req_irqs_lock);

	return 0;
}
/* We must use _sync so that this runs after the first deferred_probe run */
late_initcall_sync(acpi_gpio_handle_deferred_request_irqs);

struct acpi_gpiolib_dmi_quirk {
	bool no_edge_events_on_boot;
	char *ignore_wake;
	char *ignore_interrupt;
};

static const struct dmi_system_id gpiolib_acpi_quirks[] __initconst = {
	{
		/*
		 * The Minix Neo Z83-4 has a micro-USB-B id-pin handler for
		 * a non existing micro-USB-B connector which puts the HDMI
		 * DDC pins in GPIO mode, breaking HDMI support.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MINIX"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Z83-4"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.no_edge_events_on_boot = true,
		},
	},
	{
		/*
		 * The Terra Pad 1061 has a micro-USB-B id-pin handler, which
		 * instead of controlling the actual micro-USB-B turns the 5V
		 * boost for its USB-A connector off. The actual micro-USB-B
		 * connector is wired for charging only.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Wortmann_AG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TERRA_PAD_1061"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.no_edge_events_on_boot = true,
		},
	},
	{
		/*
		 * The Dell Venue 10 Pro 5055, with Bay Trail SoC + TI PMIC uses an
		 * external embedded-controller connected via I2C + an ACPI GPIO
		 * event handler on INT33FFC:02 pin 12, causing spurious wakeups.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Venue 10 Pro 5055"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "INT33FC:02@12",
		},
	},
	{
		/*
		 * HP X2 10 models with Cherry Trail SoC + TI PMIC use an
		 * external embedded-controller connected via I2C + an ACPI GPIO
		 * event handler on INT33FF:01 pin 0, causing spurious wakeups.
		 * When suspending by closing the LID, the power to the USB
		 * keyboard is turned off, causing INT0002 ACPI events to
		 * trigger once the XHCI controller notices the keyboard is
		 * gone. So INT0002 events cause spurious wakeups too. Ignoring
		 * EC wakes breaks wakeup when opening the lid, the user needs
		 * to press the power-button to wakeup the system. The
		 * alternative is suspend simply not working, which is worse.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP x2 Detachable 10-p0XX"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "INT33FF:01@0,INT0002:00@2",
		},
	},
	{
		/*
		 * HP X2 10 models with Bay Trail SoC + AXP288 PMIC use an
		 * external embedded-controller connected via I2C + an ACPI GPIO
		 * event handler on INT33FC:02 pin 28, causing spurious wakeups.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion x2 Detachable"),
			DMI_MATCH(DMI_BOARD_NAME, "815D"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "INT33FC:02@28",
		},
	},
	{
		/*
		 * HP X2 10 models with Cherry Trail SoC + AXP288 PMIC use an
		 * external embedded-controller connected via I2C + an ACPI GPIO
		 * event handler on INT33FF:01 pin 0, causing spurious wakeups.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion x2 Detachable"),
			DMI_MATCH(DMI_BOARD_NAME, "813E"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "INT33FF:01@0",
		},
	},
	{
		/*
		 * Interrupt storm caused from edge triggered floating pin
		 * Found in BIOS UX325UAZ.300
		 * https://bugzilla.kernel.org/show_bug.cgi?id=216208
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "ZenBook UX325UAZ_UM325UAZ"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_interrupt = "AMDI0030:00@18",
		},
	},
	{
		/*
		 * Spurious wakeups from TP_ATTN# pin
		 * Found in BIOS 1.7.8
		 * https://gitlab.freedesktop.org/drm/amd/-/issues/1722#note_1720627
		 */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "NL5xNU"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "ELAN0415:00@9",
		},
	},
	{
		/*
		 * Spurious wakeups from TP_ATTN# pin
		 * Found in BIOS 1.7.8
		 * https://gitlab.freedesktop.org/drm/amd/-/issues/1722#note_1720627
		 */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "NL5xRU"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "ELAN0415:00@9",
		},
	},
	{
		/*
		 * Spurious wakeups from TP_ATTN# pin
		 * Found in BIOS 1.7.7
		 */
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "NH5xAx"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "SYNA1202:00@16",
		},
	},
	{
		/*
		 * On the Peaq C1010 2-in-1 INT33FC:00 pin 3 is connected to
		 * a "dolby" button. At the ACPI level an _AEI event-handler
		 * is connected which sets an ACPI variable to 1 on both
		 * edges. This variable can be polled + cleared to 0 using
		 * WMI. But since the variable is set on both edges the WMI
		 * interface is pretty useless even when polling.
		 * So instead the x86-android-tablets code instantiates
		 * a gpio-keys platform device for it.
		 * Ignore the _AEI handler for the pin, so that it is not busy.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "PEAQ"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PEAQ PMM C1010 MD99187"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_interrupt = "INT33FC:00@3",
		},
	},
	{
		/*
		 * Spurious wakeups from TP_ATTN# pin
		 * Found in BIOS 0.35
		 * https://gitlab.freedesktop.org/drm/amd/-/issues/3073
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1619-04"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "PNP0C50:00@8",
		},
	},
	{
		/*
		 * Same as G1619-04. New model.
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1619-05"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "PNP0C50:00@8",
		},
	},
	{
		/*
		 * Spurious wakeups from GPIO 11
		 * Found in BIOS 1.04
		 * https://gitlab.freedesktop.org/drm/amd/-/issues/3954
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Acer Nitro V 14"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_interrupt = "AMDI0030:00@11",
		},
	},
	{
		/*
		 * Wakeup only works when keyboard backlight is turned off
		 * https://gitlab.freedesktop.org/drm/amd/-/issues/4169
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Acer Nitro V 15"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_interrupt = "AMDI0030:00@8",
		},
	},
	{
		/*
		 * Spurious wakeups from TP_ATTN# pin
		 * Found in BIOS 5.35
		 * https://gitlab.freedesktop.org/drm/amd/-/issues/4482
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "ProArt PX13"),
		},
		.driver_data = &(struct acpi_gpiolib_dmi_quirk) {
			.ignore_wake = "ASCP1A00:00@8",
		},
	},
	{} /* Terminating entry */
};

static int __init acpi_gpio_setup_params(void)
{
	const struct acpi_gpiolib_dmi_quirk *quirk = NULL;
	const struct dmi_system_id *id;

	id = dmi_first_match(gpiolib_acpi_quirks);
	if (id)
		quirk = id->driver_data;

	if (run_edge_events_on_boot < 0) {
		if (quirk && quirk->no_edge_events_on_boot)
			run_edge_events_on_boot = 0;
		else
			run_edge_events_on_boot = 1;
	}

	if (ignore_wake == NULL && quirk && quirk->ignore_wake)
		ignore_wake = quirk->ignore_wake;

	if (ignore_interrupt == NULL && quirk && quirk->ignore_interrupt)
		ignore_interrupt = quirk->ignore_interrupt;

	return 0;
}

/* Directly after dmi_setup() which runs as core_initcall() */
postcore_initcall(acpi_gpio_setup_params);
