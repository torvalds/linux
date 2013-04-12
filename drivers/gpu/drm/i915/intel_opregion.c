/*
 * Copyright 2008 Intel Corporation <hong.liu@intel.com>
 * Copyright 2008 Red Hat <mjg@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL INTEL AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/acpi_io.h>
#include <acpi/video.h>

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "intel_drv.h"

#define PCI_ASLE 0xe4
#define PCI_ASLS 0xfc

#define OPREGION_HEADER_OFFSET 0
#define OPREGION_ACPI_OFFSET   0x100
#define   ACPI_CLID 0x01ac /* current lid state indicator */
#define   ACPI_CDCK 0x01b0 /* current docking state indicator */
#define OPREGION_SWSCI_OFFSET  0x200
#define OPREGION_ASLE_OFFSET   0x300
#define OPREGION_VBT_OFFSET    0x400

#define OPREGION_SIGNATURE "IntelGraphicsMem"
#define MBOX_ACPI      (1<<0)
#define MBOX_SWSCI     (1<<1)
#define MBOX_ASLE      (1<<2)

struct opregion_header {
	u8 signature[16];
	u32 size;
	u32 opregion_ver;
	u8 bios_ver[32];
	u8 vbios_ver[16];
	u8 driver_ver[16];
	u32 mboxes;
	u8 reserved[164];
} __attribute__((packed));

/* OpRegion mailbox #1: public ACPI methods */
struct opregion_acpi {
	u32 drdy;       /* driver readiness */
	u32 csts;       /* notification status */
	u32 cevt;       /* current event */
	u8 rsvd1[20];
	u32 didl[8];    /* supported display devices ID list */
	u32 cpdl[8];    /* currently presented display list */
	u32 cadl[8];    /* currently active display list */
	u32 nadl[8];    /* next active devices list */
	u32 aslp;       /* ASL sleep time-out */
	u32 tidx;       /* toggle table index */
	u32 chpd;       /* current hotplug enable indicator */
	u32 clid;       /* current lid state*/
	u32 cdck;       /* current docking state */
	u32 sxsw;       /* Sx state resume */
	u32 evts;       /* ASL supported events */
	u32 cnot;       /* current OS notification */
	u32 nrdy;       /* driver status */
	u8 rsvd2[60];
} __attribute__((packed));

/* OpRegion mailbox #2: SWSCI */
struct opregion_swsci {
	u32 scic;       /* SWSCI command|status|data */
	u32 parm;       /* command parameters */
	u32 dslp;       /* driver sleep time-out */
	u8 rsvd[244];
} __attribute__((packed));

/* OpRegion mailbox #3: ASLE */
struct opregion_asle {
	u32 ardy;       /* driver readiness */
	u32 aslc;       /* ASLE interrupt command */
	u32 tche;       /* technology enabled indicator */
	u32 alsi;       /* current ALS illuminance reading */
	u32 bclp;       /* backlight brightness to set */
	u32 pfit;       /* panel fitting state */
	u32 cblv;       /* current brightness level */
	u16 bclm[20];   /* backlight level duty cycle mapping table */
	u32 cpfm;       /* current panel fitting mode */
	u32 epfm;       /* enabled panel fitting modes */
	u8 plut[74];    /* panel LUT and identifier */
	u32 pfmb;       /* PWM freq and min brightness */
	u8 rsvd[102];
} __attribute__((packed));

/* ASLE irq request bits */
#define ASLE_SET_ALS_ILLUM     (1 << 0)
#define ASLE_SET_BACKLIGHT     (1 << 1)
#define ASLE_SET_PFIT          (1 << 2)
#define ASLE_SET_PWM_FREQ      (1 << 3)
#define ASLE_REQ_MSK           0xf

/* response bits of ASLE irq request */
#define ASLE_ALS_ILLUM_FAILED	(1<<10)
#define ASLE_BACKLIGHT_FAILED	(1<<12)
#define ASLE_PFIT_FAILED	(1<<14)
#define ASLE_PWM_FREQ_FAILED	(1<<16)

/* ASLE backlight brightness to set */
#define ASLE_BCLP_VALID                (1<<31)
#define ASLE_BCLP_MSK          (~(1<<31))

/* ASLE panel fitting request */
#define ASLE_PFIT_VALID         (1<<31)
#define ASLE_PFIT_CENTER (1<<0)
#define ASLE_PFIT_STRETCH_TEXT (1<<1)
#define ASLE_PFIT_STRETCH_GFX (1<<2)

