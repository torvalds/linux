// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Asus Armoury (WMI) attributes driver.
 *
 * This driver uses the fw_attributes class to expose various WMI functions
 * that are present in many gaming and some non-gaming ASUS laptops.
 *
 * These typically don't fit anywhere else in the sysfs such as under LED class,
 * hwmon or others, and are set in Windows using the ASUS Armoury Crate tool.
 *
 * Copyright(C) 2024 Luke Jones <luke@ljones.dev>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/asus-wmi.h>
#include <linux/printk.h>
#include <linux/power_supply.h>
#include <linux/sysfs.h>

#include "asus-armoury.h"
#include "firmware_attributes_class.h"

#define ASUS_NB_WMI_EVENT_GUID "0B3CBB35-E3C2-45ED-91C2-4C5A6D195D1C"

#define ASUS_MINI_LED_MODE_MASK   GENMASK(1, 0)
/* Standard modes for devices with only on/off */
#define ASUS_MINI_LED_OFF         0x00
#define ASUS_MINI_LED_ON          0x01
/* Like "on" but the effect is more vibrant or brighter */
#define ASUS_MINI_LED_STRONG_MODE 0x02
/* New modes for devices with 3 mini-led mode types */
#define ASUS_MINI_LED_2024_WEAK   0x00
#define ASUS_MINI_LED_2024_STRONG 0x01
#define ASUS_MINI_LED_2024_OFF    0x02

/* Power tunable attribute name defines */
#define ATTR_PPT_PL1_SPL        "ppt_pl1_spl"
#define ATTR_PPT_PL2_SPPT       "ppt_pl2_sppt"
#define ATTR_PPT_PL3_FPPT       "ppt_pl3_fppt"
#define ATTR_PPT_APU_SPPT       "ppt_apu_sppt"
#define ATTR_PPT_PLATFORM_SPPT  "ppt_platform_sppt"
#define ATTR_NV_DYNAMIC_BOOST   "nv_dynamic_boost"
#define ATTR_NV_TEMP_TARGET     "nv_temp_target"
#define ATTR_NV_BASE_TGP        "nv_base_tgp"
#define ATTR_NV_TGP             "nv_tgp"

#define ASUS_ROG_TUNABLE_DC 0
#define ASUS_ROG_TUNABLE_AC 1

struct rog_tunables {
	const struct power_limits *power_limits;
	u32 ppt_pl1_spl;			// cpu
	u32 ppt_pl2_sppt;			// cpu
	u32 ppt_pl3_fppt;			// cpu
	u32 ppt_apu_sppt;			// plat
	u32 ppt_platform_sppt;		// plat

	u32 nv_dynamic_boost;
	u32 nv_temp_target;
	u32 nv_tgp;
};

struct asus_armoury_priv {
	struct device *fw_attr_dev;
	struct kset *fw_attr_kset;

	/*
	 * Mutex to protect eGPU activation/deactivation
	 * sequences and dGPU connection status:
	 * do not allow concurrent changes or changes
	 * before a reboot if dGPU got disabled.
	 */
	struct mutex egpu_mutex;

	/* Index 0 for DC, 1 for AC */
	struct rog_tunables *rog_tunables[2];

	u32 mini_led_dev_id;
	u32 gpu_mux_dev_id;
};

static struct asus_armoury_priv asus_armoury = {
	.egpu_mutex = __MUTEX_INITIALIZER(asus_armoury.egpu_mutex),
};

struct fw_attrs_group {
	bool pending_reboot;
};

static struct fw_attrs_group fw_attrs = {
	.pending_reboot = false,
};

struct asus_attr_group {
	const struct attribute_group *attr_group;
	u32 wmi_devid;
};

static void asus_set_reboot_and_signal_event(void)
{
	fw_attrs.pending_reboot = true;
	kobject_uevent(&asus_armoury.fw_attr_dev->kobj, KOBJ_CHANGE);
}

static ssize_t pending_reboot_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", fw_attrs.pending_reboot);
}

static struct kobj_attribute pending_reboot = __ATTR_RO(pending_reboot);

static bool asus_bios_requires_reboot(struct kobj_attribute *attr)
{
	return !strcmp(attr->attr.name, "gpu_mux_mode") ||
	       !strcmp(attr->attr.name, "panel_hd_mode");
}

/**
 * armoury_has_devstate() - Check presence of the WMI function state.
 *
 * @dev_id: The WMI method ID to check for presence.
 *
 * Returns: true iif method is supported.
 */
static bool armoury_has_devstate(u32 dev_id)
{
	u32 retval;
	int status;

	status = asus_wmi_evaluate_method(ASUS_WMI_METHODID_DSTS, dev_id, 0, &retval);
	pr_debug("%s called (0x%08x), retval: 0x%08x\n", __func__, dev_id, retval);

	return status == 0 && (retval & ASUS_WMI_DSTS_PRESENCE_BIT);
}

/**
 * armoury_get_devstate() - Get the WMI function state.
 * @attr: NULL or the kobj_attribute associated to called WMI function.
 * @dev_id: The WMI method ID to call.
 * @retval:
 * * non-NULL pointer to where to store the value returned from WMI
 * * with the function presence bit cleared.
 *
 * Intended usage is from sysfs attribute checking associated WMI function.
 *
 * Returns:
 * * %-ENODEV	- method ID is unsupported.
 * * %0		- successful and retval is filled.
 * * %other	- error from WMI call.
 */
