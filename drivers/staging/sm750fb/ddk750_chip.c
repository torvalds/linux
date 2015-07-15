#include "ddk750_help.h"
#include "ddk750_reg.h"
#include "ddk750_chip.h"
#include "ddk750_power.h"
typedef struct _pllcalparam {
	unsigned char power;/* d : 0~ 6*/
	unsigned char pod;
	unsigned char od;
	unsigned char value;/* value of  2 power d (2^d) */
}
pllcalparam;


logical_chip_type_t getChipType(void)
{
	unsigned short physicalID;
	char physicalRev;
	logical_chip_type_t chip;

	physicalID = devId750; /* either 0x718 or 0x750 */
	physicalRev = revId750;

	if (physicalID == 0x718)
		chip = SM718;
	else if (physicalID == 0x750) {
		chip = SM750;
		/* SM750 and SM750LE are different in their revision ID only. */
		if (physicalRev == SM750LE_REVISION_ID)
			chip = SM750LE;
	} else
		chip = SM_UNKNOWN;

	return chip;
}


inline unsigned int twoToPowerOfx(unsigned long x)
{
	unsigned long i;
	unsigned long result = 1;

	for (i = 1; i <= x; i++)
		result *= 2;
	return result;
}

inline unsigned int calcPLL(pll_value_t *pPLL)
{
	return (pPLL->inputFreq * pPLL->M / pPLL->N / twoToPowerOfx(pPLL->OD) / twoToPowerOfx(pPLL->POD));
}

unsigned int getPllValue(clock_type_t clockType, pll_value_t *pPLL)
{
	unsigned int ulPllReg = 0;

	pPLL->inputFreq = DEFAULT_INPUT_CLOCK;
	pPLL->clockType = clockType;

	switch (clockType) {
	case MXCLK_PLL:
		ulPllReg = PEEK32(MXCLK_PLL_CTRL);
		break;
	case PRIMARY_PLL:
		ulPllReg = PEEK32(PANEL_PLL_CTRL);
		break;
	case SECONDARY_PLL:
		ulPllReg = PEEK32(CRT_PLL_CTRL);
		break;
	case VGA0_PLL:
		ulPllReg = PEEK32(VGA_PLL0_CTRL);
		break;
	case VGA1_PLL:
		ulPllReg = PEEK32(VGA_PLL1_CTRL);
		break;
	}

	pPLL->M = FIELD_GET(ulPllReg, PANEL_PLL_CTRL, M);
	pPLL->N = FIELD_GET(ulPllReg, PANEL_PLL_CTRL, N);
	pPLL->OD = FIELD_GET(ulPllReg, PANEL_PLL_CTRL, OD);
	pPLL->POD = FIELD_GET(ulPllReg, PANEL_PLL_CTRL, POD);

	return calcPLL(pPLL);
}


unsigned int getChipClock(void)
{
	pll_value_t pll;
#if 1
	if (getChipType() == SM750LE)
		return MHz(130);
#endif

	return getPllValue(MXCLK_PLL, &pll);
}


/*
 * This function set up the main chip clock.
 *
 * Input: Frequency to be set.
 */
void setChipClock(unsigned int frequency)
{
	pll_value_t pll;
	unsigned int ulActualMxClk;
#if 1
	/* Cheok_0509: For SM750LE, the chip clock is fixed. Nothing to set. */
	if (getChipType() == SM750LE)
		return;
#endif

	if (frequency) {
		/*
		* Set up PLL, a structure to hold the value to be set in clocks.
		*/
		pll.inputFreq = DEFAULT_INPUT_CLOCK; /* Defined in CLOCK.H */
		pll.clockType = MXCLK_PLL;

		/*
		* Call calcPllValue() to fill up the other fields for PLL structure.
		* Sometime, the chip cannot set up the exact clock required by User.
		* Return value from calcPllValue() gives the actual possible clock.
		*/
		ulActualMxClk = calcPllValue(frequency, &pll);

		/* Master Clock Control: MXCLK_PLL */
		POKE32(MXCLK_PLL_CTRL, formatPllReg(&pll));
	}
}



