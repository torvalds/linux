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

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/firmware.h>
#include <acpi/video.h>

#include <drm/drm_edid.h>

#include "i915_drv.h"
#include "intel_acpi.h"
#include "intel_backlight.h"
#include "intel_display_types.h"
#include "intel_opregion.h"
#include "intel_pci_config.h"

#define OPREGION_HEADER_OFFSET 0
#define OPREGION_ACPI_OFFSET   0x100
#define   ACPI_CLID 0x01ac /* current lid state indicator */
#define   ACPI_CDCK 0x01b0 /* current docking state indicator */
#define OPREGION_SWSCI_OFFSET  0x200
#define OPREGION_ASLE_OFFSET   0x300
#define OPREGION_VBT_OFFSET    0x400
#define OPREGION_ASLE_EXT_OFFSET	0x1C00

#define OPREGION_SIGNATURE "IntelGraphicsMem"
#define MBOX_ACPI		BIT(0)	/* Mailbox #1 */
#define MBOX_SWSCI		BIT(1)	/* Mailbox #2 (obsolete from v2.x) */
#define MBOX_ASLE		BIT(2)	/* Mailbox #3 */
#define MBOX_ASLE_EXT		BIT(4)	/* Mailbox #5 */
#define MBOX_BACKLIGHT		BIT(5)	/* Mailbox #2 (valid from v3.x) */

#define PCON_HEADLESS_SKU	BIT(13)

struct opregion_header {
	u8 signature[16];
	u32 size;
	struct {
		u8 rsvd;
		u8 revision;
		u8 minor;
		u8 major;
	}  __packed over;
	u8 bios_ver[32];
	u8 vbios_ver[16];
	u8 driver_ver[16];
	u32 mboxes;
	u32 driver_model;
	u32 pcon;
	u8 dver[32];
	u8 rsvd[124];
} __packed;

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
	u32 did2[7];	/* extended supported display devices ID list */
	u32 cpd2[7];	/* extended attached display devices list */
	u8 rsvd2[4];
} __packed;

/* OpRegion mailbox #2: SWSCI */
struct opregion_swsci {
	u32 scic;       /* SWSCI command|status|data */
	u32 parm;       /* command parameters */
	u32 dslp;       /* driver sleep time-out */
	u8 rsvd[244];
} __packed;

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
	u32 cddv;       /* color correction default values */
	u32 pcft;       /* power conservation features */
	u32 srot;       /* supported rotation angles */
	u32 iuer;       /* IUER events */
	u64 fdss;
	u32 fdsp;
	u32 stat;
	u64 rvda;	/* Physical (2.0) or relative from opregion (2.1+)
			 * address of raw VBT data. */
	u32 rvds;	/* Size of raw vbt data */
	u8 rsvd[58];
} __packed;

/* OpRegion mailbox #5: ASLE ext */
struct opregion_asle_ext {
	u32 phed;	/* Panel Header */
	u8 bddc[256];	/* Panel EDID */
	u8 rsvd[764];
} __packed;

/* Driver readiness indicator */
#define ASLE_ARDY_READY		(1 << 0)
#define ASLE_ARDY_NOT_READY	(0 << 0)

/* ASLE Interrupt Command (ASLC) bits */
#define ASLC_SET_ALS_ILLUM		(1 << 0)
#define ASLC_SET_BACKLIGHT		(1 << 1)
#define ASLC_SET_PFIT			(1 << 2)
#define ASLC_SET_PWM_FREQ		(1 << 3)
#define ASLC_SUPPORTED_ROTATION_ANGLES	(1 << 4)
#define ASLC_BUTTON_ARRAY		(1 << 5)
#define ASLC_CONVERTIBLE_INDICATOR	(1 << 6)
#define ASLC_DOCKING_INDICATOR		(1 << 7)
#define ASLC_ISCT_STATE_CHANGE		(1 << 8)
#define ASLC_REQ_MSK			0x1ff
/* response bits */
#define ASLC_ALS_ILLUM_FAILED		(1 << 10)
#define ASLC_BACKLIGHT_FAILED		(1 << 12)
#define ASLC_PFIT_FAILED		(1 << 14)
#define ASLC_PWM_FREQ_FAILED		(1 << 16)
#define ASLC_ROTATION_ANGLES_FAILED	(1 << 18)
#define ASLC_BUTTON_ARRAY_FAILED	(1 << 20)
#define ASLC_CONVERTIBLE_FAILED		(1 << 22)
#define ASLC_DOCKING_FAILED		(1 << 24)
#define ASLC_ISCT_STATE_FAILED		(1 << 26)

/* Technology enabled indicator */
#define ASLE_TCHE_ALS_EN	(1 << 0)
#define ASLE_TCHE_BLC_EN	(1 << 1)
#define ASLE_TCHE_PFIT_EN	(1 << 2)
#define ASLE_TCHE_PFMB_EN	(1 << 3)

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

/* IUER */
#define ASLE_IUER_DOCKING		(1 << 7)
#define ASLE_IUER_CONVERTIBLE		(1 << 6)
#define ASLE_IUER_ROTATION_LOCK_BTN	(1 << 4)
#define ASLE_IUER_VOLUME_DOWN_BTN	(1 << 3)
#define ASLE_IUER_VOLUME_UP_BTN		(1 << 2)
#define ASLE_IUER_WINDOWS_BTN		(1 << 1)
#define ASLE_IUER_POWER_BTN		(1 << 0)

#define ASLE_PHED_EDID_VALID_MASK	0x3

