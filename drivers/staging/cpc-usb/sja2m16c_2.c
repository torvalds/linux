/****************************************************************************
*
*      Copyright (c) 2003,2004 by EMS Dr. Thomas Wuensche
*
*                  - All rights reserved -
*
* This code is provided "as is" without warranty of any kind, either
* expressed or implied, including but not limited to the liability
* concerning the freedom from material defects, the fitness for parti-
* cular purposes or the freedom of proprietary rights of third parties.
*
*****************************************************************************
* Module name.: cpcusb
*****************************************************************************
* Include file: cpc.h
*****************************************************************************
* Project.....: Windows Driver Development Kit
* Filename....: sja2m16c.cpp
* Authors.....: (GU) Gerhard Uttenthaler
*               (CS) Christian Schoett
*****************************************************************************
* Short descr.: converts baudrate between SJA1000 and M16C
*****************************************************************************
* Description.: handles the baudrate conversion from SJA1000 parameters to
*               M16C parameters
*****************************************************************************
* Address     : EMS Dr. Thomas Wuensche
*               Sonnenhang 3
*               D-85304 Ilmmuenster
*               Tel. : +49-8441-490260
*               Fax. : +49-8441-81860
*               email: support@ems-wuensche.com
*****************************************************************************
*                            History
*****************************************************************************
* Version  Date        Auth Remark
*
* 01.00    ??          GU   - initial release
* 01.10    ??????????  CS   - adapted to fit into the USB Windows driver
* 02.00    18.08.2004  GU   - improved the baudrate calculating algorithm
*                           - implemented acceptance filtering
* 02.10    10.09.2004  CS   - adapted to fit into the USB Windows driver
*****************************************************************************
*                            ToDo's
*****************************************************************************
*/

/****************************************************************************/
/*     I N C L U D E S
*/
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/usb.h>

#include "cpc.h"
#include "cpc_int.h"
#include "cpcusb.h"

#include "sja2m16c.h"

/*********************************************************************/
int baudrate_m16c(int clk, int brp, int pr, int ph1, int ph2)
{
	return (16000000 / (1 << clk)) / 2 / (brp + 1) / (1 + pr + 1 +
							    ph1 + 1 + ph2 +
							    1);
}


/*********************************************************************/
int samplepoint_m16c(int brp, int pr, int ph1, int ph2)
{
	return (100 * (1 + pr + 1 + ph1 + 1)) / (1 + pr + 1 + ph1 + 1 +
						  ph2 + 1);
}


