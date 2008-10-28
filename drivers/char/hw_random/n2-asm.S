/* n2-asm.S: Niagara2 RNG hypervisor call assembler.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */
#include <linux/linkage.h>
#include <asm/hypervisor.h>
#include "n2rng.h"

	.text

ENTRY(sun4v_rng_get_diag_ctl)
	mov	HV_FAST_RNG_GET_DIAG_CTL, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_rng_get_diag_ctl)

ENTRY(sun4v_rng_ctl_read_v1)
	mov	%o1, %o3
	mov	%o2, %o4
	mov	HV_FAST_RNG_CTL_READ, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o3]
	retl
	 stx	%o2, [%o4]
ENDPROC(sun4v_rng_ctl_read_v1)

ENTRY(sun4v_rng_ctl_read_v2)
	save	%sp, -192, %sp
	mov	%i0, %o0
	mov	%i1, %o1
	mov	HV_FAST_RNG_CTL_READ, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%i2]
	stx	%o2, [%i3]
	stx	%o3, [%i4]
	stx	%o4, [%i5]
	ret
	restore	%g0, %o0, %o0
ENDPROC(sun4v_rng_ctl_read_v2)

ENTRY(sun4v_rng_ctl_write_v1)
	mov	%o3, %o4
	mov	HV_FAST_RNG_CTL_WRITE, %o5
	ta	HV_FAST_TRAP
	retl
	 stx	%o1, [%o4]
ENDPROC(sun4v_rng_ctl_write_v1)

ENTRY(sun4v_rng_ctl_write_v2)
	mov	HV_FAST_RNG_CTL_WRITE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_rng_ctl_write_v2)

ENTRY(sun4v_rng_data_read_diag_v1)
	mov	%o2, %o4
	mov	HV_FAST_RNG_DATA_READ_DIAG, %o5
	ta	HV_FAST_TRAP
	retl
	 stx	%o1, [%o4]
ENDPROC(sun4v_rng_data_read_diag_v1)

ENTRY(sun4v_rng_data_read_diag_v2)
	mov	%o3, %o4
	mov	HV_FAST_RNG_DATA_READ_DIAG, %o5
	ta	HV_FAST_TRAP
	retl
	 stx	%o1, [%o4]
ENDPROC(sun4v_rng_data_read_diag_v2)

ENTRY(sun4v_rng_data_read)
	mov	%o1, %o4
	mov	HV_FAST_RNG_DATA_READ, %o5
	ta	HV_FAST_TRAP
	retl
	 stx	%o1, [%o4]
ENDPROC(sun4v_rng_data_read)