static int armoury_get_devstate(struct kobj_attribute *attr, u32 *retval, u32 dev_id)
{
	int err;

	err = asus_wmi_get_devstate_dsts(dev_id, retval);
	if (err) {
		if (attr)
			pr_err("Failed to get %s: %d\n", attr->attr.name, err);
		else
			pr_err("Failed to get devstate for 0x%x: %d\n", dev_id, err);

		return err;
	}

	/*
	 * asus_wmi_get_devstate_dsts will populate retval with WMI return, but
	 * the true value is expressed when ASUS_WMI_DSTS_PRESENCE_BIT is clear.
	 */
	*retval &= ~ASUS_WMI_DSTS_PRESENCE_BIT;

	return 0;
}

/**
 * armoury_set_devstate() - Set the WMI function state.
 * @attr: The kobj_attribute associated to called WMI function.
 * @dev_id: The WMI method ID to call.
 * @value: The new value to be set.
 * @retval: Where to store the value returned from WMI or NULL.
 *
 * Intended usage is from sysfs attribute setting associated WMI function.
 * Before calling the presence of the function should be checked.
 *
 * Every WMI write MUST go through this function to enforce safety checks.
 *
 * Results !1 is usually considered a fail by ASUS, but some WMI methods
 * (like eGPU or CPU cores) do use > 1 to return a status code or similar:
 * in these cases caller is interested in the actual return value
 * and should perform relevant checks.
 *
 * Returns:
 * * %-EINVAL	- attempt to set a dangerous or unsupported value.
 * * %-EIO	- WMI function returned an error.
 * * %0		- successful and retval is filled.
 * * %other	- error from WMI call.
 */
static int armoury_set_devstate(struct kobj_attribute *attr,
				     u32 value, u32 *retval, u32 dev_id)
{
	u32 result;
	int err;

	/*
	 * Prevent developers from bricking devices or issuing dangerous
	 * commands that can be difficult or impossible to recover from.
	 */
	switch (dev_id) {
	case ASUS_WMI_DEVID_APU_MEM:
		/*
		 * A hard reset might suffice to save the device,
		 * but there is no value in sending these commands.
		 */
		if (value == 0x100 || value == 0x101) {
			pr_err("Refusing to set APU memory to unsafe value: 0x%x\n", value);
			return -EINVAL;
		}
		break;
	default:
		/* No problems are known for this dev_id */
		break;
	}

	err = asus_wmi_set_devstate(dev_id, value, retval ? retval : &result);
	if (err) {
		if (attr)
			pr_err("Failed to set %s: %d\n", attr->attr.name, err);
		else
			pr_err("Failed to set devstate for 0x%x: %d\n", dev_id, err);

		return err;
	}

	/*
	 * If retval == NULL caller is uninterested in return value:
	 * perform the most common result check here.
	 */
	if ((retval == NULL) && (result == 0)) {
		pr_err("Failed to set %s: (result): 0x%x\n", attr->attr.name, result);
		return -EIO;
	}

	return 0;
}

static int armoury_attr_enum_list(char *buf, size_t enum_values)
{
	size_t i;
	int len = 0;

	for (i = 0; i < enum_values; i++) {
		if (i == 0)
			len += sysfs_emit_at(buf, len, "%zu", i);
		else
			len += sysfs_emit_at(buf, len, ";%zu", i);
	}
	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

ssize_t armoury_attr_uint_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count, u32 min, u32 max,
				u32 *store_value, u32 wmi_dev)
{
	u32 value;
	int err;

	err = kstrtou32(buf, 10, &value);
	if (err)
		return err;

	if (value < min || value > max)
		return -EINVAL;

	err = armoury_set_devstate(attr, value, NULL, wmi_dev);
	if (err)
		return err;

	if (store_value != NULL)
		*store_value = value;
	sysfs_notify(kobj, NULL, attr->attr.name);

	if (asus_bios_requires_reboot(attr))
		asus_set_reboot_and_signal_event();

	return count;
}

ssize_t armoury_attr_uint_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf, u32 wmi_dev)
{
	u32 result;
	int err;

	err = armoury_get_devstate(attr, &result, wmi_dev);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", result);
}

static ssize_t enum_type_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
	return sysfs_emit(buf, "enumeration\n");
}

static ssize_t int_type_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sysfs_emit(buf, "integer\n");
}

/* Mini-LED mode **************************************************************/

/* Values map for mini-led modes on 2023 and earlier models. */
static u32 mini_led_mode1_map[] = {
	[0] = ASUS_MINI_LED_OFF,
	[1] = ASUS_MINI_LED_ON,
};

/* Values map for mini-led modes on 2024 and later models. */
static u32 mini_led_mode2_map[] = {
	[0] = ASUS_MINI_LED_2024_OFF,
	[1] = ASUS_MINI_LED_2024_WEAK,
	[2] = ASUS_MINI_LED_2024_STRONG,
};

static ssize_t mini_led_mode_current_value_show(struct kobject *kobj,
						struct kobj_attribute *attr, char *buf)
{
	u32 *mini_led_mode_map;
	size_t mini_led_mode_map_size;
	u32 i, mode;
	int err;

	switch (asus_armoury.mini_led_dev_id) {
	case ASUS_WMI_DEVID_MINI_LED_MODE:
		mini_led_mode_map = mini_led_mode1_map;
		mini_led_mode_map_size = ARRAY_SIZE(mini_led_mode1_map);
		break;

	case ASUS_WMI_DEVID_MINI_LED_MODE2:
		mini_led_mode_map = mini_led_mode2_map;
		mini_led_mode_map_size = ARRAY_SIZE(mini_led_mode2_map);
		break;

	default:
		pr_err("Unrecognized mini-LED device: %u\n", asus_armoury.mini_led_dev_id);
		return -ENODEV;
	}

