#ifndef DDK750_DISPLAY_H__
#define DDK750_DISPLAY_H__

/*
 * panel path select
 *	80000[29:28]
 */

#define PNL_2_OFFSET 0
#define PNL_2_MASK (3 << PNL_2_OFFSET)
#define PNL_2_USAGE	(PNL_2_MASK << 16)
#define PNL_2_PRI	((0 << PNL_2_OFFSET) | PNL_2_USAGE)
#define PNL_2_SEC	((2 << PNL_2_OFFSET) | PNL_2_USAGE)

/*
 * primary timing & plane enable bit
 *	1: 80000[8] & 80000[2] on
 *	0: both off
 */
#define PRI_TP_OFFSET 4
#define PRI_TP_MASK BIT(PRI_TP_OFFSET)
#define PRI_TP_USAGE (PRI_TP_MASK << 16)
#define PRI_TP_ON ((0x1 << PRI_TP_OFFSET) | PRI_TP_USAGE)
#define PRI_TP_OFF ((0x0 << PRI_TP_OFFSET) | PRI_TP_USAGE)

/*
 * panel sequency status
 *	80000[27:24]
 */
#define PNL_SEQ_OFFSET 6
#define PNL_SEQ_MASK BIT(PNL_SEQ_OFFSET)
#define PNL_SEQ_USAGE (PNL_SEQ_MASK << 16)
#define PNL_SEQ_ON (BIT(PNL_SEQ_OFFSET) | PNL_SEQ_USAGE)
#define PNL_SEQ_OFF ((0 << PNL_SEQ_OFFSET) | PNL_SEQ_USAGE)

/*
 * dual digital output
 *	80000[19]
 */
#define DUAL_TFT_OFFSET 8
#define DUAL_TFT_MASK BIT(DUAL_TFT_OFFSET)
#define DUAL_TFT_USAGE (DUAL_TFT_MASK << 16)
#define DUAL_TFT_ON (BIT(DUAL_TFT_OFFSET) | DUAL_TFT_USAGE)
#define DUAL_TFT_OFF ((0 << DUAL_TFT_OFFSET) | DUAL_TFT_USAGE)

/*
 * secondary timing & plane enable bit
 *	1:80200[8] & 80200[2] on
 *	0: both off
 */
#define SEC_TP_OFFSET 5
#define SEC_TP_MASK BIT(SEC_TP_OFFSET)
#define SEC_TP_USAGE (SEC_TP_MASK << 16)
#define SEC_TP_ON  ((0x1 << SEC_TP_OFFSET) | SEC_TP_USAGE)
#define SEC_TP_OFF ((0x0 << SEC_TP_OFFSET) | SEC_TP_USAGE)

/*
 * crt path select
 *	80200[19:18]
 */
#define CRT_2_OFFSET 2
#define CRT_2_MASK (3 << CRT_2_OFFSET)
#define CRT_2_USAGE (CRT_2_MASK << 16)
#define CRT_2_PRI ((0x0 << CRT_2_OFFSET) | CRT_2_USAGE)
#define CRT_2_SEC ((0x2 << CRT_2_OFFSET) | CRT_2_USAGE)

/*
 * DAC affect both DVI and DSUB
 *	4[20]
 */
#define DAC_OFFSET 7
#define DAC_MASK BIT(DAC_OFFSET)
#define DAC_USAGE (DAC_MASK << 16)
#define DAC_ON ((0x0 << DAC_OFFSET) | DAC_USAGE)
#define DAC_OFF ((0x1 << DAC_OFFSET) | DAC_USAGE)

/*
 * DPMS only affect D-SUB head
 *	0[31:30]
 */
#define DPMS_OFFSET 9
#define DPMS_MASK (3 << DPMS_OFFSET)
#define DPMS_USAGE (DPMS_MASK << 16)
#define DPMS_OFF ((3 << DPMS_OFFSET) | DPMS_USAGE)
#define DPMS_ON ((0 << DPMS_OFFSET) | DPMS_USAGE)

/*
 * LCD1 means panel path TFT1  & panel path DVI (so enable DAC)
 * CRT means crt path DSUB
 */
typedef enum _disp_output_t {
	do_LCD1_PRI = PNL_2_PRI | PRI_TP_ON | PNL_SEQ_ON | DAC_ON,
	do_LCD1_SEC = PNL_2_SEC | SEC_TP_ON | PNL_SEQ_ON | DAC_ON,
	do_LCD2_PRI = CRT_2_PRI | PRI_TP_ON | DUAL_TFT_ON,
	do_LCD2_SEC = CRT_2_SEC | SEC_TP_ON | DUAL_TFT_ON,
	/*
	 * do_DSUB_PRI = CRT_2_PRI | PRI_TP_ON | DPMS_ON|DAC_ON,
	 * do_DSUB_SEC = CRT_2_SEC | SEC_TP_ON | DPMS_ON|DAC_ON,
	 */
	do_CRT_PRI = CRT_2_PRI | PRI_TP_ON | DPMS_ON | DAC_ON,
	do_CRT_SEC = CRT_2_SEC | SEC_TP_ON | DPMS_ON | DAC_ON,
}
disp_output_t;

void ddk750_setLogicalDispOut(disp_output_t output);

#endif
