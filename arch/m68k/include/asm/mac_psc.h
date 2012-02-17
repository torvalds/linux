/*
 * Apple Peripheral System Controller (PSC)
 *
 * The PSC is used on the AV Macs to control IO functions not handled
 * by the VIAs (Ethernet, DSP, SCC, Sound). This includes nine DMA
 * channels.
 *
 * The first seven DMA channels appear to be "one-shot" and are actually
 * sets of two channels; one member is active while the other is being
 * configured, and then you flip the active member and start all over again.
 * The one-shot channels are grouped together and are:
 *
 * 1. SCSI
 * 2. Ethernet Read
 * 3. Ethernet Write
 * 4. Floppy Disk Controller
 * 5. SCC Channel A Receive
 * 6. SCC Channel B Receive
 * 7. SCC Channel A Transmit
 *
 * The remaining two channels are handled somewhat differently. They appear
 * to be closely tied and share one set of registers. They also seem to run
 * continuously, although how you keep the buffer filled in this scenario is
 * not understood as there seems to be only one input and one output buffer
 * pointer.
 *
 * Much of this was extrapolated from what was known about the Ethernet
 * registers and subsequently confirmed using MacsBug (ie by pinging the
 * machine with easy-to-find patterns and looking for them in the DMA
 * buffers, or by sending a file over the serial ports and finding the
 * file in the buffers.)
 *
 * 1999-05-25 (jmt)
 */

#define PSC_BASE	(0x50F31000)

/*
 * The IER/IFR registers work like the VIA, except that it has 4
 * of them each on different interrupt levels, and each register
 * set only seems to handle four interrupts instead of seven.
 *
 * To access a particular set of registers, add 0xn0 to the base
 * where n = 3,4,5 or 6.
 */

#define pIFRbase	0x100
#define pIERbase	0x104

/*
 * One-shot DMA control registers
 */

#define PSC_MYSTERY	0x804

#define PSC_CTL_BASE	0xC00

#define PSC_SCSI_CTL	0xC00
#define PSC_ENETRD_CTL  0xC10
#define PSC_ENETWR_CTL  0xC20
#define PSC_FDC_CTL	0xC30
#define PSC_SCCA_CTL	0xC40
#define PSC_SCCB_CTL	0xC50
#define PSC_SCCATX_CTL	0xC60

/*
 * DMA channels. Add +0x10 for the second channel in the set.
 * You're supposed to use one channel while the other runs and
 * then flip channels and do the whole thing again.
 */

#define PSC_ADDR_BASE	0x1000
#define PSC_LEN_BASE	0x1004
#define PSC_CMD_BASE	0x1008

#define PSC_SET0	0x00
#define PSC_SET1	0x10

#define PSC_SCSI_ADDR	0x1000	/* confirmed */
#define PSC_SCSI_LEN	0x1004	/* confirmed */
#define PSC_SCSI_CMD	0x1008	/* confirmed */
#define PSC_ENETRD_ADDR 0x1020	/* confirmed */
#define PSC_ENETRD_LEN  0x1024	/* confirmed */
#define PSC_ENETRD_CMD  0x1028	/* confirmed */
#define PSC_ENETWR_ADDR 0x1040	/* confirmed */
#define PSC_ENETWR_LEN  0x1044	/* confirmed */
#define PSC_ENETWR_CMD  0x1048	/* confirmed */
#define PSC_FDC_ADDR	0x1060	/* strongly suspected */
#define PSC_FDC_LEN	0x1064	/* strongly suspected */
#define PSC_FDC_CMD	0x1068	/* strongly suspected */
#define PSC_SCCA_ADDR	0x1080	/* confirmed */
#define PSC_SCCA_LEN	0x1084	/* confirmed */
#define PSC_SCCA_CMD	0x1088	/* confirmed */
#define PSC_SCCB_ADDR	0x10A0	/* confirmed */
#define PSC_SCCB_LEN	0x10A4	/* confirmed */
#define PSC_SCCB_CMD	0x10A8	/* confirmed */
#define PSC_SCCATX_ADDR	0x10C0	/* confirmed */
#define PSC_SCCATX_LEN	0x10C4	/* confirmed */
#define PSC_SCCATX_CMD	0x10C8	/* confirmed */

/*
 * Free-running DMA registers. The only part known for sure are the bits in
 * the control register, the buffer addresses and the buffer length. Everything
 * else is anybody's guess.
 *
 * These registers seem to be mirrored every thirty-two bytes up until offset
 * 0x300. It's safe to assume then that a new set of registers starts there.
 */

