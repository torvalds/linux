/*
 * audio-pcm.c - Scope out the PCM audio stuff
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(HAVE_MACHINE_SOUNDCARD_H) || defined(HAVE_SYS_SOUNDCARD_H)

#include "audio.h"
#include "ntp_stdlib.h"
#include "ntp_syslog.h"
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <stdio.h>
#include "ntp_string.h"

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
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

/*
 * Global variables
 */
static int ctl_fd;		/* audio control file descriptor */

const char *m_names[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES ;

void
d_fmt(
      unsigned int format
      )
{

  if (format & AFMT_MU_LAW)	printf("MU_LAW ");
  if (format & AFMT_A_LAW)	printf("A_LAW ");
  if (format & AFMT_IMA_ADPCM)	printf("IMA_ADPCM ");
  if (format & AFMT_U8)		printf("U8 ");
  if (format & AFMT_S16_LE)	printf("S16_LE ");
  if (format & AFMT_S16_BE)	printf("S16_BE ");
  if (format & AFMT_S8)		printf("S8 ");
  if (format & AFMT_U16_LE)	printf("U16_LE ");
  if (format & AFMT_U16_BE)	printf("U16_BE ");
  if (format & AFMT_MPEG)	printf("MPEG ");
  if (format & AFMT_AC3)	printf("AC3 ");
  printf("\n");
}

void
d_mixer(
	unsigned int mixer
	)
{
  int i;
  int n = 0;

  for (i = 0; i < SOUND_MIXER_NRDEVICES; ++i)
    if ((1 << i) & mixer) {
      if (n)
	printf(", ");
      printf("%s", m_names[i]);
      n = 1;
    }
  printf("\n");
}

int
main( )
{
	int	unit = 0;	/* device unit (0-3) */
# define AI_DEV		"/dev/audio%d"
# define AC_DEV		"/dev/mixer%d"
	char ai_dev[30];
	char ac_dev[30];
	struct snd_size s_size;
	snd_chan_param s_c_p;
	snd_capabilities s_c;
	int fd;
	int rval;
	char *dname = ai_dev;		/* device name */
	char *actl = ac_dev;
	int devmask = 0, recmask = 0, recsrc = 0;

	snprintf(ai_dev, sizeof(ai_dev), AI_DEV, unit);
	snprintf(ac_dev, sizeof(ac_dev), AC_DEV, unit);

	/*
	 * Open audio device. Do not complain if not there.
	 */
	fd = open(dname, O_RDWR | O_NONBLOCK, 0777);
	if (fd < 0)
		return (fd);

	/*
	 * Open audio control device.
	 */
	ctl_fd = open(actl, O_RDWR);
	if (ctl_fd < 0) {
		fprintf(stderr, "invalid control device <%s>\n", actl);
		close(fd);
		return(ctl_fd);
	}

	printf("input:   <%s> %d\n", dname, fd);
	printf("control: <%s> %d\n", actl, ctl_fd);

	if (ioctl(ctl_fd, SOUND_MIXER_READ_DEVMASK, &devmask) == -1)
	    printf("SOUND_MIXER_READ_DEVMASK: %s\n", strerror(errno));
	if (ioctl(ctl_fd, SOUND_MIXER_READ_RECMASK, &recmask) == -1)
	    printf("SOUND_MIXER_READ_RECMASK: %s\n", strerror(errno));
	if (ioctl(ctl_fd, SOUND_MIXER_READ_RECSRC, &recsrc) == -1)
	    printf("SOUND_MIXER_READ_RECSRC: %s\n", strerror(errno));

	printf("devmask: %#x recmask: %#x recsrc: %#x\n",
		devmask, recmask, recsrc);
	printf("Devmask: "); d_mixer(devmask);
	printf("Recmask: "); d_mixer(recmask);
	printf("RecSrc:  "); d_mixer(recsrc);

	/*
	 * Set audio device parameters.
	 */
	rval = fd;

	if (ioctl(fd, AIOGSIZE, &s_size) == -1)
	    printf("AIOGSIZE: %s\n", strerror(errno));
	else
	    printf("play_size %d, rec_size %d\n",
		s_size.play_size, s_size.rec_size);

	if (ioctl(fd, AIOGFMT, &s_c_p) == -1)
	    printf("AIOGFMT: %s\n", strerror(errno));
	else {
	  printf("play_rate %lu, rec_rate %lu, play_format %#lx, rec_format %#lx\n",
		 s_c_p.play_rate, s_c_p.rec_rate, s_c_p.play_format, s_c_p.rec_format);
	  printf("Play format: "); d_fmt(s_c_p.play_format);
	  printf("Rec format:  "); d_fmt(s_c_p.rec_format);
	}

}
#endif /* HAVE_{MACHINE_SOUNDCARD,SYS_SOUNDCARD}_H */
