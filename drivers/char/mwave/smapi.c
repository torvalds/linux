/*
*
* smapi.c -- SMAPI interface routines
*
*
* Written By: Mike Sullivan IBM Corporation
*
* Copyright (C) 1999 IBM Corporation
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* NO WARRANTY
* THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
* LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
* solely responsible for determining the appropriateness of using and
* distributing the Program and assumes all risks associated with its
* exercise of rights under this Agreement, including but not limited to
* the risks and costs of program errors, damage to or loss of data,
* programs or equipment, and unavailability or interruption of operations.
*
* DISCLAIMER OF LIABILITY
* NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
* USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
* HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*
* 10/23/2000 - Alpha Release
*	First release to the public
*/

#include <linux/kernel.h>
#include <linux/mc146818rtc.h>	/* CMOS defines */
#include "smapi.h"
#include "mwavedd.h"

static unsigned short g_usSmapiPort = 0;


static int smapi_request(unsigned short inBX, unsigned short inCX,
			 unsigned short inDI, unsigned short inSI,
			 unsigned short *outAX, unsigned short *outBX,
			 unsigned short *outCX, unsigned short *outDX,
			 unsigned short *outDI, unsigned short *outSI)
{
	unsigned short myoutAX = 2, *pmyoutAX = &myoutAX;
	unsigned short myoutBX = 3, *pmyoutBX = &myoutBX;
	unsigned short myoutCX = 4, *pmyoutCX = &myoutCX;
	unsigned short myoutDX = 5, *pmyoutDX = &myoutDX;
	unsigned short myoutDI = 6, *pmyoutDI = &myoutDI;
	unsigned short myoutSI = 7, *pmyoutSI = &myoutSI;
	unsigned short usSmapiOK = -EIO, *pusSmapiOK = &usSmapiOK;
	unsigned int inBXCX = (inBX << 16) | inCX;
	unsigned int inDISI = (inDI << 16) | inSI;
	int retval = 0;

	PRINTK_5(TRACE_SMAPI, "inBX %x inCX %x inDI %x inSI %x\n",
		inBX, inCX, inDI, inSI);

	__asm__ __volatile__("movw  $0x5380,%%ax\n\t"
			    "movl  %7,%%ebx\n\t"
			    "shrl  $16, %%ebx\n\t"
			    "movw  %7,%%cx\n\t"
			    "movl  %8,%%edi\n\t"
			    "shrl  $16,%%edi\n\t"
			    "movw  %8,%%si\n\t"
			    "movw  %9,%%dx\n\t"
			    "out   %%al,%%dx\n\t"
			    "out   %%al,$0x4F\n\t"
			    "cmpb  $0x53,%%ah\n\t"
			    "je    2f\n\t"
			    "1:\n\t"
			    "orb   %%ah,%%ah\n\t"
			    "jnz   2f\n\t"
			    "movw  %%ax,%0\n\t"
			    "movw  %%bx,%1\n\t"
			    "movw  %%cx,%2\n\t"
			    "movw  %%dx,%3\n\t"
			    "movw  %%di,%4\n\t"
			    "movw  %%si,%5\n\t"
			    "movw  $1,%6\n\t"
			    "2:\n\t":"=m"(*(unsigned short *) pmyoutAX),
			    "=m"(*(unsigned short *) pmyoutBX),
			    "=m"(*(unsigned short *) pmyoutCX),
			    "=m"(*(unsigned short *) pmyoutDX),
			    "=m"(*(unsigned short *) pmyoutDI),
			    "=m"(*(unsigned short *) pmyoutSI),
			    "=m"(*(unsigned short *) pusSmapiOK)
			    :"m"(inBXCX), "m"(inDISI), "m"(g_usSmapiPort)
			    :"%eax", "%ebx", "%ecx", "%edx", "%edi",
			    "%esi");

	PRINTK_8(TRACE_SMAPI,
		"myoutAX %x myoutBX %x myoutCX %x myoutDX %x myoutDI %x myoutSI %x usSmapiOK %x\n",
		myoutAX, myoutBX, myoutCX, myoutDX, myoutDI, myoutSI,
		usSmapiOK);
	*outAX = myoutAX;
	*outBX = myoutBX;
	*outCX = myoutCX;
	*outDX = myoutDX;
	*outDI = myoutDI;
	*outSI = myoutSI;

	retval = (usSmapiOK == 1) ? 0 : -EIO;
	PRINTK_2(TRACE_SMAPI, "smapi::smapi_request exit retval %x\n", retval);
	return retval;
}


