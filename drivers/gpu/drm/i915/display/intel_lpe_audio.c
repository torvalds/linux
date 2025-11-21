/*
 * Copyright © 2016 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
 *    Jerome Anand <jerome.anand@intel.com>
 *    based on VED patches
 *
 */

/**
 * DOC: LPE Audio integration for HDMI or DP playback
 *
 * Motivation:
 * Atom platforms (e.g. valleyview and cherryTrail) integrates a DMA-based
 * interface as an alternative to the traditional HDaudio path. While this
 * mode is unrelated to the LPE aka SST audio engine, the documentation refers
 * to this mode as LPE so we keep this notation for the sake of consistency.
 *
 * The interface is handled by a separate standalone driver maintained in the
 * ALSA subsystem for simplicity. To minimize the interaction between the two
 * subsystems, a bridge is setup between the hdmi-lpe-audio and i915:
 * 1. Create a platform device to share MMIO/IRQ resources
 * 2. Make the platform device child of i915 device for runtime PM.
 * 3. Create IRQ chip to forward the LPE audio irqs.
 * the hdmi-lpe-audio driver probes the lpe audio device and creates a new
 * sound card
 *
 * Threats:
 * Due to the restriction in Linux platform device model, user need manually
 * uninstall the hdmi-lpe-audio driver before uninstalling i915 module,
 * otherwise we might run into use-after-free issues after i915 removes the
 * platform device: even though hdmi-lpe-audio driver is released, the modules
 * is still in "installed" status.
 *
 * Implementation:
 * The MMIO/REG platform resources are created according to the registers
 * specification.
 * When forwarding LPE audio irqs, the flow control handler selection depends
 * on the platform, for example on valleyview handle_simple_irq is enough.
 *
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_print.h>
#include <drm/intel/intel_lpe_audio.h>

#include "i915_irq.h"
#include "intel_audio_regs.h"
#include "intel_de.h"
#include "intel_lpe_audio.h"
#include "intel_pci_config.h"

#define HAS_LPE_AUDIO(display) ((display)->audio.lpe.platdev)

static struct platform_device *
lpe_audio_platdev_create(struct intel_display *display)
{
	struct pci_dev *pdev = to_pci_dev(display->drm->dev);
	struct platform_device_info pinfo = {};
	struct resource *rsc;
	struct platform_device *platdev;
	struct intel_hdmi_lpe_audio_pdata *pdata;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	rsc = kcalloc(2, sizeof(*rsc), GFP_KERNEL);
	if (!rsc) {
		kfree(pdata);
		return ERR_PTR(-ENOMEM);
	}

	rsc[0].start    = display->audio.lpe.irq;
	rsc[0].end      = display->audio.lpe.irq;
	rsc[0].flags    = IORESOURCE_IRQ;
	rsc[0].name     = "hdmi-lpe-audio-irq";

	rsc[1].start    = pci_resource_start(pdev, GEN4_GTTMMADR_BAR) +
		I915_HDMI_LPE_AUDIO_BASE;
	rsc[1].end      = pci_resource_start(pdev, GEN4_GTTMMADR_BAR) +
		I915_HDMI_LPE_AUDIO_BASE + I915_HDMI_LPE_AUDIO_SIZE - 1;
	rsc[1].flags    = IORESOURCE_MEM;
	rsc[1].name     = "hdmi-lpe-audio-mmio";

	pinfo.parent = display->drm->dev;
	pinfo.name = "hdmi-lpe-audio";
	pinfo.id = -1;
	pinfo.res = rsc;
	pinfo.num_res = 2;
	pinfo.data = pdata;
	pinfo.size_data = sizeof(*pdata);
	pinfo.dma_mask = DMA_BIT_MASK(32);

	pdata->num_pipes = INTEL_NUM_PIPES(display);
	pdata->num_ports = display->platform.cherryview ? 3 : 2; /* B,C,D or B,C */
	pdata->port[0].pipe = -1;
	pdata->port[1].pipe = -1;
	pdata->port[2].pipe = -1;
	spin_lock_init(&pdata->lpe_audio_slock);

	platdev = platform_device_register_full(&pinfo);
	kfree(rsc);
	kfree(pdata);

	if (IS_ERR(platdev)) {
		drm_err(display->drm,
			"Failed to allocate LPE audio platform device\n");
		return platdev;
	}

	pm_runtime_no_callbacks(&platdev->dev);

	return platdev;
}

static void lpe_audio_platdev_destroy(struct intel_display *display)
{
	/* XXX Note that platform_device_register_full() allocates a dma_mask
	 * and never frees it. We can't free it here as we cannot guarantee
	 * this is the last reference (i.e. that the dma_mask will not be
	 * used after our unregister). So ee choose to leak the sizeof(u64)
	 * allocation here - it should be fixed in the platform_device rather
	 * than us fiddle with its internals.
	 */

	platform_device_unregister(display->audio.lpe.platdev);
}

static void lpe_audio_irq_unmask(struct irq_data *d)
{
}

static void lpe_audio_irq_mask(struct irq_data *d)
{
}

static struct irq_chip lpe_audio_irqchip = {
	.name = "hdmi_lpe_audio_irqchip",
	.irq_mask = lpe_audio_irq_mask,
	.irq_unmask = lpe_audio_irq_unmask,
};

static int lpe_audio_irq_init(struct intel_display *display)
{
	int irq = display->audio.lpe.irq;

	irq_set_chip_and_handler_name(irq, &lpe_audio_irqchip,
				      handle_simple_irq,
				      "hdmi_lpe_audio_irq_handler");

	return 0;
}