void setMemoryClock(unsigned int frequency)
{
	unsigned int ulReg, divisor;
 #if 1
	/* Cheok_0509: For SM750LE, the memory clock is fixed. Nothing to set. */
	if (getChipType() == SM750LE)
		return;
#endif
	if (frequency) {
		/* Set the frequency to the maximum frequency that the DDR Memory can take
		which is 336MHz. */
		if (frequency > MHz(336))
			frequency = MHz(336);

		/* Calculate the divisor */
		divisor = (unsigned int) roundedDiv(getChipClock(), frequency);

		/* Set the corresponding divisor in the register. */
		ulReg = PEEK32(CURRENT_GATE);
		switch (divisor) {
		default:
		case 1:
			ulReg = FIELD_SET(ulReg, CURRENT_GATE, M2XCLK, DIV_1);
			break;
		case 2:
			ulReg = FIELD_SET(ulReg, CURRENT_GATE, M2XCLK, DIV_2);
			break;
		case 3:
			ulReg = FIELD_SET(ulReg, CURRENT_GATE, M2XCLK, DIV_3);
			break;
		case 4:
			ulReg = FIELD_SET(ulReg, CURRENT_GATE, M2XCLK, DIV_4);
			break;
		}

		setCurrentGate(ulReg);
	}
}


/*
 * This function set up the master clock (MCLK).
 *
 * Input: Frequency to be set.
 *
 * NOTE:
 *      The maximum frequency the engine can run is 168MHz.
 */
void setMasterClock(unsigned int frequency)
{
	unsigned int ulReg, divisor;
#if 1
	/* Cheok_0509: For SM750LE, the memory clock is fixed. Nothing to set. */
	if (getChipType() == SM750LE)
		return;
#endif
	if (frequency) {
		/* Set the frequency to the maximum frequency that the SM750 engine can
		run, which is about 190 MHz. */
		if (frequency > MHz(190))
			frequency = MHz(190);

		/* Calculate the divisor */
		divisor = (unsigned int) roundedDiv(getChipClock(), frequency);

		/* Set the corresponding divisor in the register. */
		ulReg = PEEK32(CURRENT_GATE);
		switch (divisor) {
		default:
		case 3:
			ulReg = FIELD_SET(ulReg, CURRENT_GATE, MCLK, DIV_3);
			break;
		case 4:
			ulReg = FIELD_SET(ulReg, CURRENT_GATE, MCLK, DIV_4);
			break;
		case 6:
			ulReg = FIELD_SET(ulReg, CURRENT_GATE, MCLK, DIV_6);
			break;
		case 8:
			ulReg = FIELD_SET(ulReg, CURRENT_GATE, MCLK, DIV_8);
			break;
		}

		setCurrentGate(ulReg);
		}
}


unsigned int ddk750_getVMSize(void)
{
	unsigned int reg;
	unsigned int data;

	/* sm750le only use 64 mb memory*/
	if (getChipType() == SM750LE)
		return MB(64);

	/* for 750,always use power mode0*/
	reg = PEEK32(MODE0_GATE);
	reg = FIELD_SET(reg, MODE0_GATE, GPIO, ON);
	POKE32(MODE0_GATE, reg);

	/* get frame buffer size from GPIO */
	reg = FIELD_GET(PEEK32(MISC_CTRL), MISC_CTRL, LOCALMEM_SIZE);
	switch (reg) {
	case MISC_CTRL_LOCALMEM_SIZE_8M:
		data = MB(8);  break; /* 8  Mega byte */
	case MISC_CTRL_LOCALMEM_SIZE_16M:
		data = MB(16); break; /* 16 Mega byte */
	case MISC_CTRL_LOCALMEM_SIZE_32M:
		data = MB(32); break; /* 32 Mega byte */
	case MISC_CTRL_LOCALMEM_SIZE_64M:
		data = MB(64); break; /* 64 Mega byte */
	default:
		data = 0;
		break;
	}
	return data;

}