	err = armoury_get_devstate(attr, &mode, asus_armoury.mini_led_dev_id);
	if (err)
		return err;

	mode = FIELD_GET(ASUS_MINI_LED_MODE_MASK, 0);

	for (i = 0; i < mini_led_mode_map_size; i++)
		if (mode == mini_led_mode_map[i])
			return sysfs_emit(buf, "%u\n", i);

	pr_warn("Unrecognized mini-LED mode: %u", mode);
	return -EINVAL;
}

static ssize_t mini_led_mode_current_value_store(struct kobject *kobj,
						 struct kobj_attribute *attr,
						 const char *buf, size_t count)
{
	u32 *mini_led_mode_map;
	size_t mini_led_mode_map_size;
	u32 mode;
	int err;

	err = kstrtou32(buf, 10, &mode);
	if (err)
		return err;

	switch (asus_armoury.mini_led_dev_id) {
	case ASUS_WMI_DEVID_MINI_LED_MODE:
		mini_led_mode_map = mini_led_mode1_map;
		mini_led_mode_map_size = ARRAY_SIZE(mini_led_mode1_map);
		break;

	case ASUS_WMI_DEVID_MINI_LED_MODE2:
		mini_led_mode_map = mini_led_mode2_map;
		mini_led_mode_map_size = ARRAY_SIZE(mini_led_mode2_map);
		break;

	default:
		pr_err("Unrecognized mini-LED devid: %u\n", asus_armoury.mini_led_dev_id);
		return -EINVAL;
	}

	if (mode >= mini_led_mode_map_size) {
		pr_warn("mini-LED mode unrecognized device: %u\n", mode);
		return -ENODEV;
	}

	return armoury_attr_uint_store(kobj, attr, buf, count,
				       0, mini_led_mode_map[mode],
				       NULL, asus_armoury.mini_led_dev_id);
}

static ssize_t mini_led_mode_possible_values_show(struct kobject *kobj,
						  struct kobj_attribute *attr, char *buf)
{
	switch (asus_armoury.mini_led_dev_id) {
	case ASUS_WMI_DEVID_MINI_LED_MODE:
		return armoury_attr_enum_list(buf, ARRAY_SIZE(mini_led_mode1_map));
	case ASUS_WMI_DEVID_MINI_LED_MODE2:
		return armoury_attr_enum_list(buf, ARRAY_SIZE(mini_led_mode2_map));
	default:
		return -ENODEV;
	}
}
ASUS_ATTR_GROUP_ENUM(mini_led_mode, "mini_led_mode", "Set the mini-LED backlight mode");

static ssize_t gpu_mux_mode_current_value_store(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t count)
{
	int result, err;
	bool optimus;

	err = kstrtobool(buf, &optimus);
	if (err)
		return err;

	if (armoury_has_devstate(ASUS_WMI_DEVID_DGPU)) {
		err = armoury_get_devstate(NULL, &result, ASUS_WMI_DEVID_DGPU);
		if (err)
			return err;
		if (result && !optimus) {
			pr_warn("Cannot switch MUX to dGPU mode when dGPU is disabled: %02X\n",
				result);
			return -ENODEV;
		}
	}

	if (armoury_has_devstate(ASUS_WMI_DEVID_EGPU)) {
		err = armoury_get_devstate(NULL, &result, ASUS_WMI_DEVID_EGPU);
		if (err)
			return err;
		if (result && !optimus) {
			pr_warn("Cannot switch MUX to dGPU mode when eGPU is enabled\n");
			return -EBUSY;
		}
	}

	err = armoury_set_devstate(attr, optimus ? 1 : 0, NULL, asus_armoury.gpu_mux_dev_id);
	if (err)
		return err;

	sysfs_notify(kobj, NULL, attr->attr.name);
	asus_set_reboot_and_signal_event();

	return count;
}
ASUS_WMI_SHOW_INT(gpu_mux_mode_current_value, asus_armoury.gpu_mux_dev_id);
ASUS_ATTR_GROUP_BOOL(gpu_mux_mode, "gpu_mux_mode", "Set the GPU display MUX mode");

static ssize_t dgpu_disable_current_value_store(struct kobject *kobj,
						struct kobj_attribute *attr, const char *buf,
						size_t count)
{
	int result, err;
	bool disable;

	err = kstrtobool(buf, &disable);
	if (err)
		return err;

	if (asus_armoury.gpu_mux_dev_id) {
		err = armoury_get_devstate(NULL, &result, asus_armoury.gpu_mux_dev_id);
		if (err)
			return err;
		if (!result && disable) {
			pr_warn("Cannot disable dGPU when the MUX is in dGPU mode\n");
			return -EBUSY;
		}
	}

	scoped_guard(mutex, &asus_armoury.egpu_mutex) {
		err = armoury_set_devstate(attr, disable ? 1 : 0, NULL, ASUS_WMI_DEVID_DGPU);
		if (err)
			return err;
	}

	sysfs_notify(kobj, NULL, attr->attr.name);

	return count;
}
ASUS_WMI_SHOW_INT(dgpu_disable_current_value, ASUS_WMI_DEVID_DGPU);
ASUS_ATTR_GROUP_BOOL(dgpu_disable, "dgpu_disable", "Disable the dGPU");

/* Values map for eGPU activation requests. */
static u32 egpu_status_map[] = {
	[0] = 0x00000000U,
	[1] = 0x00000001U,
	[2] = 0x00000101U,
	[3] = 0x00000201U,
};

