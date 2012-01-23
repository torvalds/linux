#define     CLINE       5

		.type	_eLIBs_CleanFlushDCacheRegion,#function
		.globl _eLIBs_CleanFlushDCacheRegion
_eLIBs_CleanFlushDCacheRegion:
            bic     r0, r0, #(1 << CLINE) - 1         @//clip 2 cline adr
            mov     r1, r1, lsr #CLINE                @//bytes to cline
10:
            mcr p15, 0, r0, c7, c14, 1         @//清除清理d-cline@adr

            add     r0, r0, #(1 << CLINE)             @//+1 next line adr
            subs    r1, r1, #1                          @//-1 cline counter
            bne     10b
11:
            mov     pc, lr
