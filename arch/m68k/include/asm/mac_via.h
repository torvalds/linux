/*
 *	6522 Versatile Interface Adapter (VIA)
 *
 *	There are two of these on the Mac II. Some IRQ's are vectored
 *	via them as are assorted bits and bobs - eg rtc, adb. The picture
 *	is a bit incomplete as the Mac documentation doesn't cover this well
 */

#ifndef _ASM_MAC_VIA_H_
#define _ASM_MAC_VIA_H_

/*
 * Base addresses for the VIAs. There are two in every machine,
 * although on some machines the second is an RBV or an OSS.
 * The OSS is different enough that it's handled separately.
 *
 * Do not use these values directly; use the via1 and via2 variables
 * instead (and don't forget to check rbv_present when using via2!)
 */

#define VIA1_BASE	(0x50F00000)
#define VIA2_BASE	(0x50F02000)
#define  RBV_BASE	(0x50F26000)

/*
 *	Not all of these are true post MacII I think.
 *      CSA: probably the ones CHRP marks as 'unused' change purposes
 *      when the IWM becomes the SWIM.
 *      http://www.rs6000.ibm.com/resource/technology/chrpio/via5.mak.html
 *      ftp://ftp.austin.ibm.com/pub/technology/spec/chrp/inwork/CHRP_IORef_1.0.pdf
 *
 * also, http://developer.apple.com/technotes/hw/hw_09.html claims the
 * following changes for IIfx:
 * VIA1A_vSccWrReq not available and that VIA1A_vSync has moved to an IOP.
 * Also, "All of the functionality of VIA2 has been moved to other chips".
 */

#define VIA1A_vSccWrReq	0x80	/* SCC write. (input)
				 * [CHRP] SCC WREQ: Reflects the state of the
				 * Wait/Request pins from the SCC.
				 * [Macintosh Family Hardware]
				 * as CHRP on SE/30,II,IIx,IIcx,IIci.
				 * on IIfx, "0 means an active request"
				 */
#define VIA1A_vRev8	0x40	/* Revision 8 board ???
                                 * [CHRP] En WaitReqB: Lets the WaitReq_L
				 * signal from port B of the SCC appear on
				 * the PA7 input pin. Output.
				 * [Macintosh Family] On the SE/30, this
				 * is the bit to flip screen buffers.
				 * 0=alternate, 1=main.
				 * on II,IIx,IIcx,IIci,IIfx this is a bit
				 * for Rev ID. 0=II,IIx, 1=IIcx,IIci,IIfx
				 */
#define VIA1A_vHeadSel	0x20	/* Head select for IWM.
				 * [CHRP] unused.
				 * [Macintosh Family] "Floppy disk
				 * state-control line SEL" on all but IIfx
				 */
#define VIA1A_vOverlay	0x10    /* [Macintosh Family] On SE/30,II,IIx,IIcx
				 * this bit enables the "Overlay" address
				 * map in the address decoders as it is on
				 * reset for mapping the ROM over the reset
				 * vector. 1=use overlay map.
				 * On the IIci,IIfx it is another bit of the
				 * CPU ID: 0=normal IIci, 1=IIci with parity
				 * feature or IIfx.
				 * [CHRP] En WaitReqA: Lets the WaitReq_L
				 * signal from port A of the SCC appear
				 * on the PA7 input pin (CHRP). Output.
				 * [MkLinux] "Drive Select"
				 *  (with 0x20 being 'disk head select')
				 */
#define VIA1A_vSync	0x08    /* [CHRP] Sync Modem: modem clock select:
                                 * 1: select the external serial clock to
				 *    drive the SCC's /RTxCA pin.
				 * 0: Select the 3.6864MHz clock to drive
				 *    the SCC cell.
				 * [Macintosh Family] Correct on all but IIfx
				 */

/* Macintosh Family Hardware sez: bits 0-2 of VIA1A are volume control
 * on Macs which had the PWM sound hardware.  Reserved on newer models.
 * On IIci,IIfx, bits 1-2 are the rest of the CPU ID:
 * bit 2: 1=IIci, 0=IIfx
 * bit 1: 1 on both IIci and IIfx.
 * MkLinux sez bit 0 is 'burnin flag' in this case.
 * CHRP sez: VIA1A bits 0-2 and 5 are 'unused': if programmed as
 * inputs, these bits will read 0.
 */
#define VIA1A_vVolume	0x07	/* Audio volume mask for PWM */
#define VIA1A_CPUID0	0x02	/* CPU id bit 0 on RBV, others */
#define VIA1A_CPUID1	0x04	/* CPU id bit 0 on RBV, others */
#define VIA1A_CPUID2	0x10	/* CPU id bit 0 on RBV, others */
#define VIA1A_CPUID3	0x40	/* CPU id bit 0 on RBV, others */

/* Info on VIA1B is from Macintosh Family Hardware & MkLinux.
 * CHRP offers no info. */
