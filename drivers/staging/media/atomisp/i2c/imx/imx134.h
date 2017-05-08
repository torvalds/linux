#ifndef __IMX134_H__
#define __IMX134_H__

/********************** imx134 setting - version 1 *********************/
static struct imx_reg const imx134_init_settings[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* Basic settings */
	{ IMX_8BIT, 0x0105, 0x01 },
	{ IMX_8BIT, 0x0220, 0x01 },
	{ IMX_8BIT, 0x3302, 0x11 },
	{ IMX_8BIT, 0x3833, 0x20 },
	{ IMX_8BIT, 0x3893, 0x00 },
	{ IMX_8BIT, 0x3906, 0x08 },
	{ IMX_8BIT, 0x3907, 0x01 },
	{ IMX_8BIT, 0x391B, 0x01 },
	{ IMX_8BIT, 0x3C09, 0x01 },
	{ IMX_8BIT, 0x600A, 0x00 },

	/* Analog settings */
	{ IMX_8BIT, 0x3008, 0xB0 },
	{ IMX_8BIT, 0x320A, 0x01 },
	{ IMX_8BIT, 0x320D, 0x10 },
	{ IMX_8BIT, 0x3216, 0x2E },
	{ IMX_8BIT, 0x322C, 0x02 },
	{ IMX_8BIT, 0x3409, 0x0C },
	{ IMX_8BIT, 0x340C, 0x2D },
	{ IMX_8BIT, 0x3411, 0x39 },
	{ IMX_8BIT, 0x3414, 0x1E },
	{ IMX_8BIT, 0x3427, 0x04 },
	{ IMX_8BIT, 0x3480, 0x1E },
	{ IMX_8BIT, 0x3484, 0x1E },
	{ IMX_8BIT, 0x3488, 0x1E },
	{ IMX_8BIT, 0x348C, 0x1E },
	{ IMX_8BIT, 0x3490, 0x1E },
	{ IMX_8BIT, 0x3494, 0x1E },
	{ IMX_8BIT, 0x3511, 0x8F },
	{ IMX_8BIT, 0x3617, 0x2D },

	GROUPED_PARAMETER_HOLD_DISABLE,
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane 3280x2464 8M 30fps, vendor provide */
static struct imx_reg const imx134_8M_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* clock setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x00 },
	{ IMX_8BIT, 0x0391, 0x11 },
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x00 },
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 }, /* down scaling 16/16 = 1 */
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /*	x_addr_start[15:8]:0	*/
	{ IMX_8BIT, 0x0345, 0x00 },      /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x00 },      /*	y_addr_start[15:8]:0	*/
	{ IMX_8BIT, 0x0347, 0x00 },      /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0C },      /*	x_addr_end[15:8]:3279	*/
	{ IMX_8BIT, 0x0349, 0xCF },      /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x09 },      /*	y_addr_end[15:8]:2463	*/
	{ IMX_8BIT, 0x034B, 0x9F },      /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x0C },      /*	x_output_size[15:8]: 3280*/
	{ IMX_8BIT, 0x034D, 0xD0 },      /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x09 },      /*	y_output_size[15:8]:2464 */
	{ IMX_8BIT, 0x034F, 0xA0 },      /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x0C },
	{ IMX_8BIT, 0x0355, 0xD0 },
	{ IMX_8BIT, 0x0356, 0x09 },
	{ IMX_8BIT, 0x0357, 0xA0 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x0C },
	{ IMX_8BIT, 0x3311, 0xD0 },
	{ IMX_8BIT, 0x3312, 0x09 },
	{ IMX_8BIT, 0x3313, 0xA0 },
	{ IMX_8BIT, 0x331C, 0x01 },
	{ IMX_8BIT, 0x331D, 0xAE },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global timing setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration time setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane, 1/2 binning 30fps 1640x1232, vendor provide */
static struct imx_reg const imx134_1640_1232_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning */
	{ IMX_8BIT, 0x0391, 0x22 },	/* 2x2 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x00 },	/* no resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 },
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /* x_addr_start[15:8]:0 */
	{ IMX_8BIT, 0x0345, 0x00 },      /* x_addr_start[7:0] */
	{ IMX_8BIT, 0x0346, 0x00 },      /* y_addr_start[15:8]:0 */
	{ IMX_8BIT, 0x0347, 0x00 },      /* y_addr_start[7:0] */
	{ IMX_8BIT, 0x0348, 0x0C },      /* x_addr_end[15:8]:3279 */
	{ IMX_8BIT, 0x0349, 0xCF },      /* x_addr_end[7:0] */
	{ IMX_8BIT, 0x034A, 0x09 },      /* y_addr_end[15:8]:2463 */
	{ IMX_8BIT, 0x034B, 0x9F },      /* y_addr_end[7:0] */
	{ IMX_8BIT, 0x034C, 0x06 },      /* x_output_size[15:8]:1640 */
	{ IMX_8BIT, 0x034D, 0x68 },      /* x_output_size[7:0] */
	{ IMX_8BIT, 0x034E, 0x04 },      /* y_output_size[15:8]:1232 */
	{ IMX_8BIT, 0x034F, 0xD0 },      /* y_output_size[7:0] */
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x06 },
	{ IMX_8BIT, 0x0355, 0x68 },
	{ IMX_8BIT, 0x0356, 0x04 },
	{ IMX_8BIT, 0x0357, 0xD0 },

	{ IMX_8BIT, 0x301D, 0x30 },

	{ IMX_8BIT, 0x3310, 0x06 },
	{ IMX_8BIT, 0x3311, 0x68 },
	{ IMX_8BIT, 0x3312, 0x04 },
	{ IMX_8BIT, 0x3313, 0xD0 },

	{ IMX_8BIT, 0x331C, 0x04 },
	{ IMX_8BIT, 0x331D, 0x06 },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane, 1/4 binning 30fps 820x616, vendor provide */