/*
 * armoury_pci_rescan() - Performs a PCI rescan
 *
 * Bring up any GPU that has been hotplugged in the system.
 */
static void armoury_pci_rescan(void)
{
	struct pci_bus *b = NULL;

	pci_lock_rescan_remove();
	while ((b = pci_find_next_bus(b)) != NULL)
		pci_rescan_bus(b);
	pci_unlock_rescan_remove();
}

/*
 * The ACPI call to enable the eGPU might also disable the internal dGPU,
 * but this is not always the case and on certain models enabling the eGPU
 * when the dGPU is either still active or has been disabled without rebooting
 * will make both GPUs malfunction and the kernel will detect many
 * PCI AER unrecoverable errors.
 */
static ssize_t egpu_enable_current_value_store(struct kobject *kobj, struct kobj_attribute *attr,
							const char *buf, size_t count)
{
	int err;
	u32 requested, enable, result;

	err = kstrtou32(buf, 10, &requested);
	if (err)
		return err;

	if (requested >= ARRAY_SIZE(egpu_status_map))
		return -EINVAL;
	enable = egpu_status_map[requested];

	scoped_guard(mutex, &asus_armoury.egpu_mutex) {
		/* Ensure the eGPU is connected before attempting to activate it. */
		if (enable) {
			err = armoury_get_devstate(NULL, &result, ASUS_WMI_DEVID_EGPU_CONNECTED);
			if (err) {
				pr_warn("Failed to get eGPU connection status: %d\n", err);
				return err;
			}
			if (!result) {
				pr_warn("Cannot activate eGPU while undetected\n");
				return -ENOENT;
			}
		}

		if (asus_armoury.gpu_mux_dev_id) {
			err = armoury_get_devstate(NULL, &result, asus_armoury.gpu_mux_dev_id);
			if (err)
				return err;

			if (!result && enable) {
				pr_warn("Cannot enable eGPU when the MUX is in dGPU mode\n");
				return -ENODEV;
			}
		}

		err = armoury_set_devstate(attr, enable, &result, ASUS_WMI_DEVID_EGPU);
		if (err) {
			pr_err("Failed to set %s: %d\n", attr->attr.name, err);
			return err;
		}

		/*
		 * ACPI returns value 0x01 on success and 0x02 on a partial activation:
		 * performing a pci rescan will bring up the device in pci-e 3.0 speed,
		 * after a reboot the device will work at full speed.
		 */
		switch (result) {
		case 0x01:
			/*
			 * When a GPU is in use it does not get disconnected even if
			 * the ACPI call returns a success.
			 */
			if (!enable) {
				err = armoury_get_devstate(attr, &result, ASUS_WMI_DEVID_EGPU);
				if (err) {
					pr_warn("Failed to ensure eGPU is deactivated: %d\n", err);
					return err;
				}

				if (result != 0)
					return -EBUSY;
			}

			pr_debug("Success changing the eGPU status\n");
			break;
		case 0x02:
			pr_info("Success changing the eGPU status, a reboot is strongly advised\n");
			asus_set_reboot_and_signal_event();
			break;
		default:
			pr_err("Failed to change the eGPU status: wmi result is 0x%x\n", result);
			return -EIO;
		}
	}

	/*
	 * Perform a PCI rescan: on every tested model this is necessary
	 * to make the eGPU visible on the bus without rebooting.
	 */
	armoury_pci_rescan();

	sysfs_notify(kobj, NULL, attr->attr.name);

	return count;
}

static ssize_t egpu_enable_current_value_show(struct kobject *kobj, struct kobj_attribute *attr,
						char *buf)
{
	int i, err;
	u32 status;

	scoped_guard(mutex, &asus_armoury.egpu_mutex) {
		err = armoury_get_devstate(attr, &status, ASUS_WMI_DEVID_EGPU);
		if (err)
			return err;
	}

	for (i = 0; i < ARRAY_SIZE(egpu_status_map); i++) {
		if (egpu_status_map[i] == status)
			return sysfs_emit(buf, "%u\n", i);
	}

	return -EIO;
}

static ssize_t egpu_enable_possible_values_show(struct kobject *kobj, struct kobj_attribute *attr,
						char *buf)
{
	return armoury_attr_enum_list(buf, ARRAY_SIZE(egpu_status_map));
}
ASUS_ATTR_GROUP_ENUM(egpu_enable, "egpu_enable", "Enable the eGPU (also disables dGPU)");

/* Device memory available to APU */

/*
 * Values map for APU reserved memory (index + 1 number of GB).
 * Some looks out of order, but are actually correct.
 */
static u32 apu_mem_map[] = {
	[0] = 0x000, /* called "AUTO" on the BIOS, is the minimum available */
	[1] = 0x102,
	[2] = 0x103,
	[3] = 0x104,
	[4] = 0x105,
	[5] = 0x107,
	[6] = 0x108,
	[7] = 0x109,
	[8] = 0x106,
};

static ssize_t apu_mem_current_value_show(struct kobject *kobj, struct kobj_attribute *attr,
					  char *buf)
{
	int err;
	u32 mem;

	err = armoury_get_devstate(attr, &mem, ASUS_WMI_DEVID_APU_MEM);
	if (err)
		return err;

	/* After 0x000 is set, a read will return 0x100 */
	if (mem == 0x100)
		return sysfs_emit(buf, "0\n");

	for (unsigned int i = 0; i < ARRAY_SIZE(apu_mem_map); i++) {
		if (apu_mem_map[i] == mem)
			return sysfs_emit(buf, "%u\n", i);
	}

	pr_warn("Unrecognised value for APU mem 0x%08x\n", mem);
	return -EIO;
}