#define VIA1B_vSound	0x80	/* Sound enable (for compatibility with
				 * PWM hardware) 0=enabled.
				 * Also, on IIci w/parity, shows parity error
				 * 0=error, 1=OK. */
#define VIA1B_vMystery	0x40    /* On IIci, parity enable. 0=enabled,1=disabled
				 * On SE/30, vertical sync interrupt enable.
				 * 0=enabled. This vSync interrupt shows up
				 * as a slot $E interrupt. */
#define VIA1B_vADBS2	0x20	/* ADB state input bit 1 (unused on IIfx) */
#define VIA1B_vADBS1	0x10	/* ADB state input bit 0 (unused on IIfx) */
#define VIA1B_vADBInt	0x08	/* ADB interrupt 0=interrupt (unused on IIfx)*/
#define VIA1B_vRTCEnb	0x04	/* Enable Real time clock. 0=enabled. */
#define VIA1B_vRTCClk	0x02    /* Real time clock serial-clock line. */
#define VIA1B_vRTCData	0x01    /* Real time clock serial-data line. */

/* MkLinux defines the following "VIA1 Register B contents where they
 * differ from standard VIA1".  From the naming scheme, we assume they
 * correspond to a VIA work-alike named 'EVR'. */
#define	EVRB_XCVR	0x08	/* XCVR_SESSION* */
#define	EVRB_FULL	0x10	/* VIA_FULL */
#define	EVRB_SYSES	0x20	/* SYS_SESSION */
#define	EVRB_AUXIE	0x00	/* Enable A/UX Interrupt Scheme */
#define	EVRB_AUXID	0x40	/* Disable A/UX Interrupt Scheme */
#define	EVRB_SFTWRIE	0x00	/* Software Interrupt ReQuest */
#define	EVRB_SFTWRID	0x80	/* Software Interrupt ReQuest */

/*
 *	VIA2 A register is the interrupt lines raised off the nubus
 *	slots.
 *      The below info is from 'Macintosh Family Hardware.'
 *      MkLinux calls the 'IIci internal video IRQ' below the 'RBV slot 0 irq.'
 *      It also notes that the slot $9 IRQ is the 'Ethernet IRQ' and
 *      defines the 'Video IRQ' as 0x40 for the 'EVR' VIA work-alike.
 *      Perhaps OSS uses vRAM1 and vRAM2 for ADB.
 */

#define VIA2A_vRAM1	0x80	/* RAM size bit 1 (IIci: reserved) */
#define VIA2A_vRAM0	0x40	/* RAM size bit 0 (IIci: internal video IRQ) */
#define VIA2A_vIRQE	0x20	/* IRQ from slot $E */
#define VIA2A_vIRQD	0x10	/* IRQ from slot $D */
#define VIA2A_vIRQC	0x08	/* IRQ from slot $C */
#define VIA2A_vIRQB	0x04	/* IRQ from slot $B */
#define VIA2A_vIRQA	0x02	/* IRQ from slot $A */
#define VIA2A_vIRQ9	0x01	/* IRQ from slot $9 */

/* RAM size bits decoded as follows:
 * bit1 bit0  size of ICs in bank A
 *  0    0    256 kbit
 *  0    1    1 Mbit
 *  1    0    4 Mbit
 *  1    1   16 Mbit
 */

/*
 *	Register B has the fun stuff in it
 */

#define VIA2B_vVBL	0x80	/* VBL output to VIA1 (60.15Hz) driven by
				 * timer T1.
				 * on IIci, parity test: 0=test mode.
				 * [MkLinux] RBV_PARODD: 1=odd,0=even. */
#define VIA2B_vSndJck	0x40	/* External sound jack status.
				 * 0=plug is inserted.  On SE/30, always 0 */
#define VIA2B_vTfr0	0x20	/* Transfer mode bit 0 ack from NuBus */
#define VIA2B_vTfr1	0x10	/* Transfer mode bit 1 ack from NuBus */
#define VIA2B_vMode32	0x08	/* 24/32bit switch - doubles as cache flush
				 * on II, AMU/PMMU control.
				 *   if AMU, 0=24bit to 32bit translation
				 *   if PMMU, 1=PMMU is accessing page table.
				 * on SE/30 tied low.
				 * on IIx,IIcx,IIfx, unused.
				 * on IIci/RBV, cache control. 0=flush cache.
				 */
#define VIA2B_vPower	0x04	/* Power off, 0=shut off power.
				 * on SE/30 this signal sent to PDS card. */
#define VIA2B_vBusLk	0x02	/* Lock NuBus transactions, 0=locked.
				 * on SE/30 sent to PDS card. */
#define VIA2B_vCDis	0x01	/* Cache control. On IIci, 1=disable cache card
				 * on others, 0=disable processor's instruction
				 * and data caches. */

/* Apple sez: http://developer.apple.com/technotes/ov/ov_04.html
 * Another example of a valid function that has no ROM support is the use
 * of the alternate video page for page-flipping animation. Since there
 * is no ROM call to flip pages, it is necessary to go play with the
 * right bit in the VIA chip (6522 Versatile Interface Adapter).
 * [CSA: don't know which one this is, but it's one of 'em!]
 */