static struct imx_reg const imx134_820_616_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning */
	{ IMX_8BIT, 0x0391, 0x44 },	/* 4x4 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x00 },	/* no resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 },
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /* x_addr_start[15:8]:0 */
	{ IMX_8BIT, 0x0345, 0x00 },      /* x_addr_start[7:0] */
	{ IMX_8BIT, 0x0346, 0x00 },      /* y_addr_start[15:8]:0 */
	{ IMX_8BIT, 0x0347, 0x00 },      /* y_addr_start[7:0] */
	{ IMX_8BIT, 0x0348, 0x0C },      /* x_addr_end[15:8]:3279 */
	{ IMX_8BIT, 0x0349, 0xCF },      /* x_addr_end[7:0] */
	{ IMX_8BIT, 0x034A, 0x09 },      /* y_addr_end[15:8]:2463 */
	{ IMX_8BIT, 0x034B, 0x9F },      /* y_addr_end[7:0] */
	{ IMX_8BIT, 0x034C, 0x03 },      /* x_output_size[15:8]:820 */
	{ IMX_8BIT, 0x034D, 0x34 },      /* x_output_size[7:0] */
	{ IMX_8BIT, 0x034E, 0x02 },      /* y_output_size[15:8]:616 */
	{ IMX_8BIT, 0x034F, 0x68 },      /* y_output_size[7:0] */
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x03 },
	{ IMX_8BIT, 0x0355, 0x34 },
	{ IMX_8BIT, 0x0356, 0x02 },
	{ IMX_8BIT, 0x0357, 0x68 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x03 },
	{ IMX_8BIT, 0x3311, 0x34 },
	{ IMX_8BIT, 0x3312, 0x02 },
	{ IMX_8BIT, 0x3313, 0x68 },
	{ IMX_8BIT, 0x331C, 0x02 },
	{ IMX_8BIT, 0x331D, 0xD0 },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane, 1/4 binning 30fps 820x552 */
static struct imx_reg const imx134_820_552_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning */
	{ IMX_8BIT, 0x0391, 0x44 },	/* 4x4 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x00 },	/* no resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 },
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /* x_addr_start[15:8]:0 */
	{ IMX_8BIT, 0x0345, 0x00 },      /* x_addr_start[7:0] */
	{ IMX_8BIT, 0x0346, 0x00 },      /* y_addr_start[15:8]:128 */
	{ IMX_8BIT, 0x0347, 0x80 },      /* y_addr_start[7:0] */
	{ IMX_8BIT, 0x0348, 0x0C },      /* x_addr_end[15:8]:3280-1 */
	{ IMX_8BIT, 0x0349, 0xCF },      /* x_addr_end[7:0] */
	{ IMX_8BIT, 0x034A, 0x09 },      /* y_addr_end[15:8]:2208+128-1 */
	{ IMX_8BIT, 0x034B, 0x1F },      /* y_addr_end[7:0] */
	{ IMX_8BIT, 0x034C, 0x03 },      /* x_output_size[15:8]: */
	{ IMX_8BIT, 0x034D, 0x34 },      /* x_output_size[7:0] */
	{ IMX_8BIT, 0x034E, 0x02 },      /* y_output_size[15:8]:616 */
	{ IMX_8BIT, 0x034F, 0x28 },      /* y_output_size[7:0] */
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x03 },
	{ IMX_8BIT, 0x0355, 0x34 },
	{ IMX_8BIT, 0x0356, 0x02 },
	{ IMX_8BIT, 0x0357, 0x28 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x03 },
	{ IMX_8BIT, 0x3311, 0x34 },
	{ IMX_8BIT, 0x3312, 0x02 },
	{ IMX_8BIT, 0x3313, 0x28 },
	{ IMX_8BIT, 0x331C, 0x02 },
	{ IMX_8BIT, 0x331D, 0xD0 },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane, 1/4 binning 30fps 720x592 */
static struct imx_reg const imx134_720_592_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning */
	{ IMX_8BIT, 0x0391, 0x44 },	/* 4x4 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x00 },	/* no resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 },
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /* x_addr_start[15:8]:200 */
	{ IMX_8BIT, 0x0345, 0xC8 },      /* x_addr_start[7:0] */
	{ IMX_8BIT, 0x0346, 0x00 },      /* y_addr_start[15:8]:40 */
	{ IMX_8BIT, 0x0347, 0x28 },      /* y_addr_start[7:0] */
	{ IMX_8BIT, 0x0348, 0x0C },      /* x_addr_end[15:8]:2880+200-1 */
	{ IMX_8BIT, 0x0349, 0x07 },      /* x_addr_end[7:0] */
	{ IMX_8BIT, 0x034A, 0x09 },      /* y_addr_end[15:8]:2368+40-1 */
	{ IMX_8BIT, 0x034B, 0x67 },      /* y_addr_end[7:0] */
	{ IMX_8BIT, 0x034C, 0x02 },      /* x_output_size[15:8]: */
	{ IMX_8BIT, 0x034D, 0xD0 },      /* x_output_size[7:0] */
	{ IMX_8BIT, 0x034E, 0x02 },      /* y_output_size[15:8]:616 */
	{ IMX_8BIT, 0x034F, 0x50 },      /* y_output_size[7:0] */
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x02 },
	{ IMX_8BIT, 0x0355, 0xD0 },
	{ IMX_8BIT, 0x0356, 0x02 },
	{ IMX_8BIT, 0x0357, 0x50 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x02 },
	{ IMX_8BIT, 0x3311, 0xD0 },
	{ IMX_8BIT, 0x3312, 0x02 },
	{ IMX_8BIT, 0x3313, 0x50 },
	{ IMX_8BIT, 0x331C, 0x02 },
	{ IMX_8BIT, 0x331D, 0xD0 },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