#define PSC_SND_CTL	0x200	/*
				 * [ 16-bit ]
				 * Sound (Singer?) control register.
				 *
				 * bit 0  : ????
				 * bit 1  : ????
				 * bit 2  : Set to one to enable sound
				 *          output. Possibly a mute flag.
				 * bit 3  : ????
				 * bit 4  : ????
				 * bit 5  : ????
				 * bit 6  : Set to one to enable pass-thru
				 *          audio. In this mode the audio data
				 *          seems to appear in both the input
				 *          buffer and the output buffer.
				 * bit 7  : Set to one to activate the
				 *          sound input DMA or zero to
				 *          disable it.
				 * bit 8  : Set to one to activate the
				 *          sound output DMA or zero to
				 *          disable it.
				 * bit 9  : \
				 * bit 11 :  |
				 *          These two bits control the sample
				 *          rate. Usually set to binary 10 and
				 *	    MacOS 8.0 says I'm at 48 KHz. Using
				 *	    a binary value of 01 makes things
				 *	    sound about 1/2 speed (24 KHz?) and
				 *          binary 00 is slower still (22 KHz?)
				 *
				 * Setting this to 0x0000 is a good way to
				 * kill all DMA at boot time so that the
				 * PSC won't overwrite the kernel image
				 * with sound data.
				 */

/*
 * 0x0202 - 0x0203 is unused. Writing there
 * seems to clobber the control register.
 */

#define PSC_SND_SOURCE	0x204	/*
				 * [ 32-bit ]
				 * Controls input source and volume:
				 *
				 * bits 12-15 : input source volume, 0 - F
				 * bits 16-19 : unknown, always 0x5
				 * bits 20-23 : input source selection:
				 *                  0x3 = CD Audio
				 *                  0x4 = External Audio
				 *
				 * The volume is definitely not the general
				 * output volume as it doesn't affect the
				 * alert sound volume.
				 */
#define PSC_SND_STATUS1	0x208	/*
				 * [ 32-bit ]
				 * Appears to be a read-only status register.
				 * The usual value is 0x00400002.
				 */
#define PSC_SND_HUH3	0x20C	/*
				 * [ 16-bit ]
				 * Unknown 16-bit value, always 0x0000.
				 */
#define PSC_SND_BITS2GO	0x20E	/*
				 * [ 16-bit ]
				 * Counts down to zero from some constant
				 * value. The value appears to be the
				 * number of _bits_ remaining before the
				 * buffer is full, which would make sense
				 * since Apple's docs say the sound DMA
				 * channels are 1 bit wide.
				 */
#define PSC_SND_INADDR	0x210	/*
				 * [ 32-bit ]
				 * Address of the sound input DMA buffer
				 */
#define PSC_SND_OUTADDR	0x214	/*
				 * [ 32-bit ]
				 * Address of the sound output DMA buffer
				 */
#define PSC_SND_LEN	0x218	/*
				 * [ 16-bit ]
				 * Length of both buffers in eight-byte units.
				 */
#define PSC_SND_HUH4	0x21A	/*
				 * [ 16-bit ]
				 * Unknown, always 0x0000.
				 */
#define PSC_SND_STATUS2	0x21C	/*
				 * [ 16-bit ]
				 * Appears to e a read-only status register.
				 * The usual value is 0x0200.
				 */
#define PSC_SND_HUH5	0x21E	/*
				 * [ 16-bit ]
				 * Unknown, always 0x0000.
				 */

#ifndef __ASSEMBLY__

extern volatile __u8 *psc;
extern int psc_present;

extern void psc_register_interrupts(void);
extern void psc_irq_enable(int);
extern void psc_irq_disable(int);

/*
 *	Access functions
 */

static inline void psc_write_byte(int offset, __u8 data)
{
	*((volatile __u8 *)(psc + offset)) = data;
}

static inline void psc_write_word(int offset, __u16 data)
{
	*((volatile __u16 *)(psc + offset)) = data;
}

static inline void psc_write_long(int offset, __u32 data)
{
	*((volatile __u32 *)(psc + offset)) = data;
}

static inline u8 psc_read_byte(int offset)
{
	return *((volatile __u8 *)(psc + offset));
}

static inline u16 psc_read_word(int offset)
{
	return *((volatile __u16 *)(psc + offset));
}

static inline u32 psc_read_long(int offset)
{
	return *((volatile __u32 *)(psc + offset));
}

#endif /* __ASSEMBLY__ */
