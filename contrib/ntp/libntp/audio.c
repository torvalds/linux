/*
 * audio.c - audio interface for reference clock audio drivers
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(HAVE_SYS_AUDIOIO_H) || defined(HAVE_SUN_AUDIOIO_H) || \
    defined(HAVE_SYS_SOUNDCARD_H) || defined(HAVE_MACHINE_SOUNDCARD_H)

#include "audio.h"
#include "ntp_stdlib.h"
#include "ntp_syslog.h"
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <stdio.h>
#include "ntp_string.h"

#ifdef HAVE_SYS_AUDIOIO_H
# include <sys/audioio.h>
#endif /* HAVE_SYS_AUDIOIO_H */

#ifdef HAVE_SUN_AUDIOIO_H
# include <sys/ioccom.h>
# include <sun/audioio.h>
#endif /* HAVE_SUN_AUDIOIO_H */

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */

#include <fcntl.h>

#ifdef HAVE_MACHINE_SOUNDCARD_H
# include <machine/soundcard.h>
# define PCM_STYLE_SOUND
#else
# ifdef HAVE_SYS_SOUNDCARD_H
#  include <sys/soundcard.h>
#  define PCM_STYLE_SOUND
# endif
#endif

#ifdef PCM_STYLE_SOUND
# include <ctype.h>
#endif


/* 
 * 4.4BSD-Lite switched to an unsigned long ioctl arg.  Detect common
 * derivatives here, and apply that type. To make the following code
 * less verbose we make a proper typedef.
 * The joy of IOCTL programming...
 */
# if defined(__FreeBSD__) || defined(__APPLE__) || defined(__NetBSD__) || defined __OpenBSD__
typedef unsigned long ioctl_arg_T;
#else
typedef int ioctl_arg_T;
#endif

/*
 * Global variables
 */
#ifdef HAVE_SYS_AUDIOIO_H
static struct audio_device device; /* audio device ident */
#endif /* HAVE_SYS_AUDIOIO_H */
#ifdef PCM_STYLE_SOUND
# define INIT_FILE "/etc/ntp.audio"

static ioctl_arg_T agc		= SOUND_MIXER_WRITE_RECLEV; /* or IGAIN or LINE */
static ioctl_arg_T audiomonitor	= SOUND_MIXER_WRITE_VOLUME; /* or OGAIN */
static int devmask = 0;
static int recmask = 0;
static char cf_c_dev[100], cf_i_dev[100], cf_agc[100], cf_monitor[100];

static const char *m_names[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES;
#else /* not PCM_STYLE_SOUND */
static struct audio_info info;	/* audio device info */
#endif /* not PCM_STYLE_SOUND */
static int ctl_fd;		/* audio control file descriptor */

#ifdef PCM_STYLE_SOUND
static void audio_config_read (int, const char **, const char **);
static int  mixer_name (const char *, int);


int
mixer_name(
	const char *m_name,
	int m_mask
	)
{
	int i;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; ++i)
		if (((1 << i) & m_mask)
		    && !strcmp(m_names[i], m_name))
			break;

	return (SOUND_MIXER_NRDEVICES == i)
	    ? -1
	    : i
	    ;
}


/*
 * Check:
 *
 * /etc/ntp.audio#	where # is the unit number
 * /etc/ntp.audio.#	where # is the unit number
 * /etc/ntp.audio
 *
 * for contents of the form:
 *
 * idev /dev/input_device
 * cdev /dev/control_device
 * agc pcm_input_device {igain,line,line1,...}
 * monitor pcm_monitor_device {ogain,...}
 *
 * The device names for the "agc" and "monitor" keywords
 * can be found by running either the "mixer" program or the
 * util/audio-pcm program.
 *
 * Great hunks of this subroutine were swiped from refclock_oncore.c
 */
