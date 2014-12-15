/*
 * dwc_modpow.h
 * See dwc_modpow.c for license and changes
 */
#ifndef _DWC_MODPOW_H
#define _DWC_MODPOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "dwc_os.h"

/** @file
 *
 * This file defines the module exponentiation function which is only used
 * internally by the DWC UWB modules for calculation of PKs during numeric
 * association.  The routine is taken from the PUTTY, an open source terminal
 * emulator.  The PUTTY License is preserved in the dwc_modpow.c file.
 *
 */

typedef uint32_t BignumInt;
typedef uint64_t BignumDblInt;
typedef BignumInt *Bignum;

/* Compute modular exponentiaion */
extern Bignum dwc_modpow(void *mem_ctx, Bignum base_in, Bignum exp, Bignum mod);

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_BIGNUM_H */