static bool lpe_audio_detect(struct intel_display *display)
{
	int lpe_present = false;

	if (display->platform.valleyview || display->platform.cherryview) {
		static const struct pci_device_id atom_hdaudio_ids[] = {
			/* Baytrail */
			{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0f04)},
			/* Braswell */
			{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2284)},
			{}
		};

		if (!pci_dev_present(atom_hdaudio_ids)) {
			drm_info(display->drm,
				 "HDaudio controller not detected, using LPE audio instead\n");
			lpe_present = true;
		}
	}
	return lpe_present;
}

static int lpe_audio_setup(struct intel_display *display)
{
	int ret;

	display->audio.lpe.irq = irq_alloc_desc(0);
	if (display->audio.lpe.irq < 0) {
		drm_err(display->drm, "Failed to allocate IRQ desc: %d\n",
			display->audio.lpe.irq);
		ret = display->audio.lpe.irq;
		goto err;
	}

	drm_dbg(display->drm, "irq = %d\n", display->audio.lpe.irq);

	ret = lpe_audio_irq_init(display);

	if (ret) {
		drm_err(display->drm,
			"Failed to initialize irqchip for lpe audio: %d\n",
			ret);
		goto err_free_irq;
	}

	display->audio.lpe.platdev = lpe_audio_platdev_create(display);

	if (IS_ERR(display->audio.lpe.platdev)) {
		ret = PTR_ERR(display->audio.lpe.platdev);
		drm_err(display->drm,
			"Failed to create lpe audio platform device: %d\n",
			ret);
		goto err_free_irq;
	}

	/* enable chicken bit; at least this is required for Dell Wyse 3040
	 * with DP outputs (but only sometimes by some reason!)
	 */
	intel_de_write(display, VLV_AUD_CHICKEN_BIT_REG,
		       VLV_CHICKEN_BIT_DBG_ENABLE);

	return 0;
err_free_irq:
	irq_free_desc(display->audio.lpe.irq);
err:
	display->audio.lpe.irq = -1;
	display->audio.lpe.platdev = NULL;
	return ret;
}

/**
 * intel_lpe_audio_irq_handler() - forwards the LPE audio irq
 * @display: display device
 *
 * the LPE Audio irq is forwarded to the irq handler registered by LPE audio
 * driver.
 */
void intel_lpe_audio_irq_handler(struct intel_display *display)
{
	int ret;

	if (!HAS_LPE_AUDIO(display))
		return;

	ret = generic_handle_irq(display->audio.lpe.irq);
	if (ret)
		drm_err_ratelimited(display->drm,
				    "error handling LPE audio irq: %d\n", ret);
}

/**
 * intel_lpe_audio_init() - detect and setup the bridge between HDMI LPE Audio
 * driver and i915
 * @display: display device
 *
 * Return: 0 if successful. non-zero if detection or
 * llocation/initialization fails
 */
int intel_lpe_audio_init(struct intel_display *display)
{
	int ret = -ENODEV;

	if (lpe_audio_detect(display)) {
		ret = lpe_audio_setup(display);
		if (ret < 0)
			drm_err(display->drm,
				"failed to setup LPE Audio bridge\n");
	}
	return ret;
}

/**
 * intel_lpe_audio_teardown() - destroy the bridge between HDMI LPE
 * audio driver and i915
 * @display: display device
 *
 * release all the resources for LPE audio <-> i915 bridge.
 */
void intel_lpe_audio_teardown(struct intel_display *display)
{
	if (!HAS_LPE_AUDIO(display))
		return;

	lpe_audio_platdev_destroy(display);

	irq_free_desc(display->audio.lpe.irq);

	display->audio.lpe.irq = -1;
	display->audio.lpe.platdev = NULL;
}

/**
 * intel_lpe_audio_notify() - notify lpe audio event
 * audio driver and i915
 * @display: display device
 * @cpu_transcoder: CPU transcoder
 * @port: port
 * @eld : ELD data
 * @ls_clock: Link symbol clock in kHz
 * @dp_output: Driving a DP output?
 *
 * Notify lpe audio driver of eld change.
 */
void intel_lpe_audio_notify(struct intel_display *display,
			    enum transcoder cpu_transcoder, enum port port,
			    const void *eld, int ls_clock, bool dp_output)
{
	unsigned long irqflags;
	struct intel_hdmi_lpe_audio_pdata *pdata;
	struct intel_hdmi_lpe_audio_port_pdata *ppdata;
	u32 audio_enable;

	if (!HAS_LPE_AUDIO(display))
		return;

	pdata = dev_get_platdata(&display->audio.lpe.platdev->dev);
	ppdata = &pdata->port[port - PORT_B];

	spin_lock_irqsave(&pdata->lpe_audio_slock, irqflags);

	audio_enable = intel_de_read(display, VLV_AUD_PORT_EN_DBG(port));

	if (eld != NULL) {
		memcpy(ppdata->eld, eld, HDMI_MAX_ELD_BYTES);
		ppdata->pipe = cpu_transcoder;
		ppdata->ls_clock = ls_clock;
		ppdata->dp_output = dp_output;

		/* Unmute the amp for both DP and HDMI */
		intel_de_write(display, VLV_AUD_PORT_EN_DBG(port),
			       audio_enable & ~VLV_AMP_MUTE);
	} else {
		memset(ppdata->eld, 0, HDMI_MAX_ELD_BYTES);
		ppdata->pipe = -1;
		ppdata->ls_clock = 0;
		ppdata->dp_output = false;

		/* Mute the amp for both DP and HDMI */
		intel_de_write(display, VLV_AUD_PORT_EN_DBG(port),
			       audio_enable | VLV_AMP_MUTE);
	}

	if (pdata->notify_audio_lpe)
		pdata->notify_audio_lpe(display->audio.lpe.platdev, port - PORT_B);

	spin_unlock_irqrestore(&pdata->lpe_audio_slock, irqflags);
}
