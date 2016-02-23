/* speakup_dtlk.h - header file for speakups DoubleTalk driver. */

#define SYNTH_IO_EXTENT	0x02
#define SYNTH_CLEAR	0x18		/* stops speech */
	/* TTS Port Status Flags */
#define TTS_READABLE	0x80	/* mask for bit which is nonzero if a
				 * byte can be read from the TTS port
				 */
#define TTS_SPEAKING	0x40	/* mask for SYNC bit, which is nonzero
				 * while DoubleTalk is producing
				 * output with TTS, PCM or CVSD
				 * synthesizers or tone generators
				 * (that is, all but LPC)
				 */
#define TTS_SPEAKING2	0x20	/* mask for SYNC2 bit,
				 * which falls to zero up to 0.4 sec
				 * before speech stops
				 */
#define TTS_WRITABLE	0x10	/* mask for RDY bit, which when set to
				 * 1, indicates the TTS port is ready
				 * to accept a byte of data.  The RDY
				 * bit goes zero 2-3 usec after
				 * writing, and goes 1 again 180-190
				 * usec later.
				 */
#define TTS_ALMOST_FULL	0x08	/* mask for AF bit: When set to 1,
					 * indicates that less than 300 bytes
					 * are available in the TTS input
					 * buffer. AF is always 0 in the PCM,
					 * TGN and CVSD modes.
					 */
#define TTS_ALMOST_EMPTY 0x04	/* mask for AE bit: When set to 1,
				 * indicates that less than 300 bytes
				 * are remaining in DoubleTalk's input
				 * (TTS or PCM) buffer. AE is always 1
				 * in the TGN and CVSD modes.
				 */

				/* data returned by Interrogate command */
struct synth_settings {
	u_short serial_number;	/* 0-7Fh:0-7Fh */
	u_char rom_version[24]; /* null terminated string */
	u_char mode;		/* 0=Character; 1=Phoneme; 2=Text */
	u_char punc_level;	/* nB; 0-7 */
	u_char formant_freq;	/* nF; 0-9 */
	u_char pitch;		/* nP; 0-99 */
	u_char speed;		/* nS; 0-9 */
	u_char volume;		/* nV; 0-9 */
	u_char tone;		/* nX; 0-2 */
	u_char expression;	/* nE; 0-9 */
	u_char ext_dict_loaded; /* 1=exception dictionary loaded */
	u_char ext_dict_status; /* 1=exception dictionary enabled */
	u_char free_ram;	/* # pages (truncated) remaining for
				 * text buffer
				 */
	u_char articulation;	/* nA; 0-9 */
	u_char reverb;		/* nR; 0-9 */
	u_char eob;		/* 7Fh value indicating end of
				 * parameter block
				 */
	u_char has_indexing;	/* nonzero if indexing is implemented */
};
