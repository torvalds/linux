#include <linux/pci.h>
#include <linux/acpi.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>

#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"
#include "drm_crtc_helper.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nv50_display.h"

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

static int nouveau_dsm(struct drm_device *dev, int func, int arg, int *result)
{
	static char muid[] = {
		0xA0, 0xA0, 0x95, 0x9D, 0x60, 0x00, 0x48, 0x4D,
		0xB3, 0x4D, 0x7E, 0x5F, 0xEA, 0x12, 0x9F, 0xD4,
	};

	struct pci_dev *pdev = dev->pdev;
	struct acpi_handle *handle;
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input;
	union acpi_object params[4];
	union acpi_object *obj;
	int err;

	handle = DEVICE_ACPI_HANDLE(&pdev->dev);

	if (!handle)
		return -ENODEV;

	input.count = 4;
	input.pointer = params;
	params[0].type = ACPI_TYPE_BUFFER;
	params[0].buffer.length = sizeof(muid);
	params[0].buffer.pointer = (char *)muid;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = 0x00000102;
	params[2].type = ACPI_TYPE_INTEGER;
	params[2].integer.value = func;
	params[3].type = ACPI_TYPE_INTEGER;
	params[3].integer.value = arg;

	err = acpi_evaluate_object(handle, "_DSM", &input, &output);
	if (err) {
		NV_INFO(dev, "failed to evaluate _DSM: %d\n", err);
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

int nouveau_hybrid_setup(struct drm_device *dev)
{
	int result;

	if (nouveau_dsm(dev, NOUVEAU_DSM_POWER, NOUVEAU_DSM_POWER_STATE,
								&result))
		return -ENODEV;

	NV_INFO(dev, "_DSM hardware status gave 0x%x\n", result);

	if (result) { /* Ensure that the external GPU is enabled */
		nouveau_dsm(dev, NOUVEAU_DSM_LED, NOUVEAU_DSM_LED_SPEED, NULL);
		nouveau_dsm(dev, NOUVEAU_DSM_POWER, NOUVEAU_DSM_POWER_SPEED,
									NULL);
	} else { /* Stamina mode - disable the external GPU */
		nouveau_dsm(dev, NOUVEAU_DSM_LED, NOUVEAU_DSM_LED_STAMINA,
									NULL);
		nouveau_dsm(dev, NOUVEAU_DSM_POWER, NOUVEAU_DSM_POWER_STAMINA,
									NULL);
	}

	return 0;
}

bool nouveau_dsm_probe(struct drm_device *dev)
{
	int support = 0;

	if (nouveau_dsm(dev, NOUVEAU_DSM_SUPPORTED,
				NOUVEAU_DSM_SUPPORTED_FUNCTIONS, &support))
		return false;

	if (!support)
		return false;

	return true;
}