int ddk750_initHw(initchip_param_t *pInitParam)
{

	unsigned int ulReg;
#if 0
	/* move the code to map regiter function. */
	if (getChipType() == SM718) {
		/* turn on big endian bit*/
		ulReg = PEEK32(0x74);
		/* now consider register definition in a big endian pattern*/
		POKE32(0x74, ulReg|0x80000000);
	}

#endif


	if (pInitParam->powerMode != 0)
		pInitParam->powerMode = 0;
	setPowerMode(pInitParam->powerMode);

	/* Enable display power gate & LOCALMEM power gate*/
	ulReg = PEEK32(CURRENT_GATE);
	ulReg = FIELD_SET(ulReg, CURRENT_GATE, DISPLAY, ON);
	ulReg = FIELD_SET(ulReg, CURRENT_GATE, LOCALMEM, ON);
	setCurrentGate(ulReg);

	if (getChipType() != SM750LE) {
		/*	set panel pll and graphic mode via mmio_88 */
		ulReg = PEEK32(VGA_CONFIGURATION);
		ulReg = FIELD_SET(ulReg, VGA_CONFIGURATION, PLL, PANEL);
		ulReg = FIELD_SET(ulReg, VGA_CONFIGURATION, MODE, GRAPHIC);
		POKE32(VGA_CONFIGURATION, ulReg);
	} else {
#if defined(__i386__) || defined(__x86_64__)
		/* set graphic mode via IO method */
		outb_p(0x88, 0x3d4);
		outb_p(0x06, 0x3d5);
#endif
	}

	/* Set the Main Chip Clock */
	setChipClock(MHz((unsigned int)pInitParam->chipClock));

	/* Set up memory clock. */
	setMemoryClock(MHz(pInitParam->memClock));

	/* Set up master clock */
	setMasterClock(MHz(pInitParam->masterClock));


	/* Reset the memory controller. If the memory controller is not reset in SM750,
	   the system might hang when sw accesses the memory.
	   The memory should be resetted after changing the MXCLK.
	 */
	if (pInitParam->resetMemory == 1) {
		ulReg = PEEK32(MISC_CTRL);
		ulReg = FIELD_SET(ulReg, MISC_CTRL, LOCALMEM_RESET, RESET);
		POKE32(MISC_CTRL, ulReg);

		ulReg = FIELD_SET(ulReg, MISC_CTRL, LOCALMEM_RESET, NORMAL);
		POKE32(MISC_CTRL, ulReg);
	}

	if (pInitParam->setAllEngOff == 1) {
		enable2DEngine(0);

		/* Disable Overlay, if a former application left it on */
		ulReg = PEEK32(VIDEO_DISPLAY_CTRL);
		ulReg = FIELD_SET(ulReg, VIDEO_DISPLAY_CTRL, PLANE, DISABLE);
		POKE32(VIDEO_DISPLAY_CTRL, ulReg);

		/* Disable video alpha, if a former application left it on */
		ulReg = PEEK32(VIDEO_ALPHA_DISPLAY_CTRL);
		ulReg = FIELD_SET(ulReg, VIDEO_ALPHA_DISPLAY_CTRL, PLANE, DISABLE);
		POKE32(VIDEO_ALPHA_DISPLAY_CTRL, ulReg);

		/* Disable alpha plane, if a former application left it on */
		ulReg = PEEK32(ALPHA_DISPLAY_CTRL);
		ulReg = FIELD_SET(ulReg, ALPHA_DISPLAY_CTRL, PLANE, DISABLE);
		POKE32(ALPHA_DISPLAY_CTRL, ulReg);

#if 0
		/* Disable LCD hardware cursor, if a former application left it on */
		ulReg = PEEK32(PANEL_HWC_ADDRESS);
		ulReg = FIELD_SET(ulReg, PANEL_HWC_ADDRESS, ENABLE, DISABLE);
		POKE32(PANEL_HWC_ADDRESS, ulReg);

		/* Disable CRT hardware cursor, if a former application left it on */
		ulReg = PEEK32(CRT_HWC_ADDRESS);
		ulReg = FIELD_SET(ulReg, CRT_HWC_ADDRESS, ENABLE, DISABLE);
		POKE32(CRT_HWC_ADDRESS, ulReg);

		/* Disable ZV Port 0, if a former application left it on */
		ulReg = PEEK32(ZV0_CAPTURE_CTRL);
		ulReg = FIELD_SET(ulReg, ZV0_CAPTURE_CTRL, CAP, DISABLE);
		POKE32(ZV0_CAPTURE_CTRL, ulReg);

		/* Disable ZV Port 1, if a former application left it on */
		ulReg = PEEK32(ZV1_CAPTURE_CTRL);
		ulReg = FIELD_SET(ulReg, ZV1_CAPTURE_CTRL, CAP, DISABLE);
		POKE32(ZV1_CAPTURE_CTRL, ulReg);

		/* Disable ZV Port Power, if a former application left it on */
		enableZVPort(0);
		/* Disable DMA Channel, if a former application left it on */
		ulReg = PEEK32(DMA_ABORT_INTERRUPT);
		ulReg = FIELD_SET(ulReg, DMA_ABORT_INTERRUPT, ABORT_1, ABORT);
		POKE32(DMA_ABORT_INTERRUPT, ulReg);

		/* Disable i2c */
		enableI2C(0);
#endif
		/* Disable DMA Channel, if a former application left it on */
		ulReg = PEEK32(DMA_ABORT_INTERRUPT);
		ulReg = FIELD_SET(ulReg, DMA_ABORT_INTERRUPT, ABORT_1, ABORT);
		POKE32(DMA_ABORT_INTERRUPT, ulReg);

		/* Disable DMA Power, if a former application left it on */
		enableDMA(0);
	}

	/* We can add more initialization as needed. */

	return 0;
}

