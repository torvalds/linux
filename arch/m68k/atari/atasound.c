/*
 * linux/arch/m68k/atari/atasound.c
 *
 * ++Geert: Moved almost all stuff to linux/drivers/sound/
 *
 * The author of atari_nosound, atari_mksound and atari_microwire_cmd is
 * unknown. (++roman: That's me... :-)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * 1998-05-31 ++andreas: atari_mksound rewritten to always use the envelope,
 *			 no timer, atari_nosound removed.
 *
 */


#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/atarihw.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/atariints.h>


/*
 * stuff from the old atasound.c
 */

void atari_microwire_cmd (int cmd)
{
	tt_microwire.mask = 0x7ff;
	tt_microwire.data = MW_LM1992_ADDR | cmd;

	/* Busy wait for data being completely sent :-( */
	while( tt_microwire.mask != 0x7ff)
		;
}
EXPORT_SYMBOL(atari_microwire_cmd);


/* PSG base frequency */
#define	PSG_FREQ	125000
/* PSG envelope base frequency times 10 */
#define PSG_ENV_FREQ_10	78125

void atari_mksound (unsigned int hz, unsigned int ticks)
{
	/* Generates sound of some frequency for some number of clock
	   ticks.  */
	unsigned long flags;
	unsigned char tmp;
	int period;

	local_irq_save(flags);


	/* Disable generator A in mixer control.  */
	sound_ym.rd_data_reg_sel = 7;
	tmp = sound_ym.rd_data_reg_sel;
	tmp |= 011;
	sound_ym.wd_data = tmp;

	if (hz) {
	    /* Convert from frequency value to PSG period value (base
	       frequency 125 kHz).  */

	    period = PSG_FREQ / hz;

	    if (period > 0xfff) period = 0xfff;

	/* Set generator A frequency to hz.  */
	sound_ym.rd_data_reg_sel = 0;
	sound_ym.wd_data = period & 0xff;
	sound_ym.rd_data_reg_sel = 1;
	sound_ym.wd_data = (period >> 8) & 0xf;
	if (ticks) {
		/* Set length of envelope (max 8 sec).  */
		int length = (ticks * PSG_ENV_FREQ_10) / HZ / 10;

		if (length > 0xffff) length = 0xffff;
		sound_ym.rd_data_reg_sel = 11;
		sound_ym.wd_data = length & 0xff;
		sound_ym.rd_data_reg_sel = 12;
		sound_ym.wd_data = length >> 8;
		/* Envelope form: max -> min single.  */
		sound_ym.rd_data_reg_sel = 13;
		sound_ym.wd_data = 0;
		/* Use envelope for generator A.  */
		sound_ym.rd_data_reg_sel = 8;
		sound_ym.wd_data = 0x10;
	} else {
		/* Set generator A level to maximum, no envelope.  */
		sound_ym.rd_data_reg_sel = 8;
		sound_ym.wd_data = 15;
	}
	/* Turn on generator A in mixer control.  */
	sound_ym.rd_data_reg_sel = 7;
	tmp &= ~1;
	sound_ym.wd_data = tmp;
	}
	local_irq_restore(flags);
}