/* PWM frequency and minimum brightness */
#define ASLE_PFMB_BRIGHTNESS_MASK (0xff)
#define ASLE_PFMB_BRIGHTNESS_VALID (1<<8)
#define ASLE_PFMB_PWM_MASK (0x7ffffe00)
#define ASLE_PFMB_PWM_VALID (1<<31)

#define ASLE_CBLV_VALID         (1<<31)

#define ACPI_OTHER_OUTPUT (0<<8)
#define ACPI_VGA_OUTPUT (1<<8)
#define ACPI_TV_OUTPUT (2<<8)
#define ACPI_DIGITAL_OUTPUT (3<<8)
#define ACPI_LVDS_OUTPUT (4<<8)

#ifdef CONFIG_ACPI
static u32 asle_set_backlight(struct drm_device *dev, u32 bclp)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct opregion_asle __iomem *asle = dev_priv->opregion.asle;

	DRM_DEBUG_DRIVER("bclp = 0x%08x\n", bclp);

	if (!(bclp & ASLE_BCLP_VALID))
		return ASLE_BACKLIGHT_FAILED;

	bclp &= ASLE_BCLP_MSK;
	if (bclp > 255)
		return ASLE_BACKLIGHT_FAILED;

	intel_panel_set_backlight(dev, bclp, 255);
	iowrite32((bclp*0x64)/0xff | ASLE_CBLV_VALID, &asle->cblv);

	return 0;
}

static u32 asle_set_als_illum(struct drm_device *dev, u32 alsi)
{
	/* alsi is the current ALS reading in lux. 0 indicates below sensor
	   range, 0xffff indicates above sensor range. 1-0xfffe are valid */
	return 0;
}

static u32 asle_set_pwm_freq(struct drm_device *dev, u32 pfmb)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	if (pfmb & ASLE_PFMB_PWM_VALID) {
		u32 blc_pwm_ctl = I915_READ(BLC_PWM_CTL);
		u32 pwm = pfmb & ASLE_PFMB_PWM_MASK;
		blc_pwm_ctl &= BACKLIGHT_DUTY_CYCLE_MASK;
		pwm = pwm >> 9;
		/* FIXME - what do we do with the PWM? */
	}
	return 0;
}

static u32 asle_set_pfit(struct drm_device *dev, u32 pfit)
{
	/* Panel fitting is currently controlled by the X code, so this is a
	   noop until modesetting support works fully */
	if (!(pfit & ASLE_PFIT_VALID))
		return ASLE_PFIT_FAILED;
	return 0;
}

void intel_opregion_asle_intr(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct opregion_asle __iomem *asle = dev_priv->opregion.asle;
	u32 asle_stat = 0;
	u32 asle_req;

	if (!asle)
		return;

	asle_req = ioread32(&asle->aslc) & ASLE_REQ_MSK;

	if (!asle_req) {
		DRM_DEBUG_DRIVER("non asle set request??\n");
		return;
	}

	if (asle_req & ASLE_SET_ALS_ILLUM)
		asle_stat |= asle_set_als_illum(dev, ioread32(&asle->alsi));

	if (asle_req & ASLE_SET_BACKLIGHT)
		asle_stat |= asle_set_backlight(dev, ioread32(&asle->bclp));

	if (asle_req & ASLE_SET_PFIT)
		asle_stat |= asle_set_pfit(dev, ioread32(&asle->pfit));

	if (asle_req & ASLE_SET_PWM_FREQ)
		asle_stat |= asle_set_pwm_freq(dev, ioread32(&asle->pfmb));

	iowrite32(asle_stat, &asle->aslc);
}

void intel_opregion_gse_intr(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct opregion_asle __iomem *asle = dev_priv->opregion.asle;
	u32 asle_stat = 0;
	u32 asle_req;

	if (!asle)
		return;

	asle_req = ioread32(&asle->aslc) & ASLE_REQ_MSK;

	if (!asle_req) {
		DRM_DEBUG_DRIVER("non asle set request??\n");
		return;
	}

	if (asle_req & ASLE_SET_ALS_ILLUM) {
		DRM_DEBUG_DRIVER("Illum is not supported\n");
		asle_stat |= ASLE_ALS_ILLUM_FAILED;
	}

	if (asle_req & ASLE_SET_BACKLIGHT)
		asle_stat |= asle_set_backlight(dev, ioread32(&asle->bclp));

	if (asle_req & ASLE_SET_PFIT) {
		DRM_DEBUG_DRIVER("Pfit is not supported\n");
		asle_stat |= ASLE_PFIT_FAILED;
	}

	if (asle_req & ASLE_SET_PWM_FREQ) {
		DRM_DEBUG_DRIVER("PWM freq is not supported\n");
		asle_stat |= ASLE_PWM_FREQ_FAILED;
	}

	iowrite32(asle_stat, &asle->aslc);
}
#define ASLE_ALS_EN    (1<<0)
#define ASLE_BLC_EN    (1<<1)
#define ASLE_PFIT_EN   (1<<2)
#define ASLE_PFMB_EN   (1<<3)

