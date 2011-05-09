/*
 * Copyright (c) 2010 Red Hat Inc.
 * Author : Dave Airlie <airlied@redhat.com>
 *
 * Licensed under GPLv2
 *
 * ATPX support for both Intel/ATI
 */
#include <linux/vga_switcheroo.h>
#include <linux/slab.h>
#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <linux/pci.h>

#define ATPX_VERSION 0
#define ATPX_GPU_PWR 2
#define ATPX_MUX_SELECT 3
#define ATPX_I2C_MUX_SELECT 4
#define ATPX_SWITCH_START 5
#define ATPX_SWITCH_END 6

#define ATPX_INTEGRATED 0
#define ATPX_DISCRETE 1

#define ATPX_MUX_IGD 0
#define ATPX_MUX_DISCRETE 1

static struct radeon_atpx_priv {
	bool atpx_detected;
	/* handle for device - and atpx */
	acpi_handle dhandle;
	acpi_handle atpx_handle;
	acpi_handle atrm_handle;
} radeon_atpx_priv;

/* retrieve the ROM in 4k blocks */
static int radeon_atrm_call(acpi_handle atrm_handle, uint8_t *bios,
			    int offset, int len)
{
	acpi_status status;
	union acpi_object atrm_arg_elements[2], *obj;
	struct acpi_object_list atrm_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL};

	atrm_arg.count = 2;
	atrm_arg.pointer = &atrm_arg_elements[0];

	atrm_arg_elements[0].type = ACPI_TYPE_INTEGER;
	atrm_arg_elements[0].integer.value = offset;

	atrm_arg_elements[1].type = ACPI_TYPE_INTEGER;
	atrm_arg_elements[1].integer.value = len;

	status = acpi_evaluate_object(atrm_handle, NULL, &atrm_arg, &buffer);
	if (ACPI_FAILURE(status)) {
		printk("failed to evaluate ATRM got %s\n", acpi_format_exception(status));
		return -ENODEV;
	}

	obj = (union acpi_object *)buffer.pointer;
	memcpy(bios+offset, obj->buffer.pointer, len);
	kfree(buffer.pointer);
	return len;
}

bool radeon_atrm_supported(struct pci_dev *pdev)
{
	/* get the discrete ROM only via ATRM */
	if (!radeon_atpx_priv.atpx_detected)
		return false;

	if (radeon_atpx_priv.dhandle == DEVICE_ACPI_HANDLE(&pdev->dev))
		return false;
	return true;
}


int radeon_atrm_get_bios_chunk(uint8_t *bios, int offset, int len)
{
	return radeon_atrm_call(radeon_atpx_priv.atrm_handle, bios, offset, len);
}

static int radeon_atpx_get_version(acpi_handle handle)
{
	acpi_status status;
	union acpi_object atpx_arg_elements[2], *obj;
	struct acpi_object_list atpx_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	atpx_arg.count = 2;
	atpx_arg.pointer = &atpx_arg_elements[0];

	atpx_arg_elements[0].type = ACPI_TYPE_INTEGER;
	atpx_arg_elements[0].integer.value = ATPX_VERSION;

	atpx_arg_elements[1].type = ACPI_TYPE_INTEGER;
	atpx_arg_elements[1].integer.value = ATPX_VERSION;

	status = acpi_evaluate_object(handle, NULL, &atpx_arg, &buffer);
	if (ACPI_FAILURE(status)) {
		printk("%s: failed to call ATPX: %s\n", __func__, acpi_format_exception(status));
		return -ENOSYS;
	}
	obj = (union acpi_object *)buffer.pointer;
	if (obj && (obj->type == ACPI_TYPE_BUFFER))
		printk(KERN_INFO "radeon atpx: version is %d\n", *((u8 *)(obj->buffer.pointer) + 2));
	kfree(buffer.pointer);
	return 0;
}

static int radeon_atpx_execute(acpi_handle handle, int cmd_id, u16 value)
{
	acpi_status status;
	union acpi_object atpx_arg_elements[2];
	struct acpi_object_list atpx_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	uint8_t buf[4] = {0};

	if (!handle)
		return -EINVAL;

	atpx_arg.count = 2;
	atpx_arg.pointer = &atpx_arg_elements[0];

	atpx_arg_elements[0].type = ACPI_TYPE_INTEGER;
	atpx_arg_elements[0].integer.value = cmd_id;

	buf[2] = value & 0xff;
	buf[3] = (value >> 8) & 0xff;

	atpx_arg_elements[1].type = ACPI_TYPE_BUFFER;
	atpx_arg_elements[1].buffer.length = 4;
	atpx_arg_elements[1].buffer.pointer = buf;

	status = acpi_evaluate_object(handle, NULL, &atpx_arg, &buffer);
	if (ACPI_FAILURE(status)) {
		printk("%s: failed to call ATPX: %s\n", __func__, acpi_format_exception(status));
		return -ENOSYS;
	}
	kfree(buffer.pointer);

	return 0;
}

