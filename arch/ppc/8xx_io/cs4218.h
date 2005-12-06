#ifndef _cs4218_h_
/*
 *  Hacked version of linux/drivers/sound/dmasound/dmasound.h
 *
 *
 *  Minor numbers for the sound driver.
 *
 *  Unfortunately Creative called the codec chip of SB as a DSP. For this
 *  reason the /dev/dsp is reserved for digitized audio use. There is a
 *  device for true DSP processors but it will be called something else.
 *  In v3.0 it's /dev/sndproc but this could be a temporary solution.
 */
#define _cs4218_h_

#include <linux/types.h>
#include <linux/config.h>

#define SND_NDEVS	256	/* Number of supported devices */
#define SND_DEV_CTL	0	/* Control port /dev/mixer */
#define SND_DEV_SEQ	1	/* Sequencer output /dev/sequencer (FM
				   synthesizer and MIDI output) */
#define SND_DEV_MIDIN	2	/* Raw midi access */
#define SND_DEV_DSP	3	/* Digitized voice /dev/dsp */
#define SND_DEV_AUDIO	4	/* Sparc compatible /dev/audio */
#define SND_DEV_DSP16	5	/* Like /dev/dsp but 16 bits/sample */
#define SND_DEV_STATUS	6	/* /dev/sndstat */
/* #7 not in use now. Was in 2.4. Free for use after v3.0. */
#define SND_DEV_SEQ2	8	/* /dev/sequencer, level 2 interface */
#define SND_DEV_SNDPROC 9	/* /dev/sndproc for programmable devices */
#define SND_DEV_PSS	SND_DEV_SNDPROC

/* switch on various prinks */
#define DEBUG_DMASOUND 1

#define MAX_AUDIO_DEV	5
#define MAX_MIXER_DEV	4
#define MAX_SYNTH_DEV	3
#define MAX_MIDI_DEV	6
#define MAX_TIMER_DEV	3

#define MAX_CATCH_RADIUS	10

#define le2be16(x)	(((x)<<8 & 0xff00) | ((x)>>8 & 0x00ff))
#define le2be16dbl(x)	(((x)<<8 & 0xff00ff00) | ((x)>>8 & 0x00ff00ff))

#define IOCTL_IN(arg, ret) \
	do { int error = get_user(ret, (int *)(arg)); \
		if (error) return error; \
	} while (0)
#define IOCTL_OUT(arg, ret)	ioctl_return((int *)(arg), ret)

static inline int ioctl_return(int *addr, int value)
{
	return value < 0 ? value : put_user(value, addr);
}

#define HAS_RECORD

    /*
     *  Initialization
     */

/* description of the set-up applies to either hard or soft settings */

typedef struct {
    int format;		/* AFMT_* */
    int stereo;		/* 0 = mono, 1 = stereo */
    int size;		/* 8/16 bit*/
    int speed;		/* speed */
} SETTINGS;

    /*
     *  Machine definitions
     */

typedef struct {
    const char *name;
    const char *name2;
    void (*open)(void);
    void (*release)(void);
    void *(*dma_alloc)(unsigned int, gfp_t);
    void (*dma_free)(void *, unsigned int);
    int (*irqinit)(void);
#ifdef MODULE
    void (*irqcleanup)(void);
#endif
    void (*init)(void);
    void (*silence)(void);
    int (*setFormat)(int);
    int (*setVolume)(int);
    int (*setBass)(int);
    int (*setTreble)(int);
    int (*setGain)(int);
    void (*play)(void);
    void (*record)(void);		/* optional */
    void (*mixer_init)(void);		/* optional */
    int (*mixer_ioctl)(u_int, u_long);	/* optional */
    int (*write_sq_setup)(void);	/* optional */
    int (*read_sq_setup)(void);		/* optional */
    int (*sq_open)(mode_t);		/* optional */
    int (*state_info)(char *, size_t);	/* optional */
    void (*abort_read)(void);		/* optional */
    int min_dsp_speed;
    int max_dsp_speed;
    int version ;
    int hardware_afmts ;		/* OSS says we only return h'ware info */
					/* when queried via SNDCTL_DSP_GETFMTS */
    int capabilities ;		/* low-level reply to SNDCTL_DSP_GETCAPS */
    SETTINGS default_hard ;	/* open() or init() should set something valid */
    SETTINGS default_soft ;	/* you can make it look like old OSS, if you want to */
} MACHINE;

    /*
     *  Low level stuff
     */

typedef struct {
    ssize_t (*ct_ulaw)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_alaw)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_s8)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_u8)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_s16be)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_u16be)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_s16le)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
    ssize_t (*ct_u16le)(const u_char *, size_t, u_char *, ssize_t *, ssize_t);
} TRANS;


    /*
     * Sound queue stuff, the heart of the driver
     */

struct sound_queue {
    /* buffers allocated for this queue */
    int numBufs;		/* real limits on what the user can have */
    int bufSize;		/* in bytes */
    char **buffers;

    /* current parameters */
    int locked ;		/* params cannot be modified when != 0 */
    int user_frags ;		/* user requests this many */
    int user_frag_size ;	/* of this size */
    int max_count;		/* actual # fragments <= numBufs */
    int block_size;		/* internal block size in bytes */
    int max_active;		/* in-use fragments <= max_count */

    /* it shouldn't be necessary to declare any of these volatile */
    int front, rear, count;
    int rear_size;
    /*
     *	The use of the playing field depends on the hardware
     *
     *	Atari, PMac: The number of frames that are loaded/playing
     *
     *	Amiga: Bit 0 is set: a frame is loaded
     *	       Bit 1 is set: a frame is playing
     */
    int active;
    wait_queue_head_t action_queue, open_queue, sync_queue;
    int open_mode;
    int busy, syncing, xruns, died;
};

#define SLEEP(queue)		interruptible_sleep_on_timeout(&queue, HZ)
#define WAKE_UP(queue)		(wake_up_interruptible(&queue))

#endif /* _cs4218_h_ */