static ssize_t apu_mem_current_value_store(struct kobject *kobj, struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	int result, err;
	u32 requested, mem;

	result = kstrtou32(buf, 10, &requested);
	if (result)
		return result;

	if (requested >= ARRAY_SIZE(apu_mem_map))
		return -EINVAL;
	mem = apu_mem_map[requested];

	err = armoury_set_devstate(attr, mem, NULL, ASUS_WMI_DEVID_APU_MEM);
	if (err) {
		pr_warn("Failed to set apu_mem 0x%x: %d\n", mem, err);
		return err;
	}

	pr_info("APU memory changed to %uGB, reboot required\n", requested + 1);
	sysfs_notify(kobj, NULL, attr->attr.name);

	asus_set_reboot_and_signal_event();

	return count;
}

static ssize_t apu_mem_possible_values_show(struct kobject *kobj, struct kobj_attribute *attr,
					    char *buf)
{
	return armoury_attr_enum_list(buf, ARRAY_SIZE(apu_mem_map));
}
ASUS_ATTR_GROUP_ENUM(apu_mem, "apu_mem", "Set available system RAM (in GB) for the APU to use");

/* Define helper to access the current power mode tunable values */
static inline struct rog_tunables *get_current_tunables(void)
{
	if (power_supply_is_system_supplied())
		return asus_armoury.rog_tunables[ASUS_ROG_TUNABLE_AC];

	return asus_armoury.rog_tunables[ASUS_ROG_TUNABLE_DC];
}

/* Simple attribute creation */
ASUS_ATTR_GROUP_ENUM_INT_RO(charge_mode, "charge_mode", ASUS_WMI_DEVID_CHARGE_MODE, "0;1;2\n",
			    "Show the current mode of charging");
ASUS_ATTR_GROUP_BOOL_RW(boot_sound, "boot_sound", ASUS_WMI_DEVID_BOOT_SOUND,
			"Set the boot POST sound");
ASUS_ATTR_GROUP_BOOL_RW(mcu_powersave, "mcu_powersave", ASUS_WMI_DEVID_MCU_POWERSAVE,
			"Set MCU powersaving mode");
ASUS_ATTR_GROUP_BOOL_RW(panel_od, "panel_overdrive", ASUS_WMI_DEVID_PANEL_OD,
			"Set the panel refresh overdrive");
ASUS_ATTR_GROUP_BOOL_RW(panel_hd_mode, "panel_hd_mode", ASUS_WMI_DEVID_PANEL_HD,
			"Set the panel HD mode to UHD<0> or FHD<1>");
ASUS_ATTR_GROUP_BOOL_RW(screen_auto_brightness, "screen_auto_brightness",
			ASUS_WMI_DEVID_SCREEN_AUTO_BRIGHTNESS,
			"Set the panel brightness to Off<0> or On<1>");
ASUS_ATTR_GROUP_BOOL_RO(egpu_connected, "egpu_connected", ASUS_WMI_DEVID_EGPU_CONNECTED,
			"Show the eGPU connection status");
ASUS_ATTR_GROUP_ROG_TUNABLE(ppt_pl1_spl, ATTR_PPT_PL1_SPL, ASUS_WMI_DEVID_PPT_PL1_SPL,
			    "Set the CPU slow package limit");
ASUS_ATTR_GROUP_ROG_TUNABLE(ppt_pl2_sppt, ATTR_PPT_PL2_SPPT, ASUS_WMI_DEVID_PPT_PL2_SPPT,
			    "Set the CPU fast package limit");
ASUS_ATTR_GROUP_ROG_TUNABLE(ppt_pl3_fppt, ATTR_PPT_PL3_FPPT, ASUS_WMI_DEVID_PPT_PL3_FPPT,
			    "Set the CPU fastest package limit");
ASUS_ATTR_GROUP_ROG_TUNABLE(ppt_apu_sppt, ATTR_PPT_APU_SPPT, ASUS_WMI_DEVID_PPT_APU_SPPT,
			    "Set the APU package limit");
ASUS_ATTR_GROUP_ROG_TUNABLE(ppt_platform_sppt, ATTR_PPT_PLATFORM_SPPT, ASUS_WMI_DEVID_PPT_PLAT_SPPT,
			    "Set the platform package limit");
ASUS_ATTR_GROUP_ROG_TUNABLE(nv_dynamic_boost, ATTR_NV_DYNAMIC_BOOST, ASUS_WMI_DEVID_NV_DYN_BOOST,
			    "Set the Nvidia dynamic boost limit");
ASUS_ATTR_GROUP_ROG_TUNABLE(nv_temp_target, ATTR_NV_TEMP_TARGET, ASUS_WMI_DEVID_NV_THERM_TARGET,
			    "Set the Nvidia max thermal limit");
ASUS_ATTR_GROUP_ROG_TUNABLE(nv_tgp, "nv_tgp", ASUS_WMI_DEVID_DGPU_SET_TGP,
			    "Set the additional TGP on top of the base TGP");
ASUS_ATTR_GROUP_INT_VALUE_ONLY_RO(nv_base_tgp, ATTR_NV_BASE_TGP, ASUS_WMI_DEVID_DGPU_BASE_TGP,
				  "Read the base TGP value");