static int radeon_atpx_set_discrete_state(acpi_handle handle, int state)
{
	return radeon_atpx_execute(handle, ATPX_GPU_PWR, state);
}

static int radeon_atpx_switch_mux(acpi_handle handle, int mux_id)
{
	return radeon_atpx_execute(handle, ATPX_MUX_SELECT, mux_id);
}

static int radeon_atpx_switch_i2c_mux(acpi_handle handle, int mux_id)
{
	return radeon_atpx_execute(handle, ATPX_I2C_MUX_SELECT, mux_id);
}

static int radeon_atpx_switch_start(acpi_handle handle, int gpu_id)
{
	return radeon_atpx_execute(handle, ATPX_SWITCH_START, gpu_id);
}

static int radeon_atpx_switch_end(acpi_handle handle, int gpu_id)
{
	return radeon_atpx_execute(handle, ATPX_SWITCH_END, gpu_id);
}

static int radeon_atpx_switchto(enum vga_switcheroo_client_id id)
{
	int gpu_id;

	if (id == VGA_SWITCHEROO_IGD)
		gpu_id = ATPX_INTEGRATED;
	else
		gpu_id = ATPX_DISCRETE;

	radeon_atpx_switch_start(radeon_atpx_priv.atpx_handle, gpu_id);
	radeon_atpx_switch_mux(radeon_atpx_priv.atpx_handle, gpu_id);
	radeon_atpx_switch_i2c_mux(radeon_atpx_priv.atpx_handle, gpu_id);
	radeon_atpx_switch_end(radeon_atpx_priv.atpx_handle, gpu_id);

	return 0;
}

static int radeon_atpx_power_state(enum vga_switcheroo_client_id id,
				   enum vga_switcheroo_state state)
{
	/* on w500 ACPI can't change intel gpu state */
	if (id == VGA_SWITCHEROO_IGD)
		return 0;

	radeon_atpx_set_discrete_state(radeon_atpx_priv.atpx_handle, state);
	return 0;
}

static bool radeon_atpx_pci_probe_handle(struct pci_dev *pdev)
{
	acpi_handle dhandle, atpx_handle, atrm_handle;
	acpi_status status;

	dhandle = DEVICE_ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return false;

	status = acpi_get_handle(dhandle, "ATPX", &atpx_handle);
	if (ACPI_FAILURE(status))
		return false;

	status = acpi_get_handle(dhandle, "ATRM", &atrm_handle);
	if (ACPI_FAILURE(status))
		return false;

	radeon_atpx_priv.dhandle = dhandle;
	radeon_atpx_priv.atpx_handle = atpx_handle;
	radeon_atpx_priv.atrm_handle = atrm_handle;
	return true;
}

static int radeon_atpx_init(void)
{
	/* set up the ATPX handle */

	radeon_atpx_get_version(radeon_atpx_priv.atpx_handle);
	return 0;
}

static int radeon_atpx_get_client_id(struct pci_dev *pdev)
{
	if (radeon_atpx_priv.dhandle == DEVICE_ACPI_HANDLE(&pdev->dev))
		return VGA_SWITCHEROO_IGD;
	else
		return VGA_SWITCHEROO_DIS;
}

static struct vga_switcheroo_handler radeon_atpx_handler = {
	.switchto = radeon_atpx_switchto,
	.power_state = radeon_atpx_power_state,
	.init = radeon_atpx_init,
	.get_client_id = radeon_atpx_get_client_id,
};

static bool radeon_atpx_detect(void)
{
	char acpi_method_name[255] = { 0 };
	struct acpi_buffer buffer = {sizeof(acpi_method_name), acpi_method_name};
	struct pci_dev *pdev = NULL;
	bool has_atpx = false;
	int vga_count = 0;

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev)) != NULL) {
		vga_count++;

		has_atpx |= (radeon_atpx_pci_probe_handle(pdev) == true);
	}

	if (has_atpx && vga_count == 2) {
		acpi_get_name(radeon_atpx_priv.atpx_handle, ACPI_FULL_PATHNAME, &buffer);
		printk(KERN_INFO "VGA switcheroo: detected switching method %s handle\n",
		       acpi_method_name);
		radeon_atpx_priv.atpx_detected = true;
		return true;
	}
	return false;
}

void radeon_register_atpx_handler(void)
{
	bool r;

	/* detect if we have any ATPX + 2 VGA in the system */
	r = radeon_atpx_detect();
	if (!r)
		return;

	vga_switcheroo_register_handler(&radeon_atpx_handler);
}

void radeon_unregister_atpx_handler(void)
{
	vga_switcheroo_unregister_handler();
}
