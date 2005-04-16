/*
 *	Mac bong noise generator. Note - we ought to put a boingy noise
 *	here 8)
 *
 *	----------------------------------------------------------------------
 *	16.11.98:
 *	rewrote some functions, added support for Enhanced ASC (Quadras)
 *	after the NetBSD asc.c console bell patch by Colin Wood/Frederick Bruck
 *	Juergen Mellinger (juergen.mellinger@t-online.de)
 */

#include <linux/sched.h>
#include <linux/timer.h>

#include <asm/macintosh.h>
#include <asm/mac_asc.h>

static int mac_asc_inited;
/*
 * dumb triangular wave table
 */
static __u8 mac_asc_wave_tab[ 0x800 ];

/*
 * Alan's original sine table; needs interpolating to 0x800
 * (hint: interpolate or hardwire [0 -> Pi/2[, it's symmetric)
 */
static const signed char sine_data[] = {
	0,  39,  75,  103,  121,  127,  121,  103,  75,  39,
	0, -39, -75, -103, -121, -127, -121, -103, -75, -39
};

/*
 * where the ASC hides ...
 */
static volatile __u8* mac_asc_regs = ( void* )0x50F14000;

/*
 * sample rate; is this a good default value?
 */
static unsigned long mac_asc_samplespersec = 11050;
static int mac_bell_duration;
static unsigned long mac_bell_phase; /* 0..2*Pi -> 0..0x800 (wavetable size) */
static unsigned long mac_bell_phasepersample;

/*
 * some function protos
 */
static void mac_init_asc( void );
static void mac_nosound( unsigned long );
static void mac_quadra_start_bell( unsigned int, unsigned int, unsigned int );
static void mac_quadra_ring_bell( unsigned long );
static void mac_av_start_bell( unsigned int, unsigned int, unsigned int );
static void ( *mac_special_bell )( unsigned int, unsigned int, unsigned int );

/*
 * our timer to start/continue/stop the bell
 */
static struct timer_list mac_sound_timer =
		TIMER_INITIALIZER(mac_nosound, 0, 0);

/*
 * Sort of initialize the sound chip (called from mac_mksound on the first
 * beep).
 */
static void mac_init_asc( void )
{
	int i;

	/*
	 * do some machine specific initialization
	 * BTW:
	 * the NetBSD Quadra patch identifies the Enhanced Apple Sound Chip via
	 *	mac_asc_regs[ 0x800 ] & 0xF0 != 0
	 * this makes no sense here, because we have to set the default sample
	 * rate anyway if we want correct frequencies
	 */
	switch ( macintosh_config->ident )
	{
		case MAC_MODEL_IIFX:
			/*
			 * The IIfx is always special ...
			 */
			mac_asc_regs = ( void* )0x50010000;
			break;
			/*
			 * not sure about how correct this list is
			 * machines with the EASC enhanced apple sound chip
			 */
		case MAC_MODEL_Q630:
		case MAC_MODEL_P475:
			mac_special_bell = mac_quadra_start_bell;
			mac_asc_samplespersec = 22150;
			break;
		case MAC_MODEL_C660:
		case MAC_MODEL_Q840:
			/*
			 * The Quadra 660AV and 840AV use the "Singer" custom ASIC for sound I/O.
			 * It appears to be similar to the "AWACS" custom ASIC in the Power Mac
			 * [678]100.  Because Singer and AWACS may have a similar hardware
			 * interface, this would imply that the code in drivers/sound/dmasound.c
			 * for AWACS could be used as a basis for Singer support.  All we have to
			 * do is figure out how to do DMA on the 660AV/840AV through the PSC and
			 * figure out where the Singer hardware sits in memory. (I'd look in the
			 * vicinity of the AWACS location in a Power Mac [678]100 first, or the
			 * current location of the Apple Sound Chip--ASC--in other Macs.)  The
			 * Power Mac [678]100 info can be found in MkLinux Mach kernel sources.
			 *
			 * Quoted from Apple's Tech Info Library, article number 16405:
			 *   "Among desktop Macintosh computers, only the 660AV, 840AV, and Power
			 *   Macintosh models have 16-bit audio input and output capability
			 *   because of the AT&T DSP3210 hardware circuitry and the 16-bit Singer
			 *   codec circuitry in the AVs.  The Audio Waveform Amplifier and
			 *   Converter (AWAC) chip in the Power Macintosh performs the same
			 *   16-bit I/O functionality.  The PowerBook 500 series computers
			 *   support 16-bit stereo output, but only mono input."
			 *
			 *   http://til.info.apple.com/techinfo.nsf/artnum/n16405
			 *
			 * --David Kilzer
			 */
			mac_special_bell = mac_av_start_bell;
			break;
		case MAC_MODEL_Q650:
		case MAC_MODEL_Q700:
		case MAC_MODEL_Q800:
		case MAC_MODEL_Q900:
		case MAC_MODEL_Q950:
			/*
			 * Currently not implemented!
			 */
			mac_special_bell = NULL;
			break;
		default:
			/*
			 * Every switch needs a default
			 */
			mac_special_bell = NULL;
			break;
	}

	/*
	 * init the wave table with a simple triangular wave
	 * A sine wave would sure be nicer here ...
	 */
	for ( i = 0; i < 0x400; i++ )
	{
		mac_asc_wave_tab[ i ] = i / 4;
		mac_asc_wave_tab[ i + 0x400 ] = 0xFF - i / 4;
	}
	mac_asc_inited = 1;
}

