// SPDX-License-Identifier: GPL-2.0
/*
 * Architecture-specific ACPI-based support for suspend-to-idle.
 *
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *
 * On platforms supporting the Low Power S0 Idle interface there is an ACPI
 * device object with the PNP0D80 compatible device ID (System Power Management
 * Controller) and a specific _DSM method under it.  That method, if present,
 * can be used to indicate to the platform that the OS is transitioning into a
 * low-power state in which certain types of activity are not desirable or that
 * it is leaving such a state, which allows the platform to adjust its operation
 * mode accordingly.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/suspend.h>

#include "../sleep.h"

#ifdef CONFIG_SUSPEND

static bool sleep_no_lps0 __read_mostly;
module_param(sleep_no_lps0, bool, 0644);
MODULE_PARM_DESC(sleep_no_lps0, "Do not use the special LPS0 device interface");

static const struct acpi_device_id lps0_device_ids[] = {
	{"PNP0D80", },
	{"", },
};

/* Microsoft platform agnostic UUID */
#define ACPI_LPS0_DSM_UUID_MICROSOFT      "11e00d56-ce64-47ce-837b-1f898f9aa461"

#define ACPI_LPS0_DSM_UUID	"c4eb40a0-6cd2-11e2-bcfd-0800200c9a66"

#define ACPI_LPS0_GET_DEVICE_CONSTRAINTS	1
#define ACPI_LPS0_SCREEN_OFF	3
#define ACPI_LPS0_SCREEN_ON	4
#define ACPI_LPS0_ENTRY		5
#define ACPI_LPS0_EXIT		6
#define ACPI_LPS0_MS_ENTRY      7
#define ACPI_LPS0_MS_EXIT       8

/* AMD */
#define ACPI_LPS0_DSM_UUID_AMD      "e3f32452-febc-43ce-9039-932122d37721"
#define ACPI_LPS0_ENTRY_AMD         2
#define ACPI_LPS0_EXIT_AMD          3
#define ACPI_LPS0_SCREEN_OFF_AMD    4
#define ACPI_LPS0_SCREEN_ON_AMD     5

static acpi_handle lps0_device_handle;
static guid_t lps0_dsm_guid;
static int lps0_dsm_func_mask;

static guid_t lps0_dsm_guid_microsoft;
static int lps0_dsm_func_mask_microsoft;

/* Device constraint entry structure */
struct lpi_device_info {
	char *name;
	int enabled;
	union acpi_object *package;
};

/* Constraint package structure */
struct lpi_device_constraint {
	int uid;
	int min_dstate;
	int function_states;
};

struct lpi_constraints {
	acpi_handle handle;
	int min_dstate;
};

/* AMD Constraint package structure */
struct lpi_device_constraint_amd {
	char *name;
	int enabled;
	int function_states;
	int min_dstate;
};

static LIST_HEAD(lps0_s2idle_devops_head);

static struct lpi_constraints *lpi_constraints_table;
static int lpi_constraints_table_size;
static int rev_id;

static void lpi_device_get_constraints_amd(void)
{
	union acpi_object *out_obj;
	int i, j, k;

	out_obj = acpi_evaluate_dsm_typed(lps0_device_handle, &lps0_dsm_guid,
					  rev_id, ACPI_LPS0_GET_DEVICE_CONSTRAINTS,
					  NULL, ACPI_TYPE_PACKAGE);

	acpi_handle_debug(lps0_device_handle, "_DSM function 1 eval %s\n",
			  out_obj ? "successful" : "failed");

	if (!out_obj)
		return;

	for (i = 0; i < out_obj->package.count; i++) {
		union acpi_object *package = &out_obj->package.elements[i];

		if (package->type == ACPI_TYPE_PACKAGE) {
			lpi_constraints_table = kcalloc(package->package.count,
							sizeof(*lpi_constraints_table),
							GFP_KERNEL);

			if (!lpi_constraints_table)
				goto free_acpi_buffer;

			acpi_handle_debug(lps0_device_handle,
					  "LPI: constraints list begin:\n");

			for (j = 0; j < package->package.count; ++j) {
				union acpi_object *info_obj = &package->package.elements[j];
				struct lpi_device_constraint_amd dev_info = {};
				struct lpi_constraints *list;
				acpi_status status;

				for (k = 0; k < info_obj->package.count; ++k) {
					union acpi_object *obj = &info_obj->package.elements[k];

					list = &lpi_constraints_table[lpi_constraints_table_size];
					list->min_dstate = -1;

					switch (k) {
					case 0:
						dev_info.enabled = obj->integer.value;
						break;
					case 1:
						dev_info.name = obj->string.pointer;
						break;
					case 2:
						dev_info.function_states = obj->integer.value;
						break;
					case 3:
						dev_info.min_dstate = obj->integer.value;
						break;
					}

					if (!dev_info.enabled || !dev_info.name ||
					    !dev_info.min_dstate)
						continue;

					status = acpi_get_handle(NULL, dev_info.name,
								 &list->handle);
					if (ACPI_FAILURE(status))
						continue;

					acpi_handle_debug(lps0_device_handle,
							  "Name:%s\n", dev_info.name);

					list->min_dstate = dev_info.min_dstate;

					if (list->min_dstate < 0) {
						acpi_handle_debug(lps0_device_handle,
								  "Incomplete constraint defined\n");
						continue;
					}
				}
				lpi_constraints_table_size++;
			}
		}
	}

	acpi_handle_debug(lps0_device_handle, "LPI: constraints list end\n");

free_acpi_buffer:
	ACPI_FREE(out_obj);
}

