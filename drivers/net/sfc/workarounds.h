/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_WORKAROUNDS_H
#define EFX_WORKAROUNDS_H

/*
 * Hardware workarounds.
 * Bug numbers are from Solarflare's Bugzilla.
 */

#define EFX_WORKAROUND_ALWAYS(efx) 1
#define EFX_WORKAROUND_FALCON_A(efx) (falcon_rev(efx) <= FALCON_REV_A1)
#define EFX_WORKAROUND_10G(efx) EFX_IS10G(efx)
#define EFX_WORKAROUND_SFT9001(efx) ((efx)->phy_type == PHY_TYPE_SFT9001A || \
				     (efx)->phy_type == PHY_TYPE_SFT9001B)

/* XAUI resets if link not detected */
#define EFX_WORKAROUND_5147 EFX_WORKAROUND_ALWAYS
/* RX PCIe double split performance issue */
#define EFX_WORKAROUND_7575 EFX_WORKAROUND_ALWAYS
/* Bit-bashed I2C reads cause performance drop */
#define EFX_WORKAROUND_7884 EFX_WORKAROUND_10G
/* TX pkt parser problem with <= 16 byte TXes */
#define EFX_WORKAROUND_9141 EFX_WORKAROUND_ALWAYS
/* TX_EV_PKT_ERR can be caused by a dangling TX descriptor
 * or a PCIe error (bug 11028) */
#define EFX_WORKAROUND_10727 EFX_WORKAROUND_ALWAYS
/* Transmit flow control may get disabled */
#define EFX_WORKAROUND_11482 EFX_WORKAROUND_ALWAYS
/* Flush events can take a very long time to appear */
#define EFX_WORKAROUND_11557 EFX_WORKAROUND_ALWAYS
/* Truncated IPv4 packets can confuse the TX packet parser */
#define EFX_WORKAROUND_15592 EFX_WORKAROUND_ALWAYS

/* Spurious parity errors in TSORT buffers */
#define EFX_WORKAROUND_5129 EFX_WORKAROUND_FALCON_A
/* iSCSI parsing errors */
#define EFX_WORKAROUND_5583 EFX_WORKAROUND_FALCON_A
/* RX events go missing */
#define EFX_WORKAROUND_5676 EFX_WORKAROUND_FALCON_A
/* RX_RESET on A1 */
#define EFX_WORKAROUND_6555 EFX_WORKAROUND_FALCON_A
/* Increase filter depth to avoid RX_RESET */
#define EFX_WORKAROUND_7244 EFX_WORKAROUND_FALCON_A
/* Flushes may never complete */
#define EFX_WORKAROUND_7803 EFX_WORKAROUND_FALCON_A
/* Leak overlength packets rather than free */
#define EFX_WORKAROUND_8071 EFX_WORKAROUND_FALCON_A

/* Need to send XNP pages for 100BaseT */
#define EFX_WORKAROUND_13204 EFX_WORKAROUND_SFT9001
/* Don't restart AN in near-side loopback */
#define EFX_WORKAROUND_15195 EFX_WORKAROUND_SFT9001

#endif /* EFX_WORKAROUNDS_H */