static struct imx_reg const imx134_752_616_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning */
	{ IMX_8BIT, 0x0391, 0x44 },	/* 4x4 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x00 },	/* no resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 },
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /* x_addr_start[15:8]:136 */
	{ IMX_8BIT, 0x0345, 0x88 },      /* x_addr_start[7:0] */
	{ IMX_8BIT, 0x0346, 0x00 },      /* y_addr_start[15:8]:0 */
	{ IMX_8BIT, 0x0347, 0x00 },      /* y_addr_start[7:0] */
	{ IMX_8BIT, 0x0348, 0x0C },      /* x_addr_end[15:8]:3145+134-1 */
	{ IMX_8BIT, 0x0349, 0x47 },      /* x_addr_end[7:0] */
	{ IMX_8BIT, 0x034A, 0x09 },      /* y_addr_end[15:8]:2463 */
	{ IMX_8BIT, 0x034B, 0x9F },      /* y_addr_end[7:0] */
	{ IMX_8BIT, 0x034C, 0x02 },      /* x_output_size[15:8]: 752*/
	{ IMX_8BIT, 0x034D, 0xF0 },      /* x_output_size[7:0] */
	{ IMX_8BIT, 0x034E, 0x02 },      /* y_output_size[15:8]:616 */
	{ IMX_8BIT, 0x034F, 0x68 },      /* y_output_size[7:0] */
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },

	{ IMX_8BIT, 0x0354, 0x02 },
	{ IMX_8BIT, 0x0355, 0xF0 },
	{ IMX_8BIT, 0x0356, 0x02 },
	{ IMX_8BIT, 0x0357, 0x68 },

	{ IMX_8BIT, 0x301D, 0x30 },

	{ IMX_8BIT, 0x3310, 0x02 },
	{ IMX_8BIT, 0x3311, 0xF0 },
	{ IMX_8BIT, 0x3312, 0x02 },
	{ IMX_8BIT, 0x3313, 0x68 },

	{ IMX_8BIT, 0x331C, 0x02 },
	{ IMX_8BIT, 0x331D, 0xD0 },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 1424x1168  */
static struct imx_reg const imx134_1424_1168_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x00 },	/* binning */
	{ IMX_8BIT, 0x0391, 0x11 },	/* no binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x02 },	/* resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x22 },	/* 34/16=2.125 */
	{ IMX_8BIT, 0x4082, 0x00 },	/* ?? */
	{ IMX_8BIT, 0x4083, 0x00 },	/* ?? */
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /* x_addr_start[15:8]:136 */
	{ IMX_8BIT, 0x0345, 0x80 },      /* x_addr_start[7:0] */
	{ IMX_8BIT, 0x0346, 0x00 },      /* y_addr_start[15:8]:0 */
	{ IMX_8BIT, 0x0347, 0x00 },      /* y_addr_start[7:0] */
	{ IMX_8BIT, 0x0348, 0x0C },      /* x_addr_end[15:8]:3145+134-1 */
	{ IMX_8BIT, 0x0349, 0x51 },      /* x_addr_end[7:0] */
	{ IMX_8BIT, 0x034A, 0x09 },      /* y_addr_end[15:8]:2463 */
	{ IMX_8BIT, 0x034B, 0xB1 },      /* y_addr_end[7:0] */
	{ IMX_8BIT, 0x034C, 0x05 },      /* x_output_size[15:8]: 1424*/
	{ IMX_8BIT, 0x034D, 0x90 },      /* x_output_size[7:0] */
	{ IMX_8BIT, 0x034E, 0x04 },      /* y_output_size[15:8]:1168 */
	{ IMX_8BIT, 0x034F, 0x90 },      /* y_output_size[7:0] */
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },

	{ IMX_8BIT, 0x0354, 0x0B },
	{ IMX_8BIT, 0x0355, 0xD2 },
	{ IMX_8BIT, 0x0356, 0x09 },
	{ IMX_8BIT, 0x0357, 0xB2 },

	{ IMX_8BIT, 0x301D, 0x30 },

	{ IMX_8BIT, 0x3310, 0x05 },
	{ IMX_8BIT, 0x3311, 0x90 },
	{ IMX_8BIT, 0x3312, 0x04 },
	{ IMX_8BIT, 0x3313, 0x90 },

	{ IMX_8BIT, 0x331C, 0x02 },
	{ IMX_8BIT, 0x331D, 0xD0 },
	{ IMX_8BIT, 0x4084, 0x05 },
	{ IMX_8BIT, 0x4085, 0x90 },
	{ IMX_8BIT, 0x4086, 0x04 },
	{ IMX_8BIT, 0x4087, 0x90 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane, 1/4 binning, 16/35 down scaling, 30fps, dvs */
static struct imx_reg const imx134_240_196_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/*4x4 binning */
	{ IMX_8BIT, 0x0391, 0x44 },
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x02 },	/* resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x23 },	/* down scaling = 16/35 */
	{ IMX_8BIT, 0x4082, 0x00 },
	{ IMX_8BIT, 0x4083, 0x00 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x02 },      /* x_addr_start[15:8]:590 */
	{ IMX_8BIT, 0x0345, 0x4E },      /* x_addr_start[7:0] */
	{ IMX_8BIT, 0x0346, 0x01 },      /* y_addr_start[15:8]:366 */
	{ IMX_8BIT, 0x0347, 0x6E },      /* y_addr_start[7:0] */
	{ IMX_8BIT, 0x0348, 0x0A },      /* x_addr_end[15:8]:2104+590-1 */
	{ IMX_8BIT, 0x0349, 0x85 },      /* x_addr_end[7:0] */
	{ IMX_8BIT, 0x034A, 0x08 },      /* y_addr_end[15:8]:1720+366-1 */
	{ IMX_8BIT, 0x034B, 0x25 },      /* y_addr_end[7:0] */
	{ IMX_8BIT, 0x034C, 0x00 },      /* x_output_size[15:8]: 240*/
	{ IMX_8BIT, 0x034D, 0xF0 },      /* x_output_size[7:0] */
	{ IMX_8BIT, 0x034E, 0x00 },      /* y_output_size[15:8]:196 */
	{ IMX_8BIT, 0x034F, 0xC4 },      /* y_output_size[7:0] */
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x02 },	/* crop_x: 526 */
	{ IMX_8BIT, 0x0355, 0x0E },
	{ IMX_8BIT, 0x0356, 0x01 },	/* crop_y: 430 */
	{ IMX_8BIT, 0x0357, 0xAE },

	{ IMX_8BIT, 0x301D, 0x30 },

	{ IMX_8BIT, 0x3310, 0x00 },
	{ IMX_8BIT, 0x3311, 0xF0 },
	{ IMX_8BIT, 0x3312, 0x00 },
	{ IMX_8BIT, 0x3313, 0xC4 },

	{ IMX_8BIT, 0x331C, 0x04 },
	{ IMX_8BIT, 0x331D, 0x4C },

	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0xF0 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0xC4 },

	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x0A },
	{ IMX_8BIT, 0x0203, 0x88 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane, 1/2 binning, 16/38 downscaling, 30fps, dvs */
static struct imx_reg const imx134_448_366_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* 2x2 binning */
	{ IMX_8BIT, 0x0391, 0x22 },
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x02 },	/* resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x26 },	/* down scaling = 16/38 */
	{ IMX_8BIT, 0x4082, 0x00 },
	{ IMX_8BIT, 0x4083, 0x00 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x02 },      /* x_addr_start[15:8]:590 */
	{ IMX_8BIT, 0x0345, 0x4E },      /* x_addr_start[7:0] */
	{ IMX_8BIT, 0x0346, 0x01 },      /* y_addr_start[15:8]:366 */
	{ IMX_8BIT, 0x0347, 0x6E },      /* y_addr_start[7:0] */
	{ IMX_8BIT, 0x0348, 0x0A },      /* x_addr_end[15:8]:2128+590-1 */
	{ IMX_8BIT, 0x0349, 0x9D },      /* x_addr_end[7:0] */
	{ IMX_8BIT, 0x034A, 0x08 },      /* y_addr_end[15:8]:1740+366-1 */
	{ IMX_8BIT, 0x034B, 0x39 },      /* y_addr_end[7:0] */
	{ IMX_8BIT, 0x034C, 0x01 },      /* x_output_size[15:8]: 448*/
	{ IMX_8BIT, 0x034D, 0xC0 },      /* x_output_size[7:0] */
	{ IMX_8BIT, 0x034E, 0x01 },      /* y_output_size[15:8]:366 */
	{ IMX_8BIT, 0x034F, 0x6E },      /* y_output_size[7:0] */
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x04 },	/* crop_x: 1064 */
	{ IMX_8BIT, 0x0355, 0x28 },
	{ IMX_8BIT, 0x0356, 0x03 },	/* crop_y: 870 */
	{ IMX_8BIT, 0x0357, 0x66 },

	{ IMX_8BIT, 0x301D, 0x30 },

	{ IMX_8BIT, 0x3310, 0x01 },
	{ IMX_8BIT, 0x3311, 0xC0 },
	{ IMX_8BIT, 0x3312, 0x01 },
	{ IMX_8BIT, 0x3313, 0x6E },

	{ IMX_8BIT, 0x331C, 0x02 },
	{ IMX_8BIT, 0x331D, 0xD0 },

	{ IMX_8BIT, 0x4084, 0x01 },
	{ IMX_8BIT, 0x4085, 0xC0 },
	{ IMX_8BIT, 0x4086, 0x01 },
	{ IMX_8BIT, 0x4087, 0x6E },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane 2336x1312, 30fps, for 1080p dvs,  vendor provide */