static void lpi_device_get_constraints(void)
{
	union acpi_object *out_obj;
	int i;

	out_obj = acpi_evaluate_dsm_typed(lps0_device_handle, &lps0_dsm_guid,
					  1, ACPI_LPS0_GET_DEVICE_CONSTRAINTS,
					  NULL, ACPI_TYPE_PACKAGE);

	acpi_handle_debug(lps0_device_handle, "_DSM function 1 eval %s\n",
			  out_obj ? "successful" : "failed");

	if (!out_obj)
		return;

	lpi_constraints_table = kcalloc(out_obj->package.count,
					sizeof(*lpi_constraints_table),
					GFP_KERNEL);
	if (!lpi_constraints_table)
		goto free_acpi_buffer;

	acpi_handle_debug(lps0_device_handle, "LPI: constraints list begin:\n");

	for (i = 0; i < out_obj->package.count; i++) {
		struct lpi_constraints *constraint;
		acpi_status status;
		union acpi_object *package = &out_obj->package.elements[i];
		struct lpi_device_info info = { };
		int package_count = 0, j;

		if (!package)
			continue;

		for (j = 0; j < package->package.count; ++j) {
			union acpi_object *element =
					&(package->package.elements[j]);

			switch (element->type) {
			case ACPI_TYPE_INTEGER:
				info.enabled = element->integer.value;
				break;
			case ACPI_TYPE_STRING:
				info.name = element->string.pointer;
				break;
			case ACPI_TYPE_PACKAGE:
				package_count = element->package.count;
				info.package = element->package.elements;
				break;
			}
		}

		if (!info.enabled || !info.package || !info.name)
			continue;

		constraint = &lpi_constraints_table[lpi_constraints_table_size];

		status = acpi_get_handle(NULL, info.name, &constraint->handle);
		if (ACPI_FAILURE(status))
			continue;

		acpi_handle_debug(lps0_device_handle,
				  "index:%d Name:%s\n", i, info.name);

		constraint->min_dstate = -1;

		for (j = 0; j < package_count; ++j) {
			union acpi_object *info_obj = &info.package[j];
			union acpi_object *cnstr_pkg;
			union acpi_object *obj;
			struct lpi_device_constraint dev_info;

			switch (info_obj->type) {
			case ACPI_TYPE_INTEGER:
				/* version */
				break;
			case ACPI_TYPE_PACKAGE:
				if (info_obj->package.count < 2)
					break;

				cnstr_pkg = info_obj->package.elements;
				obj = &cnstr_pkg[0];
				dev_info.uid = obj->integer.value;
				obj = &cnstr_pkg[1];
				dev_info.min_dstate = obj->integer.value;

				acpi_handle_debug(lps0_device_handle,
					"uid:%d min_dstate:%s\n",
					dev_info.uid,
					acpi_power_state_string(dev_info.min_dstate));

				constraint->min_dstate = dev_info.min_dstate;
				break;
			}
		}

		if (constraint->min_dstate < 0) {
			acpi_handle_debug(lps0_device_handle,
					  "Incomplete constraint defined\n");
			continue;
		}

		lpi_constraints_table_size++;
	}

	acpi_handle_debug(lps0_device_handle, "LPI: constraints list end\n");

free_acpi_buffer:
	ACPI_FREE(out_obj);
}

