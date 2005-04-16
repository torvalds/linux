/* 
 * linux/arch/sh/boards/cat68701/setup.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *               2001  Yutaro Ebihara
 *
 * Setup routines for A-ONE Corp CAT-68701 SH7708 Board
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/mach/io.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

const char *get_system_type(void)
{
	return "CAT-68701";
}

#ifdef CONFIG_HEARTBEAT
void heartbeat_cat68701()
{
        static unsigned int cnt = 0, period = 0 , bit = 0;
        cnt += 1;
        if (cnt < period) {
                return;
        }
        cnt = 0;

        /* Go through the points (roughly!):
         * f(0)=10, f(1)=16, f(2)=20, f(5)=35,f(inf)->110
         */
        period = 110 - ( (300<<FSHIFT)/
                         ((avenrun[0]/5) + (3<<FSHIFT)) );

	if(bit){ bit=0; }else{ bit=1; }
	outw(bit<<15,0x3fe);
}
#endif /* CONFIG_HEARTBEAT */

unsigned long cat68701_isa_port2addr(unsigned long offset)
{
	/* CompactFlash (IDE) */
	if (((offset >= 0x1f0) && (offset <= 0x1f7)) || (offset==0x3f6))
		return 0xba000000 + offset;

	/* INPUT PORT */
	if ((offset >= 0x3fc) && (offset <= 0x3fd))
		return 0xb4007000 + offset;

	/* OUTPUT PORT */
	if ((offset >= 0x3fe) && (offset <= 0x3ff))
		return 0xb4007400 + offset;

	return offset + 0xb4000000; /* other I/O (EREA 5)*/
}

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_cat68701 __initmv = {
	.mv_nr_irqs		= 32,
	.mv_isa_port2addr	= cat68701_isa_port2addr,
	.mv_irq_demux		= cat68701_irq_demux,

	.mv_init_irq		= init_cat68701_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_cat68701,
#endif
};
ALIAS_MV(cat68701)

int __init platform_setup(void)
{
	/* dummy read erea5 (CS8900A) */
}

