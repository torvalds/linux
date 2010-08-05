/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef _BATYPE_H_
#define _BATYPE_H_

#define 	TOTAL_TXBA_NUM	16
#define	TOTAL_RXBA_NUM	16

#define	BA_SETUP_TIMEOUT	200
#define	BA_INACT_TIMEOUT	60000

#define	BA_POLICY_DELAYED		0
#define	BA_POLICY_IMMEDIATE	1

#define	ADDBA_STATUS_SUCCESS			0
#define	ADDBA_STATUS_REFUSED		37
#define	ADDBA_STATUS_INVALID_PARAM	38

#define	DELBA_REASON_QSTA_LEAVING	36
#define	DELBA_REASON_END_BA			37
#define	DELBA_REASON_UNKNOWN_BA	38
#define	DELBA_REASON_TIMEOUT			39

typedef union _SEQUENCE_CONTROL{
	u16 ShortData;
	struct
	{
		u16	FragNum:4;
		u16	SeqNum:12;
	}field;
}SEQUENCE_CONTROL, *PSEQUENCE_CONTROL;

typedef union _BA_PARAM_SET {
	u8 charData[2];
	u16 shortData;
	struct {
		u16 AMSDU_Support:1;
		u16 BAPolicy:1;
		u16 TID:4;
		u16 BufferSize:10;
	} field;
} BA_PARAM_SET, *PBA_PARAM_SET;

typedef union _DELBA_PARAM_SET {
	u8 charData[2];
	u16 shortData;
	struct {
		u16 Reserved:11;
		u16 Initiator:1;
		u16 TID:4;
	} field;
} DELBA_PARAM_SET, *PDELBA_PARAM_SET;

typedef struct _BA_RECORD {
	struct timer_list		Timer;
	u8				bValid;
	u8				DialogToken;
	BA_PARAM_SET		BaParamSet;
	u16				BaTimeoutValue;
	SEQUENCE_CONTROL	BaStartSeqCtrl;
} BA_RECORD, *PBA_RECORD;

#endif
