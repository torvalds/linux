#ifndef _ASM_X86_DISABLED_FEATURES_H
#define _ASM_X86_DISABLED_FEATURES_H

/* These features, although they might be available in a CPU
 * will not be used because the compile options to support
 * them are not present.
 *
 * This code allows them to be checked and disabled at
 * compile time without an explicit #ifdef.  Use
 * cpu_feature_enabled().
 */

/*
 * Make sure to add features to the correct mask
 */
#define DISABLED_MASK0	0
#define DISABLED_MASK1	0
#define DISABLED_MASK2	0
#define DISABLED_MASK3	0
#define DISABLED_MASK4	0
#define DISABLED_MASK5	0
#define DISABLED_MASK6	0
#define DISABLED_MASK7	0
#define DISABLED_MASK8	0
#define DISABLED_MASK9	0

#endif /* _ASM_X86_DISABLED_FEATURES_H */
