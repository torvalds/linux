#ifndef __ASM_MEMCPY_UTILS
#define __ASM_MEMCPY_UTILS

#include <linux/linkage.h>
#include <asm/asi.h>
#include <asm/visasm.h>

ENTRY(__restore_asi_fp)
	VISExitHalf
	retl
	 wr	%g0, ASI_AIUS, %asi
ENDPROC(__restore_asi_fp)

ENTRY(__restore_asi)
	retl
	 wr	%g0, ASI_AIUS, %asi
ENDPROC(__restore_asi)

ENTRY(memcpy_retl_o2)
	ba,pt	%xcc, __restore_asi
	 mov	%o2, %o0
ENDPROC(memcpy_retl_o2)
ENTRY(memcpy_retl_o2_plus_1)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 1, %o0
ENDPROC(memcpy_retl_o2_plus_1)
ENTRY(memcpy_retl_o2_plus_3)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 3, %o0
ENDPROC(memcpy_retl_o2_plus_3)
ENTRY(memcpy_retl_o2_plus_4)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 4, %o0
ENDPROC(memcpy_retl_o2_plus_4)
ENTRY(memcpy_retl_o2_plus_5)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 5, %o0
ENDPROC(memcpy_retl_o2_plus_5)
ENTRY(memcpy_retl_o2_plus_6)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 6, %o0
ENDPROC(memcpy_retl_o2_plus_6)
ENTRY(memcpy_retl_o2_plus_7)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 7, %o0
ENDPROC(memcpy_retl_o2_plus_7)
ENTRY(memcpy_retl_o2_plus_8)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 8, %o0
ENDPROC(memcpy_retl_o2_plus_8)
ENTRY(memcpy_retl_o2_plus_15)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 15, %o0
ENDPROC(memcpy_retl_o2_plus_15)
ENTRY(memcpy_retl_o2_plus_15_8)
	 add	%o2, 15, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 8, %o0
ENDPROC(memcpy_retl_o2_plus_15_8)
ENTRY(memcpy_retl_o2_plus_16)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 16, %o0
ENDPROC(memcpy_retl_o2_plus_16)
ENTRY(memcpy_retl_o2_plus_24)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 24, %o0
ENDPROC(memcpy_retl_o2_plus_24)
ENTRY(memcpy_retl_o2_plus_31)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 31, %o0
ENDPROC(memcpy_retl_o2_plus_31)
ENTRY(memcpy_retl_o2_plus_32)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 32, %o0
ENDPROC(memcpy_retl_o2_plus_32)
ENTRY(memcpy_retl_o2_plus_31_32)
	add	%o2, 31, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 32, %o0
ENDPROC(memcpy_retl_o2_plus_31_32)
ENTRY(memcpy_retl_o2_plus_31_24)
	add	%o2, 31, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 24, %o0
ENDPROC(memcpy_retl_o2_plus_31_24)
ENTRY(memcpy_retl_o2_plus_31_16)
	add	%o2, 31, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 16, %o0
ENDPROC(memcpy_retl_o2_plus_31_16)
ENTRY(memcpy_retl_o2_plus_31_8)
	add	%o2, 31, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 8, %o0
ENDPROC(memcpy_retl_o2_plus_31_8)
ENTRY(memcpy_retl_o2_plus_63)
	ba,pt	%xcc, __restore_asi
	 add	%o2, 63, %o0
ENDPROC(memcpy_retl_o2_plus_63)
ENTRY(memcpy_retl_o2_plus_63_64)
	 add	%o2, 63, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 64, %o0
ENDPROC(memcpy_retl_o2_plus_63_64)
ENTRY(memcpy_retl_o2_plus_63_56)
	 add	%o2, 63, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 56, %o0
ENDPROC(memcpy_retl_o2_plus_63_56)
ENTRY(memcpy_retl_o2_plus_63_48)
	 add	%o2, 63, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 48, %o0
ENDPROC(memcpy_retl_o2_plus_63_48)
ENTRY(memcpy_retl_o2_plus_63_40)
	 add	%o2, 63, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 40, %o0
ENDPROC(memcpy_retl_o2_plus_63_40)
ENTRY(memcpy_retl_o2_plus_63_32)
	 add	%o2, 63, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 32, %o0
ENDPROC(memcpy_retl_o2_plus_63_32)
ENTRY(memcpy_retl_o2_plus_63_24)
	 add	%o2, 63, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 24, %o0
ENDPROC(memcpy_retl_o2_plus_63_24)
ENTRY(memcpy_retl_o2_plus_63_16)
	 add	%o2, 63, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 16, %o0
ENDPROC(memcpy_retl_o2_plus_63_16)
ENTRY(memcpy_retl_o2_plus_63_8)
	 add	%o2, 63, %o2
	ba,pt	%xcc, __restore_asi
	 add	%o2, 8, %o0
ENDPROC(memcpy_retl_o2_plus_63_8)
ENTRY(memcpy_retl_o2_plus_o5)
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5)
ENTRY(memcpy_retl_o2_plus_o5_plus_1)
	add	%o5, 1, %o5
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_1)
ENTRY(memcpy_retl_o2_plus_o5_plus_4)
	add	%o5, 4, %o5
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_4)
ENTRY(memcpy_retl_o2_plus_o5_plus_8)
	add	%o5, 8, %o5
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_8)
ENTRY(memcpy_retl_o2_plus_o5_plus_16)
	add	%o5, 16, %o5
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_16)
ENTRY(memcpy_retl_o2_plus_o5_plus_24)
	add	%o5, 24, %o5
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_24)
ENTRY(memcpy_retl_o2_plus_o5_plus_32)
	add	%o5, 32, %o5
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_32)
ENTRY(memcpy_retl_o2_plus_o5_64)
	add	%o5, 32, %o5
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_64)
ENTRY(memcpy_retl_o2_plus_g1)
	ba,pt	%xcc, __restore_asi
	 add	%o2, %g1, %o0
