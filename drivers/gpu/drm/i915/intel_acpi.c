/*
 * Intel ACPI functions
 *
 * _DSM related code stolen from nouveau_acpi.c.
 */
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/vga_switcheroo.h>
#include <acpi/acpi_drivers.h>

#include <drm/drmP.h>
#include "i915_drv.h"

#define INTEL_DSM_REVISION_ID 1 /* For Calpella anyway... */

#define INTEL_DSM_FN_SUPPORTED_FUNCTIONS 0 /* No args */
#define INTEL_DSM_FN_PLATFORM_MUX_INFO 1 /* No args */

static struct intel_dsm_priv {
	acpi_handle dhandle;
} intel_dsm_priv;

static const u8 intel_dsm_guid[] = {
	0xd3, 0x73, 0xd8, 0x7e,
	0xd0, 0xc2,
	0x4f, 0x4e,
	0xa8, 0x54,
	0x0f, 0x13, 0x17, 0xb0, 0x1c, 0x2c
};

static int intel_dsm(acpi_handle handle, int func)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input;
	union acpi_object params[4];
	union acpi_object *obj;
	u32 result;
	int ret = 0;

	input.count = 4;
	input.pointer = params;
	params[0].type = ACPI_TYPE_BUFFER;
	params[0].buffer.length = sizeof(intel_dsm_guid);
	params[0].buffer.pointer = (char *)intel_dsm_guid;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = INTEL_DSM_REVISION_ID;
	params[2].type = ACPI_TYPE_INTEGER;
	params[2].integer.value = func;
	params[3].type = ACPI_TYPE_PACKAGE;
	params[3].package.count = 0;
	params[3].package.elements = NULL;

	ret = acpi_evaluate_object(handle, "_DSM", &input, &output);
	if (ret) {
		DRM_DEBUG_DRIVER("failed to evaluate _DSM: %d\n", ret);
		return ret;
	}

	obj = (union acpi_object *)output.pointer;

	result = 0;
	switch (obj->type) {
	case ACPI_TYPE_INTEGER:
		result = obj->integer.value;
		break;

	case ACPI_TYPE_BUFFER:
		if (obj->buffer.length == 4) {
			result = (obj->buffer.pointer[0] |
				(obj->buffer.pointer[1] <<  8) |
				(obj->buffer.pointer[2] << 16) |
				(obj->buffer.pointer[3] << 24));
			break;
		}
	default:
		ret = -EINVAL;
		break;
	}
	if (result == 0x80000002)
		ret = -ENODEV;

	kfree(output.pointer);
	return ret;
}

static char *intel_dsm_port_name(u8 id)
{
	switch (id) {
	case 0:
		return "Reserved";
	case 1:
		return "Analog VGA";
	case 2:
		return "LVDS";
	case 3:
		return "Reserved";
	case 4:
		return "HDMI/DVI_B";
	case 5:
		return "HDMI/DVI_C";
	case 6:
		return "HDMI/DVI_D";
	case 7:
		return "DisplayPort_A";
	case 8:
		return "DisplayPort_B";
	case 9:
		return "DisplayPort_C";
	case 0xa:
		return "DisplayPort_D";
	case 0xb:
	case 0xc:
	case 0xd:
		return "Reserved";
	case 0xe:
		return "WiDi";
	default:
		return "bad type";
	}
}

static char *intel_dsm_mux_type(u8 type)
{
	switch (type) {
	case 0:
		return "unknown";
	case 1:
		return "No MUX, iGPU only";
	case 2:
		return "No MUX, dGPU only";
	case 3:
		return "MUXed between iGPU and dGPU";
	default:
		return "bad type";
	}
}

static void intel_dsm_platform_mux_info(void)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input;
	union acpi_object params[4];
	union acpi_object *pkg;
	int i, ret;

	input.count = 4;
	input.pointer = params;
	params[0].type = ACPI_TYPE_BUFFER;
	params[0].buffer.length = sizeof(intel_dsm_guid);
	params[0].buffer.pointer = (char *)intel_dsm_guid;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = INTEL_DSM_REVISION_ID;
	params[2].type = ACPI_TYPE_INTEGER;
	params[2].integer.value = INTEL_DSM_FN_PLATFORM_MUX_INFO;
	params[3].type = ACPI_TYPE_PACKAGE;
	params[3].package.count = 0;
	params[3].package.elements = NULL;

	ret = acpi_evaluate_object(intel_dsm_priv.dhandle, "_DSM", &input,
				   &output);
	if (ret) {
		DRM_DEBUG_DRIVER("failed to evaluate _DSM: %d\n", ret);
		goto out;
	}

	pkg = (union acpi_object *)output.pointer;

	if (pkg->type == ACPI_TYPE_PACKAGE) {
		union acpi_object *connector_count = &pkg->package.elements[0];
		DRM_DEBUG_DRIVER("MUX info connectors: %lld\n",
			  (unsigned long long)connector_count->integer.value);
		for (i = 1; i < pkg->package.count; i++) {
			union acpi_object *obj = &pkg->package.elements[i];
			union acpi_object *connector_id =
				&obj->package.elements[0];
			union acpi_object *info = &obj->package.elements[1];
			DRM_DEBUG_DRIVER("Connector id: 0x%016llx\n",
				  (unsigned long long)connector_id->integer.value);
			DRM_DEBUG_DRIVER("  port id: %s\n",
			       intel_dsm_port_name(info->buffer.pointer[0]));
			DRM_DEBUG_DRIVER("  display mux info: %s\n",
			       intel_dsm_mux_type(info->buffer.pointer[1]));
			DRM_DEBUG_DRIVER("  aux/dc mux info: %s\n",
			       intel_dsm_mux_type(info->buffer.pointer[2]));
			DRM_DEBUG_DRIVER("  hpd mux info: %s\n",
			       intel_dsm_mux_type(info->buffer.pointer[3]));
		}
	}

out:
	kfree(output.pointer);
}

static bool intel_dsm_pci_probe(struct pci_dev *pdev)
{
	acpi_handle dhandle;
	int ret;

	dhandle = DEVICE_ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return false;

	if (!acpi_has_method(dhandle, "_DSM")) {
		DRM_DEBUG_KMS("no _DSM method for intel device\n");
		return false;
	}

	ret = intel_dsm(dhandle, INTEL_DSM_FN_SUPPORTED_FUNCTIONS);
	if (ret < 0) {
		DRM_DEBUG_KMS("failed to get supported _DSM functions\n");
		return false;
	}

	intel_dsm_priv.dhandle = dhandle;

	intel_dsm_platform_mux_info();
	return true;
}

static bool intel_dsm_detect(void)
{
	char acpi_method_name[255] = { 0 };
	struct acpi_buffer buffer = {sizeof(acpi_method_name), acpi_method_name};
	struct pci_dev *pdev = NULL;
	bool has_dsm = false;
	int vga_count = 0;

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev)) != NULL) {
		vga_count++;
		has_dsm |= intel_dsm_pci_probe(pdev);
	}

	if (vga_count == 2 && has_dsm) {
		acpi_get_name(intel_dsm_priv.dhandle, ACPI_FULL_PATHNAME, &buffer);
		DRM_DEBUG_DRIVER("VGA switcheroo: detected DSM switching method %s handle\n",
				 acpi_method_name);
		return true;
	}

	return false;
}

void intel_register_dsm_handler(void)
{
	if (!intel_dsm_detect())
		return;
}

void intel_unregister_dsm_handler(void)
{
}
