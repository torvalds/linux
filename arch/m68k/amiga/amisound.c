/*
 * linux/arch/m68k/amiga/amisound.c
 *
 * amiga sound driver for Linux/m68k
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/string.h>

#include <asm/system.h>
#include <asm/amigahw.h>

static unsigned short *snd_data;
static const signed char sine_data[] = {
	0,  39,  75,  103,  121,  127,  121,  103,  75,  39,
	0, -39, -75, -103, -121, -127, -121, -103, -75, -39
};
#define DATA_SIZE	(sizeof(sine_data)/sizeof(sine_data[0]))

    /*
     * The minimum period for audio may be modified by the frame buffer
     * device since it depends on htotal (for OCS/ECS/AGA)
     */

volatile unsigned short amiga_audio_min_period = 124; /* Default for pre-OCS */

#define MAX_PERIOD	(65535)


    /*
     *	Current period (set by dmasound.c)
     */

unsigned short amiga_audio_period = MAX_PERIOD;

static unsigned long clock_constant;

void __init amiga_init_sound(void)
{
	static struct resource beep_res = { .name = "Beep" };

	snd_data = amiga_chip_alloc_res(sizeof(sine_data), &beep_res);
	if (!snd_data) {
		printk (KERN_CRIT "amiga init_sound: failed to allocate chipmem\n");
		return;
	}
	memcpy (snd_data, sine_data, sizeof(sine_data));

	/* setup divisor */
	clock_constant = (amiga_colorclock+DATA_SIZE/2)/DATA_SIZE;

	/* without amifb, turn video off and enable high quality sound */
#ifndef CONFIG_FB_AMIGA
	amifb_video_off();
#endif
}

static void nosound( unsigned long ignored );
static DEFINE_TIMER(sound_timer, nosound, 0, 0);

void amiga_mksound( unsigned int hz, unsigned int ticks )
{
	unsigned long flags;

	if (!snd_data)
		return;

	local_irq_save(flags);
	del_timer( &sound_timer );

	if (hz > 20 && hz < 32767) {
		unsigned long period = (clock_constant / hz);

		if (period < amiga_audio_min_period)
			period = amiga_audio_min_period;
		if (period > MAX_PERIOD)
			period = MAX_PERIOD;

		/* setup pointer to data, period, length and volume */
		custom.aud[2].audlc = snd_data;
		custom.aud[2].audlen = sizeof(sine_data)/2;
		custom.aud[2].audper = (unsigned short)period;
		custom.aud[2].audvol = 32; /* 50% of maxvol */

		if (ticks) {
			sound_timer.expires = jiffies + ticks;
			add_timer( &sound_timer );
		}

		/* turn on DMA for audio channel 2 */
		custom.dmacon = DMAF_SETCLR | DMAF_AUD2;

	} else
		nosound( 0 );

	local_irq_restore(flags);
}


static void nosound( unsigned long ignored )
{
	/* turn off DMA for audio channel 2 */
	custom.dmacon = DMAF_AUD2;
	/* restore period to previous value after beeping */
	custom.aud[2].audper = amiga_audio_period;
}
