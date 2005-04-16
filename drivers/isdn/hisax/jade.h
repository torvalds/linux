/* $Id: jade.h,v 1.5.2.3 2004/01/14 16:04:48 keil Exp $
 *
 * JADE specific defines
 *
 * Author       Roland Klabunde
 * Copyright    by Roland Klabunde   <R.Klabunde@Berkom.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

/* All Registers original Siemens Spec  */
#ifndef	__JADE_H__
#define	__JADE_H__

/* Special registers for access to indirect accessible JADE regs */
#define	DIRECT_IO_JADE	0x0000	/* Jade direct io access area */
#define	COMM_JADE	0x0040	/* Jade communication area */	   	

/********************************************************************/
/* JADE-HDLC registers         									    */
/********************************************************************/
#define jade_HDLC_RFIFO	   				0x00				   /* R */
#define jade_HDLC_XFIFO	   				0x00				   /* W */

#define	jade_HDLC_STAR	   				0x20				   /* R */
	#define	jadeSTAR_XDOV				0x80
	#define	jadeSTAR_XFW 				0x40 /* Does not work*/
	#define	jadeSTAR_XCEC 				0x20
	#define	jadeSTAR_RCEC				0x10
	#define	jadeSTAR_BSY 				0x08
	#define	jadeSTAR_RNA 				0x04
	#define	jadeSTAR_STR 				0x02
	#define	jadeSTAR_STX				0x01

#define	jade_HDLC_XCMD	   				0x20				   /* W */
	#define	jadeXCMD_XF				0x80
	#define	jadeXCMD_XME				0x40
	#define	jadeXCMD_XRES				0x20
	#define	jadeXCMD_STX				0x01

#define	jade_HDLC_RSTA	   				0x21				   /* R */
    #define	jadeRSTA_VFR				0x80
    #define	jadeRSTA_RDO				0x40
    #define	jadeRSTA_CRC				0x20
    #define	jadeRSTA_RAB				0x10
    #define	jadeRSTA_MASK			   	0xF0

#define	jade_HDLC_MODE					0x22				   /* RW*/
    #define	jadeMODE_TMO				0x80
    #define	jadeMODE_RAC				0x40
    #define	jadeMODE_XAC				0x20
    #define	jadeMODE_TLP				0x10
    #define	jadeMODE_ERFS				0x02
    #define	jadeMODE_ETFS				0x01

#define	jade_HDLC_RBCH					0x24				   /* R */

#define	jade_HDLC_RBCL	 				0x25				   /* R */
#define	jade_HDLC_RCMD	 				0x25				   /* W */
	#define	jadeRCMD_RMC 				0x80
	#define	jadeRCMD_RRES				0x40
	#define	jadeRCMD_RMD				0x20
	#define	jadeRCMD_STR				0x02

#define	jade_HDLC_CCR0					0x26				   /* RW*/
	#define	jadeCCR0_PU  				0x80
	#define	jadeCCR0_ITF				0x40
	#define	jadeCCR0_C32				0x20
	#define	jadeCCR0_CRL				0x10
	#define	jadeCCR0_RCRC				0x08
	#define	jadeCCR0_XCRC				0x04
	#define	jadeCCR0_RMSB				0x02
	#define	jadeCCR0_XMSB				0x01

#define	jade_HDLC_CCR1					0x27				   /* RW*/
    #define	jadeCCR1_RCS0				0x80
    #define	jadeCCR1_RCONT				0x40
    #define	jadeCCR1_RFDIS				0x20
    #define	jadeCCR1_XCS0				0x10
    #define	jadeCCR1_XCONT				0x08
    #define	jadeCCR1_XFDIS				0x04

#define	jade_HDLC_TSAR					0x28				   /* RW*/
#define	jade_HDLC_TSAX					0x29				   /* RW*/
#define	jade_HDLC_RCCR					0x2A				   /* RW*/
#define	jade_HDLC_XCCR					0x2B				   /* RW*/

#define	jade_HDLC_ISR 					0x2C				   /* R */
#define	jade_HDLC_IMR 					0x2C				   /* W */
	#define	jadeISR_RME					0x80
	#define	jadeISR_RPF					0x40
	#define	jadeISR_RFO					0x20
	#define	jadeISR_XPR					0x10
	#define	jadeISR_XDU					0x08
	#define	jadeISR_ALLS				0x04

#define jade_INT            			0x75
    #define jadeINT_HDLC1   			0x02
    #define jadeINT_HDLC2   			0x01
    #define jadeINT_DSP				0x04
#define jade_INTR            			0x70

/********************************************************************/
/* Indirect accessible JADE registers of common interest		   	*/
/********************************************************************/
#define	jade_CHIPVERSIONNR				0x00 /* Does not work*/

#define	jade_HDLCCNTRACCESS				0x10		
	#define	jadeINDIRECT_HAH1			0x02
	#define	jadeINDIRECT_HAH2			0x01

#define	jade_HDLC1SERRXPATH				0x1D
#define	jade_HDLC1SERTXPATH				0x1E
#define	jade_HDLC2SERRXPATH				0x1F
#define	jade_HDLC2SERTXPATH				0x20
	#define	jadeINDIRECT_SLIN1			0x10
	#define	jadeINDIRECT_SLIN0			0x08
	#define	jadeINDIRECT_LMOD1			0x04
	#define	jadeINDIRECT_LMOD0			0x02
	#define	jadeINDIRECT_HHR			0x01
	#define	jadeINDIRECT_HHX			0x01

#define	jade_RXAUDIOCH1CFG				0x11
#define	jade_RXAUDIOCH2CFG				0x14
#define	jade_TXAUDIOCH1CFG				0x17
#define	jade_TXAUDIOCH2CFG				0x1A

extern int JadeVersion(struct IsdnCardState *cs, char *s);
extern void modejade(struct BCState *bcs, int mode, int bc);
extern void clear_pending_jade_ints(struct IsdnCardState *cs);
extern void initjade(struct IsdnCardState *cs);

#endif	/* __JADE_H__ */