static void
audio_config_read(
	int unit,
	const char **c_dev,	/* Control device */
	const char **i_dev	/* input device */
	)
{
	FILE *fd;
	char device[20], line[100], ab[100];

	snprintf(device, sizeof(device), "%s%d", INIT_FILE, unit);
	if ((fd = fopen(device, "r")) == NULL) {
		printf("audio_config_read: <%s> NO\n", device);
		snprintf(device, sizeof(device), "%s.%d", INIT_FILE,
			 unit);
		if ((fd = fopen(device, "r")) == NULL) {
			printf("audio_config_read: <%s> NO\n", device);
			snprintf(device, sizeof(device), "%s",
				 INIT_FILE);
			if ((fd = fopen(device, "r")) == NULL) {
				printf("audio_config_read: <%s> NO\n",
				       device);
				return;
			}
		}
	}
	printf("audio_config_read: reading <%s>\n", device);
	while (fgets(line, sizeof line, fd)) {
		char *cp, *cc, *ca;
		int i;

		/* Remove comments */
		if ((cp = strchr(line, '#')))
			*cp = '\0';

		/* Remove any trailing spaces */
		for (i = strlen(line);
		     i > 0 && isascii((unsigned char)line[i - 1]) && isspace((unsigned char)line[i - 1]);
			)
			line[--i] = '\0';

		/* Remove leading space */
		for (cc = line; *cc && isascii((unsigned char)*cc) && isspace((unsigned char)*cc); cc++)
			continue;

		/* Stop if nothing left */
		if (!*cc)
			continue;

		/* Uppercase the command and find the arg */
		for (ca = cc; *ca; ca++) {
			if (isascii((unsigned char)*ca)) {
				if (islower((unsigned char)*ca)) {
					*ca = toupper((unsigned char)*ca);
				} else if (isspace((unsigned char)*ca) || (*ca == '='))
					break;
			}
		}

		/* Remove space (and possible =) leading the arg */
		for (; *ca && isascii((unsigned char)*ca) && (isspace((unsigned char)*ca) || (*ca == '=')); ca++)
			continue;

		if (!strncmp(cc, "IDEV", 4) &&
		    1 == sscanf(ca, "%99s", ab)) {
			strlcpy(cf_i_dev, ab, sizeof(cf_i_dev));
			printf("idev <%s>\n", ab);
		} else if (!strncmp(cc, "CDEV", 4) &&
			   1 == sscanf(ca, "%99s", ab)) {
			strlcpy(cf_c_dev, ab, sizeof(cf_c_dev));
			printf("cdev <%s>\n", ab);
		} else if (!strncmp(cc, "AGC", 3) &&
			   1 == sscanf(ca, "%99s", ab)) {
			strlcpy(cf_agc, ab, sizeof(cf_agc));
			printf("agc <%s> %d\n", ab, i);
		} else if (!strncmp(cc, "MONITOR", 7) &&
			   1 == sscanf(ca, "%99s", ab)) {
			strlcpy(cf_monitor, ab, sizeof(cf_monitor));
			printf("monitor <%s> %d\n", ab, mixer_name(ab, -1));
		}
	}
	fclose(fd);
	return;
}
#endif /* PCM_STYLE_SOUND */

/*
 * audio_init - open and initialize audio device
 *
 * This code works with SunOS 4.x, Solaris 2.x, and PCM; however, it is
 * believed generic and applicable to other systems with a minor twid
 * or two. All it does is open the device, set the buffer size (Solaris
 * only), preset the gain and set the input port. It assumes that the
 * codec sample rate (8000 Hz), precision (8 bits), number of channels
 * (1) and encoding (ITU-T G.711 mu-law companded) have been set by
 * default.
 */
int
audio_init(
	const char *dname,	/* device name */
	int	bufsiz,		/* buffer size */
	int	unit		/* device unit (0-3) */
	)
{
#ifdef PCM_STYLE_SOUND
# define ACTL_DEV	"/dev/mixer%d"
	char actl_dev[30];
# ifdef HAVE_STRUCT_SND_SIZE
	struct snd_size s_size;
# endif
# ifdef AIOGFMT
	snd_chan_param s_c_p;
# endif
#endif
	int fd;
	int rval;
	const char *actl =
#ifdef PCM_STYLE_SOUND
		actl_dev
#else
		"/dev/audioctl"
#endif
		;

#ifdef PCM_STYLE_SOUND
	snprintf(actl_dev, sizeof(actl_dev), ACTL_DEV, unit);

	audio_config_read(unit, &actl, &dname);
	/* If we have values for cf_c_dev or cf_i_dev, use them. */
	if (*cf_c_dev)
		actl = cf_c_dev;
	if (*cf_i_dev)
		dname = cf_i_dev;
#endif

	/*
	 * Open audio device
	 */
	fd = open(dname, O_RDWR | O_NONBLOCK, 0777);
	if (fd < 0) {
		msyslog(LOG_ERR, "audio_init: %s %m", dname);
		return (fd);
	}

	/*
	 * Open audio control device.
	 */
	ctl_fd = open(actl, O_RDWR);
	if (ctl_fd < 0) {
		msyslog(LOG_ERR, "audio_init: invalid control device <%s>",
		    actl);
		close(fd);
		return(ctl_fd);
	}

	/*
	 * Set audio device parameters.
	 */
#ifdef PCM_STYLE_SOUND
	printf("audio_init: <%s> bufsiz %d\n", dname, bufsiz);
	rval = fd;

# ifdef HAVE_STRUCT_SND_SIZE
	if (ioctl(fd, AIOGSIZE, &s_size) == -1)
	    printf("audio_init: AIOGSIZE: %s\n", strerror(errno));
	else
	    printf("audio_init: orig: play_size %d, rec_size %d\n",
		s_size.play_size, s_size.rec_size);

	s_size.play_size = s_size.rec_size = bufsiz;
	printf("audio_init: want: play_size %d, rec_size %d\n",
	       s_size.play_size, s_size.rec_size);

	if (ioctl(fd, AIOSSIZE, &s_size) == -1)
	    printf("audio_init: AIOSSIZE: %s\n", strerror(errno));
	else
	    printf("audio_init: set:  play_size %d, rec_size %d\n",
		s_size.play_size, s_size.rec_size);
# endif /* HAVE_STRUCT_SND_SIZE */

# ifdef SNDCTL_DSP_SETFRAGMENT
	{
		int tmp = (16 << 16) + 6; /* 16 fragments, each 2^6 bytes */
		if (ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &tmp) == -1)
		    printf("audio_init: SNDCTL_DSP_SETFRAGMENT: %s\n",
			   strerror(errno));
	}
