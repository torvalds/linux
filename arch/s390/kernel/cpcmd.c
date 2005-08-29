/*
 *  arch/s390/kernel/cpcmd.c
 *
 *  S390 version
 *    Copyright (C) 1999,2005 IBM Deutschland Entwicklung GmbH, IBM Corporation
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
static char cpcmd_buf[241];

/*
 * the caller of __cpcmd has to ensure that the response buffer is below 2 GB
 */
int  __cpcmd(const char *cmd, char *response, int rlen, int *response_code)
{
	const int mask = 0x40000000L;
	unsigned long flags;
	int return_code;
	int return_len;
	int cmdlen;

	spin_lock_irqsave(&cpcmd_lock, flags);
	cmdlen = strlen(cmd);
	BUG_ON(cmdlen > 240);
	memcpy(cpcmd_buf, cmd, cmdlen);
	ASCEBC(cpcmd_buf, cmdlen);

	if (response != NULL && rlen > 0) {
		memset(response, 0, rlen);
#ifndef CONFIG_ARCH_S390X
		asm volatile (	"lra	2,0(%2)\n"
				"lr	4,%3\n"
				"o	4,%6\n"
				"lra	3,0(%4)\n"
				"lr	5,%5\n"
				"diag	2,4,0x8\n"
				"brc	8, 1f\n"
				"ar	5, %5\n"
				"1: \n"
				"lr	%0,4\n"
				"lr	%1,5\n"
				: "=d" (return_code), "=d" (return_len)
				: "a" (cpcmd_buf), "d" (cmdlen),
				"a" (response), "d" (rlen), "m" (mask)
				: "cc", "2", "3", "4", "5" );
#else /* CONFIG_ARCH_S390X */
                asm volatile (	"lrag	2,0(%2)\n"
				"lgr	4,%3\n"
				"o	4,%6\n"
				"lrag	3,0(%4)\n"
				"lgr	5,%5\n"
				"sam31\n"
				"diag	2,4,0x8\n"
				"sam64\n"
				"brc	8, 1f\n"
				"agr	5, %5\n"
				"1: \n"
				"lgr	%0,4\n"
				"lgr	%1,5\n"
				: "=d" (return_code), "=d" (return_len)
				: "a" (cpcmd_buf), "d" (cmdlen),
				"a" (response), "d" (rlen), "m" (mask)
				: "cc", "2", "3", "4", "5" );
#endif /* CONFIG_ARCH_S390X */
                EBCASC(response, rlen);
        } else {
		return_len = 0;
#ifndef CONFIG_ARCH_S390X
                asm volatile (	"lra	2,0(%1)\n"
				"lr	3,%2\n"
				"diag	2,3,0x8\n"
				"lr	%0,3\n"
				: "=d" (return_code)
				: "a" (cpcmd_buf), "d" (cmdlen)
				: "2", "3"  );
#else /* CONFIG_ARCH_S390X */
                asm volatile (	"lrag	2,0(%1)\n"
				"lgr	3,%2\n"
				"sam31\n"
				"diag	2,3,0x8\n"
				"sam64\n"
				"lgr	%0,3\n"
				: "=d" (return_code)
				: "a" (cpcmd_buf), "d" (cmdlen)
				: "2", "3" );
#endif /* CONFIG_ARCH_S390X */
        }
	spin_unlock_irqrestore(&cpcmd_lock, flags);
	if (response_code != NULL)
		*response_code = return_code;
	return return_len;
}

EXPORT_SYMBOL(__cpcmd);

#ifdef CONFIG_ARCH_S390X
int cpcmd(const char *cmd, char *response, int rlen, int *response_code)
{
	char *lowbuf;
	int len;

	if ((rlen == 0) || (response == NULL)
	    || !((unsigned long)response >> 31))
		len = __cpcmd(cmd, response, rlen, response_code);
	else {
		lowbuf = kmalloc(rlen, GFP_KERNEL | GFP_DMA);
		if (!lowbuf) {
			printk(KERN_WARNING
				"cpcmd: could not allocate response buffer\n");
			return -ENOMEM;
		}
		len = __cpcmd(cmd, lowbuf, rlen, response_code);
		memcpy(response, lowbuf, rlen);
		kfree(lowbuf);
	}
	return len;
}

EXPORT_SYMBOL(cpcmd);
#endif		/* CONFIG_ARCH_S390X */