/* Software System Control Interrupt (SWSCI) */
#define SWSCI_SCIC_INDICATOR		(1 << 0)
#define SWSCI_SCIC_MAIN_FUNCTION_SHIFT	1
#define SWSCI_SCIC_MAIN_FUNCTION_MASK	(0xf << 1)
#define SWSCI_SCIC_SUB_FUNCTION_SHIFT	8
#define SWSCI_SCIC_SUB_FUNCTION_MASK	(0xff << 8)
#define SWSCI_SCIC_EXIT_PARAMETER_SHIFT	8
#define SWSCI_SCIC_EXIT_PARAMETER_MASK	(0xff << 8)
#define SWSCI_SCIC_EXIT_STATUS_SHIFT	5
#define SWSCI_SCIC_EXIT_STATUS_MASK	(7 << 5)
#define SWSCI_SCIC_EXIT_STATUS_SUCCESS	1

#define SWSCI_FUNCTION_CODE(main, sub) \
	((main) << SWSCI_SCIC_MAIN_FUNCTION_SHIFT | \
	 (sub) << SWSCI_SCIC_SUB_FUNCTION_SHIFT)

/* SWSCI: Get BIOS Data (GBDA) */
#define SWSCI_GBDA			4
#define SWSCI_GBDA_SUPPORTED_CALLS	SWSCI_FUNCTION_CODE(SWSCI_GBDA, 0)
#define SWSCI_GBDA_REQUESTED_CALLBACKS	SWSCI_FUNCTION_CODE(SWSCI_GBDA, 1)
#define SWSCI_GBDA_BOOT_DISPLAY_PREF	SWSCI_FUNCTION_CODE(SWSCI_GBDA, 4)
#define SWSCI_GBDA_PANEL_DETAILS	SWSCI_FUNCTION_CODE(SWSCI_GBDA, 5)
#define SWSCI_GBDA_TV_STANDARD		SWSCI_FUNCTION_CODE(SWSCI_GBDA, 6)
#define SWSCI_GBDA_INTERNAL_GRAPHICS	SWSCI_FUNCTION_CODE(SWSCI_GBDA, 7)
#define SWSCI_GBDA_SPREAD_SPECTRUM	SWSCI_FUNCTION_CODE(SWSCI_GBDA, 10)

/* SWSCI: System BIOS Callbacks (SBCB) */
#define SWSCI_SBCB			6
#define SWSCI_SBCB_SUPPORTED_CALLBACKS	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 0)
#define SWSCI_SBCB_INIT_COMPLETION	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 1)
#define SWSCI_SBCB_PRE_HIRES_SET_MODE	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 3)
#define SWSCI_SBCB_POST_HIRES_SET_MODE	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 4)
#define SWSCI_SBCB_DISPLAY_SWITCH	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 5)
#define SWSCI_SBCB_SET_TV_FORMAT	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 6)
#define SWSCI_SBCB_ADAPTER_POWER_STATE	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 7)
#define SWSCI_SBCB_DISPLAY_POWER_STATE	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 8)
#define SWSCI_SBCB_SET_BOOT_DISPLAY	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 9)
#define SWSCI_SBCB_SET_PANEL_DETAILS	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 10)
#define SWSCI_SBCB_SET_INTERNAL_GFX	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 11)
#define SWSCI_SBCB_POST_HIRES_TO_DOS_FS	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 16)
#define SWSCI_SBCB_SUSPEND_RESUME	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 17)
#define SWSCI_SBCB_SET_SPREAD_SPECTRUM	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 18)
#define SWSCI_SBCB_POST_VBE_PM		SWSCI_FUNCTION_CODE(SWSCI_SBCB, 19)
#define SWSCI_SBCB_ENABLE_DISABLE_AUDIO	SWSCI_FUNCTION_CODE(SWSCI_SBCB, 21)

#define MAX_DSLP	1500

static int check_swsci_function(struct drm_i915_private *i915, u32 function)
{
	struct opregion_swsci *swsci = i915->display.opregion.swsci;
	u32 main_function, sub_function;

	if (!swsci)
		return -ENODEV;

	main_function = (function & SWSCI_SCIC_MAIN_FUNCTION_MASK) >>
		SWSCI_SCIC_MAIN_FUNCTION_SHIFT;
	sub_function = (function & SWSCI_SCIC_SUB_FUNCTION_MASK) >>
		SWSCI_SCIC_SUB_FUNCTION_SHIFT;

	/* Check if we can call the function. See swsci_setup for details. */
	if (main_function == SWSCI_SBCB) {
		if ((i915->display.opregion.swsci_sbcb_sub_functions &
		     (1 << sub_function)) == 0)
			return -EINVAL;
	} else if (main_function == SWSCI_GBDA) {
		if ((i915->display.opregion.swsci_gbda_sub_functions &
		     (1 << sub_function)) == 0)
			return -EINVAL;
	}

	return 0;
}

