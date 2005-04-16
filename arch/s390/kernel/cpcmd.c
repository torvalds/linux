/*
 *  arch/s390/kernel/cpcmd.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Christian Borntraeger (cborntra@de.ibm.com),
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <asm/ebcdic.h>
#include <asm/cpcmd.h>
#include <asm/system.h>

static DEFINE_SPINLOCK(cpcmd_lock);
static char cpcmd_buf[240];

/*
 * the caller of __cpcmd has to ensure that the response buffer is below 2 GB
 */
void __cpcmd(char *cmd, char *response, int rlen)
{
	const int mask = 0x40000000L;
	unsigned long flags;
	int cmdlen;

	spin_lock_irqsave(&cpcmd_lock, flags);
	cmdlen = strlen(cmd);
	BUG_ON(cmdlen > 240);
	strcpy(cpcmd_buf, cmd);
	ASCEBC(cpcmd_buf, cmdlen);

	if (response != NULL && rlen > 0) {
		memset(response, 0, rlen);
#ifndef CONFIG_ARCH_S390X
		asm volatile ("LRA   2,0(%0)\n\t"
                              "LR    4,%1\n\t"
                              "O     4,%4\n\t"
                              "LRA   3,0(%2)\n\t"
                              "LR    5,%3\n\t"
                              ".long 0x83240008 # Diagnose X'08'\n\t"
                              : /* no output */
                              : "a" (cpcmd_buf), "d" (cmdlen),
                                "a" (response), "d" (rlen), "m" (mask)
                              : "cc", "2", "3", "4", "5" );
#else /* CONFIG_ARCH_S390X */
                asm volatile ("   lrag  2,0(%0)\n"
                              "   lgr   4,%1\n"
                              "   o     4,%4\n"
                              "   lrag  3,0(%2)\n"
                              "   lgr   5,%3\n"
                              "   sam31\n"
                              "   .long 0x83240008 # Diagnose X'08'\n"
                              "   sam64"
                              : /* no output */
                              : "a" (cpcmd_buf), "d" (cmdlen),
                                "a" (response), "d" (rlen), "m" (mask)
                              : "cc", "2", "3", "4", "5" );
#endif /* CONFIG_ARCH_S390X */
                EBCASC(response, rlen);
        } else {
#ifndef CONFIG_ARCH_S390X
                asm volatile ("LRA   2,0(%0)\n\t"
                              "LR    3,%1\n\t"
                              ".long 0x83230008 # Diagnose X'08'\n\t"
                              : /* no output */
                              : "a" (cpcmd_buf), "d" (cmdlen)
                              : "2", "3"  );
#else /* CONFIG_ARCH_S390X */
                asm volatile ("   lrag  2,0(%0)\n"
                              "   lgr   3,%1\n"
                              "   sam31\n"
                              "   .long 0x83230008 # Diagnose X'08'\n"
                              "   sam64"
                              : /* no output */
                              : "a" (cpcmd_buf), "d" (cmdlen)
                              : "2", "3"  );
#endif /* CONFIG_ARCH_S390X */
        }
	spin_unlock_irqrestore(&cpcmd_lock, flags);
}

EXPORT_SYMBOL(__cpcmd);

#ifdef CONFIG_ARCH_S390X
void cpcmd(char *cmd, char *response, int rlen)
{
	char *lowbuf;
	if ((rlen == 0) || (response == NULL)
	    || !((unsigned long)response >> 31))
		__cpcmd(cmd, response, rlen);
	else {
		lowbuf = kmalloc(rlen, GFP_KERNEL | GFP_DMA);
		if (!lowbuf) {
			printk(KERN_WARNING
				"cpcmd: could not allocate response buffer\n");
			return;
		}
		__cpcmd(cmd, lowbuf, rlen);
		memcpy(response, lowbuf, rlen);
		kfree(lowbuf);
	}
}

EXPORT_SYMBOL(cpcmd);
#endif		/* CONFIG_ARCH_S390X */