ENDPROC(memcpy_retl_o2_plus_g1)
ENTRY(memcpy_retl_o2_plus_g1_plus_1)
	add	%g1, 1, %g1
	ba,pt	%xcc, __restore_asi
	 add	%o2, %g1, %o0
ENDPROC(memcpy_retl_o2_plus_g1_plus_1)
ENTRY(memcpy_retl_o2_plus_g1_plus_8)
	add	%g1, 8, %g1
	ba,pt	%xcc, __restore_asi
	 add	%o2, %g1, %o0
ENDPROC(memcpy_retl_o2_plus_g1_plus_8)
ENTRY(memcpy_retl_o2_plus_o4)
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4)
ENTRY(memcpy_retl_o2_plus_o4_plus_8)
	add	%o4, 8, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_8)
ENTRY(memcpy_retl_o2_plus_o4_plus_16)
	add	%o4, 16, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_16)
ENTRY(memcpy_retl_o2_plus_o4_plus_24)
	add	%o4, 24, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_24)
ENTRY(memcpy_retl_o2_plus_o4_plus_32)
	add	%o4, 32, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_32)
ENTRY(memcpy_retl_o2_plus_o4_plus_40)
	add	%o4, 40, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_40)
ENTRY(memcpy_retl_o2_plus_o4_plus_48)
	add	%o4, 48, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_48)
ENTRY(memcpy_retl_o2_plus_o4_plus_56)
	add	%o4, 56, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_56)
ENTRY(memcpy_retl_o2_plus_o4_plus_64)
	add	%o4, 64, %o4
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_64)
ENTRY(memcpy_retl_o2_plus_o5_plus_64)
	add	%o5, 64, %o5
	ba,pt	%xcc, __restore_asi
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_64)
ENTRY(memcpy_retl_o2_plus_o3_fp)
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o3, %o0
ENDPROC(memcpy_retl_o2_plus_o3_fp)
ENTRY(memcpy_retl_o2_plus_o3_plus_1_fp)
	add	%o3, 1, %o3
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o3, %o0
ENDPROC(memcpy_retl_o2_plus_o3_plus_1_fp)
ENTRY(memcpy_retl_o2_plus_o3_plus_4_fp)
	add	%o3, 4, %o3
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o3, %o0
ENDPROC(memcpy_retl_o2_plus_o3_plus_4_fp)
ENTRY(memcpy_retl_o2_plus_o4_fp)
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_fp)
ENTRY(memcpy_retl_o2_plus_o4_plus_8_fp)
	add	%o4, 8, %o4
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_8_fp)
ENTRY(memcpy_retl_o2_plus_o4_plus_16_fp)
	add	%o4, 16, %o4
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_16_fp)
ENTRY(memcpy_retl_o2_plus_o4_plus_24_fp)
	add	%o4, 24, %o4
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_24_fp)
ENTRY(memcpy_retl_o2_plus_o4_plus_32_fp)
	add	%o4, 32, %o4
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_32_fp)
ENTRY(memcpy_retl_o2_plus_o4_plus_40_fp)
	add	%o4, 40, %o4
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_40_fp)
ENTRY(memcpy_retl_o2_plus_o4_plus_48_fp)
	add	%o4, 48, %o4
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_48_fp)
ENTRY(memcpy_retl_o2_plus_o4_plus_56_fp)
	add	%o4, 56, %o4
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_56_fp)
ENTRY(memcpy_retl_o2_plus_o4_plus_64_fp)
	add	%o4, 64, %o4
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o4, %o0
ENDPROC(memcpy_retl_o2_plus_o4_plus_64_fp)
ENTRY(memcpy_retl_o2_plus_o5_fp)
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_fp)
ENTRY(memcpy_retl_o2_plus_o5_plus_64_fp)
	add	%o5, 64, %o5
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_64_fp)
ENTRY(memcpy_retl_o2_plus_o5_plus_56_fp)
	add	%o5, 56, %o5
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_56_fp)
ENTRY(memcpy_retl_o2_plus_o5_plus_48_fp)
	add	%o5, 48, %o5
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_48_fp)
ENTRY(memcpy_retl_o2_plus_o5_plus_40_fp)
	add	%o5, 40, %o5
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_40_fp)
ENTRY(memcpy_retl_o2_plus_o5_plus_32_fp)
	add	%o5, 32, %o5
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_32_fp)
ENTRY(memcpy_retl_o2_plus_o5_plus_24_fp)
	add	%o5, 24, %o5
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_24_fp)
ENTRY(memcpy_retl_o2_plus_o5_plus_16_fp)
	add	%o5, 16, %o5
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_16_fp)
ENTRY(memcpy_retl_o2_plus_o5_plus_8_fp)
	add	%o5, 8, %o5
	ba,pt	%xcc, __restore_asi_fp
	 add	%o2, %o5, %o0
ENDPROC(memcpy_retl_o2_plus_o5_plus_8_fp)

#endif