static int swsci(struct drm_i915_private *dev_priv,
		 u32 function, u32 parm, u32 *parm_out)
{
	struct opregion_swsci *swsci = dev_priv->display.opregion.swsci;
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	u32 scic, dslp;
	u16 swsci_val;
	int ret;

	ret = check_swsci_function(dev_priv, function);
	if (ret)
		return ret;

	/* Driver sleep timeout in ms. */
	dslp = swsci->dslp;
	if (!dslp) {
		/* The spec says 2ms should be the default, but it's too small
		 * for some machines. */
		dslp = 50;
	} else if (dslp > MAX_DSLP) {
		/* Hey bios, trust must be earned. */
		DRM_INFO_ONCE("ACPI BIOS requests an excessive sleep of %u ms, "
			      "using %u ms instead\n", dslp, MAX_DSLP);
		dslp = MAX_DSLP;
	}

	/* The spec tells us to do this, but we are the only user... */
	scic = swsci->scic;
	if (scic & SWSCI_SCIC_INDICATOR) {
		drm_dbg(&dev_priv->drm, "SWSCI request already in progress\n");
		return -EBUSY;
	}

	scic = function | SWSCI_SCIC_INDICATOR;

	swsci->parm = parm;
	swsci->scic = scic;

	/* Ensure SCI event is selected and event trigger is cleared. */
	pci_read_config_word(pdev, SWSCI, &swsci_val);
	if (!(swsci_val & SWSCI_SCISEL) || (swsci_val & SWSCI_GSSCIE)) {
		swsci_val |= SWSCI_SCISEL;
		swsci_val &= ~SWSCI_GSSCIE;
		pci_write_config_word(pdev, SWSCI, swsci_val);
	}

	/* Use event trigger to tell bios to check the mail. */
	swsci_val |= SWSCI_GSSCIE;
	pci_write_config_word(pdev, SWSCI, swsci_val);

	/* Poll for the result. */
#define C (((scic = swsci->scic) & SWSCI_SCIC_INDICATOR) == 0)
	if (wait_for(C, dslp)) {
		drm_dbg(&dev_priv->drm, "SWSCI request timed out\n");
		return -ETIMEDOUT;
	}

	scic = (scic & SWSCI_SCIC_EXIT_STATUS_MASK) >>
		SWSCI_SCIC_EXIT_STATUS_SHIFT;

	/* Note: scic == 0 is an error! */
	if (scic != SWSCI_SCIC_EXIT_STATUS_SUCCESS) {
		drm_dbg(&dev_priv->drm, "SWSCI request error %u\n", scic);
		return -EIO;
	}

	if (parm_out)
		*parm_out = swsci->parm;

	return 0;

#undef C
}

#define DISPLAY_TYPE_CRT			0
#define DISPLAY_TYPE_TV				1
#define DISPLAY_TYPE_EXTERNAL_FLAT_PANEL	2
#define DISPLAY_TYPE_INTERNAL_FLAT_PANEL	3

int intel_opregion_notify_encoder(struct intel_encoder *intel_encoder,
				  bool enable)
{
	struct drm_i915_private *dev_priv = to_i915(intel_encoder->base.dev);
	u32 parm = 0;
	u32 type = 0;
	u32 port;
	int ret;

	/* don't care about old stuff for now */
	if (!HAS_DDI(dev_priv))
		return 0;

	/* Avoid port out of bounds checks if SWSCI isn't there. */
	ret = check_swsci_function(dev_priv, SWSCI_SBCB_DISPLAY_POWER_STATE);
	if (ret)
		return ret;

	if (intel_encoder->type == INTEL_OUTPUT_DSI)
		port = 0;
	else
		port = intel_encoder->port;

	if (port == PORT_E)  {
		port = 0;
	} else {
		parm |= 1 << port;
		port++;
	}

	/*
	 * The port numbering and mapping here is bizarre. The now-obsolete
	 * swsci spec supports ports numbered [0..4]. Port E is handled as a
	 * special case, but port F and beyond are not. The functionality is
	 * supposed to be obsolete for new platforms. Just bail out if the port
	 * number is out of bounds after mapping.
	 */
	if (port > 4) {
		drm_dbg_kms(&dev_priv->drm,
			    "[ENCODER:%d:%s] port %c (index %u) out of bounds for display power state notification\n",
			    intel_encoder->base.base.id, intel_encoder->base.name,
			    port_name(intel_encoder->port), port);
		return -EINVAL;
	}

	if (!enable)
		parm |= 4 << 8;

	switch (intel_encoder->type) {
	case INTEL_OUTPUT_ANALOG:
		type = DISPLAY_TYPE_CRT;
		break;
	case INTEL_OUTPUT_DDI:
	case INTEL_OUTPUT_DP:
	case INTEL_OUTPUT_HDMI:
	case INTEL_OUTPUT_DP_MST:
		type = DISPLAY_TYPE_EXTERNAL_FLAT_PANEL;
		break;
	case INTEL_OUTPUT_EDP:
	case INTEL_OUTPUT_DSI:
		type = DISPLAY_TYPE_INTERNAL_FLAT_PANEL;
		break;
	default:
		drm_WARN_ONCE(&dev_priv->drm, 1,
			      "unsupported intel_encoder type %d\n",
			      intel_encoder->type);
		return -EINVAL;
	}

	parm |= type << (16 + port * 3);

	return swsci(dev_priv, SWSCI_SBCB_DISPLAY_POWER_STATE, parm, NULL);
}

static const struct {
	pci_power_t pci_power_state;
	u32 parm;
} power_state_map[] = {
	{ PCI_D0,	0x00 },
	{ PCI_D1,	0x01 },
	{ PCI_D2,	0x02 },
	{ PCI_D3hot,	0x04 },
	{ PCI_D3cold,	0x04 },
};

int intel_opregion_notify_adapter(struct drm_i915_private *dev_priv,
				  pci_power_t state)
{
	int i;

	if (!HAS_DDI(dev_priv))
		return 0;

	for (i = 0; i < ARRAY_SIZE(power_state_map); i++) {
		if (state == power_state_map[i].pci_power_state)
			return swsci(dev_priv, SWSCI_SBCB_ADAPTER_POWER_STATE,
				     power_state_map[i].parm, NULL);
	}

	return -EINVAL;
}

