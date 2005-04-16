/* $Id: isdn_audio.h,v 1.1.2.2 2004/01/12 22:37:18 keil Exp $
 *
 * Linux ISDN subsystem, audio conversion and compression (linklevel).
 *
 * Copyright 1994-1999 by Fritz Elfert (fritz@isdn4linux.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#define DTMF_NPOINTS 205        /* Number of samples for DTMF recognition */
typedef struct adpcm_state {
	int a;
	int d;
	int word;
	int nleft;
	int nbits;
} adpcm_state;

typedef struct dtmf_state {
	char last;
	char llast;
	int idx;
	int buf[DTMF_NPOINTS];
} dtmf_state;

typedef struct silence_state {
	int state;
	unsigned int idx;
} silence_state;

extern void isdn_audio_ulaw2alaw(unsigned char *, unsigned long);
extern void isdn_audio_alaw2ulaw(unsigned char *, unsigned long);
extern adpcm_state *isdn_audio_adpcm_init(adpcm_state *, int);
extern int isdn_audio_adpcm2xlaw(adpcm_state *, int, unsigned char *, unsigned char *, int);
extern int isdn_audio_xlaw2adpcm(adpcm_state *, int, unsigned char *, unsigned char *, int);
extern int isdn_audio_2adpcm_flush(adpcm_state * s, unsigned char *out);
extern void isdn_audio_calc_dtmf(modem_info *, unsigned char *, int, int);
extern void isdn_audio_eval_dtmf(modem_info *);
dtmf_state *isdn_audio_dtmf_init(dtmf_state *);
extern void isdn_audio_calc_silence(modem_info *, unsigned char *, int, int);
extern void isdn_audio_eval_silence(modem_info *);
silence_state *isdn_audio_silence_init(silence_state *);
extern void isdn_audio_put_dle_code(modem_info *, u_char);
