/*
 * Copyright 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/acpi.h>
#include "psb_drv.h"
#include "psb_intel_reg.h"

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
} __packed;

/* OpRegion mailbox #1: public ACPI methods */
struct opregion_acpi {
	u32 drdy;	/* driver readiness */
	u32 csts;	/* notification status */
	u32 cevt;	/* current event */
	u8 rsvd1[20];
	u32 didl[8];	/* supported display devices ID list */
	u32 cpdl[8];	/* currently presented display list */
	u32 cadl[8];	/* currently active display list */
	u32 nadl[8];	/* next active devices list */
	u32 aslp;	/* ASL sleep time-out */
	u32 tidx;	/* toggle table index */
	u32 chpd;	/* current hotplug enable indicator */
	u32 clid;	/* current lid state*/
	u32 cdck;	/* current docking state */
	u32 sxsw;	/* Sx state resume */
	u32 evts;	/* ASL supported events */
	u32 cnot;	/* current OS notification */
	u32 nrdy;	/* driver status */
	u8 rsvd2[60];
} __packed;

/* OpRegion mailbox #2: SWSCI */
struct opregion_swsci {
	/*FIXME: add it later*/
} __packed;

/* OpRegion mailbox #3: ASLE */
struct opregion_asle {
	u32 ardy;	/* driver readiness */
	u32 aslc;	/* ASLE interrupt command */
	u32 tche;	/* technology enabled indicator */
	u32 alsi;	/* current ALS illuminance reading */
	u32 bclp;	/* backlight brightness to set */
	u32 pfit;	/* panel fitting state */
	u32 cblv;	/* current brightness level */
	u16 bclm[20];	/* backlight level duty cycle mapping table */
	u32 cpfm;	/* current panel fitting mode */
	u32 epfm;	/* enabled panel fitting modes */
	u8 plut[74];	/* panel LUT and identifier */
	u32 pfmb;	/* PWM freq and min brightness */
	u8 rsvd[102];
} __packed;

/* ASLE irq request bits */
#define ASLE_SET_ALS_ILLUM     (1 << 0)
#define ASLE_SET_BACKLIGHT     (1 << 1)
#define ASLE_SET_PFIT          (1 << 2)
#define ASLE_SET_PWM_FREQ      (1 << 3)
#define ASLE_REQ_MSK           0xf

/* response bits of ASLE irq request */
#define ASLE_ALS_ILLUM_FAILED   (1<<10)
#define ASLE_BACKLIGHT_FAILED   (1<<12)
#define ASLE_PFIT_FAILED        (1<<14)
#define ASLE_PWM_FREQ_FAILED    (1<<16)

/* ASLE backlight brightness to set */
#define ASLE_BCLP_VALID                (1<<31)
#define ASLE_BCLP_MSK          (~(1<<31))

/* ASLE panel fitting request */
#define ASLE_PFIT_VALID         (1<<31)
#define ASLE_PFIT_CENTER (1<<0)
#define ASLE_PFIT_STRETCH_TEXT (1<<1)
#define ASLE_PFIT_STRETCH_GFX (1<<2)

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

static struct psb_intel_opregion *system_opregion;

static u32 asle_set_backlight(struct drm_device *dev, u32 bclp)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct opregion_asle *asle = dev_priv->opregion.asle;
	struct backlight_device *bd = dev_priv->backlight_device;

	DRM_DEBUG_DRIVER("asle set backlight %x\n", bclp);

	if (!(bclp & ASLE_BCLP_VALID))
		return ASLE_BACKLIGHT_FAILED;

	if (bd == NULL)
		return ASLE_BACKLIGHT_FAILED;

	bclp &= ASLE_BCLP_MSK;
	if (bclp > 255)
		return ASLE_BACKLIGHT_FAILED;

	gma_backlight_set(dev, bclp * bd->props.max_brightness / 255);

	asle->cblv = (bclp * 0x64) / 0xff | ASLE_CBLV_VALID;

	return 0;
}

static void psb_intel_opregion_asle_work(struct work_struct *work)
{
	struct psb_intel_opregion *opregion =
		container_of(work, struct psb_intel_opregion, asle_work);
	struct drm_psb_private *dev_priv =
		container_of(opregion, struct drm_psb_private, opregion);
	struct opregion_asle *asle = opregion->asle;
	u32 asle_stat = 0;
	u32 asle_req;

	if (!asle)
		return;

	asle_req = asle->aslc & ASLE_REQ_MSK;
	if (!asle_req) {
		DRM_DEBUG_DRIVER("non asle set request??\n");
		return;
	}

	if (asle_req & ASLE_SET_BACKLIGHT)
		asle_stat |= asle_set_backlight(dev_priv->dev, asle->bclp);

	asle->aslc = asle_stat;

}