static u32 asle_set_backlight(struct drm_i915_private *dev_priv, u32 bclp)
{
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct opregion_asle *asle = dev_priv->display.opregion.asle;
	struct drm_device *dev = &dev_priv->drm;

	drm_dbg(&dev_priv->drm, "bclp = 0x%08x\n", bclp);

	if (acpi_video_get_backlight_type() == acpi_backlight_native) {
		drm_dbg_kms(&dev_priv->drm,
			    "opregion backlight request ignored\n");
		return 0;
	}

	if (!(bclp & ASLE_BCLP_VALID))
		return ASLC_BACKLIGHT_FAILED;

	bclp &= ASLE_BCLP_MSK;
	if (bclp > 255)
		return ASLC_BACKLIGHT_FAILED;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);

	/*
	 * Update backlight on all connectors that support backlight (usually
	 * only one).
	 */
	drm_dbg_kms(&dev_priv->drm, "updating opregion backlight %d/255\n",
		    bclp);
	drm_connector_list_iter_begin(dev, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter)
		intel_backlight_set_acpi(connector->base.state, bclp, 255);
	drm_connector_list_iter_end(&conn_iter);
	asle->cblv = DIV_ROUND_UP(bclp * 100, 255) | ASLE_CBLV_VALID;

	drm_modeset_unlock(&dev->mode_config.connection_mutex);


	return 0;
}

static u32 asle_set_als_illum(struct drm_i915_private *dev_priv, u32 alsi)
{
	/* alsi is the current ALS reading in lux. 0 indicates below sensor
	   range, 0xffff indicates above sensor range. 1-0xfffe are valid */
	drm_dbg(&dev_priv->drm, "Illum is not supported\n");
	return ASLC_ALS_ILLUM_FAILED;
}

static u32 asle_set_pwm_freq(struct drm_i915_private *dev_priv, u32 pfmb)
{
	drm_dbg(&dev_priv->drm, "PWM freq is not supported\n");
	return ASLC_PWM_FREQ_FAILED;
}

static u32 asle_set_pfit(struct drm_i915_private *dev_priv, u32 pfit)
{
	/* Panel fitting is currently controlled by the X code, so this is a
	   noop until modesetting support works fully */
	drm_dbg(&dev_priv->drm, "Pfit is not supported\n");
	return ASLC_PFIT_FAILED;
}

static u32 asle_set_supported_rotation_angles(struct drm_i915_private *dev_priv, u32 srot)
{
	drm_dbg(&dev_priv->drm, "SROT is not supported\n");
	return ASLC_ROTATION_ANGLES_FAILED;
}

static u32 asle_set_button_array(struct drm_i915_private *dev_priv, u32 iuer)
{
	if (!iuer)
		drm_dbg(&dev_priv->drm,
			"Button array event is not supported (nothing)\n");
	if (iuer & ASLE_IUER_ROTATION_LOCK_BTN)
		drm_dbg(&dev_priv->drm,
			"Button array event is not supported (rotation lock)\n");
	if (iuer & ASLE_IUER_VOLUME_DOWN_BTN)
		drm_dbg(&dev_priv->drm,
			"Button array event is not supported (volume down)\n");
	if (iuer & ASLE_IUER_VOLUME_UP_BTN)
		drm_dbg(&dev_priv->drm,
			"Button array event is not supported (volume up)\n");
	if (iuer & ASLE_IUER_WINDOWS_BTN)
		drm_dbg(&dev_priv->drm,
			"Button array event is not supported (windows)\n");
	if (iuer & ASLE_IUER_POWER_BTN)
		drm_dbg(&dev_priv->drm,
			"Button array event is not supported (power)\n");

	return ASLC_BUTTON_ARRAY_FAILED;
}

static u32 asle_set_convertible(struct drm_i915_private *dev_priv, u32 iuer)
{
	if (iuer & ASLE_IUER_CONVERTIBLE)
		drm_dbg(&dev_priv->drm,
			"Convertible is not supported (clamshell)\n");
	else
		drm_dbg(&dev_priv->drm,
			"Convertible is not supported (slate)\n");

	return ASLC_CONVERTIBLE_FAILED;
}

static u32 asle_set_docking(struct drm_i915_private *dev_priv, u32 iuer)
{
	if (iuer & ASLE_IUER_DOCKING)
		drm_dbg(&dev_priv->drm, "Docking is not supported (docked)\n");
	else
		drm_dbg(&dev_priv->drm,
			"Docking is not supported (undocked)\n");

	return ASLC_DOCKING_FAILED;
}

static u32 asle_isct_state(struct drm_i915_private *dev_priv)
{
	drm_dbg(&dev_priv->drm, "ISCT is not supported\n");
	return ASLC_ISCT_STATE_FAILED;
}

