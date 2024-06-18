// SPDX-License-Identifier: GPL-2.0
/*
 * Intel ACPI functions
 *
 * _DSM related code stolen from nouveau_acpi.c.
 */

#include <linux/pci.h>
#include <linux/acpi.h>
#include <acpi/video.h>

#include "i915_drv.h"
#include "intel_acpi.h"
#include "intel_display_types.h"

#define INTEL_DSM_REVISION_ID 1 /* For Calpella anyway... */
#define INTEL_DSM_FN_PLATFORM_MUX_INFO 1 /* No args */

static const guid_t intel_dsm_guid =
	GUID_INIT(0x7ed873d3, 0xc2d0, 0x4e4f,
		  0xa8, 0x54, 0x0f, 0x13, 0x17, 0xb0, 0x1c, 0x2c);

#define INTEL_DSM_FN_GET_BIOS_DATA_FUNCS_SUPPORTED 0 /* No args */

static const guid_t intel_dsm_guid2 =
	GUID_INIT(0x3e5b41c6, 0xeb1d, 0x4260,
		  0x9d, 0x15, 0xc7, 0x1f, 0xba, 0xda, 0xe4, 0x14);

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

static void intel_dsm_platform_mux_info(acpi_handle dhandle)
{
	int i;
	union acpi_object *pkg, *connector_count;

	pkg = acpi_evaluate_dsm_typed(dhandle, &intel_dsm_guid,
			INTEL_DSM_REVISION_ID, INTEL_DSM_FN_PLATFORM_MUX_INFO,
			NULL, ACPI_TYPE_PACKAGE);
	if (!pkg) {
		DRM_DEBUG_DRIVER("failed to evaluate _DSM\n");
		return;
	}

	if (!pkg->package.count) {
		DRM_DEBUG_DRIVER("no connection in _DSM\n");
		return;
	}

	connector_count = &pkg->package.elements[0];
	DRM_DEBUG_DRIVER("MUX info connectors: %lld\n",
		  (unsigned long long)connector_count->integer.value);
	for (i = 1; i < pkg->package.count; i++) {
		union acpi_object *obj = &pkg->package.elements[i];
		union acpi_object *connector_id;
		union acpi_object *info;

		if (obj->type != ACPI_TYPE_PACKAGE || obj->package.count < 2) {
			DRM_DEBUG_DRIVER("Invalid object for MUX #%d\n", i);
			continue;
		}

		connector_id = &obj->package.elements[0];
		info = &obj->package.elements[1];
		if (info->type != ACPI_TYPE_BUFFER || info->buffer.length < 4) {
			DRM_DEBUG_DRIVER("Invalid info for MUX obj #%d\n", i);
			continue;
		}

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

	ACPI_FREE(pkg);
}

static acpi_handle intel_dsm_pci_probe(struct pci_dev *pdev)
{
	acpi_handle dhandle;

	dhandle = ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return NULL;

	if (!acpi_check_dsm(dhandle, &intel_dsm_guid, INTEL_DSM_REVISION_ID,
			    1 << INTEL_DSM_FN_PLATFORM_MUX_INFO)) {
		DRM_DEBUG_KMS("no _DSM method for intel device\n");
		return NULL;
	}

	intel_dsm_platform_mux_info(dhandle);

	return dhandle;
}

static bool intel_dsm_detect(void)
{
	acpi_handle dhandle = NULL;
	char acpi_method_name[255] = {};
	struct acpi_buffer buffer = {sizeof(acpi_method_name), acpi_method_name};
	struct pci_dev *pdev = NULL;
	int vga_count = 0;

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev)) != NULL) {
		vga_count++;
		dhandle = intel_dsm_pci_probe(pdev) ?: dhandle;
	}

	if (vga_count == 2 && dhandle) {
		acpi_get_name(dhandle, ACPI_FULL_PATHNAME, &buffer);
		DRM_DEBUG_DRIVER("vga_switcheroo: detected DSM switching method %s handle\n",
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

void intel_dsm_get_bios_data_funcs_supported(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	acpi_handle dhandle;
	union acpi_object *obj;

	dhandle = ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return;

	obj = acpi_evaluate_dsm(dhandle, &intel_dsm_guid2, INTEL_DSM_REVISION_ID,
				INTEL_DSM_FN_GET_BIOS_DATA_FUNCS_SUPPORTED, NULL);
	if (obj)
		ACPI_FREE(obj);
}

/*
 * ACPI Specification, Revision 5.0, Appendix B.3.2 _DOD (Enumerate All Devices
 * Attached to the Display Adapter).
 */
#define ACPI_DISPLAY_INDEX_SHIFT		0
#define ACPI_DISPLAY_INDEX_MASK			(0xf << 0)
#define ACPI_DISPLAY_PORT_ATTACHMENT_SHIFT	4
#define ACPI_DISPLAY_PORT_ATTACHMENT_MASK	(0xf << 4)
#define ACPI_DISPLAY_TYPE_SHIFT			8
#define ACPI_DISPLAY_TYPE_MASK			(0xf << 8)
#define ACPI_DISPLAY_TYPE_OTHER			(0 << 8)
#define ACPI_DISPLAY_TYPE_VGA			(1 << 8)
#define ACPI_DISPLAY_TYPE_TV			(2 << 8)
#define ACPI_DISPLAY_TYPE_EXTERNAL_DIGITAL	(3 << 8)
#define ACPI_DISPLAY_TYPE_INTERNAL_DIGITAL	(4 << 8)
#define ACPI_VENDOR_SPECIFIC_SHIFT		12
#define ACPI_VENDOR_SPECIFIC_MASK		(0xf << 12)
#define ACPI_BIOS_CAN_DETECT			(1 << 16)
#define ACPI_DEPENDS_ON_VGA			(1 << 17)
#define ACPI_PIPE_ID_SHIFT			18
#define ACPI_PIPE_ID_MASK			(7 << 18)
#define ACPI_DEVICE_ID_SCHEME			(1ULL << 31)

static u32 acpi_display_type(struct intel_connector *connector)
{
	u32 display_type;

	switch (connector->base.connector_type) {
	case DRM_MODE_CONNECTOR_VGA:
	case DRM_MODE_CONNECTOR_DVIA:
		display_type = ACPI_DISPLAY_TYPE_VGA;
		break;
	case DRM_MODE_CONNECTOR_Composite:
	case DRM_MODE_CONNECTOR_SVIDEO:
	case DRM_MODE_CONNECTOR_Component:
	case DRM_MODE_CONNECTOR_9PinDIN:
	case DRM_MODE_CONNECTOR_TV:
		display_type = ACPI_DISPLAY_TYPE_TV;
		break;
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
		display_type = ACPI_DISPLAY_TYPE_EXTERNAL_DIGITAL;
		break;
	case DRM_MODE_CONNECTOR_LVDS:
	case DRM_MODE_CONNECTOR_eDP:
	case DRM_MODE_CONNECTOR_DSI:
		display_type = ACPI_DISPLAY_TYPE_INTERNAL_DIGITAL;
		break;
	case DRM_MODE_CONNECTOR_Unknown:
	case DRM_MODE_CONNECTOR_VIRTUAL:
		display_type = ACPI_DISPLAY_TYPE_OTHER;
		break;
	default:
		MISSING_CASE(connector->base.connector_type);
		display_type = ACPI_DISPLAY_TYPE_OTHER;
		break;
	}

	return display_type;
}

void intel_acpi_device_id_update(struct drm_i915_private *dev_priv)
{
	struct drm_device *drm_dev = &dev_priv->drm;
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;
	u8 display_index[16] = {};

	/* Populate the ACPI IDs for all connectors for a given drm_device */
	drm_connector_list_iter_begin(drm_dev, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		u32 device_id, type;

		device_id = acpi_display_type(connector);

		/* Use display type specific display index. */
		type = (device_id & ACPI_DISPLAY_TYPE_MASK)
			>> ACPI_DISPLAY_TYPE_SHIFT;
		device_id |= display_index[type]++ << ACPI_DISPLAY_INDEX_SHIFT;

		connector->acpi_device_id = device_id;
	}
	drm_connector_list_iter_end(&conn_iter);
}

/* NOTE: The connector order must be final before this is called. */
void intel_acpi_assign_connector_fwnodes(struct drm_i915_private *i915)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_device *drm_dev = &i915->drm;
	struct fwnode_handle *fwnode = NULL;
	struct drm_connector *connector;
	struct acpi_device *adev;

	drm_connector_list_iter_begin(drm_dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		/* Always getting the next, even when the last was not used. */
		fwnode = device_get_next_child_node(drm_dev->dev, fwnode);
		if (!fwnode)
			break;

		switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_LVDS:
		case DRM_MODE_CONNECTOR_eDP:
		case DRM_MODE_CONNECTOR_DSI:
			/*
			 * Integrated displays have a specific address 0x1f on
			 * most Intel platforms, but not on all of them.
			 */
			adev = acpi_find_child_device(ACPI_COMPANION(drm_dev->dev),
						      0x1f, 0);
			if (adev) {
				connector->fwnode =
					fwnode_handle_get(acpi_fwnode_handle(adev));
				break;
			}
			fallthrough;
		default:
			connector->fwnode = fwnode_handle_get(fwnode);
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	/*
	 * device_get_next_child_node() takes a reference on the fwnode, if
	 * we stopped iterating because we are out of connectors we need to
	 * put this, otherwise fwnode is NULL and the put is a no-op.
	 */
	fwnode_handle_put(fwnode);
}

void intel_acpi_video_register(struct drm_i915_private *i915)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	acpi_video_register();

	/*
	 * If i915 is driving an internal panel without registering its native
	 * backlight handler try to register the acpi_video backlight.
	 * For panels not driven by i915 another GPU driver may still register
	 * a native backlight later and acpi_video_register_backlight() should
	 * only be called after any native backlights have been registered.
	 */
	drm_connector_list_iter_begin(&i915->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_panel *panel = &to_intel_connector(connector)->panel;

		if (panel->backlight.funcs && !panel->backlight.device) {
			acpi_video_register_backlight();
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
}
