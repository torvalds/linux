/*
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the
 * GNU General Public License for more details.
*/

//#include "si_c99support.h"
//#include "si_memsegsupport.h"
#include "si_common.h"
#include "si_osdebug.h"
#include <linux/slab.h>
#include <linux/string.h>

#define MAX_DEBUG_MSG_SIZE	512

void SiiOsDebugPrint(const char *pszFileName, uint32_t iLineNum, uint8_t printFlags, const char *pszFormat, ...)
{
	uint8_t		*pBuf = NULL;
	uint8_t		*pBufOffset;
	int			remainingBufLen = MAX_DEBUG_MSG_SIZE;
	int			len;
	va_list		ap;
    extern int debug_level;

    if (( printFlags & 0xFF ) > debug_level )
    {
        return;
    }
    //if (SiiOsDebugChannelIsEnabled( channel ))
    {
	    pBuf = kmalloc(remainingBufLen, GFP_KERNEL);
	    if(pBuf == NULL)
	    	return;
	    pBufOffset = pBuf;

        if(pszFileName != NULL)
        {
        	// only print the file name, not the full path.
        	const char *pc;

            for(pc = &pszFileName[strlen(pszFileName)];pc  >= pszFileName;--pc)
            {
                if ('\\' == *pc)
                {
                    ++pc;
                    break;
                }
                if ('/' ==*pc)
                {
                    ++pc;
                    break;
                }
            }
            len = scnprintf(pBufOffset, remainingBufLen, "%s:%d ",pc,(int)iLineNum);
            if(len < 0) {
            	kfree(pBuf);
            	return;
            }

            remainingBufLen -= len;
            pBufOffset += len;
        }

        va_start(ap,pszFormat);
        vsnprintf(pBufOffset, remainingBufLen, pszFormat, ap);
        va_end(ap);

    	printk(pBuf);
		kfree(pBuf);
    }
}