static void lpi_check_constraints(void)
{
	int i;

	for (i = 0; i < lpi_constraints_table_size; ++i) {
		acpi_handle handle = lpi_constraints_table[i].handle;
		struct acpi_device *adev = acpi_fetch_acpi_dev(handle);

		if (!adev)
			continue;

		acpi_handle_debug(handle,
			"LPI: required min power state:%s current power state:%s\n",
			acpi_power_state_string(lpi_constraints_table[i].min_dstate),
			acpi_power_state_string(adev->power.state));

		if (!adev->flags.power_manageable) {
			acpi_handle_info(handle, "LPI: Device not power manageable\n");
			lpi_constraints_table[i].handle = NULL;
			continue;
		}

		if (adev->power.state < lpi_constraints_table[i].min_dstate)
			acpi_handle_info(handle,
				"LPI: Constraint not met; min power state:%s current power state:%s\n",
				acpi_power_state_string(lpi_constraints_table[i].min_dstate),
				acpi_power_state_string(adev->power.state));
	}
}

static void acpi_sleep_run_lps0_dsm(unsigned int func, unsigned int func_mask, guid_t dsm_guid)
{
	union acpi_object *out_obj;

	if (!(func_mask & (1 << func)))
		return;

	out_obj = acpi_evaluate_dsm(lps0_device_handle, &dsm_guid,
					rev_id, func, NULL);
	ACPI_FREE(out_obj);

	acpi_handle_debug(lps0_device_handle, "_DSM function %u evaluation %s\n",
			  func, out_obj ? "successful" : "failed");
}

static bool acpi_s2idle_vendor_amd(void)
{
	return boot_cpu_data.x86_vendor == X86_VENDOR_AMD;
}

static int validate_dsm(acpi_handle handle, const char *uuid, int rev, guid_t *dsm_guid)
{
	union acpi_object *obj;
	int ret = -EINVAL;

	guid_parse(uuid, dsm_guid);
	obj = acpi_evaluate_dsm(handle, dsm_guid, rev, 0, NULL);

	/* Check if the _DSM is present and as expected. */
	if (!obj || obj->type != ACPI_TYPE_BUFFER || obj->buffer.length == 0 ||
	    obj->buffer.length > sizeof(u32)) {
		acpi_handle_debug(handle,
				"_DSM UUID %s rev %d function 0 evaluation failed\n", uuid, rev);
		goto out;
	}

	ret = *(int *)obj->buffer.pointer;
	acpi_handle_debug(handle, "_DSM UUID %s rev %d function mask: 0x%x\n", uuid, rev, ret);

out:
	ACPI_FREE(obj);
	return ret;
}

struct amd_lps0_hid_device_data {
	const bool check_off_by_one;
};

static const struct amd_lps0_hid_device_data amd_picasso = {
	.check_off_by_one = true,
};

static const struct amd_lps0_hid_device_data amd_cezanne = {
	.check_off_by_one = false,
};

static const struct acpi_device_id amd_hid_ids[] = {
	{"AMD0004",	(kernel_ulong_t)&amd_picasso,	},
	{"AMD0005",	(kernel_ulong_t)&amd_picasso,	},
	{"AMDI0005",	(kernel_ulong_t)&amd_picasso,	},
	{"AMDI0006",	(kernel_ulong_t)&amd_cezanne,	},
	{}
};

static int lps0_prefer_amd(const struct dmi_system_id *id)
{
	pr_debug("Using AMD GUID w/ _REV 2.\n");
	rev_id = 2;
	return 0;
}
static const struct dmi_system_id s2idle_dmi_table[] __initconst = {
	{
		/*
		 * AMD Rembrandt based HP EliteBook 835/845/865 G9
		 * Contains specialized AML in AMD/_REV 2 path to avoid
		 * triggering a bug in Qualcomm WLAN firmware. This may be
		 * removed in the future if that firmware is fixed.
		 */
		.callback = lps0_prefer_amd,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "HP"),
			DMI_MATCH(DMI_BOARD_NAME, "8990"),
		},
	},
	{}
};

