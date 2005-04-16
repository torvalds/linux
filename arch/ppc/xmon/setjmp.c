/*
 * Copyright (C) 1996 Paul Mackerras.
 *
 * NB this file must be compiled with -O2.
 */

int
xmon_setjmp(long *buf)
{
    asm ("mflr 0; stw 0,0(%0);"
	 "stw 1,4(%0); stw 2,8(%0);"
	 "mfcr 0; stw 0,12(%0);"
	 "stmw 13,16(%0)"
	 : : "r" (buf));
    /* XXX should save fp regs as well */
    return 0;
}

void
xmon_longjmp(long *buf, int val)
{
    if (val == 0)
	val = 1;
    asm ("lmw 13,16(%0);"
	 "lwz 0,12(%0); mtcrf 0x38,0;"
	 "lwz 0,0(%0); lwz 1,4(%0); lwz 2,8(%0);"
	 "mtlr 0; mr 3,%1"
	 : : "r" (buf), "r" (val));
}