static struct imx_reg const imx134_2336_1312_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x00 },	/* disable binning */
	{ IMX_8BIT, 0x0391, 0x11 },
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x02 },	/* H/V resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x16 },	/* down scaling = 16/22 = 8/11 */
	{ IMX_8BIT, 0x4082, 0x00 },
	{ IMX_8BIT, 0x4083, 0x00 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },  /*	x_addr_start[15:8]:34	*/
	{ IMX_8BIT, 0x0345, 0x22 },  /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x01 },  /*	y_addr_start[15:8]:332	*/
	{ IMX_8BIT, 0x0347, 0x4C },  /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0C },  /*	x_addr_end[15:8]:3245	*/
	{ IMX_8BIT, 0x0349, 0xAD },  /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x08 },  /*	y_addr_end[15:8]:2135	*/
	{ IMX_8BIT, 0x034B, 0x57 },  /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x09 },  /*	x_output_size[15:8]:2336 */
	{ IMX_8BIT, 0x034D, 0x20 },  /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x05 },  /*	y_output_size[15:8]:1312 */
	{ IMX_8BIT, 0x034F, 0x20 },  /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x0C },
	{ IMX_8BIT, 0x0355, 0x8C },
	{ IMX_8BIT, 0x0356, 0x07 },
	{ IMX_8BIT, 0x0357, 0x0C },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x09 },
	{ IMX_8BIT, 0x3311, 0x20 },
	{ IMX_8BIT, 0x3312, 0x05 },
	{ IMX_8BIT, 0x3313, 0x20 },
	{ IMX_8BIT, 0x331C, 0x03 },
	{ IMX_8BIT, 0x331D, 0xEB },
	{ IMX_8BIT, 0x4084, 0x09 },
	{ IMX_8BIT, 0x4085, 0x20 },
	{ IMX_8BIT, 0x4086, 0x05 },
	{ IMX_8BIT, 0x4087, 0x20 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane 1920x1080, 30fps, for 720p still capture */
static struct imx_reg const imx134_1936_1096_30fps_v1[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x00 },	/* disable binning */
	{ IMX_8BIT, 0x0391, 0x11 },	/* 2x2 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x02 },	/* H/V resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x1A },	/* downscaling 16/26*/
	{ IMX_8BIT, 0x4082, 0x00 },
	{ IMX_8BIT, 0x4083, 0x00 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },  /*	x_addr_start[15:8]:64	*/
	{ IMX_8BIT, 0x0345, 0x40 },  /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x01 },  /*	y_addr_start[15:8]:340	*/
	{ IMX_8BIT, 0x0347, 0x54 },  /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0C },  /*	x_addr_end[15:8]:3209	*/
	{ IMX_8BIT, 0x0349, 0x89 },  /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x08 },  /*	y_addr_end[15:8]:2121	*/
	{ IMX_8BIT, 0x034B, 0x49 },  /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x07 },  /*	x_output_size[15:8]:1936 */
	{ IMX_8BIT, 0x034D, 0x90 },  /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x04 },  /*	y_output_size[15:8]:1096 */
	{ IMX_8BIT, 0x034F, 0x48 },  /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x0C }, /* crop x:3146 */
	{ IMX_8BIT, 0x0355, 0x4A },
	{ IMX_8BIT, 0x0356, 0x06 }, /* xrop y:1782 */
	{ IMX_8BIT, 0x0357, 0xF6 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x07 },
	{ IMX_8BIT, 0x3311, 0x80 },
	{ IMX_8BIT, 0x3312, 0x04 },
	{ IMX_8BIT, 0x3313, 0x38 },
	{ IMX_8BIT, 0x331C, 0x04 },
	{ IMX_8BIT, 0x331D, 0x1E },
	{ IMX_8BIT, 0x4084, 0x07 },
	{ IMX_8BIT, 0x4085, 0x80 },
	{ IMX_8BIT, 0x4086, 0x04 },
	{ IMX_8BIT, 0x4087, 0x38 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane 1920x1080, 30fps, for 720p still capture,  vendor provide */
static struct imx_reg const imx134_1936_1096_30fps_v2[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x00 }, /* disable binning */
	{ IMX_8BIT, 0x0391, 0x11 },
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x02 }, /* H/V resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x1B }, /* downscaling 16/27*/
	{ IMX_8BIT, 0x4082, 0x00 },
	{ IMX_8BIT, 0x4083, 0x00 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* Optionnal Function setting */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },  /*	x_addr_start[15:8]:64	*/
	{ IMX_8BIT, 0x0345, 0x06 },  /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x01 },  /*	y_addr_start[15:8]:340	*/
	{ IMX_8BIT, 0x0347, 0x34 },  /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0C },  /*	x_addr_end[15:8]:3209	*/
	{ IMX_8BIT, 0x0349, 0xC9 },  /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x08 },  /*	y_addr_end[15:8]:2121	*/
	{ IMX_8BIT, 0x034B, 0x6F },  /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x07 },  /*	x_output_size[15:8]:1936 */
	{ IMX_8BIT, 0x034D, 0x90 },  /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x04 },  /*	y_output_size[15:8]:1096 */
	{ IMX_8BIT, 0x034F, 0x48 },  /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x0C }, /* crop x:3146 */
	{ IMX_8BIT, 0x0355, 0xC4 },
	{ IMX_8BIT, 0x0356, 0x07 }, /* xrop y:1782 */
	{ IMX_8BIT, 0x0357, 0x3A },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x07 }, /* decide by mode and output size */
	{ IMX_8BIT, 0x3311, 0x90 },
	{ IMX_8BIT, 0x3312, 0x04 },
	{ IMX_8BIT, 0x3313, 0x48 },
	{ IMX_8BIT, 0x331C, 0x04 },
	{ IMX_8BIT, 0x331D, 0x1E },
	{ IMX_8BIT, 0x4084, 0x07 },
	{ IMX_8BIT, 0x4085, 0x90 },
	{ IMX_8BIT, 0x4086, 0x04 },
	{ IMX_8BIT, 0x4087, 0x48 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Setting */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane 1296x736, 30fps, for 720p still capture,  vendor provide */
static struct imx_reg const imx134_1296_736_30fps_v2[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning */
	{ IMX_8BIT, 0x0391, 0x22 },	/* 2x2 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x02 },	/* H/V resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x14 },
	{ IMX_8BIT, 0x4082, 0x00 },
	{ IMX_8BIT, 0x4083, 0x00 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* OptionnalFunction settig */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /*	x_addr_start[15:8]:40	*/
	{ IMX_8BIT, 0x0345, 0x14 },      /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x01 },      /*	y_addr_start[15:8]:332	*/
	{ IMX_8BIT, 0x0347, 0x38 },      /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0C },      /*	x_addr_end[15:8]:3239	*/
	{ IMX_8BIT, 0x0349, 0xBB },      /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x08 },      /*	y_addr_end[15:8]:2131	*/
	{ IMX_8BIT, 0x034B, 0x67 },      /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x05 },      /*	x_output_size[15:8]:1280 */
	{ IMX_8BIT, 0x034D, 0x10 },      /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x02 },      /*	y_output_size[15:8]:720 */
	{ IMX_8BIT, 0x034F, 0xE0 },      /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x06 },
	{ IMX_8BIT, 0x0355, 0x54 },
	{ IMX_8BIT, 0x0356, 0x03 },
	{ IMX_8BIT, 0x0357, 0x98 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x05 },
	{ IMX_8BIT, 0x3311, 0x10 },
	{ IMX_8BIT, 0x3312, 0x02 },
	{ IMX_8BIT, 0x3313, 0xE0 },
	{ IMX_8BIT, 0x331C, 0x01 },
	{ IMX_8BIT, 0x331D, 0x10 },
	{ IMX_8BIT, 0x4084, 0x05 },
	{ IMX_8BIT, 0x4085, 0x10 },
	{ IMX_8BIT, 0x4086, 0x02 },
	{ IMX_8BIT, 0x4087, 0xE0 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Settin */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

/* 4 lane 1280x720, 30fps, for 720p dvs,  vendor provide */
static struct imx_reg const imx134_1568_880_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xA9 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning*/
	{ IMX_8BIT, 0x0391, 0x22 },	/* 2x2 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x02 },	/* H/V resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 },	/* down scaling 16/16 = 1 */
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* OptionnalFunction settig */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /*	x_addr_start[15:8]:72	*/
	{ IMX_8BIT, 0x0345, 0x48 },      /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x01 },      /*	y_addr_start[15:8]:356	*/
	{ IMX_8BIT, 0x0347, 0x64 },      /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0C },      /*	x_addr_end[15:8]:3207	*/
	{ IMX_8BIT, 0x0349, 0x87 },      /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x08 },      /*	y_addr_end[15:8]:2115	*/
	{ IMX_8BIT, 0x034B, 0x43 },      /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x06 },      /*	x_output_size[15:8]:1568 */
	{ IMX_8BIT, 0x034D, 0x20 },      /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x03 },      /*	y_output_size[15:8]:880 */
	{ IMX_8BIT, 0x034F, 0x70 },      /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x06 },
	{ IMX_8BIT, 0x0355, 0x20 },
	{ IMX_8BIT, 0x0356, 0x03 },
	{ IMX_8BIT, 0x0357, 0x70 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x06 },
	{ IMX_8BIT, 0x3311, 0x20 },
	{ IMX_8BIT, 0x3312, 0x03 },
	{ IMX_8BIT, 0x3313, 0x70 },
	{ IMX_8BIT, 0x331C, 0x03 },
	{ IMX_8BIT, 0x331D, 0xF2 },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xAF },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Settin */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

