/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_SPECTRE_H
#define __ASM_SPECTRE_H

enum {
	SPECTRE_UNAFFECTED,
	SPECTRE_MITIGATED,
	SPECTRE_VULNERABLE,
};

enum {
	__SPECTRE_V2_METHOD_BPIALL,
	__SPECTRE_V2_METHOD_ICIALLU,
	__SPECTRE_V2_METHOD_SMC,
	__SPECTRE_V2_METHOD_HVC,
	__SPECTRE_V2_METHOD_LOOP8,
};

enum {
	SPECTRE_V2_METHOD_BPIALL = BIT(__SPECTRE_V2_METHOD_BPIALL),
	SPECTRE_V2_METHOD_ICIALLU = BIT(__SPECTRE_V2_METHOD_ICIALLU),
	SPECTRE_V2_METHOD_SMC = BIT(__SPECTRE_V2_METHOD_SMC),
	SPECTRE_V2_METHOD_HVC = BIT(__SPECTRE_V2_METHOD_HVC),
	SPECTRE_V2_METHOD_LOOP8 = BIT(__SPECTRE_V2_METHOD_LOOP8),
};

#ifdef CONFIG_GENERIC_CPU_VULNERABILITIES
void spectre_v2_update_state(unsigned int state, unsigned int methods);
#else
static inline void spectre_v2_update_state(unsigned int state,
					   unsigned int methods)
{}
#endif

int spectre_bhb_update_vectors(unsigned int method);

void cpu_v7_ca8_ibe(void);
void cpu_v7_ca15_ibe(void);
void cpu_v7_bugs_init(void);

#endif