static int lps0_device_attach(struct acpi_device *adev,
			      const struct acpi_device_id *not_used)
{
	if (lps0_device_handle)
		return 0;

	lps0_dsm_func_mask_microsoft = validate_dsm(adev->handle,
						    ACPI_LPS0_DSM_UUID_MICROSOFT, 0,
						    &lps0_dsm_guid_microsoft);
	if (acpi_s2idle_vendor_amd()) {
		static const struct acpi_device_id *dev_id;
		const struct amd_lps0_hid_device_data *data;

		for (dev_id = &amd_hid_ids[0]; dev_id->id[0]; dev_id++)
			if (acpi_dev_hid_uid_match(adev, dev_id->id, NULL))
				break;
		if (dev_id->id[0])
			data = (const struct amd_lps0_hid_device_data *) dev_id->driver_data;
		else
			data = &amd_cezanne;
		lps0_dsm_func_mask = validate_dsm(adev->handle,
					ACPI_LPS0_DSM_UUID_AMD, rev_id, &lps0_dsm_guid);
		if (lps0_dsm_func_mask > 0x3 && data->check_off_by_one) {
			lps0_dsm_func_mask = (lps0_dsm_func_mask << 1) | 0x1;
			acpi_handle_debug(adev->handle, "_DSM UUID %s: Adjusted function mask: 0x%x\n",
					  ACPI_LPS0_DSM_UUID_AMD, lps0_dsm_func_mask);
		} else if (lps0_dsm_func_mask_microsoft > 0 && rev_id) {
			lps0_dsm_func_mask_microsoft = -EINVAL;
			acpi_handle_debug(adev->handle, "_DSM Using AMD method\n");
		}
	} else {
		rev_id = 1;
		lps0_dsm_func_mask = validate_dsm(adev->handle,
					ACPI_LPS0_DSM_UUID, rev_id, &lps0_dsm_guid);
		lps0_dsm_func_mask_microsoft = -EINVAL;
	}

	if (lps0_dsm_func_mask < 0 && lps0_dsm_func_mask_microsoft < 0)
		return 0; //function evaluation failed

	lps0_device_handle = adev->handle;

	if (acpi_s2idle_vendor_amd())
		lpi_device_get_constraints_amd();
	else
		lpi_device_get_constraints();

	/*
	 * Use suspend-to-idle by default if ACPI_FADT_LOW_POWER_S0 is set in
	 * the FADT and the default suspend mode was not set from the command
	 * line.
	 */
	if ((acpi_gbl_FADT.flags & ACPI_FADT_LOW_POWER_S0) &&
	    mem_sleep_default > PM_SUSPEND_MEM && !acpi_sleep_default_s3) {
		mem_sleep_current = PM_SUSPEND_TO_IDLE;
		pr_info("Low-power S0 idle used by default for system suspend\n");
	}

	/*
	 * Some LPS0 systems, like ASUS Zenbook UX430UNR/i7-8550U, require the
	 * EC GPE to be enabled while suspended for certain wakeup devices to
	 * work, so mark it as wakeup-capable.
	 */
	acpi_ec_mark_gpe_for_wake();

	return 0;
}

static struct acpi_scan_handler lps0_handler = {
	.ids = lps0_device_ids,
	.attach = lps0_device_attach,
};

int acpi_s2idle_prepare_late(void)
{
	struct acpi_s2idle_dev_ops *handler;

	if (!lps0_device_handle || sleep_no_lps0)
		return 0;

	if (pm_debug_messages_on)
		lpi_check_constraints();

	/* Screen off */
	if (lps0_dsm_func_mask > 0)
		acpi_sleep_run_lps0_dsm(acpi_s2idle_vendor_amd() ?
					ACPI_LPS0_SCREEN_OFF_AMD :
					ACPI_LPS0_SCREEN_OFF,
					lps0_dsm_func_mask, lps0_dsm_guid);

	if (lps0_dsm_func_mask_microsoft > 0)
		acpi_sleep_run_lps0_dsm(ACPI_LPS0_SCREEN_OFF,
				lps0_dsm_func_mask_microsoft, lps0_dsm_guid_microsoft);

	/* LPS0 entry */
	if (lps0_dsm_func_mask > 0)
		acpi_sleep_run_lps0_dsm(acpi_s2idle_vendor_amd() ?
					ACPI_LPS0_ENTRY_AMD :
					ACPI_LPS0_ENTRY,
					lps0_dsm_func_mask, lps0_dsm_guid);
	if (lps0_dsm_func_mask_microsoft > 0) {
		acpi_sleep_run_lps0_dsm(ACPI_LPS0_ENTRY,
				lps0_dsm_func_mask_microsoft, lps0_dsm_guid_microsoft);
		/* modern standby entry */
		acpi_sleep_run_lps0_dsm(ACPI_LPS0_MS_ENTRY,
				lps0_dsm_func_mask_microsoft, lps0_dsm_guid_microsoft);
	}

	list_for_each_entry(handler, &lps0_s2idle_devops_head, list_node) {
		if (handler->prepare)
			handler->prepare();
	}

	return 0;
}

