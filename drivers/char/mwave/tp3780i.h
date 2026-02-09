/*
*
* tp3780i.h -- declarations for tp3780i.c
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

#ifndef _LINUX_TP3780I_H
#define _LINUX_TP3780I_H

#include <asm/io.h>
#include "mwavepub.h"


/* DSP abilities constants for 3780i based Thinkpads */
#define TP_ABILITIES_INTS_PER_SEC       39160800
#define TP_ABILITIES_DATA_SIZE          32768
#define TP_ABILITIES_INST_SIZE          32768
#define TP_ABILITIES_MWAVEOS_NAME       "mwaveos0700.dsp"
#define TP_ABILITIES_BIOSTASK_NAME      "mwbio701.dsp"


/* DSP configuration values for 3780i based Thinkpads */
#define TP_CFG_NumTransfers     3	/* 16 transfers */
#define TP_CFG_RerequestTimer   1	/* 2 usec */
#define TP_CFG_MEMCS16          0	/* Disabled, 16-bit memory assumed */
#define TP_CFG_IsaMemCmdWidth   3	/* 295 nsec (16-bit) */
#define TP_CFG_GateIOCHRDY      0	/* No IOCHRDY gating */
#define TP_CFG_EnablePwrMgmt    1	/* Enable low poser suspend/resume */
#define TP_CFG_HBusTimerValue 255	/* HBus timer load value */
#define TP_CFG_DisableLBusTimeout 0	/* Enable LBus timeout */
#define TP_CFG_N_Divisor       32	/* Clock = 39.1608 Mhz */
#define TP_CFG_M_Multiplier    37	/* " */
#define TP_CFG_PllBypass        0	/* don't bypass */
#define TP_CFG_ChipletEnable 0xFFFF	/* Enable all chiplets */

struct thinkpad_bd_data {
	int bDSPEnabled;
	int bShareDspIrq;
	int bShareUartIrq;
	struct dsp_3780i_config_settings rDspSettings;
};

int tp3780I_InitializeBoardData(struct thinkpad_bd_data *pBDData);
int tp3780I_CalcResources(struct thinkpad_bd_data *pBDData);
int tp3780I_ClaimResources(struct thinkpad_bd_data *pBDData);
int tp3780I_ReleaseResources(struct thinkpad_bd_data *pBDData);
int tp3780I_EnableDSP(struct thinkpad_bd_data *pBDData);
int tp3780I_DisableDSP(struct thinkpad_bd_data *pBDData);
int tp3780I_ResetDSP(struct thinkpad_bd_data *pBDData);
int tp3780I_StartDSP(struct thinkpad_bd_data *pBDData);
int tp3780I_QueryAbilities(struct thinkpad_bd_data *pBDData, struct mw_abilities *pAbilities);
void tp3780I_Cleanup(struct thinkpad_bd_data *pBDData);
int tp3780I_ReadWriteDspDStore(struct thinkpad_bd_data *pBDData, unsigned int uOpcode,
                               void __user *pvBuffer, unsigned int uCount,
                               unsigned long ulDSPAddr);
int tp3780I_ReadWriteDspIStore(struct thinkpad_bd_data *pBDData, unsigned int uOpcode,
                               void __user *pvBuffer, unsigned int uCount,
                               unsigned long ulDSPAddr);


#endif