/* If an attribute does not require any special case handling add it here */
static const struct asus_attr_group armoury_attr_groups[] = {
	{ &egpu_connected_attr_group, ASUS_WMI_DEVID_EGPU_CONNECTED },
	{ &egpu_enable_attr_group, ASUS_WMI_DEVID_EGPU },
	{ &dgpu_disable_attr_group, ASUS_WMI_DEVID_DGPU },
	{ &apu_mem_attr_group, ASUS_WMI_DEVID_APU_MEM },

	{ &ppt_pl1_spl_attr_group, ASUS_WMI_DEVID_PPT_PL1_SPL },
	{ &ppt_pl2_sppt_attr_group, ASUS_WMI_DEVID_PPT_PL2_SPPT },
	{ &ppt_pl3_fppt_attr_group, ASUS_WMI_DEVID_PPT_PL3_FPPT },
	{ &ppt_apu_sppt_attr_group, ASUS_WMI_DEVID_PPT_APU_SPPT },
	{ &ppt_platform_sppt_attr_group, ASUS_WMI_DEVID_PPT_PLAT_SPPT },
	{ &nv_dynamic_boost_attr_group, ASUS_WMI_DEVID_NV_DYN_BOOST },
	{ &nv_temp_target_attr_group, ASUS_WMI_DEVID_NV_THERM_TARGET },
	{ &nv_base_tgp_attr_group, ASUS_WMI_DEVID_DGPU_BASE_TGP },
	{ &nv_tgp_attr_group, ASUS_WMI_DEVID_DGPU_SET_TGP },

	{ &charge_mode_attr_group, ASUS_WMI_DEVID_CHARGE_MODE },
	{ &boot_sound_attr_group, ASUS_WMI_DEVID_BOOT_SOUND },
	{ &mcu_powersave_attr_group, ASUS_WMI_DEVID_MCU_POWERSAVE },
	{ &panel_od_attr_group, ASUS_WMI_DEVID_PANEL_OD },
	{ &panel_hd_mode_attr_group, ASUS_WMI_DEVID_PANEL_HD },
	{ &screen_auto_brightness_attr_group, ASUS_WMI_DEVID_SCREEN_AUTO_BRIGHTNESS },
};

/**
 * is_power_tunable_attr - Determines if an attribute is a power-related tunable
 * @name: The name of the attribute to check
 *
 * This function checks if the given attribute name is related to power tuning.
 *
 * Return: true if the attribute is a power-related tunable, false otherwise
 */
static bool is_power_tunable_attr(const char *name)
{
	static const char * const power_tunable_attrs[] = {
		ATTR_PPT_PL1_SPL,	ATTR_PPT_PL2_SPPT,
		ATTR_PPT_PL3_FPPT,	ATTR_PPT_APU_SPPT,
		ATTR_PPT_PLATFORM_SPPT, ATTR_NV_DYNAMIC_BOOST,
		ATTR_NV_TEMP_TARGET,	ATTR_NV_BASE_TGP,
		ATTR_NV_TGP
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(power_tunable_attrs); i++) {
		if (!strcmp(name, power_tunable_attrs[i]))
			return true;
	}

	return false;
}

/**
 * has_valid_limit - Checks if a power-related attribute has a valid limit value
 * @name: The name of the attribute to check
 * @limits: Pointer to the power_limits structure containing limit values
 *
 * This function checks if a power-related attribute has a valid limit value.
 * It returns false if limits is NULL or if the corresponding limit value is zero.
 *
 * Return: true if the attribute has a valid limit value, false otherwise
 */
static bool has_valid_limit(const char *name, const struct power_limits *limits)
{
	u32 limit_value = 0;

	if (!limits)
		return false;

	if (!strcmp(name, ATTR_PPT_PL1_SPL))
		limit_value = limits->ppt_pl1_spl_max;
	else if (!strcmp(name, ATTR_PPT_PL2_SPPT))
		limit_value = limits->ppt_pl2_sppt_max;
	else if (!strcmp(name, ATTR_PPT_PL3_FPPT))
		limit_value = limits->ppt_pl3_fppt_max;
	else if (!strcmp(name, ATTR_PPT_APU_SPPT))
		limit_value = limits->ppt_apu_sppt_max;
	else if (!strcmp(name, ATTR_PPT_PLATFORM_SPPT))
		limit_value = limits->ppt_platform_sppt_max;
	else if (!strcmp(name, ATTR_NV_DYNAMIC_BOOST))
		limit_value = limits->nv_dynamic_boost_max;
	else if (!strcmp(name, ATTR_NV_TEMP_TARGET))
		limit_value = limits->nv_temp_target_max;
	else if (!strcmp(name, ATTR_NV_BASE_TGP) ||
		 !strcmp(name, ATTR_NV_TGP))
		limit_value = limits->nv_tgp_max;

	return limit_value > 0;
}