/*
 *	6522 registers - see databook.
 * CSA: Assignments for VIA1 confirmed from CHRP spec.
 */

/* partial address decode.  0xYYXX : XX part for RBV, YY part for VIA */
/* Note: 15 VIA regs, 8 RBV regs */

#define vBufB	0x0000	/* [VIA/RBV]  Register B */
#define vBufAH	0x0200  /* [VIA only] Buffer A, with handshake. DON'T USE! */
#define vDirB	0x0400  /* [VIA only] Data Direction Register B. */
#define vDirA	0x0600  /* [VIA only] Data Direction Register A. */
#define vT1CL	0x0800  /* [VIA only] Timer one counter low. */
#define vT1CH	0x0a00  /* [VIA only] Timer one counter high. */
#define vT1LL	0x0c00  /* [VIA only] Timer one latches low. */
#define vT1LH	0x0e00  /* [VIA only] Timer one latches high. */
#define vT2CL	0x1000  /* [VIA only] Timer two counter low. */
#define vT2CH	0x1200  /* [VIA only] Timer two counter high. */
#define vSR	0x1400  /* [VIA only] Shift register. */
#define vACR	0x1600  /* [VIA only] Auxiliary control register. */
#define vPCR	0x1800  /* [VIA only] Peripheral control register. */
                        /*            CHRP sez never ever to *write* this.
			 *            Mac family says never to *change* this.
			 * In fact we need to initialize it once at start. */
#define vIFR	0x1a00  /* [VIA/RBV]  Interrupt flag register. */
#define vIER	0x1c00  /* [VIA/RBV]  Interrupt enable register. */
#define vBufA	0x1e00  /* [VIA/RBV] register A (no handshake) */

/* The RBV only decodes the bottom eight address lines; the VIA doesn't
 * decode the bottom eight -- so vBufB | rBufB will always get you BufB */
/* CSA: in fact, only bits 0,1, and 4 seem to be decoded.
 * BUT note the values for rIER and rIFR, where the top 8 bits *do* seem
 * to matter.  In fact *all* of the top 8 bits seem to matter;
 * setting rIER=0x1813 and rIFR=0x1803 doesn't work, either.
 * Perhaps some sort of 'compatibility mode' is built-in? [21-May-1999]
 */

#define rBufB   0x0000  /* [VIA/RBV]  Register B */
#define rExp	0x0001	/* [RBV only] RBV future expansion (always 0) */
#define rSIFR	0x0002  /* [RBV only] RBV slot interrupts register. */
#define rIFR	0x1a03  /* [VIA/RBV]  RBV interrupt flag register. */
#define rMonP   0x0010  /* [RBV only] RBV video monitor type. */
#define rChpT   0x0011  /* [RBV only] RBV test mode register (reads as 0). */
#define rSIER   0x0012  /* [RBV only] RBV slot interrupt enables. */
#define rIER    0x1c13  /* [VIA/RBV]  RBV interrupt flag enable register. */
#define rBufA	rSIFR   /* the 'slot interrupts register' is BufA on a VIA */

/*
 * Video monitor parameters, for rMonP:
 */
#define RBV_DEPTH  0x07	/* bits per pixel: 000=1,001=2,010=4,011=8 */
#define RBV_MONID  0x38	/* monitor type, as below. */
#define RBV_VIDOFF 0x40	/* 1 turns off onboard video */
/* Supported monitor types: */
#define MON_15BW   (1<<3) /* 15" BW portrait. */
#define MON_IIGS   (2<<3) /* 12" color (modified IIGS monitor). */
#define MON_15RGB  (5<<3) /* 15" RGB portrait. */
#define MON_12OR13 (6<<3) /* 12" BW or 13" RGB. */
#define MON_NONE   (7<<3) /* No monitor attached. */

/* To clarify IER manipulations */
#define IER_SET_BIT(b) (0x80 | (1<<(b)) )
#define IER_CLR_BIT(b) (0x7F & (1<<(b)) )

#ifndef __ASSEMBLY__

extern volatile __u8 *via1,*via2;
extern int rbv_present,via_alt_mapping;

struct irq_desc;

extern void via_register_interrupts(void);
extern void via_irq_enable(int);
extern void via_irq_disable(int);
extern void via_nubus_irq_startup(int irq);
extern void via_nubus_irq_shutdown(int irq);
extern void via1_irq(unsigned int irq, struct irq_desc *desc);
extern void via1_set_head(int);
extern int via2_scsi_drq_pending(void);

static inline int rbv_set_video_bpp(int bpp)
{
	char val = (bpp==1)?0:(bpp==2)?1:(bpp==4)?2:(bpp==8)?3:-1;
	if (!rbv_present || val<0) return -1;
	via2[rMonP] = (via2[rMonP] & ~RBV_DEPTH) | val;
	return 0;
}

#endif /* __ASSEMBLY__ */

#endif /* _ASM_MAC_VIA_H_ */
