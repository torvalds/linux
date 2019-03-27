/*
 * \file       trc_printable_elem.cpp
 * \brief      OpenCSD : 
 * 
 * \copyright  Copyright (c) 2015, ARM Limited. All Rights Reserved.
 */

/* 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors 
 * may be used to endorse or promote products derived from this software without 
 * specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */ 

#include "common/trc_printable_elem.h"
#include <cassert>
#include <cstring>
#if defined(_MSC_VER) && (_MSC_VER < 1900)
 /** VS2010 does not support inttypes - remove when VS2010 support is dropped */
#define __PRI64_PREFIX "ll"
#define PRIX64 __PRI64_PREFIX "X"
#define PRIu64 __PRI64_PREFIX "u"
#define PRIu32 "u"
#else
#include <cinttypes>
#endif

void trcPrintableElem::getValStr(std::string &valStr, const int valTotalBitSize, const int valValidBits, const uint64_t value, const bool asHex /* = true*/, const int updateBits /* = 0*/)
{
    static char szStrBuffer[128];
    static char szFormatBuffer[32];

    assert((valTotalBitSize >= 4) && (valTotalBitSize <= 64));

    uint64_t LimitMask = ~0ULL;
    LimitMask >>= 64-valTotalBitSize;
    valStr = "0x";

    if(asHex)
    {
        int numHexChars = valTotalBitSize / 4;
        numHexChars += ((valTotalBitSize % 4) > 0) ? 1 : 0;

        int validChars = valValidBits / 4;
        if((valValidBits % 4) > 0) validChars++; 
        int QM = numHexChars - validChars;
        while(QM)
        {
            QM--;
            valStr += "?";
        }
        if(valValidBits > 32)
        {
            sprintf(szFormatBuffer,"%%0%dllX",validChars);  // create the format
            sprintf(szStrBuffer,szFormatBuffer,value); // fill the buffer
        }
        else
        {
            sprintf(szFormatBuffer,"%%0%dlX",validChars);  // create the format
            sprintf(szStrBuffer,szFormatBuffer,(uint32_t)value); // fill the buffer
        }
        valStr+=szStrBuffer;
        if(valValidBits < valTotalBitSize)
        {
            sprintf(szStrBuffer," (%d:0)", valValidBits-1);
            valStr+=szStrBuffer;
        }
        
        if(updateBits)
        {
            uint64_t updateMask = ~0ULL;
            updateMask >>= 64-updateBits;
            sprintf(szStrBuffer," ~[0x%" PRIX64 "]",value & updateMask);
            valStr+=szStrBuffer;
        }
    }
    else
    {
        valStr = "";
        if(valValidBits < valTotalBitSize)
            valStr += "??";
        if(valValidBits > 32)
        {
            sprintf(szStrBuffer,"%" PRIu64 ,value);
        }
        else
        {
            sprintf(szStrBuffer,"%" PRIu32 ,(uint32_t)value);
        }
        valStr +=  szStrBuffer;
        if(valValidBits < valTotalBitSize)
        {
            sprintf(szStrBuffer," (%d:0)", valValidBits-1);
            valStr+=szStrBuffer;
        }
    }
}


/* End of File trc_printable_elem.cpp */