void acpi_s2idle_check(void)
{
	struct acpi_s2idle_dev_ops *handler;

	if (!lps0_device_handle || sleep_no_lps0)
		return;

	list_for_each_entry(handler, &lps0_s2idle_devops_head, list_node) {
		if (handler->check)
			handler->check();
	}
}

void acpi_s2idle_restore_early(void)
{
	struct acpi_s2idle_dev_ops *handler;

	if (!lps0_device_handle || sleep_no_lps0)
		return;

	list_for_each_entry(handler, &lps0_s2idle_devops_head, list_node)
		if (handler->restore)
			handler->restore();

	/* Modern standby exit */
	if (lps0_dsm_func_mask_microsoft > 0)
		acpi_sleep_run_lps0_dsm(ACPI_LPS0_MS_EXIT,
				lps0_dsm_func_mask_microsoft, lps0_dsm_guid_microsoft);

	/* LPS0 exit */
	if (lps0_dsm_func_mask > 0)
		acpi_sleep_run_lps0_dsm(acpi_s2idle_vendor_amd() ?
					ACPI_LPS0_EXIT_AMD :
					ACPI_LPS0_EXIT,
					lps0_dsm_func_mask, lps0_dsm_guid);
	if (lps0_dsm_func_mask_microsoft > 0)
		acpi_sleep_run_lps0_dsm(ACPI_LPS0_EXIT,
				lps0_dsm_func_mask_microsoft, lps0_dsm_guid_microsoft);

	/* Screen on */
	if (lps0_dsm_func_mask_microsoft > 0)
		acpi_sleep_run_lps0_dsm(ACPI_LPS0_SCREEN_ON,
				lps0_dsm_func_mask_microsoft, lps0_dsm_guid_microsoft);
	if (lps0_dsm_func_mask > 0)
		acpi_sleep_run_lps0_dsm(acpi_s2idle_vendor_amd() ?
					ACPI_LPS0_SCREEN_ON_AMD :
					ACPI_LPS0_SCREEN_ON,
					lps0_dsm_func_mask, lps0_dsm_guid);
}

static const struct platform_s2idle_ops acpi_s2idle_ops_lps0 = {
	.begin = acpi_s2idle_begin,
	.prepare = acpi_s2idle_prepare,
	.prepare_late = acpi_s2idle_prepare_late,
	.check = acpi_s2idle_check,
	.wake = acpi_s2idle_wake,
	.restore_early = acpi_s2idle_restore_early,
	.restore = acpi_s2idle_restore,
	.end = acpi_s2idle_end,
};

void __init acpi_s2idle_setup(void)
{
	dmi_check_system(s2idle_dmi_table);
	acpi_scan_add_handler(&lps0_handler);
	s2idle_set_ops(&acpi_s2idle_ops_lps0);
}

int acpi_register_lps0_dev(struct acpi_s2idle_dev_ops *arg)
{
	unsigned int sleep_flags;

	if (!lps0_device_handle || sleep_no_lps0)
		return -ENODEV;

	sleep_flags = lock_system_sleep();
	list_add(&arg->list_node, &lps0_s2idle_devops_head);
	unlock_system_sleep(sleep_flags);

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_register_lps0_dev);

void acpi_unregister_lps0_dev(struct acpi_s2idle_dev_ops *arg)
{
	unsigned int sleep_flags;

	if (!lps0_device_handle || sleep_no_lps0)
		return;

	sleep_flags = lock_system_sleep();
	list_del(&arg->list_node);
	unlock_system_sleep(sleep_flags);
}
EXPORT_SYMBOL_GPL(acpi_unregister_lps0_dev);

#endif /* CONFIG_SUSPEND */
