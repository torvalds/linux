/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Interface for managing mitigations for riscv vulnerabilities.
 *
 * Copyright (C) 2024 Rivos Inc.
 */

#ifndef __ASM_BUGS_H
#define __ASM_BUGS_H

/* Watch out, ordering is important here. */
enum mitigation_state {
	UNAFFECTED,
	MITIGATED,
	VULNERABLE,
};

void ghostwrite_set_vulnerable(void);
bool ghostwrite_enable_mitigation(void);
enum mitigation_state ghostwrite_get_state(void);

#endif /* __ASM_BUGS_H */
