/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_HFI_H
#define _INTEL_HFI_H

#if defined(CONFIG_INTEL_HFI_THERMAL)
void __init intel_hfi_init(void);
#else
static inline void intel_hfi_init(void) { }
#endif /* CONFIG_INTEL_HFI_THERMAL */

#endif /* _INTEL_HFI_H */