static int asus_fw_attr_add(void)
{
	const struct rog_tunables *const ac_rog_tunables =
		asus_armoury.rog_tunables[ASUS_ROG_TUNABLE_AC];
	const struct power_limits *limits;
	bool should_create;
	const char *name;
	int err, i;

	asus_armoury.fw_attr_dev = device_create(&firmware_attributes_class, NULL, MKDEV(0, 0),
						NULL, "%s", DRIVER_NAME);
	if (IS_ERR(asus_armoury.fw_attr_dev)) {
		err = PTR_ERR(asus_armoury.fw_attr_dev);
		goto fail_class_get;
	}

	asus_armoury.fw_attr_kset = kset_create_and_add("attributes", NULL,
						&asus_armoury.fw_attr_dev->kobj);
	if (!asus_armoury.fw_attr_kset) {
		err = -ENOMEM;
		goto err_destroy_classdev;
	}

	err = sysfs_create_file(&asus_armoury.fw_attr_kset->kobj, &pending_reboot.attr);
	if (err) {
		pr_err("Failed to create sysfs level attributes\n");
		goto err_destroy_kset;
	}

	asus_armoury.mini_led_dev_id = 0;
	if (armoury_has_devstate(ASUS_WMI_DEVID_MINI_LED_MODE))
		asus_armoury.mini_led_dev_id = ASUS_WMI_DEVID_MINI_LED_MODE;
	else if (armoury_has_devstate(ASUS_WMI_DEVID_MINI_LED_MODE2))
		asus_armoury.mini_led_dev_id = ASUS_WMI_DEVID_MINI_LED_MODE2;

	if (asus_armoury.mini_led_dev_id) {
		err = sysfs_create_group(&asus_armoury.fw_attr_kset->kobj,
					 &mini_led_mode_attr_group);
		if (err) {
			pr_err("Failed to create sysfs-group for mini_led\n");
			goto err_remove_file;
		}
	}

	asus_armoury.gpu_mux_dev_id = 0;
	if (armoury_has_devstate(ASUS_WMI_DEVID_GPU_MUX))
		asus_armoury.gpu_mux_dev_id = ASUS_WMI_DEVID_GPU_MUX;
	else if (armoury_has_devstate(ASUS_WMI_DEVID_GPU_MUX_VIVO))
		asus_armoury.gpu_mux_dev_id = ASUS_WMI_DEVID_GPU_MUX_VIVO;

	if (asus_armoury.gpu_mux_dev_id) {
		err = sysfs_create_group(&asus_armoury.fw_attr_kset->kobj,
					 &gpu_mux_mode_attr_group);
		if (err) {
			pr_err("Failed to create sysfs-group for gpu_mux\n");
			goto err_remove_mini_led_group;
		}
	}

	for (i = 0; i < ARRAY_SIZE(armoury_attr_groups); i++) {
		if (!armoury_has_devstate(armoury_attr_groups[i].wmi_devid))
			continue;

		/* Always create by default, unless PPT is not present */
		should_create = true;
		name = armoury_attr_groups[i].attr_group->name;

		/* Check if this is a power-related tunable requiring limits */
		if (ac_rog_tunables && ac_rog_tunables->power_limits &&
		    is_power_tunable_attr(name)) {
			limits = ac_rog_tunables->power_limits;
			/* Check only AC: if not present then DC won't be either */
			should_create = has_valid_limit(name, limits);
			if (!should_create)
				pr_debug("Missing max value for tunable %s\n", name);
		}

		if (should_create) {
			err = sysfs_create_group(&asus_armoury.fw_attr_kset->kobj,
						 armoury_attr_groups[i].attr_group);
			if (err) {
				pr_err("Failed to create sysfs-group for %s\n",
				       armoury_attr_groups[i].attr_group->name);
				goto err_remove_groups;
			}
		}
	}

	return 0;

err_remove_groups:
	while (i--) {
		if (armoury_has_devstate(armoury_attr_groups[i].wmi_devid))
			sysfs_remove_group(&asus_armoury.fw_attr_kset->kobj,
					   armoury_attr_groups[i].attr_group);
	}
	if (asus_armoury.gpu_mux_dev_id)
		sysfs_remove_group(&asus_armoury.fw_attr_kset->kobj, &gpu_mux_mode_attr_group);
err_remove_mini_led_group:
	if (asus_armoury.mini_led_dev_id)
		sysfs_remove_group(&asus_armoury.fw_attr_kset->kobj, &mini_led_mode_attr_group);
err_remove_file:
	sysfs_remove_file(&asus_armoury.fw_attr_kset->kobj, &pending_reboot.attr);
err_destroy_kset:
	kset_unregister(asus_armoury.fw_attr_kset);
err_destroy_classdev:
fail_class_get:
	device_destroy(&firmware_attributes_class, MKDEV(0, 0));
	return err;
}

/* Init / exit ****************************************************************/