int smapi_query_DSP_cfg(SMAPI_DSP_SETTINGS * pSettings)
{
	int bRC = -EIO;
	unsigned short usAX, usBX, usCX, usDX, usDI, usSI;
	unsigned short ausDspBases[] = { 0x0030, 0x4E30, 0x8E30, 0xCE30, 0x0130, 0x0350, 0x0070, 0x0DB0 };
	unsigned short ausUartBases[] = { 0x03F8, 0x02F8, 0x03E8, 0x02E8 };
	unsigned short numDspBases = 8;
	unsigned short numUartBases = 4;

	PRINTK_1(TRACE_SMAPI, "smapi::smapi_query_DSP_cfg entry\n");

	bRC = smapi_request(0x1802, 0x0000, 0, 0,
		&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
	if (bRC) {
		PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_query_DSP_cfg: Error: Could not get DSP Settings. Aborting.\n");
		return bRC;
	}

	PRINTK_1(TRACE_SMAPI, "smapi::smapi_query_DSP_cfg, smapi_request OK\n");

	pSettings->bDSPPresent = ((usBX & 0x0100) != 0);
	pSettings->bDSPEnabled = ((usCX & 0x0001) != 0);
	pSettings->usDspIRQ = usSI & 0x00FF;
	pSettings->usDspDMA = (usSI & 0xFF00) >> 8;
	if ((usDI & 0x00FF) < numDspBases) {
		pSettings->usDspBaseIO = ausDspBases[usDI & 0x00FF];
	} else {
		pSettings->usDspBaseIO = 0;
	}
	PRINTK_6(TRACE_SMAPI,
		"smapi::smapi_query_DSP_cfg get DSP Settings bDSPPresent %x bDSPEnabled %x usDspIRQ %x usDspDMA %x usDspBaseIO %x\n",
		pSettings->bDSPPresent, pSettings->bDSPEnabled,
		pSettings->usDspIRQ, pSettings->usDspDMA,
		pSettings->usDspBaseIO);

	/* check for illegal values */
	if ( pSettings->usDspBaseIO == 0 ) 
		PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_query_DSP_cfg: Worry: DSP base I/O address is 0\n");
	if ( pSettings->usDspIRQ == 0 )
		PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_query_DSP_cfg: Worry: DSP IRQ line is 0\n");

	bRC = smapi_request(0x1804, 0x0000, 0, 0,
	   	&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
	if (bRC) {
		PRINTK_ERROR("smapi::smapi_query_DSP_cfg: Error: Could not get DSP modem settings. Aborting.\n");
		return bRC;
	} 

	PRINTK_1(TRACE_SMAPI, "smapi::smapi_query_DSP_cfg, smapi_request OK\n");

	pSettings->bModemEnabled = ((usCX & 0x0001) != 0);
	pSettings->usUartIRQ = usSI & 0x000F;
	if (((usSI & 0xFF00) >> 8) < numUartBases) {
		pSettings->usUartBaseIO = ausUartBases[(usSI & 0xFF00) >> 8];
	} else {
		pSettings->usUartBaseIO = 0;
	}

	PRINTK_4(TRACE_SMAPI,
		"smapi::smapi_query_DSP_cfg get DSP modem settings bModemEnabled %x usUartIRQ %x usUartBaseIO %x\n",
		pSettings->bModemEnabled,
		pSettings->usUartIRQ,
		pSettings->usUartBaseIO);

	/* check for illegal values */
	if ( pSettings->usUartBaseIO == 0 ) 
		PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_query_DSP_cfg: Worry: UART base I/O address is 0\n");
	if ( pSettings->usUartIRQ == 0 )
		PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_query_DSP_cfg: Worry: UART IRQ line is 0\n");

	PRINTK_2(TRACE_SMAPI, "smapi::smapi_query_DSP_cfg exit bRC %x\n", bRC);

	return bRC;
}


int smapi_set_DSP_cfg(void)
{
	int bRC = -EIO;
	int i;
	unsigned short usAX, usBX, usCX, usDX, usDI, usSI;
	unsigned short ausDspBases[] = { 0x0030, 0x4E30, 0x8E30, 0xCE30, 0x0130, 0x0350, 0x0070, 0x0DB0 };
	unsigned short ausUartBases[] = { 0x03F8, 0x02F8, 0x03E8, 0x02E8 };
	unsigned short ausDspIrqs[] = { 5, 7, 10, 11, 15 };
	unsigned short ausUartIrqs[] = { 3, 4 };

	unsigned short numDspBases = 8;
	unsigned short numUartBases = 4;
	unsigned short numDspIrqs = 5;
	unsigned short numUartIrqs = 2;
	unsigned short dspio_index = 0, uartio_index = 0;

	PRINTK_5(TRACE_SMAPI,
		"smapi::smapi_set_DSP_cfg entry mwave_3780i_irq %x mwave_3780i_io %x mwave_uart_irq %x mwave_uart_io %x\n",
		mwave_3780i_irq, mwave_3780i_io, mwave_uart_irq, mwave_uart_io);

	if (mwave_3780i_io) {
		for (i = 0; i < numDspBases; i++) {
			if (mwave_3780i_io == ausDspBases[i])
				break;
		}
		if (i == numDspBases) {
			PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_set_DSP_cfg: Error: Invalid mwave_3780i_io address %x. Aborting.\n", mwave_3780i_io);
			return bRC;
		}
		dspio_index = i;
	}

	if (mwave_3780i_irq) {
		for (i = 0; i < numDspIrqs; i++) {
			if (mwave_3780i_irq == ausDspIrqs[i])
				break;
		}
		if (i == numDspIrqs) {
			PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_set_DSP_cfg: Error: Invalid mwave_3780i_irq %x. Aborting.\n", mwave_3780i_irq);
			return bRC;
		}
	}

	if (mwave_uart_io) {
		for (i = 0; i < numUartBases; i++) {
			if (mwave_uart_io == ausUartBases[i])
				break;
		}
		if (i == numUartBases) {
			PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_set_DSP_cfg: Error: Invalid mwave_uart_io address %x. Aborting.\n", mwave_uart_io);
			return bRC;
		}
		uartio_index = i;
	}


	if (mwave_uart_irq) {
		for (i = 0; i < numUartIrqs; i++) {
			if (mwave_uart_irq == ausUartIrqs[i])
				break;
		}
		if (i == numUartIrqs) {
			PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_set_DSP_cfg: Error: Invalid mwave_uart_irq %x. Aborting.\n", mwave_uart_irq);
			return bRC;
		}
	}

	if (mwave_uart_irq || mwave_uart_io) {

		/* Check serial port A */
		bRC = smapi_request(0x1402, 0x0000, 0, 0,
			&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
		if (bRC) goto exit_smapi_request_error;
		/* bRC == 0 */
		if (usBX & 0x0100) {	/* serial port A is present */
			if (usCX & 1) {	/* serial port is enabled */
				if ((usSI & 0xFF) == mwave_uart_irq) {
#ifndef MWAVE_FUTZ_WITH_OTHER_DEVICES
					PRINTK_ERROR(KERN_ERR_MWAVE
						"smapi::smapi_set_DSP_cfg: Serial port A irq %x conflicts with mwave_uart_irq %x\n", usSI & 0xFF, mwave_uart_irq);
#else
					PRINTK_3(TRACE_SMAPI,
						"smapi::smapi_set_DSP_cfg: Serial port A irq %x conflicts with mwave_uart_irq %x\n", usSI & 0xFF, mwave_uart_irq);
#endif
#ifdef MWAVE_FUTZ_WITH_OTHER_DEVICES
					PRINTK_1(TRACE_SMAPI,
						"smapi::smapi_set_DSP_cfg Disabling conflicting serial port\n");
					bRC = smapi_request(0x1403, 0x0100, 0, usSI,
						&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
					if (bRC) goto exit_smapi_request_error;
					bRC = smapi_request(0x1402, 0x0000, 0, 0,
						&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
					if (bRC) goto exit_smapi_request_error;
#else
					goto exit_conflict;
#endif
				} else {
					if ((usSI >> 8) == uartio_index) {
#ifndef MWAVE_FUTZ_WITH_OTHER_DEVICES
						PRINTK_ERROR(KERN_ERR_MWAVE
							"smapi::smapi_set_DSP_cfg: Serial port A base I/O address %x conflicts with mwave uart I/O %x\n", ausUartBases[usSI >> 8], ausUartBases[uartio_index]);
#else
						PRINTK_3(TRACE_SMAPI,
							"smapi::smapi_set_DSP_cfg: Serial port A base I/O address %x conflicts with mwave uart I/O %x\n", ausUartBases[usSI >> 8], ausUartBases[uartio_index]);
#endif
#ifdef MWAVE_FUTZ_WITH_OTHER_DEVICES
						PRINTK_1(TRACE_SMAPI,
							"smapi::smapi_set_DSP_cfg Disabling conflicting serial port A\n");
						bRC = smapi_request (0x1403, 0x0100, 0, usSI,
							&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
						if (bRC) goto exit_smapi_request_error;
						bRC = smapi_request (0x1402, 0x0000, 0, 0,
							&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
						if (bRC) goto exit_smapi_request_error;
#else
						goto exit_conflict;
#endif
					}
				}
			}
		}

		/* Check serial port B */
		bRC = smapi_request(0x1404, 0x0000, 0, 0,
			&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
		if (bRC) goto exit_smapi_request_error;
		/* bRC == 0 */
		if (usBX & 0x0100) {	/* serial port B is present */
			if (usCX & 1) {	/* serial port is enabled */
				if ((usSI & 0xFF) == mwave_uart_irq) {
#ifndef MWAVE_FUTZ_WITH_OTHER_DEVICES
					PRINTK_ERROR(KERN_ERR_MWAVE
						"smapi::smapi_set_DSP_cfg: Serial port B irq %x conflicts with mwave_uart_irq %x\n", usSI & 0xFF, mwave_uart_irq);
#else
					PRINTK_3(TRACE_SMAPI,
						"smapi::smapi_set_DSP_cfg: Serial port B irq %x conflicts with mwave_uart_irq %x\n", usSI & 0xFF, mwave_uart_irq);
#endif
#ifdef MWAVE_FUTZ_WITH_OTHER_DEVICES
					PRINTK_1(TRACE_SMAPI,
						"smapi::smapi_set_DSP_cfg Disabling conflicting serial port B\n");
					bRC = smapi_request(0x1405, 0x0100, 0, usSI,
						&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
					if (bRC) goto exit_smapi_request_error;
					bRC = smapi_request(0x1404, 0x0000, 0, 0,
						&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
					if (bRC) goto exit_smapi_request_error;
#else
					goto exit_conflict;
#endif
				} else {
					if ((usSI >> 8) == uartio_index) {
#ifndef MWAVE_FUTZ_WITH_OTHER_DEVICES
						PRINTK_ERROR(KERN_ERR_MWAVE
							"smapi::smapi_set_DSP_cfg: Serial port B base I/O address %x conflicts with mwave uart I/O %x\n", ausUartBases[usSI >> 8], ausUartBases[uartio_index]);
#else
						PRINTK_3(TRACE_SMAPI,
							"smapi::smapi_set_DSP_cfg: Serial port B base I/O address %x conflicts with mwave uart I/O %x\n", ausUartBases[usSI >> 8], ausUartBases[uartio_index]);
#endif
#ifdef MWAVE_FUTZ_WITH_OTHER_DEVICES
						PRINTK_1 (TRACE_SMAPI,
						    "smapi::smapi_set_DSP_cfg Disabling conflicting serial port B\n");
						bRC = smapi_request (0x1405, 0x0100, 0, usSI,
							&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
						if (bRC) goto exit_smapi_request_error;
						bRC = smapi_request (0x1404, 0x0000, 0, 0,
							&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
						if (bRC) goto exit_smapi_request_error;
#else
						goto exit_conflict;
#endif
					}
				}
			}
		}

		/* Check IR port */
		bRC = smapi_request(0x1700, 0x0000, 0, 0,
			&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
		if (bRC) goto exit_smapi_request_error;
		bRC = smapi_request(0x1704, 0x0000, 0, 0,
			&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
		if (bRC) goto exit_smapi_request_error;
		/* bRC == 0 */
		if ((usCX & 0xff) != 0xff) { /* IR port not disabled */
			if ((usCX & 0xff) == mwave_uart_irq) {
#ifndef MWAVE_FUTZ_WITH_OTHER_DEVICES
				PRINTK_ERROR(KERN_ERR_MWAVE
					"smapi::smapi_set_DSP_cfg: IR port irq %x conflicts with mwave_uart_irq %x\n", usCX & 0xff, mwave_uart_irq);
#else
				PRINTK_3(TRACE_SMAPI,
					"smapi::smapi_set_DSP_cfg: IR port irq %x conflicts with mwave_uart_irq %x\n", usCX & 0xff, mwave_uart_irq);
#endif
#ifdef MWAVE_FUTZ_WITH_OTHER_DEVICES
				PRINTK_1(TRACE_SMAPI,
					"smapi::smapi_set_DSP_cfg Disabling conflicting IR port\n");
				bRC = smapi_request(0x1701, 0x0100, 0, 0,
					&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
				if (bRC) goto exit_smapi_request_error;
				bRC = smapi_request(0x1700, 0, 0, 0,
					&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
				if (bRC) goto exit_smapi_request_error;
				bRC = smapi_request(0x1705, 0x01ff, 0, usSI,
					&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
				if (bRC) goto exit_smapi_request_error;
				bRC = smapi_request(0x1704, 0x0000, 0, 0,
					&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
				if (bRC) goto exit_smapi_request_error;
#else
				goto exit_conflict;
#endif
			} else {
				if ((usSI & 0xff) == uartio_index) {
#ifndef MWAVE_FUTZ_WITH_OTHER_DEVICES
					PRINTK_ERROR(KERN_ERR_MWAVE
						"smapi::smapi_set_DSP_cfg: IR port base I/O address %x conflicts with mwave uart I/O %x\n", ausUartBases[usSI & 0xff], ausUartBases[uartio_index]);
#else
					PRINTK_3(TRACE_SMAPI,
						"smapi::smapi_set_DSP_cfg: IR port base I/O address %x conflicts with mwave uart I/O %x\n", ausUartBases[usSI & 0xff], ausUartBases[uartio_index]);
#endif
#ifdef MWAVE_FUTZ_WITH_OTHER_DEVICES
					PRINTK_1(TRACE_SMAPI,
						"smapi::smapi_set_DSP_cfg Disabling conflicting IR port\n");
					bRC = smapi_request(0x1701, 0x0100, 0, 0,
						&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
					if (bRC) goto exit_smapi_request_error;
					bRC = smapi_request(0x1700, 0, 0, 0,
						&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
					if (bRC) goto exit_smapi_request_error;
					bRC = smapi_request(0x1705, 0x01ff, 0, usSI,
						&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
					if (bRC) goto exit_smapi_request_error;
					bRC = smapi_request(0x1704, 0x0000, 0, 0,
						&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
					if (bRC) goto exit_smapi_request_error;
#else
					goto exit_conflict;
#endif
				}
			}
		}
	}

	bRC = smapi_request(0x1802, 0x0000, 0, 0,
		&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
	if (bRC) goto exit_smapi_request_error;

	if (mwave_3780i_io) {
		usDI = dspio_index;
	}
	if (mwave_3780i_irq) {
		usSI = (usSI & 0xff00) | mwave_3780i_irq;
	}

	bRC = smapi_request(0x1803, 0x0101, usDI, usSI,
		&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
	if (bRC) goto exit_smapi_request_error;

	bRC = smapi_request(0x1804, 0x0000, 0, 0,
		&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
	if (bRC) goto exit_smapi_request_error;

	if (mwave_uart_io) {
		usSI = (usSI & 0x00ff) | (uartio_index << 8);
	}
	if (mwave_uart_irq) {
		usSI = (usSI & 0xff00) | mwave_uart_irq;
	}
	bRC = smapi_request(0x1805, 0x0101, 0, usSI,
		&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
	if (bRC) goto exit_smapi_request_error;

	bRC = smapi_request(0x1802, 0x0000, 0, 0,
		&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
	if (bRC) goto exit_smapi_request_error;

	bRC = smapi_request(0x1804, 0x0000, 0, 0,
		&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);
	if (bRC) goto exit_smapi_request_error;

/* normal exit: */
	PRINTK_1(TRACE_SMAPI, "smapi::smapi_set_DSP_cfg exit\n");
	return 0;

exit_conflict:
	/* Message has already been printed */
	return -EIO;

exit_smapi_request_error:
	PRINTK_ERROR(KERN_ERR_MWAVE "smapi::smapi_set_DSP_cfg exit on smapi_request error bRC %x\n", bRC);
	return bRC;
}


int smapi_set_DSP_power_state(BOOLEAN bOn)
{
	int bRC = -EIO;
	unsigned short usAX, usBX, usCX, usDX, usDI, usSI;
	unsigned short usPowerFunction;

	PRINTK_2(TRACE_SMAPI, "smapi::smapi_set_DSP_power_state entry bOn %x\n", bOn);

	usPowerFunction = (bOn) ? 1 : 0;

	bRC = smapi_request(0x4901, 0x0000, 0, usPowerFunction,
		&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);

	PRINTK_2(TRACE_SMAPI, "smapi::smapi_set_DSP_power_state exit bRC %x\n", bRC);

	return bRC;
}

#if 0
static int SmapiQuerySystemID(void)
{
	int bRC = -EIO;
	unsigned short usAX = 0xffff, usBX = 0xffff, usCX = 0xffff,
		usDX = 0xffff, usDI = 0xffff, usSI = 0xffff;

	printk("smapi::SmapiQUerySystemID entry\n");
	bRC = smapi_request(0x0000, 0, 0, 0,
		&usAX, &usBX, &usCX, &usDX, &usDI, &usSI);

	if (bRC == 0) {
		printk("AX=%x, BX=%x, CX=%x, DX=%x, DI=%x, SI=%x\n",
			usAX, usBX, usCX, usDX, usDI, usSI);
	} else {
		printk("smapi::SmapiQuerySystemID smapi_request error\n");
	}

	return bRC;
}
#endif  /*  0  */

int smapi_init(void)
{
	int retval = -EIO;
	unsigned short usSmapiID = 0;
	unsigned long flags;

	PRINTK_1(TRACE_SMAPI, "smapi::smapi_init entry\n");

	spin_lock_irqsave(&rtc_lock, flags);
	usSmapiID = CMOS_READ(0x7C);
	usSmapiID |= (CMOS_READ(0x7D) << 8);
	spin_unlock_irqrestore(&rtc_lock, flags);
	PRINTK_2(TRACE_SMAPI, "smapi::smapi_init usSmapiID %x\n", usSmapiID);

	if (usSmapiID == 0x5349) {
		spin_lock_irqsave(&rtc_lock, flags);
		g_usSmapiPort = CMOS_READ(0x7E);
		g_usSmapiPort |= (CMOS_READ(0x7F) << 8);
		spin_unlock_irqrestore(&rtc_lock, flags);
		if (g_usSmapiPort == 0) {
			PRINTK_ERROR("smapi::smapi_init, ERROR unable to read from SMAPI port\n");
		} else {
			PRINTK_2(TRACE_SMAPI,
				"smapi::smapi_init, exit TRUE g_usSmapiPort %x\n",
				g_usSmapiPort);
			retval = 0;
			//SmapiQuerySystemID();
		}
	} else {
		PRINTK_ERROR("smapi::smapi_init, ERROR invalid usSmapiID\n");
		retval = -ENXIO;
	}

	return retval;
}