static struct imx_reg const imx134_1568_876_60fps_0625[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0x8F },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning*/
	{ IMX_8BIT, 0x0391, 0x22 },	/* 2x2 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x00 },	/* H/V resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 },	/* down scaling 16/16 = 1 */
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* OptionnalFunction settig */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /*	x_addr_start[15:8]:72	*/
	{ IMX_8BIT, 0x0345, 0x48 },      /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x01 },      /*	y_addr_start[15:8]:356	*/
	{ IMX_8BIT, 0x0347, 0x64 },      /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0C },      /*	x_addr_end[15:8]:3207	*/
	{ IMX_8BIT, 0x0349, 0x87 },      /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x08 },      /*	y_addr_end[15:8]:2115	*/
	{ IMX_8BIT, 0x034B, 0x3B },      /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x06 },      /*	x_output_size[15:8]:1568 */
	{ IMX_8BIT, 0x034D, 0x20 },      /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x03 },      /*	y_output_size[15:8]:880 */
	{ IMX_8BIT, 0x034F, 0x6C },      /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x06 },
	{ IMX_8BIT, 0x0355, 0x20 },
	{ IMX_8BIT, 0x0356, 0x03 },
	{ IMX_8BIT, 0x0357, 0x6C },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x06 },
	{ IMX_8BIT, 0x3311, 0x20 },
	{ IMX_8BIT, 0x3312, 0x03 },
	{ IMX_8BIT, 0x3313, 0x6C },
	{ IMX_8BIT, 0x331C, 0x03 },
	{ IMX_8BIT, 0x331D, 0xF2 },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x6F },
	{ IMX_8BIT, 0x0831, 0x27 },
	{ IMX_8BIT, 0x0832, 0x4F },
	{ IMX_8BIT, 0x0833, 0x2F },
	{ IMX_8BIT, 0x0834, 0x2F },
	{ IMX_8BIT, 0x0835, 0x2F },
	{ IMX_8BIT, 0x0836, 0x9F },
	{ IMX_8BIT, 0x0837, 0x37 },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Settin */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};


