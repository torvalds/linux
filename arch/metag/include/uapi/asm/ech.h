/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_METAG_ECH_H
#define _UAPI_METAG_ECH_H

/*
 * These bits can be set in the top half of the D0.8 register when DSP context
 * switching is enabled, in order to support partial DSP context save/restore.
 */

#define TBICTX_XEXT_BIT	0x1000	/* Enable extended context save */
#define TBICTX_XTDP_BIT	0x0800	/* DSP accumulators/RAM/templates */
#define TBICTX_XHL2_BIT	0x0400	/* Hardware loops */
#define TBICTX_XAXX_BIT	0x0200	/* Extended AX registers (A*.4-7) */
#define TBICTX_XDX8_BIT	0x0100	/* Extended DX registers (D*.8-15) */

#endif /* _UAPI_METAG_ECH_H */
