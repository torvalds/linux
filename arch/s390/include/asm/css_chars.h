#ifndef _ASM_CSS_CHARS_H
#define _ASM_CSS_CHARS_H

#include <linux/types.h>

#ifdef __KERNEL__

struct css_general_char {
	u64 : 12;
	u32 dynio : 1;	 /* bit 12 */
	u32 : 4;
	u32 eadm : 1;	 /* bit 17 */
	u32 : 23;
	u32 aif : 1;	 /* bit 41 */
	u32 : 3;
	u32 mcss : 1;	 /* bit 45 */
	u32 fcs : 1;	 /* bit 46 */
	u32 : 1;
	u32 ext_mb : 1;  /* bit 48 */
	u32 : 7;
	u32 aif_tdd : 1; /* bit 56 */
	u32 : 1;
	u32 qebsm : 1;	 /* bit 58 */
	u32 : 8;
	u32 aif_osa : 1; /* bit 67 */
	u32 : 12;
	u32 eadm_rf : 1; /* bit 80 */
	u32 : 1;
	u32 cib : 1;	 /* bit 82 */
	u32 : 5;
	u32 fcx : 1;	 /* bit 88 */
	u32 : 19;
	u32 alt_ssi : 1; /* bit 108 */
} __packed;

extern struct css_general_char css_general_characteristics;

#endif /* __KERNEL__ */
#endif