/* 4 lane for 720p dvs,  vendor provide */
static struct imx_reg const imx134_1568_880[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xC8 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning*/
	{ IMX_8BIT, 0x0391, 0x22 },	/* 2x2 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x00 },	/* H/V resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 },	/* down scaling 16/16 = 1 */
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* OptionnalFunction settig */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /*	x_addr_start[15:8]:72	*/
	{ IMX_8BIT, 0x0345, 0x48 },      /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x01 },      /*	y_addr_start[15:8]:356	*/
	{ IMX_8BIT, 0x0347, 0x64 },      /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0C },      /*	x_addr_end[15:8]:3207	*/
	{ IMX_8BIT, 0x0349, 0x87 },      /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x08 },      /*	y_addr_end[15:8]:2115	*/
	{ IMX_8BIT, 0x034B, 0x43 },      /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x06 },      /*	x_output_size[15:8]:1568 */
	{ IMX_8BIT, 0x034D, 0x20 },      /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x03 },      /*	y_output_size[15:8]:880 */
	{ IMX_8BIT, 0x034F, 0x70 },      /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x06 },
	{ IMX_8BIT, 0x0355, 0x20 },
	{ IMX_8BIT, 0x0356, 0x03 },
	{ IMX_8BIT, 0x0357, 0x70 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x06 },
	{ IMX_8BIT, 0x3311, 0x20 },
	{ IMX_8BIT, 0x3312, 0x03 },
	{ IMX_8BIT, 0x3313, 0x70 },
	{ IMX_8BIT, 0x331C, 0x03 },
	{ IMX_8BIT, 0x331D, 0xF2 },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x5F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x37 },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xBF },
	{ IMX_8BIT, 0x0837, 0x3F },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },


	/* Integration Time Settin */
	{ IMX_8BIT, 0x0202, 0x09 },
	{ IMX_8BIT, 0x0203, 0xD2 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};