void intel_opregion_enable_asle(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct opregion_asle __iomem *asle = dev_priv->opregion.asle;

	if (asle) {
		if (IS_MOBILE(dev))
			intel_enable_asle(dev);

		iowrite32(ASLE_ALS_EN | ASLE_BLC_EN | ASLE_PFIT_EN |
			  ASLE_PFMB_EN,
			  &asle->tche);
		iowrite32(1, &asle->ardy);
	}
}

#define ACPI_EV_DISPLAY_SWITCH (1<<0)
#define ACPI_EV_LID            (1<<1)
#define ACPI_EV_DOCK           (1<<2)

static struct intel_opregion *system_opregion;

static int intel_opregion_video_event(struct notifier_block *nb,
				      unsigned long val, void *data)
{
	/* The only video events relevant to opregion are 0x80. These indicate
	   either a docking event, lid switch or display switch request. In
	   Linux, these are handled by the dock, button and video drivers.
	*/

	struct opregion_acpi __iomem *acpi;
	struct acpi_bus_event *event = data;
	int ret = NOTIFY_OK;

	if (strcmp(event->device_class, ACPI_VIDEO_CLASS) != 0)
		return NOTIFY_DONE;

	if (!system_opregion)
		return NOTIFY_DONE;

	acpi = system_opregion->acpi;

	if (event->type == 0x80 &&
	    (ioread32(&acpi->cevt) & 1) == 0)
		ret = NOTIFY_BAD;

	iowrite32(0, &acpi->csts);

	return ret;
}

static struct notifier_block intel_opregion_notifier = {
	.notifier_call = intel_opregion_video_event,
};

/*
 * Initialise the DIDL field in opregion. This passes a list of devices to
 * the firmware. Values are defined by section B.4.2 of the ACPI specification
 * (version 3)
 */

static void intel_didl_outputs(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_opregion *opregion = &dev_priv->opregion;
	struct drm_connector *connector;
	acpi_handle handle;
	struct acpi_device *acpi_dev, *acpi_cdev, *acpi_video_bus = NULL;
	unsigned long long device_id;
	acpi_status status;
	u32 temp;
	int i = 0;

	handle = DEVICE_ACPI_HANDLE(&dev->pdev->dev);
	if (!handle || acpi_bus_get_device(handle, &acpi_dev))
		return;

	if (acpi_is_video_device(handle))
		acpi_video_bus = acpi_dev;
	else {
		list_for_each_entry(acpi_cdev, &acpi_dev->children, node) {
			if (acpi_is_video_device(acpi_cdev->handle)) {
				acpi_video_bus = acpi_cdev;
				break;
			}
		}
	}

	if (!acpi_video_bus) {
		pr_warn("No ACPI video bus found\n");
		return;
	}

	list_for_each_entry(acpi_cdev, &acpi_video_bus->children, node) {
		if (i >= 8) {
			dev_printk(KERN_ERR, &dev->pdev->dev,
				    "More than 8 outputs detected\n");
			return;
		}
		status =
			acpi_evaluate_integer(acpi_cdev->handle, "_ADR",
						NULL, &device_id);
		if (ACPI_SUCCESS(status)) {
			if (!device_id)
				goto blind_set;
			iowrite32((u32)(device_id & 0x0f0f),
				  &opregion->acpi->didl[i]);
			i++;
		}
	}

end:
	/* If fewer than 8 outputs, the list must be null terminated */
	if (i < 8)
		iowrite32(0, &opregion->acpi->didl[i]);
	return;

blind_set:
	i = 0;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		int output_type = ACPI_OTHER_OUTPUT;
		if (i >= 8) {
			dev_printk(KERN_ERR, &dev->pdev->dev,
				    "More than 8 outputs detected\n");
			return;
		}
		switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_VGA:
		case DRM_MODE_CONNECTOR_DVIA:
			output_type = ACPI_VGA_OUTPUT;
			break;
		case DRM_MODE_CONNECTOR_Composite:
		case DRM_MODE_CONNECTOR_SVIDEO:
		case DRM_MODE_CONNECTOR_Component:
		case DRM_MODE_CONNECTOR_9PinDIN:
			output_type = ACPI_TV_OUTPUT;
			break;
		case DRM_MODE_CONNECTOR_DVII:
		case DRM_MODE_CONNECTOR_DVID:
		case DRM_MODE_CONNECTOR_DisplayPort:
		case DRM_MODE_CONNECTOR_HDMIA:
		case DRM_MODE_CONNECTOR_HDMIB:
			output_type = ACPI_DIGITAL_OUTPUT;
			break;
		case DRM_MODE_CONNECTOR_LVDS:
			output_type = ACPI_LVDS_OUTPUT;
			break;
		}
		temp = ioread32(&opregion->acpi->didl[i]);
		iowrite32(temp | (1<<31) | output_type | i,
			  &opregion->acpi->didl[i]);
		i++;
	}
	goto end;
}