# endif /* SNDCTL_DSP_SETFRAGMENT */

# ifdef AIOGFMT
	if (ioctl(fd, AIOGFMT, &s_c_p) == -1)
	    printf("audio_init: AIOGFMT: %s\n", strerror(errno));
	else
	    printf("audio_init: play_rate %lu, rec_rate %lu, play_format %#lx, rec_format %#lx\n",
		s_c_p.play_rate, s_c_p.rec_rate, s_c_p.play_format, s_c_p.rec_format);
# endif

	/* Grab the device and record masks */

	if (ioctl(ctl_fd, SOUND_MIXER_READ_DEVMASK, &devmask) == -1)
	    printf("SOUND_MIXER_READ_DEVMASK: %s\n", strerror(errno));
	if (ioctl(ctl_fd, SOUND_MIXER_READ_RECMASK, &recmask) == -1)
	    printf("SOUND_MIXER_READ_RECMASK: %s\n", strerror(errno));

	/* validate and set any specified config file stuff */
	if (cf_agc[0] != '\0') {
		int i;

		/* recmask */
		i = mixer_name(cf_agc, recmask);
		if (i >= 0)
			agc = MIXER_WRITE(i);
		else
			printf("input %s not in recmask %#x\n",
			       cf_agc, recmask);
	}

	if (cf_monitor[0] != '\0') {
		int i;

		/* devmask */
		i = mixer_name(cf_monitor, devmask);
		if (i >= 0)
                       audiomonitor = MIXER_WRITE(i);
		else
			printf("monitor %s not in devmask %#x\n",
			       cf_monitor, devmask);
	}

#else /* not PCM_STYLE_SOUND */
	AUDIO_INITINFO(&info);
	info.play.gain = AUDIO_MAX_GAIN;
	info.play.port = AUDIO_SPEAKER;
# ifdef HAVE_SYS_AUDIOIO_H
	info.record.buffer_size = bufsiz;
# endif /* HAVE_SYS_AUDIOIO_H */
	rval = ioctl(ctl_fd, AUDIO_SETINFO, (char *)&info);
	if (rval < 0) {
		msyslog(LOG_ERR, "audio: invalid control device parameters");
		close(ctl_fd);
		close(fd);
		return(rval);
	}
	rval = fd;
#endif /* not PCM_STYLE_SOUND */
	return (rval);
}


/*
 * audio_gain - adjust codec gains and port
 */