/* 4 lane for 480p dvs, default 60fps,  vendor provide */
static struct imx_reg const imx134_880_592[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xC8 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x01 },	/* binning*/
	{ IMX_8BIT, 0x0391, 0x22 },	/* 2x2 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x02 },	/* H/V resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x1D },	/* downscaling ratio = 16/29 */
	{ IMX_8BIT, 0x4082, 0x00 },
	{ IMX_8BIT, 0x4083, 0x00 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* OptionnalFunction settig */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x00 },      /*	x_addr_start[15:8]:44	*/
	{ IMX_8BIT, 0x0345, 0x2C },      /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x00 },      /*	y_addr_start[15:8]:160	*/
	{ IMX_8BIT, 0x0347, 0xA0 },      /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0C },      /*	x_addr_end[15:8]:3235	*/
	{ IMX_8BIT, 0x0349, 0xA3 },      /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x09 },      /*	y_addr_end[15:8]:2307	*/
	{ IMX_8BIT, 0x034B, 0x03 },      /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x03 },      /*	x_output_size[15:8]:880 */
	{ IMX_8BIT, 0x034D, 0x70 },      /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x02 },      /*	y_output_size[15:8]:592 */
	{ IMX_8BIT, 0x034F, 0x50 },      /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x06 },
	{ IMX_8BIT, 0x0355, 0x3C },
	{ IMX_8BIT, 0x0356, 0x04 },
	{ IMX_8BIT, 0x0357, 0x32 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x03 },
	{ IMX_8BIT, 0x3311, 0x70 },
	{ IMX_8BIT, 0x3312, 0x02 },
	{ IMX_8BIT, 0x3313, 0x50 },
	{ IMX_8BIT, 0x331C, 0x04 },
	{ IMX_8BIT, 0x331D, 0x4C },
	{ IMX_8BIT, 0x4084, 0x03 },
	{ IMX_8BIT, 0x4085, 0x70 },
	{ IMX_8BIT, 0x4086, 0x02 },
	{ IMX_8BIT, 0x4087, 0x50 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x5F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x37 },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xBF },
	{ IMX_8BIT, 0x0837, 0x3F },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },


	/* Integration Time Settin */
	{ IMX_8BIT, 0x0202, 0x05 },
	{ IMX_8BIT, 0x0203, 0x42 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};
static struct imx_reg const imx134_2336_1308_60fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* mode set clear */
	{ IMX_8BIT, 0x3A43, 0x01 },
	/* Clock Setting */
	{ IMX_8BIT, 0x011E, 0x13 },
	{ IMX_8BIT, 0x011F, 0x33 },
	{ IMX_8BIT, 0x0301, 0x05 },
	{ IMX_8BIT, 0x0303, 0x01 },
	{ IMX_8BIT, 0x0305, 0x0C },
	{ IMX_8BIT, 0x0309, 0x05 },
	{ IMX_8BIT, 0x030B, 0x01 },
	{ IMX_8BIT, 0x030C, 0x01 },
	{ IMX_8BIT, 0x030D, 0xC8 },
	{ IMX_8BIT, 0x030E, 0x01 },
	{ IMX_8BIT, 0x3A06, 0x11 },

	/* Mode setting */
	{ IMX_8BIT, 0x0108, 0x03 },
	{ IMX_8BIT, 0x0112, 0x0A },
	{ IMX_8BIT, 0x0113, 0x0A },
	{ IMX_8BIT, 0x0381, 0x01 },
	{ IMX_8BIT, 0x0383, 0x01 },
	{ IMX_8BIT, 0x0385, 0x01 },
	{ IMX_8BIT, 0x0387, 0x01 },
	{ IMX_8BIT, 0x0390, 0x00 },	/* binning*/
	{ IMX_8BIT, 0x0391, 0x11 },	/* 2x2 binning */
	{ IMX_8BIT, 0x0392, 0x00 },
	{ IMX_8BIT, 0x0401, 0x00 },	/* H/V resize */
	{ IMX_8BIT, 0x0404, 0x00 },
	{ IMX_8BIT, 0x0405, 0x10 },	/* down scaling 16/16 = 1 */
	{ IMX_8BIT, 0x4082, 0x01 },
	{ IMX_8BIT, 0x4083, 0x01 },
	{ IMX_8BIT, 0x7006, 0x04 },

	/* OptionnalFunction settig */
	{ IMX_8BIT, 0x0700, 0x00 },
	{ IMX_8BIT, 0x3A63, 0x00 },
	{ IMX_8BIT, 0x4100, 0xF8 },
	{ IMX_8BIT, 0x4203, 0xFF },
	{ IMX_8BIT, 0x4344, 0x00 },
	{ IMX_8BIT, 0x441C, 0x01 },

	/* Size setting */
	{ IMX_8BIT, 0x0344, 0x01 },      /*	x_addr_start[15:8]:72	*/
	{ IMX_8BIT, 0x0345, 0xD8 },      /*	x_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0346, 0x02 },      /*	y_addr_start[15:8]:356	*/
	{ IMX_8BIT, 0x0347, 0x44 },      /*	y_addr_start[7:0]	*/
	{ IMX_8BIT, 0x0348, 0x0A },      /*	x_addr_end[15:8]:3207	*/
	{ IMX_8BIT, 0x0349, 0xF7 },      /*	x_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034A, 0x07 },      /*	y_addr_end[15:8]:2107	*/
	{ IMX_8BIT, 0x034B, 0x5F+4 },      /*	y_addr_end[7:0]		*/
	{ IMX_8BIT, 0x034C, 0x09 },      /*	x_output_size[15:8]:1568 */
	{ IMX_8BIT, 0x034D, 0x20 },      /*	x_output_size[7:0]	*/
	{ IMX_8BIT, 0x034E, 0x05 },      /*	y_output_size[15:8]:876 */
	{ IMX_8BIT, 0x034F, 0x1C+4 },      /*	y_output_size[7:0]	*/
	{ IMX_8BIT, 0x0350, 0x00 },
	{ IMX_8BIT, 0x0351, 0x00 },
	{ IMX_8BIT, 0x0352, 0x00 },
	{ IMX_8BIT, 0x0353, 0x00 },
	{ IMX_8BIT, 0x0354, 0x09 },
	{ IMX_8BIT, 0x0355, 0x20 },
	{ IMX_8BIT, 0x0356, 0x05 },
	{ IMX_8BIT, 0x0357, 0x1C+4 },
	{ IMX_8BIT, 0x301D, 0x30 },
	{ IMX_8BIT, 0x3310, 0x09 },
	{ IMX_8BIT, 0x3311, 0x20 },
	{ IMX_8BIT, 0x3312, 0x05 },
	{ IMX_8BIT, 0x3313, 0x1C+4 },
	{ IMX_8BIT, 0x331C, 0x03 },
	{ IMX_8BIT, 0x331D, 0xE8 },
	{ IMX_8BIT, 0x4084, 0x00 },
	{ IMX_8BIT, 0x4085, 0x00 },
	{ IMX_8BIT, 0x4086, 0x00 },
	{ IMX_8BIT, 0x4087, 0x00 },
	{ IMX_8BIT, 0x4400, 0x00 },

	/* Global Timing Setting */
	{ IMX_8BIT, 0x0830, 0x77 },
	{ IMX_8BIT, 0x0831, 0x2F },
	{ IMX_8BIT, 0x0832, 0x5F },
	{ IMX_8BIT, 0x0833, 0x37 },
	{ IMX_8BIT, 0x0834, 0x37 },
	{ IMX_8BIT, 0x0835, 0x37 },
	{ IMX_8BIT, 0x0836, 0xBF },
	{ IMX_8BIT, 0x0837, 0x3F },
	{ IMX_8BIT, 0x0839, 0x1F },
	{ IMX_8BIT, 0x083A, 0x17 },
	{ IMX_8BIT, 0x083B, 0x02 },

	/* Integration Time Settin */
	{ IMX_8BIT, 0x0202, 0x05 },
	{ IMX_8BIT, 0x0203, 0x42 },

	/* HDR Setting */
	{ IMX_8BIT, 0x0230, 0x00 },
	{ IMX_8BIT, 0x0231, 0x00 },
	{ IMX_8BIT, 0x0233, 0x00 },
	{ IMX_8BIT, 0x0234, 0x00 },
	{ IMX_8BIT, 0x0235, 0x40 },
	{ IMX_8BIT, 0x0238, 0x00 },
	{ IMX_8BIT, 0x0239, 0x04 },
	{ IMX_8BIT, 0x023B, 0x00 },
	{ IMX_8BIT, 0x023C, 0x01 },
	{ IMX_8BIT, 0x33B0, 0x04 },
	{ IMX_8BIT, 0x33B1, 0x00 },
	{ IMX_8BIT, 0x33B3, 0x00 },
	{ IMX_8BIT, 0x33B4, 0x01 },
	{ IMX_8BIT, 0x3800, 0x00 },
	{ IMX_TOK_TERM, 0, 0 }
};

