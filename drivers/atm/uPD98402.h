/* drivers/atm/uPD98402.h - NEC uPD98402 (PHY) declarations */
 
/* Written 1995 by Werner Almesberger, EPFL LRC */


#ifndef DRIVERS_ATM_uPD98402_H
#define DRIVERS_ATM_uPD98402_H

/*
 * Registers
 */

#define uPD98402_CMR		0x00	/* Command Register */
#define uPD98402_MDR		0x01	/* Mode Register */
#define uPD98402_PICR		0x02	/* PHY Interrupt Cause Register */
#define uPD98402_PIMR		0x03	/* PHY Interrupt Mask Register */
#define uPD98402_ACR		0x04	/* Alarm Cause Register */
#define uPD98402_ACMR		0x05	/* Alarm Cause Mask Register */
#define uPD98402_PCR		0x06	/* Performance Cause Register */
#define uPD98402_PCMR		0x07	/* Performance Cause Mask Register */
#define uPD98402_IACM		0x08	/* Internal Alarm Cause Mask Register */
#define uPD98402_B1ECT		0x09	/* B1 Error Count Register */
#define uPD98402_B2ECT		0x0a	/* B2 Error Count Register */
#define uPD98402_B3ECT		0x0b	/* B3 Error Count Regster */
#define uPD98402_PFECB		0x0c	/* Path FEBE Count Register */
#define uPD98402_LECCT		0x0d	/* Line FEBE Count Register */
#define uPD98402_HECCT		0x0e	/* HEC Error Count Register */
#define uPD98402_FJCT		0x0f	/* Frequence Justification Count Reg */
#define uPD98402_PCOCR		0x10	/* Perf. Counter Overflow Cause Reg */
#define uPD98402_PCOMR		0x11	/* Perf. Counter Overflow Mask Reg */
#define uPD98402_C11T		0x20	/* C11T Data Register */
#define uPD98402_C12T		0x21	/* C12T Data Register */
#define uPD98402_C13T		0x22	/* C13T Data Register */
#define uPD98402_F1T		0x23	/* F1T Data Register */
#define uPD98402_K2T		0x25	/* K2T Data Register */
#define uPD98402_C2T		0x26	/* C2T Data Register */
#define uPD98402_F2T		0x27	/* F2T Data Register */
#define uPD98402_C11R		0x30	/* C11T Data Register */
#define uPD98402_C12R		0x31	/* C12T Data Register */
#define uPD98402_C13R		0x32	/* C13T Data Register */
#define uPD98402_F1R		0x33	/* F1T Data Register */
#define uPD98402_K2R		0x35	/* K2T Data Register */
#define uPD98402_C2R		0x36	/* C2T Data Register */
#define uPD98402_F2R		0x37	/* F2T Data Register */

/* CMR is at 0x00 */
#define uPD98402_CMR_PFRF	0x01	/* Send path FERF */
#define uPD98402_CMR_LFRF	0x02	/* Send line FERF */
#define uPD98402_CMR_PAIS	0x04	/* Send path AIS */
#define uPD98402_CMR_LAIS	0x08	/* Send line AIS */

/* MDR is at 0x01 */
#define uPD98402_MDR_ALP	0x01	/* ATM layer loopback */
#define uPD98402_MDR_TPLP	0x02	/* PMD loopback, to host */
#define uPD98402_MDR_RPLP	0x04	/* PMD loopback, to network */
#define uPD98402_MDR_SS0	0x08	/* SS0 */
#define uPD98402_MDR_SS1	0x10	/* SS1 */
#define uPD98402_MDR_SS_MASK	0x18	/* mask */
#define uPD98402_MDR_SS_SHIFT	3	/* shift */
#define uPD98402_MDR_HEC	0x20	/* disable HEC inbound processing */
#define uPD98402_MDR_FSR	0x40	/* disable frame scrambler */
#define uPD98402_MDR_CSR	0x80	/* disable cell scrambler */

/* PICR is at 0x02, PIMR is at 0x03 */
#define uPD98402_INT_PFM	0x01	/* performance counter has changed */
#define uPD98402_INT_ALM	0x02	/* line fault */
#define uPD98402_INT_RFO	0x04	/* receive FIFO overflow */
#define uPD98402_INT_PCO	0x08	/* performance counter overflow */
#define uPD98402_INT_OTD	0x20	/* OTD has occurred */
#define uPD98402_INT_LOS	0x40	/* Loss Of Signal */
#define uPD98402_INT_LOF	0x80	/* Loss Of Frame */

/* ACR is as 0x04, ACMR is at 0x05 */
#define uPD98402_ALM_PFRF	0x01	/* path FERF */
#define uPD98402_ALM_LFRF	0x02	/* line FERF */
#define uPD98402_ALM_PAIS	0x04	/* path AIS */
#define uPD98402_ALM_LAIS	0x08	/* line AIS */
#define uPD98402_ALM_LOD	0x10	/* loss of delineation */
#define uPD98402_ALM_LOP	0x20	/* loss of pointer */
#define uPD98402_ALM_OOF	0x40	/* out of frame */

/* PCR is at 0x06, PCMR is at 0x07 */
#define uPD98402_PFM_PFEB	0x01	/* path FEBE */
#define uPD98402_PFM_LFEB	0x02	/* line FEBE */
#define uPD98402_PFM_B3E	0x04	/* B3 error */
#define uPD98402_PFM_B2E	0x08	/* B2 error */
#define uPD98402_PFM_B1E	0x10	/* B1 error */
#define uPD98402_PFM_FJ		0x20	/* frequency justification */

/* IACM is at 0x08 */
#define uPD98402_IACM_PFRF	0x01	/* don't generate path FERF */
#define uPD98402_IACM_LFRF	0x02	/* don't generate line FERF */

/* PCOCR is at 0x010, PCOMR is at 0x11 */
#define uPD98402_PCO_B1EC	0x01	/* B1ECT overflow */
#define uPD98402_PCO_B2EC	0x02	/* B2ECT overflow */
#define uPD98402_PCO_B3EC	0x04	/* B3ECT overflow */
#define uPD98402_PCO_PFBC	0x08	/* PFEBC overflow */
#define uPD98402_PCO_LFBC	0x10	/* LFEVC overflow */
#define uPD98402_PCO_HECC	0x20	/* HECCT overflow */
#define uPD98402_PCO_FJC	0x40	/* FJCT overflow */


int uPD98402_init(struct atm_dev *dev);

#endif
