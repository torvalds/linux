/*
 * \file       item_printer.h
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

#ifndef ARM_ITEM_PRINTER_H_INCLUDED
#define ARM_ITEM_PRINTER_H_INCLUDED

#include "opencsd.h"
#include <string>

class ItemPrinter 
{
public:
    ItemPrinter();
    virtual ~ItemPrinter();

    void setMessageLogger(ocsdMsgLogger *pMsgLogger) { m_pMsgLogger = pMsgLogger; };
    void itemPrintLine(const std::string &msg);

    // return wait for the first N packets - test the wait mechanism.
    void setTestWaits(const int num_waits);
    const int getTestWaits() const;
    void decTestWaits();

protected:
    ocsdMsgLogger *m_pMsgLogger;    
    int m_test_waits;
};

inline ItemPrinter::ItemPrinter() :
   m_pMsgLogger(0),
   m_test_waits(0)
{
}

inline ItemPrinter::~ItemPrinter()
{
    m_pMsgLogger = 0;
}

inline void ItemPrinter::itemPrintLine(const std::string &msg)
{
    if(m_pMsgLogger)
        m_pMsgLogger->LogMsg(msg);
}

inline void ItemPrinter::setTestWaits(const int num_waits)
{
    m_test_waits = num_waits;
}

inline const int ItemPrinter::getTestWaits() const
{
    return m_test_waits;
}

inline void ItemPrinter::decTestWaits()
{
    m_test_waits--;
}

#endif // ARM_ITEM_PRINTER_H_INCLUDED

/* End of File item_printer.h */