struct imx_resolution imx134_res_preview[] = {
	{
		.desc = "imx134_CIF_30fps",
		.regs = imx134_720_592_30fps,
		.width = 720,
		.height = 592,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
	},
	{
		.desc = "imx134_820_552_30fps_preview",
		.regs = imx134_820_552_30fps,
		.width = 820,
		.height = 552,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
	},
	{
		.desc = "imx134_820_616_preview_30fps",
		.regs = imx134_820_616_30fps,
		.width = 820,
		.height = 616,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
	},
	{
		.desc = "imx134_1080p_preview_30fps",
		.regs = imx134_1936_1096_30fps_v2,
		.width = 1936,
		.height = 1096,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
	},
	{
		.desc = "imx134_1640_1232_preview_30fps",
		.regs = imx134_1640_1232_30fps,
		.width = 1640,
		.height = 1232,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
	},
	{
		.desc = "imx134_8M_preview_30fps",
		.regs = imx134_8M_30fps,
		.width = 3280,
		.height = 2464,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
	},
};

struct imx_resolution imx134_res_still[] = {
	{
		.desc = "imx134_CIF_30fps",
		.regs = imx134_1424_1168_30fps,
		.width = 1424,
		.height = 1168,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
	},
	{
		.desc = "imx134_VGA_still_30fps",
		.regs = imx134_1640_1232_30fps,
		.width = 1640,
		.height = 1232,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
	},
	{
		.desc = "imx134_1080p_still_30fps",
		.regs = imx134_1936_1096_30fps_v2,
		.width = 1936,
		.height = 1096,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
	},
	{
		.desc = "imx134_1640_1232_still_30fps",
		.regs = imx134_1640_1232_30fps,
		.width = 1640,
		.height = 1232,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
	},
	{
		.desc = "imx134_8M_still_30fps",
		.regs = imx134_8M_30fps,
		.width = 3280,
		.height = 2464,
		.fps_options = {
			{
				/* WORKAROUND for FW performance limitation */
				 .fps = 8,
				 .pixels_per_line = 6400,
				 .lines_per_frame = 5312,
			},
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
	},
};

struct imx_resolution imx134_res_video[] = {
	{
		.desc = "imx134_QCIF_DVS_30fps",
		.regs = imx134_240_196_30fps,
		.width = 240,
		.height = 196,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
	},
	{
		.desc = "imx134_CIF_DVS_30fps",
		.regs = imx134_448_366_30fps,
		.width = 448,
		.height = 366,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
	},
	{
		.desc = "imx134_VGA_30fps",
		.regs = imx134_820_616_30fps,
		.width = 820,
		.height = 616,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.used = 0,
	},
	{
		.desc = "imx134_480p",
		.regs = imx134_880_592,
		.width = 880,
		.height = 592,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2700,
			},
			{
				 .fps = 60,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 1350,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
	},
	{
		.desc = "imx134_1568_880",
		.regs = imx134_1568_880,
		.width = 1568,
		.height = 880,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2700,
			},
			{
				 .fps = 60,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 1350,
			},
			{
			}
		},
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.used = 0,
	},
	{
		.desc = "imx134_1080p_dvs_30fps",
		.regs = imx134_2336_1312_30fps,
		.width = 2336,
		.height = 1312,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
	},
	{
		.desc = "imx134_1080p_dvs_60fps",
		.regs = imx134_2336_1308_60fps,
		.width = 2336,
		.height = 1312,
		.fps_options = {
			{
				 .fps = 60,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 1350,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
	},
	{
		/*This setting only be used for SDV mode*/
		.desc = "imx134_8M_sdv_30fps",
		.regs = imx134_8M_30fps,
		.width = 3280,
		.height = 2464,
		.fps_options = {
			{
				 .fps = 30,
				 .pixels_per_line = 3600,
				 .lines_per_frame = 2518,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
	},
};

#endif