static void asle_work(struct work_struct *work)
{
	struct intel_opregion *opregion =
		container_of(work, struct intel_opregion, asle_work);
	struct drm_i915_private *dev_priv =
		container_of(opregion, struct drm_i915_private, display.opregion);
	struct opregion_asle *asle = dev_priv->display.opregion.asle;
	u32 aslc_stat = 0;
	u32 aslc_req;

	if (!asle)
		return;

	aslc_req = asle->aslc;

	if (!(aslc_req & ASLC_REQ_MSK)) {
		drm_dbg(&dev_priv->drm,
			"No request on ASLC interrupt 0x%08x\n", aslc_req);
		return;
	}

	if (aslc_req & ASLC_SET_ALS_ILLUM)
		aslc_stat |= asle_set_als_illum(dev_priv, asle->alsi);

	if (aslc_req & ASLC_SET_BACKLIGHT)
		aslc_stat |= asle_set_backlight(dev_priv, asle->bclp);

	if (aslc_req & ASLC_SET_PFIT)
		aslc_stat |= asle_set_pfit(dev_priv, asle->pfit);

	if (aslc_req & ASLC_SET_PWM_FREQ)
		aslc_stat |= asle_set_pwm_freq(dev_priv, asle->pfmb);

	if (aslc_req & ASLC_SUPPORTED_ROTATION_ANGLES)
		aslc_stat |= asle_set_supported_rotation_angles(dev_priv,
							asle->srot);

	if (aslc_req & ASLC_BUTTON_ARRAY)
		aslc_stat |= asle_set_button_array(dev_priv, asle->iuer);

	if (aslc_req & ASLC_CONVERTIBLE_INDICATOR)
		aslc_stat |= asle_set_convertible(dev_priv, asle->iuer);

	if (aslc_req & ASLC_DOCKING_INDICATOR)
		aslc_stat |= asle_set_docking(dev_priv, asle->iuer);

	if (aslc_req & ASLC_ISCT_STATE_CHANGE)
		aslc_stat |= asle_isct_state(dev_priv);

	asle->aslc = aslc_stat;
}

void intel_opregion_asle_intr(struct drm_i915_private *dev_priv)
{
	if (dev_priv->display.opregion.asle)
		schedule_work(&dev_priv->display.opregion.asle_work);
}

#define ACPI_EV_DISPLAY_SWITCH (1<<0)
#define ACPI_EV_LID            (1<<1)
#define ACPI_EV_DOCK           (1<<2)

/*
 * The only video events relevant to opregion are 0x80. These indicate either a
 * docking event, lid switch or display switch request. In Linux, these are
 * handled by the dock, button and video drivers.
 */
static int intel_opregion_video_event(struct notifier_block *nb,
				      unsigned long val, void *data)
{
	struct intel_opregion *opregion = container_of(nb, struct intel_opregion,
						       acpi_notifier);
	struct acpi_bus_event *event = data;
	struct opregion_acpi *acpi;
	int ret = NOTIFY_OK;

	if (strcmp(event->device_class, ACPI_VIDEO_CLASS) != 0)
		return NOTIFY_DONE;

	acpi = opregion->acpi;

	if (event->type == 0x80 && ((acpi->cevt & 1) == 0))
		ret = NOTIFY_BAD;

	acpi->csts = 0;

	return ret;
}

/*
 * Initialise the DIDL field in opregion. This passes a list of devices to
 * the firmware. Values are defined by section B.4.2 of the ACPI specification
 * (version 3)
 */

static void set_did(struct intel_opregion *opregion, int i, u32 val)
{
	if (i < ARRAY_SIZE(opregion->acpi->didl)) {
		opregion->acpi->didl[i] = val;
	} else {
		i -= ARRAY_SIZE(opregion->acpi->didl);

		if (WARN_ON(i >= ARRAY_SIZE(opregion->acpi->did2)))
			return;

		opregion->acpi->did2[i] = val;
	}
}

static void intel_didl_outputs(struct drm_i915_private *dev_priv)
{
	struct intel_opregion *opregion = &dev_priv->display.opregion;
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;
	int i = 0, max_outputs;

	/*
	 * In theory, did2, the extended didl, gets added at opregion version
	 * 3.0. In practice, however, we're supposed to set it for earlier
	 * versions as well, since a BIOS that doesn't understand did2 should
	 * not look at it anyway. Use a variable so we can tweak this if a need
	 * arises later.
	 */
	max_outputs = ARRAY_SIZE(opregion->acpi->didl) +
		ARRAY_SIZE(opregion->acpi->did2);

	intel_acpi_device_id_update(dev_priv);

	drm_connector_list_iter_begin(&dev_priv->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		if (i < max_outputs)
			set_did(opregion, i, connector->acpi_device_id);
		i++;
	}
	drm_connector_list_iter_end(&conn_iter);

	drm_dbg_kms(&dev_priv->drm, "%d outputs detected\n", i);

	if (i > max_outputs)
		drm_err(&dev_priv->drm,
			"More than %d outputs in connector list\n",
			max_outputs);

	/* If fewer than max outputs, the list must be null terminated */
	if (i < max_outputs)
		set_did(opregion, i, 0);
}

static void intel_setup_cadls(struct drm_i915_private *dev_priv)
{
	struct intel_opregion *opregion = &dev_priv->display.opregion;
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;
	int i = 0;

	/*
	 * Initialize the CADL field from the connector device ids. This is
	 * essentially the same as copying from the DIDL. Technically, this is
	 * not always correct as display outputs may exist, but not active. This
	 * initialization is necessary for some Clevo laptops that check this
	 * field before processing the brightness and display switching hotkeys.
	 *
	 * Note that internal panels should be at the front of the connector
	 * list already, ensuring they're not left out.
	 */
	drm_connector_list_iter_begin(&dev_priv->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		if (i >= ARRAY_SIZE(opregion->acpi->cadl))
			break;
		opregion->acpi->cadl[i++] = connector->acpi_device_id;
	}
	drm_connector_list_iter_end(&conn_iter);

	/* If fewer than 8 active devices, the list must be null terminated */
	if (i < ARRAY_SIZE(opregion->acpi->cadl))
		opregion->acpi->cadl[i] = 0;
}

