/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef _AP_DIVERSITY_H_
#define _AP_DIVERSITY_H_

#include "rtmp.h"

#define ADDBGPRINT(format,args...)	do{if(atomic_read(&DEBUG_VERBOSE_MODE))	printk( format, ##args);}while(0)
#define PROC_DIR	"AntDiv"

/*
 *		TAB width:8
 */

/*
 *  For shorter udelay().
 *  (ripped from rtmp.h)
 */
/*#define RTMP_BBP_IO_READ8_BY_REG_ID(_A, _I, _pV)    {} */
/* Read BBP register by register's ID. Generate PER to test BA */
#define RTMP_BBP_IO_READ8_BY_REG_ID_SHORT_DELAY(_A, _I, _pV)		\
{									\
	BBP_CSR_CFG_STRUC  BbpCsr;					\
	int	i, k;							\
	if ((_A)->bPCIclkOff == FALSE)                     		\
	{                                                   		\
		for (i=0; i<MAX_BUSY_COUNT; i++)                    	\
		{                                                   	\
			RTMP_IO_READ32(_A, H2M_BBP_AGENT, &BbpCsr.word);\
			if (BbpCsr.field.Busy == BUSY)			\
				continue;                               \
			BbpCsr.word = 0;                                \
			BbpCsr.field.fRead = 1;                         \
			BbpCsr.field.BBP_RW_MODE = 1;                   \
			BbpCsr.field.Busy = 1;                          \
			BbpCsr.field.RegNum = _I;                       \
			RTMP_IO_WRITE32(_A, H2M_BBP_AGENT, BbpCsr.word);\
			AsicSendCommandToMcu(_A, 0x80, 0xff, 0x0, 0x0);	\
			RTMPusecDelay(10);				\
			for (k=0; k<MAX_BUSY_COUNT; k++)		\
			{                                               \
				RTMP_IO_READ32(_A, H2M_BBP_AGENT, &BbpCsr.word);			\
				if (BbpCsr.field.Busy == IDLE)          \
					break;                          \
				else					\
					printk("MCU busy\n");		\
			}                                               \
			if ((BbpCsr.field.Busy == IDLE) &&              \
				(BbpCsr.field.RegNum == _I))            \
			{                                               \
				*(_pV) = (UCHAR)BbpCsr.field.Value;     \
				break;                                  \
			}                                               \
		}                                                   	\
		if (BbpCsr.field.Busy == BUSY)                      	\
		{                                                   	\
			DBGPRINT_ERR(("BBP read R%d=0x%x fail\n", _I, BbpCsr.word));	\
			*(_pV) = (_A)->BbpWriteLatch[_I];               \
			RTMP_IO_READ32(_A, H2M_BBP_AGENT, &BbpCsr.word);\
			BbpCsr.field.Busy = 0;                          \
			RTMP_IO_WRITE32(_A, H2M_BBP_AGENT, BbpCsr.word);\
		}                                                   \
	}                   \
}


/*
 * proc fs related macros.
 */
#define CREATE_PROC_ENTRY(x)	\
	if ((pProc##x = create_proc_entry(#x, 0, pProcDir))){		\
		pProc##x->read_proc = (read_proc_t*)&x##Read;		\
		pProc##x->write_proc = (write_proc_t*)&x##Write;	\
	}								\

#define IMPLEMENT_PROC_ENTRY(x,y,z)			\
static struct proc_dir_entry *pProc##x;			\
IMPLEMENT_PROC_ENTRY_READ(x,y,z)			\
IMPLEMENT_PROC_ENTRY_WRITE(x,y,z)

#define IMPLEMENT_PROC_ENTRY_READ(x,y,z)		\
static INT x##Read(char *page, char **start, off_t off,	\
		   int count, int *eof, void *data){	\
	INT	len;					\
	sprintf(page, "%d\n", atomic_read(&x));		\
	len = strlen(page) + 1;				\
	*eof = 1;					\
	return len;					\
}
#define IMPLEMENT_PROC_ENTRY_WRITE(x,y,z)		\
static INT x##Write(struct file *file, const char *buffer, \
			 unsigned long count, void *data){ \
	CHAR tmp[32];INT tmp_val;			\
	if (count > 32)	count = 32;			\
	memset(tmp, 0, 32);				\
	if (copy_from_user(tmp, buffer, count))		\
		return -EFAULT;				\
	tmp_val = simple_strtol(tmp, 0, 10);		\
	if(tmp_val > z || tmp_val < y)			\
		return -EFAULT;				\
	atomic_set(&x,  (int)tmp_val);			\
	return count;					\
}
#define DESTORY_PROC_ENTRY(x) if (pProc##x)	remove_proc_entry(#x, pProcDir);

/*
 * function prototype
 */
VOID RT3XXX_AntDiversity_Init(
    IN PRTMP_ADAPTER pAd
);

VOID RT3XXX_AntDiversity_Fini(
    IN PRTMP_ADAPTER pAd
);

VOID AntDiversity_Update_Rssi_Sample(
	IN PRTMP_ADAPTER        pAd,
	IN RSSI_SAMPLE          *pRssi,
	IN PRXWI_STRUC          pRxWI
);
                        

#endif
