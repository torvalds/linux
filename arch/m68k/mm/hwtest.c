/* Tests for presence or absence of hardware registers.
 * This code was originally in atari/config.c, but I noticed
 * that it was also in drivers/nubus/nubus.c and I wanted to
 * use it in hp300/config.c, so it seemed sensible to pull it
 * out into its own file.
 *
 * The test is for use when trying to read a hardware register
 * that isn't present would cause a bus error. We set up a
 * temporary handler so that this doesn't kill the kernel.
 *
 * There is a test-by-reading and a test-by-writing; I present
 * them here complete with the comments from the original atari
 * config.c...
 *                -- PMM <pmaydell@chiark.greenend.org.uk>, 05/1998
 */

/* This function tests for the presence of an address, specially a
 * hardware register address. It is called very early in the kernel
 * initialization process, when the VBR register isn't set up yet. On
 * an Atari, it still points to address 0, which is unmapped. So a bus
 * error would cause another bus error while fetching the exception
 * vector, and the CPU would do nothing at all. So we needed to set up
 * a temporary VBR and a vector table for the duration of the test.
 */

#include <linux/module.h>

int hwreg_present( volatile void *regp )
{
    int	ret = 0;
    unsigned long flags;
    long	save_sp, save_vbr;
    long	tmp_vectors[3];

    local_irq_save(flags);
    __asm__ __volatile__
	(	"movec	%/vbr,%2\n\t"
		"movel	#Lberr1,%4@(8)\n\t"
                "movec	%4,%/vbr\n\t"
		"movel	%/sp,%1\n\t"
		"moveq	#0,%0\n\t"
		"tstb	%3@\n\t"
		"nop\n\t"
		"moveq	#1,%0\n"
                "Lberr1:\n\t"
		"movel	%1,%/sp\n\t"
		"movec	%2,%/vbr"
		: "=&d" (ret), "=&r" (save_sp), "=&r" (save_vbr)
		: "a" (regp), "a" (tmp_vectors)
                );
    local_irq_restore(flags);

    return( ret );
}
EXPORT_SYMBOL(hwreg_present);

/* Basically the same, but writes a value into a word register, protected
 * by a bus error handler. Returns 1 if successful, 0 otherwise.
 */

int hwreg_write( volatile void *regp, unsigned short val )
{
	int		ret;
	unsigned long flags;
	long	save_sp, save_vbr;
	long	tmp_vectors[3];

	local_irq_save(flags);
	__asm__ __volatile__
	(	"movec	%/vbr,%2\n\t"
		"movel	#Lberr2,%4@(8)\n\t"
		"movec	%4,%/vbr\n\t"
		"movel	%/sp,%1\n\t"
		"moveq	#0,%0\n\t"
		"movew	%5,%3@\n\t"
		"nop	\n\t"	/* If this nop isn't present, 'ret' may already be
				 * loaded with 1 at the time the bus error
				 * happens! */
		"moveq	#1,%0\n"
	"Lberr2:\n\t"
		"movel	%1,%/sp\n\t"
		"movec	%2,%/vbr"
		: "=&d" (ret), "=&r" (save_sp), "=&r" (save_vbr)
		: "a" (regp), "a" (tmp_vectors), "g" (val)
	);
	local_irq_restore(flags);

	return( ret );
}
EXPORT_SYMBOL(hwreg_write);