static void intel_setup_cadls(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_opregion *opregion = &dev_priv->opregion;
	int i = 0;
	u32 disp_id;

	/* Initialize the CADL field by duplicating the DIDL values.
	 * Technically, this is not always correct as display outputs may exist,
	 * but not active. This initialization is necessary for some Clevo
	 * laptops that check this field before processing the brightness and
	 * display switching hotkeys. Just like DIDL, CADL is NULL-terminated if
	 * there are less than eight devices. */
	do {
		disp_id = ioread32(&opregion->acpi->didl[i]);
		iowrite32(disp_id, &opregion->acpi->cadl[i]);
	} while (++i < 8 && disp_id != 0);
}

void intel_opregion_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_opregion *opregion = &dev_priv->opregion;

	if (!opregion->header)
		return;

	if (opregion->acpi) {
		if (drm_core_check_feature(dev, DRIVER_MODESET)) {
			intel_didl_outputs(dev);
			intel_setup_cadls(dev);
		}

		/* Notify BIOS we are ready to handle ACPI video ext notifs.
		 * Right now, all the events are handled by the ACPI video module.
		 * We don't actually need to do anything with them. */
		iowrite32(0, &opregion->acpi->csts);
		iowrite32(1, &opregion->acpi->drdy);

		system_opregion = opregion;
		register_acpi_notifier(&intel_opregion_notifier);
	}

	if (opregion->asle)
		intel_opregion_enable_asle(dev);
}

void intel_opregion_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_opregion *opregion = &dev_priv->opregion;

	if (!opregion->header)
		return;

	if (opregion->acpi) {
		iowrite32(0, &opregion->acpi->drdy);

		system_opregion = NULL;
		unregister_acpi_notifier(&intel_opregion_notifier);
	}

	/* just clear all opregion memory pointers now */
	iounmap(opregion->header);
	opregion->header = NULL;
	opregion->acpi = NULL;
	opregion->swsci = NULL;
	opregion->asle = NULL;
	opregion->vbt = NULL;
}
#endif

int intel_opregion_setup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_opregion *opregion = &dev_priv->opregion;
	void __iomem *base;
	u32 asls, mboxes;
	char buf[sizeof(OPREGION_SIGNATURE)];
	int err = 0;

	pci_read_config_dword(dev->pdev, PCI_ASLS, &asls);
	DRM_DEBUG_DRIVER("graphic opregion physical addr: 0x%x\n", asls);
	if (asls == 0) {
		DRM_DEBUG_DRIVER("ACPI OpRegion not supported!\n");
		return -ENOTSUPP;
	}

	base = acpi_os_ioremap(asls, OPREGION_SIZE);
	if (!base)
		return -ENOMEM;

	memcpy_fromio(buf, base, sizeof(buf));

	if (memcmp(buf, OPREGION_SIGNATURE, 16)) {
		DRM_DEBUG_DRIVER("opregion signature mismatch\n");
		err = -EINVAL;
		goto err_out;
	}
	opregion->header = base;
	opregion->vbt = base + OPREGION_VBT_OFFSET;

	opregion->lid_state = base + ACPI_CLID;

	mboxes = ioread32(&opregion->header->mboxes);
	if (mboxes & MBOX_ACPI) {
		DRM_DEBUG_DRIVER("Public ACPI methods supported\n");
		opregion->acpi = base + OPREGION_ACPI_OFFSET;
	}

	if (mboxes & MBOX_SWSCI) {
		DRM_DEBUG_DRIVER("SWSCI supported\n");
		opregion->swsci = base + OPREGION_SWSCI_OFFSET;
	}
	if (mboxes & MBOX_ASLE) {
		DRM_DEBUG_DRIVER("ASLE supported\n");
		opregion->asle = base + OPREGION_ASLE_OFFSET;
	}

	return 0;

err_out:
	iounmap(base);
	return err;
}