/*
 * Called to make noise; current single entry to the boing driver.
 * Does the job for simple ASC, calls other routines else.
 * XXX Fixme:
 * Should be split into asc_mksound, easc_mksound, av_mksound and
 * function pointer set in mac_init_asc which would be called at
 * init time.
 * _This_ is rather ugly ...
 */
void mac_mksound( unsigned int freq, unsigned int length )
{
	__u32 cfreq = ( freq << 5 ) / 468;
	__u32 flags;
	int i;

	if ( mac_special_bell == NULL )
	{
		/* Do nothing */
		return;
	}

	if ( !mac_asc_inited )
		mac_init_asc();

	if ( mac_special_bell )
	{
		mac_special_bell( freq, length, 128 );
		return;
	}

	if ( freq < 20 || freq > 20000 || length == 0 )
	{
		mac_nosound( 0 );
		return;
	}

	local_irq_save(flags);

	del_timer( &mac_sound_timer );

	for ( i = 0; i < 0x800; i++ )
		mac_asc_regs[ i ] = 0;
	for ( i = 0; i < 0x800; i++ )
		mac_asc_regs[ i ] = mac_asc_wave_tab[ i ];

	for ( i = 0; i < 8; i++ )
		*( __u32* )( ( __u32 )mac_asc_regs + ASC_CONTROL + 0x814 + 8 * i ) = cfreq;

	mac_asc_regs[ 0x807 ] = 0;
	mac_asc_regs[ ASC_VOLUME ] = 128;
	mac_asc_regs[ 0x805 ] = 0;
	mac_asc_regs[ 0x80F ] = 0;
	mac_asc_regs[ ASC_MODE ] = ASC_MODE_SAMPLE;
	mac_asc_regs[ ASC_ENABLE ] = ASC_ENABLE_SAMPLE;

	mac_sound_timer.expires = jiffies + length;
	add_timer( &mac_sound_timer );

	local_irq_restore(flags);
}

/*
 * regular ASC: stop whining ..
 */
static void mac_nosound( unsigned long ignored )
{
	mac_asc_regs[ ASC_ENABLE ] = 0;
}

/*
 * EASC entry; init EASC, don't load wavetable, schedule 'start whining'.
 */
static void mac_quadra_start_bell( unsigned int freq, unsigned int length, unsigned int volume )
{
	__u32 flags;

	/* if the bell is already ringing, ring longer */
	if ( mac_bell_duration > 0 )
	{
		mac_bell_duration += length;
		return;
	}

	mac_bell_duration = length;
	mac_bell_phase = 0;
	mac_bell_phasepersample = ( freq * sizeof( mac_asc_wave_tab ) ) / mac_asc_samplespersec;
	/* this is reasonably big for small frequencies */

	local_irq_save(flags);

	/* set the volume */
	mac_asc_regs[ 0x806 ] = volume;

	/* set up the ASC registers */
	if ( mac_asc_regs[ 0x801 ] != 1 )
	{
		/* select mono mode */
		mac_asc_regs[ 0x807 ] = 0;
		/* select sampled sound mode */
		mac_asc_regs[ 0x802 ] = 0;
		/* ??? */
		mac_asc_regs[ 0x801 ] = 1;
		mac_asc_regs[ 0x803 ] |= 0x80;
		mac_asc_regs[ 0x803 ] &= 0x7F;
	}

	mac_sound_timer.function = mac_quadra_ring_bell;
	mac_sound_timer.expires = jiffies + 1;
	add_timer( &mac_sound_timer );

	local_irq_restore(flags);
}

/*
 * EASC 'start/continue whining'; I'm not sure why the above function didn't
 * already load the wave table, or at least call this one...
 * This piece keeps reloading the wave table until done.
 */
static void mac_quadra_ring_bell( unsigned long ignored )
{
	int	i, count = mac_asc_samplespersec / HZ;
	__u32 flags;

	/*
	 * we neither want a sound buffer overflow nor underflow, so we need to match
	 * the number of samples per timer interrupt as exactly as possible.
	 * using the asc interrupt will give better results in the future
	 * ...and the possibility to use a real sample (a boingy noise, maybe...)
	 */

	local_irq_save(flags);

	del_timer( &mac_sound_timer );

	if ( mac_bell_duration-- > 0 )
	{
		for ( i = 0; i < count; i++ )
		{
			mac_bell_phase += mac_bell_phasepersample;
			mac_asc_regs[ 0 ] = mac_asc_wave_tab[ mac_bell_phase & ( sizeof( mac_asc_wave_tab ) - 1 ) ];
		}
		mac_sound_timer.expires = jiffies + 1;
		add_timer( &mac_sound_timer );
	}
	else
		mac_asc_regs[ 0x801 ] = 0;

	local_irq_restore(flags);
}

/*
 * AV code - please fill in.
 */
static void mac_av_start_bell( unsigned int freq, unsigned int length, unsigned int volume )
{
}
