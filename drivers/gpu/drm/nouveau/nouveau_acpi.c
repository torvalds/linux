#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>

#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"
#include "drm_crtc_helper.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nv50_display.h"

#include <linux/vga_switcheroo.h>

#define NOUVEAU_DSM_SUPPORTED 0x00
#define NOUVEAU_DSM_SUPPORTED_FUNCTIONS 0x00

#define NOUVEAU_DSM_ACTIVE 0x01
#define NOUVEAU_DSM_ACTIVE_QUERY 0x00

#define NOUVEAU_DSM_LED 0x02
#define NOUVEAU_DSM_LED_STATE 0x00
#define NOUVEAU_DSM_LED_OFF 0x10
#define NOUVEAU_DSM_LED_STAMINA 0x11
#define NOUVEAU_DSM_LED_SPEED 0x12

#define NOUVEAU_DSM_POWER 0x03
#define NOUVEAU_DSM_POWER_STATE 0x00
#define NOUVEAU_DSM_POWER_SPEED 0x01
#define NOUVEAU_DSM_POWER_STAMINA 0x02

static struct nouveau_dsm_priv {
	bool dsm_detected;
	acpi_handle dhandle;
	acpi_handle rom_handle;
} nouveau_dsm_priv;

static const char nouveau_dsm_muid[] = {
	0xA0, 0xA0, 0x95, 0x9D, 0x60, 0x00, 0x48, 0x4D,
	0xB3, 0x4D, 0x7E, 0x5F, 0xEA, 0x12, 0x9F, 0xD4,
};

static int nouveau_dsm(acpi_handle handle, int func, int arg, int *result)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input;
	union acpi_object params[4];
	union acpi_object *obj;
	int err;

	input.count = 4;
	input.pointer = params;
	params[0].type = ACPI_TYPE_BUFFER;
	params[0].buffer.length = sizeof(nouveau_dsm_muid);
	params[0].buffer.pointer = (char *)nouveau_dsm_muid;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = 0x00000102;
	params[2].type = ACPI_TYPE_INTEGER;
	params[2].integer.value = func;
	params[3].type = ACPI_TYPE_INTEGER;
	params[3].integer.value = arg;

	err = acpi_evaluate_object(handle, "_DSM", &input, &output);
	if (err) {
		printk(KERN_INFO "failed to evaluate _DSM: %d\n", err);
		return err;
	}

	obj = (union acpi_object *)output.pointer;

	if (obj->type == ACPI_TYPE_INTEGER)
		if (obj->integer.value == 0x80000002)
			return -ENODEV;

	if (obj->type == ACPI_TYPE_BUFFER) {
		if (obj->buffer.length == 4 && result) {
			*result = 0;
			*result |= obj->buffer.pointer[0];
			*result |= (obj->buffer.pointer[1] << 8);
			*result |= (obj->buffer.pointer[2] << 16);
			*result |= (obj->buffer.pointer[3] << 24);
		}
	}

	kfree(output.pointer);
	return 0;
}

static int nouveau_dsm_switch_mux(acpi_handle handle, int mux_id)
{
	return nouveau_dsm(handle, NOUVEAU_DSM_LED, mux_id, NULL);
}

static int nouveau_dsm_set_discrete_state(acpi_handle handle, enum vga_switcheroo_state state)
{
	int arg;
	if (state == VGA_SWITCHEROO_ON)
		arg = NOUVEAU_DSM_POWER_SPEED;
	else
		arg = NOUVEAU_DSM_POWER_STAMINA;
	nouveau_dsm(handle, NOUVEAU_DSM_POWER, arg, NULL);
	return 0;
}

static int nouveau_dsm_switchto(enum vga_switcheroo_client_id id)
{
	if (id == VGA_SWITCHEROO_IGD)
		return nouveau_dsm_switch_mux(nouveau_dsm_priv.dhandle, NOUVEAU_DSM_LED_STAMINA);
	else
		return nouveau_dsm_switch_mux(nouveau_dsm_priv.dhandle, NOUVEAU_DSM_LED_SPEED);
}

static int nouveau_dsm_power_state(enum vga_switcheroo_client_id id,
				   enum vga_switcheroo_state state)
{
	if (id == VGA_SWITCHEROO_IGD)
		return 0;

