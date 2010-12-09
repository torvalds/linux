/* speakup_acntpc.h - header file for speakups Accent-PC driver. */

#define SYNTH_IO_EXTENT	0x02

#define SYNTH_CLEAR	0x18		/* stops speech */

	/* Port Status Flags */
#define SYNTH_READABLE	0x01	/* mask for bit which is nonzero if a
				   byte can be read from the data port */
#define SYNTH_WRITABLE	0x02	/* mask for RDY bit, which when set to
				   1, indicates the data port is ready
				   to accept a byte of data. */
#define SYNTH_QUIET	'S' /* synth is not speaking */
#define SYNTH_FULL	'F' /* synth is full. */
#define SYNTH_ALMOST_EMPTY 'M' /* synth has les than 2 seconds of text left */
#define SYNTH_SPEAKING	's' /* synth is speaking and has a fare way to go */
