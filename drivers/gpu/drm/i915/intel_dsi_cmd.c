/*
 * Copyright © 2013 Intel Corporation
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
 * Author: Jani Nikula <jani.nikula@intel.com>
 */

#include <linux/export.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <video/mipi_display.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_dsi.h"
#include "intel_dsi_cmd.h"

/*
 * XXX: MIPI_DATA_ADDRESS, MIPI_DATA_LENGTH, MIPI_COMMAND_LENGTH, and
 * MIPI_COMMAND_ADDRESS registers.
 *
 * Apparently these registers provide a MIPI adapter level way to send (lots of)
 * commands and data to the receiver, without having to write the commands and
 * data to MIPI_{HS,LP}_GEN_{CTRL,DATA} registers word by word.
 *
 * Presumably for anything other than MIPI_DCS_WRITE_MEMORY_START and
 * MIPI_DCS_WRITE_MEMORY_CONTINUE (which are used to update the external
 * framebuffer in command mode displays) these are just an optimization that can
 * come later.
 *
 * For memory writes, these should probably be used for performance.
 */

static void print_stat(struct intel_dsi *intel_dsi, enum port port)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 val;

	val = I915_READ(MIPI_INTR_STAT(port));

#define STAT_BIT(val, bit) (val) & (bit) ? " " #bit : ""
	DRM_DEBUG_KMS("MIPI_INTR_STAT(%c) = %08x"
		      "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"
		      "\n", port_name(port), val,
		      STAT_BIT(val, TEARING_EFFECT),
		      STAT_BIT(val, SPL_PKT_SENT_INTERRUPT),
		      STAT_BIT(val, GEN_READ_DATA_AVAIL),
		      STAT_BIT(val, LP_GENERIC_WR_FIFO_FULL),
		      STAT_BIT(val, HS_GENERIC_WR_FIFO_FULL),
		      STAT_BIT(val, RX_PROT_VIOLATION),
		      STAT_BIT(val, RX_INVALID_TX_LENGTH),
		      STAT_BIT(val, ACK_WITH_NO_ERROR),
		      STAT_BIT(val, TURN_AROUND_ACK_TIMEOUT),
		      STAT_BIT(val, LP_RX_TIMEOUT),
		      STAT_BIT(val, HS_TX_TIMEOUT),
		      STAT_BIT(val, DPI_FIFO_UNDERRUN),
		      STAT_BIT(val, LOW_CONTENTION),
		      STAT_BIT(val, HIGH_CONTENTION),
		      STAT_BIT(val, TXDSI_VC_ID_INVALID),
		      STAT_BIT(val, TXDSI_DATA_TYPE_NOT_RECOGNISED),
		      STAT_BIT(val, TXCHECKSUM_ERROR),
		      STAT_BIT(val, TXECC_MULTIBIT_ERROR),
		      STAT_BIT(val, TXECC_SINGLE_BIT_ERROR),
		      STAT_BIT(val, TXFALSE_CONTROL_ERROR),
		      STAT_BIT(val, RXDSI_VC_ID_INVALID),
		      STAT_BIT(val, RXDSI_DATA_TYPE_NOT_REGOGNISED),
		      STAT_BIT(val, RXCHECKSUM_ERROR),
		      STAT_BIT(val, RXECC_MULTIBIT_ERROR),
		      STAT_BIT(val, RXECC_SINGLE_BIT_ERROR),
		      STAT_BIT(val, RXFALSE_CONTROL_ERROR),
		      STAT_BIT(val, RXHS_RECEIVE_TIMEOUT_ERROR),
		      STAT_BIT(val, RX_LP_TX_SYNC_ERROR),
		      STAT_BIT(val, RXEXCAPE_MODE_ENTRY_ERROR),
		      STAT_BIT(val, RXEOT_SYNC_ERROR),
		      STAT_BIT(val, RXSOT_SYNC_ERROR),
		      STAT_BIT(val, RXSOT_ERROR));
#undef STAT_BIT
}

/* enable or disable command mode hs transmissions */
void dsi_hs_mode_enable(struct intel_dsi *intel_dsi, bool enable,
						enum port port)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 temp;
	u32 mask = DBI_FIFO_EMPTY;

	if (wait_for((I915_READ(MIPI_GEN_FIFO_STAT(port)) & mask) == mask, 50))
		DRM_ERROR("Timeout waiting for DBI FIFO empty\n");

	temp = I915_READ(MIPI_HS_LP_DBI_ENABLE(port));
	temp &= DBI_HS_LP_MODE_MASK;
	I915_WRITE(MIPI_HS_LP_DBI_ENABLE(port), enable ? DBI_HS_MODE : DBI_LP_MODE);

	intel_dsi->hs = enable;
}
