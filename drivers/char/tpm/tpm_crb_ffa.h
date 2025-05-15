/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Arm Ltd.
 *
 * This device driver implements the TPM CRB start method
 * as defined in the TPM Service Command Response Buffer
 * Interface Over FF-A (DEN0138).
 */
#ifndef _TPM_CRB_FFA_H
#define _TPM_CRB_FFA_H

#if IS_REACHABLE(CONFIG_TCG_ARM_CRB_FFA)
int tpm_crb_ffa_init(void);
int tpm_crb_ffa_get_interface_version(u16 *major, u16 *minor);
int tpm_crb_ffa_start(int request_type, int locality);
#else
static inline int tpm_crb_ffa_init(void) { return 0; }
static inline int tpm_crb_ffa_get_interface_version(u16 *major, u16 *minor) { return 0; }
static inline int tpm_crb_ffa_start(int request_type, int locality) { return 0; }
#endif

#define CRB_FFA_START_TYPE_COMMAND 0
#define CRB_FFA_START_TYPE_LOCALITY_REQUEST 1

#endif
