/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * DMI based code to deal with broken DSDTs on X86 tablets which ship with
 * Android as (part of) the factory image. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2023 Hans de Goede <hansg@kernel.org>
 */
#ifndef __PDX86_X86_ANDROID_TABLETS_H
#define __PDX86_X86_ANDROID_TABLETS_H

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/irqdomain_defs.h>
#include <linux/spi/spi.h>

struct gpio_desc;
struct platform_device_info;
struct software_node;

/*
 * Helpers to get Linux IRQ numbers given a description of the IRQ source
 * (either IOAPIC index, or GPIO chip name + pin-number).
 */
enum x86_acpi_irq_type {
	X86_ACPI_IRQ_TYPE_NONE,
	X86_ACPI_IRQ_TYPE_APIC,
	X86_ACPI_IRQ_TYPE_GPIOINT,
	X86_ACPI_IRQ_TYPE_PMIC,
};

enum x86_gpiochip_type {
	X86_GPIOCHIP_UNSPECIFIED = 0,
	X86_GPIOCHIP_BAYTRAIL,
	X86_GPIOCHIP_CHERRYVIEW,
};

struct x86_acpi_irq_data {
	char *chip;   /* GPIO chip label (GPIOINT) or PMIC ACPI path (PMIC) */
	enum x86_acpi_irq_type type;
	enum irq_domain_bus_token domain;
	int index;
	int trigger;  /* ACPI_EDGE_SENSITIVE / ACPI_LEVEL_SENSITIVE */
	int polarity; /* ACPI_ACTIVE_HIGH / ACPI_ACTIVE_LOW / ACPI_ACTIVE_BOTH */
	bool free_gpio; /* Release GPIO after getting IRQ (for TYPE_GPIOINT) */
	const char *con_id;
};

/* Structs to describe devices to instantiate */
struct x86_i2c_client_info {
	struct i2c_board_info board_info;
	char *adapter_path;
	struct x86_acpi_irq_data irq_data;
};

struct x86_spi_dev_info {
	struct spi_board_info board_info;
	char *ctrl_path;
	struct x86_acpi_irq_data irq_data;
};

struct x86_serdev_info {
	union {
		struct {
			const char *hid;
			const char *uid;
		} acpi;
		struct {
			unsigned int devfn;
		} pci;
	} ctrl;
	const char *ctrl_devname;
	/*
	 * ATM the serdev core only supports of or ACPI matching; and so far all
	 * Android x86 tablets DSDTs have usable serdev nodes, but sometimes
	 * under the wrong controller. So we just tie the existing serdev ACPI
	 * node to the right controller.
	 */
	const char *serdev_hid;
};

struct x86_dev_info {
	const char * const *modules;
	const struct software_node **swnode_group;
	const struct x86_i2c_client_info *i2c_client_info;
	const struct x86_spi_dev_info *spi_dev_info;
	const struct platform_device_info *pdev_info;
	const struct x86_serdev_info *serdev_info;
	const struct software_node **gpio_button_swnodes;
	int i2c_client_count;
	int spi_dev_count;
	int pdev_count;
	int serdev_count;
	int (*init)(struct device *dev);
	void (*exit)(void);
	bool use_pci;
	enum x86_gpiochip_type gpiochip_type;
};

int x86_android_tablet_get_gpiod(const char *chip, int pin, const char *con_id,
				 bool active_low, enum gpiod_flags dflags,
				 struct gpio_desc **desc);
int x86_acpi_irq_helper_get(const struct x86_acpi_irq_data *data);

/* Software nodes representing GPIO chips used by various tablets */
extern const struct software_node baytrail_gpiochip_nodes[];
extern const struct software_node cherryview_gpiochip_nodes[];

/*
 * Extern declarations of x86_dev_info structs so there can be a single
 * MODULE_DEVICE_TABLE(dmi, ...), while splitting the board descriptions.
 */
extern const struct x86_dev_info acer_a1_840_info;
extern const struct x86_dev_info acer_b1_750_info;
extern const struct x86_dev_info advantech_mica_071_info;
extern const struct x86_dev_info asus_me176c_info;
extern const struct x86_dev_info asus_tf103c_info;
extern const struct x86_dev_info chuwi_hi8_info;
extern const struct x86_dev_info cyberbook_t116_info;
extern const struct x86_dev_info czc_p10t;
extern const struct x86_dev_info lenovo_yogabook_x90_info;
extern const struct x86_dev_info lenovo_yogabook_x91_info;
extern const struct x86_dev_info lenovo_yoga_tab2_830_1050_info;
extern const struct x86_dev_info lenovo_yoga_tab2_1380_info;
extern const struct x86_dev_info lenovo_yt3_info;
extern const struct x86_dev_info medion_lifetab_s10346_info;
extern const struct x86_dev_info nextbook_ares8_info;
extern const struct x86_dev_info nextbook_ares8a_info;
extern const struct x86_dev_info peaq_c1010_info;
extern const struct x86_dev_info whitelabel_tm800a550l_info;
extern const struct x86_dev_info vexia_edu_atla10_5v_info;
extern const struct x86_dev_info vexia_edu_atla10_9v_info;
extern const struct x86_dev_info xiaomi_mipad2_info;
extern const struct dmi_system_id x86_android_tablet_ids[];

#endif