	return nouveau_dsm_set_discrete_state(nouveau_dsm_priv.dhandle, state);
}

static int nouveau_dsm_init(void)
{
	return 0;
}

static int nouveau_dsm_get_client_id(struct pci_dev *pdev)
{
	if (nouveau_dsm_priv.dhandle == DEVICE_ACPI_HANDLE(&pdev->dev))
		return VGA_SWITCHEROO_IGD;
	else
		return VGA_SWITCHEROO_DIS;
}

static struct vga_switcheroo_handler nouveau_dsm_handler = {
	.switchto = nouveau_dsm_switchto,
	.power_state = nouveau_dsm_power_state,
	.init = nouveau_dsm_init,
	.get_client_id = nouveau_dsm_get_client_id,
};

static bool nouveau_dsm_pci_probe(struct pci_dev *pdev)
{
	acpi_handle dhandle, nvidia_handle;
	acpi_status status;
	int ret;
	uint32_t result;

	dhandle = DEVICE_ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return false;

	status = acpi_get_handle(dhandle, "_DSM", &nvidia_handle);
	if (ACPI_FAILURE(status)) {
		return false;
	}

	ret = nouveau_dsm(dhandle, NOUVEAU_DSM_SUPPORTED,
			  NOUVEAU_DSM_SUPPORTED_FUNCTIONS, &result);
	if (ret < 0)
		return false;

	nouveau_dsm_priv.dhandle = dhandle;
	return true;
}

static bool nouveau_dsm_detect(void)
{
	char acpi_method_name[255] = { 0 };
	struct acpi_buffer buffer = {sizeof(acpi_method_name), acpi_method_name};
	struct pci_dev *pdev = NULL;
	int has_dsm = 0;
	int vga_count = 0;

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev)) != NULL) {
		vga_count++;

		has_dsm |= (nouveau_dsm_pci_probe(pdev) == true);
	}

	if (vga_count == 2 && has_dsm) {
		acpi_get_name(nouveau_dsm_priv.dhandle, ACPI_FULL_PATHNAME, &buffer);
		printk(KERN_INFO "VGA switcheroo: detected DSM switching method %s handle\n",
		       acpi_method_name);
		nouveau_dsm_priv.dsm_detected = true;
		return true;
	}
	return false;
}

void nouveau_register_dsm_handler(void)
{
	bool r;

	r = nouveau_dsm_detect();
	if (!r)
		return;

	vga_switcheroo_register_handler(&nouveau_dsm_handler);
}

void nouveau_unregister_dsm_handler(void)
{
	vga_switcheroo_unregister_handler();
}

/* retrieve the ROM in 4k blocks */
static int nouveau_rom_call(acpi_handle rom_handle, uint8_t *bios,
			    int offset, int len)
{
	acpi_status status;
	union acpi_object rom_arg_elements[2], *obj;
	struct acpi_object_list rom_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL};

	rom_arg.count = 2;
	rom_arg.pointer = &rom_arg_elements[0];

	rom_arg_elements[0].type = ACPI_TYPE_INTEGER;
	rom_arg_elements[0].integer.value = offset;

	rom_arg_elements[1].type = ACPI_TYPE_INTEGER;
	rom_arg_elements[1].integer.value = len;

	status = acpi_evaluate_object(rom_handle, NULL, &rom_arg, &buffer);
	if (ACPI_FAILURE(status)) {
		printk(KERN_INFO "failed to evaluate ROM got %s\n", acpi_format_exception(status));
		return -ENODEV;
	}
	obj = (union acpi_object *)buffer.pointer;
	memcpy(bios+offset, obj->buffer.pointer, len);
	kfree(buffer.pointer);
	return len;
}

bool nouveau_acpi_rom_supported(struct pci_dev *pdev)
{
	acpi_status status;
	acpi_handle dhandle, rom_handle;

	if (!nouveau_dsm_priv.dsm_detected)
		return false;

	dhandle = DEVICE_ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return false;

	status = acpi_get_handle(dhandle, "_ROM", &rom_handle);
	if (ACPI_FAILURE(status))
		return false;

	nouveau_dsm_priv.rom_handle = rom_handle;
	return true;
}

int nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len)
{
	return nouveau_rom_call(nouveau_dsm_priv.rom_handle, bios, offset, len);
}