#if 0

unsigned int absDiff(unsigned int a, unsigned int b)
{
	if (a > b)
		return(a - b);
	else
		return(b - a);
}

#endif
/*
	monk liu @ 4/6/2011:
		   re-write the calculatePLL function of ddk750.
		   the original version function does not use some mathematics tricks and shortcut
		   when it doing the calculation of the best N,M,D combination
		   I think this version gives a little upgrade in speed

	750 pll clock formular:
	Request Clock = (Input Clock * M )/(N * X)

	Input Clock = 14318181 hz
	X = 2 power D
	D ={0,1,2,3,4,5,6}
	M = {1,...,255}
	N = {2,...,15}
*/
unsigned int calcPllValue(unsigned int request_orig, pll_value_t *pll)
{
	/* used for primary and secondary channel pixel clock pll */
	static pllcalparam xparm_PIXEL[] = {
		/* 2^0 = 1*/			{0, 0, 0, 1},
		/* 2^ 1 =2*/			{1, 0, 1, 2},
		/* 2^ 2  = 4*/		{2, 0, 2, 4},
							{3, 0, 3, 8},
							{4, 1, 3, 16},
							{5, 2, 3, 32},
		/* 2^6 = 64  */		{6, 3, 3, 64},
							};

	/* used for MXCLK (chip clock) */
	static pllcalparam xparm_MXCLK[] = {
		/* 2^0 = 1*/			{0, 0, 0, 1},
		/* 2^ 1 =2*/			{1, 0, 1, 2},
		/* 2^ 2  = 4*/		{2, 0, 2, 4},
							{3, 0, 3, 8},
							};

	/* as sm750 register definition, N located in 2,15 and M located in 1,255	*/
	int N, M, X, d;
	int xcnt;
	int miniDiff;
	unsigned int RN, quo, rem, fl_quo;
	unsigned int input, request;
	unsigned int tmpClock, ret;
	pllcalparam *xparm;

#if 1
	if (getChipType() == SM750LE) {
		/* SM750LE don't have prgrammable PLL and M/N values to work on.
		Just return the requested clock. */
		return request_orig;
	}
#endif

	ret = 0;
	miniDiff = ~0;
	request = request_orig / 1000;
	input = pll->inputFreq / 1000;

	/* for MXCLK register , no POD provided, so need be treated differently	*/

	if (pll->clockType != MXCLK_PLL) {
		xparm = &xparm_PIXEL[0];
		xcnt = sizeof(xparm_PIXEL)/sizeof(xparm_PIXEL[0]);
	} else {
		xparm = &xparm_MXCLK[0];
		xcnt = sizeof(xparm_MXCLK)/sizeof(xparm_MXCLK[0]);
	}


	for (N = 15; N > 1; N--) {
		/* RN will not exceed maximum long if @request <= 285 MHZ (for 32bit cpu) */
		RN = N * request;
		quo = RN / input;
		rem = RN % input;/* rem always small than 14318181 */
		fl_quo = (rem * 10000 / input);

		for (d = xcnt - 1; d >= 0; d--) {
			X = xparm[d].value;
			M = quo*X;
			M += fl_quo * X / 10000;
			/* round step */
			M += (fl_quo*X % 10000) > 5000?1:0;
			if (M < 256 && M > 0) {
				unsigned int diff;

				tmpClock = pll->inputFreq * M / N / X;
				diff = absDiff(tmpClock, request_orig);
				if (diff < miniDiff) {
					pll->M = M;
					pll->N = N;
					pll->OD = xparm[d].od;
					pll->POD = xparm[d].pod;
					miniDiff = diff;
					ret = tmpClock;
				}
			}
		}
	}
	return ret;
}