int
audio_gain(
	int gain,		/* volume level (gain) 0-255 */
	int mongain,		/* input to output mix (monitor gain) 0-255 */
	int port		/* selected I/O port: 1 mic/2 line in */
	)
{
	int rval;
	static int o_mongain = -1;
	static int o_port = -1;

#ifdef PCM_STYLE_SOUND
	int l, r;

# ifdef GCC
	rval = 0;		/* GCC thinks rval is used uninitialized */
# endif

	r = l = 100 * gain / 255;	/* Normalize to 0-100 */
# ifdef DEBUG
	if (debug > 1)
		printf("audio_gain: gain %d/%d\n", gain, l);
# endif
#if 0	/* not a good idea to do this; connector wiring dependency */
	/* figure out what channel(s) to use. just nuke right for now. */
	r = 0 ; /* setting to zero nicely mutes the channel */
#endif
	l |= r << 8;
	if (cf_agc[0] != '\0')
		rval = ioctl(ctl_fd, agc, &l);
	else
		rval = ioctl(ctl_fd
			    , (2 == port)
				? SOUND_MIXER_WRITE_LINE
				: SOUND_MIXER_WRITE_MIC
			    , &l);
	if (-1 == rval) {
		printf("audio_gain: agc write: %s\n", strerror(errno));
		return rval;
	}

	if (o_mongain != mongain) {
		r = l = 100 * mongain / 255;    /* Normalize to 0-100 */
# ifdef DEBUG
		if (debug > 1)
			printf("audio_gain: mongain %d/%d\n", mongain, l);
# endif
		l |= r << 8;
		if (cf_monitor[0] != '\0')
                       rval = ioctl(ctl_fd, audiomonitor, &l );
		else 
			rval = ioctl(ctl_fd, SOUND_MIXER_WRITE_VOLUME,
				     &l);
		if (-1 == rval) {
			printf("audio_gain: mongain write: %s\n",
			       strerror(errno));
			return (rval);
		}
		o_mongain = mongain;
	}

	if (o_port != port) {
# ifdef DEBUG
		if (debug > 1)
			printf("audio_gain: port %d\n", port);
# endif
		l = (1 << ((port == 2) ? SOUND_MIXER_LINE : SOUND_MIXER_MIC));
		rval = ioctl(ctl_fd, SOUND_MIXER_WRITE_RECSRC, &l);
		if (rval == -1) {
			printf("SOUND_MIXER_WRITE_RECSRC: %s\n",
			       strerror(errno));
			return (rval);
		}
# ifdef DEBUG
		if (debug > 1) {
			if (ioctl(ctl_fd, SOUND_MIXER_READ_RECSRC, &l) == -1)
				printf("SOUND_MIXER_WRITE_RECSRC: %s\n",
				       strerror(errno));
			else
				printf("audio_gain: recsrc is %d\n", l);
		}
# endif
		o_port = port;
	}
#else /* not PCM_STYLE_SOUND */
	ioctl(ctl_fd, AUDIO_GETINFO, (char *)&info);
	info.record.encoding = AUDIO_ENCODING_ULAW;
	info.record.error = 0;
	info.record.gain = gain;
	if (o_mongain != mongain)
		o_mongain = info.monitor_gain = mongain;
	if (o_port != port)
		o_port = info.record.port = port;
	rval = ioctl(ctl_fd, AUDIO_SETINFO, (char *)&info);
	if (rval < 0) {
		msyslog(LOG_ERR, "audio_gain: %m");
		return (rval);
	}
	rval = info.record.error;
#endif /* not PCM_STYLE_SOUND */
	return (rval);
}


/*
 * audio_show - display audio parameters
 *
 * This code doesn't really do anything, except satisfy curiousity and
 * verify the ioctl's work.
 */
void
audio_show(void)
{
#ifdef PCM_STYLE_SOUND
	int recsrc = 0;

	printf("audio_show: ctl_fd %d\n", ctl_fd);
	if (ioctl(ctl_fd, SOUND_MIXER_READ_RECSRC, &recsrc) == -1)
	    printf("SOUND_MIXER_READ_RECSRC: %s\n", strerror(errno));

#else /* not PCM_STYLE_SOUND */
# ifdef HAVE_SYS_AUDIOIO_H
	ioctl(ctl_fd, AUDIO_GETDEV, &device);
	printf("audio: name %s, version %s, config %s\n",
	    device.name, device.version, device.config);
# endif /* HAVE_SYS_AUDIOIO_H */
	ioctl(ctl_fd, AUDIO_GETINFO, (char *)&info);
	printf(
	    "audio: rate %d, chan %d, prec %d, code %d, gain %d, mon %d, port %d\n",
	    info.record.sample_rate, info.record.channels,
	    info.record.precision, info.record.encoding,
	    info.record.gain, info.monitor_gain, info.record.port);
	printf(
	    "audio: samples %d, eof %d, pause %d, error %d, waiting %d, balance %d\n",
	    info.record.samples, info.record.eof,
	    info.record.pause, info.record.error,
	    info.record.waiting, info.record.balance);
#endif /* not PCM_STYLE_SOUND */
}
#else
int audio_bs;
#endif /* HAVE_{SYS_AUDIOIO,SUN_AUDIOIO,MACHINE_SOUNDCARD,SYS_SOUNDCARD}_H */