/****************************************************************************
* Function.....: SJA1000_TO_M16C_BASIC_Params
*
* Task.........: This routine converts SJA1000 CAN btr parameters into M16C
*                parameters based on the sample point and the error. In
*                addition it converts the acceptance filter parameters to
*                suit the M16C parameters
*
* Parameters...: None
*
* Return values: None
*
* Comments.....:
*****************************************************************************
*                History
*****************************************************************************
* 19.01.2005  CS   - modifed the conversion of SJA1000 filter params into
*                    M16C params. Due to compatibility reasons with the
*                    older 82C200 CAN controller the SJA1000
****************************************************************************/
int SJA1000_TO_M16C_BASIC_Params(CPC_MSG_T * in)
{
	int sjaBaudrate;
	int sjaSamplepoint;
	int *baudrate_error;	// BRP[0..15], PR[0..7], PH1[0..7], PH2[0..7]
	int *samplepoint_error;	// BRP[0..15], PR[0..7], PH1[0..7], PH2[0..7]
	int baudrate_error_merk;
	int clk, brp, pr, ph1, ph2;
	int clk_merk, brp_merk, pr_merk, ph1_merk, ph2_merk;
	int index;
	unsigned char acc_code0, acc_code1, acc_code2, acc_code3;
	unsigned char acc_mask0, acc_mask1, acc_mask2, acc_mask3;
	CPC_MSG_T * out;
	C0CONR_T c0con;
	C1CONR_T c1con;
	int tmpAccCode;
	int tmpAccMask;

	    // we have to convert the parameters into M16C parameters
	    CPC_SJA1000_PARAMS_T * pParams;

	    // check if the type is CAN parameters and if we have to convert the given params
	    if (in->type != CPC_CMD_T_CAN_PRMS
		|| in->msg.canparams.cc_type != SJA1000)
		return 0;
	pParams =
	    (CPC_SJA1000_PARAMS_T *) & in->msg.canparams.cc_params.sja1000;
	acc_code0 = pParams->acc_code0;
	acc_code1 = pParams->acc_code1;
	acc_code2 = pParams->acc_code2;
	acc_code3 = pParams->acc_code3;
	acc_mask0 = pParams->acc_mask0;
	acc_mask1 = pParams->acc_mask1;
	acc_mask2 = pParams->acc_mask2;
	acc_mask3 = pParams->acc_mask3;

#ifdef _DEBUG_OUTPUT_CAN_PARAMS
	    info("acc_code0: %2.2Xh\n", acc_code0);
	info("acc_code1: %2.2Xh\n", acc_code1);
	info("acc_code2: %2.2Xh\n", acc_code2);
	info("acc_code3: %2.2Xh\n", acc_code3);
	info("acc_mask0: %2.2Xh\n", acc_mask0);
	info("acc_mask1: %2.2Xh\n", acc_mask1);
	info("acc_mask2: %2.2Xh\n", acc_mask2);
	info("acc_mask3: %2.2Xh\n", acc_mask3);

#endif	/*  */
	    if (!
		 (baudrate_error =
		  (int *) vmalloc(sizeof(int) * 16 * 8 * 8 * 8 * 5))) {
		err("Could not allocate memory\n");
		return -3;
	}
	if (!
	      (samplepoint_error =
	       (int *) vmalloc(sizeof(int) * 16 * 8 * 8 * 8 * 5))) {
		err("Could not allocate memory\n");
		vfree(baudrate_error);
		return -3;
	}
	memset(baudrate_error, 0xff, sizeof(baudrate_error));
	memset(samplepoint_error, 0xff, sizeof(baudrate_error));
	sjaBaudrate =
	    16000000 / 2 / SJA_BRP / (1 + SJA_TSEG1 + SJA_TSEG2);
	sjaSamplepoint =
	    100 * (1 + SJA_TSEG1) / (1 + SJA_TSEG1 + SJA_TSEG2);
	if (sjaBaudrate == 0) {
		vfree(baudrate_error);
		vfree(samplepoint_error);
		return -2;
	}

#ifdef _DEBUG_OUTPUT_CAN_PARAMS
	    info("\nStarting SJA CAN params\n");
	info("-------------------------\n");
	info("TS1     : %2.2Xh TS2 : %2.2Xh\n", SJA_TSEG1, SJA_TSEG2);
	info("BTR0    : %2.2Xh BTR1: %2.2Xh\n", pParams->btr0,
	      pParams->btr1);
	info("Baudrate: %d.%dkBaud\n", sjaBaudrate / 1000,
	      sjaBaudrate % 1000);
	info("Sample P: 0.%d\n", sjaSamplepoint);
	info("\n");

#endif	/*  */
	    c0con.bc0con.sam = SJA_SAM;
	c1con.bc1con.sjw = SJA_SJW;

	    // calculate errors for all baudrates
	    index = 0;
	for (clk = 0; clk < 5; clk++) {
		for (brp = 0; brp < 16; brp++) {
			for (pr = 0; pr < 8; pr++) {
				for (ph1 = 0; ph1 < 8; ph1++) {
					for (ph2 = 0; ph2 < 8; ph2++) {
						baudrate_error[index] =
						    100 *
						    abs(baudrate_m16c
							(clk, brp, pr, ph1,
							 ph2) -
							sjaBaudrate) /
						    sjaBaudrate;
						samplepoint_error[index] =
						    abs(samplepoint_m16c
							(brp, pr, ph1,
							 ph2) -
							sjaSamplepoint);

#if 0
						    info
						    ("Baudrate      : %d kBaud\n",
						     baudrate_m16c(clk,
								   brp, pr,
								   ph1,
								   ph2));
						info
						    ("Baudrate Error: %d\n",
						     baudrate_error
						     [index]);
						info
						    ("Sample P Error: %d\n",
						     samplepoint_error
						     [index]);
						info
						    ("clk           : %d\n",
						     clk);

#endif	/*  */
						    index++;
					}
				}
			}
		}
	}

	    // mark all baudrate_error entries which are outer limits
	    index = 0;
	for (clk = 0; clk < 5; clk++) {
		for (brp = 0; brp < 16; brp++) {
			for (pr = 0; pr < 8; pr++) {
				for (ph1 = 0; ph1 < 8; ph1++) {
					for (ph2 = 0; ph2 < 8; ph2++) {
						if ((baudrate_error[index]
						      >
						      BAUDRATE_TOLERANCE_PERCENT)
						     ||
						     (samplepoint_error
						       [index] >
						       SAMPLEPOINT_TOLERANCE_PERCENT)
						     ||
						     (samplepoint_m16c
						       (brp, pr, ph1,
							ph2) >
						       SAMPLEPOINT_UPPER_LIMIT))
						{
							baudrate_error
							    [index] = -1;
						} else
						    if (((1 + pr + 1 +
							  ph1 + 1 + ph2 +
							  1) < 8)
							||
							((1 + pr + 1 +
							  ph1 + 1 + ph2 +
							  1) > 25)) {
							baudrate_error
							    [index] = -1;
						}

#if 0
						    else {
							info
							    ("Baudrate      : %d kBaud\n",
							     baudrate_m16c
							     (clk, brp, pr,
							      ph1, ph2));
							info
							    ("Baudrate Error: %d\n",
							     baudrate_error
							     [index]);
							info
							    ("Sample P Error: %d\n",
							     samplepoint_error
							     [index]);
						}

#endif	/*  */
						    index++;
					}
				}
			}
		}
	}

	    // find list of minimum of baudrate_error within unmarked entries
	    clk_merk = brp_merk = pr_merk = ph1_merk = ph2_merk = 0;
	baudrate_error_merk = 100;
	index = 0;
	for (clk = 0; clk < 5; clk++) {
		for (brp = 0; brp < 16; brp++) {
			for (pr = 0; pr < 8; pr++) {
				for (ph1 = 0; ph1 < 8; ph1++) {
					for (ph2 = 0; ph2 < 8; ph2++) {
						if (baudrate_error[index]
						     != -1) {
							if (baudrate_error
							     [index] <
							     baudrate_error_merk)
							{
								baudrate_error_merk
								    =
								    baudrate_error
								    [index];
								brp_merk =
								    brp;
								pr_merk =
								    pr;
								ph1_merk =
								    ph1;
								ph2_merk =
								    ph2;
								clk_merk =
								    clk;

#if 0
								    info
								    ("brp: %2.2Xh pr: %2.2Xh ph1: %2.2Xh ph2: %2.2Xh\n",
								     brp,
								     pr,
								     ph1,
								     ph2);
								info
								    ("Baudrate      : %d kBaud\n",
								     baudrate_m16c
								     (clk,
								      brp,
								      pr,
								      ph1,
								      ph2));
								info
								    ("Baudrate Error: %d\n",
								     baudrate_error
								     [index]);
								info
								    ("Sample P Error: %d\n",
								     samplepoint_error
								     [index]);

#endif	/*  */
							}
						}
						index++;
					}
				}
			}
		}
	}
	if (baudrate_error_merk == 100) {
		info("ERROR: Could not convert CAN init parameter\n");
		vfree(baudrate_error);
		vfree(samplepoint_error);
		return -1;
	}

	    // setting m16c CAN parameter
	    c0con.bc0con.brp = brp_merk;
	c0con.bc0con.pr = pr_merk;
	c1con.bc1con.ph1 = ph1_merk;
	c1con.bc1con.ph2 = ph2_merk;

#ifdef _DEBUG_OUTPUT_CAN_PARAMS
	    info("\nResulting M16C CAN params\n");
	info("-------------------------\n");
	info("clk     : %2.2Xh\n", clk_merk);
	info("ph1     : %2.2Xh ph2: %2.2Xh\n", c1con.bc1con.ph1 + 1,
	      c1con.bc1con.ph2 + 1);
	info("pr      : %2.2Xh brp: %2.2Xh\n", c0con.bc0con.pr + 1,
	      c0con.bc0con.brp + 1);
	info("sjw     : %2.2Xh sam: %2.2Xh\n", c1con.bc1con.sjw,
	      c0con.bc0con.sam);
	info("co1     : %2.2Xh co0: %2.2Xh\n", c1con.c1con, c0con.c0con);
	info("Baudrate: %d.%dBaud\n",
	       baudrate_m16c(clk_merk, c0con.bc0con.brp, c0con.bc0con.pr,
			     c1con.bc1con.ph1, c1con.bc1con.ph2) / 1000,
	       baudrate_m16c(clk_merk, c0con.bc0con.brp, c0con.bc0con.pr,
			      c1con.bc1con.ph1, c1con.bc1con.ph2) % 1000);
	info("Sample P: 0.%d\n",
	      samplepoint_m16c(c0con.bc0con.brp, c0con.bc0con.pr,
			       c1con.bc1con.ph1, c1con.bc1con.ph2));
	info("\n");

#endif	/*  */
	    out = in;
	out->type = 6;
	out->length = sizeof(CPC_M16C_BASIC_PARAMS_T) + 1;
	out->msg.canparams.cc_type = M16C_BASIC;
	out->msg.canparams.cc_params.m16c_basic.con0 = c0con.c0con;
	out->msg.canparams.cc_params.m16c_basic.con1 = c1con.c1con;
	out->msg.canparams.cc_params.m16c_basic.ctlr0 = 0x4C;
	out->msg.canparams.cc_params.m16c_basic.ctlr1 = 0x00;
	out->msg.canparams.cc_params.m16c_basic.clk = clk_merk;
	out->msg.canparams.cc_params.m16c_basic.acc_std_code0 =
	    acc_code0;
	out->msg.canparams.cc_params.m16c_basic.acc_std_code1 = acc_code1;

//      info("code0: 0x%2.2X, code1: 0x%2.2X\n", out->msg.canparams.cc_params.m16c_basic.acc_std_code0, out->msg.canparams.cc_params.m16c_basic.acc_std_code1);
	    tmpAccCode = (acc_code1 >> 5) + (acc_code0 << 3);
	out->msg.canparams.cc_params.m16c_basic.acc_std_code0 =
	    (unsigned char) tmpAccCode;
	out->msg.canparams.cc_params.m16c_basic.acc_std_code1 =
	    (unsigned char) (tmpAccCode >> 8);

//      info("code0: 0x%2.2X, code1: 0x%2.2X\n", out->msg.canparams.cc_params.m16c_basic.acc_std_code0, out->msg.canparams.cc_params.m16c_basic.acc_std_code1);
	    out->msg.canparams.cc_params.m16c_basic.acc_std_mask0 =
	    ~acc_mask0;
	out->msg.canparams.cc_params.m16c_basic.acc_std_mask1 =
	    ~acc_mask1;

//      info("mask0: 0x%2.2X, mask1: 0x%2.2X\n", out->msg.canparams.cc_params.m16c_basic.acc_std_mask0, out->msg.canparams.cc_params.m16c_basic.acc_std_mask1);
	    tmpAccMask = ((acc_mask1) >> 5) + ((acc_mask0) << 3);

//      info("tmpAccMask: 0x%4.4X\n", tmpAccMask);
	    out->msg.canparams.cc_params.m16c_basic.acc_std_mask0 =
	    (unsigned char) ~tmpAccMask;
	out->msg.canparams.cc_params.m16c_basic.acc_std_mask1 =
	    (unsigned char) ~(tmpAccMask >> 8);

//      info("mask0: 0x%2.2X, mask1: 0x%2.2X\n", out->msg.canparams.cc_params.m16c_basic.acc_std_mask0, out->msg.canparams.cc_params.m16c_basic.acc_std_mask1);
	    out->msg.canparams.cc_params.m16c_basic.acc_ext_code0 =
	    (unsigned char) tmpAccCode;
	out->msg.canparams.cc_params.m16c_basic.acc_ext_code1 =
	    (unsigned char) (tmpAccCode >> 8);
	out->msg.canparams.cc_params.m16c_basic.acc_ext_code2 = acc_code2;
	out->msg.canparams.cc_params.m16c_basic.acc_ext_code3 = acc_code3;
	out->msg.canparams.cc_params.m16c_basic.acc_ext_mask0 =
	    (unsigned char) ~tmpAccMask;
	out->msg.canparams.cc_params.m16c_basic.acc_ext_mask1 =
	    (unsigned char) ~(tmpAccMask >> 8);
	out->msg.canparams.cc_params.m16c_basic.acc_ext_mask2 =
	    ~acc_mask2;
	out->msg.canparams.cc_params.m16c_basic.acc_ext_mask3 =
	    ~acc_mask3;
	vfree(baudrate_error);
	vfree(samplepoint_error);
	return 0;
}


