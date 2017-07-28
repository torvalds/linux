/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2006-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EF4_WORKAROUNDS_H
#define EF4_WORKAROUNDS_H

/*
 * Hardware workarounds.
 * Bug numbers are from Solarflare's Bugzilla.
 */

#define EF4_WORKAROUND_FALCON_A(efx) (ef4_nic_rev(efx) <= EF4_REV_FALCON_A1)
#define EF4_WORKAROUND_FALCON_AB(efx) (ef4_nic_rev(efx) <= EF4_REV_FALCON_B0)
#define EF4_WORKAROUND_10G(efx) 1

/* Bit-bashed I2C reads cause performance drop */
#define EF4_WORKAROUND_7884 EF4_WORKAROUND_10G
/* Truncated IPv4 packets can confuse the TX packet parser */
#define EF4_WORKAROUND_15592 EF4_WORKAROUND_FALCON_AB

/* Spurious parity errors in TSORT buffers */
#define EF4_WORKAROUND_5129 EF4_WORKAROUND_FALCON_A
/* Unaligned read request >512 bytes after aligning may break TSORT */
#define EF4_WORKAROUND_5391 EF4_WORKAROUND_FALCON_A
/* iSCSI parsing errors */
#define EF4_WORKAROUND_5583 EF4_WORKAROUND_FALCON_A
/* RX events go missing */
#define EF4_WORKAROUND_5676 EF4_WORKAROUND_FALCON_A
/* RX_RESET on A1 */
#define EF4_WORKAROUND_6555 EF4_WORKAROUND_FALCON_A
/* Increase filter depth to avoid RX_RESET */
#define EF4_WORKAROUND_7244 EF4_WORKAROUND_FALCON_A
/* Flushes may never complete */
#define EF4_WORKAROUND_7803 EF4_WORKAROUND_FALCON_AB
/* Leak overlength packets rather than free */
#define EF4_WORKAROUND_8071 EF4_WORKAROUND_FALCON_A

#endif /* EF4_WORKAROUNDS_H */