static void swsci_setup(struct drm_i915_private *dev_priv)
{
	struct intel_opregion *opregion = &dev_priv->display.opregion;
	bool requested_callbacks = false;
	u32 tmp;

	/* Sub-function code 0 is okay, let's allow them. */
	opregion->swsci_gbda_sub_functions = 1;
	opregion->swsci_sbcb_sub_functions = 1;

	/* We use GBDA to ask for supported GBDA calls. */
	if (swsci(dev_priv, SWSCI_GBDA_SUPPORTED_CALLS, 0, &tmp) == 0) {
		/* make the bits match the sub-function codes */
		tmp <<= 1;
		opregion->swsci_gbda_sub_functions |= tmp;
	}

	/*
	 * We also use GBDA to ask for _requested_ SBCB callbacks. The driver
	 * must not call interfaces that are not specifically requested by the
	 * bios.
	 */
	if (swsci(dev_priv, SWSCI_GBDA_REQUESTED_CALLBACKS, 0, &tmp) == 0) {
		/* here, the bits already match sub-function codes */
		opregion->swsci_sbcb_sub_functions |= tmp;
		requested_callbacks = true;
	}

	/*
	 * But we use SBCB to ask for _supported_ SBCB calls. This does not mean
	 * the callback is _requested_. But we still can't call interfaces that
	 * are not requested.
	 */
	if (swsci(dev_priv, SWSCI_SBCB_SUPPORTED_CALLBACKS, 0, &tmp) == 0) {
		/* make the bits match the sub-function codes */
		u32 low = tmp & 0x7ff;
		u32 high = tmp & ~0xfff; /* bit 11 is reserved */
		tmp = (high << 4) | (low << 1) | 1;

		/* best guess what to do with supported wrt requested */
		if (requested_callbacks) {
			u32 req = opregion->swsci_sbcb_sub_functions;
			if ((req & tmp) != req)
				drm_dbg(&dev_priv->drm,
					"SWSCI BIOS requested (%08x) SBCB callbacks that are not supported (%08x)\n",
					req, tmp);
			/* XXX: for now, trust the requested callbacks */
			/* opregion->swsci_sbcb_sub_functions &= tmp; */
		} else {
			opregion->swsci_sbcb_sub_functions |= tmp;
		}
	}

	drm_dbg(&dev_priv->drm,
		"SWSCI GBDA callbacks %08x, SBCB callbacks %08x\n",
		opregion->swsci_gbda_sub_functions,
		opregion->swsci_sbcb_sub_functions);
}

static int intel_no_opregion_vbt_callback(const struct dmi_system_id *id)
{
	DRM_DEBUG_KMS("Falling back to manually reading VBT from "
		      "VBIOS ROM for %s\n", id->ident);
	return 1;
}

static const struct dmi_system_id intel_no_opregion_vbt[] = {
	{
		.callback = intel_no_opregion_vbt_callback,
		.ident = "ThinkCentre A57",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "97027RG"),
		},
	},
	{ }
};

static int intel_load_vbt_firmware(struct drm_i915_private *dev_priv)
{
	struct intel_opregion *opregion = &dev_priv->display.opregion;
	const struct firmware *fw = NULL;
	const char *name = dev_priv->params.vbt_firmware;
	int ret;

	if (!name || !*name)
		return -ENOENT;

	ret = request_firmware(&fw, name, dev_priv->drm.dev);
	if (ret) {
		drm_err(&dev_priv->drm,
			"Requesting VBT firmware \"%s\" failed (%d)\n",
			name, ret);
		return ret;
	}

	if (intel_bios_is_valid_vbt(fw->data, fw->size)) {
		opregion->vbt_firmware = kmemdup(fw->data, fw->size, GFP_KERNEL);
		if (opregion->vbt_firmware) {
			drm_dbg_kms(&dev_priv->drm,
				    "Found valid VBT firmware \"%s\"\n", name);
			opregion->vbt = opregion->vbt_firmware;
			opregion->vbt_size = fw->size;
			ret = 0;
		} else {
			ret = -ENOMEM;
		}
	} else {
		drm_dbg_kms(&dev_priv->drm, "Invalid VBT firmware \"%s\"\n",
			    name);
		ret = -EINVAL;
	}

	release_firmware(fw);

	return ret;
}