void psb_intel_opregion_asle_intr(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (dev_priv->opregion.asle)
		schedule_work(&dev_priv->opregion.asle_work);
}

#define ASLE_ALS_EN    (1<<0)
#define ASLE_BLC_EN    (1<<1)
#define ASLE_PFIT_EN   (1<<2)
#define ASLE_PFMB_EN   (1<<3)

void psb_intel_opregion_enable_asle(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct opregion_asle *asle = dev_priv->opregion.asle;

	if (asle && system_opregion ) {
		/* Don't do this on Medfield or other non PC like devices, they
		   use the bit for something different altogether */
		psb_enable_pipestat(dev_priv, 0, PIPE_LEGACY_BLC_EVENT_ENABLE);
		psb_enable_pipestat(dev_priv, 1, PIPE_LEGACY_BLC_EVENT_ENABLE);

		asle->tche = ASLE_ALS_EN | ASLE_BLC_EN | ASLE_PFIT_EN
								| ASLE_PFMB_EN;
		asle->ardy = 1;
	}
}

#define ACPI_EV_DISPLAY_SWITCH (1<<0)
#define ACPI_EV_LID            (1<<1)
#define ACPI_EV_DOCK           (1<<2)


static int psb_intel_opregion_video_event(struct notifier_block *nb,
					  unsigned long val, void *data)
{
	/* The only video events relevant to opregion are 0x80. These indicate
	   either a docking event, lid switch or display switch request. In
	   Linux, these are handled by the dock, button and video drivers.
	   We might want to fix the video driver to be opregion-aware in
	   future, but right now we just indicate to the firmware that the
	   request has been handled */

	struct opregion_acpi *acpi;

	if (!system_opregion)
		return NOTIFY_DONE;

	acpi = system_opregion->acpi;
	acpi->csts = 0;

	return NOTIFY_OK;
}

static struct notifier_block psb_intel_opregion_notifier = {
	.notifier_call = psb_intel_opregion_video_event,
};

void psb_intel_opregion_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_opregion *opregion = &dev_priv->opregion;

	if (!opregion->header)
		return;

	if (opregion->acpi) {
		/* Notify BIOS we are ready to handle ACPI video ext notifs.
		 * Right now, all the events are handled by the ACPI video
		 * module. We don't actually need to do anything with them. */
		opregion->acpi->csts = 0;
		opregion->acpi->drdy = 1;

		system_opregion = opregion;
		register_acpi_notifier(&psb_intel_opregion_notifier);
	}
}

void psb_intel_opregion_fini(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_opregion *opregion = &dev_priv->opregion;

	if (!opregion->header)
		return;

	if (opregion->acpi) {
		opregion->acpi->drdy = 0;

		system_opregion = NULL;
		unregister_acpi_notifier(&psb_intel_opregion_notifier);
	}

	cancel_work_sync(&opregion->asle_work);

	/* just clear all opregion memory pointers now */
	iounmap(opregion->header);
	opregion->header = NULL;
	opregion->acpi = NULL;
	opregion->swsci = NULL;
	opregion->asle = NULL;
	opregion->vbt = NULL;
}

int psb_intel_opregion_setup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_opregion *opregion = &dev_priv->opregion;
	u32 opregion_phy, mboxes;
	void __iomem *base;
	int err = 0;

	pci_read_config_dword(dev->pdev, PCI_ASLS, &opregion_phy);
	if (opregion_phy == 0) {
		DRM_DEBUG_DRIVER("ACPI Opregion not supported\n");
		return -ENOTSUPP;
	}

	INIT_WORK(&opregion->asle_work, psb_intel_opregion_asle_work);

	DRM_DEBUG("OpRegion detected at 0x%8x\n", opregion_phy);
	base = acpi_os_ioremap(opregion_phy, 8*1024);
	if (!base)
		return -ENOMEM;

	if (memcmp(base, OPREGION_SIGNATURE, 16)) {
		DRM_DEBUG_DRIVER("opregion signature mismatch\n");
		err = -EINVAL;
		goto err_out;
	}

	opregion->header = base;
	opregion->vbt = base + OPREGION_VBT_OFFSET;

	opregion->lid_state = base + ACPI_CLID;

	mboxes = opregion->header->mboxes;
	if (mboxes & MBOX_ACPI) {
		DRM_DEBUG_DRIVER("Public ACPI methods supported\n");
		opregion->acpi = base + OPREGION_ACPI_OFFSET;
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

