#ifndef _ASM_C6X_TLB_H
#define _ASM_C6X_TLB_H

#define tlb_flush(tlb) flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>

#endif /* _ASM_C6X_TLB_H */