unsigned int calcPllValue2(
unsigned int ulRequestClk, /* Required pixel clock in Hz unit */
pll_value_t *pPLL           /* Structure to hold the value to be set in PLL */
)
{
	unsigned int M, N, OD, POD = 0, diff, pllClk, odPower, podPower;
	unsigned int bestDiff = 0xffffffff; /* biggest 32 bit unsigned number */
	unsigned int ret;
    /* Init PLL structure to know states */
	pPLL->M = 0;
	pPLL->N = 0;
	pPLL->OD = 0;
	pPLL->POD = 0;

    /* Sanity check: None at the moment */

    /* Convert everything in Khz range in order to avoid calculation overflow */
	pPLL->inputFreq /= 1000;
	ulRequestClk /= 1000;

#ifndef VALIDATION_CHIP
    /* The maximum of post divider is 8. */
	for (POD = 0; POD <= 3; POD++)
#endif
		{

#ifndef VALIDATION_CHIP
	/* MXCLK_PLL does not have post divider. */
	if ((POD > 0) && (pPLL->clockType == MXCLK_PLL))
		break;
#endif

	/* Work out 2 to the power of POD */
	podPower = twoToPowerOfx(POD);

	/* OD has only 2 bits [15:14] and its value must between 0 to 3 */
	for (OD = 0; OD <= 3; OD++) {
		/* Work out 2 to the power of OD */
		odPower = twoToPowerOfx(OD);

#ifdef VALIDATION_CHIP
	if (odPower > 4)
		podPower = 4;
	else
		podPower = odPower;
#endif

		/* N has 4 bits [11:8] and its value must between 2 and 15.
		The N == 1 will behave differently --> Result is not correct. */
	for (N = 2; N <= 15; N++) {
		/* The formula for PLL is ulRequestClk = inputFreq * M / N / (2^OD)
		In the following steps, we try to work out a best M value given the others are known.
		To avoid decimal calculation, we use 1000 as multiplier for up to 3 decimal places of accuracy.
		*/
		M = ulRequestClk * N * odPower * 1000 / pPLL->inputFreq;
		M = roundedDiv(M, 1000);

		/* M field has only 8 bits, reject value bigger than 8 bits */
		if (M < 256) {
			/* Calculate the actual clock for a given M & N */
			pllClk = pPLL->inputFreq * M / N / odPower / podPower;

			/* How much are we different from the requirement */
			diff = absDiff(pllClk, ulRequestClk);

			if (diff < bestDiff) {
				bestDiff = diff;

				/* Store M and N values */
				pPLL->M  = M;
				pPLL->N  = N;
				pPLL->OD = OD;

#ifdef VALIDATION_CHIP
			if (OD > 2)
				POD = 2;
			else
				POD = OD;
#endif

			pPLL->POD = POD;
			}
		}
	}
	}
	}

    /* Restore input frequency from Khz to hz unit */
	ulRequestClk *= 1000;
	pPLL->inputFreq = DEFAULT_INPUT_CLOCK; /* Default reference clock */

    /* Return actual frequency that the PLL can set */
	ret = calcPLL(pPLL);
	return ret;
}





unsigned int formatPllReg(pll_value_t *pPLL)
{
	unsigned int ulPllReg = 0;

    /* Note that all PLL's have the same format. Here, we just use Panel PLL parameter
       to work out the bit fields in the register.
       On returning a 32 bit number, the value can be applied to any PLL in the calling function.
    */
	ulPllReg =
	FIELD_SET(0, PANEL_PLL_CTRL, BYPASS, OFF)
	| FIELD_SET(0, PANEL_PLL_CTRL, POWER,  ON)
	| FIELD_SET(0, PANEL_PLL_CTRL, INPUT,  OSC)
#ifndef VALIDATION_CHIP
	| FIELD_VALUE(0, PANEL_PLL_CTRL, POD,    pPLL->POD)
#endif
	| FIELD_VALUE(0, PANEL_PLL_CTRL, OD,     pPLL->OD)
	| FIELD_VALUE(0, PANEL_PLL_CTRL, N,      pPLL->N)
	| FIELD_VALUE(0, PANEL_PLL_CTRL, M,      pPLL->M);

	return ulPllReg;
}