/* Set up the min/max and defaults for ROG tunables */
static void init_rog_tunables(void)
{
	const struct power_limits *ac_limits, *dc_limits;
	struct rog_tunables *ac_rog_tunables = NULL, *dc_rog_tunables = NULL;
	const struct power_data *power_data;
	const struct dmi_system_id *dmi_id;

	/* Match the system against the power_limits table */
	dmi_id = dmi_first_match(power_limits);
	if (!dmi_id) {
		pr_warn("No matching power limits found for this system\n");
		return;
	}

	/* Get the power data for this system */
	power_data = dmi_id->driver_data;
	if (!power_data) {
		pr_info("No power data available for this system\n");
		return;
	}

	/* Initialize AC power tunables */
	ac_limits = power_data->ac_data;
	if (ac_limits) {
		ac_rog_tunables = kzalloc(sizeof(*asus_armoury.rog_tunables[ASUS_ROG_TUNABLE_AC]),
				GFP_KERNEL);
		if (!ac_rog_tunables)
			goto err_nomem;

		asus_armoury.rog_tunables[ASUS_ROG_TUNABLE_AC] = ac_rog_tunables;
		ac_rog_tunables->power_limits = ac_limits;

		/* Set initial AC values */
		ac_rog_tunables->ppt_pl1_spl =
			ac_limits->ppt_pl1_spl_def ?
				ac_limits->ppt_pl1_spl_def :
				ac_limits->ppt_pl1_spl_max;

		ac_rog_tunables->ppt_pl2_sppt =
			ac_limits->ppt_pl2_sppt_def ?
				ac_limits->ppt_pl2_sppt_def :
				ac_limits->ppt_pl2_sppt_max;

		ac_rog_tunables->ppt_pl3_fppt =
			ac_limits->ppt_pl3_fppt_def ?
				ac_limits->ppt_pl3_fppt_def :
				ac_limits->ppt_pl3_fppt_max;

		ac_rog_tunables->ppt_apu_sppt =
			ac_limits->ppt_apu_sppt_def ?
				ac_limits->ppt_apu_sppt_def :
				ac_limits->ppt_apu_sppt_max;

		ac_rog_tunables->ppt_platform_sppt =
			ac_limits->ppt_platform_sppt_def ?
				ac_limits->ppt_platform_sppt_def :
				ac_limits->ppt_platform_sppt_max;

		ac_rog_tunables->nv_dynamic_boost =
			ac_limits->nv_dynamic_boost_max;
		ac_rog_tunables->nv_temp_target =
			ac_limits->nv_temp_target_max;
		ac_rog_tunables->nv_tgp = ac_limits->nv_tgp_max;

		pr_debug("AC power limits initialized for %s\n", dmi_id->matches[0].substr);
	} else {
		pr_debug("No AC PPT limits defined\n");
	}

	/* Initialize DC power tunables */
	dc_limits = power_data->dc_data;
	if (dc_limits) {
		dc_rog_tunables = kzalloc(sizeof(*asus_armoury.rog_tunables[ASUS_ROG_TUNABLE_DC]),
					  GFP_KERNEL);
		if (!dc_rog_tunables) {
			kfree(ac_rog_tunables);
			goto err_nomem;
		}

		asus_armoury.rog_tunables[ASUS_ROG_TUNABLE_DC] = dc_rog_tunables;
		dc_rog_tunables->power_limits = dc_limits;

		/* Set initial DC values */
		dc_rog_tunables->ppt_pl1_spl =
			dc_limits->ppt_pl1_spl_def ?
				dc_limits->ppt_pl1_spl_def :
				dc_limits->ppt_pl1_spl_max;

		dc_rog_tunables->ppt_pl2_sppt =
			dc_limits->ppt_pl2_sppt_def ?
				dc_limits->ppt_pl2_sppt_def :
				dc_limits->ppt_pl2_sppt_max;

		dc_rog_tunables->ppt_pl3_fppt =
			dc_limits->ppt_pl3_fppt_def ?
				dc_limits->ppt_pl3_fppt_def :
				dc_limits->ppt_pl3_fppt_max;

		dc_rog_tunables->ppt_apu_sppt =
			dc_limits->ppt_apu_sppt_def ?
				dc_limits->ppt_apu_sppt_def :
				dc_limits->ppt_apu_sppt_max;

		dc_rog_tunables->ppt_platform_sppt =
			dc_limits->ppt_platform_sppt_def ?
				dc_limits->ppt_platform_sppt_def :
				dc_limits->ppt_platform_sppt_max;

		dc_rog_tunables->nv_dynamic_boost =
			dc_limits->nv_dynamic_boost_max;
		dc_rog_tunables->nv_temp_target =
			dc_limits->nv_temp_target_max;
		dc_rog_tunables->nv_tgp = dc_limits->nv_tgp_max;

		pr_debug("DC power limits initialized for %s\n", dmi_id->matches[0].substr);
	} else {
		pr_debug("No DC PPT limits defined\n");
	}

	return;

err_nomem:
	pr_err("Failed to allocate memory for tunables\n");
}

static int __init asus_fw_init(void)
{
	char *wmi_uid;

	wmi_uid = wmi_get_acpi_device_uid(ASUS_WMI_MGMT_GUID);
	if (!wmi_uid)
		return -ENODEV;

	/*
	 * if equal to "ASUSWMI" then it's DCTS that can't be used for this
	 * driver, DSTS is required.
	 */
	if (!strcmp(wmi_uid, ASUS_ACPI_UID_ASUSWMI))
		return -ENODEV;

	init_rog_tunables();

	/* Must always be last step to ensure data is available */
	return asus_fw_attr_add();
}

static void __exit asus_fw_exit(void)
{
	int i;

	for (i = ARRAY_SIZE(armoury_attr_groups) - 1; i >= 0; i--) {
		if (armoury_has_devstate(armoury_attr_groups[i].wmi_devid))
			sysfs_remove_group(&asus_armoury.fw_attr_kset->kobj,
					   armoury_attr_groups[i].attr_group);
	}

	if (asus_armoury.gpu_mux_dev_id)
		sysfs_remove_group(&asus_armoury.fw_attr_kset->kobj, &gpu_mux_mode_attr_group);

	if (asus_armoury.mini_led_dev_id)
		sysfs_remove_group(&asus_armoury.fw_attr_kset->kobj, &mini_led_mode_attr_group);

	sysfs_remove_file(&asus_armoury.fw_attr_kset->kobj, &pending_reboot.attr);
	kset_unregister(asus_armoury.fw_attr_kset);
	device_destroy(&firmware_attributes_class, MKDEV(0, 0));

	kfree(asus_armoury.rog_tunables[ASUS_ROG_TUNABLE_AC]);
	kfree(asus_armoury.rog_tunables[ASUS_ROG_TUNABLE_DC]);
}

module_init(asus_fw_init);
module_exit(asus_fw_exit);

MODULE_IMPORT_NS("ASUS_WMI");
MODULE_AUTHOR("Luke Jones <luke@ljones.dev>");
MODULE_DESCRIPTION("ASUS BIOS Configuration Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("wmi:" ASUS_NB_WMI_EVENT_GUID);
