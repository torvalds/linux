#include <asm/ppc_asm.h>

/* unsigned long xmon_mfspr(sprn, default_value) */
_GLOBAL(xmon_mfspr)
	ld	r5, .Lmfspr_table@got(r2)
	b	xmon_mxspr

/* void xmon_mtspr(sprn, new_value) */
_GLOBAL(xmon_mtspr)
	ld	r5, .Lmtspr_table@got(r2)
	b	xmon_mxspr

/*
 * r3 = sprn
 * r4 = default or new value
 * r5 = table base
 */
xmon_mxspr:
	/*
	 * To index into the table of mxsprs we need:
	 *  i = (sprn & 0x3ff) * 8
	 * or using rwlinm:
	 *  i = (sprn << 3) & (0x3ff << 3)
	 */
	rlwinm	r3, r3, 3, 0x3ff << 3
	add	r5, r5, r3
	mtctr	r5
	mr	r3, r4 /* put default_value in r3 for mfspr */
	bctr

.Lmfspr_table:
	spr = 0
	.rept	1024
	mfspr	r3, spr
	blr
	spr = spr + 1
	.endr

.Lmtspr_table:
	spr = 0
	.rept	1024
	mtspr	spr, r4
	blr
	spr = spr + 1
	.endr
