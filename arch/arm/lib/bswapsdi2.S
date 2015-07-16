#include <linux/linkage.h>
#include <asm/assembler.h>

#if __LINUX_ARM_ARCH__ >= 6
ENTRY(__bswapsi2)
	rev r0, r0
	bx lr
ENDPROC(__bswapsi2)

ENTRY(__bswapdi2)
	rev r3, r0
	rev r0, r1
	mov r1, r3
	bx lr
ENDPROC(__bswapdi2)
#else
ENTRY(__bswapsi2)
	eor r3, r0, r0, ror #16
	mov r3, r3, lsr #8
	bic r3, r3, #0xff00
	eor r0, r3, r0, ror #8
	ret lr
ENDPROC(__bswapsi2)

ENTRY(__bswapdi2)
	mov ip, r1
	eor r3, ip, ip, ror #16
	eor r1, r0, r0, ror #16
	mov r1, r1, lsr #8
	mov r3, r3, lsr #8
	bic r3, r3, #0xff00
	bic r1, r1, #0xff00
	eor r1, r1, r0, ror #8
	eor r0, r3, ip, ror #8
	ret lr
ENDPROC(__bswapdi2)
#endif