int intel_opregion_setup(struct drm_i915_private *dev_priv)
{
	struct intel_opregion *opregion = &dev_priv->display.opregion;
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	u32 asls, mboxes;
	char buf[sizeof(OPREGION_SIGNATURE)];
	int err = 0;
	void *base;
	const void *vbt;
	u32 vbt_size;

	BUILD_BUG_ON(sizeof(struct opregion_header) != 0x100);
	BUILD_BUG_ON(sizeof(struct opregion_acpi) != 0x100);
	BUILD_BUG_ON(sizeof(struct opregion_swsci) != 0x100);
	BUILD_BUG_ON(sizeof(struct opregion_asle) != 0x100);
	BUILD_BUG_ON(sizeof(struct opregion_asle_ext) != 0x400);

	pci_read_config_dword(pdev, ASLS, &asls);
	drm_dbg(&dev_priv->drm, "graphic opregion physical addr: 0x%x\n",
		asls);
	if (asls == 0) {
		drm_dbg(&dev_priv->drm, "ACPI OpRegion not supported!\n");
		return -ENOTSUPP;
	}

	INIT_WORK(&opregion->asle_work, asle_work);

	base = memremap(asls, OPREGION_SIZE, MEMREMAP_WB);
	if (!base)
		return -ENOMEM;

	memcpy(buf, base, sizeof(buf));

	if (memcmp(buf, OPREGION_SIGNATURE, 16)) {
		drm_dbg(&dev_priv->drm, "opregion signature mismatch\n");
		err = -EINVAL;
		goto err_out;
	}
	opregion->header = base;
	opregion->lid_state = base + ACPI_CLID;

	drm_dbg(&dev_priv->drm, "ACPI OpRegion version %u.%u.%u\n",
		opregion->header->over.major,
		opregion->header->over.minor,
		opregion->header->over.revision);

	mboxes = opregion->header->mboxes;
	if (mboxes & MBOX_ACPI) {
		drm_dbg(&dev_priv->drm, "Public ACPI methods supported\n");
		opregion->acpi = base + OPREGION_ACPI_OFFSET;
		/*
		 * Indicate we handle monitor hotplug events ourselves so we do
		 * not need ACPI notifications for them. Disabling these avoids
		 * triggering the AML code doing the notifation, which may be
		 * broken as Windows also seems to disable these.
		 */
		opregion->acpi->chpd = 1;
	}

	if (mboxes & MBOX_SWSCI) {
		u8 major = opregion->header->over.major;

		if (major >= 3) {
			drm_err(&dev_priv->drm, "SWSCI Mailbox #2 present for opregion v3.x, ignoring\n");
		} else {
			if (major >= 2)
				drm_dbg(&dev_priv->drm, "SWSCI Mailbox #2 present for opregion v2.x\n");
			drm_dbg(&dev_priv->drm, "SWSCI supported\n");
			opregion->swsci = base + OPREGION_SWSCI_OFFSET;
			swsci_setup(dev_priv);
		}
	}

	if (mboxes & MBOX_ASLE) {
		drm_dbg(&dev_priv->drm, "ASLE supported\n");
		opregion->asle = base + OPREGION_ASLE_OFFSET;

		opregion->asle->ardy = ASLE_ARDY_NOT_READY;
	}

	if (mboxes & MBOX_ASLE_EXT) {
		drm_dbg(&dev_priv->drm, "ASLE extension supported\n");
		opregion->asle_ext = base + OPREGION_ASLE_EXT_OFFSET;
	}

	if (mboxes & MBOX_BACKLIGHT) {
		drm_dbg(&dev_priv->drm, "Mailbox #2 for backlight present\n");
	}

	if (intel_load_vbt_firmware(dev_priv) == 0)
		goto out;

	if (dmi_check_system(intel_no_opregion_vbt))
		goto out;

	if (opregion->header->over.major >= 2 && opregion->asle &&
	    opregion->asle->rvda && opregion->asle->rvds) {
		resource_size_t rvda = opregion->asle->rvda;

		/*
		 * opregion 2.0: rvda is the physical VBT address.
		 *
		 * opregion 2.1+: rvda is unsigned, relative offset from
		 * opregion base, and should never point within opregion.
		 */
		if (opregion->header->over.major > 2 ||
		    opregion->header->over.minor >= 1) {
			drm_WARN_ON(&dev_priv->drm, rvda < OPREGION_SIZE);

			rvda += asls;
		}

		opregion->rvda = memremap(rvda, opregion->asle->rvds,
					  MEMREMAP_WB);

		vbt = opregion->rvda;
		vbt_size = opregion->asle->rvds;
		if (intel_bios_is_valid_vbt(vbt, vbt_size)) {
			drm_dbg_kms(&dev_priv->drm,
				    "Found valid VBT in ACPI OpRegion (RVDA)\n");
			opregion->vbt = vbt;
			opregion->vbt_size = vbt_size;
			goto out;
		} else {
			drm_dbg_kms(&dev_priv->drm,
				    "Invalid VBT in ACPI OpRegion (RVDA)\n");
			memunmap(opregion->rvda);
			opregion->rvda = NULL;
		}
	}

	vbt = base + OPREGION_VBT_OFFSET;
	/*
	 * The VBT specification says that if the ASLE ext mailbox is not used
	 * its area is reserved, but on some CHT boards the VBT extends into the
	 * ASLE ext area. Allow this even though it is against the spec, so we
	 * do not end up rejecting the VBT on those boards (and end up not
	 * finding the LCD panel because of this).
	 */
	vbt_size = (mboxes & MBOX_ASLE_EXT) ?
		OPREGION_ASLE_EXT_OFFSET : OPREGION_SIZE;
	vbt_size -= OPREGION_VBT_OFFSET;
	if (intel_bios_is_valid_vbt(vbt, vbt_size)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Found valid VBT in ACPI OpRegion (Mailbox #4)\n");
		opregion->vbt = vbt;
		opregion->vbt_size = vbt_size;
	} else {
		drm_dbg_kms(&dev_priv->drm,
			    "Invalid VBT in ACPI OpRegion (Mailbox #4)\n");
	}

out:
	return 0;

err_out:
	memunmap(base);
	return err;
}

static int intel_use_opregion_panel_type_callback(const struct dmi_system_id *id)
{
	DRM_INFO("Using panel type from OpRegion on %s\n", id->ident);
	return 1;
}

static const struct dmi_system_id intel_use_opregion_panel_type[] = {
	{
		.callback = intel_use_opregion_panel_type_callback,
		.ident = "Conrac GmbH IX45GM2",
		.matches = {DMI_MATCH(DMI_SYS_VENDOR, "Conrac GmbH"),
			    DMI_MATCH(DMI_PRODUCT_NAME, "IX45GM2"),
		},
	},
	{ }
};

int
intel_opregion_get_panel_type(struct drm_i915_private *dev_priv)
{
	u32 panel_details;
	int ret;

	ret = swsci(dev_priv, SWSCI_GBDA_PANEL_DETAILS, 0x0, &panel_details);
	if (ret)
		return ret;

	ret = (panel_details >> 8) & 0xff;
	if (ret > 0x10) {
		drm_dbg_kms(&dev_priv->drm,
			    "Invalid OpRegion panel type 0x%x\n", ret);
		return -EINVAL;
	}

	/* fall back to VBT panel type? */
	if (ret == 0x0) {
		drm_dbg_kms(&dev_priv->drm, "No panel type in OpRegion\n");
		return -ENODEV;
	}

	/*
	 * So far we know that some machined must use it, others must not use it.
	 * There doesn't seem to be any way to determine which way to go, except
	 * via a quirk list :(
	 */
	if (!dmi_check_system(intel_use_opregion_panel_type)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Ignoring OpRegion panel type (%d)\n", ret - 1);
		return -ENODEV;
	}

	return ret - 1;
}

/**
 * intel_opregion_get_edid - Fetch EDID from ACPI OpRegion mailbox #5
 * @intel_connector: eDP connector
 *
 * This reads the ACPI Opregion mailbox #5 to extract the EDID that is passed
 * to it.
 *
 * Returns:
 * The EDID in the OpRegion, or NULL if there is none or it's invalid.
 *
 */
struct edid *intel_opregion_get_edid(struct intel_connector *intel_connector)
{
	struct drm_connector *connector = &intel_connector->base;
	struct drm_i915_private *i915 = to_i915(connector->dev);
	struct intel_opregion *opregion = &i915->display.opregion;
	const void *in_edid;
	const struct edid *edid;
	struct edid *new_edid;
	int len;

	if (!opregion->asle_ext)
		return NULL;

	in_edid = opregion->asle_ext->bddc;

	/* Validity corresponds to number of 128-byte blocks */
	len = (opregion->asle_ext->phed & ASLE_PHED_EDID_VALID_MASK) * 128;
	if (!len || !memchr_inv(in_edid, 0, len))
		return NULL;

	edid = in_edid;

	if (len < EDID_LENGTH * (1 + edid->extensions)) {
		drm_dbg_kms(&i915->drm, "Invalid EDID in ACPI OpRegion (Mailbox #5): too short\n");
		return NULL;
	}
	new_edid = drm_edid_duplicate(edid);
	if (!new_edid)
		return NULL;
	if (!drm_edid_is_valid(new_edid)) {
		kfree(new_edid);
		drm_dbg_kms(&i915->drm, "Invalid EDID in ACPI OpRegion (Mailbox #5)\n");
		return NULL;
	}
	return new_edid;
}

bool intel_opregion_headless_sku(struct drm_i915_private *i915)
{
	struct intel_opregion *opregion = &i915->display.opregion;
	struct opregion_header *header = opregion->header;

	if (!header || header->over.major < 2 ||
	    (header->over.major == 2 && header->over.minor < 3))
		return false;

	return opregion->header->pcon & PCON_HEADLESS_SKU;
}

void intel_opregion_register(struct drm_i915_private *i915)
{
	struct intel_opregion *opregion = &i915->display.opregion;

	if (!opregion->header)
		return;

	if (opregion->acpi) {
		opregion->acpi_notifier.notifier_call =
			intel_opregion_video_event;
		register_acpi_notifier(&opregion->acpi_notifier);
	}

	intel_opregion_resume(i915);
}

void intel_opregion_resume(struct drm_i915_private *i915)
{
	struct intel_opregion *opregion = &i915->display.opregion;

	if (!opregion->header)
		return;

	if (opregion->acpi) {
		intel_didl_outputs(i915);
		intel_setup_cadls(i915);

		/*
		 * Notify BIOS we are ready to handle ACPI video ext notifs.
		 * Right now, all the events are handled by the ACPI video
		 * module. We don't actually need to do anything with them.
		 */
		opregion->acpi->csts = 0;
		opregion->acpi->drdy = 1;
	}

	if (opregion->asle) {
		opregion->asle->tche = ASLE_TCHE_BLC_EN;
		opregion->asle->ardy = ASLE_ARDY_READY;
	}

	/* Some platforms abuse the _DSM to enable MUX */
	intel_dsm_get_bios_data_funcs_supported(i915);

	intel_opregion_notify_adapter(i915, PCI_D0);
}

void intel_opregion_suspend(struct drm_i915_private *i915, pci_power_t state)
{
	struct intel_opregion *opregion = &i915->display.opregion;

	if (!opregion->header)
		return;

	intel_opregion_notify_adapter(i915, state);

	if (opregion->asle)
		opregion->asle->ardy = ASLE_ARDY_NOT_READY;

	cancel_work_sync(&i915->display.opregion.asle_work);

	if (opregion->acpi)
		opregion->acpi->drdy = 0;
}

void intel_opregion_unregister(struct drm_i915_private *i915)
{
	struct intel_opregion *opregion = &i915->display.opregion;

	intel_opregion_suspend(i915, PCI_D1);

	if (!opregion->header)
		return;

	if (opregion->acpi_notifier.notifier_call) {
		unregister_acpi_notifier(&opregion->acpi_notifier);
		opregion->acpi_notifier.notifier_call = NULL;
	}

	/* just clear all opregion memory pointers now */
	memunmap(opregion->header);
	if (opregion->rvda) {
		memunmap(opregion->rvda);
		opregion->rvda = NULL;
	}
	if (opregion->vbt_firmware) {
		kfree(opregion->vbt_firmware);
		opregion->vbt_firmware = NULL;
	}
	opregion->header = NULL;
	opregion->acpi = NULL;
	opregion->swsci = NULL;
	opregion->asle = NULL;
	opregion->asle_ext = NULL;
	opregion->vbt = NULL;
	opregion->lid_state = NULL;
}
